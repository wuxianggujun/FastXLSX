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

} // namespace

int main()
{
    try {
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
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
