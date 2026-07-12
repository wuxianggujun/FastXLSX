#include "test_workbook_editor_public_state_formula_audits_support.hpp"

namespace {

void test_public_worksheet_editor_full_calculation_renamed_source_formula_audits_preserve_source_scan()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-source-formula-audit-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-source-formula-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-source-formula-audit-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);
    const std::size_t shifted_memory = sheet.estimated_memory_usage();

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    const std::optional<fastxlsx::CellValue> materialized_formula =
        sheet.try_cell("D3");
    check(materialized_formula.has_value() &&
            materialized_formula->kind() == fastxlsx::CellValueKind::Formula &&
            materialized_formula->text_value() == shifted_formula &&
            materialized_formula->has_style() &&
            materialized_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc source formula audit setup should expose the translated styled formula");
    check(editor.has_worksheet("RenamedData") &&
            !editor.has_worksheet("Data") &&
            editor.pending_change_count() == 2 &&
            editor.pending_materialized_worksheet_names()
                == std::vector<std::string>{"RenamedData"} &&
            editor.pending_materialized_cell_count() == 7 &&
            editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "renamed full-calc source formula audit setup should keep rename, metadata, and materialized diagnostics pending");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "renamed full-calc source formula audit setup should expose one dirty materialized summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" &&
                    summaries[0].planned_name == "RenamedData" &&
                    summaries[0].renamed &&
                    summaries[0].materialized_dirty &&
                    summaries[0].materialized_cell_count == 7 &&
                    summaries[0].estimated_materialized_memory_usage == shifted_memory,
                "renamed full-calc source formula audit setup should report the dirty renamed summary");
        }
    }

    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc source formula audit");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "renamed full-calc source formula audit save_as should clean the materialized sheet");
    check(editor.pending_change_count() == 3,
        "renamed full-calc source formula audit save_as should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc source formula audit save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc source formula audit save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(workbook_xml, R"(name="RenamedData")",
        "renamed full-calc source formula audit save_as should persist the planned catalog name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc source formula audit save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "renamed full-calc source formula audit save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed full-calc source formula audit save_as should write shifted qualified formula");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "renamed full-calc source formula audit save_as should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc source formula audit");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc source formula audit no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc source formula audit no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc source formula audit no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc source formula audit no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc source formula audit no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc source formula audit no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc source formula audit no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "renamed full-calc source formula audit no-op output should match the first materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc source formula audit no-op save should leave the source package unchanged");
    check_public_state_reopened_shift_formula_audit_output(
        noop_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc source formula audit no-op save");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_preserve_materialized_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    const std::optional<fastxlsx::CellValue> materialized_formula =
        sheet.try_cell("D3");
    check(materialized_formula.has_value() &&
            materialized_formula->kind() == fastxlsx::CellValueKind::Formula &&
            materialized_formula->text_value() == shifted_formula &&
            materialized_formula->has_style() &&
            materialized_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit setup should expose the translated styled formula");
    check(editor.pending_change_count() == 2 &&
            editor.pending_materialized_worksheet_names()
                == std::vector<std::string>{"RenamedData"} &&
            editor.pending_materialized_cell_count() == 7 &&
            editor.estimated_pending_materialized_memory_usage() > 0,
        "renamed full-calc formula audit setup should keep rename, metadata, and materialized diagnostics pending");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit");
    check(audits.size() == 2,
        "renamed full-calc formula audit should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit shifted B reference");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit save_as should clean the materialized sheet");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit save_as should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit save_as should persist the planned catalog name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "renamed full-calc formula audit save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed full-calc formula audit save_as should write shifted qualified formula");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit save_as should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "renamed full-calc formula audit no-op output should match the first materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit no-op save should leave the source package unchanged");
    check_public_state_reopened_shift_formula_audit_output(
        noop_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit no-op output");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_failed_save_preserve_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-failed-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-failed-save-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-failed-save-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-failed-save-second-noop-output.xlsx");
    const auto source_entries_before_save = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    const std::size_t shifted_memory = sheet.estimated_memory_usage();
    const auto check_dirty_state = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep last_edit_error clear");
        check(editor.has_pending_changes() &&
                editor.pending_change_count() == 2 &&
                sheet.has_pending_changes(),
            label + " should keep rename, metadata, and materialized edits pending");
        check(editor.has_worksheet("RenamedData") &&
                !editor.has_worksheet("Data"),
            label + " should expose only the planned sheet name");
        check_workbook_editor_no_replacement_diagnostics(
            editor, label + " should not invent replacement diagnostics");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"RenamedData"},
            label + " should report dirty materialized state under the planned name");
        check(editor.pending_materialized_cell_count() == 7 &&
                editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            label + " should preserve dirty materialized count and memory");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1,
                label + " should expose one dirty materialized summary");
            if (summaries.size() == 1) {
                check(summaries[0].source_name == "Data" &&
                        summaries[0].planned_name == "RenamedData" &&
                        summaries[0].renamed &&
                        summaries[0].materialized_dirty &&
                        summaries[0].materialized_cell_count == 7 &&
                        summaries[0].estimated_materialized_memory_usage == shifted_memory,
                    label + " should preserve the renamed dirty summary");
            }
        }

        const std::optional<fastxlsx::CellValue> materialized_formula =
            sheet.try_cell("D3");
        check(materialized_formula.has_value() &&
                materialized_formula->kind() == fastxlsx::CellValueKind::Formula &&
                materialized_formula->text_value() == shifted_formula &&
                materialized_formula->has_style() &&
                materialized_formula->style_id().value() == styled_formula_style.value(),
            label + " should preserve the shifted styled formula");
        check(!sheet.try_cell("D2").has_value(),
            label + " should keep the old formula coordinate absent");
    };

    check_dirty_state(
        "renamed full-calc formula audit failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "renamed full-calc formula audit failed save should reject exact source overwrite");
    check_dirty_state(
        "renamed full-calc formula audit failed save rejected source overwrite");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit failed save materialized audit");
    check(materialized_audits.size() == 2,
        "renamed full-calc formula audit failed save should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit failed save shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit failed save shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit failed save source audit");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_save,
        "renamed full-calc formula audit failed save should leave source package bytes unchanged");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit failed save retry should clean the materialized sheet");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit failed save retry should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit failed save retry should clear dirty materialized diagnostics");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_save,
        "renamed full-calc formula audit failed save retry should keep source package bytes unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit failed save retry should persist the planned catalog name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit failed save retry should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "renamed full-calc formula audit failed save retry should not invent calcChain.xml");
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed full-calc formula audit failed save retry should write shifted qualified formula");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit failed save retry should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit failed save retry");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit failed save retry no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit failed save retry no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit failed save retry no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit failed save retry no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit failed save retry no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit failed save retry no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit failed save retry no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_save,
        "renamed full-calc formula audit failed save retry no-op save should keep source package bytes unchanged");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "renamed full-calc formula audit failed save retry no-op output should match the first materialized output");
    check_public_state_reopened_shift_formula_audit_output(
        noop_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit failed save retry no-op save");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit failed save retry second no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit failed save retry second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit failed save retry second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit failed save retry second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit failed save retry second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_second_noop,
        "renamed full-calc formula audit failed save retry second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_second_noop,
        "renamed full-calc formula audit failed save retry second no-op save");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_save,
        "renamed full-calc formula audit failed save retry second no-op save should keep source package bytes unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "renamed full-calc formula audit failed save retry second no-op save should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "renamed full-calc formula audit failed save retry second no-op save should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "renamed full-calc formula audit failed save retry second no-op output should match the first no-op output");
    check_public_state_reopened_shift_formula_audit_output(
        second_noop_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit failed save retry second no-op save");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_option_mismatch_preserve_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-options-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-options-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-options-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    const std::size_t shifted_memory = sheet.estimated_memory_usage();
    const auto check_dirty_state = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep last_edit_error clear");
        check(editor.has_pending_changes() &&
                editor.pending_change_count() == 2 &&
                sheet.has_pending_changes(),
            label + " should keep rename, metadata, and materialized edits pending");
        check(editor.pending_replacement_worksheet_names().empty() &&
                editor.pending_replacement_cell_count() == 0 &&
                editor.estimated_pending_replacement_memory_usage() == 0,
            label + " should not invent replacement diagnostics");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"RenamedData"},
            label + " should report dirty materialized state under the planned name");
        check(editor.pending_materialized_cell_count() == 7 &&
                editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            label + " should preserve dirty materialized count and memory");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1,
                label + " should expose one dirty materialized summary");
            if (summaries.size() == 1) {
                check(summaries[0].source_name == "Data" &&
                        summaries[0].planned_name == "RenamedData" &&
                        summaries[0].renamed &&
                        summaries[0].materialized_dirty &&
                        summaries[0].materialized_cell_count == 7 &&
                        summaries[0].estimated_materialized_memory_usage == shifted_memory,
                    label + " should preserve the renamed dirty summary");
            }
        }

        const std::optional<fastxlsx::CellValue> materialized_formula =
            sheet.try_cell("D3");
        check(materialized_formula.has_value() &&
                materialized_formula->kind() == fastxlsx::CellValueKind::Formula &&
                materialized_formula->text_value() == shifted_formula &&
                materialized_formula->has_style() &&
                materialized_formula->style_id().value() == styled_formula_style.value(),
            label + " should preserve the shifted styled formula");
        check(!sheet.try_cell("D2").has_value(),
            label + " should keep the old formula coordinate absent");
    };

    check_dirty_state(
        "renamed full-calc formula audit option mismatch dirty state before failures");
    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 2;
    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("RenamedData", mismatched_options);
    }), "renamed full-calc formula audit option mismatch try_worksheet should reject different options");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("RenamedData", mismatched_options);
    }), "renamed full-calc formula audit option mismatch worksheet should reject different options");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit option mismatch should keep the old source name unavailable");
    check_dirty_state(
        "renamed full-calc formula audit option mismatch after rejected access");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit option mismatch materialized audit");
    check(materialized_audits.size() == 2,
        "renamed full-calc formula audit option mismatch should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit option mismatch shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit option mismatch shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit option mismatch source audit");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit option mismatch save_as should clean the materialized sheet");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit option mismatch save_as should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit option mismatch save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit option mismatch save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit option mismatch save_as should persist the planned catalog name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit option mismatch save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "renamed full-calc formula audit option mismatch save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed full-calc formula audit option mismatch save_as should write shifted qualified formula");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit option mismatch save_as should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit option mismatch");
    check_public_state_renamed_shift_formula_audit_noop_save(
        editor, sheet, noop_output, output_entries, shifted_formula,
        styled_formula_style, "renamed full-calc formula audit option mismatch");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit option mismatch no-op save should leave the source package unchanged");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_missing_query_preserve_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-missing-query-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-missing-query-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-missing-query-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    const std::size_t shifted_memory = sheet.estimated_memory_usage();
    const auto check_dirty_state = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep last_edit_error clear");
        check(editor.has_pending_changes() &&
                editor.pending_change_count() == 2 &&
                sheet.has_pending_changes(),
            label + " should keep rename, metadata, and materialized edits pending");
        check(editor.pending_replacement_worksheet_names().empty() &&
                editor.pending_replacement_cell_count() == 0 &&
                editor.estimated_pending_replacement_memory_usage() == 0,
            label + " should not invent replacement diagnostics");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"RenamedData"},
            label + " should report dirty materialized state under the planned name");
        check(editor.pending_materialized_cell_count() == 7 &&
                editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            label + " should preserve dirty materialized count and memory");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1,
                label + " should expose one dirty materialized summary");
            if (summaries.size() == 1) {
                check(summaries[0].source_name == "Data" &&
                        summaries[0].planned_name == "RenamedData" &&
                        summaries[0].renamed &&
                        summaries[0].materialized_dirty &&
                        summaries[0].materialized_cell_count == 7 &&
                        summaries[0].estimated_materialized_memory_usage == shifted_memory,
                    label + " should preserve the renamed dirty summary");
            }
        }

        const std::optional<fastxlsx::CellValue> materialized_formula =
            sheet.try_cell("D3");
        check(materialized_formula.has_value() &&
                materialized_formula->kind() == fastxlsx::CellValueKind::Formula &&
                materialized_formula->text_value() == shifted_formula &&
                materialized_formula->has_style() &&
                materialized_formula->style_id().value() == styled_formula_style.value(),
            label + " should preserve the shifted styled formula");
        check(!sheet.try_cell("D2").has_value(),
            label + " should keep the old formula coordinate absent");
    };

    check_dirty_state(
        "renamed full-calc formula audit missing query dirty state before failures");
    check(!editor.try_worksheet("Missing").has_value(),
        "renamed full-calc formula audit missing query try_worksheet should return empty for missing sheet");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit missing query should keep the old source name unavailable");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Missing");
    }), "renamed full-calc formula audit missing query worksheet should reject missing sheet");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Data");
    }), "renamed full-calc formula audit missing query worksheet should reject old source name");
    check_dirty_state(
        "renamed full-calc formula audit missing query after rejected access");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit missing query materialized audit");
    check(materialized_audits.size() == 2,
        "renamed full-calc formula audit missing query should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit missing query shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit missing query shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit missing query source audit");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit missing query save_as should clean the materialized sheet");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit missing query save_as should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit missing query save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit missing query save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit missing query save_as should persist the planned catalog name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit missing query save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "renamed full-calc formula audit missing query save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed full-calc formula audit missing query save_as should write shifted qualified formula");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit missing query save_as should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit missing query");
    check_public_state_renamed_shift_formula_audit_noop_save(
        editor, sheet, noop_output, output_entries, shifted_formula,
        styled_formula_style, "renamed full-calc formula audit missing query");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit missing query no-op save should leave the source package unchanged");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_invalid_reads_preserve_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-invalid-reads-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-invalid-reads-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-invalid-reads-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    const std::size_t shifted_memory = sheet.estimated_memory_usage();
    const auto check_dirty_state = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep last_edit_error clear");
        check(editor.has_pending_changes() &&
                editor.pending_change_count() == 2 &&
                sheet.has_pending_changes(),
            label + " should keep rename, metadata, and materialized edits pending");
        check(editor.pending_replacement_worksheet_names().empty() &&
                editor.pending_replacement_cell_count() == 0 &&
                editor.estimated_pending_replacement_memory_usage() == 0,
            label + " should not invent replacement diagnostics");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"RenamedData"},
            label + " should report dirty materialized state under the planned name");
        check(editor.pending_materialized_cell_count() == 7 &&
                editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            label + " should preserve dirty materialized count and memory");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1,
                label + " should expose one dirty materialized summary");
            if (summaries.size() == 1) {
                check(summaries[0].source_name == "Data" &&
                        summaries[0].planned_name == "RenamedData" &&
                        summaries[0].renamed &&
                        summaries[0].materialized_dirty &&
                        summaries[0].materialized_cell_count == 7 &&
                        summaries[0].estimated_materialized_memory_usage == shifted_memory,
                    label + " should preserve the renamed dirty summary");
            }
        }

        const std::optional<fastxlsx::CellValue> materialized_formula =
            sheet.try_cell("D3");
        check(materialized_formula.has_value() &&
                materialized_formula->kind() == fastxlsx::CellValueKind::Formula &&
                materialized_formula->text_value() == shifted_formula &&
                materialized_formula->has_style() &&
                materialized_formula->style_id().value() == styled_formula_style.value(),
            label + " should preserve the shifted styled formula");
        check(!sheet.try_cell("D2").has_value(),
            label + " should keep the old formula coordinate absent");
    };

    check_dirty_state(
        "renamed full-calc formula audit invalid reads dirty state before failures");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "renamed full-calc formula audit invalid reads should reject row-zero try_cell");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell(1, 0); }),
        "renamed full-calc formula audit invalid reads should reject column-zero get_cell");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell("a1"); }),
        "renamed full-calc formula audit invalid reads should reject lowercase A1 reads");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell("XFE1"); }),
        "renamed full-calc formula audit invalid reads should reject A1 column overflow");
    check(threw_fastxlsx_error([&] {
        (void)sheet.sparse_cells(fastxlsx::CellRange {3, 3, 2, 2});
    }), "renamed full-calc formula audit invalid reads should reject reversed CellRange reads");
    check(threw_fastxlsx_error([&] { (void)sheet.sparse_cells("B2:A1"); }),
        "renamed full-calc formula audit invalid reads should reject reversed A1 range reads");
    check(threw_fastxlsx_error([&] {
        const std::array<fastxlsx::WorksheetCellReference, 2> invalid_batch {
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {1048577, 1},
        };
        (void)sheet.sparse_cells(invalid_batch);
    }), "renamed full-calc formula audit invalid reads should reject invalid coordinate batch reads");
    check(threw_fastxlsx_error([&] { (void)sheet.row_cells(0); }),
        "renamed full-calc formula audit invalid reads should reject row_cells row zero");
    check(threw_fastxlsx_error([&] { (void)sheet.column_cells(16385); }),
        "renamed full-calc formula audit invalid reads should reject column_cells column overflow");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell("C4"); }),
        "renamed full-calc formula audit invalid reads should reject valid but missing get_cell reads");
    check_dirty_state(
        "renamed full-calc formula audit invalid reads after rejected reads");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit invalid reads materialized audit");
    check(materialized_audits.size() == 2,
        "renamed full-calc formula audit invalid reads should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit invalid reads shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit invalid reads shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit invalid reads source audit");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit invalid reads save_as should clean the materialized sheet");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit invalid reads save_as should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit invalid reads save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit invalid reads save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit invalid reads save_as should persist the planned catalog name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit invalid reads save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "renamed full-calc formula audit invalid reads save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed full-calc formula audit invalid reads save_as should write shifted qualified formula");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit invalid reads save_as should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit invalid reads");
    check_public_state_renamed_shift_formula_audit_noop_save(
        editor, sheet, noop_output, output_entries, shifted_formula,
        styled_formula_style, "renamed full-calc formula audit invalid reads");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit invalid reads no-op save should leave the source package unchanged");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_invalid_mutations_preserve_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-invalid-mutations-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-invalid-mutations-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-invalid-mutations-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    const std::size_t shifted_memory = sheet.estimated_memory_usage();
    const auto check_dirty_state = [&](std::string_view scenario,
                                       bool expect_invalid_mutation_error) {
        const std::string label = std::string(scenario);

        if (expect_invalid_mutation_error) {
            check(editor.last_edit_error().has_value(),
                label + " should preserve the invalid mutation diagnostic");
            if (editor.last_edit_error().has_value()) {
                check_contains(*editor.last_edit_error(),
                    "WorksheetEditor cell reference is invalid",
                    label + " should expose the invalid reference diagnostic");
            }
        } else {
            check(!editor.last_edit_error().has_value(),
                label + " should keep last_edit_error clear");
        }
        check(editor.has_pending_changes() &&
                editor.pending_change_count() == 2 &&
                sheet.has_pending_changes(),
            label + " should keep rename, metadata, and materialized edits pending");
        check(editor.pending_replacement_worksheet_names().empty() &&
                editor.pending_replacement_cell_count() == 0 &&
                editor.estimated_pending_replacement_memory_usage() == 0,
            label + " should not invent replacement diagnostics");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"RenamedData"},
            label + " should report dirty materialized state under the planned name");
        check(editor.pending_materialized_cell_count() == 7 &&
                editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            label + " should preserve dirty materialized count and memory");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1,
                label + " should expose one dirty materialized summary");
            if (summaries.size() == 1) {
                check(summaries[0].source_name == "Data" &&
                        summaries[0].planned_name == "RenamedData" &&
                        summaries[0].renamed &&
                        summaries[0].materialized_dirty &&
                        summaries[0].materialized_cell_count == 7 &&
                        summaries[0].estimated_materialized_memory_usage == shifted_memory,
                    label + " should preserve the renamed dirty summary");
            }
        }

        const std::optional<fastxlsx::CellValue> materialized_formula =
            sheet.try_cell("D3");
        check(materialized_formula.has_value() &&
                materialized_formula->kind() == fastxlsx::CellValueKind::Formula &&
                materialized_formula->text_value() == shifted_formula &&
                materialized_formula->has_style() &&
                materialized_formula->style_id().value() == styled_formula_style.value(),
            label + " should preserve the shifted styled formula");
        check(!sheet.try_cell("D2").has_value(),
            label + " should keep the old formula coordinate absent");
    };

    check_dirty_state(
        "renamed full-calc formula audit invalid mutations dirty state before failures",
        false);
    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1,
            fastxlsx::CellValue::formula("invalid-renamed-full-calc-formula-row-zero"));
    }), "renamed full-calc formula audit invalid mutations should reject row-zero set_cell");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell("XFE1",
            fastxlsx::CellValue::formula("invalid-renamed-full-calc-formula-a1-overflow"));
    }), "renamed full-calc formula audit invalid mutations should reject A1 column overflow set_cell");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1",
            fastxlsx::CellValue::formula("invalid-renamed-full-calc-formula-lowercase"));
    }), "renamed full-calc formula audit invalid mutations should reject lowercase A1 set_cell");
    check(threw_fastxlsx_error([&] { sheet.erase_cell(0, 1); }),
        "renamed full-calc formula audit invalid mutations should reject row-zero erase_cell");
    check(threw_fastxlsx_error([&] {
        sheet.erase_cells(fastxlsx::CellRange {3, 3, 2, 2});
    }), "renamed full-calc formula audit invalid mutations should reject reversed erase_cells range");
    check(threw_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "renamed full-calc formula audit invalid mutations should reject range erase_cell references");
    check_dirty_state(
        "renamed full-calc formula audit invalid mutations after rejected mutations",
        true);

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit invalid mutations materialized audit");
    check(materialized_audits.size() == 2,
        "renamed full-calc formula audit invalid mutations should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit invalid mutations shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit invalid mutations shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit invalid mutations source audit");
    check(editor.last_edit_error().has_value(),
        "renamed full-calc formula audit invalid mutations audits should preserve last_edit_error");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit invalid mutations save_as should clean the materialized sheet");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit invalid mutations save_as should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit invalid mutations save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit invalid mutations save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit invalid mutations save_as should persist the planned catalog name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit invalid mutations save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "renamed full-calc formula audit invalid mutations save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed full-calc formula audit invalid mutations save_as should write shifted qualified formula");
    check_not_contains(worksheet_xml, "invalid-renamed-full-calc-formula",
        "renamed full-calc formula audit invalid mutations save_as should omit rejected payloads");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit invalid mutations save_as should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit invalid mutations");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    check(save_state_before_noop.last_edit_error.has_value(),
        "renamed full-calc formula audit invalid mutations save_as should preserve the diagnostic before no-op");
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit invalid mutations no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit invalid mutations no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit invalid mutations no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit invalid mutations no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit invalid mutations no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit invalid mutations no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "renamed full-calc formula audit invalid mutations no-op output should match the first materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit invalid mutations no-op save should leave the source package unchanged");
    check_public_state_reopened_shift_formula_audit_output(
        noop_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit invalid mutations no-op save");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_invalid_shifts_preserve_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-invalid-shifts-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-invalid-shifts-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-invalid-shifts-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    const std::size_t shifted_memory = sheet.estimated_memory_usage();
    const auto check_dirty_state = [&](std::string_view scenario,
                                       bool expect_invalid_shift_error) {
        const std::string label = std::string(scenario);

        if (expect_invalid_shift_error) {
            check(editor.last_edit_error().has_value(),
                label + " should preserve the invalid shift diagnostic");
        } else {
            check(!editor.last_edit_error().has_value(),
                label + " should keep last_edit_error clear");
        }
        check(editor.has_pending_changes() &&
                editor.pending_change_count() == 2 &&
                sheet.has_pending_changes(),
            label + " should keep rename, metadata, and materialized edits pending");
        check(editor.pending_replacement_worksheet_names().empty() &&
                editor.pending_replacement_cell_count() == 0 &&
                editor.estimated_pending_replacement_memory_usage() == 0,
            label + " should not invent replacement diagnostics");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"RenamedData"},
            label + " should report dirty materialized state under the planned name");
        check(editor.pending_materialized_cell_count() == 7 &&
                editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            label + " should preserve dirty materialized count and memory");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1,
                label + " should expose one dirty materialized summary");
            if (summaries.size() == 1) {
                check(summaries[0].source_name == "Data" &&
                        summaries[0].planned_name == "RenamedData" &&
                        summaries[0].renamed &&
                        summaries[0].materialized_dirty &&
                        summaries[0].materialized_cell_count == 7 &&
                        summaries[0].estimated_materialized_memory_usage == shifted_memory,
                    label + " should preserve the renamed dirty summary");
            }
        }

        const std::optional<fastxlsx::CellValue> materialized_formula =
            sheet.try_cell("D3");
        check(materialized_formula.has_value() &&
                materialized_formula->kind() == fastxlsx::CellValueKind::Formula &&
                materialized_formula->text_value() == shifted_formula &&
                materialized_formula->has_style() &&
                materialized_formula->style_id().value() == styled_formula_style.value(),
            label + " should preserve the shifted styled formula");
        check(!sheet.try_cell("D2").has_value(),
            label + " should keep the old formula coordinate absent");
    };

    check_dirty_state(
        "renamed full-calc formula audit invalid shifts dirty state before failures",
        false);
    check(threw_fastxlsx_error([&] { sheet.insert_rows(0, 1); }),
        "renamed full-calc formula audit invalid shifts should reject row-zero insert_rows");
    check(threw_fastxlsx_error([&] { sheet.delete_rows(1048576, 2); }),
        "renamed full-calc formula audit invalid shifts should reject overflowing delete_rows");
    check(threw_fastxlsx_error([&] { sheet.insert_columns(0, 1); }),
        "renamed full-calc formula audit invalid shifts should reject column-zero insert_columns");
    check(threw_fastxlsx_error([&] { sheet.delete_columns(16384, 2); }),
        "renamed full-calc formula audit invalid shifts should reject overflowing delete_columns");
    check_dirty_state(
        "renamed full-calc formula audit invalid shifts after rejected shifts",
        true);

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit invalid shifts materialized audit");
    check(materialized_audits.size() == 2,
        "renamed full-calc formula audit invalid shifts should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit invalid shifts shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit invalid shifts shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit invalid shifts source audit");
    check(editor.last_edit_error().has_value(),
        "renamed full-calc formula audit invalid shifts audits should preserve last_edit_error");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit invalid shifts save_as should clean the materialized sheet");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit invalid shifts save_as should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit invalid shifts save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit invalid shifts save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit invalid shifts save_as should persist the planned catalog name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit invalid shifts save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "renamed full-calc formula audit invalid shifts save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed full-calc formula audit invalid shifts save_as should write shifted qualified formula");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit invalid shifts save_as should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit invalid shifts");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    check(save_state_before_noop.last_edit_error.has_value(),
        "renamed full-calc formula audit invalid shifts save_as should preserve the diagnostic before no-op");
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit invalid shifts no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit invalid shifts no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit invalid shifts no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit invalid shifts no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit invalid shifts no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit invalid shifts no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "renamed full-calc formula audit invalid shifts no-op output should match the first materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit invalid shifts no-op save should leave the source package unchanged");
    check_public_state_reopened_shift_formula_audit_output(
        noop_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit invalid shifts no-op save");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_invalid_diagnostic_recovery_preserve_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-invalid-diagnostic-recovery-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-invalid-diagnostic-recovery-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-invalid-diagnostic-recovery-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    const auto check_dirty_state = [&](std::string_view scenario,
                                       std::size_t expected_cell_count,
                                       std::size_t expected_memory,
                                       bool expect_invalid_diagnostic) {
        const std::string label = std::string(scenario);

        if (expect_invalid_diagnostic) {
            check(editor.last_edit_error().has_value(),
                label + " should preserve the invalid diagnostic");
        } else {
            check(!editor.last_edit_error().has_value(),
                label + " should keep last_edit_error clear");
        }
        check(editor.has_pending_changes() &&
                editor.pending_change_count() == 2 &&
                sheet.has_pending_changes(),
            label + " should keep rename, metadata, and materialized edits pending");
        check(editor.pending_replacement_worksheet_names().empty() &&
                editor.pending_replacement_cell_count() == 0 &&
                editor.estimated_pending_replacement_memory_usage() == 0,
            label + " should not invent replacement diagnostics");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"RenamedData"},
            label + " should report dirty materialized state under the planned name");
        check(editor.pending_materialized_cell_count() == expected_cell_count &&
                editor.estimated_pending_materialized_memory_usage() == expected_memory,
            label + " should preserve dirty materialized count and memory");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1,
                label + " should expose one dirty materialized summary");
            if (summaries.size() == 1) {
                check(summaries[0].source_name == "Data" &&
                        summaries[0].planned_name == "RenamedData" &&
                        summaries[0].renamed &&
                        summaries[0].materialized_dirty &&
                        summaries[0].materialized_cell_count == expected_cell_count &&
                        summaries[0].estimated_materialized_memory_usage == expected_memory,
                    label + " should preserve the renamed dirty summary");
            }
        }

        const std::optional<fastxlsx::CellValue> materialized_formula =
            sheet.try_cell("D3");
        check(materialized_formula.has_value() &&
                materialized_formula->kind() == fastxlsx::CellValueKind::Formula &&
                materialized_formula->text_value() == shifted_formula &&
                materialized_formula->has_style() &&
                materialized_formula->style_id().value() == styled_formula_style.value(),
            label + " should preserve the shifted styled formula");
        check(!sheet.try_cell("D2").has_value(),
            label + " should keep the old formula coordinate absent");
    };

    const std::size_t shifted_memory = sheet.estimated_memory_usage();
    check_dirty_state(
        "renamed full-calc formula audit invalid diagnostic recovery dirty state before failure",
        7, shifted_memory, false);
    check(threw_fastxlsx_error([&] {
        sheet.set_cell("XFE1",
            fastxlsx::CellValue::formula("invalid-renamed-full-calc-formula-recovery"));
    }), "renamed full-calc formula audit invalid diagnostic recovery should reject invalid formula payload");
    check_dirty_state(
        "renamed full-calc formula audit invalid diagnostic recovery after rejected mutation",
        7, shifted_memory, true);

    sheet.set_cell(5, 3, fastxlsx::CellValue::text("recovered-c5"));
    const std::size_t recovered_memory = sheet.estimated_memory_usage();
    check_dirty_state(
        "renamed full-calc formula audit invalid diagnostic recovery after valid mutation",
        8, recovered_memory, false);
    const std::optional<fastxlsx::CellValue> recovered_cell = sheet.try_cell("C5");
    check(recovered_cell.has_value() &&
            recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            recovered_cell->text_value() == "recovered-c5",
        "renamed full-calc formula audit invalid diagnostic recovery should keep the recovered cell");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> materialized_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit invalid diagnostic recovery materialized audit");
    check(materialized_audits.size() == 2,
        "renamed full-calc formula audit invalid diagnostic recovery should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit invalid diagnostic recovery shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        materialized_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit invalid diagnostic recovery shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit invalid diagnostic recovery source audit");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit invalid diagnostic recovery save_as should clean the materialized sheet");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit invalid diagnostic recovery save_as should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit invalid diagnostic recovery save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit invalid diagnostic recovery save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit invalid diagnostic recovery save_as should persist the planned catalog name");
    check_contains(workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit invalid diagnostic recovery save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "renamed full-calc formula audit invalid diagnostic recovery save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed full-calc formula audit invalid diagnostic recovery save_as should write shifted qualified formula");
    check_contains(worksheet_xml, "recovered-c5",
        "renamed full-calc formula audit invalid diagnostic recovery save_as should write recovered cell text");
    check_not_contains(worksheet_xml, "invalid-renamed-full-calc-formula-recovery",
        "renamed full-calc formula audit invalid diagnostic recovery save_as should omit rejected payloads");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit invalid diagnostic recovery save_as should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit invalid diagnostic recovery");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    const std::optional<fastxlsx::CellValue> reopened_recovered_cell =
        reopened_sheet.try_cell("C5");
    check(reopened_recovered_cell.has_value() &&
            reopened_recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            reopened_recovered_cell->text_value() == "recovered-c5",
        "renamed full-calc formula audit invalid diagnostic recovery reopened output should read recovered cell text");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit invalid diagnostic recovery no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit invalid diagnostic recovery no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit invalid diagnostic recovery no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit invalid diagnostic recovery no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit invalid diagnostic recovery no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit invalid diagnostic recovery no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit invalid diagnostic recovery no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "renamed full-calc formula audit invalid diagnostic recovery no-op output should match the recovery output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit invalid diagnostic recovery no-op save should leave the source package unchanged");
    check_public_state_reopened_shift_formula_audit_output(
        noop_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit invalid diagnostic recovery no-op output");
    fastxlsx::WorkbookEditor reopened_noop = fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor reopened_noop_sheet = reopened_noop.worksheet("RenamedData");
    const std::optional<fastxlsx::CellValue> reopened_noop_recovered_cell =
        reopened_noop_sheet.try_cell("C5");
    check(reopened_noop_recovered_cell.has_value() &&
            reopened_noop_recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            reopened_noop_recovered_cell->text_value() == "recovered-c5",
        "renamed full-calc formula audit invalid diagnostic recovery no-op output should read recovered cell text");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened_noop,
        "renamed full-calc formula audit invalid diagnostic recovery no-op output after C5 read");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_preserve_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    const std::optional<fastxlsx::CellValue> shifted_formula_cell =
        sheet.try_cell("D3");
    check(shifted_formula_cell.has_value() &&
            shifted_formula_cell->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula_cell->text_value() == shifted_formula &&
            shifted_formula_cell->has_style() &&
            shifted_formula_cell->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire setup should expose the shifted styled formula");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"} &&
            editor.pending_materialized_cell_count() == 7,
        "renamed full-calc formula audit saved reacquire setup should dirty the planned-name session");

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire first save should keep diagnostics clear");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire first output should keep the planned catalog name");
    check_contains(first_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire first output should keep fullCalcOnLoad");
    check(first_entries.find("xl/calcChain.xml") == first_entries.end(),
        "renamed full-calc formula audit saved reacquire first output should not invent calcChain.xml");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "renamed full-calc formula audit saved reacquire first output should keep shifted bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire first output should keep the shifted styled formula");
    check_not_contains(first_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire first output should omit old formula coordinate");
    check_not_contains(first_worksheet_xml, "post-save-c5",
        "renamed full-calc formula audit saved reacquire first output should omit the later mutation");

    std::optional<fastxlsx::WorksheetEditor> maybe_reacquired =
        editor.try_worksheet("RenamedData");
    check(maybe_reacquired.has_value(),
        "renamed full-calc formula audit saved reacquire should find the planned-name saved session");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire should keep the old source name unavailable");
    if (!maybe_reacquired.has_value()) {
        return;
    }

    fastxlsx::WorksheetEditor reacquired = std::move(*maybe_reacquired);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire should return a clean shared session");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire should not queue replacement diagnostics");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed full-calc formula audit saved reacquire should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire should preserve the planned workbook catalog");
    check(reacquired.cell_count() == 7 && sheet.cell_count() == 7,
        "renamed full-calc formula audit saved reacquire should keep shifted sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 4, 4,
        "renamed full-calc formula audit saved reacquire should expose saved shifted bounds");
    const std::optional<fastxlsx::CellValue> reacquired_formula =
        reacquired.try_cell("D3");
    check(reacquired_formula.has_value() &&
            reacquired_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_formula->text_value() == shifted_formula &&
            reacquired_formula->has_style() &&
            reacquired_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire should read the saved shifted styled formula");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A4").text_value() == "extra-c3",
        "renamed full-calc formula audit saved reacquire should preserve shifted source rows");
    check(!sheet.try_cell("A2").has_value() && !reacquired.try_cell("D2").has_value(),
        "renamed full-calc formula audit saved reacquire should keep old coordinates absent");

    check_public_state_renamed_full_calc_formula_audit(
        editor, shifted_formula, "renamed full-calc formula audit saved reacquire");

    reacquired.set_cell(5, 3, fastxlsx::CellValue::text("post-save-c5"));
    const std::size_t post_save_memory = reacquired.estimated_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire later mutation should keep diagnostics clear");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire later mutation should dirty both handles");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed full-calc formula audit saved reacquire later mutation should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 8,
        "renamed full-calc formula audit saved reacquire later mutation should grow the sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == post_save_memory,
        "renamed full-calc formula audit saved reacquire later mutation should report dirty memory");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "renamed full-calc formula audit saved reacquire later mutation should expose one summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" &&
                    summaries[0].planned_name == "RenamedData" &&
                    summaries[0].renamed &&
                    summaries[0].materialized_dirty &&
                    summaries[0].materialized_cell_count == 8 &&
                    summaries[0].estimated_materialized_memory_usage == post_save_memory,
                "renamed full-calc formula audit saved reacquire later mutation summary should retain planned dirty state");
        }
    }
    const std::optional<fastxlsx::CellValue> post_save_cell = sheet.try_cell("C5");
    check(post_save_cell.has_value() &&
            post_save_cell->kind() == fastxlsx::CellValueKind::Text &&
            post_save_cell->text_value() == "post-save-c5",
        "renamed full-calc formula audit saved reacquire later mutation should expose the new text cell");
    const std::optional<fastxlsx::CellValue> preserved_formula =
        reacquired.try_cell("D3");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == shifted_formula &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire later mutation should preserve the shifted styled formula");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> dirty_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire dirty materialized audit");
    check(dirty_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire dirty state should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire dirty shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire dirty shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire dirty source audit");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire second save should clean both handles");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire second save should clear dirty diagnostics again");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire second save should keep diagnostics clear");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire second output should omit the source catalog name");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed full-calc formula audit saved reacquire second output should project recovered bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire second output should keep the shifted styled formula");
    check_contains(second_worksheet_xml, "post-save-c5",
        "renamed full-calc formula audit saved reacquire second output should write the later text cell");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire second output should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        second_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire second output");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    const std::optional<fastxlsx::CellValue> reopened_post_save_cell =
        reopened_sheet.try_cell("C5");
    check(reopened_post_save_cell.has_value() &&
            reopened_post_save_cell->kind() == fastxlsx::CellValueKind::Text &&
            reopened_post_save_cell->text_value() == "post-save-c5",
        "renamed full-calc formula audit saved reacquire reopened output should read the later text cell");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire no-op save should keep both handles clean");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed full-calc formula audit saved reacquire no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed full-calc formula audit saved reacquire no-op save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "renamed full-calc formula audit saved reacquire no-op output should match the second output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire no-op save should leave the source package unchanged");
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> noop_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire no-op materialized audit");
    check(noop_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire no-op should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        noop_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire no-op shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        noop_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire no-op shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire no-op source audit");
    check_public_state_reopened_shift_formula_audit_output(
        noop_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire no-op output");
    fastxlsx::WorkbookEditor reopened_noop = fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor reopened_noop_sheet = reopened_noop.worksheet("RenamedData");
    const std::optional<fastxlsx::CellValue> reopened_noop_post_save_cell =
        reopened_noop_sheet.try_cell("C5");
    check(reopened_noop_post_save_cell.has_value() &&
            reopened_noop_post_save_cell->kind() == fastxlsx::CellValueKind::Text &&
            reopened_noop_post_save_cell->text_value() == "post-save-c5",
        "renamed full-calc formula audit saved reacquire no-op reopened output should read the later text cell");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened_noop,
        "renamed full-calc formula audit saved reacquire no-op reopened output after C5 read");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_failed_save_preserve_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-failed-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-failed-save-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-failed-save-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-failed-save-noop-output.xlsx");
    const auto source_entries_before_save = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire failed save first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire failed save first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire failed save first save should clear dirty diagnostics");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire failed save reacquire should start clean");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire failed save should keep old source name unavailable");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire failed save");

    reacquired.set_cell(5, 3, fastxlsx::CellValue::text("failed-save-c5"));
    const std::size_t failed_save_memory = reacquired.estimated_memory_usage();
    const auto check_dirty_reacquired_state = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep diagnostics clear");
        check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
            label + " should keep both planned-name handles dirty");
        check(editor.pending_change_count() == 3,
            label + " should not record the later materialized handoff before a successful save");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"RenamedData"},
            label + " should report the dirty planned-name session once");
        check(editor.pending_materialized_cell_count() == 8 &&
                editor.estimated_pending_materialized_memory_usage() == failed_save_memory,
            label + " should keep the dirty sparse count and memory");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
                editor.pending_worksheet_edits();
            check(summaries.size() == 1,
                label + " should expose one dirty summary");
            if (summaries.size() == 1) {
                check(summaries[0].source_name == "Data" &&
                        summaries[0].planned_name == "RenamedData" &&
                        summaries[0].renamed &&
                        summaries[0].materialized_dirty &&
                        summaries[0].materialized_cell_count == 8 &&
                        summaries[0].estimated_materialized_memory_usage == failed_save_memory,
                    label + " summary should keep renamed dirty materialized state");
            }
        }
        check(editor.source_worksheet_names() == expected_source_names &&
                editor.worksheet_names() == expected_planned_names,
            label + " should preserve source and planned worksheet names");
        check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
            label + " should preserve the planned workbook catalog");

        const std::optional<fastxlsx::CellValue> materialized_formula =
            sheet.try_cell("D3");
        check(materialized_formula.has_value() &&
                materialized_formula->kind() == fastxlsx::CellValueKind::Formula &&
                materialized_formula->text_value() == shifted_formula &&
                materialized_formula->has_style() &&
                materialized_formula->style_id().value() == styled_formula_style.value(),
            label + " should preserve the shifted styled formula");
        const std::optional<fastxlsx::CellValue> dirty_cell =
            reacquired.try_cell("C5");
        check(dirty_cell.has_value() &&
                dirty_cell->kind() == fastxlsx::CellValueKind::Text &&
                dirty_cell->text_value() == "failed-save-c5",
            label + " should preserve the later dirty text cell");
        check(!sheet.try_cell("D2").has_value(),
            label + " should keep the old formula coordinate absent");
    };

    check_dirty_reacquired_state(
        "renamed full-calc formula audit saved reacquire failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "renamed full-calc formula audit saved reacquire failed save should reject exact source overwrite");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_save,
        "renamed full-calc formula audit saved reacquire failed save should leave source package bytes unchanged");
    check_dirty_reacquired_state(
        "renamed full-calc formula audit saved reacquire failed save rejected source overwrite");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> dirty_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire failed save materialized audit");
    check(dirty_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire failed save should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire failed save shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire failed save shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire failed save source audit");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire failed save first output should keep the planned catalog name");
    check_contains(first_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire failed save first output should keep fullCalcOnLoad");
    check(first_entries.find("xl/calcChain.xml") == first_entries.end(),
        "renamed full-calc formula audit saved reacquire failed save first output should not invent calcChain.xml");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "renamed full-calc formula audit saved reacquire failed save first output should keep first-save bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire failed save first output should keep the shifted styled formula");
    check_not_contains(first_worksheet_xml, "failed-save-c5",
        "renamed full-calc formula audit saved reacquire failed save first output should omit the later dirty cell");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire failed save safe retry should clean both handles");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire failed save safe retry should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire failed save safe retry should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire failed save safe retry should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_save,
        "renamed full-calc formula audit saved reacquire failed save safe retry should keep source package bytes unchanged");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire failed save second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire failed save second output should omit the source catalog name");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire failed save second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire failed save second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed full-calc formula audit saved reacquire failed save second output should project dirty retry bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire failed save second output should keep the shifted styled formula");
    check_contains(second_worksheet_xml, "failed-save-c5",
        "renamed full-calc formula audit saved reacquire failed save second output should write the dirty text cell");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire failed save second output should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        second_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire failed save second output");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    const std::optional<fastxlsx::CellValue> reopened_dirty_cell =
        reopened_sheet.try_cell("C5");
    check(reopened_dirty_cell.has_value() &&
            reopened_dirty_cell->kind() == fastxlsx::CellValueKind::Text &&
            reopened_dirty_cell->text_value() == "failed-save-c5",
        "renamed full-calc formula audit saved reacquire failed save reopened output should read the dirty text cell");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire failed save no-op save should keep both handles clean");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire failed save no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire failed save no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire failed save no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire failed save no-op save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_save,
        "renamed full-calc formula audit saved reacquire failed save no-op save should keep source package bytes unchanged");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed full-calc formula audit saved reacquire failed save no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed full-calc formula audit saved reacquire failed save no-op save");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == second_entries,
        "renamed full-calc formula audit saved reacquire failed save no-op output should match the safe retry output");
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> noop_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire failed save no-op materialized audit");
    check(noop_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire failed save no-op should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        noop_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire failed save no-op shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        noop_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire failed save no-op shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire failed save no-op source audit");
    check_public_state_reopened_shift_formula_audit_output(
        noop_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire failed save no-op output");
    fastxlsx::WorkbookEditor reopened_noop = fastxlsx::WorkbookEditor::open(noop_output);
    fastxlsx::WorksheetEditor reopened_noop_sheet = reopened_noop.worksheet("RenamedData");
    const std::optional<fastxlsx::CellValue> reopened_noop_dirty_cell =
        reopened_noop_sheet.try_cell("C5");
    check(reopened_noop_dirty_cell.has_value() &&
            reopened_noop_dirty_cell->kind() == fastxlsx::CellValueKind::Text &&
            reopened_noop_dirty_cell->text_value() == "failed-save-c5",
        "renamed full-calc formula audit saved reacquire failed save no-op output should read the dirty text cell");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened_noop,
        "renamed full-calc formula audit saved reacquire failed save no-op output after C5 read");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_invalid_mutation_recovery()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-mutation-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-mutation-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-mutation-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-mutation-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid mutation first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid mutation first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid mutation first save should clear dirty diagnostics");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid mutation first save should leave the source package unchanged");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid mutation reacquire should start clean");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire invalid mutation should keep old source name unavailable");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire invalid mutation");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1,
            fastxlsx::CellValue::formula("invalid-saved-reacquire-row-zero"));
    }), "renamed full-calc formula audit saved reacquire invalid mutation should reject row-zero set_cell");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell("XFE1",
            fastxlsx::CellValue::formula("invalid-saved-reacquire-column-overflow"));
    }), "renamed full-calc formula audit saved reacquire invalid mutation should reject column-overflow set_cell");
    check(threw_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "renamed full-calc formula audit saved reacquire invalid mutation should reject range erase_cell references");

    check(editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid mutation should populate last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(), "WorksheetEditor cell reference is invalid",
            "renamed full-calc formula audit saved reacquire invalid mutation should expose invalid reference diagnostic");
    }
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid mutation should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid mutation should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid mutation should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire invalid mutation should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed full-calc formula audit saved reacquire invalid mutation should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire invalid mutation should preserve the planned workbook catalog");
    check(sheet.cell_count() == 7 && reacquired.cell_count() == 7,
        "renamed full-calc formula audit saved reacquire invalid mutation should preserve sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 4, 4,
        "renamed full-calc formula audit saved reacquire invalid mutation should preserve shifted bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula =
        reacquired.try_cell("D3");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == shifted_formula &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire invalid mutation should preserve the shifted styled formula");
    check(!sheet.try_cell("C5").has_value(),
        "renamed full-calc formula audit saved reacquire invalid mutation should not stage rejected payloads");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> clean_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire invalid mutation clean audit");
    check(clean_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire invalid mutation should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire invalid mutation shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire invalid mutation shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire invalid mutation source audit");
    check(editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid mutation audits should preserve last_edit_error");

    sheet.set_cell(5, 3, fastxlsx::CellValue::text("invalid-recovery-c5"));
    const std::size_t recovery_memory = sheet.estimated_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid mutation valid recovery should clear diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid mutation recovery should dirty both handles");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed full-calc formula audit saved reacquire invalid mutation recovery should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 8,
        "renamed full-calc formula audit saved reacquire invalid mutation recovery should grow sparse count");
    check_public_state_renamed_dirty_materialized_summary_memory(
        editor, sheet, reacquired, 8, recovery_memory,
        "renamed full-calc formula audit saved reacquire invalid mutation recovery");
    const std::optional<fastxlsx::CellValue> recovered_cell =
        reacquired.try_cell("C5");
    check(recovered_cell.has_value() &&
            recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            recovered_cell->text_value() == "invalid-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid mutation recovery should expose the valid text cell");
    const std::optional<fastxlsx::CellValue> recovered_formula =
        sheet.try_cell("D3");
    check(recovered_formula.has_value() &&
            recovered_formula->kind() == fastxlsx::CellValueKind::Formula &&
            recovered_formula->text_value() == shifted_formula &&
            recovered_formula->has_style() &&
            recovered_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire invalid mutation recovery should preserve shifted formula");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> dirty_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire invalid mutation recovery dirty audit");
    check(dirty_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire invalid mutation recovery should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire invalid mutation recovery shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire invalid mutation recovery shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire invalid mutation recovery source audit");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid mutation recovery save should clean both handles");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire invalid mutation recovery save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid mutation recovery save should clear dirty diagnostics");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire invalid mutation first output should keep fullCalcOnLoad");
    check_not_contains(first_worksheet_xml, "invalid-saved-reacquire-",
        "renamed full-calc formula audit saved reacquire invalid mutation first output should omit rejected payloads");
    check_not_contains(first_worksheet_xml, "invalid-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid mutation first output should omit the later recovery cell");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid mutation recovery save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire invalid mutation second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire invalid mutation second output should omit the source catalog name");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire invalid mutation second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire invalid mutation second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed full-calc formula audit saved reacquire invalid mutation second output should project recovered bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire invalid mutation second output should keep shifted formula with style");
    check_contains(second_worksheet_xml, "invalid-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid mutation second output should write recovered text");
    check_not_contains(second_worksheet_xml, "invalid-saved-reacquire-",
        "renamed full-calc formula audit saved reacquire invalid mutation second output should not leak rejected payloads");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire invalid mutation second output should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        second_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire invalid mutation second output");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    const std::optional<fastxlsx::CellValue> reopened_recovered_cell =
        reopened_sheet.try_cell("C5");
    check(reopened_recovered_cell.has_value() &&
            reopened_recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            reopened_recovered_cell->text_value() == "invalid-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid mutation reopened output should read recovered text");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid mutation no-op save should keep both handles clean");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire invalid mutation no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid mutation no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire invalid mutation no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid mutation no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed full-calc formula audit saved reacquire invalid mutation no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed full-calc formula audit saved reacquire invalid mutation no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed full-calc formula audit saved reacquire invalid mutation no-op output should match the recovery output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid mutation no-op save should leave the source package unchanged");
    check_public_state_renamed_full_calc_noop_formula_audit_readback(
        editor, noop_output, shifted_formula, styled_formula_style,
        "invalid-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid mutation");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_invalid_mutation_noop_save()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-mutation-noop-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-mutation-noop-save-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-mutation-noop-save-second-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save first save should keep diagnostics clear");
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save first save should leave the source package unchanged");
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> expected_summaries =
        editor.pending_worksheet_edits();

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save reacquire should start clean");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should keep old source name unavailable");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire invalid mutation noop-save");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1,
            fastxlsx::CellValue::formula("invalid-saved-reacquire-noop-row-zero"));
    }), "renamed full-calc formula audit saved reacquire invalid mutation noop-save should reject row-zero set_cell");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell("XFE1",
            fastxlsx::CellValue::formula("invalid-saved-reacquire-noop-column-overflow"));
    }), "renamed full-calc formula audit saved reacquire invalid mutation noop-save should reject column-overflow set_cell");
    check(threw_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should reject range erase_cell references");

    const std::optional<std::string> invalid_error = editor.last_edit_error();
    check(invalid_error.has_value(),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should populate last_edit_error");
    if (invalid_error.has_value()) {
        check_contains(*invalid_error, "WorksheetEditor cell reference is invalid",
            "renamed full-calc formula audit saved reacquire invalid mutation noop-save should expose invalid reference diagnostic");
    }
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should not add materialized handoffs");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), expected_summaries),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should preserve saved edit summaries");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should preserve the planned workbook catalog");
    check(sheet.cell_count() == 7 && reacquired.cell_count() == 7,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should preserve sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 4, 4,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should preserve shifted bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula =
        reacquired.try_cell("D3");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == shifted_formula &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should preserve the shifted styled formula");
    check(!sheet.try_cell("C5").has_value(),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should not stage rejected payloads");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> clean_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire invalid mutation noop-save clean audit");
    check(clean_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire invalid mutation noop-save source audit");
    check(editor.last_edit_error() == invalid_error,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save audits should preserve last_edit_error");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second save should not record another materialized handoff");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), expected_summaries),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second save should preserve edit summaries");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second save should not queue replacement diagnostics");
    check(editor.last_edit_error() == invalid_error,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second save should preserve invalid mutation diagnostic");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names &&
            workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second save should preserve catalog diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second save");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second save should leave the source package unchanged");
    check(second_entries == first_entries,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second output should match the pre-error save");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second output should omit the source catalog name");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second output should keep shifted bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second output should keep shifted formula with style");
    check_not_contains(second_worksheet_xml, "invalid-saved-reacquire-noop-",
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second output should not leak rejected payloads");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second output should omit old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="C5")",
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save second output should not invent recovery cells");

    check_public_state_renamed_full_calc_noop_formula_audit_source_rows_readback(
        editor, second_output, shifted_formula, styled_formula_style,
        "renamed full-calc formula audit saved reacquire invalid mutation noop-save");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_invalid_reads_recovery()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-reads-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-reads-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-reads-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-reads-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid reads first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid reads first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid reads first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads first save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid reads first save should leave the source package unchanged");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid reads reacquire should start clean");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads should keep old source name unavailable");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire invalid reads");

    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "renamed full-calc formula audit saved reacquire invalid reads should reject row-zero try_cell");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell(1, 0); }),
        "renamed full-calc formula audit saved reacquire invalid reads should reject column-zero get_cell");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell("a1"); }),
        "renamed full-calc formula audit saved reacquire invalid reads should reject lowercase A1 reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("XFE1"); }),
        "renamed full-calc formula audit saved reacquire invalid reads should reject A1 column overflow");
    check(threw_fastxlsx_error([&] {
        (void)sheet.sparse_cells(fastxlsx::CellRange {3, 3, 2, 2});
    }), "renamed full-calc formula audit saved reacquire invalid reads should reject reversed CellRange reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.sparse_cells("B2:A1"); }),
        "renamed full-calc formula audit saved reacquire invalid reads should reject reversed A1 range reads");
    check(threw_fastxlsx_error([&] {
        const std::array<fastxlsx::WorksheetCellReference, 2> invalid_batch {
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {1048577, 1},
        };
        (void)sheet.sparse_cells(invalid_batch);
    }), "renamed full-calc formula audit saved reacquire invalid reads should reject invalid coordinate batch reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.row_cells(0); }),
        "renamed full-calc formula audit saved reacquire invalid reads should reject row_cells row zero");
    check(threw_fastxlsx_error([&] { (void)sheet.column_cells(16385); }),
        "renamed full-calc formula audit saved reacquire invalid reads should reject column_cells column overflow");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("C5"); }),
        "renamed full-calc formula audit saved reacquire invalid reads should reject valid but missing get_cell reads");

    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads should keep last_edit_error clear");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid reads should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid reads should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid reads should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire invalid reads should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed full-calc formula audit saved reacquire invalid reads should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire invalid reads should preserve the planned workbook catalog");
    check(sheet.cell_count() == 7 && reacquired.cell_count() == 7,
        "renamed full-calc formula audit saved reacquire invalid reads should preserve sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 4, 4,
        "renamed full-calc formula audit saved reacquire invalid reads should preserve shifted bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula =
        sheet.try_cell("D3");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == shifted_formula &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire invalid reads should preserve the shifted styled formula");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A4").text_value() == "extra-c3",
        "renamed full-calc formula audit saved reacquire invalid reads should preserve shifted source rows");
    check(!sheet.try_cell("A2").has_value() && !reacquired.try_cell("D2").has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads should keep old coordinates absent");
    check(!reacquired.try_cell("C5").has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads should not stage recovery cells before mutation");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> clean_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire invalid reads clean audit");
    check(clean_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire invalid reads should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire invalid reads shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire invalid reads shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire invalid reads source audit");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads audits should keep diagnostics clear");

    reacquired.set_cell(5, 3, fastxlsx::CellValue::text("invalid-read-recovery-c5"));
    const std::size_t recovery_memory = reacquired.estimated_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads recovery should keep diagnostics clear");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid reads recovery should dirty both handles");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed full-calc formula audit saved reacquire invalid reads recovery should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 8,
        "renamed full-calc formula audit saved reacquire invalid reads recovery should grow sparse count");
    check_public_state_renamed_dirty_materialized_summary_memory(
        editor, sheet, reacquired, 8, recovery_memory,
        "renamed full-calc formula audit saved reacquire invalid reads recovery");
    const std::optional<fastxlsx::CellValue> recovered_cell =
        sheet.try_cell("C5");
    check(recovered_cell.has_value() &&
            recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            recovered_cell->text_value() == "invalid-read-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid reads recovery should expose the valid text cell");
    const std::optional<fastxlsx::CellValue> recovered_formula =
        reacquired.try_cell("D3");
    check(recovered_formula.has_value() &&
            recovered_formula->kind() == fastxlsx::CellValueKind::Formula &&
            recovered_formula->text_value() == shifted_formula &&
            recovered_formula->has_style() &&
            recovered_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire invalid reads recovery should preserve shifted formula");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> dirty_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire invalid reads recovery dirty audit");
    check(dirty_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire invalid reads recovery should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire invalid reads recovery shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire invalid reads recovery shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire invalid reads recovery source audit");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid reads recovery save should clean both handles");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire invalid reads recovery save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid reads recovery save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads recovery save should keep diagnostics clear");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire invalid reads first output should keep fullCalcOnLoad");
    check(first_entries.find("xl/calcChain.xml") == first_entries.end(),
        "renamed full-calc formula audit saved reacquire invalid reads first output should not invent calcChain.xml");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "renamed full-calc formula audit saved reacquire invalid reads first output should keep shifted bounds");
    check_not_contains(first_worksheet_xml, "invalid-read-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid reads first output should omit the later recovery cell");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid reads recovery save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire invalid reads second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire invalid reads second output should omit the source catalog name");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire invalid reads second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire invalid reads second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed full-calc formula audit saved reacquire invalid reads second output should project recovered bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire invalid reads second output should keep shifted formula with style");
    check_contains(second_worksheet_xml, "invalid-read-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid reads second output should write recovered text");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire invalid reads second output should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        second_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire invalid reads second output");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    const std::optional<fastxlsx::CellValue> reopened_recovered_cell =
        reopened_sheet.try_cell("C5");
    check(reopened_recovered_cell.has_value() &&
            reopened_recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            reopened_recovered_cell->text_value() == "invalid-read-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid reads reopened output should read recovered text");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid reads no-op save should keep both handles clean");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire invalid reads no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid reads no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire invalid reads no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit saved reacquire invalid reads no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit saved reacquire invalid reads no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed full-calc formula audit saved reacquire invalid reads no-op output should match the recovery output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid reads no-op save should leave the source package unchanged");
    check_public_state_renamed_full_calc_noop_formula_audit_readback(
        editor, noop_output, shifted_formula, styled_formula_style,
        "invalid-read-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid reads");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_invalid_reads_noop_save()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-reads-noop-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-reads-noop-save-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-reads-noop-save-second-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save first save should keep diagnostics clear");
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save first save should leave the source package unchanged");
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> expected_summaries =
        editor.pending_worksheet_edits();

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save reacquire should start clean");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should keep old source name unavailable");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire invalid reads noop-save");

    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should reject row-zero try_cell");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell(1, 0); }),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should reject column-zero get_cell");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell("a1"); }),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should reject lowercase A1 reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("XFE1"); }),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should reject A1 column overflow");
    check(threw_fastxlsx_error([&] {
        (void)sheet.sparse_cells(fastxlsx::CellRange {3, 3, 2, 2});
    }), "renamed full-calc formula audit saved reacquire invalid reads noop-save should reject reversed CellRange reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.sparse_cells("B2:A1"); }),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should reject reversed A1 range reads");
    check(threw_fastxlsx_error([&] {
        const std::array<fastxlsx::WorksheetCellReference, 2> invalid_batch {
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {1048577, 1},
        };
        (void)sheet.sparse_cells(invalid_batch);
    }), "renamed full-calc formula audit saved reacquire invalid reads noop-save should reject invalid coordinate batch reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.row_cells(0); }),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should reject row_cells row zero");
    check(threw_fastxlsx_error([&] { (void)sheet.column_cells(16385); }),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should reject column_cells column overflow");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("C5"); }),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should reject valid but missing get_cell reads");

    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should keep last_edit_error clear");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should not add materialized handoffs");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), expected_summaries),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should preserve saved edit summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire invalid reads noop-save should not queue replacement diagnostics");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should not dirty materialized diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should preserve the planned workbook catalog");
    check(sheet.cell_count() == 7 && reacquired.cell_count() == 7,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should preserve sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 4, 4,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should preserve shifted bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula =
        sheet.try_cell("D3");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == shifted_formula &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should preserve the shifted styled formula");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A4").text_value() == "extra-c3",
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should preserve shifted source rows");
    check(!sheet.try_cell("A2").has_value() &&
            !reacquired.try_cell("D2").has_value() &&
            !reacquired.try_cell("C5").has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should keep unstaged coordinates absent");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> clean_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire invalid reads noop-save clean audit");
    check(clean_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire invalid reads noop-save shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire invalid reads noop-save shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire invalid reads noop-save source audit");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save audits should keep diagnostics clear");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second save should not record another materialized handoff");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), expected_summaries),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second save should preserve edit summaries");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire invalid reads noop-save second save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second save should keep diagnostics clear");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names &&
            workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second save should preserve catalog diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second save");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second save should leave the source package unchanged");
    check(second_entries == first_entries,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second output should match the pre-error save");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second output should omit the source catalog name");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second output should keep shifted bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second output should keep shifted formula with style");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second output should omit old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="C5")",
        "renamed full-calc formula audit saved reacquire invalid reads noop-save second output should not invent recovery cells");

    check_public_state_renamed_full_calc_noop_formula_audit_source_rows_readback(
        editor, second_output, shifted_formula, styled_formula_style,
        "renamed full-calc formula audit saved reacquire invalid reads noop-save");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_invalid_shifts_recovery()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-shifts-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-shifts-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-shifts-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-shifts-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid shifts first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid shifts first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid shifts first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid shifts first save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid shifts first save should leave the source package unchanged");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid shifts reacquire should start clean");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire invalid shifts should keep old source name unavailable");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire invalid shifts");

    check(threw_fastxlsx_error([&] { sheet.insert_rows(0, 1); }),
        "renamed full-calc formula audit saved reacquire invalid shifts should reject row-zero insert_rows");
    check(threw_fastxlsx_error([&] { reacquired.delete_rows(1048576, 2); }),
        "renamed full-calc formula audit saved reacquire invalid shifts should reject overflowing delete_rows");
    check(threw_fastxlsx_error([&] { sheet.insert_columns(0, 1); }),
        "renamed full-calc formula audit saved reacquire invalid shifts should reject column-zero insert_columns");
    check(threw_fastxlsx_error([&] { reacquired.delete_columns(16384, 2); }),
        "renamed full-calc formula audit saved reacquire invalid shifts should reject overflowing delete_columns");

    check(editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid shifts should populate last_edit_error");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid shifts should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid shifts should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid shifts should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire invalid shifts should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed full-calc formula audit saved reacquire invalid shifts should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire invalid shifts should preserve the planned workbook catalog");
    check(sheet.cell_count() == 7 && reacquired.cell_count() == 7,
        "renamed full-calc formula audit saved reacquire invalid shifts should preserve sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 4, 4,
        "renamed full-calc formula audit saved reacquire invalid shifts should preserve shifted bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula =
        sheet.try_cell("D3");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == shifted_formula &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire invalid shifts should preserve the shifted styled formula");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A4").text_value() == "extra-c3",
        "renamed full-calc formula audit saved reacquire invalid shifts should preserve shifted source rows");
    check(!sheet.try_cell("A2").has_value() && !reacquired.try_cell("D2").has_value(),
        "renamed full-calc formula audit saved reacquire invalid shifts should keep old coordinates absent");
    check(!reacquired.try_cell("C5").has_value(),
        "renamed full-calc formula audit saved reacquire invalid shifts should not stage recovery cells before mutation");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> clean_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire invalid shifts clean audit");
    check(clean_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire invalid shifts should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire invalid shifts shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire invalid shifts shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire invalid shifts source audit");
    check(editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid shifts audits should preserve last_edit_error");

    reacquired.set_cell(5, 3, fastxlsx::CellValue::text("invalid-shift-recovery-c5"));
    const std::size_t recovery_memory = reacquired.estimated_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid shifts recovery should clear diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid shifts recovery should dirty both handles");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed full-calc formula audit saved reacquire invalid shifts recovery should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 8,
        "renamed full-calc formula audit saved reacquire invalid shifts recovery should grow sparse count");
    check_public_state_renamed_dirty_materialized_summary_memory(
        editor, sheet, reacquired, 8, recovery_memory,
        "renamed full-calc formula audit saved reacquire invalid shifts recovery");
    const std::optional<fastxlsx::CellValue> recovered_cell =
        sheet.try_cell("C5");
    check(recovered_cell.has_value() &&
            recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            recovered_cell->text_value() == "invalid-shift-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid shifts recovery should expose the valid text cell");
    const std::optional<fastxlsx::CellValue> recovered_formula =
        reacquired.try_cell("D3");
    check(recovered_formula.has_value() &&
            recovered_formula->kind() == fastxlsx::CellValueKind::Formula &&
            recovered_formula->text_value() == shifted_formula &&
            recovered_formula->has_style() &&
            recovered_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire invalid shifts recovery should preserve shifted formula");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> dirty_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire invalid shifts recovery dirty audit");
    check(dirty_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire invalid shifts recovery should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire invalid shifts recovery shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire invalid shifts recovery shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire invalid shifts recovery source audit");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid shifts recovery save should clean both handles");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire invalid shifts recovery save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid shifts recovery save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid shifts recovery save should keep diagnostics clear");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire invalid shifts first output should keep fullCalcOnLoad");
    check(first_entries.find("xl/calcChain.xml") == first_entries.end(),
        "renamed full-calc formula audit saved reacquire invalid shifts first output should not invent calcChain.xml");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "renamed full-calc formula audit saved reacquire invalid shifts first output should keep shifted bounds");
    check_not_contains(first_worksheet_xml, "invalid-shift-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid shifts first output should omit the later recovery cell");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid shifts recovery save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire invalid shifts second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire invalid shifts second output should omit the source catalog name");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire invalid shifts second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire invalid shifts second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed full-calc formula audit saved reacquire invalid shifts second output should project recovered bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire invalid shifts second output should keep shifted formula with style");
    check_contains(second_worksheet_xml, "invalid-shift-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid shifts second output should write recovered text");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire invalid shifts second output should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        second_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire invalid shifts second output");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    const std::optional<fastxlsx::CellValue> reopened_recovered_cell =
        reopened_sheet.try_cell("C5");
    check(reopened_recovered_cell.has_value() &&
            reopened_recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            reopened_recovered_cell->text_value() == "invalid-shift-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid shifts reopened output should read recovered text");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid shifts no-op save should keep both handles clean");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire invalid shifts no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid shifts no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire invalid shifts no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid shifts no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit saved reacquire invalid shifts no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit saved reacquire invalid shifts no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed full-calc formula audit saved reacquire invalid shifts no-op output should match the recovery output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid shifts no-op save should leave the source package unchanged");
    check_public_state_renamed_full_calc_noop_formula_audit_readback(
        editor, noop_output, shifted_formula, styled_formula_style,
        "invalid-shift-recovery-c5",
        "renamed full-calc formula audit saved reacquire invalid shifts");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_invalid_shifts_noop_save()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-shifts-noop-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-shifts-noop-save-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-invalid-shifts-noop-save-second-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save first save should keep diagnostics clear");
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save first save should leave the source package unchanged");
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> expected_summaries =
        editor.pending_worksheet_edits();

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save reacquire should start clean");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should keep the old source name unavailable");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire invalid shifts noop-save");

    check(threw_fastxlsx_error([&] { sheet.insert_rows(0, 1); }),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should reject row-zero insert_rows");
    check(threw_fastxlsx_error([&] { reacquired.delete_rows(1048576, 2); }),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should reject overflowing delete_rows");
    check(threw_fastxlsx_error([&] { sheet.insert_columns(0, 1); }),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should reject column-zero insert_columns");
    check(threw_fastxlsx_error([&] { reacquired.delete_columns(16384, 2); }),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should reject overflowing delete_columns");

    const std::optional<std::string> invalid_shift_error =
        editor.last_edit_error();
    check(invalid_shift_error.has_value(),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should populate last_edit_error");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should not add materialized handoffs");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), expected_summaries),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should preserve saved edit summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire invalid shifts noop-save should not queue replacement diagnostics");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should not dirty materialized diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should preserve the planned workbook catalog");
    check(sheet.cell_count() == 7 && reacquired.cell_count() == 7,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should preserve sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 4, 4,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should preserve shifted bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula =
        sheet.try_cell("D3");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == shifted_formula &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should preserve the shifted styled formula");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A4").text_value() == "extra-c3",
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should preserve shifted source rows");
    check(!sheet.try_cell("A2").has_value() &&
            !reacquired.try_cell("D2").has_value() &&
            !reacquired.try_cell("C5").has_value(),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should keep unstaged coordinates absent");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> clean_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire invalid shifts noop-save clean audit");
    check(clean_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire invalid shifts noop-save source audit");
    check(editor.last_edit_error() == invalid_shift_error,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save audits should preserve last_edit_error");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second save should not record another materialized handoff");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), expected_summaries),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second save should preserve edit summaries");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire invalid shifts noop-save second save should not queue replacement diagnostics");
    check(editor.last_edit_error() == invalid_shift_error,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second save should preserve the invalid shift diagnostic");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names &&
            workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second save should preserve catalog diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second save");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second save should leave the source package unchanged");
    check(second_entries == first_entries,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second output should match the pre-error save");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second output should omit the source catalog name");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second output should keep shifted bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second output should keep shifted formula with style");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second output should omit old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="C5")",
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save second output should not invent recovery cells");

    check_public_state_renamed_full_calc_noop_formula_audit_source_rows_readback(
        editor, second_output, shifted_formula, styled_formula_style,
        "renamed full-calc formula audit saved reacquire invalid shifts noop-save");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_missing_query_recovery()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-missing-query-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-missing-query-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-missing-query-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-missing-query-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire missing query first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire missing query first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire missing query first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire missing query first save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire missing query first save should leave the source package unchanged");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire missing query reacquire should start clean");

    check(!editor.try_worksheet("Missing").has_value(),
        "renamed full-calc formula audit saved reacquire missing query try_worksheet should return empty for missing sheet");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire missing query should keep the old source name unavailable");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire missing query");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Missing");
    }), "renamed full-calc formula audit saved reacquire missing query worksheet should reject missing sheet");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Data");
    }), "renamed full-calc formula audit saved reacquire missing query worksheet should reject old source name");

    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire missing query should keep last_edit_error clear");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire missing query should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire missing query should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire missing query should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire missing query should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed full-calc formula audit saved reacquire missing query should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire missing query should preserve the planned workbook catalog");
    check(sheet.cell_count() == 7 && reacquired.cell_count() == 7,
        "renamed full-calc formula audit saved reacquire missing query should preserve sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 4, 4,
        "renamed full-calc formula audit saved reacquire missing query should preserve shifted bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula =
        sheet.try_cell("D3");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == shifted_formula &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire missing query should preserve the shifted styled formula");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A4").text_value() == "extra-c3",
        "renamed full-calc formula audit saved reacquire missing query should preserve shifted source rows");
    check(!sheet.try_cell("A2").has_value() && !reacquired.try_cell("D2").has_value(),
        "renamed full-calc formula audit saved reacquire missing query should keep old coordinates absent");
    check(!reacquired.try_cell("C5").has_value(),
        "renamed full-calc formula audit saved reacquire missing query should not stage recovery cells before mutation");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> clean_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire missing query clean audit");
    check(clean_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire missing query should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire missing query shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire missing query shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire missing query source audit");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire missing query audits should keep diagnostics clear");

    reacquired.set_cell(5, 3, fastxlsx::CellValue::text("missing-query-recovery-c5"));
    const std::size_t recovery_memory = reacquired.estimated_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire missing query recovery should keep diagnostics clear");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire missing query recovery should dirty both handles");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed full-calc formula audit saved reacquire missing query recovery should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 8,
        "renamed full-calc formula audit saved reacquire missing query recovery should grow sparse count");
    check_public_state_renamed_dirty_materialized_summary_memory(
        editor, sheet, reacquired, 8, recovery_memory,
        "renamed full-calc formula audit saved reacquire missing query recovery");
    const std::optional<fastxlsx::CellValue> recovered_cell =
        sheet.try_cell("C5");
    check(recovered_cell.has_value() &&
            recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            recovered_cell->text_value() == "missing-query-recovery-c5",
        "renamed full-calc formula audit saved reacquire missing query recovery should expose the valid text cell");
    const std::optional<fastxlsx::CellValue> recovered_formula =
        reacquired.try_cell("D3");
    check(recovered_formula.has_value() &&
            recovered_formula->kind() == fastxlsx::CellValueKind::Formula &&
            recovered_formula->text_value() == shifted_formula &&
            recovered_formula->has_style() &&
            recovered_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire missing query recovery should preserve shifted formula");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> dirty_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire missing query recovery dirty audit");
    check(dirty_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire missing query recovery should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire missing query recovery shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire missing query recovery shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire missing query recovery source audit");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire missing query recovery save should clean both handles");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire missing query recovery save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire missing query recovery save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire missing query recovery save should keep diagnostics clear");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire missing query first output should keep fullCalcOnLoad");
    check(first_entries.find("xl/calcChain.xml") == first_entries.end(),
        "renamed full-calc formula audit saved reacquire missing query first output should not invent calcChain.xml");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "renamed full-calc formula audit saved reacquire missing query first output should keep shifted bounds");
    check_not_contains(first_worksheet_xml, "missing-query-recovery-c5",
        "renamed full-calc formula audit saved reacquire missing query first output should omit the later recovery cell");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire missing query recovery save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire missing query second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire missing query second output should omit the source catalog name");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire missing query second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire missing query second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed full-calc formula audit saved reacquire missing query second output should project recovered bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire missing query second output should keep shifted formula with style");
    check_contains(second_worksheet_xml, "missing-query-recovery-c5",
        "renamed full-calc formula audit saved reacquire missing query second output should write recovered text");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire missing query second output should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        second_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire missing query second output");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    const std::optional<fastxlsx::CellValue> reopened_recovered_cell =
        reopened_sheet.try_cell("C5");
    check(reopened_recovered_cell.has_value() &&
            reopened_recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            reopened_recovered_cell->text_value() == "missing-query-recovery-c5",
        "renamed full-calc formula audit saved reacquire missing query reopened output should read recovered text");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire missing query no-op save should keep both handles clean");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire missing query no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire missing query no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire missing query no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire missing query no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit saved reacquire missing query no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit saved reacquire missing query no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed full-calc formula audit saved reacquire missing query no-op output should match the recovery output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire missing query no-op save should leave the source package unchanged");
    check_public_state_renamed_full_calc_noop_formula_audit_readback(
        editor, noop_output, shifted_formula, styled_formula_style,
        "missing-query-recovery-c5",
        "renamed full-calc formula audit saved reacquire missing query");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_missing_query_noop_save()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-missing-query-noop-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-missing-query-noop-save-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-missing-query-noop-save-second-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire missing query noop-save first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire missing query noop-save first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire missing query noop-save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire missing query noop-save first save should keep diagnostics clear");
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire missing query noop-save first save should leave the source package unchanged");
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> expected_summaries =
        editor.pending_worksheet_edits();

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire missing query noop-save reacquire should start clean");

    check(!editor.try_worksheet("Missing").has_value(),
        "renamed full-calc formula audit saved reacquire missing query noop-save try_worksheet should return empty for missing sheet");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire missing query noop-save should keep the old source name unavailable");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire missing query noop-save");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Missing");
    }), "renamed full-calc formula audit saved reacquire missing query noop-save worksheet should reject missing sheet");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Data");
    }), "renamed full-calc formula audit saved reacquire missing query noop-save worksheet should reject old source name");

    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire missing query noop-save should keep last_edit_error clear");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire missing query noop-save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire missing query noop-save should not add materialized handoffs");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), expected_summaries),
        "renamed full-calc formula audit saved reacquire missing query noop-save should preserve saved edit summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire missing query noop-save should not queue replacement diagnostics");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire missing query noop-save should not dirty materialized diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed full-calc formula audit saved reacquire missing query noop-save should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire missing query noop-save should preserve the planned workbook catalog");
    check(sheet.cell_count() == 7 && reacquired.cell_count() == 7,
        "renamed full-calc formula audit saved reacquire missing query noop-save should preserve sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 4, 4,
        "renamed full-calc formula audit saved reacquire missing query noop-save should preserve shifted bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula =
        sheet.try_cell("D3");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == shifted_formula &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire missing query noop-save should preserve the shifted styled formula");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A4").text_value() == "extra-c3",
        "renamed full-calc formula audit saved reacquire missing query noop-save should preserve shifted source rows");
    check(!sheet.try_cell("A2").has_value() &&
            !reacquired.try_cell("D2").has_value() &&
            !reacquired.try_cell("C5").has_value(),
        "renamed full-calc formula audit saved reacquire missing query noop-save should keep unstaged coordinates absent");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> clean_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire missing query noop-save clean audit");
    check(clean_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire missing query noop-save should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire missing query noop-save shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire missing query noop-save shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire missing query noop-save source audit");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire missing query noop-save audits should keep diagnostics clear");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire missing query noop-save second save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire missing query noop-save second save should not record another materialized handoff");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), expected_summaries),
        "renamed full-calc formula audit saved reacquire missing query noop-save second save should preserve edit summaries");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire missing query noop-save second save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire missing query noop-save second save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire missing query noop-save second save should keep diagnostics clear");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names &&
            workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire missing query noop-save second save should preserve catalog diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit saved reacquire missing query noop-save second save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit saved reacquire missing query noop-save second save");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire missing query noop-save second save should leave the source package unchanged");
    check(second_entries == first_entries,
        "renamed full-calc formula audit saved reacquire missing query noop-save second output should match the pre-query save");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire missing query noop-save second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire missing query noop-save second output should omit the source catalog name");
    check_not_contains(second_workbook_xml, R"(name="Missing")",
        "renamed full-calc formula audit saved reacquire missing query noop-save second output should omit rejected sheet names");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire missing query noop-save second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire missing query noop-save second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "renamed full-calc formula audit saved reacquire missing query noop-save second output should keep shifted bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire missing query noop-save second output should keep shifted formula with style");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire missing query noop-save second output should omit old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="C5")",
        "renamed full-calc formula audit saved reacquire missing query noop-save second output should not invent recovery cells");

    check_public_state_renamed_full_calc_noop_formula_audit_source_rows_readback(
        editor, second_output, shifted_formula, styled_formula_style,
        "renamed full-calc formula audit saved reacquire missing query noop-save");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_option_mismatch_recovery()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-option-mismatch-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-option-mismatch-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-option-mismatch-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-option-mismatch-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire option mismatch first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire option mismatch first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire option mismatch first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch first save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire option mismatch first save should leave the source package unchanged");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire option mismatch reacquire should start clean");

    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 2;
    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("RenamedData", mismatched_options);
    }), "renamed full-calc formula audit saved reacquire option mismatch try_worksheet should reject different options");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("RenamedData", mismatched_options);
    }), "renamed full-calc formula audit saved reacquire option mismatch worksheet should reject different options");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch should keep the old source name unavailable");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire option mismatch");

    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch should keep last_edit_error clear");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire option mismatch should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire option mismatch should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire option mismatch should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire option mismatch should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed full-calc formula audit saved reacquire option mismatch should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire option mismatch should preserve the planned workbook catalog");
    check(sheet.cell_count() == 7 && reacquired.cell_count() == 7,
        "renamed full-calc formula audit saved reacquire option mismatch should preserve sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 4, 4,
        "renamed full-calc formula audit saved reacquire option mismatch should preserve shifted bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula =
        sheet.try_cell("D3");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == shifted_formula &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire option mismatch should preserve the shifted styled formula");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A4").text_value() == "extra-c3",
        "renamed full-calc formula audit saved reacquire option mismatch should preserve shifted source rows");
    check(!sheet.try_cell("A2").has_value() && !reacquired.try_cell("D2").has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch should keep old coordinates absent");
    check(!reacquired.try_cell("C5").has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch should not stage recovery cells before mutation");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> clean_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire option mismatch clean audit");
    check(clean_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire option mismatch should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire option mismatch shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire option mismatch shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire option mismatch source audit");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch audits should keep diagnostics clear");

    reacquired.set_cell(5, 3, fastxlsx::CellValue::text("option-mismatch-recovery-c5"));
    const std::size_t recovery_memory = reacquired.estimated_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch recovery should keep diagnostics clear");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire option mismatch recovery should dirty both handles");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed full-calc formula audit saved reacquire option mismatch recovery should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 8,
        "renamed full-calc formula audit saved reacquire option mismatch recovery should grow sparse count");
    check_public_state_renamed_dirty_materialized_summary_memory(
        editor, sheet, reacquired, 8, recovery_memory,
        "renamed full-calc formula audit saved reacquire option mismatch recovery");
    const std::optional<fastxlsx::CellValue> recovered_cell =
        sheet.try_cell("C5");
    check(recovered_cell.has_value() &&
            recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            recovered_cell->text_value() == "option-mismatch-recovery-c5",
        "renamed full-calc formula audit saved reacquire option mismatch recovery should expose the valid text cell");
    const std::optional<fastxlsx::CellValue> recovered_formula =
        reacquired.try_cell("D3");
    check(recovered_formula.has_value() &&
            recovered_formula->kind() == fastxlsx::CellValueKind::Formula &&
            recovered_formula->text_value() == shifted_formula &&
            recovered_formula->has_style() &&
            recovered_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire option mismatch recovery should preserve shifted formula");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> dirty_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire option mismatch recovery dirty audit");
    check(dirty_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire option mismatch recovery should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire option mismatch recovery shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire option mismatch recovery shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire option mismatch recovery source audit");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire option mismatch recovery save should clean both handles");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire option mismatch recovery save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire option mismatch recovery save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch recovery save should keep diagnostics clear");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire option mismatch first output should keep fullCalcOnLoad");
    check(first_entries.find("xl/calcChain.xml") == first_entries.end(),
        "renamed full-calc formula audit saved reacquire option mismatch first output should not invent calcChain.xml");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "renamed full-calc formula audit saved reacquire option mismatch first output should keep shifted bounds");
    check_not_contains(first_worksheet_xml, "option-mismatch-recovery-c5",
        "renamed full-calc formula audit saved reacquire option mismatch first output should omit the later recovery cell");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire option mismatch recovery save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire option mismatch second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire option mismatch second output should omit the source catalog name");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire option mismatch second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire option mismatch second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed full-calc formula audit saved reacquire option mismatch second output should project recovered bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire option mismatch second output should keep shifted formula with style");
    check_contains(second_worksheet_xml, "option-mismatch-recovery-c5",
        "renamed full-calc formula audit saved reacquire option mismatch second output should write recovered text");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire option mismatch second output should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        second_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire option mismatch second output");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    const std::optional<fastxlsx::CellValue> reopened_recovered_cell =
        reopened_sheet.try_cell("C5");
    check(reopened_recovered_cell.has_value() &&
            reopened_recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            reopened_recovered_cell->text_value() == "option-mismatch-recovery-c5",
        "renamed full-calc formula audit saved reacquire option mismatch reopened output should read recovered text");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire option mismatch no-op save should keep both handles clean");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire option mismatch no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire option mismatch no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire option mismatch no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit saved reacquire option mismatch no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit saved reacquire option mismatch no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed full-calc formula audit saved reacquire option mismatch no-op output should match the recovery output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire option mismatch no-op save should leave the source package unchanged");
    check_public_state_renamed_full_calc_noop_formula_audit_readback(
        editor, noop_output, shifted_formula, styled_formula_style,
        "option-mismatch-recovery-c5",
        "renamed full-calc formula audit saved reacquire option mismatch");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_option_mismatch_noop_save()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-option-mismatch-noop-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-option-mismatch-noop-save-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-option-mismatch-noop-save-second-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save first save should keep diagnostics clear");
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save first save should leave the source package unchanged");
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> expected_summaries =
        editor.pending_worksheet_edits();

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save reacquire should start clean");

    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 2;
    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("RenamedData", mismatched_options);
    }), "renamed full-calc formula audit saved reacquire option mismatch noop-save try_worksheet should reject different options");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("RenamedData", mismatched_options);
    }), "renamed full-calc formula audit saved reacquire option mismatch noop-save worksheet should reject different options");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should keep the old source name unavailable");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire option mismatch noop-save");

    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should keep last_edit_error clear");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should not add materialized handoffs");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), expected_summaries),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should preserve saved edit summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire option mismatch noop-save should not queue replacement diagnostics");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should not dirty materialized diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should preserve the planned workbook catalog");
    check(sheet.cell_count() == 7 && reacquired.cell_count() == 7,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should preserve sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 4, 4,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should preserve shifted bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula =
        sheet.try_cell("D3");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == shifted_formula &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should preserve the shifted styled formula");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A4").text_value() == "extra-c3",
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should preserve shifted source rows");
    check(!sheet.try_cell("A2").has_value() &&
            !reacquired.try_cell("D2").has_value() &&
            !reacquired.try_cell("C5").has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should keep unstaged coordinates absent");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> clean_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire option mismatch noop-save clean audit");
    check(clean_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire option mismatch noop-save shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire option mismatch noop-save shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire option mismatch noop-save source audit");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save audits should keep diagnostics clear");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second save should not record another materialized handoff");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), expected_summaries),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second save should preserve edit summaries");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire option mismatch noop-save second save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second save should keep diagnostics clear");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names &&
            workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second save should preserve catalog diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second save");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second save should leave the source package unchanged");
    check(second_entries == first_entries,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second output should match the pre-option save");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second output should omit the source catalog name");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second output should keep shifted bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second output should keep shifted formula with style");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second output should omit old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="C5")",
        "renamed full-calc formula audit saved reacquire option mismatch noop-save second output should not invent recovery cells");

    check_public_state_renamed_full_calc_noop_formula_audit_source_rows_readback(
        editor, second_output, shifted_formula, styled_formula_style,
        "renamed full-calc formula audit saved reacquire option mismatch noop-save");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_same_sheet_guard_recovery()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-same-sheet-guard-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-same-sheet-guard-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-same-sheet-guard-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-same-sheet-guard-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire same-sheet guard first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire same-sheet guard first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire same-sheet guard first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire same-sheet guard first save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire same-sheet guard first save should leave the source package unchanged");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire same-sheet guard reacquire should start clean");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire same-sheet guard should keep the old source name unavailable");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire same-sheet guard");

    const std::optional<std::string> guard_error =
        check_public_same_sheet_rename_then_replacement_guard_sequence(
            editor, "RenamedData", "BlockedData", "blocked-same-sheet-replacement",
            "renamed full-calc formula audit saved reacquire same-sheet guard");

    check(editor.last_edit_error() == guard_error,
        "renamed full-calc formula audit saved reacquire same-sheet guard should retain replacement guard diagnostic");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire same-sheet guard should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire same-sheet guard should not add public edits");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire same-sheet guard should not queue replacement diagnostics");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire same-sheet guard should not dirty materialized diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed full-calc formula audit saved reacquire same-sheet guard should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire same-sheet guard should preserve the planned workbook catalog");
    check(editor.has_worksheet("RenamedData") &&
            !editor.has_worksheet("Data") &&
            !editor.has_worksheet("BlockedData"),
        "renamed full-calc formula audit saved reacquire same-sheet guard should not expose rejected sheet names");
    check(sheet.cell_count() == 7 && reacquired.cell_count() == 7,
        "renamed full-calc formula audit saved reacquire same-sheet guard should preserve sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 4, 4,
        "renamed full-calc formula audit saved reacquire same-sheet guard should preserve shifted bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula =
        sheet.try_cell("D3");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == shifted_formula &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire same-sheet guard should preserve the shifted styled formula");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A4").text_value() == "extra-c3",
        "renamed full-calc formula audit saved reacquire same-sheet guard should preserve shifted source rows");
    check(!sheet.try_cell("A2").has_value() && !reacquired.try_cell("D2").has_value(),
        "renamed full-calc formula audit saved reacquire same-sheet guard should keep old coordinates absent");
    check(!reacquired.try_cell("C5").has_value(),
        "renamed full-calc formula audit saved reacquire same-sheet guard should not stage recovery cells before mutation");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> clean_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire same-sheet guard clean audit");
    check(clean_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire same-sheet guard should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire same-sheet guard shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire same-sheet guard shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire same-sheet guard source audit");
    check(editor.last_edit_error() == guard_error,
        "renamed full-calc formula audit saved reacquire same-sheet guard audits should preserve guard diagnostic");

    reacquired.set_cell(5, 3, fastxlsx::CellValue::text("same-sheet-guard-recovery-c5"));
    const std::size_t recovery_memory = reacquired.estimated_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery should clear diagnostics");
    check(sheet.has_pending_changes() && reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery should dirty both handles");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 8,
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery should grow sparse count");
    check_public_state_renamed_dirty_materialized_summary_memory(
        editor, sheet, reacquired, 8, recovery_memory,
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery");
    const std::optional<fastxlsx::CellValue> recovered_cell =
        sheet.try_cell("C5");
    check(recovered_cell.has_value() &&
            recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            recovered_cell->text_value() == "same-sheet-guard-recovery-c5",
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery should expose the valid text cell");
    const std::optional<fastxlsx::CellValue> recovered_formula =
        reacquired.try_cell("D3");
    check(recovered_formula.has_value() &&
            recovered_formula->kind() == fastxlsx::CellValueKind::Formula &&
            recovered_formula->text_value() == shifted_formula &&
            recovered_formula->has_style() &&
            recovered_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery should preserve shifted formula");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> dirty_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire same-sheet guard recovery dirty audit");
    check(dirty_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        dirty_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire same-sheet guard recovery source audit");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery save should clean both handles");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery save should keep diagnostics clear");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire same-sheet guard first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="BlockedData")",
        "renamed full-calc formula audit saved reacquire same-sheet guard first output should omit rejected sheet name");
    check_contains(first_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire same-sheet guard first output should keep fullCalcOnLoad");
    check(first_entries.find("xl/calcChain.xml") == first_entries.end(),
        "renamed full-calc formula audit saved reacquire same-sheet guard first output should not invent calcChain.xml");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "renamed full-calc formula audit saved reacquire same-sheet guard first output should keep shifted bounds");
    check_not_contains(first_worksheet_xml, "blocked-same-sheet-replacement",
        "renamed full-calc formula audit saved reacquire same-sheet guard first output should omit rejected replacement payload");
    check_not_contains(first_worksheet_xml, "same-sheet-guard-recovery-c5",
        "renamed full-calc formula audit saved reacquire same-sheet guard first output should omit the later recovery cell");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire same-sheet guard second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire same-sheet guard second output should omit the source catalog name");
    check_not_contains(second_workbook_xml, R"(name="BlockedData")",
        "renamed full-calc formula audit saved reacquire same-sheet guard second output should omit rejected sheet name");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire same-sheet guard second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire same-sheet guard second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed full-calc formula audit saved reacquire same-sheet guard second output should project recovered bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire same-sheet guard second output should keep shifted formula with style");
    check_contains(second_worksheet_xml, "same-sheet-guard-recovery-c5",
        "renamed full-calc formula audit saved reacquire same-sheet guard second output should write recovered text");
    check_not_contains(second_worksheet_xml, "blocked-same-sheet-replacement",
        "renamed full-calc formula audit saved reacquire same-sheet guard second output should omit rejected replacement payload");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire same-sheet guard second output should omit old formula coordinate");

    check_public_state_reopened_shift_formula_audit_output(
        second_output, "D3", 3, 4, shifted_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire same-sheet guard second output");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    const std::optional<fastxlsx::CellValue> reopened_recovered_cell =
        reopened_sheet.try_cell("C5");
    check(reopened_recovered_cell.has_value() &&
            reopened_recovered_cell->kind() == fastxlsx::CellValueKind::Text &&
            reopened_recovered_cell->text_value() == "same-sheet-guard-recovery-c5",
        "renamed full-calc formula audit saved reacquire same-sheet guard reopened output should read recovered text");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire same-sheet guard no-op save should keep both handles clean");
    check(editor.pending_change_count() == 4,
        "renamed full-calc formula audit saved reacquire same-sheet guard no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire same-sheet guard no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed full-calc formula audit saved reacquire same-sheet guard no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire same-sheet guard no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit saved reacquire same-sheet guard no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit saved reacquire same-sheet guard no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed full-calc formula audit saved reacquire same-sheet guard no-op output should match the recovery output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire same-sheet guard no-op save should leave the source package unchanged");
    check_public_state_renamed_full_calc_noop_formula_audit_readback(
        editor, noop_output, shifted_formula, styled_formula_style,
        "same-sheet-guard-recovery-c5",
        "renamed full-calc formula audit saved reacquire same-sheet guard recovery");
}

void test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_same_sheet_guard_noop_save()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-same-sheet-guard-noop-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-same-sheet-guard-noop-save-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-renamed-full-calc-formula-audit-saved-reacquire-same-sheet-guard-noop-save-second-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    editor.request_full_calculation();
    const std::vector<std::string> expected_source_names =
        editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names =
        editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 1);

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save first save should clean the original handle");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save first save should count rename, metadata, and materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save first save should clear dirty diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save first save should keep diagnostics clear");
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save first save should leave the source package unchanged");
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> expected_summaries =
        editor.pending_worksheet_edits();

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save reacquire should start clean");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should keep the old source name unavailable");
    check_workbook_editor_renamed_formula_full_calc_saved_reacquire_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire same-sheet guard noop-save");

    const std::optional<std::string> guard_error =
        check_public_same_sheet_rename_then_replacement_guard_sequence(
            editor, "RenamedData", "BlockedData", "blocked-same-sheet-noop-replacement",
            "renamed full-calc formula audit saved reacquire same-sheet guard noop-save");

    check(editor.last_edit_error() == guard_error,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should retain replacement guard diagnostic");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should not add public edits");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), expected_summaries),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should preserve saved edit summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should not queue replacement diagnostics");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should not dirty materialized diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should preserve the planned workbook catalog");
    check(editor.has_worksheet("RenamedData") &&
            !editor.has_worksheet("Data") &&
            !editor.has_worksheet("BlockedData"),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should not expose rejected sheet names");
    check(sheet.cell_count() == 7 && reacquired.cell_count() == 7,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should preserve sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 4, 4,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should preserve shifted bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula =
        sheet.try_cell("D3");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == shifted_formula &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should preserve the shifted styled formula");
    check(sheet.get_cell("A3").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A4").text_value() == "extra-c3",
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should preserve shifted source rows");
    check(!sheet.try_cell("A2").has_value() &&
            !reacquired.try_cell("D2").has_value() &&
            !reacquired.try_cell("C5").has_value(),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should keep unstaged coordinates absent");

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> clean_audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed full-calc formula audit saved reacquire same-sheet guard noop-save clean audit");
    check(clean_audits.size() == 2,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save should report both shifted references");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!A1", "A1",
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        clean_audits, 3, 4, shifted_formula, "Data!B1", "B1",
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed full-calc formula audit saved reacquire same-sheet guard noop-save source audit");
    check(editor.last_edit_error() == guard_error,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save audits should preserve guard diagnostic");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second save should keep both handles clean");
    check(editor.pending_change_count() == 3,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second save should not record another materialized handoff");
    check(workbook_editor_edit_summaries_equal(
            editor.pending_worksheet_edits(), expected_summaries),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second save should preserve edit summaries");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second save should not queue replacement diagnostics");
    check(editor.last_edit_error() == guard_error,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second save should preserve the guard diagnostic");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names &&
            workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second save should preserve catalog diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second save");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second save should leave the source package unchanged");
    check(second_entries == first_entries,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second output should match the pre-guard save");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second output should omit the source catalog name");
    check_not_contains(second_workbook_xml, R"(name="BlockedData")",
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second output should omit rejected sheet name");
    check_contains(second_workbook_xml, R"(fullCalcOnLoad="1")",
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second output should keep fullCalcOnLoad");
    check(second_entries.find("xl/calcChain.xml") == second_entries.end(),
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second output should not invent calcChain.xml");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second output should keep shifted bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second output should keep shifted formula with style");
    check_not_contains(second_worksheet_xml, "blocked-same-sheet-noop-replacement",
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second output should omit rejected replacement payload");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second output should omit old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="C5")",
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save second output should not invent recovery cells");

    check_public_state_renamed_full_calc_noop_formula_audit_source_rows_readback(
        editor, second_output, shifted_formula, styled_formula_style,
        "renamed full-calc formula audit saved reacquire same-sheet guard noop-save");
}

void test_public_worksheet_editor_shift_after_rename_uses_planned_name()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        "shift after rename should expose only the planned sheet name");
    check(editor.pending_change_count() == 1,
        "shift after rename should count the catalog rename before materialized edits");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0,
        "shift after rename should not dirty materialized diagnostics before materialization");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    check(sheet.name() == "RenamedData",
        "shift after rename materialized handle should expose the planned name");
    sheet.insert_rows(2, 1);
    const std::size_t shifted_memory = sheet.estimated_memory_usage();

    check(sheet.has_pending_changes(),
        "shift after rename insert_rows should dirty the planned-name materialized handle");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "shift after rename should report dirty materialized state under the planned name");
    check(editor.pending_materialized_cell_count() == 3,
        "shift after rename should expose the shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "shift after rename should expose the shifted materialized memory estimate");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "shift after rename should expose one combined summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" &&
                    summaries[0].planned_name == "RenamedData" &&
                    summaries[0].renamed &&
                    summaries[0].materialized_dirty,
                "shift after rename summary should combine rename and materialized dirty state");
            check(summaries[0].materialized_cell_count == 3,
                "shift after rename summary should report shifted sparse count");
            check(summaries[0].estimated_materialized_memory_usage == shifted_memory,
                "shift after rename summary should report shifted memory estimate");
        }
    }
    check(sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift after rename should shift source-backed cells through the planned-name handle");
    check(!sheet.try_cell("A2").has_value(),
        "shift after rename should remove old source coordinates");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "shift after rename save_as should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "shift after rename save_as should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "shift after rename save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift after rename save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(workbook_xml, R"(name="RenamedData")",
        "shift after rename save_as should write the planned catalog name");
    check_not_contains(workbook_xml, R"(name="Data")",
        "shift after rename save_as should not restore the source catalog name");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "shift after rename save_as should project shifted sparse bounds");
    check_contains(worksheet_xml, R"(<c r="B1"><v>1</v></c>)",
        "shift after rename save_as should preserve B1");
    check_contains(worksheet_xml, R"(<c r="A3")",
        "shift after rename save_as should write shifted source A2");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "shift after rename save_as should omit old A2");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "shift after rename reopened output should expose the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "shift after rename reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "shift after rename reopened output should not expose pending diagnostics");
    check(reopened_sheet.cell_count() == 3,
        "shift after rename reopened output should keep sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 2,
        "shift after rename reopened output should expose shifted bounds");
    check(reopened_sheet.get_cell("A3").text_value() == "placeholder-a2",
        "shift after rename reopened output should read shifted source A2");
    check(!reopened_sheet.try_cell("A2").has_value(),
        "shift after rename reopened output should keep old source coordinate absent");

    check_public_state_renamed_clean_noop_save(
        editor, sheet, noop_output, output_entries, "shift after rename", 2,
        [](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 3,
                "shift after rename no-op save reopened output should keep sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 3, 2,
                "shift after rename no-op save reopened output should expose shifted bounds");
            check(noop_sheet.get_cell("A3").text_value() == "placeholder-a2",
                "shift after rename no-op save reopened output should read shifted source A2");
            check(!noop_sheet.try_cell("A2").has_value(),
                "shift after rename no-op save reopened output should keep old source coordinate absent");
        });
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "shift after rename no-op save should leave the source package unchanged");
}

void test_public_worksheet_editor_shift_after_rename_preserves_formula_style()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
        editor, "renamed formula shift");
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 2);
    const std::size_t shifted_memory = sheet.estimated_memory_usage();

    check(sheet.cell_count() == 7,
        "renamed formula shift should preserve sparse count while shifting records");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D4");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "A1+B1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula shift should translate formula text and preserve style id");
    check(sheet.get_cell("A4").text_value() == "placeholder-a2" &&
            sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula shift should move source-backed rows through the planned name");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A2").has_value(),
        "renamed formula shift should keep old shifted coordinates absent");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula shift should report dirty materialized state under the planned name");
    check(editor.pending_materialized_cell_count() == 7,
        "renamed formula shift should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "renamed formula shift should report shifted materialized memory estimate");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "renamed formula shift save_as should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula shift save_as should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula shift save_as should clear dirty materialized diagnostics");
    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula shift post-save reacquire should reuse a clean saved session");
    const std::optional<fastxlsx::CellValue> reacquired_d4 =
        reacquired.try_cell("D4");
    check(reacquired_d4.has_value() &&
            reacquired_d4->kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_d4->text_value() == "A1+B1" &&
            reacquired_d4->has_style() &&
            reacquired_d4->style_id().value() == styled_formula_style.value(),
        "renamed formula shift post-save reacquire should read translated styled formula");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula shift");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula shift save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(workbook_xml, R"(name="RenamedData")",
        "renamed formula shift save_as should write the planned catalog name");
    check_not_contains(workbook_xml, R"(name="Data")",
        "renamed formula shift save_as should omit the source catalog name");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed formula shift save_as should project shifted bounds");
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed formula shift save_as should write translated formula with style id");
    check_contains(worksheet_xml, R"(<c r="A4")",
        "renamed formula shift save_as should write shifted source A2");
    check_contains(worksheet_xml, R"(<c r="A5")",
        "renamed formula shift save_as should write shifted source trailing row");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "renamed formula shift save_as should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A2")",
        "renamed formula shift save_as should omit old source row coordinate");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula shift reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula shift reopened output should start clean");
    check(reopened_sheet.cell_count() == 7,
        "renamed formula shift reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 4,
        "renamed formula shift reopened output should expose shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_d4 =
        reopened_sheet.try_cell("D4");
    check(reopened_d4.has_value() &&
            reopened_d4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_d4->text_value() == "A1+B1" &&
            reopened_d4->has_style() &&
            reopened_d4->style_id().value() == styled_formula_style.value(),
        "renamed formula shift reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A4").text_value() == "placeholder-a2" &&
            reopened_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula shift reopened output should read shifted source rows");
    check(!reopened_sheet.try_cell("D2").has_value() &&
            !reopened_sheet.try_cell("A2").has_value(),
        "renamed formula shift reopened output should keep old coordinates absent");

    check_public_state_renamed_clean_noop_save(
        editor, sheet, noop_output, output_entries, "renamed formula shift", 2,
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 7,
                "renamed formula shift no-op save reopened output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 5, 4,
                "renamed formula shift no-op save reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> noop_d4 =
                noop_sheet.try_cell("D4");
            check(noop_d4.has_value() &&
                    noop_d4->kind() == fastxlsx::CellValueKind::Formula &&
                    noop_d4->text_value() == "A1+B1" &&
                    noop_d4->has_style() &&
                    noop_d4->style_id().value() == styled_formula_style.value(),
                "renamed formula shift no-op save reopened output should read translated styled formula");
            check(noop_sheet.get_cell("A4").text_value() == "placeholder-a2" &&
                    noop_sheet.get_cell("A5").text_value() == "extra-c3",
                "renamed formula shift no-op save reopened output should read shifted source rows");
            check(!noop_sheet.try_cell("D2").has_value() &&
                    !noop_sheet.try_cell("A2").has_value(),
                "renamed formula shift no-op save reopened output should keep old coordinates absent");
        });
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula shift no-op save should leave the source package unchanged");
}

void test_public_worksheet_editor_shift_after_rename_formula_audits_use_shifted_formula()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-audit-source.xlsx",
            styled_formula_style);
    const std::filesystem::path equivalent_source =
        source.parent_path() / "." / source.filename();
    const std::filesystem::path missing_parent_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-audit-missing-parent") /
        "out.xlsx";
    const std::filesystem::path file_parent =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-audit-file-parent");
    const std::filesystem::path non_directory_output = file_parent / "out.xlsx";
    const std::filesystem::path directory_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-audit-directory-output");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-audit-noop-output.xlsx");
    std::filesystem::remove_all(missing_parent_output.parent_path());
    std::filesystem::remove_all(file_parent);
    fastxlsx::test::write_file(file_parent, "not a directory");
    std::filesystem::remove_all(directory_output);
    std::filesystem::create_directories(directory_output);
    const auto source_entries_before_save_preflights =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
        editor, "renamed formula audit shift");
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 2);

    constexpr std::string_view expected_formula = "Data!A1+Data!B1";
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D4");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == expected_formula &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula audit shift should expose the translated styled formula");

    const std::vector<std::string> materialized_names_before_audit =
        editor.pending_materialized_worksheet_names();
    const std::size_t materialized_count_before_audit =
        editor.pending_materialized_cell_count();
    const std::size_t materialized_memory_before_audit =
        editor.estimated_pending_materialized_memory_usage();
    check(materialized_memory_before_audit == sheet.estimated_memory_usage(),
        "renamed formula audit shifted row should snapshot shifted materialized memory");
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed formula audit shift");
    check(audits.size() == 2,
        "renamed formula audit shift should report both shifted sheet-qualified references");

    check_public_state_renamed_shift_formula_audit(
        audits, 4, 4, expected_formula, "Data!A1", "A1",
        "renamed formula audit shifted A reference");
    check_public_state_renamed_shift_formula_audit(
        audits, 4, 4, expected_formula, "Data!B1", "B1",
        "renamed formula audit shifted B reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row source scan");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "renamed formula audit shifted row should reject source overwrite");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row rejected source-overwrite source scan");
    check(threw_fastxlsx_error([&] { editor.save_as(equivalent_source); }),
        "renamed formula audit shifted row should reject path-equivalent source overwrite");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row rejected path-equivalent source scan");
    check(threw_fastxlsx_error([&] { editor.save_as(std::filesystem::path()); }),
        "renamed formula audit shifted row should reject empty output path");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row rejected empty-output source scan");
    check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
        "renamed formula audit shifted row should reject missing output parent");
    check(!std::filesystem::exists(missing_parent_output),
        "renamed formula audit shifted row should not create rejected missing-parent output");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row rejected missing-parent source scan");
    check(threw_fastxlsx_error([&] { editor.save_as(non_directory_output); }),
        "renamed formula audit shifted row should reject non-directory output parent");
    check(std::filesystem::is_regular_file(file_parent) &&
            fastxlsx::test::read_file(file_parent) == "not a directory",
        "renamed formula audit shifted row should preserve non-directory parent file");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row rejected non-directory parent source scan");
    check(threw_fastxlsx_error([&] { editor.save_as(directory_output); }),
        "renamed formula audit shifted row should reject existing directory output");
    check(std::filesystem::is_directory(directory_output),
        "renamed formula audit shifted row should preserve rejected output directory");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row rejected directory-output source scan");
    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 2;
    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("RenamedData", mismatched_options);
    }), "renamed formula audit shifted row should reject mismatched try_worksheet options");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("RenamedData", mismatched_options);
    }), "renamed formula audit shifted row should reject mismatched worksheet options");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row option-mismatch source scan");
    check(!editor.try_worksheet("Missing").has_value(),
        "renamed formula audit shifted row should keep missing optional lookup empty");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula audit shifted row should keep old source-name optional lookup empty");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Missing"); }),
        "renamed formula audit shifted row should reject missing worksheet lookup");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Data"); }),
        "renamed formula audit shifted row should reject old source-name worksheet lookup");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row missing-query source scan");
    fastxlsx::WorksheetEditorOptions failing_materialization_options;
    failing_materialization_options.max_cells = 1;
    const std::optional<std::string> last_error_before_materialization_failure =
        editor.last_edit_error();
    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("Untouched", failing_materialization_options);
    }), "renamed formula audit shifted row should reject guardrail try_worksheet materialization");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Untouched", failing_materialization_options);
    }), "renamed formula audit shifted row should reject guardrail worksheet materialization");
    check(editor.last_edit_error() == last_error_before_materialization_failure,
        "renamed formula audit shifted row materialization failure should preserve last_edit_error");
    check(editor.pending_materialized_worksheet_names() == materialized_names_before_audit &&
            editor.pending_materialized_cell_count() == materialized_count_before_audit &&
            editor.estimated_pending_materialized_memory_usage() == materialized_memory_before_audit,
        "renamed formula audit shifted row materialization failure should preserve dirty materialized diagnostics");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row materialization-failure source scan");
    fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
    check(!untouched.has_pending_changes(),
        "renamed formula audit shifted row materialization recovery should stay clean");
    check(untouched.get_cell("A1").text_value() == "keep-me" &&
            untouched.get_cell("B1").number_value() == 99.0,
        "renamed formula audit shifted row materialization recovery should read untouched source cells");
    check(editor.pending_materialized_worksheet_names() == materialized_names_before_audit &&
            editor.pending_materialized_cell_count() == materialized_count_before_audit &&
            editor.estimated_pending_materialized_memory_usage() == materialized_memory_before_audit,
        "renamed formula audit shifted row materialization recovery should preserve dirty materialized diagnostics");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row materialization-recovery source scan");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "renamed formula audit shifted row should reject row-zero try_cell");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell("XFE1"); }),
        "renamed formula audit shifted row should reject A1 column overflow get_cell");
    check(threw_fastxlsx_error([&] { (void)sheet.sparse_cells("B2:A1"); }),
        "renamed formula audit shifted row should reject reversed sparse_cells range");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row invalid-read source scan");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1,
            fastxlsx::CellValue::formula("invalid-renamed-formula-row-zero"));
    }), "renamed formula audit shifted row should reject row-zero set_cell");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell("XFE1",
            fastxlsx::CellValue::formula("invalid-renamed-formula-a1-overflow"));
    }), "renamed formula audit shifted row should reject A1 column overflow set_cell");
    check(threw_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "renamed formula audit shifted row should reject range erase_cell references");
    check(editor.last_edit_error().has_value(),
        "renamed formula audit shifted row invalid mutations should populate last_edit_error");
    check(sheet.has_pending_changes() &&
            editor.pending_materialized_cell_count() == materialized_count_before_audit &&
            editor.estimated_pending_materialized_memory_usage() == materialized_memory_before_audit,
        "renamed formula audit shifted row invalid mutations should preserve dirty materialized diagnostics");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row invalid-mutation source scan");
    check(threw_fastxlsx_error([&] { sheet.insert_rows(0, 1); }),
        "renamed formula audit shifted row should reject invalid insert_rows start");
    check(threw_fastxlsx_error([&] { sheet.delete_rows(1048576, 2); }),
        "renamed formula audit shifted row should reject invalid delete_rows count range");
    check(sheet.has_pending_changes() &&
            editor.pending_materialized_cell_count() == materialized_count_before_audit &&
            editor.estimated_pending_materialized_memory_usage() == materialized_memory_before_audit,
        "renamed formula audit shifted row invalid shifts should preserve dirty materialized diagnostics");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed formula audit shifted row invalid-shift source scan");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "renamed formula audit shift save_as should clean the planned-name handle");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula audit shift post-save reacquire should reuse a clean saved session");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula audit shift post-save reacquire should keep materialized diagnostics clean");
    check_public_state_renamed_insert_formula_saved_reacquire_audit(
        editor, 4, 4, expected_formula, "Data!A1", "A1", "Data!B1", "B1",
        "renamed formula audit shift");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed formula audit shift save_as should write the shifted qualified formula");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_save_preflights,
        "renamed formula audit shift safe save should leave source package bytes unchanged");
    check(!std::filesystem::exists(missing_parent_output),
        "renamed formula audit shift safe save should keep rejected missing-parent output absent");
    check(std::filesystem::is_regular_file(file_parent) &&
            fastxlsx::test::read_file(file_parent) == "not a directory",
        "renamed formula audit shift safe save should preserve non-directory parent file");
    check(!std::filesystem::exists(non_directory_output),
        "renamed formula audit shift safe save should not create non-directory child output");
    check(std::filesystem::is_directory(directory_output),
        "renamed formula audit shift safe save should preserve rejected output directory");
    check_public_state_reopened_shift_formula_audit_output(
        output, "D4", 4, 4, expected_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed formula audit shifted row reopened output");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    check(save_state_before_noop.last_edit_error.has_value(),
        "renamed formula audit shifted row save_as should preserve the diagnostic before no-op");
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula audit shifted row no-op save should keep materialized handles clean");
    check(editor.pending_change_count() == 2,
        "renamed formula audit shifted row no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula audit shifted row no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed formula audit shifted row no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed formula audit shifted row no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed formula audit shifted row no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "renamed formula audit shifted row no-op output should match the first materialized output");
    check_public_state_renamed_insert_formula_noop_audit_readback(
        editor, noop_output, "D4", 4, 4, expected_formula, styled_formula_style,
        "Data!A1", "A1", "Data!B1", "B1",
        "renamed formula audit shifted row no-op save");
}

void test_public_worksheet_editor_shift_after_rename_column_formula_audits_use_shifted_formula()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-column-formula-audit-source.xlsx",
            styled_formula_style);
    const std::filesystem::path equivalent_source =
        source.parent_path() / "." / source.filename();
    const std::filesystem::path missing_parent_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-column-formula-audit-missing-parent") /
        "out.xlsx";
    const std::filesystem::path file_parent =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-column-formula-audit-file-parent");
    const std::filesystem::path non_directory_output = file_parent / "out.xlsx";
    const std::filesystem::path directory_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-column-formula-audit-directory-output");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-column-formula-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-column-formula-audit-noop-output.xlsx");
    std::filesystem::remove_all(missing_parent_output.parent_path());
    std::filesystem::remove_all(file_parent);
    fastxlsx::test::write_file(file_parent, "not a directory");
    std::filesystem::remove_all(directory_output);
    std::filesystem::create_directories(directory_output);
    const auto source_entries_before_save_preflights =
        fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
        editor, "renamed column formula audit shift");
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_columns(2, 1);

    constexpr std::string_view expected_formula = "Data!A1+Data!C1";
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E2");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == expected_formula &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed column formula audit shift should expose the translated styled formula");

    const std::vector<std::string> materialized_names_before_audit =
        editor.pending_materialized_worksheet_names();
    const std::size_t materialized_count_before_audit =
        editor.pending_materialized_cell_count();
    const std::size_t materialized_memory_before_audit =
        editor.estimated_pending_materialized_memory_usage();
    check(materialized_memory_before_audit == sheet.estimated_memory_usage(),
        "renamed column formula audit shifted should snapshot shifted materialized memory");
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "renamed column formula audit shift");
    check(audits.size() == 2,
        "renamed column formula audit shift should report both shifted sheet-qualified references");
    check_public_state_renamed_shift_formula_audit(
        audits, 2, 5, expected_formula, "Data!A1", "A1",
        "renamed column formula audit shifted B reference");
    check_public_state_renamed_shift_formula_audit(
        audits, 2, 5, expected_formula, "Data!C1", "C1",
        "renamed column formula audit shifted C reference");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted source scan");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "renamed column formula audit shifted should reject source overwrite");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted rejected source-overwrite source scan");
    check(threw_fastxlsx_error([&] { editor.save_as(equivalent_source); }),
        "renamed column formula audit shifted should reject path-equivalent source overwrite");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted rejected path-equivalent source scan");
    check(threw_fastxlsx_error([&] { editor.save_as(std::filesystem::path()); }),
        "renamed column formula audit shifted should reject empty output path");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted rejected empty-output source scan");
    check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
        "renamed column formula audit shifted should reject missing output parent");
    check(!std::filesystem::exists(missing_parent_output),
        "renamed column formula audit shifted should not create rejected missing-parent output");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted rejected missing-parent source scan");
    check(threw_fastxlsx_error([&] { editor.save_as(non_directory_output); }),
        "renamed column formula audit shifted should reject non-directory output parent");
    check(std::filesystem::is_regular_file(file_parent) &&
            fastxlsx::test::read_file(file_parent) == "not a directory",
        "renamed column formula audit shifted should preserve non-directory parent file");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted rejected non-directory parent source scan");
    check(threw_fastxlsx_error([&] { editor.save_as(directory_output); }),
        "renamed column formula audit shifted should reject existing directory output");
    check(std::filesystem::is_directory(directory_output),
        "renamed column formula audit shifted should preserve rejected output directory");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted rejected directory-output source scan");
    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 2;
    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("RenamedData", mismatched_options);
    }), "renamed column formula audit shifted should reject mismatched try_worksheet options");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("RenamedData", mismatched_options);
    }), "renamed column formula audit shifted should reject mismatched worksheet options");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted option-mismatch source scan");
    check(!editor.try_worksheet("Missing").has_value(),
        "renamed column formula audit shifted should keep missing optional lookup empty");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed column formula audit shifted should keep old source-name optional lookup empty");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Missing"); }),
        "renamed column formula audit shifted should reject missing worksheet lookup");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Data"); }),
        "renamed column formula audit shifted should reject old source-name worksheet lookup");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted missing-query source scan");
    fastxlsx::WorksheetEditorOptions failing_materialization_options;
    failing_materialization_options.max_cells = 1;
    const std::optional<std::string> last_error_before_materialization_failure =
        editor.last_edit_error();
    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("Untouched", failing_materialization_options);
    }), "renamed column formula audit shifted should reject guardrail try_worksheet materialization");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Untouched", failing_materialization_options);
    }), "renamed column formula audit shifted should reject guardrail worksheet materialization");
    check(editor.last_edit_error() == last_error_before_materialization_failure,
        "renamed column formula audit shifted materialization failure should preserve last_edit_error");
    check(editor.pending_materialized_worksheet_names() == materialized_names_before_audit &&
            editor.pending_materialized_cell_count() == materialized_count_before_audit &&
            editor.estimated_pending_materialized_memory_usage() == materialized_memory_before_audit,
        "renamed column formula audit shifted materialization failure should preserve dirty materialized diagnostics");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted materialization-failure source scan");
    fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
    check(!untouched.has_pending_changes(),
        "renamed column formula audit shifted materialization recovery should stay clean");
    check(untouched.get_cell("A1").text_value() == "keep-me" &&
            untouched.get_cell("B1").number_value() == 99.0,
        "renamed column formula audit shifted materialization recovery should read untouched source cells");
    check(editor.pending_materialized_worksheet_names() == materialized_names_before_audit &&
            editor.pending_materialized_cell_count() == materialized_count_before_audit &&
            editor.estimated_pending_materialized_memory_usage() == materialized_memory_before_audit,
        "renamed column formula audit shifted materialization recovery should preserve dirty materialized diagnostics");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted materialization-recovery source scan");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "renamed column formula audit shifted should reject row-zero try_cell");
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell("XFE1"); }),
        "renamed column formula audit shifted should reject A1 column overflow get_cell");
    check(threw_fastxlsx_error([&] { (void)sheet.sparse_cells("B2:A1"); }),
        "renamed column formula audit shifted should reject reversed sparse_cells range");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted invalid-read source scan");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1,
            fastxlsx::CellValue::formula("invalid-renamed-column-formula-row-zero"));
    }), "renamed column formula audit shifted should reject row-zero set_cell");
    check(threw_fastxlsx_error([&] {
        sheet.set_cell("XFE1",
            fastxlsx::CellValue::formula("invalid-renamed-column-formula-a1-overflow"));
    }), "renamed column formula audit shifted should reject A1 column overflow set_cell");
    check(threw_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "renamed column formula audit shifted should reject range erase_cell references");
    check(editor.last_edit_error().has_value(),
        "renamed column formula audit shifted invalid mutations should populate last_edit_error");
    check(sheet.has_pending_changes() &&
            editor.pending_materialized_cell_count() == materialized_count_before_audit &&
            editor.estimated_pending_materialized_memory_usage() == materialized_memory_before_audit,
        "renamed column formula audit shifted invalid mutations should preserve dirty materialized diagnostics");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted invalid-mutation source scan");
    check(threw_fastxlsx_error([&] { sheet.insert_columns(0, 1); }),
        "renamed column formula audit shifted should reject invalid insert_columns start");
    check(threw_fastxlsx_error([&] { sheet.delete_columns(16384, 2); }),
        "renamed column formula audit shifted should reject invalid delete_columns count range");
    check(sheet.has_pending_changes() &&
            editor.pending_materialized_cell_count() == materialized_count_before_audit &&
            editor.estimated_pending_materialized_memory_usage() == materialized_memory_before_audit,
        "renamed column formula audit shifted invalid shifts should preserve dirty materialized diagnostics");
    check_public_state_source_formula_audit_preserves_shift_fixture(
        editor, "renamed column formula audit shifted invalid-shift source scan");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "renamed column formula audit shift save_as should clean the planned-name handle");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed column formula audit shift post-save reacquire should reuse a clean saved session");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed column formula audit shift post-save reacquire should keep materialized diagnostics clean");
    check_public_state_renamed_insert_formula_saved_reacquire_audit(
        editor, 2, 5, expected_formula, "Data!A1", "A1", "Data!C1", "C1",
        "renamed column formula audit shift");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="E2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!C1</f></c>)";
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed column formula audit shift save_as should write the shifted qualified formula");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_save_preflights,
        "renamed column formula audit shift safe save should leave source package bytes unchanged");
    check(!std::filesystem::exists(missing_parent_output),
        "renamed column formula audit shift safe save should keep rejected missing-parent output absent");
    check(std::filesystem::is_regular_file(file_parent) &&
            fastxlsx::test::read_file(file_parent) == "not a directory",
        "renamed column formula audit shift safe save should preserve non-directory parent file");
    check(!std::filesystem::exists(non_directory_output),
        "renamed column formula audit shift safe save should not create non-directory child output");
    check(std::filesystem::is_directory(directory_output),
        "renamed column formula audit shift safe save should preserve rejected output directory");
    check_public_state_reopened_shift_formula_audit_output(
        output, "E2", 2, 5, expected_formula, styled_formula_style,
        "Data!A1", "A1", "Data!C1", "C1",
        "renamed column formula audit shifted output");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    check(save_state_before_noop.last_edit_error.has_value(),
        "renamed column formula audit shifted save_as should preserve the diagnostic before no-op");
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed column formula audit shifted no-op save should keep materialized handles clean");
    check(editor.pending_change_count() == 2,
        "renamed column formula audit shifted no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed column formula audit shifted no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "renamed column formula audit shifted no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_noop,
        "renamed column formula audit shifted no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_noop,
        "renamed column formula audit shifted no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "renamed column formula audit shifted no-op output should match the first materialized output");
    check_public_state_renamed_insert_formula_noop_audit_readback(
        editor, noop_output, "E2", 2, 5, expected_formula, styled_formula_style,
        "Data!A1", "A1", "Data!C1", "C1",
        "renamed column formula audit shifted no-op save");
}

void test_public_worksheet_editor_shift_after_rename_delete_formula_audits_skip_ref_tokens()
{
    {
        fastxlsx::StyleId styled_formula_style;
        const std::filesystem::path source =
            write_two_sheet_source_with_qualified_delete_formula(
                "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-audit-source.xlsx",
                styled_formula_style);
        const std::filesystem::path equivalent_source =
            source.parent_path() / "." / source.filename();
        const std::filesystem::path missing_parent_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-audit-missing-parent") /
            "out.xlsx";
        const std::filesystem::path file_parent =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-audit-file-parent");
        const std::filesystem::path non_directory_output = file_parent / "out.xlsx";
        const std::filesystem::path directory_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-audit-directory-output");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-audit-output.xlsx");
        const std::filesystem::path noop_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-audit-noop-output.xlsx");
        std::filesystem::remove_all(missing_parent_output.parent_path());
        std::filesystem::remove_all(file_parent);
        fastxlsx::test::write_file(file_parent, "not a directory");
        std::filesystem::remove_all(directory_output);
        std::filesystem::create_directories(directory_output);
        const auto source_entries_before_save_preflights =
            fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

        editor.rename_sheet("Data", "RenamedData");
        check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
            editor, "renamed delete-row formula audit");
        fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
        sheet.delete_rows(1, 1);

        constexpr std::string_view expected_formula = "Data!#REF!+Data!B1";
        const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D1");
        check(shifted_formula.has_value() &&
                shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula->text_value() == expected_formula &&
                shifted_formula->has_style() &&
                shifted_formula->style_id().value() == styled_formula_style.value(),
            "renamed delete-row formula audit should expose the translated styled formula");

        const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
            check_public_state_formula_audits_preserve_editor_diagnostics(
                editor, "renamed delete-row formula audit");
        check(audits.size() == 1,
            "renamed delete-row formula audit should skip the translated #REF! token");
        check(find_public_state_formula_audit(audits, 1, 4, "Data!#REF!") == nullptr,
            "renamed delete-row formula audit should not expose Data!#REF! as a reference");
        check_public_state_renamed_shift_formula_audit(
            audits, 1, 4, expected_formula, "Data!B1", "B1",
            "renamed delete-row formula audit shifted surviving B reference");

        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-row formula audit shifted source scan");
        check(threw_fastxlsx_error([&] { editor.save_as(source); }),
            "renamed delete-row formula audit should reject source overwrite");
        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-row formula audit rejected source-overwrite source scan");
        check(threw_fastxlsx_error([&] { editor.save_as(equivalent_source); }),
            "renamed delete-row formula audit should reject path-equivalent source overwrite");
        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-row formula audit rejected path-equivalent source scan");
        check(threw_fastxlsx_error([&] { editor.save_as(std::filesystem::path()); }),
            "renamed delete-row formula audit should reject empty output path");
        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-row formula audit rejected empty-output source scan");
        check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
            "renamed delete-row formula audit should reject missing output parent");
        check(!std::filesystem::exists(missing_parent_output),
            "renamed delete-row formula audit should not create rejected missing-parent output");
        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-row formula audit rejected missing-parent source scan");
        check(threw_fastxlsx_error([&] { editor.save_as(non_directory_output); }),
            "renamed delete-row formula audit should reject non-directory output parent");
        check(std::filesystem::is_regular_file(file_parent) &&
                fastxlsx::test::read_file(file_parent) == "not a directory",
            "renamed delete-row formula audit should preserve non-directory parent file");
        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-row formula audit rejected non-directory parent source scan");
        check(threw_fastxlsx_error([&] { editor.save_as(directory_output); }),
            "renamed delete-row formula audit should reject existing directory output");
        check(std::filesystem::is_directory(directory_output),
            "renamed delete-row formula audit should preserve rejected output directory");
        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-row formula audit rejected directory-output source scan");

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "renamed delete-row formula audit save_as should clean the planned-name handle");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0,
            "renamed delete-row formula audit save_as should clear dirty materialized diagnostics");

        fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
        check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
            "renamed delete-row formula audit reacquire should reuse a clean saved session");
        check_public_state_renamed_delete_formula_saved_reacquire_audit(
            editor, 1, 4, expected_formula, "Data!B1", "B1",
            "renamed delete-row formula audit");

        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        const std::string styled_formula_xml =
            std::string(R"(<c r="D1" s=")")
            + std::to_string(styled_formula_style.value())
            + R"("><f>Data!#REF!+Data!B1</f></c>)";
        check_contains(worksheet_xml, styled_formula_xml,
            "renamed delete-row formula audit save_as should write the #REF! formula");
        check(fastxlsx::test::read_zip_entries(source) == source_entries_before_save_preflights,
            "renamed delete-row formula audit safe save should leave source package bytes unchanged");
        check(!std::filesystem::exists(missing_parent_output),
            "renamed delete-row formula audit safe save should keep rejected missing-parent output absent");
        check(std::filesystem::is_regular_file(file_parent) &&
                fastxlsx::test::read_file(file_parent) == "not a directory",
            "renamed delete-row formula audit safe save should preserve non-directory parent file");
        check(!std::filesystem::exists(non_directory_output),
            "renamed delete-row formula audit safe save should not create non-directory child output");
        check(std::filesystem::is_directory(directory_output),
            "renamed delete-row formula audit safe save should preserve rejected output directory");
        check_public_state_reopened_delete_formula_audit_output(
            output, "D1", 1, 4, expected_formula, styled_formula_style,
            "Data!B1", "B1",
            "renamed delete-row formula audit reopened output surviving B reference");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
            "renamed delete-row formula audit no-op save should keep materialized handles clean");
        check(editor.pending_change_count() == 2,
            "renamed delete-row formula audit no-op save should not record another materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "renamed delete-row formula audit no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "renamed delete-row formula audit no-op save should not queue replacement diagnostics");
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_noop,
            "renamed delete-row formula audit no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_noop,
            "renamed delete-row formula audit no-op save");
        check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
            "renamed delete-row formula audit no-op output should match the first materialized output");
        check_public_state_renamed_delete_formula_noop_audit_readback(
            editor, noop_output, "D1", 1, 4, expected_formula, styled_formula_style,
            "Data!B1", "B1",
            "renamed delete-row formula audit no-op save surviving B reference");
    }

    {
        fastxlsx::StyleId styled_formula_style;
        const std::filesystem::path source =
            write_two_sheet_source_with_qualified_delete_formula(
                "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-audit-source.xlsx",
                styled_formula_style);
        const std::filesystem::path equivalent_source =
            source.parent_path() / "." / source.filename();
        const std::filesystem::path missing_parent_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-audit-missing-parent") /
            "out.xlsx";
        const std::filesystem::path file_parent =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-audit-file-parent");
        const std::filesystem::path non_directory_output = file_parent / "out.xlsx";
        const std::filesystem::path directory_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-audit-directory-output");
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-audit-output.xlsx");
        const std::filesystem::path noop_output =
            artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-audit-noop-output.xlsx");
        std::filesystem::remove_all(missing_parent_output.parent_path());
        std::filesystem::remove_all(file_parent);
        fastxlsx::test::write_file(file_parent, "not a directory");
        std::filesystem::remove_all(directory_output);
        std::filesystem::create_directories(directory_output);
        const auto source_entries_before_save_preflights =
            fastxlsx::test::read_zip_entries(source);

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

        editor.rename_sheet("Data", "RenamedData");
        check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
            editor, "renamed delete-column formula audit");
        fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
        sheet.delete_columns(1, 1);

        constexpr std::string_view expected_formula = "Data!#REF!+Data!A2";
        const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("C2");
        check(shifted_formula.has_value() &&
                shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula->text_value() == expected_formula &&
                shifted_formula->has_style() &&
                shifted_formula->style_id().value() == styled_formula_style.value(),
            "renamed delete-column formula audit should expose the translated styled formula");

        const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
            check_public_state_formula_audits_preserve_editor_diagnostics(
                editor, "renamed delete-column formula audit");
        check(audits.size() == 1,
            "renamed delete-column formula audit should skip the translated #REF! token");
        check(find_public_state_formula_audit(audits, 2, 3, "Data!#REF!") == nullptr,
            "renamed delete-column formula audit should not expose Data!#REF! as a reference");
        check_public_state_renamed_shift_formula_audit(
            audits, 2, 3, expected_formula, "Data!A2", "A2",
            "renamed delete-column formula audit shifted surviving A reference");

        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-column formula audit shifted source scan");
        check(threw_fastxlsx_error([&] { editor.save_as(source); }),
            "renamed delete-column formula audit should reject source overwrite");
        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-column formula audit rejected source-overwrite source scan");
        check(threw_fastxlsx_error([&] { editor.save_as(equivalent_source); }),
            "renamed delete-column formula audit should reject path-equivalent source overwrite");
        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-column formula audit rejected path-equivalent source scan");
        check(threw_fastxlsx_error([&] { editor.save_as(std::filesystem::path()); }),
            "renamed delete-column formula audit should reject empty output path");
        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-column formula audit rejected empty-output source scan");
        check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
            "renamed delete-column formula audit should reject missing output parent");
        check(!std::filesystem::exists(missing_parent_output),
            "renamed delete-column formula audit should not create rejected missing-parent output");
        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-column formula audit rejected missing-parent source scan");
        check(threw_fastxlsx_error([&] { editor.save_as(non_directory_output); }),
            "renamed delete-column formula audit should reject non-directory output parent");
        check(std::filesystem::is_regular_file(file_parent) &&
                fastxlsx::test::read_file(file_parent) == "not a directory",
            "renamed delete-column formula audit should preserve non-directory parent file");
        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-column formula audit rejected non-directory parent source scan");
        check(threw_fastxlsx_error([&] { editor.save_as(directory_output); }),
            "renamed delete-column formula audit should reject existing directory output");
        check(std::filesystem::is_directory(directory_output),
            "renamed delete-column formula audit should preserve rejected output directory");
        check_public_state_delete_formula_source_audit_preserves_shift_fixture(
            editor, "renamed delete-column formula audit rejected directory-output source scan");

        editor.save_as(output);
        check(!sheet.has_pending_changes(),
            "renamed delete-column formula audit save_as should clean the planned-name handle");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0,
            "renamed delete-column formula audit save_as should clear dirty materialized diagnostics");

        fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
        check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
            "renamed delete-column formula audit reacquire should reuse a clean saved session");
        check_public_state_renamed_delete_formula_saved_reacquire_audit(
            editor, 2, 3, expected_formula, "Data!A2", "A2",
            "renamed delete-column formula audit");

        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        const std::string styled_formula_xml =
            std::string(R"(<c r="C2" s=")")
            + std::to_string(styled_formula_style.value())
            + R"("><f>Data!#REF!+Data!A2</f></c>)";
        check_contains(worksheet_xml, styled_formula_xml,
            "renamed delete-column formula audit save_as should write the #REF! formula");
        check(fastxlsx::test::read_zip_entries(source) == source_entries_before_save_preflights,
            "renamed delete-column formula audit safe save should leave source package bytes unchanged");
        check(!std::filesystem::exists(missing_parent_output),
            "renamed delete-column formula audit safe save should keep rejected missing-parent output absent");
        check(std::filesystem::is_regular_file(file_parent) &&
                fastxlsx::test::read_file(file_parent) == "not a directory",
            "renamed delete-column formula audit safe save should preserve non-directory parent file");
        check(!std::filesystem::exists(non_directory_output),
            "renamed delete-column formula audit safe save should not create non-directory child output");
        check(std::filesystem::is_directory(directory_output),
            "renamed delete-column formula audit safe save should preserve rejected output directory");
        check_public_state_reopened_delete_formula_audit_output(
            output, "C2", 2, 3, expected_formula, styled_formula_style,
            "Data!A2", "A2",
            "renamed delete-column formula audit reopened output surviving A reference");

        const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
            workbook_editor_public_catalog_snapshot(editor);
        const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
            workbook_editor_public_save_state_snapshot(editor);
        editor.save_as(noop_output);
        check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
            "renamed delete-column formula audit no-op save should keep materialized handles clean");
        check(editor.pending_change_count() == 2,
            "renamed delete-column formula audit no-op save should not record another materialized handoff");
        check(editor.pending_materialized_worksheet_names().empty() &&
                editor.pending_materialized_cell_count() == 0 &&
                editor.estimated_pending_materialized_memory_usage() == 0,
            "renamed delete-column formula audit no-op save should keep dirty diagnostics clear");
        check_workbook_editor_no_replacement_diagnostics(
            editor, "renamed delete-column formula audit no-op save should not queue replacement diagnostics");
        check_workbook_editor_public_save_state_preserved(
            editor,
            save_state_before_noop,
            "renamed delete-column formula audit no-op save");
        check_workbook_editor_public_catalog_preserved(
            editor,
            catalog_before_noop,
            "renamed delete-column formula audit no-op save");
        check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
            "renamed delete-column formula audit no-op output should match the first materialized output");
        check_public_state_renamed_delete_formula_noop_audit_readback(
            editor, noop_output, "C2", 2, 3, expected_formula, styled_formula_style,
            "Data!A2", "A2",
            "renamed delete-column formula audit no-op save surviving A reference");
    }
}

void test_public_worksheet_editor_shift_after_rename_preserves_column_formula_style()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-column-formula-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-column-formula-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-column-formula-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
        editor, "renamed column formula shift");
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_columns(2, 1);
    const std::size_t shifted_memory = sheet.estimated_memory_usage();

    check(sheet.cell_count() == 7,
        "renamed column formula shift should preserve sparse count while shifting records");
    check_cell_range_equals(sheet.used_range(), 1, 1, 3, 5,
        "renamed column formula shift should expose shifted bounds");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E2");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "A1+C1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed column formula shift should translate formula text and preserve style id");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("C1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "renamed column formula shift should move source B1 to C1");
    check(sheet.get_cell("C2").text_value() == "row2-gap-b2" &&
            sheet.get_cell("D2").text_value() == "row2-gap-c2" &&
            sheet.get_cell("A3").text_value() == "extra-c3",
        "renamed column formula shift should move source-backed columns through the planned name");
    check(!sheet.try_cell("B1").has_value() && !sheet.try_cell("B2").has_value(),
        "renamed column formula shift should keep inserted blank column coordinates absent");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed column formula shift should report dirty materialized state under the planned name");
    check(editor.pending_materialized_cell_count() == 7,
        "renamed column formula shift should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "renamed column formula shift should report shifted materialized memory estimate");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "renamed column formula shift save_as should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed column formula shift save_as should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed column formula shift save_as should clear dirty materialized diagnostics");
    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed column formula shift post-save reacquire should reuse a clean saved session");
    const std::optional<fastxlsx::CellValue> reacquired_e2 =
        reacquired.try_cell("E2");
    check(reacquired_e2.has_value() &&
            reacquired_e2->kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_e2->text_value() == "A1+C1" &&
            reacquired_e2->has_style() &&
            reacquired_e2->style_id().value() == styled_formula_style.value(),
        "renamed column formula shift post-save reacquire should read translated styled formula");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed column formula shift");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed column formula shift save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="E2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+C1</f></c>)";
    check_contains(workbook_xml, R"(name="RenamedData")",
        "renamed column formula shift save_as should write the planned catalog name");
    check_not_contains(workbook_xml, R"(name="Data")",
        "renamed column formula shift save_as should omit the source catalog name");
    check_contains(worksheet_xml, R"(<dimension ref="A1:E3"/>)",
        "renamed column formula shift save_as should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
        "renamed column formula shift save_as should write shifted B1");
    check_contains(worksheet_xml, R"(<c r="C2")",
        "renamed column formula shift save_as should write shifted B2");
    check_contains(worksheet_xml, R"(<c r="D2")",
        "renamed column formula shift save_as should write shifted C2");
    check_contains(worksheet_xml, R"(<c r="A3")",
        "renamed column formula shift save_as should keep trailing source row");
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed column formula shift save_as should write translated formula with style id");
    check_not_contains(worksheet_xml, R"(r="B1")",
        "renamed column formula shift save_as should omit inserted blank B1");
    check_not_contains(worksheet_xml, R"(r="B2")",
        "renamed column formula shift save_as should omit inserted blank B2");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed column formula shift reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed column formula shift reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed column formula shift reopened output should not expose pending diagnostics");
    check(reopened_sheet.cell_count() == 7,
        "renamed column formula shift reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 5,
        "renamed column formula shift reopened output should expose shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_e2 =
        reopened_sheet.try_cell("E2");
    check(reopened_e2.has_value() &&
            reopened_e2->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_e2->text_value() == "A1+C1" &&
            reopened_e2->has_style() &&
            reopened_e2->style_id().value() == styled_formula_style.value(),
        "renamed column formula shift reopened output should read translated styled formula");
    const fastxlsx::CellValue reopened_c1 = reopened_sheet.get_cell("C1");
    check(reopened_c1.kind() == fastxlsx::CellValueKind::Number &&
            reopened_c1.number_value() == 1.0,
        "renamed column formula shift reopened output should read shifted B1");
    check(reopened_sheet.get_cell("C2").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("D2").text_value() == "row2-gap-c2" &&
            reopened_sheet.get_cell("A3").text_value() == "extra-c3",
        "renamed column formula shift reopened output should read shifted source columns");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("B2").has_value(),
        "renamed column formula shift reopened output should keep inserted blank column absent");

    check_public_state_renamed_clean_noop_save(
        editor, sheet, noop_output, output_entries, "renamed column formula shift", 2,
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 7,
                "renamed column formula shift no-op save reopened output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 3, 5,
                "renamed column formula shift no-op save reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> noop_e2 =
                noop_sheet.try_cell("E2");
            check(noop_e2.has_value() &&
                    noop_e2->kind() == fastxlsx::CellValueKind::Formula &&
                    noop_e2->text_value() == "A1+C1" &&
                    noop_e2->has_style() &&
                    noop_e2->style_id().value() == styled_formula_style.value(),
                "renamed column formula shift no-op save reopened output should read translated styled formula");
            const fastxlsx::CellValue noop_c1 = noop_sheet.get_cell("C1");
            check(noop_c1.kind() == fastxlsx::CellValueKind::Number &&
                    noop_c1.number_value() == 1.0,
                "renamed column formula shift no-op save reopened output should read shifted B1");
            check(noop_sheet.get_cell("C2").text_value() == "row2-gap-b2" &&
                    noop_sheet.get_cell("D2").text_value() == "row2-gap-c2" &&
                    noop_sheet.get_cell("A3").text_value() == "extra-c3",
                "renamed column formula shift no-op save reopened output should read shifted source columns");
            check(!noop_sheet.try_cell("B1").has_value() &&
                    !noop_sheet.try_cell("B2").has_value(),
                "renamed column formula shift no-op save reopened output should keep inserted blank column absent");
        });
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed column formula shift no-op save should leave the source package unchanged");
}

void test_public_worksheet_editor_shift_after_rename_formula_reacquire_reuses_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-reacquire-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-reacquire-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-reacquire-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-reacquire-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
        editor, "renamed formula reacquire");
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 2);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula reacquire first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula reacquire first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula reacquire first save should clear dirty materialized diagnostics");

    std::optional<fastxlsx::WorksheetEditor> maybe_reacquired =
        editor.try_worksheet("RenamedData");
    check(maybe_reacquired.has_value(),
        "renamed formula reacquire should find the planned-name saved session");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula reacquire should keep the old source name unavailable");
    if (!maybe_reacquired.has_value()) {
        return;
    }

    fastxlsx::WorksheetEditor reacquired = std::move(*maybe_reacquired);
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula reacquire should return the saved clean styled session");
    const std::optional<fastxlsx::CellValue> reacquired_d4 =
        reacquired.try_cell("D4");
    check(reacquired_d4.has_value() &&
            reacquired_d4->kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_d4->text_value() == "A1+B1" &&
            reacquired_d4->has_style() &&
            reacquired_d4->style_id().value() == styled_formula_style.value(),
        "renamed formula reacquire should read the saved translated styled formula");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula reacquire");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula reacquire later column shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula reacquire later column shift should report dirty state under the planned name");
    check(editor.pending_materialized_cell_count() == 7,
        "renamed formula reacquire later column shift should keep the shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula reacquire later column shift should report shifted materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E4");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "A1+C1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula reacquire later column shift should translate and preserve style id");
    check(sheet.get_cell("C1").number_value() == 1.0 &&
            reacquired.get_cell("C1").number_value() == 1.0,
        "renamed formula reacquire later column shift should share shifted B1 across handles");
    check(sheet.get_cell("C4").text_value() == "row2-gap-b2" &&
            sheet.get_cell("D4").text_value() == "row2-gap-c2",
        "renamed formula reacquire later column shift should move source-backed row cells");
    check(!sheet.try_cell("B1").has_value() && !sheet.try_cell("B4").has_value(),
        "renamed formula reacquire later column shift should keep inserted blank coordinates absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula reacquire second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula reacquire second save should record the second materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula reacquire second save should clear dirty materialized diagnostics");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula reacquire first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula reacquire first output should write the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula reacquire first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed formula reacquire first output should contain only the row shift");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula reacquire first output should keep the row-shifted formula");
    check_not_contains(first_worksheet_xml, R"(r="E4")",
        "renamed formula reacquire first output should not include the later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula reacquire second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="E4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+C1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula reacquire second output should write the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula reacquire second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:E5"/>)",
        "renamed formula reacquire second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="C1"><v>1</v></c>)",
        "renamed formula reacquire second output should write shifted B1");
    check_contains(second_worksheet_xml, R"(<c r="C4")",
        "renamed formula reacquire second output should write shifted B2");
    check_contains(second_worksheet_xml, R"(<c r="D4")",
        "renamed formula reacquire second output should write shifted C2");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula reacquire second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "renamed formula reacquire second output should omit inserted blank B1");
    check_not_contains(second_worksheet_xml, R"(r="B4")",
        "renamed formula reacquire second output should omit inserted blank B4");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula reacquire no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula reacquire no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula reacquire no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula reacquire no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula reacquire no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula reacquire no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula reacquire no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula reacquire no-op save should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened_noop = fastxlsx::WorkbookEditor::open(noop_output);
    check(reopened_noop.has_worksheet("RenamedData") && !reopened_noop.has_worksheet("Data"),
        "renamed formula reacquire reopened no-op output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_noop_sheet = reopened_noop.worksheet("RenamedData");
    check(!reopened_noop.has_pending_changes() && !reopened_noop_sheet.has_pending_changes(),
        "renamed formula reacquire reopened no-op output should start clean");
    check(reopened_noop_sheet.cell_count() == 7,
        "renamed formula reacquire reopened no-op output should keep shifted sparse count");
    check_cell_range_equals(reopened_noop_sheet.used_range(), 1, 1, 5, 5,
        "renamed formula reacquire reopened no-op output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_noop_e4 =
        reopened_noop_sheet.try_cell("E4");
    check(reopened_noop_e4.has_value() &&
            reopened_noop_e4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_noop_e4->text_value() == "A1+C1" &&
            reopened_noop_e4->has_style() &&
            reopened_noop_e4->style_id().value() == styled_formula_style.value(),
        "renamed formula reacquire reopened no-op output should read translated styled formula");
    check(reopened_noop_sheet.get_cell("C1").number_value() == 1.0 &&
            reopened_noop_sheet.get_cell("C4").text_value() == "row2-gap-b2" &&
            reopened_noop_sheet.get_cell("D4").text_value() == "row2-gap-c2" &&
            reopened_noop_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula reacquire reopened no-op output should read shifted source cells");
    check(!reopened_noop_sheet.try_cell("B1").has_value() &&
            !reopened_noop_sheet.try_cell("B4").has_value() &&
            !reopened_noop_sheet.try_cell("D2").has_value(),
        "renamed formula reacquire reopened no-op output should keep old coordinates absent");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened_noop,
        "renamed formula reacquire reopened no-op output after shifted sparse reads");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula reacquire reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula reacquire reopened output should start clean");
    check(reopened_sheet.cell_count() == 7,
        "renamed formula reacquire reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 5,
        "renamed formula reacquire reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_e4 =
        reopened_sheet.try_cell("E4");
    check(reopened_e4.has_value() &&
            reopened_e4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_e4->text_value() == "A1+C1" &&
            reopened_e4->has_style() &&
            reopened_e4->style_id().value() == styled_formula_style.value(),
        "renamed formula reacquire reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("C1").number_value() == 1.0 &&
            reopened_sheet.get_cell("C4").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("D4").text_value() == "row2-gap-c2" &&
            reopened_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula reacquire reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("B4").has_value() &&
            !reopened_sheet.try_cell("D2").has_value(),
        "renamed formula reacquire reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_formula_failed_save_preserves_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-failed-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-failed-save-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-failed-save-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-failed-save-second-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();

    editor.rename_sheet("Data", "RenamedData");
    check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
        editor, "renamed formula failed save");
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");

    sheet.insert_rows(2, 2);
    const std::size_t shifted_memory = sheet.estimated_memory_usage();
    const auto check_dirty_styled_formula_session = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep last_edit_error clear");
        check(editor.has_pending_changes(),
            label + " should keep the public editor dirty");
        check(editor.pending_change_count() == 1,
            label + " should keep only the queued rename before materialized handoff");
        check(sheet.has_pending_changes(),
            label + " should keep the styled formula handle dirty");
        check(sheet.cell_count() == 7,
            label + " should preserve the shifted sparse count");
        check(sheet.estimated_memory_usage() == shifted_memory,
            label + " should preserve the shifted materialized memory estimate");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"RenamedData"},
            label + " should report dirty materialized state under the planned name");
        check(editor.pending_materialized_cell_count() == 7,
            label + " should report the shifted styled formula sparse count");
        check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            label + " should report the shifted styled formula memory estimate");
        check(editor.source_worksheet_names() == expected_source_names,
            label + " should preserve source worksheet names");
        check(editor.worksheet_names() == expected_planned_names,
            label + " should preserve planned worksheet names");
        check(workbook_editor_catalog_entries_equal(
                  editor.worksheet_catalog(), expected_catalog),
            label + " should preserve the renamed worksheet catalog");
        check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
            label + " should expose only the planned sheet name");

        const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D4");
        check(shifted_formula.has_value() &&
                shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula->text_value() == "A1+B1" &&
                shifted_formula->has_style() &&
                shifted_formula->style_id().value() == styled_formula_style.value(),
            label + " should keep the shifted styled formula in memory");
        check(sheet.get_cell("A4").text_value() == "placeholder-a2" &&
                sheet.get_cell("A5").text_value() == "extra-c3",
            label + " should keep shifted source rows in memory");
        check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A2").has_value(),
            label + " should keep old shifted coordinates absent");
    };

    check_dirty_styled_formula_session(
        "renamed formula failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "renamed formula failed save should reject exact source overwrite");
    check_dirty_styled_formula_session(
        "renamed formula failed save rejected source overwrite");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_workbook_xml = source_entries.at("xl/workbook.xml");
    const std::string source_worksheet_xml = source_entries.at("xl/worksheets/sheet1.xml");
    const std::string source_styled_formula_xml =
        std::string(R"(<c r="D2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(source_workbook_xml, R"(name="Data")",
        "renamed formula failed save should leave the source workbook catalog unchanged");
    check_not_contains(source_workbook_xml, R"(name="RenamedData")",
        "renamed formula failed save should not write the planned name into the source workbook");
    check_contains(source_worksheet_xml, R"(<dimension ref="A1:D3"/>)",
        "renamed formula failed save should leave the source workbook bounds unchanged");
    check_contains(source_worksheet_xml, source_styled_formula_xml,
        "renamed formula failed save should leave the source styled formula unchanged");
    check_contains(source_worksheet_xml, R"(<c r="A3" t="inlineStr"><is><t>extra-c3</t></is></c>)",
        "renamed formula failed save should leave the source trailing row unchanged");
    check_not_contains(source_worksheet_xml, R"(r="D4")",
        "renamed formula failed save should not write the row-shifted formula into the source workbook");
    check_not_contains(source_worksheet_xml, R"(r="A5")",
        "renamed formula failed save should not write shifted trailing rows into the source workbook");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "renamed formula failed save safe retry should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula failed save safe retry should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula failed save safe retry should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula failed save safe retry should keep diagnostics clear");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string output_workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string output_styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(output_workbook_xml, R"(name="RenamedData")",
        "renamed formula failed save safe retry should write the planned catalog name");
    check_not_contains(output_workbook_xml, R"(name="Data")",
        "renamed formula failed save safe retry should omit the source catalog name");
    check_contains(output_worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed formula failed save safe retry should write row-shifted bounds");
    check_contains(output_worksheet_xml, output_styled_formula_xml,
        "renamed formula failed save safe retry should write translated formula with style id");
    check_contains(output_worksheet_xml, R"(<c r="A4")",
        "renamed formula failed save safe retry should write shifted source row two");
    check_contains(output_worksheet_xml, R"(<c r="A5" t="inlineStr"><is><t>extra-c3</t></is></c>)",
        "renamed formula failed save safe retry should write shifted source row three");
    check_not_contains(output_worksheet_xml, R"(r="D2")",
        "renamed formula failed save safe retry should omit the old formula coordinate");
    check_not_contains(output_worksheet_xml, R"(r="A2")",
        "renamed formula failed save safe retry should omit the inserted blank row coordinate");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "renamed formula failed save no-op retry should keep the styled handle clean");
    check(editor.pending_change_count() == 2,
        "renamed formula failed save no-op retry should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula failed save no-op retry should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula failed save no-op retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula failed save no-op retry should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula failed save no-op retry");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula failed save no-op retry");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "renamed formula failed save no-op retry should keep output entries stable");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "renamed formula failed save second no-op retry should keep the styled handle clean");
    check(editor.pending_change_count() == 2,
        "renamed formula failed save second no-op retry should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula failed save second no-op retry should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula failed save second no-op retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula failed save second no-op retry should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "renamed formula failed save second no-op retry");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "renamed formula failed save second no-op retry");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula failed save second no-op retry should keep source entries unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "renamed formula failed save second no-op retry should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "renamed formula failed save second no-op retry should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "renamed formula failed save second no-op retry should keep output entries stable");
    fastxlsx::WorkbookEditor reopened_second_noop =
        fastxlsx::WorkbookEditor::open(second_noop_output);
    check(reopened_second_noop.has_worksheet("RenamedData") &&
            !reopened_second_noop.has_worksheet("Data"),
        "renamed formula failed save reopened second no-op output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_second_noop_sheet =
        reopened_second_noop.worksheet("RenamedData");
    check(!reopened_second_noop.has_pending_changes() &&
            !reopened_second_noop_sheet.has_pending_changes(),
        "renamed formula failed save reopened second no-op output should start clean");
    check(reopened_second_noop_sheet.cell_count() == 7,
        "renamed formula failed save reopened second no-op output should keep shifted sparse count");
    check_cell_range_equals(reopened_second_noop_sheet.used_range(), 1, 1, 5, 4,
        "renamed formula failed save reopened second no-op output should expose row-shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_second_noop_d4 =
        reopened_second_noop_sheet.try_cell("D4");
    check(reopened_second_noop_d4.has_value() &&
            reopened_second_noop_d4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_second_noop_d4->text_value() == "A1+B1" &&
            reopened_second_noop_d4->has_style() &&
            reopened_second_noop_d4->style_id().value() == styled_formula_style.value(),
        "renamed formula failed save reopened second no-op output should read translated styled formula");
    check(reopened_second_noop_sheet.get_cell("A4").text_value() == "placeholder-a2" &&
            reopened_second_noop_sheet.get_cell("B4").text_value() == "row2-gap-b2" &&
            reopened_second_noop_sheet.get_cell("C4").text_value() == "row2-gap-c2" &&
            reopened_second_noop_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula failed save reopened second no-op output should read shifted source cells");
    check(!reopened_second_noop_sheet.try_cell("D2").has_value() &&
            !reopened_second_noop_sheet.try_cell("A2").has_value(),
        "renamed formula failed save reopened second no-op output should keep old coordinates absent");

    fastxlsx::WorkbookEditor reopened_noop = fastxlsx::WorkbookEditor::open(noop_output);
    check(reopened_noop.has_worksheet("RenamedData") && !reopened_noop.has_worksheet("Data"),
        "renamed formula failed save reopened no-op output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_noop_sheet = reopened_noop.worksheet("RenamedData");
    check(!reopened_noop.has_pending_changes() && !reopened_noop_sheet.has_pending_changes(),
        "renamed formula failed save reopened no-op output should start clean");
    check(reopened_noop_sheet.cell_count() == 7,
        "renamed formula failed save reopened no-op output should keep shifted sparse count");
    check_cell_range_equals(reopened_noop_sheet.used_range(), 1, 1, 5, 4,
        "renamed formula failed save reopened no-op output should expose row-shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_noop_d4 =
        reopened_noop_sheet.try_cell("D4");
    check(reopened_noop_d4.has_value() &&
            reopened_noop_d4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_noop_d4->text_value() == "A1+B1" &&
            reopened_noop_d4->has_style() &&
            reopened_noop_d4->style_id().value() == styled_formula_style.value(),
        "renamed formula failed save reopened no-op output should read translated styled formula");
    check(reopened_noop_sheet.get_cell("A4").text_value() == "placeholder-a2" &&
            reopened_noop_sheet.get_cell("B4").text_value() == "row2-gap-b2" &&
            reopened_noop_sheet.get_cell("C4").text_value() == "row2-gap-c2" &&
            reopened_noop_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula failed save reopened no-op output should read shifted source cells");
    check(!reopened_noop_sheet.try_cell("D2").has_value() &&
            !reopened_noop_sheet.try_cell("A2").has_value(),
        "renamed formula failed save reopened no-op output should keep old coordinates absent");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened_noop,
        "renamed formula failed save reopened no-op output after shifted sparse reads");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula failed save reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula failed save reopened output should start clean");
    check(reopened_sheet.cell_count() == 7,
        "renamed formula failed save reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 4,
        "renamed formula failed save reopened output should expose row-shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_d4 =
        reopened_sheet.try_cell("D4");
    check(reopened_d4.has_value() &&
            reopened_d4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_d4->text_value() == "A1+B1" &&
            reopened_d4->has_style() &&
            reopened_d4->style_id().value() == styled_formula_style.value(),
        "renamed formula failed save reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A4").text_value() == "placeholder-a2" &&
            reopened_sheet.get_cell("B4").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("C4").text_value() == "row2-gap-c2" &&
            reopened_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula failed save reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("D2").has_value() &&
            !reopened_sheet.try_cell("A2").has_value(),
        "renamed formula failed save reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_formula_option_mismatch_preserves_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-options-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-options-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-options-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-options-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
        editor, "renamed formula option mismatch");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 2);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula option mismatch first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula option mismatch first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula option mismatch first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula option mismatch first save should keep diagnostics clear");

    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 2;
    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("RenamedData", mismatched_options);
    }), "renamed formula option mismatch try_worksheet should reject different options");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("RenamedData", mismatched_options);
    }), "renamed formula option mismatch worksheet should reject different options");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula option mismatch should keep the old source name unavailable");
    check(!editor.last_edit_error().has_value(),
        "renamed formula option mismatch should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        "renamed formula option mismatch should leave the saved planned-name handle clean");
    check(editor.pending_change_count() == 2,
        "renamed formula option mismatch should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula option mismatch should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula option mismatch should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula option mismatch should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula option mismatch should preserve the planned workbook catalog");
    const std::optional<fastxlsx::CellValue> saved_formula = sheet.try_cell("D4");
    check(saved_formula.has_value() &&
            saved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula->text_value() == "A1+B1" &&
            saved_formula->has_style() &&
            saved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula option mismatch should preserve the saved styled formula");
    check(sheet.get_cell("A4").text_value() == "placeholder-a2" &&
            sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula option mismatch should preserve shifted source rows");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A2").has_value(),
        "renamed formula option mismatch should keep old shifted coordinates absent");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula option mismatch matching reacquire should stay clean");
    check(reacquired.name() == "RenamedData",
        "renamed formula option mismatch matching reacquire should expose the planned name");
    const std::optional<fastxlsx::CellValue> reacquired_formula =
        reacquired.try_cell("D4");
    check(reacquired_formula.has_value() &&
            reacquired_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_formula->text_value() == "A1+B1" &&
            reacquired_formula->has_style() &&
            reacquired_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula option mismatch matching reacquire should reuse the saved styled formula");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula option mismatch");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula option mismatch later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula option mismatch later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 7,
        "renamed formula option mismatch later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula option mismatch later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E4");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "A1+C1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula option mismatch later shift should translate and preserve style id");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula option mismatch second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula option mismatch second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula option mismatch second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula option mismatch first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula option mismatch first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula option mismatch first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed formula option mismatch first output should keep row-shifted bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula option mismatch first output should keep the row-shifted styled formula");
    check_not_contains(first_worksheet_xml, R"(r="E4")",
        "renamed formula option mismatch first output should not include the later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula option mismatch second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="E4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+C1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula option mismatch second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula option mismatch second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:E5"/>)",
        "renamed formula option mismatch second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula option mismatch second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed formula option mismatch second output should omit the old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="B4")",
        "renamed formula option mismatch second output should omit inserted blank B4");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula option mismatch no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula option mismatch no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula option mismatch no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula option mismatch no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula option mismatch no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula option mismatch no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula option mismatch no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula option mismatch no-op save should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened_noop = fastxlsx::WorkbookEditor::open(noop_output);
    check(reopened_noop.has_worksheet("RenamedData") && !reopened_noop.has_worksheet("Data"),
        "renamed formula option mismatch reopened no-op output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_noop_sheet = reopened_noop.worksheet("RenamedData");
    check(!reopened_noop.has_pending_changes() && !reopened_noop_sheet.has_pending_changes(),
        "renamed formula option mismatch reopened no-op output should start clean");
    check(reopened_noop.pending_change_count() == 0 &&
            reopened_noop.pending_materialized_worksheet_names().empty() &&
            reopened_noop.pending_materialized_cell_count() == 0 &&
            reopened_noop.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula option mismatch reopened no-op output should not expose dirty diagnostics");
    check(reopened_noop_sheet.cell_count() == 7,
        "renamed formula option mismatch reopened no-op output should keep shifted sparse count");
    check_cell_range_equals(reopened_noop_sheet.used_range(), 1, 1, 5, 5,
        "renamed formula option mismatch reopened no-op output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_noop_e4 =
        reopened_noop_sheet.try_cell("E4");
    check(reopened_noop_e4.has_value() &&
            reopened_noop_e4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_noop_e4->text_value() == "A1+C1" &&
            reopened_noop_e4->has_style() &&
            reopened_noop_e4->style_id().value() == styled_formula_style.value(),
        "renamed formula option mismatch reopened no-op output should read translated styled formula");
    check(reopened_noop_sheet.get_cell("C1").number_value() == 1.0 &&
            reopened_noop_sheet.get_cell("C4").text_value() == "row2-gap-b2" &&
            reopened_noop_sheet.get_cell("D4").text_value() == "row2-gap-c2" &&
            reopened_noop_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula option mismatch reopened no-op output should read shifted source cells");
    check(!reopened_noop_sheet.try_cell("B1").has_value() &&
            !reopened_noop_sheet.try_cell("B4").has_value() &&
            !reopened_noop_sheet.try_cell("D2").has_value(),
        "renamed formula option mismatch reopened no-op output should keep old coordinates absent");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened_noop,
        "renamed formula option mismatch reopened no-op output after shifted sparse reads");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula option mismatch reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula option mismatch reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula option mismatch reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 7,
        "renamed formula option mismatch reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 5,
        "renamed formula option mismatch reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_e4 =
        reopened_sheet.try_cell("E4");
    check(reopened_e4.has_value() &&
            reopened_e4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_e4->text_value() == "A1+C1" &&
            reopened_e4->has_style() &&
            reopened_e4->style_id().value() == styled_formula_style.value(),
        "renamed formula option mismatch reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("C1").number_value() == 1.0 &&
            reopened_sheet.get_cell("C4").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("D4").text_value() == "row2-gap-c2" &&
            reopened_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula option mismatch reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("B4").has_value() &&
            !reopened_sheet.try_cell("D2").has_value(),
        "renamed formula option mismatch reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_formula_invalid_mutations_preserve_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-invalid-mutation-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-invalid-mutation-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-invalid-mutation-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-invalid-mutation-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
        editor, "renamed formula invalid mutations");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 2);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula invalid mutations first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula invalid mutations first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula invalid mutations first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula invalid mutations first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula invalid mutations matching reacquire should stay clean before failures");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula invalid mutations should keep the old source name unavailable");
    const std::optional<fastxlsx::CellValue> saved_formula = reacquired.try_cell("D4");
    check(saved_formula.has_value() &&
            saved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula->text_value() == "A1+B1" &&
            saved_formula->has_style() &&
            saved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula invalid mutations matching reacquire should read the saved styled formula");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1,
            fastxlsx::CellValue::formula("invalid-renamed-formula-row-zero"));
    }), "renamed formula invalid mutations should reject formula set_cell row zero");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell("XFE1",
            fastxlsx::CellValue::formula("invalid-renamed-formula-a1-overflow"));
    }), "renamed formula invalid mutations should reject formula set_cell column overflow");
    check(threw_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "renamed formula invalid mutations should reject range erase_cell references");

    check(editor.last_edit_error().has_value(),
        "renamed formula invalid mutations should populate last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(), "WorksheetEditor cell reference is invalid",
            "renamed formula invalid mutations should expose the latest invalid reference diagnostic");
    }
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula invalid mutations should keep both planned-name handles clean");
    check(editor.pending_change_count() == 2,
        "renamed formula invalid mutations should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula invalid mutations should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula invalid mutations should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula invalid mutations should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula invalid mutations should preserve the planned workbook catalog");
    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        "renamed formula invalid mutations should preserve planned-name lookup state");
    const std::optional<fastxlsx::CellValue> preserved_formula = sheet.try_cell("D4");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == "A1+B1" &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula invalid mutations should preserve the saved styled formula");
    check(sheet.get_cell("A4").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A5").text_value() == "extra-c3",
        "renamed formula invalid mutations should preserve shifted source cells");
    check(!sheet.try_cell("D2").has_value() && !reacquired.try_cell("A2").has_value(),
        "renamed formula invalid mutations should keep old shifted coordinates absent");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula invalid mutations");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "renamed formula invalid mutations later valid shift should clear diagnostics");
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula invalid mutations later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula invalid mutations later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 7,
        "renamed formula invalid mutations later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula invalid mutations later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E4");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "A1+C1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula invalid mutations later shift should translate and preserve style id");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula invalid mutations second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula invalid mutations second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula invalid mutations second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula invalid mutations first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula invalid mutations first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula invalid mutations first output should omit the source catalog name");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula invalid mutations first output should keep the row-shifted styled formula");
    check_not_contains(first_worksheet_xml, "invalid-renamed-formula-",
        "renamed formula invalid mutations first output should not contain rejected payloads");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula invalid mutations second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="E4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+C1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula invalid mutations second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula invalid mutations second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:E5"/>)",
        "renamed formula invalid mutations second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula invalid mutations second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, "invalid-renamed-formula-",
        "renamed formula invalid mutations second output should not leak rejected payloads");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed formula invalid mutations second output should omit the old formula coordinate");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula invalid mutations no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula invalid mutations no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula invalid mutations no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula invalid mutations no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula invalid mutations no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula invalid mutations no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula invalid mutations no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula invalid mutations no-op save should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened_noop = fastxlsx::WorkbookEditor::open(noop_output);
    check(reopened_noop.has_worksheet("RenamedData") && !reopened_noop.has_worksheet("Data"),
        "renamed formula invalid mutations reopened no-op output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_noop_sheet = reopened_noop.worksheet("RenamedData");
    check(!reopened_noop.has_pending_changes() && !reopened_noop_sheet.has_pending_changes(),
        "renamed formula invalid mutations reopened no-op output should start clean");
    check(reopened_noop.pending_change_count() == 0 &&
            reopened_noop.pending_materialized_worksheet_names().empty() &&
            reopened_noop.pending_materialized_cell_count() == 0 &&
            reopened_noop.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula invalid mutations reopened no-op output should not expose dirty diagnostics");
    check(reopened_noop_sheet.cell_count() == 7,
        "renamed formula invalid mutations reopened no-op output should keep shifted sparse count");
    check_cell_range_equals(reopened_noop_sheet.used_range(), 1, 1, 5, 5,
        "renamed formula invalid mutations reopened no-op output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_noop_e4 =
        reopened_noop_sheet.try_cell("E4");
    check(reopened_noop_e4.has_value() &&
            reopened_noop_e4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_noop_e4->text_value() == "A1+C1" &&
            reopened_noop_e4->has_style() &&
            reopened_noop_e4->style_id().value() == styled_formula_style.value(),
        "renamed formula invalid mutations reopened no-op output should read translated styled formula");
    check(reopened_noop_sheet.get_cell("C1").number_value() == 1.0 &&
            reopened_noop_sheet.get_cell("C4").text_value() == "row2-gap-b2" &&
            reopened_noop_sheet.get_cell("D4").text_value() == "row2-gap-c2" &&
            reopened_noop_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula invalid mutations reopened no-op output should read shifted source cells");
    check(!reopened_noop_sheet.try_cell("B1").has_value() &&
            !reopened_noop_sheet.try_cell("B4").has_value() &&
            !reopened_noop_sheet.try_cell("D2").has_value(),
        "renamed formula invalid mutations reopened no-op output should keep old coordinates absent");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened_noop,
        "renamed formula invalid mutations reopened no-op output after shifted sparse reads");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula invalid mutations reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula invalid mutations reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula invalid mutations reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 7,
        "renamed formula invalid mutations reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 5,
        "renamed formula invalid mutations reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_e4 =
        reopened_sheet.try_cell("E4");
    check(reopened_e4.has_value() &&
            reopened_e4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_e4->text_value() == "A1+C1" &&
            reopened_e4->has_style() &&
            reopened_e4->style_id().value() == styled_formula_style.value(),
        "renamed formula invalid mutations reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("C1").number_value() == 1.0 &&
            reopened_sheet.get_cell("C4").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("D4").text_value() == "row2-gap-c2" &&
            reopened_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula invalid mutations reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("B4").has_value() &&
            !reopened_sheet.try_cell("D2").has_value(),
        "renamed formula invalid mutations reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_formula_missing_query_preserves_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-missing-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-missing-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-missing-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-missing-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
        editor, "renamed formula missing query");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 2);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula missing query first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula missing query first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula missing query first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula missing query first save should keep diagnostics clear");

    check(!editor.try_worksheet("Missing").has_value(),
        "renamed formula missing query try_worksheet should report a missing sheet");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula missing query should keep the old source name unavailable");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Missing"); }),
        "renamed formula missing query worksheet should reject a missing sheet");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Data"); }),
        "renamed formula missing query worksheet should reject the old source name");
    check(!editor.last_edit_error().has_value(),
        "renamed formula missing query should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        "renamed formula missing query should leave the saved planned-name handle clean");
    check(editor.pending_change_count() == 2,
        "renamed formula missing query should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula missing query should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula missing query should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula missing query should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula missing query should preserve the planned workbook catalog");
    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        "renamed formula missing query should preserve planned-name lookup state");
    const std::optional<fastxlsx::CellValue> saved_formula = sheet.try_cell("D4");
    check(saved_formula.has_value() &&
            saved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula->text_value() == "A1+B1" &&
            saved_formula->has_style() &&
            saved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula missing query should preserve the saved styled formula");
    check(sheet.get_cell("A4").text_value() == "placeholder-a2" &&
            sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula missing query should preserve shifted source rows");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A2").has_value(),
        "renamed formula missing query should keep old shifted coordinates absent");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula missing query matching reacquire should stay clean");
    const std::optional<fastxlsx::CellValue> reacquired_formula =
        reacquired.try_cell("D4");
    check(reacquired_formula.has_value() &&
            reacquired_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_formula->text_value() == "A1+B1" &&
            reacquired_formula->has_style() &&
            reacquired_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula missing query matching reacquire should reuse saved styled formula");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula missing query");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula missing query later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula missing query later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 7,
        "renamed formula missing query later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula missing query later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E4");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "A1+C1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula missing query later shift should translate and preserve style id");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula missing query second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula missing query second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula missing query second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula missing query first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula missing query first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula missing query first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed formula missing query first output should keep row-shifted bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula missing query first output should keep the row-shifted styled formula");
    check_not_contains(first_worksheet_xml, R"(r="E4")",
        "renamed formula missing query first output should not include the later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula missing query second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="E4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+C1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula missing query second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula missing query second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:E5"/>)",
        "renamed formula missing query second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula missing query second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed formula missing query second output should omit the old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="B4")",
        "renamed formula missing query second output should omit inserted blank B4");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula missing query no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula missing query no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula missing query no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula missing query no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula missing query no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula missing query no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula missing query no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula missing query no-op save should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened_noop = fastxlsx::WorkbookEditor::open(noop_output);
    check(reopened_noop.has_worksheet("RenamedData") && !reopened_noop.has_worksheet("Data"),
        "renamed formula missing query reopened no-op output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_noop_sheet = reopened_noop.worksheet("RenamedData");
    check(!reopened_noop.has_pending_changes() && !reopened_noop_sheet.has_pending_changes(),
        "renamed formula missing query reopened no-op output should start clean");
    check(reopened_noop.pending_change_count() == 0 &&
            reopened_noop.pending_materialized_worksheet_names().empty() &&
            reopened_noop.pending_materialized_cell_count() == 0 &&
            reopened_noop.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula missing query reopened no-op output should not expose dirty diagnostics");
    check(reopened_noop_sheet.cell_count() == 7,
        "renamed formula missing query reopened no-op output should keep shifted sparse count");
    check_cell_range_equals(reopened_noop_sheet.used_range(), 1, 1, 5, 5,
        "renamed formula missing query reopened no-op output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_noop_e4 =
        reopened_noop_sheet.try_cell("E4");
    check(reopened_noop_e4.has_value() &&
            reopened_noop_e4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_noop_e4->text_value() == "A1+C1" &&
            reopened_noop_e4->has_style() &&
            reopened_noop_e4->style_id().value() == styled_formula_style.value(),
        "renamed formula missing query reopened no-op output should read translated styled formula");
    check(reopened_noop_sheet.get_cell("C1").number_value() == 1.0 &&
            reopened_noop_sheet.get_cell("C4").text_value() == "row2-gap-b2" &&
            reopened_noop_sheet.get_cell("D4").text_value() == "row2-gap-c2" &&
            reopened_noop_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula missing query reopened no-op output should read shifted source cells");
    check(!reopened_noop_sheet.try_cell("B1").has_value() &&
            !reopened_noop_sheet.try_cell("B4").has_value() &&
            !reopened_noop_sheet.try_cell("D2").has_value(),
        "renamed formula missing query reopened no-op output should keep old coordinates absent");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened_noop,
        "renamed formula missing query reopened no-op output after shifted sparse reads");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula missing query reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula missing query reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula missing query reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 7,
        "renamed formula missing query reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 5,
        "renamed formula missing query reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_e4 =
        reopened_sheet.try_cell("E4");
    check(reopened_e4.has_value() &&
            reopened_e4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_e4->text_value() == "A1+C1" &&
            reopened_e4->has_style() &&
            reopened_e4->style_id().value() == styled_formula_style.value(),
        "renamed formula missing query reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("C1").number_value() == 1.0 &&
            reopened_sheet.get_cell("C4").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("D4").text_value() == "row2-gap-c2" &&
            reopened_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula missing query reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("B4").has_value() &&
            !reopened_sheet.try_cell("D2").has_value(),
        "renamed formula missing query reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_formula_invalid_reads_preserve_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-invalid-read-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-invalid-read-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-invalid-read-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-invalid-read-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
        editor, "renamed formula invalid reads");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 2);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula invalid reads first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula invalid reads first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula invalid reads first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula invalid reads first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula invalid reads matching reacquire should stay clean before failures");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula invalid reads should keep the old source name unavailable");
    const std::optional<fastxlsx::CellValue> saved_formula = reacquired.try_cell("D4");
    check(saved_formula.has_value() &&
            saved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula->text_value() == "A1+B1" &&
            saved_formula->has_style() &&
            saved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula invalid reads matching reacquire should read the saved styled formula");

    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "renamed formula invalid reads should reject row-zero try_cell on the original handle");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell(1, 0); }),
        "renamed formula invalid reads should reject column-zero get_cell on the reacquired handle");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell("a1"); }),
        "renamed formula invalid reads should reject lowercase A1 reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("XFE1"); }),
        "renamed formula invalid reads should reject A1 column overflow");
    check(threw_fastxlsx_error([&] {
        (void)sheet.sparse_cells(fastxlsx::CellRange {0, 1, 1, 1});
    }), "renamed formula invalid reads should reject invalid CellRange reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.sparse_cells("B2:A1"); }),
        "renamed formula invalid reads should reject reversed A1 range reads");
    check(threw_fastxlsx_error([&] {
        const std::array<fastxlsx::WorksheetCellReference, 2> invalid_batch {
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {1048577, 1},
        };
        (void)sheet.sparse_cells(invalid_batch);
    }), "renamed formula invalid reads should reject invalid coordinate batch reads");
    check(threw_fastxlsx_error([&] { (void)sheet.row_cells(0); }),
        "renamed formula invalid reads should reject row_cells row zero");
    check(threw_fastxlsx_error([&] { (void)reacquired.column_cells(16385); }),
        "renamed formula invalid reads should reject column_cells column overflow");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("E4"); }),
        "renamed formula invalid reads should reject valid but missing get_cell reads");

    check(!editor.last_edit_error().has_value(),
        "renamed formula invalid reads should not update last_edit_error");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula invalid reads should keep both planned-name handles clean");
    check(editor.pending_change_count() == 2,
        "renamed formula invalid reads should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula invalid reads should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula invalid reads should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula invalid reads should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula invalid reads should preserve the planned workbook catalog");
    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        "renamed formula invalid reads should preserve planned-name lookup state");
    check(reacquired.cell_count() == 7 && sheet.cell_count() == 7,
        "renamed formula invalid reads should preserve sparse counts");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 5, 4,
        "renamed formula invalid reads should preserve row-shifted bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula = sheet.try_cell("D4");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == "A1+B1" &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula invalid reads should preserve the saved styled formula");
    check(sheet.get_cell("A4").text_value() == "placeholder-a2" &&
            reacquired.get_cell("A5").text_value() == "extra-c3",
        "renamed formula invalid reads should preserve shifted source cells");
    check(!sheet.try_cell("D2").has_value() && !reacquired.try_cell("A2").has_value(),
        "renamed formula invalid reads should keep old shifted coordinates absent");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula invalid reads");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula invalid reads later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula invalid reads later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 7,
        "renamed formula invalid reads later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula invalid reads later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E4");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "A1+C1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula invalid reads later shift should translate and preserve style id");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula invalid reads second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula invalid reads second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula invalid reads second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula invalid reads first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="D4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula invalid reads first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula invalid reads first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D5"/>)",
        "renamed formula invalid reads first output should keep row-shifted bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula invalid reads first output should keep the row-shifted styled formula");
    check_not_contains(first_worksheet_xml, R"(r="E4")",
        "renamed formula invalid reads first output should not include the later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula invalid reads second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="E4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+C1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula invalid reads second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula invalid reads second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:E5"/>)",
        "renamed formula invalid reads second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula invalid reads second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed formula invalid reads second output should omit the old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="B4")",
        "renamed formula invalid reads second output should omit inserted blank B4");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula invalid reads no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula invalid reads no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula invalid reads no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula invalid reads no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula invalid reads no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula invalid reads no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula invalid reads no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula invalid reads no-op save should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened_noop = fastxlsx::WorkbookEditor::open(noop_output);
    check(reopened_noop.has_worksheet("RenamedData") && !reopened_noop.has_worksheet("Data"),
        "renamed formula invalid reads reopened no-op output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_noop_sheet = reopened_noop.worksheet("RenamedData");
    check(!reopened_noop.has_pending_changes() && !reopened_noop_sheet.has_pending_changes(),
        "renamed formula invalid reads reopened no-op output should start clean");
    check(reopened_noop.pending_change_count() == 0 &&
            reopened_noop.pending_materialized_worksheet_names().empty() &&
            reopened_noop.pending_materialized_cell_count() == 0 &&
            reopened_noop.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula invalid reads reopened no-op output should not expose dirty diagnostics");
    check(reopened_noop_sheet.cell_count() == 7,
        "renamed formula invalid reads reopened no-op output should keep shifted sparse count");
    check_cell_range_equals(reopened_noop_sheet.used_range(), 1, 1, 5, 5,
        "renamed formula invalid reads reopened no-op output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_noop_e4 =
        reopened_noop_sheet.try_cell("E4");
    check(reopened_noop_e4.has_value() &&
            reopened_noop_e4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_noop_e4->text_value() == "A1+C1" &&
            reopened_noop_e4->has_style() &&
            reopened_noop_e4->style_id().value() == styled_formula_style.value(),
        "renamed formula invalid reads reopened no-op output should read translated styled formula");
    check(reopened_noop_sheet.get_cell("C1").number_value() == 1.0 &&
            reopened_noop_sheet.get_cell("C4").text_value() == "row2-gap-b2" &&
            reopened_noop_sheet.get_cell("D4").text_value() == "row2-gap-c2" &&
            reopened_noop_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula invalid reads reopened no-op output should read shifted source cells");
    check(!reopened_noop_sheet.try_cell("B1").has_value() &&
            !reopened_noop_sheet.try_cell("B4").has_value() &&
            !reopened_noop_sheet.try_cell("D2").has_value(),
        "renamed formula invalid reads reopened no-op output should keep old coordinates absent");
    check_public_state_reopened_formula_audit_clean_editor(
        reopened_noop,
        "renamed formula invalid reads reopened no-op output after shifted sparse reads");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula invalid reads reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula invalid reads reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula invalid reads reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 7,
        "renamed formula invalid reads reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 5,
        "renamed formula invalid reads reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_e4 =
        reopened_sheet.try_cell("E4");
    check(reopened_e4.has_value() &&
            reopened_e4->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_e4->text_value() == "A1+C1" &&
            reopened_e4->has_style() &&
            reopened_e4->style_id().value() == styled_formula_style.value(),
        "renamed formula invalid reads reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("C1").number_value() == 1.0 &&
            reopened_sheet.get_cell("C4").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("D4").text_value() == "row2-gap-c2" &&
            reopened_sheet.get_cell("A5").text_value() == "extra-c3",
        "renamed formula invalid reads reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("B4").has_value() &&
            !reopened_sheet.try_cell("D2").has_value(),
        "renamed formula invalid reads reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_formula_snapshot_reads_preserve_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-snapshot-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-snapshot-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-snapshot-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-formula-snapshot-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
        editor, "renamed formula snapshot reads");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.insert_rows(2, 2);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula snapshot reads first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula snapshot reads first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula snapshot reads first save should clear dirty materialized diagnostics");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula snapshot reads first save should leave the source package unchanged");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula snapshot reads matching reacquire should stay clean");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula snapshot reads should keep the old source name unavailable");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        reacquired.sparse_cells();
    check(all_cells.size() == 7,
        "renamed formula snapshot reads should return all saved sparse cells");
    check(all_cells[0].reference.row == 1 && all_cells[0].reference.column == 1 &&
            all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
            all_cells[0].value.text_value() == "placeholder-a1",
        "renamed formula snapshot reads should keep source A1 first");
    check(all_cells[1].reference.row == 1 && all_cells[1].reference.column == 2 &&
            all_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
            all_cells[1].value.number_value() == 1.0,
        "renamed formula snapshot reads should keep source B1 second");
    check(all_cells[5].reference.row == 4 && all_cells[5].reference.column == 4 &&
            all_cells[5].value.kind() == fastxlsx::CellValueKind::Formula &&
            all_cells[5].value.text_value() == "A1+B1" &&
            all_cells[5].value.has_style() &&
            all_cells[5].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads should expose the saved styled formula");
    check(all_cells[6].reference.row == 5 && all_cells[6].reference.column == 1 &&
            all_cells[6].value.kind() == fastxlsx::CellValueKind::Text &&
            all_cells[6].value.text_value() == "extra-c3",
        "renamed formula snapshot reads should keep the shifted trailing source cell");

    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
        sheet.sparse_cells("A4:D5");
    check(shifted_range.size() == 5,
        "renamed formula snapshot reads should return only represented cells in the range");
    check(shifted_range[0].reference.row == 4 && shifted_range[0].reference.column == 1 &&
            shifted_range[0].value.text_value() == "placeholder-a2",
        "renamed formula snapshot reads should keep shifted A2 first in the range");
    check(shifted_range[3].reference.row == 4 && shifted_range[3].reference.column == 4 &&
            shifted_range[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_range[3].value.text_value() == "A1+B1" &&
            shifted_range[3].value.has_style() &&
            shifted_range[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads should expose the styled formula in range order");
    check(shifted_range[4].reference.row == 5 && shifted_range[4].reference.column == 1 &&
            shifted_range[4].value.text_value() == "extra-c3",
        "renamed formula snapshot reads should keep trailing cells after row-four range cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_four =
        reacquired.row_cells(4);
    check(row_four.size() == 4,
        "renamed formula snapshot reads row_cells should expose the shifted formula row");
    check(row_four[0].reference.row == 4 && row_four[0].reference.column == 1 &&
            row_four[0].value.text_value() == "placeholder-a2",
        "renamed formula snapshot reads row_cells should keep shifted source A2 first");
    check(row_four[1].reference.row == 4 && row_four[1].reference.column == 2 &&
            row_four[1].value.text_value() == "row2-gap-b2",
        "renamed formula snapshot reads row_cells should keep shifted source B2 second");
    check(row_four[2].reference.row == 4 && row_four[2].reference.column == 3 &&
            row_four[2].value.text_value() == "row2-gap-c2",
        "renamed formula snapshot reads row_cells should keep shifted source C2 third");
    check(row_four[3].reference.row == 4 && row_four[3].reference.column == 4 &&
            row_four[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            row_four[3].value.text_value() == "A1+B1" &&
            row_four[3].value.has_style() &&
            row_four[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads row_cells should keep the styled formula last");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
        sheet.column_cells(4);
    check(column_four.size() == 1,
        "renamed formula snapshot reads column_cells should expose the formula column");
    check(column_four[0].reference.row == 4 &&
            column_four[0].reference.column == 4 &&
            column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            column_four[0].value.text_value() == "A1+B1" &&
            column_four[0].value.has_style() &&
            column_four[0].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads column_cells should keep formula style id");

    const std::array<fastxlsx::WorksheetCellReference, 4> requested_refs {
        fastxlsx::WorksheetCellReference {4, 4},
        fastxlsx::WorksheetCellReference {5, 1},
        fastxlsx::WorksheetCellReference {4, 1},
        fastxlsx::WorksheetCellReference {4, 4},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
        reacquired.sparse_cells(requested_refs);
    check(requested_cells.size() == 4,
        "renamed formula snapshot reads coordinate batch should keep requested represented cells");
    check(requested_cells[0].reference.row == 4 && requested_cells[0].reference.column == 4 &&
            requested_cells[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            requested_cells[0].value.text_value() == "A1+B1" &&
            requested_cells[0].value.has_style() &&
            requested_cells[0].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads coordinate batch should return D4 first");
    check(requested_cells[1].reference.row == 5 && requested_cells[1].reference.column == 1 &&
            requested_cells[1].value.text_value() == "extra-c3",
        "renamed formula snapshot reads coordinate batch should return A5 second");
    check(requested_cells[2].reference.row == 4 && requested_cells[2].reference.column == 1 &&
            requested_cells[2].value.text_value() == "placeholder-a2",
        "renamed formula snapshot reads coordinate batch should return A4 third");
    check(requested_cells[3].reference.row == 4 && requested_cells[3].reference.column == 4 &&
            requested_cells[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            requested_cells[3].value.text_value() == "A1+B1" &&
            requested_cells[3].value.has_style() &&
            requested_cells[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads coordinate batch should preserve duplicate D4 reads");

    check(!editor.last_edit_error().has_value(),
        "renamed formula snapshot reads should keep diagnostics clear");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula snapshot reads should keep both planned-name handles clean");
    check(editor.pending_change_count() == 2,
        "renamed formula snapshot reads should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula snapshot reads should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula snapshot reads should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula snapshot reads should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula snapshot reads should preserve the planned workbook catalog");
    check(reacquired.cell_count() == 7 && sheet.cell_count() == 7,
        "renamed formula snapshot reads should preserve sparse counts");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 5, 4,
        "renamed formula snapshot reads should preserve row-shifted bounds");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula snapshot reads");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(all_cells[5].reference.row == 4 && all_cells[5].reference.column == 4 &&
            all_cells[5].value.text_value() == "A1+B1" &&
            all_cells[5].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads should return owning snapshots across later shifts");
    check(row_four[3].reference.row == 4 && row_four[3].reference.column == 4 &&
            row_four[3].value.text_value() == "A1+B1" &&
            row_four[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads row snapshot should remain stable after later shifts");
    check(column_four[0].reference.row == 4 && column_four[0].reference.column == 4 &&
            column_four[0].value.text_value() == "A1+B1" &&
            column_four[0].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads column snapshot should remain stable after later shifts");
    check(requested_cells[3].reference.row == 4 && requested_cells[3].reference.column == 4 &&
            requested_cells[3].value.text_value() == "A1+B1" &&
            requested_cells[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads batch snapshot should remain stable after later shifts");
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula snapshot reads later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula snapshot reads later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 7,
        "renamed formula snapshot reads later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula snapshot reads later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E4");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "A1+C1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads later shift should translate and preserve style id");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula snapshot reads second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula snapshot reads second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula snapshot reads second save should clear dirty diagnostics again");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula snapshot reads second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="E4" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+C1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula snapshot reads second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula snapshot reads second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:E5"/>)",
        "renamed formula snapshot reads second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula snapshot reads second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed formula snapshot reads second output should omit the old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="B4")",
        "renamed formula snapshot reads second output should omit inserted blank B4");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula snapshot reads no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula snapshot reads no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula snapshot reads no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula snapshot reads no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula snapshot reads no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula snapshot reads no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula snapshot reads no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula snapshot reads no-op save should leave the source package unchanged");
    fastxlsx::WorkbookEditor reopened_noop = fastxlsx::WorkbookEditor::open(noop_output);
    check(reopened_noop.has_worksheet("RenamedData") && !reopened_noop.has_worksheet("Data"),
        "renamed formula snapshot reads reopened no-op output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_noop_sheet = reopened_noop.worksheet("RenamedData");
    check(!reopened_noop.has_pending_changes() && !reopened_noop_sheet.has_pending_changes(),
        "renamed formula snapshot reads reopened no-op output should start clean");
    check(reopened_noop.pending_change_count() == 0 &&
            reopened_noop.pending_materialized_worksheet_names().empty() &&
            reopened_noop.pending_materialized_cell_count() == 0 &&
            reopened_noop.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula snapshot reads reopened no-op output should not expose dirty diagnostics");
    check(reopened_noop_sheet.cell_count() == 7,
        "renamed formula snapshot reads reopened no-op output should keep shifted sparse count");
    check_cell_range_equals(reopened_noop_sheet.used_range(), 1, 1, 5, 5,
        "renamed formula snapshot reads reopened no-op output should expose combined shifted bounds");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_noop_row_four =
        reopened_noop_sheet.row_cells(4);
    check(reopened_noop_row_four.size() == 4,
        "renamed formula snapshot reads reopened no-op row_cells should expose shifted row four");
    check(reopened_noop_row_four[0].reference.row == 4 &&
            reopened_noop_row_four[0].reference.column == 1 &&
            reopened_noop_row_four[0].value.text_value() == "placeholder-a2",
        "renamed formula snapshot reads reopened no-op row_cells should read shifted A2");
    check(reopened_noop_row_four[1].reference.row == 4 &&
            reopened_noop_row_four[1].reference.column == 3 &&
            reopened_noop_row_four[1].value.text_value() == "row2-gap-b2",
        "renamed formula snapshot reads reopened no-op row_cells should read shifted B2");
    check(reopened_noop_row_four[2].reference.row == 4 &&
            reopened_noop_row_four[2].reference.column == 4 &&
            reopened_noop_row_four[2].value.text_value() == "row2-gap-c2",
        "renamed formula snapshot reads reopened no-op row_cells should read shifted C2");
    check(reopened_noop_row_four[3].reference.row == 4 &&
            reopened_noop_row_four[3].reference.column == 5 &&
            reopened_noop_row_four[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_noop_row_four[3].value.text_value() == "A1+C1" &&
            reopened_noop_row_four[3].value.has_style() &&
            reopened_noop_row_four[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads reopened no-op row_cells should read translated styled formula");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_noop_column_five =
        reopened_noop_sheet.column_cells(5);
    check(reopened_noop_column_five.size() == 1 &&
            reopened_noop_column_five[0].reference.row == 4 &&
            reopened_noop_column_five[0].reference.column == 5 &&
            reopened_noop_column_five[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_noop_column_five[0].value.text_value() == "A1+C1" &&
            reopened_noop_column_five[0].value.has_style() &&
            reopened_noop_column_five[0].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads reopened no-op column_cells should read translated styled formula");
    check(!reopened_noop_sheet.try_cell("B1").has_value() &&
            !reopened_noop_sheet.try_cell("B4").has_value() &&
            !reopened_noop_sheet.try_cell("D2").has_value(),
        "renamed formula snapshot reads reopened no-op output should keep old coordinates absent");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula snapshot reads reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula snapshot reads reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula snapshot reads reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 7,
        "renamed formula snapshot reads reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 5, 5,
        "renamed formula snapshot reads reopened output should expose combined shifted bounds");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_four =
        reopened_sheet.row_cells(4);
    check(reopened_row_four.size() == 4,
        "renamed formula snapshot reads reopened row_cells should expose shifted row four");
    check(reopened_row_four[0].reference.row == 4 && reopened_row_four[0].reference.column == 1 &&
            reopened_row_four[0].value.text_value() == "placeholder-a2",
        "renamed formula snapshot reads reopened row_cells should read shifted A2");
    check(reopened_row_four[1].reference.row == 4 && reopened_row_four[1].reference.column == 3 &&
            reopened_row_four[1].value.text_value() == "row2-gap-b2",
        "renamed formula snapshot reads reopened row_cells should read shifted B2");
    check(reopened_row_four[2].reference.row == 4 && reopened_row_four[2].reference.column == 4 &&
            reopened_row_four[2].value.text_value() == "row2-gap-c2",
        "renamed formula snapshot reads reopened row_cells should read shifted C2");
    check(reopened_row_four[3].reference.row == 4 && reopened_row_four[3].reference.column == 5 &&
            reopened_row_four[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_row_four[3].value.text_value() == "A1+C1" &&
            reopened_row_four[3].value.has_style() &&
            reopened_row_four[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads reopened row_cells should read translated styled formula");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_five =
        reopened_sheet.column_cells(5);
    check(reopened_column_five.size() == 1 &&
            reopened_column_five[0].reference.row == 4 &&
            reopened_column_five[0].reference.column == 5 &&
            reopened_column_five[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_column_five[0].value.text_value() == "A1+C1" &&
            reopened_column_five[0].value.has_style() &&
            reopened_column_five[0].value.style_id().value() == styled_formula_style.value(),
        "renamed formula snapshot reads reopened column_cells should read translated styled formula");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("B4").has_value() &&
            !reopened_sheet.try_cell("D2").has_value(),
        "renamed formula snapshot reads reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_deletes_formula_references()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-ref-formula-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-ref-formula-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-ref-formula-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
        editor, "renamed formula delete_columns");
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_columns(1, 1);
    const std::size_t shifted_memory = sheet.estimated_memory_usage();

    check(sheet.cell_count() == 4,
        "renamed formula delete_columns should remove deleted-column records");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("A1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "renamed formula delete_columns should shift B1 to A1");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("C2");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+A1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete_columns should translate deleted references and preserve style id");
    check(sheet.get_cell("A2").text_value() == "row2-gap-b2",
        "renamed formula delete_columns should shift B2 to A2");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A3").has_value(),
        "renamed formula delete_columns should keep deleted and old formula coordinates absent");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete_columns should report dirty materialized state under the planned name");
    check(editor.pending_materialized_cell_count() == 4,
        "renamed formula delete_columns should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "renamed formula delete_columns should report shifted materialized memory estimate");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete_columns save_as should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete_columns save_as should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete_columns save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete_columns save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(workbook_xml, R"(name="RenamedData")",
        "renamed formula delete_columns save_as should write the planned catalog name");
    check_not_contains(workbook_xml, R"(name="Data")",
        "renamed formula delete_columns save_as should omit the source catalog name");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "renamed formula delete_columns save_as should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "renamed formula delete_columns save_as should write shifted B1");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "renamed formula delete_columns save_as should write shifted B2");
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed formula delete_columns save_as should write #REF formula with style id");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "renamed formula delete_columns save_as should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "renamed formula delete_columns save_as should omit deleted trailing coordinate");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete_columns reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete_columns reopened output should start clean");
    check(reopened_sheet.cell_count() == 4,
        "renamed formula delete_columns reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
        "renamed formula delete_columns reopened output should expose shifted bounds");
    const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
    check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
            reopened_a1.number_value() == 1.0,
        "renamed formula delete_columns reopened output should read shifted B1");
    const std::optional<fastxlsx::CellValue> reopened_c2 =
        reopened_sheet.try_cell("C2");
    check(reopened_c2.has_value() &&
            reopened_c2->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_c2->text_value() == "#REF!+A1" &&
            reopened_c2->has_style() &&
            reopened_c2->style_id().value() == styled_formula_style.value(),
        "renamed formula delete_columns reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A2").text_value() == "row2-gap-b2",
        "renamed formula delete_columns reopened output should read shifted B2");
    check(!reopened_sheet.try_cell("D2").has_value() &&
            !reopened_sheet.try_cell("A3").has_value(),
        "renamed formula delete_columns reopened output should keep old coordinates absent");

    check_public_state_renamed_clean_noop_save(
        editor, sheet, noop_output, output_entries, "renamed formula delete_columns", 2,
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 4,
                "renamed formula delete_columns no-op save reopened output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 2, 3,
                "renamed formula delete_columns no-op save reopened output should expose shifted bounds");
            const fastxlsx::CellValue noop_a1 = noop_sheet.get_cell("A1");
            check(noop_a1.kind() == fastxlsx::CellValueKind::Number &&
                    noop_a1.number_value() == 1.0,
                "renamed formula delete_columns no-op save reopened output should read shifted B1");
            const std::optional<fastxlsx::CellValue> noop_c2 =
                noop_sheet.try_cell("C2");
            check(noop_c2.has_value() &&
                    noop_c2->kind() == fastxlsx::CellValueKind::Formula &&
                    noop_c2->text_value() == "#REF!+A1" &&
                    noop_c2->has_style() &&
                    noop_c2->style_id().value() == styled_formula_style.value(),
                "renamed formula delete_columns no-op save reopened output should read translated styled formula");
            check(noop_sheet.get_cell("A2").text_value() == "row2-gap-b2",
                "renamed formula delete_columns no-op save reopened output should read shifted B2");
            check(!noop_sheet.try_cell("D2").has_value() &&
                    !noop_sheet.try_cell("A3").has_value(),
                "renamed formula delete_columns no-op save reopened output should keep old coordinates absent");
        });
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete_columns no-op save should leave the source package unchanged");
}

void test_public_worksheet_editor_shift_after_rename_delete_columns_formula_failed_save_preserves_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-failed-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-failed-save-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-failed-save-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-failed-save-second-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");

    sheet.delete_columns(1, 1);
    const std::size_t shifted_memory = sheet.estimated_memory_usage();
    const auto check_dirty_styled_formula_session = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep last_edit_error clear");
        check(editor.has_pending_changes(),
            label + " should keep the public editor dirty");
        check(editor.pending_change_count() == 1,
            label + " should keep only the queued rename before materialized handoff");
        check(sheet.has_pending_changes(),
            label + " should keep the styled formula handle dirty");
        check(sheet.cell_count() == 4,
            label + " should preserve the delete-column sparse count");
        check(sheet.estimated_memory_usage() == shifted_memory,
            label + " should preserve the delete-column materialized memory estimate");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"RenamedData"},
            label + " should report dirty materialized state under the planned name");
        check(editor.pending_materialized_cell_count() == 4,
            label + " should report the delete-column styled formula sparse count");
        check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            label + " should report the delete-column styled formula memory estimate");
        check(editor.source_worksheet_names() == expected_source_names,
            label + " should preserve source worksheet names");
        check(editor.worksheet_names() == expected_planned_names,
            label + " should preserve planned worksheet names");
        check(workbook_editor_catalog_entries_equal(
                  editor.worksheet_catalog(), expected_catalog),
            label + " should preserve the renamed worksheet catalog");
        check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
            label + " should expose only the planned sheet name");

        const fastxlsx::CellValue shifted_number = sheet.get_cell("A1");
        check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
                shifted_number.number_value() == 1.0,
            label + " should keep shifted source B1 in memory");
        const std::optional<fastxlsx::CellValue> shifted_formula =
            sheet.try_cell("C2");
        check(shifted_formula.has_value() &&
                shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula->text_value() == "#REF!+A1" &&
                shifted_formula->has_style() &&
                shifted_formula->style_id().value() == styled_formula_style.value(),
            label + " should keep the delete-column styled formula in memory");
        check(sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                sheet.get_cell("B2").text_value() == "row2-gap-c2",
            label + " should keep shifted source row cells in memory");
        check(!sheet.try_cell("D2").has_value() &&
                !sheet.try_cell("A3").has_value(),
            label + " should keep old and deleted coordinates absent");
    };

    check_dirty_styled_formula_session(
        "renamed formula delete-column failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "renamed formula delete-column failed save should reject exact source overwrite");
    check_dirty_styled_formula_session(
        "renamed formula delete-column failed save rejected source overwrite");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_workbook_xml = source_entries.at("xl/workbook.xml");
    const std::string source_worksheet_xml = source_entries.at("xl/worksheets/sheet1.xml");
    const std::string source_styled_formula_xml =
        std::string(R"(<c r="D2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(source_workbook_xml, R"(name="Data")",
        "renamed formula delete-column failed save should leave the source workbook catalog unchanged");
    check_not_contains(source_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-column failed save should not write the planned name into the source workbook");
    check_contains(source_worksheet_xml, R"(<dimension ref="A1:D3"/>)",
        "renamed formula delete-column failed save should leave the source workbook bounds unchanged");
    check_contains(source_worksheet_xml, source_styled_formula_xml,
        "renamed formula delete-column failed save should leave the source styled formula unchanged");
    check_contains(source_worksheet_xml, R"(<c r="A3" t="inlineStr"><is><t>extra-c3</t></is></c>)",
        "renamed formula delete-column failed save should leave the source trailing row unchanged");
    check_not_contains(source_worksheet_xml, "#REF!+A1",
        "renamed formula delete-column failed save should not write the translated formula into the source workbook");
    check_not_contains(source_worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "renamed formula delete-column failed save should not write shifted B1 into the source workbook");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-column failed save safe retry should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column failed save safe retry should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column failed save safe retry should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column failed save safe retry should keep diagnostics clear");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string output_workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string output_styled_formula_xml =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(output_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-column failed save safe retry should write the planned catalog name");
    check_not_contains(output_workbook_xml, R"(name="Data")",
        "renamed formula delete-column failed save safe retry should omit the source catalog name");
    check_contains(output_worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "renamed formula delete-column failed save safe retry should project delete-column bounds");
    check_contains(output_worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "renamed formula delete-column failed save safe retry should write shifted B1");
    check_contains(output_worksheet_xml, R"(<c r="A2")",
        "renamed formula delete-column failed save safe retry should write shifted B2");
    check_contains(output_worksheet_xml, R"(<c r="B2")",
        "renamed formula delete-column failed save safe retry should write shifted C2");
    check_contains(output_worksheet_xml, output_styled_formula_xml,
        "renamed formula delete-column failed save safe retry should write translated formula with style id");
    check_not_contains(output_worksheet_xml, R"(r="D2")",
        "renamed formula delete-column failed save safe retry should omit the old formula coordinate");
    check_not_contains(output_worksheet_xml, R"(r="A3")",
        "renamed formula delete-column failed save safe retry should omit the deleted trailing coordinate");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-column failed save no-op retry should keep the styled handle clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column failed save no-op retry should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column failed save no-op retry should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column failed save no-op retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column failed save no-op retry should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-column failed save no-op retry");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-column failed save no-op retry");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "renamed formula delete-column failed save no-op retry should keep output entries stable");
    check_reopened_delete_column_formula_noop_output(
        noop_output, styled_formula_style,
        "renamed formula delete-column failed save no-op output");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-column failed save second no-op retry should keep the styled handle clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column failed save second no-op retry should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column failed save second no-op retry should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column failed save second no-op retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column failed save second no-op retry should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "renamed formula delete-column failed save second no-op retry");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "renamed formula delete-column failed save second no-op retry");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column failed save second no-op retry should keep source entries unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "renamed formula delete-column failed save second no-op retry should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "renamed formula delete-column failed save second no-op retry should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "renamed formula delete-column failed save second no-op retry should keep output entries stable");
    check_reopened_delete_column_formula_noop_output(
        second_noop_output, styled_formula_style,
        "renamed formula delete-column failed save second no-op output");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-column failed save reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-column failed save reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column failed save reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 4,
        "renamed formula delete-column failed save reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
        "renamed formula delete-column failed save reopened output should expose shifted bounds");
    const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
    check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
            reopened_a1.number_value() == 1.0,
        "renamed formula delete-column failed save reopened output should read shifted B1");
    const std::optional<fastxlsx::CellValue> reopened_c2 =
        reopened_sheet.try_cell("C2");
    check(reopened_c2.has_value() &&
            reopened_c2->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_c2->text_value() == "#REF!+A1" &&
            reopened_c2->has_style() &&
            reopened_c2->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column failed save reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("B2").text_value() == "row2-gap-c2",
        "renamed formula delete-column failed save reopened output should read shifted row cells");
    check(!reopened_sheet.try_cell("D2").has_value() &&
            !reopened_sheet.try_cell("A3").has_value(),
        "renamed formula delete-column failed save reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_delete_columns_formula_option_mismatch_preserves_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-options-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-options-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-options-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-options-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_columns(1, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-column option mismatch first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column option mismatch first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column option mismatch first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column option mismatch first save should keep diagnostics clear");

    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 2;
    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("RenamedData", mismatched_options);
    }), "renamed formula delete-column option mismatch try_worksheet should reject different options");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("RenamedData", mismatched_options);
    }), "renamed formula delete-column option mismatch worksheet should reject different options");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula delete-column option mismatch should keep the old source name unavailable");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column option mismatch should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        "renamed formula delete-column option mismatch should leave the saved planned-name handle clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column option mismatch should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column option mismatch should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column option mismatch should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula delete-column option mismatch should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula delete-column option mismatch should preserve the planned workbook catalog");
    const std::optional<fastxlsx::CellValue> saved_formula = sheet.try_cell("C2");
    check(saved_formula.has_value() &&
            saved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula->text_value() == "#REF!+A1" &&
            saved_formula->has_style() &&
            saved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column option mismatch should preserve the saved styled formula");
    check(sheet.get_cell("A1").number_value() == 1.0 &&
            sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
            sheet.get_cell("B2").text_value() == "row2-gap-c2",
        "renamed formula delete-column option mismatch should preserve shifted source cells");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A3").has_value(),
        "renamed formula delete-column option mismatch should keep old shifted coordinates absent");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula delete-column option mismatch matching reacquire should stay clean");
    check(reacquired.name() == "RenamedData",
        "renamed formula delete-column option mismatch matching reacquire should expose the planned name");
    const std::optional<fastxlsx::CellValue> reacquired_formula =
        reacquired.try_cell("C2");
    check(reacquired_formula.has_value() &&
            reacquired_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_formula->text_value() == "#REF!+A1" &&
            reacquired_formula->has_style() &&
            reacquired_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column option mismatch matching reacquire should reuse the saved styled formula");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula delete-column option mismatch");

    reacquired.insert_rows(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula delete-column option mismatch later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete-column option mismatch later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 4,
        "renamed formula delete-column option mismatch later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula delete-column option mismatch later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("C3");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+A1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column option mismatch later shift should translate and preserve style id");
    check(sheet.get_cell("A1").number_value() == 1.0 &&
            sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
            sheet.get_cell("B3").text_value() == "row2-gap-c2",
        "renamed formula delete-column option mismatch later shift should move source-backed cells");
    check(!reacquired.try_cell("A2").has_value() &&
            !reacquired.try_cell("C2").has_value() &&
            !reacquired.try_cell("D2").has_value(),
        "renamed formula delete-column option mismatch later shift should keep old coordinates absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column option mismatch second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-column option mismatch second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column option mismatch second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column option mismatch first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-column option mismatch first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula delete-column option mismatch first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "renamed formula delete-column option mismatch first output should keep delete-column bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula delete-column option mismatch first output should keep the delete-column styled formula");
    check_not_contains(first_worksheet_xml, R"(r="C3")",
        "renamed formula delete-column option mismatch first output should not include the later row shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column option mismatch second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="C3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-column option mismatch second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula delete-column option mismatch second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "renamed formula delete-column option mismatch second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "renamed formula delete-column option mismatch second output should keep shifted B1");
    check_contains(second_worksheet_xml, R"(<c r="A3")",
        "renamed formula delete-column option mismatch second output should write shifted B2");
    check_contains(second_worksheet_xml, R"(<c r="B3")",
        "renamed formula delete-column option mismatch second output should write shifted C2");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula delete-column option mismatch second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="C2")",
        "renamed formula delete-column option mismatch second output should omit the old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="A2")",
        "renamed formula delete-column option mismatch second output should omit inserted blank A2");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column option mismatch no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-column option mismatch no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column option mismatch no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column option mismatch no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-column option mismatch no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-column option mismatch no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula delete-column option mismatch no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column option mismatch no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "RenamedData",
        "renamed formula delete-column option mismatch no-op output",
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 4,
                "renamed formula delete-column option mismatch no-op output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 3, 3,
                "renamed formula delete-column option mismatch no-op output should expose combined shifted bounds");
            const std::optional<fastxlsx::CellValue> noop_c3 =
                noop_sheet.try_cell("C3");
            check(noop_c3.has_value() &&
                    noop_c3->kind() == fastxlsx::CellValueKind::Formula &&
                    noop_c3->text_value() == "#REF!+A1" &&
                    noop_c3->has_style() &&
                    noop_c3->style_id().value() == styled_formula_style.value(),
                "renamed formula delete-column option mismatch no-op output should read translated styled formula");
            check(noop_sheet.get_cell("A1").number_value() == 1.0 &&
                    noop_sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
                    noop_sheet.get_cell("B3").text_value() == "row2-gap-c2",
                "renamed formula delete-column option mismatch no-op output should read shifted source cells");
            check(!noop_sheet.try_cell("A2").has_value() &&
                    !noop_sheet.try_cell("C2").has_value() &&
                    !noop_sheet.try_cell("D2").has_value(),
                "renamed formula delete-column option mismatch no-op output should keep old coordinates absent");
        });

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-column option mismatch reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-column option mismatch reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column option mismatch reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 4,
        "renamed formula delete-column option mismatch reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
        "renamed formula delete-column option mismatch reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_c3 =
        reopened_sheet.try_cell("C3");
    check(reopened_c3.has_value() &&
            reopened_c3->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_c3->text_value() == "#REF!+A1" &&
            reopened_c3->has_style() &&
            reopened_c3->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column option mismatch reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A1").number_value() == 1.0 &&
            reopened_sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("B3").text_value() == "row2-gap-c2",
        "renamed formula delete-column option mismatch reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("A2").has_value() &&
            !reopened_sheet.try_cell("C2").has_value() &&
            !reopened_sheet.try_cell("D2").has_value(),
        "renamed formula delete-column option mismatch reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_delete_columns_formula_invalid_mutations_preserve_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-invalid-mutation-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-invalid-mutation-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-invalid-mutation-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-invalid-mutation-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_columns(1, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-column invalid mutations first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column invalid mutations first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column invalid mutations first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column invalid mutations first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula delete-column invalid mutations matching reacquire should stay clean before failures");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula delete-column invalid mutations should keep the old source name unavailable");
    const std::optional<fastxlsx::CellValue> saved_formula = reacquired.try_cell("C2");
    check(saved_formula.has_value() &&
            saved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula->text_value() == "#REF!+A1" &&
            saved_formula->has_style() &&
            saved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column invalid mutations matching reacquire should read the saved styled formula");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1,
            fastxlsx::CellValue::formula("invalid-renamed-delete-column-formula-row-zero"));
    }), "renamed formula delete-column invalid mutations should reject formula set_cell row zero");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell("XFE1",
            fastxlsx::CellValue::formula("invalid-renamed-delete-column-formula-a1-overflow"));
    }), "renamed formula delete-column invalid mutations should reject formula set_cell column overflow");
    check(threw_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "renamed formula delete-column invalid mutations should reject range erase_cell references");

    check(editor.last_edit_error().has_value(),
        "renamed formula delete-column invalid mutations should populate last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(), "WorksheetEditor cell reference is invalid",
            "renamed formula delete-column invalid mutations should expose the latest invalid reference diagnostic");
    }
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column invalid mutations should keep both planned-name handles clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column invalid mutations should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column invalid mutations should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column invalid mutations should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula delete-column invalid mutations should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula delete-column invalid mutations should preserve the planned workbook catalog");
    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        "renamed formula delete-column invalid mutations should preserve planned-name lookup state");
    const std::optional<fastxlsx::CellValue> preserved_formula = sheet.try_cell("C2");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == "#REF!+A1" &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column invalid mutations should preserve the saved styled formula");
    check(sheet.get_cell("A1").number_value() == 1.0 &&
            reacquired.get_cell("A2").text_value() == "row2-gap-b2" &&
            reacquired.get_cell("B2").text_value() == "row2-gap-c2",
        "renamed formula delete-column invalid mutations should preserve shifted source cells");
    check(!sheet.try_cell("D2").has_value() && !reacquired.try_cell("A3").has_value(),
        "renamed formula delete-column invalid mutations should keep old shifted coordinates absent");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula delete-column invalid mutations");

    reacquired.insert_rows(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column invalid mutations later valid shift should clear diagnostics");
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula delete-column invalid mutations later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete-column invalid mutations later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 4,
        "renamed formula delete-column invalid mutations later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula delete-column invalid mutations later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("C3");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+A1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column invalid mutations later shift should translate and preserve style id");
    check(sheet.get_cell("A1").number_value() == 1.0 &&
            sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
            sheet.get_cell("B3").text_value() == "row2-gap-c2",
        "renamed formula delete-column invalid mutations later shift should move source-backed cells");
    check(!reacquired.try_cell("A2").has_value() &&
            !reacquired.try_cell("C2").has_value() &&
            !reacquired.try_cell("D2").has_value(),
        "renamed formula delete-column invalid mutations later shift should keep old coordinates absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column invalid mutations second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-column invalid mutations second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column invalid mutations second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column invalid mutations first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-column invalid mutations first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula delete-column invalid mutations first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "renamed formula delete-column invalid mutations first output should keep delete-column bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula delete-column invalid mutations first output should keep the delete-column styled formula");
    check_not_contains(first_worksheet_xml, "invalid-renamed-delete-column-formula-",
        "renamed formula delete-column invalid mutations first output should not contain rejected payloads");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column invalid mutations second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="C3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-column invalid mutations second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula delete-column invalid mutations second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "renamed formula delete-column invalid mutations second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "renamed formula delete-column invalid mutations second output should keep shifted B1");
    check_contains(second_worksheet_xml, R"(<c r="A3")",
        "renamed formula delete-column invalid mutations second output should write shifted B2");
    check_contains(second_worksheet_xml, R"(<c r="B3")",
        "renamed formula delete-column invalid mutations second output should write shifted C2");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula delete-column invalid mutations second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, "invalid-renamed-delete-column-formula-",
        "renamed formula delete-column invalid mutations second output should not leak rejected payloads");
    check_not_contains(second_worksheet_xml, R"(r="C2")",
        "renamed formula delete-column invalid mutations second output should omit the old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="A2")",
        "renamed formula delete-column invalid mutations second output should omit inserted blank A2");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column invalid mutations no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-column invalid mutations no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column invalid mutations no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column invalid mutations no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-column invalid mutations no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-column invalid mutations no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula delete-column invalid mutations no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column invalid mutations no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "RenamedData",
        "renamed formula delete-column invalid mutations no-op output",
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 4,
                "renamed formula delete-column invalid mutations no-op output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 3, 3,
                "renamed formula delete-column invalid mutations no-op output should expose combined shifted bounds");
            const std::optional<fastxlsx::CellValue> noop_c3 =
                noop_sheet.try_cell("C3");
            check(noop_c3.has_value() &&
                    noop_c3->kind() == fastxlsx::CellValueKind::Formula &&
                    noop_c3->text_value() == "#REF!+A1" &&
                    noop_c3->has_style() &&
                    noop_c3->style_id().value() == styled_formula_style.value(),
                "renamed formula delete-column invalid mutations no-op output should read translated styled formula");
            check(noop_sheet.get_cell("A1").number_value() == 1.0 &&
                    noop_sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
                    noop_sheet.get_cell("B3").text_value() == "row2-gap-c2",
                "renamed formula delete-column invalid mutations no-op output should read shifted source cells");
            check(!noop_sheet.try_cell("A2").has_value() &&
                    !noop_sheet.try_cell("C2").has_value() &&
                    !noop_sheet.try_cell("D2").has_value(),
                "renamed formula delete-column invalid mutations no-op output should keep old coordinates absent");
        });

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-column invalid mutations reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-column invalid mutations reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column invalid mutations reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 4,
        "renamed formula delete-column invalid mutations reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
        "renamed formula delete-column invalid mutations reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_c3 =
        reopened_sheet.try_cell("C3");
    check(reopened_c3.has_value() &&
            reopened_c3->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_c3->text_value() == "#REF!+A1" &&
            reopened_c3->has_style() &&
            reopened_c3->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column invalid mutations reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A1").number_value() == 1.0 &&
            reopened_sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("B3").text_value() == "row2-gap-c2",
        "renamed formula delete-column invalid mutations reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("A2").has_value() &&
            !reopened_sheet.try_cell("C2").has_value() &&
            !reopened_sheet.try_cell("D2").has_value(),
        "renamed formula delete-column invalid mutations reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_delete_columns_formula_missing_query_preserves_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-missing-query-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-missing-query-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-missing-query-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-missing-query-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_columns(1, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-column missing query first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column missing query first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column missing query first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column missing query first save should keep diagnostics clear");

    const std::optional<fastxlsx::WorksheetEditor> missing =
        editor.try_worksheet("Missing");
    check(!missing.has_value(),
        "renamed formula delete-column missing query try_worksheet should report a missing sheet");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula delete-column missing query should keep the old source name unavailable");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Missing"); }),
        "renamed formula delete-column missing query worksheet should reject a missing sheet");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Data"); }),
        "renamed formula delete-column missing query worksheet should reject the old source name");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column missing query should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        "renamed formula delete-column missing query should leave the saved planned-name handle clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column missing query should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column missing query should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column missing query should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula delete-column missing query should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula delete-column missing query should preserve the planned workbook catalog");
    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        "renamed formula delete-column missing query should preserve planned-name lookup state");
    const std::optional<fastxlsx::CellValue> saved_formula = sheet.try_cell("C2");
    check(saved_formula.has_value() &&
            saved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula->text_value() == "#REF!+A1" &&
            saved_formula->has_style() &&
            saved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column missing query should preserve the saved styled formula");
    check(sheet.get_cell("A1").number_value() == 1.0 &&
            sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
            sheet.get_cell("B2").text_value() == "row2-gap-c2",
        "renamed formula delete-column missing query should preserve shifted source cells");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A3").has_value(),
        "renamed formula delete-column missing query should keep old shifted coordinates absent");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula delete-column missing query matching reacquire should stay clean");
    const std::optional<fastxlsx::CellValue> reacquired_formula =
        reacquired.try_cell("C2");
    check(reacquired_formula.has_value() &&
            reacquired_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_formula->text_value() == "#REF!+A1" &&
            reacquired_formula->has_style() &&
            reacquired_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column missing query matching reacquire should reuse the saved styled formula");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula delete-column missing query");

    reacquired.insert_rows(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula delete-column missing query later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete-column missing query later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 4,
        "renamed formula delete-column missing query later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula delete-column missing query later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("C3");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+A1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column missing query later shift should translate and preserve style id");
    check(sheet.get_cell("A1").number_value() == 1.0 &&
            sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
            sheet.get_cell("B3").text_value() == "row2-gap-c2",
        "renamed formula delete-column missing query later shift should move source-backed cells");
    check(!reacquired.try_cell("A2").has_value() &&
            !reacquired.try_cell("C2").has_value() &&
            !reacquired.try_cell("D2").has_value(),
        "renamed formula delete-column missing query later shift should keep old coordinates absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column missing query second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-column missing query second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column missing query second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column missing query first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-column missing query first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula delete-column missing query first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "renamed formula delete-column missing query first output should keep delete-column bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula delete-column missing query first output should keep the delete-column styled formula");
    check_not_contains(first_worksheet_xml, R"(r="C3")",
        "renamed formula delete-column missing query first output should not include the later row shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column missing query second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="C3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-column missing query second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula delete-column missing query second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "renamed formula delete-column missing query second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "renamed formula delete-column missing query second output should keep shifted B1");
    check_contains(second_worksheet_xml, R"(<c r="A3")",
        "renamed formula delete-column missing query second output should write shifted B2");
    check_contains(second_worksheet_xml, R"(<c r="B3")",
        "renamed formula delete-column missing query second output should write shifted C2");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula delete-column missing query second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="C2")",
        "renamed formula delete-column missing query second output should omit the old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="A2")",
        "renamed formula delete-column missing query second output should omit inserted blank A2");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column missing query no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-column missing query no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column missing query no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column missing query no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-column missing query no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-column missing query no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula delete-column missing query no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column missing query no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "RenamedData",
        "renamed formula delete-column missing query no-op output",
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 4,
                "renamed formula delete-column missing query no-op output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 3, 3,
                "renamed formula delete-column missing query no-op output should expose combined shifted bounds");
            const std::optional<fastxlsx::CellValue> noop_c3 =
                noop_sheet.try_cell("C3");
            check(noop_c3.has_value() &&
                    noop_c3->kind() == fastxlsx::CellValueKind::Formula &&
                    noop_c3->text_value() == "#REF!+A1" &&
                    noop_c3->has_style() &&
                    noop_c3->style_id().value() == styled_formula_style.value(),
                "renamed formula delete-column missing query no-op output should read translated styled formula");
            check(noop_sheet.get_cell("A1").number_value() == 1.0 &&
                    noop_sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
                    noop_sheet.get_cell("B3").text_value() == "row2-gap-c2",
                "renamed formula delete-column missing query no-op output should read shifted source cells");
            check(!noop_sheet.try_cell("A2").has_value() &&
                    !noop_sheet.try_cell("C2").has_value() &&
                    !noop_sheet.try_cell("D2").has_value(),
                "renamed formula delete-column missing query no-op output should keep old coordinates absent");
        });

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-column missing query reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-column missing query reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column missing query reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 4,
        "renamed formula delete-column missing query reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
        "renamed formula delete-column missing query reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_c3 =
        reopened_sheet.try_cell("C3");
    check(reopened_c3.has_value() &&
            reopened_c3->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_c3->text_value() == "#REF!+A1" &&
            reopened_c3->has_style() &&
            reopened_c3->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column missing query reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A1").number_value() == 1.0 &&
            reopened_sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("B3").text_value() == "row2-gap-c2",
        "renamed formula delete-column missing query reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("A2").has_value() &&
            !reopened_sheet.try_cell("C2").has_value() &&
            !reopened_sheet.try_cell("D2").has_value(),
        "renamed formula delete-column missing query reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_delete_columns_formula_invalid_reads_preserve_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-invalid-read-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-invalid-read-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-invalid-read-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-invalid-read-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_columns(1, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-column invalid reads first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column invalid reads first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column invalid reads first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column invalid reads first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula delete-column invalid reads matching reacquire should stay clean before failures");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula delete-column invalid reads should keep the old source name unavailable");
    const std::optional<fastxlsx::CellValue> saved_formula = reacquired.try_cell("C2");
    check(saved_formula.has_value() &&
            saved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula->text_value() == "#REF!+A1" &&
            saved_formula->has_style() &&
            saved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column invalid reads matching reacquire should read the saved styled formula");

    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "renamed formula delete-column invalid reads should reject row-zero try_cell on the original handle");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell(1, 0); }),
        "renamed formula delete-column invalid reads should reject column-zero get_cell on the reacquired handle");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell("a1"); }),
        "renamed formula delete-column invalid reads should reject lowercase A1 reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("XFE1"); }),
        "renamed formula delete-column invalid reads should reject A1 column overflow");
    check(threw_fastxlsx_error([&] {
        (void)sheet.sparse_cells(fastxlsx::CellRange {0, 1, 1, 1});
    }), "renamed formula delete-column invalid reads should reject invalid CellRange reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.sparse_cells("B2:A1"); }),
        "renamed formula delete-column invalid reads should reject reversed A1 range reads");
    check(threw_fastxlsx_error([&] {
        const std::array<fastxlsx::WorksheetCellReference, 2> invalid_batch {
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {1048577, 1},
        };
        (void)sheet.sparse_cells(invalid_batch);
    }), "renamed formula delete-column invalid reads should reject invalid coordinate batch reads");
    check(threw_fastxlsx_error([&] { (void)sheet.row_cells(0); }),
        "renamed formula delete-column invalid reads should reject row_cells row zero");
    check(threw_fastxlsx_error([&] { (void)reacquired.column_cells(16385); }),
        "renamed formula delete-column invalid reads should reject column_cells column overflow");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("D2"); }),
        "renamed formula delete-column invalid reads should reject valid but missing get_cell reads");

    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column invalid reads should not update last_edit_error");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column invalid reads should keep both planned-name handles clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column invalid reads should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column invalid reads should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column invalid reads should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula delete-column invalid reads should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula delete-column invalid reads should preserve the planned workbook catalog");
    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        "renamed formula delete-column invalid reads should preserve planned-name lookup state");
    check(reacquired.cell_count() == 4 && sheet.cell_count() == 4,
        "renamed formula delete-column invalid reads should preserve sparse counts");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 2, 3,
        "renamed formula delete-column invalid reads should preserve delete-column bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula = sheet.try_cell("C2");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == "#REF!+A1" &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column invalid reads should preserve the saved styled formula");
    check(sheet.get_cell("A1").number_value() == 1.0 &&
            reacquired.get_cell("A2").text_value() == "row2-gap-b2" &&
            reacquired.get_cell("B2").text_value() == "row2-gap-c2",
        "renamed formula delete-column invalid reads should preserve shifted source cells");
    check(!sheet.try_cell("D2").has_value() && !reacquired.try_cell("A3").has_value(),
        "renamed formula delete-column invalid reads should keep old shifted coordinates absent");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula delete-column invalid reads");

    reacquired.insert_rows(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula delete-column invalid reads later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete-column invalid reads later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 4,
        "renamed formula delete-column invalid reads later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula delete-column invalid reads later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("C3");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+A1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column invalid reads later shift should translate and preserve style id");
    check(sheet.get_cell("A1").number_value() == 1.0 &&
            sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
            sheet.get_cell("B3").text_value() == "row2-gap-c2",
        "renamed formula delete-column invalid reads later shift should move source-backed cells");
    check(!reacquired.try_cell("A2").has_value() &&
            !reacquired.try_cell("C2").has_value() &&
            !reacquired.try_cell("D2").has_value(),
        "renamed formula delete-column invalid reads later shift should keep old coordinates absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column invalid reads second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-column invalid reads second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column invalid reads second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column invalid reads first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-column invalid reads first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula delete-column invalid reads first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "renamed formula delete-column invalid reads first output should keep delete-column bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula delete-column invalid reads first output should keep the delete-column styled formula");
    check_not_contains(first_worksheet_xml, R"(r="C3")",
        "renamed formula delete-column invalid reads first output should not include the later row shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column invalid reads second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="C3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-column invalid reads second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula delete-column invalid reads second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "renamed formula delete-column invalid reads second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "renamed formula delete-column invalid reads second output should keep shifted B1");
    check_contains(second_worksheet_xml, R"(<c r="A3")",
        "renamed formula delete-column invalid reads second output should write shifted B2");
    check_contains(second_worksheet_xml, R"(<c r="B3")",
        "renamed formula delete-column invalid reads second output should write shifted C2");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula delete-column invalid reads second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="C2")",
        "renamed formula delete-column invalid reads second output should omit the old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="A2")",
        "renamed formula delete-column invalid reads second output should omit inserted blank A2");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column invalid reads no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-column invalid reads no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column invalid reads no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column invalid reads no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-column invalid reads no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-column invalid reads no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula delete-column invalid reads no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column invalid reads no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "RenamedData",
        "renamed formula delete-column invalid reads no-op output",
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 4,
                "renamed formula delete-column invalid reads no-op output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 3, 3,
                "renamed formula delete-column invalid reads no-op output should expose combined shifted bounds");
            const std::optional<fastxlsx::CellValue> noop_c3 =
                noop_sheet.try_cell("C3");
            check(noop_c3.has_value() &&
                    noop_c3->kind() == fastxlsx::CellValueKind::Formula &&
                    noop_c3->text_value() == "#REF!+A1" &&
                    noop_c3->has_style() &&
                    noop_c3->style_id().value() == styled_formula_style.value(),
                "renamed formula delete-column invalid reads no-op output should read translated styled formula");
            check(noop_sheet.get_cell("A1").number_value() == 1.0 &&
                    noop_sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
                    noop_sheet.get_cell("B3").text_value() == "row2-gap-c2",
                "renamed formula delete-column invalid reads no-op output should read shifted source cells");
            check(!noop_sheet.try_cell("A2").has_value() &&
                    !noop_sheet.try_cell("C2").has_value() &&
                    !noop_sheet.try_cell("D2").has_value(),
                "renamed formula delete-column invalid reads no-op output should keep old coordinates absent");
        });

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-column invalid reads reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-column invalid reads reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column invalid reads reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 4,
        "renamed formula delete-column invalid reads reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
        "renamed formula delete-column invalid reads reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_c3 =
        reopened_sheet.try_cell("C3");
    check(reopened_c3.has_value() &&
            reopened_c3->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_c3->text_value() == "#REF!+A1" &&
            reopened_c3->has_style() &&
            reopened_c3->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column invalid reads reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A1").number_value() == 1.0 &&
            reopened_sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("B3").text_value() == "row2-gap-c2",
        "renamed formula delete-column invalid reads reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("A2").has_value() &&
            !reopened_sheet.try_cell("C2").has_value() &&
            !reopened_sheet.try_cell("D2").has_value(),
        "renamed formula delete-column invalid reads reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_delete_columns_formula_snapshot_reads_preserve_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-snapshot-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-snapshot-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-snapshot-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-snapshot-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_columns(1, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-column snapshot reads first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column snapshot reads first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column snapshot reads first save should clear dirty materialized diagnostics");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column snapshot reads first save should leave the source package unchanged");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula delete-column snapshot reads matching reacquire should stay clean");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula delete-column snapshot reads should keep the old source name unavailable");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        reacquired.sparse_cells();
    check(all_cells.size() == 4,
        "renamed formula delete-column snapshot reads should return all saved sparse cells");
    check(all_cells[0].reference.row == 1 && all_cells[0].reference.column == 1 &&
            all_cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
            all_cells[0].value.number_value() == 1.0,
        "renamed formula delete-column snapshot reads should keep shifted B1 first");
    check(all_cells[1].reference.row == 2 && all_cells[1].reference.column == 1 &&
            all_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
            all_cells[1].value.text_value() == "row2-gap-b2",
        "renamed formula delete-column snapshot reads should keep shifted B2 second");
    check(all_cells[2].reference.row == 2 && all_cells[2].reference.column == 2 &&
            all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
            all_cells[2].value.text_value() == "row2-gap-c2",
        "renamed formula delete-column snapshot reads should keep shifted C2 third");
    check(all_cells[3].reference.row == 2 && all_cells[3].reference.column == 3 &&
            all_cells[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            all_cells[3].value.text_value() == "#REF!+A1" &&
            all_cells[3].value.has_style() &&
            all_cells[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column snapshot reads should expose the saved styled formula");

    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
        sheet.sparse_cells("A1:C2");
    check(shifted_range.size() == 4,
        "renamed formula delete-column snapshot reads should return represented cells in range order");
    check(shifted_range[0].reference.row == 1 && shifted_range[0].reference.column == 1 &&
            shifted_range[0].value.kind() == fastxlsx::CellValueKind::Number &&
            shifted_range[0].value.number_value() == 1.0,
        "renamed formula delete-column snapshot reads should keep shifted A1 first in range");
    check(shifted_range[3].reference.row == 2 && shifted_range[3].reference.column == 3 &&
            shifted_range[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_range[3].value.text_value() == "#REF!+A1" &&
            shifted_range[3].value.has_style() &&
            shifted_range[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column snapshot reads should expose styled formula in range order");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_two =
        reacquired.row_cells(2);
    check(row_two.size() == 3,
        "renamed formula delete-column snapshot reads row_cells should expose the shifted formula row");
    check(row_two[0].reference.row == 2 && row_two[0].reference.column == 1 &&
            row_two[0].value.text_value() == "row2-gap-b2",
        "renamed formula delete-column snapshot reads row_cells should keep shifted B2 first");
    check(row_two[1].reference.row == 2 && row_two[1].reference.column == 2 &&
            row_two[1].value.text_value() == "row2-gap-c2",
        "renamed formula delete-column snapshot reads row_cells should keep shifted C2 second");
    check(row_two[2].reference.row == 2 && row_two[2].reference.column == 3 &&
            row_two[2].value.kind() == fastxlsx::CellValueKind::Formula &&
            row_two[2].value.text_value() == "#REF!+A1" &&
            row_two[2].value.has_style() &&
            row_two[2].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column snapshot reads row_cells should keep styled formula last");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_three =
        sheet.column_cells(3);
    check(column_three.size() == 1,
        "renamed formula delete-column snapshot reads column_cells should expose the formula column");
    check(column_three[0].reference.row == 2 &&
            column_three[0].reference.column == 3 &&
            column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            column_three[0].value.text_value() == "#REF!+A1" &&
            column_three[0].value.has_style() &&
            column_three[0].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column snapshot reads column_cells should keep formula style id");

    const std::array<fastxlsx::WorksheetCellReference, 4> requested_refs {
        fastxlsx::WorksheetCellReference {2, 3},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {2, 3},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
        reacquired.sparse_cells(requested_refs);
    check(requested_cells.size() == 4,
        "renamed formula delete-column snapshot reads coordinate batch should keep requested represented cells");
    check(requested_cells[0].reference.row == 2 && requested_cells[0].reference.column == 3 &&
            requested_cells[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            requested_cells[0].value.text_value() == "#REF!+A1" &&
            requested_cells[0].value.has_style() &&
            requested_cells[0].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column snapshot reads coordinate batch should return C2 first");
    check(requested_cells[1].reference.row == 1 && requested_cells[1].reference.column == 1 &&
            requested_cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
            requested_cells[1].value.number_value() == 1.0,
        "renamed formula delete-column snapshot reads coordinate batch should return A1 second");
    check(requested_cells[2].reference.row == 2 && requested_cells[2].reference.column == 1 &&
            requested_cells[2].value.text_value() == "row2-gap-b2",
        "renamed formula delete-column snapshot reads coordinate batch should return A2 third");
    check(requested_cells[3].reference.row == 2 && requested_cells[3].reference.column == 3 &&
            requested_cells[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            requested_cells[3].value.text_value() == "#REF!+A1" &&
            requested_cells[3].value.has_style() &&
            requested_cells[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column snapshot reads coordinate batch should preserve duplicate C2 reads");

    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column snapshot reads should keep diagnostics clear");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column snapshot reads should keep both planned-name handles clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column snapshot reads should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column snapshot reads should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column snapshot reads should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula delete-column snapshot reads should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula delete-column snapshot reads should preserve the planned workbook catalog");
    check(reacquired.cell_count() == 4 && sheet.cell_count() == 4,
        "renamed formula delete-column snapshot reads should preserve sparse counts");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 2, 3,
        "renamed formula delete-column snapshot reads should preserve delete-column bounds");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula delete-column snapshot reads");

    reacquired.insert_rows(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(all_cells[3].reference.row == 2 && all_cells[3].reference.column == 3 &&
            all_cells[3].value.text_value() == "#REF!+A1" &&
            all_cells[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column snapshot reads should return owning snapshots across later shifts");
    check(row_two[2].reference.row == 2 && row_two[2].reference.column == 3 &&
            row_two[2].value.text_value() == "#REF!+A1" &&
            row_two[2].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column snapshot reads row snapshot should remain stable after later shifts");
    check(column_three[0].reference.row == 2 && column_three[0].reference.column == 3 &&
            column_three[0].value.text_value() == "#REF!+A1" &&
            column_three[0].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column snapshot reads column snapshot should remain stable after later shifts");
    check(requested_cells[3].reference.row == 2 && requested_cells[3].reference.column == 3 &&
            requested_cells[3].value.text_value() == "#REF!+A1" &&
            requested_cells[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column snapshot reads batch snapshot should remain stable after later shifts");
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula delete-column snapshot reads later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete-column snapshot reads later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 4,
        "renamed formula delete-column snapshot reads later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula delete-column snapshot reads later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("C3");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+A1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column snapshot reads later shift should translate and preserve style id");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column snapshot reads second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-column snapshot reads second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column snapshot reads second save should clear dirty diagnostics again");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column snapshot reads second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="C3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-column snapshot reads second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula delete-column snapshot reads second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "renamed formula delete-column snapshot reads second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula delete-column snapshot reads second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="C2")",
        "renamed formula delete-column snapshot reads second output should omit the old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="A2")",
        "renamed formula delete-column snapshot reads second output should omit inserted blank A2");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column snapshot reads no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-column snapshot reads no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column snapshot reads no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column snapshot reads no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-column snapshot reads no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-column snapshot reads no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula delete-column snapshot reads no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column snapshot reads no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "RenamedData",
        "renamed formula delete-column snapshot reads no-op output",
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 4,
                "renamed formula delete-column snapshot reads no-op output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 3, 3,
                "renamed formula delete-column snapshot reads no-op output should expose combined shifted bounds");
            const std::vector<fastxlsx::WorksheetCellSnapshot> noop_row_three =
                noop_sheet.row_cells(3);
            check(noop_row_three.size() == 3,
                "renamed formula delete-column snapshot reads no-op row_cells should expose shifted row three");
            check(noop_row_three[0].reference.row == 3 &&
                    noop_row_three[0].reference.column == 1 &&
                    noop_row_three[0].value.text_value() == "row2-gap-b2",
                "renamed formula delete-column snapshot reads no-op row_cells should read shifted B2");
            check(noop_row_three[1].reference.row == 3 &&
                    noop_row_three[1].reference.column == 2 &&
                    noop_row_three[1].value.text_value() == "row2-gap-c2",
                "renamed formula delete-column snapshot reads no-op row_cells should read shifted C2");
            check(noop_row_three[2].reference.row == 3 &&
                    noop_row_three[2].reference.column == 3 &&
                    noop_row_three[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                    noop_row_three[2].value.text_value() == "#REF!+A1" &&
                    noop_row_three[2].value.has_style() &&
                    noop_row_three[2].value.style_id().value() == styled_formula_style.value(),
                "renamed formula delete-column snapshot reads no-op row_cells should read translated styled formula");
            const std::vector<fastxlsx::WorksheetCellSnapshot> noop_column_three =
                noop_sheet.column_cells(3);
            check(noop_column_three.size() == 1 &&
                    noop_column_three[0].reference.row == 3 &&
                    noop_column_three[0].reference.column == 3 &&
                    noop_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    noop_column_three[0].value.text_value() == "#REF!+A1" &&
                    noop_column_three[0].value.has_style() &&
                    noop_column_three[0].value.style_id().value() == styled_formula_style.value(),
                "renamed formula delete-column snapshot reads no-op column_cells should read translated styled formula");
            check(!noop_sheet.try_cell("A2").has_value() &&
                    !noop_sheet.try_cell("C2").has_value() &&
                    !noop_sheet.try_cell("D2").has_value(),
                "renamed formula delete-column snapshot reads no-op output should keep old coordinates absent");
        });

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-column snapshot reads reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-column snapshot reads reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column snapshot reads reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 4,
        "renamed formula delete-column snapshot reads reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
        "renamed formula delete-column snapshot reads reopened output should expose combined shifted bounds");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_three =
        reopened_sheet.row_cells(3);
    check(reopened_row_three.size() == 3,
        "renamed formula delete-column snapshot reads reopened row_cells should expose shifted row three");
    check(reopened_row_three[0].reference.row == 3 && reopened_row_three[0].reference.column == 1 &&
            reopened_row_three[0].value.text_value() == "row2-gap-b2",
        "renamed formula delete-column snapshot reads reopened row_cells should read shifted B2");
    check(reopened_row_three[1].reference.row == 3 && reopened_row_three[1].reference.column == 2 &&
            reopened_row_three[1].value.text_value() == "row2-gap-c2",
        "renamed formula delete-column snapshot reads reopened row_cells should read shifted C2");
    check(reopened_row_three[2].reference.row == 3 && reopened_row_three[2].reference.column == 3 &&
            reopened_row_three[2].value.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_row_three[2].value.text_value() == "#REF!+A1" &&
            reopened_row_three[2].value.has_style() &&
            reopened_row_three[2].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column snapshot reads reopened row_cells should read translated styled formula");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
        reopened_sheet.column_cells(3);
    check(reopened_column_three.size() == 1 &&
            reopened_column_three[0].reference.row == 3 &&
            reopened_column_three[0].reference.column == 3 &&
            reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_column_three[0].value.text_value() == "#REF!+A1" &&
            reopened_column_three[0].value.has_style() &&
            reopened_column_three[0].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column snapshot reads reopened column_cells should read translated styled formula");
    check(!reopened_sheet.try_cell("A2").has_value() &&
            !reopened_sheet.try_cell("C2").has_value() &&
            !reopened_sheet.try_cell("D2").has_value(),
        "renamed formula delete-column snapshot reads reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_delete_columns_formula_reacquire_reuses_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-reacquire-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-reacquire-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-reacquire-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-column-formula-reacquire-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_columns(1, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-column reacquire first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column reacquire first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column reacquire first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column reacquire first save should keep diagnostics clear");

    std::optional<fastxlsx::WorksheetEditor> maybe_reacquired =
        editor.try_worksheet("RenamedData");
    check(maybe_reacquired.has_value(),
        "renamed formula delete-column reacquire should find the planned-name saved session");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula delete-column reacquire should keep the old source name unavailable");
    if (!maybe_reacquired.has_value()) {
        return;
    }

    fastxlsx::WorksheetEditor reacquired = std::move(*maybe_reacquired);
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula delete-column reacquire should return the saved clean styled session");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula delete-column reacquire should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula delete-column reacquire should preserve the planned workbook catalog");
    check(reacquired.cell_count() == 4 && sheet.cell_count() == 4,
        "renamed formula delete-column reacquire should keep shifted sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 2, 3,
        "renamed formula delete-column reacquire should expose delete-column bounds");
    const std::optional<fastxlsx::CellValue> reacquired_c2 =
        reacquired.try_cell("C2");
    check(reacquired_c2.has_value() &&
            reacquired_c2->kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_c2->text_value() == "#REF!+A1" &&
            reacquired_c2->has_style() &&
            reacquired_c2->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column reacquire should read the saved translated styled formula");
    check(sheet.get_cell("A1").number_value() == 1.0 &&
            reacquired.get_cell("A2").text_value() == "row2-gap-b2" &&
            reacquired.get_cell("B2").text_value() == "row2-gap-c2",
        "renamed formula delete-column reacquire should read shifted source-backed cells");
    check(!sheet.try_cell("D2").has_value() &&
            !sheet.try_cell("A3").has_value(),
        "renamed formula delete-column reacquire should keep old coordinates absent");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-column reacquire should keep diagnostics clear");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-column reacquire should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column reacquire should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column reacquire should not queue replacement diagnostics");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula delete-column reacquire");

    reacquired.insert_rows(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula delete-column reacquire later row shift should dirty the shared session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete-column reacquire later row shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 4,
        "renamed formula delete-column reacquire later row shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula delete-column reacquire later row shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("C3");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+A1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column reacquire later row shift should translate and preserve style id");
    check(sheet.get_cell("A1").number_value() == 1.0 &&
            sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
            sheet.get_cell("B3").text_value() == "row2-gap-c2",
        "renamed formula delete-column reacquire later row shift should move source-backed cells");
    check(!reacquired.try_cell("A2").has_value() &&
            !reacquired.try_cell("C2").has_value() &&
            !reacquired.try_cell("D2").has_value(),
        "renamed formula delete-column reacquire later row shift should keep old coordinates absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column reacquire second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-column reacquire second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column reacquire second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column reacquire first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-column reacquire first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula delete-column reacquire first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "renamed formula delete-column reacquire first output should keep delete-column bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula delete-column reacquire first output should keep translated formula with style id");
    check_not_contains(first_worksheet_xml, R"(r="C3")",
        "renamed formula delete-column reacquire first output should not include the later row shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column reacquire second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="C3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-column reacquire second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula delete-column reacquire second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:C3"/>)",
        "renamed formula delete-column reacquire second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "renamed formula delete-column reacquire second output should keep shifted B1");
    check_contains(second_worksheet_xml, R"(<c r="A3")",
        "renamed formula delete-column reacquire second output should write shifted B2");
    check_contains(second_worksheet_xml, R"(<c r="B3")",
        "renamed formula delete-column reacquire second output should write shifted C2");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula delete-column reacquire second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="C2")",
        "renamed formula delete-column reacquire second output should omit the old formula coordinate");
    check_not_contains(second_worksheet_xml, R"(r="A2")",
        "renamed formula delete-column reacquire second output should omit inserted blank A2");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-column reacquire no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-column reacquire no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column reacquire no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-column reacquire no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-column reacquire no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-column reacquire no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula delete-column reacquire no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-column reacquire no-op save should leave the source package unchanged");
    check_reopened_clean_sheet_output(
        noop_output, "RenamedData",
        "renamed formula delete-column reacquire no-op output",
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 4,
                "renamed formula delete-column reacquire no-op output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 3, 3,
                "renamed formula delete-column reacquire no-op output should expose combined shifted bounds");
            const std::optional<fastxlsx::CellValue> noop_c3 =
                noop_sheet.try_cell("C3");
            check(noop_c3.has_value() &&
                    noop_c3->kind() == fastxlsx::CellValueKind::Formula &&
                    noop_c3->text_value() == "#REF!+A1" &&
                    noop_c3->has_style() &&
                    noop_c3->style_id().value() == styled_formula_style.value(),
                "renamed formula delete-column reacquire no-op output should read translated styled formula");
            check(noop_sheet.get_cell("A1").number_value() == 1.0 &&
                    noop_sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
                    noop_sheet.get_cell("B3").text_value() == "row2-gap-c2",
                "renamed formula delete-column reacquire no-op output should read shifted source cells");
            check(!noop_sheet.try_cell("A2").has_value() &&
                    !noop_sheet.try_cell("C2").has_value() &&
                    !noop_sheet.try_cell("D2").has_value(),
                "renamed formula delete-column reacquire no-op output should keep old coordinates absent");
        });

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-column reacquire reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-column reacquire reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-column reacquire reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 4,
        "renamed formula delete-column reacquire reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 3, 3,
        "renamed formula delete-column reacquire reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_c3 =
        reopened_sheet.try_cell("C3");
    check(reopened_c3.has_value() &&
            reopened_c3->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_c3->text_value() == "#REF!+A1" &&
            reopened_c3->has_style() &&
            reopened_c3->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-column reacquire reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A1").number_value() == 1.0 &&
            reopened_sheet.get_cell("A3").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("B3").text_value() == "row2-gap-c2",
        "renamed formula delete-column reacquire reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("A2").has_value() &&
            !reopened_sheet.try_cell("C2").has_value() &&
            !reopened_sheet.try_cell("D2").has_value(),
        "renamed formula delete-column reacquire reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_deletes_formula_rows()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-ref-row-formula-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-ref-row-formula-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-ref-row-formula-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    check_workbook_editor_renamed_formula_pre_materialization_diagnostics(
        editor, "renamed formula delete_rows");
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_rows(1, 1);
    const std::size_t shifted_memory = sheet.estimated_memory_usage();

    check(sheet.cell_count() == 5,
        "renamed formula delete_rows should remove deleted-row records");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
        "renamed formula delete_rows should expose shifted bounds");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D1");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+#REF!" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete_rows should translate deleted row references and preserve style id");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete_rows should shift source-backed rows through the planned name");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A3").has_value(),
        "renamed formula delete_rows should keep old shifted coordinates absent");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete_rows should report dirty materialized state under the planned name");
    check(editor.pending_materialized_cell_count() == 5,
        "renamed formula delete_rows should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "renamed formula delete_rows should report shifted materialized memory estimate");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete_rows save_as should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete_rows save_as should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete_rows save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete_rows save_as should leave the source package unchanged");
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(workbook_xml, R"(name="RenamedData")",
        "renamed formula delete_rows save_as should write the planned catalog name");
    check_not_contains(workbook_xml, R"(name="Data")",
        "renamed formula delete_rows save_as should omit the source catalog name");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "renamed formula delete_rows save_as should project shifted bounds");
    check_contains(worksheet_xml, styled_formula_xml,
        "renamed formula delete_rows save_as should write #REF formula with style id");
    check_contains(worksheet_xml, R"(<c r="A1")",
        "renamed formula delete_rows save_as should write shifted source A2");
    check_contains(worksheet_xml, R"(<c r="B1")",
        "renamed formula delete_rows save_as should write shifted source B2");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "renamed formula delete_rows save_as should write shifted source trailing row");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "renamed formula delete_rows save_as should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "renamed formula delete_rows save_as should omit old trailing row coordinate");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete_rows reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete_rows reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete_rows reopened output should not expose pending diagnostics");
    check(reopened_sheet.cell_count() == 5,
        "renamed formula delete_rows reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
        "renamed formula delete_rows reopened output should expose shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_d1 =
        reopened_sheet.try_cell("D1");
    check(reopened_d1.has_value() &&
            reopened_d1->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_d1->text_value() == "#REF!+#REF!" &&
            reopened_d1->has_style() &&
            reopened_d1->style_id().value() == styled_formula_style.value(),
        "renamed formula delete_rows reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            reopened_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete_rows reopened output should read shifted source rows");
    check(!reopened_sheet.try_cell("D2").has_value() &&
            !reopened_sheet.try_cell("A3").has_value(),
        "renamed formula delete_rows reopened output should keep old coordinates absent");

    check_public_state_renamed_clean_noop_save(
        editor, sheet, noop_output, output_entries, "renamed formula delete_rows", 2,
        [styled_formula_style](fastxlsx::WorksheetEditor& noop_sheet) {
            check(noop_sheet.cell_count() == 5,
                "renamed formula delete_rows no-op save reopened output should keep shifted sparse count");
            check_cell_range_equals(noop_sheet.used_range(), 1, 1, 2, 4,
                "renamed formula delete_rows no-op save reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> noop_d1 =
                noop_sheet.try_cell("D1");
            check(noop_d1.has_value() &&
                    noop_d1->kind() == fastxlsx::CellValueKind::Formula &&
                    noop_d1->text_value() == "#REF!+#REF!" &&
                    noop_d1->has_style() &&
                    noop_d1->style_id().value() == styled_formula_style.value(),
                "renamed formula delete_rows no-op save reopened output should read translated styled formula");
            check(noop_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    noop_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                    noop_sheet.get_cell("A2").text_value() == "extra-c3",
                "renamed formula delete_rows no-op save reopened output should read shifted source rows");
            check(!noop_sheet.try_cell("D2").has_value() &&
                    !noop_sheet.try_cell("A3").has_value(),
                "renamed formula delete_rows no-op save reopened output should keep old coordinates absent");
        });
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete_rows no-op save should leave the source package unchanged");
}

void test_public_worksheet_editor_shift_after_rename_delete_rows_formula_failed_save_preserves_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-failed-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-failed-save-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-failed-save-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-failed-save-second-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");

    sheet.delete_rows(1, 1);
    const std::size_t shifted_memory = sheet.estimated_memory_usage();
    const auto check_dirty_styled_formula_session = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep last_edit_error clear");
        check(editor.has_pending_changes(),
            label + " should keep the public editor dirty");
        check(editor.pending_change_count() == 1,
            label + " should keep only the queued rename before materialized handoff");
        check(sheet.has_pending_changes(),
            label + " should keep the styled formula handle dirty");
        check(sheet.cell_count() == 5,
            label + " should preserve the delete-row sparse count");
        check(sheet.estimated_memory_usage() == shifted_memory,
            label + " should preserve the delete-row materialized memory estimate");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"RenamedData"},
            label + " should report dirty materialized state under the planned name");
        check(editor.pending_materialized_cell_count() == 5,
            label + " should report the delete-row styled formula sparse count");
        check(editor.estimated_pending_materialized_memory_usage() == shifted_memory,
            label + " should report the delete-row styled formula memory estimate");
        check(editor.source_worksheet_names() == expected_source_names,
            label + " should preserve source worksheet names");
        check(editor.worksheet_names() == expected_planned_names,
            label + " should preserve planned worksheet names");
        check(workbook_editor_catalog_entries_equal(
                  editor.worksheet_catalog(), expected_catalog),
            label + " should preserve the renamed worksheet catalog");
        check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
            label + " should expose only the planned sheet name");
        check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
            label + " should preserve delete-row bounds");

        const std::optional<fastxlsx::CellValue> shifted_formula =
            sheet.try_cell("D1");
        check(shifted_formula.has_value() &&
                shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula->text_value() == "#REF!+#REF!" &&
                shifted_formula->has_style() &&
                shifted_formula->style_id().value() == styled_formula_style.value(),
            label + " should keep the delete-row styled formula in memory");
        check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                sheet.get_cell("C1").text_value() == "row2-gap-c2" &&
                sheet.get_cell("A2").text_value() == "extra-c3",
            label + " should keep shifted source rows in memory");
        check(!sheet.try_cell("D2").has_value() &&
                !sheet.try_cell("A3").has_value(),
            label + " should keep old and deleted coordinates absent");
    };

    check_dirty_styled_formula_session(
        "renamed formula delete-row failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "renamed formula delete-row failed save should reject exact source overwrite");
    check_dirty_styled_formula_session(
        "renamed formula delete-row failed save rejected source overwrite");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string source_workbook_xml = source_entries.at("xl/workbook.xml");
    const std::string source_worksheet_xml = source_entries.at("xl/worksheets/sheet1.xml");
    const std::string source_styled_formula_xml =
        std::string(R"(<c r="D2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>A1+B1</f></c>)";
    check_contains(source_workbook_xml, R"(name="Data")",
        "renamed formula delete-row failed save should leave the source workbook catalog unchanged");
    check_not_contains(source_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-row failed save should not write the planned name into the source workbook");
    check_contains(source_worksheet_xml, R"(<dimension ref="A1:D3"/>)",
        "renamed formula delete-row failed save should leave the source workbook bounds unchanged");
    check_contains(source_worksheet_xml, source_styled_formula_xml,
        "renamed formula delete-row failed save should leave the source styled formula unchanged");
    check_contains(source_worksheet_xml, R"(<c r="A3" t="inlineStr"><is><t>extra-c3</t></is></c>)",
        "renamed formula delete-row failed save should leave the source trailing row unchanged");
    check_not_contains(source_worksheet_xml, "#REF!+#REF!",
        "renamed formula delete-row failed save should not write the translated formula into the source workbook");
    check_not_contains(source_worksheet_xml, R"(r="D1")",
        "renamed formula delete-row failed save should not write shifted formula coordinates into the source workbook");

    editor.save_as(output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-row failed save safe retry should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row failed save safe retry should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row failed save safe retry should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row failed save safe retry should keep diagnostics clear");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string output_workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string output_worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string output_styled_formula_xml =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(output_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-row failed save safe retry should write the planned catalog name");
    check_not_contains(output_workbook_xml, R"(name="Data")",
        "renamed formula delete-row failed save safe retry should omit the source catalog name");
    check_contains(output_worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "renamed formula delete-row failed save safe retry should project delete-row bounds");
    check_contains(output_worksheet_xml, R"(<c r="A1")",
        "renamed formula delete-row failed save safe retry should write shifted source A2");
    check_contains(output_worksheet_xml, R"(<c r="B1")",
        "renamed formula delete-row failed save safe retry should write shifted source B2");
    check_contains(output_worksheet_xml, R"(<c r="C1")",
        "renamed formula delete-row failed save safe retry should write shifted source C2");
    check_contains(output_worksheet_xml, R"(<c r="A2")",
        "renamed formula delete-row failed save safe retry should write shifted source trailing row");
    check_contains(output_worksheet_xml, output_styled_formula_xml,
        "renamed formula delete-row failed save safe retry should write translated formula with style id");
    check_not_contains(output_worksheet_xml, R"(r="D2")",
        "renamed formula delete-row failed save safe retry should omit the old formula coordinate");
    check_not_contains(output_worksheet_xml, R"(r="A3")",
        "renamed formula delete-row failed save safe retry should omit the old trailing coordinate");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-row failed save no-op retry should keep the styled handle clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row failed save no-op retry should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row failed save no-op retry should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row failed save no-op retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row failed save no-op retry should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-row failed save no-op retry");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-row failed save no-op retry");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "renamed formula delete-row failed save no-op retry should keep output entries stable");
    check_reopened_delete_row_formula_noop_output(
        noop_output, styled_formula_style,
        "renamed formula delete-row failed save no-op output");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-row failed save second no-op retry should keep the styled handle clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row failed save second no-op retry should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row failed save second no-op retry should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row failed save second no-op retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row failed save second no-op retry should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "renamed formula delete-row failed save second no-op retry");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "renamed formula delete-row failed save second no-op retry");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row failed save second no-op retry should keep source entries unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "renamed formula delete-row failed save second no-op retry should leave the safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "renamed formula delete-row failed save second no-op retry should leave the first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == noop_entries,
        "renamed formula delete-row failed save second no-op retry should keep output entries stable");
    check_reopened_delete_row_formula_noop_output(
        second_noop_output, styled_formula_style,
        "renamed formula delete-row failed save second no-op output");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-row failed save reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-row failed save reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row failed save reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 5,
        "renamed formula delete-row failed save reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
        "renamed formula delete-row failed save reopened output should expose shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_d1 =
        reopened_sheet.try_cell("D1");
    check(reopened_d1.has_value() &&
            reopened_d1->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_d1->text_value() == "#REF!+#REF!" &&
            reopened_d1->has_style() &&
            reopened_d1->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row failed save reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            reopened_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("C1").text_value() == "row2-gap-c2" &&
            reopened_sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row failed save reopened output should read shifted source rows");
    check(!reopened_sheet.try_cell("D2").has_value() &&
            !reopened_sheet.try_cell("A3").has_value(),
        "renamed formula delete-row failed save reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_delete_rows_formula_option_mismatch_preserves_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-options-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-options-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-options-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-options-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_rows(1, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-row option mismatch first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row option mismatch first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row option mismatch first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row option mismatch first save should keep diagnostics clear");

    fastxlsx::WorksheetEditorOptions mismatched_options;
    mismatched_options.max_cells = 2;
    check(threw_fastxlsx_error([&] {
        (void)editor.try_worksheet("RenamedData", mismatched_options);
    }), "renamed formula delete-row option mismatch try_worksheet should reject different options");
    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("RenamedData", mismatched_options);
    }), "renamed formula delete-row option mismatch worksheet should reject different options");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula delete-row option mismatch should keep the old source name unavailable");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row option mismatch should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        "renamed formula delete-row option mismatch should leave the saved planned-name handle clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row option mismatch should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row option mismatch should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row option mismatch should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula delete-row option mismatch should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula delete-row option mismatch should preserve the planned workbook catalog");
    const std::optional<fastxlsx::CellValue> saved_formula = sheet.try_cell("D1");
    check(saved_formula.has_value() &&
            saved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula->text_value() == "#REF!+#REF!" &&
            saved_formula->has_style() &&
            saved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row option mismatch should preserve the saved styled formula");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
            sheet.get_cell("C1").text_value() == "row2-gap-c2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row option mismatch should preserve shifted source rows");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A3").has_value(),
        "renamed formula delete-row option mismatch should keep old shifted coordinates absent");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula delete-row option mismatch matching reacquire should stay clean");
    check(reacquired.name() == "RenamedData",
        "renamed formula delete-row option mismatch matching reacquire should expose the planned name");
    const std::optional<fastxlsx::CellValue> reacquired_formula =
        reacquired.try_cell("D1");
    check(reacquired_formula.has_value() &&
            reacquired_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_formula->text_value() == "#REF!+#REF!" &&
            reacquired_formula->has_style() &&
            reacquired_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row option mismatch matching reacquire should reuse the saved styled formula");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula delete-row option mismatch");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula delete-row option mismatch later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete-row option mismatch later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 5,
        "renamed formula delete-row option mismatch later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula delete-row option mismatch later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E1");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+#REF!" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row option mismatch later shift should preserve #REF formula and style id");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            sheet.get_cell("C1").text_value() == "row2-gap-b2" &&
            sheet.get_cell("D1").text_value() == "row2-gap-c2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row option mismatch later shift should move source-backed cells");
    check(!reacquired.try_cell("B1").has_value() &&
            !reacquired.try_cell("D2").has_value() &&
            !reacquired.try_cell("A3").has_value(),
        "renamed formula delete-row option mismatch later shift should keep old coordinates absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row option mismatch second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-row option mismatch second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row option mismatch second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row option mismatch first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-row option mismatch first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula delete-row option mismatch first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "renamed formula delete-row option mismatch first output should keep delete-row bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula delete-row option mismatch first output should keep the delete-row styled formula");
    check_not_contains(first_worksheet_xml, R"(r="E1")",
        "renamed formula delete-row option mismatch first output should not include the later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row option mismatch second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="E1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-row option mismatch second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula delete-row option mismatch second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:E2"/>)",
        "renamed formula delete-row option mismatch second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="A1")",
        "renamed formula delete-row option mismatch second output should keep shifted A2");
    check_contains(second_worksheet_xml, R"(<c r="C1")",
        "renamed formula delete-row option mismatch second output should write shifted B2");
    check_contains(second_worksheet_xml, R"(<c r="D1")",
        "renamed formula delete-row option mismatch second output should write shifted C2");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula delete-row option mismatch second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "renamed formula delete-row option mismatch second output should omit inserted blank B1");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed formula delete-row option mismatch second output should omit the old formula coordinate");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row option mismatch no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-row option mismatch no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row option mismatch no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row option mismatch no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-row option mismatch no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-row option mismatch no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula delete-row option mismatch no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row option mismatch no-op save should leave the source package unchanged");
    check_reopened_delete_row_formula_column_shift_noop_output(
        noop_output, styled_formula_style,
        "renamed formula delete-row option mismatch no-op output");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-row option mismatch reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-row option mismatch reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row option mismatch reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 5,
        "renamed formula delete-row option mismatch reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 5,
        "renamed formula delete-row option mismatch reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_e1 =
        reopened_sheet.try_cell("E1");
    check(reopened_e1.has_value() &&
            reopened_e1->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_e1->text_value() == "#REF!+#REF!" &&
            reopened_e1->has_style() &&
            reopened_e1->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row option mismatch reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            reopened_sheet.get_cell("C1").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("D1").text_value() == "row2-gap-c2" &&
            reopened_sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row option mismatch reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("D2").has_value() &&
            !reopened_sheet.try_cell("A3").has_value(),
        "renamed formula delete-row option mismatch reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_delete_rows_formula_invalid_mutations_preserve_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-invalid-mutation-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-invalid-mutation-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-invalid-mutation-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-invalid-mutation-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_rows(1, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-row invalid mutations first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row invalid mutations first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row invalid mutations first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row invalid mutations first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula delete-row invalid mutations matching reacquire should stay clean before failures");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula delete-row invalid mutations should keep the old source name unavailable");
    const std::optional<fastxlsx::CellValue> saved_formula = reacquired.try_cell("D1");
    check(saved_formula.has_value() &&
            saved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula->text_value() == "#REF!+#REF!" &&
            saved_formula->has_style() &&
            saved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row invalid mutations matching reacquire should read the saved styled formula");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell(0, 1,
            fastxlsx::CellValue::formula("invalid-renamed-delete-row-formula-row-zero"));
    }), "renamed formula delete-row invalid mutations should reject formula set_cell row zero");
    check(threw_fastxlsx_error([&] {
        reacquired.set_cell("XFE1",
            fastxlsx::CellValue::formula("invalid-renamed-delete-row-formula-a1-overflow"));
    }), "renamed formula delete-row invalid mutations should reject formula set_cell column overflow");
    check(threw_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "renamed formula delete-row invalid mutations should reject range erase_cell references");

    check(editor.last_edit_error().has_value(),
        "renamed formula delete-row invalid mutations should populate last_edit_error");
    if (editor.last_edit_error().has_value()) {
        check_contains(*editor.last_edit_error(), "WorksheetEditor cell reference is invalid",
            "renamed formula delete-row invalid mutations should expose the latest invalid reference diagnostic");
    }
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row invalid mutations should keep both planned-name handles clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row invalid mutations should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row invalid mutations should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row invalid mutations should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula delete-row invalid mutations should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula delete-row invalid mutations should preserve the planned workbook catalog");
    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        "renamed formula delete-row invalid mutations should preserve planned-name lookup state");
    const std::optional<fastxlsx::CellValue> preserved_formula = sheet.try_cell("D1");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == "#REF!+#REF!" &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row invalid mutations should preserve the saved styled formula");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            reacquired.get_cell("B1").text_value() == "row2-gap-b2" &&
            reacquired.get_cell("C1").text_value() == "row2-gap-c2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row invalid mutations should preserve shifted source cells");
    check(!sheet.try_cell("D2").has_value() &&
            !reacquired.try_cell("A3").has_value(),
        "renamed formula delete-row invalid mutations should keep old shifted coordinates absent");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula delete-row invalid mutations");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row invalid mutations later valid shift should clear diagnostics");
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula delete-row invalid mutations later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete-row invalid mutations later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 5,
        "renamed formula delete-row invalid mutations later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula delete-row invalid mutations later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E1");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+#REF!" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row invalid mutations later shift should preserve #REF formula and style id");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            sheet.get_cell("C1").text_value() == "row2-gap-b2" &&
            sheet.get_cell("D1").text_value() == "row2-gap-c2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row invalid mutations later shift should move source-backed cells");
    check(!reacquired.try_cell("B1").has_value() &&
            !reacquired.try_cell("D2").has_value() &&
            !reacquired.try_cell("A3").has_value(),
        "renamed formula delete-row invalid mutations later shift should keep old coordinates absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row invalid mutations second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-row invalid mutations second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row invalid mutations second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row invalid mutations first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-row invalid mutations first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula delete-row invalid mutations first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "renamed formula delete-row invalid mutations first output should keep delete-row bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula delete-row invalid mutations first output should keep the delete-row styled formula");
    check_not_contains(first_worksheet_xml, "invalid-renamed-delete-row-formula-",
        "renamed formula delete-row invalid mutations first output should not contain rejected payloads");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row invalid mutations second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="E1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-row invalid mutations second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula delete-row invalid mutations second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:E2"/>)",
        "renamed formula delete-row invalid mutations second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="A1")",
        "renamed formula delete-row invalid mutations second output should keep shifted A2");
    check_contains(second_worksheet_xml, R"(<c r="C1")",
        "renamed formula delete-row invalid mutations second output should write shifted B2");
    check_contains(second_worksheet_xml, R"(<c r="D1")",
        "renamed formula delete-row invalid mutations second output should write shifted C2");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula delete-row invalid mutations second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, "invalid-renamed-delete-row-formula-",
        "renamed formula delete-row invalid mutations second output should not leak rejected payloads");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "renamed formula delete-row invalid mutations second output should omit inserted blank B1");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed formula delete-row invalid mutations second output should omit the old formula coordinate");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row invalid mutations no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-row invalid mutations no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row invalid mutations no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row invalid mutations no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-row invalid mutations no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-row invalid mutations no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula delete-row invalid mutations no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row invalid mutations no-op save should leave the source package unchanged");
    check_reopened_delete_row_formula_column_shift_noop_output(
        noop_output, styled_formula_style,
        "renamed formula delete-row invalid mutations no-op output");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-row invalid mutations reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-row invalid mutations reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row invalid mutations reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 5,
        "renamed formula delete-row invalid mutations reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 5,
        "renamed formula delete-row invalid mutations reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_e1 =
        reopened_sheet.try_cell("E1");
    check(reopened_e1.has_value() &&
            reopened_e1->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_e1->text_value() == "#REF!+#REF!" &&
            reopened_e1->has_style() &&
            reopened_e1->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row invalid mutations reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            reopened_sheet.get_cell("C1").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("D1").text_value() == "row2-gap-c2" &&
            reopened_sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row invalid mutations reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("D2").has_value() &&
            !reopened_sheet.try_cell("A3").has_value(),
        "renamed formula delete-row invalid mutations reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_delete_rows_formula_missing_query_preserves_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-missing-query-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-missing-query-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-missing-query-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-missing-query-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_rows(1, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-row missing query first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row missing query first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row missing query first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row missing query first save should keep diagnostics clear");

    const std::optional<fastxlsx::WorksheetEditor> missing =
        editor.try_worksheet("Missing");
    check(!missing.has_value(),
        "renamed formula delete-row missing query try_worksheet should report a missing sheet");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula delete-row missing query should keep the old source name unavailable");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Missing"); }),
        "renamed formula delete-row missing query worksheet should reject a missing sheet");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet("Data"); }),
        "renamed formula delete-row missing query worksheet should reject the old source name");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row missing query should not update last_edit_error");
    check(!sheet.has_pending_changes(),
        "renamed formula delete-row missing query should leave the saved planned-name handle clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row missing query should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row missing query should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row missing query should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula delete-row missing query should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula delete-row missing query should preserve the planned workbook catalog");
    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        "renamed formula delete-row missing query should preserve planned-name lookup state");
    const std::optional<fastxlsx::CellValue> saved_formula = sheet.try_cell("D1");
    check(saved_formula.has_value() &&
            saved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula->text_value() == "#REF!+#REF!" &&
            saved_formula->has_style() &&
            saved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row missing query should preserve the saved styled formula");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
            sheet.get_cell("C1").text_value() == "row2-gap-c2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row missing query should preserve shifted source rows");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A3").has_value(),
        "renamed formula delete-row missing query should keep old shifted coordinates absent");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula delete-row missing query matching reacquire should stay clean");
    const std::optional<fastxlsx::CellValue> reacquired_formula =
        reacquired.try_cell("D1");
    check(reacquired_formula.has_value() &&
            reacquired_formula->kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_formula->text_value() == "#REF!+#REF!" &&
            reacquired_formula->has_style() &&
            reacquired_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row missing query matching reacquire should reuse the saved styled formula");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula delete-row missing query");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula delete-row missing query later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete-row missing query later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 5,
        "renamed formula delete-row missing query later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula delete-row missing query later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E1");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+#REF!" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row missing query later shift should preserve #REF formula and style id");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            sheet.get_cell("C1").text_value() == "row2-gap-b2" &&
            sheet.get_cell("D1").text_value() == "row2-gap-c2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row missing query later shift should move source-backed cells");
    check(!reacquired.try_cell("B1").has_value() &&
            !reacquired.try_cell("D2").has_value() &&
            !reacquired.try_cell("A3").has_value(),
        "renamed formula delete-row missing query later shift should keep old coordinates absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row missing query second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-row missing query second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row missing query second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row missing query first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-row missing query first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula delete-row missing query first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "renamed formula delete-row missing query first output should keep delete-row bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula delete-row missing query first output should keep the delete-row styled formula");
    check_not_contains(first_worksheet_xml, R"(r="E1")",
        "renamed formula delete-row missing query first output should not include the later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row missing query second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="E1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-row missing query second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula delete-row missing query second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:E2"/>)",
        "renamed formula delete-row missing query second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="A1")",
        "renamed formula delete-row missing query second output should keep shifted A2");
    check_contains(second_worksheet_xml, R"(<c r="C1")",
        "renamed formula delete-row missing query second output should write shifted B2");
    check_contains(second_worksheet_xml, R"(<c r="D1")",
        "renamed formula delete-row missing query second output should write shifted C2");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula delete-row missing query second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "renamed formula delete-row missing query second output should omit inserted blank B1");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed formula delete-row missing query second output should omit the old formula coordinate");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row missing query no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-row missing query no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row missing query no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row missing query no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-row missing query no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-row missing query no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula delete-row missing query no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row missing query no-op save should leave the source package unchanged");
    check_reopened_delete_row_formula_column_shift_noop_output(
        noop_output, styled_formula_style,
        "renamed formula delete-row missing query no-op output");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-row missing query reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-row missing query reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row missing query reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 5,
        "renamed formula delete-row missing query reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 5,
        "renamed formula delete-row missing query reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_e1 =
        reopened_sheet.try_cell("E1");
    check(reopened_e1.has_value() &&
            reopened_e1->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_e1->text_value() == "#REF!+#REF!" &&
            reopened_e1->has_style() &&
            reopened_e1->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row missing query reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            reopened_sheet.get_cell("C1").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("D1").text_value() == "row2-gap-c2" &&
            reopened_sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row missing query reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("D2").has_value() &&
            !reopened_sheet.try_cell("A3").has_value(),
        "renamed formula delete-row missing query reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_delete_rows_formula_invalid_reads_preserve_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-invalid-read-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-invalid-read-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-invalid-read-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-invalid-read-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_rows(1, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-row invalid reads first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row invalid reads first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row invalid reads first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row invalid reads first save should keep diagnostics clear");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula delete-row invalid reads matching reacquire should stay clean before failures");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula delete-row invalid reads should keep the old source name unavailable");
    const std::optional<fastxlsx::CellValue> saved_formula = reacquired.try_cell("D1");
    check(saved_formula.has_value() &&
            saved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            saved_formula->text_value() == "#REF!+#REF!" &&
            saved_formula->has_style() &&
            saved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row invalid reads matching reacquire should read the saved styled formula");

    check(threw_fastxlsx_error([&] { (void)sheet.try_cell(0, 1); }),
        "renamed formula delete-row invalid reads should reject row-zero try_cell on the original handle");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell(1, 0); }),
        "renamed formula delete-row invalid reads should reject column-zero get_cell on the reacquired handle");
    check(threw_fastxlsx_error([&] { (void)sheet.try_cell("a1"); }),
        "renamed formula delete-row invalid reads should reject lowercase A1 reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("XFE1"); }),
        "renamed formula delete-row invalid reads should reject A1 column overflow");
    check(threw_fastxlsx_error([&] {
        (void)sheet.sparse_cells(fastxlsx::CellRange {0, 1, 1, 1});
    }), "renamed formula delete-row invalid reads should reject invalid CellRange reads");
    check(threw_fastxlsx_error([&] { (void)reacquired.sparse_cells("B2:A1"); }),
        "renamed formula delete-row invalid reads should reject reversed A1 range reads");
    check(threw_fastxlsx_error([&] {
        const std::array<fastxlsx::WorksheetCellReference, 2> invalid_batch {
            fastxlsx::WorksheetCellReference {1, 1},
            fastxlsx::WorksheetCellReference {1048577, 1},
        };
        (void)sheet.sparse_cells(invalid_batch);
    }), "renamed formula delete-row invalid reads should reject invalid coordinate batch reads");
    check(threw_fastxlsx_error([&] { (void)sheet.row_cells(0); }),
        "renamed formula delete-row invalid reads should reject row_cells row zero");
    check(threw_fastxlsx_error([&] { (void)reacquired.column_cells(16385); }),
        "renamed formula delete-row invalid reads should reject column_cells column overflow");
    check(threw_fastxlsx_error([&] { (void)reacquired.get_cell("D2"); }),
        "renamed formula delete-row invalid reads should reject valid but missing get_cell reads");

    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row invalid reads should not update last_edit_error");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row invalid reads should keep both planned-name handles clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row invalid reads should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row invalid reads should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row invalid reads should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula delete-row invalid reads should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula delete-row invalid reads should preserve the planned workbook catalog");
    check(editor.has_worksheet("RenamedData") && !editor.has_worksheet("Data"),
        "renamed formula delete-row invalid reads should preserve planned-name lookup state");
    check(reacquired.cell_count() == 5 && sheet.cell_count() == 5,
        "renamed formula delete-row invalid reads should preserve sparse counts");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 2, 4,
        "renamed formula delete-row invalid reads should preserve delete-row bounds");
    const std::optional<fastxlsx::CellValue> preserved_formula = sheet.try_cell("D1");
    check(preserved_formula.has_value() &&
            preserved_formula->kind() == fastxlsx::CellValueKind::Formula &&
            preserved_formula->text_value() == "#REF!+#REF!" &&
            preserved_formula->has_style() &&
            preserved_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row invalid reads should preserve the saved styled formula");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            reacquired.get_cell("B1").text_value() == "row2-gap-b2" &&
            reacquired.get_cell("C1").text_value() == "row2-gap-c2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row invalid reads should preserve shifted source cells");
    check(!sheet.try_cell("D2").has_value() &&
            !reacquired.try_cell("A3").has_value(),
        "renamed formula delete-row invalid reads should keep old shifted coordinates absent");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula delete-row invalid reads");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula delete-row invalid reads later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete-row invalid reads later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 5,
        "renamed formula delete-row invalid reads later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula delete-row invalid reads later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E1");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+#REF!" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row invalid reads later shift should preserve #REF formula and style id");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            sheet.get_cell("C1").text_value() == "row2-gap-b2" &&
            sheet.get_cell("D1").text_value() == "row2-gap-c2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row invalid reads later shift should move source-backed cells");
    check(!reacquired.try_cell("B1").has_value() &&
            !reacquired.try_cell("D2").has_value() &&
            !reacquired.try_cell("A3").has_value(),
        "renamed formula delete-row invalid reads later shift should keep old coordinates absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row invalid reads second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-row invalid reads second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row invalid reads second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row invalid reads first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-row invalid reads first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula delete-row invalid reads first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "renamed formula delete-row invalid reads first output should keep delete-row bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula delete-row invalid reads first output should keep the delete-row styled formula");
    check_not_contains(first_worksheet_xml, R"(r="E1")",
        "renamed formula delete-row invalid reads first output should not include the later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row invalid reads second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="E1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-row invalid reads second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula delete-row invalid reads second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:E2"/>)",
        "renamed formula delete-row invalid reads second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="A1")",
        "renamed formula delete-row invalid reads second output should keep shifted A2");
    check_contains(second_worksheet_xml, R"(<c r="C1")",
        "renamed formula delete-row invalid reads second output should write shifted B2");
    check_contains(second_worksheet_xml, R"(<c r="D1")",
        "renamed formula delete-row invalid reads second output should write shifted C2");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula delete-row invalid reads second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "renamed formula delete-row invalid reads second output should omit inserted blank B1");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed formula delete-row invalid reads second output should omit the old formula coordinate");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row invalid reads no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-row invalid reads no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row invalid reads no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row invalid reads no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-row invalid reads no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-row invalid reads no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula delete-row invalid reads no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row invalid reads no-op save should leave the source package unchanged");
    check_reopened_delete_row_formula_column_shift_noop_output(
        noop_output, styled_formula_style,
        "renamed formula delete-row invalid reads no-op output");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-row invalid reads reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-row invalid reads reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row invalid reads reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 5,
        "renamed formula delete-row invalid reads reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 5,
        "renamed formula delete-row invalid reads reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_e1 =
        reopened_sheet.try_cell("E1");
    check(reopened_e1.has_value() &&
            reopened_e1->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_e1->text_value() == "#REF!+#REF!" &&
            reopened_e1->has_style() &&
            reopened_e1->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row invalid reads reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            reopened_sheet.get_cell("C1").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("D1").text_value() == "row2-gap-c2" &&
            reopened_sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row invalid reads reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("D2").has_value() &&
            !reopened_sheet.try_cell("A3").has_value(),
        "renamed formula delete-row invalid reads reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_delete_rows_formula_snapshot_reads_preserve_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-snapshot-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-snapshot-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-snapshot-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-snapshot-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_rows(1, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-row snapshot reads first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row snapshot reads first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row snapshot reads first save should clear dirty materialized diagnostics");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row snapshot reads first save should leave the source package unchanged");

    fastxlsx::WorksheetEditor reacquired = editor.worksheet("RenamedData");
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula delete-row snapshot reads matching reacquire should stay clean");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula delete-row snapshot reads should keep the old source name unavailable");

    const std::vector<fastxlsx::WorksheetCellSnapshot> all_cells =
        reacquired.sparse_cells();
    check(all_cells.size() == 5,
        "renamed formula delete-row snapshot reads should return all saved sparse cells");
    check(all_cells[0].reference.row == 1 && all_cells[0].reference.column == 1 &&
            all_cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
            all_cells[0].value.text_value() == "placeholder-a2",
        "renamed formula delete-row snapshot reads should keep shifted A2 first");
    check(all_cells[1].reference.row == 1 && all_cells[1].reference.column == 2 &&
            all_cells[1].value.kind() == fastxlsx::CellValueKind::Text &&
            all_cells[1].value.text_value() == "row2-gap-b2",
        "renamed formula delete-row snapshot reads should keep shifted B2 second");
    check(all_cells[2].reference.row == 1 && all_cells[2].reference.column == 3 &&
            all_cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
            all_cells[2].value.text_value() == "row2-gap-c2",
        "renamed formula delete-row snapshot reads should keep shifted C2 third");
    check(all_cells[3].reference.row == 1 && all_cells[3].reference.column == 4 &&
            all_cells[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            all_cells[3].value.text_value() == "#REF!+#REF!" &&
            all_cells[3].value.has_style() &&
            all_cells[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row snapshot reads should expose the saved styled formula");
    check(all_cells[4].reference.row == 2 && all_cells[4].reference.column == 1 &&
            all_cells[4].value.kind() == fastxlsx::CellValueKind::Text &&
            all_cells[4].value.text_value() == "extra-c3",
        "renamed formula delete-row snapshot reads should keep shifted trailing source cell");

    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_range =
        sheet.sparse_cells("A1:D2");
    check(shifted_range.size() == 5,
        "renamed formula delete-row snapshot reads should return represented cells in range order");
    check(shifted_range[0].reference.row == 1 && shifted_range[0].reference.column == 1 &&
            shifted_range[0].value.text_value() == "placeholder-a2",
        "renamed formula delete-row snapshot reads should keep shifted A2 first in range");
    check(shifted_range[3].reference.row == 1 && shifted_range[3].reference.column == 4 &&
            shifted_range[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_range[3].value.text_value() == "#REF!+#REF!" &&
            shifted_range[3].value.has_style() &&
            shifted_range[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row snapshot reads should expose styled formula in range order");
    check(shifted_range[4].reference.row == 2 && shifted_range[4].reference.column == 1 &&
            shifted_range[4].value.text_value() == "extra-c3",
        "renamed formula delete-row snapshot reads should keep trailing cells after row-one range cells");

    const std::vector<fastxlsx::WorksheetCellSnapshot> row_one =
        reacquired.row_cells(1);
    check(row_one.size() == 4,
        "renamed formula delete-row snapshot reads row_cells should expose the shifted formula row");
    check(row_one[0].reference.row == 1 && row_one[0].reference.column == 1 &&
            row_one[0].value.text_value() == "placeholder-a2",
        "renamed formula delete-row snapshot reads row_cells should keep shifted A2 first");
    check(row_one[1].reference.row == 1 && row_one[1].reference.column == 2 &&
            row_one[1].value.text_value() == "row2-gap-b2",
        "renamed formula delete-row snapshot reads row_cells should keep shifted B2 second");
    check(row_one[2].reference.row == 1 && row_one[2].reference.column == 3 &&
            row_one[2].value.text_value() == "row2-gap-c2",
        "renamed formula delete-row snapshot reads row_cells should keep shifted C2 third");
    check(row_one[3].reference.row == 1 && row_one[3].reference.column == 4 &&
            row_one[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            row_one[3].value.text_value() == "#REF!+#REF!" &&
            row_one[3].value.has_style() &&
            row_one[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row snapshot reads row_cells should keep styled formula last");

    const std::vector<fastxlsx::WorksheetCellSnapshot> column_four =
        sheet.column_cells(4);
    check(column_four.size() == 1,
        "renamed formula delete-row snapshot reads column_cells should expose the formula column");
    check(column_four[0].reference.row == 1 &&
            column_four[0].reference.column == 4 &&
            column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            column_four[0].value.text_value() == "#REF!+#REF!" &&
            column_four[0].value.has_style() &&
            column_four[0].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row snapshot reads column_cells should keep formula style id");

    const std::array<fastxlsx::WorksheetCellReference, 4> requested_refs {
        fastxlsx::WorksheetCellReference {1, 4},
        fastxlsx::WorksheetCellReference {2, 1},
        fastxlsx::WorksheetCellReference {1, 1},
        fastxlsx::WorksheetCellReference {1, 4},
    };
    const std::vector<fastxlsx::WorksheetCellSnapshot> requested_cells =
        reacquired.sparse_cells(requested_refs);
    check(requested_cells.size() == 4,
        "renamed formula delete-row snapshot reads coordinate batch should keep requested represented cells");
    check(requested_cells[0].reference.row == 1 && requested_cells[0].reference.column == 4 &&
            requested_cells[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            requested_cells[0].value.text_value() == "#REF!+#REF!" &&
            requested_cells[0].value.has_style() &&
            requested_cells[0].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row snapshot reads coordinate batch should return D1 first");
    check(requested_cells[1].reference.row == 2 && requested_cells[1].reference.column == 1 &&
            requested_cells[1].value.text_value() == "extra-c3",
        "renamed formula delete-row snapshot reads coordinate batch should return A2 second");
    check(requested_cells[2].reference.row == 1 && requested_cells[2].reference.column == 1 &&
            requested_cells[2].value.text_value() == "placeholder-a2",
        "renamed formula delete-row snapshot reads coordinate batch should return A1 third");
    check(requested_cells[3].reference.row == 1 && requested_cells[3].reference.column == 4 &&
            requested_cells[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            requested_cells[3].value.text_value() == "#REF!+#REF!" &&
            requested_cells[3].value.has_style() &&
            requested_cells[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row snapshot reads coordinate batch should preserve duplicate D1 reads");

    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row snapshot reads should keep diagnostics clear");
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row snapshot reads should keep both planned-name handles clean");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row snapshot reads should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row snapshot reads should not dirty materialized diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row snapshot reads should not queue replacement diagnostics");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula delete-row snapshot reads should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula delete-row snapshot reads should preserve the planned workbook catalog");
    check(reacquired.cell_count() == 5 && sheet.cell_count() == 5,
        "renamed formula delete-row snapshot reads should preserve sparse counts");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 2, 4,
        "renamed formula delete-row snapshot reads should preserve delete-row bounds");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula delete-row snapshot reads");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(all_cells[3].reference.row == 1 && all_cells[3].reference.column == 4 &&
            all_cells[3].value.text_value() == "#REF!+#REF!" &&
            all_cells[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row snapshot reads should return owning snapshots across later shifts");
    check(row_one[3].reference.row == 1 && row_one[3].reference.column == 4 &&
            row_one[3].value.text_value() == "#REF!+#REF!" &&
            row_one[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row snapshot reads row snapshot should remain stable after later shifts");
    check(column_four[0].reference.row == 1 && column_four[0].reference.column == 4 &&
            column_four[0].value.text_value() == "#REF!+#REF!" &&
            column_four[0].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row snapshot reads column snapshot should remain stable after later shifts");
    check(requested_cells[3].reference.row == 1 && requested_cells[3].reference.column == 4 &&
            requested_cells[3].value.text_value() == "#REF!+#REF!" &&
            requested_cells[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row snapshot reads batch snapshot should remain stable after later shifts");
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula delete-row snapshot reads later shift should dirty the shared styled session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete-row snapshot reads later shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 5,
        "renamed formula delete-row snapshot reads later shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula delete-row snapshot reads later shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E1");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+#REF!" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row snapshot reads later shift should preserve #REF formula and style id");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row snapshot reads second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-row snapshot reads second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row snapshot reads second save should clear dirty diagnostics again");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row snapshot reads second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="E1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-row snapshot reads second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula delete-row snapshot reads second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:E2"/>)",
        "renamed formula delete-row snapshot reads second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula delete-row snapshot reads second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "renamed formula delete-row snapshot reads second output should omit inserted blank B1");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed formula delete-row snapshot reads second output should omit the old formula coordinate");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row snapshot reads no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-row snapshot reads no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row snapshot reads no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row snapshot reads no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-row snapshot reads no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-row snapshot reads no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula delete-row snapshot reads no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row snapshot reads no-op save should leave the source package unchanged");
    check_reopened_delete_row_formula_column_shift_snapshot_noop_output(
        noop_output, styled_formula_style,
        "renamed formula delete-row snapshot reads no-op output");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-row snapshot reads reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-row snapshot reads reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row snapshot reads reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 5,
        "renamed formula delete-row snapshot reads reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 5,
        "renamed formula delete-row snapshot reads reopened output should expose combined shifted bounds");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
        reopened_sheet.row_cells(1);
    check(reopened_row_one.size() == 4,
        "renamed formula delete-row snapshot reads reopened row_cells should expose shifted row one");
    check(reopened_row_one[0].reference.row == 1 && reopened_row_one[0].reference.column == 1 &&
            reopened_row_one[0].value.text_value() == "placeholder-a2",
        "renamed formula delete-row snapshot reads reopened row_cells should read shifted A2");
    check(reopened_row_one[1].reference.row == 1 && reopened_row_one[1].reference.column == 3 &&
            reopened_row_one[1].value.text_value() == "row2-gap-b2",
        "renamed formula delete-row snapshot reads reopened row_cells should read shifted B2");
    check(reopened_row_one[2].reference.row == 1 && reopened_row_one[2].reference.column == 4 &&
            reopened_row_one[2].value.text_value() == "row2-gap-c2",
        "renamed formula delete-row snapshot reads reopened row_cells should read shifted C2");
    check(reopened_row_one[3].reference.row == 1 && reopened_row_one[3].reference.column == 5 &&
            reopened_row_one[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_row_one[3].value.text_value() == "#REF!+#REF!" &&
            reopened_row_one[3].value.has_style() &&
            reopened_row_one[3].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row snapshot reads reopened row_cells should read translated styled formula");
    const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_five =
        reopened_sheet.column_cells(5);
    check(reopened_column_five.size() == 1 &&
            reopened_column_five[0].reference.row == 1 &&
            reopened_column_five[0].reference.column == 5 &&
            reopened_column_five[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            reopened_column_five[0].value.text_value() == "#REF!+#REF!" &&
            reopened_column_five[0].value.has_style() &&
            reopened_column_five[0].value.style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row snapshot reads reopened column_cells should read translated styled formula");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("D2").has_value() &&
            !reopened_sheet.try_cell("A3").has_value(),
        "renamed formula delete-row snapshot reads reopened output should keep old coordinates absent");
}

void test_public_worksheet_editor_shift_after_rename_delete_rows_formula_reacquire_reuses_styled_session()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-reacquire-source.xlsx",
            styled_formula_style);
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-reacquire-first-output.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-reacquire-second-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-shift-after-rename-delete-row-formula-reacquire-noop-output.xlsx");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.rename_sheet("Data", "RenamedData");
    const std::vector<std::string> expected_source_names = editor.source_worksheet_names();
    const std::vector<std::string> expected_planned_names = editor.worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> expected_catalog =
        editor.worksheet_catalog();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("RenamedData");
    sheet.delete_rows(1, 1);

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "renamed formula delete-row reacquire first save should clean the planned-name handle");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row reacquire first save should count rename plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row reacquire first save should clear dirty materialized diagnostics");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row reacquire first save should keep diagnostics clear");

    std::optional<fastxlsx::WorksheetEditor> maybe_reacquired =
        editor.try_worksheet("RenamedData");
    check(maybe_reacquired.has_value(),
        "renamed formula delete-row reacquire should find the planned-name saved session");
    check(!editor.try_worksheet("Data").has_value(),
        "renamed formula delete-row reacquire should keep the old source name unavailable");
    if (!maybe_reacquired.has_value()) {
        return;
    }

    fastxlsx::WorksheetEditor reacquired = std::move(*maybe_reacquired);
    check(!reacquired.has_pending_changes() && !sheet.has_pending_changes(),
        "renamed formula delete-row reacquire should return the saved clean styled session");
    check(editor.source_worksheet_names() == expected_source_names &&
            editor.worksheet_names() == expected_planned_names,
        "renamed formula delete-row reacquire should preserve source and planned worksheet names");
    check(workbook_editor_catalog_entries_equal(editor.worksheet_catalog(), expected_catalog),
        "renamed formula delete-row reacquire should preserve the planned workbook catalog");
    check(reacquired.cell_count() == 5 && sheet.cell_count() == 5,
        "renamed formula delete-row reacquire should keep shifted sparse count");
    check_cell_range_equals(reacquired.used_range(), 1, 1, 2, 4,
        "renamed formula delete-row reacquire should expose delete-row bounds");
    const std::optional<fastxlsx::CellValue> reacquired_d1 =
        reacquired.try_cell("D1");
    check(reacquired_d1.has_value() &&
            reacquired_d1->kind() == fastxlsx::CellValueKind::Formula &&
            reacquired_d1->text_value() == "#REF!+#REF!" &&
            reacquired_d1->has_style() &&
            reacquired_d1->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row reacquire should read the saved translated styled formula");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            reacquired.get_cell("B1").text_value() == "row2-gap-b2" &&
            reacquired.get_cell("C1").text_value() == "row2-gap-c2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row reacquire should read shifted source-backed rows");
    check(!sheet.try_cell("D2").has_value() &&
            !sheet.try_cell("A3").has_value(),
        "renamed formula delete-row reacquire should keep old coordinates absent");
    check(!editor.last_edit_error().has_value(),
        "renamed formula delete-row reacquire should keep diagnostics clear");
    check(editor.pending_change_count() == 2,
        "renamed formula delete-row reacquire should not add materialized handoffs");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row reacquire should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row reacquire should not queue replacement diagnostics");
    check_workbook_editor_renamed_formula_saved_reacquire_diagnostics(
        editor, "renamed formula delete-row reacquire");

    reacquired.insert_columns(2, 1);
    const std::size_t shifted_memory = reacquired.estimated_memory_usage();
    check(reacquired.has_pending_changes() && sheet.has_pending_changes(),
        "renamed formula delete-row reacquire later column shift should dirty the shared session");
    check(editor.pending_materialized_worksheet_names()
              == std::vector<std::string>{"RenamedData"},
        "renamed formula delete-row reacquire later column shift should report RenamedData dirty once");
    check(editor.pending_materialized_cell_count() == 5,
        "renamed formula delete-row reacquire later column shift should keep the styled sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory &&
            sheet.estimated_memory_usage() == shifted_memory,
        "renamed formula delete-row reacquire later column shift should report styled materialized memory");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("E1");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+#REF!" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row reacquire later column shift should preserve #REF formula and style id");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            sheet.get_cell("C1").text_value() == "row2-gap-b2" &&
            sheet.get_cell("D1").text_value() == "row2-gap-c2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row reacquire later column shift should move source-backed cells");
    check(!reacquired.try_cell("B1").has_value() &&
            !reacquired.try_cell("D2").has_value() &&
            !reacquired.try_cell("A3").has_value(),
        "renamed formula delete-row reacquire later column shift should keep old coordinates absent");

    editor.save_as(second_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row reacquire second save should clean both styled handles");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-row reacquire second save should record the later materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row reacquire second save should clear dirty diagnostics again");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row reacquire first save should leave the source package unchanged");
    const std::string first_workbook_xml = first_entries.at("xl/workbook.xml");
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_styled_formula_xml =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(first_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-row reacquire first output should keep the planned catalog name");
    check_not_contains(first_workbook_xml, R"(name="Data")",
        "renamed formula delete-row reacquire first output should omit the source catalog name");
    check_contains(first_worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "renamed formula delete-row reacquire first output should keep delete-row bounds");
    check_contains(first_worksheet_xml, first_styled_formula_xml,
        "renamed formula delete-row reacquire first output should keep translated formula with style id");
    check_not_contains(first_worksheet_xml, R"(r="E1")",
        "renamed formula delete-row reacquire first output should not include the later column shift");

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row reacquire second save should leave the source package unchanged");
    const std::string second_workbook_xml = second_entries.at("xl/workbook.xml");
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_styled_formula_xml =
        std::string(R"(<c r="E1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(second_workbook_xml, R"(name="RenamedData")",
        "renamed formula delete-row reacquire second output should keep the planned catalog name");
    check_not_contains(second_workbook_xml, R"(name="Data")",
        "renamed formula delete-row reacquire second output should omit the source catalog name");
    check_contains(second_worksheet_xml, R"(<dimension ref="A1:E2"/>)",
        "renamed formula delete-row reacquire second output should project combined shifted bounds");
    check_contains(second_worksheet_xml, R"(<c r="A1")",
        "renamed formula delete-row reacquire second output should keep shifted A2");
    check_contains(second_worksheet_xml, R"(<c r="C1")",
        "renamed formula delete-row reacquire second output should write shifted B2");
    check_contains(second_worksheet_xml, R"(<c r="D1")",
        "renamed formula delete-row reacquire second output should write shifted C2");
    check_contains(second_worksheet_xml, second_styled_formula_xml,
        "renamed formula delete-row reacquire second output should write translated formula with style id");
    check_not_contains(second_worksheet_xml, R"(r="B1")",
        "renamed formula delete-row reacquire second output should omit inserted blank B1");
    check_not_contains(second_worksheet_xml, R"(r="D2")",
        "renamed formula delete-row reacquire second output should omit the old formula coordinate");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes() && !reacquired.has_pending_changes(),
        "renamed formula delete-row reacquire no-op save should keep both styled handles clean");
    check(editor.pending_change_count() == 3,
        "renamed formula delete-row reacquire no-op save should not add another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row reacquire no-op save should keep dirty diagnostics empty");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "renamed formula delete-row reacquire no-op save should not queue replacement diagnostics");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop,
        "renamed formula delete-row reacquire no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop,
        "renamed formula delete-row reacquire no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == second_entries,
        "renamed formula delete-row reacquire no-op save should keep output entries stable");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "renamed formula delete-row reacquire no-op save should leave the source package unchanged");
    check_reopened_delete_row_formula_column_shift_noop_output(
        noop_output, styled_formula_style,
        "renamed formula delete-row reacquire no-op output");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(second_output);
    check(reopened.has_worksheet("RenamedData") && !reopened.has_worksheet("Data"),
        "renamed formula delete-row reacquire reopened output should expose only the planned catalog name");
    fastxlsx::WorksheetEditor reopened_sheet = reopened.worksheet("RenamedData");
    check(!reopened.has_pending_changes() && !reopened_sheet.has_pending_changes(),
        "renamed formula delete-row reacquire reopened output should start clean");
    check(reopened.pending_change_count() == 0 &&
            reopened.pending_materialized_worksheet_names().empty() &&
            reopened.pending_materialized_cell_count() == 0 &&
            reopened.estimated_pending_materialized_memory_usage() == 0,
        "renamed formula delete-row reacquire reopened output should not expose dirty diagnostics");
    check(reopened_sheet.cell_count() == 5,
        "renamed formula delete-row reacquire reopened output should keep shifted sparse count");
    check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 5,
        "renamed formula delete-row reacquire reopened output should expose combined shifted bounds");
    const std::optional<fastxlsx::CellValue> reopened_e1 =
        reopened_sheet.try_cell("E1");
    check(reopened_e1.has_value() &&
            reopened_e1->kind() == fastxlsx::CellValueKind::Formula &&
            reopened_e1->text_value() == "#REF!+#REF!" &&
            reopened_e1->has_style() &&
            reopened_e1->style_id().value() == styled_formula_style.value(),
        "renamed formula delete-row reacquire reopened output should read translated styled formula");
    check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            reopened_sheet.get_cell("C1").text_value() == "row2-gap-b2" &&
            reopened_sheet.get_cell("D1").text_value() == "row2-gap-c2" &&
            reopened_sheet.get_cell("A2").text_value() == "extra-c3",
        "renamed formula delete-row reacquire reopened output should read shifted source cells");
    check(!reopened_sheet.try_cell("B1").has_value() &&
            !reopened_sheet.try_cell("D2").has_value() &&
            !reopened_sheet.try_cell("A3").has_value(),
        "renamed formula delete-row reacquire reopened output should keep old coordinates absent");
}

} // namespace

int main()
{
    try {
        test_public_worksheet_editor_full_calculation_renamed_source_formula_audits_preserve_source_scan();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_preserve_materialized_state();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_failed_save_preserve_state();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_option_mismatch_preserve_state();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_missing_query_preserve_state();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_invalid_reads_preserve_state();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_invalid_mutations_preserve_state();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_invalid_shifts_preserve_state();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_invalid_diagnostic_recovery_preserve_state();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_preserve_state();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_failed_save_preserve_state();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_invalid_mutation_recovery();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_invalid_mutation_noop_save();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_invalid_reads_recovery();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_invalid_reads_noop_save();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_invalid_shifts_recovery();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_invalid_shifts_noop_save();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_missing_query_recovery();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_missing_query_noop_save();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_option_mismatch_recovery();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_option_mismatch_noop_save();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_same_sheet_guard_recovery();
        test_public_worksheet_editor_full_calculation_renamed_formula_audits_saved_reacquire_same_sheet_guard_noop_save();
        test_public_worksheet_editor_shift_after_rename_uses_planned_name();
        test_public_worksheet_editor_shift_after_rename_preserves_formula_style();
        test_public_worksheet_editor_shift_after_rename_formula_audits_use_shifted_formula();
        test_public_worksheet_editor_shift_after_rename_column_formula_audits_use_shifted_formula();
        test_public_worksheet_editor_shift_after_rename_delete_formula_audits_skip_ref_tokens();
        test_public_worksheet_editor_shift_after_rename_preserves_column_formula_style();
        test_public_worksheet_editor_shift_after_rename_formula_reacquire_reuses_styled_session();
        test_public_worksheet_editor_shift_after_rename_formula_failed_save_preserves_styled_session();
        test_public_worksheet_editor_shift_after_rename_formula_option_mismatch_preserves_styled_session();
        test_public_worksheet_editor_shift_after_rename_formula_invalid_mutations_preserve_styled_session();
        test_public_worksheet_editor_shift_after_rename_formula_missing_query_preserves_styled_session();
        test_public_worksheet_editor_shift_after_rename_formula_invalid_reads_preserve_styled_session();
        test_public_worksheet_editor_shift_after_rename_formula_snapshot_reads_preserve_styled_session();
        test_public_worksheet_editor_shift_after_rename_deletes_formula_references();
        test_public_worksheet_editor_shift_after_rename_delete_columns_formula_failed_save_preserves_styled_session();
        test_public_worksheet_editor_shift_after_rename_delete_columns_formula_option_mismatch_preserves_styled_session();
        test_public_worksheet_editor_shift_after_rename_delete_columns_formula_invalid_mutations_preserve_styled_session();
        test_public_worksheet_editor_shift_after_rename_delete_columns_formula_missing_query_preserves_styled_session();
        test_public_worksheet_editor_shift_after_rename_delete_columns_formula_invalid_reads_preserve_styled_session();
        test_public_worksheet_editor_shift_after_rename_delete_columns_formula_snapshot_reads_preserve_styled_session();
        test_public_worksheet_editor_shift_after_rename_delete_columns_formula_reacquire_reuses_styled_session();
        test_public_worksheet_editor_shift_after_rename_deletes_formula_rows();
        test_public_worksheet_editor_shift_after_rename_delete_rows_formula_failed_save_preserves_styled_session();
        test_public_worksheet_editor_shift_after_rename_delete_rows_formula_option_mismatch_preserves_styled_session();
        test_public_worksheet_editor_shift_after_rename_delete_rows_formula_invalid_mutations_preserve_styled_session();
        test_public_worksheet_editor_shift_after_rename_delete_rows_formula_missing_query_preserves_styled_session();
        test_public_worksheet_editor_shift_after_rename_delete_rows_formula_invalid_reads_preserve_styled_session();
        test_public_worksheet_editor_shift_after_rename_delete_rows_formula_snapshot_reads_preserve_styled_session();
        test_public_worksheet_editor_shift_after_rename_delete_rows_formula_reacquire_reuses_styled_session();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor formula-audits check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor formula-audits tests passed\n");
    return 0;
}