#include <fastxlsx/fastxlsx.hpp>

#include "zip_test_utils.hpp"

#include <cstdint>
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

void test_reader_projects_number_formats_and_cell_xfs_in_source_order()
{
    const std::filesystem::path path = write_fixture(
        "styles-reader-representative.xlsx",
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<numFmts count="2"><numFmt numFmtId="164" formatCode="yyyy-mm-dd"/>)"
        R"(<numFmt numFmtId="165" formatCode="0.00 &amp; &quot;units&quot;"/></numFmts>)"
        R"(<fonts count="2"><font><sz val="11"/></font><font><b/></font></fonts>)"
        R"(<fills count="3"><fill/><fill/><fill><patternFill patternType="solid"/></fill></fills>)"
        R"(<borders count="1"><border/></borders>)"
        R"(<cellStyleXfs count="1"><xf numFmtId="0"/></cellStyleXfs>)"
        R"(<cellXfs count="3">)"
        R"(<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>)"
        R"(<xf numFmtId="164" fontId="1" fillId="2" borderId="0" xfId="0" applyNumberFormat="1" applyFont="true" applyFill="1" applyAlignment="1">)"
        R"(<alignment wrapText="1" horizontal="center" vertical="bottom"/></xf>)"
        R"(<xf numFmtId="165" fontId="0" fillId="0" borderId="0" xfId="0" applyFill="false">)"
        R"(<alignment horizontal="right" vertical="top"/></xf>)"
        R"(</cellXfs><cellStyles count="1"><cellStyle name="Normal" xfId="0"/></cellStyles>)"
        R"(</styleSheet>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::vector<std::uint32_t> number_format_ids;
    std::vector<std::string> number_format_codes;
    std::vector<fastxlsx::CellFormatView> cell_formats;
    fastxlsx::CellFormatReadCallbacks callbacks;
    callbacks.on_number_format = [&](const fastxlsx::NumberFormatView& format) {
        number_format_ids.push_back(format.id);
        number_format_codes.emplace_back(format.format_code);
    };
    callbacks.on_cell_format = [&](const fastxlsx::CellFormatView& format) {
        cell_formats.push_back(format);
    };
    const fastxlsx::CellFormatReadSummary summary =
        reader.read_cell_formats(callbacks);

    check(number_format_ids == std::vector<std::uint32_t>({164, 165}),
        "custom number-format source order mismatch");
    check(number_format_codes
            == std::vector<std::string>({"yyyy-mm-dd", "0.00 & \"units\""}),
        "custom number-format decode mismatch");
    check(cell_formats.size() == 3, "cellXfs callback count mismatch");
    check(cell_formats[0].index == 0 && cell_formats[0].number_format_id == 0,
        "default cell format projection mismatch");
    check(cell_formats[1].index == 1 && cell_formats[1].number_format_id == 164
            && cell_formats[1].font_id == 1 && cell_formats[1].fill_id == 2,
        "custom cell format id projection mismatch");
    check(cell_formats[1].apply_number_format == std::optional<bool>(true)
            && cell_formats[1].apply_font == std::optional<bool>(true)
            && cell_formats[1].apply_fill == std::optional<bool>(true)
            && cell_formats[1].apply_alignment == std::optional<bool>(true),
        "custom cell format apply flags mismatch");
    check(cell_formats[1].alignment.has_value()
            && cell_formats[1].alignment->wrap_text == std::optional<bool>(true)
            && cell_formats[1].alignment->horizontal
                == fastxlsx::CellFormatHorizontalAlignment::Center
            && cell_formats[1].alignment->vertical
                == fastxlsx::CellFormatVerticalAlignment::Bottom,
        "custom cell format alignment mismatch");
    check(cell_formats[2].alignment.has_value()
            && cell_formats[2].alignment->horizontal
                == fastxlsx::CellFormatHorizontalAlignment::Right
            && cell_formats[2].alignment->vertical
                == fastxlsx::CellFormatVerticalAlignment::Top
            && cell_formats[2].apply_fill == std::optional<bool>(false),
        "second custom cell format projection mismatch");
    check(summary.custom_number_format_count == 2 && summary.cell_format_count == 3,
        "styles reader summary counts mismatch");
    check(summary.peak_format_code_bytes == number_format_codes[1].size(),
        "styles reader peak format-code bytes mismatch");
    check(summary.peak_xml_nesting_depth >= 3,
        "styles reader peak nesting depth mismatch");
}

class CallbackFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void test_callback_failure_releases_entry_and_reader_can_retry()
{
    const std::filesystem::path path = write_fixture(
        "styles-reader-callback-retry.xlsx",
        R"(<styleSheet><numFmts count="1"><numFmt numFmtId="164" formatCode="0.00"/></numFmts><cellXfs count="2"><xf/><xf/></cellXfs></styleSheet>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::CellFormatReadCallbacks callbacks;
    callbacks.on_number_format = [](const fastxlsx::NumberFormatView&) {
        throw CallbackFailure("caller stopped number-format traversal");
    };
    bool saw_exact_number_format_failure = false;
    try {
        (void)reader.read_cell_formats(callbacks);
    } catch (const CallbackFailure& error) {
        saw_exact_number_format_failure = std::string_view(error.what())
            == "caller stopped number-format traversal";
    }
    check(saw_exact_number_format_failure,
        "number-format callback exception should propagate unchanged");
    check(reader.read_cell_formats().custom_number_format_count == 1,
        "styles traversal should restart after a number-format callback failure");

    callbacks.on_number_format = {};
    callbacks.on_cell_format = [](const fastxlsx::CellFormatView&) {
        throw CallbackFailure("caller stopped styles traversal");
    };
    bool saw_exact_failure = false;
    try {
        (void)reader.read_cell_formats(callbacks);
    } catch (const CallbackFailure& error) {
        saw_exact_failure = std::string_view(error.what())
            == "caller stopped styles traversal";
    }
    check(saw_exact_failure, "styles callback exception should propagate unchanged");
    check(reader.read_cell_formats().cell_format_count == 2,
        "styles traversal should restart after callback failure");
}

void test_reader_move_transfers_styles_state()
{
    const std::filesystem::path path = write_fixture(
        "styles-reader-move.xlsx",
        R"(<styleSheet><cellXfs count="1"><xf/></cellXfs></styleSheet>)");
    fastxlsx::WorkbookReader source = fastxlsx::WorkbookReader::open(path);
    fastxlsx::WorkbookReader moved = std::move(source);

    expect_fastxlsx_error([&] { (void)source.read_cell_formats(); }, "not open");
    check(moved.read_cell_formats().cell_format_count == 1,
        "moved reader should retain styles traversal state");
}

void test_reader_enforces_memory_guardrails()
{
    const std::filesystem::path path = write_fixture(
        "styles-reader-guardrails.xlsx",
        R"(<styleSheet><numFmts count="2"><numFmt numFmtId="164" formatCode="abcdefgh"/><numFmt numFmtId="165" formatCode="0.00"/></numFmts><cellXfs count="1"><xf/></cellXfs></styleSheet>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::CellFormatReaderOptions code_options;
    code_options.max_format_code_bytes = 4;
    expect_fastxlsx_error(
        [&] { (void)reader.read_cell_formats({}, code_options); },
        "max_format_code_bytes");

    fastxlsx::CellFormatReaderOptions window_options;
    window_options.max_xml_window_bytes = 8;
    expect_fastxlsx_error(
        [&] { (void)reader.read_cell_formats({}, window_options); },
        "bounded input window");

    fastxlsx::CellFormatReaderOptions depth_options;
    depth_options.max_xml_nesting_depth = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_cell_formats({}, depth_options); },
        "max_xml_nesting_depth");

    fastxlsx::CellFormatReaderOptions count_options;
    count_options.max_custom_number_format_count = 1;
    expect_fastxlsx_error(
        [&] { (void)reader.read_cell_formats({}, count_options); },
        "max_custom_number_format_count");

    fastxlsx::CellFormatReaderOptions zero_window;
    zero_window.max_xml_window_bytes = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_cell_formats({}, zero_window); },
        "nonzero max_xml_window_bytes");
    fastxlsx::CellFormatReaderOptions zero_code;
    zero_code.max_format_code_bytes = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_cell_formats({}, zero_code); },
        "nonzero max_format_code_bytes");
    fastxlsx::CellFormatReaderOptions zero_depth;
    zero_depth.max_xml_nesting_depth = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_cell_formats({}, zero_depth); },
        "nonzero max_xml_nesting_depth");
    fastxlsx::CellFormatReaderOptions zero_count;
    zero_count.max_custom_number_format_count = 0;
    expect_fastxlsx_error(
        [&] { (void)reader.read_cell_formats({}, zero_count); },
        "nonzero max_custom_number_format_count");
}

void test_reader_handles_package_chunk_boundaries()
{
    const std::string code((1024U * 1024U) + 31U, 'x');
    const std::filesystem::path path = write_fixture(
        "styles-reader-chunk-boundary.xlsx",
        R"(<styleSheet><numFmts count="1"><numFmt numFmtId="164" formatCode=")"
            + code
            + R"("/></numFmts><cellXfs count="1"><xf numFmtId="164"/></cellXfs></styleSheet>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::size_t observed_size = 0;
    fastxlsx::CellFormatReadCallbacks callbacks;
    callbacks.on_number_format = [&](const fastxlsx::NumberFormatView& format) {
        observed_size = format.format_code.size();
    };
    fastxlsx::CellFormatReaderOptions options;
    options.max_xml_window_bytes = 2U * 1024U * 1024U;
    options.max_format_code_bytes = 2U * 1024U * 1024U;
    const fastxlsx::CellFormatReadSummary summary =
        reader.read_cell_formats(callbacks, options);
    check(observed_size == code.size() && summary.peak_format_code_bytes == code.size(),
        "styles reader should preserve a format code across package chunks");
}

void test_reader_handles_comment_prefix_at_package_chunk_boundary()
{
    constexpr std::size_t package_chunk_bytes = 1024U * 1024U;
    constexpr std::string_view root_open = "<styleSheet>";
    constexpr std::string_view comment = "<!-- boundary comment -->";

    for (std::size_t comment_prefix_bytes = 1; comment_prefix_bytes <= 4;
         ++comment_prefix_bytes) {
        std::string styles_xml(root_open);
        styles_xml.append(
            package_chunk_bytes - styles_xml.size() - comment_prefix_bytes, ' ');
        styles_xml += comment;
        styles_xml += R"(<cellXfs count="1"><xf/></cellXfs></styleSheet>)";

        const std::string file_name = "styles-reader-comment-prefix-"
            + std::to_string(comment_prefix_bytes) + ".xlsx";
        const std::filesystem::path path =
            write_fixture(file_name, std::move(styles_xml));
        const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

        fastxlsx::CellFormatReaderOptions options;
        options.max_xml_window_bytes = 2U * 1024U * 1024U;
        const fastxlsx::CellFormatReadSummary summary =
            reader.read_cell_formats({}, options);
        check(summary.cell_format_count == 1,
            "styles reader should accept comment prefixes split across package chunks");
    }
}

void test_reader_accepts_standard_noop_cell_format_metadata()
{
    const std::filesystem::path path = write_fixture(
        "styles-reader-noop-cell-format-metadata.xlsx",
        R"(<styleSheet><cellXfs count="1"><xf applyBorder="0" applyProtection="false" quotePrefix="0" pivotButton="false" applyAlignment="1"/></cellXfs></styleSheet>)");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::optional<fastxlsx::CellFormatView> observed;
    fastxlsx::CellFormatReadCallbacks callbacks;
    callbacks.on_cell_format = [&](const fastxlsx::CellFormatView& format) {
        observed = format;
    };
    const fastxlsx::CellFormatReadSummary summary =
        reader.read_cell_formats(callbacks);
    check(summary.cell_format_count == 1 && observed.has_value(),
        "standard no-op cell format metadata should remain readable");
    check(observed->apply_alignment == std::optional<bool>(true)
            && !observed->alignment.has_value(),
        "applyAlignment without inline alignment should remain explicit");
}

void test_reader_rejects_unsupported_or_malformed_style_shapes()
{
    const auto expect_failure = [](std::string_view name,
                                    std::string xml,
                                    std::string_view diagnostic) {
        const std::filesystem::path path = write_fixture(name, std::move(xml));
        const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
        expect_fastxlsx_error(
            [&] { (void)reader.read_cell_formats(); }, diagnostic);
    };

    expect_failure("styles-reader-root.xlsx", R"(<styles/>)", "styleSheet root");
    expect_failure("styles-reader-missing-cellxfs.xlsx", R"(<styleSheet/>)",
        "at least one cellXfs record");
    expect_failure("styles-reader-count-mismatch.xlsx",
        R"(<styleSheet><cellXfs count="2"><xf/></cellXfs></styleSheet>)",
        "cellXfs count mismatch");
    expect_failure("styles-reader-duplicate-cellxfs.xlsx",
        R"(<styleSheet><cellXfs count="1"><xf/></cellXfs><cellXfs count="1"><xf/></cellXfs></styleSheet>)",
        "duplicate cellXfs");
    expect_failure("styles-reader-late-numfmts.xlsx",
        R"(<styleSheet><cellXfs count="1"><xf/></cellXfs><numFmts count="0"/></styleSheet>)",
        "numFmts before cellXfs");
    expect_failure("styles-reader-border.xlsx",
        R"(<styleSheet><cellXfs count="1"><xf borderId="1"/></cellXfs></styleSheet>)",
        "non-default borders");
    expect_failure("styles-reader-enabled-apply-border.xlsx",
        R"(<styleSheet><cellXfs count="1"><xf applyBorder="1"/></cellXfs></styleSheet>)",
        "enabled applyBorder");
    expect_failure("styles-reader-base-style.xlsx",
        R"(<styleSheet><cellXfs count="1"><xf xfId="2"/></cellXfs></styleSheet>)",
        "base style references");
    expect_failure("styles-reader-protection.xlsx",
        R"(<styleSheet><cellXfs count="1"><xf><protection locked="1"/></xf></cellXfs></styleSheet>)",
        "unsupported nested cell format metadata");
    expect_failure("styles-reader-alignment.xlsx",
        R"(<styleSheet><cellXfs count="1"><xf><alignment horizontal="justify"/></xf></cellXfs></styleSheet>)",
        "unsupported horizontal alignment");
    expect_failure("styles-reader-number-format-child.xlsx",
        R"(<styleSheet><numFmts count="1"><numFmt numFmtId="164" formatCode="0"><ext/></numFmt></numFmts><cellXfs count="1"><xf/></cellXfs></styleSheet>)",
        "nested number format metadata");
    expect_failure("styles-reader-number-format-entity.xlsx",
        R"(<styleSheet><numFmts count="1"><numFmt numFmtId="164" formatCode="&unknown;"/></numFmts><cellXfs count="1"><xf/></cellXfs></styleSheet>)",
        "unknown XML entity");
    expect_failure("styles-reader-duplicate-number-format-id.xlsx",
        R"(<styleSheet><numFmts count="2"><numFmt numFmtId="164" formatCode="0"/><numFmt numFmtId="164" formatCode="0.00"/></numFmts><cellXfs count="1"><xf/></cellXfs></styleSheet>)",
        "duplicate custom numFmtId");
    expect_failure("styles-reader-mismatch.xlsx",
        R"(<styleSheet><cellXfs count="1"><xf></cellXfs></xf></styleSheet>)",
        "mismatched XML boundary");
}

void test_reader_audits_relationship_target_and_content_type()
{
    const std::string minimal_styles =
        R"(<styleSheet><cellXfs count="1"><xf/></cellXfs></styleSheet>)";

    FixtureOptions missing_relationship;
    missing_relationship.include_relationship = false;
    const std::filesystem::path no_relationship = write_fixture(
        "styles-reader-no-relationship.xlsx", minimal_styles, missing_relationship);
    const fastxlsx::WorkbookReader no_relationship_reader =
        fastxlsx::WorkbookReader::open(no_relationship);
    expect_fastxlsx_error(
        [&] { (void)no_relationship_reader.read_cell_formats(); },
        "no styles relationship");

    FixtureOptions missing_target;
    missing_target.relationship_target = "missing.xml";
    const std::filesystem::path unknown_part = write_fixture(
        "styles-reader-missing-target.xlsx", minimal_styles, missing_target);
    const fastxlsx::WorkbookReader unknown_part_reader =
        fastxlsx::WorkbookReader::open(unknown_part);
    expect_fastxlsx_error(
        [&] { (void)unknown_part_reader.read_cell_formats(); }, "unknown part");

    FixtureOptions wrong_content_type;
    wrong_content_type.content_type = "application/xml";
    const std::filesystem::path wrong_type = write_fixture(
        "styles-reader-wrong-content-type.xlsx", minimal_styles, wrong_content_type);
    const fastxlsx::WorkbookReader wrong_type_reader =
        fastxlsx::WorkbookReader::open(wrong_type);
    expect_fastxlsx_error(
        [&] { (void)wrong_type_reader.read_cell_formats(); }, "wrong content type");

    FixtureOptions encoded_target;
    encoded_target.relationship_target = "sty%6Ces.xml";
    const std::filesystem::path encoded = write_fixture(
        "styles-reader-encoded-target.xlsx", minimal_styles, encoded_target);
    const fastxlsx::WorkbookReader encoded_reader =
        fastxlsx::WorkbookReader::open(encoded);
    check(encoded_reader.read_cell_formats().cell_format_count == 1,
        "percent-encoded styles relationship target should resolve");

    FixtureOptions invalid_percent;
    invalid_percent.relationship_target = "styles%ZZ.xml";
    const std::filesystem::path invalid_percent_path = write_fixture(
        "styles-reader-invalid-percent-target.xlsx", minimal_styles, invalid_percent);
    const fastxlsx::WorkbookReader invalid_percent_reader =
        fastxlsx::WorkbookReader::open(invalid_percent_path);
    expect_fastxlsx_error(
        [&] { (void)invalid_percent_reader.read_cell_formats(); },
        "invalid percent escape");

    FixtureOptions qualified_target;
    qualified_target.relationship_target = "styles.xml?version=1";
    const std::filesystem::path qualified_path = write_fixture(
        "styles-reader-qualified-target.xlsx", minimal_styles, qualified_target);
    const fastxlsx::WorkbookReader qualified_reader =
        fastxlsx::WorkbookReader::open(qualified_path);
    expect_fastxlsx_error(
        [&] { (void)qualified_reader.read_cell_formats(); },
        "must be a package part");

    FixtureOptions external;
    external.external_relationship = true;
    const std::filesystem::path external_path = write_fixture(
        "styles-reader-external-relationship.xlsx", minimal_styles, external);
    expect_fastxlsx_error(
        [&] { (void)fastxlsx::WorkbookReader::open(external_path); },
        "internal workbook relationships");

    FixtureOptions duplicate;
    duplicate.duplicate_relationship = true;
    const std::filesystem::path duplicate_path = write_fixture(
        "styles-reader-duplicate-relationship.xlsx", minimal_styles, duplicate);
    expect_fastxlsx_error(
        [&] { (void)fastxlsx::WorkbookReader::open(duplicate_path); },
        "duplicate workbook relationship type");
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_reader_reads_production_deflate_writer_output()
{
    const std::filesystem::path path =
        fastxlsx::test::artifact_path("styles-reader-deflate.xlsx");
    fastxlsx::WorkbookWriterOptions options;
    options.zip_compression_level = 1;
    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path, options);

    fastxlsx::CellStyle style;
    style.number_format = "0.00";
    style.font = fastxlsx::CellFont {true, true, std::nullopt};
    style.fill = fastxlsx::CellFill {fastxlsx::ArgbColor {0xFF, 0x12, 0x34, 0x56}};
    style.alignment = fastxlsx::CellAlignment {
        true, fastxlsx::HorizontalAlignment::Center, fastxlsx::VerticalAlignment::Bottom};
    const fastxlsx::StyleId style_id = writer.add_style(style);
    fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Data");
    sheet.append_row({fastxlsx::CellView::number(12.5).with_style(style_id)});
    writer.close();

    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::vector<std::string> codes;
    std::vector<fastxlsx::CellFormatView> formats;
    fastxlsx::CellFormatReadCallbacks callbacks;
    callbacks.on_number_format = [&](const fastxlsx::NumberFormatView& format) {
        codes.emplace_back(format.format_code);
    };
    callbacks.on_cell_format = [&](const fastxlsx::CellFormatView& format) {
        formats.push_back(format);
    };
    const fastxlsx::CellFormatReadSummary summary =
        reader.read_cell_formats(callbacks);
    check(summary.custom_number_format_count == 1 && summary.cell_format_count == 2,
        "DEFLATE styles summary mismatch");
    check(codes == std::vector<std::string>({"0.00"}),
        "DEFLATE custom number-format projection mismatch");
    check(formats[1].font_id == 1 && formats[1].fill_id == 2
            && formats[1].alignment.has_value(),
        "DEFLATE custom cell format projection mismatch");
}
#endif

} // namespace

int main()
{
    try {
        test_reader_projects_number_formats_and_cell_xfs_in_source_order();
        test_callback_failure_releases_entry_and_reader_can_retry();
        test_reader_move_transfers_styles_state();
        test_reader_enforces_memory_guardrails();
        test_reader_handles_package_chunk_boundaries();
        test_reader_handles_comment_prefix_at_package_chunk_boundary();
        test_reader_accepts_standard_noop_cell_format_metadata();
        test_reader_rejects_unsupported_or_malformed_style_shapes();
        test_reader_audits_relationship_target_and_content_type();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_reader_reads_production_deflate_writer_output();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
