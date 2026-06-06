#include <fastxlsx/streaming_writer.hpp>

#include "zip_test_utils.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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

const std::array<unsigned char, 67>& tiny_rgba_png()
{
    static constexpr std::array<unsigned char, 67> bytes {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
        0x0a, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x00, 0x01, 0x00, 0x00,
        0x05, 0x00, 0x01, 0x0d, 0x0a, 0x2d, 0xb4, 0x00, 0x00, 0x00, 0x00, 0x49,
        0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
    };
    return bytes;
}

std::span<const std::byte> tiny_png_bytes()
{
    const auto& bytes = tiny_rgba_png();
    return std::as_bytes(std::span<const unsigned char>(bytes.data(), bytes.size()));
}

void write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw TestFailure("failed to create test file");
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw TestFailure("failed to write test file");
    }
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

void test_streaming_writer_document_properties()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-document-properties.xlsx";

    fastxlsx::WorkbookWriterOptions options;
    options.document_properties.creator = "Stream & Author";
    options.document_properties.last_modified_by = "Stream <QA>";
    options.document_properties.title = "Streaming <Props>";
    options.document_properties.subject = "DocProps & Streaming";
    options.document_properties.description = "Streaming docProps test";
    options.document_properties.keywords = "streaming;docprops";
    options.document_properties.category = "Validation";
    options.document_properties.application = "FastXLSX Streaming & Tools";
    options.document_properties.app_version = "3.1";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path, options);
    auto sheet = workbook.add_worksheet("StreamProps");
    sheet.append_row({fastxlsx::CellView::text("document properties")});
    workbook.close();

    check(std::filesystem::exists(output_path), "streaming document properties xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("docProps/core.xml"), "missing streaming custom core properties part");
    check(entries.contains("docProps/app.xml"), "missing streaming custom extended properties part");
    check(!entries.contains("docProps/custom.xml"),
        "streaming document properties API should not create custom properties part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check_contains(
        content_types, "/docProps/core.xml", "missing streaming core properties content type override");
    check_contains(
        content_types, "/docProps/app.xml", "missing streaming extended properties content type override");
    check(content_types.find("/docProps/custom.xml") == std::string::npos,
        "streaming document properties API should not add custom properties content type");

    const auto& package_rels = entries.at("_rels/.rels");
    check_contains(
        package_rels, "Target=\"docProps/core.xml\"", "missing streaming core properties relationship");
    check_contains(
        package_rels, "Target=\"docProps/app.xml\"", "missing streaming extended properties relationship");
    check(package_rels.find("docProps/custom.xml") == std::string::npos,
        "streaming document properties API should not add custom properties relationship");

    const auto& core_properties_xml = entries.at("docProps/core.xml");
    check_contains(core_properties_xml,
        "<dc:creator>Stream &amp; Author</dc:creator>",
        "streaming document properties creator escaping mismatch");
    check_contains(core_properties_xml,
        "<cp:lastModifiedBy>Stream &lt;QA&gt;</cp:lastModifiedBy>",
        "streaming document properties lastModifiedBy escaping mismatch");
    check_contains(core_properties_xml,
        "<dc:title>Streaming &lt;Props&gt;</dc:title>",
        "streaming document properties title escaping mismatch");
    check_contains(core_properties_xml,
        "<dc:subject>DocProps &amp; Streaming</dc:subject>",
        "streaming document properties subject escaping mismatch");
    check_contains(core_properties_xml,
        "<dc:description>Streaming docProps test</dc:description>",
        "streaming document properties description mismatch");
    check_contains(core_properties_xml,
        "<cp:keywords>streaming;docprops</cp:keywords>",
        "streaming document properties keywords mismatch");
    check_contains(core_properties_xml,
        "<cp:category>Validation</cp:category>",
        "streaming document properties category mismatch");

    const auto& extended_properties_xml = entries.at("docProps/app.xml");
    check_contains(extended_properties_xml,
        "<Application>FastXLSX Streaming &amp; Tools</Application>",
        "streaming document properties application escaping mismatch");
    check_contains(extended_properties_xml,
        "<AppVersion>3.1</AppVersion>",
        "streaming document properties app version mismatch");
}

void test_streaming_writer_phase3_metadata_structure()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-phase3-metadata.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("Metadata");

    sheet.set_column_width(1, 1, 12.25);
    sheet.set_column_width(3, 4, 8.75);
    sheet.freeze_panes(1, 0);
    sheet.freeze_panes(2, 3);
    sheet.set_auto_filter({1, 1, 2, 2});
    sheet.set_auto_filter({2, 2, 4, 4});
    sheet.merge_cells({3, 1, 3, 2});
    sheet.merge_cells({4, 3, 4, 4});

    sheet.append_row({
        fastxlsx::CellView::text("Header A"),
        fastxlsx::CellView::text("Header B"),
        fastxlsx::CellView::text("Header C"),
        fastxlsx::CellView::text("Header D"),
    });
    sheet.append_row(
        {
            fastxlsx::CellView::number(42.0),
            fastxlsx::CellView::formula("A2*2"),
            fastxlsx::CellView::formula("IF(A2>0,\"<yes>\",\"&no\")"),
            fastxlsx::CellView::boolean(true),
        },
        fastxlsx::RowOptions {19.25});
    sheet.append_row({
        fastxlsx::CellView::text("Merged left"),
        fastxlsx::CellView::text("Merged right"),
        fastxlsx::CellView::text("plain"),
        fastxlsx::CellView::text("plain"),
    });
    sheet.append_row({
        fastxlsx::CellView::text("tail"),
        fastxlsx::CellView::number(7.0),
        fastxlsx::CellView::text("merged c"),
        fastxlsx::CellView::text("merged d"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "phase3 metadata xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing phase3 metadata worksheet part");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "phase3 metadata should not create worksheet relationships");
    check(!entries.contains("xl/sharedStrings.xml"),
        "phase3 metadata inline string package should not include shared strings");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("drawing") == std::string::npos,
        "phase3 metadata should not create drawing content types");
    check(content_types.find("spreadsheetml.table+xml") == std::string::npos,
        "phase3 metadata should not create table content type overrides");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(count_occurrences(workbook_rels, "<Relationship ") == 1,
        "phase3 metadata should not add workbook relationships");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(worksheet_xml.find("xmlns:r=") == std::string::npos,
        "phase3 metadata worksheet should not declare relationship namespace");
    check_contains(worksheet_xml, "<dimension ref=\"A1:D4\"/>",
        "phase3 metadata worksheet dimension mismatch");
    check_contains(worksheet_xml,
        "<sheetViews><sheetView workbookViewId=\"0\"><pane xSplit=\"3\" ySplit=\"2\" "
        "topLeftCell=\"D3\" activePane=\"bottomRight\" state=\"frozen\"/></sheetView></sheetViews>",
        "last freeze pane XML mismatch");
    check(worksheet_xml.find("topLeftCell=\"A2\"") == std::string::npos,
        "obsolete freeze pane setting should not be serialized");
    check(count_occurrences(worksheet_xml, "<col ") == 2,
        "phase3 metadata column width count mismatch");
    check_contains(worksheet_xml,
        "<cols><col min=\"1\" max=\"1\" width=\"12.25\" customWidth=\"1\"/>"
        "<col min=\"3\" max=\"4\" width=\"8.75\" customWidth=\"1\"/></cols>",
        "phase3 metadata column width XML mismatch");
    check_contains(worksheet_xml,
        "<row r=\"2\" ht=\"19.25\" customHeight=\"1\">",
        "phase3 metadata row height XML mismatch");
    check_contains(
        worksheet_xml, "<c r=\"B2\"><f>A2*2</f></c>", "plain formula XML mismatch");
    check_contains(worksheet_xml,
        "<c r=\"C2\"><f>IF(A2&gt;0,\"&lt;yes&gt;\",\"&amp;no\")</f></c>",
        "escaped formula XML mismatch");
    check_contains(worksheet_xml, "<autoFilter ref=\"B2:D4\"/>",
        "last autoFilter range mismatch");
    check(worksheet_xml.find("<autoFilter ref=\"A1:B2\"/>") == std::string::npos,
        "obsolete autoFilter range should not be serialized");
    check_contains(worksheet_xml,
        "<mergeCells count=\"2\"><mergeCell ref=\"A3:B3\"/><mergeCell ref=\"C4:D4\"/></mergeCells>",
        "phase3 metadata mergeCells XML mismatch");
    check_contains(worksheet_xml,
        "</sheetData><autoFilter ref=\"B2:D4\"/><mergeCells count=\"2\">",
        "phase3 metadata suffix ordering mismatch");
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

void test_streaming_writer_tables()
{
    const auto output_path = std::filesystem::current_path() / "fastxlsx-streaming-tables.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto inventory = workbook.add_worksheet("Inventory");
    auto totals = workbook.add_worksheet("Totals");
    auto plain = workbook.add_worksheet("Plain");

    inventory.append_row({
        fastxlsx::CellView::text("Name"),
        fastxlsx::CellView::text("Qty"),
        fastxlsx::CellView::text("Price & <Cost>"),
    });
    inventory.append_row({
        fastxlsx::CellView::text("Widget"),
        fastxlsx::CellView::number(7.0),
        fastxlsx::CellView::number(12.5),
    });
    inventory.append_row({
        fastxlsx::CellView::text("Gadget"),
        fastxlsx::CellView::number(3.0),
        fastxlsx::CellView::number(8.25),
    });
    inventory.add_external_hyperlink(2, 1, "https://example.com/items/widget");

    fastxlsx::TableOptions inventory_table;
    inventory_table.name = "InventoryTable";
    inventory_table.column_names = {"Name", "Qty", "Price & <Cost>"};
    inventory_table.style_name = "TableStyleMedium9";
    inventory.add_table({1, 1, 3, 3}, inventory_table);

    totals.append_row({
        fastxlsx::CellView::text("Metric"),
        fastxlsx::CellView::text("Value"),
    });
    totals.append_row({
        fastxlsx::CellView::text("Rows"),
        fastxlsx::CellView::number(2.0),
    });
    fastxlsx::TableOptions totals_table;
    totals_table.name = "TotalsTable";
    totals_table.column_names = {"Metric", "Value"};
    totals_table.style_name.clear();
    totals.add_table({1, 1, 2, 2}, totals_table);

    plain.append_row({
        fastxlsx::CellView::text("No table sheet"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "table xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/tables/table1.xml"), "missing first table part");
    check(entries.contains("xl/tables/table2.xml"), "missing second table part");
    check(entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "missing first worksheet table relationships");
    check(entries.contains("xl/worksheets/_rels/sheet2.xml.rels"),
        "missing second worksheet table relationships");
    check(!entries.contains("xl/worksheets/_rels/sheet3.xml.rels"),
        "plain worksheet should not create table relationships");
    check(!entries.contains("xl/styles.xml"), "table slice should not create a styles part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check_contains(content_types,
        R"(<Override PartName="/xl/tables/table1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml"/>)",
        "first table content type override missing");
    check_contains(content_types,
        R"(<Override PartName="/xl/tables/table2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml"/>)",
        "second table content type override missing");

    const auto& first_sheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_sheet_xml,
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)",
        "table worksheet namespace mismatch");
    check_contains(first_sheet_xml, "<dimension ref=\"A1:C3\"/>",
        "table worksheet dimension mismatch");
    check_contains(first_sheet_xml,
        "</sheetData><hyperlinks><hyperlink ref=\"A2\" r:id=\"rId1\"/></hyperlinks>"
        "<tableParts count=\"1\"><tablePart r:id=\"rId2\"/></tableParts></worksheet>",
        "tableParts XML should follow hyperlinks and use the next worksheet rId");

    const auto& first_sheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check_contains(first_sheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/items/widget" TargetMode="External"/>)",
        "hyperlink relationship should keep the first worksheet rId");
    check_contains(first_sheet_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)",
        "table relationship should use the next worksheet rId");

    const auto& first_table_xml = entries.at("xl/tables/table1.xml");
    check_contains(first_table_xml,
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="1" name="InventoryTable" displayName="InventoryTable" ref="A1:C3" totalsRowShown="0">)",
        "first table root XML mismatch");
    check_contains(first_table_xml, R"(<autoFilter ref="A1:C3"/>)",
        "first table autoFilter mismatch");
    check_contains(first_table_xml, R"(<tableColumns count="3">)",
        "first table column count mismatch");
    check_contains(first_table_xml, R"(<tableColumn id="1" name="Name"/>)",
        "first table first column mismatch");
    check_contains(first_table_xml, R"(<tableColumn id="3" name="Price &amp; &lt;Cost&gt;"/>)",
        "first table column XML escaping mismatch");
    check_contains(first_table_xml,
        R"(<tableStyleInfo name="TableStyleMedium9" showFirstColumn="0" showLastColumn="0" showRowStripes="1" showColumnStripes="0"/>)",
        "first table style info mismatch");

    const auto& second_sheet_xml = entries.at("xl/worksheets/sheet2.xml");
    check_contains(second_sheet_xml,
        "<tableParts count=\"1\"><tablePart r:id=\"rId1\"/></tableParts>",
        "second sheet table relationship id should be owner-local");
    const auto& second_sheet_rels = entries.at("xl/worksheets/_rels/sheet2.xml.rels");
    check_contains(second_sheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table2.xml"/>)",
        "second table relationship target mismatch");

    const auto& second_table_xml = entries.at("xl/tables/table2.xml");
    check_contains(second_table_xml,
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="2" name="TotalsTable" displayName="TotalsTable" ref="A1:B2" totalsRowShown="0">)",
        "second table root XML mismatch");
    check(second_table_xml.find("<tableStyleInfo") == std::string::npos,
        "empty table style name should omit style info");

    const auto& third_sheet_xml = entries.at("xl/worksheets/sheet3.xml");
    check(third_sheet_xml.find("<tableParts") == std::string::npos,
        "plain worksheet should not contain tableParts");
    check(third_sheet_xml.find("xmlns:r=") == std::string::npos,
        "plain worksheet should not include relationship namespace");
}

void test_streaming_writer_images()
{
    const auto image_path = std::filesystem::current_path() / "fastxlsx-streaming-image-source.png";
    write_bytes(image_path, tiny_png_bytes());

#ifdef FASTXLSX_TEST_HAS_STB
    const auto output_path = std::filesystem::current_path() / "fastxlsx-streaming-images.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto images = workbook.add_worksheet("Images");
    auto second = workbook.add_worksheet("SecondImage");
    auto plain = workbook.add_worksheet("Plain");

    images.append_row({
        fastxlsx::CellView::text("Name"),
        fastxlsx::CellView::text("Qty"),
        fastxlsx::CellView::text("Image"),
    });
    images.append_row({
        fastxlsx::CellView::text("Widget"),
        fastxlsx::CellView::number(7.0),
        fastxlsx::CellView::text("anchored"),
    });
    images.add_external_hyperlink(2, 1, "https://example.com/items/widget");
    images.add_image(image_path, {1, 3, 4, 5});

    fastxlsx::TableOptions table;
    table.name = "ImageTable";
    table.column_names = {"Name", "Qty"};
    images.add_table({1, 1, 2, 2}, table);

    second.add_image(image_path, {1, 1, 1, 1});
    plain.append_row({fastxlsx::CellView::text("No image sheet")});

    workbook.close();
    check(std::filesystem::exists(output_path), "image xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/media/image1.png"), "missing first image media part");
    check(entries.contains("xl/media/image2.png"), "missing second image media part");
    check(entries.contains("xl/drawings/drawing1.xml"), "missing first drawing part");
    check(entries.contains("xl/drawings/drawing2.xml"), "missing second drawing part");
    check(entries.contains("xl/drawings/_rels/drawing1.xml.rels"),
        "missing first drawing relationships");
    check(entries.contains("xl/drawings/_rels/drawing2.xml.rels"),
        "missing second drawing relationships");
    check(entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "missing first worksheet drawing relationships");
    check(entries.contains("xl/worksheets/_rels/sheet2.xml.rels"),
        "missing second worksheet drawing relationships");
    check(!entries.contains("xl/worksheets/_rels/sheet3.xml.rels"),
        "plain worksheet should not create image relationships");
    check(entries.at("xl/media/image1.png").size() == tiny_rgba_png().size(),
        "first media part byte size mismatch");

    const auto& content_types = entries.at("[Content_Types].xml");
    check_contains(content_types,
        R"(<Default Extension="png" ContentType="image/png"/>)",
        "PNG content type default missing");
    check_contains(content_types,
        R"(<Override PartName="/xl/drawings/drawing1.xml" ContentType="application/vnd.openxmlformats-officedocument.drawing+xml"/>)",
        "first drawing content type override missing");
    check_contains(content_types,
        R"(<Override PartName="/xl/drawings/drawing2.xml" ContentType="application/vnd.openxmlformats-officedocument.drawing+xml"/>)",
        "second drawing content type override missing");

    const auto& first_sheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_sheet_xml,
        "</sheetData><hyperlinks><hyperlink ref=\"A2\" r:id=\"rId1\"/></hyperlinks>"
        "<drawing r:id=\"rId2\"/><tableParts count=\"1\"><tablePart r:id=\"rId3\"/></tableParts></worksheet>",
        "worksheet drawing reference should follow hyperlinks and precede tableParts");

    const auto& first_sheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check_contains(first_sheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/items/widget" TargetMode="External"/>)",
        "image test hyperlink relationship mismatch");
    check_contains(first_sheet_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing1.xml"/>)",
        "worksheet drawing relationship mismatch");
    check_contains(first_sheet_rels,
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)",
        "table relationship should follow drawing rId");

    const auto& first_drawing_xml = entries.at("xl/drawings/drawing1.xml");
    check_contains(first_drawing_xml,
        R"(<xdr:wsDr xmlns:xdr="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)",
        "drawing root namespace mismatch");
    check_contains(first_drawing_xml,
        "<xdr:from><xdr:col>2</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>0</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:from>",
        "drawing from marker mismatch");
    check_contains(first_drawing_xml,
        "<xdr:to><xdr:col>5</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>4</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:to>",
        "drawing to marker mismatch");
    check_contains(first_drawing_xml,
        R"(<a:blip r:embed="rId1"/>)",
        "drawing image relationship id mismatch");
    check_contains(first_drawing_xml,
        R"(<a:ext cx="9525" cy="9525"/>)",
        "drawing intrinsic EMU size mismatch");
    check(count_occurrences(first_drawing_xml, "<xdr:twoCellAnchor") == 1,
        "first drawing anchor count mismatch");

    const auto& first_drawing_rels = entries.at("xl/drawings/_rels/drawing1.xml.rels");
    check_contains(first_drawing_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image1.png"/>)",
        "first drawing image relationship mismatch");

    const auto& second_sheet_xml = entries.at("xl/worksheets/sheet2.xml");
    check_contains(second_sheet_xml,
        "</sheetData><drawing r:id=\"rId1\"/></worksheet>",
        "second worksheet drawing id should be owner-local");
    const auto& second_sheet_rels = entries.at("xl/worksheets/_rels/sheet2.xml.rels");
    check_contains(second_sheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing2.xml"/>)",
        "second worksheet drawing relationship mismatch");
    const auto& second_drawing_rels = entries.at("xl/drawings/_rels/drawing2.xml.rels");
    check_contains(second_drawing_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image2.png"/>)",
        "second drawing image relationship mismatch");
#else
    auto workbook = fastxlsx::WorkbookWriter::create(
        std::filesystem::current_path() / "fastxlsx-streaming-images-disabled.xlsx");
    auto sheet = workbook.add_worksheet("Images");
    check_fastxlsx_error(
        [&sheet, &image_path] { sheet.add_image(image_path, {1, 1, 1, 1}); },
        "streaming add_image should require opt-in stb support");
#endif
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
    fastxlsx::TableOptions table;
    table.name = "ClosedTable";
    table.column_names = {"A", "B"};
    check_fastxlsx_error(
        [&sheet, &table] { sheet.add_table({1, 1, 2, 2}, table); },
        "add_table should reject mutation after workbook close");
    check_fastxlsx_error(
        [&sheet] {
            sheet.add_image(std::filesystem::current_path() / "fastxlsx-unused-image.png",
                {1, 1, 1, 1});
        },
        "add_image should reject mutation after workbook close");
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

    fastxlsx::TableOptions table;
    table.name = "InvalidRangeTable";
    table.column_names = {"A"};
    check_fastxlsx_error(
        [&sheet, &table] { sheet.add_table({1, 1, 1, 1}, table); },
        "tables should reject a header-only range");
    table.column_names = {"A", "B"};
    check_fastxlsx_error(
        [&sheet, &table] { sheet.add_table({1, 1, 2, 16385}, table); },
        "tables should reject a column beyond Excel's limit");

    const auto image_path = std::filesystem::current_path() / "fastxlsx-unused-image.png";
    check_fastxlsx_error(
        [&sheet, &image_path] { sheet.add_image(image_path, {0, 1, 1, 1}); },
        "images should reject a zero anchor row");
    check_fastxlsx_error(
        [&sheet, &image_path] { sheet.add_image(image_path, {1, 0, 1, 1}); },
        "images should reject a zero anchor column");
    check_fastxlsx_error(
        [&sheet, &image_path] { sheet.add_image(image_path, {2, 1, 1, 1}); },
        "images should reject a reversed anchor row range");
    check_fastxlsx_error(
        [&sheet, &image_path] { sheet.add_image(image_path, {1, 2, 1, 1}); },
        "images should reject a reversed anchor column range");
    check_fastxlsx_error(
        [&sheet, &image_path] { sheet.add_image(image_path, {1, 1, 1048577, 1}); },
        "images should reject an anchor row beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet, &image_path] { sheet.add_image(image_path, {1, 1, 1, 16385}); },
        "images should reject an anchor column beyond Excel's limit");
}

void test_streaming_writer_invalid_table_options()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-invalid-tables.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("InvalidTables");

    fastxlsx::TableOptions valid;
    valid.name = "ValidTable";
    valid.column_names = {"First", "Second"};

    fastxlsx::TableOptions empty_name = valid;
    empty_name.name.clear();
    check_fastxlsx_error(
        [&sheet, &empty_name] { sheet.add_table({1, 1, 2, 2}, empty_name); },
        "tables should reject an empty name");

    fastxlsx::TableOptions numeric_name = valid;
    numeric_name.name = "1Table";
    check_fastxlsx_error(
        [&sheet, &numeric_name] { sheet.add_table({1, 1, 2, 2}, numeric_name); },
        "tables should reject a name starting with a digit");

    fastxlsx::TableOptions spaced_name = valid;
    spaced_name.name = "Bad Table";
    check_fastxlsx_error(
        [&sheet, &spaced_name] { sheet.add_table({1, 1, 2, 2}, spaced_name); },
        "tables should reject names with spaces");

    fastxlsx::TableOptions cell_reference_name = valid;
    cell_reference_name.name = "A1";
    check_fastxlsx_error(
        [&sheet, &cell_reference_name] { sheet.add_table({1, 1, 2, 2}, cell_reference_name); },
        "tables should reject names that look like cell references");

    fastxlsx::TableOptions wrong_column_count = valid;
    wrong_column_count.name = "WrongColumnCount";
    wrong_column_count.column_names = {"OnlyOne"};
    check_fastxlsx_error(
        [&sheet, &wrong_column_count] { sheet.add_table({1, 1, 2, 2}, wrong_column_count); },
        "tables should reject a column count mismatch");

    fastxlsx::TableOptions empty_column_name = valid;
    empty_column_name.name = "EmptyColumnName";
    empty_column_name.column_names = {"First", ""};
    check_fastxlsx_error(
        [&sheet, &empty_column_name] { sheet.add_table({1, 1, 2, 2}, empty_column_name); },
        "tables should reject empty column names");

    fastxlsx::TableOptions duplicate_column_name = valid;
    duplicate_column_name.name = "DuplicateColumnName";
    duplicate_column_name.column_names = {"Name", "name"};
    check_fastxlsx_error(
        [&sheet, &duplicate_column_name] { sheet.add_table({1, 1, 2, 2}, duplicate_column_name); },
        "tables should reject duplicate column names");

    sheet.add_table({1, 1, 2, 2}, valid);

    fastxlsx::TableOptions duplicate_table_name = valid;
    duplicate_table_name.name = "validtable";
    check_fastxlsx_error(
        [&sheet, &duplicate_table_name] { sheet.add_table({3, 1, 4, 2}, duplicate_table_name); },
        "tables should reject duplicate workbook table names case-insensitively");
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
        [&sheet] { sheet.set_column_width(1, 1, std::numeric_limits<double>::quiet_NaN()); },
        "set_column_width should reject a NaN width");
    check_fastxlsx_error(
        [&sheet] { sheet.set_column_width(1, 1, std::numeric_limits<double>::infinity()); },
        "set_column_width should reject an infinite width");

    check_fastxlsx_error(
        [&sheet] { sheet.freeze_panes(1048577, 0); },
        "freeze_panes should reject a row split beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet] { sheet.freeze_panes(0, 16385); },
        "freeze_panes should reject a column split beyond Excel's limit");

    check_fastxlsx_error(
        [&sheet] {
            sheet.append_row(
                {fastxlsx::CellView::number(std::numeric_limits<double>::quiet_NaN())});
        },
        "append_row should reject a NaN numeric cell");
    check_fastxlsx_error(
        [&sheet] {
            sheet.append_row(
                {fastxlsx::CellView::number(std::numeric_limits<double>::infinity())});
        },
        "append_row should reject an infinite numeric cell");
    check_fastxlsx_error(
        [&sheet] {
            sheet.append_row(
                {fastxlsx::CellView::text("bad height")},
                fastxlsx::RowOptions {std::numeric_limits<double>::quiet_NaN()});
        },
        "append_row should reject a NaN row height");
    check_fastxlsx_error(
        [&sheet] {
            sheet.append_row(
                {fastxlsx::CellView::text("bad height")},
                fastxlsx::RowOptions {std::numeric_limits<double>::infinity()});
        },
        "append_row should reject an infinite row height");

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
        test_streaming_writer_document_properties();
        test_streaming_writer_phase3_metadata_structure();
        test_streaming_writer_file_backed_body_round_trip();
        test_streaming_writer_data_validations();
        test_streaming_writer_external_hyperlinks();
        test_streaming_writer_tables();
        test_streaming_writer_images();
        test_streaming_writer_shared_string_package();
        test_streaming_writer_shared_strings_workbook_scope_and_crlf();
        test_streaming_writer_file_backed_multi_sheet_bodies_do_not_alias();
        test_streaming_writer_rejects_mutation_after_close();
        test_streaming_writer_invalid_ranges();
        test_streaming_writer_invalid_table_options();
        test_streaming_writer_invalid_data_validation_rules();
        test_streaming_writer_invalid_metadata_and_rows();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
