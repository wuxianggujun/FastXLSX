#include <fastxlsx/fastxlsx.hpp>

#include "image_test_bytes.hpp"
#include "zip_test_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if FASTXLSX_HAS_IMAGES

namespace {

constexpr std::string_view drawing_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing";
constexpr std::string_view image_relationship_type =
    "http://schemas.openxmlformats.org/officeDocument/2006/relationships/image";
constexpr std::string_view drawing_content_type =
    "application/vnd.openxmlformats-officedocument.drawing+xml";

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
    bool matched = false;
    try {
        callback();
    } catch (const fastxlsx::FastXlsxError& error) {
        matched = std::string_view(error.what()).find(expected_text)
            != std::string_view::npos;
    }
    if (!matched) {
        throw TestFailure(
            "expected FastXlsxError diagnostic containing: " + std::string(expected_text));
    }
}

std::string bytes_to_string(std::span<const std::byte> bytes)
{
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

struct MediaFixture {
    std::string entry_name;
    std::string bytes;
    std::string content_type;
};

struct DrawingFixture {
    std::string worksheet_xml;
    std::string worksheet_relationships;
    std::optional<std::string> drawing_xml;
    std::string drawing_relationships;
    std::string drawing_entry = "xl/drawings/drawing1.xml";
    std::string drawing_part_content_type = std::string(drawing_content_type);
    std::vector<MediaFixture> media;
};

std::string worksheet_with_drawing(std::string_view relationship_id = "rIdDrawing")
{
    return std::string(
        R"(<s:worksheet xmlns:s="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><s:sheetData/><s:drawing rel:id=")")
        + std::string(relationship_id) + R"("/></s:worksheet>)";
}

std::string worksheet_drawing_relationships(
    std::string_view target = "../drawings/drawing1.xml",
    std::string_view type = drawing_relationship_type,
    std::string_view target_mode = {})
{
    std::string xml =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rIdDrawing" Type=")";
    xml += type;
    xml += R"(" Target=")";
    xml += target;
    xml += '"';
    if (!target_mode.empty()) {
        xml += R"( TargetMode=")";
        xml += target_mode;
        xml += '"';
    }
    xml += R"(/></Relationships>)";
    return xml;
}

std::string drawing_relationships(
    std::string_view first_target = "../media/image%31.png",
    std::string_view first_type = image_relationship_type,
    std::string_view first_target_mode = {},
    bool include_second = true)
{
    std::string xml =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rIdImage1" Type=")";
    xml += first_type;
    xml += R"(" Target=")";
    xml += first_target;
    xml += '"';
    if (!first_target_mode.empty()) {
        xml += R"( TargetMode=")";
        xml += first_target_mode;
        xml += '"';
    }
    xml += "/>";
    if (include_second) {
        xml += std::string(R"(<Relationship Id="rIdImage2" Type=")")
            + std::string(image_relationship_type)
            + R"(" Target="/xl/media/./image2.jpg"/>)";
    }
    xml += "</Relationships>";
    return xml;
}

std::string marker_xml(std::string_view prefix, std::uint32_t column,
    std::uint64_t column_offset, std::uint32_t row, std::uint64_t row_offset)
{
    return "<d:" + std::string(prefix) + "><d:col>" + std::to_string(column)
        + "</d:col><d:colOff>" + std::to_string(column_offset)
        + "</d:colOff><d:row>" + std::to_string(row)
        + "</d:row><d:rowOff>" + std::to_string(row_offset)
        + "</d:rowOff></d:" + std::string(prefix) + ">";
}

std::string anchor_xml(std::string_view edit_as, std::uint32_t object_id,
    std::string_view escaped_name, std::string_view escaped_description,
    std::string_view relationship_id, std::uint32_t from_column,
    std::uint32_t from_row, std::uint32_t to_column, std::uint32_t to_row,
    std::uint64_t width_emu, std::uint64_t height_emu)
{
    std::string xml = "<d:twoCellAnchor editAs=\"" + std::string(edit_as) + "\">";
    xml += marker_xml("from", from_column, 12, from_row, 34);
    xml += marker_xml("to", to_column, 56, to_row, 78);
    xml += "<d:pic><d:nvPicPr><d:cNvPr id=\"" + std::to_string(object_id)
        + "\" name=\"" + std::string(escaped_name) + "\"";
    if (!escaped_description.empty()) {
        xml += " descr=\"" + std::string(escaped_description) + "\"";
    }
    xml += R"(/><d:cNvPicPr><a:picLocks noChangeAspect="1"/></d:cNvPicPr></d:nvPicPr>)";
    xml += R"(<d:blipFill><a:blip rel:embed=")" + std::string(relationship_id)
        + R"("/><a:stretch><a:fillRect/></a:stretch></d:blipFill>)";
    xml += R"(<d:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx=")"
        + std::to_string(width_emu) + R"(" cy=")" + std::to_string(height_emu)
        + R"("/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom></d:spPr>)";
    xml += R"(</d:pic><d:clientData/></d:twoCellAnchor>)";
    return xml;
}

std::string drawing_root(std::string body)
{
    return std::string(R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<d:wsDr xmlns:d="http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)")
        + std::move(body) + "</d:wsDr>";
}

std::string representative_drawing_xml()
{
    std::string body = anchor_xml("oneCell", 7, "Logo &amp; Main",
        "A &quot;sample&quot;", "rIdImage1", 0, 2, 4, 6, 9525, 19050);
    body += anchor_xml("absolute", 8, "Photo", {}, "rIdImage2",
        1, 1, 3, 4, 19050, 9525);
    body += anchor_xml("twoCell", 9, "Logo Reuse", {}, "rIdImage1",
        5, 0, 6, 1, 9525, 9525);
    return drawing_root(std::move(body));
}

DrawingFixture representative_fixture()
{
    DrawingFixture fixture;
    fixture.worksheet_xml = worksheet_with_drawing();
    fixture.worksheet_relationships = worksheet_drawing_relationships("../drawings/./drawing1.xml");
    fixture.drawing_xml = representative_drawing_xml();
    fixture.drawing_relationships = drawing_relationships();
    fixture.media = {
        {"xl/media/image1.png", bytes_to_string(fastxlsx::test::tiny_png_bytes()), "image/png"},
        {"xl/media/image2.jpg", bytes_to_string(fastxlsx::test::tiny_jpeg_bytes()), "image/jpeg"},
    };
    return fixture;
}

std::map<std::string, std::string> workbook_entries(DrawingFixture fixture)
{
    std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)";
    if (fixture.drawing_xml.has_value()) {
        content_types += "<Override PartName=\"/" + fixture.drawing_entry
            + "\" ContentType=\"" + fixture.drawing_part_content_type + "\"/>";
    }
    for (const MediaFixture& media : fixture.media) {
        content_types += "<Override PartName=\"/" + media.entry_name
            + "\" ContentType=\"" + media.content_type + "\"/>";
    }
    content_types += "</Types>";

    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Data" sheetId="1" r:id="rId1"/></sheets></workbook>)";

    std::map<std::string, std::string> entries;
    fastxlsx::test::insert_zip_entry(entries, "[Content_Types].xml", content_types);
    fastxlsx::test::insert_zip_entry(entries, "_rels/.rels", package_relationships);
    fastxlsx::test::insert_zip_entry(entries, "xl/workbook.xml", workbook);
    fastxlsx::test::insert_zip_entry(entries,
        "xl/_rels/workbook.xml.rels", workbook_relationships);
    fastxlsx::test::insert_zip_entry(entries,
        "xl/worksheets/sheet1.xml", std::move(fixture.worksheet_xml));
    if (!fixture.worksheet_relationships.empty()) {
        fastxlsx::test::insert_zip_entry(entries,
            "xl/worksheets/_rels/sheet1.xml.rels",
            std::move(fixture.worksheet_relationships));
    }
    if (fixture.drawing_xml.has_value()) {
        fastxlsx::test::insert_zip_entry(entries,
            fixture.drawing_entry, std::move(*fixture.drawing_xml));
        if (!fixture.drawing_relationships.empty()) {
            const std::size_t slash = fixture.drawing_entry.find_last_of('/');
            const std::string relationships_entry =
                fixture.drawing_entry.substr(0, slash) + "/_rels/"
                + fixture.drawing_entry.substr(slash + 1) + ".rels";
            fastxlsx::test::insert_zip_entry(entries, relationships_entry,
                std::move(fixture.drawing_relationships));
        }
    }
    for (MediaFixture& media : fixture.media) {
        fastxlsx::test::insert_zip_entry(entries,
            std::move(media.entry_name), std::move(media.bytes));
    }
    return entries;
}

std::filesystem::path write_fixture(std::string_view name, DrawingFixture fixture)
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(name);
    fastxlsx::test::write_stored_zip_entries(path, workbook_entries(std::move(fixture)));
    return path;
}

void replace_once(std::string& text, std::string_view from, std::string_view to)
{
    const std::size_t position = text.find(from);
    if (position == std::string::npos) {
        throw TestFailure("test fixture replacement source not found");
    }
    text.replace(position, from.size(), to);
}

void test_projects_images_in_source_order_and_reuses_media()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-image-reader-representative.xlsx", representative_fixture());
    const std::string before = fastxlsx::test::read_file(path);
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    std::vector<fastxlsx::WorksheetImageView> values;
    fastxlsx::WorksheetImageReadCallbacks callbacks;
    callbacks.on_image = [&](const fastxlsx::WorksheetImageView& value) {
        values.push_back(value);
    };
    const fastxlsx::WorksheetImageReadSummary summary =
        reader.read_worksheet_images("Data", callbacks);

    check(values.size() == 3 && values[0].index == 0 && values[2].index == 2,
        "image source-order projection mismatch");
    check(values[0].edit_as == fastxlsx::ImageEditAs::OneCell
            && values[0].from.column_index == 0
            && values[0].from.row_index == 2
            && values[0].from.offset.column_emu == 12
            && values[0].to.column_index == 4
            && values[0].to.row_index == 6
            && values[0].to.offset.row_emu == 78,
        "first image anchor projection mismatch");
    check(values[0].name == "Logo & Main"
            && values[0].description == "A \"sample\""
            && values[0].transform_width_emu == 9525
            && values[0].transform_height_emu == 19050
            && values[0].format == fastxlsx::ImageFormat::Png
            && values[0].encoded_size_bytes == fastxlsx::test::tiny_png_bytes().size(),
        "first image metadata/media projection mismatch");
    check(values[1].edit_as == fastxlsx::ImageEditAs::Absolute
            && values[1].format == fastxlsx::ImageFormat::Jpeg
            && values[1].encoded_size_bytes == fastxlsx::test::tiny_jpeg_bytes().size()
            && values[2].format == fastxlsx::ImageFormat::Png,
        "JPEG or reused PNG projection mismatch");
    check(summary.image_count == 3 && summary.unique_media_count == 2
            && summary.unique_media_bytes
                == fastxlsx::test::tiny_png_bytes().size()
                    + fastxlsx::test::tiny_jpeg_bytes().size()
            && summary.peak_media_bytes == fastxlsx::test::tiny_png_bytes().size()
            && summary.peak_retained_media_count == 2,
        "image reader media summary mismatch");
    check(summary.peak_xml_nesting_depth >= 5
            && summary.peak_relationship_id_bytes >= 9
            && summary.peak_relationship_target_bytes >= 20
            && summary.peak_name_bytes >= 11
            && summary.peak_description_bytes >= 10
            && summary.peak_numeric_text_bytes >= 5,
        "image reader guardrail telemetry mismatch");
    check(fastxlsx::test::read_file(path) == before,
        "image reader changed the source package");
}

void test_callback_failure_allows_retry()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-image-reader-retry.xlsx", representative_fixture());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    fastxlsx::WorksheetImageReadCallbacks throwing;
    throwing.on_image = [](const fastxlsx::WorksheetImageView&) {
        throw CallbackFailure("image callback stopped traversal");
    };
    bool saw_exact_exception = false;
    try {
        (void)reader.read_worksheet_images("Data", throwing);
    } catch (const CallbackFailure& error) {
        saw_exact_exception = std::string_view(error.what())
            == "image callback stopped traversal";
    }
    check(saw_exact_exception,
        "image callback exception should propagate unchanged");

    std::size_t count = 0;
    fastxlsx::WorksheetImageReadCallbacks retry;
    retry.on_image = [&](const fastxlsx::WorksheetImageView&) { ++count; };
    const auto summary = reader.read_worksheet_images("Data", retry);
    check(count == 3 && summary.image_count == 3,
        "image reader should retry from the worksheet after callback failure");
}

void test_absent_drawing_is_clean()
{
    DrawingFixture fixture;
    fixture.worksheet_xml =
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main"><sheetData/></worksheet>)";
    const std::filesystem::path path = write_fixture(
        "worksheet-image-reader-absent.xlsx", std::move(fixture));
    const auto reader = fastxlsx::WorkbookReader::open(path);
    const auto summary = reader.read_worksheet_images("Data");
    check(summary.image_count == 0 && summary.unique_media_count == 0,
        "absent drawing should be a clean empty result");
}

void test_guardrails()
{
    const std::filesystem::path path = write_fixture(
        "worksheet-image-reader-guardrails.xlsx", representative_fixture());
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);

    fastxlsx::WorksheetImageReaderOptions options;
    options.max_image_count = 2;
    expect_fastxlsx_error([&] { (void)reader.read_worksheet_images("Data", {}, options); },
        "max_image_count");
    options = {};
    options.max_relationship_id_bytes = 4;
    expect_fastxlsx_error([&] { (void)reader.read_worksheet_images("Data", {}, options); },
        "max_relationship_id_bytes");
    options = {};
    options.max_relationship_target_bytes = 8;
    expect_fastxlsx_error([&] { (void)reader.read_worksheet_images("Data", {}, options); },
        "max_relationship_target_bytes");
    options = {};
    options.max_name_bytes = 4;
    expect_fastxlsx_error([&] { (void)reader.read_worksheet_images("Data", {}, options); },
        "max_name_bytes");
    options = {};
    options.max_description_bytes = 4;
    expect_fastxlsx_error([&] { (void)reader.read_worksheet_images("Data", {}, options); },
        "max_description_bytes");
    options = {};
    options.max_numeric_text_bytes = 2;
    expect_fastxlsx_error([&] { (void)reader.read_worksheet_images("Data", {}, options); },
        "max_numeric_text_bytes");
    options = {};
    options.max_media_bytes = 16;
    expect_fastxlsx_error([&] { (void)reader.read_worksheet_images("Data", {}, options); },
        "max_media_bytes");
    options = {};
    options.max_xml_nesting_depth = 2;
    expect_fastxlsx_error([&] { (void)reader.read_worksheet_images("Data", {}, options); },
        "max_xml_nesting_depth");
    options = {};
    options.max_xml_window_bytes = 16;
    expect_fastxlsx_error([&] { (void)reader.read_worksheet_images("Data", {}, options); },
        "bounded input window");
    options = {};
    options.max_media_bytes = 0;
    expect_fastxlsx_error([&] { (void)reader.read_worksheet_images("Data", {}, options); },
        "nonzero max_media_bytes");
}

void expect_fixture_error(std::string_view artifact_name,
    DrawingFixture fixture, std::string_view expected_text)
{
    const std::filesystem::path path = write_fixture(artifact_name, std::move(fixture));
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_images("Data"); }, expected_text);
}

void test_relationship_and_content_type_audit()
{
    DrawingFixture fixture = representative_fixture();
    fixture.worksheet_relationships.clear();
    expect_fixture_error("worksheet-image-reader-missing-worksheet-rels.xlsx",
        std::move(fixture), "requires worksheet relationships");

    fixture = representative_fixture();
    fixture.worksheet_xml = worksheet_with_drawing("rIdMissing");
    expect_fixture_error("worksheet-image-reader-missing-drawing-id.xlsx",
        std::move(fixture), "relationship id is missing");

    fixture = representative_fixture();
    fixture.worksheet_relationships = worksheet_drawing_relationships(
        "../drawings/drawing1.xml", "urn:not-drawing");
    expect_fixture_error("worksheet-image-reader-wrong-drawing-type.xlsx",
        std::move(fixture), "wrong type");

    fixture = representative_fixture();
    fixture.worksheet_relationships = worksheet_drawing_relationships(
        "https://example.invalid/drawing.xml", drawing_relationship_type, "External");
    expect_fixture_error("worksheet-image-reader-external-drawing.xlsx",
        std::move(fixture), "must be internal");

    fixture = representative_fixture();
    fixture.worksheet_relationships = worksheet_drawing_relationships(
        "../drawings/missing.xml");
    expect_fixture_error("worksheet-image-reader-unknown-drawing.xlsx",
        std::move(fixture), "unknown part");

    fixture = representative_fixture();
    fixture.drawing_part_content_type = "application/xml";
    expect_fixture_error("worksheet-image-reader-wrong-drawing-content-type.xlsx",
        std::move(fixture), "wrong content type");

    fixture = representative_fixture();
    fixture.drawing_relationships.clear();
    expect_fixture_error("worksheet-image-reader-missing-drawing-rels.xlsx",
        std::move(fixture), "requires drawing relationships");

    fixture = representative_fixture();
    replace_once(*fixture.drawing_xml, "rIdImage1", "rIdMissing");
    expect_fixture_error("worksheet-image-reader-missing-image-id.xlsx",
        std::move(fixture), "image relationship id is missing");

    fixture = representative_fixture();
    fixture.drawing_relationships = drawing_relationships(
        "../media/image1.png", "urn:not-image", {}, false);
    expect_fixture_error("worksheet-image-reader-wrong-image-type.xlsx",
        std::move(fixture), "wrong type");

    fixture = representative_fixture();
    fixture.drawing_relationships = drawing_relationships(
        "https://example.invalid/image.png", image_relationship_type, "External", false);
    expect_fixture_error("worksheet-image-reader-external-image.xlsx",
        std::move(fixture), "must be internal");

    fixture = representative_fixture();
    fixture.drawing_relationships = drawing_relationships(
        "../media/missing.png", image_relationship_type, {}, false);
    expect_fixture_error("worksheet-image-reader-unknown-media.xlsx",
        std::move(fixture), "unknown media part");

    fixture = representative_fixture();
    fixture.media[0].content_type = "application/octet-stream";
    expect_fixture_error("worksheet-image-reader-unsupported-media-content-type.xlsx",
        std::move(fixture), "unsupported content type");

    fixture = representative_fixture();
    fixture.drawing_relationships = drawing_relationships(
        "../media/image%XZ.png", image_relationship_type, {}, false);
    expect_fixture_error("worksheet-image-reader-invalid-percent-target.xlsx",
        std::move(fixture), "percent encoding");
}

void test_rejects_unsupported_drawing_shapes()
{
    DrawingFixture fixture = representative_fixture();
    fixture.drawing_xml = drawing_root(
        R"(<d:oneCellAnchor><d:clientData/></d:oneCellAnchor>)");
    expect_fixture_error("worksheet-image-reader-one-cell-anchor.xlsx",
        std::move(fixture), "only xdr:twoCellAnchor");

    fixture = representative_fixture();
    fixture.drawing_xml = drawing_root(
        R"(<d:absoluteAnchor><d:clientData/></d:absoluteAnchor>)");
    expect_fixture_error("worksheet-image-reader-absolute-anchor.xlsx",
        std::move(fixture), "only xdr:twoCellAnchor");

    fixture = representative_fixture();
    fixture.drawing_xml = drawing_root(R"(<d:graphicFrame/>)");
    expect_fixture_error("worksheet-image-reader-chart-object.xlsx",
        std::move(fixture), "non-picture drawing objects");

    fixture = representative_fixture();
    replace_once(*fixture.drawing_xml,
        R"(<d:cNvPr id="7" name="Logo &amp; Main" descr="A &quot;sample&quot;"/>)",
        R"(<d:cNvPr id="7" name="Logo &amp; Main" descr="A &quot;sample&quot;"><a:hlinkClick rel:id="rIdLink"/></d:cNvPr>)");
    expect_fixture_error("worksheet-image-reader-picture-hyperlink.xlsx",
        std::move(fixture), "picture hyperlinks");

    fixture = representative_fixture();
    replace_once(*fixture.drawing_xml, R"(<a:off x="0" y="0"/>)",
        R"(<a:off x="1" y="0"/>)");
    expect_fixture_error("worksheet-image-reader-transformed-position.xlsx",
        std::move(fixture), "transformed picture position");

    fixture = representative_fixture();
    replace_once(*fixture.drawing_xml, "<a:xfrm>", R"(<a:xfrm rot="1">)");
    expect_fixture_error("worksheet-image-reader-rotation.xlsx",
        std::move(fixture), "unsupported attribute");

    fixture = representative_fixture();
    replace_once(*fixture.drawing_xml, "<a:stretch>", "<a:srcRect/><a:stretch>");
    expect_fixture_error("worksheet-image-reader-crop.xlsx",
        std::move(fixture), "unsupported child order");

    fixture = representative_fixture();
    replace_once(*fixture.drawing_xml, "<d:to><d:col>4", "<d:to><d:col>0");
    expect_fixture_error("worksheet-image-reader-empty-anchor.xlsx",
        std::move(fixture), "non-empty range");

    fixture = representative_fixture();
    replace_once(*fixture.drawing_xml, "<d:pic>",
        R"(<d:pic xmlns:a="urn:foreign">)");
    expect_fixture_error("worksheet-image-reader-namespace-rebind.xlsx",
        std::move(fixture), "nested namespace declarations");
}

void corrupt_stored_entry_payload(
    const std::filesystem::path& path, std::string_view entry_name)
{
    std::string archive = fastxlsx::test::read_file(path);
    std::size_t offset = 0;
    while (offset + 30U <= archive.size()
        && fastxlsx::test::read_u32(archive, offset) == 0x04034b50U) {
        const std::uint32_t size = fastxlsx::test::read_u32(archive, offset + 18U);
        const std::uint16_t name_length = fastxlsx::test::read_u16(archive, offset + 26U);
        const std::uint16_t extra_length = fastxlsx::test::read_u16(archive, offset + 28U);
        const std::string name = archive.substr(offset + 30U, name_length);
        const std::size_t payload_offset = offset + 30U + name_length + extra_length;
        if (name == entry_name) {
            if (size == 0) {
                throw TestFailure("cannot corrupt an empty stored entry");
            }
            archive[payload_offset + size - 1U] ^= 0x01;
            fastxlsx::test::write_file(path, archive);
            return;
        }
        offset = payload_offset + size;
    }
    throw TestFailure("stored entry to corrupt was not found");
}

void test_media_signature_and_crc_audit()
{
    DrawingFixture fixture = representative_fixture();
    fixture.media[0].bytes = bytes_to_string(fastxlsx::test::tiny_jpeg_bytes());
    expect_fixture_error("worksheet-image-reader-png-signature.xlsx",
        std::move(fixture), "PNG media signature");

    const std::filesystem::path path = write_fixture(
        "worksheet-image-reader-media-crc.xlsx", representative_fixture());
    corrupt_stored_entry_payload(path, "xl/media/image1.png");
    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    expect_fastxlsx_error(
        [&] { (void)reader.read_worksheet_images("Data"); }, "CRC mismatch");
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_reads_production_deflate_images()
{
    const std::filesystem::path path = fastxlsx::test::artifact_path(
        "worksheet-image-reader-deflate.xlsx");
    fastxlsx::WorkbookWriterOptions writer_options;
    writer_options.zip_compression_level = 1;
    fastxlsx::WorkbookWriter writer =
        fastxlsx::WorkbookWriter::create(path, writer_options);
    fastxlsx::WorksheetWriter sheet = writer.add_worksheet("Data");
    sheet.append_row({fastxlsx::CellView::text("Pictures")});
    fastxlsx::ImageOptions png_options;
    png_options.edit_as = fastxlsx::ImageEditAs::OneCell;
    png_options.from_offset = {123, 456};
    png_options.to_offset = {789, 1011};
    png_options.name = "Generated Logo";
    png_options.description = "Generated description";
    sheet.add_image(fastxlsx::test::tiny_png_bytes(), {1, 1, 2, 2}, png_options);
    sheet.add_image(fastxlsx::test::tiny_jpeg_bytes(), {3, 2, 4, 3});
    writer.close();

    const fastxlsx::WorkbookReader reader = fastxlsx::WorkbookReader::open(path);
    std::vector<fastxlsx::WorksheetImageView> values;
    fastxlsx::WorksheetImageReadCallbacks callbacks;
    callbacks.on_image = [&](const fastxlsx::WorksheetImageView& value) {
        values.push_back(value);
    };
    const auto summary = reader.read_worksheet_images("Data", callbacks);
    check(values.size() == 2 && summary.image_count == 2
            && summary.unique_media_count == 2,
        "DEFLATE image traversal count mismatch");
    check(values[0].name == "Generated Logo"
            && values[0].description == "Generated description"
            && values[0].edit_as == fastxlsx::ImageEditAs::OneCell
            && values[0].from.column_index == 0
            && values[0].from.row_index == 0
            && values[0].from.offset.column_emu == 123
            && values[0].to.column_index == 2
            && values[0].to.row_index == 2
            && values[0].to.offset.row_emu == 1011
            && values[0].format == fastxlsx::ImageFormat::Png,
        "DEFLATE PNG anchor metadata mismatch");
    check(values[1].format == fastxlsx::ImageFormat::Jpeg
            && values[1].transform_width_emu == 19050
            && values[1].transform_height_emu == 9525,
        "DEFLATE JPEG media/extent mismatch");
}
#endif

} // namespace

#endif

int main()
{
#if !FASTXLSX_HAS_IMAGES
    std::cout << "Worksheet image reader tests skipped because image support is disabled\n";
    return 0;
#else
    try {
        test_projects_images_in_source_order_and_reuses_media();
        test_callback_failure_allows_retry();
        test_absent_drawing_is_clean();
        test_guardrails();
        test_relationship_and_content_type_audit();
        test_rejects_unsupported_drawing_shapes();
        test_media_signature_and_crc_audit();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_reads_production_deflate_images();
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "All worksheet image reader tests passed\n";
    return 0;
#endif
}
