#include <fastxlsx/streaming_writer.hpp>

#include "image_test_bytes.hpp"
#include "zip_test_utils.hpp"

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
    check_contains(workbook_xml, R"(<calcPr calcId="124519" fullCalcOnLoad="1"/>)",
        "streaming formulas should request full recalculation on load");

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

void test_streaming_writer_empty_rows_dimension()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-empty-row-dimensions.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto no_rows = workbook.add_worksheet("NoRows");
    auto only_empty = workbook.add_worksheet("OnlyEmpty");
    auto sparse = workbook.add_worksheet("Sparse");

    std::span<const fastxlsx::CellView> empty_cells;
    only_empty.append_row(empty_cells);
    only_empty.append_row(empty_cells);

    sparse.append_row(empty_cells);
    sparse.append_row({
        fastxlsx::CellView::text("after empty row"),
        fastxlsx::CellView::number(7.0),
        fastxlsx::CellView::boolean(true),
    });
    sparse.append_row(empty_cells);

    workbook.close();
    check(std::filesystem::exists(output_path), "empty-row dimension xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing no-rows worksheet part");
    check(entries.contains("xl/worksheets/sheet2.xml"), "missing only-empty worksheet part");
    check(entries.contains("xl/worksheets/sheet3.xml"), "missing sparse worksheet part");

    const auto& no_rows_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(no_rows_xml, "<dimension ref=\"A1\"/>",
        "no-row worksheet dimension should stay at A1");
    check_contains(no_rows_xml, "<sheetData></sheetData>",
        "no-row worksheet should serialize empty sheetData");
    check(no_rows_xml.find("<row ") == std::string::npos,
        "no-row worksheet should not serialize row elements");

    const auto& only_empty_xml = entries.at("xl/worksheets/sheet2.xml");
    check_contains(only_empty_xml, "<dimension ref=\"A1\"/>",
        "all-empty-row worksheet dimension should stay at A1");
    check(count_occurrences(only_empty_xml, "<row ") == 2,
        "all-empty-row worksheet should serialize both appended rows");
    check_contains(only_empty_xml, "<row r=\"1\"></row><row r=\"2\"></row>",
        "all-empty-row worksheet row XML mismatch");
    check(only_empty_xml.find("<c r=") == std::string::npos,
        "all-empty-row worksheet should not serialize cells");

    const auto& sparse_xml = entries.at("xl/worksheets/sheet3.xml");
    check_contains(sparse_xml, "<dimension ref=\"A1:C3\"/>",
        "sparse worksheet dimension should include trailing appended empty row");
    check(sparse_xml.find("<dimension ref=\"A1:C2\"/>") == std::string::npos,
        "sparse worksheet dimension should not drop the trailing empty row");
    check(count_occurrences(sparse_xml, "<row ") == 3,
        "sparse worksheet should serialize leading, data, and trailing rows");
    check_contains(sparse_xml,
        "<row r=\"1\"></row><row r=\"2\"><c r=\"A2\" t=\"inlineStr\"><is>"
        "<t>after empty row</t></is></c><c r=\"B2\"><v>7</v></c>"
        "<c r=\"C2\" t=\"b\"><v>1</v></c></row><row r=\"3\"></row>",
        "sparse empty-row worksheet XML mismatch");
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

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(<calcPr calcId="124519" fullCalcOnLoad="1"/>)",
        "phase3 formula metadata should request full recalculation on load");
    check(!entries.contains("xl/calcChain.xml"),
        "phase3 formula metadata should not create calcChain");

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

void test_streaming_writer_data_validation_multi_range_sqref()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-data-validation-multi-range.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("ValidationRanges");

    sheet.append_row({
        fastxlsx::CellView::text("A"),
        fastxlsx::CellView::text("B"),
        fastxlsx::CellView::text("C"),
        fastxlsx::CellView::text("D"),
        fastxlsx::CellView::text("E"),
    });
    sheet.append_row({
        fastxlsx::CellView::number(1.0),
        fastxlsx::CellView::text("gap"),
        fastxlsx::CellView::number(2.0),
        fastxlsx::CellView::text("gap"),
        fastxlsx::CellView::number(3.0),
    });

    fastxlsx::DataValidationRule whole;
    whole.type = fastxlsx::DataValidationType::Whole;
    whole.operator_type = fastxlsx::DataValidationOperator::Between;
    whole.formula1 = "1";
    whole.formula2 = "10";
    whole.allow_blank = true;
    sheet.add_data_validation({{2, 1, 10, 1}, {2, 3, 10, 3}, {2, 5, 10, 5}}, whole);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing multi-range validation worksheet");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "multi-range data validation should not create worksheet relationships");
    check(!entries.contains("xl/metadata.xml"),
        "multi-range data validation should not create metadata part");
    check(!entries.contains("xl/styles.xml"),
        "multi-range data validation should not create styles");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("dataValidation") == std::string::npos,
        "multi-range data validation should not add content types");
    check(content_types.find("styles") == std::string::npos,
        "multi-range data validation should not add style content types");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(count_occurrences(workbook_rels, "<Relationship ") == 1,
        "multi-range data validation should not add workbook relationships");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check(workbook_xml.find("<calcPr") == std::string::npos,
        "multi-range data validation formulas should not request calculation metadata");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(worksheet_xml.find("xmlns:r=") == std::string::npos,
        "multi-range validation-only worksheet should not declare relationship namespace");
    check_contains(worksheet_xml, "<dimension ref=\"A1:E2\"/>",
        "multi-range data validation dimension mismatch");
    check_contains(worksheet_xml, "</sheetData><dataValidations count=\"1\">",
        "multi-range dataValidations count mismatch");
    check_contains(worksheet_xml,
        "<dataValidation type=\"whole\" allowBlank=\"1\" operator=\"between\" "
        "sqref=\"A2:A10 C2:C10 E2:E10\"><formula1>1</formula1><formula2>10</formula2></dataValidation>",
        "multi-range data validation sqref XML mismatch");
    check(count_occurrences(worksheet_xml, "<dataValidation ") == 1,
        "multi-range dataValidation item count mismatch");
}

void test_streaming_writer_data_validations_with_relationship_metadata()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-validation-relationship-metadata.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("ValidationRels");

    sheet.append_row({fastxlsx::CellView::text("Value"), fastxlsx::CellView::text("Link")});
    sheet.append_row({fastxlsx::CellView::number(5.0), fastxlsx::CellView::text("Docs")});
    sheet.append_row({fastxlsx::CellView::number(7.0), fastxlsx::CellView::text("More")});

    fastxlsx::DataValidationRule whole;
    whole.type = fastxlsx::DataValidationType::Whole;
    whole.operator_type = fastxlsx::DataValidationOperator::Between;
    whole.formula1 = "1";
    whole.formula2 = "10";
    whole.allow_blank = true;
    whole.show_input_message = true;
    whole.prompt_title = "Input";
    whole.prompt = "Use 1 to 10";
    whole.show_error_message = true;
    whole.error_style = fastxlsx::DataValidationErrorStyle::Stop;
    whole.error_title = "Range";
    whole.error = "Out of range";
    sheet.add_data_validation({{2, 1, 10, 1}, {12, 1, 14, 1}}, whole);

    sheet.add_external_hyperlink(2, 2, "https://example.com/docs");
    sheet.add_external_hyperlink(3, 2, "https://example.com/more");

    fastxlsx::TableOptions table;
    table.name = "ValidationRelTable";
    table.column_names = {"Value", "Link"};
    sheet.add_table({1, 1, 3, 2}, table);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "validation relationship metadata sheet should create worksheet relationships");
    check(entries.contains("xl/tables/table1.xml"),
        "validation relationship metadata sheet should create table part");
    check(!entries.contains("xl/metadata.xml"),
        "data validation relationship metadata should not create metadata part");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(count_occurrences(workbook_rels, "<Relationship ") == 1,
        "data validation relationship metadata should not add workbook relationships");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("dataValidation") == std::string::npos,
        "data validation relationship metadata should not add data validation content types");
    check_contains(content_types,
        R"(<Override PartName="/xl/tables/table1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml"/>)",
        "validation relationship metadata table content type missing");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)",
        "validation relationship metadata worksheet should include relationship namespace");
    check_contains(worksheet_xml, "<dimension ref=\"A1:B3\"/>",
        "validation relationship metadata dimension mismatch");
    check_contains(worksheet_xml,
        "</sheetData><dataValidations count=\"1\"><dataValidation type=\"whole\" allowBlank=\"1\" "
        "showInputMessage=\"1\" showErrorMessage=\"1\" errorStyle=\"stop\" errorTitle=\"Range\" "
        "error=\"Out of range\" promptTitle=\"Input\" prompt=\"Use 1 to 10\" operator=\"between\" "
        "sqref=\"A2:A10 A12:A14\"><formula1>1</formula1><formula2>10</formula2></dataValidation></dataValidations>"
        "<hyperlinks><hyperlink ref=\"B2\" r:id=\"rId1\"/><hyperlink ref=\"B3\" r:id=\"rId2\"/></hyperlinks>"
        "<tableParts count=\"1\"><tablePart r:id=\"rId3\"/></tableParts></worksheet>",
        "dataValidations should not consume relationship ids before hyperlinks and tables");

    const auto& worksheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check(count_occurrences(worksheet_rels, "<Relationship ") == 3,
        "validation relationship metadata relationship count mismatch");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/docs" TargetMode="External"/>)",
        "first validation relationship metadata hyperlink relationship mismatch");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/more" TargetMode="External"/>)",
        "second validation relationship metadata hyperlink relationship mismatch");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)",
        "validation relationship metadata table relationship mismatch");
}

void test_streaming_writer_data_validation_formula2_escape_and_namespace()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-data-validation-formula2-escape.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("Formula2Escape");

    sheet.append_row({fastxlsx::CellView::text("Text")});
    sheet.append_row({fastxlsx::CellView::text("abc")});

    fastxlsx::DataValidationRule rule;
    rule.type = fastxlsx::DataValidationType::TextLength;
    rule.operator_type = fastxlsx::DataValidationOperator::Between;
    rule.formula1 = "1";
    rule.formula2 = "LEN(A2&\"<max>\")";
    sheet.add_data_validation({2, 1, 10, 1}, rule);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "formula2 escape data validation should not create worksheet relationships");
    check(!entries.contains("xl/metadata.xml"),
        "formula2 escape data validation should not create metadata part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("dataValidation") == std::string::npos,
        "formula2 escape data validation should not add content types");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check(workbook_xml.find("<calcPr") == std::string::npos,
        "data validation formulas should not request workbook recalculation metadata");
    check(worksheet_xml.find("xmlns:r=") == std::string::npos,
        "validation-only worksheet should not declare relationship namespace");
    check_contains(worksheet_xml,
        "<dataValidation type=\"textLength\" operator=\"between\" sqref=\"A2:A10\">"
        "<formula1>1</formula1><formula2>LEN(A2&amp;\"&lt;max&gt;\")</formula2></dataValidation>",
        "formula2 XML escaping mismatch");
}

void test_streaming_writer_data_validation_prompt_error_metadata()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-data-validation-prompts.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("ValidationPrompt");

    sheet.append_row({
        fastxlsx::CellView::text("Whole"),
        fastxlsx::CellView::text("List"),
        fastxlsx::CellView::text("Decimal"),
        fastxlsx::CellView::text("Custom"),
    });
    sheet.append_row({
        fastxlsx::CellView::number(5.0),
        fastxlsx::CellView::text("A"),
        fastxlsx::CellView::number(1.5),
        fastxlsx::CellView::text("abc"),
    });

    fastxlsx::DataValidationRule whole;
    whole.type = fastxlsx::DataValidationType::Whole;
    whole.operator_type = fastxlsx::DataValidationOperator::Between;
    whole.formula1 = "1";
    whole.formula2 = "10";
    whole.allow_blank = true;
    whole.show_input_message = true;
    whole.show_error_message = true;
    whole.error_style = fastxlsx::DataValidationErrorStyle::Warning;
    whole.error_title = "Error \"Title\" & <bad>";
    whole.error = "Bad 'value' & <cell>";
    whole.prompt_title = "Input <Title> & \"Quote\"";
    whole.prompt = "Enter 'whole' & <value>";
    sheet.add_data_validation({2, 1, 10, 1}, whole);

    fastxlsx::DataValidationRule list;
    list.type = fastxlsx::DataValidationType::List;
    list.formula1 = "\"A,B,C\"";
    list.show_input_message = true;
    list.prompt_title = "Choice";
    list.prompt = "Pick A, B, or C";
    sheet.add_data_validation({2, 2, 10, 2}, list);

    fastxlsx::DataValidationRule decimal;
    decimal.type = fastxlsx::DataValidationType::Decimal;
    decimal.operator_type = fastxlsx::DataValidationOperator::GreaterThan;
    decimal.formula1 = "0";
    decimal.show_error_message = true;
    decimal.error_style = fastxlsx::DataValidationErrorStyle::Information;
    decimal.error_title = "Decimal";
    decimal.error = "Use a positive decimal";
    sheet.add_data_validation({2, 3, 10, 3}, decimal);

    fastxlsx::DataValidationRule custom;
    custom.type = fastxlsx::DataValidationType::Custom;
    custom.formula1 = "LEN(D2)>0";
    custom.error_style = fastxlsx::DataValidationErrorStyle::Stop;
    sheet.add_data_validation({2, 4, 10, 4}, custom);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing prompt/error validation worksheet");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "prompt/error data validation should not create worksheet relationships");
    check(!entries.contains("xl/metadata.xml"),
        "prompt/error data validation should not create metadata part");
    check(!entries.contains("xl/styles.xml"),
        "prompt/error data validation should not create styles");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("dataValidation") == std::string::npos,
        "prompt/error data validation should not add data validation content types");
    check(content_types.find("styles") == std::string::npos,
        "prompt/error data validation should not add style content types");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(count_occurrences(workbook_rels, "<Relationship ") == 1,
        "prompt/error data validation should not add workbook relationships");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check(workbook_xml.find("<calcPr") == std::string::npos,
        "prompt/error data validation should not request workbook recalculation metadata");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(worksheet_xml.find("xmlns:r=") == std::string::npos,
        "prompt/error validation-only worksheet should not declare relationship namespace");
    check_contains(worksheet_xml, "<dimension ref=\"A1:D2\"/>",
        "prompt/error data validation dimension mismatch");
    check_contains(worksheet_xml, "</sheetData><dataValidations count=\"4\">",
        "prompt/error dataValidations should follow sheetData");
    check_contains(worksheet_xml,
        "<dataValidation type=\"whole\" allowBlank=\"1\" showInputMessage=\"1\" "
        "showErrorMessage=\"1\" errorStyle=\"warning\" "
        "errorTitle=\"Error &quot;Title&quot; &amp; &lt;bad&gt;\" "
        "error=\"Bad &apos;value&apos; &amp; &lt;cell&gt;\" "
        "promptTitle=\"Input &lt;Title&gt; &amp; &quot;Quote&quot;\" "
        "prompt=\"Enter &apos;whole&apos; &amp; &lt;value&gt;\" operator=\"between\" "
        "sqref=\"A2:A10\"><formula1>1</formula1><formula2>10</formula2></dataValidation>",
        "whole prompt/error validation XML mismatch");
    check_contains(worksheet_xml,
        "<dataValidation type=\"list\" showInputMessage=\"1\" promptTitle=\"Choice\" "
        "prompt=\"Pick A, B, or C\" sqref=\"B2:B10\"><formula1>\"A,B,C\"</formula1></dataValidation>",
        "prompt-only list validation XML mismatch");
    check_contains(worksheet_xml,
        "<dataValidation type=\"decimal\" showErrorMessage=\"1\" errorStyle=\"information\" "
        "errorTitle=\"Decimal\" error=\"Use a positive decimal\" operator=\"greaterThan\" "
        "sqref=\"C2:C10\"><formula1>0</formula1></dataValidation>",
        "error-only decimal validation XML mismatch");
    check_contains(worksheet_xml,
        "<dataValidation type=\"custom\" errorStyle=\"stop\" sqref=\"D2:D10\">"
        "<formula1>LEN(D2)&gt;0</formula1></dataValidation>",
        "stop-style custom validation XML mismatch");
    check(worksheet_xml.find("showInputMessage=\"0\"") == std::string::npos,
        "false showInputMessage should be omitted");
    check(worksheet_xml.find("showErrorMessage=\"0\"") == std::string::npos,
        "false showErrorMessage should be omitted");
    check(worksheet_xml.find("promptTitle=\"\"") == std::string::npos,
        "empty promptTitle should be omitted");
    check(worksheet_xml.find("prompt=\"\"") == std::string::npos,
        "empty prompt should be omitted");
    check(worksheet_xml.find("errorTitle=\"\"") == std::string::npos,
        "empty errorTitle should be omitted");
    check(worksheet_xml.find("error=\"\"") == std::string::npos,
        "empty error should be omitted");
    check(count_occurrences(worksheet_xml, "<dataValidation ") == 4,
        "prompt/error dataValidation item count mismatch");
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

void test_streaming_writer_internal_hyperlinks()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-internal-hyperlinks.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto internal_only = workbook.add_worksheet("Internal");
    auto target = workbook.add_worksheet("Target & <Sheet>");
    auto mixed = workbook.add_worksheet("Mixed");
    auto plain = workbook.add_worksheet("Plain");

    internal_only.append_row({fastxlsx::CellView::text("Jump to target")});
    internal_only.append_row({fastxlsx::CellView::text("Second jump")});
    internal_only.add_internal_hyperlink(1, 1, "'Target & <Sheet>'!A1");
    internal_only.add_internal_hyperlink(2, 1, "'Target & <Sheet>'!B2:\"quoted\"");

    target.append_row({fastxlsx::CellView::text("Target cell")});
    target.append_row({fastxlsx::CellView::text("Second target")});

    mixed.append_row({
        fastxlsx::CellView::text("External"),
        fastxlsx::CellView::text("Internal"),
    });
    mixed.append_row({fastxlsx::CellView::text("External 2")});
    mixed.add_external_hyperlink(1, 1, "https://example.com/");
    mixed.add_internal_hyperlink(1, 2, "'Target & <Sheet>'!A1");
    mixed.add_external_hyperlink(2, 1, "https://example.com/more");

    plain.append_row({fastxlsx::CellView::text("No hyperlink sheet")});

    workbook.close();
    check(std::filesystem::exists(output_path), "internal hyperlinks xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing internal hyperlink worksheet");
    check(entries.contains("xl/worksheets/sheet2.xml"), "missing internal hyperlink target worksheet");
    check(entries.contains("xl/worksheets/sheet3.xml"), "missing mixed hyperlink worksheet");
    check(entries.contains("xl/worksheets/sheet4.xml"), "missing plain hyperlink worksheet");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "internal-only hyperlinks should not create worksheet relationships");
    check(!entries.contains("xl/worksheets/_rels/sheet2.xml.rels"),
        "target sheet without relationships should not create worksheet relationships");
    check(entries.contains("xl/worksheets/_rels/sheet3.xml.rels"),
        "mixed sheet should keep external hyperlink relationships");
    check(!entries.contains("xl/worksheets/_rels/sheet4.xml.rels"),
        "plain sheet should not create worksheet relationships");
    check(!entries.contains("xl/metadata.xml"), "internal hyperlinks should not create metadata part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("hyperlink") == std::string::npos,
        "internal hyperlinks should not add content type overrides");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(count_occurrences(workbook_rels, "<Relationship ") == 4,
        "internal hyperlinks should not add workbook relationships");

    const auto& internal_sheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(internal_sheet_xml.find("xmlns:r=") == std::string::npos,
        "internal-only worksheet should not declare relationship namespace");
    check_contains(internal_sheet_xml,
        "<c r=\"A1\" t=\"inlineStr\"><is><t>Jump to target</t></is></c>",
        "internal hyperlink should not replace A1 cell text");
    check_contains(internal_sheet_xml,
        "</sheetData><hyperlinks>"
        "<hyperlink ref=\"A1\" location=\"&apos;Target &amp; &lt;Sheet&gt;&apos;!A1\"/>"
        "<hyperlink ref=\"A2\" location=\"&apos;Target &amp; &lt;Sheet&gt;&apos;!B2:&quot;quoted&quot;\"/>"
        "</hyperlinks></worksheet>",
        "internal-only hyperlink XML mismatch or location escaping failure");
    check(count_occurrences(internal_sheet_xml, "<hyperlink ") == 2,
        "internal-only worksheet hyperlink count mismatch");
    check(internal_sheet_xml.find("r:id=") == std::string::npos,
        "internal-only hyperlinks should not use relationship ids");

    const auto& mixed_sheet_xml = entries.at("xl/worksheets/sheet3.xml");
    check_contains(mixed_sheet_xml,
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)",
        "mixed hyperlink worksheet namespace mismatch");
    check_contains(mixed_sheet_xml,
        "<hyperlinks><hyperlink ref=\"A1\" r:id=\"rId1\"/>"
        "<hyperlink ref=\"A2\" r:id=\"rId2\"/>"
        "<hyperlink ref=\"B1\" location=\"&apos;Target &amp; &lt;Sheet&gt;&apos;!A1\"/></hyperlinks>",
        "mixed external/internal hyperlink XML mismatch");
    check(count_occurrences(mixed_sheet_xml, "<hyperlink ") == 3,
        "mixed worksheet hyperlink count mismatch");

    const auto& mixed_sheet_rels = entries.at("xl/worksheets/_rels/sheet3.xml.rels");
    check(count_occurrences(mixed_sheet_rels, "<Relationship ") == 2,
        "internal hyperlinks should not create relationship entries");
    check_contains(mixed_sheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/" TargetMode="External"/>)",
        "first mixed external hyperlink relationship mismatch");
    check_contains(mixed_sheet_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/more" TargetMode="External"/>)",
        "second mixed external hyperlink relationship mismatch");
    check(mixed_sheet_rels.find("Target &amp; &lt;Sheet&gt;") == std::string::npos,
        "internal hyperlink location should not be written to worksheet relationships");

    const auto& plain_sheet_xml = entries.at("xl/worksheets/sheet4.xml");
    check(plain_sheet_xml.find("<hyperlinks>") == std::string::npos,
        "plain worksheet should not contain hyperlinks");
    check(plain_sheet_xml.find("xmlns:r=") == std::string::npos,
        "plain worksheet should not include relationship namespace");
}

void test_streaming_writer_internal_hyperlink_with_table_relationship_id()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-internal-hyperlink-table-rels.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("Objects");
    auto target = workbook.add_worksheet("Target");

    sheet.append_row({
        fastxlsx::CellView::text("Name"),
        fastxlsx::CellView::text("Qty"),
    });
    sheet.append_row({
        fastxlsx::CellView::text("Widget"),
        fastxlsx::CellView::number(7.0),
    });
    sheet.add_internal_hyperlink(2, 1, "Target!A1");

    fastxlsx::TableOptions table;
    table.name = "InternalLinkTable";
    table.column_names = {"Name", "Qty"};
    sheet.add_table({1, 1, 2, 2}, table);

    target.append_row({fastxlsx::CellView::text("Destination")});

    workbook.close();
    check(std::filesystem::exists(output_path),
        "internal hyperlink + table xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "table should still create worksheet relationships");
    check(!entries.contains("xl/worksheets/_rels/sheet2.xml.rels"),
        "target sheet should not create worksheet relationships");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        "<hyperlinks><hyperlink ref=\"A2\" location=\"Target!A1\"/></hyperlinks>"
        "<tableParts count=\"1\"><tablePart r:id=\"rId1\"/></tableParts>",
        "internal hyperlink should not shift table relationship id");

    const auto& worksheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check(count_occurrences(worksheet_rels, "<Relationship ") == 1,
        "internal hyperlink should not create a relationship next to table");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)",
        "table relationship should remain rId1 when only internal hyperlinks precede it");
    check(worksheet_rels.find("hyperlink") == std::string::npos,
        "internal hyperlink should not create hyperlink relationship entries");
}

void test_streaming_writer_hyperlink_display_tooltips()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-hyperlink-display-tooltips.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto external = workbook.add_worksheet("ExternalAttrs");
    auto internal = workbook.add_worksheet("InternalAttrs");
    auto target = workbook.add_worksheet("Target");

    external.append_row({
        fastxlsx::CellView::text("External both"),
        fastxlsx::CellView::text("External display"),
        fastxlsx::CellView::text("External tooltip"),
    });
    fastxlsx::HyperlinkOptions external_both;
    external_both.display = "Open & <Docs> \"Q\" 'A'";
    external_both.tooltip = "External tip & <more> \"Q\" 'A'";
    external.add_external_hyperlink(1, 1, "https://example.com/both", external_both);
    fastxlsx::HyperlinkOptions external_display;
    external_display.display = "Display only";
    external.add_external_hyperlink(1, 2, "https://example.com/display", external_display);
    fastxlsx::HyperlinkOptions external_tooltip;
    external_tooltip.tooltip = "Tooltip only";
    external.add_external_hyperlink(1, 3, "https://example.com/tooltip", external_tooltip);

    internal.append_row({
        fastxlsx::CellView::text("Internal both"),
        fastxlsx::CellView::text("Internal display"),
        fastxlsx::CellView::text("Internal tooltip"),
        fastxlsx::CellView::text("Internal empty options"),
    });
    fastxlsx::HyperlinkOptions internal_both;
    internal_both.display = "Jump & <Target> \"Q\" 'A'";
    internal_both.tooltip = "Internal tip & <more> \"Q\" 'A'";
    internal.add_internal_hyperlink(1, 1, "Target!A1", internal_both);
    fastxlsx::HyperlinkOptions internal_display;
    internal_display.display = "Internal display only";
    internal.add_internal_hyperlink(1, 2, "Target!A2", internal_display);
    fastxlsx::HyperlinkOptions internal_tooltip;
    internal_tooltip.tooltip = "Internal tooltip only";
    internal.add_internal_hyperlink(1, 3, "Target!A3", internal_tooltip);
    internal.add_internal_hyperlink(1, 4, "Target!D4", fastxlsx::HyperlinkOptions {});

    target.append_row({
        fastxlsx::CellView::text("A1"),
        fastxlsx::CellView::text("A2"),
        fastxlsx::CellView::text("A3"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path),
        "hyperlink display/tooltip xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "external display/tooltip hyperlinks should keep worksheet relationships");
    check(!entries.contains("xl/worksheets/_rels/sheet2.xml.rels"),
        "internal display/tooltip hyperlinks should not create worksheet relationships");
    check(!entries.contains("xl/worksheets/_rels/sheet3.xml.rels"),
        "target sheet should not create worksheet relationships");

    const auto& external_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(external_xml,
        "<hyperlinks><hyperlink ref=\"A1\" r:id=\"rId1\" "
        "display=\"Open &amp; &lt;Docs&gt; &quot;Q&quot; &apos;A&apos;\" "
        "tooltip=\"External tip &amp; &lt;more&gt; &quot;Q&quot; &apos;A&apos;\"/>"
        "<hyperlink ref=\"B1\" r:id=\"rId2\" display=\"Display only\"/>"
        "<hyperlink ref=\"C1\" r:id=\"rId3\" tooltip=\"Tooltip only\"/></hyperlinks>",
        "external hyperlink display/tooltip XML mismatch");
    check_contains(external_xml,
        "<c r=\"A1\" t=\"inlineStr\"><is><t>External both</t></is></c>",
        "external hyperlink display should not replace cell text");

    const auto& external_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check(count_occurrences(external_rels, "<Relationship ") == 3,
        "external display/tooltip hyperlinks should only create URL relationships");
    check(external_rels.find("display=") == std::string::npos,
        "display attribute should not be written to worksheet relationships");
    check(external_rels.find("tooltip=") == std::string::npos,
        "tooltip attribute should not be written to worksheet relationships");

    const auto& internal_xml = entries.at("xl/worksheets/sheet2.xml");
    check(internal_xml.find("xmlns:r=") == std::string::npos,
        "internal display/tooltip worksheet should not declare relationship namespace");
    check(internal_xml.find("r:id=") == std::string::npos,
        "internal display/tooltip hyperlinks should not use relationship ids");
    check_contains(internal_xml,
        "<hyperlinks><hyperlink ref=\"A1\" location=\"Target!A1\" "
        "display=\"Jump &amp; &lt;Target&gt; &quot;Q&quot; &apos;A&apos;\" "
        "tooltip=\"Internal tip &amp; &lt;more&gt; &quot;Q&quot; &apos;A&apos;\"/>"
        "<hyperlink ref=\"B1\" location=\"Target!A2\" display=\"Internal display only\"/>"
        "<hyperlink ref=\"C1\" location=\"Target!A3\" tooltip=\"Internal tooltip only\"/>"
        "<hyperlink ref=\"D1\" location=\"Target!D4\"/></hyperlinks>",
        "internal hyperlink display/tooltip XML mismatch");
    check(internal_xml.find("display=\"\"") == std::string::npos,
        "explicit empty display should be omitted");
    check(internal_xml.find("tooltip=\"\"") == std::string::npos,
        "explicit empty tooltip should be omitted");
    check_contains(internal_xml,
        "<c r=\"A1\" t=\"inlineStr\"><is><t>Internal both</t></is></c>",
        "internal hyperlink display should not replace cell text");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("hyperlink") == std::string::npos,
        "hyperlink display/tooltip should not add content type overrides");
    check(content_types.find("styles.xml") == std::string::npos,
        "hyperlink display/tooltip should not create styles");
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

void test_streaming_writer_table_style_flags()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-table-style-flags.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("StyleFlags");

    sheet.append_row({fastxlsx::CellView::text("Name"), fastxlsx::CellView::text("Value")});
    sheet.append_row({fastxlsx::CellView::text("A"), fastxlsx::CellView::number(1.0)});

    fastxlsx::TableOptions table;
    table.name = "StyleFlagTable";
    table.column_names = {"Name", "Value"};
    table.style_name = "TableStyleMedium4";
    table.show_first_column = true;
    table.show_last_column = true;
    table.show_row_stripes = false;
    table.show_column_stripes = true;
    sheet.add_table({1, 1, 2, 2}, table);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(!entries.contains("xl/styles.xml"),
        "table style flags should not create a styles part");
    check(entries.contains("xl/tables/table1.xml"),
        "table style flags should create a table part");

    const auto& table_xml = entries.at("xl/tables/table1.xml");
    check_contains(table_xml,
        R"(<tableStyleInfo name="TableStyleMedium4" showFirstColumn="1" showLastColumn="1" showRowStripes="0" showColumnStripes="1"/>)",
        "table style flags XML mismatch");
}

void test_streaming_writer_table_column_attribute_escaping()
{
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-table-column-escape.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("TableEscapes");

    sheet.append_row({
        fastxlsx::CellView::text("Text \"quoted\""),
        fastxlsx::CellView::text("Owner's Share"),
        fastxlsx::CellView::text("A&B<Limit>"),
    });
    sheet.append_row({
        fastxlsx::CellView::text("alpha"),
        fastxlsx::CellView::number(42.0),
        fastxlsx::CellView::text("done"),
    });

    fastxlsx::TableOptions table;
    table.name = "EscapedColumnTable";
    table.column_names = {"Text \"quoted\"", "Owner's Share", "A&B<Limit>"};
    table.style_name.clear();
    sheet.add_table({1, 1, 2, 3}, table);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/tables/table1.xml"),
        "table column escape test should create a table part");
    check(!entries.contains("xl/styles.xml"),
        "table column escape test should not create a styles part");

    const auto& table_xml = entries.at("xl/tables/table1.xml");
    check_contains(table_xml,
        R"(<tableColumn id="1" name="Text &quot;quoted&quot;"/>)",
        "table column double-quote attribute escape mismatch");
    check_contains(table_xml,
        R"(<tableColumn id="2" name="Owner&apos;s Share"/>)",
        "table column apostrophe attribute escape mismatch");
    check_contains(table_xml,
        R"(<tableColumn id="3" name="A&amp;B&lt;Limit&gt;"/>)",
        "table column ampersand and angle-bracket attribute escape mismatch");
}

void test_streaming_writer_images()
{
    const auto image_path = std::filesystem::current_path() / "fastxlsx-streaming-image-source.png";
    write_bytes(image_path, fastxlsx::test::tiny_png_bytes());

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
    check(entries.at("xl/media/image1.png").size() == fastxlsx::test::tiny_rgba_png().size(),
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
        R"(<xdr:cNvPr id="1" name="Picture 1"/>)",
        "default drawing picture name mismatch");
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
    const auto& second_drawing_xml = entries.at("xl/drawings/drawing2.xml");
    check_contains(second_drawing_xml,
        R"(<xdr:cNvPr id="2" name="Picture 2"/>)",
        "default drawing picture name should follow workbook image index");
#else
    auto workbook = fastxlsx::WorkbookWriter::create(
        std::filesystem::current_path() / "fastxlsx-streaming-images-disabled.xlsx");
    auto sheet = workbook.add_worksheet("Images");
    check_fastxlsx_error(
        [&sheet, &image_path] { sheet.add_image(image_path, {1, 1, 1, 1}); },
        "streaming add_image should require opt-in stb support");
#endif
}

void test_streaming_writer_image_metadata()
{
    const auto image_path =
        std::filesystem::current_path() / "fastxlsx-streaming-image-metadata-source.png";
    write_bytes(image_path, fastxlsx::test::tiny_png_bytes());

#ifdef FASTXLSX_TEST_HAS_STB
    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-image-metadata.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("ImageMetadata");

    fastxlsx::ImageOptions escaped_options;
    escaped_options.name = R"(Logo "A&B<1>')";
    escaped_options.description = R"(Alt "quoted" & <tag> 'owner')";
    sheet.add_image(image_path, {1, 1, 2, 2}, escaped_options);

    fastxlsx::ImageOptions named_only;
    named_only.name = "NamedOnly";
    sheet.add_image(image_path, {3, 1, 4, 2}, named_only);

    sheet.add_image(image_path, {5, 1, 6, 2});
    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/media/image1.png"), "missing first image metadata media part");
    check(entries.contains("xl/media/image2.png"), "missing second image metadata media part");
    check(entries.contains("xl/media/image3.png"), "missing third image metadata media part");
    check(entries.contains("xl/drawings/drawing1.xml"), "missing image metadata drawing part");
    check(!entries.contains("xl/drawings/drawing2.xml"),
        "image metadata images on one sheet should share one drawing part");

    const auto& drawing_xml = entries.at("xl/drawings/drawing1.xml");
    check(count_occurrences(drawing_xml, "<xdr:twoCellAnchor") == 3,
        "image metadata drawing anchor count mismatch");
    check_contains(drawing_xml,
        R"(<xdr:cNvPr id="1" name="Logo &quot;A&amp;B&lt;1&gt;&apos;" descr="Alt &quot;quoted&quot; &amp; &lt;tag&gt; &apos;owner&apos;"/>)",
        "image metadata name/description attribute escape mismatch");
    check_contains(drawing_xml,
        R"(<xdr:cNvPr id="2" name="NamedOnly"/>)",
        "image metadata named-only picture mismatch");
    check_contains(drawing_xml,
        R"(<xdr:cNvPr id="3" name="Picture 3"/>)",
        "image metadata default picture name mismatch");
    check(drawing_xml.find(R"(name="NamedOnly" descr=)") == std::string::npos,
        "empty image metadata description should be omitted");
    check(drawing_xml.find(R"(name="Picture 3" descr=)") == std::string::npos,
        "default image metadata description should be omitted");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        "</sheetData><drawing r:id=\"rId1\"/></worksheet>",
        "image metadata worksheet drawing relationship mismatch");

    const auto& worksheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check(count_occurrences(worksheet_rels, "<Relationship ") == 1,
        "image metadata should only create one worksheet drawing relationship");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing1.xml"/>)",
        "image metadata worksheet relationship target mismatch");

    const auto& drawing_rels = entries.at("xl/drawings/_rels/drawing1.xml.rels");
    check(count_occurrences(drawing_rels, "<Relationship ") == 3,
        "image metadata drawing relationship count mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image1.png"/>)",
        "image metadata first drawing relationship mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image2.png"/>)",
        "image metadata second drawing relationship mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image3.png"/>)",
        "image metadata third drawing relationship mismatch");
#else
    auto workbook = fastxlsx::WorkbookWriter::create(
        std::filesystem::current_path() / "fastxlsx-streaming-image-metadata-disabled.xlsx");
    auto sheet = workbook.add_worksheet("ImageMetadata");
    fastxlsx::ImageOptions options;
    options.name = "NoStb";
    options.description = "Still metadata only";
    check_fastxlsx_error(
        [&sheet, &image_path, &options] { sheet.add_image(image_path, {1, 1, 1, 1}, options); },
        "streaming add_image metadata should require opt-in stb support");
#endif
}

void test_streaming_writer_jpeg_images()
{
#ifdef FASTXLSX_TEST_HAS_STB
    const auto image_path = std::filesystem::current_path() / "fastxlsx-streaming-image-source.jpg";
    write_bytes(image_path, fastxlsx::test::tiny_jpeg_bytes());
    const auto output_path = std::filesystem::current_path() / "fastxlsx-streaming-jpeg-images.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("JpegImage");
    sheet.append_row({fastxlsx::CellView::text("jpeg")});
    sheet.add_image(image_path, {1, 1, 2, 2});
    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/media/image1.jpg"), "missing JPEG media part");
    check(entries.at("xl/media/image1.jpg").size() == fastxlsx::test::tiny_rgb_jpeg_header().size(),
        "JPEG media part byte size mismatch");

    const auto& content_types = entries.at("[Content_Types].xml");
    check_contains(content_types,
        R"(<Default Extension="jpg" ContentType="image/jpeg"/>)",
        "JPEG content type default missing");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        "</sheetData><drawing r:id=\"rId1\"/></worksheet>",
        "JPEG worksheet drawing relationship id mismatch");

    const auto& drawing_xml = entries.at("xl/drawings/drawing1.xml");
    check_contains(drawing_xml,
        R"(<a:ext cx="19050" cy="9525"/>)",
        "JPEG intrinsic EMU size mismatch");

    const auto& drawing_rels = entries.at("xl/drawings/_rels/drawing1.xml.rels");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image1.jpg"/>)",
        "JPEG drawing image relationship mismatch");
#endif
}

void test_streaming_writer_mixed_image_formats()
{
#ifdef FASTXLSX_TEST_HAS_STB
    const auto png_path = std::filesystem::current_path() / "fastxlsx-streaming-mixed-image-source.png";
    const auto jpeg_path = std::filesystem::current_path() / "fastxlsx-streaming-mixed-image-source.jpg";
    write_bytes(png_path, fastxlsx::test::tiny_png_bytes());
    write_bytes(jpeg_path, fastxlsx::test::tiny_jpeg_bytes());

    const auto output_path = std::filesystem::current_path() / "fastxlsx-streaming-mixed-images.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("MixedImages");
    sheet.append_row({fastxlsx::CellView::text("mixed")});
    sheet.add_image(png_path, {1, 1, 1, 1});
    sheet.add_image(jpeg_path, {2, 2, 3, 3});
    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/media/image1.png"), "missing mixed PNG media part");
    check(entries.contains("xl/media/image2.jpg"), "missing mixed JPEG media part");
    check(!entries.contains("xl/drawings/drawing2.xml"), "mixed image formats should share worksheet drawing");
    check(entries.at("xl/media/image1.png").size() == fastxlsx::test::tiny_rgba_png().size(),
        "mixed PNG media part byte size mismatch");
    check(entries.at("xl/media/image2.jpg").size() == fastxlsx::test::tiny_rgb_jpeg_header().size(),
        "mixed JPEG media part byte size mismatch");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(count_occurrences(content_types,
              R"(<Default Extension="png" ContentType="image/png"/>)")
            == 1,
        "mixed image workbook should write one PNG content type default");
    check(count_occurrences(content_types,
              R"(<Default Extension="jpg" ContentType="image/jpeg"/>)")
            == 1,
        "mixed image workbook should write one JPEG content type default");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        "</sheetData><drawing r:id=\"rId1\"/></worksheet>",
        "mixed image worksheet drawing relationship id mismatch");

    const auto& drawing_xml = entries.at("xl/drawings/drawing1.xml");
    check(count_occurrences(drawing_xml, "<xdr:twoCellAnchor") == 2,
        "mixed image drawing anchor count mismatch");
    check_contains(drawing_xml,
        R"(<a:blip r:embed="rId1"/>)",
        "mixed PNG drawing relationship id mismatch");
    check_contains(drawing_xml,
        R"(<a:blip r:embed="rId2"/>)",
        "mixed JPEG drawing relationship id mismatch");
    check_contains(drawing_xml,
        R"(<a:ext cx="9525" cy="9525"/>)",
        "mixed PNG intrinsic EMU size mismatch");
    check_contains(drawing_xml,
        R"(<a:ext cx="19050" cy="9525"/>)",
        "mixed JPEG intrinsic EMU size mismatch");

    const auto& drawing_rels = entries.at("xl/drawings/_rels/drawing1.xml.rels");
    check(count_occurrences(drawing_rels, "<Relationship ") == 2,
        "mixed image drawing relationship count mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image1.png"/>)",
        "mixed PNG drawing image relationship mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image2.jpg"/>)",
        "mixed JPEG drawing image relationship mismatch");
#endif
}

void test_streaming_writer_image_anchor_markers()
{
#ifdef FASTXLSX_TEST_HAS_STB
    const auto image_path = std::filesystem::current_path() / "fastxlsx-streaming-anchor-image-source.png";
    write_bytes(image_path, fastxlsx::test::tiny_png_bytes());

    const auto output_path = std::filesystem::current_path() / "fastxlsx-streaming-image-anchors.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("Anchors");
    sheet.add_image(image_path, {1, 1, 1, 1});
    sheet.add_image(image_path, {1048576, 16384, 1048576, 16384});
    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& drawing_xml = entries.at("xl/drawings/drawing1.xml");
    check(count_occurrences(drawing_xml, "<xdr:twoCellAnchor") == 2,
        "image anchor marker test should write two anchors");
    check_contains(drawing_xml,
        "<xdr:from><xdr:col>0</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>0</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:from>",
        "single-cell image from marker mismatch");
    check_contains(drawing_xml,
        "<xdr:to><xdr:col>1</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>1</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:to>",
        "single-cell image to marker mismatch");
    check_contains(drawing_xml,
        "<xdr:from><xdr:col>16383</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>1048575</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:from>",
        "max-cell image from marker mismatch");
    check_contains(drawing_xml,
        "<xdr:to><xdr:col>16384</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>1048576</xdr:row><xdr:rowOff>0</xdr:rowOff></xdr:to>",
        "max-cell image to marker mismatch");
#endif
}

void test_streaming_writer_mixed_object_relationship_ids()
{
#ifdef FASTXLSX_TEST_HAS_STB
    const auto png_path = std::filesystem::current_path() / "fastxlsx-streaming-object-rels-source.png";
    const auto jpeg_path = std::filesystem::current_path() / "fastxlsx-streaming-object-rels-source.jpg";
    write_bytes(png_path, fastxlsx::test::tiny_png_bytes());
    write_bytes(jpeg_path, fastxlsx::test::tiny_jpeg_bytes());

    const auto output_path =
        std::filesystem::current_path() / "fastxlsx-streaming-mixed-object-rels.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto first = workbook.add_worksheet("Objects");
    auto second = workbook.add_worksheet("MoreObjects");
    auto plain = workbook.add_worksheet("Plain");

    first.append_row({fastxlsx::CellView::text("Name"), fastxlsx::CellView::text("Link")});
    first.append_row({fastxlsx::CellView::text("Widget"), fastxlsx::CellView::text("Docs")});
    first.append_row({fastxlsx::CellView::text("Kind"), fastxlsx::CellView::text("Value")});
    first.append_row({fastxlsx::CellView::text("Image"), fastxlsx::CellView::text("Two")});
    first.add_external_hyperlink(2, 1, "https://example.com/widget");
    first.add_external_hyperlink(2, 2, "https://example.com/docs");
    first.add_image(png_path, {1, 3, 2, 3});
    first.add_image(jpeg_path, {3, 3, 4, 3});

    fastxlsx::TableOptions first_table;
    first_table.name = "ObjectTableOne";
    first_table.column_names = {"Name", "Link"};
    first.add_table({1, 1, 2, 2}, first_table);

    fastxlsx::TableOptions second_table;
    second_table.name = "ObjectTableTwo";
    second_table.column_names = {"Kind", "Value"};
    first.add_table({3, 1, 4, 2}, second_table);

    second.append_row({fastxlsx::CellView::text("Name"), fastxlsx::CellView::text("Link")});
    second.append_row({fastxlsx::CellView::text("Gadget"), fastxlsx::CellView::text("More")});
    second.add_external_hyperlink(2, 1, "https://example.com/gadget");
    second.add_image(png_path, {1, 3, 2, 3});

    fastxlsx::TableOptions third_table;
    third_table.name = "ObjectTableThree";
    third_table.column_names = {"Name", "Link"};
    second.add_table({1, 1, 2, 2}, third_table);

    plain.append_row({fastxlsx::CellView::text("No object relationships")});

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/media/image1.png"), "missing first object media part");
    check(entries.contains("xl/media/image2.jpg"), "missing second object media part");
    check(entries.contains("xl/media/image3.png"), "missing third object media part");
    check(entries.contains("xl/drawings/drawing1.xml"), "missing first object drawing part");
    check(entries.contains("xl/drawings/drawing2.xml"), "missing second object drawing part");
    check(!entries.contains("xl/drawings/drawing3.xml"), "plain sheet should not create a drawing part");
    check(entries.contains("xl/tables/table1.xml"), "missing first object table part");
    check(entries.contains("xl/tables/table2.xml"), "missing second object table part");
    check(entries.contains("xl/tables/table3.xml"), "missing third object table part");
    check(!entries.contains("xl/worksheets/_rels/sheet3.xml.rels"),
        "plain sheet should not create object relationships");

    const auto& first_sheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_sheet_xml,
        "</sheetData><hyperlinks><hyperlink ref=\"A2\" r:id=\"rId1\"/>"
        "<hyperlink ref=\"B2\" r:id=\"rId2\"/></hyperlinks><drawing r:id=\"rId3\"/>"
        "<tableParts count=\"2\"><tablePart r:id=\"rId4\"/><tablePart r:id=\"rId5\"/></tableParts></worksheet>",
        "first object sheet relationship ids should be hyperlink, drawing, then tables");

    const auto& first_sheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check(count_occurrences(first_sheet_rels, "<Relationship ") == 5,
        "first object sheet relationship count mismatch");
    check_contains(first_sheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/widget" TargetMode="External"/>)",
        "first object hyperlink relationship mismatch");
    check_contains(first_sheet_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/docs" TargetMode="External"/>)",
        "second object hyperlink relationship mismatch");
    check_contains(first_sheet_rels,
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing1.xml"/>)",
        "first object drawing relationship mismatch");
    check_contains(first_sheet_rels,
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)",
        "first object table relationship mismatch");
    check_contains(first_sheet_rels,
        R"(<Relationship Id="rId5" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table2.xml"/>)",
        "second object table relationship mismatch");

    const auto& first_drawing_rels = entries.at("xl/drawings/_rels/drawing1.xml.rels");
    check(count_occurrences(first_drawing_rels, "<Relationship ") == 2,
        "first object drawing relationship count mismatch");
    check_contains(first_drawing_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image1.png"/>)",
        "first object PNG drawing relationship mismatch");
    check_contains(first_drawing_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image2.jpg"/>)",
        "first object JPEG drawing relationship mismatch");

    const auto& second_sheet_xml = entries.at("xl/worksheets/sheet2.xml");
    check_contains(second_sheet_xml,
        "</sheetData><hyperlinks><hyperlink ref=\"A2\" r:id=\"rId1\"/></hyperlinks>"
        "<drawing r:id=\"rId2\"/><tableParts count=\"1\"><tablePart r:id=\"rId3\"/></tableParts></worksheet>",
        "second object sheet relationship ids should reset per worksheet owner");

    const auto& second_sheet_rels = entries.at("xl/worksheets/_rels/sheet2.xml.rels");
    check(count_occurrences(second_sheet_rels, "<Relationship ") == 3,
        "second object sheet relationship count mismatch");
    check_contains(second_sheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/gadget" TargetMode="External"/>)",
        "second sheet hyperlink relationship mismatch");
    check_contains(second_sheet_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing2.xml"/>)",
        "second sheet drawing relationship mismatch");
    check_contains(second_sheet_rels,
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table3.xml"/>)",
        "second sheet table relationship mismatch");

    const auto& second_drawing_rels = entries.at("xl/drawings/_rels/drawing2.xml.rels");
    check_contains(second_drawing_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image3.png"/>)",
        "second drawing relationship id should reset for its drawing owner");
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
    check_fastxlsx_error(
        [&sheet] { sheet.add_internal_hyperlink(1, 1, "Sheet1!A1"); },
        "add_internal_hyperlink should reject mutation after workbook close");
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
        [&sheet, &list] {
            const std::span<const fastxlsx::CellRange> no_ranges;
            sheet.add_data_validation(no_ranges, list);
        },
        "dataValidations should reject an empty multi-range list");
    check_fastxlsx_error(
        [&sheet, &list] {
            sheet.add_data_validation({{1, 1, 1, 1}, {1, 0, 1, 1}}, list);
        },
        "dataValidations should reject an invalid range inside a multi-range list");

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
    check_fastxlsx_error(
        [&sheet] { sheet.add_internal_hyperlink(0, 1, "Sheet1!A1"); },
        "internal hyperlinks should reject a zero row");
    check_fastxlsx_error(
        [&sheet] { sheet.add_internal_hyperlink(1, 0, "Sheet1!A1"); },
        "internal hyperlinks should reject a zero column");
    check_fastxlsx_error(
        [&sheet] { sheet.add_internal_hyperlink(1048577, 1, "Sheet1!A1"); },
        "internal hyperlinks should reject a row beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet] { sheet.add_internal_hyperlink(1, 16385, "Sheet1!A1"); },
        "internal hyperlinks should reject a column beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet] { sheet.add_internal_hyperlink(1, 1, ""); },
        "internal hyperlinks should reject an empty location");

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
        [&sheet] { sheet.set_column_width(1, 1, -std::numeric_limits<double>::infinity()); },
        "set_column_width should reject a negative infinite width");

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
                {fastxlsx::CellView::number(-std::numeric_limits<double>::infinity())});
        },
        "append_row should reject a negative infinite numeric cell");
    check_fastxlsx_error(
        [&sheet] {
            sheet.append_row(
                {fastxlsx::CellView::text("bad height")},
                fastxlsx::RowOptions {0.0});
        },
        "append_row should reject a zero row height");
    check_fastxlsx_error(
        [&sheet] {
            sheet.append_row(
                {fastxlsx::CellView::text("bad height")},
                fastxlsx::RowOptions {-1.0});
        },
        "append_row should reject a negative row height");
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
    check_fastxlsx_error(
        [&sheet] {
            sheet.append_row(
                {fastxlsx::CellView::text("bad height")},
                fastxlsx::RowOptions {-std::numeric_limits<double>::infinity()});
        },
        "append_row should reject a negative infinite row height");

    std::vector<fastxlsx::CellView> too_wide_row(16385, fastxlsx::CellView::number(1.0));
    check_fastxlsx_error(
        [&sheet, &too_wide_row] {
            sheet.append_row(std::span<const fastxlsx::CellView>(
                too_wide_row.data(), too_wide_row.size()));
        },
        "append_row should reject rows beyond Excel's column limit");

    fastxlsx::detail::testing_set_worksheet_row_count(sheet, 1048576);
    check_fastxlsx_error(
        [&sheet] { sheet.append_row({}); },
        "append_row should reject rows beyond Excel's row limit");
}

} // namespace

int main()
{
    try {
        test_streaming_writer_smoke_package();
        test_streaming_writer_document_properties();
        test_streaming_writer_empty_rows_dimension();
        test_streaming_writer_phase3_metadata_structure();
        test_streaming_writer_file_backed_body_round_trip();
        test_streaming_writer_data_validations();
        test_streaming_writer_data_validation_multi_range_sqref();
        test_streaming_writer_data_validations_with_relationship_metadata();
        test_streaming_writer_data_validation_formula2_escape_and_namespace();
        test_streaming_writer_data_validation_prompt_error_metadata();
        test_streaming_writer_external_hyperlinks();
        test_streaming_writer_internal_hyperlinks();
        test_streaming_writer_internal_hyperlink_with_table_relationship_id();
        test_streaming_writer_hyperlink_display_tooltips();
        test_streaming_writer_tables();
        test_streaming_writer_table_style_flags();
        test_streaming_writer_table_column_attribute_escaping();
        test_streaming_writer_images();
        test_streaming_writer_image_metadata();
        test_streaming_writer_jpeg_images();
        test_streaming_writer_mixed_image_formats();
        test_streaming_writer_image_anchor_markers();
        test_streaming_writer_mixed_object_relationship_ids();
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
