#include <fastxlsx/fastxlsx.hpp>

#include "zip_test_utils.hpp"

#include <array>
#include <cmath>
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

bool near(double left, double right)
{
    return std::fabs(left - right) < 0.000001;
}

bool same_color(fastxlsx::ArgbColor color,
    std::uint8_t alpha, std::uint8_t red,
    std::uint8_t green, std::uint8_t blue)
{
    return color.alpha == alpha && color.red == red
        && color.green == green && color.blue == blue;
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
        R"(<x:conditionalFormatting sqref="$A$2:$A$4 C2:C4">)"
        R"(<x:cfRule type="colorScale" priority="1"><x:colorScale>)"
        R"(<x:cfvo type="min"/><x:cfvo type="max"/>)"
        R"(<x:color rgb="FFFF0000"/><x:color rgb="FF00B050"/>)"
        R"(</x:colorScale></x:cfRule></x:conditionalFormatting>)"
        R"(<x:conditionalFormatting sqref="D2:D8">)"
        R"(<x:cfRule type="colorScale" priority="2"><x:colorScale>)"
        R"(<x:cfvo type="min"/><x:cfvo type="percentile" val="50"/><x:cfvo type="max"/>)"
        R"(<x:color rgb="FFF8696B"/><x:color rgb="FFFFEB84"/><x:color rgb="FF63BE7B"/>)"
        R"(</x:colorScale></x:cfRule></x:conditionalFormatting>)"
        R"(<x:conditionalFormatting sqref="E2:E8">)"
        R"(<x:cfRule type="dataBar" priority="3"><x:dataBar showValue="0">)"
        R"(<x:cfvo type="num" val="0"/><x:cfvo type="percentile" val="90"/>)"
        R"(<x:color rgb="FF638EC6"/></x:dataBar></x:cfRule></x:conditionalFormatting>)"
        R"(<x:conditionalFormatting sqref="F2:F8">)"
        R"(<x:cfRule type="iconSet" priority="4"><x:iconSet iconSet="3Arrows" showValue="0" reverse="1">)"
        R"(<x:cfvo type="percentile" val="10"/><x:cfvo type="percentile" val="50"/>)"
        R"(<x:cfvo type="percentile" val="90"/></x:iconSet></x:cfRule></x:conditionalFormatting>)"
        R"(<x:dataValidations/>)"
        R"(</x:worksheet>)";
}

void test_projects_writer_compatible_rules_in_source_order()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-conditional-format-reader-representative.xlsx",
        representative_worksheet_xml());
    const std::string before = fastxlsx::test::read_file(path);
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::vector<fastxlsx::WorksheetConditionalFormatView> values;
    fastxlsx::WorksheetConditionalFormatReadCallbacks callbacks;
    callbacks.on_conditional_format =
        [&](const fastxlsx::WorksheetConditionalFormatView& value) {
            values.push_back(value);
        };
    const fastxlsx::WorksheetConditionalFormatReadSummary summary =
        reader.read_worksheet_conditional_formats("Data", callbacks);

    check(values.size() == 4, "conditional-format callback count mismatch");
    for (std::size_t index = 0; index < values.size(); ++index) {
        check(values[index].index == index,
            "conditional-format source indexes are not zero-based");
        check(values[index].priority == index + 1,
            "conditional-format priority projection mismatch");
    }

    check(values[0].kind == fastxlsx::WorksheetConditionalFormatKind::TwoColorScale
            && values[0].two_color_scale.has_value()
            && values[0].ranges.size() == 2
            && values[0].ranges[0].first_row == 2
            && values[0].ranges[0].last_row == 4
            && values[0].ranges[1].first_column == 3,
        "two-color scale range projection mismatch");
    const auto& two = *values[0].two_color_scale;
    check(two.lower.type == fastxlsx::ColorScaleValueType::Minimum
            && two.upper.type == fastxlsx::ColorScaleValueType::Maximum
            && same_color(two.lower.color, 0xFF, 0xFF, 0x00, 0x00)
            && same_color(two.upper.color, 0xFF, 0x00, 0xB0, 0x50),
        "two-color scale payload projection mismatch");

    check(values[1].kind == fastxlsx::WorksheetConditionalFormatKind::ThreeColorScale
            && values[1].three_color_scale.has_value(),
        "three-color scale kind projection mismatch");
    const auto& three = *values[1].three_color_scale;
    check(three.midpoint.type == fastxlsx::ColorScaleValueType::Percentile
            && near(three.midpoint.value, 50.0)
            && same_color(three.lower.color, 0xFF, 0xF8, 0x69, 0x6B)
            && same_color(three.midpoint.color, 0xFF, 0xFF, 0xEB, 0x84)
            && same_color(three.upper.color, 0xFF, 0x63, 0xBE, 0x7B),
        "three-color scale payload projection mismatch");

    check(values[2].kind == fastxlsx::WorksheetConditionalFormatKind::DataBar
            && values[2].data_bar.has_value(),
        "data bar kind projection mismatch");
    const auto& data_bar = *values[2].data_bar;
    check(data_bar.lower.type == fastxlsx::DataBarValueType::Number
            && near(data_bar.lower.value, 0.0)
            && data_bar.upper.type == fastxlsx::DataBarValueType::Percentile
            && near(data_bar.upper.value, 90.0)
            && !data_bar.show_value
            && same_color(data_bar.color, 0xFF, 0x63, 0x8E, 0xC6),
        "data bar payload projection mismatch");

    check(values[3].kind == fastxlsx::WorksheetConditionalFormatKind::IconSet
            && values[3].icon_set.has_value(),
        "icon set kind projection mismatch");
    const auto& icon_set = *values[3].icon_set;
    check(icon_set.style == fastxlsx::IconSetStyle::ThreeArrows
            && icon_set.value_type == fastxlsx::IconSetValueType::Percentile
            && near(icon_set.thresholds[0], 10.0)
            && near(icon_set.thresholds[1], 50.0)
            && near(icon_set.thresholds[2], 90.0)
            && !icon_set.show_value
            && icon_set.reverse,
        "icon set payload projection mismatch");

    check(summary.conditional_format_count == 4
            && summary.color_scale_count == 2
            && summary.data_bar_count == 1
            && summary.icon_set_count == 1
            && summary.range_count == 5
            && summary.peak_ranges_per_format == 2
            && summary.peak_sqref_bytes >= 13
            && summary.peak_xml_nesting_depth >= 3,
        "conditional-format summary telemetry mismatch");
    check(fastxlsx::test::read_file(path) == before,
        "conditional-format reader changed the source package");
}

class CallbackFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void test_callback_failure_allows_retry()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-conditional-format-reader-retry.xlsx",
        representative_worksheet_xml());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetConditionalFormatReadCallbacks throwing;
    throwing.on_conditional_format =
        [](const fastxlsx::WorksheetConditionalFormatView&) {
            throw CallbackFailure("conditional-format callback stopped traversal");
        };
    bool saw_exact_exception = false;
    try {
        (void)reader.read_worksheet_conditional_formats("Data", throwing);
    } catch (const CallbackFailure& error) {
        saw_exact_exception = std::string_view(error.what())
            == "conditional-format callback stopped traversal";
    }
    check(saw_exact_exception,
        "conditional-format callback exception should propagate unchanged");

    std::size_t count = 0;
    fastxlsx::WorksheetConditionalFormatReadCallbacks retry;
    retry.on_conditional_format =
        [&](const fastxlsx::WorksheetConditionalFormatView&) { ++count; };
    const auto summary = reader.read_worksheet_conditional_formats("Data", retry);
    check(count == 4 && summary.conditional_format_count == 4,
        "conditional-format reader should retry after callback failure");
}

void test_absent_container_is_clean()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-conditional-format-reader-absent.xlsx",
        R"(<worksheet><sheetData/></worksheet>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    const auto summary = reader.read_worksheet_conditional_formats("Data");
    check(summary.conditional_format_count == 0 && summary.range_count == 0,
        "absent conditional formatting should be a clean empty result");
}

void test_guardrails()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-conditional-format-reader-guardrails.xlsx",
        representative_worksheet_xml());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetConditionalFormatReaderOptions options;
    options.max_sqref_bytes = 3;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_conditional_formats("Data", {}, options); },
        "max_sqref_bytes");
    options = {};
    options.max_ranges_per_format = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_conditional_formats("Data", {}, options); },
        "max_ranges_per_format");
    options = {};
    options.max_conditional_format_count = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_conditional_formats("Data", {}, options); },
        "max_conditional_format_count");
    options = {};
    options.max_xml_nesting_depth = 2;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_conditional_formats("Data", {}, options); },
        "max_xml_nesting_depth");
    options = {};
    options.max_xml_window_bytes = 8;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_conditional_formats("Data", {}, options); },
        "bounded input window");
    options = {};
    options.max_sqref_bytes = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_conditional_formats("Data", {}, options); },
        "nonzero max_sqref_bytes");
}

void expect_conditional_format_failure(
    std::string_view name, std::string xml, std::string_view diagnostic)
{
    const std::filesystem::path path = write_fixture(name, std::move(xml));
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_conditional_formats("Data"); },
        diagnostic);
}

void test_rejects_unsupported_and_malformed_shapes()
{
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-empty-container.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"/></worksheet>)",
        "exactly one cfRule");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-missing-sqref.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting><cfRule type="colorScale" priority="1"><colorScale><cfvo type="min"/><cfvo type="max"/><color rgb="FFFF0000"/><color rgb="FF00B050"/></colorScale></cfRule></conditionalFormatting></worksheet>)",
        "requires sqref");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-invalid-sqref.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A0"><cfRule type="colorScale" priority="1"><colorScale><cfvo type="min"/><cfvo type="max"/><color rgb="FFFF0000"/><color rgb="FF00B050"/></colorScale></cfRule></conditionalFormatting></worksheet>)",
        "invalid A1");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-missing-priority.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"><cfRule type="colorScale"><colorScale><cfvo type="min"/><cfvo type="max"/><color rgb="FFFF0000"/><color rgb="FF00B050"/></colorScale></cfRule></conditionalFormatting></worksheet>)",
        "requires priority");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-expression.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"><cfRule type="expression" priority="1"><formula>1=1</formula></cfRule></conditionalFormatting></worksheet>)",
        "unsupported type");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-dxf.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"><cfRule type="colorScale" priority="1" dxfId="0"><colorScale><cfvo type="min"/><cfvo type="max"/><color rgb="FFFF0000"/><color rgb="FF00B050"/></colorScale></cfRule></conditionalFormatting></worksheet>)",
        "unsupported attribute");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-multiple-cfrule.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"><cfRule type="dataBar" priority="1"><dataBar><cfvo type="min"/><cfvo type="max"/><color rgb="FF638EC6"/></dataBar></cfRule><cfRule type="dataBar" priority="2"><dataBar><cfvo type="min"/><cfvo type="max"/><color rgb="FF638EC6"/></dataBar></cfRule></conditionalFormatting></worksheet>)",
        "multiple cfRule");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-color-count.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"><cfRule type="colorScale" priority="1"><colorScale><cfvo type="min"/><cfvo type="max"/><color rgb="FFFF0000"/></colorScale></cfRule></conditionalFormatting></worksheet>)",
        "color count");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-min-val.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"><cfRule type="colorScale" priority="1"><colorScale><cfvo type="min" val="0"/><cfvo type="max"/><color rgb="FFFF0000"/><color rgb="FF00B050"/></colorScale></cfRule></conditionalFormatting></worksheet>)",
        "value shape");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-num-missing-val.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"><cfRule type="dataBar" priority="1"><dataBar><cfvo type="num"/><cfvo type="max"/><color rgb="FF638EC6"/></dataBar></cfRule></conditionalFormatting></worksheet>)",
        "value shape");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-bad-color.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"><cfRule type="dataBar" priority="1"><dataBar><cfvo type="min"/><cfvo type="max"/><color rgb="638EC6"/></dataBar></cfRule></conditionalFormatting></worksheet>)",
        "8-digit ARGB");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-advanced-databar.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"><cfRule type="dataBar" priority="1"><dataBar minLength="0"><cfvo type="min"/><cfvo type="max"/><color rgb="FF638EC6"/></dataBar></cfRule></conditionalFormatting></worksheet>)",
        "advanced attribute");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-custom-icon.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"><cfRule type="iconSet" priority="1"><iconSet iconSet="3TrafficLights1"><cfvo type="percent" val="0"/><cfvo type="percent" val="33"/><cfvo type="percent" val="67"/></iconSet></cfRule></conditionalFormatting></worksheet>)",
        "3Arrows");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-icon-mixed-type.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"><cfRule type="iconSet" priority="1"><iconSet iconSet="3Arrows"><cfvo type="percent" val="0"/><cfvo type="num" val="33"/><cfvo type="percent" val="67"/></iconSet></cfRule></conditionalFormatting></worksheet>)",
        "types must match");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-icon-order.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"><cfRule type="iconSet" priority="1"><iconSet iconSet="3Arrows"><cfvo type="percent" val="0"/><cfvo type="percent" val="67"/><cfvo type="percent" val="33"/></iconSet></cfRule></conditionalFormatting></worksheet>)",
        "strictly ascending");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-formula-child.xlsx",
        R"(<worksheet><sheetData/><conditionalFormatting sqref="A1"><cfRule type="colorScale" priority="1"><formula>1=1</formula></cfRule></conditionalFormatting></worksheet>)",
        "unsupported child");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-schema.xlsx",
        R"(<worksheet><sheetData/><dataValidations/><conditionalFormatting sqref="A1"><cfRule type="dataBar" priority="1"><dataBar><cfvo type="min"/><cfvo type="max"/><color rgb="FF638EC6"/></dataBar></cfRule></conditionalFormatting></worksheet>)",
        "schema order");
    expect_conditional_format_failure(
        "worksheet-conditional-format-reader-qname.xlsx",
        R"(<x:worksheet xmlns:x="urn:main"><x:sheetData/><x:conditionalFormatting sqref="A1"><x:cfRule type="dataBar" priority="1"><x:dataBar><x:cfvo type="min"/><x:cfvo type="max"/><x:color rgb="FF638EC6"/></x:dataBar></x:cfRule></y:conditionalFormatting></x:worksheet>)",
        "QName");
}

void test_foreign_extension_names_do_not_alias_conditional_formatting()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-conditional-format-reader-foreign-extension.xlsx",
        R"(<x:worksheet xmlns:x="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:z="urn:test"><x:sheetData/><x:conditionalFormatting sqref="A1"><x:cfRule type="dataBar" priority="1"><x:dataBar><x:cfvo type="min"/><x:cfvo type="max"/><x:color rgb="FF638EC6"/></x:dataBar></x:cfRule></x:conditionalFormatting><x:extLst><x:ext uri="test"><z:conditionalFormatting sqref="B1"><z:cfRule type="dataBar" priority="99"/></z:conditionalFormatting></x:ext></x:extLst></x:worksheet>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::size_t count = 0;
    fastxlsx::WorksheetConditionalFormatReadCallbacks callbacks;
    callbacks.on_conditional_format =
        [&](const fastxlsx::WorksheetConditionalFormatView&) { ++count; };
    const auto summary = reader.read_worksheet_conditional_formats("Data", callbacks);
    check(count == 1 && summary.conditional_format_count == 1,
        "foreign extension local names must not alias conditional formatting");
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
fastxlsx::TwoColorScaleRule make_two_color_scale()
{
    fastxlsx::TwoColorScaleRule rule;
    rule.lower = {fastxlsx::ColorScaleValueType::Minimum, 0.0,
        fastxlsx::ArgbColor {0xFF, 0xFF, 0x00, 0x00}};
    rule.upper = {fastxlsx::ColorScaleValueType::Maximum, 0.0,
        fastxlsx::ArgbColor {0xFF, 0x00, 0xB0, 0x50}};
    return rule;
}

fastxlsx::DataBarRule make_data_bar()
{
    fastxlsx::DataBarRule rule;
    rule.lower = {fastxlsx::DataBarValueType::Minimum, 0.0};
    rule.upper = {fastxlsx::DataBarValueType::Maximum, 0.0};
    rule.color = fastxlsx::ArgbColor {0xFF, 0x63, 0x8E, 0xC6};
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

void test_reads_production_deflate_conditional_formats()
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(
        "worksheet-conditional-format-reader-deflate.xlsx");
    fastxlsx::WorkbookWriterOptions options;
    options.zip_compression_level = 1;
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path, options);
    fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Data");
    sheet.append_row({fastxlsx::CellView::text("A"),
        fastxlsx::CellView::text("B"), fastxlsx::CellView::text("C")});
    sheet.append_row({fastxlsx::CellView::number(1.0),
        fastxlsx::CellView::number(2.0), fastxlsx::CellView::number(3.0)});
    sheet.append_row({fastxlsx::CellView::number(4.0),
        fastxlsx::CellView::number(5.0), fastxlsx::CellView::number(6.0)});
    sheet.add_conditional_color_scale({2, 1, 3, 1}, make_two_color_scale());
    sheet.add_conditional_data_bar({2, 2, 3, 2}, make_data_bar());
    sheet.add_conditional_icon_set({2, 3, 3, 3}, make_icon_set());
    writer.close();

    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::vector<fastxlsx::WorksheetConditionalFormatKind> kinds;
    fastxlsx::WorksheetConditionalFormatReadCallbacks callbacks;
    callbacks.on_conditional_format =
        [&](const fastxlsx::WorksheetConditionalFormatView& value) {
            kinds.push_back(value.kind);
        };
    const auto summary = reader.read_worksheet_conditional_formats("Data", callbacks);
    check(kinds.size() == 3
            && kinds[0] == fastxlsx::WorksheetConditionalFormatKind::TwoColorScale
            && kinds[1] == fastxlsx::WorksheetConditionalFormatKind::DataBar
            && kinds[2] == fastxlsx::WorksheetConditionalFormatKind::IconSet
            && summary.color_scale_count == 1
            && summary.data_bar_count == 1
            && summary.icon_set_count == 1,
        "DEFLATE conditional-format traversal mismatch");
}
#endif

} // namespace

int main()
{
    try {
        test_projects_writer_compatible_rules_in_source_order();
        test_callback_failure_allows_retry();
        test_absent_container_is_clean();
        test_guardrails();
        test_rejects_unsupported_and_malformed_shapes();
        test_foreign_extension_names_do_not_alias_conditional_formatting();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_reads_production_deflate_conditional_formats();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    std::cout << "All worksheet conditional-format reader tests passed\n";
    return 0;
}
