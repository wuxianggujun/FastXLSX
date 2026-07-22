#include <fastxlsx/fastxlsx.hpp>

#include "zip_test_utils.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
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

class CallbackFailure : public std::runtime_error {
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
    bool failed = false;
    try {
        callback();
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = std::string_view(error.what()).find(expected_text) != std::string_view::npos;
    }
    check(failed, "expected FastXlsxError diagnostic was not observed");
}

struct FixtureOptions {
    bool include_relationship = true;
    bool external_relationship = false;
    bool duplicate_relationship = false;
    std::string relationship_target = "styles.xml";
    bool include_part = true;
    std::string part_path = "xl/styles.xml";
    std::string content_type =
        "application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml";
};

std::map<std::string, std::string> workbook_entries(
    std::string styles_xml, const FixtureOptions& options = {})
{
    std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)";
    if (options.include_part) {
        content_types += R"(<Override PartName="/)" + options.part_path
            + R"(" ContentType=")" + options.content_type + R"("/>)";
    }
    content_types += "</Types>";

    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)";
    if (options.include_relationship) {
        workbook_relationships +=
            R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target=")"
            + options.relationship_target + "\"";
        if (options.external_relationship) {
            workbook_relationships += R"( TargetMode="External")";
        }
        workbook_relationships += "/>";
        if (options.duplicate_relationship) {
            workbook_relationships +=
                R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)";
        }
    }
    workbook_relationships += "</Relationships>";

    const std::string workbook =
        R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Data" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::string worksheet =
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData/></worksheet>)";

    std::map<std::string, std::string> entries;
    fastxlsx::test::insert_zip_entry(entries, "[Content_Types].xml", content_types);
    fastxlsx::test::insert_zip_entry(entries, "_rels/.rels", package_relationships);
    fastxlsx::test::insert_zip_entry(entries, "xl/workbook.xml", workbook);
    fastxlsx::test::insert_zip_entry(
        entries, "xl/_rels/workbook.xml.rels", workbook_relationships);
    fastxlsx::test::insert_zip_entry(entries, "xl/worksheets/sheet1.xml", worksheet);
    if (options.include_part) {
        fastxlsx::test::insert_zip_entry(entries, options.part_path, std::move(styles_xml));
    }
    return entries;
}

std::filesystem::path write_fixture(std::string_view name,
    std::string styles_xml,
    const FixtureOptions& options = {})
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(name);
    fastxlsx::test::write_stored_zip_entries(
        path, workbook_entries(std::move(styles_xml), options));
    return path;
}

std::string minimal_components_xml()
{
    return R"(<styleSheet><fonts count="1"><font/></fonts><fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills></styleSheet>)";
}

void test_reader_projects_fonts_and_fills_in_source_order()
{
    const std::filesystem::path input_path = write_fixture(
        "style-components-representative.xlsx",
        R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<numFmts count="1"><numFmt numFmtId="164" formatCode="0.00"/></numFmts>)"
        R"(<fonts count="4">)"
        R"(<font><sz val="11"/><color theme="1"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
        R"(<font><b/><sz val="11.0"/><color rgb="ffc00000"/><name val="Calibri"/><family val="2"/><scheme val="minor"/></font>)"
        R"(<font><i val="1"/><color rgb="FF008000"/></font>)"
        R"(<font><b val="0"></b><i val="false"></i></font>)"
        R"(</fonts>)"
        R"(<fills count="3">)"
        R"(<fill><patternFill patternType="none"/></fill>)"
        R"(<fill><patternFill patternType="gray125"/></fill>)"
        R"(<fill><patternFill patternType="solid"><fgColor rgb="FFFFEB84"/><bgColor indexed="64"/></patternFill></fill>)"
        R"(</fills>)"
        R"(<borders count="1"><border/></borders>)"
        R"(<cellXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/></cellXfs>)"
        R"(</styleSheet>)");

    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(input_path);
    std::vector<fastxlsx::CellFormatFontView> fonts;
    std::vector<fastxlsx::CellFormatFillView> fills;
    fastxlsx::StyleComponentReadCallbacks callbacks;
    callbacks.on_font = [&](const fastxlsx::CellFormatFontView& font) {
        fonts.push_back(font);
    };
    callbacks.on_fill = [&](const fastxlsx::CellFormatFillView& fill) {
        fills.push_back(fill);
    };

    const fastxlsx::StyleComponentReadSummary summary =
        reader.read_style_components(callbacks);
    check(fonts.size() == 4 && fills.size() == 3,
        "style component callback counts mismatch");
    check(fonts[0].index == 0 && !fonts[0].bold && !fonts[0].italic
            && !fonts[0].direct_argb_color.has_value(),
        "default font projection mismatch");
    check(fonts[1].index == 1 && fonts[1].bold && !fonts[1].italic
            && fonts[1].direct_argb_color == std::optional<std::uint32_t>(0xFFC00000U),
        "bold direct-color font projection mismatch");
    check(fonts[2].index == 2 && !fonts[2].bold && fonts[2].italic
            && fonts[2].direct_argb_color == std::optional<std::uint32_t>(0xFF008000U),
        "italic direct-color font projection mismatch");
    check(fonts[3].index == 3 && !fonts[3].bold && !fonts[3].italic,
        "explicit false font flags should remain false");

    check(fills[0].index == 0
            && fills[0].pattern == fastxlsx::CellFormatFillPattern::None
            && !fills[0].foreground_argb_color.has_value(),
        "none fill projection mismatch");
    check(fills[1].index == 1
            && fills[1].pattern == fastxlsx::CellFormatFillPattern::Gray125
            && !fills[1].foreground_argb_color.has_value(),
        "gray125 fill projection mismatch");
    check(fills[2].index == 2
            && fills[2].pattern == fastxlsx::CellFormatFillPattern::Solid
            && fills[2].foreground_argb_color
                == std::optional<std::uint32_t>(0xFFFFEB84U),
        "solid fill projection mismatch");
    check(summary.font_count == 4 && summary.fill_count == 3,
        "style component summary counts mismatch");
    check(summary.peak_xml_nesting_depth >= 4,
        "style component nesting summary mismatch");
}

void test_callback_failure_releases_entry_and_reader_can_retry()
{
    const std::filesystem::path input_path = write_fixture(
        "style-components-callback-retry.xlsx", minimal_components_xml());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(input_path);

    fastxlsx::StyleComponentReadCallbacks callbacks;
    callbacks.on_font = [](const fastxlsx::CellFormatFontView&) {
        throw CallbackFailure("caller stopped font traversal");
    };
    bool saw_font_failure = false;
    try {
        (void)reader.read_style_components(callbacks);
    } catch (const CallbackFailure& error) {
        saw_font_failure = std::string_view(error.what()) == "caller stopped font traversal";
    }
    check(saw_font_failure, "font callback exception should propagate unchanged");
    check(reader.read_style_components().font_count == 1,
        "style traversal should restart after font callback failure");

    callbacks.on_font = {};
    callbacks.on_fill = [](const fastxlsx::CellFormatFillView&) {
        throw CallbackFailure("caller stopped fill traversal");
    };
    bool saw_fill_failure = false;
    try {
        (void)reader.read_style_components(callbacks);
    } catch (const CallbackFailure& error) {
        saw_fill_failure = std::string_view(error.what()) == "caller stopped fill traversal";
    }
    check(saw_fill_failure, "fill callback exception should propagate unchanged");
    check(reader.read_style_components().fill_count == 2,
        "style traversal should restart after fill callback failure");
}

void test_reader_enforces_memory_guardrails()
{
    const std::filesystem::path input_path = write_fixture(
        "style-components-guardrails.xlsx",
        R"(<styleSheet><fonts count="2"><font/><font/></fonts><fills count="2"><fill><patternFill patternType="none"/></fill><fill><patternFill patternType="gray125"/></fill></fills></styleSheet>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(input_path);

    fastxlsx::StyleComponentReaderOptions font_options;
    font_options.max_font_count = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_style_components({}, font_options); },
        "max_font_count");

    fastxlsx::StyleComponentReaderOptions fill_options;
    fill_options.max_fill_count = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_style_components({}, fill_options); },
        "max_fill_count");

    fastxlsx::StyleComponentReaderOptions nesting_options;
    nesting_options.max_xml_nesting_depth = 2;
    expect_fastxlsx_error(
        [&] { (void)reader.read_style_components({}, nesting_options); },
        "max_xml_nesting_depth");

    fastxlsx::StyleComponentReaderOptions window_options;
    window_options.max_xml_window_bytes = 16;
    expect_fastxlsx_error(
        [&] { (void)reader.read_style_components({}, window_options); },
        "bounded input window");

    fastxlsx::StyleComponentReaderOptions zero_window;
    zero_window.max_xml_window_bytes = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_style_components({}, zero_window); },
        "nonzero max_xml_window_bytes");
    fastxlsx::StyleComponentReaderOptions zero_depth;
    zero_depth.max_xml_nesting_depth = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_style_components({}, zero_depth); },
        "nonzero max_xml_nesting_depth");
    fastxlsx::StyleComponentReaderOptions zero_fonts;
    zero_fonts.max_font_count = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_style_components({}, zero_fonts); },
        "nonzero max_font_count");
    fastxlsx::StyleComponentReaderOptions zero_fills;
    zero_fills.max_fill_count = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_style_components({}, zero_fills); },
        "nonzero max_fill_count");
}

void test_reader_rejects_unsupported_or_malformed_component_shapes()
{
    const auto expect_failure = [](std::string_view name,
                                    std::string xml,
                                    std::string_view diagnostic) {
        const std::filesystem::path path = write_fixture(name, std::move(xml));
        const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
        expect_fastxlsx_error(
            [&] { (void)reader.read_style_components(); }, diagnostic);
    };

    expect_failure("style-components-root.xlsx", R"(<styles/>)", "styleSheet root");
    expect_failure("style-components-font-count.xlsx",
        R"(<styleSheet><fonts count="2"><font/></fonts></styleSheet>)",
        "fonts count mismatch");
    expect_failure("style-components-fill-count.xlsx",
        R"(<styleSheet><fills count="2"><fill><patternFill patternType="none"/></fill></fills></styleSheet>)",
        "fills count mismatch");
    expect_failure("style-components-duplicate-fonts.xlsx",
        R"(<styleSheet><fonts/><fonts/></styleSheet>)", "duplicate fonts");
    expect_failure("style-components-order.xlsx",
        R"(<styleSheet><fills/><fonts/></styleSheet>)", "fonts before fills");
    expect_failure("style-components-font-underline.xlsx",
        R"(<styleSheet><fonts count="1"><font><u/></font></fonts></styleSheet>)",
        "unsupported font metadata");
    expect_failure("style-components-font-size.xlsx",
        R"(<styleSheet><fonts count="1"><font><sz val="12"/></font></fonts></styleSheet>)",
        "non-default font size");
    expect_failure("style-components-font-theme.xlsx",
        R"(<styleSheet><fonts count="1"><font><color theme="2"/></font></fonts></styleSheet>)",
        "non-default font theme");
    expect_failure("style-components-font-color.xlsx",
        R"(<styleSheet><fonts count="1"><font><color rgb="FF00GG00"/></font></fonts></styleSheet>)",
        "invalid font ARGB");
    expect_failure("style-components-duplicate-bold.xlsx",
        R"(<styleSheet><fonts count="1"><font><b/><b/></font></fonts></styleSheet>)",
        "duplicate bold");
    expect_failure("style-components-gradient-fill.xlsx",
        R"(<styleSheet><fills count="1"><fill><gradientFill/></fill></fills></styleSheet>)",
        "patternFill");
    expect_failure("style-components-solid-missing-color.xlsx",
        R"(<styleSheet><fills count="1"><fill><patternFill patternType="solid"/></fill></fills></styleSheet>)",
        "solid fill foreground");
    expect_failure("style-components-none-color.xlsx",
        R"(<styleSheet><fills count="1"><fill><patternFill patternType="none"><fgColor rgb="FFFFFFFF"/></patternFill></fill></fills></styleSheet>)",
        "non-solid fill");
    expect_failure("style-components-fill-background.xlsx",
        R"(<styleSheet><fills count="1"><fill><patternFill patternType="solid"><fgColor rgb="FFFFFFFF"/><bgColor indexed="63"/></patternFill></fill></fills></styleSheet>)",
        "indexed fill background 64");
    expect_failure("style-components-duplicate-pattern.xlsx",
        R"(<styleSheet><fills count="1"><fill><patternFill patternType="none"/><patternFill patternType="gray125"/></fill></fills></styleSheet>)",
        "one patternFill");
    expect_failure("style-components-boundary.xlsx",
        R"(<styleSheet><fonts count="1"><font></fonts></styleSheet>)",
        "mismatched XML boundary");
}

void test_reader_audits_relationship_target_and_content_type()
{
    const std::string components = minimal_components_xml();

    FixtureOptions missing_relationship;
    missing_relationship.include_relationship = false;
    const fastxlsx::WorkbookReader no_relationship_reader = fastxlsx::WorkbookReader::open(
        write_fixture("style-components-no-relationship.xlsx", components,
            missing_relationship));
    expect_fastxlsx_error(
        [&] { (void)no_relationship_reader.read_style_components(); },
        "no styles relationship");

    FixtureOptions missing_target;
    missing_target.relationship_target = "missing.xml";
    const fastxlsx::WorkbookReader missing_target_reader = fastxlsx::WorkbookReader::open(
        write_fixture("style-components-missing-target.xlsx", components, missing_target));
    expect_fastxlsx_error(
        [&] { (void)missing_target_reader.read_style_components(); }, "unknown part");

    FixtureOptions wrong_content_type;
    wrong_content_type.content_type = "application/xml";
    const fastxlsx::WorkbookReader wrong_type_reader = fastxlsx::WorkbookReader::open(
        write_fixture("style-components-wrong-content.xlsx", components,
            wrong_content_type));
    expect_fastxlsx_error(
        [&] { (void)wrong_type_reader.read_style_components(); },
        "wrong content type");

    FixtureOptions encoded_target;
    encoded_target.relationship_target = "sty%6Ces.xml";
    const fastxlsx::WorkbookReader encoded_reader = fastxlsx::WorkbookReader::open(
        write_fixture("style-components-encoded-target.xlsx", components,
            encoded_target));
    check(encoded_reader.read_style_components().font_count == 1,
        "percent-encoded styles relationship target should resolve");

    FixtureOptions external;
    external.external_relationship = true;
    const std::filesystem::path external_path =
        write_fixture("style-components-external.xlsx", components, external);
    expect_fastxlsx_error(
        [&] { (void)fastxlsx::WorkbookReader::open(external_path); },
        "internal workbook relationships");

    FixtureOptions duplicate;
    duplicate.duplicate_relationship = true;
    expect_fastxlsx_error(
        [&] {
            (void)fastxlsx::WorkbookReader::open(
                write_fixture("style-components-duplicate-relationship.xlsx",
                    components, duplicate));
        },
        "duplicate workbook relationship type");
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_reader_reads_production_deflate_writer_output()
{
    const std::filesystem::path output_path =
        fastxlsx::test::artifact_path("style-components-deflate.xlsx");
    auto workbook = fastxlsx::WorkbookWriter::create(output_path);
    fastxlsx::CellStyle style;
    fastxlsx::CellFont font;
    font.bold = true;
    font.italic = true;
    font.color = fastxlsx::ArgbColor {0xFF, 0xC0, 0x00, 0x00};
    style.font = font;
    style.fill = fastxlsx::CellFill {
        fastxlsx::ArgbColor {0xFF, 0xFF, 0xEB, 0x84}};
    const fastxlsx::StyleId style_id = workbook.add_style(style);
    auto sheet = workbook.add_worksheet("Data");
    sheet.append_row({fastxlsx::CellView::text("styled").with_style(style_id)});
    workbook.close();

    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(output_path);
    std::vector<fastxlsx::CellFormatFontView> fonts;
    std::vector<fastxlsx::CellFormatFillView> fills;
    fastxlsx::StyleComponentReadCallbacks callbacks;
    callbacks.on_font = [&](const fastxlsx::CellFormatFontView& value) {
        fonts.push_back(value);
    };
    callbacks.on_fill = [&](const fastxlsx::CellFormatFillView& value) {
        fills.push_back(value);
    };
    const fastxlsx::StyleComponentReadSummary summary =
        reader.read_style_components(callbacks);

    check(summary.font_count == 2 && summary.fill_count == 3,
        "DEFLATE style component summary mismatch");
    check(fonts[1].bold && fonts[1].italic
            && fonts[1].direct_argb_color
                == std::optional<std::uint32_t>(0xFFC00000U),
        "DEFLATE font projection mismatch");
    check(fills[2].pattern == fastxlsx::CellFormatFillPattern::Solid
            && fills[2].foreground_argb_color
                == std::optional<std::uint32_t>(0xFFFFEB84U),
        "DEFLATE fill projection mismatch");
}
#endif

} // namespace

int main()
{
    try {
        test_reader_projects_fonts_and_fills_in_source_order();
        test_callback_failure_releases_entry_and_reader_can_retry();
        test_reader_enforces_memory_guardrails();
        test_reader_rejects_unsupported_or_malformed_component_shapes();
        test_reader_audits_relationship_target_and_content_type();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_reader_reads_production_deflate_writer_output();
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
