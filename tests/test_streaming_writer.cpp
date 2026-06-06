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
        test_streaming_writer_shared_string_package();
        test_streaming_writer_shared_strings_workbook_scope_and_crlf();
        test_streaming_writer_rejects_mutation_after_close();
        test_streaming_writer_invalid_ranges();
        test_streaming_writer_invalid_metadata_and_rows();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
