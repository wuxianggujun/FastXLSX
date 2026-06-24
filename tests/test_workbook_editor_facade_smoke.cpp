#include "test_workbook_editor_facade_common.hpp"

void test_public_workbook_editor_editing_end_to_end_smoke()
{
    const std::filesystem::path source =
        write_public_editing_e2e_source("fastxlsx-workbook-editor-editing-e2e-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-editing-e2e-output.xlsx");

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::string replacement_png_bytes = fastxlsx::test::read_file(replacement_png_path);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string picture_sheet_before = source_entries.at("xl/worksheets/sheet3.xml");
    const std::string picture_sheet_rels_before =
        source_entries.at("xl/worksheets/_rels/sheet3.xml.rels");
    const std::string drawing_before = source_entries.at("xl/drawings/drawing1.xml");
    const std::string drawing_rels_before =
        source_entries.at("xl/drawings/_rels/drawing1.xml.rels");
    const std::string content_types_before = source_entries.at("[Content_Types].xml");
    const std::string package_rels_before = source_entries.at("_rels/.rels");
    const std::string workbook_rels_before = source_entries.at("xl/_rels/workbook.xml.rels");
    const std::string core_props_before = source_entries.at("docProps/core.xml");
    const std::string app_props_before = source_entries.at("docProps/app.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "EditedData");

    fastxlsx::WorksheetEditor edited_data = editor.worksheet("EditedData");
    check(edited_data.name() == "EditedData",
        "renamed materialized WorksheetEditor should expose the planned sheet name");
    edited_data.set_cell(1, 1, fastxlsx::CellValue::text("materialized-edit"));
    edited_data.set_cell(2, 2, fastxlsx::CellValue::number(42.0));

    editor.replace_sheet_data("ReplaceMe",
        {{fastxlsx::CellValue::text("sheetdata-final"),
            fastxlsx::CellValue::number(7.0)}});
    editor.replace_image("xl/media/image1.png", replacement_png_path);

    check(editor.has_pending_changes(),
        "combined public editing smoke should expose pending work before save_as");
    check(editor.pending_change_count() == 3,
        "combined public editing smoke should count rename, sheetData, and image edits");
    check(editor.pending_materialized_cell_count() == edited_data.cell_count(),
        "combined public editing smoke should report dirty materialized cell count");
    {
        const std::vector<std::string> pending_names =
            editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "ReplaceMe",
            "combined public editing smoke should track only the replaced sheetData sheet");
    }

    editor.save_as(output);
    check(editor.pending_change_count() == 4,
        "save_as should add the materialized worksheet flush to the public edit count");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string data_sheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string replaced_sheet_xml = output_entries.at("xl/worksheets/sheet2.xml");

    check_contains(workbook_xml, R"(name="EditedData")",
        "combined public editing smoke should save the renamed sheet catalog entry");
    check_contains(workbook_xml, R"(name="ReplaceMe")",
        "combined public editing smoke should preserve the replacement sheet catalog entry");
    check_contains(workbook_xml, R"(name="Pictures")",
        "combined public editing smoke should preserve the image sheet catalog entry");
    check_not_contains(workbook_xml, R"(name="Data")",
        "combined public editing smoke should remove the old sheet name");

    check_contains(data_sheet_xml, "materialized-edit",
        "combined public editing smoke should persist materialized text edits");
    check_contains(data_sheet_xml, R"(<c r="B2"><v>42</v></c>)",
        "combined public editing smoke should persist materialized numeric edits");
    check_not_contains(data_sheet_xml, "placeholder-a1",
        "combined public editing smoke should replace the edited source cell");
    check_contains(data_sheet_xml, R"(<dimension ref="A1:B2"/>)",
        "combined public editing smoke should refresh materialized worksheet dimension");

    check_contains(replaced_sheet_xml, "sheetdata-final",
        "combined public editing smoke should persist sheetData replacement text");
    check_contains(replaced_sheet_xml, R"(<c r="B1"><v>7</v></c>)",
        "combined public editing smoke should persist sheetData replacement number");
    check_not_contains(replaced_sheet_xml, "replace-old",
        "combined public editing smoke should drop old sheetData rows");

    check(output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "combined public editing smoke should replace the target media bytes");
    check(output_entries.at("xl/worksheets/sheet3.xml") == picture_sheet_before,
        "combined public editing smoke should preserve the picture worksheet XML");
    check(output_entries.at("xl/worksheets/_rels/sheet3.xml.rels") == picture_sheet_rels_before,
        "combined public editing smoke should preserve picture worksheet relationships");
    check(output_entries.at("xl/drawings/drawing1.xml") == drawing_before,
        "combined public editing smoke should preserve drawing XML");
    check(output_entries.at("xl/drawings/_rels/drawing1.xml.rels") == drawing_rels_before,
        "combined public editing smoke should preserve drawing relationships");
    check(output_entries.at("[Content_Types].xml") == content_types_before,
        "combined public editing smoke should preserve content types for same-format edits");
    check(output_entries.at("_rels/.rels") == package_rels_before,
        "combined public editing smoke should preserve package relationships");
    check(output_entries.at("xl/_rels/workbook.xml.rels") == workbook_rels_before,
        "combined public editing smoke should preserve workbook relationships");
    check(output_entries.at("docProps/core.xml") == core_props_before,
        "combined public editing smoke should preserve core document properties");
    check(output_entries.at("docProps/app.xml") == app_props_before,
        "combined public editing smoke should preserve app document properties");
}

void test_public_workbook_editor_combined_failed_save_as_preserves_state()
{
    const std::filesystem::path source =
        write_public_editing_e2e_source("fastxlsx-workbook-editor-combined-failed-save-source.xlsx");
    const std::filesystem::path missing_parent_output =
        artifact("fastxlsx-workbook-editor-combined-failed-save-missing-parent") / "out.xlsx";
    const std::filesystem::path safe_output =
        artifact("fastxlsx-workbook-editor-combined-failed-save-recovered-output.xlsx");
    std::filesystem::remove_all(missing_parent_output.parent_path());

    const std::filesystem::path replacement_png_path =
        repository_asset("docs/assets/donation/weixin.png");
    const std::string replacement_png_bytes = fastxlsx::test::read_file(replacement_png_path);
    std::string staged_image_bytes = replacement_png_bytes;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "RecoveredData");
    fastxlsx::WorksheetEditor recovered_data = editor.worksheet("RecoveredData");
    recovered_data.set_cell(1, 1, fastxlsx::CellValue::text("dirty-before-failed-save"));
    recovered_data.set_cell(3, 3, fastxlsx::CellValue::number(123.0));
    editor.replace_sheet_data("ReplaceMe",
        {{fastxlsx::CellValue::text("replacement-before-failed-save"),
            fastxlsx::CellValue::number(88.0)}});
    editor.replace_image("xl/media/image1.png", as_bytes(staged_image_bytes));
    staged_image_bytes.assign(staged_image_bytes.size(), '\0');

    check(editor.has_pending_changes(),
        "combined failed save_as recovery should queue mixed public edits first");
    check(editor.pending_change_count() == 3,
        "combined failed save_as recovery should count rename, sheetData, and image before flush");
    check(editor.pending_materialized_cell_count() == recovered_data.cell_count(),
        "combined failed save_as recovery should expose dirty materialized cells before failure");
    check(!editor.last_edit_error().has_value(),
        "combined failed save_as recovery should start with no public edit diagnostic");

    const std::size_t pending_count_before_failure = editor.pending_change_count();
    const std::size_t replacement_cells_before_failure =
        editor.pending_replacement_cell_count();
    const std::size_t replacement_memory_before_failure =
        editor.estimated_pending_replacement_memory_usage();
    const std::size_t materialized_cells_before_failure =
        editor.pending_materialized_cell_count();
    const std::size_t materialized_memory_before_failure =
        editor.estimated_pending_materialized_memory_usage();
    const std::vector<std::string> replacement_names_before_failure =
        editor.pending_replacement_worksheet_names();
    const std::vector<std::string> materialized_names_before_failure =
        editor.pending_materialized_worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_before_failure =
        editor.worksheet_catalog();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_failure =
        editor.pending_worksheet_edits();

    check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
        "combined save_as should fail before dirty materialized flush when parent is missing");

    check(editor.pending_change_count() == pending_count_before_failure,
        "failed combined save_as should preserve pending public edit count");
    check(editor.pending_replacement_cell_count() == replacement_cells_before_failure,
        "failed combined save_as should preserve replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage() ==
            replacement_memory_before_failure,
        "failed combined save_as should preserve replacement memory estimate");
    check(editor.pending_materialized_cell_count() == materialized_cells_before_failure,
        "failed combined save_as should preserve dirty materialized cell count");
    check(editor.estimated_pending_materialized_memory_usage() ==
            materialized_memory_before_failure,
        "failed combined save_as should preserve dirty materialized memory estimate");
    check(editor.pending_replacement_worksheet_names() ==
            replacement_names_before_failure,
        "failed combined save_as should preserve replacement sheet names");
    check(editor.pending_materialized_worksheet_names() ==
            materialized_names_before_failure,
        "failed combined save_as should preserve dirty materialized sheet names");
    check(workbook_editor_catalog_entries_equal(
              editor.worksheet_catalog(), catalog_before_failure),
        "failed combined save_as should preserve planned worksheet catalog");
    check(workbook_editor_edit_summaries_equal(
              editor.pending_worksheet_edits(), summaries_before_failure),
        "failed combined save_as should preserve worksheet edit summaries");
    check(recovered_data.has_pending_changes(),
        "failed combined save_as should keep the borrowed WorksheetEditor dirty");
    check(recovered_data.cell_count() == materialized_cells_before_failure,
        "failed combined save_as should keep dirty sparse cells on the borrowed handle");
    check(!editor.last_edit_error().has_value(),
        "failed combined save_as should not create a public edit diagnostic");
    check(!std::filesystem::exists(missing_parent_output),
        "failed combined save_as should not create the missing-parent output");

    editor.save_as(safe_output);

    check(editor.pending_change_count() == pending_count_before_failure + 1,
        "recovered combined save_as should count the materialized worksheet flush");
    check(editor.pending_materialized_cell_count() == 0,
        "recovered combined save_as should clear dirty materialized aggregate diagnostics");
    check(editor.pending_materialized_worksheet_names().empty(),
        "recovered combined save_as should clear dirty materialized sheet names");
    check(!recovered_data.has_pending_changes(),
        "recovered combined save_as should clear the borrowed WorksheetEditor dirty flag");
    check(!editor.last_edit_error().has_value(),
        "recovered combined save_as should leave public edit diagnostics clear");

    const auto output_entries = fastxlsx::test::read_zip_entries(safe_output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string data_sheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string replaced_sheet_xml = output_entries.at("xl/worksheets/sheet2.xml");

    check_contains(workbook_xml, R"(name="RecoveredData")",
        "recovered combined save_as should persist the queued rename");
    check_not_contains(workbook_xml, R"(name="Data")",
        "recovered combined save_as should not resurrect the old sheet name");
    check_contains(data_sheet_xml, "dirty-before-failed-save",
        "recovered combined save_as should persist dirty materialized text");
    check_contains(data_sheet_xml, R"(<c r="C3"><v>123</v></c>)",
        "recovered combined save_as should persist dirty materialized number");
    check_contains(data_sheet_xml, R"(<dimension ref="A1:C3"/>)",
        "recovered combined save_as should refresh materialized worksheet dimension");
    check_contains(replaced_sheet_xml, "replacement-before-failed-save",
        "recovered combined save_as should persist the queued sheetData replacement");
    check_contains(replaced_sheet_xml, R"(<c r="B1"><v>88</v></c>)",
        "recovered combined save_as should persist the queued replacement number");
    check(output_entries.at("xl/media/image1.png") == replacement_png_bytes,
        "recovered combined save_as should persist memory-backed image bytes");
}

void test_docprops_are_preserved_through_patch()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_document_properties("fastxlsx-workbook-editor-docprops-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-docprops-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string core_before = source_entries.at("docProps/core.xml");
    const std::string app_before = source_entries.at("docProps/app.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(123.0)}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("docProps/core.xml") == core_before,
        "patch save should preserve docProps/core.xml bytes");
    check(output_entries.at("docProps/app.xml") == app_before,
        "patch save should preserve docProps/app.xml bytes");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), R"(<v>123</v>)",
        "patch save should still apply the requested workbook edit");
}

} // namespace

int main()
{
    try {
        test_public_workbook_editor_editing_end_to_end_smoke();
        test_public_workbook_editor_combined_failed_save_as_preserves_state();
        test_docprops_are_preserved_through_patch();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor facade smoke check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor facade smoke tests passed\n");
    return 0;
}
