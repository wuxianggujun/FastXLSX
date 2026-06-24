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

} // namespace

int main()
{
    try {
        test_streaming_writer_images();
        test_streaming_writer_image_metadata();
        test_streaming_writer_jpeg_images();
        test_streaming_writer_mixed_image_formats();
        test_streaming_writer_memory_images();
        test_streaming_writer_image_hyperlinks();
        test_streaming_writer_image_hyperlinks_mixed_objects();
        test_streaming_writer_image_anchor_markers();
        test_streaming_writer_mixed_object_relationship_ids();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
