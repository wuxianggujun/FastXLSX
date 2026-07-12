#include "test_workbook_editor_public_state_shifts_support.hpp"

namespace {

void test_public_worksheet_editor_delete_rows_preserves_shifted_source_formula_style()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-delete-rows-styled-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-styled-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-styled-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-styled-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-styled-post-noop-output.xlsx");
    const std::filesystem::path post_noop_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-styled-post-noop-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.delete_rows(1, 1);

    check(sheet.cell_count() == 5,
        "delete_rows styled source formula should remove deleted-row records");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
        "delete_rows styled source formula should expose shifted bounds");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D1");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+#REF!" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "delete_rows styled source formula should translate deleted row references and preserve style id");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "delete_rows styled source formula should shift remaining source rows");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A3").has_value(),
        "delete_rows styled source formula should keep old coordinates absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_one = sheet.row_cells(1);
    check(shifted_row_one.size() == 4,
        "delete_rows styled source formula row_cells should expose shifted row one");
    check(shifted_row_one[3].reference.row == 1 &&
            shifted_row_one[3].reference.column == 4 &&
            shifted_row_one[3].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_row_one[3].value.text_value() == "#REF!+#REF!" &&
            shifted_row_one[3].value.has_style() &&
            shifted_row_one[3].value.style_id().value() == styled_formula_style.value(),
        "delete_rows styled source formula row_cells should keep shifted formula style id");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_four =
        sheet.column_cells(4);
    check(shifted_column_four.size() == 1,
        "delete_rows styled source formula column_cells should expose shifted formula column");
    check(shifted_column_four[0].reference.row == 1 &&
            shifted_column_four[0].reference.column == 4 &&
            shifted_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_four[0].value.text_value() == "#REF!+#REF!" &&
            shifted_column_four[0].value.has_style() &&
            shifted_column_four[0].value.style_id().value() == styled_formula_style.value(),
        "delete_rows styled source formula column_cells should keep shifted formula style id");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "delete_rows styled source formula should report Data as dirty");
    check(editor.pending_materialized_cell_count() == 5,
        "delete_rows styled source formula should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "delete_rows styled source formula should report shifted sparse memory");
    check(!editor.last_edit_error().has_value(),
        "delete_rows styled source formula should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows styled source formula save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "delete_rows styled source formula save_as should project shifted bounds");
    check_contains(worksheet_xml, styled_formula_xml,
        "delete_rows styled source formula save_as should write shifted formula with style id");
    check_contains(worksheet_xml, R"(<c r="A1")",
        "delete_rows styled source formula save_as should write shifted source A2");
    check_contains(worksheet_xml, R"(<c r="B1")",
        "delete_rows styled source formula save_as should write shifted source B2");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "delete_rows styled source formula save_as should write shifted trailing row");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "delete_rows styled source formula save_as should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "delete_rows styled source formula save_as should omit old trailing row coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "delete_rows styled source formula should preserve untouched worksheets");
    const auto inspect_delete_rows_styled_formula_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "delete_rows styled source formula reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "delete_rows styled source formula reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d1->text_value() == "#REF!+#REF!" &&
                    reopened_d1->has_style() &&
                    reopened_d1->style_id().value() == styled_formula_style.value(),
                "delete_rows styled source formula reopened output should read styled formula");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("A2").text_value() == "extra-c3",
                "delete_rows styled source formula reopened output should read shifted source rows");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "delete_rows styled source formula reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(output, "delete_rows styled source formula",
        inspect_delete_rows_styled_formula_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "delete_rows styled source formula no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "delete_rows styled source formula no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "delete_rows styled source formula no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "delete_rows styled source formula no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_rows styled source formula no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_rows styled source formula no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "delete_rows styled source formula no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "delete_rows styled source formula no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "delete_rows styled source formula no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows styled source formula no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output,
        "delete_rows styled source formula no-op save",
        inspect_delete_rows_styled_formula_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "delete_rows styled source formula second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "delete_rows styled source formula second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "delete_rows styled source formula second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "delete_rows styled source formula second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_rows styled source formula second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_rows styled source formula second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "delete_rows styled source formula second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "delete_rows styled source formula second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "delete_rows styled source formula second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows styled source formula second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_rows styled source formula second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(second_noop_output,
        "delete_rows styled source formula second no-op save",
        inspect_delete_rows_styled_formula_output);

    sheet.set_cell("E2", fastxlsx::CellValue::text("post-noop-delete-rows-styled"));
    check(sheet.has_pending_changes(),
        "delete_rows styled source formula post-noop edit should dirty the saved handle");
    check(sheet.cell_count() == 6,
        "delete_rows styled source formula post-noop edit should add one sparse cell");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 5,
        "delete_rows styled source formula post-noop edit should expand bounds to E2");
    const std::optional<fastxlsx::CellValue> retained_formula = sheet.try_cell("D1");
    check(retained_formula.has_value() &&
            retained_formula->kind() == fastxlsx::CellValueKind::Formula &&
            retained_formula->text_value() == "#REF!+#REF!" &&
            retained_formula->has_style() &&
            retained_formula->style_id().value() == styled_formula_style.value(),
        "delete_rows styled source formula post-noop edit should preserve shifted formula style id");
    check(editor.pending_change_count() == 1,
        "delete_rows styled source formula post-noop edit should retain the prior handoff before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 6 &&
            editor.estimated_pending_materialized_memory_usage() == sheet.estimated_memory_usage(),
        "delete_rows styled source formula post-noop edit should report the dirty materialized sheet");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "delete_rows styled source formula post-noop save should clean the materialized handle");
    check(editor.pending_change_count() == 2,
        "delete_rows styled source formula post-noop save should record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_rows styled source formula post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_rows styled source formula post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_rows styled source formula post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "delete_rows styled source formula post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_rows styled source formula post-noop save should leave the no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "delete_rows styled source formula post-noop save should leave the second no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows styled source formula post-noop save should leave the source package unchanged");
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, styled_formula_xml,
        "delete_rows styled source formula post-noop save should keep the styled formula cell");
    check_contains(post_noop_xml, R"(<c r="E2")",
        "delete_rows styled source formula post-noop save should write the post-noop edit");
    const auto inspect_delete_rows_styled_formula_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 6,
                "delete_rows styled source formula post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 5,
                "delete_rows styled source formula post-noop reopened output should expose expanded bounds");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d1->text_value() == "#REF!+#REF!" &&
                    reopened_d1->has_style() &&
                    reopened_d1->style_id().value() == styled_formula_style.value(),
                "delete_rows styled source formula post-noop reopened output should keep styled formula");
            const std::optional<fastxlsx::CellValue> reopened_e2 =
                reopened_sheet.try_cell("E2");
            check(reopened_e2.has_value() &&
                    reopened_e2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_e2->text_value() == "post-noop-delete-rows-styled",
                "delete_rows styled source formula post-noop reopened output should read post-noop edit");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("A2").text_value() == "extra-c3",
                "delete_rows styled source formula post-noop reopened output should keep shifted source rows");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "delete_rows styled source formula post-noop reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(post_noop_output,
        "delete_rows styled source formula post-noop save",
        inspect_delete_rows_styled_formula_post_noop_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_post_noop_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_post_noop_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(post_noop_noop_output);
    check(!sheet.has_pending_changes(),
        "delete_rows styled source formula post-noop noop save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "delete_rows styled source formula post-noop noop save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_rows styled source formula post-noop noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_rows styled source formula post-noop noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_rows styled source formula post-noop noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_post_noop_noop,
        "delete_rows styled source formula post-noop noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_post_noop_noop,
        "delete_rows styled source formula post-noop noop save");
    const auto post_noop_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_noop_output);
    check(post_noop_noop_entries == post_noop_entries,
        "delete_rows styled source formula post-noop noop output should match post-noop output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows styled source formula post-noop noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(post_noop_output) == post_noop_entries,
        "delete_rows styled source formula post-noop noop save should leave prior post-noop output unchanged");
    check_reopened_shift_output(post_noop_noop_output,
        "delete_rows styled source formula post-noop noop save",
        inspect_delete_rows_styled_formula_post_noop_output);
}

void test_public_worksheet_editor_delete_rows_preserves_shifted_value_only_style()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-delete-rows-value-only-style-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-value-only-style-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-value-only-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-value-only-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-value-only-style-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell_value("D2", fastxlsx::CellValue::text("delete-row-value-only-styled"));
    const std::optional<fastxlsx::CellValue> value_only_d2 = sheet.try_cell("D2");
    check(value_only_d2.has_value() &&
            value_only_d2->kind() == fastxlsx::CellValueKind::Text &&
            value_only_d2->text_value() == "delete-row-value-only-styled" &&
            value_only_d2->has_style() &&
            value_only_d2->style_id().value() == styled_formula_style.value(),
        "set_cell_value should preserve the source style before delete_rows shifts it");

    sheet.delete_rows(1, 1);

    check(sheet.cell_count() == 5,
        "delete_rows should keep value-only shifted sparse count after deleting source row one");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
        "delete_rows should refresh bounds for shifted value-only styled cells");
    const std::optional<fastxlsx::CellValue> shifted_value = sheet.try_cell("D1");
    check(shifted_value.has_value() &&
            shifted_value->kind() == fastxlsx::CellValueKind::Text &&
            shifted_value->text_value() == "delete-row-value-only-styled" &&
            shifted_value->has_style() &&
            shifted_value->style_id().value() == styled_formula_style.value(),
        "delete_rows should move value-only cells with the preserved source style id");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "delete_rows value-only style should shift remaining source rows");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A3").has_value(),
        "delete_rows value-only style should keep old coordinates absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_one = sheet.row_cells(1);
    check(shifted_row_one.size() == 4,
        "delete_rows value-only row_cells should expose shifted row one");
    check(shifted_row_one[3].reference.row == 1 &&
            shifted_row_one[3].reference.column == 4 &&
            shifted_row_one[3].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_one[3].value.text_value() == "delete-row-value-only-styled" &&
            shifted_row_one[3].value.has_style() &&
            shifted_row_one[3].value.style_id().value() == styled_formula_style.value(),
        "delete_rows value-only row_cells should keep the shifted style id");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_four =
        sheet.column_cells(4);
    check(shifted_column_four.size() == 1,
        "delete_rows value-only column_cells should expose shifted style column");
    check(shifted_column_four[0].reference.row == 1 &&
            shifted_column_four[0].reference.column == 4 &&
            shifted_column_four[0].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_column_four[0].value.text_value() == "delete-row-value-only-styled" &&
            shifted_column_four[0].value.has_style() &&
            shifted_column_four[0].value.style_id().value() == styled_formula_style.value(),
        "delete_rows value-only column_cells should keep the shifted style id");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "delete_rows value-only style pre-save shift summary");
    check(editor.pending_materialized_cell_count() == 5,
        "delete_rows value-only style should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "delete_rows value-only style should report shifted sparse memory");
    check(!editor.last_edit_error().has_value(),
        "successful delete_rows value-only style shift should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows value-only style save_as should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "delete_rows value-only style save_as should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_text =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"(" t="inlineStr"><is><t>delete-row-value-only-styled</t></is></c>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "delete_rows value-only style save_as should project shifted bounds");
    check_contains(worksheet_xml, styled_text,
        "delete_rows value-only style save_as should write shifted text with source style id");
    check_contains(worksheet_xml, R"(<c r="A1")",
        "delete_rows value-only style save_as should write shifted source A2");
    check_contains(worksheet_xml, R"(<c r="B1")",
        "delete_rows value-only style save_as should write shifted source B2");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "delete_rows value-only style save_as should write shifted trailing row");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "delete_rows value-only style save_as should omit the old coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "delete_rows value-only style save_as should omit the old trailing row coordinate");
    check_not_contains(worksheet_xml, R"(<f>A1+B1</f>)",
        "delete_rows value-only style save_as should omit the replaced source formula");
    check_not_contains(worksheet_xml, R"(<f>#REF!+#REF!</f>)",
        "delete_rows value-only style save_as should not resurrect the shifted formula");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "delete_rows value-only style should preserve untouched worksheets");

    const auto inspect_delete_rows_value_only_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "delete_rows value-only style reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "delete_rows value-only style reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_d1->text_value() == "delete-row-value-only-styled" &&
                    reopened_d1->has_style() &&
                    reopened_d1->style_id().value() == styled_formula_style.value(),
                "delete_rows value-only style reopened output should preserve shifted source style");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("A2").text_value() == "extra-c3",
                "delete_rows value-only style reopened output should read shifted source rows");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "delete_rows value-only style reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(output, "delete_rows value-only style",
        inspect_delete_rows_value_only_output);
    check_reopened_untouched_keep_me_output(output, "delete_rows value-only style");
    const auto noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        "delete_rows value-only style no-op save",
        inspect_delete_rows_value_only_output);
    const auto second_noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        "delete_rows value-only style second no-op save",
        inspect_delete_rows_value_only_output);
    check(second_noop_entries == noop_entries,
        "delete_rows value-only style second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_rows value-only style second no-op save should leave first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "delete_rows value-only style second no-op save should leave the materialized output unchanged");
    const auto inspect_delete_rows_value_only_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 6,
                "delete_rows value-only style post-noop output should include the later edit");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 5,
                "delete_rows value-only style post-noop output should expand to the later edit");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_d1->text_value() == "delete-row-value-only-styled" &&
                    reopened_d1->has_style() &&
                    reopened_d1->style_id().value() == styled_formula_style.value(),
                "delete_rows value-only style post-noop output should preserve shifted source style");
            const std::optional<fastxlsx::CellValue> reopened_e2 =
                reopened_sheet.try_cell("E2");
            check(reopened_e2.has_value() &&
                    reopened_e2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_e2->text_value() == "post-noop-delete-rows-value-only-style",
                "delete_rows value-only style post-noop output should include the later edit");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("A2").text_value() == "extra-c3",
                "delete_rows value-only style post-noop output should read shifted source rows");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "delete_rows value-only style post-noop output should keep old coordinates absent");
        };
    check_shift_post_noop_edit_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        second_noop_entries,
        post_noop_output,
        "delete_rows value-only style post-noop edit save",
        6,
        [&sheet]() {
            sheet.set_cell("E2",
                fastxlsx::CellValue::text("post-noop-delete-rows-value-only-style"));
        },
        inspect_delete_rows_value_only_post_noop_output);
}

void test_public_worksheet_editor_delete_rows_preserves_shifted_clear_value_style()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-delete-rows-clear-value-style-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-clear-value-style-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-clear-value-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-clear-value-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-rows-clear-value-style-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.clear_cell_value("D2");
    const std::optional<fastxlsx::CellValue> cleared_d2 = sheet.try_cell("D2");
    check(cleared_d2.has_value() &&
            cleared_d2->kind() == fastxlsx::CellValueKind::Blank &&
            cleared_d2->has_style() &&
            cleared_d2->style_id().value() == styled_formula_style.value(),
        "clear_cell_value should preserve the source style before delete_rows shifts it");

    sheet.delete_rows(1, 1);

    check(sheet.cell_count() == 5,
        "delete_rows should keep cleared shifted sparse count after deleting source row one");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
        "delete_rows should refresh bounds for shifted cleared styled cells");
    const std::optional<fastxlsx::CellValue> shifted_blank = sheet.try_cell("D1");
    check(shifted_blank.has_value() &&
            shifted_blank->kind() == fastxlsx::CellValueKind::Blank &&
            shifted_blank->has_style() &&
            shifted_blank->style_id().value() == styled_formula_style.value(),
        "delete_rows should move cleared cells with the preserved source style id");
    check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
            sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
            sheet.get_cell("A2").text_value() == "extra-c3",
        "delete_rows cleared style should shift remaining source rows");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A3").has_value(),
        "delete_rows cleared style should keep old coordinates absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_one = sheet.row_cells(1);
    check(shifted_row_one.size() == 4,
        "delete_rows cleared row_cells should expose shifted row one");
    check(shifted_row_one[3].reference.row == 1 &&
            shifted_row_one[3].reference.column == 4 &&
            shifted_row_one[3].value.kind() == fastxlsx::CellValueKind::Blank &&
            shifted_row_one[3].value.has_style() &&
            shifted_row_one[3].value.style_id().value() == styled_formula_style.value(),
        "delete_rows cleared row_cells should keep the shifted style id");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_four =
        sheet.column_cells(4);
    check(shifted_column_four.size() == 1 &&
            shifted_column_four[0].reference.row == 1 &&
            shifted_column_four[0].reference.column == 4 &&
            shifted_column_four[0].value.kind() == fastxlsx::CellValueKind::Blank &&
            shifted_column_four[0].value.has_style() &&
            shifted_column_four[0].value.style_id().value() == styled_formula_style.value(),
        "delete_rows cleared column_cells should keep the shifted style id");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "delete_rows cleared style pre-save shift summary");
    check(editor.pending_materialized_cell_count() == 5,
        "delete_rows cleared style should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "delete_rows cleared style should report shifted sparse memory");
    check(!editor.last_edit_error().has_value(),
        "successful delete_rows cleared style shift should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_rows cleared style save_as should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "delete_rows cleared style save_as should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_blank =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value()) + R"("/>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "delete_rows cleared style save_as should project shifted bounds");
    check_contains(worksheet_xml, styled_blank,
        "delete_rows cleared style save_as should write shifted blank with source style id");
    check_contains(worksheet_xml, R"(<c r="A1")",
        "delete_rows cleared style save_as should write shifted source A2");
    check_contains(worksheet_xml, R"(<c r="B1")",
        "delete_rows cleared style save_as should write shifted source B2");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "delete_rows cleared style save_as should write shifted trailing row");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "delete_rows cleared style save_as should omit the old coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "delete_rows cleared style save_as should omit the old trailing row coordinate");
    check_not_contains(worksheet_xml, R"(<f>A1+B1</f>)",
        "delete_rows cleared style save_as should omit the cleared source formula");
    check_not_contains(worksheet_xml, R"(<f>#REF!+#REF!</f>)",
        "delete_rows cleared style save_as should not resurrect the shifted formula");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "delete_rows cleared style should preserve untouched worksheets");

    const auto inspect_delete_rows_cleared_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "delete_rows cleared style reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "delete_rows cleared style reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Blank &&
                    reopened_d1->has_style() &&
                    reopened_d1->style_id().value() == styled_formula_style.value(),
                "delete_rows cleared style reopened output should preserve shifted source style");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("A2").text_value() == "extra-c3",
                "delete_rows cleared style reopened output should read shifted source rows");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "delete_rows cleared style reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(output, "delete_rows cleared style",
        inspect_delete_rows_cleared_output);
    check_reopened_untouched_keep_me_output(output, "delete_rows cleared style");
    const auto noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        "delete_rows cleared style no-op save",
        inspect_delete_rows_cleared_output);
    const auto second_noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        "delete_rows cleared style second no-op save",
        inspect_delete_rows_cleared_output);
    check(second_noop_entries == noop_entries,
        "delete_rows cleared style second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_rows cleared style second no-op save should leave first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "delete_rows cleared style second no-op save should leave the materialized output unchanged");
    const auto inspect_delete_rows_cleared_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 6,
                "delete_rows cleared style post-noop output should include the later edit");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 5,
                "delete_rows cleared style post-noop output should expand to the later edit");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Blank &&
                    reopened_d1->has_style() &&
                    reopened_d1->style_id().value() == styled_formula_style.value(),
                "delete_rows cleared style post-noop output should preserve shifted source style");
            const std::optional<fastxlsx::CellValue> reopened_e2 =
                reopened_sheet.try_cell("E2");
            check(reopened_e2.has_value() &&
                    reopened_e2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_e2->text_value() == "post-noop-delete-rows-cleared-style",
                "delete_rows cleared style post-noop output should include the later edit");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("A2").text_value() == "extra-c3",
                "delete_rows cleared style post-noop output should read shifted source rows");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "delete_rows cleared style post-noop output should keep old coordinates absent");
        };
    check_shift_post_noop_edit_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        second_noop_entries,
        post_noop_output,
        "delete_rows cleared style post-noop edit save",
        6,
        [&sheet]() {
            sheet.set_cell("E2",
                fastxlsx::CellValue::text("post-noop-delete-rows-cleared-style"));
        },
        inspect_delete_rows_cleared_post_noop_output);
}

void test_public_worksheet_editor_full_calculation_preserves_delete_rows_ref_shift()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-delete-rows-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-delete-rows-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-delete-rows-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-delete-rows-second-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.delete_rows(1, 1);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(dirty_cell_count == 5,
        "full-calc delete_rows setup should keep shifted sparse count");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D1");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+#REF!" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "full-calc delete_rows setup should translate deleted references and preserve style id");

    editor.request_full_calculation();

    check(!editor.last_edit_error().has_value(),
        "request_full_calculation after delete_rows should clear diagnostics");
    check(editor.pending_change_count() == 1,
        "request_full_calculation after delete_rows should add one metadata edit");
    check(sheet.has_pending_changes(),
        "request_full_calculation after delete_rows should keep the shifted sheet dirty");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "request_full_calculation after delete_rows should preserve dirty materialized names");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "request_full_calculation after delete_rows should preserve dirty sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "request_full_calculation after delete_rows should preserve dirty sparse memory");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 1, "request_full_calculation after delete_rows dirty summary");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc delete_rows save_as should clean the shifted materialized sheet");
    check(editor.pending_change_count() == 2,
        "full-calc delete_rows save_as should count metadata edit plus materialized flush");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc delete_rows save_as should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc delete_rows save_as should clear dirty materialized count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc delete_rows save_as should clear dirty materialized memory");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc delete_rows save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc delete_rows save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc delete_rows save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "full-calc delete_rows save_as should project shifted bounds");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc delete_rows save_as should write shifted #REF! formula with style id");
    check_contains(worksheet_xml, R"(<c r="A1")",
        "full-calc delete_rows save_as should write shifted source row");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "full-calc delete_rows save_as should write shifted trailing row");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc delete_rows save_as should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "full-calc delete_rows save_as should omit old trailing coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc delete_rows save_as should preserve untouched worksheets");

    const auto inspect_full_calc_delete_rows_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "full-calc delete_rows reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "full-calc delete_rows reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d1->text_value() == "#REF!+#REF!" &&
                    reopened_d1->has_style() &&
                    reopened_d1->style_id().value() == styled_formula_style.value(),
                "full-calc delete_rows reopened output should read shifted #REF! formula");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("A2").text_value() == "extra-c3",
                "full-calc delete_rows reopened output should read shifted source rows");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
                reopened_sheet.row_cells(1);
            check(reopened_row_one.size() == 4 &&
                    reopened_row_one[0].reference.row == 1 &&
                    reopened_row_one[0].reference.column == 1 &&
                    reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_one[1].reference.row == 1 &&
                    reopened_row_one[1].reference.column == 2 &&
                    reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[1].value.text_value() == "row2-gap-b2" &&
                    reopened_row_one[2].reference.row == 1 &&
                    reopened_row_one[2].reference.column == 3 &&
                    reopened_row_one[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[2].value.text_value() == "row2-gap-c2" &&
                    reopened_row_one[3].reference.row == 1 &&
                    reopened_row_one[3].reference.column == 4 &&
                    reopened_row_one[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_one[3].value.text_value() == "#REF!+#REF!" &&
                    reopened_row_one[3].value.has_style() &&
                    reopened_row_one[3].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc delete_rows reopened row_cells should expose shifted first row");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_one =
                reopened_sheet.column_cells(1);
            check(reopened_column_one.size() == 2 &&
                    reopened_column_one[0].reference.row == 1 &&
                    reopened_column_one[0].reference.column == 1 &&
                    reopened_column_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_one[0].value.text_value() == "placeholder-a2" &&
                    reopened_column_one[1].reference.row == 2 &&
                    reopened_column_one[1].reference.column == 1 &&
                    reopened_column_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_column_one[1].value.text_value() == "extra-c3",
                "full-calc delete_rows reopened column_cells should expose shifted source rows");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "full-calc delete_rows reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(output, "full-calc delete_rows",
        inspect_full_calc_delete_rows_output);
    check_reopened_untouched_keep_me_output(
        output, "full-calc delete_rows Untouched");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc delete_rows no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc delete_rows no-op save should not record another workbook or materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc delete_rows no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc delete_rows no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc delete_rows no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc delete_rows no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "full-calc delete_rows no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "full-calc delete_rows no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc delete_rows no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc delete_rows no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "full-calc delete_rows no-op save",
        inspect_full_calc_delete_rows_output);
    check_reopened_untouched_keep_me_output(
        noop_output, "full-calc delete_rows no-op Untouched");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc delete_rows second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc delete_rows second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc delete_rows second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc delete_rows second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc delete_rows second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc delete_rows second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "full-calc delete_rows second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "full-calc delete_rows second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc delete_rows second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc delete_rows second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc delete_rows second no-op save should leave materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc delete_rows second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(second_noop_output,
        "full-calc delete_rows second no-op save",
        inspect_full_calc_delete_rows_output);
    check_reopened_untouched_keep_me_output(
        second_noop_output, "full-calc delete_rows second no-op Untouched");
}

void test_public_worksheet_editor_full_calculation_preserves_delete_rows_ref_shift_failed_save_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-delete-rows-failed-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-delete-rows-failed-save-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-delete-rows-failed-save-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-delete-rows-failed-save-second-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.delete_rows(1, 1);
    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(dirty_cell_count == 5,
        "full-calc delete_rows failed save setup should keep shifted sparse count");

    editor.request_full_calculation();

    const auto source_entries_before_failed_save =
        fastxlsx::test::read_zip_entries(source);
    const auto check_dirty_delete_rows_session = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep diagnostics clear");
        check(editor.has_pending_changes(),
            label + " should keep the public editor dirty");
        check(editor.pending_change_count() == 1,
            label + " should keep only the queued workbook metadata edit before materialized handoff");
        check(sheet.has_pending_changes(),
            label + " should keep the shifted worksheet dirty");
        check(sheet.cell_count() == dirty_cell_count,
            label + " should preserve the shifted sparse count");
        check(sheet.estimated_memory_usage() == dirty_memory_usage,
            label + " should preserve the shifted materialized memory estimate");
        check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
            label + " should expose shifted bounds");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"Data"},
            label + " should report Data dirty");
        check(editor.pending_materialized_cell_count() == dirty_cell_count,
            label + " should report the shifted sparse count");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
            label + " should report the shifted sparse memory");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, label + " dirty materialized summary");

        const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D1");
        check(shifted_formula.has_value() &&
                shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula->text_value() == "#REF!+#REF!" &&
                shifted_formula->has_style() &&
                shifted_formula->style_id().value() == styled_formula_style.value(),
            label + " should keep translated #REF! formula and style id in memory");
        check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                sheet.get_cell("C1").text_value() == "row2-gap-c2" &&
                sheet.get_cell("A2").text_value() == "extra-c3",
            label + " should keep shifted source rows in memory");
        check(!sheet.try_cell("D2").has_value() &&
                !sheet.try_cell("A3").has_value(),
            label + " should keep old and deleted coordinates absent");
    };

    check_dirty_delete_rows_session(
        "full-calc delete_rows failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "full-calc delete_rows failed save should reject exact source overwrite");
    check_dirty_delete_rows_session(
        "full-calc delete_rows failed save rejected source overwrite");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc delete_rows failed save should leave source package unchanged");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc delete_rows failed save safe retry should clean the shifted sheet");
    check(editor.has_pending_changes(),
        "full-calc delete_rows failed save safe retry should retain staged public changes");
    check(editor.pending_change_count() == 2,
        "full-calc delete_rows failed save safe retry should count metadata plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc delete_rows failed save safe retry should clear dirty diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc delete_rows failed save safe retry should clear dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc delete_rows failed save safe retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc delete_rows failed save safe retry should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc delete_rows failed save safe retry should leave source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc delete_rows failed save safe retry should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc delete_rows failed save safe retry should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "full-calc delete_rows failed save safe retry should project shifted bounds");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc delete_rows failed save safe retry should write shifted styled #REF! formula");
    check_contains(worksheet_xml, R"(<c r="A1")",
        "full-calc delete_rows failed save safe retry should write shifted source row");
    check_contains(worksheet_xml, R"(<c r="B1")",
        "full-calc delete_rows failed save safe retry should write shifted source column");
    check_contains(worksheet_xml, R"(<c r="A2" t="inlineStr"><is><t>extra-c3</t></is></c>)",
        "full-calc delete_rows failed save safe retry should write shifted trailing row");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc delete_rows failed save safe retry should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "full-calc delete_rows failed save safe retry should omit old trailing coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc delete_rows failed save safe retry should preserve untouched worksheets");

    const auto inspect_full_calc_delete_rows_failed_save_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "full-calc delete_rows failed save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "full-calc delete_rows failed save reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d1->text_value() == "#REF!+#REF!" &&
                    reopened_d1->has_style() &&
                    reopened_d1->style_id().value() == styled_formula_style.value(),
                "full-calc delete_rows failed save reopened output should read shifted #REF formula style");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("C1").text_value() == "row2-gap-c2" &&
                    reopened_sheet.get_cell("A2").text_value() == "extra-c3",
                "full-calc delete_rows failed save reopened output should read shifted source rows");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
                reopened_sheet.row_cells(1);
            check(reopened_row_one.size() == 4 &&
                    reopened_row_one[0].reference.row == 1 &&
                    reopened_row_one[0].reference.column == 1 &&
                    reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_one[1].reference.row == 1 &&
                    reopened_row_one[1].reference.column == 2 &&
                    reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[1].value.text_value() == "row2-gap-b2" &&
                    reopened_row_one[2].reference.row == 1 &&
                    reopened_row_one[2].reference.column == 3 &&
                    reopened_row_one[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[2].value.text_value() == "row2-gap-c2" &&
                    reopened_row_one[3].reference.row == 1 &&
                    reopened_row_one[3].reference.column == 4 &&
                    reopened_row_one[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_one[3].value.text_value() == "#REF!+#REF!" &&
                    reopened_row_one[3].value.has_style() &&
                    reopened_row_one[3].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc delete_rows failed save reopened row_cells should expose shifted sparse order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_four =
                reopened_sheet.column_cells(4);
            check(reopened_column_four.size() == 1 &&
                    reopened_column_four[0].reference.row == 1 &&
                    reopened_column_four[0].reference.column == 4 &&
                    reopened_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_four[0].value.text_value() == "#REF!+#REF!" &&
                    reopened_column_four[0].value.has_style() &&
                    reopened_column_four[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc delete_rows failed save reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "full-calc delete_rows failed save reopened output should keep old coordinates absent");
        };

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc delete_rows failed save no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc delete_rows failed save no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc delete_rows failed save no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc delete_rows failed save no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc delete_rows failed save no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "full-calc delete_rows failed save no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "full-calc delete_rows failed save no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc delete_rows failed save no-op output should match safe retry output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc delete_rows failed save no-op save should leave safe retry output unchanged");
    check_reopened_shift_output(
        noop_output,
        "full-calc delete_rows failed save no-op save",
        inspect_full_calc_delete_rows_failed_save_output);
    check_reopened_untouched_keep_me_output(
        noop_output, "full-calc delete_rows failed save no-op Untouched");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc delete_rows failed save source after no-op");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc delete_rows failed save second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc delete_rows failed save second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc delete_rows failed save second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc delete_rows failed save second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc delete_rows failed save second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "full-calc delete_rows failed save second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "full-calc delete_rows failed save second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc delete_rows failed save second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc delete_rows failed save second no-op save should leave safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc delete_rows failed save second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(
        second_noop_output,
        "full-calc delete_rows failed save second no-op save",
        inspect_full_calc_delete_rows_failed_save_output);
    check_reopened_untouched_keep_me_output(
        second_noop_output, "full-calc delete_rows failed save second no-op Untouched");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc delete_rows failed save source after second no-op");
}

void test_public_worksheet_editor_full_calculation_before_delete_rows_ref_shift()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-rows-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-rows-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-rows-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-rows-second-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.request_full_calculation();
    check(!editor.last_edit_error().has_value(),
        "full-calc before delete_rows setup should clear diagnostics");
    check(editor.pending_change_count() == 1,
        "full-calc before delete_rows setup should queue one workbook metadata edit");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc before delete_rows setup should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc before delete_rows setup should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before delete_rows setup should not expose dirty materialized memory");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    check(!sheet.has_pending_changes(),
        "worksheet() after full-calc before delete_rows should materialize cleanly");
    check(editor.pending_change_count() == 1,
        "clean materialization after full-calc before delete_rows should keep metadata edit count");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "clean materialization after full-calc before delete_rows should keep dirty diagnostics clear");

    sheet.delete_rows(1, 1);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(dirty_cell_count == 5,
        "full-calc before delete_rows should keep shifted sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
        "full-calc before delete_rows should expose shifted bounds");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D1");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+#REF!" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "full-calc before delete_rows should translate deleted references and preserve style id");
    check(editor.pending_change_count() == 1,
        "full-calc before delete_rows should not flush materialized state before save_as");
    check(sheet.has_pending_changes(),
        "full-calc before delete_rows should leave the shifted sheet dirty");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "full-calc before delete_rows should report Data dirty");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "full-calc before delete_rows should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "full-calc before delete_rows should report shifted sparse memory");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 1, "full-calc before delete_rows pre-save dirty summary");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc before delete_rows save_as should clean the shifted materialized sheet");
    check(editor.pending_change_count() == 2,
        "full-calc before delete_rows save_as should count metadata edit plus materialized flush");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc before delete_rows save_as should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc before delete_rows save_as should clear dirty materialized count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before delete_rows save_as should clear dirty materialized memory");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before delete_rows save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc before delete_rows save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc before delete_rows save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "full-calc before delete_rows save_as should project shifted bounds");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc before delete_rows save_as should write shifted #REF! formula with style id");
    check_contains(worksheet_xml, R"(<c r="A1")",
        "full-calc before delete_rows save_as should write shifted source row");
    check_contains(worksheet_xml, R"(<c r="B1")",
        "full-calc before delete_rows save_as should write shifted source column");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "full-calc before delete_rows save_as should write shifted trailing row");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc before delete_rows save_as should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "full-calc before delete_rows save_as should omit deleted trailing coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc before delete_rows should preserve untouched worksheets");

    const auto inspect_full_calc_before_delete_rows_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "full-calc before delete_rows reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "full-calc before delete_rows reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d1->text_value() == "#REF!+#REF!" &&
                    reopened_d1->has_style() &&
                    reopened_d1->style_id().value() == styled_formula_style.value(),
                "full-calc before delete_rows reopened output should read shifted styled #REF! formula");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("C1").text_value() == "row2-gap-c2" &&
                    reopened_sheet.get_cell("A2").text_value() == "extra-c3",
                "full-calc before delete_rows reopened output should read shifted source rows");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
                reopened_sheet.row_cells(1);
            check(reopened_row_one.size() == 4 &&
                    reopened_row_one[0].reference.row == 1 &&
                    reopened_row_one[0].reference.column == 1 &&
                    reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_one[1].reference.row == 1 &&
                    reopened_row_one[1].reference.column == 2 &&
                    reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[1].value.text_value() == "row2-gap-b2" &&
                    reopened_row_one[2].reference.row == 1 &&
                    reopened_row_one[2].reference.column == 3 &&
                    reopened_row_one[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[2].value.text_value() == "row2-gap-c2" &&
                    reopened_row_one[3].reference.row == 1 &&
                    reopened_row_one[3].reference.column == 4 &&
                    reopened_row_one[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_one[3].value.text_value() == "#REF!+#REF!" &&
                    reopened_row_one[3].value.has_style() &&
                    reopened_row_one[3].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before delete_rows reopened row_cells should expose shifted sparse order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_four =
                reopened_sheet.column_cells(4);
            check(reopened_column_four.size() == 1 &&
                    reopened_column_four[0].reference.row == 1 &&
                    reopened_column_four[0].reference.column == 4 &&
                    reopened_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_four[0].value.text_value() == "#REF!+#REF!" &&
                    reopened_column_four[0].value.has_style() &&
                    reopened_column_four[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before delete_rows reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "full-calc before delete_rows reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(output,
        "full-calc before delete_rows",
        inspect_full_calc_before_delete_rows_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before delete_rows no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before delete_rows no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before delete_rows no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before delete_rows no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before delete_rows no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "full-calc before delete_rows no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "full-calc before delete_rows no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc before delete_rows no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before delete_rows no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output,
        "full-calc before delete_rows no-op save",
        inspect_full_calc_before_delete_rows_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before delete_rows second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before delete_rows second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before delete_rows second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before delete_rows second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before delete_rows second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_second_noop,
        "full-calc before delete_rows second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_second_noop,
        "full-calc before delete_rows second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc before delete_rows second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before delete_rows second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc before delete_rows second no-op save should leave materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc before delete_rows second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(second_noop_output,
        "full-calc before delete_rows second no-op save",
        inspect_full_calc_before_delete_rows_output);
}

void test_public_worksheet_editor_full_calculation_before_delete_rows_ref_shift_failed_save_preserves_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-rows-failed-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-rows-failed-save-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-rows-failed-save-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-rows-failed-save-second-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.request_full_calculation();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.delete_rows(1, 1);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    const auto source_entries_before_failed_save =
        fastxlsx::test::read_zip_entries(source);
    check(dirty_cell_count == 5,
        "full-calc before delete_rows failed save setup should keep shifted sparse count");

    const auto check_dirty_delete_rows_session = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep diagnostics clear");
        check(editor.has_pending_changes(),
            label + " should keep the public editor dirty");
        check(editor.pending_change_count() == 1,
            label + " should keep only the queued workbook metadata edit before materialized handoff");
        check(sheet.has_pending_changes(),
            label + " should keep the shifted worksheet dirty");
        check(sheet.cell_count() == dirty_cell_count,
            label + " should preserve the shifted sparse count");
        check(sheet.estimated_memory_usage() == dirty_memory_usage,
            label + " should preserve the shifted materialized memory estimate");
        check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
            label + " should expose shifted bounds");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"Data"},
            label + " should report Data dirty");
        check(editor.pending_materialized_cell_count() == dirty_cell_count,
            label + " should report the shifted sparse count");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
            label + " should report the shifted sparse memory");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, label + " dirty materialized summary");

        const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D1");
        check(shifted_formula.has_value() &&
                shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula->text_value() == "#REF!+#REF!" &&
                shifted_formula->has_style() &&
                shifted_formula->style_id().value() == styled_formula_style.value(),
            label + " should keep translated #REF! formula and style id in memory");
        check(sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                sheet.get_cell("C1").text_value() == "row2-gap-c2" &&
                sheet.get_cell("A2").text_value() == "extra-c3",
            label + " should keep shifted source rows in memory");
        check(!sheet.try_cell("D2").has_value() &&
                !sheet.try_cell("A3").has_value(),
            label + " should keep old and deleted coordinates absent");
    };

    check_dirty_delete_rows_session(
        "full-calc before delete_rows failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "full-calc before delete_rows failed save should reject exact source overwrite");
    check_dirty_delete_rows_session(
        "full-calc before delete_rows failed save rejected source overwrite");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc before delete_rows failed save should leave source package unchanged");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc before delete_rows failed save safe retry should clean the shifted sheet");
    check(editor.has_pending_changes(),
        "full-calc before delete_rows failed save safe retry should retain staged public changes");
    check(editor.pending_change_count() == 2,
        "full-calc before delete_rows failed save safe retry should count metadata plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before delete_rows failed save safe retry should clear dirty diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc before delete_rows failed save safe retry should clear dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc before delete_rows failed save safe retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before delete_rows failed save safe retry should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc before delete_rows failed save safe retry should leave source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D1" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+#REF!</f></c>)";
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc before delete_rows failed save safe retry should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc before delete_rows failed save safe retry should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "full-calc before delete_rows failed save safe retry should project shifted bounds");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc before delete_rows failed save safe retry should write shifted styled #REF! formula");
    check_contains(worksheet_xml, R"(<c r="A1")",
        "full-calc before delete_rows failed save safe retry should write shifted source row");
    check_contains(worksheet_xml, R"(<c r="B1")",
        "full-calc before delete_rows failed save safe retry should write shifted source column");
    check_contains(worksheet_xml, R"(<c r="A2" t="inlineStr"><is><t>extra-c3</t></is></c>)",
        "full-calc before delete_rows failed save safe retry should write shifted trailing row");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc before delete_rows failed save safe retry should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "full-calc before delete_rows failed save safe retry should omit old trailing coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc before delete_rows failed save safe retry should preserve untouched worksheets");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before delete_rows failed save no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before delete_rows failed save no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before delete_rows failed save no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc before delete_rows failed save no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before delete_rows failed save no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "full-calc before delete_rows failed save no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "full-calc before delete_rows failed save no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc before delete_rows failed save no-op output should match safe retry output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc before delete_rows failed save no-op save should leave safe retry output unchanged");
    check_reopened_shift_output(
        noop_output,
        "full-calc before delete_rows failed save no-op save",
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "full-calc before delete_rows failed save no-op reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "full-calc before delete_rows failed save no-op reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d1 = reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d1->text_value() == "#REF!+#REF!" &&
                    reopened_d1->has_style() &&
                    reopened_d1->style_id().value() == styled_formula_style.value(),
                "full-calc before delete_rows failed save no-op reopened output should read shifted #REF formula style");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("C1").text_value() == "row2-gap-c2" &&
                    reopened_sheet.get_cell("A2").text_value() == "extra-c3",
                "full-calc before delete_rows failed save no-op reopened output should read shifted source rows");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_one =
                reopened_sheet.row_cells(1);
            check(reopened_row_one.size() == 4 &&
                    reopened_row_one[0].reference.row == 1 &&
                    reopened_row_one[0].reference.column == 1 &&
                    reopened_row_one[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[0].value.text_value() == "placeholder-a2" &&
                    reopened_row_one[1].reference.row == 1 &&
                    reopened_row_one[1].reference.column == 2 &&
                    reopened_row_one[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[1].value.text_value() == "row2-gap-b2" &&
                    reopened_row_one[2].reference.row == 1 &&
                    reopened_row_one[2].reference.column == 3 &&
                    reopened_row_one[2].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_one[2].value.text_value() == "row2-gap-c2" &&
                    reopened_row_one[3].reference.row == 1 &&
                    reopened_row_one[3].reference.column == 4 &&
                    reopened_row_one[3].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_one[3].value.text_value() == "#REF!+#REF!" &&
                    reopened_row_one[3].value.has_style() &&
                    reopened_row_one[3].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before delete_rows failed save no-op reopened row_cells should expose shifted sparse order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_four =
                reopened_sheet.column_cells(4);
            check(reopened_column_four.size() == 1 &&
                    reopened_column_four[0].reference.row == 1 &&
                    reopened_column_four[0].reference.column == 4 &&
                    reopened_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_four[0].value.text_value() == "#REF!+#REF!" &&
                    reopened_column_four[0].value.has_style() &&
                    reopened_column_four[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before delete_rows failed save no-op reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "full-calc before delete_rows failed save no-op reopened output should keep old coordinates absent");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before delete_rows failed save second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before delete_rows failed save second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before delete_rows failed save second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before delete_rows failed save second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before delete_rows failed save second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_second_noop,
        "full-calc before delete_rows failed save second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_second_noop,
        "full-calc before delete_rows failed save second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc before delete_rows failed save second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc before delete_rows failed save second no-op save should leave safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc before delete_rows failed save second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(
        second_noop_output,
        "full-calc before delete_rows failed save second no-op save",
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "full-calc before delete_rows failed save second no-op reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "full-calc before delete_rows failed save second no-op reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d1->text_value() == "#REF!+#REF!" &&
                    reopened_d1->has_style() &&
                    reopened_d1->style_id().value() == styled_formula_style.value(),
                "full-calc before delete_rows failed save second no-op reopened output should read shifted #REF formula style");
            check(reopened_sheet.get_cell("A1").text_value() == "placeholder-a2" &&
                    reopened_sheet.get_cell("B1").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("C1").text_value() == "row2-gap-c2" &&
                    reopened_sheet.get_cell("A2").text_value() == "extra-c3",
                "full-calc before delete_rows failed save second no-op reopened output should read shifted source rows");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_four =
                reopened_sheet.column_cells(4);
            check(reopened_column_four.size() == 1 &&
                    reopened_column_four[0].reference.row == 1 &&
                    reopened_column_four[0].reference.column == 4 &&
                    reopened_column_four[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_four[0].value.text_value() == "#REF!+#REF!" &&
                    reopened_column_four[0].value.has_style() &&
                    reopened_column_four[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before delete_rows failed save second no-op reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "full-calc before delete_rows failed save second no-op reopened output should keep old coordinates absent");
        });
    check_reopened_untouched_keep_me_output(
        noop_output, "full-calc before delete_rows failed save no-op Untouched");
    check_reopened_untouched_keep_me_output(
        second_noop_output,
        "full-calc before delete_rows failed save second no-op Untouched");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc before delete_rows failed save source after no-op");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc before delete_rows failed save source after second no-op");
}

void test_public_worksheet_editor_full_calculation_preserves_delete_columns_ref_shift()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-delete-columns-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-delete-columns-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-delete-columns-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-delete-columns-second-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.delete_columns(1, 1);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(dirty_cell_count == 4,
        "full-calc delete_columns setup should keep shifted sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "full-calc delete_columns setup should expose shifted bounds");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("C2");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+A1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "full-calc delete_columns setup should translate deleted references and preserve style id");

    editor.request_full_calculation();

    check(!editor.last_edit_error().has_value(),
        "request_full_calculation after delete_columns should clear diagnostics");
    check(editor.pending_change_count() == 1,
        "request_full_calculation after delete_columns should add one metadata edit");
    check(sheet.has_pending_changes(),
        "request_full_calculation after delete_columns should keep the shifted sheet dirty");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "request_full_calculation after delete_columns should preserve dirty materialized names");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "request_full_calculation after delete_columns should preserve dirty sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "request_full_calculation after delete_columns should preserve dirty sparse memory");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 1, "request_full_calculation after delete_columns dirty summary");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc delete_columns save_as should clean the shifted materialized sheet");
    check(editor.pending_change_count() == 2,
        "full-calc delete_columns save_as should count metadata edit plus materialized flush");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc delete_columns save_as should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc delete_columns save_as should clear dirty materialized count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc delete_columns save_as should clear dirty materialized memory");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc delete_columns save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc delete_columns save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc delete_columns save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "full-calc delete_columns save_as should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "full-calc delete_columns save_as should write shifted source number");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "full-calc delete_columns save_as should write shifted source text");
    check_contains(worksheet_xml, R"(<c r="B2")",
        "full-calc delete_columns save_as should write shifted source column");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc delete_columns save_as should write shifted #REF! formula with style id");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc delete_columns save_as should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "full-calc delete_columns save_as should omit deleted trailing coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc delete_columns should preserve untouched worksheets");

    const auto inspect_full_calc_delete_columns_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "full-calc delete_columns reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                "full-calc delete_columns reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "full-calc delete_columns reopened output should read shifted source number");
            const std::optional<fastxlsx::CellValue> reopened_c2 =
                reopened_sheet.try_cell("C2");
            check(reopened_c2.has_value() &&
                    reopened_c2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c2->text_value() == "#REF!+A1" &&
                    reopened_c2->has_style() &&
                    reopened_c2->style_id().value() == styled_formula_style.value(),
                "full-calc delete_columns reopened output should read shifted #REF! formula");
            check(reopened_sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("B2").text_value() == "row2-gap-c2",
                "full-calc delete_columns reopened output should read shifted source columns");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
                reopened_sheet.row_cells(2);
            check(reopened_row_two.size() == 3 &&
                    reopened_row_two[0].reference.row == 2 &&
                    reopened_row_two[0].reference.column == 1 &&
                    reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[0].value.text_value() == "row2-gap-b2" &&
                    reopened_row_two[1].reference.row == 2 &&
                    reopened_row_two[1].reference.column == 2 &&
                    reopened_row_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[1].value.text_value() == "row2-gap-c2" &&
                    reopened_row_two[2].reference.row == 2 &&
                    reopened_row_two[2].reference.column == 3 &&
                    reopened_row_two[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_two[2].value.text_value() == "#REF!+A1" &&
                    reopened_row_two[2].value.has_style() &&
                    reopened_row_two[2].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc delete_columns reopened row_cells should expose shifted sparse order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
                reopened_sheet.column_cells(3);
            check(reopened_column_three.size() == 1 &&
                    reopened_column_three[0].reference.row == 2 &&
                    reopened_column_three[0].reference.column == 3 &&
                    reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_three[0].value.text_value() == "#REF!+A1" &&
                    reopened_column_three[0].value.has_style() &&
                    reopened_column_three[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc delete_columns reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "full-calc delete_columns reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(output, "full-calc delete_columns",
        inspect_full_calc_delete_columns_output);
    check_reopened_untouched_keep_me_output(
        output, "full-calc delete_columns Untouched");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc delete_columns no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc delete_columns no-op save should not record another workbook or materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc delete_columns no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc delete_columns no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc delete_columns no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc delete_columns no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "full-calc delete_columns no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "full-calc delete_columns no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc delete_columns no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc delete_columns no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output,
        "full-calc delete_columns no-op save",
        inspect_full_calc_delete_columns_output);
    check_reopened_untouched_keep_me_output(
        noop_output, "full-calc delete_columns no-op Untouched");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc delete_columns second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc delete_columns second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc delete_columns second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc delete_columns second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc delete_columns second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc delete_columns second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "full-calc delete_columns second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "full-calc delete_columns second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc delete_columns second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc delete_columns second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc delete_columns second no-op save should leave materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc delete_columns second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(second_noop_output,
        "full-calc delete_columns second no-op save",
        inspect_full_calc_delete_columns_output);
    check_reopened_untouched_keep_me_output(
        second_noop_output, "full-calc delete_columns second no-op Untouched");
}

void test_public_worksheet_editor_full_calculation_preserves_delete_columns_ref_shift_failed_save_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-delete-columns-failed-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-delete-columns-failed-save-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-delete-columns-failed-save-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-delete-columns-failed-save-second-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.delete_columns(1, 1);
    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(dirty_cell_count == 4,
        "full-calc delete_columns failed save setup should keep shifted sparse count");

    editor.request_full_calculation();

    const auto source_entries_before_failed_save =
        fastxlsx::test::read_zip_entries(source);
    const auto check_dirty_delete_columns_session = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep diagnostics clear");
        check(editor.has_pending_changes(),
            label + " should keep the public editor dirty");
        check(editor.pending_change_count() == 1,
            label + " should keep only the queued workbook metadata edit before materialized handoff");
        check(sheet.has_pending_changes(),
            label + " should keep the shifted worksheet dirty");
        check(sheet.cell_count() == dirty_cell_count,
            label + " should preserve the shifted sparse count");
        check(sheet.estimated_memory_usage() == dirty_memory_usage,
            label + " should preserve the shifted materialized memory estimate");
        check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
            label + " should expose shifted bounds");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"Data"},
            label + " should report Data dirty");
        check(editor.pending_materialized_cell_count() == dirty_cell_count,
            label + " should report the shifted sparse count");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
            label + " should report the shifted sparse memory");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, label + " dirty materialized summary");

        const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("C2");
        check(shifted_formula.has_value() &&
                shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula->text_value() == "#REF!+A1" &&
                shifted_formula->has_style() &&
                shifted_formula->style_id().value() == styled_formula_style.value(),
            label + " should keep translated #REF! formula and style id in memory");
        const fastxlsx::CellValue shifted_number = sheet.get_cell("A1");
        check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
                shifted_number.number_value() == 1.0 &&
                sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                sheet.get_cell("B2").text_value() == "row2-gap-c2",
            label + " should keep shifted source columns in memory");
        check(!sheet.try_cell("D2").has_value() &&
                !sheet.try_cell("A3").has_value(),
            label + " should keep old and deleted coordinates absent");
    };

    check_dirty_delete_columns_session(
        "full-calc delete_columns failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "full-calc delete_columns failed save should reject exact source overwrite");
    check_dirty_delete_columns_session(
        "full-calc delete_columns failed save rejected source overwrite");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc delete_columns failed save should leave source package unchanged");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc delete_columns failed save safe retry should clean the shifted sheet");
    check(editor.has_pending_changes(),
        "full-calc delete_columns failed save safe retry should retain staged public changes");
    check(editor.pending_change_count() == 2,
        "full-calc delete_columns failed save safe retry should count metadata plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc delete_columns failed save safe retry should clear dirty diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc delete_columns failed save safe retry should clear dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc delete_columns failed save safe retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc delete_columns failed save safe retry should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc delete_columns failed save safe retry should leave source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc delete_columns failed save safe retry should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc delete_columns failed save safe retry should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "full-calc delete_columns failed save safe retry should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "full-calc delete_columns failed save safe retry should write shifted source number");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "full-calc delete_columns failed save safe retry should write shifted source text");
    check_contains(worksheet_xml, R"(<c r="B2")",
        "full-calc delete_columns failed save safe retry should write shifted source column");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc delete_columns failed save safe retry should write shifted styled #REF! formula");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc delete_columns failed save safe retry should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "full-calc delete_columns failed save safe retry should omit old trailing coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc delete_columns failed save safe retry should preserve untouched worksheets");

    const auto inspect_full_calc_delete_columns_failed_save_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "full-calc delete_columns failed save reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                "full-calc delete_columns failed save reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "full-calc delete_columns failed save reopened output should read shifted source number");
            const std::optional<fastxlsx::CellValue> reopened_c2 =
                reopened_sheet.try_cell("C2");
            check(reopened_c2.has_value() &&
                    reopened_c2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c2->text_value() == "#REF!+A1" &&
                    reopened_c2->has_style() &&
                    reopened_c2->style_id().value() == styled_formula_style.value(),
                "full-calc delete_columns failed save reopened output should read shifted #REF formula style");
            check(reopened_sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("B2").text_value() == "row2-gap-c2",
                "full-calc delete_columns failed save reopened output should read shifted source columns");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
                reopened_sheet.row_cells(2);
            check(reopened_row_two.size() == 3 &&
                    reopened_row_two[0].reference.row == 2 &&
                    reopened_row_two[0].reference.column == 1 &&
                    reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[0].value.text_value() == "row2-gap-b2" &&
                    reopened_row_two[1].reference.row == 2 &&
                    reopened_row_two[1].reference.column == 2 &&
                    reopened_row_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[1].value.text_value() == "row2-gap-c2" &&
                    reopened_row_two[2].reference.row == 2 &&
                    reopened_row_two[2].reference.column == 3 &&
                    reopened_row_two[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_two[2].value.text_value() == "#REF!+A1" &&
                    reopened_row_two[2].value.has_style() &&
                    reopened_row_two[2].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc delete_columns failed save reopened row_cells should expose shifted sparse order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
                reopened_sheet.column_cells(3);
            check(reopened_column_three.size() == 1 &&
                    reopened_column_three[0].reference.row == 2 &&
                    reopened_column_three[0].reference.column == 3 &&
                    reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_three[0].value.text_value() == "#REF!+A1" &&
                    reopened_column_three[0].value.has_style() &&
                    reopened_column_three[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc delete_columns failed save reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "full-calc delete_columns failed save reopened output should keep old coordinates absent");
        };

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc delete_columns failed save no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc delete_columns failed save no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc delete_columns failed save no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc delete_columns failed save no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc delete_columns failed save no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "full-calc delete_columns failed save no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "full-calc delete_columns failed save no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc delete_columns failed save no-op output should match safe retry output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc delete_columns failed save no-op save should leave safe retry output unchanged");
    check_reopened_shift_output(
        noop_output,
        "full-calc delete_columns failed save no-op save",
        inspect_full_calc_delete_columns_failed_save_output);
    check_reopened_untouched_keep_me_output(
        noop_output, "full-calc delete_columns failed save no-op Untouched");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc delete_columns failed save source after no-op");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc delete_columns failed save second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc delete_columns failed save second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc delete_columns failed save second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc delete_columns failed save second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc delete_columns failed save second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "full-calc delete_columns failed save second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "full-calc delete_columns failed save second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc delete_columns failed save second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc delete_columns failed save second no-op save should leave safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc delete_columns failed save second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(
        second_noop_output,
        "full-calc delete_columns failed save second no-op save",
        inspect_full_calc_delete_columns_failed_save_output);
    check_reopened_untouched_keep_me_output(
        second_noop_output, "full-calc delete_columns failed save second no-op Untouched");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc delete_columns failed save source after second no-op");
}

void test_public_worksheet_editor_delete_columns_preserves_shifted_source_formula_style()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-delete-columns-styled-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-styled-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-styled-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-delete-columns-styled-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-styled-post-noop-output.xlsx");
    const std::filesystem::path post_noop_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-styled-post-noop-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.delete_columns(1, 1);

    check(sheet.cell_count() == 4,
        "delete_columns styled source formula should remove deleted-column records");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "delete_columns styled source formula should expose shifted bounds");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("A1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "delete_columns styled source formula should shift B1 to A1");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("C2");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+A1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "delete_columns styled source formula should translate deleted references and preserve style id");
    check(sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
            sheet.get_cell("B2").text_value() == "row2-gap-c2",
        "delete_columns styled source formula should shift remaining source columns");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A3").has_value(),
        "delete_columns styled source formula should keep deleted and old coordinates absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_two = sheet.row_cells(2);
    check(shifted_row_two.size() == 3,
        "delete_columns styled source formula row_cells should expose shifted row two");
    check(shifted_row_two[2].reference.row == 2 &&
            shifted_row_two[2].reference.column == 3 &&
            shifted_row_two[2].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_row_two[2].value.text_value() == "#REF!+A1" &&
            shifted_row_two[2].value.has_style() &&
            shifted_row_two[2].value.style_id().value() == styled_formula_style.value(),
        "delete_columns styled source formula row_cells should keep shifted formula style id");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_three =
        sheet.column_cells(3);
    check(shifted_column_three.size() == 1,
        "delete_columns styled source formula column_cells should expose shifted formula column");
    check(shifted_column_three[0].reference.row == 2 &&
            shifted_column_three[0].reference.column == 3 &&
            shifted_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
            shifted_column_three[0].value.text_value() == "#REF!+A1" &&
            shifted_column_three[0].value.has_style() &&
            shifted_column_three[0].value.style_id().value() == styled_formula_style.value(),
        "delete_columns styled source formula column_cells should keep shifted formula style id");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "delete_columns styled source formula should report Data as dirty");
    check(editor.pending_materialized_cell_count() == 4,
        "delete_columns styled source formula should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "delete_columns styled source formula should report shifted sparse memory");
    check(!editor.last_edit_error().has_value(),
        "delete_columns styled source formula should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns styled source formula save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "delete_columns styled source formula save_as should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "delete_columns styled source formula save_as should write shifted B1");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "delete_columns styled source formula save_as should write shifted B2");
    check_contains(worksheet_xml, R"(<c r="B2")",
        "delete_columns styled source formula save_as should write shifted C2");
    check_contains(worksheet_xml, styled_formula_xml,
        "delete_columns styled source formula save_as should write shifted formula with style id");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "delete_columns styled source formula save_as should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "delete_columns styled source formula save_as should omit deleted trailing coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "delete_columns styled source formula should preserve untouched worksheets");
    const auto inspect_delete_columns_styled_formula_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "delete_columns styled source formula reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                "delete_columns styled source formula reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "delete_columns styled source formula reopened output should read shifted B1");
            const std::optional<fastxlsx::CellValue> reopened_c2 =
                reopened_sheet.try_cell("C2");
            check(reopened_c2.has_value() &&
                    reopened_c2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c2->text_value() == "#REF!+A1" &&
                    reopened_c2->has_style() &&
                    reopened_c2->style_id().value() == styled_formula_style.value(),
                "delete_columns styled source formula reopened output should read styled formula");
            check(reopened_sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("B2").text_value() == "row2-gap-c2",
                "delete_columns styled source formula reopened output should read shifted source columns");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "delete_columns styled source formula reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(output, "delete_columns styled source formula",
        inspect_delete_columns_styled_formula_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "delete_columns styled source formula no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "delete_columns styled source formula no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "delete_columns styled source formula no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "delete_columns styled source formula no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_columns styled source formula no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_columns styled source formula no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "delete_columns styled source formula no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "delete_columns styled source formula no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "delete_columns styled source formula no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns styled source formula no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output,
        "delete_columns styled source formula no-op save",
        inspect_delete_columns_styled_formula_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "delete_columns styled source formula second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 1,
        "delete_columns styled source formula second no-op save should not record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "delete_columns styled source formula second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "delete_columns styled source formula second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_columns styled source formula second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_columns styled source formula second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_second_noop,
        "delete_columns styled source formula second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_second_noop,
        "delete_columns styled source formula second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "delete_columns styled source formula second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns styled source formula second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_columns styled source formula second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(second_noop_output,
        "delete_columns styled source formula second no-op save",
        inspect_delete_columns_styled_formula_output);

    sheet.set_cell("D1", fastxlsx::CellValue::text("post-noop-delete-columns-styled"));
    check(sheet.has_pending_changes(),
        "delete_columns styled source formula post-noop edit should dirty the saved handle");
    check(sheet.cell_count() == 5,
        "delete_columns styled source formula post-noop edit should add one sparse cell");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 4,
        "delete_columns styled source formula post-noop edit should expand bounds to D1");
    const std::optional<fastxlsx::CellValue> retained_formula = sheet.try_cell("C2");
    check(retained_formula.has_value() &&
            retained_formula->kind() == fastxlsx::CellValueKind::Formula &&
            retained_formula->text_value() == "#REF!+A1" &&
            retained_formula->has_style() &&
            retained_formula->style_id().value() == styled_formula_style.value(),
        "delete_columns styled source formula post-noop edit should preserve shifted formula style id");
    check(editor.pending_change_count() == 1,
        "delete_columns styled source formula post-noop edit should retain the prior handoff before save");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 5 &&
            editor.estimated_pending_materialized_memory_usage() == sheet.estimated_memory_usage(),
        "delete_columns styled source formula post-noop edit should report the dirty materialized sheet");

    editor.save_as(post_noop_output);
    check(!sheet.has_pending_changes(),
        "delete_columns styled source formula post-noop save should clean the materialized handle");
    check(editor.pending_change_count() == 2,
        "delete_columns styled source formula post-noop save should record another materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_columns styled source formula post-noop save should clear dirty diagnostics");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_columns styled source formula post-noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_columns styled source formula post-noop save should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "delete_columns styled source formula post-noop save should leave the first output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_columns styled source formula post-noop save should leave the no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(second_noop_output) == second_noop_entries,
        "delete_columns styled source formula post-noop save should leave the second no-op output unchanged");

    const auto post_noop_entries = fastxlsx::test::read_zip_entries(post_noop_output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns styled source formula post-noop save should leave the source package unchanged");
    const std::string post_noop_xml = post_noop_entries.at("xl/worksheets/sheet1.xml");
    check_contains(post_noop_xml, styled_formula_xml,
        "delete_columns styled source formula post-noop save should keep the styled formula cell");
    check_contains(post_noop_xml, R"(<c r="D1")",
        "delete_columns styled source formula post-noop save should write the post-noop edit");
    const auto inspect_delete_columns_styled_formula_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "delete_columns styled source formula post-noop reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "delete_columns styled source formula post-noop reopened output should expose expanded bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "delete_columns styled source formula post-noop reopened output should keep shifted source number");
            const std::optional<fastxlsx::CellValue> reopened_c2 =
                reopened_sheet.try_cell("C2");
            check(reopened_c2.has_value() &&
                    reopened_c2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c2->text_value() == "#REF!+A1" &&
                    reopened_c2->has_style() &&
                    reopened_c2->style_id().value() == styled_formula_style.value(),
                "delete_columns styled source formula post-noop reopened output should keep styled formula");
            const std::optional<fastxlsx::CellValue> reopened_d1 =
                reopened_sheet.try_cell("D1");
            check(reopened_d1.has_value() &&
                    reopened_d1->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_d1->text_value() == "post-noop-delete-columns-styled",
                "delete_columns styled source formula post-noop reopened output should read post-noop edit");
            check(reopened_sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("B2").text_value() == "row2-gap-c2",
                "delete_columns styled source formula post-noop reopened output should keep shifted source columns");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "delete_columns styled source formula post-noop reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(post_noop_output,
        "delete_columns styled source formula post-noop save",
        inspect_delete_columns_styled_formula_post_noop_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_post_noop_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_post_noop_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(post_noop_noop_output);
    check(!sheet.has_pending_changes(),
        "delete_columns styled source formula post-noop noop save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "delete_columns styled source formula post-noop noop save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "delete_columns styled source formula post-noop noop save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "delete_columns styled source formula post-noop noop save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "delete_columns styled source formula post-noop noop save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_post_noop_noop,
        "delete_columns styled source formula post-noop noop save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_post_noop_noop,
        "delete_columns styled source formula post-noop noop save");
    const auto post_noop_noop_entries =
        fastxlsx::test::read_zip_entries(post_noop_noop_output);
    check(post_noop_noop_entries == post_noop_entries,
        "delete_columns styled source formula post-noop noop output should match post-noop output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns styled source formula post-noop noop save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(post_noop_output) == post_noop_entries,
        "delete_columns styled source formula post-noop noop save should leave prior post-noop output unchanged");
    check_reopened_shift_output(post_noop_noop_output,
        "delete_columns styled source formula post-noop noop save",
        inspect_delete_columns_styled_formula_post_noop_output);
}

void test_public_worksheet_editor_delete_columns_preserves_shifted_value_only_style()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-delete-columns-value-only-style-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-value-only-style-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-value-only-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-value-only-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-value-only-style-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell_value("D2", fastxlsx::CellValue::text("delete-column-value-only-styled"));
    const std::optional<fastxlsx::CellValue> value_only_d2 = sheet.try_cell("D2");
    check(value_only_d2.has_value() &&
            value_only_d2->kind() == fastxlsx::CellValueKind::Text &&
            value_only_d2->text_value() == "delete-column-value-only-styled" &&
            value_only_d2->has_style() &&
            value_only_d2->style_id().value() == styled_formula_style.value(),
        "set_cell_value should preserve the source style before delete_columns shifts it");

    sheet.delete_columns(1, 1);

    check(sheet.cell_count() == 4,
        "delete_columns should keep value-only shifted sparse count after deleting source column A");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "delete_columns should refresh bounds for shifted value-only styled cells");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("A1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "delete_columns value-only style should shift B1 to A1");
    const std::optional<fastxlsx::CellValue> shifted_value = sheet.try_cell("C2");
    check(shifted_value.has_value() &&
            shifted_value->kind() == fastxlsx::CellValueKind::Text &&
            shifted_value->text_value() == "delete-column-value-only-styled" &&
            shifted_value->has_style() &&
            shifted_value->style_id().value() == styled_formula_style.value(),
        "delete_columns should move value-only cells with the preserved source style id");
    check(sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
            sheet.get_cell("B2").text_value() == "row2-gap-c2",
        "delete_columns value-only style should shift remaining source columns");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A3").has_value(),
        "delete_columns value-only style should keep deleted and old coordinates absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_two = sheet.row_cells(2);
    check(shifted_row_two.size() == 3,
        "delete_columns value-only row_cells should expose shifted row two");
    check(shifted_row_two[2].reference.row == 2 &&
            shifted_row_two[2].reference.column == 3 &&
            shifted_row_two[2].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_row_two[2].value.text_value() == "delete-column-value-only-styled" &&
            shifted_row_two[2].value.has_style() &&
            shifted_row_two[2].value.style_id().value() == styled_formula_style.value(),
        "delete_columns value-only row_cells should keep the shifted style id");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_three =
        sheet.column_cells(3);
    check(shifted_column_three.size() == 1 &&
            shifted_column_three[0].reference.row == 2 &&
            shifted_column_three[0].reference.column == 3 &&
            shifted_column_three[0].value.kind() == fastxlsx::CellValueKind::Text &&
            shifted_column_three[0].value.text_value() == "delete-column-value-only-styled" &&
            shifted_column_three[0].value.has_style() &&
            shifted_column_three[0].value.style_id().value() == styled_formula_style.value(),
        "delete_columns value-only column_cells should keep the shifted style id");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "delete_columns value-only style pre-save shift summary");
    check(editor.pending_materialized_cell_count() == 4,
        "delete_columns value-only style should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "delete_columns value-only style should report shifted sparse memory");
    check(!editor.last_edit_error().has_value(),
        "successful delete_columns value-only style shift should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns value-only style save_as should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "delete_columns value-only style save_as should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_text =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"(" t="inlineStr"><is><t>delete-column-value-only-styled</t></is></c>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "delete_columns value-only style save_as should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "delete_columns value-only style save_as should write shifted B1");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "delete_columns value-only style save_as should write shifted B2");
    check_contains(worksheet_xml, R"(<c r="B2")",
        "delete_columns value-only style save_as should write shifted C2");
    check_contains(worksheet_xml, styled_text,
        "delete_columns value-only style save_as should write shifted text with source style id");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "delete_columns value-only style save_as should omit the old coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "delete_columns value-only style save_as should omit the deleted trailing coordinate");
    check_not_contains(worksheet_xml, R"(<f>A1+B1</f>)",
        "delete_columns value-only style save_as should omit the replaced source formula");
    check_not_contains(worksheet_xml, R"(<f>#REF!+A1</f>)",
        "delete_columns value-only style save_as should not resurrect the shifted formula");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "delete_columns value-only style should preserve untouched worksheets");

    const auto inspect_delete_columns_value_only_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "delete_columns value-only style reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                "delete_columns value-only style reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "delete_columns value-only style reopened output should read shifted B1");
            const std::optional<fastxlsx::CellValue> reopened_c2 =
                reopened_sheet.try_cell("C2");
            check(reopened_c2.has_value() &&
                    reopened_c2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c2->text_value() == "delete-column-value-only-styled" &&
                    reopened_c2->has_style() &&
                    reopened_c2->style_id().value() == styled_formula_style.value(),
                "delete_columns value-only style reopened output should preserve shifted source style");
            check(reopened_sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("B2").text_value() == "row2-gap-c2",
                "delete_columns value-only style reopened output should read shifted source columns");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "delete_columns value-only style reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(output, "delete_columns value-only style",
        inspect_delete_columns_value_only_output);
    check_reopened_untouched_keep_me_output(output, "delete_columns value-only style");
    const auto noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        "delete_columns value-only style no-op save",
        inspect_delete_columns_value_only_output);
    const auto second_noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        "delete_columns value-only style second no-op save",
        inspect_delete_columns_value_only_output);
    check(second_noop_entries == noop_entries,
        "delete_columns value-only style second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_columns value-only style second no-op save should leave first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "delete_columns value-only style second no-op save should leave the materialized output unchanged");
    const auto inspect_delete_columns_value_only_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "delete_columns value-only style post-noop output should include the later edit");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "delete_columns value-only style post-noop output should expand to the later edit");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "delete_columns value-only style post-noop output should read shifted B1");
            const std::optional<fastxlsx::CellValue> reopened_c2 =
                reopened_sheet.try_cell("C2");
            check(reopened_c2.has_value() &&
                    reopened_c2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_c2->text_value() == "delete-column-value-only-styled" &&
                    reopened_c2->has_style() &&
                    reopened_c2->style_id().value() == styled_formula_style.value(),
                "delete_columns value-only style post-noop output should preserve shifted source style");
            const std::optional<fastxlsx::CellValue> reopened_d2 =
                reopened_sheet.try_cell("D2");
            check(reopened_d2.has_value() &&
                    reopened_d2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_d2->text_value() == "post-noop-delete-columns-value-only-style",
                "delete_columns value-only style post-noop output should include the later edit");
            check(reopened_sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("B2").text_value() == "row2-gap-c2",
                "delete_columns value-only style post-noop output should read shifted source columns");
            check(!reopened_sheet.try_cell("A3").has_value(),
                "delete_columns value-only style post-noop output should keep deleted trailing coordinate absent");
        };
    check_shift_post_noop_edit_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        second_noop_entries,
        post_noop_output,
        "delete_columns value-only style post-noop edit save",
        5,
        [&sheet]() {
            sheet.set_cell("D2",
                fastxlsx::CellValue::text("post-noop-delete-columns-value-only-style"));
        },
        inspect_delete_columns_value_only_post_noop_output);
}

void test_public_worksheet_editor_delete_columns_preserves_shifted_clear_value_style()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-delete-columns-clear-value-style-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-clear-value-style-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-clear-value-style-noop-output.xlsx");
    const std::filesystem::path second_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-clear-value-style-second-noop-output.xlsx");
    const std::filesystem::path post_noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-delete-columns-clear-value-style-post-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.clear_cell_value("D2");
    const std::optional<fastxlsx::CellValue> cleared_d2 = sheet.try_cell("D2");
    check(cleared_d2.has_value() &&
            cleared_d2->kind() == fastxlsx::CellValueKind::Blank &&
            cleared_d2->has_style() &&
            cleared_d2->style_id().value() == styled_formula_style.value(),
        "clear_cell_value should preserve the source style before delete_columns shifts it");

    sheet.delete_columns(1, 1);

    check(sheet.cell_count() == 4,
        "delete_columns should keep cleared shifted sparse count after deleting source column A");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "delete_columns should refresh bounds for shifted cleared styled cells");
    const fastxlsx::CellValue shifted_number = sheet.get_cell("A1");
    check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
            shifted_number.number_value() == 1.0,
        "delete_columns cleared style should shift B1 to A1");
    const std::optional<fastxlsx::CellValue> shifted_blank = sheet.try_cell("C2");
    check(shifted_blank.has_value() &&
            shifted_blank->kind() == fastxlsx::CellValueKind::Blank &&
            shifted_blank->has_style() &&
            shifted_blank->style_id().value() == styled_formula_style.value(),
        "delete_columns should move cleared cells with the preserved source style id");
    check(sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
            sheet.get_cell("B2").text_value() == "row2-gap-c2",
        "delete_columns cleared style should shift remaining source columns");
    check(!sheet.try_cell("D2").has_value() && !sheet.try_cell("A3").has_value(),
        "delete_columns cleared style should keep deleted and old coordinates absent");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_row_two = sheet.row_cells(2);
    check(shifted_row_two.size() == 3,
        "delete_columns cleared row_cells should expose shifted row two");
    check(shifted_row_two[2].reference.row == 2 &&
            shifted_row_two[2].reference.column == 3 &&
            shifted_row_two[2].value.kind() == fastxlsx::CellValueKind::Blank &&
            shifted_row_two[2].value.has_style() &&
            shifted_row_two[2].value.style_id().value() == styled_formula_style.value(),
        "delete_columns cleared row_cells should keep the shifted style id");
    const std::vector<fastxlsx::WorksheetCellSnapshot> shifted_column_three =
        sheet.column_cells(3);
    check(shifted_column_three.size() == 1 &&
            shifted_column_three[0].reference.row == 2 &&
            shifted_column_three[0].reference.column == 3 &&
            shifted_column_three[0].value.kind() == fastxlsx::CellValueKind::Blank &&
            shifted_column_three[0].value.has_style() &&
            shifted_column_three[0].value.style_id().value() == styled_formula_style.value(),
        "delete_columns cleared column_cells should keep the shifted style id");
    const std::size_t shifted_memory_usage = sheet.estimated_memory_usage();
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 0, "delete_columns cleared style pre-save shift summary");
    check(editor.pending_materialized_cell_count() == 4,
        "delete_columns cleared style should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == shifted_memory_usage,
        "delete_columns cleared style should report shifted sparse memory");
    check(!editor.last_edit_error().has_value(),
        "successful delete_columns cleared style shift should keep diagnostics clear");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "delete_columns cleared style save_as should leave the source package unchanged");
    check(output_entries.at("xl/styles.xml") == source_entries.at("xl/styles.xml"),
        "delete_columns cleared style save_as should preserve source styles.xml bytes");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_blank =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value()) + R"("/>)";
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "delete_columns cleared style save_as should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "delete_columns cleared style save_as should write shifted B1");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "delete_columns cleared style save_as should write shifted B2");
    check_contains(worksheet_xml, R"(<c r="B2")",
        "delete_columns cleared style save_as should write shifted C2");
    check_contains(worksheet_xml, styled_blank,
        "delete_columns cleared style save_as should write shifted blank with source style id");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "delete_columns cleared style save_as should omit the old coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "delete_columns cleared style save_as should omit the deleted trailing coordinate");
    check_not_contains(worksheet_xml, R"(<f>A1+B1</f>)",
        "delete_columns cleared style save_as should omit the cleared source formula");
    check_not_contains(worksheet_xml, R"(<f>#REF!+A1</f>)",
        "delete_columns cleared style save_as should not resurrect the shifted formula");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "delete_columns cleared style should preserve untouched worksheets");

    const auto inspect_delete_columns_cleared_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "delete_columns cleared style reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                "delete_columns cleared style reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "delete_columns cleared style reopened output should read shifted B1");
            const std::optional<fastxlsx::CellValue> reopened_c2 =
                reopened_sheet.try_cell("C2");
            check(reopened_c2.has_value() &&
                    reopened_c2->kind() == fastxlsx::CellValueKind::Blank &&
                    reopened_c2->has_style() &&
                    reopened_c2->style_id().value() == styled_formula_style.value(),
                "delete_columns cleared style reopened output should preserve shifted source style");
            check(reopened_sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("B2").text_value() == "row2-gap-c2",
                "delete_columns cleared style reopened output should read shifted source columns");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "delete_columns cleared style reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(output, "delete_columns cleared style",
        inspect_delete_columns_cleared_output);
    check_reopened_untouched_keep_me_output(output, "delete_columns cleared style");
    const auto noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        "delete_columns cleared style no-op save",
        inspect_delete_columns_cleared_output);
    const auto second_noop_entries = check_clean_shift_noop_save_output(
        editor,
        sheet,
        source,
        source_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        "delete_columns cleared style second no-op save",
        inspect_delete_columns_cleared_output);
    check(second_noop_entries == noop_entries,
        "delete_columns cleared style second no-op output should match first no-op output");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "delete_columns cleared style second no-op save should leave first no-op output unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "delete_columns cleared style second no-op save should leave the materialized output unchanged");
    const auto inspect_delete_columns_cleared_post_noop_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 5,
                "delete_columns cleared style post-noop output should include the later edit");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 4,
                "delete_columns cleared style post-noop output should expand to the later edit");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "delete_columns cleared style post-noop output should read shifted B1");
            const std::optional<fastxlsx::CellValue> reopened_c2 =
                reopened_sheet.try_cell("C2");
            check(reopened_c2.has_value() &&
                    reopened_c2->kind() == fastxlsx::CellValueKind::Blank &&
                    reopened_c2->has_style() &&
                    reopened_c2->style_id().value() == styled_formula_style.value(),
                "delete_columns cleared style post-noop output should preserve shifted source style");
            const std::optional<fastxlsx::CellValue> reopened_d2 =
                reopened_sheet.try_cell("D2");
            check(reopened_d2.has_value() &&
                    reopened_d2->kind() == fastxlsx::CellValueKind::Text &&
                    reopened_d2->text_value() == "post-noop-delete-columns-cleared-style",
                "delete_columns cleared style post-noop output should include the later edit");
            check(reopened_sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("B2").text_value() == "row2-gap-c2",
                "delete_columns cleared style post-noop output should read shifted source columns");
            check(!reopened_sheet.try_cell("A3").has_value(),
                "delete_columns cleared style post-noop output should keep deleted trailing coordinate absent");
        };
    check_shift_post_noop_edit_save_output(
        editor,
        sheet,
        source,
        source_entries,
        output,
        output_entries,
        noop_output,
        noop_entries,
        second_noop_output,
        second_noop_entries,
        post_noop_output,
        "delete_columns cleared style post-noop edit save",
        5,
        [&sheet]() {
            sheet.set_cell("D2",
                fastxlsx::CellValue::text("post-noop-delete-columns-cleared-style"));
        },
        inspect_delete_columns_cleared_post_noop_output);
}

void test_public_worksheet_editor_full_calculation_before_delete_columns_ref_shift()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-columns-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-columns-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-columns-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-columns-second-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.request_full_calculation();
    check(!editor.last_edit_error().has_value(),
        "full-calc before delete_columns setup should clear diagnostics");
    check(editor.pending_change_count() == 1,
        "full-calc before delete_columns setup should queue one workbook metadata edit");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc before delete_columns setup should not expose dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc before delete_columns setup should not expose dirty materialized cells");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before delete_columns setup should not expose dirty materialized memory");

    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    check(!sheet.has_pending_changes(),
        "worksheet() after full-calc before delete_columns should materialize cleanly");
    check(editor.pending_change_count() == 1,
        "clean materialization after full-calc before delete_columns should keep metadata edit count");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "clean materialization after full-calc before delete_columns should keep dirty diagnostics clear");

    sheet.delete_columns(1, 1);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    check(dirty_cell_count == 4,
        "full-calc before delete_columns should keep shifted sparse count");
    check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
        "full-calc before delete_columns should expose shifted bounds");
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("C2");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == "#REF!+A1" &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "full-calc before delete_columns should translate deleted references and preserve style id");
    check(editor.pending_change_count() == 1,
        "full-calc before delete_columns should not flush materialized state before save_as");
    check(sheet.has_pending_changes(),
        "full-calc before delete_columns should leave the shifted sheet dirty");
    check(editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"},
        "full-calc before delete_columns should report Data dirty");
    check(editor.pending_materialized_cell_count() == dirty_cell_count,
        "full-calc before delete_columns should report shifted sparse count");
    check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
        "full-calc before delete_columns should report shifted sparse memory");
    check_public_state_single_data_dirty_materialized_summary(
        editor, sheet, 1, "full-calc before delete_columns pre-save dirty summary");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc before delete_columns save_as should clean the shifted materialized sheet");
    check(editor.pending_change_count() == 2,
        "full-calc before delete_columns save_as should count metadata edit plus materialized flush");
    check(editor.pending_materialized_worksheet_names().empty(),
        "full-calc before delete_columns save_as should clear dirty materialized names");
    check(editor.pending_materialized_cell_count() == 0,
        "full-calc before delete_columns save_as should clear dirty materialized count");
    check(editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before delete_columns save_as should clear dirty materialized memory");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before delete_columns save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc before delete_columns save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc before delete_columns save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "full-calc before delete_columns save_as should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "full-calc before delete_columns save_as should write shifted source number");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "full-calc before delete_columns save_as should write shifted source text");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc before delete_columns save_as should write shifted #REF! formula with style id");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc before delete_columns save_as should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "full-calc before delete_columns save_as should omit deleted trailing coordinate");

    const auto inspect_full_calc_delete_columns_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "full-calc before delete_columns reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                "full-calc before delete_columns reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "full-calc before delete_columns reopened output should read shifted source number");
            const std::optional<fastxlsx::CellValue> reopened_c2 =
                reopened_sheet.try_cell("C2");
            check(reopened_c2.has_value() &&
                    reopened_c2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c2->text_value() == "#REF!+A1" &&
                    reopened_c2->has_style() &&
                    reopened_c2->style_id().value() == styled_formula_style.value(),
                "full-calc before delete_columns reopened output should read shifted #REF! formula");
            check(reopened_sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("B2").text_value() == "row2-gap-c2",
                "full-calc before delete_columns reopened output should read shifted source columns");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
                reopened_sheet.row_cells(2);
            check(reopened_row_two.size() == 3 &&
                    reopened_row_two[0].reference.row == 2 &&
                    reopened_row_two[0].reference.column == 1 &&
                    reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[0].value.text_value() == "row2-gap-b2" &&
                    reopened_row_two[1].reference.row == 2 &&
                    reopened_row_two[1].reference.column == 2 &&
                    reopened_row_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[1].value.text_value() == "row2-gap-c2" &&
                    reopened_row_two[2].reference.row == 2 &&
                    reopened_row_two[2].reference.column == 3 &&
                    reopened_row_two[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_two[2].value.text_value() == "#REF!+A1" &&
                    reopened_row_two[2].value.has_style() &&
                    reopened_row_two[2].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before delete_columns reopened row_cells should expose shifted sparse order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
                reopened_sheet.column_cells(3);
            check(reopened_column_three.size() == 1 &&
                    reopened_column_three[0].reference.row == 2 &&
                    reopened_column_three[0].reference.column == 3 &&
                    reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_three[0].value.text_value() == "#REF!+A1" &&
                    reopened_column_three[0].value.has_style() &&
                    reopened_column_three[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before delete_columns reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "full-calc before delete_columns reopened output should keep old coordinates absent");
        };
    check_reopened_shift_output(output, "full-calc before delete_columns",
        inspect_full_calc_delete_columns_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before delete_columns no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before delete_columns no-op save should not record another workbook or materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before delete_columns no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc before delete_columns no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc before delete_columns no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before delete_columns no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "full-calc before delete_columns no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "full-calc before delete_columns no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc before delete_columns no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before delete_columns no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "full-calc before delete_columns no-op save",
        inspect_full_calc_delete_columns_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before delete_columns second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before delete_columns second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before delete_columns second no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc before delete_columns second no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc before delete_columns second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before delete_columns second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_second_noop,
        "full-calc before delete_columns second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_second_noop,
        "full-calc before delete_columns second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc before delete_columns second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc before delete_columns second no-op save should leave the source package unchanged");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc before delete_columns second no-op save should leave materialized output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc before delete_columns second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(second_noop_output,
        "full-calc before delete_columns second no-op save",
        inspect_full_calc_delete_columns_output);
}

void test_public_worksheet_editor_full_calculation_before_delete_columns_ref_shift_failed_save_preserves_state()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_styled_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-columns-failed-save-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-columns-failed-save-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-columns-failed-save-noop-output.xlsx");
    const std::filesystem::path second_noop_output = artifact(
        "fastxlsx-workbook-editor-public-worksheet-full-calc-before-delete-columns-failed-save-second-noop-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    editor.request_full_calculation();
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.delete_columns(1, 1);

    const std::size_t dirty_cell_count = sheet.cell_count();
    const std::size_t dirty_memory_usage = sheet.estimated_memory_usage();
    const auto source_entries_before_failed_save =
        fastxlsx::test::read_zip_entries(source);
    check(dirty_cell_count == 4,
        "full-calc before delete_columns failed save setup should keep shifted sparse count");

    const auto check_dirty_delete_columns_session = [&](std::string_view scenario) {
        const std::string label = std::string(scenario);

        check(!editor.last_edit_error().has_value(),
            label + " should keep diagnostics clear");
        check(editor.has_pending_changes(),
            label + " should keep the public editor dirty");
        check(editor.pending_change_count() == 1,
            label + " should keep only the queued workbook metadata edit before materialized handoff");
        check(sheet.has_pending_changes(),
            label + " should keep the shifted worksheet dirty");
        check(sheet.cell_count() == dirty_cell_count,
            label + " should preserve the shifted sparse count");
        check(sheet.estimated_memory_usage() == dirty_memory_usage,
            label + " should preserve the shifted materialized memory estimate");
        check_cell_range_equals(sheet.used_range(), 1, 1, 2, 3,
            label + " should expose shifted bounds");
        check(editor.pending_materialized_worksheet_names()
                  == std::vector<std::string>{"Data"},
            label + " should report Data dirty");
        check(editor.pending_materialized_cell_count() == dirty_cell_count,
            label + " should report the shifted sparse count");
        check(editor.estimated_pending_materialized_memory_usage() == dirty_memory_usage,
            label + " should report the shifted sparse memory");
        check_public_state_single_data_dirty_materialized_summary(
            editor, sheet, 1, label + " dirty materialized summary");

        const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("C2");
        check(shifted_formula.has_value() &&
                shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
                shifted_formula->text_value() == "#REF!+A1" &&
                shifted_formula->has_style() &&
                shifted_formula->style_id().value() == styled_formula_style.value(),
            label + " should keep translated #REF! formula and style id in memory");
        const fastxlsx::CellValue shifted_number = sheet.get_cell("A1");
        check(shifted_number.kind() == fastxlsx::CellValueKind::Number &&
                shifted_number.number_value() == 1.0 &&
                sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                sheet.get_cell("B2").text_value() == "row2-gap-c2",
            label + " should keep shifted source columns in memory");
        check(!sheet.try_cell("D2").has_value() &&
                !sheet.try_cell("A3").has_value(),
            label + " should keep old and deleted coordinates absent");
    };

    check_dirty_delete_columns_session(
        "full-calc before delete_columns failed save dirty state before rejected source overwrite");
    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "full-calc before delete_columns failed save should reject exact source overwrite");
    check_dirty_delete_columns_session(
        "full-calc before delete_columns failed save rejected source overwrite");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc before delete_columns failed save should leave source package unchanged");

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc before delete_columns failed save safe retry should clean the shifted sheet");
    check(editor.has_pending_changes(),
        "full-calc before delete_columns failed save safe retry should retain staged public changes");
    check(editor.pending_change_count() == 2,
        "full-calc before delete_columns failed save safe retry should count metadata plus materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc before delete_columns failed save safe retry should clear dirty diagnostics");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc before delete_columns failed save safe retry should clear dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before delete_columns failed save safe retry should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before delete_columns failed save safe retry should keep diagnostics clear");
    check(fastxlsx::test::read_zip_entries(source) == source_entries_before_failed_save,
        "full-calc before delete_columns failed save safe retry should leave source package unchanged");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="C2" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>#REF!+A1</f></c>)";
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc before delete_columns failed save safe retry should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc before delete_columns failed save safe retry should not invent calcChain.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "full-calc before delete_columns failed save safe retry should project shifted bounds");
    check_contains(worksheet_xml, R"(<c r="A1"><v>1</v></c>)",
        "full-calc before delete_columns failed save safe retry should write shifted source number");
    check_contains(worksheet_xml, R"(<c r="A2")",
        "full-calc before delete_columns failed save safe retry should write shifted source text");
    check_contains(worksheet_xml, R"(<c r="B2")",
        "full-calc before delete_columns failed save safe retry should write shifted source column");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc before delete_columns failed save safe retry should write shifted styled #REF! formula");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc before delete_columns failed save safe retry should omit old formula coordinate");
    check_not_contains(worksheet_xml, R"(r="A3")",
        "full-calc before delete_columns failed save safe retry should omit old trailing coordinate");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "full-calc before delete_columns failed save safe retry should preserve untouched worksheets");

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before delete_columns failed save no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before delete_columns failed save no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before delete_columns failed save no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc before delete_columns failed save no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before delete_columns failed save no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "full-calc before delete_columns failed save no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "full-calc before delete_columns failed save no-op save");
    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == output_entries,
        "full-calc before delete_columns failed save no-op output should match safe retry output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc before delete_columns failed save no-op save should leave safe retry output unchanged");
    check_reopened_shift_output(
        noop_output,
        "full-calc before delete_columns failed save no-op save",
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "full-calc before delete_columns failed save no-op reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                "full-calc before delete_columns failed save no-op reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "full-calc before delete_columns failed save no-op reopened output should read shifted source number");
            const std::optional<fastxlsx::CellValue> reopened_c2 = reopened_sheet.try_cell("C2");
            check(reopened_c2.has_value() &&
                    reopened_c2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c2->text_value() == "#REF!+A1" &&
                    reopened_c2->has_style() &&
                    reopened_c2->style_id().value() == styled_formula_style.value(),
                "full-calc before delete_columns failed save no-op reopened output should read shifted #REF formula style");
            check(reopened_sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("B2").text_value() == "row2-gap-c2",
                "full-calc before delete_columns failed save no-op reopened output should read shifted source columns");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_row_two =
                reopened_sheet.row_cells(2);
            check(reopened_row_two.size() == 3 &&
                    reopened_row_two[0].reference.row == 2 &&
                    reopened_row_two[0].reference.column == 1 &&
                    reopened_row_two[0].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[0].value.text_value() == "row2-gap-b2" &&
                    reopened_row_two[1].reference.row == 2 &&
                    reopened_row_two[1].reference.column == 2 &&
                    reopened_row_two[1].value.kind() == fastxlsx::CellValueKind::Text &&
                    reopened_row_two[1].value.text_value() == "row2-gap-c2" &&
                    reopened_row_two[2].reference.row == 2 &&
                    reopened_row_two[2].reference.column == 3 &&
                    reopened_row_two[2].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_row_two[2].value.text_value() == "#REF!+A1" &&
                    reopened_row_two[2].value.has_style() &&
                    reopened_row_two[2].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before delete_columns failed save no-op reopened row_cells should expose shifted sparse order");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
                reopened_sheet.column_cells(3);
            check(reopened_column_three.size() == 1 &&
                    reopened_column_three[0].reference.row == 2 &&
                    reopened_column_three[0].reference.column == 3 &&
                    reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_three[0].value.text_value() == "#REF!+A1" &&
                    reopened_column_three[0].value.has_style() &&
                    reopened_column_three[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before delete_columns failed save no-op reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "full-calc before delete_columns failed save no-op reopened output should keep old coordinates absent");
        });

    const WorkbookEditorPublicCatalogSnapshot catalog_before_second_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_second_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(second_noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc before delete_columns failed save second no-op save should keep the materialized handle clean");
    check(editor.pending_change_count() == 2,
        "full-calc before delete_columns failed save second no-op save should not record another handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0 &&
            editor.pending_worksheet_edits().empty(),
        "full-calc before delete_columns failed save second no-op save should keep dirty diagnostics clear");
    check_workbook_editor_no_replacement_diagnostics(
        editor,
        "full-calc before delete_columns failed save second no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc before delete_columns failed save second no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor,
        save_state_before_second_noop,
        "full-calc before delete_columns failed save second no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor,
        catalog_before_second_noop,
        "full-calc before delete_columns failed save second no-op save");
    const auto second_noop_entries =
        fastxlsx::test::read_zip_entries(second_noop_output);
    check(second_noop_entries == noop_entries,
        "full-calc before delete_columns failed save second no-op output should match the first no-op output");
    check(fastxlsx::test::read_zip_entries(output) == output_entries,
        "full-calc before delete_columns failed save second no-op save should leave safe retry output unchanged");
    check(fastxlsx::test::read_zip_entries(noop_output) == noop_entries,
        "full-calc before delete_columns failed save second no-op save should leave the first no-op output unchanged");
    check_reopened_shift_output(
        second_noop_output,
        "full-calc before delete_columns failed save second no-op save",
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 4,
                "full-calc before delete_columns failed save second no-op reopened output should keep sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 2, 3,
                "full-calc before delete_columns failed save second no-op reopened output should expose shifted bounds");
            const fastxlsx::CellValue reopened_a1 = reopened_sheet.get_cell("A1");
            check(reopened_a1.kind() == fastxlsx::CellValueKind::Number &&
                    reopened_a1.number_value() == 1.0,
                "full-calc before delete_columns failed save second no-op reopened output should read shifted source number");
            const std::optional<fastxlsx::CellValue> reopened_c2 =
                reopened_sheet.try_cell("C2");
            check(reopened_c2.has_value() &&
                    reopened_c2->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_c2->text_value() == "#REF!+A1" &&
                    reopened_c2->has_style() &&
                    reopened_c2->style_id().value() == styled_formula_style.value(),
                "full-calc before delete_columns failed save second no-op reopened output should read shifted #REF formula style");
            check(reopened_sheet.get_cell("A2").text_value() == "row2-gap-b2" &&
                    reopened_sheet.get_cell("B2").text_value() == "row2-gap-c2",
                "full-calc before delete_columns failed save second no-op reopened output should read shifted source columns");
            const std::vector<fastxlsx::WorksheetCellSnapshot> reopened_column_three =
                reopened_sheet.column_cells(3);
            check(reopened_column_three.size() == 1 &&
                    reopened_column_three[0].reference.row == 2 &&
                    reopened_column_three[0].reference.column == 3 &&
                    reopened_column_three[0].value.kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_column_three[0].value.text_value() == "#REF!+A1" &&
                    reopened_column_three[0].value.has_style() &&
                    reopened_column_three[0].value.style_id().value() ==
                        styled_formula_style.value(),
                "full-calc before delete_columns failed save second no-op reopened column_cells should expose shifted styled formula");
            check(!reopened_sheet.try_cell("D2").has_value() &&
                    !reopened_sheet.try_cell("A3").has_value(),
                "full-calc before delete_columns failed save second no-op reopened output should keep old coordinates absent");
        });
    check_reopened_untouched_keep_me_output(
        noop_output, "full-calc before delete_columns failed save no-op Untouched");
    check_reopened_untouched_keep_me_output(
        second_noop_output,
        "full-calc before delete_columns failed save second no-op Untouched");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc before delete_columns failed save source after no-op");
    check_reopened_styled_shift_source_output(
        source,
        styled_formula_style,
        "full-calc before delete_columns failed save source after second no-op");
}

void test_public_worksheet_editor_full_calculation_shift_formula_audits_preserve_diagnostics()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-shift-formula-audit-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-shift-formula-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-shift-formula-audit-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    editor.request_full_calculation();
    const std::size_t shifted_memory = sheet.estimated_memory_usage();

    constexpr std::string_view expected_formula = "Data!A1+Data!B1";
    const std::optional<fastxlsx::CellValue> shifted_formula = sheet.try_cell("D3");
    check(shifted_formula.has_value() &&
            shifted_formula->kind() == fastxlsx::CellValueKind::Formula &&
            shifted_formula->text_value() == expected_formula &&
            shifted_formula->has_style() &&
            shifted_formula->style_id().value() == styled_formula_style.value(),
        "full-calc shifted formula audit setup should expose the translated styled formula");
    check(editor.pending_change_count() == 1 &&
            editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 7 &&
            editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "full-calc shifted formula audit setup should keep metadata and materialized diagnostics pending");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "full-calc shifted formula audit setup should expose one dirty materialized summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" &&
                    summaries[0].planned_name == "Data" &&
                    !summaries[0].renamed &&
                    summaries[0].materialized_dirty &&
                    summaries[0].materialized_cell_count == 7 &&
                    summaries[0].estimated_materialized_memory_usage == shifted_memory,
                "full-calc shifted formula audit setup should report shifted materialized memory");
        }
    }

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
        check_public_state_formula_audits_preserve_editor_diagnostics(
            editor, "full-calc shifted formula audit");
    check(audits.size() == 2,
        "full-calc shifted formula audit should report both shifted references");

    const fastxlsx::WorkbookEditorFormulaReferenceAudit* first_audit =
        find_public_state_formula_audit(audits, 3, 4, "Data!A1");
    check(first_audit != nullptr,
        "full-calc shifted formula audit should expose the shifted A reference");
    if (first_audit != nullptr) {
        check(first_audit->formula_sheet_source_name == "Data" &&
                first_audit->formula_sheet_planned_name == "Data" &&
                first_audit->formula_text == expected_formula &&
                first_audit->sheet_qualifier_text == "Data!" &&
                first_audit->reference_text == "A1" &&
                first_audit->referenced_sheet_name == "Data",
            "full-calc shifted formula audit should report shifted A formula tokens");
        check(first_audit->matched_current_workbook_sheet &&
                first_audit->matched_source_sheet_name == "Data" &&
                first_audit->matched_planned_sheet_name == "Data" &&
                !first_audit->references_renamed_source_name &&
                first_audit->references_planned_sheet_name,
            "full-calc shifted formula audit should match the current Data sheet");
    }
    const fastxlsx::WorkbookEditorFormulaReferenceAudit* second_audit =
        find_public_state_formula_audit(audits, 3, 4, "Data!B1");
    check(second_audit != nullptr,
        "full-calc shifted formula audit should expose the shifted B reference");
    if (second_audit != nullptr) {
        check(second_audit->formula_sheet_source_name == "Data" &&
                second_audit->formula_sheet_planned_name == "Data" &&
                second_audit->formula_text == expected_formula &&
                second_audit->sheet_qualifier_text == "Data!" &&
                second_audit->reference_text == "B1" &&
                second_audit->referenced_sheet_name == "Data",
            "full-calc shifted formula audit should report shifted B formula tokens");
        check(second_audit->matched_current_workbook_sheet &&
                second_audit->matched_source_sheet_name == "Data" &&
                second_audit->matched_planned_sheet_name == "Data" &&
                !second_audit->references_renamed_source_name &&
                second_audit->references_planned_sheet_name,
            "full-calc shifted formula audit should keep B reference matched to Data");
    }

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc shifted formula audit save_as should clean the materialized sheet");
    check(editor.pending_change_count() == 2,
        "full-calc shifted formula audit save_as should count metadata edit plus materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc shifted formula audit save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc shifted formula audit save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc shifted formula audit save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc shifted formula audit save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc shifted formula audit save_as should write shifted qualified formula");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc shifted formula audit save_as should omit old formula coordinate");

    const auto inspect_full_calc_shift_formula_audit_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "full-calc shifted formula audit reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 4, 4,
                "full-calc shifted formula audit reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d3 =
                reopened_sheet.try_cell("D3");
            check(reopened_d3.has_value() &&
                    reopened_d3->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d3->text_value() == "Data!A1+Data!B1" &&
                    reopened_d3->has_style() &&
                    reopened_d3->style_id().value() == styled_formula_style.value(),
                "full-calc shifted formula audit reopened output should read shifted formula");
            check(!reopened_sheet.try_cell("D2").has_value(),
                "full-calc shifted formula audit reopened output should keep old formula coordinate absent");
        };
    check_reopened_shift_output(output, "full-calc shifted formula audit",
        inspect_full_calc_shift_formula_audit_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc shifted formula audit no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 2,
        "full-calc shifted formula audit no-op save should not record another workbook or materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc shifted formula audit no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc shifted formula audit no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc shifted formula audit no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc shifted formula audit no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "full-calc shifted formula audit no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "full-calc shifted formula audit no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "full-calc shifted formula audit no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc shifted formula audit no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "full-calc shifted formula audit no-op save",
        inspect_full_calc_shift_formula_audit_output);
}

void test_public_worksheet_editor_full_calculation_source_formula_audits_preserve_source_scan()
{
    fastxlsx::StyleId styled_formula_style;
    const std::filesystem::path source =
        write_two_sheet_source_with_qualified_shift_formula(
            "fastxlsx-workbook-editor-public-worksheet-full-calc-source-formula-audit-source.xlsx",
            styled_formula_style);
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-source-formula-audit-output.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-full-calc-source-formula-audit-noop-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.insert_rows(2, 1);
    editor.request_full_calculation();
    const std::size_t shifted_memory = sheet.estimated_memory_usage();

    constexpr std::string_view shifted_formula = "Data!A1+Data!B1";
    constexpr std::string_view source_formula = "Data!A1+Data!B1";
    const std::optional<fastxlsx::CellValue> materialized_formula =
        sheet.try_cell("D3");
    check(materialized_formula.has_value() &&
            materialized_formula->kind() == fastxlsx::CellValueKind::Formula &&
            materialized_formula->text_value() == shifted_formula &&
            materialized_formula->has_style() &&
            materialized_formula->style_id().value() == styled_formula_style.value(),
        "full-calc source formula audit setup should expose the translated styled formula");
    check(editor.pending_change_count() == 1 &&
            editor.pending_materialized_worksheet_names() == std::vector<std::string>{"Data"} &&
            editor.pending_materialized_cell_count() == 7 &&
            editor.estimated_pending_materialized_memory_usage() == shifted_memory,
        "full-calc source formula audit setup should keep metadata and materialized diagnostics pending");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "full-calc source formula audit setup should expose one dirty materialized summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" &&
                    summaries[0].planned_name == "Data" &&
                    !summaries[0].renamed &&
                    summaries[0].materialized_dirty &&
                    summaries[0].materialized_cell_count == 7 &&
                    summaries[0].estimated_materialized_memory_usage == shifted_memory,
                "full-calc source formula audit setup should report the dirty Data summary");
        }
    }

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> source_audits =
        check_public_state_source_formula_audits_preserve_editor_diagnostics(
            editor, "full-calc source formula audit");
    check(source_audits.size() == 2,
        "full-calc source formula audit should report only the original source formula references");

    const fastxlsx::WorkbookEditorFormulaReferenceAudit* first_audit =
        find_public_state_formula_audit(source_audits, 2, 4, "Data!A1");
    check(first_audit != nullptr,
        "full-calc source formula audit should expose the source A reference");
    if (first_audit != nullptr) {
        check(first_audit->formula_sheet_source_name == "Data" &&
                first_audit->formula_sheet_planned_name == "Data" &&
                first_audit->formula_text == source_formula &&
                first_audit->sheet_qualifier_text == "Data!" &&
                first_audit->reference_text == "A1" &&
                first_audit->referenced_sheet_name == "Data",
            "full-calc source formula audit should report original A formula tokens");
        check(first_audit->matched_current_workbook_sheet &&
                first_audit->matched_source_sheet_name == "Data" &&
                first_audit->matched_planned_sheet_name == "Data" &&
                !first_audit->references_renamed_source_name &&
                first_audit->references_planned_sheet_name,
            "full-calc source formula audit should match source A to the current Data sheet");
    }

    const fastxlsx::WorkbookEditorFormulaReferenceAudit* second_audit =
        find_public_state_formula_audit(source_audits, 2, 4, "Data!B1");
    check(second_audit != nullptr,
        "full-calc source formula audit should expose the source B reference");
    if (second_audit != nullptr) {
        check(second_audit->formula_sheet_source_name == "Data" &&
                second_audit->formula_sheet_planned_name == "Data" &&
                second_audit->formula_text == source_formula &&
                second_audit->sheet_qualifier_text == "Data!" &&
                second_audit->reference_text == "B1" &&
                second_audit->referenced_sheet_name == "Data",
            "full-calc source formula audit should report original B formula tokens");
        check(second_audit->matched_current_workbook_sheet &&
                second_audit->matched_source_sheet_name == "Data" &&
                second_audit->matched_planned_sheet_name == "Data" &&
                !second_audit->references_renamed_source_name &&
                second_audit->references_planned_sheet_name,
            "full-calc source formula audit should match source B to the current Data sheet");
    }

    editor.save_as(output);

    check(!sheet.has_pending_changes(),
        "full-calc source formula audit save_as should clean the materialized sheet");
    check(editor.pending_change_count() == 2,
        "full-calc source formula audit save_as should count metadata edit plus materialized flush");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc source formula audit save_as should clear dirty materialized diagnostics");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc source formula audit save_as should leave the source package unchanged");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string styled_formula_xml =
        std::string(R"(<c r="D3" s=")")
        + std::to_string(styled_formula_style.value())
        + R"("><f>Data!A1+Data!B1</f></c>)";
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "full-calc source formula audit save_as should persist workbook fullCalcOnLoad metadata");
    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "full-calc source formula audit save_as should not invent calcChain.xml");
    check_contains(worksheet_xml, styled_formula_xml,
        "full-calc source formula audit save_as should write shifted qualified formula");
    check_not_contains(worksheet_xml, R"(r="D2")",
        "full-calc source formula audit save_as should omit old formula coordinate");

    const auto inspect_full_calc_source_formula_audit_output =
        [styled_formula_style](fastxlsx::WorksheetEditor& reopened_sheet) {
            check(reopened_sheet.cell_count() == 7,
                "full-calc source formula audit reopened output should keep shifted sparse count");
            check_cell_range_equals(reopened_sheet.used_range(), 1, 1, 4, 4,
                "full-calc source formula audit reopened output should expose shifted bounds");
            const std::optional<fastxlsx::CellValue> reopened_d3 =
                reopened_sheet.try_cell("D3");
            check(reopened_d3.has_value() &&
                    reopened_d3->kind() == fastxlsx::CellValueKind::Formula &&
                    reopened_d3->text_value() == "Data!A1+Data!B1" &&
                    reopened_d3->has_style() &&
                    reopened_d3->style_id().value() == styled_formula_style.value(),
                "full-calc source formula audit reopened output should read shifted formula");
            check(!reopened_sheet.try_cell("D2").has_value(),
                "full-calc source formula audit reopened output should keep old formula coordinate absent");
        };
    check_reopened_shift_output(output, "full-calc source formula audit",
        inspect_full_calc_source_formula_audit_output);

    const WorkbookEditorPublicCatalogSnapshot catalog_before_noop =
        workbook_editor_public_catalog_snapshot(editor);
    const WorkbookEditorPublicSaveStateSnapshot save_state_before_noop =
        workbook_editor_public_save_state_snapshot(editor);
    editor.save_as(noop_output);
    check(!sheet.has_pending_changes(),
        "full-calc source formula audit no-op save should keep the materialized sheet clean");
    check(editor.pending_change_count() == 2,
        "full-calc source formula audit no-op save should not record another workbook or materialized handoff");
    check(editor.pending_materialized_worksheet_names().empty() &&
            editor.pending_materialized_cell_count() == 0 &&
            editor.estimated_pending_materialized_memory_usage() == 0,
        "full-calc source formula audit no-op save should keep dirty diagnostics clear");
    check(editor.pending_worksheet_edits().empty(),
        "full-calc source formula audit no-op save should not leave dirty summaries");
    check_workbook_editor_no_replacement_diagnostics(
        editor, "full-calc source formula audit no-op save should not queue replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "full-calc source formula audit no-op save should keep diagnostics clear");
    check_workbook_editor_public_save_state_preserved(
        editor, save_state_before_noop, "full-calc source formula audit no-op save");
    check_workbook_editor_public_catalog_preserved(
        editor, catalog_before_noop, "full-calc source formula audit no-op save");
    check(fastxlsx::test::read_zip_entries(noop_output) == output_entries,
        "full-calc source formula audit no-op output should match the materialized output");
    check(fastxlsx::test::read_zip_entries(source) == source_entries,
        "full-calc source formula audit no-op save should leave the source package unchanged");
    check_reopened_shift_output(noop_output, "full-calc source formula audit no-op save",
        inspect_full_calc_source_formula_audit_output);
}

} // namespace

int main()
{
    try {
            test_public_worksheet_editor_delete_rows_preserves_shifted_source_formula_style();
            test_public_worksheet_editor_delete_rows_preserves_shifted_value_only_style();
            test_public_worksheet_editor_delete_rows_preserves_shifted_clear_value_style();
            test_public_worksheet_editor_full_calculation_preserves_delete_rows_ref_shift();
            test_public_worksheet_editor_full_calculation_preserves_delete_rows_ref_shift_failed_save_state();
            test_public_worksheet_editor_full_calculation_before_delete_rows_ref_shift();
            test_public_worksheet_editor_full_calculation_before_delete_rows_ref_shift_failed_save_preserves_state();
            test_public_worksheet_editor_delete_columns_preserves_shifted_source_formula_style();
            test_public_worksheet_editor_delete_columns_preserves_shifted_value_only_style();
            test_public_worksheet_editor_delete_columns_preserves_shifted_clear_value_style();
            test_public_worksheet_editor_full_calculation_preserves_delete_columns_ref_shift();
            test_public_worksheet_editor_full_calculation_preserves_delete_columns_ref_shift_failed_save_state();
            test_public_worksheet_editor_full_calculation_before_delete_columns_ref_shift();
            test_public_worksheet_editor_full_calculation_before_delete_columns_ref_shift_failed_save_preserves_state();
            test_public_worksheet_editor_full_calculation_shift_formula_audits_preserve_diagnostics();
            test_public_worksheet_editor_full_calculation_source_formula_audits_preserve_source_scan();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor public-state shift deletion tests passed\n");
    return 0;
}
