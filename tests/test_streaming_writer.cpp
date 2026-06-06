#include <fastxlsx/streaming_writer.hpp>

#include "zip_test_utils.hpp"

#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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

template <typename Func>
void check_fastxlsx_error(Func func, const char* message)
{
    try {
        func();
    } catch (const fastxlsx::FastXlsxError&) {
        return;
    } catch (const std::exception& error) {
        throw TestFailure(std::string(message) + ": unexpected exception: " + error.what());
    }

    throw TestFailure(message);
}

void check_contains(const std::string& text, const char* fragment, const char* message)
{
    check(text.find(fragment) != std::string::npos, message);
}

std::size_t count_occurrences(const std::string& text, std::string_view fragment)
{
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = text.find(fragment, offset)) != std::string::npos) {
        ++count;
        offset += fragment.size();
    }
    return count;
}

void test_streaming_writer_smoke_package()
{
    const auto output_path = std::filesystem::current_path() / "fastxlsx-streaming-smoke.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("Streaming");

    sheet.set_column_width(1, 2, 18.75);
    sheet.set_column_width(4, 4, 9.5);
    sheet.freeze_panes(1, 1);
    sheet.set_auto_filter({1, 1, 3, 4});
    sheet.merge_cells({3, 1, 3, 2});

    sheet.append_row(
        {
            fastxlsx::CellView::number(123.5),
            fastxlsx::CellView::text(" text & <tag>"),
            fastxlsx::CellView::boolean(true),
            fastxlsx::CellView::formula("SUM(A1,C1)"),
        },
        fastxlsx::RowOptions {24.5});
    sheet.append_row({
        fastxlsx::CellView::number(10.0),
        fastxlsx::CellView::text("plain"),
        fastxlsx::CellView::boolean(false),
        fastxlsx::CellView::formula("A2*2"),
    });
    sheet.append_row({
        fastxlsx::CellView::text("Merged"),
        fastxlsx::CellView::text(""),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "streaming xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("[Content_Types].xml"), "missing content types part");
    check(entries.contains("_rels/.rels"), "missing package relationships part");
    check(entries.contains("docProps/core.xml"), "missing streaming core properties part");
    check(entries.contains("docProps/app.xml"), "missing streaming extended properties part");
    check(entries.contains("xl/workbook.xml"), "missing workbook part");
    check(entries.contains("xl/_rels/workbook.xml.rels"), "missing workbook relationships part");
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing streaming worksheet part");
    check(!entries.contains("xl/sharedStrings.xml"), "inline string package should not include shared strings");

    const auto& content_types = entries.at("[Content_Types].xml");
    check_contains(content_types, "/xl/workbook.xml", "missing workbook content type override");
    check_contains(
        content_types, "/xl/worksheets/sheet1.xml", "missing worksheet content type override");
    check_contains(
        content_types, "/docProps/core.xml", "missing core properties content type override");
    check_contains(
        content_types, "/docProps/app.xml", "missing extended properties content type override");

    const auto& package_rels = entries.at("_rels/.rels");
    check_contains(package_rels, "officeDocument", "missing officeDocument relationship");
    check_contains(
        package_rels, "Target=\"xl/workbook.xml\"", "package relationship target mismatch");
    check_contains(
        package_rels, "Target=\"docProps/core.xml\"", "missing core properties package relationship");
    check_contains(
        package_rels, "Target=\"docProps/app.xml\"", "missing extended properties package relationship");

    const auto& core_properties_xml = entries.at("docProps/core.xml");
    check_contains(
        core_properties_xml, "<dc:creator>FastXLSX</dc:creator>", "core properties creator missing");

    const auto& extended_properties_xml = entries.at("docProps/app.xml");
    check_contains(extended_properties_xml,
        "<Application>FastXLSX</Application>",
        "extended properties application missing");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check_contains(workbook_xml, "name=\"Streaming\"", "workbook streaming sheet name missing");
    check_contains(workbook_xml, "r:id=\"rId1\"", "workbook relationship id missing");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check_contains(
        workbook_rels,
        "Target=\"worksheets/sheet1.xml\"",
        "worksheet relationship target mismatch");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:D3\"/>", "worksheet dimension mismatch");
    check_contains(
        worksheet_xml,
        "<sheetViews><sheetView workbookViewId=\"0\"><pane xSplit=\"1\" ySplit=\"1\" "
        "topLeftCell=\"B2\" activePane=\"bottomRight\" state=\"frozen\"/></sheetView></sheetViews>",
        "freeze pane XML mismatch");
    check_contains(worksheet_xml, "<cols>", "missing cols collection");
    check_contains(
        worksheet_xml,
        "<col min=\"1\" max=\"2\" width=\"18.75\" customWidth=\"1\"/>",
        "first column width mismatch");
    check_contains(
        worksheet_xml,
        "<col min=\"4\" max=\"4\" width=\"9.5\" customWidth=\"1\"/>",
        "second column width mismatch");
    check_contains(worksheet_xml, "<sheetData>", "missing sheetData");
    check_contains(
        worksheet_xml,
        "<row r=\"1\" ht=\"24.5\" customHeight=\"1\">",
        "row height XML mismatch");
    check_contains(
        worksheet_xml, "<c r=\"A1\"><v>123.5</v></c>", "number cell encoding mismatch");
    check_contains(
        worksheet_xml,
        "<c r=\"B1\" t=\"inlineStr\"><is><t xml:space=\"preserve\"> text &amp; "
        "&lt;tag&gt;</t></is></c>",
        "inline string encoding mismatch");
    check_contains(
        worksheet_xml, "<c r=\"C1\" t=\"b\"><v>1</v></c>", "boolean true cell encoding mismatch");
    check_contains(
        worksheet_xml, "<c r=\"D1\"><f>SUM(A1,C1)</f></c>", "formula cell encoding mismatch");
    check_contains(
        worksheet_xml, "<c r=\"C2\" t=\"b\"><v>0</v></c>", "boolean false cell encoding mismatch");
    check_contains(worksheet_xml, "<autoFilter ref=\"A1:D3\"/>", "autoFilter range mismatch");
    check_contains(
        worksheet_xml,
        "<mergeCells count=\"1\"><mergeCell ref=\"A3:B3\"/></mergeCells>",
        "mergeCells XML mismatch");
}

void test_streaming_writer_file_backed_body_round_trip()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-file-backed-body.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("FileBody");

    constexpr std::uint32_t row_count = 160;
    constexpr std::uint32_t column_count = 8;
    for (std::uint32_t row = 1; row <= row_count; ++row) {
        std::vector<std::string> values;
        values.reserve(column_count);
        for (std::uint32_t column = 1; column <= column_count; ++column) {
            if (row == 1 && column == 1) {
                values.emplace_back("FIRST_BODY_SENTINEL");
            } else if (row == 80 && column == 4) {
                values.emplace_back("MIDDLE_BODY_SENTINEL");
            } else if (row == row_count && column == column_count) {
                values.emplace_back("LAST_BODY_SENTINEL");
            } else {
                values.emplace_back("body-row-" + std::to_string(row) + "-col-"
                    + std::to_string(column) + "-payload-abcdefghijklmnopqrstuvwxyz");
            }
        }

        std::vector<fastxlsx::CellView> cells;
        cells.reserve(values.size());
        for (const std::string& value : values) {
            cells.push_back(fastxlsx::CellView::text(value));
        }
        sheet.append_row(std::span<const fastxlsx::CellView>(cells.data(), cells.size()));
    }

    workbook.close();
    check(std::filesystem::exists(output_path), "file-backed body xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing file-backed worksheet part");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:H160\"/>",
        "file-backed worksheet dimension mismatch");
    check_contains(worksheet_xml, "<sheetData>", "file-backed worksheet sheetData missing");
    check_contains(worksheet_xml, "FIRST_BODY_SENTINEL",
        "file-backed worksheet first sentinel missing");
    check_contains(worksheet_xml, "MIDDLE_BODY_SENTINEL",
        "file-backed worksheet middle sentinel missing");
    check_contains(worksheet_xml, "LAST_BODY_SENTINEL",
        "file-backed worksheet last sentinel missing");
    check_contains(
        worksheet_xml, "</sheetData></worksheet>", "file-backed worksheet footer was truncated");
}

void test_streaming_writer_data_validations()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-data-validations.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("Validation");

    sheet.append_row({
        fastxlsx::CellView::text("Number"),
        fastxlsx::CellView::text("Decimal"),
        fastxlsx::CellView::text("Choice"),
        fastxlsx::CellView::text("Date"),
        fastxlsx::CellView::text("Time"),
        fastxlsx::CellView::text("Length"),
        fastxlsx::CellView::text("Custom"),
    });
    sheet.append_row({
        fastxlsx::CellView::number(5.0),
        fastxlsx::CellView::number(1.5),
        fastxlsx::CellView::text("A"),
        fastxlsx::CellView::number(45500.0),
        fastxlsx::CellView::number(0.5),
        fastxlsx::CellView::text("short"),
        fastxlsx::CellView::text("abc"),
    });

    fastxlsx::DataValidationRule whole;
    whole.type = fastxlsx::DataValidationType::Whole;
    whole.operator_type = fastxlsx::DataValidationOperator::Between;
    whole.formula1 = "1";
    whole.formula2 = "10";
    whole.allow_blank = true;
    sheet.add_data_validation({2, 1, 10, 1}, whole);

    fastxlsx::DataValidationRule decimal;
    decimal.type = fastxlsx::DataValidationType::Decimal;
    decimal.operator_type = fastxlsx::DataValidationOperator::GreaterThan;
    decimal.formula1 = "0.5";
    sheet.add_data_validation({2, 2, 10, 2}, decimal);

    fastxlsx::DataValidationRule list;
    list.type = fastxlsx::DataValidationType::List;
    list.formula1 = "\"A,B,C\"";
    list.allow_blank = true;
    sheet.add_data_validation({2, 3, 10, 3}, list);

    fastxlsx::DataValidationRule date;
    date.type = fastxlsx::DataValidationType::Date;
    date.operator_type = fastxlsx::DataValidationOperator::NotBetween;
    date.formula1 = "DATE(2026,1,1)";
    date.formula2 = "DATE(2026,12,31)";
    sheet.add_data_validation({2, 4, 10, 4}, date);

    fastxlsx::DataValidationRule time;
    time.type = fastxlsx::DataValidationType::Time;
    time.operator_type = fastxlsx::DataValidationOperator::LessThan;
    time.formula1 = "TIME(18,0,0)";
    sheet.add_data_validation({2, 5, 10, 5}, time);

    fastxlsx::DataValidationRule text_length;
    text_length.type = fastxlsx::DataValidationType::TextLength;
    text_length.operator_type = fastxlsx::DataValidationOperator::LessThanOrEqual;
    text_length.formula1 = "12";
    sheet.add_data_validation({2, 6, 10, 6}, text_length);

    fastxlsx::DataValidationRule custom;
    custom.type = fastxlsx::DataValidationType::Custom;
    custom.formula1 = "LEN(G2)&\"<tag>\"";
    sheet.add_data_validation({2, 7, 10, 7}, custom);

    workbook.close();
    check(std::filesystem::exists(output_path), "data validation xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing data validation worksheet part");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "data validation should not create worksheet relationships");
    check(!entries.contains("xl/metadata.xml"),
        "data validation should not create a metadata package part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("dataValidation") == std::string::npos,
        "data validation should not add content type overrides");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(count_occurrences(workbook_rels, "<Relationship ") == 1,
        "data validation should not add workbook relationships");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:G2\"/>",
        "data validation worksheet dimension mismatch");
    check_contains(worksheet_xml, "</sheetData><dataValidations count=\"7\">",
        "dataValidations should follow sheetData when no other suffix metadata exists");
    check_contains(worksheet_xml,
        "<dataValidation type=\"whole\" allowBlank=\"1\" operator=\"between\" "
        "sqref=\"A2:A10\"><formula1>1</formula1><formula2>10</formula2></dataValidation>",
        "whole-number data validation XML mismatch");
    check_contains(worksheet_xml,
        "<dataValidation type=\"decimal\" operator=\"greaterThan\" "
        "sqref=\"B2:B10\"><formula1>0.5</formula1></dataValidation>",
        "decimal data validation XML mismatch");
    check_contains(worksheet_xml,
        "<dataValidation type=\"list\" allowBlank=\"1\" sqref=\"C2:C10\">"
        "<formula1>\"A,B,C\"</formula1></dataValidation>",
        "list data validation XML mismatch");
    check_contains(worksheet_xml,
        "<dataValidation type=\"date\" operator=\"notBetween\" "
        "sqref=\"D2:D10\"><formula1>DATE(2026,1,1)</formula1><formula2>DATE(2026,12,31)</formula2></dataValidation>",
        "date data validation XML mismatch");
    check_contains(worksheet_xml,
        "<dataValidation type=\"time\" operator=\"lessThan\" "
        "sqref=\"E2:E10\"><formula1>TIME(18,0,0)</formula1></dataValidation>",
        "time data validation XML mismatch");
    check_contains(worksheet_xml,
        "<dataValidation type=\"textLength\" operator=\"lessThanOrEqual\" "
        "sqref=\"F2:F10\"><formula1>12</formula1></dataValidation>",
        "textLength data validation XML mismatch");
    check_contains(worksheet_xml,
        "<dataValidation type=\"custom\" sqref=\"G2:G10\">"
        "<formula1>LEN(G2)&amp;\"&lt;tag&gt;\"</formula1></dataValidation>",
        "custom data validation XML escaping mismatch");
    check_contains(
        worksheet_xml, "</dataValidations></worksheet>", "dataValidations footer mismatch");
    check(count_occurrences(worksheet_xml, "<dataValidation ") == 7,
        "dataValidation item count mismatch");
}

void test_streaming_writer_external_hyperlinks()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-external-hyperlinks.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto links = workbook.add_worksheet("Links");
    auto more_links = workbook.add_worksheet("MoreLinks");
    auto plain = workbook.add_worksheet("Plain");

    links.append_row({
        fastxlsx::CellView::text("OpenAI"),
        fastxlsx::CellView::text("No link"),
    });
    links.append_row({
        fastxlsx::CellView::text("Row2"),
        fastxlsx::CellView::text("Docs & <API>"),
    });
    links.add_external_hyperlink(1, 1, "https://openai.com/");
    links.add_external_hyperlink(2, 2, "https://example.com/path?a=1&b=2");

    more_links.append_row({
        fastxlsx::CellView::text("Second sheet"),
    });
    more_links.add_external_hyperlink(1, 1, "mailto:test@example.com");

    plain.append_row({
        fastxlsx::CellView::text("No hyperlink sheet"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "external hyperlinks xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing first hyperlink worksheet");
    check(entries.contains("xl/worksheets/sheet2.xml"), "missing second hyperlink worksheet");
    check(entries.contains("xl/worksheets/sheet3.xml"), "missing plain worksheet");
    check(entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "missing first worksheet hyperlink relationships");
    check(entries.contains("xl/worksheets/_rels/sheet2.xml.rels"),
        "missing second worksheet hyperlink relationships");
    check(!entries.contains("xl/worksheets/_rels/sheet3.xml.rels"),
        "plain worksheet should not create relationships");
    check(!entries.contains("xl/metadata.xml"), "external hyperlinks should not create metadata part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("hyperlink") == std::string::npos,
        "external hyperlinks should not add content type overrides");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(count_occurrences(workbook_rels, "<Relationship ") == 3,
        "external hyperlinks should not add workbook relationships");

    const auto& first_sheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_sheet_xml,
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)",
        "hyperlink worksheet namespace mismatch");
    check_contains(first_sheet_xml, "<dimension ref=\"A1:B2\"/>",
        "hyperlink worksheet dimension mismatch");
    check_contains(first_sheet_xml,
        "<c r=\"A1\" t=\"inlineStr\"><is><t>OpenAI</t></is></c>",
        "hyperlink should not replace A1 cell text");
    check_contains(first_sheet_xml,
        "<c r=\"B2\" t=\"inlineStr\"><is><t>Docs &amp; &lt;API&gt;</t></is></c>",
        "hyperlink should not replace B2 escaped cell text");
    check_contains(first_sheet_xml,
        "</sheetData><hyperlinks><hyperlink ref=\"A1\" r:id=\"rId1\"/>"
        "<hyperlink ref=\"B2\" r:id=\"rId2\"/></hyperlinks></worksheet>",
        "first worksheet hyperlink XML mismatch");
    check(count_occurrences(first_sheet_xml, "<hyperlink ") == 2,
        "first worksheet hyperlink count mismatch");

    const auto& first_sheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check(count_occurrences(first_sheet_rels, "<Relationship ") == 2,
        "first worksheet hyperlink relationship count mismatch");
    check_contains(first_sheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://openai.com/" TargetMode="External"/>)",
        "first hyperlink relationship mismatch");
    check_contains(first_sheet_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/path?a=1&amp;b=2" TargetMode="External"/>)",
        "second hyperlink relationship mismatch or target escape failure");

    const auto& second_sheet_xml = entries.at("xl/worksheets/sheet2.xml");
    check_contains(second_sheet_xml,
        "<hyperlinks><hyperlink ref=\"A1\" r:id=\"rId1\"/></hyperlinks>",
        "second worksheet hyperlink XML mismatch");
    const auto& second_sheet_rels = entries.at("xl/worksheets/_rels/sheet2.xml.rels");
    check_contains(second_sheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="mailto:test@example.com" TargetMode="External"/>)",
        "second worksheet owner-local hyperlink relationship mismatch");

    const auto& third_sheet_xml = entries.at("xl/worksheets/sheet3.xml");
    check(third_sheet_xml.find("<hyperlinks>") == std::string::npos,
        "plain worksheet should not contain hyperlinks");
    check(third_sheet_xml.find("xmlns:r=") == std::string::npos,
        "plain worksheet should not include relationship namespace");
}

void test_streaming_writer_shared_string_package()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-shared-strings.xlsx";

    fastxlsx::WorkbookWriterOptions options;
    options.string_strategy = fastxlsx::StringStrategy::SharedString;

    auto workbook = fastxlsx::WorkbookWriter::create(output_path, options);
    auto sheet = workbook.add_worksheet("Shared");

    sheet.append_row({
        fastxlsx::CellView::text("repeat"),
        fastxlsx::CellView::text("space "),
        fastxlsx::CellView::text("escaped & <tag>"),
    });
    sheet.append_row({
        fastxlsx::CellView::text("repeat"),
        fastxlsx::CellView::text("space "),
    });
    sheet.append_row({
        fastxlsx::CellView::text(""),
        fastxlsx::CellView::text(" leading"),
        fastxlsx::CellView::text("\tindent"),
        fastxlsx::CellView::text("repeat"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "shared string xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("[Content_Types].xml"), "missing shared string content types part");
    check(entries.contains("docProps/core.xml"), "missing shared string core properties part");
    check(entries.contains("docProps/app.xml"), "missing shared string extended properties part");
    check(entries.contains("xl/_rels/workbook.xml.rels"), "missing shared string workbook rels part");
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing shared string worksheet part");
    check(entries.contains("xl/sharedStrings.xml"), "missing shared strings part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check_contains(
        content_types, "/xl/sharedStrings.xml", "missing shared strings content type override");
    check_contains(content_types,
        "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml",
        "shared strings content type mismatch");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check_contains(workbook_rels, "relationships/sharedStrings", "missing shared strings relationship");
    check_contains(
        workbook_rels, "Target=\"sharedStrings.xml\"", "shared strings relationship target mismatch");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:D3\"/>", "shared string dimension mismatch");
    check(count_occurrences(worksheet_xml, " t=\"s\"") == 9, "shared string cell count mismatch");
    check(count_occurrences(worksheet_xml, "inlineStr") == 0, "shared string worksheet used inlineStr");
    check_contains(
        worksheet_xml, "<c r=\"A1\" t=\"s\"><v>0</v></c>", "shared string A1 index mismatch");
    check_contains(
        worksheet_xml, "<c r=\"B1\" t=\"s\"><v>1</v></c>", "shared string B1 index mismatch");
    check_contains(
        worksheet_xml, "<c r=\"C1\" t=\"s\"><v>2</v></c>", "shared string C1 index mismatch");
    check_contains(
        worksheet_xml, "<c r=\"A2\" t=\"s\"><v>0</v></c>", "shared string duplicate A2 mismatch");
    check_contains(
        worksheet_xml, "<c r=\"B2\" t=\"s\"><v>1</v></c>", "shared string duplicate B2 mismatch");
    check_contains(
        worksheet_xml, "<c r=\"A3\" t=\"s\"><v>3</v></c>", "shared string empty A3 mismatch");
    check_contains(worksheet_xml, "<c r=\"B3\" t=\"s\"><v>4</v></c>",
        "shared string leading-space B3 mismatch");
    check_contains(
        worksheet_xml, "<c r=\"C3\" t=\"s\"><v>5</v></c>", "shared string tab C3 mismatch");
    check_contains(
        worksheet_xml, "<c r=\"D3\" t=\"s\"><v>0</v></c>", "shared string duplicate D3 mismatch");
    check_contains(worksheet_xml,
        "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c><c r=\"C1\" t=\"s\"><v>2</v></c></row>",
        "shared string row 1 mapping mismatch");
    check_contains(worksheet_xml,
        "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>0</v></c><c r=\"B2\" t=\"s\"><v>1</v></c></row>",
        "shared string duplicate row mapping mismatch");
    check_contains(worksheet_xml,
        "<row r=\"3\"><c r=\"A3\" t=\"s\"><v>3</v></c><c r=\"B3\" t=\"s\"><v>4</v></c><c r=\"C3\" t=\"s\"><v>5</v></c><c r=\"D3\" t=\"s\"><v>0</v></c></row>",
        "shared string empty/space/tab row mapping mismatch");

    const auto& shared_strings_xml = entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_xml, "<sst ", "missing shared strings root");
    check_contains(shared_strings_xml, "count=\"9\"", "shared strings count mismatch");
    check_contains(shared_strings_xml, "uniqueCount=\"6\"", "shared strings uniqueCount mismatch");
    check(count_occurrences(shared_strings_xml, "<si>") == 6, "shared string unique entry count mismatch");
    check_contains(shared_strings_xml, "<si><t>repeat</t></si>", "first shared string mismatch");
    check_contains(shared_strings_xml,
        "<si><t xml:space=\"preserve\">space </t></si>",
        "space-preserved shared string mismatch");
    check_contains(shared_strings_xml,
        "<si><t>escaped &amp; &lt;tag&gt;</t></si>",
        "escaped shared string mismatch");
    check_contains(shared_strings_xml, "<si><t></t></si>", "empty shared string mismatch");
    check_contains(shared_strings_xml,
        "<si><t xml:space=\"preserve\"> leading</t></si>",
        "leading-space shared string mismatch");
    check_contains(shared_strings_xml,
        "<si><t xml:space=\"preserve\">\tindent</t></si>",
        "tab-preserved shared string mismatch");
    check_contains(shared_strings_xml,
        "<si><t>repeat</t></si><si><t xml:space=\"preserve\">space </t></si><si><t>escaped &amp; &lt;tag&gt;</t></si><si><t></t></si><si><t xml:space=\"preserve\"> leading</t></si><si><t xml:space=\"preserve\">\tindent</t></si>",
        "shared strings order/index mapping mismatch");
}

void test_streaming_writer_shared_strings_workbook_scope_and_crlf()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-shared-strings-workbook-scope.xlsx";

    fastxlsx::WorkbookWriterOptions options;
    options.string_strategy = fastxlsx::StringStrategy::SharedString;

    auto workbook = fastxlsx::WorkbookWriter::create(output_path, options);
    auto first = workbook.add_worksheet("One");
    auto second = workbook.add_worksheet("Two");

    first.append_row({
        fastxlsx::CellView::text("repeat"),
        fastxlsx::CellView::text("line\n"),
    });
    second.append_row({
        fastxlsx::CellView::text("repeat"),
        fastxlsx::CellView::text("\rline"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "workbook-scope shared string xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing first shared string worksheet part");
    check(entries.contains("xl/worksheets/sheet2.xml"), "missing second shared string worksheet part");
    check(entries.contains("xl/sharedStrings.xml"), "missing workbook-scope shared strings part");

    const auto& first_sheet_xml = entries.at("xl/worksheets/sheet1.xml");
    const auto& second_sheet_xml = entries.at("xl/worksheets/sheet2.xml");
    check_contains(first_sheet_xml, "<c r=\"A1\" t=\"s\"><v>0</v></c>",
        "first worksheet shared string repeat index mismatch");
    check_contains(first_sheet_xml, "<c r=\"B1\" t=\"s\"><v>1</v></c>",
        "first worksheet newline shared string index mismatch");
    check_contains(second_sheet_xml, "<c r=\"A1\" t=\"s\"><v>0</v></c>",
        "second worksheet shared string repeat index mismatch");
    check_contains(second_sheet_xml, "<c r=\"B1\" t=\"s\"><v>2</v></c>",
        "second worksheet carriage-return shared string index mismatch");

    const auto& shared_strings_xml = entries.at("xl/sharedStrings.xml");
    check_contains(shared_strings_xml, "count=\"4\"", "workbook-scope shared strings count mismatch");
    check_contains(
        shared_strings_xml, "uniqueCount=\"3\"", "workbook-scope shared strings uniqueCount mismatch");
    check(count_occurrences(shared_strings_xml, "<si>") == 3,
        "workbook-scope shared string unique entry count mismatch");
    check_contains(shared_strings_xml,
        "<si><t>repeat</t></si><si><t xml:space=\"preserve\">line\n</t></si><si><t xml:space=\"preserve\">\rline</t></si>",
        "workbook-scope shared strings order or preserve mapping mismatch");
}

void test_streaming_writer_file_backed_multi_sheet_bodies_do_not_alias()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-file-backed-multi-sheet.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto first = workbook.add_worksheet("One");
    auto second = workbook.add_worksheet("Two");

    first.append_row({
        fastxlsx::CellView::text("SHEET_ONE_ONLY"),
        fastxlsx::CellView::text("one payload"),
    });
    second.append_row({
        fastxlsx::CellView::text("SHEET_TWO_ONLY"),
        fastxlsx::CellView::text("two payload"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "multi-sheet file-backed xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing first file-backed sheet");
    check(entries.contains("xl/worksheets/sheet2.xml"), "missing second file-backed sheet");

    const auto& first_sheet_xml = entries.at("xl/worksheets/sheet1.xml");
    const auto& second_sheet_xml = entries.at("xl/worksheets/sheet2.xml");
    check_contains(first_sheet_xml, "SHEET_ONE_ONLY", "first sheet sentinel missing");
    check(first_sheet_xml.find("SHEET_TWO_ONLY") == std::string::npos,
        "first sheet contains second sheet body");
    check_contains(second_sheet_xml, "SHEET_TWO_ONLY", "second sheet sentinel missing");
    check(second_sheet_xml.find("SHEET_ONE_ONLY") == std::string::npos,
        "second sheet contains first sheet body");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check_contains(
        workbook_rels, "Target=\"worksheets/sheet1.xml\"", "first worksheet relationship missing");
    check_contains(
        workbook_rels, "Target=\"worksheets/sheet2.xml\"", "second worksheet relationship missing");
}

void test_streaming_writer_rejects_mutation_after_close()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-after-close.xlsx";

    fastxlsx::WorkbookWriterOptions options;
    options.string_strategy = fastxlsx::StringStrategy::SharedString;

    auto workbook = fastxlsx::WorkbookWriter::create(output_path, options);
    auto sheet = workbook.add_worksheet("Closed");
    sheet.append_row({fastxlsx::CellView::text("before close")});

    workbook.close();
    workbook.close();

    check_fastxlsx_error(
        [&sheet] { sheet.append_row({fastxlsx::CellView::text("after close")}); },
        "append_row should reject mutation after workbook close");
    check_fastxlsx_error(
        [&sheet] { sheet.set_column_width(1, 1, 12.0); },
        "set_column_width should reject mutation after workbook close");
    check_fastxlsx_error(
        [&sheet] { sheet.freeze_panes(1, 1); },
        "freeze_panes should reject mutation after workbook close");
    check_fastxlsx_error(
        [&sheet] { sheet.set_auto_filter({1, 1, 1, 1}); },
        "set_auto_filter should reject mutation after workbook close");
    check_fastxlsx_error(
        [&sheet] { sheet.merge_cells({1, 1, 1, 2}); },
        "merge_cells should reject mutation after workbook close");
    check_fastxlsx_error(
        [&sheet] {
            fastxlsx::DataValidationRule rule;
            rule.type = fastxlsx::DataValidationType::List;
            rule.formula1 = "\"A,B\"";
            sheet.add_data_validation({1, 1, 1, 1}, rule);
        },
        "add_data_validation should reject mutation after workbook close");
    check_fastxlsx_error(
        [&sheet] { sheet.add_external_hyperlink(1, 1, "https://example.com/"); },
        "add_external_hyperlink should reject mutation after workbook close");
}

void test_streaming_writer_invalid_ranges()
{
    const auto output_path = std::filesystem::current_path() / "fastxlsx-streaming-invalid-ranges.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("Ranges");

    check_fastxlsx_error(
        [&sheet] { sheet.set_auto_filter({0, 1, 1, 1}); },
        "autoFilter should reject a zero row");
    check_fastxlsx_error(
        [&sheet] { sheet.set_auto_filter({1, 0, 1, 1}); },
        "autoFilter should reject a zero column");
    check_fastxlsx_error(
        [&sheet] { sheet.set_auto_filter({2, 1, 1, 1}); },
        "autoFilter should reject a reversed row range");
    check_fastxlsx_error(
        [&sheet] { sheet.set_auto_filter({1, 2, 1, 1}); },
        "autoFilter should reject a reversed column range");
    check_fastxlsx_error(
        [&sheet] { sheet.set_auto_filter({1, 1, 1048577, 1}); },
        "autoFilter should reject a row beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet] { sheet.set_auto_filter({1, 1, 1, 16385}); },
        "autoFilter should reject a column beyond Excel's limit");

    check_fastxlsx_error(
        [&sheet] { sheet.merge_cells({0, 1, 1, 2}); },
        "mergeCells should reject a zero row");
    check_fastxlsx_error(
        [&sheet] { sheet.merge_cells({1, 0, 1, 2}); },
        "mergeCells should reject a zero column");
    check_fastxlsx_error(
        [&sheet] { sheet.merge_cells({2, 1, 1, 2}); },
        "mergeCells should reject a reversed row range");
    check_fastxlsx_error(
        [&sheet] { sheet.merge_cells({1, 2, 1, 1}); },
        "mergeCells should reject a reversed column range");
    check_fastxlsx_error(
        [&sheet] { sheet.merge_cells({1, 1, 1048577, 2}); },
        "mergeCells should reject a row beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet] { sheet.merge_cells({1, 1, 1, 16385}); },
        "mergeCells should reject a column beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet] { sheet.merge_cells({1, 1, 1, 1}); },
        "mergeCells should reject a single-cell range");

    fastxlsx::DataValidationRule list;
    list.type = fastxlsx::DataValidationType::List;
    list.formula1 = "\"A,B\"";
    check_fastxlsx_error(
        [&sheet, &list] { sheet.add_data_validation({0, 1, 1, 1}, list); },
        "dataValidations should reject a zero row");
    check_fastxlsx_error(
        [&sheet, &list] { sheet.add_data_validation({1, 0, 1, 1}, list); },
        "dataValidations should reject a zero column");
    check_fastxlsx_error(
        [&sheet, &list] { sheet.add_data_validation({2, 1, 1, 1}, list); },
        "dataValidations should reject a reversed row range");
    check_fastxlsx_error(
        [&sheet, &list] { sheet.add_data_validation({1, 2, 1, 1}, list); },
        "dataValidations should reject a reversed column range");
    check_fastxlsx_error(
        [&sheet, &list] { sheet.add_data_validation({1, 1, 1048577, 1}, list); },
        "dataValidations should reject a row beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet, &list] { sheet.add_data_validation({1, 1, 1, 16385}, list); },
        "dataValidations should reject a column beyond Excel's limit");

    check_fastxlsx_error(
        [&sheet] { sheet.add_external_hyperlink(0, 1, "https://example.com/"); },
        "external hyperlinks should reject a zero row");
    check_fastxlsx_error(
        [&sheet] { sheet.add_external_hyperlink(1, 0, "https://example.com/"); },
        "external hyperlinks should reject a zero column");
    check_fastxlsx_error(
        [&sheet] { sheet.add_external_hyperlink(1048577, 1, "https://example.com/"); },
        "external hyperlinks should reject a row beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet] { sheet.add_external_hyperlink(1, 16385, "https://example.com/"); },
        "external hyperlinks should reject a column beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet] { sheet.add_external_hyperlink(1, 1, ""); },
        "external hyperlinks should reject an empty target");
}

void test_streaming_writer_invalid_data_validation_rules()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-invalid-validations.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("Validation");

    fastxlsx::DataValidationRule missing_formula;
    missing_formula.type = fastxlsx::DataValidationType::List;
    check_fastxlsx_error(
        [&sheet, &missing_formula] { sheet.add_data_validation({1, 1, 1, 1}, missing_formula); },
        "dataValidations should reject empty formula1");

    fastxlsx::DataValidationRule list_with_operator;
    list_with_operator.type = fastxlsx::DataValidationType::List;
    list_with_operator.operator_type = fastxlsx::DataValidationOperator::Equal;
    list_with_operator.formula1 = "\"A,B\"";
    check_fastxlsx_error(
        [&sheet, &list_with_operator] { sheet.add_data_validation({1, 1, 1, 1}, list_with_operator); },
        "list dataValidations should reject an operator");

    fastxlsx::DataValidationRule custom_with_formula2;
    custom_with_formula2.type = fastxlsx::DataValidationType::Custom;
    custom_with_formula2.formula1 = "A1>0";
    custom_with_formula2.formula2 = "B1";
    check_fastxlsx_error(
        [&sheet, &custom_with_formula2] { sheet.add_data_validation({1, 1, 1, 1}, custom_with_formula2); },
        "custom dataValidations should reject formula2");

    fastxlsx::DataValidationRule whole_without_operator;
    whole_without_operator.type = fastxlsx::DataValidationType::Whole;
    whole_without_operator.formula1 = "1";
    check_fastxlsx_error(
        [&sheet, &whole_without_operator] { sheet.add_data_validation({1, 1, 1, 1}, whole_without_operator); },
        "whole dataValidations should require an operator");

    fastxlsx::DataValidationRule between_without_formula2;
    between_without_formula2.type = fastxlsx::DataValidationType::Decimal;
    between_without_formula2.operator_type = fastxlsx::DataValidationOperator::Between;
    between_without_formula2.formula1 = "1.5";
    check_fastxlsx_error(
        [&sheet, &between_without_formula2] { sheet.add_data_validation({1, 1, 1, 1}, between_without_formula2); },
        "between dataValidations should require formula2");

    fastxlsx::DataValidationRule equal_with_formula2;
    equal_with_formula2.type = fastxlsx::DataValidationType::Decimal;
    equal_with_formula2.operator_type = fastxlsx::DataValidationOperator::Equal;
    equal_with_formula2.formula1 = "1.5";
    equal_with_formula2.formula2 = "2.5";
    check_fastxlsx_error(
        [&sheet, &equal_with_formula2] { sheet.add_data_validation({1, 1, 1, 1}, equal_with_formula2); },
        "single-formula dataValidations should reject formula2");
}

void test_streaming_writer_invalid_metadata_and_rows()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-invalid-metadata.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("Metadata");

    check_fastxlsx_error(
        [&sheet] { sheet.set_column_width(0, 1, 10.0); },
        "set_column_width should reject a zero first column");
    check_fastxlsx_error(
        [&sheet] { sheet.set_column_width(1, 0, 10.0); },
        "set_column_width should reject a zero last column");
    check_fastxlsx_error(
        [&sheet] { sheet.set_column_width(3, 2, 10.0); },
        "set_column_width should reject a reversed column range");
    check_fastxlsx_error(
        [&sheet] { sheet.set_column_width(1, 16385, 10.0); },
        "set_column_width should reject a column beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet] { sheet.set_column_width(1, 1, 0.0); },
        "set_column_width should reject a zero width");
    check_fastxlsx_error(
        [&sheet] { sheet.set_column_width(1, 1, -1.0); },
        "set_column_width should reject a negative width");

    check_fastxlsx_error(
        [&sheet] { sheet.freeze_panes(1048577, 0); },
        "freeze_panes should reject a row split beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet] { sheet.freeze_panes(0, 16385); },
        "freeze_panes should reject a column split beyond Excel's limit");

    std::vector<fastxlsx::CellView> too_wide_row(16385, fastxlsx::CellView::number(1.0));
    check_fastxlsx_error(
        [&sheet, &too_wide_row] {
            sheet.append_row(std::span<const fastxlsx::CellView>(
                too_wide_row.data(), too_wide_row.size()));
        },
        "append_row should reject rows beyond Excel's column limit");
}

} // namespace

int main()
{
    try {
        test_streaming_writer_smoke_package();
        test_streaming_writer_file_backed_body_round_trip();
        test_streaming_writer_data_validations();
        test_streaming_writer_external_hyperlinks();
        test_streaming_writer_shared_string_package();
        test_streaming_writer_shared_strings_workbook_scope_and_crlf();
        test_streaming_writer_file_backed_multi_sheet_bodies_do_not_alias();
        test_streaming_writer_rejects_mutation_after_close();
        test_streaming_writer_invalid_ranges();
        test_streaming_writer_invalid_data_validation_rules();
        test_streaming_writer_invalid_metadata_and_rows();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
