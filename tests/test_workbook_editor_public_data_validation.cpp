#include "../src/package_editor.hpp"
#include "test_workbook_editor_facade_common.hpp"

class ScopedWorksheetReplacementStagedHook {
public:
    explicit ScopedWorksheetReplacementStagedHook(
        fastxlsx::detail::PackageEditorWorksheetPartReplacementStagedHook hook)
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_part_replacement_staged_hook(
            hook);
    }

    ~ScopedWorksheetReplacementStagedHook()
    {
        fastxlsx::detail::testing_set_package_editor_worksheet_part_replacement_staged_hook(
            nullptr);
    }

    ScopedWorksheetReplacementStagedHook(const ScopedWorksheetReplacementStagedHook&) = delete;
    ScopedWorksheetReplacementStagedHook& operator=(
        const ScopedWorksheetReplacementStagedHook&) = delete;
};

void fail_after_data_validation_staging()
{
    throw fastxlsx::FastXlsxError("injected data validation staging failure");
}

std::filesystem::path write_source_with_validation_and_hyperlink(std::string_view name)
{
    const std::filesystem::path path = artifact(name);
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    auto data = writer.add_worksheet("Data");
    data.append_row({fastxlsx::CellView::text("source")});
    fastxlsx::DataValidationRule initial;
    initial.type = fastxlsx::DataValidationType::Whole;
    initial.operator_type = fastxlsx::DataValidationOperator::Between;
    initial.formula1 = "1";
    initial.formula2 = "10";
    data.add_data_validation({2, 1, 4, 1}, initial);
    data.add_external_hyperlink(1, 1, "https://example.invalid/source");
    auto untouched = writer.add_worksheet("Untouched");
    untouched.append_row({fastxlsx::CellView::text("keep")});
    writer.close();
    return path;
}

void test_insert_multi_range_validation_and_preserve_unknown_parts()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-data-validation-insert-source.xlsx");
    auto source_entries = fastxlsx::test::read_zip_entries(source);
    source_entries.emplace("custom/opaque.bin", "data validation unknown entry");
    fastxlsx::test::write_stored_zip_entries(source, source_entries);

    fastxlsx::DataValidationRule rule;
    rule.type = fastxlsx::DataValidationType::Custom;
    rule.formula1 = "A2<>\"A&B\"";
    rule.allow_blank = true;
    rule.show_input_message = true;
    rule.show_error_message = true;
    rule.error_style = fastxlsx::DataValidationErrorStyle::Warning;
    rule.error_title = "Bad & \"value\"";
    rule.error = "Use < 10";
    rule.prompt_title = "Choose 'value'";
    rule.prompt = "A & B";

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-data-validation-insert-output.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_data_validation(
        "Data", {{2, 1, 4, 1}, {2, 3, 4, 3}}, rule);
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1 && summaries.front().data_validation_count == 1,
        "data validation edit should expose one pending rule");
    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    const std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<dataValidations count="1"><dataValidation type="custom" allowBlank="1" showInputMessage="1" showErrorMessage="1" errorStyle="warning" errorTitle="Bad &amp; &quot;value&quot;" error="Use &lt; 10" promptTitle="Choose &apos;value&apos;" prompt="A &amp; B" sqref="A2:A4 C2:C4"><formula1>A2&lt;&gt;"A&amp;B"</formula1></dataValidation></dataValidations>)",
        "multi-range data validation should serialize sqref and escaped metadata");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "data validation should not create worksheet relationships");
    check(entries.at("custom/opaque.bin") == "data validation unknown entry",
        "data validation edit should preserve unknown package entries");
}

void test_append_count_order_relationship_and_reopen()
{
    const std::filesystem::path source = write_source_with_validation_and_hyperlink(
        "fastxlsx-workbook-editor-data-validation-existing-source.xlsx");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-data-validation-existing-output.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    fastxlsx::DataValidationRule list;
    list.type = fastxlsx::DataValidationType::List;
    list.formula1 = "\"One,Two\"";
    list.hide_dropdown_arrow = true;
    editor.add_data_validation("Data", {{2, 2, 4, 2}, {6, 2, 8, 2}}, list);

    fastxlsx::DataValidationRule decimal;
    decimal.type = fastxlsx::DataValidationType::Decimal;
    decimal.operator_type = fastxlsx::DataValidationOperator::GreaterThan;
    decimal.formula1 = "0";
    editor.add_data_validation("Data", fastxlsx::CellRange {2, 3, 8, 3}, decimal);

    fastxlsx::DataValidationRule whole;
    whole.type = fastxlsx::DataValidationType::Whole;
    whole.operator_type = fastxlsx::DataValidationOperator::Between;
    whole.formula1 = "1";
    whole.formula2 = "100";
    editor.add_data_validation("Data", fastxlsx::CellRange {2, 4, 8, 4}, whole);
    check(editor.pending_worksheet_edits().front().data_validation_count == 3,
        "three data validation calls should publish three pending rules");
    editor.save_as(output);

    const auto entries = fastxlsx::test::read_zip_entries(output);
    const std::string& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dataValidations count="4">)",
        "existing data validation count should include appended rules");
    check_contains(worksheet_xml,
        R"(<dataValidation type="list" showDropDown="1" sqref="B2:B4 B6:B8"><formula1>"One,Two"</formula1></dataValidation>)",
        "list validation should preserve multi-range sqref and dropdown metadata");
    check_contains(worksheet_xml,
        R"(<dataValidation type="decimal" operator="greaterThan" sqref="C2:C8"><formula1>0</formula1></dataValidation>)",
        "decimal validation should append after the existing rule");
    check_contains(worksheet_xml,
        R"(<dataValidation type="whole" operator="between" sqref="D2:D8"><formula1>1</formula1><formula2>100</formula2></dataValidation>)",
        "between validation should serialize formula1 and formula2");
    check(worksheet_xml.find("<dataValidations") < worksheet_xml.find("<hyperlinks>"),
        "data validations should remain before hyperlinks in worksheet schema order");
    check_contains(entries.at("xl/worksheets/_rels/sheet1.xml.rels"),
        R"(Target="https://example.invalid/source" TargetMode="External")",
        "data validation edit should preserve existing hyperlink relationships");

    const std::filesystem::path reopened_output = artifact(
        "fastxlsx-workbook-editor-data-validation-reopened-output.xlsx");
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    fastxlsx::DataValidationRule custom;
    custom.type = fastxlsx::DataValidationType::Custom;
    custom.formula1 = "D2>0";
    reopened.add_data_validation("Data", fastxlsx::CellRange {2, 4, 8, 4}, custom);
    reopened.save_as(reopened_output);
    check_contains(fastxlsx::test::read_zip_entries(reopened_output)
            .at("xl/worksheets/sheet1.xml"),
        R"(<dataValidations count="5">)",
        "reopened editor should append and increment the validation count");
}

void test_self_closing_missing_count_and_count_mismatch()
{
    const std::filesystem::path self_closing_source = write_two_sheet_source(
        "fastxlsx-workbook-editor-data-validation-self-closing-source.xlsx");
    auto self_closing_entries = fastxlsx::test::read_zip_entries(self_closing_source);
    replace_first_or_throw(self_closing_entries.at("xl/worksheets/sheet1.xml"),
        "</sheetData>", "</sheetData><dataValidations/>");
    fastxlsx::test::write_stored_zip_entries(self_closing_source, self_closing_entries);

    fastxlsx::DataValidationRule list;
    list.type = fastxlsx::DataValidationType::List;
    list.formula1 = "\"Yes,No\"";
    const std::filesystem::path self_closing_output = artifact(
        "fastxlsx-workbook-editor-data-validation-self-closing-output.xlsx");
    fastxlsx::WorkbookEditor self_closing_editor =
        fastxlsx::WorkbookEditor::open(self_closing_source);
    self_closing_editor.add_data_validation(
        "Data", fastxlsx::CellRange {1, 1, 3, 1}, list);
    self_closing_editor.save_as(self_closing_output);
    check_contains(fastxlsx::test::read_zip_entries(self_closing_output)
            .at("xl/worksheets/sheet1.xml"),
        R"(<dataValidations count="1"><dataValidation type="list" sqref="A1:A3">)",
        "self-closing data validation container should expand with count one");

    const std::filesystem::path no_count_source = write_source_with_validation_and_hyperlink(
        "fastxlsx-workbook-editor-data-validation-no-count-source.xlsx");
    auto no_count_entries = fastxlsx::test::read_zip_entries(no_count_source);
    replace_first_or_throw(no_count_entries.at("xl/worksheets/sheet1.xml"),
        R"(<dataValidations count="1">)", "<dataValidations>");
    fastxlsx::test::write_stored_zip_entries(no_count_source, no_count_entries);
    const std::filesystem::path no_count_output = artifact(
        "fastxlsx-workbook-editor-data-validation-no-count-output.xlsx");
    fastxlsx::WorkbookEditor no_count_editor = fastxlsx::WorkbookEditor::open(no_count_source);
    no_count_editor.add_data_validation(
        "Data", fastxlsx::CellRange {2, 2, 4, 2}, list);
    no_count_editor.save_as(no_count_output);
    check_contains(fastxlsx::test::read_zip_entries(no_count_output)
            .at("xl/worksheets/sheet1.xml"),
        R"(<dataValidations count="2">)",
        "missing count should be populated from the direct child count");

    const std::filesystem::path mismatch_source = write_source_with_validation_and_hyperlink(
        "fastxlsx-workbook-editor-data-validation-count-mismatch-source.xlsx");
    auto mismatch_entries = fastxlsx::test::read_zip_entries(mismatch_source);
    replace_first_or_throw(mismatch_entries.at("xl/worksheets/sheet1.xml"),
        R"(<dataValidations count="1">)", R"(<dataValidations count="2">)");
    fastxlsx::test::write_stored_zip_entries(mismatch_source, mismatch_entries);
    fastxlsx::WorkbookEditor mismatch_editor = fastxlsx::WorkbookEditor::open(mismatch_source);
    check(threw_fastxlsx_error([&] {
        mismatch_editor.add_data_validation(
            "Data", fastxlsx::CellRange {2, 2, 4, 2}, list);
    }), "mismatched existing validation count should fail instead of being repaired");
    check(!mismatch_editor.has_pending_changes() && !mismatch_editor.has_unsaved_changes(),
        "count mismatch should fail before public state publication");
}

void test_validation_failure_state_rename_and_retry()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-data-validation-failure-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    fastxlsx::DataValidationRule list;
    list.type = fastxlsx::DataValidationType::List;
    list.formula1 = "\"A,B\"";
    check(threw_fastxlsx_error([&] {
        editor.add_data_validation(
            "Data", fastxlsx::CellRange {0, 1, 2, 1}, list);
    }), "invalid data validation range should be rejected");

    fastxlsx::DataValidationRule missing_formula;
    check(threw_fastxlsx_error([&] {
        editor.add_data_validation(
            "Data", fastxlsx::CellRange {1, 1, 2, 1}, missing_formula);
    }), "data validation formula1 should be required");

    fastxlsx::DataValidationRule missing_formula2;
    missing_formula2.type = fastxlsx::DataValidationType::Whole;
    missing_formula2.operator_type = fastxlsx::DataValidationOperator::Between;
    missing_formula2.formula1 = "1";
    check(threw_fastxlsx_error([&] {
        editor.add_data_validation(
            "Data", fastxlsx::CellRange {1, 1, 2, 1}, missing_formula2);
    }), "between data validation should require formula2");
    check(threw_fastxlsx_error([&] {
        editor.add_data_validation(
            "Data", std::span<const fastxlsx::CellRange> {}, list);
    }), "empty data validation range list should be rejected");
    check(!editor.has_pending_changes() && editor.pending_worksheet_edits().empty(),
        "invalid data validation calls should not publish pending state");

    editor.add_data_validation("Data", fastxlsx::CellRange {1, 1, 2, 1}, list);
    {
        ScopedWorksheetReplacementStagedHook hook(fail_after_data_validation_staging);
        check(threw_fastxlsx_error([&] {
            editor.add_data_validation(
                "Data", fastxlsx::CellRange {1, 2, 2, 2}, list);
        }), "injected data validation staging failure should escape");
    }
    check(editor.pending_worksheet_edits().front().data_validation_count == 1,
        "staging failure should not publish a second validation diagnostic");
    editor.add_data_validation("Data", fastxlsx::CellRange {1, 2, 2, 2}, list);
    check(!editor.last_edit_error().has_value(),
        "successful data validation retry should clear the previous error");
    editor.rename_sheet("Data", "Renamed Data");
    check(editor.pending_worksheet_edits().front().planned_name == "Renamed Data"
            && editor.pending_worksheet_edits().front().data_validation_count == 2,
        "rename should migrate data validation diagnostics");

    check(threw_fastxlsx_error([&] {
        editor.save_as(artifact("fastxlsx-workbook-editor-data-validation-missing")
            / "parent" / "output.xlsx");
    }), "data validation save should fail when the output parent is missing");
    check(editor.has_unsaved_changes(),
        "failed data validation save should retain unsaved retry state");
    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-data-validation-retry-output.xlsx");
    editor.save_as(output);
    check_contains(fastxlsx::test::read_zip_entries(output)
            .at("xl/worksheets/sheet1.xml"),
        R"(<dataValidations count="2">)",
        "save retry should retain both staged data validation rules");
}

void test_validation_on_added_renamed_worksheet()
{
    const std::filesystem::path source = write_two_sheet_source(
        "fastxlsx-workbook-editor-data-validation-added-source.xlsx");
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.add_worksheet("Added");
    fastxlsx::DataValidationRule list;
    list.type = fastxlsx::DataValidationType::List;
    list.formula1 = "\"X,Y\"";
    editor.add_data_validation("Added", fastxlsx::CellRange {1, 1, 5, 1}, list);
    editor.rename_sheet("Added", "Renamed Added");
    const auto summaries = editor.pending_worksheet_edits();
    check(summaries.size() == 1 && summaries.front().added
            && summaries.front().planned_name == "Renamed Added"
            && summaries.front().data_validation_count == 1,
        "added worksheet rename should migrate data validation diagnostics");

    const std::filesystem::path output = artifact(
        "fastxlsx-workbook-editor-data-validation-added-output.xlsx");
    editor.save_as(output);
    const auto entries = fastxlsx::test::read_zip_entries(output);
    check_contains(entries.at("xl/worksheets/sheet3.xml"),
        R"(<dataValidations count="1"><dataValidation type="list" sqref="A1:A5">)",
        "data validation should target a worksheet added in the same editor");
    check_contains(entries.at("xl/workbook.xml"), R"(name="Renamed Added")",
        "added worksheet rename should compose with validation staging");
}

} // namespace

int main()
{
    try {
        test_insert_multi_range_validation_and_preserve_unknown_parts();
        test_append_count_order_relationship_and_reopen();
        test_self_closing_missing_count_and_count_mismatch();
        test_validation_failure_state_rename_and_retry();
        test_validation_on_added_renamed_worksheet();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor data validation checks failed\n", g_failures);
        return 1;
    }
    std::puts("All WorkbookEditor data validation tests passed");
    return 0;
}
