#include <fastxlsx/streaming_writer.hpp>

#include "image_test_bytes.hpp"
#include "zip_test_utils.hpp"

#include <chrono>
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

void test_streaming_writer_date_time_helpers_and_presets()
{
    const auto leap_day = std::chrono::year_month_day {
        std::chrono::year {2024}, std::chrono::month {2}, std::chrono::day {29}};
    const auto invalid_day = std::chrono::year_month_day {
        std::chrono::year {2024}, std::chrono::month {2}, std::chrono::day {30}};

    check(fastxlsx::date_time::excel_1900_date_serial(
              {std::chrono::year {1900}, std::chrono::month {1}, std::chrono::day {1}})
            == 1.0,
        "Excel 1900 date serial should start at 1 for 1900-01-01");
    check(fastxlsx::date_time::excel_1900_date_serial(
              {std::chrono::year {1900}, std::chrono::month {2}, std::chrono::day {28}})
            == 59.0,
        "Excel 1900 date serial should map 1900-02-28 to 59");
    check(fastxlsx::date_time::excel_1900_date_serial(
              {std::chrono::year {1900}, std::chrono::month {3}, std::chrono::day {1}})
            == 61.0,
        "Excel 1900 date serial should preserve the serial-60 compatibility gap");
    check(fastxlsx::date_time::excel_1900_date_serial(leap_day) == 45351.0,
        "Excel 1900 date serial mismatch for 2024-02-29");
    check(fastxlsx::date_time::excel_1900_time_fraction(std::chrono::hours {12}) == 0.5,
        "Excel time fraction mismatch for noon");
    check(fastxlsx::date_time::excel_1900_date_time_serial(
              leap_day, std::chrono::hours {12})
            == 45351.5,
        "Excel date-time serial mismatch for 2024-02-29 noon");

    check_fastxlsx_error(
        [invalid_day] { static_cast<void>(fastxlsx::date_time::excel_1900_date_serial(invalid_day)); },
        "date helper should reject invalid Gregorian dates");
    check_fastxlsx_error(
        [] {
            static_cast<void>(fastxlsx::date_time::excel_1900_date_serial(
                {std::chrono::year {1899}, std::chrono::month {12}, std::chrono::day {31}}));
        },
        "date helper should reject dates before Excel's 1900 date-system range");
    check_fastxlsx_error(
        [] {
            static_cast<void>(fastxlsx::date_time::excel_1900_time_fraction(
                std::chrono::milliseconds {-1}));
        },
        "time helper should reject negative time-of-day values");
    check_fastxlsx_error(
        [] {
            static_cast<void>(fastxlsx::date_time::excel_1900_time_fraction(
                std::chrono::hours {24}));
        },
        "time helper should reject 24:00:00 as an out-of-day value");

    const auto output_path =
        fastxlsx::test::artifact_dir() / "fastxlsx-streaming-styles-date-time-presets.xlsx";

    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    const auto date_style = workbook.add_style(fastxlsx::style_preset::date_iso());
    const auto time_style = workbook.add_style(fastxlsx::style_preset::time_hh_mm_ss());
    const auto date_time_style = workbook.add_style(fastxlsx::style_preset::date_time_iso());

    auto sheet = workbook.add_worksheet("DateTime");
    sheet.append_row({
        fastxlsx::CellView::text("Date"),
        fastxlsx::CellView::text("Time"),
        fastxlsx::CellView::text("DateTime"),
    });
    sheet.append_row({
        fastxlsx::CellView::number(
            fastxlsx::date_time::excel_1900_date_serial(leap_day)).with_style(date_style),
        fastxlsx::CellView::number(
            fastxlsx::date_time::excel_1900_time_fraction(std::chrono::hours {12}))
            .with_style(time_style),
        fastxlsx::CellView::number(
            fastxlsx::date_time::excel_1900_date_time_serial(
                leap_day, std::chrono::hours {12}))
            .with_style(date_time_style),
    });

    workbook.close();
    check(std::filesystem::exists(output_path), "date/time preset xlsx file was not generated");

    const auto entries = fastxlsx::test::read_zip_entries(output_path);
    check(entries.contains("xl/styles.xml"), "missing date/time preset styles part");
    check(!entries.contains("xl/worksheets/_rels/sheet1.xml.rels"),
        "date/time presets should not create worksheet relationships");
    check(!entries.contains("xl/sharedStrings.xml"),
        "date/time preset inline sample should not create shared strings");
    check(!entries.contains("xl/calcChain.xml"),
        "date/time preset sample should not create calcChain");

    const auto& styles_xml = entries.at("xl/styles.xml");
    check_contains(styles_xml, R"(<numFmts count="3">)",
        "date/time presets should create three custom number formats");
    check_contains(styles_xml, R"(<numFmt numFmtId="164" formatCode="yyyy-mm-dd"/>)",
        "date preset number format mismatch");
    check_contains(styles_xml, R"(<numFmt numFmtId="165" formatCode="hh:mm:ss"/>)",
        "time preset number format mismatch");
    check_contains(styles_xml,
        R"(<numFmt numFmtId="166" formatCode="yyyy-mm-dd hh:mm:ss"/>)",
        "date-time preset number format mismatch");
    check_contains(styles_xml,
        R"(<cellXfs count="4"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>)",
        "date/time preset cellXfs count mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="164" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/>)",
        "date preset xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="165" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/>)",
        "time preset xf mismatch");
    check_contains(styles_xml,
        R"(<xf numFmtId="166" fontId="0" fillId="0" borderId="0" xfId="0" applyNumberFormat="1"/>)",
        "date-time preset xf mismatch");

    const auto& worksheet_xml = entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:C2"/>)",
        "date/time preset worksheet dimension mismatch");
    check_contains(worksheet_xml, R"(<c r="A2" s="1"><v>45351</v></c>)",
        "date helper should write a numeric date serial");
    check_contains(worksheet_xml, R"(<c r="B2" s="2"><v>0.5</v></c>)",
        "time helper should write a numeric time fraction");
    check_contains(worksheet_xml, R"(<c r="C2" s="3"><v>45351.5</v></c>)",
        "date-time helper should write a numeric date-time serial");
    check(worksheet_xml.find("t=\"d\"") == std::string::npos,
        "date/time helpers should not create a dedicated date cell type");
    check(worksheet_xml.find("s=\"0\"") == std::string::npos,
        "date/time preset sample should not serialize s=\"0\"");
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

} // namespace

int main()
{
    try {
        test_streaming_writer_number_format_styles();
        test_streaming_writer_date_time_helpers_and_presets();
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
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
