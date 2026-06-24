#include "test_workbook_editor_facade_common.hpp"

void test_rename_sheet_changes_catalog_name_and_preserves_parts()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string data_sheet_before = source_entries.at("xl/worksheets/sheet1.xml");
    const std::string untouched_sheet_before = source_entries.at("xl/worksheets/sheet2.xml");
    const std::string content_types_before = source_entries.at("[Content_Types].xml");
    const std::string package_rels_before = source_entries.at("_rels/.rels");
    const std::string workbook_rels_before = source_entries.at("xl/_rels/workbook.xml.rels");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "Renamed & Data");

    const std::vector<std::string> names = editor.worksheet_names();
    check(names.size() == 2 && names[0] == "Renamed & Data",
        "planned workbook view should expose the queued rename");
    check(names.size() == 2 && names[1] == "Untouched",
        "planned workbook view should keep the untouched sheet name after rename");
    check(!editor.has_worksheet("Data"),
        "planned workbook view should not expose the original sheet name after rename");
    check(editor.has_worksheet("Renamed & Data"),
        "planned workbook view should expose the queued rename before save");
    const std::vector<std::string> source_names = editor.source_worksheet_names();
    check(source_names.size() == 2 && source_names[0] == "Data",
        "source workbook view should stay on the original catalog after rename");
    check(source_names.size() == 2 && source_names[1] == "Untouched",
        "source workbook view should keep the untouched sheet name after rename");
    check(editor.has_source_worksheet("Data"),
        "source workbook view should still expose the original sheet name after rename");
    check(!editor.has_source_worksheet("Renamed & Data"),
        "source workbook view should not expose the planned rename before save");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "worksheet_catalog should keep both sheets after queued rename");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data",
                "worksheet_catalog should keep the source name for renamed sheet");
            check(catalog[0].planned_name == "Renamed & Data",
                "worksheet_catalog should expose the queued planned name");
            check(catalog[0].renamed,
                "worksheet_catalog should mark the queued rename");
            check(catalog[1].source_name == "Untouched",
                "worksheet_catalog should keep the untouched source name");
            check(catalog[1].planned_name == "Untouched",
                "worksheet_catalog should keep the untouched planned name");
            check(!catalog[1].renamed,
                "worksheet_catalog should not mark untouched sheet as renamed");
        }
    }

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed &amp; Data")",
        "rename should XML-escape the new sheet name in the output catalog");
    check_not_contains(workbook_xml, R"(name="Data")",
        "rename should drop the old sheet name from the output catalog");

    check(output_entries.at("xl/worksheets/sheet1.xml") == data_sheet_before,
        "rename should preserve the renamed sheet's worksheet bytes");
    check(output_entries.at("xl/worksheets/sheet2.xml") == untouched_sheet_before,
        "rename should preserve untouched worksheet bytes");
    check(output_entries.at("[Content_Types].xml") == content_types_before,
        "rename should preserve content types bytes");
    check(output_entries.at("_rels/.rels") == package_rels_before,
        "rename should preserve package relationships bytes");
    check(output_entries.at("xl/_rels/workbook.xml.rels") == workbook_rels_before,
        "rename should preserve workbook relationships bytes");

    // Reopening the output package confirms the new catalog name is the one a
    // reader sees, and the other sheet is unchanged.
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    const std::vector<std::string> reopened_names = reopened.worksheet_names();
    check(reopened_names.size() == 2 && reopened_names[0] == "Renamed & Data",
        "reopened output should expose the renamed sheet in catalog order");
    check(reopened_names.size() == 2 && reopened_names[1] == "Untouched",
        "reopened output should keep the untouched sheet name");
    check(!reopened.has_worksheet("Data"),
        "reopened output should no longer expose the old sheet name");
}

void test_replace_sheet_data_uses_planned_catalog_after_rename()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-planned-catalog-source.xlsx");

    {
        const std::filesystem::path rename_only_output =
            artifact("fastxlsx-workbook-editor-planned-catalog-rename-only-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.rename_sheet("Data", "RenamedOnly");

        check(threw_fastxlsx_error([&] {
            editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(9.0)}});
        }), "replace_sheet_data should reject the old source name after a planned rename");
        check(editor.pending_change_count() == 1,
            "old-name replace failure should not add a public pending change after rename");
        check_workbook_editor_no_replacement_payload_size_diagnostics(
            editor, "old-name replace failure after rename");
        check(!editor.has_worksheet("Data"),
            "planned workbook inspection should reject the old name after planned rename");
        check(editor.has_worksheet("RenamedOnly"),
            "planned workbook inspection should expose the planned name before save");
        check(editor.has_source_worksheet("Data"),
            "source workbook inspection should still expose the old name after planned rename");
        check(!editor.has_source_worksheet("RenamedOnly"),
            "source workbook inspection should not expose the planned name before save");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
                editor.worksheet_catalog();
            check(catalog.size() == 2,
                "worksheet_catalog should remain available after old-name replacement failure");
            if (catalog.size() == 2) {
                check(catalog[0].source_name == "Data",
                    "worksheet_catalog should keep source name after old-name failure");
                check(catalog[0].planned_name == "RenamedOnly",
                    "worksheet_catalog should keep planned name after old-name failure");
                check(catalog[0].renamed,
                    "worksheet_catalog should keep rename flag after old-name failure");
            }
        }

        editor.save_as(rename_only_output);
        const auto rename_only_entries = fastxlsx::test::read_zip_entries(rename_only_output);
        check_contains(rename_only_entries.at("xl/workbook.xml"), R"(name="RenamedOnly")",
            "old-name replace failure should preserve the queued catalog rename");
        check_contains(rename_only_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
            "old-name replace failure should preserve the source sheetData");
        check_not_contains(rename_only_entries.at("xl/worksheets/sheet1.xml"), R"(<v>9</v>)",
            "old-name replace failure should not leak rejected replacement cells");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-planned-catalog-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(1.0)}});
        check(editor.pending_replacement_cell_count() == 1,
            "initial replacement should record one pending replacement cell");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
                editor.worksheet_catalog();
            check(catalog.size() == 2 && catalog[0].source_name == "Data" &&
                    catalog[0].planned_name == "Data" && !catalog[0].renamed,
                "replacement-only edit should not change worksheet_catalog");
        }

        editor.rename_sheet("Data", "RenamedData");
        check(editor.pending_change_count() == 2,
            "rename after replacement should add one public pending change");
        check(editor.pending_replacement_cell_count() == 1,
            "rename should migrate the pending replacement diagnostic to the planned name");
        {
            const std::vector<std::string> pending_names =
                editor.pending_replacement_worksheet_names();
            check(pending_names.size() == 1 && pending_names[0] == "RenamedData",
                "rename should migrate pending replacement names to the planned sheet name");
            check(editor.has_pending_replacement("RenamedData"),
                "rename should mark the planned sheet name as pending replacement");
            check(!editor.has_pending_replacement("Data"),
                "rename should stop reporting the old source name as pending replacement");
        }
        check(editor.estimated_pending_replacement_memory_usage() > 0,
            "rename should preserve the pending replacement memory diagnostic");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
                editor.worksheet_catalog();
            check(catalog.size() == 2 && catalog[0].source_name == "Data" &&
                    catalog[0].planned_name == "RenamedData" && catalog[0].renamed,
                "worksheet_catalog should report replace+rename planned catalog state");
        }

        check(threw_fastxlsx_error([&] {
            editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(9.0)}});
        }), "replace_sheet_data should keep resolving through the planned catalog");
        check(editor.pending_change_count() == 2,
            "old-name replace failure should preserve prior replace+rename count");
        check(editor.pending_replacement_cell_count() == 1,
            "old-name replace failure should preserve the prior replacement diagnostic");

        editor.replace_sheet_data("RenamedData",
            {{fastxlsx::CellValue::number(2.0), fastxlsx::CellValue::number(3.0)}});
        check(editor.pending_change_count() == 3,
            "new planned-name replacement should add one public pending change");
        check(editor.pending_replacement_cell_count() == 2,
            "new planned-name replacement should overwrite the stale pre-rename diagnostic");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
                editor.worksheet_catalog();
            check(catalog.size() == 2 && catalog[0].source_name == "Data" &&
                    catalog[0].planned_name == "RenamedData" && catalog[0].renamed,
                "planned-name replacement should not change worksheet_catalog rename mapping");
        }
        {
            const std::vector<std::string> pending_names =
                editor.pending_replacement_worksheet_names();
            check(pending_names.size() == 1 && pending_names[0] == "RenamedData",
                "planned-name replacement should keep one pending replacement name");
        }

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedData")",
            "planned-name replacement should preserve the queued rename in output");
        check_not_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
            "planned-name replacement should drop the old catalog name in output");

        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<c r="A1"><v>2</v></c>)",
            "planned-name replacement should write the final A1 cell");
        check_contains(worksheet_xml, R"(<c r="B1"><v>3</v></c>)",
            "planned-name replacement should write the final B1 cell");
        check_not_contains(worksheet_xml, R"(<v>1</v>)",
            "planned-name replacement should drop the stale pre-rename payload");
        check_not_contains(worksheet_xml, R"(<v>9</v>)",
            "planned-name replacement should drop the rejected old-name payload");

        fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
        check(reopened.has_worksheet("RenamedData"),
            "reopened output should expose the planned catalog name");
        check(!reopened.has_worksheet("Data"),
            "reopened output should not expose the old source name");
    }
}

void test_rename_back_to_source_name_restores_public_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-back-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-back-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(11.0)}});
    const std::size_t replacement_memory =
        editor.estimated_pending_replacement_memory_usage();
    check(replacement_memory > 0,
        "initial replacement before rename-back should record memory");

    editor.rename_sheet("Data", "Temporary");
    check(editor.has_worksheet("Temporary"),
        "first rename should expose the temporary planned name");
    check(!editor.has_worksheet("Data"),
        "first rename should hide the source name from the planned catalog");
    check(editor.has_pending_replacement("Temporary"),
        "first rename should migrate replacement diagnostics to the planned name");

    editor.rename_sheet("Temporary", "Data");
    check(editor.pending_change_count() == 3,
        "rename-back should count as a public edit without committing");
    check(editor.has_worksheet("Data"),
        "rename-back should restore the source name in the planned catalog");
    check(!editor.has_worksheet("Temporary"),
        "rename-back should remove the temporary planned name");
    check(editor.has_source_worksheet("Data"),
        "rename-back should not change the source catalog view");
    check(editor.pending_replacement_cell_count() == 1,
        "rename-back should preserve the queued replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage() == replacement_memory,
        "rename-back should preserve the replacement memory diagnostic");
    {
        const std::vector<std::string> pending_names =
            editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "Data",
            "rename-back should migrate replacement diagnostics back to the source name");
        check(editor.has_pending_replacement("Data"),
            "rename-back should report the restored source name as data-replaced");
        check(!editor.has_pending_replacement("Temporary"),
            "rename-back should stop reporting the temporary planned name as data-replaced");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "rename-back should preserve the catalog entry count");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data" && catalog[0].planned_name == "Data",
                "rename-back should restore the source-to-planned mapping");
            check(!catalog[0].renamed,
                "rename-back should clear the public renamed flag");
        }
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "rename-back with replacement should keep one edit summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" && summaries[0].planned_name == "Data",
                "rename-back summary should restore source and planned names");
            check(!summaries[0].renamed,
                "rename-back summary should not remain marked as renamed");
            check(summaries[0].sheet_data_replaced,
                "rename-back summary should preserve sheetData replacement");
            check(summaries[0].replacement_cell_count == 1,
                "rename-back summary should preserve replacement cell count");
            check(summaries[0].estimated_replacement_memory_usage == replacement_memory,
                "rename-back summary should preserve replacement memory");
        }
    }

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "rename-back output should use the restored source name");
    check_not_contains(output_entries.at("xl/workbook.xml"), "Temporary",
        "rename-back output should not leak the transient planned name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), R"(<c r="A1"><v>11</v></c>)",
        "rename-back output should preserve the queued replacement payload");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "rename-back output should drop the old source sheetData");
}

void test_rename_chain_back_to_source_name_clears_rename_only_summary()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-chain-back-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-chain-back-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TemporaryA");
    editor.rename_sheet("TemporaryA", "TemporaryB");
    editor.rename_sheet("TemporaryB", "Data");

    check(editor.has_pending_changes(),
        "rename-only chain should still report queued public edit calls");
    check(editor.pending_change_count() == 3,
        "rename-only chain back to source should count each successful public edit");
    check(editor.has_worksheet("Data"),
        "rename-only chain back should restore the source name in planned inspection");
    check(!editor.has_worksheet("TemporaryA"),
        "rename-only chain back should remove the first transient planned name");
    check(!editor.has_worksheet("TemporaryB"),
        "rename-only chain back should remove the second transient planned name");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "rename-only chain back");
    check(editor.pending_worksheet_edits().empty(),
        "rename-only chain back to the source name should clear rename-only summaries");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "rename-only chain back should preserve catalog entry count");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data" && catalog[0].planned_name == "Data",
                "rename-only chain back should restore source-to-planned mapping");
            check(!catalog[0].renamed,
                "rename-only chain back should clear the public renamed flag");
        }
    }

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Untouched"); }),
        "failed rename after chain-back should throw FastXlsxError");
    {
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "failed rename after chain-back should record last_edit_error");
        if (last_error.has_value()) {
            check_contains(*last_error, "Data",
                "failed rename after chain-back diagnostic should include restored source name");
            check_contains(*last_error, "Untouched",
                "failed rename after chain-back diagnostic should include rejected target name");
        }
    }
    check(editor.pending_change_count() == 3,
        "failed rename after chain-back should not add a public pending change");
    check(editor.pending_worksheet_edits().empty(),
        "failed rename after chain-back should preserve empty rename-only summaries");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "failed rename after chain-back should preserve catalog entry count");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data" && catalog[0].planned_name == "Data",
                "failed rename after chain-back should preserve restored catalog mapping");
            check(!catalog[0].renamed,
                "failed rename after chain-back should preserve cleared renamed flag");
        }
    }

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "rename-only chain back output should use the restored source name");
    check_not_contains(workbook_xml, "TemporaryA",
        "rename-only chain back output should not leak the first transient name");
    check_not_contains(workbook_xml, "TemporaryB",
        "rename-only chain back output should not leak the second transient name");
}

void test_replacement_after_rename_chain_back_failure_uses_restored_name()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-chain-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-chain-replace-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TemporaryA");
    editor.rename_sheet("TemporaryA", "TemporaryB");
    editor.rename_sheet("TemporaryB", "Data");

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Untouched"); }),
        "duplicate rename after chain-back should throw FastXlsxError");
    check(editor.last_edit_error().has_value(),
        "duplicate rename after chain-back should leave last_edit_error");
    check(editor.pending_worksheet_edits().empty(),
        "duplicate rename after chain-back should preserve empty summaries");

    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::text("after chain-back failure"),
            fastxlsx::CellValue::number(31.0)}});

    check(!editor.last_edit_error().has_value(),
        "successful replacement after chain-back failure should clear last_edit_error");
    check(editor.pending_change_count() == 4,
        "replacement after three successful renames should add one public edit call");
    check(editor.pending_replacement_cell_count() == 2,
        "replacement after chain-back failure should record final replacement cells");
    {
        const std::vector<std::string> pending_names =
            editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "Data",
            "replacement after chain-back failure should use the restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "replacement after chain-back failure should create one summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" && summaries[0].planned_name == "Data",
                "replacement after chain-back failure should keep restored catalog names");
            check(!summaries[0].renamed,
                "replacement after chain-back failure should not reintroduce rename flag");
            check(summaries[0].sheet_data_replaced,
                "replacement after chain-back failure should report sheetData replacement");
            check(summaries[0].replacement_cell_count == 2,
                "replacement after chain-back failure should report replacement cell count");
        }
    }

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "replacement after chain-back failure output should keep restored source name");
    check_not_contains(workbook_xml, "TemporaryA",
        "replacement after chain-back failure output should not leak first transient name");
    check_not_contains(workbook_xml, "TemporaryB",
        "replacement after chain-back failure output should not leak second transient name");

    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>after chain-back failure</t></is></c>)",
        "replacement after chain-back failure should write final text cell");
    check_contains(worksheet_xml, R"(<c r="B1"><v>31</v></c>)",
        "replacement after chain-back failure should write final number cell");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "replacement after chain-back failure should remove old sheetData");
}

void test_failed_rename_preserves_pending_replacement_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-failure-diagnostics-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-failure-diagnostics-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(1.0)}});

    const std::size_t memory_after_initial_replacement =
        editor.estimated_pending_replacement_memory_usage();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summary_after_initial_replacement =
        editor.pending_worksheet_edits();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_after_initial_replacement =
        editor.worksheet_catalog();
    check(editor.pending_change_count() == 1,
        "initial replacement before failed rename should add one public pending change");
    check(editor.pending_replacement_cell_count() == 1,
        "initial replacement before failed rename should record one cell");
    check(memory_after_initial_replacement > 0,
        "initial replacement before failed rename should record memory");

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Untouched"); }),
        "duplicate rename after a replacement should throw FastXlsxError");
    {
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "duplicate rename failure should record a public last_edit_error");
        if (last_error.has_value()) {
            check_contains(*last_error, "Data",
                "duplicate rename last_edit_error should include the source sheet");
            check_contains(*last_error, "Untouched",
                "duplicate rename last_edit_error should include the rejected target sheet");
        }
    }
    check(editor.pending_change_count() == 1,
        "duplicate rename failure should not add a public pending change");
    check(editor.pending_replacement_cell_count() == 1,
        "duplicate rename failure should preserve the old-name replacement diagnostic");
    check(editor.estimated_pending_replacement_memory_usage() == memory_after_initial_replacement,
        "duplicate rename failure should preserve the replacement memory diagnostic");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == catalog_after_initial_replacement.size(),
            "duplicate rename failure should preserve catalog entry count");
        if (catalog.size() == 2 && catalog_after_initial_replacement.size() == 2) {
            check(catalog[0].source_name == catalog_after_initial_replacement[0].source_name,
                "duplicate rename failure should preserve catalog source name");
            check(catalog[0].planned_name == catalog_after_initial_replacement[0].planned_name,
                "duplicate rename failure should preserve catalog planned name");
            check(catalog[0].renamed == catalog_after_initial_replacement[0].renamed,
                "duplicate rename failure should preserve catalog rename flag");
        }
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == summary_after_initial_replacement.size(),
            "duplicate rename failure should preserve summary count");
        if (summaries.size() == 1 && summary_after_initial_replacement.size() == 1) {
            check(summaries[0].source_name == summary_after_initial_replacement[0].source_name,
                "duplicate rename failure should preserve summary source name");
            check(summaries[0].planned_name == summary_after_initial_replacement[0].planned_name,
                "duplicate rename failure should preserve summary planned name");
            check(summaries[0].renamed == summary_after_initial_replacement[0].renamed,
                "duplicate rename failure should preserve summary rename flag");
            check(summaries[0].sheet_data_replaced ==
                    summary_after_initial_replacement[0].sheet_data_replaced,
                "duplicate rename failure should preserve summary replacement flag");
            check(summaries[0].replacement_cell_count ==
                    summary_after_initial_replacement[0].replacement_cell_count,
                "duplicate rename failure should preserve summary cell count");
            check(summaries[0].estimated_replacement_memory_usage ==
                    summary_after_initial_replacement[0].estimated_replacement_memory_usage,
                "duplicate rename failure should preserve summary memory estimate");
        }
    }

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Bad/Name"); }),
        "invalid rename after a replacement should throw FastXlsxError");
    {
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "invalid rename failure should record a public last_edit_error");
        if (last_error.has_value()) {
            check_contains(*last_error, "Data",
                "invalid rename last_edit_error should include the source sheet");
            check_contains(*last_error, "Bad/Name",
                "invalid rename last_edit_error should include the rejected target sheet");
            check_not_contains(*last_error, "Untouched",
                "invalid rename last_edit_error should replace the previous duplicate diagnostic");
        }
    }
    check(editor.pending_change_count() == 1,
        "invalid rename failure should not add a public pending change");
    check(editor.pending_replacement_cell_count() == 1,
        "invalid rename failure should preserve the pending replacement diagnostic");
    check(editor.estimated_pending_replacement_memory_usage() == memory_after_initial_replacement,
        "invalid rename failure should preserve replacement memory");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == catalog_after_initial_replacement.size(),
            "invalid rename failure should preserve catalog entry count");
        if (catalog.size() == 2 && catalog_after_initial_replacement.size() == 2) {
            check(catalog[0].source_name == catalog_after_initial_replacement[0].source_name,
                "invalid rename failure should preserve catalog source name");
            check(catalog[0].planned_name == catalog_after_initial_replacement[0].planned_name,
                "invalid rename failure should preserve catalog planned name");
            check(catalog[0].renamed == catalog_after_initial_replacement[0].renamed,
                "invalid rename failure should preserve catalog rename flag");
        }
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == summary_after_initial_replacement.size(),
            "invalid rename failure should preserve summary count");
        if (summaries.size() == 1 && summary_after_initial_replacement.size() == 1) {
            check(summaries[0].source_name == summary_after_initial_replacement[0].source_name,
                "invalid rename failure should preserve summary source name");
            check(summaries[0].planned_name == summary_after_initial_replacement[0].planned_name,
                "invalid rename failure should preserve summary planned name");
            check(summaries[0].renamed == summary_after_initial_replacement[0].renamed,
                "invalid rename failure should preserve summary rename flag");
            check(summaries[0].sheet_data_replaced ==
                    summary_after_initial_replacement[0].sheet_data_replaced,
                "invalid rename failure should preserve summary replacement flag");
            check(summaries[0].replacement_cell_count ==
                    summary_after_initial_replacement[0].replacement_cell_count,
                "invalid rename failure should preserve summary cell count");
            check(summaries[0].estimated_replacement_memory_usage ==
                    summary_after_initial_replacement[0].estimated_replacement_memory_usage,
                "invalid rename failure should preserve summary memory estimate");
        }
    }

    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::number(4.0), fastxlsx::CellValue::number(5.0)}});
    check(editor.pending_change_count() == 2,
        "valid replacement after failed renames should add one pending change");
    check(editor.pending_replacement_cell_count() == 2,
        "valid replacement after failed renames should overwrite the stale payload");
    check(editor.estimated_pending_replacement_memory_usage() > 0,
        "valid replacement after failed renames should keep replacement memory visible");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "failed renames should leave the original Data catalog name in output");
    check_not_contains(output_entries.at("xl/workbook.xml"), "Bad/Name",
        "failed invalid rename should not leak the rejected name into the output catalog");

    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1"><v>4</v></c>)",
        "replacement after failed renames should write the final A1 cell");
    check_contains(worksheet_xml, R"(<c r="B1"><v>5</v></c>)",
        "replacement after failed renames should write the final B1 cell");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "replacement after failed renames should drop the stale pre-failure payload");
}

void test_rename_to_existing_name_throws_and_editor_stays_usable()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-dup-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-dup-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Untouched"); }),
        "renaming to an existing sheet name should throw FastXlsxError");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "untouched"); }),
        "renaming to an ASCII case-insensitive duplicate should throw FastXlsxError");

    // The editor must remain usable after a rejected rename.
    editor.rename_sheet("Data", "Renamed");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Renamed")",
        "editor should still apply a valid rename after a rejected one");
}

void test_rename_missing_sheet_throws_and_editor_stays_usable()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-missing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-missing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Missing", "Renamed"); }),
        "renaming a missing sheet should throw FastXlsxError");

    // The editor must remain usable after a rejected rename.
    editor.rename_sheet("Data", "Renamed");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Renamed")",
        "editor should still apply a valid rename after a missing-sheet rejection");
}

void test_rename_to_invalid_name_throws()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-invalid-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Bad/Name"); }),
        "renaming to a sheet name with invalid characters should throw FastXlsxError");
}

} // namespace

int main()
{
    try {
        test_rename_sheet_changes_catalog_name_and_preserves_parts();
        test_replace_sheet_data_uses_planned_catalog_after_rename();
        test_rename_back_to_source_name_restores_public_diagnostics();
        test_rename_chain_back_to_source_name_clears_rename_only_summary();
        test_replacement_after_rename_chain_back_failure_uses_restored_name();
        test_failed_rename_preserves_pending_replacement_diagnostics();
        test_rename_to_existing_name_throws_and_editor_stays_usable();
        test_rename_missing_sheet_throws_and_editor_stays_usable();
        test_rename_to_invalid_name_throws();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor facade rename check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor facade rename tests passed\n");
    return 0;
}
