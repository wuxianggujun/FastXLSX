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

bool bytes_equal(std::string_view text, std::span<const std::byte> bytes)
{
    if (text.size() != bytes.size()) {
        return false;
    }
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (static_cast<unsigned char>(text[index])
            != std::to_integer<unsigned char>(bytes[index])) {
            return false;
        }
    }
    return true;
}

fastxlsx::TwoColorScaleRule make_two_color_scale(
    fastxlsx::ArgbColor lower, fastxlsx::ArgbColor upper)
{
    fastxlsx::TwoColorScaleRule rule;
    rule.lower = {fastxlsx::ColorScaleValueType::Minimum, 0.0, lower};
    rule.upper = {fastxlsx::ColorScaleValueType::Maximum, 0.0, upper};
    return rule;
}

fastxlsx::ThreeColorScaleRule make_three_color_scale(
    fastxlsx::ArgbColor lower, fastxlsx::ArgbColor midpoint, fastxlsx::ArgbColor upper)
{
    fastxlsx::ThreeColorScaleRule rule;
    rule.lower = {fastxlsx::ColorScaleValueType::Minimum, 0.0, lower};
    rule.midpoint = {fastxlsx::ColorScaleValueType::Percentile, 50.0, midpoint};
    rule.upper = {fastxlsx::ColorScaleValueType::Maximum, 0.0, upper};
    return rule;
}

fastxlsx::DataBarRule make_data_bar(fastxlsx::ArgbColor color)
{
    fastxlsx::DataBarRule rule;
    rule.lower = {fastxlsx::DataBarValueType::Minimum, 0.0};
    rule.upper = {fastxlsx::DataBarValueType::Maximum, 0.0};
    rule.color = color;
    return rule;
}

fastxlsx::IconSetRule make_icon_set()
{
    fastxlsx::IconSetRule rule;
    rule.style = fastxlsx::IconSetStyle::ThreeArrows;
    rule.value_type = fastxlsx::IconSetValueType::Percent;
    rule.thresholds = {0.0, 33.0, 67.0};
    return rule;
}

void test_streaming_writer_smoke_package()
{
    const auto output_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-smoke.xlsx";

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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-document-properties.xlsx";

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

void test_streaming_writer_zip_compression_level_options()
{
    const auto stored_output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-compression-level-0.xlsx";

    fastxlsx::WorkbookWriterOptions options;
    options.zip_compression_level = fastxlsx::min_zip_compression_level;
    auto workbook = fastxlsx::WorkbookWriter::create(stored_output_path, options);
    auto sheet = workbook.add_worksheet("Compression");
    sheet.append_row({fastxlsx::CellView::text("level 0")});
    workbook.close();

    const auto stored_entries = fastxlsx::test::read_zip_entries(stored_output_path);
    check(stored_entries.contains("xl/worksheets/sheet1.xml"),
        "compression level 0 workbook should be readable");

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
    const auto compressed_output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-compression-level-9.xlsx";

    options.zip_compression_level = fastxlsx::max_zip_compression_level;
    auto compressed_workbook = fastxlsx::WorkbookWriter::create(compressed_output_path, options);
    auto compressed_sheet = compressed_workbook.add_worksheet("Compression");
    compressed_sheet.append_row({fastxlsx::CellView::text("level 9")});
    compressed_workbook.close();

    const auto compressed_entries = fastxlsx::test::read_zip_entries(compressed_output_path);
    check(compressed_entries.contains("xl/worksheets/sheet1.xml"),
        "compression level 9 workbook should be readable with minizip");
#else
    options.zip_compression_level = fastxlsx::max_zip_compression_level;
    bool unsupported_level_failed = false;
    try {
        (void)fastxlsx::WorkbookWriter::create(
            fastxlsx::test::artifact_dir() / "fastxlsx-streaming-compression-unsupported.xlsx",
            options);
    } catch (const fastxlsx::FastXlsxError& error) {
        unsupported_level_failed = true;
        check_contains(error.what(), "minizip-ng",
            "stored bootstrap positive compression-level failure should mention minizip-ng");
    }
    check(unsupported_level_failed,
        "stored bootstrap should reject positive compression levels before writing rows");
#endif

    auto check_invalid_level = [](int compression_level, std::string_view output_name) {
        const auto output_path =
            fastxlsx::test::artifact_dir() / std::filesystem::path(std::string(output_name));
        const std::string sentinel = "preserve existing invalid-compression output";
        {
            std::ofstream output(output_path, std::ios::binary);
            output << sentinel;
        }

        fastxlsx::WorkbookWriterOptions invalid_options;
        invalid_options.zip_compression_level = compression_level;

        bool failed = false;
        try {
            (void)fastxlsx::WorkbookWriter::create(output_path, invalid_options);
        } catch (const fastxlsx::FastXlsxError& error) {
            failed = true;
            check_contains(error.what(), "ZIP compression level",
                "invalid compression-level failure should explain the bad option");
        }

        check(failed, "WorkbookWriter should reject invalid compression levels");
        check(fastxlsx::test::read_file(output_path) == sentinel,
            "invalid compression level should fail before overwriting output");
    };

    check_invalid_level(fastxlsx::default_zip_compression_level - 1,
        "fastxlsx-streaming-invalid-compression-low.xlsx");
    check_invalid_level(fastxlsx::max_zip_compression_level + 1,
        "fastxlsx-streaming-invalid-compression-high.xlsx");
}

void test_streaming_writer_empty_rows_dimension()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-empty-row-dimensions.xlsx";

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

void test_streaming_writer_blank_cells()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-blank-cells.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    const fastxlsx::StyleId text_style = workbook.add_style(fastxlsx::CellStyle {"@"});
    auto sheet = workbook.add_worksheet("Blank");

    sheet.append_row({
        fastxlsx::CellView::blank(),
        fastxlsx::CellView::text(""),
        fastxlsx::CellView::blank().with_style(text_style),
        fastxlsx::CellView::text("tail"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "blank-cell xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:D1\"/>",
        "blank cells should participate in dimension tracking");
    check_contains(worksheet_xml,
        "<row r=\"1\"><c r=\"A1\"/><c r=\"B1\" t=\"inlineStr\"><is><t></t></is></c>"
        "<c r=\"C1\" s=\"1\"/><c r=\"D1\" t=\"inlineStr\"><is><t>tail</t></is></c></row>",
        "blank, empty-string, styled-blank, and text cell XML should stay distinct");
    check(worksheet_xml.find("E1") == std::string::npos,
        "missing cells beyond the appended row should not be serialized");

    const auto shared_blank_output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-shared-strings-blank-cells.xlsx";

    fastxlsx::WorkbookWriterOptions options;
    options.string_strategy = fastxlsx::StringStrategy::SharedString;
    auto shared_workbook = fastxlsx::WorkbookWriter::create(shared_blank_output_path, options);
    auto shared_sheet = shared_workbook.add_worksheet("BlankOnly");
    shared_sheet.append_row({
        fastxlsx::CellView::blank(),
        fastxlsx::CellView::boolean(false),
        fastxlsx::CellView::number(1.0),
    });
    shared_workbook.close();

    const auto shared_entries = fastxlsx::test::read_zip_entries(shared_blank_output_path);
    check(!shared_entries.contains("xl/sharedStrings.xml"),
        "blank cells should not create sharedStrings.xml under SharedString strategy");
    const auto& shared_content_types = shared_entries.at("[Content_Types].xml");
    check(shared_content_types.find("/xl/sharedStrings.xml") == std::string::npos,
        "blank cells should not add a sharedStrings content type override");
    const auto& shared_workbook_rels = shared_entries.at("xl/_rels/workbook.xml.rels");
    check(shared_workbook_rels.find("relationships/sharedStrings") == std::string::npos,
        "blank cells should not add a sharedStrings workbook relationship");
    const auto& shared_worksheet_xml = shared_entries.at("xl/worksheets/sheet1.xml");
    check_contains(shared_worksheet_xml, "<dimension ref=\"A1:C1\"/>",
        "blank-only shared-string worksheet dimension mismatch");
    check_contains(shared_worksheet_xml,
        "<row r=\"1\"><c r=\"A1\"/><c r=\"B1\" t=\"b\"><v>0</v></c>"
        "<c r=\"C1\"><v>1</v></c></row>",
        "blank cells should not use shared or inline string markup");
    check(shared_worksheet_xml.find(" t=\"s\"") == std::string::npos,
        "blank cells should not reference shared string indexes");
    check(shared_worksheet_xml.find("inlineStr") == std::string::npos,
        "blank cells should not create inline string markup");
}

void test_streaming_writer_sparse_rows()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-sparse-rows.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    const fastxlsx::StyleId text_style = workbook.add_style(fastxlsx::CellStyle {"@"});
    auto sheet = workbook.add_worksheet("Sparse");

    sheet.append_sparse_row(
        {
            {3, fastxlsx::CellView::text("C1")},
            {5, fastxlsx::CellView::number(5.0)},
            {16384, fastxlsx::CellView::blank().with_style(text_style)},
        },
        fastxlsx::RowOptions {18.5});
    sheet.append_sparse_row({});
    sheet.append_sparse_row({{2, fastxlsx::CellView::formula("A1+1")}});

    workbook.close();
    check(std::filesystem::exists(output_path), "sparse-row xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:XFD3\"/>",
        "sparse rows should track the highest sparse column and final appended row");
    check_contains(worksheet_xml,
        "<row r=\"1\" ht=\"18.5\" customHeight=\"1\"><c r=\"C1\" t=\"inlineStr\">"
        "<is><t>C1</t></is></c><c r=\"E1\"><v>5</v></c><c r=\"XFD1\" s=\"1\"/></row>",
        "sparse row should write only provided entries at their requested columns");
    check_contains(worksheet_xml, "<row r=\"2\"></row>",
        "empty sparse row should still append an empty row");
    check_contains(worksheet_xml, "<row r=\"3\"><c r=\"B3\"><f>A1+1</f></c></row>",
        "sparse formula row should preserve the requested column");
    check(worksheet_xml.find("r=\"A1\"") == std::string::npos,
        "sparse row should not synthesize missing leading cells");
    check(worksheet_xml.find("r=\"D1\"") == std::string::npos,
        "sparse row should not synthesize missing middle cells");
    check(worksheet_xml.find("r=\"F1\"") == std::string::npos,
        "sparse row should not synthesize missing cells before a styled blank");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check_contains(workbook_xml, "<calcPr calcId=\"124519\" fullCalcOnLoad=\"1\"/>",
        "sparse formula row should request workbook recalculation");
}

void test_streaming_writer_max_column_boundary()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-max-column-boundary.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("MaxColumn");

    std::vector<fastxlsx::CellView> max_columns(16384, fastxlsx::CellView::number(1.0));
    sheet.append_row(std::span<const fastxlsx::CellView>(max_columns.data(), max_columns.size()));

    workbook.close();
    check(std::filesystem::exists(output_path), "max-column streaming xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:XFD1\"/>",
        "max-column streaming worksheet dimension mismatch");
    check_contains(worksheet_xml, "<c r=\"A1\"><v>1</v></c>",
        "max-column streaming first cell reference mismatch");
    check_contains(worksheet_xml, "<c r=\"XFD1\"><v>1</v></c>",
        "max-column streaming last cell reference mismatch");
    check(worksheet_xml.find("XFE1") == std::string::npos,
        "max-column streaming worksheet should not write a column beyond XFD");
}

void test_streaming_writer_max_row_boundary_with_test_hook()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-max-row-boundary.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("MaxRow");

    fastxlsx::detail::testing_set_worksheet_row_count(sheet, 1048575);
    sheet.append_row({
        fastxlsx::CellView::number(45500.0),
        fastxlsx::CellView::boolean(true),
        fastxlsx::CellView::formula("A1048576+1"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "max-row streaming xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:C1048576\"/>",
        "max-row streaming worksheet dimension mismatch");
    check_contains(worksheet_xml, "<row r=\"1048576\">",
        "max-row streaming worksheet should serialize the last legal row");
    check_contains(worksheet_xml, "<c r=\"A1048576\"><v>45500</v></c>",
        "max-row streaming numeric cell reference mismatch");
    check_contains(worksheet_xml, "<c r=\"B1048576\" t=\"b\"><v>1</v></c>",
        "max-row streaming boolean cell reference mismatch");
    check_contains(worksheet_xml, "<c r=\"C1048576\"><f>A1048576+1</f></c>",
        "max-row streaming formula cell reference mismatch");
    check(worksheet_xml.find("1048577") == std::string::npos,
        "max-row streaming worksheet should not write a row beyond Excel's limit");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check_contains(workbook_xml, "<calcPr calcId=\"124519\" fullCalcOnLoad=\"1\"/>",
        "max-row streaming formula should request workbook recalculation");
}

void test_streaming_writer_failed_append_preserves_state()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-failed-append-state.xlsx";

    fastxlsx::WorkbookWriterOptions options;
    options.string_strategy = fastxlsx::StringStrategy::SharedString;
    auto workbook = fastxlsx::WorkbookWriter::create(output_path, options);
    auto sheet = workbook.add_worksheet("FailedAppend");

    check_fastxlsx_error(
        [&sheet] {
            sheet.append_row(
                {
                    fastxlsx::CellView::text("bad shared string"),
                    fastxlsx::CellView::formula("A1+1"),
                },
                fastxlsx::RowOptions {0.0});
        },
        "append_row should reject invalid row height before mutating streaming state");

    check_fastxlsx_error(
        [&sheet] {
            sheet.append_sparse_row(
                {
                    {2, fastxlsx::CellView::text("bad sparse shared string")},
                    {2, fastxlsx::CellView::formula("A1+1")},
                });
        },
        "append_sparse_row should reject duplicate columns before mutating streaming state");

    sheet.append_row({fastxlsx::CellView::number(7.0)});

    check_fastxlsx_error(
        [&sheet] {
            sheet.append_row(
                {fastxlsx::CellView::number(std::numeric_limits<double>::quiet_NaN())});
        },
        "append_row should reject NaN before consuming a row number");

    check_fastxlsx_error(
        [&sheet] { sheet.append_row({fastxlsx::CellView::formula("")}); },
        "append_row should reject empty formula text before mutating streaming state");

    check_fastxlsx_error(
        [&sheet] {
            sheet.append_sparse_row(
                {{2, fastxlsx::CellView::number(std::numeric_limits<double>::quiet_NaN())}});
        },
        "append_sparse_row should reject NaN before consuming a row number");

    check_fastxlsx_error(
        [&sheet] {
            sheet.append_sparse_row({{2, fastxlsx::CellView::formula("")}});
        },
        "append_sparse_row should reject empty formula text before mutating streaming state");

    sheet.append_row({fastxlsx::CellView::boolean(true)});

    workbook.close();
    check(std::filesystem::exists(output_path), "failed-append state xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(!entries.contains("xl/sharedStrings.xml"),
        "failed append should not create an unused sharedStrings part");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:A2\"/>",
        "failed append should not advance the streaming dimension");
    check_contains(worksheet_xml, "<row r=\"1\"><c r=\"A1\"><v>7</v></c></row>",
        "first valid row should remain row 1 after a failed append");
    check_contains(worksheet_xml, "<row r=\"2\"><c r=\"A2\" t=\"b\"><v>1</v></c></row>",
        "second valid row should remain row 2 after a failed append");
    check(worksheet_xml.find("<row r=\"3\"") == std::string::npos,
        "failed append should not leave a gap in row numbers");
    check(worksheet_xml.find("bad shared string") == std::string::npos,
        "failed append should not serialize rejected text cells");
    check(worksheet_xml.find("bad sparse shared string") == std::string::npos,
        "failed sparse append should not serialize rejected text cells");
    check(worksheet_xml.find("<f>A1+1</f>") == std::string::npos,
        "failed append should not serialize rejected formulas");
    check(worksheet_xml.find("<f></f>") == std::string::npos,
        "failed append should not serialize rejected empty formulas");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check(workbook_xml.find("<calcPr") == std::string::npos,
        "failed append should not mark the workbook for formula recalculation");
}

void test_streaming_writer_phase3_metadata_structure()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-phase3-metadata.xlsx";

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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-file-backed-body.xlsx";

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

void test_streaming_writer_invalid_ranges()
{
    const auto output_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-invalid-ranges.xlsx";

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
    table.show_totals_row = true;
    check_fastxlsx_error(
        [&sheet, &table] { sheet.add_table({1, 1, 2, 1}, table); },
        "tables should reject totals row metadata without a data row");
    check_fastxlsx_error(
        [&sheet, &table] { sheet.add_table({1, 1, 3, 1}, table); },
        "tables should reject visible totals rows without a totals function");
    table.show_totals_row = false;
    table.column_names = {"A", "B"};
    check_fastxlsx_error(
        [&sheet, &table] { sheet.add_table({1, 1, 2, 16385}, table); },
        "tables should reject a column beyond Excel's limit");

    const auto image_path = fastxlsx::test::artifact_dir() / "fastxlsx-unused-image.png";
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

    const std::span<const std::byte> empty_image;
    check_fastxlsx_error(
        [&sheet, empty_image] { sheet.add_image(empty_image, {1, 1, 1, 1}); },
        "memory images should reject an empty buffer");

    std::vector<unsigned char> unsupported_image {0x00, 0x01, 0x02, 0x03};
    check_fastxlsx_error(
        [&sheet, &unsupported_image] {
            sheet.add_image(
                std::as_bytes(std::span<const unsigned char>(
                    unsupported_image.data(), unsupported_image.size())),
                {1, 1, 1, 1});
        },
        "memory images should reject unsupported headers");

    const auto valid_image_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-invalid-offset-image-source.png";
    write_bytes(valid_image_path, fastxlsx::test::tiny_png_bytes());
    fastxlsx::ImageOptions negative_offset;
    negative_offset.from_offset = {-1, 0};
    check_fastxlsx_error(
        [&sheet, &valid_image_path, &negative_offset] {
            sheet.add_image(valid_image_path, {1, 1, 1, 1}, negative_offset);
        },
        "images should reject negative anchor offsets");
    fastxlsx::ImageOptions too_large_offset;
    too_large_offset.to_offset = {27273042316901LL, 0};
    check_fastxlsx_error(
        [&sheet, &valid_image_path, &too_large_offset] {
            sheet.add_image(valid_image_path, {1, 1, 1, 1}, too_large_offset);
        },
        "images should reject anchor offsets beyond OpenXML coordinate bounds");

    fastxlsx::ImageOptions tooltip_without_url;
    tooltip_without_url.external_hyperlink_tooltip = "tooltip only";
    check_fastxlsx_error(
        [&sheet, &valid_image_path, &tooltip_without_url] {
            sheet.add_image(valid_image_path, {1, 1, 1, 1}, tooltip_without_url);
        },
        "images should reject hyperlink tooltip without external hyperlink URL");
}

void test_streaming_writer_sheet_name_uniqueness()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-sheet-name-uniqueness.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);

    check_fastxlsx_error(
        [&workbook] {
            workbook.add_worksheet("Sheet1");
            workbook.add_worksheet("sheet1");
        },
        "streaming workbook should reject ASCII case-insensitive duplicate sheet names");

    auto data = workbook.add_worksheet("Data");
    auto summary = workbook.add_worksheet("Summary");
    (void)data;
    (void)summary;

    check_fastxlsx_error(
        [&workbook] { workbook.add_worksheet("SUMMARY"); },
        "streaming workbook should reject case-insensitive duplicate sheet names after valid adds");
    check_fastxlsx_error(
        [&workbook] { workbook.add_worksheet("Data"); },
        "streaming workbook should reject exact duplicate sheet names");

    auto final_sheet = workbook.add_worksheet("Final");
    final_sheet.append_row({fastxlsx::CellView::text("ok")});
    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check(workbook_xml.find(R"(name="Data")") != std::string::npos,
        "streaming workbook should keep the first valid sheet");
    check(workbook_xml.find(R"(name="Summary")") != std::string::npos,
        "streaming workbook should keep the second valid sheet");
    check(workbook_xml.find(R"(name="Final")") != std::string::npos,
        "streaming workbook should keep the later valid sheet");
}

void test_streaming_writer_invalid_metadata_and_rows()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-invalid-metadata.xlsx";

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
    check_fastxlsx_error(
        [&sheet] { sheet.append_sparse_row({{0, fastxlsx::CellView::text("bad column")}}); },
        "append_sparse_row should reject a zero column");
    check_fastxlsx_error(
        [&sheet] { sheet.append_sparse_row({{16385, fastxlsx::CellView::text("bad column")}}); },
        "append_sparse_row should reject a column beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet] {
            sheet.append_sparse_row(
                {
                    {3, fastxlsx::CellView::text("bad order")},
                    {2, fastxlsx::CellView::text("bad order")},
                });
        },
        "append_sparse_row should reject non-increasing columns");

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
        test_streaming_writer_zip_compression_level_options();
        test_streaming_writer_empty_rows_dimension();
        test_streaming_writer_blank_cells();
        test_streaming_writer_sparse_rows();
        test_streaming_writer_max_column_boundary();
        test_streaming_writer_max_row_boundary_with_test_hook();
        test_streaming_writer_failed_append_preserves_state();
        test_streaming_writer_phase3_metadata_structure();
        test_streaming_writer_file_backed_body_round_trip();
        test_streaming_writer_invalid_ranges();
        test_streaming_writer_sheet_name_uniqueness();
        test_streaming_writer_invalid_metadata_and_rows();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
