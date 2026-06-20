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

    sheet.append_row({fastxlsx::CellView::number(7.0)});

    check_fastxlsx_error(
        [&sheet] {
            sheet.append_row(
                {fastxlsx::CellView::number(std::numeric_limits<double>::quiet_NaN())});
        },
        "append_row should reject NaN before consuming a row number");

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
    check(worksheet_xml.find("<f>A1+1</f>") == std::string::npos,
        "failed append should not serialize rejected formulas");

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

void test_streaming_writer_number_format_styles()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-styles-number-formats.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    const auto currency_style = workbook.add_style(fastxlsx::CellStyle {"$#,##0.00"});
    const auto duplicate_currency_style =
        workbook.add_style(fastxlsx::CellStyle {"$#,##0.00"});
    const auto escaped_style =
        workbook.add_style(fastxlsx::CellStyle {R"(0.00 "kg & <unit>")"});

    check(currency_style.value() == 1, "first custom style id should be 1");
    check(duplicate_currency_style.value() == 1, "duplicate style should reuse style id");
    check(escaped_style.value() == 2, "second custom style id should be 2");

    auto sheet = workbook.add_worksheet("Styles");
    sheet.append_row({
        fastxlsx::CellView::text("Currency"),
        fastxlsx::CellView::text("Escaped"),
        fastxlsx::CellView::text("Default"),
        fastxlsx::CellView::text("Text"),
        fastxlsx::CellView::text("Bool"),
        fastxlsx::CellView::text("Formula"),
    });
    sheet.append_row({
        fastxlsx::CellView::number(1234.5).with_style(currency_style),
        fastxlsx::CellView::number(7.25).with_style(escaped_style),
        fastxlsx::CellView::number(9.0),
        fastxlsx::CellView::text("styled text").with_style(escaped_style),
        fastxlsx::CellView::boolean(true).with_style(currency_style),
        fastxlsx::CellView::formula("A2*2").with_style(currency_style),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "styles xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/styles.xml"), "missing styles part");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "styles should not create worksheet relationships");
    check(!entries.contains("xl/sharedStrings.xml"),
        "styles-only inline package should not include shared strings");
    check(!entries.contains("xl/calcChain.xml"),
        "styled formula should not create calcChain");

    const auto& content_types = entries.at("[Content_Types].xml");
    check_contains(content_types,
        R"(<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>)",
        "missing styles content type override");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(<calcPr calcId="124519" fullCalcOnLoad="1"/>)",
        "styled formula should request full recalculation");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(count_occurrences(workbook_rels, "<Relationship ") == 2,
        "styles workbook relationship count mismatch");
    check_contains(workbook_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)",
        "styles workbook relationship mismatch");

    const auto& styles_xml = entries.at("xl/styles.xml");
    check_contains(styles_xml, R"(<numFmts count="2">)",
        "custom number format count mismatch");
    check_contains(styles_xml, R"(<numFmt numFmtId="164" formatCode="$#,##0.00"/>)",
        "first custom number format mismatch");
    check_contains(styles_xml,
        R"(<numFmt numFmtId="165" formatCode="0.00 &quot;kg &amp; &lt;unit&gt;&quot;"/>)",
        "escaped custom number format mismatch");
    check_contains(styles_xml, R"(<fonts count="1">)", "default font collection missing");
    check_contains(styles_xml, R"(<fills count="2">)", "default fill collection missing");
    check_contains(styles_xml, R"(<borders count="1">)", "default border collection missing");
    check_contains(styles_xml,
        R"(<cellXfs count="3"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>)",
        "cellXfs default style mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="164" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/>)",
        "first style xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="165" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/>)",
        "second style xf mismatch");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:F2"/>)",
        "styles worksheet dimension mismatch");
    check_contains(worksheet_xml, R"(<c r="A2" s="1"><v>1234.5</v></c>)",
        "styled number cell mismatch");
    check_contains(worksheet_xml, R"(<c r="B2" s="2"><v>7.25</v></c>)",
        "second styled number cell mismatch");
    check_contains(worksheet_xml, R"(<c r="C2"><v>9</v></c>)",
        "default number cell should omit style attribute");
    check_contains(worksheet_xml,
        R"(<c r="D2" s="2" t="inlineStr"><is><t>styled text</t></is></c>)",
        "styled inline string cell mismatch");
    check_contains(worksheet_xml, R"(<c r="E2" s="1" t="b"><v>1</v></c>)",
        "styled boolean cell mismatch");
    check_contains(worksheet_xml, R"(<c r="F2" s="1"><f>A2*2</f></c>)",
        "styled formula cell mismatch");
    check(worksheet_xml.find("s=\"0\"") == std::string::npos,
        "default style should not be serialized as s=\"0\"");
}

void test_streaming_writer_alignment_styles()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-styles-alignment.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);

    fastxlsx::CellStyle wrap_text_style_definition;
    wrap_text_style_definition.alignment = fastxlsx::CellAlignment {true};
    const auto wrap_text_style = workbook.add_style(wrap_text_style_definition);
    const auto duplicate_wrap_text_style = workbook.add_style(wrap_text_style_definition);

    fastxlsx::CellAlignment left_alignment;
    left_alignment.horizontal = fastxlsx::HorizontalAlignment::Left;
    fastxlsx::CellStyle left_style_definition;
    left_style_definition.alignment = left_alignment;
    const auto left_style = workbook.add_style(left_style_definition);

    fastxlsx::CellAlignment center_alignment;
    center_alignment.horizontal = fastxlsx::HorizontalAlignment::Center;
    fastxlsx::CellStyle center_style_definition;
    center_style_definition.alignment = center_alignment;
    const auto center_style = workbook.add_style(center_style_definition);

    fastxlsx::CellAlignment right_alignment;
    right_alignment.horizontal = fastxlsx::HorizontalAlignment::Right;
    fastxlsx::CellStyle right_style_definition;
    right_style_definition.alignment = right_alignment;
    const auto right_style = workbook.add_style(right_style_definition);

    fastxlsx::CellAlignment top_alignment;
    top_alignment.vertical = fastxlsx::VerticalAlignment::Top;
    fastxlsx::CellStyle top_style_definition;
    top_style_definition.alignment = top_alignment;
    const auto top_style = workbook.add_style(top_style_definition);

    fastxlsx::CellAlignment middle_alignment;
    middle_alignment.vertical = fastxlsx::VerticalAlignment::Center;
    fastxlsx::CellStyle middle_style_definition;
    middle_style_definition.alignment = middle_alignment;
    const auto middle_style = workbook.add_style(middle_style_definition);

    fastxlsx::CellAlignment bottom_alignment;
    bottom_alignment.vertical = fastxlsx::VerticalAlignment::Bottom;
    fastxlsx::CellStyle bottom_style_definition;
    bottom_style_definition.alignment = bottom_alignment;
    const auto bottom_style = workbook.add_style(bottom_style_definition);

    fastxlsx::CellStyle number_style_definition {"0.0"};
    const auto number_style = workbook.add_style(number_style_definition);

    fastxlsx::CellAlignment combined_alignment;
    combined_alignment.wrap_text = true;
    combined_alignment.horizontal = fastxlsx::HorizontalAlignment::Center;
    combined_alignment.vertical = fastxlsx::VerticalAlignment::Center;
    fastxlsx::CellStyle number_combined_style_definition {"0.0"};
    number_combined_style_definition.alignment = combined_alignment;
    const auto number_combined_style = workbook.add_style(number_combined_style_definition);
    const auto duplicate_number_combined_style =
        workbook.add_style(number_combined_style_definition);

    check(wrap_text_style.value() == 1, "first alignment style id should be 1");
    check(duplicate_wrap_text_style.value() == 1, "duplicate alignment style should reuse id");
    check(left_style.value() == 2, "left alignment style should be second style id");
    check(center_style.value() == 3, "center alignment style should be third style id");
    check(right_style.value() == 4, "right alignment style should be fourth style id");
    check(top_style.value() == 5, "top alignment style should be fifth style id");
    check(middle_style.value() == 6, "middle alignment style should be sixth style id");
    check(bottom_style.value() == 7, "bottom alignment style should be seventh style id");
    check(number_style.value() == 8, "number format style should be eighth style id");
    check(number_combined_style.value() == 9,
        "combined number/alignment style should be ninth style id");
    check(duplicate_number_combined_style.value() == 9,
        "duplicate combined number/alignment style should reuse id");

    auto sheet = workbook.add_worksheet("Alignment");
    sheet.append_row({
        fastxlsx::CellView::text("Wrapped"),
        fastxlsx::CellView::text("Left"),
        fastxlsx::CellView::text("Center"),
        fastxlsx::CellView::text("Right"),
        fastxlsx::CellView::text("Top"),
        fastxlsx::CellView::text("Middle"),
        fastxlsx::CellView::text("Bottom"),
        fastxlsx::CellView::text("Number"),
        fastxlsx::CellView::text("Combined"),
        fastxlsx::CellView::text("Default"),
    });
    sheet.append_row({
        fastxlsx::CellView::text("line 1\nline 2").with_style(wrap_text_style),
        fastxlsx::CellView::text("left").with_style(left_style),
        fastxlsx::CellView::text("center").with_style(center_style),
        fastxlsx::CellView::text("right").with_style(right_style),
        fastxlsx::CellView::text("top").with_style(top_style),
        fastxlsx::CellView::text("middle").with_style(middle_style),
        fastxlsx::CellView::text("bottom").with_style(bottom_style),
        fastxlsx::CellView::number(12.5).with_style(number_style),
        fastxlsx::CellView::number(42.5).with_style(number_combined_style),
        fastxlsx::CellView::text("plain"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "alignment styles xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/styles.xml"), "missing alignment styles part");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "alignment styles should not create worksheet relationships");
    check(!entries.contains("xl/sharedStrings.xml"),
        "alignment styles inline sample should not create shared strings");

    const auto& styles_xml = entries.at("xl/styles.xml");
    check_contains(styles_xml, R"(<numFmts count="1">)",
        "alignment-only style should not create a custom number format");
    check_contains(styles_xml, R"(<numFmt numFmtId="164" formatCode="0.0"/>)",
        "combined style should reuse the custom number format id");
    check_contains(styles_xml, R"(<fonts count="1">)",
        "alignment slice should keep default fonts only");
    check_contains(styles_xml, R"(<fills count="2">)",
        "alignment slice should keep default fills only");
    check_contains(styles_xml, R"(<borders count="1">)",
        "alignment slice should keep default borders only");
    check_contains(styles_xml,
        R"(<cellXfs count="10"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>)",
        "alignment cellXfs default style mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyAlignment="1"><alignment wrapText="1"/></xf>)",
        "wrap-text alignment xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyAlignment="1"><alignment horizontal="left"/></xf>)",
        "left alignment xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyAlignment="1"><alignment horizontal="center"/></xf>)",
        "center alignment xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyAlignment="1"><alignment horizontal="right"/></xf>)",
        "right alignment xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyAlignment="1"><alignment vertical="top"/></xf>)",
        "top alignment xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyAlignment="1"><alignment vertical="center"/></xf>)",
        "middle alignment xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0" applyAlignment="1"><alignment vertical="bottom"/></xf>)",
        "bottom alignment xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="164" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/>)",
        "number-only xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="164" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1" applyAlignment="1"><alignment wrapText="1" horizontal="center" vertical="center"/></xf>)",
        "number plus combined alignment xf mismatch");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:J2"/>)",
        "alignment worksheet dimension mismatch");
    check_contains(worksheet_xml,
        "<c r=\"A2\" s=\"1\" t=\"inlineStr\"><is><t>line 1\nline 2</t></is></c>",
        "wrapped inline string cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="B2" s="2" t="inlineStr"><is><t>left</t></is></c>)",
        "left aligned string cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="C2" s="3" t="inlineStr"><is><t>center</t></is></c>)",
        "center aligned string cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="D2" s="4" t="inlineStr"><is><t>right</t></is></c>)",
        "right aligned string cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="E2" s="5" t="inlineStr"><is><t>top</t></is></c>)",
        "top aligned string cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="F2" s="6" t="inlineStr"><is><t>middle</t></is></c>)",
        "middle aligned string cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="G2" s="7" t="inlineStr"><is><t>bottom</t></is></c>)",
        "bottom aligned string cell mismatch");
    check_contains(worksheet_xml, R"(<c r="H2" s="8"><v>12.5</v></c>)",
        "number-only styled cell mismatch");
    check_contains(worksheet_xml, R"(<c r="I2" s="9"><v>42.5</v></c>)",
        "number plus alignment styled cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="J2" t="inlineStr"><is><t>plain</t></is></c>)",
        "default inline string cell mismatch");
    check(worksheet_xml.find("s=\"0\"") == std::string::npos,
        "default style should not be serialized as s=\"0\" in alignment sample");
}

void test_streaming_writer_font_styles()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-styles-fonts.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);

    fastxlsx::CellFont bold_font;
    bold_font.bold = true;
    fastxlsx::CellStyle bold_style_definition;
    bold_style_definition.font = bold_font;
    const auto bold_style = workbook.add_style(bold_style_definition);
    const auto duplicate_bold_style = workbook.add_style(bold_style_definition);

    fastxlsx::CellFont italic_font;
    italic_font.italic = true;
    fastxlsx::CellStyle italic_style_definition;
    italic_style_definition.font = italic_font;
    const auto italic_style = workbook.add_style(italic_style_definition);

    fastxlsx::CellFont bold_italic_font;
    bold_italic_font.bold = true;
    bold_italic_font.italic = true;
    fastxlsx::CellStyle bold_italic_style_definition;
    bold_italic_style_definition.font = bold_italic_font;
    const auto bold_italic_style = workbook.add_style(bold_italic_style_definition);

    fastxlsx::CellFont red_font;
    red_font.color = fastxlsx::ArgbColor {0xFF, 0xC0, 0x00, 0x00};
    fastxlsx::CellStyle red_style_definition;
    red_style_definition.font = red_font;
    const auto red_style = workbook.add_style(red_style_definition);
    const auto duplicate_red_style = workbook.add_style(red_style_definition);

    fastxlsx::CellFont bold_red_font;
    bold_red_font.bold = true;
    bold_red_font.color = fastxlsx::ArgbColor {0xFF, 0xC0, 0x00, 0x00};
    fastxlsx::CellStyle bold_red_style_definition;
    bold_red_style_definition.font = bold_red_font;
    const auto bold_red_style = workbook.add_style(bold_red_style_definition);

    fastxlsx::CellStyle number_bold_style_definition {"0.0"};
    number_bold_style_definition.font = bold_font;
    const auto number_bold_style = workbook.add_style(number_bold_style_definition);

    fastxlsx::CellStyle number_red_style_definition {"0.0"};
    number_red_style_definition.font = red_font;
    const auto number_red_style = workbook.add_style(number_red_style_definition);

    check(bold_style.value() == 1, "first font style id should be 1");
    check(duplicate_bold_style.value() == 1, "duplicate bold style should reuse id");
    check(italic_style.value() == 2, "italic style should be second style id");
    check(bold_italic_style.value() == 3, "bold italic style should be third style id");
    check(red_style.value() == 4, "red font style should be fourth style id");
    check(duplicate_red_style.value() == 4, "duplicate red font style should reuse id");
    check(bold_red_style.value() == 5, "bold red font style should be fifth style id");
    check(number_bold_style.value() == 6, "number plus bold style should be sixth style id");
    check(number_red_style.value() == 7, "number plus red style should be seventh style id");

    auto sheet = workbook.add_worksheet("Fonts");
    sheet.append_row({
        fastxlsx::CellView::text("Bold"),
        fastxlsx::CellView::text("Italic"),
        fastxlsx::CellView::text("BoldItalic"),
        fastxlsx::CellView::text("Red"),
        fastxlsx::CellView::text("BoldRed"),
        fastxlsx::CellView::text("NumberBold"),
        fastxlsx::CellView::text("NumberRed"),
        fastxlsx::CellView::text("Default"),
    });
    sheet.append_row({
        fastxlsx::CellView::text("bold").with_style(bold_style),
        fastxlsx::CellView::text("italic").with_style(italic_style),
        fastxlsx::CellView::boolean(true).with_style(bold_italic_style),
        fastxlsx::CellView::text("red").with_style(red_style),
        fastxlsx::CellView::text("bold red").with_style(bold_red_style),
        fastxlsx::CellView::number(12.5).with_style(number_bold_style),
        fastxlsx::CellView::number(42.5).with_style(number_red_style),
        fastxlsx::CellView::text("plain"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "font styles xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/styles.xml"), "missing font styles part");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "font styles should not create worksheet relationships");

    const auto& styles_xml = entries.at("xl/styles.xml");
    check_contains(styles_xml, R"(<numFmts count="1">)",
        "font sample should create only one custom number format");
    check_contains(styles_xml, R"(<numFmt numFmtId="164" formatCode="0.0"/>)",
        "font sample custom number format mismatch");
    check_contains(styles_xml, R"(<fonts count="6">)",
        "font sample custom font count mismatch");
    check_contains(styles_xml,
        R"(<font><b/><sz val="11"/><color theme="1"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)",
        "bold font XML mismatch");
    check_contains(styles_xml,
        R"(<font><i/><sz val="11"/><color theme="1"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)",
        "italic font XML mismatch");
    check_contains(styles_xml,
        R"(<font><b/><i/><sz val="11"/><color theme="1"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)",
        "bold italic font XML mismatch");
    check_contains(styles_xml,
        R"(<font><sz val="11"/><color rgb="FFC00000"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)",
        "red font XML mismatch");
    check_contains(styles_xml,
        R"(<font><b/><sz val="11"/><color rgb="FFC00000"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)",
        "bold red font XML mismatch");
    check_contains(styles_xml,
        R"(<cellXfs count="8"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>)",
        "font cellXfs default style mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="1" fillId="0" borderId="0" xfId="0" applyFont="1"/>)",
        "bold xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="2" fillId="0" borderId="0" xfId="0" applyFont="1"/>)",
        "italic xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="3" fillId="0" borderId="0" xfId="0" applyFont="1"/>)",
        "bold italic xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="4" fillId="0" borderId="0" xfId="0" applyFont="1"/>)",
        "red xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="5" fillId="0" borderId="0" xfId="0" applyFont="1"/>)",
        "bold red xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="164" fontId="1" fillId="0" borderId="0" xfId="0" applyNumberFormat="1" applyFont="1"/>)",
        "number plus bold xf should reuse bold font id");
    check_contains(styles_xml,
        R"(<xf numFmtId="164" fontId="4" fillId="0" borderId="0" xfId="0" applyNumberFormat="1" applyFont="1"/>)",
        "number plus red xf should reuse red font id");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:H2"/>)",
        "font worksheet dimension mismatch");
    check_contains(worksheet_xml,
        R"(<c r="A2" s="1" t="inlineStr"><is><t>bold</t></is></c>)",
        "bold styled text cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="B2" s="2" t="inlineStr"><is><t>italic</t></is></c>)",
        "italic styled text cell mismatch");
    check_contains(worksheet_xml, R"(<c r="C2" s="3" t="b"><v>1</v></c>)",
        "bold italic styled boolean cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="D2" s="4" t="inlineStr"><is><t>red</t></is></c>)",
        "red styled text cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="E2" s="5" t="inlineStr"><is><t>bold red</t></is></c>)",
        "bold red styled text cell mismatch");
    check_contains(worksheet_xml, R"(<c r="F2" s="6"><v>12.5</v></c>)",
        "number plus bold styled number cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="G2" s="7"><v>42.5</v></c>)",
        "number plus red styled number cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="H2" t="inlineStr"><is><t>plain</t></is></c>)",
        "default font sample cell mismatch");
    check(worksheet_xml.find("s=\"0\"") == std::string::npos,
        "default style should not be serialized as s=\"0\" in font sample");
}

void test_streaming_writer_fill_styles()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-styles-fills.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);

    fastxlsx::CellFill yellow_fill {fastxlsx::ArgbColor {0xFF, 0xFF, 0xEB, 0x84}};
    fastxlsx::CellStyle yellow_style_definition;
    yellow_style_definition.fill = yellow_fill;
    const auto yellow_style = workbook.add_style(yellow_style_definition);
    const auto duplicate_yellow_style = workbook.add_style(yellow_style_definition);

    fastxlsx::CellFill blue_fill {fastxlsx::ArgbColor {0xFF, 0x5A, 0x8A, 0xD6}};
    fastxlsx::CellStyle blue_style_definition;
    blue_style_definition.fill = blue_fill;
    const auto blue_style = workbook.add_style(blue_style_definition);

    fastxlsx::CellStyle number_yellow_style_definition {"0.0"};
    number_yellow_style_definition.fill = yellow_fill;
    const auto number_yellow_style = workbook.add_style(number_yellow_style_definition);

    fastxlsx::CellFont bold_font;
    bold_font.bold = true;
    fastxlsx::CellStyle bold_yellow_style_definition;
    bold_yellow_style_definition.font = bold_font;
    bold_yellow_style_definition.fill = yellow_fill;
    const auto bold_yellow_style = workbook.add_style(bold_yellow_style_definition);

    check(yellow_style.value() == 1, "first fill style id should be 1");
    check(duplicate_yellow_style.value() == 1, "duplicate fill style should reuse id");
    check(blue_style.value() == 2, "blue fill style should be second style id");
    check(number_yellow_style.value() == 3, "number plus fill style should be third style id");
    check(bold_yellow_style.value() == 4, "font plus fill style should be fourth style id");

    auto sheet = workbook.add_worksheet("Fills");
    sheet.append_row({
        fastxlsx::CellView::text("Yellow"),
        fastxlsx::CellView::text("Blue"),
        fastxlsx::CellView::text("NumberYellow"),
        fastxlsx::CellView::text("BoldYellow"),
        fastxlsx::CellView::text("Default"),
    });
    sheet.append_row({
        fastxlsx::CellView::text("yellow").with_style(yellow_style),
        fastxlsx::CellView::text("blue").with_style(blue_style),
        fastxlsx::CellView::number(12.5).with_style(number_yellow_style),
        fastxlsx::CellView::text("bold yellow").with_style(bold_yellow_style),
        fastxlsx::CellView::text("plain"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "fill styles xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/styles.xml"), "missing fill styles part");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "fill styles should not create worksheet relationships");
    check(!entries.contains("xl/sharedStrings.xml"),
        "fill styles inline sample should not create sharedStrings.xml");

    const auto& styles_xml = entries.at("xl/styles.xml");
    check_contains(styles_xml, R"(<numFmts count="1">)",
        "fill sample should create only one custom number format");
    check_contains(styles_xml, R"(<numFmt numFmtId="164" formatCode="0.0"/>)",
        "fill sample custom number format mismatch");
    check_contains(styles_xml, R"(<fonts count="2">)",
        "fill sample should create default font plus bold font");
    check_contains(styles_xml, R"(<fills count="4">)",
        "fill sample custom fill count mismatch");
    check_contains(styles_xml,
        R"(<fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill>)",
        "fill sample should keep the default fill records");
    check_contains(styles_xml,
        R"(<fill><patternFill patternType="solid"><fgColor rgb="FFFFEB84"/><bgColor indexed="64"/></patternFill></fill>)",
        "yellow solid fill XML mismatch");
    check_contains(styles_xml,
        R"(<fill><patternFill patternType="solid"><fgColor rgb="FF5A8AD6"/><bgColor indexed="64"/></patternFill></fill>)",
        "blue solid fill XML mismatch");
    check_contains(styles_xml,
        R"(<cellXfs count="5"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>)",
        "fill cellXfs default style mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="0" fillId="2" borderId="0" xfId="0" applyFill="1"/>)",
        "yellow fill xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="0" fillId="3" borderId="0" xfId="0" applyFill="1"/>)",
        "blue fill xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="164" fontId="0" fillId="2" borderId="0" xfId="0" applyNumberFormat="1" applyFill="1"/>)",
        "number plus fill xf should reuse yellow fill id");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="1" fillId="2" borderId="0" xfId="0" applyFont="1" applyFill="1"/>)",
        "font plus fill xf should reuse yellow fill id");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:E2"/>)",
        "fill worksheet dimension mismatch");
    check_contains(worksheet_xml,
        R"(<c r="A2" s="1" t="inlineStr"><is><t>yellow</t></is></c>)",
        "yellow fill styled text cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="B2" s="2" t="inlineStr"><is><t>blue</t></is></c>)",
        "blue fill styled text cell mismatch");
    check_contains(worksheet_xml, R"(<c r="C2" s="3"><v>12.5</v></c>)",
        "number plus fill styled number cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="D2" s="4" t="inlineStr"><is><t>bold yellow</t></is></c>)",
        "font plus fill styled text cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="E2" t="inlineStr"><is><t>plain</t></is></c>)",
        "default fill sample cell mismatch");
    check(worksheet_xml.find("s=\"0\"") == std::string::npos,
        "default style should not be serialized as s=\"0\" in fill sample");
}

void test_streaming_writer_styles_with_shared_strings()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-styles-shared-strings.xlsx";

    fastxlsx::WorkbookWriterOptions options;
    options.string_strategy = fastxlsx::StringStrategy::SharedString;
    auto workbook = fastxlsx::WorkbookWriter::create(output_path, options);
    const auto text_style = workbook.add_style(fastxlsx::CellStyle {"@"});
    auto sheet = workbook.add_worksheet("StyledShared");

    sheet.append_row({
        fastxlsx::CellView::text("styled shared").with_style(text_style),
        fastxlsx::CellView::text("plain shared"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "styled shared string xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/sharedStrings.xml"), "missing shared strings part");
    check(entries.contains("xl/styles.xml"), "missing styles part");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "shared strings plus styles should not create worksheet relationships");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check_contains(workbook_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>)",
        "shared strings relationship should remain before styles");
    check_contains(workbook_rels,
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)",
        "styles relationship id should follow shared strings");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1" s="1" t="s"><v>0</v></c>)",
        "styled shared string cell mismatch");
    check_contains(worksheet_xml, R"(<c r="B1" t="s"><v>1</v></c>)",
        "plain shared string cell mismatch");
}

void test_streaming_writer_invalid_style_preserves_state()
{
    const auto source_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-style-source-unused.xlsx";
    auto source_workbook = fastxlsx::WorkbookWriter::create(source_path);
    const auto foreign_style = source_workbook.add_style(fastxlsx::CellStyle {"0.0000"});

    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-style-invalid-state.xlsx";
    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("StyleState");

    check_fastxlsx_error(
        [&sheet, foreign_style] {
            sheet.append_row({fastxlsx::CellView::text("bad style").with_style(foreign_style)});
        },
        "append_row should reject a style id not registered in this workbook");

    sheet.append_row({fastxlsx::CellView::text("good")});
    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(!entries.contains("xl/styles.xml"),
        "failed foreign style append should not create styles.xml");
    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:A1"/>)",
        "failed style append should not advance worksheet dimension");
    check(worksheet_xml.find("bad style") == std::string::npos,
        "failed style append should not serialize rejected cell text");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>good</t></is></c>)",
        "valid row after failed style append mismatch");
}

void test_streaming_writer_foreign_style_collision_is_rejected()
{
    const auto source_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-style-collision-source.xlsx";
    auto source_workbook = fastxlsx::WorkbookWriter::create(source_path);
    const auto foreign_style = source_workbook.add_style(fastxlsx::CellStyle {"0.0000"});
    check(foreign_style.value() == 1, "foreign style id setup mismatch");

    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-style-collision-target.xlsx";
    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    const auto own_style = workbook.add_style(fastxlsx::CellStyle {"0.0"});
    check(own_style.value() == 1, "target style id setup mismatch");
    auto sheet = workbook.add_worksheet("Collision");

    check_fastxlsx_error(
        [&sheet, foreign_style] {
            sheet.append_row({fastxlsx::CellView::text("bad collision").with_style(foreign_style)});
        },
        "append_row should reject a foreign style id even when the numeric value collides");

    sheet.append_row({fastxlsx::CellView::text("good").with_style(own_style)});
    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:A1"/>)",
        "foreign style collision failure should not advance dimension");
    check(worksheet_xml.find("bad collision") == std::string::npos,
        "foreign style collision failure should not serialize rejected text");
    check_contains(worksheet_xml,
        R"(<c r="A1" s="1" t="inlineStr"><is><t>good</t></is></c>)",
        "valid row after foreign style collision mismatch");
}

void test_streaming_writer_default_style_id_clears_cell_style()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-style-default-clear.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    const auto number_style = workbook.add_style(fastxlsx::CellStyle {"0.0"});

    auto sheet = workbook.add_worksheet("DefaultStyle");
    sheet.append_row({
        fastxlsx::CellView::text("Styled"),
        fastxlsx::CellView::text("Cleared"),
        fastxlsx::CellView::text("ExplicitDefault"),
    });
    sheet.append_row({
        fastxlsx::CellView::number(12.5).with_style(number_style),
        fastxlsx::CellView::number(42.5)
            .with_style(number_style)
            .with_style(fastxlsx::StyleId {}),
        fastxlsx::CellView::text("plain").with_style(fastxlsx::StyleId {}),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "default style clear xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/styles.xml"), "registered style should still create styles.xml");

    const auto& styles_xml = entries.at("xl/styles.xml");
    check_contains(styles_xml,
        R"(<cellXfs count="2"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>)",
        "default clear style cellXfs mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="164" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/>)",
        "registered number style xf mismatch");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A2" s="1"><v>12.5</v></c>)",
        "registered style should write the style id");
    check_contains(worksheet_xml, R"(<c r="B2"><v>42.5</v></c>)",
        "StyleId{} should clear a previously styled number cell");
    check_contains(worksheet_xml,
        R"(<c r="C2" t="inlineStr"><is><t>plain</t></is></c>)",
        "StyleId{} should keep explicit default text unstyled");
    check(worksheet_xml.find("s=\"0\"") == std::string::npos,
        "default style clear should not serialize s=\"0\"");
}

void test_streaming_writer_all_default_style_metadata_is_ignored()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-style-default-metadata.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);

    fastxlsx::CellStyle number_style_definition {"0.0"};
    const auto number_style = workbook.add_style(number_style_definition);

    fastxlsx::CellStyle number_with_default_metadata {"0.0"};
    number_with_default_metadata.alignment = fastxlsx::CellAlignment {};
    number_with_default_metadata.font = fastxlsx::CellFont {};
    const auto duplicate_number_style = workbook.add_style(number_with_default_metadata);

    fastxlsx::CellFont bold_font;
    bold_font.bold = true;
    fastxlsx::CellStyle bold_style_definition;
    bold_style_definition.font = bold_font;
    const auto bold_style = workbook.add_style(bold_style_definition);

    fastxlsx::CellStyle bold_with_default_alignment;
    bold_with_default_alignment.font = bold_font;
    bold_with_default_alignment.alignment = fastxlsx::CellAlignment {};
    const auto duplicate_bold_style = workbook.add_style(bold_with_default_alignment);

    check(number_style.value() == 1, "number style should be first custom style id");
    check(duplicate_number_style.value() == 1,
        "all-default alignment/font metadata should not create a distinct number style");
    check(bold_style.value() == 2, "bold style should be second custom style id");
    check(duplicate_bold_style.value() == 2,
        "all-default alignment metadata should not create a distinct bold style");

    auto sheet = workbook.add_worksheet("DefaultMetadata");
    sheet.append_row({
        fastxlsx::CellView::text("Number"),
        fastxlsx::CellView::text("NumberDefaults"),
        fastxlsx::CellView::text("Bold"),
        fastxlsx::CellView::text("BoldDefaults"),
    });
    sheet.append_row({
        fastxlsx::CellView::number(12.5).with_style(number_style),
        fastxlsx::CellView::number(42.5).with_style(duplicate_number_style),
        fastxlsx::CellView::text("bold").with_style(bold_style),
        fastxlsx::CellView::text("bold defaults").with_style(duplicate_bold_style),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "default style metadata xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/styles.xml"),
        "valid styles with default metadata should create styles.xml");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "default style metadata should not create worksheet relationships");

    const auto& styles_xml = entries.at("xl/styles.xml");
    check_contains(styles_xml, R"(<numFmts count="1">)",
        "default metadata style should create one custom number format");
    check_contains(styles_xml, R"(<numFmt numFmtId="164" formatCode="0.0"/>)",
        "default metadata style custom number format mismatch");
    check_contains(styles_xml, R"(<fonts count="2">)",
        "all-default font metadata should not create a custom font");
    check_contains(styles_xml,
        R"(<font><b/><sz val="11"/><color theme="1"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)",
        "bold font XML mismatch with default metadata");
    check_contains(styles_xml, R"(<fills count="2">)",
        "default metadata style should keep default fills only");
    check_contains(styles_xml,
        R"(<cellXfs count="3"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>)",
        "default metadata style should not create extra cell formats");
    check_contains(styles_xml,
        R"(<xf numFmtId="164" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/>)",
        "number style with default metadata should reuse the number format xf");
    check_contains(styles_xml,
        R"(<xf numFmtId="0" fontId="1" fillId="0" borderId="0" xfId="0" applyFont="1"/>)",
        "bold style with default metadata should reuse the bold font xf");
    check(styles_xml.find("applyAlignment=\"1\"") == std::string::npos,
        "all-default alignment metadata should not apply alignment");
    check(styles_xml.find("<alignment") == std::string::npos,
        "all-default alignment metadata should not create alignment XML");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D2"/>)",
        "default metadata worksheet dimension mismatch");
    check_contains(worksheet_xml, R"(<c r="A2" s="1"><v>12.5</v></c>)",
        "number style cell mismatch");
    check_contains(worksheet_xml, R"(<c r="B2" s="1"><v>42.5</v></c>)",
        "number style with all-default metadata should reuse s=\"1\"");
    check_contains(worksheet_xml,
        R"(<c r="C2" s="2" t="inlineStr"><is><t>bold</t></is></c>)",
        "bold style cell mismatch");
    check_contains(worksheet_xml,
        R"(<c r="D2" s="2" t="inlineStr"><is><t>bold defaults</t></is></c>)",
        "bold style with all-default alignment should reuse s=\"2\"");
    check(worksheet_xml.find("s=\"0\"") == std::string::npos,
        "default metadata style test should not serialize s=\"0\"");
}

void test_streaming_writer_styles_with_relationship_metadata()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-styles-relationship-metadata.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    const auto number_style = workbook.add_style(fastxlsx::CellStyle {"0.0"});
    auto sheet = workbook.add_worksheet("StyledObjects");

    sheet.append_row({
        fastxlsx::CellView::text("Name"),
        fastxlsx::CellView::text("Qty"),
    });
    sheet.append_row({
        fastxlsx::CellView::text("Widget"),
        fastxlsx::CellView::number(7.0).with_style(number_style),
    });
    sheet.add_external_hyperlink(2, 1, "https://example.com/styled-widget");

    fastxlsx::TableOptions table;
    table.name = "StyledObjectTable";
    table.column_names = {"Name", "Qty"};
    sheet.add_table({1, 1, 2, 2}, table);

    workbook.close();
    check(std::filesystem::exists(output_path),
        "styles with relationship metadata xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/styles.xml"), "registered style should create styles.xml");
    check(entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "hyperlink and table should create worksheet relationships");
    check(entries.contains("xl/tables/table1.xml"), "table part should still be generated");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(count_occurrences(workbook_rels, "<Relationship ") == 2,
        "styles sample workbook relationship count mismatch");
    check_contains(workbook_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)",
        "styles relationship should remain workbook-local");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)",
        "relationship-backed metadata should declare the worksheet relationship namespace");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "styled relationship metadata worksheet dimension mismatch");
    check_contains(worksheet_xml, R"(<c r="B2" s="1"><v>7</v></c>)",
        "styled number cell should keep its style id next to metadata");
    check_contains(worksheet_xml,
        "</sheetData><hyperlinks><hyperlink ref=\"A2\" r:id=\"rId1\"/></hyperlinks>"
        "<tableParts count=\"1\"><tablePart r:id=\"rId2\"/></tableParts></worksheet>",
        "worksheet relationship metadata should keep owner-local rId order");
    check(worksheet_xml.find("s=\"0\"") == std::string::npos,
        "styles with relationship metadata should not serialize s=\"0\"");

    const auto& worksheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check(count_occurrences(worksheet_rels, "<Relationship ") == 2,
        "worksheet relationship count should only include hyperlink and table");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/styled-widget" TargetMode="External"/>)",
        "external hyperlink should keep the first worksheet rId");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)",
        "table should keep the second worksheet rId");
    check(worksheet_rels.find("styles") == std::string::npos,
        "styles relationship must not be written to worksheet relationships");
}

void test_streaming_writer_invalid_style_registration()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-style-registration-errors.xlsx";
    auto workbook = fastxlsx::WorkbookWriter::create(output_path);

    check_fastxlsx_error(
        [&workbook] { static_cast<void>(workbook.add_style(fastxlsx::CellStyle {""})); },
        "add_style should reject an empty style");

    fastxlsx::CellStyle false_alignment_style;
    false_alignment_style.alignment = fastxlsx::CellAlignment {};
    check_fastxlsx_error(
        [&workbook, false_alignment_style] {
            static_cast<void>(workbook.add_style(false_alignment_style));
        },
        "add_style should reject alignment metadata without a supported property");

    fastxlsx::CellStyle invalid_horizontal_alignment_style;
    fastxlsx::CellAlignment invalid_horizontal_alignment;
    invalid_horizontal_alignment.horizontal = static_cast<fastxlsx::HorizontalAlignment>(99);
    invalid_horizontal_alignment_style.alignment = invalid_horizontal_alignment;
    check_fastxlsx_error(
        [&workbook, invalid_horizontal_alignment_style] {
            static_cast<void>(workbook.add_style(invalid_horizontal_alignment_style));
        },
        "add_style should reject unsupported horizontal alignment");

    fastxlsx::CellStyle invalid_vertical_alignment_style;
    fastxlsx::CellAlignment invalid_vertical_alignment;
    invalid_vertical_alignment.vertical = static_cast<fastxlsx::VerticalAlignment>(99);
    invalid_vertical_alignment_style.alignment = invalid_vertical_alignment;
    check_fastxlsx_error(
        [&workbook, invalid_vertical_alignment_style] {
            static_cast<void>(workbook.add_style(invalid_vertical_alignment_style));
        },
        "add_style should reject unsupported vertical alignment");

    fastxlsx::CellStyle false_font_style;
    false_font_style.font = fastxlsx::CellFont {};
    check_fastxlsx_error(
        [&workbook, false_font_style] { static_cast<void>(workbook.add_style(false_font_style)); },
        "add_style should reject font metadata without a supported property");

    const auto valid_style = workbook.add_style(fastxlsx::CellStyle {"0.0"});
    check(valid_style.value() == 1,
        "failed style registrations should not consume workbook style ids");

    workbook.add_worksheet("Registration")
        .append_row({
            fastxlsx::CellView::number(12.5).with_style(valid_style),
            fastxlsx::CellView::text("done"),
        });
    workbook.close();

    check_fastxlsx_error(
        [&workbook] { static_cast<void>(workbook.add_style(fastxlsx::CellStyle {"0.0"})); },
        "add_style should reject mutation after close");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/styles.xml"),
        "valid style after failed registrations should still create styles.xml");

    const auto& styles_xml = entries.at("xl/styles.xml");
    check_contains(styles_xml, R"(<numFmts count="1">)",
        "failed style registrations should not create custom number formats");
    check_contains(styles_xml, R"(<numFmt numFmtId="164" formatCode="0.0"/>)",
        "valid style should receive the first custom number format id");
    check_contains(styles_xml, R"(<fonts count="1">)",
        "failed font registration should not create a custom font");
    check_contains(styles_xml, R"(<fills count="2">)",
        "failed style registrations should keep only default fills");
    check_contains(styles_xml,
        R"(<cellXfs count="2"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>)",
        "failed style registrations should not create extra cell formats");
    check_contains(styles_xml,
        R"(<xf numFmtId="164" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/>)",
        "valid style after failed registrations should be the only custom cell format");
    check(styles_xml.find("<alignment") == std::string::npos,
        "failed alignment registrations should not create alignment XML");
    check(styles_xml.find("applyFont=\"1\"") == std::string::npos,
        "failed font registration should not apply a custom font");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B1"/>)",
        "style registration failure test worksheet dimension mismatch");
    check_contains(worksheet_xml, R"(<c r="A1" s="1"><v>12.5</v></c>)",
        "valid style after failed registrations should serialize as s=\"1\"");
    check_contains(worksheet_xml,
        R"(<c r="B1" t="inlineStr"><is><t>done</t></is></c>)",
        "plain cell after failed registrations should remain unstyled");
    check(worksheet_xml.find("s=\"0\"") == std::string::npos,
        "failed style registration test should not serialize s=\"0\"");
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

void test_streaming_writer_conditional_formatting_two_color_scale()
{
    const auto output_path =
        fastxlsx::test::artifact_dir()
        / "fastxlsx-streaming-conditional-formatting-two-color-scale.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("ColorScale");

    sheet.append_row({fastxlsx::CellView::text("Score")});
    for (int value = 1; value <= 9; ++value) {
        sheet.append_row({fastxlsx::CellView::number(static_cast<double>(value))});
    }

    sheet.add_conditional_color_scale(
        {2, 1, 10, 1},
        make_two_color_scale(
            fastxlsx::ArgbColor {0xFF, 0xFF, 0x00, 0x00},
            fastxlsx::ArgbColor {0xFF, 0x00, 0xB0, 0x50}));

    workbook.close();
    check(std::filesystem::exists(output_path),
        "conditional formatting color scale xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"),
        "missing conditional formatting worksheet");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "conditional formatting should not create worksheet relationships");
    check(!entries.contains("xl/styles.xml"),
        "two-color scale should not create styles.xml or dxfs");
    check(!entries.contains("xl/metadata.xml"),
        "two-color scale should not create metadata part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("conditionalFormatting") == std::string::npos,
        "conditional formatting should not add content type overrides");
    check(content_types.find("styles.xml") == std::string::npos,
        "two-color scale should not add styles content type");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(count_occurrences(workbook_rels, "<Relationship ") == 1,
        "two-color scale should not add workbook relationships");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check(workbook_xml.find("<calcPr") == std::string::npos,
        "two-color scale should not request calculation metadata");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(worksheet_xml.find("xmlns:r=") == std::string::npos,
        "conditional formatting-only worksheet should not declare relationship namespace");
    check_contains(worksheet_xml, "<dimension ref=\"A1:A10\"/>",
        "conditional formatting worksheet dimension mismatch");
    check_contains(worksheet_xml,
        "</sheetData><conditionalFormatting sqref=\"A2:A10\">"
        "<cfRule type=\"colorScale\" priority=\"1\"><colorScale>"
        "<cfvo type=\"min\"/><cfvo type=\"max\"/>"
        "<color rgb=\"FFFF0000\"/><color rgb=\"FF00B050\"/>"
        "</colorScale></cfRule></conditionalFormatting></worksheet>",
        "two-color scale XML mismatch");
    check(count_occurrences(worksheet_xml, "<conditionalFormatting ") == 1,
        "conditional formatting element count mismatch");
    check(count_occurrences(worksheet_xml, "<cfRule ") == 1,
        "conditional formatting rule count mismatch");
    check(count_occurrences(worksheet_xml, "<cfvo ") == 2,
        "two-color scale cfvo count mismatch");
    check(count_occurrences(worksheet_xml, "<color rgb=") == 2,
        "two-color scale color count mismatch");
}

void test_streaming_writer_conditional_formatting_three_color_scale()
{
    const auto output_path =
        fastxlsx::test::artifact_dir()
        / "fastxlsx-streaming-conditional-formatting-three-color-scale.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("ThreeColorScale");

    sheet.append_row({fastxlsx::CellView::text("Score")});
    for (int value = 1; value <= 9; ++value) {
        sheet.append_row({fastxlsx::CellView::number(static_cast<double>(value))});
    }

    sheet.add_conditional_color_scale(
        {2, 1, 10, 1},
        make_three_color_scale(
            fastxlsx::ArgbColor {0xFF, 0xF8, 0x69, 0x6B},
            fastxlsx::ArgbColor {0xFF, 0xFF, 0xEB, 0x84},
            fastxlsx::ArgbColor {0xFF, 0x63, 0xBE, 0x7B}));

    workbook.close();
    check(std::filesystem::exists(output_path),
        "conditional formatting three-color scale xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"),
        "missing conditional formatting three-color worksheet");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "three-color scale should not create worksheet relationships");
    check(!entries.contains("xl/styles.xml"),
        "three-color scale should not create styles");
    check(!entries.contains("xl/metadata.xml"),
        "three-color scale should not create metadata part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("styles") == std::string::npos,
        "three-color scale should not add style content types");
    check(content_types.find("conditionalFormatting") == std::string::npos,
        "three-color scale should not add conditional formatting content types");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(workbook_rels.find("styles") == std::string::npos,
        "three-color scale should not add styles workbook relationship");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(worksheet_xml.find("xmlns:r=") == std::string::npos,
        "three-color scale worksheet should not declare relationship namespace");
    check_contains(worksheet_xml, "<dimension ref=\"A1:A10\"/>",
        "three-color scale worksheet dimension mismatch");
    check_contains(worksheet_xml,
        "</sheetData><conditionalFormatting sqref=\"A2:A10\">"
        "<cfRule type=\"colorScale\" priority=\"1\"><colorScale>"
        "<cfvo type=\"min\"/><cfvo type=\"percentile\" val=\"50\"/><cfvo type=\"max\"/>"
        "<color rgb=\"FFF8696B\"/><color rgb=\"FFFFEB84\"/><color rgb=\"FF63BE7B\"/>"
        "</colorScale></cfRule></conditionalFormatting></worksheet>",
        "three-color scale XML mismatch");
    check(count_occurrences(worksheet_xml, "<conditionalFormatting ") == 1,
        "three-color conditional formatting element count mismatch");
    check(count_occurrences(worksheet_xml, "<cfRule ") == 1,
        "three-color conditional formatting rule count mismatch");
    check(count_occurrences(worksheet_xml, "<cfvo ") == 3,
        "three-color scale cfvo count mismatch");
    check(count_occurrences(worksheet_xml, "<color rgb=") == 3,
        "three-color scale color count mismatch");
}

void test_streaming_writer_conditional_formatting_metadata_order()
{
    const auto output_path =
        fastxlsx::test::artifact_dir()
        / "fastxlsx-streaming-conditional-formatting-metadata-order.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("Objects");

    sheet.append_row({
        fastxlsx::CellView::text("Value"),
        fastxlsx::CellView::text("Link"),
    });
    sheet.append_row({
        fastxlsx::CellView::number(5.0),
        fastxlsx::CellView::text("Docs"),
    });
    sheet.append_row({
        fastxlsx::CellView::number(7.0),
        fastxlsx::CellView::text("More"),
    });

    sheet.merge_cells({4, 1, 4, 2});
    sheet.add_conditional_color_scale(
        {2, 1, 10, 1},
        make_two_color_scale(
            fastxlsx::ArgbColor {0xFF, 0xFF, 0x00, 0x00},
            fastxlsx::ArgbColor {0xFF, 0x00, 0xB0, 0x50}));

    fastxlsx::DataValidationRule whole;
    whole.type = fastxlsx::DataValidationType::Whole;
    whole.operator_type = fastxlsx::DataValidationOperator::Between;
    whole.formula1 = "1";
    whole.formula2 = "10";
    sheet.add_data_validation({2, 1, 10, 1}, whole);

    sheet.add_external_hyperlink(2, 2, "https://example.com/docs");

    fastxlsx::TableOptions table;
    table.name = "ConditionalFormatTable";
    table.column_names = {"Value", "Link"};
    sheet.add_table({1, 1, 3, 2}, table);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "relationship-backed metadata should still create worksheet relationships");
    check(entries.contains("xl/tables/table1.xml"),
        "conditional formatting metadata order test should create table part");
    check(!entries.contains("xl/styles.xml"),
        "conditional formatting with other metadata should not create styles");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:B3\"/>",
        "conditional formatting metadata should not expand worksheet dimension");
    check_contains(worksheet_xml,
        "</sheetData><mergeCells count=\"1\"><mergeCell ref=\"A4:B4\"/></mergeCells>"
        "<conditionalFormatting sqref=\"A2:A10\">"
        "<cfRule type=\"colorScale\" priority=\"1\"><colorScale>"
        "<cfvo type=\"min\"/><cfvo type=\"max\"/>"
        "<color rgb=\"FFFF0000\"/><color rgb=\"FF00B050\"/>"
        "</colorScale></cfRule></conditionalFormatting>"
        "<dataValidations count=\"1\"><dataValidation type=\"whole\" operator=\"between\" "
        "sqref=\"A2:A10\"><formula1>1</formula1><formula2>10</formula2></dataValidation>"
        "</dataValidations><hyperlinks><hyperlink ref=\"B2\" r:id=\"rId1\"/></hyperlinks>"
        "<tableParts count=\"1\"><tablePart r:id=\"rId2\"/></tableParts></worksheet>",
        "conditional formatting suffix ordering or relationship ids mismatch");

    const auto& worksheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check(count_occurrences(worksheet_rels, "<Relationship ") == 2,
        "conditional formatting should not add worksheet relationship entries");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/docs" TargetMode="External"/>)",
        "hyperlink relationship id should remain rId1");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)",
        "table relationship id should remain rId2 after conditional formatting");
}

void test_streaming_writer_conditional_formatting_multi_range_sqref()
{
    const auto output_path =
        fastxlsx::test::artifact_dir()
        / "fastxlsx-streaming-conditional-formatting-multi-range.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("ColorScaleRanges");

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
    sheet.append_row({
        fastxlsx::CellView::number(4.0),
        fastxlsx::CellView::text("gap"),
        fastxlsx::CellView::number(5.0),
        fastxlsx::CellView::text("gap"),
        fastxlsx::CellView::number(6.0),
    });

    sheet.add_conditional_color_scale(
        {{2, 1, 3, 1}, {2, 3, 3, 3}, {2, 5, 3, 5}},
        make_two_color_scale(
            fastxlsx::ArgbColor {0xFF, 0xFF, 0x00, 0x00},
            fastxlsx::ArgbColor {0xFF, 0x00, 0xB0, 0x50}));

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"), "missing multi-range color scale worksheet");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "multi-range color scale should not create worksheet relationships");
    check(!entries.contains("xl/styles.xml"),
        "multi-range color scale should not create styles");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:E3\"/>",
        "multi-range color scale dimension mismatch");
    check_contains(worksheet_xml,
        "<conditionalFormatting sqref=\"A2:A3 C2:C3 E2:E3\">"
        "<cfRule type=\"colorScale\" priority=\"1\"><colorScale>"
        "<cfvo type=\"min\"/><cfvo type=\"max\"/>"
        "<color rgb=\"FFFF0000\"/><color rgb=\"FF00B050\"/>"
        "</colorScale></cfRule></conditionalFormatting>",
        "multi-range color scale sqref XML mismatch");
    check(count_occurrences(worksheet_xml, "<conditionalFormatting ") == 1,
        "multi-range color scale should be one conditionalFormatting element");
}

void test_streaming_writer_conditional_formatting_priorities()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-conditional-formatting-priorities.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto first = workbook.add_worksheet("First");
    auto second = workbook.add_worksheet("Second");
    auto plain = workbook.add_worksheet("Plain");

    first.append_row({fastxlsx::CellView::text("Value")});
    first.append_row({fastxlsx::CellView::number(1.0)});
    first.append_row({fastxlsx::CellView::number(2.0)});
    first.add_conditional_color_scale(
        {2, 1, 3, 1},
        make_two_color_scale(
            fastxlsx::ArgbColor {0xFF, 0xF8, 0x69, 0x6B},
            fastxlsx::ArgbColor {0xFF, 0x63, 0xBE, 0x7B}));
    fastxlsx::TwoColorScaleRule numeric_rule;
    numeric_rule.lower = {
        fastxlsx::ColorScaleValueType::Number,
        0.0,
        fastxlsx::ArgbColor {0xFF, 0xFF, 0xEB, 0x84},
    };
    numeric_rule.upper = {
        fastxlsx::ColorScaleValueType::Percentile,
        90.0,
        fastxlsx::ArgbColor {0xFF, 0x5A, 0x8A, 0xD6},
    };
    first.add_conditional_color_scale({2, 1, 3, 1}, numeric_rule);

    second.append_row({fastxlsx::CellView::text("Value")});
    second.append_row({fastxlsx::CellView::number(3.0)});
    second.add_conditional_color_scale(
        {2, 1, 2, 1},
        make_two_color_scale(
            fastxlsx::ArgbColor {0xFF, 0xFF, 0x00, 0x00},
            fastxlsx::ArgbColor {0xFF, 0x00, 0xB0, 0x50}));

    plain.append_row({fastxlsx::CellView::text("No formatting")});

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& first_xml = entries.at("xl/worksheets/sheet1.xml");
    check(count_occurrences(first_xml, "<conditionalFormatting ") == 2,
        "first worksheet conditional formatting count mismatch");
    check_contains(first_xml, "priority=\"1\"", "first worksheet missing first priority");
    check_contains(first_xml, "priority=\"2\"", "first worksheet missing second priority");
    check_contains(first_xml,
        "<cfvo type=\"num\" val=\"0\"/><cfvo type=\"percentile\" val=\"90\"/>"
        "<color rgb=\"FFFFEB84\"/><color rgb=\"FF5A8AD6\"/>",
        "number/percentile color scale endpoint XML mismatch");

    const auto& second_xml = entries.at("xl/worksheets/sheet2.xml");
    check(count_occurrences(second_xml, "<conditionalFormatting ") == 1,
        "second worksheet conditional formatting count mismatch");
    check_contains(second_xml, "priority=\"1\"", "second worksheet priority should reset to 1");
    check(second_xml.find("priority=\"2\"") == std::string::npos,
        "second worksheet should not inherit first worksheet priority");

    const auto& plain_xml = entries.at("xl/worksheets/sheet3.xml");
    check(plain_xml.find("<conditionalFormatting") == std::string::npos,
        "plain worksheet should not contain conditional formatting");
    check(plain_xml.find("xmlns:r=") == std::string::npos,
        "plain worksheet should not declare relationship namespace");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "conditional formatting-only first sheet should not create relationships");
    check(!entries.contains("xl/worksheets/_rels/sheet2.xml.rels"),
        "conditional formatting-only second sheet should not create relationships");
}

void test_streaming_writer_conditional_formatting_failed_call_preserves_priority()
{
    const auto output_path = fastxlsx::test::artifact_dir()
        / "fastxlsx-streaming-conditional-formatting-failed-call-priority.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("Priority");

    sheet.append_row({fastxlsx::CellView::text("Value")});
    sheet.append_row({fastxlsx::CellView::number(42.0)});

    const auto valid_rule = make_two_color_scale(
        fastxlsx::ArgbColor {0xFF, 0xF8, 0x69, 0x6B},
        fastxlsx::ArgbColor {0xFF, 0x63, 0xBE, 0x7B});
    sheet.add_conditional_color_scale({2, 1, 2, 1}, valid_rule);

    fastxlsx::TwoColorScaleRule invalid_rule = valid_rule;
    invalid_rule.upper.type = fastxlsx::ColorScaleValueType::Minimum;
    check_fastxlsx_error(
        [&sheet, &invalid_rule] {
            sheet.add_conditional_color_scale({2, 1, 2, 1}, invalid_rule);
        },
        "failed conditional formatting call should not mutate priority state");

    fastxlsx::DataBarRule invalid_data_bar = make_data_bar(fastxlsx::ArgbColor {0xFF, 0x63, 0x8E, 0xC6});
    invalid_data_bar.lower.type = fastxlsx::DataBarValueType::Maximum;
    check_fastxlsx_error(
        [&sheet, &invalid_data_bar] {
            sheet.add_conditional_data_bar({2, 1, 2, 1}, invalid_data_bar);
        },
        "failed data bar conditional formatting call should not mutate priority state");

    fastxlsx::IconSetRule invalid_icon_set = make_icon_set();
    invalid_icon_set.thresholds = {0.0, 67.0, 33.0};
    check_fastxlsx_error(
        [&sheet, &invalid_icon_set] {
            sheet.add_conditional_icon_set({2, 1, 2, 1}, invalid_icon_set);
        },
        "failed icon set conditional formatting call should not mutate priority state");

    fastxlsx::TwoColorScaleRule percent_rule;
    percent_rule.lower = {
        fastxlsx::ColorScaleValueType::Percent,
        10.0,
        fastxlsx::ArgbColor {0xFF, 0xFF, 0xEB, 0x84},
    };
    percent_rule.upper = {
        fastxlsx::ColorScaleValueType::Percent,
        90.0,
        fastxlsx::ArgbColor {0xFF, 0x5A, 0x8A, 0xD6},
    };
    sheet.add_conditional_color_scale({2, 1, 2, 1}, percent_rule);
    sheet.add_conditional_data_bar(
        {2, 1, 2, 1}, make_data_bar(fastxlsx::ArgbColor {0xFF, 0x63, 0x8E, 0xC6}));
    sheet.add_conditional_icon_set({2, 1, 2, 1}, make_icon_set());

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "conditional formatting failed-call sample should not create worksheet relationships");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:A2\"/>",
        "conditional formatting ranges should not expand worksheet dimension");
    check(count_occurrences(worksheet_xml, "<conditionalFormatting ") == 4,
        "failed conditional formatting call should not add a rule");
    check_contains(worksheet_xml, "<cfRule type=\"colorScale\" priority=\"1\">",
        "first successful conditional formatting priority mismatch");
    check_contains(worksheet_xml, "<cfRule type=\"colorScale\" priority=\"2\">",
        "second successful conditional formatting priority mismatch");
    check_contains(worksheet_xml, "<cfRule type=\"dataBar\" priority=\"3\">",
        "third successful data bar conditional formatting priority mismatch");
    check_contains(worksheet_xml, "<cfRule type=\"iconSet\" priority=\"4\">",
        "fourth successful icon set conditional formatting priority mismatch");
    check(worksheet_xml.find("priority=\"5\"") == std::string::npos,
        "failed conditional formatting call should not consume a priority");
    check_contains(worksheet_xml,
        "<cfvo type=\"percent\" val=\"10\"/><cfvo type=\"percent\" val=\"90\"/>"
        "<color rgb=\"FFFFEB84\"/><color rgb=\"FF5A8AD6\"/>",
        "percent endpoint color scale XML mismatch");
    check_contains(worksheet_xml,
        "<cfRule type=\"dataBar\" priority=\"3\"><dataBar>"
        "<cfvo type=\"min\"/><cfvo type=\"max\"/>"
        "<color rgb=\"FF638EC6\"/></dataBar></cfRule>",
        "valid data bar after failed calls XML mismatch");
    check_contains(worksheet_xml,
        "<cfRule type=\"iconSet\" priority=\"4\"><iconSet iconSet=\"3Arrows\">"
        "<cfvo type=\"percent\" val=\"0\"/><cfvo type=\"percent\" val=\"33\"/>"
        "<cfvo type=\"percent\" val=\"67\"/></iconSet></cfRule>",
        "valid icon set after failed calls XML mismatch");
}

void test_streaming_writer_conditional_formatting_data_bar()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-conditional-formatting-data-bar.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("DataBar");

    sheet.append_row({fastxlsx::CellView::text("Score")});
    for (int value = 1; value <= 9; ++value) {
        sheet.append_row({fastxlsx::CellView::number(static_cast<double>(value))});
    }

    sheet.add_conditional_data_bar(
        {2, 1, 10, 1}, make_data_bar(fastxlsx::ArgbColor {0xFF, 0x63, 0x8E, 0xC6}));

    workbook.close();
    check(std::filesystem::exists(output_path),
        "conditional formatting data bar xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"),
        "missing conditional formatting data bar worksheet");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "data bar should not create worksheet relationships");
    check(!entries.contains("xl/styles.xml"), "data bar should not create styles");
    check(!entries.contains("xl/metadata.xml"), "data bar should not create metadata part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("styles") == std::string::npos,
        "data bar should not add style content types");
    check(content_types.find("conditionalFormatting") == std::string::npos,
        "data bar should not add conditional formatting content types");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(workbook_rels.find("styles") == std::string::npos,
        "data bar should not add styles workbook relationship");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(worksheet_xml.find("xmlns:r=") == std::string::npos,
        "data bar worksheet should not declare relationship namespace");
    check_contains(worksheet_xml, "<dimension ref=\"A1:A10\"/>",
        "data bar worksheet dimension mismatch");
    check_contains(worksheet_xml,
        "</sheetData><conditionalFormatting sqref=\"A2:A10\">"
        "<cfRule type=\"dataBar\" priority=\"1\"><dataBar>"
        "<cfvo type=\"min\"/><cfvo type=\"max\"/>"
        "<color rgb=\"FF638EC6\"/></dataBar></cfRule></conditionalFormatting></worksheet>",
        "data bar XML mismatch");
    check(count_occurrences(worksheet_xml, "<conditionalFormatting ") == 1,
        "data bar conditional formatting element count mismatch");
    check(count_occurrences(worksheet_xml, "<cfRule ") == 1,
        "data bar conditional formatting rule count mismatch");
    check(count_occurrences(worksheet_xml, "<cfvo ") == 2, "data bar cfvo count mismatch");
    check(count_occurrences(worksheet_xml, "<color rgb=") == 1, "data bar color count mismatch");
}

void test_streaming_writer_conditional_formatting_data_bar_metadata_order()
{
    const auto output_path = fastxlsx::test::artifact_dir()
        / "fastxlsx-streaming-conditional-formatting-data-bar-metadata-order.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("DataBarObjects");

    sheet.append_row({fastxlsx::CellView::text("Value"), fastxlsx::CellView::text("Link")});
    sheet.append_row({fastxlsx::CellView::number(5.0), fastxlsx::CellView::text("Docs")});
    sheet.append_row({fastxlsx::CellView::number(7.0), fastxlsx::CellView::text("More")});

    sheet.merge_cells({4, 1, 4, 2});
    auto data_bar_rule = make_data_bar(fastxlsx::ArgbColor {0xFF, 0x63, 0x8E, 0xC6});
    data_bar_rule.show_value = false;
    sheet.add_conditional_data_bar({2, 1, 10, 1}, data_bar_rule);

    fastxlsx::DataValidationRule whole;
    whole.type = fastxlsx::DataValidationType::Whole;
    whole.operator_type = fastxlsx::DataValidationOperator::Between;
    whole.formula1 = "1";
    whole.formula2 = "10";
    sheet.add_data_validation({2, 1, 10, 1}, whole);
    sheet.add_external_hyperlink(2, 2, "https://example.com/docs");

    fastxlsx::TableOptions table;
    table.name = "DataBarTable";
    table.column_names = {"Value", "Link"};
    sheet.add_table({1, 1, 3, 2}, table);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "data bar relationship-backed metadata should still create worksheet relationships");
    check(entries.contains("xl/tables/table1.xml"), "data bar metadata order test should create table part");
    check(!entries.contains("xl/styles.xml"),
        "data bar with other metadata should not create styles");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:B3\"/>",
        "data bar metadata should not expand worksheet dimension");
    check_contains(worksheet_xml,
        "</sheetData><mergeCells count=\"1\"><mergeCell ref=\"A4:B4\"/></mergeCells>"
        "<conditionalFormatting sqref=\"A2:A10\">"
        "<cfRule type=\"dataBar\" priority=\"1\"><dataBar showValue=\"0\">"
        "<cfvo type=\"min\"/><cfvo type=\"max\"/>"
        "<color rgb=\"FF638EC6\"/></dataBar></cfRule></conditionalFormatting>"
        "<dataValidations count=\"1\"><dataValidation type=\"whole\" operator=\"between\" "
        "sqref=\"A2:A10\"><formula1>1</formula1><formula2>10</formula2></dataValidation>"
        "</dataValidations><hyperlinks><hyperlink ref=\"B2\" r:id=\"rId1\"/></hyperlinks>"
        "<tableParts count=\"1\"><tablePart r:id=\"rId2\"/></tableParts></worksheet>",
        "data bar suffix ordering or relationship ids mismatch");

    const auto& worksheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check(count_occurrences(worksheet_rels, "<Relationship ") == 2,
        "data bar should not add worksheet relationship entries");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/docs" TargetMode="External"/>)",
        "data bar hyperlink relationship id should remain rId1");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)",
        "data bar table relationship id should remain rId2");
}

void test_streaming_writer_conditional_formatting_data_bar_multi_range_sqref()
{
    const auto output_path = fastxlsx::test::artifact_dir()
        / "fastxlsx-streaming-conditional-formatting-data-bar-multi-range.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("DataBarRanges");

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
    sheet.append_row({
        fastxlsx::CellView::number(4.0),
        fastxlsx::CellView::text("gap"),
        fastxlsx::CellView::number(5.0),
        fastxlsx::CellView::text("gap"),
        fastxlsx::CellView::number(6.0),
    });

    sheet.add_conditional_data_bar(
        {{2, 1, 3, 1}, {2, 3, 3, 3}, {2, 5, 3, 5}},
        make_data_bar(fastxlsx::ArgbColor {0xFF, 0x63, 0x8E, 0xC6}));

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "multi-range data bar should not create worksheet relationships");
    check(!entries.contains("xl/styles.xml"), "multi-range data bar should not create styles");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:E3\"/>",
        "multi-range data bar dimension mismatch");
    check_contains(worksheet_xml,
        "<conditionalFormatting sqref=\"A2:A3 C2:C3 E2:E3\">"
        "<cfRule type=\"dataBar\" priority=\"1\"><dataBar>"
        "<cfvo type=\"min\"/><cfvo type=\"max\"/>"
        "<color rgb=\"FF638EC6\"/></dataBar></cfRule></conditionalFormatting>",
        "multi-range data bar sqref XML mismatch");
    check(count_occurrences(worksheet_xml, "<conditionalFormatting ") == 1,
        "multi-range data bar should be one conditionalFormatting element");
}

void test_streaming_writer_conditional_formatting_data_bar_priorities()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-conditional-formatting-data-bar-priorities.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto first = workbook.add_worksheet("First");
    auto second = workbook.add_worksheet("Second");

    first.append_row({fastxlsx::CellView::text("Value")});
    first.append_row({fastxlsx::CellView::number(1.0)});
    first.append_row({fastxlsx::CellView::number(2.0)});
    first.add_conditional_color_scale(
        {2, 1, 3, 1},
        make_two_color_scale(
            fastxlsx::ArgbColor {0xFF, 0xF8, 0x69, 0x6B},
            fastxlsx::ArgbColor {0xFF, 0x63, 0xBE, 0x7B}));
    fastxlsx::DataBarRule numeric_rule = make_data_bar(fastxlsx::ArgbColor {0xFF, 0x63, 0x8E, 0xC6});
    numeric_rule.lower = {fastxlsx::DataBarValueType::Number, 0.0};
    numeric_rule.upper = {fastxlsx::DataBarValueType::Percentile, 90.0};
    first.add_conditional_data_bar({2, 1, 3, 1}, numeric_rule);

    second.append_row({fastxlsx::CellView::text("Value")});
    second.append_row({fastxlsx::CellView::number(3.0)});
    second.add_conditional_data_bar(
        {2, 1, 2, 1}, make_data_bar(fastxlsx::ArgbColor {0xFF, 0x63, 0x8E, 0xC6}));

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& first_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<cfRule type="colorScale" priority="1">)",
        "color scale priority should remain first");
    check_contains(first_xml, R"(<cfRule type="dataBar" priority="2">)",
        "data bar should share conditional formatting priority sequence");
    check_contains(first_xml,
        "<cfvo type=\"num\" val=\"0\"/><cfvo type=\"percentile\" val=\"90\"/>"
        "<color rgb=\"FF638EC6\"/>",
        "numeric/percentile data bar endpoint XML mismatch");

    const auto& second_xml = entries.at("xl/worksheets/sheet2.xml");
    check_contains(second_xml, R"(<cfRule type="dataBar" priority="1">)",
        "data bar priority should reset per worksheet");
    check(second_xml.find("priority=\"2\"") == std::string::npos,
        "second worksheet data bar should not inherit first worksheet priority");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "data bar priorities first sheet should not create relationships");
    check(!entries.contains("xl/worksheets/_rels/sheet2.xml.rels"),
        "data bar priorities second sheet should not create relationships");
}

void test_streaming_writer_conditional_formatting_icon_set()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-conditional-formatting-icon-set.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("IconSet");

    sheet.append_row({fastxlsx::CellView::text("Score")});
    for (int value = 1; value <= 9; ++value) {
        sheet.append_row({fastxlsx::CellView::number(static_cast<double>(value))});
    }

    sheet.add_conditional_icon_set({2, 1, 10, 1}, make_icon_set());

    workbook.close();
    check(std::filesystem::exists(output_path),
        "conditional formatting icon set xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"),
        "missing conditional formatting icon set worksheet");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "icon set should not create worksheet relationships");
    check(!entries.contains("xl/styles.xml"), "icon set should not create styles");
    check(!entries.contains("xl/metadata.xml"), "icon set should not create metadata part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("styles") == std::string::npos,
        "icon set should not add style content types");
    check(content_types.find("conditionalFormatting") == std::string::npos,
        "icon set should not add conditional formatting content types");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check(workbook_xml.find("<calcPr") == std::string::npos,
        "icon set should not request recalculation");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(workbook_rels.find("styles") == std::string::npos,
        "icon set should not add styles workbook relationship");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(worksheet_xml.find("xmlns:r=") == std::string::npos,
        "icon set worksheet should not declare relationship namespace");
    check_contains(worksheet_xml, "<dimension ref=\"A1:A10\"/>",
        "icon set worksheet dimension mismatch");
    check_contains(worksheet_xml,
        "</sheetData><conditionalFormatting sqref=\"A2:A10\">"
        "<cfRule type=\"iconSet\" priority=\"1\"><iconSet iconSet=\"3Arrows\">"
        "<cfvo type=\"percent\" val=\"0\"/><cfvo type=\"percent\" val=\"33\"/>"
        "<cfvo type=\"percent\" val=\"67\"/></iconSet></cfRule></conditionalFormatting></worksheet>",
        "icon set XML mismatch");
    check(count_occurrences(worksheet_xml, "<conditionalFormatting ") == 1,
        "icon set conditional formatting element count mismatch");
    check(count_occurrences(worksheet_xml, "<cfRule ") == 1,
        "icon set conditional formatting rule count mismatch");
    check(count_occurrences(worksheet_xml, "<cfvo ") == 3, "icon set cfvo count mismatch");
    check(count_occurrences(worksheet_xml, "<iconSet ") == 1, "icon set element count mismatch");
}

void test_streaming_writer_conditional_formatting_icon_set_metadata_order()
{
    const auto output_path = fastxlsx::test::artifact_dir()
        / "fastxlsx-streaming-conditional-formatting-icon-set-metadata-order.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("IconSetObjects");

    sheet.append_row({fastxlsx::CellView::text("Value"), fastxlsx::CellView::text("Link")});
    sheet.append_row({fastxlsx::CellView::number(5.0), fastxlsx::CellView::text("Docs")});
    sheet.append_row({fastxlsx::CellView::number(7.0), fastxlsx::CellView::text("More")});

    fastxlsx::IconSetRule rule = make_icon_set();
    rule.show_value = false;
    rule.reverse = true;

    sheet.merge_cells({4, 1, 4, 2});
    sheet.add_conditional_icon_set({2, 1, 10, 1}, rule);

    fastxlsx::DataValidationRule whole;
    whole.type = fastxlsx::DataValidationType::Whole;
    whole.operator_type = fastxlsx::DataValidationOperator::Between;
    whole.formula1 = "1";
    whole.formula2 = "10";
    sheet.add_data_validation({2, 1, 10, 1}, whole);
    sheet.add_external_hyperlink(2, 2, "https://example.com/docs");

    fastxlsx::TableOptions table;
    table.name = "IconSetTable";
    table.column_names = {"Value", "Link"};
    sheet.add_table({1, 1, 3, 2}, table);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "icon set relationship-backed metadata should still create worksheet relationships");
    check(entries.contains("xl/tables/table1.xml"), "icon set metadata order test should create table part");
    check(!entries.contains("xl/styles.xml"),
        "icon set with other metadata should not create styles");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:B3\"/>",
        "icon set metadata should not expand worksheet dimension");
    check_contains(worksheet_xml,
        "</sheetData><mergeCells count=\"1\"><mergeCell ref=\"A4:B4\"/></mergeCells>"
        "<conditionalFormatting sqref=\"A2:A10\">"
        "<cfRule type=\"iconSet\" priority=\"1\"><iconSet iconSet=\"3Arrows\" showValue=\"0\" reverse=\"1\">"
        "<cfvo type=\"percent\" val=\"0\"/><cfvo type=\"percent\" val=\"33\"/>"
        "<cfvo type=\"percent\" val=\"67\"/></iconSet></cfRule></conditionalFormatting>"
        "<dataValidations count=\"1\"><dataValidation type=\"whole\" operator=\"between\" "
        "sqref=\"A2:A10\"><formula1>1</formula1><formula2>10</formula2></dataValidation>"
        "</dataValidations><hyperlinks><hyperlink ref=\"B2\" r:id=\"rId1\"/></hyperlinks>"
        "<tableParts count=\"1\"><tablePart r:id=\"rId2\"/></tableParts></worksheet>",
        "icon set suffix ordering or relationship ids mismatch");

    const auto& worksheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check(count_occurrences(worksheet_rels, "<Relationship ") == 2,
        "icon set should not add worksheet relationship entries");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/docs" TargetMode="External"/>)",
        "icon set hyperlink relationship id should remain rId1");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)",
        "icon set table relationship id should remain rId2");
}

void test_streaming_writer_conditional_formatting_icon_set_percentile_thresholds()
{
    const auto output_path = fastxlsx::test::artifact_dir()
        / "fastxlsx-streaming-conditional-formatting-icon-set-percentile.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("IconSetPercentile");

    sheet.append_row({fastxlsx::CellView::text("Score")});
    for (int value = 1; value <= 9; ++value) {
        sheet.append_row({fastxlsx::CellView::number(static_cast<double>(value))});
    }

    fastxlsx::IconSetRule rule = make_icon_set();
    rule.value_type = fastxlsx::IconSetValueType::Percentile;
    rule.thresholds = {10.0, 50.0, 90.0};
    rule.show_value = false;
    rule.reverse = true;
    sheet.add_conditional_icon_set({2, 1, 10, 1}, rule);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/worksheets/sheet1.xml"),
        "missing percentile icon set worksheet");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "percentile icon set should not create worksheet relationships");
    check(!entries.contains("xl/styles.xml"), "percentile icon set should not create styles");
    check(!entries.contains("xl/metadata.xml"), "percentile icon set should not create metadata part");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("styles") == std::string::npos,
        "percentile icon set should not add style content types");
    check(content_types.find("conditionalFormatting") == std::string::npos,
        "percentile icon set should not add conditional formatting content types");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check(workbook_xml.find("<calcPr") == std::string::npos,
        "percentile icon set should not request recalculation");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(workbook_rels.find("styles") == std::string::npos,
        "percentile icon set should not add styles workbook relationship");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(worksheet_xml.find("xmlns:r=") == std::string::npos,
        "percentile icon set worksheet should not declare relationship namespace");
    check_contains(worksheet_xml, "<dimension ref=\"A1:A10\"/>",
        "percentile icon set worksheet dimension mismatch");
    check_contains(worksheet_xml,
        "</sheetData><conditionalFormatting sqref=\"A2:A10\">"
        "<cfRule type=\"iconSet\" priority=\"1\"><iconSet iconSet=\"3Arrows\" showValue=\"0\" reverse=\"1\">"
        "<cfvo type=\"percentile\" val=\"10\"/><cfvo type=\"percentile\" val=\"50\"/>"
        "<cfvo type=\"percentile\" val=\"90\"/></iconSet></cfRule></conditionalFormatting></worksheet>",
        "percentile icon set XML mismatch");
    check(count_occurrences(worksheet_xml, "<conditionalFormatting ") == 1,
        "percentile icon set conditional formatting element count mismatch");
    check(count_occurrences(worksheet_xml, "<cfRule ") == 1,
        "percentile icon set conditional formatting rule count mismatch");
    check(count_occurrences(worksheet_xml, "<cfvo ") == 3,
        "percentile icon set cfvo count mismatch");
    check(count_occurrences(worksheet_xml, "<iconSet ") == 1,
        "percentile icon set element count mismatch");
}

void test_streaming_writer_conditional_formatting_icon_set_multi_range_sqref()
{
    const auto output_path = fastxlsx::test::artifact_dir()
        / "fastxlsx-streaming-conditional-formatting-icon-set-multi-range.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("IconSetRanges");

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
    sheet.append_row({
        fastxlsx::CellView::number(4.0),
        fastxlsx::CellView::text("gap"),
        fastxlsx::CellView::number(5.0),
        fastxlsx::CellView::text("gap"),
        fastxlsx::CellView::number(6.0),
    });

    sheet.add_conditional_icon_set(
        {{2, 1, 3, 1}, {2, 3, 3, 3}, {2, 5, 3, 5}},
        make_icon_set());

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "multi-range icon set should not create worksheet relationships");
    check(!entries.contains("xl/styles.xml"), "multi-range icon set should not create styles");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<dimension ref=\"A1:E3\"/>",
        "multi-range icon set dimension mismatch");
    check_contains(worksheet_xml,
        "<conditionalFormatting sqref=\"A2:A3 C2:C3 E2:E3\">"
        "<cfRule type=\"iconSet\" priority=\"1\"><iconSet iconSet=\"3Arrows\">"
        "<cfvo type=\"percent\" val=\"0\"/><cfvo type=\"percent\" val=\"33\"/>"
        "<cfvo type=\"percent\" val=\"67\"/></iconSet></cfRule></conditionalFormatting>",
        "multi-range icon set sqref XML mismatch");
    check(count_occurrences(worksheet_xml, "<conditionalFormatting ") == 1,
        "multi-range icon set should be one conditionalFormatting element");
}

void test_streaming_writer_conditional_formatting_icon_set_priorities()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-conditional-formatting-icon-set-priorities.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto first = workbook.add_worksheet("First");
    auto second = workbook.add_worksheet("Second");

    first.append_row({fastxlsx::CellView::text("Value")});
    first.append_row({fastxlsx::CellView::number(1.0)});
    first.append_row({fastxlsx::CellView::number(2.0)});
    first.add_conditional_color_scale(
        {2, 1, 3, 1},
        make_two_color_scale(
            fastxlsx::ArgbColor {0xFF, 0xF8, 0x69, 0x6B},
            fastxlsx::ArgbColor {0xFF, 0x63, 0xBE, 0x7B}));
    first.add_conditional_data_bar(
        {2, 1, 3, 1}, make_data_bar(fastxlsx::ArgbColor {0xFF, 0x63, 0x8E, 0xC6}));
    fastxlsx::IconSetRule numeric_rule = make_icon_set();
    numeric_rule.value_type = fastxlsx::IconSetValueType::Number;
    numeric_rule.thresholds = {0.0, 5.0, 10.0};
    first.add_conditional_icon_set({2, 1, 3, 1}, numeric_rule);

    second.append_row({fastxlsx::CellView::text("Value")});
    second.append_row({fastxlsx::CellView::number(3.0)});
    second.add_conditional_icon_set({2, 1, 2, 1}, make_icon_set());

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& first_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_xml, R"(<cfRule type="colorScale" priority="1">)",
        "color scale priority should remain first");
    check_contains(first_xml, R"(<cfRule type="dataBar" priority="2">)",
        "data bar priority should remain second");
    check_contains(first_xml, R"(<cfRule type="iconSet" priority="3">)",
        "icon set should share conditional formatting priority sequence");
    check_contains(first_xml,
        "<cfvo type=\"num\" val=\"0\"/><cfvo type=\"num\" val=\"5\"/><cfvo type=\"num\" val=\"10\"/>",
        "numeric icon set threshold XML mismatch");

    const auto& second_xml = entries.at("xl/worksheets/sheet2.xml");
    check_contains(second_xml, R"(<cfRule type="iconSet" priority="1">)",
        "icon set priority should reset per worksheet");
    check(second_xml.find("priority=\"2\"") == std::string::npos,
        "second worksheet icon set should not inherit first worksheet priority");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "icon set priorities first sheet should not create relationships");
    check(!entries.contains("xl/worksheets/_rels/sheet2.xml.rels"),
        "icon set priorities second sheet should not create relationships");
}

void test_streaming_writer_invalid_conditional_formatting()
{
    const auto output_path =
        fastxlsx::test::artifact_dir()
        / "fastxlsx-streaming-invalid-conditional-formatting.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("InvalidCF");

    const auto valid_rule = make_two_color_scale(
        fastxlsx::ArgbColor {0xFF, 0xFF, 0x00, 0x00},
        fastxlsx::ArgbColor {0xFF, 0x00, 0xB0, 0x50});
    const auto valid_three_color_rule = make_three_color_scale(
        fastxlsx::ArgbColor {0xFF, 0xF8, 0x69, 0x6B},
        fastxlsx::ArgbColor {0xFF, 0xFF, 0xEB, 0x84},
        fastxlsx::ArgbColor {0xFF, 0x63, 0xBE, 0x7B});
    const auto valid_data_bar = make_data_bar(fastxlsx::ArgbColor {0xFF, 0x63, 0x8E, 0xC6});
    const auto valid_icon_set = make_icon_set();
    check_fastxlsx_error(
        [&sheet, &valid_rule] { sheet.add_conditional_color_scale({0, 1, 1, 1}, valid_rule); },
        "conditional formatting should reject a zero row");
    check_fastxlsx_error(
        [&sheet, &valid_rule] { sheet.add_conditional_color_scale({1, 0, 1, 1}, valid_rule); },
        "conditional formatting should reject a zero column");
    check_fastxlsx_error(
        [&sheet, &valid_rule] { sheet.add_conditional_color_scale({2, 1, 1, 1}, valid_rule); },
        "conditional formatting should reject a reversed row range");
    check_fastxlsx_error(
        [&sheet, &valid_rule] { sheet.add_conditional_color_scale({1, 2, 1, 1}, valid_rule); },
        "conditional formatting should reject a reversed column range");
    check_fastxlsx_error(
        [&sheet, &valid_rule] {
            sheet.add_conditional_color_scale({1, 1, 1048577, 1}, valid_rule);
        },
        "conditional formatting should reject a row beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet, &valid_rule] {
            sheet.add_conditional_color_scale({1, 1, 1, 16385}, valid_rule);
        },
        "conditional formatting should reject a column beyond Excel's limit");
    check_fastxlsx_error(
        [&sheet, &valid_rule] {
            const std::span<const fastxlsx::CellRange> no_ranges;
            sheet.add_conditional_color_scale(no_ranges, valid_rule);
        },
        "conditional formatting should reject an empty multi-range list");
    check_fastxlsx_error(
        [&sheet, &valid_rule] {
            sheet.add_conditional_color_scale({{1, 1, 1, 1}, {1, 0, 1, 1}}, valid_rule);
        },
        "conditional formatting should reject an invalid range inside a multi-range list");
    check_fastxlsx_error(
        [&sheet, &valid_data_bar] { sheet.add_conditional_data_bar({0, 1, 1, 1}, valid_data_bar); },
        "data bar conditional formatting should reject a zero row");
    check_fastxlsx_error(
        [&sheet, &valid_data_bar] {
            const std::span<const fastxlsx::CellRange> no_ranges;
            sheet.add_conditional_data_bar(no_ranges, valid_data_bar);
        },
        "data bar conditional formatting should reject an empty multi-range list");
    check_fastxlsx_error(
        [&sheet, &valid_data_bar] {
            sheet.add_conditional_data_bar({{1, 1, 1, 1}, {1, 0, 1, 1}}, valid_data_bar);
        },
        "data bar conditional formatting should reject an invalid range inside a multi-range list");
    check_fastxlsx_error(
        [&sheet, &valid_icon_set] { sheet.add_conditional_icon_set({0, 1, 1, 1}, valid_icon_set); },
        "icon set conditional formatting should reject a zero row");
    check_fastxlsx_error(
        [&sheet, &valid_icon_set] {
            const std::span<const fastxlsx::CellRange> no_ranges;
            sheet.add_conditional_icon_set(no_ranges, valid_icon_set);
        },
        "icon set conditional formatting should reject an empty multi-range list");
    check_fastxlsx_error(
        [&sheet, &valid_icon_set] {
            sheet.add_conditional_icon_set({{1, 1, 1, 1}, {1, 0, 1, 1}}, valid_icon_set);
        },
        "icon set conditional formatting should reject an invalid range inside a multi-range list");

    fastxlsx::TwoColorScaleRule nan_rule = valid_rule;
    nan_rule.lower.type = fastxlsx::ColorScaleValueType::Number;
    nan_rule.lower.value = std::numeric_limits<double>::quiet_NaN();
    check_fastxlsx_error(
        [&sheet, &nan_rule] { sheet.add_conditional_color_scale({1, 1, 1, 1}, nan_rule); },
        "conditional formatting should reject non-finite numeric endpoints");

    fastxlsx::TwoColorScaleRule infinity_rule = valid_rule;
    infinity_rule.upper.type = fastxlsx::ColorScaleValueType::Number;
    infinity_rule.upper.value = std::numeric_limits<double>::infinity();
    check_fastxlsx_error(
        [&sheet, &infinity_rule] {
            sheet.add_conditional_color_scale({1, 1, 1, 1}, infinity_rule);
        },
        "conditional formatting should reject non-finite upper numeric endpoints");

    fastxlsx::TwoColorScaleRule lower_max_rule = valid_rule;
    lower_max_rule.lower.type = fastxlsx::ColorScaleValueType::Maximum;
    check_fastxlsx_error(
        [&sheet, &lower_max_rule] {
            sheet.add_conditional_color_scale({1, 1, 1, 1}, lower_max_rule);
        },
        "conditional formatting should reject maximum as lower endpoint");

    fastxlsx::TwoColorScaleRule upper_min_rule = valid_rule;
    upper_min_rule.upper.type = fastxlsx::ColorScaleValueType::Minimum;
    check_fastxlsx_error(
        [&sheet, &upper_min_rule] {
            sheet.add_conditional_color_scale({1, 1, 1, 1}, upper_min_rule);
        },
        "conditional formatting should reject minimum as upper endpoint");

    fastxlsx::ThreeColorScaleRule middle_min_rule = valid_three_color_rule;
    middle_min_rule.midpoint.type = fastxlsx::ColorScaleValueType::Minimum;
    check_fastxlsx_error(
        [&sheet, &middle_min_rule] {
            sheet.add_conditional_color_scale({1, 1, 1, 1}, middle_min_rule);
        },
        "three-color conditional formatting should reject minimum as midpoint");

    fastxlsx::ThreeColorScaleRule middle_infinity_rule = valid_three_color_rule;
    middle_infinity_rule.midpoint.value = std::numeric_limits<double>::infinity();
    check_fastxlsx_error(
        [&sheet, &middle_infinity_rule] {
            sheet.add_conditional_color_scale({1, 1, 1, 1}, middle_infinity_rule);
        },
        "three-color conditional formatting should reject non-finite midpoint values");

    fastxlsx::DataBarRule data_bar_lower_max_rule = valid_data_bar;
    data_bar_lower_max_rule.lower.type = fastxlsx::DataBarValueType::Maximum;
    check_fastxlsx_error(
        [&sheet, &data_bar_lower_max_rule] {
            sheet.add_conditional_data_bar({1, 1, 1, 1}, data_bar_lower_max_rule);
        },
        "data bar conditional formatting should reject maximum as lower endpoint");

    fastxlsx::DataBarRule data_bar_upper_min_rule = valid_data_bar;
    data_bar_upper_min_rule.upper.type = fastxlsx::DataBarValueType::Minimum;
    check_fastxlsx_error(
        [&sheet, &data_bar_upper_min_rule] {
            sheet.add_conditional_data_bar({1, 1, 1, 1}, data_bar_upper_min_rule);
        },
        "data bar conditional formatting should reject minimum as upper endpoint");

    fastxlsx::DataBarRule data_bar_nan_rule = valid_data_bar;
    data_bar_nan_rule.lower.type = fastxlsx::DataBarValueType::Number;
    data_bar_nan_rule.lower.value = std::numeric_limits<double>::quiet_NaN();
    check_fastxlsx_error(
        [&sheet, &data_bar_nan_rule] {
            sheet.add_conditional_data_bar({1, 1, 1, 1}, data_bar_nan_rule);
        },
        "data bar conditional formatting should reject non-finite lower numeric endpoints");

    fastxlsx::DataBarRule data_bar_infinity_rule = valid_data_bar;
    data_bar_infinity_rule.upper.type = fastxlsx::DataBarValueType::Percentile;
    data_bar_infinity_rule.upper.value = std::numeric_limits<double>::infinity();
    check_fastxlsx_error(
        [&sheet, &data_bar_infinity_rule] {
            sheet.add_conditional_data_bar({1, 1, 1, 1}, data_bar_infinity_rule);
        },
        "data bar conditional formatting should reject non-finite upper numeric endpoints");

    fastxlsx::IconSetRule icon_set_nan_rule = valid_icon_set;
    icon_set_nan_rule.thresholds[1] = std::numeric_limits<double>::quiet_NaN();
    check_fastxlsx_error(
        [&sheet, &icon_set_nan_rule] {
            sheet.add_conditional_icon_set({1, 1, 1, 1}, icon_set_nan_rule);
        },
        "icon set conditional formatting should reject non-finite thresholds");

    fastxlsx::IconSetRule icon_set_infinity_rule = valid_icon_set;
    icon_set_infinity_rule.thresholds[2] = std::numeric_limits<double>::infinity();
    check_fastxlsx_error(
        [&sheet, &icon_set_infinity_rule] {
            sheet.add_conditional_icon_set({1, 1, 1, 1}, icon_set_infinity_rule);
        },
        "icon set conditional formatting should reject infinite thresholds");

    fastxlsx::IconSetRule icon_set_descending_rule = valid_icon_set;
    icon_set_descending_rule.thresholds = {0.0, 67.0, 33.0};
    check_fastxlsx_error(
        [&sheet, &icon_set_descending_rule] {
            sheet.add_conditional_icon_set({1, 1, 1, 1}, icon_set_descending_rule);
        },
        "icon set conditional formatting should reject descending thresholds");

    fastxlsx::IconSetRule icon_set_duplicate_rule = valid_icon_set;
    icon_set_duplicate_rule.thresholds = {0.0, 33.0, 33.0};
    check_fastxlsx_error(
        [&sheet, &icon_set_duplicate_rule] {
            sheet.add_conditional_icon_set({1, 1, 1, 1}, icon_set_duplicate_rule);
        },
        "icon set conditional formatting should reject duplicate thresholds");

    fastxlsx::IconSetRule icon_set_invalid_style_rule = valid_icon_set;
    icon_set_invalid_style_rule.style = static_cast<fastxlsx::IconSetStyle>(255);
    check_fastxlsx_error(
        [&sheet, &icon_set_invalid_style_rule] {
            sheet.add_conditional_icon_set({1, 1, 1, 1}, icon_set_invalid_style_rule);
        },
        "icon set conditional formatting should reject unknown icon set styles");

    fastxlsx::IconSetRule icon_set_invalid_type_rule = valid_icon_set;
    icon_set_invalid_type_rule.value_type = static_cast<fastxlsx::IconSetValueType>(255);
    check_fastxlsx_error(
        [&sheet, &icon_set_invalid_type_rule] {
            sheet.add_conditional_icon_set({1, 1, 1, 1}, icon_set_invalid_type_rule);
        },
        "icon set conditional formatting should reject unknown icon set value types");

    sheet.append_row({fastxlsx::CellView::number(1.0)});
    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(worksheet_xml.find("<conditionalFormatting") == std::string::npos,
        "failed conditional formatting calls should not mutate worksheet metadata");
    check_contains(worksheet_xml, "<dimension ref=\"A1:A1\"/>",
        "failed conditional formatting calls should not advance dimension");
    check(!entries.contains("xl/styles.xml"),
        "failed conditional formatting calls should not create styles");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "failed conditional formatting calls should not create relationships");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check(workbook_xml.find("<calcPr") == std::string::npos,
        "failed conditional formatting calls should not request recalculation");
}

void test_streaming_writer_data_validations()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-data-validations.xlsx";

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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-data-validation-multi-range.xlsx";

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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-validation-relationship-metadata.xlsx";

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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-data-validation-formula2-escape.xlsx";

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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-data-validation-prompts.xlsx";

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
    list.hide_dropdown_arrow = true;
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
        "<dataValidation type=\"list\" showDropDown=\"1\" showInputMessage=\"1\" promptTitle=\"Choice\" "
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
    check(count_occurrences(worksheet_xml, "showDropDown=\"1\"") == 1,
        "hide dropdown arrow should be serialized only for the list validation");
    check(worksheet_xml.find("showDropDown=\"0\"") == std::string::npos,
        "false showDropDown should be omitted");
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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-external-hyperlinks.xlsx";

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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-internal-hyperlinks.xlsx";

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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-internal-hyperlink-table-rels.xlsx";

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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-hyperlink-display-tooltips.xlsx";

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
    const auto output_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-tables.xlsx";

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
    totals.append_row({
        fastxlsx::CellView::text("Total"),
        fastxlsx::CellView::number(2.0),
    });
    fastxlsx::TableOptions totals_table;
    totals_table.name = "TotalsTable";
    totals_table.column_names = {"Metric", "Value"};
    totals_table.show_totals_row = true;
    totals_table.column_totals_labels = {"Total", ""};
    totals_table.column_totals_functions.resize(2);
    totals_table.column_totals_functions[1] = fastxlsx::TableTotalsFunction::Sum;
    totals_table.style_name.clear();
    totals.add_table({1, 1, 3, 2}, totals_table);

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
        R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id="2" name="TotalsTable" displayName="TotalsTable" ref="A1:B3" totalsRowCount="1">)",
        "second table root XML mismatch");
    check_contains(second_table_xml, R"(<autoFilter ref="A1:B2"/>)",
        "second table autoFilter should exclude the totals row");
    check(second_table_xml.find("totalsRowShown=\"0\"") == std::string::npos,
        "visible totals row table should not also write hidden totals metadata");
    check_contains(second_table_xml, R"(<tableColumn id="1" name="Metric" totalsRowLabel="Total"/>)",
        "totals row label metadata mismatch");
    check_contains(second_table_xml, R"(<tableColumn id="2" name="Value" totalsRowFunction="sum"/>)",
        "totals row function metadata mismatch");
    check(second_table_xml.find("totalsRowFormula") == std::string::npos,
        "totals row metadata should not generate formula text");
    check(second_table_xml.find("calculatedColumnFormula") == std::string::npos,
        "totals row metadata should not generate calculated column formulas");
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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-table-style-flags.xlsx";

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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-table-column-escape.xlsx";

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
    sheet.append_row({
        fastxlsx::CellView::text("Total \"quoted\" & <done>"),
        fastxlsx::CellView::number(42.0),
        fastxlsx::CellView::text("Owner's Total"),
    });

    fastxlsx::TableOptions table;
    table.name = "EscapedColumnTable";
    table.column_names = {"Text \"quoted\"", "Owner's Share", "A&B<Limit>"};
    table.show_totals_row = true;
    table.column_totals_labels = {"Total \"quoted\" & <done>", "", ""};
    table.column_totals_functions.resize(3);
    table.column_totals_functions[1] = fastxlsx::TableTotalsFunction::Sum;
    table.style_name.clear();
    sheet.add_table({1, 1, 3, 3}, table);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/tables/table1.xml"),
        "table column escape test should create a table part");
    check(!entries.contains("xl/styles.xml"),
        "table column escape test should not create a styles part");

    const auto& table_xml = entries.at("xl/tables/table1.xml");
    check_contains(table_xml,
        R"(<tableColumn id="1" name="Text &quot;quoted&quot;" totalsRowLabel="Total &quot;quoted&quot; &amp; &lt;done&gt;"/>)",
        "table column double-quote attribute escape mismatch");
    check_contains(table_xml,
        R"(<tableColumn id="2" name="Owner&apos;s Share" totalsRowFunction="sum"/>)",
        "table column apostrophe attribute escape mismatch");
    check_contains(table_xml,
        R"(<tableColumn id="3" name="A&amp;B&lt;Limit&gt;"/>)",
        "table column ampersand and angle-bracket attribute escape mismatch");
    check(table_xml.find("totalsRowLabel=\"\"") == std::string::npos,
        "empty totals row labels should be omitted");
    check(table_xml.find("totalsRowFormula") == std::string::npos,
        "totals labels should not generate totals row formula text");
}

void test_streaming_writer_table_range_overlap()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-table-range-overlap.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("Tables");
    auto other_sheet = workbook.add_worksheet("OtherTables");

    for (int row = 0; row < 4; ++row) {
        sheet.append_row({
            fastxlsx::CellView::text(row == 0 ? "A" : "a"),
            fastxlsx::CellView::text(row == 0 ? "B" : "b"),
            fastxlsx::CellView::text(row == 0 ? "C" : "c"),
            fastxlsx::CellView::text(row == 0 ? "D" : "d"),
        });
        other_sheet.append_row({
            fastxlsx::CellView::text(row == 0 ? "A" : "a"),
            fastxlsx::CellView::text(row == 0 ? "B" : "b"),
        });
    }

    fastxlsx::TableOptions first_table;
    first_table.name = "FirstTable";
    first_table.column_names = {"A", "B"};
    first_table.style_name.clear();
    sheet.add_table({1, 1, 2, 2}, first_table);

    fastxlsx::TableOptions adjacent_columns = first_table;
    adjacent_columns.name = "AdjacentColumnsTable";
    adjacent_columns.column_names = {"C", "D"};
    sheet.add_table({1, 3, 2, 4}, adjacent_columns);

    fastxlsx::TableOptions adjacent_rows = first_table;
    adjacent_rows.name = "AdjacentRowsTable";
    sheet.add_table({3, 1, 4, 2}, adjacent_rows);

    fastxlsx::TableOptions overlapping = first_table;
    overlapping.name = "OverlappingTable";
    check_fastxlsx_error(
        [&sheet, &overlapping] { sheet.add_table({2, 2, 3, 3}, overlapping); },
        "tables should reject overlapping ranges in the same worksheet");

    fastxlsx::TableOptions other_sheet_table = first_table;
    other_sheet_table.name = "OtherSheetTable";
    other_sheet.add_table({1, 1, 2, 2}, other_sheet_table);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& first_sheet_xml = entries.at("xl/worksheets/sheet1.xml");
    const auto& second_sheet_xml = entries.at("xl/worksheets/sheet2.xml");
    check_contains(first_sheet_xml, R"(<tableParts count="3">)",
        "non-overlapping tables in the same worksheet should all be kept");
    check_contains(second_sheet_xml, R"(<tableParts count="1">)",
        "same table range on a different worksheet should be allowed");
    check(entries.contains("xl/tables/table1.xml"), "missing first overlap test table");
    check(entries.contains("xl/tables/table2.xml"), "missing adjacent-column table");
    check(entries.contains("xl/tables/table3.xml"), "missing adjacent-row table");
    check(entries.contains("xl/tables/table4.xml"), "missing cross-worksheet table");
    check(!entries.contains("xl/tables/table5.xml"),
        "rejected overlapping table should not create a table part");
}

void test_streaming_writer_images()
{
    const auto image_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-image-source.png";
    write_bytes(image_path, fastxlsx::test::tiny_png_bytes());

    const auto output_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-images.xlsx";

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
}

void test_streaming_writer_image_metadata()
{
    const auto image_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-image-metadata-source.png";
    write_bytes(image_path, fastxlsx::test::tiny_png_bytes());

    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-image-metadata.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("ImageMetadata");

    fastxlsx::ImageOptions escaped_options;
    escaped_options.edit_as = fastxlsx::ImageEditAs::OneCell;
    escaped_options.from_offset = {111, 222};
    escaped_options.to_offset = {333, 444};
    escaped_options.name = R"(Logo "A&B<1>')";
    escaped_options.description = R"(Alt "quoted" & <tag> 'owner')";
    sheet.add_image(image_path, {1, 1, 2, 2}, escaped_options);

    fastxlsx::ImageOptions named_only;
    named_only.edit_as = fastxlsx::ImageEditAs::Absolute;
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
    check(count_occurrences(drawing_xml, R"(editAs="oneCell")") == 1,
        "image metadata oneCell editAs count mismatch");
    check(count_occurrences(drawing_xml, R"(editAs="absolute")") == 1,
        "image metadata absolute editAs count mismatch");
    check(count_occurrences(drawing_xml, R"(editAs="twoCell")") == 1,
        "image metadata default twoCell editAs count mismatch");
    check_contains(drawing_xml,
        R"(<xdr:twoCellAnchor editAs="oneCell"><xdr:from><xdr:col>0</xdr:col><xdr:colOff>111</xdr:colOff><xdr:row>0</xdr:row><xdr:rowOff>222</xdr:rowOff></xdr:from>)",
        "image metadata oneCell from marker offset mismatch");
    check_contains(drawing_xml,
        R"(<xdr:to><xdr:col>2</xdr:col><xdr:colOff>333</xdr:colOff><xdr:row>2</xdr:row><xdr:rowOff>444</xdr:rowOff></xdr:to>)",
        "image metadata oneCell to marker offset mismatch");
    check_contains(drawing_xml,
        R"(<xdr:twoCellAnchor editAs="absolute"><xdr:from><xdr:col>0</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>2</xdr:row>)",
        "image metadata absolute anchor mismatch");
    check_contains(drawing_xml,
        R"(<xdr:twoCellAnchor editAs="twoCell"><xdr:from><xdr:col>0</xdr:col><xdr:colOff>0</xdr:colOff><xdr:row>4</xdr:row>)",
        "image metadata default twoCell anchor mismatch");
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
}

void test_streaming_writer_jpeg_images()
{
    const auto image_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-image-source.jpg";
    write_bytes(image_path, fastxlsx::test::tiny_jpeg_bytes());
    const auto output_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-jpeg-images.xlsx";

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
}

void test_streaming_writer_mixed_image_formats()
{
    const auto png_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-mixed-image-source.png";
    const auto jpeg_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-mixed-image-source.jpg";
    write_bytes(png_path, fastxlsx::test::tiny_png_bytes());
    write_bytes(jpeg_path, fastxlsx::test::tiny_jpeg_bytes());

    const auto output_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-mixed-images.xlsx";

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
}

void test_streaming_writer_memory_images()
{
    const auto output_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-memory-images.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("MemoryImages");
    sheet.append_row({fastxlsx::CellView::text("memory")});

    const auto& png_source = fastxlsx::test::tiny_rgba_png();
    std::vector<unsigned char> mutable_png(png_source.begin(), png_source.end());
    const auto mutable_png_bytes =
        std::as_bytes(std::span<const unsigned char>(mutable_png.data(), mutable_png.size()));

    fastxlsx::ImageOptions png_options;
    png_options.edit_as = fastxlsx::ImageEditAs::OneCell;
    png_options.from_offset = {101, 202};
    png_options.to_offset = {303, 404};
    png_options.name = "Memory PNG";
    png_options.description = "PNG bytes from memory";
    sheet.add_image(mutable_png_bytes, {1, 1, 2, 2}, png_options);
    mutable_png.assign(mutable_png.size(), 0);

    sheet.add_image(fastxlsx::test::tiny_jpeg_bytes(), {3, 2, 4, 3});
    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/media/image1.png"), "missing memory PNG media part");
    check(entries.contains("xl/media/image2.jpg"), "missing memory JPEG media part");
    check(!entries.contains("xl/drawings/drawing2.xml"), "memory images should share worksheet drawing");
    check(entries.at("xl/media/image1.png").size() == fastxlsx::test::tiny_rgba_png().size(),
        "memory PNG media part byte size mismatch");
    check(bytes_equal(entries.at("xl/media/image1.png"), fastxlsx::test::tiny_png_bytes()),
        "memory PNG media bytes should be copied before caller buffer mutation");
    check(entries.at("xl/media/image2.jpg").size() == fastxlsx::test::tiny_rgb_jpeg_header().size(),
        "memory JPEG media part byte size mismatch");
    check(bytes_equal(entries.at("xl/media/image2.jpg"), fastxlsx::test::tiny_jpeg_bytes()),
        "memory JPEG media bytes mismatch");

    const auto& content_types = entries.at("[Content_Types].xml");
    check_contains(content_types,
        R"(<Default Extension="png" ContentType="image/png"/>)",
        "memory PNG content type default missing");
    check_contains(content_types,
        R"(<Default Extension="jpg" ContentType="image/jpeg"/>)",
        "memory JPEG content type default missing");
    check_contains(content_types,
        R"(<Override PartName="/xl/drawings/drawing1.xml" ContentType="application/vnd.openxmlformats-officedocument.drawing+xml"/>)",
        "memory drawing content type override missing");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        "</sheetData><drawing r:id=\"rId1\"/></worksheet>",
        "memory image worksheet drawing relationship id mismatch");

    const auto& worksheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing1.xml"/>)",
        "memory image worksheet drawing relationship mismatch");

    const auto& drawing_xml = entries.at("xl/drawings/drawing1.xml");
    check(count_occurrences(drawing_xml, "<xdr:twoCellAnchor") == 2,
        "memory image drawing anchor count mismatch");
    check_contains(drawing_xml,
        R"(<xdr:twoCellAnchor editAs="oneCell"><xdr:from><xdr:col>0</xdr:col><xdr:colOff>101</xdr:colOff><xdr:row>0</xdr:row><xdr:rowOff>202</xdr:rowOff></xdr:from>)",
        "memory image options from marker mismatch");
    check_contains(drawing_xml,
        R"(<xdr:to><xdr:col>2</xdr:col><xdr:colOff>303</xdr:colOff><xdr:row>2</xdr:row><xdr:rowOff>404</xdr:rowOff></xdr:to>)",
        "memory image options to marker mismatch");
    check_contains(drawing_xml,
        R"(<xdr:cNvPr id="1" name="Memory PNG" descr="PNG bytes from memory"/>)",
        "memory image options metadata mismatch");
    check_contains(drawing_xml,
        R"(<xdr:cNvPr id="2" name="Picture 2"/>)",
        "memory image default picture name mismatch");
    check_contains(drawing_xml,
        R"(<a:blip r:embed="rId1"/>)",
        "memory PNG drawing relationship id mismatch");
    check_contains(drawing_xml,
        R"(<a:blip r:embed="rId2"/>)",
        "memory JPEG drawing relationship id mismatch");
    check_contains(drawing_xml,
        R"(<a:ext cx="9525" cy="9525"/>)",
        "memory PNG intrinsic EMU size mismatch");
    check_contains(drawing_xml,
        R"(<a:ext cx="19050" cy="9525"/>)",
        "memory JPEG intrinsic EMU size mismatch");

    const auto& drawing_rels = entries.at("xl/drawings/_rels/drawing1.xml.rels");
    check(count_occurrences(drawing_rels, "<Relationship ") == 2,
        "memory image drawing relationship count mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image1.png"/>)",
        "memory PNG drawing image relationship mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image2.jpg"/>)",
        "memory JPEG drawing image relationship mismatch");
}

void test_streaming_writer_image_hyperlinks()
{
    const auto image_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-image-hyperlinks-source.png";
    write_bytes(image_path, fastxlsx::test::tiny_png_bytes());

    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-image-hyperlinks.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("ImageLinks");
    sheet.append_row({fastxlsx::CellView::text("linked images")});

    fastxlsx::ImageOptions path_options;
    path_options.name = "Linked Path";
    path_options.description = "Path image link";
    path_options.external_hyperlink_url = "https://example.com/path?a=1&b=2";
    path_options.external_hyperlink_tooltip = R"(Open "path" & <tag>)";
    sheet.add_image(image_path, {1, 1, 2, 2}, path_options);

    fastxlsx::ImageOptions memory_options;
    memory_options.name = "Linked Memory";
    memory_options.external_hyperlink_url = "mailto:image@example.com";
    sheet.add_image(fastxlsx::test::tiny_jpeg_bytes(), {3, 2, 4, 3}, memory_options);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/media/image1.png"), "missing linked path image media part");
    check(entries.contains("xl/media/image2.jpg"), "missing linked memory image media part");
    check(entries.contains("xl/drawings/drawing1.xml"), "missing linked image drawing part");
    check(!entries.contains("xl/drawings/drawing2.xml"),
        "linked images on one sheet should share one drawing part");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        "</sheetData><drawing r:id=\"rId1\"/></worksheet>",
        "linked images should create only a worksheet drawing reference");
    const auto& worksheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check(count_occurrences(worksheet_rels, "<Relationship ") == 1,
        "image object hyperlinks should not create worksheet hyperlink relationships");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing1.xml"/>)",
        "linked image worksheet drawing relationship mismatch");
    check(worksheet_rels.find("relationships/hyperlink") == std::string::npos,
        "image object hyperlinks should stay in drawing relationships");

    const auto& drawing_xml = entries.at("xl/drawings/drawing1.xml");
    check(count_occurrences(drawing_xml, "<xdr:twoCellAnchor") == 2,
        "linked image drawing anchor count mismatch");
    check_contains(drawing_xml,
        R"(<xdr:cNvPr id="1" name="Linked Path" descr="Path image link"><a:hlinkClick r:id="rId3" tooltip="Open &quot;path&quot; &amp; &lt;tag&gt;"/></xdr:cNvPr>)",
        "path image hyperlink XML mismatch or tooltip escape failure");
    check_contains(drawing_xml,
        R"(<xdr:cNvPr id="2" name="Linked Memory"><a:hlinkClick r:id="rId4"/></xdr:cNvPr>)",
        "memory image hyperlink XML mismatch");
    check_contains(drawing_xml,
        R"(<a:blip r:embed="rId1"/>)",
        "path image media relationship id should remain rId1");
    check_contains(drawing_xml,
        R"(<a:blip r:embed="rId2"/>)",
        "memory image media relationship id should remain rId2");

    const auto& drawing_rels = entries.at("xl/drawings/_rels/drawing1.xml.rels");
    check(count_occurrences(drawing_rels, "<Relationship ") == 4,
        "linked image drawing relationship count mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image1.png"/>)",
        "linked path image relationship mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image2.jpg"/>)",
        "linked memory image relationship mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/path?a=1&amp;b=2" TargetMode="External"/>)",
        "linked path image hyperlink relationship mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="mailto:image@example.com" TargetMode="External"/>)",
        "linked memory image hyperlink relationship mismatch");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("hyperlink") == std::string::npos,
        "image object hyperlinks should not add content type entries");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(workbook_rels.find("relationships/hyperlink") == std::string::npos,
        "image object hyperlinks should not create workbook relationships");
}

void test_streaming_writer_image_hyperlinks_mixed_objects()
{
    const auto image_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-image-hyperlink-mixed-source.png";
    write_bytes(image_path, fastxlsx::test::tiny_png_bytes());

    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-image-hyperlink-mixed-objects.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    auto sheet = workbook.add_worksheet("MixedImageLinks");
    sheet.append_row({fastxlsx::CellView::text("Item"), fastxlsx::CellView::text("Link")});
    sheet.append_row({fastxlsx::CellView::text("Widget"), fastxlsx::CellView::text("Cell link")});
    sheet.add_external_hyperlink(2, 2, "https://example.com/cell-link");

    fastxlsx::ImageOptions linked_image;
    linked_image.name = "Linked Picture";
    linked_image.external_hyperlink_url = "https://example.com/picture-link";
    sheet.add_image(image_path, {3, 1, 4, 2}, linked_image);
    sheet.add_image(fastxlsx::test::tiny_jpeg_bytes(), {3, 3, 4, 4});

    fastxlsx::TableOptions table_options;
    table_options.name = "MixedImageLinkTable";
    table_options.column_names = {"Item", "Link"};
    sheet.add_table({1, 1, 2, 2}, table_options);

    workbook.close();

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<hyperlinks><hyperlink ref="B2" r:id="rId1"/></hyperlinks><drawing r:id="rId2"/><tableParts count="1"><tablePart r:id="rId3"/></tableParts>)",
        "mixed image hyperlink worksheet suffix or relationship ids mismatch");

    const auto& worksheet_rels = entries.at("xl/worksheets/_rels/sheet1.xml.rels");
    check(count_occurrences(worksheet_rels, "<Relationship ") == 3,
        "mixed image hyperlink worksheet relationship count mismatch");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/cell-link" TargetMode="External"/>)",
        "mixed image hyperlink cell hyperlink relationship mismatch");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing1.xml"/>)",
        "mixed image hyperlink drawing relationship mismatch");
    check_contains(worksheet_rels,
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/table" Target="../tables/table1.xml"/>)",
        "mixed image hyperlink table relationship mismatch");

    const auto& drawing_xml = entries.at("xl/drawings/drawing1.xml");
    check_contains(drawing_xml,
        R"(<xdr:cNvPr id="1" name="Linked Picture"><a:hlinkClick r:id="rId3"/></xdr:cNvPr>)",
        "mixed image hyperlink drawing hyperlink XML mismatch");
    check_contains(drawing_xml,
        R"(<xdr:cNvPr id="2" name="Picture 2"/>)",
        "mixed image hyperlink non-linked picture should keep plain cNvPr");
    check_contains(drawing_xml,
        R"(<a:blip r:embed="rId1"/>)",
        "mixed image hyperlink linked media relationship id mismatch");
    check_contains(drawing_xml,
        R"(<a:blip r:embed="rId2"/>)",
        "mixed image hyperlink plain media relationship id mismatch");

    const auto& drawing_rels = entries.at("xl/drawings/_rels/drawing1.xml.rels");
    check(count_occurrences(drawing_rels, "<Relationship ") == 3,
        "mixed image hyperlink drawing relationship count mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image1.png"/>)",
        "mixed image hyperlink first image relationship mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="../media/image2.jpg"/>)",
        "mixed image hyperlink second image relationship mismatch");
    check_contains(drawing_rels,
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink" Target="https://example.com/picture-link" TargetMode="External"/>)",
        "mixed image hyperlink drawing hyperlink relationship mismatch");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(workbook_rels.find("relationships/hyperlink") == std::string::npos,
        "mixed image hyperlink should not create workbook hyperlink relationships");
}

void test_streaming_writer_image_anchor_markers()
{
    const auto image_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-anchor-image-source.png";
    write_bytes(image_path, fastxlsx::test::tiny_png_bytes());

    const auto output_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-image-anchors.xlsx";

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
}

void test_streaming_writer_mixed_object_relationship_ids()
{
    const auto png_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-object-rels-source.png";
    const auto jpeg_path = fastxlsx::test::artifact_dir() / "fastxlsx-streaming-object-rels-source.jpg";
    write_bytes(png_path, fastxlsx::test::tiny_png_bytes());
    write_bytes(jpeg_path, fastxlsx::test::tiny_jpeg_bytes());

    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-mixed-object-rels.xlsx";

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
}

void test_streaming_writer_shared_string_package()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-shared-strings.xlsx";

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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-shared-strings-workbook-scope.xlsx";

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

void test_streaming_writer_shared_string_option_without_string_cells()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-shared-strings-empty-table.xlsx";

    fastxlsx::WorkbookWriterOptions options;
    options.string_strategy = fastxlsx::StringStrategy::SharedString;

    auto workbook = fastxlsx::WorkbookWriter::create(output_path, options);
    auto sheet = workbook.add_worksheet("NoStrings");

    sheet.append_row({
        fastxlsx::CellView::number(42.0),
        fastxlsx::CellView::boolean(true),
        fastxlsx::CellView::formula("A1+1"),
    });

    workbook.close();
    check(std::filesystem::exists(output_path),
        "shared string option without strings xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(!entries.contains("xl/sharedStrings.xml"),
        "shared string option without string cells should not create sharedStrings.xml");

    const auto& content_types = entries.at("[Content_Types].xml");
    check(content_types.find("/xl/sharedStrings.xml") == std::string::npos,
        "shared string option without string cells should not create sharedStrings content type");
    check(content_types.find(
              "application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml")
            == std::string::npos,
        "shared string option without string cells should not create sharedStrings MIME type");

    const auto& workbook_rels = entries.at("xl/_rels/workbook.xml.rels");
    check(workbook_rels.find("relationships/sharedStrings") == std::string::npos,
        "shared string option without string cells should not create sharedStrings relationship");

    const auto& workbook_xml = entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(<calcPr calcId="124519" fullCalcOnLoad="1"/>)",
        "formula without shared strings should still request recalculation");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check(worksheet_xml.find(" t=\"s\"") == std::string::npos,
        "worksheet without string cells should not reference shared strings");
    check(worksheet_xml.find("inlineStr") == std::string::npos,
        "worksheet without string cells should not write inline strings");
    check_contains(
        worksheet_xml, R"(<c r="A1"><v>42</v></c>)", "numeric cell should still be written");
    check_contains(
        worksheet_xml, R"(<c r="B1" t="b"><v>1</v></c>)", "boolean cell should still be written");
    check_contains(
        worksheet_xml, R"(<c r="C1"><f>A1+1</f></c>)", "formula cell should still be written");
}

void test_streaming_writer_file_backed_multi_sheet_bodies_do_not_alias()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-file-backed-multi-sheet.xlsx";

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
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-after-close.xlsx";

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
            sheet.add_conditional_color_scale(
                {1, 1, 1, 1},
                make_two_color_scale(
                    fastxlsx::ArgbColor {0xFF, 0xFF, 0x00, 0x00},
                    fastxlsx::ArgbColor {0xFF, 0x00, 0xB0, 0x50}));
        },
        "add_conditional_color_scale should reject mutation after workbook close");
    check_fastxlsx_error(
        [&sheet] {
            sheet.add_conditional_color_scale(
                {1, 1, 1, 1},
                make_three_color_scale(
                    fastxlsx::ArgbColor {0xFF, 0xF8, 0x69, 0x6B},
                    fastxlsx::ArgbColor {0xFF, 0xFF, 0xEB, 0x84},
                    fastxlsx::ArgbColor {0xFF, 0x63, 0xBE, 0x7B}));
        },
        "add_conditional_color_scale three-color should reject mutation after workbook close");
    check_fastxlsx_error(
        [&sheet] {
            sheet.add_conditional_data_bar(
                {1, 1, 1, 1}, make_data_bar(fastxlsx::ArgbColor {0xFF, 0x63, 0x8E, 0xC6}));
        },
        "add_conditional_data_bar should reject mutation after workbook close");
    check_fastxlsx_error(
        [&sheet] { sheet.add_conditional_icon_set({1, 1, 1, 1}, make_icon_set()); },
        "add_conditional_icon_set should reject mutation after workbook close");
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
            sheet.add_image(fastxlsx::test::artifact_dir() / "fastxlsx-unused-image.png",
                {1, 1, 1, 1});
        },
        "add_image should reject mutation after workbook close");
    check_fastxlsx_error(
        [&sheet] { sheet.add_image(fastxlsx::test::tiny_png_bytes(), {1, 1, 1, 1}); },
        "add_image memory overload should reject mutation after workbook close");
    check_fastxlsx_error(
        [&sheet] {
            fastxlsx::ImageOptions options;
            options.external_hyperlink_url = "https://example.com/after-close";
            sheet.add_image(fastxlsx::test::tiny_png_bytes(), {1, 1, 1, 1}, options);
        },
        "add_image hyperlink metadata should reject mutation after workbook close");
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

void test_streaming_writer_invalid_table_options()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-invalid-tables.xlsx";

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

    fastxlsx::TableOptions totals_function_without_totals_row = valid;
    totals_function_without_totals_row.name = "TotalsFunctionWithoutTotalsRow";
    totals_function_without_totals_row.column_totals_functions.resize(2);
    totals_function_without_totals_row.column_totals_functions[1] =
        fastxlsx::TableTotalsFunction::Sum;
    check_fastxlsx_error(
        [&sheet, &totals_function_without_totals_row] {
            sheet.add_table({1, 1, 2, 2}, totals_function_without_totals_row);
        },
        "tables should reject totals functions without visible totals row metadata");

    fastxlsx::TableOptions totals_label_without_totals_row = valid;
    totals_label_without_totals_row.name = "TotalsLabelWithoutTotalsRow";
    totals_label_without_totals_row.column_totals_labels = {"Total", ""};
    check_fastxlsx_error(
        [&sheet, &totals_label_without_totals_row] {
            sheet.add_table({1, 1, 2, 2}, totals_label_without_totals_row);
        },
        "tables should reject totals labels without visible totals row metadata");

    fastxlsx::TableOptions wrong_totals_function_count = valid;
    wrong_totals_function_count.name = "WrongTotalsFunctionCount";
    wrong_totals_function_count.show_totals_row = true;
    wrong_totals_function_count.column_totals_functions.resize(1);
    wrong_totals_function_count.column_totals_functions[0] =
        fastxlsx::TableTotalsFunction::Sum;
    check_fastxlsx_error(
        [&sheet, &wrong_totals_function_count] {
            sheet.add_table({1, 1, 3, 2}, wrong_totals_function_count);
        },
        "tables should reject a totals function count mismatch");

    fastxlsx::TableOptions wrong_totals_label_count = valid;
    wrong_totals_label_count.name = "WrongTotalsLabelCount";
    wrong_totals_label_count.show_totals_row = true;
    wrong_totals_label_count.column_totals_functions.resize(2);
    wrong_totals_label_count.column_totals_functions[1] =
        fastxlsx::TableTotalsFunction::Sum;
    wrong_totals_label_count.column_totals_labels = {"Total"};
    check_fastxlsx_error(
        [&sheet, &wrong_totals_label_count] {
            sheet.add_table({1, 1, 3, 2}, wrong_totals_label_count);
        },
        "tables should reject a totals label count mismatch");

    fastxlsx::TableOptions labels_only_visible_totals = valid;
    labels_only_visible_totals.name = "LabelsOnlyVisibleTotals";
    labels_only_visible_totals.show_totals_row = true;
    labels_only_visible_totals.column_totals_labels = {"Total", ""};
    check_fastxlsx_error(
        [&sheet, &labels_only_visible_totals] {
            sheet.add_table({1, 1, 3, 2}, labels_only_visible_totals);
        },
        "visible totals rows with only labels should still require a totals function");

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

void test_streaming_writer_invalid_data_validation_rules()
{
    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-invalid-validations.xlsx";

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

    fastxlsx::DataValidationRule whole_with_hidden_dropdown = whole_without_operator;
    whole_with_hidden_dropdown.operator_type = fastxlsx::DataValidationOperator::Equal;
    whole_with_hidden_dropdown.hide_dropdown_arrow = true;
    check_fastxlsx_error(
        [&sheet, &whole_with_hidden_dropdown] {
            sheet.add_data_validation({1, 1, 1, 1}, whole_with_hidden_dropdown);
        },
        "non-list dataValidations should reject hidden dropdown arrows");

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
        test_streaming_writer_max_column_boundary();
        test_streaming_writer_max_row_boundary_with_test_hook();
        test_streaming_writer_failed_append_preserves_state();
        test_streaming_writer_phase3_metadata_structure();
        test_streaming_writer_number_format_styles();
        test_streaming_writer_alignment_styles();
        test_streaming_writer_font_styles();
        test_streaming_writer_fill_styles();
        test_streaming_writer_styles_with_shared_strings();
        test_streaming_writer_invalid_style_preserves_state();
        test_streaming_writer_foreign_style_collision_is_rejected();
        test_streaming_writer_default_style_id_clears_cell_style();
        test_streaming_writer_all_default_style_metadata_is_ignored();
        test_streaming_writer_styles_with_relationship_metadata();
        test_streaming_writer_invalid_style_registration();
        test_streaming_writer_file_backed_body_round_trip();
        test_streaming_writer_conditional_formatting_two_color_scale();
        test_streaming_writer_conditional_formatting_three_color_scale();
        test_streaming_writer_conditional_formatting_metadata_order();
        test_streaming_writer_conditional_formatting_multi_range_sqref();
        test_streaming_writer_conditional_formatting_priorities();
        test_streaming_writer_conditional_formatting_failed_call_preserves_priority();
        test_streaming_writer_conditional_formatting_data_bar();
        test_streaming_writer_conditional_formatting_data_bar_metadata_order();
        test_streaming_writer_conditional_formatting_data_bar_multi_range_sqref();
        test_streaming_writer_conditional_formatting_data_bar_priorities();
        test_streaming_writer_conditional_formatting_icon_set();
        test_streaming_writer_conditional_formatting_icon_set_metadata_order();
        test_streaming_writer_conditional_formatting_icon_set_percentile_thresholds();
        test_streaming_writer_conditional_formatting_icon_set_multi_range_sqref();
        test_streaming_writer_conditional_formatting_icon_set_priorities();
        test_streaming_writer_invalid_conditional_formatting();
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
        test_streaming_writer_table_range_overlap();
        test_streaming_writer_images();
        test_streaming_writer_image_metadata();
        test_streaming_writer_jpeg_images();
        test_streaming_writer_mixed_image_formats();
        test_streaming_writer_memory_images();
        test_streaming_writer_image_hyperlinks();
        test_streaming_writer_image_hyperlinks_mixed_objects();
        test_streaming_writer_image_anchor_markers();
        test_streaming_writer_mixed_object_relationship_ids();
        test_streaming_writer_shared_string_package();
        test_streaming_writer_shared_strings_workbook_scope_and_crlf();
        test_streaming_writer_shared_string_option_without_string_cells();
        test_streaming_writer_file_backed_multi_sheet_bodies_do_not_alias();
        test_streaming_writer_rejects_mutation_after_close();
        test_streaming_writer_invalid_ranges();
        test_streaming_writer_invalid_table_options();
        test_streaming_writer_sheet_name_uniqueness();
        test_streaming_writer_invalid_data_validation_rules();
        test_streaming_writer_invalid_metadata_and_rows();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
