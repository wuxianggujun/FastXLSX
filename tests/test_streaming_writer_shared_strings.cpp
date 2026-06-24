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

} // namespace

int main()
{
    try {
        test_streaming_writer_shared_string_package();
        test_streaming_writer_shared_strings_workbook_scope_and_crlf();
        test_streaming_writer_shared_string_option_without_string_cells();
        test_streaming_writer_file_backed_multi_sheet_bodies_do_not_alias();
        test_streaming_writer_rejects_mutation_after_close();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
