#include <fastxlsx/fastxlsx.hpp>

#include "zip_test_utils.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

class TestFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void check(bool condition, const char* message)
{
    if (!condition) {
        throw TestFailure(message);
    }
}

template <typename Callback>
void expect_fastxlsx_error(Callback&& callback, std::string_view expected_text)
{
    bool matched = false;
    try {
        callback();
    } catch (const fastxlsx::FastXlsxError& error) {
        matched = std::string_view(error.what()).find(expected_text)
            != std::string_view::npos;
    }
    if (!matched) {
        throw TestFailure(
            "expected FastXlsxError diagnostic containing: "
            + std::string(expected_text));
    }
}

std::map<std::string, std::string> workbook_entries(std::string worksheet_xml)
{
    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Data" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";

    std::map<std::string, std::string> entries;
    fastxlsx::test::insert_zip_entry(entries, "[Content_Types].xml", content_types);
    fastxlsx::test::insert_zip_entry(entries, "_rels/.rels", package_relationships);
    fastxlsx::test::insert_zip_entry(entries, "xl/workbook.xml", workbook);
    fastxlsx::test::insert_zip_entry(
        entries, "xl/_rels/workbook.xml.rels", workbook_relationships);
    fastxlsx::test::insert_zip_entry(
        entries, "xl/worksheets/sheet1.xml", std::move(worksheet_xml));
    return entries;
}

std::filesystem::path write_fixture(std::string_view name, std::string worksheet_xml)
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(name);
    fastxlsx::test::write_stored_zip_entries(
        path, workbook_entries(std::move(worksheet_xml)));
    return path;
}

std::string representative_worksheet_xml()
{
    return R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<x:sheetData/>)"
        R"(<x:conditionalFormatting sqref="A1"><x:cfRule type="expression"><x:formula>1=1</x:formula></x:cfRule></x:conditionalFormatting>)"
        R"(<x:dataValidations count="2" disablePrompts="0" xWindow="0" yWindow="0">)"
        R"(<x:dataValidation type="custom" allowBlank="true" showInputMessage="1" showErrorMessage="true" errorStyle="warning" errorTitle="Bad &amp; &quot;value&quot;" error="Use &lt; 10" promptTitle="Choose &apos;value&apos;" prompt="A &#x26; B" sqref="$A$2:$A$4 C2:C4">)"
        R"(<x:formula1>A2&lt;&gt;&quot;A&amp;B&quot;</x:formula1>)"
        R"(</x:dataValidation>)"
        R"(<x:dataValidation type="whole" operator="between" sqref="D2:D8">)"
        R"(<x:formula1>1</x:formula1><x:formula2>100</x:formula2>)"
        R"(</x:dataValidation>)"
        R"(</x:dataValidations>)"
        R"(<x:hyperlinks/>)"
        R"(</x:worksheet>)";
}

void test_projects_owning_rules_in_source_order()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-data-validation-reader-representative.xlsx",
        representative_worksheet_xml());
    const std::string before = fastxlsx::test::read_file(path);
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::vector<fastxlsx::WorksheetDataValidationView> values;
    fastxlsx::WorksheetDataValidationReadCallbacks callbacks;
    callbacks.on_data_validation = [&](const fastxlsx::WorksheetDataValidationView& value) {
        values.push_back(value);
    };
    const fastxlsx::WorksheetDataValidationReadSummary summary =
        reader.read_worksheet_data_validations("Data", callbacks);

    check(values.size() == 2, "data-validation callback count mismatch");
    check(values[0].index == 0 && values[1].index == 1,
        "data-validation source indexes are not zero-based");
    check(values[0].ranges.size() == 2
            && values[0].ranges[0].first_row == 2
            && values[0].ranges[0].first_column == 1
            && values[0].ranges[0].last_row == 4
            && values[0].ranges[1].first_column == 3,
        "multi-range sqref projection mismatch");
    check(values[0].rule.type == fastxlsx::DataValidationType::Custom
            && values[0].rule.allow_blank
            && values[0].rule.show_input_message
            && values[0].rule.show_error_message
            && values[0].rule.error_style
            && *values[0].rule.error_style == fastxlsx::DataValidationErrorStyle::Warning,
        "data-validation boolean/enumeration projection mismatch");
    check(values[0].rule.error_title == "Bad & \"value\""
            && values[0].rule.error == "Use < 10"
            && values[0].rule.prompt_title == "Choose 'value'"
            && values[0].rule.prompt == "A & B"
            && values[0].rule.formula1 == "A2<>\"A&B\"",
        "data-validation XML entity decoding mismatch");
    check(values[1].rule.type == fastxlsx::DataValidationType::Whole
            && values[1].rule.operator_type
            && *values[1].rule.operator_type == fastxlsx::DataValidationOperator::Between
            && values[1].rule.formula1 == "1"
            && values[1].rule.formula2 == "100",
        "data-validation formula projection mismatch");
    check(summary.validation_count == 2 && summary.range_count == 3
            && summary.peak_ranges_per_validation == 2
            && summary.peak_sqref_bytes >= 13
            && summary.peak_formula_text_bytes >= 9
            && summary.peak_metadata_text_bytes >= 14
            && summary.peak_xml_nesting_depth >= 2,
        "data-validation summary telemetry mismatch");
    check(fastxlsx::test::read_file(path) == before,
        "data-validation reader changed the source package");
}

class CallbackFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void test_callback_failure_allows_retry()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-data-validation-reader-retry.xlsx",
        representative_worksheet_xml());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetDataValidationReadCallbacks throwing;
    throwing.on_data_validation = [](const fastxlsx::WorksheetDataValidationView&) {
        throw CallbackFailure("data-validation callback stopped traversal");
    };
    bool saw_exact_exception = false;
    try {
        (void)reader.read_worksheet_data_validations("Data", throwing);
    } catch (const CallbackFailure& error) {
        saw_exact_exception = std::string_view(error.what())
            == "data-validation callback stopped traversal";
    }
    check(saw_exact_exception,
        "data-validation callback exception should propagate unchanged");

    std::size_t count = 0;
    fastxlsx::WorksheetDataValidationReadCallbacks retry;
    retry.on_data_validation =
        [&](const fastxlsx::WorksheetDataValidationView&) { ++count; };
    const fastxlsx::WorksheetDataValidationReadSummary summary =
        reader.read_worksheet_data_validations("Data", retry);
    check(summary.validation_count == 2 && count == 2,
        "data-validation reader should retry after callback failure");
}

void test_absent_and_empty_container_are_clean()
{
    const std::filesystem::path absent = write_fixture(
        "worksheet-data-validation-reader-absent.xlsx",
        R"(<worksheet><sheetData/></worksheet>)");
    const fastxlsx::WorkbookReader absent_reader = fastxlsx::WorkbookReader::open(absent);
    const fastxlsx::WorksheetDataValidationReadSummary absent_summary =
        absent_reader.read_worksheet_data_validations("Data");
    check(absent_summary.validation_count == 0 && absent_summary.range_count == 0,
        "absent dataValidations should be a clean empty result");

    const std::filesystem::path empty = write_fixture(
        "worksheet-data-validation-reader-empty.xlsx",
        R"(<worksheet><sheetData/><dataValidations count="0"/></worksheet>)");
    const fastxlsx::WorkbookReader empty_reader = fastxlsx::WorkbookReader::open(empty);
    const fastxlsx::WorksheetDataValidationReadSummary empty_summary =
        empty_reader.read_worksheet_data_validations("Data");
    check(empty_summary.validation_count == 0 && empty_summary.range_count == 0,
        "empty dataValidations should be a clean empty result");
}

void test_guardrails()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-data-validation-reader-guardrails.xlsx",
        representative_worksheet_xml());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetDataValidationReaderOptions options;
    options.max_sqref_bytes = 3;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_data_validations("Data", {}, options); },
        "max_sqref_bytes");
    options = {};
    options.max_formula_text_bytes = 2;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_data_validations("Data", {}, options); },
        "max_formula_text_bytes");
    options = {};
    options.max_metadata_text_bytes = 2;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_data_validations("Data", {}, options); },
        "errorTitle exceeds");
    options = {};
    options.max_ranges_per_validation = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_data_validations("Data", {}, options); },
        "max_ranges_per_validation");
    options = {};
    options.max_validation_count = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_data_validations("Data", {}, options); },
        "max_validation_count");
    options = {};
    options.max_xml_nesting_depth = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_data_validations("Data", {}, options); },
        "max_xml_nesting_depth");
    options = {};
    options.max_xml_window_bytes = 8;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_data_validations("Data", {}, options); },
        "bounded input window");
    options = {};
    options.max_formula_text_bytes = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_data_validations("Data", {}, options); },
        "nonzero max_formula_text_bytes");
}

void expect_validation_failure(
    std::string_view name, std::string xml, std::string_view diagnostic)
{
    const std::filesystem::path path = write_fixture(name, std::move(xml));
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_data_validations("Data"); }, diagnostic);
}

void test_rejects_unsupported_and_malformed_shapes()
{
    expect_validation_failure("worksheet-data-validation-count.xlsx",
        R"(<worksheet><sheetData/><dataValidations count="2"><dataValidation type="list" sqref="A1"><formula1>1</formula1></dataValidation></dataValidations></worksheet>)",
        "count does not match");
    expect_validation_failure("worksheet-data-validation-missing-formula.xlsx",
        R"(<worksheet><sheetData/><dataValidations><dataValidation type="list" sqref="A1"/></dataValidations></worksheet>)",
        "formula1 cannot be empty");
    expect_validation_failure("worksheet-data-validation-invalid-sqref.xlsx",
        R"(<worksheet><sheetData/><dataValidations><dataValidation type="list" sqref="A0"><formula1>1</formula1></dataValidation></dataValidations></worksheet>)",
        "invalid A1 range");
    expect_validation_failure("worksheet-data-validation-invalid-bool.xlsx",
        R"(<worksheet><sheetData/><dataValidations><dataValidation type="list" allowBlank="yes" sqref="A1"><formula1>1</formula1></dataValidation></dataValidations></worksheet>)",
        "invalid allowBlank");
    expect_validation_failure("worksheet-data-validation-unsupported-attr.xlsx",
        R"(<worksheet><sheetData/><dataValidations><dataValidation type="list" imeMode="fullAlpha" sqref="A1"><formula1>1</formula1></dataValidation></dataValidations></worksheet>)",
        "unsupported attribute");
    expect_validation_failure("worksheet-data-validation-unsupported-child.xlsx",
        R"(<worksheet><sheetData/><dataValidations><dataValidation type="list" sqref="A1"><x:ext xmlns:x="urn:test"/><formula1>1</formula1></dataValidation></dataValidations></worksheet>)",
        "unsupported child");
    expect_validation_failure("worksheet-data-validation-formula-order.xlsx",
        R"(<worksheet><sheetData/><dataValidations><dataValidation type="whole" operator="between" sqref="A1"><formula2>2</formula2><formula1>1</formula1></dataValidation></dataValidations></worksheet>)",
        "out-of-order formula2");
    expect_validation_failure("worksheet-data-validation-empty-formula2.xlsx",
        R"(<worksheet><sheetData/><dataValidations><dataValidation type="whole" operator="equal" sqref="A1"><formula1>1</formula1><formula2/></dataValidation></dataValidations></worksheet>)",
        "formula2 cannot be empty");
    expect_validation_failure("worksheet-data-validation-schema-order.xlsx",
        R"(<worksheet><sheetData/><dataValidations><dataValidation type="list" sqref="A1"><formula1>1</formula1></dataValidation></dataValidations><conditionalFormatting sqref="A1"/></worksheet>)",
        "schema order");
    expect_validation_failure("worksheet-data-validation-prompt-window.xlsx",
        R"(<worksheet><sheetData/><dataValidations disablePrompts="1"><dataValidation type="list" sqref="A1"><formula1>1</formula1></dataValidation></dataValidations></worksheet>)",
        "disablePrompts");
    expect_validation_failure("worksheet-data-validation-qname.xlsx",
        R"(<x:worksheet xmlns:x="urn:main"><x:sheetData/><x:dataValidations><x:dataValidation type="list" sqref="A1"><x:formula1>1</y:formula1></x:dataValidation></x:dataValidations></x:worksheet>)",
        "QName");
}

void test_foreign_extension_names_do_not_alias_validation_elements()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-data-validation-reader-foreign-extension.xlsx",
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:z="urn:test"><x:sheetData/><x:dataValidations count="1"><x:dataValidation type="list" sqref="A1"><x:formula1>1</x:formula1></x:dataValidation></x:dataValidations><x:extLst><x:ext uri="test"><z:dataValidations><z:dataValidation><z:formula1>ignored</z:formula1></z:dataValidation></z:dataValidations></x:ext></x:extLst></x:worksheet>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::size_t count = 0;
    fastxlsx::WorksheetDataValidationReadCallbacks callbacks;
    callbacks.on_data_validation =
        [&](const fastxlsx::WorksheetDataValidationView&) { ++count; };
    const fastxlsx::WorksheetDataValidationReadSummary summary =
        reader.read_worksheet_data_validations("Data", callbacks);
    check(count == 1 && summary.validation_count == 1,
        "foreign extension local names must not alias worksheet validations");
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_reads_production_deflate_validation()
{
    const std::filesystem::path path =
        fastxlsx::test::artifact_path("worksheet-data-validation-reader-deflate.xlsx");
    fastxlsx::WorkbookWriterOptions options;
    options.zip_compression_level = 1;
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path, options);
    fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Data");
    fastxlsx::DataValidationRule rule;
    rule.type = fastxlsx::DataValidationType::List;
    rule.formula1 = "\"One,Two\"";
    const std::array<fastxlsx::CellRange, 2> ranges {
        fastxlsx::CellRange {1, 1, 3, 1},
        fastxlsx::CellRange {5, 1, 7, 1},
    };
    sheet.add_data_validation(ranges, rule);
    sheet.append_row({fastxlsx::CellView::text("deflate")});
    writer.close();

    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::size_t count = 0;
    fastxlsx::WorksheetDataValidationReadCallbacks callbacks;
    callbacks.on_data_validation = [&](const fastxlsx::WorksheetDataValidationView& value) {
        ++count;
        check(value.ranges.size() == 2
                && value.rule.type == fastxlsx::DataValidationType::List
                && value.rule.formula1 == "\"One,Two\"",
            "DEFLATE data-validation projection mismatch");
    };
    const fastxlsx::WorksheetDataValidationReadSummary summary =
        reader.read_worksheet_data_validations("Data", callbacks);
    check(count == 1 && summary.validation_count == 1
            && summary.range_count == 2,
        "DEFLATE data-validation traversal mismatch");
}
#endif

} // namespace

int main()
{
    try {
        test_projects_owning_rules_in_source_order();
        test_callback_failure_allows_retry();
        test_absent_and_empty_container_are_clean();
        test_guardrails();
        test_rejects_unsupported_and_malformed_shapes();
        test_foreign_extension_names_do_not_alias_validation_elements();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_reads_production_deflate_validation();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
