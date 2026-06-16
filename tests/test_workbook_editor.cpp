// Structure tests for the public Patch-mode WorkbookEditor facade.
//
// These tests build a real source workbook through the public WorkbookWriter,
// edit it through WorkbookEditor::replace_sheet_data(), and verify the output
// package through the shared ZIP test reader.
// They intentionally stay at the public-API level and do not touch the internal
// PackageEditor test surface in test_package_editor.cpp.

#include <fastxlsx/workbook.hpp>
#include <fastxlsx/workbook_editor.hpp>
#include <fastxlsx/streaming_writer.hpp>

#include "zip_test_utils.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, std::string_view message)
{
    if (!condition) {
        ++g_failures;
        std::fprintf(stderr, "FAILED: %.*s\n",
            static_cast<int>(message.size()), message.data());
    }
}

void check_contains(
    const std::string& haystack, std::string_view needle, std::string_view message)
{
    if (haystack.find(needle) == std::string::npos) {
        ++g_failures;
        std::fprintf(stderr, "FAILED: %.*s\n  missing: %.*s\n  in: %s\n",
            static_cast<int>(message.size()), message.data(),
            static_cast<int>(needle.size()), needle.data(), haystack.c_str());
    }
}

void check_not_contains(
    const std::string& haystack, std::string_view needle, std::string_view message)
{
    check(haystack.find(needle) == std::string::npos, message);
}

bool threw_fastxlsx_error(const std::function<void()>& action)
{
    try {
        action();
    } catch (const fastxlsx::FastXlsxError&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

void check_public_inspection_preserves_last_edit_error(
    fastxlsx::WorkbookEditor& editor, const std::optional<std::string>& expected)
{
    (void)editor.worksheet_names();
    check(editor.last_edit_error() == expected,
        "worksheet_names should not update last_edit_error");

    (void)editor.has_worksheet("Data");
    check(editor.last_edit_error() == expected,
        "has_worksheet should not update last_edit_error");

    (void)editor.source_worksheet_names();
    check(editor.last_edit_error() == expected,
        "source_worksheet_names should not update last_edit_error");

    (void)editor.has_source_worksheet("Data");
    check(editor.last_edit_error() == expected,
        "has_source_worksheet should not update last_edit_error");

    (void)editor.has_pending_changes();
    check(editor.last_edit_error() == expected,
        "has_pending_changes should not update last_edit_error");

    (void)editor.pending_change_count();
    check(editor.last_edit_error() == expected,
        "pending_change_count should not update last_edit_error");

    (void)editor.pending_replacement_cell_count();
    check(editor.last_edit_error() == expected,
        "pending_replacement_cell_count should not update last_edit_error");

    (void)editor.pending_replacement_worksheet_names();
    check(editor.last_edit_error() == expected,
        "pending_replacement_worksheet_names should not update last_edit_error");

    (void)editor.pending_materialized_worksheet_names();
    check(editor.last_edit_error() == expected,
        "pending_materialized_worksheet_names should not update last_edit_error");

    (void)editor.has_pending_replacement("Data");
    check(editor.last_edit_error() == expected,
        "has_pending_replacement should not update last_edit_error");

    (void)editor.estimated_pending_replacement_memory_usage();
    check(editor.last_edit_error() == expected,
        "estimated_pending_replacement_memory_usage should not update last_edit_error");

    (void)editor.pending_worksheet_edits();
    check(editor.last_edit_error() == expected,
        "pending_worksheet_edits should not update last_edit_error");

    (void)editor.worksheet_catalog();
    check(editor.last_edit_error() == expected,
        "worksheet_catalog should not update last_edit_error");
}

std::filesystem::path artifact(std::string_view name)
{
    return fastxlsx::test::artifact_path(name);
}

void write_binary_file(const std::filesystem::path& path, std::string_view data)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("failed to open test artifact for writing");
    }
    if (!data.empty()) {
        stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
    if (!stream) {
        throw std::runtime_error("failed to write test artifact");
    }
}

std::size_t find_end_of_central_directory(const std::string& data)
{
    if (data.size() < 22) {
        throw std::runtime_error("test ZIP package is too small");
    }

    for (std::size_t offset = data.size() - 22; offset != static_cast<std::size_t>(-1);
         --offset) {
        if (fastxlsx::test::read_u32(data, offset) == 0x06054b50u) {
            return offset;
        }
        if (offset == 0) {
            break;
        }
    }

    throw std::runtime_error("test ZIP end of central directory not found");
}

struct ZipEntryPayloadLocation {
    std::size_t central_offset = 0;
    std::size_t data_offset = 0;
    std::uint32_t compressed_size = 0;
};

ZipEntryPayloadLocation find_zip_entry_payload_location(
    const std::string& data, std::string_view name)
{
    const std::size_t eocd_offset = find_end_of_central_directory(data);
    const std::uint16_t entry_count =
        fastxlsx::test::read_u16(data, eocd_offset + 10u);
    std::size_t offset = fastxlsx::test::read_u32(data, eocd_offset + 16u);

    for (std::uint16_t index = 0; index < entry_count; ++index) {
        if (offset + 46u > data.size()
            || fastxlsx::test::read_u32(data, offset) != 0x02014b50u) {
            throw std::runtime_error("test ZIP central directory entry is invalid");
        }

        const std::uint16_t name_size = fastxlsx::test::read_u16(data, offset + 28u);
        const std::uint16_t extra_size = fastxlsx::test::read_u16(data, offset + 30u);
        const std::uint16_t comment_size = fastxlsx::test::read_u16(data, offset + 32u);
        const std::size_t record_size = 46u + name_size + extra_size + comment_size;
        if (offset + record_size > data.size()) {
            throw std::runtime_error("test ZIP central directory entry is truncated");
        }

        const std::string entry_name = data.substr(offset + 46u, name_size);
        if (entry_name == name) {
            const std::uint32_t compressed_size =
                fastxlsx::test::read_u32(data, offset + 20u);
            const std::size_t local_offset =
                fastxlsx::test::read_u32(data, offset + 42u);
            if (local_offset + 30u > data.size()
                || fastxlsx::test::read_u32(data, local_offset) != 0x04034b50u) {
                throw std::runtime_error("test ZIP local header entry is invalid");
            }

            const std::uint16_t local_name_size =
                fastxlsx::test::read_u16(data, local_offset + 26u);
            const std::uint16_t local_extra_size =
                fastxlsx::test::read_u16(data, local_offset + 28u);
            const std::size_t data_offset =
                local_offset + 30u + local_name_size + local_extra_size;
            if (compressed_size == 0 || data_offset + compressed_size > data.size()) {
                throw std::runtime_error("test ZIP entry payload is invalid");
            }
            return {offset, data_offset, compressed_size};
        }

        offset += record_size;
    }

    throw std::runtime_error("test ZIP entry not found");
}

void write_u32(std::string& data, std::size_t offset, std::uint32_t value)
{
    if (offset + 4u > data.size()) {
        throw std::runtime_error("test ZIP patch offset is out of range");
    }
    data[offset] = static_cast<char>(value & 0xffu);
    data[offset + 1u] = static_cast<char>((value >> 8u) & 0xffu);
    data[offset + 2u] = static_cast<char>((value >> 16u) & 0xffu);
    data[offset + 3u] = static_cast<char>((value >> 24u) & 0xffu);
}

void append_u16(std::string& output, std::uint16_t value)
{
    output.push_back(static_cast<char>(value & 0xffu));
    output.push_back(static_cast<char>((value >> 8u) & 0xffu));
}

void append_u32(std::string& output, std::uint32_t value)
{
    output.push_back(static_cast<char>(value & 0xffu));
    output.push_back(static_cast<char>((value >> 8u) & 0xffu));
    output.push_back(static_cast<char>((value >> 16u) & 0xffu));
    output.push_back(static_cast<char>((value >> 24u) & 0xffu));
}

std::uint16_t checked_zip_u16(std::size_t value, std::string_view field)
{
    if (value > 0xffffu) {
        throw std::runtime_error(std::string("test ZIP field exceeds uint16: ")
            + std::string(field));
    }
    return static_cast<std::uint16_t>(value);
}

std::uint32_t checked_zip_u32(std::size_t value, std::string_view field)
{
    if (value > 0xffffffffu) {
        throw std::runtime_error(std::string("test ZIP field exceeds uint32: ")
            + std::string(field));
    }
    return static_cast<std::uint32_t>(value);
}

const std::array<std::uint32_t, 256>& test_crc32_table()
{
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values{};
        constexpr std::uint32_t polynomial = 0xedb88320u;
        for (std::uint32_t i = 0; i < values.size(); ++i) {
            std::uint32_t crc = i;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 1u) != 0u ? (crc >> 1u) ^ polynomial : crc >> 1u;
            }
            values[i] = crc;
        }
        return values;
    }();
    return table;
}

std::uint32_t test_crc32(std::string_view data)
{
    std::uint32_t crc = 0xffffffffu;
    const auto& table = test_crc32_table();
    for (unsigned char byte : data) {
        crc = (crc >> 8u) ^ table[(crc ^ byte) & 0xffu];
    }
    return crc ^ 0xffffffffu;
}

void write_stored_zip_entries(
    const std::filesystem::path& path, const std::map<std::string, std::string>& entries)
{
    struct CentralRecord {
        std::string name;
        std::uint32_t crc = 0;
        std::uint32_t size = 0;
        std::uint32_t local_header_offset = 0;
    };

    std::string archive;
    std::vector<CentralRecord> central_records;
    central_records.reserve(entries.size());

    for (const auto& [name, payload] : entries) {
        const std::uint16_t name_size = checked_zip_u16(name.size(), "entry name");
        const std::uint32_t payload_size = checked_zip_u32(payload.size(), "entry payload");
        const std::uint32_t local_header_offset =
            checked_zip_u32(archive.size(), "local header offset");
        const std::uint32_t crc = test_crc32(payload);

        append_u32(archive, 0x04034b50u);
        append_u16(archive, 20);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u32(archive, crc);
        append_u32(archive, payload_size);
        append_u32(archive, payload_size);
        append_u16(archive, name_size);
        append_u16(archive, 0);
        archive.append(name);
        archive.append(payload);

        central_records.push_back({name, crc, payload_size, local_header_offset});
    }

    const std::uint32_t central_directory_offset =
        checked_zip_u32(archive.size(), "central directory offset");
    for (const CentralRecord& record : central_records) {
        const std::uint16_t name_size = checked_zip_u16(record.name.size(), "entry name");
        append_u32(archive, 0x02014b50u);
        append_u16(archive, 20);
        append_u16(archive, 20);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u32(archive, record.crc);
        append_u32(archive, record.size);
        append_u32(archive, record.size);
        append_u16(archive, name_size);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u16(archive, 0);
        append_u32(archive, 0);
        append_u32(archive, record.local_header_offset);
        archive.append(record.name);
    }

    const std::uint32_t central_directory_size =
        checked_zip_u32(archive.size() - central_directory_offset, "central directory size");
    const std::uint16_t entry_count = checked_zip_u16(entries.size(), "entry count");
    append_u32(archive, 0x06054b50u);
    append_u16(archive, 0);
    append_u16(archive, 0);
    append_u16(archive, entry_count);
    append_u16(archive, entry_count);
    append_u32(archive, central_directory_size);
    append_u32(archive, central_directory_offset);
    append_u16(archive, 0);

    write_binary_file(path, archive);
}

void rewrite_package_entry_as_stored(
    const std::filesystem::path& path, std::string_view entry_name, std::string replacement)
{
    std::map<std::string, std::string> entries = fastxlsx::test::read_zip_entries(path);
    auto entry = entries.find(std::string(entry_name));
    if (entry == entries.end()) {
        throw std::runtime_error("test package entry to rewrite was not found");
    }
    entry->second = std::move(replacement);
    write_stored_zip_entries(path, entries);
}

void corrupt_zip_entry_payload(std::string& data, std::string_view entry_name)
{
    const ZipEntryPayloadLocation location =
        find_zip_entry_payload_location(data, entry_name);
    const std::size_t corrupt_offset =
        location.data_offset + static_cast<std::size_t>(location.compressed_size / 2u);
    data[corrupt_offset] = data[corrupt_offset] == 'X' ? 'Y' : 'X';
}

void corrupt_zip_entry_crc_metadata(std::string& data, std::string_view entry_name)
{
    const ZipEntryPayloadLocation location =
        find_zip_entry_payload_location(data, entry_name);
    const std::size_t central_crc_offset = location.central_offset + 16u;
    const std::uint32_t crc = fastxlsx::test::read_u32(data, central_crc_offset);
    write_u32(data, central_crc_offset, crc ^ 0xffffffffu);
}

// Writes a small two-sheet workbook through the public streaming writer. The
// first sheet carries placeholder rows to be replaced; the second sheet is left
// untouched so preservation can be checked.
std::filesystem::path write_two_sheet_source(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

// Writes a source workbook with document properties so patch tests can verify
// that WorkbookEditor preserves docProps bytes through save_as().
std::filesystem::path write_two_sheet_source_with_document_properties(std::string_view name)
{
    const std::filesystem::path path = artifact(name);

    fastxlsx::WorkbookWriterOptions options;
    options.document_properties.creator = "WorkbookEditor Tests";
    options.document_properties.last_modified_by = "WorkbookEditor Tests";
    options.document_properties.title = "WorkbookEditor preservation";
    options.document_properties.subject = "FastXLSX";
    options.document_properties.description = "WorkbookEditor docProps preservation source";
    options.document_properties.keywords = "FastXLSX,WorkbookEditor,Patch";
    options.document_properties.category = "tests";
    options.document_properties.application = "FastXLSX";
    options.document_properties.app_version = "1.0";

    fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(path, options);
    {
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("placeholder-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("placeholder-a2")});
    }
    {
        fastxlsx::WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({fastxlsx::CellView::text("keep-me"),
            fastxlsx::CellView::number(99.0)});
    }
    writer.close();

    return path;
}

void test_replaces_sheet_data_and_preserves_untouched_parts()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-replace-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string untouched_sheet_before = source_entries.at("xl/worksheets/sheet2.xml");
    const std::string content_types_before = source_entries.at("[Content_Types].xml");
    const std::string package_rels_before = source_entries.at("_rels/.rels");
    const std::string workbook_rels_before = source_entries.at("xl/_rels/workbook.xml.rels");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(7.0)}});
    editor.replace_sheet_data("Data",
        {
            {fastxlsx::CellValue::number(42.25), fastxlsx::CellValue::text("fresh")},
            {fastxlsx::CellValue::formula("SUM(A1:A1)")},
        });
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_not_contains(worksheet_xml, R"(<v>7</v>)",
        "second replacement should overwrite earlier queued data");
    check_contains(worksheet_xml, R"(<c r="A1"><v>42.25</v></c>)",
        "replaced sheet should carry new numeric cell");
    check_contains(worksheet_xml, R"(<c r="B1" t="inlineStr"><is><t>fresh</t></is></c>)",
        "replaced sheet should carry new inline text cell");
    check_contains(worksheet_xml, "<f>SUM(A1:A1)</f>",
        "replaced sheet should carry new formula cell");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "replaced sheet should drop old placeholder data");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "replaced sheet should drop old placeholder data");

    check(output_entries.at("xl/worksheets/sheet2.xml") == untouched_sheet_before,
        "untouched worksheet bytes should be preserved");
    check(output_entries.at("[Content_Types].xml") == content_types_before,
        "content types bytes should be preserved");
    check(output_entries.at("_rels/.rels") == package_rels_before,
        "package relationships bytes should be preserved");
    check(output_entries.at("xl/_rels/workbook.xml.rels") == workbook_rels_before,
        "workbook relationships bytes should be preserved");
}

void test_replace_sheet_data_preserves_surrounding_worksheet_metadata()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-metadata-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-metadata-output.xlsx");

    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.set_column_width(1, 2, 18.75);
        data.freeze_panes(1, 1);
        data.set_auto_filter({1, 1, 3, 2});
        data.merge_cells({3, 1, 3, 2});
        data.append_row({fastxlsx::CellView::text("old-a1"),
            fastxlsx::CellView::number(1.0)});
        data.append_row({fastxlsx::CellView::text("old-a2"),
            fastxlsx::CellView::number(2.0)});
        data.append_row({fastxlsx::CellView::text("old-a3"),
            fastxlsx::CellView::number(3.0)});
        writer.close();
    }

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::number(9.0), fastxlsx::CellValue::text("new-cell")}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");

    check_contains(worksheet_xml, R"(<c r="A1"><v>9</v></c>)",
        "metadata-preserving replacement should write the new numeric cell");
    check_contains(worksheet_xml, R"(<c r="B1" t="inlineStr"><is><t>new-cell</t></is></c>)",
        "metadata-preserving replacement should write the new inline string cell");
    check_not_contains(worksheet_xml, "old-a1",
        "metadata-preserving replacement should remove old sheetData cells");
    check_not_contains(worksheet_xml, "old-a2",
        "metadata-preserving replacement should remove old sheetData cells");
    check_not_contains(worksheet_xml, "old-a3",
        "metadata-preserving replacement should remove old sheetData cells");

    check_contains(worksheet_xml, R"(<dimension ref="A1:B3"/>)",
        "replace_sheet_data should preserve the source dimension metadata as-is");
    check_contains(worksheet_xml,
        R"(<col min="1" max="2" width="18.75" customWidth="1"/>)",
        "replace_sheet_data should preserve source column metadata");
    check_contains(worksheet_xml,
        R"(<pane xSplit="1" ySplit="1" topLeftCell="B2" activePane="bottomRight" state="frozen"/>)",
        "replace_sheet_data should preserve source frozen-pane metadata");
    check_contains(worksheet_xml, R"(<autoFilter ref="A1:B3"/>)",
        "replace_sheet_data should preserve source autoFilter metadata");
    check_contains(worksheet_xml,
        R"(<mergeCells count="1"><mergeCell ref="A3:B3"/></mergeCells>)",
        "replace_sheet_data should preserve source mergeCells metadata");
    check_contains(worksheet_xml, R"(</sheetData><autoFilter ref="A1:B3"/>)",
        "replace_sheet_data should replace only sheetData and keep suffix metadata after it");
}

void test_replace_sheet_data_writes_caller_style_ids_as_is()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-styled-replacement-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-styled-replacement-output.xlsx");

    fastxlsx::StyleId number_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        number_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("old styled").with_style(number_style),
            fastxlsx::CellView::text("old plain")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string styles_before = source_entries.at("xl/styles.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::number(9.5).with_style(number_style),
            fastxlsx::CellValue::text("explicit default").with_style(fastxlsx::StyleId {})}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == styles_before,
        "styled WorkbookEditor replacement should preserve source styles.xml bytes");

    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1" s="1"><v>9.5</v></c>)",
        "WorkbookEditor should write caller-supplied non-default StyleId as-is");
    check_contains(worksheet_xml, R"(<c r="B1" t="inlineStr"><is><t>explicit default</t></is></c>)",
        "WorkbookEditor should omit s=\"0\" for explicit default StyleId");
    check_not_contains(worksheet_xml, "old styled",
        "styled replacement should remove old sheetData text");
    check_not_contains(worksheet_xml, R"(r="B1" s="0")",
        "explicit default StyleId should not serialize a default style attribute");
}

void test_replace_sheet_data_distinguishes_blank_cells_from_missing_cells()
{
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-blank-replacement-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-blank-replacement-output.xlsx");

    fastxlsx::StyleId number_style;
    {
        fastxlsx::WorkbookWriter writer = fastxlsx::WorkbookWriter::create(source);
        number_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("old-a"),
            fastxlsx::CellView::text("old-b"), fastxlsx::CellView::text("old-c")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string styles_before = source_entries.at("xl/styles.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::blank().with_style(number_style),
             fastxlsx::CellValue::text("after blank")},
            {},
            {fastxlsx::CellValue::blank(), fastxlsx::CellValue::number(42.0)}});

    check(editor.pending_replacement_cell_count() == 4,
        "blank replacement should count explicit blank cells but not empty rows");
    check(editor.has_pending_replacement("Data"),
        "blank replacement should still register a pending sheetData payload");

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("xl/styles.xml") == styles_before,
        "blank replacement should preserve source styles.xml bytes");

    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<row r="1"><c r="A1" s="1"/><c r="B1" t="inlineStr"><is><t>after blank</t></is></c></row>)",
        "styled blank replacement should emit an empty styled cell before text");
    check_contains(worksheet_xml,
        R"(<row r="3"><c r="A3"/><c r="B3"><v>42</v></c></row>)",
        "default-style blank replacement should emit an empty cell and preserve row gaps");
    check_not_contains(worksheet_xml, R"(<row r="2")",
        "empty replacement rows should remain missing rows, not explicit blank rows");
    check_not_contains(worksheet_xml, "old-a",
        "blank replacement should remove old source sheetData text");
    check_not_contains(worksheet_xml, R"(s="0")",
        "blank replacement should omit explicit default style attributes");
}

void test_worksheet_names_and_has_worksheet()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-names-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    const std::vector<std::string> names = editor.worksheet_names();
    check(names.size() == 2, "worksheet_names should list both sheets");
    check(names.size() == 2 && names[0] == "Data",
        "worksheet_names should list sheets in catalog order");
    check(names.size() == 2 && names[1] == "Untouched",
        "worksheet_names should list the second sheet name");

    check(editor.has_worksheet("Data"), "has_worksheet should find an existing sheet");
    check(editor.has_worksheet("Untouched"), "has_worksheet should find the second sheet");
    check(!editor.has_worksheet("Missing"),
        "has_worksheet should reject an absent sheet name");

    const std::vector<std::string> source_names = editor.source_worksheet_names();
    check(source_names == names,
        "source_worksheet_names should match worksheet_names before queued edits");
    check(editor.has_source_worksheet("Data"),
        "has_source_worksheet should find an existing source sheet");
    check(editor.has_source_worksheet("Untouched"),
        "has_source_worksheet should find the second source sheet");
    check(!editor.has_source_worksheet("Missing"),
        "has_source_worksheet should reject an absent source sheet name");

    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
        editor.worksheet_catalog();
    check(catalog.size() == 2,
        "worksheet_catalog should list source-to-planned sheets");
    if (catalog.size() == 2) {
        check(catalog[0].source_name == "Data",
            "worksheet_catalog should preserve source sheet order");
        check(catalog[0].planned_name == "Data",
            "worksheet_catalog should match planned name before edits");
        check(!catalog[0].renamed,
            "worksheet_catalog should not mark unchanged source sheet as renamed");
        check(catalog[1].source_name == "Untouched",
            "worksheet_catalog should list the second source sheet");
        check(catalog[1].planned_name == "Untouched",
            "worksheet_catalog should list the second planned sheet");
        check(!catalog[1].renamed,
            "worksheet_catalog should not mark the second unchanged sheet as renamed");
    }
}

void test_moved_from_workbook_editor_operations_throw()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-moved-from-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-moved-from-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(moved.has_worksheet("Data"),
        "moved-to editor should keep the opened workbook state");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "moved-from worksheet_names should throw FastXlsxError");
    check(threw_fastxlsx_error([&] { (void)editor.has_worksheet("Data"); }),
        "moved-from has_worksheet should throw FastXlsxError");
    check(threw_fastxlsx_error([&] { (void)editor.source_worksheet_names(); }),
        "moved-from source_worksheet_names should throw FastXlsxError");
    check(threw_fastxlsx_error([&] { (void)editor.has_source_worksheet("Data"); }),
        "moved-from has_source_worksheet should throw FastXlsxError");
    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(1.0)}});
    }), "moved-from replace_sheet_data should throw FastXlsxError");
    check(!editor.has_pending_changes(),
        "moved-from has_pending_changes should return false");
    check(editor.pending_change_count() == 0,
        "moved-from pending_change_count should return zero");
    check(editor.pending_replacement_cell_count() == 0,
        "moved-from pending_replacement_cell_count should return zero");
    check(editor.pending_replacement_worksheet_names().empty(),
        "moved-from pending replacement names should be empty");
    check(!editor.has_pending_replacement("Data"),
        "moved-from pending replacement lookup should return false");
    check(editor.estimated_pending_replacement_memory_usage() == 0,
        "moved-from estimated pending replacement memory should return zero");
    check(editor.pending_worksheet_edits().empty(),
        "moved-from pending worksheet edit summaries should be empty");
    check(editor.worksheet_catalog().empty(),
        "moved-from worksheet_catalog should return an empty view");
    check(!editor.last_edit_error().has_value(),
        "moved-from last_edit_error should return no diagnostic");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Moved"); }),
        "moved-from rename_sheet should throw FastXlsxError");
    check(threw_fastxlsx_error([&] { editor.save_as(output); }),
        "moved-from save_as should throw FastXlsxError");
}

void test_moved_to_workbook_editor_preserves_pending_public_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-moved-to-state-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-moved-to-state-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("moved-state")}});
    editor.rename_sheet("Data", "MovedReport");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Untouched", "Bad/Name"); }),
        "failed rename before move should record a public diagnostic");
    const std::optional<std::string> last_error_before_move = editor.last_edit_error();
    check(last_error_before_move.has_value(),
        "failed rename before move should set last_edit_error");

    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(moved.has_worksheet("MovedReport"),
        "moved-to editor should keep the planned renamed worksheet");
    check(!moved.has_worksheet("Data"),
        "moved-to editor should not expose the old planned worksheet name");
    check(moved.has_source_worksheet("Data"),
        "moved-to editor should keep the source workbook catalog");
    check(moved.has_pending_changes(),
        "moved-to editor should keep pending public edits");
    check(moved.pending_change_count() == 2,
        "moved-to editor should keep the successful public edit count");
    check(moved.pending_replacement_cell_count() == 1,
        "moved-to editor should keep pending replacement cell diagnostics");
    check(moved.estimated_pending_replacement_memory_usage() > 0,
        "moved-to editor should keep pending replacement memory diagnostics");
    {
        const std::vector<std::string> pending_names =
            moved.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "MovedReport",
            "moved-to editor should keep pending replacement planned names");
    }
    check(moved.has_pending_replacement("MovedReport"),
        "moved-to editor should keep pending replacement lookup by planned name");
    check(moved.last_edit_error() == last_error_before_move,
        "moved-to editor should keep the last failed public edit diagnostic");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            moved.pending_worksheet_edits();
        check(summaries.size() == 1,
            "moved-to editor should keep pending worksheet summaries");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data",
                "moved-to summary should keep the source sheet name");
            check(summaries[0].planned_name == "MovedReport",
                "moved-to summary should keep the planned sheet name");
            check(summaries[0].renamed,
                "moved-to summary should keep rename diagnostics");
            check(summaries[0].sheet_data_replaced,
                "moved-to summary should keep replacement diagnostics");
        }
    }

    moved.save_as(output);

    check(moved.last_edit_error() == last_error_before_move,
        "save_as on moved-to editor should not update last_edit_error");
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="MovedReport")",
        "moved-to editor should save the queued rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "moved-state",
        "moved-to editor should save the queued replacement data");
}

void test_clean_moved_to_workbook_editor_preserves_noop_public_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-clean-moved-to-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-clean-moved-to-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(!editor.has_pending_changes(),
        "clean editor should start with no pending changes before move construction");
    check(!editor.last_edit_error().has_value(),
        "clean editor should start with no last_edit_error before move construction");

    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(moved.has_worksheet("Data"),
        "clean moved-to editor should keep the current planned catalog");
    check(moved.has_source_worksheet("Data"),
        "clean moved-to editor should keep the source workbook catalog");
    check(!moved.has_pending_changes(),
        "clean moved-to editor should keep no pending changes");
    check(moved.pending_change_count() == 0,
        "clean moved-to editor should keep zero pending public edits");
    check(moved.pending_replacement_cell_count() == 0,
        "clean moved-to editor should keep zero replacement cells");
    check(moved.pending_replacement_worksheet_names().empty(),
        "clean moved-to editor should keep no pending replacement names");
    check(!moved.has_pending_replacement("Data"),
        "clean moved-to editor should keep no pending replacement lookup");
    check(moved.estimated_pending_replacement_memory_usage() == 0,
        "clean moved-to editor should keep zero replacement memory");
    check(moved.pending_worksheet_edits().empty(),
        "clean moved-to editor should keep no pending worksheet edit summaries");
    check(!moved.last_edit_error().has_value(),
        "clean moved-to editor should keep no last_edit_error");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "clean moved-from editor should throw for worksheet_names");
    check(!editor.has_pending_changes(),
        "clean moved-from editor should report no pending changes");
    check(!editor.last_edit_error().has_value(),
        "clean moved-from editor should report no last_edit_error");

    moved.save_as(output);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "clean moved-to no-op save_as should copy the source package");
}

void test_moved_to_workbook_editor_preserves_replacement_options()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-moved-to-options-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-moved-to-options-output.xlsx");

    fastxlsx::WorkbookEditorOptions options;
    options.max_replacement_cells = 1;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source, options);
    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(threw_fastxlsx_error([&] {
        moved.replace_sheet_data("Data",
            {{fastxlsx::CellValue::text("too-many-a"),
                fastxlsx::CellValue::text("too-many-b")}});
    }), "moved-to editor should keep max_replacement_cells guardrail");
    check(!moved.has_pending_changes(),
        "failed guarded replacement after move should not queue public edits");
    check(moved.pending_replacement_cell_count() == 0,
        "failed guarded replacement after move should not add replacement cells");
    check(moved.last_edit_error().has_value(),
        "failed guarded replacement after move should set last_edit_error");

    moved.replace_sheet_data("Data", {{fastxlsx::CellValue::text("guarded-state")}});
    check(moved.has_pending_changes(),
        "valid guarded replacement after move should queue public edits");
    check(moved.pending_replacement_cell_count() == 1,
        "valid guarded replacement after move should record one replacement cell");
    check(!moved.last_edit_error().has_value(),
        "valid guarded replacement after move should clear last_edit_error");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "options moved-from editor should throw for worksheet_names");
    check(!editor.has_pending_changes(),
        "options moved-from editor should report no pending changes");

    moved.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "guarded-state",
        "moved-to editor with preserved options should save the valid replacement");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "too-many-a",
        "moved-to editor should not leak the rejected oversized replacement");
}

void test_moved_to_workbook_editor_preserves_replacement_memory_budget()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-moved-to-memory-budget-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-moved-to-memory-budget-output.xlsx");

    fastxlsx::WorkbookEditorOptions options;
    options.replacement_memory_budget_bytes = 1;

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source, options);
    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(threw_fastxlsx_error([&] {
        moved.replace_sheet_data("Data", {{fastxlsx::CellValue::text("too large")}});
    }), "moved-to editor should keep replacement_memory_budget_bytes guardrail");
    check(!moved.has_pending_changes(),
        "moved-to memory-budget failure should not queue public edits");
    check(moved.pending_replacement_cell_count() == 0,
        "moved-to memory-budget failure should not add replacement cells");
    check(moved.estimated_pending_replacement_memory_usage() == 0,
        "moved-to memory-budget failure should not record replacement memory");
    check(moved.last_edit_error().has_value(),
        "moved-to memory-budget failure should set last_edit_error");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "memory-budget moved-from editor should throw for worksheet_names");
    check(!editor.has_pending_changes(),
        "memory-budget moved-from editor should report no pending changes");

    moved.save_as(output);

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "moved-to memory-budget no-op save_as should copy the source package");
}

void test_move_assigned_workbook_editor_replaces_target_with_pending_public_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-source.xlsx");
    const std::filesystem::path discarded_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-discarded-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("assigned-state")}});
    editor.rename_sheet("Data", "AssignedReport");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Untouched", "Bad/Name"); }),
        "failed rename before move assignment should record a public diagnostic");
    const std::optional<std::string> last_error_before_assignment = editor.last_edit_error();
    check(last_error_before_assignment.has_value(),
        "failed rename before move assignment should set last_edit_error");

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(discarded_source);
    target.replace_sheet_data("Data", {{fastxlsx::CellValue::text("discarded-state")}});
    target.rename_sheet("Data", "DiscardedReport");

    target = std::move(editor);

    check(target.has_worksheet("AssignedReport"),
        "move-assigned editor should keep the source planned renamed worksheet");
    check(!target.has_worksheet("DiscardedReport"),
        "move-assigned editor should discard the previous target planned state");
    check(target.has_source_worksheet("Data"),
        "move-assigned editor should keep the assigned source workbook catalog");
    check(target.pending_change_count() == 2,
        "move-assigned editor should keep the assigned successful edit count");
    {
        const std::vector<std::string> pending_names =
            target.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "AssignedReport",
            "move-assigned editor should keep assigned pending replacement names");
    }
    check(target.has_pending_replacement("AssignedReport"),
        "move-assigned editor should keep assigned pending replacement lookup");
    check(target.last_edit_error() == last_error_before_assignment,
        "move-assigned editor should keep assigned last failed public edit diagnostic");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "move-assigned-from editor should throw for worksheet_names");
    check(!editor.has_pending_changes(),
        "move-assigned-from editor should report no pending changes");
    check(!editor.last_edit_error().has_value(),
        "move-assigned-from editor should report no last edit error");

    target.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="AssignedReport")",
        "move-assigned editor should save the assigned queued rename");
    check_not_contains(output_entries.at("xl/workbook.xml"), "DiscardedReport",
        "move-assigned output should not keep the discarded target rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "assigned-state",
        "move-assigned editor should save the assigned replacement data");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "discarded-state",
        "move-assigned output should not keep discarded target replacement data");
}

void test_move_assigned_clean_workbook_editor_clears_dirty_target_state()
{
    const std::filesystem::path clean_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-clean-source.xlsx");
    const std::filesystem::path dirty_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-dirty-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-clean-output.xlsx");

    fastxlsx::WorkbookEditor clean = fastxlsx::WorkbookEditor::open(clean_source);
    check(!clean.has_pending_changes(),
        "clean move-assignment source should start with no pending changes");

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(dirty_source);
    target.replace_sheet_data("Data", {{fastxlsx::CellValue::text("target-dirty-state")}});
    target.rename_sheet("Data", "DirtyTarget");
    check(threw_fastxlsx_error([&] { target.rename_sheet("Untouched", "Bad/Name"); }),
        "dirty target should record a failed public edit diagnostic before assignment");
    check(target.has_pending_changes(),
        "dirty target should have pending state before clean-source assignment");
    check(target.last_edit_error().has_value(),
        "dirty target should have last_edit_error before clean-source assignment");

    target = std::move(clean);

    check(target.has_worksheet("Data"),
        "clean-source move assignment should expose the assigned source catalog");
    check(!target.has_worksheet("DirtyTarget"),
        "clean-source move assignment should discard the old target planned name");
    check(!target.has_pending_changes(),
        "clean-source move assignment should clear old target pending changes");
    check(target.pending_change_count() == 0,
        "clean-source move assignment should clear old target pending count");
    check(target.pending_replacement_cell_count() == 0,
        "clean-source move assignment should clear old target replacement cell count");
    check(target.pending_replacement_worksheet_names().empty(),
        "clean-source move assignment should clear old target replacement names");
    check(!target.has_pending_replacement("DirtyTarget"),
        "clean-source move assignment should clear old target replacement lookup");
    check(target.estimated_pending_replacement_memory_usage() == 0,
        "clean-source move assignment should clear old target replacement memory");
    check(target.pending_worksheet_edits().empty(),
        "clean-source move assignment should clear old target edit summaries");
    check(!target.last_edit_error().has_value(),
        "clean-source move assignment should clear old target last_edit_error");

    check(threw_fastxlsx_error([&] { (void)clean.worksheet_names(); }),
        "clean move-assigned-from editor should throw for worksheet_names");
    check(!clean.has_pending_changes(),
        "clean move-assigned-from editor should report no pending changes");
    check(!clean.last_edit_error().has_value(),
        "clean move-assigned-from editor should report no last edit error");

    target.save_as(output);

    const auto source_entries = fastxlsx::test::read_zip_entries(clean_source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "clean-source move-assigned no-op save_as should copy the assigned source package");
    check_not_contains(output_entries.at("xl/workbook.xml"), "DirtyTarget",
        "clean-source move-assigned output should not keep old target rename");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "target-dirty-state",
        "clean-source move-assigned output should not keep old target replacement data");
}

void test_move_assignment_revives_moved_from_target_workbook_editor()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-revive-source.xlsx");
    const std::filesystem::path target_seed =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-revive-target-seed.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-revive-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("revived-state")}});
    editor.rename_sheet("Data", "RevivedReport");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Untouched", "Bad/Name"); }),
        "source editor should record a failed public edit before revive assignment");
    const std::optional<std::string> last_error_before_assignment = editor.last_edit_error();
    check(last_error_before_assignment.has_value(),
        "source editor should have last_edit_error before revive assignment");

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_seed);
    fastxlsx::WorkbookEditor holder = std::move(target);
    check(holder.has_worksheet("Data"),
        "holder should keep the target seed workbook after moving from target");
    check(threw_fastxlsx_error([&] { (void)target.worksheet_names(); }),
        "target should be moved-from before revive assignment");
    check(!target.has_pending_changes(),
        "moved-from target should report no pending changes before revive assignment");

    target = std::move(editor);

    check(target.has_worksheet("RevivedReport"),
        "move assignment should revive moved-from target with assigned planned catalog");
    check(target.has_source_worksheet("Data"),
        "revived target should expose the assigned source catalog");
    check(target.pending_change_count() == 2,
        "revived target should keep assigned pending public edit count");
    check(target.has_pending_replacement("RevivedReport"),
        "revived target should keep assigned pending replacement lookup");
    check(target.last_edit_error() == last_error_before_assignment,
        "revived target should keep assigned last failed public edit diagnostic");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "move-assigned-from source editor should throw for worksheet_names");
    check(!editor.has_pending_changes(),
        "move-assigned-from source editor should report no pending changes");
    check(!editor.last_edit_error().has_value(),
        "move-assigned-from source editor should report no last edit error");

    target.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RevivedReport")",
        "revived target should save the assigned queued rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "revived-state",
        "revived target should save the assigned replacement data");
}

void test_move_assignment_replaces_target_replacement_options()
{
    const std::filesystem::path strict_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-options-source.xlsx");
    const std::filesystem::path permissive_target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-options-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-options-output.xlsx");

    fastxlsx::WorkbookEditorOptions strict_options;
    strict_options.max_replacement_cells = 1;
    fastxlsx::WorkbookEditor editor =
        fastxlsx::WorkbookEditor::open(strict_source, strict_options);

    fastxlsx::WorkbookEditorOptions permissive_options;
    permissive_options.max_replacement_cells = 10;
    fastxlsx::WorkbookEditor target =
        fastxlsx::WorkbookEditor::open(permissive_target_source, permissive_options);

    target = std::move(editor);

    check(threw_fastxlsx_error([&] {
        target.replace_sheet_data("Data",
            {{fastxlsx::CellValue::text("assigned-too-many-a"),
                fastxlsx::CellValue::text("assigned-too-many-b")}});
    }), "move-assigned editor should use assigned source replacement guardrails");
    check(!target.has_pending_changes(),
        "failed guarded replacement after assignment should not queue public edits");
    check(target.pending_replacement_cell_count() == 0,
        "failed guarded replacement after assignment should not add replacement cells");

    target.replace_sheet_data("Data", {{fastxlsx::CellValue::text("assigned-guarded-state")}});
    check(target.has_pending_changes(),
        "valid guarded replacement after assignment should queue public edits");
    check(target.pending_replacement_cell_count() == 1,
        "valid guarded replacement after assignment should record one replacement cell");
    check(!target.last_edit_error().has_value(),
        "valid guarded replacement after assignment should clear last_edit_error");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "options move-assigned-from editor should throw for worksheet_names");

    target.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "assigned-guarded-state",
        "move-assigned editor with source options should save the valid replacement");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "assigned-too-many-a",
        "move-assigned editor should not leak the rejected oversized replacement");
}

void test_move_assignment_replaces_target_replacement_memory_budget()
{
    const std::filesystem::path strict_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-memory-source.xlsx");
    const std::filesystem::path permissive_target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-memory-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-memory-output.xlsx");

    fastxlsx::WorkbookEditorOptions strict_options;
    strict_options.replacement_memory_budget_bytes = 1;
    fastxlsx::WorkbookEditor editor =
        fastxlsx::WorkbookEditor::open(strict_source, strict_options);

    fastxlsx::WorkbookEditorOptions permissive_options;
    permissive_options.replacement_memory_budget_bytes = 1024 * 1024;
    fastxlsx::WorkbookEditor target =
        fastxlsx::WorkbookEditor::open(permissive_target_source, permissive_options);

    target = std::move(editor);

    check(threw_fastxlsx_error([&] {
        target.replace_sheet_data("Data", {{fastxlsx::CellValue::text("assigned-too-large")}});
    }), "move-assigned editor should use assigned source memory-budget guardrail");
    check(!target.has_pending_changes(),
        "move-assigned memory-budget failure should not queue public edits");
    check(target.pending_replacement_cell_count() == 0,
        "move-assigned memory-budget failure should not add replacement cells");
    check(target.estimated_pending_replacement_memory_usage() == 0,
        "move-assigned memory-budget failure should not record replacement memory");
    check(target.last_edit_error().has_value(),
        "move-assigned memory-budget failure should set last_edit_error");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "memory-budget move-assigned-from editor should throw for worksheet_names");

    target.save_as(output);

    const auto source_entries = fastxlsx::test::read_zip_entries(strict_source);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "move-assigned memory-budget no-op save_as should copy the assigned source package");
}

void test_move_assignment_clears_target_replacement_options_when_source_is_default()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-default-options-source.xlsx");
    const std::filesystem::path strict_target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-default-options-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-default-options-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    fastxlsx::WorkbookEditorOptions strict_options;
    strict_options.max_replacement_cells = 1;
    strict_options.replacement_memory_budget_bytes = 1;
    fastxlsx::WorkbookEditor target =
        fastxlsx::WorkbookEditor::open(strict_target_source, strict_options);

    target = std::move(editor);

    target.replace_sheet_data("Data",
        {{fastxlsx::CellValue::text("default-options-a"),
            fastxlsx::CellValue::text("default-options-b")}});

    check(target.has_pending_changes(),
        "default-source move assignment should allow replacement after clearing target options");
    check(target.pending_replacement_cell_count() == 2,
        "default-source move assignment should not retain target max_replacement_cells");
    check(target.estimated_pending_replacement_memory_usage() > 1,
        "default-source move assignment should not retain target memory budget");
    check(!target.last_edit_error().has_value(),
        "valid replacement after default-source move assignment should keep no last_edit_error");

    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "default-options move-assigned-from editor should throw for worksheet_names");

    target.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "default-options-a",
        "default-source move-assigned output should write the first replacement cell");
    check_contains(worksheet_xml, "default-options-b",
        "default-source move-assigned output should write the second replacement cell");
}

void test_move_assignment_from_moved_from_source_clears_dirty_target_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-moved-from-source.xlsx");
    const std::filesystem::path dirty_target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-move-assign-moved-from-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-move-assign-moved-from-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorkbookEditor holder = std::move(editor);
    check(holder.has_worksheet("Data"),
        "holder should keep the original source workbook after moving from editor");
    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "source editor should be moved-from before assignment");

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(dirty_target_source);
    target.replace_sheet_data("Data", {{fastxlsx::CellValue::text("discarded-dirty-state")}});
    target.rename_sheet("Data", "DirtyBeforeMovedFromAssign");
    check(threw_fastxlsx_error([&] { target.rename_sheet("Untouched", "Bad/Name"); }),
        "dirty target should record a failed public edit before moved-from assignment");
    check(target.has_pending_changes(),
        "dirty target should have pending changes before moved-from assignment");
    check(target.last_edit_error().has_value(),
        "dirty target should have a last edit diagnostic before moved-from assignment");

    target = std::move(editor);

    check(threw_fastxlsx_error([&] { (void)target.worksheet_names(); }),
        "assignment from a moved-from source should leave the target moved-from");
    check(!target.has_pending_changes(),
        "assignment from a moved-from source should clear target pending changes");
    check(target.pending_change_count() == 0,
        "assignment from a moved-from source should clear target pending count");
    check(target.pending_replacement_cell_count() == 0,
        "assignment from a moved-from source should clear replacement cell diagnostics");
    check(target.pending_replacement_worksheet_names().empty(),
        "assignment from a moved-from source should clear pending replacement names");
    check(target.pending_materialized_worksheet_names().empty(),
        "assignment from a moved-from source should clear dirty materialized names");
    check(!target.has_pending_replacement("DirtyBeforeMovedFromAssign"),
        "assignment from a moved-from source should clear target replacement lookup");
    check(target.estimated_pending_replacement_memory_usage() == 0,
        "assignment from a moved-from source should clear replacement memory diagnostics");
    check(target.pending_worksheet_edits().empty(),
        "assignment from a moved-from source should clear target edit summaries");
    check(target.worksheet_catalog().empty(),
        "assignment from a moved-from source should expose an empty moved-from catalog");
    check(!target.last_edit_error().has_value(),
        "assignment from a moved-from source should clear target last_edit_error");
    check(threw_fastxlsx_error([&] { target.save_as(output); }),
        "assignment from a moved-from source should make save_as throw");
    check(holder.has_worksheet("Data"),
        "assigning from the moved-from source should not disturb the prior moved-to holder");
}

void test_internal_materialized_sessions_move_with_workbook_editor_impl()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-move-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-move-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-move-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 1,
        "test hook should insert one materialized session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "newly materialized source session should start clean");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 2, 2, fastxlsx::CellValue::text("dirty-materialized"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "test hook mutation should mark the materialized session dirty");
    editor.replace_sheet_data(
        "Untouched", {{fastxlsx::CellValue::text("queued-other-sheet-before-move")}});
    check(editor.pending_change_count() == 1,
        "queued cross-sheet public edit should coexist with a dirty materialized session");
    check(editor.pending_replacement_cell_count() == 1,
        "queued cross-sheet public edit should record one replacement cell before move");
    {
        const std::vector<std::string> dirty_names =
            fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_names(editor);
        check(dirty_names.size() == 1 && dirty_names[0] == "Data",
            "dirty materialized session names should expose the planned sheet name");
    }

    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "moved-from editor should expose no materialized sessions");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(moved) == 1,
        "move construction should transfer materialized sessions with Impl state");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(moved) == 1,
        "move construction should preserve materialized dirty state");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(moved, "Data"),
        "move construction should keep the materialized planned sheet name");
    check(moved.pending_change_count() == 1,
        "move construction should preserve queued cross-sheet public edits");
    check(moved.pending_replacement_cell_count() == 1,
        "move construction should preserve queued replacement diagnostics");
    {
        const std::vector<std::string> names = moved.pending_replacement_worksheet_names();
        check(names.size() == 1 && names[0] == "Untouched",
            "move construction should preserve queued replacement planned names");
    }

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        target, "Untouched", "Untouched");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        target, "Untouched", 1, 1, fastxlsx::CellValue::text("discarded-target-materialized"));
    target.replace_sheet_data(
        "Data", {{fastxlsx::CellValue::text("discarded-target-public-replacement")}});
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(target, "Untouched"),
        "target should start with its own materialized session before assignment");

    target = std::move(moved);

    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(moved) == 0,
        "move-assigned-from editor should expose no materialized sessions");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(target) == 1,
        "move assignment should replace target materialized sessions");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 1,
        "move assignment should preserve assigned materialized dirty state");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(target, "Data"),
        "move assignment should keep assigned materialized session");
    check(!fastxlsx::detail::testing_workbook_editor_has_materialized_session(target, "Untouched"),
        "move assignment should discard previous target materialized session");
    check(target.pending_change_count() == 1,
        "move assignment should preserve queued source public edits");
    check(target.pending_replacement_cell_count() == 1,
        "move assignment should discard target replacement diagnostics");
    {
        const std::vector<std::string> names = target.pending_replacement_worksheet_names();
        check(names.size() == 1 && names[0] == "Untouched",
            "move assignment should preserve only source queued replacement names");
    }

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(target);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 0,
        "flush after move assignment should clear the assigned dirty materialized state");
    check(target.pending_change_count() == 2,
        "flush after move assignment should queue a materialized projection beside public edits");

    target.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string untouched_xml = output_entries.at("xl/worksheets/sheet2.xml");
    check_contains(data_xml, "dirty-materialized",
        "move-assigned materialized session should flush and save its payload");
    check_contains(untouched_xml, "queued-other-sheet-before-move",
        "move-assigned editor should save the queued source public replacement");
    check_not_contains(data_xml, "discarded-target-public-replacement",
        "move assignment should not leak discarded target public replacement");
    check_not_contains(untouched_xml, "discarded-target-materialized",
        "move assignment should not leak discarded target materialized session");
}

void test_public_worksheet_editor_handles_invalidate_after_owner_move()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-move-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-move-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor old_handle = editor.worksheet("Data");
    old_handle.set_cell(1, 1, fastxlsx::CellValue::text("moved-handle-before"));

    fastxlsx::WorkbookEditor moved = std::move(editor);

    check(threw_fastxlsx_error([&] { (void)old_handle.has_pending_changes(); }),
        "WorksheetEditor handle borrowed before owner move construction should be invalid");
    check(threw_fastxlsx_error([&] {
        old_handle.set_cell(1, 1, fastxlsx::CellValue::text("stale-handle-write"));
    }), "invalidated WorksheetEditor handle should reject writes after owner move");

    fastxlsx::WorksheetEditor reacquired = moved.worksheet("Data");
    check(reacquired.has_pending_changes(),
        "reacquired handle should see the moved dirty materialized session");
    const fastxlsx::CellValue moved_value = reacquired.get_cell(1, 1);
    check(moved_value.kind() == fastxlsx::CellValueKind::Text &&
            moved_value.text_value() == "moved-handle-before",
        "reacquired handle should read the moved materialized cell state");
    reacquired.set_cell(2, 1, fastxlsx::CellValue::text("moved-handle-after"));

    moved.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "moved-handle-before",
        "moved-to editor should save state written before owner move");
    check_contains(worksheet_xml, "moved-handle-after",
        "moved-to editor should save state written through reacquired handle");
    check_not_contains(worksheet_xml, "stale-handle-write",
        "invalidated pre-move handle should not mutate moved-to state");
}

void test_public_worksheet_editor_handles_invalidate_after_move_assignment()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-assign-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-assign-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-assign-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor source_handle = editor.worksheet("Data");
    source_handle.set_cell(1, 1, fastxlsx::CellValue::text("assigned-source-before"));

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::WorksheetEditor target_handle = target.worksheet("Data");
    target_handle.set_cell(1, 1, fastxlsx::CellValue::text("discarded-target-before"));

    target = std::move(editor);

    check(threw_fastxlsx_error([&] { (void)source_handle.has_pending_changes(); }),
        "source WorksheetEditor handle should be invalid after move assignment");
    check(threw_fastxlsx_error([&] { (void)target_handle.has_pending_changes(); }),
        "overwritten target WorksheetEditor handle should be invalid after move assignment");
    check(threw_fastxlsx_error([&] {
        target_handle.set_cell(1, 1, fastxlsx::CellValue::text("stale-target-write"));
    }), "invalidated target handle should not attach to the assigned source session");

    fastxlsx::WorksheetEditor reacquired = target.worksheet("Data");
    check(reacquired.has_pending_changes(),
        "reacquired assigned handle should see the assigned dirty source session");
    const fastxlsx::CellValue assigned_value = reacquired.get_cell(1, 1);
    check(assigned_value.kind() == fastxlsx::CellValueKind::Text &&
            assigned_value.text_value() == "assigned-source-before",
        "reacquired assigned handle should read the source materialized state");
    reacquired.set_cell(2, 1, fastxlsx::CellValue::text("assigned-source-after"));

    target.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "assigned-source-before",
        "move-assigned editor should save source handle edits made before assignment");
    check_contains(worksheet_xml, "assigned-source-after",
        "move-assigned editor should save edits made through a reacquired handle");
    check_not_contains(worksheet_xml, "discarded-target-before",
        "move assignment should discard overwritten target materialized edits");
    check_not_contains(worksheet_xml, "stale-target-write",
        "invalidated target handle should not mutate the assigned source state");
}

void test_public_worksheet_editor_set_cell_auto_flushes_on_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-set-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-set-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check(sheet.name() == "Data",
        "public WorksheetEditor should expose the planned worksheet name");
    const std::optional<fastxlsx::CellValue> original = sheet.try_cell(1, 1);
    check(original.has_value() && original->kind() == fastxlsx::CellValueKind::Text &&
            original->text_value() == "placeholder-a1",
        "public WorksheetEditor should read a supported source cell");
    check(!sheet.try_cell(3, 3).has_value(),
        "public WorksheetEditor should report missing sparse cells as nullopt");

    sheet.set_cell(1, 1, fastxlsx::CellValue::text("public-random-edit"));
    check(editor.has_pending_changes(),
        "dirty public WorksheetEditor edits should make save_as pending");
    check(editor.pending_change_count() == 0,
        "dirty public WorksheetEditor edits should not queue a Patch handoff before save_as");

    editor.save_as(output);
    check(editor.pending_change_count() == 1,
        "save_as should auto-flush one dirty public WorksheetEditor session");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "public-random-edit",
        "public WorksheetEditor set_cell should persist through save_as");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "public WorksheetEditor save_as should refresh the sparse worksheet dimension");
}

void test_public_try_worksheet_missing_returns_empty_and_preserves_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-try-worksheet-missing-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Missing", {{fastxlsx::CellValue::number(1.0)}});
    }), "precondition failed replacement should record a public diagnostic");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "precondition failed replacement should leave last_edit_error populated");

    const std::optional<fastxlsx::WorksheetEditor> missing =
        editor.try_worksheet("Missing");
    check(!missing.has_value(),
        "try_worksheet should return empty for a missing planned worksheet");
    check(editor.last_edit_error() == prior_error,
        "try_worksheet missing-sheet lookup should not update last_edit_error");
    check(!editor.has_pending_changes(),
        "try_worksheet missing-sheet lookup should not dirty the editor");
    check(editor.pending_change_count() == 0,
        "try_worksheet missing-sheet lookup should not queue public edits");
}

void test_public_try_worksheet_existing_handle_reads_mutates_and_saves()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-try-worksheet-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-try-worksheet-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    std::optional<fastxlsx::WorksheetEditor> maybe_sheet = editor.try_worksheet("Data");
    check(maybe_sheet.has_value(),
        "try_worksheet should return a handle for an existing planned worksheet");
    if (!maybe_sheet.has_value()) {
        return;
    }

    fastxlsx::WorksheetEditor& sheet = *maybe_sheet;
    const fastxlsx::CellValue old_a1 = sheet.get_cell(1, 1);
    check(old_a1.kind() == fastxlsx::CellValueKind::Text &&
            old_a1.text_value() == "placeholder-a1",
        "get_cell should read an existing source-backed cell");
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("try-worksheet-updated"));
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "try-worksheet-updated",
        "try_worksheet handle mutation should persist through save_as");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "try_worksheet handle mutation should replace the old value");
}

void test_public_try_worksheet_reuses_options_and_blocks_replacement_mix()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-try-worksheet-options-source.xlsx");

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        std::optional<fastxlsx::WorksheetEditor> first = editor.try_worksheet("Data");
        check(first.has_value(),
            "try_worksheet should materialize the first matching worksheet session");

        fastxlsx::WorksheetEditorOptions mismatched_options;
        mismatched_options.max_cells = 100;
        check(threw_fastxlsx_error([&] {
            (void)editor.try_worksheet("Data", mismatched_options);
        }), "try_worksheet should reject option mismatch for an existing session");
        check(!editor.last_edit_error().has_value(),
            "try_worksheet option mismatch should not update last_edit_error");
    }

    {
        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("queued")}});
        check(threw_fastxlsx_error([&] {
            (void)editor.try_worksheet("Data");
        }), "try_worksheet should reject a worksheet with queued sheetData replacement");
        check(!editor.last_edit_error().has_value(),
            "try_worksheet replacement-mix rejection should not update last_edit_error");
        check(editor.pending_change_count() == 1,
            "try_worksheet replacement-mix rejection should preserve queued edits");
    }
}

void test_public_worksheet_editor_has_pending_changes_tracks_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-dirty-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-dirty-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-public-worksheet-dirty-second.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check(!sheet.has_pending_changes(),
        "freshly materialized public WorksheetEditor should start clean");
    check(!editor.has_pending_changes(),
        "clean materialized WorksheetEditor should not make save_as pending");

    (void)sheet.try_cell(1, 1);
    (void)sheet.get_cell(1, 1);
    (void)sheet.cell_count();
    (void)sheet.estimated_memory_usage();
    (void)sheet.sparse_cells();
    (void)sheet.sparse_cells(fastxlsx::CellRange {1, 1, 2, 2});
    check(!sheet.has_pending_changes(),
        "read-only WorksheetEditor APIs should not dirty the materialized session");
    check(!editor.last_edit_error().has_value(),
        "read-only WorksheetEditor APIs should not update last_edit_error");

    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("invalid-reference"));
    }), "invalid A1 mutation should fail before dirtying the worksheet");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "failed WorksheetEditor mutation should set last_edit_error");

    check(!sheet.has_pending_changes(),
        "failed WorksheetEditor mutation should preserve clean dirty state");
    (void)sheet.has_pending_changes();
    (void)sheet.try_cell(1, 1);
    (void)sheet.get_cell(1, 1);
    (void)sheet.cell_count();
    (void)sheet.estimated_memory_usage();
    (void)sheet.sparse_cells();
    (void)sheet.sparse_cells(fastxlsx::CellRange {1, 1, 1, 2});
    check(editor.last_edit_error() == prior_error,
        "WorksheetEditor dirty/read inspection should not update last_edit_error");

    sheet.erase_cell(5, 5);
    check(!sheet.has_pending_changes(),
        "erasing a missing sparse record should keep a clean worksheet clean");
    check(!editor.has_pending_changes(),
        "missing-cell erase should not make the WorkbookEditor save_as pending");
    check(!editor.last_edit_error().has_value(),
        "successful missing-cell erase should clear prior mutation diagnostics");

    sheet.erase_cell(2, 1);
    check(sheet.has_pending_changes(),
        "erasing an existing sparse record should dirty the worksheet session");
    check(editor.has_pending_changes(),
        "dirty WorksheetEditor should make WorkbookEditor save_as pending");
    check(editor.pending_change_count() == 0,
        "dirty WorksheetEditor should not count as a queued Patch handoff before flush");

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as path preflight failure should run before dirty-session flush");
    check(sheet.has_pending_changes(),
        "save_as path preflight failure should preserve dirty WorksheetEditor state");
    check(editor.pending_change_count() == 0,
        "save_as path preflight failure should not queue a materialized handoff");

    editor.save_as(first_output);
    check(!sheet.has_pending_changes(),
        "successful save_as should clear the flushed WorksheetEditor dirty state");
    check(editor.pending_change_count() == 1,
        "successful save_as should count one materialized Patch handoff");

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_not_contains(first_xml, "placeholder-a2",
        "flushed dirty WorksheetEditor state should persist erased source cells");

    sheet.set_cell(3, 3, fastxlsx::CellValue::text("dirty-again"));
    check(sheet.has_pending_changes(),
        "WorksheetEditor should become dirty again after a post-save mutation");
    editor.save_as(second_output);

    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check_contains(second_entries.at("xl/worksheets/sheet1.xml"), "dirty-again",
        "post-save dirty WorksheetEditor mutation should persist on a later save_as");
}

void test_public_workbook_editor_pending_materialized_names_track_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-materialized-names-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-materialized-names-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(editor.pending_materialized_worksheet_names().empty(),
        "fresh WorkbookEditor should expose no pending materialized worksheet names");

    fastxlsx::WorksheetEditor data = editor.worksheet("Data");
    fastxlsx::WorksheetEditor untouched = editor.worksheet("Untouched");
    check(editor.pending_materialized_worksheet_names().empty(),
        "clean materialized sessions should not appear in pending materialized names");

    check(threw_fastxlsx_error([&] {
        data.set_cell("a1", fastxlsx::CellValue::text("invalid-reference"));
    }), "failed WorksheetEditor mutation should throw before pending materialized diagnostics");
    const std::optional<std::string> prior_error = editor.last_edit_error();
    check(prior_error.has_value(),
        "failed WorksheetEditor mutation should set last_edit_error before diagnostics");
    check(editor.pending_materialized_worksheet_names().empty(),
        "failed WorksheetEditor mutation should not add a dirty materialized name");
    check(editor.last_edit_error() == prior_error,
        "pending materialized name inspection should not update last_edit_error");

    data.set_cell(1, 1, fastxlsx::CellValue::text("dirty-data"));
    {
        const std::vector<std::string> names = editor.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "one dirty materialized session should report its planned sheet name");
    }
    check(editor.pending_change_count() == 0,
        "dirty materialized names should not count as Patch handoffs before save_as");

    untouched.set_cell(1, 2, fastxlsx::CellValue::text("dirty-untouched"));
    {
        const std::vector<std::string> names = editor.pending_materialized_worksheet_names();
        check(names.size() == 2,
            "two dirty materialized sessions should both be reported");
        if (names.size() == 2) {
            check(names[0] == "Data",
                "dirty materialized names should follow planned catalog order");
            check(names[1] == "Untouched",
                "dirty materialized names should include the second planned sheet");
        }
    }

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as path preflight failure should not flush dirty materialized sessions");
    {
        const std::vector<std::string> names = editor.pending_materialized_worksheet_names();
        check(names.size() == 2 && names[0] == "Data" && names[1] == "Untouched",
            "failed save_as should preserve dirty materialized names");
    }

    editor.save_as(output);
    check(editor.pending_materialized_worksheet_names().empty(),
        "successful save_as should clear dirty materialized names");
    check(editor.pending_change_count() == 2,
        "successful save_as should count both materialized Patch handoffs");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "dirty-data",
        "first dirty materialized worksheet should persist through save_as");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "dirty-untouched",
        "second dirty materialized worksheet should persist through save_as");
}

void test_public_workbook_editor_pending_materialized_names_move_with_owner()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-materialized-names-move-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-materialized-names-move-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-materialized-names-move-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor source_sheet = editor.worksheet("Data");
    source_sheet.set_cell(1, 1, fastxlsx::CellValue::text("moved-dirty-data"));

    fastxlsx::WorkbookEditor moved = std::move(editor);
    check(editor.pending_materialized_worksheet_names().empty(),
        "moved-from editor should expose no pending materialized names");
    {
        const std::vector<std::string> names = moved.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "move construction should preserve dirty materialized names");
    }

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::WorksheetEditor target_sheet = target.worksheet("Untouched");
    target_sheet.set_cell(1, 1, fastxlsx::CellValue::text("discarded-target-dirty"));
    {
        const std::vector<std::string> names = target.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Untouched",
            "target should start with its own dirty materialized name");
    }

    target = std::move(moved);
    check(moved.pending_materialized_worksheet_names().empty(),
        "move-assigned-from editor should expose no pending materialized names");
    {
        const std::vector<std::string> names = target.pending_materialized_worksheet_names();
        check(names.size() == 1 && names[0] == "Data",
            "move assignment should replace target dirty materialized names");
    }

    target.save_as(output);
    check(target.pending_materialized_worksheet_names().empty(),
        "save_as after move assignment should clear assigned dirty materialized names");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "moved-dirty-data",
        "move-assigned editor should save assigned dirty materialized payload");
    check_not_contains(output_entries.at("xl/worksheets/sheet2.xml"), "discarded-target-dirty",
        "move assignment should not leak discarded target dirty materialized payload");
}

void test_public_worksheet_editor_get_cell_missing_and_blank_semantics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-get-cell-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::size_t cell_count_before_missing_read = sheet.cell_count();
    check(threw_fastxlsx_error([&] { (void)sheet.get_cell(4, 4); }),
        "get_cell should throw for a missing sparse cell");
    check(sheet.cell_count() == cell_count_before_missing_read,
        "failed get_cell should not mutate the sparse store");
    check(!editor.last_edit_error().has_value(),
        "failed get_cell missing-cell read should not update last_edit_error");

    sheet.set_cell(4, 4, fastxlsx::CellValue::blank());
    const fastxlsx::CellValue explicit_blank = sheet.get_cell(4, 4);
    check(explicit_blank.kind() == fastxlsx::CellValueKind::Blank,
        "get_cell should return explicit blank records distinctly from missing cells");
    check(sheet.try_cell(5, 5) == std::nullopt,
        "try_cell should continue to report unrelated missing cells as nullopt");
}

void test_public_worksheet_editor_a1_overloads_read_mutate_and_save()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-a1-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-a1-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::optional<fastxlsx::CellValue> maybe_a1 = sheet.try_cell("A1");
    check(maybe_a1.has_value() && maybe_a1->kind() == fastxlsx::CellValueKind::Text &&
            maybe_a1->text_value() == "placeholder-a1",
        "A1 try_cell overload should read source-backed text");

    const fastxlsx::CellValue b1 = sheet.get_cell("B1");
    check(b1.kind() == fastxlsx::CellValueKind::Number && b1.number_value() == 1.0,
        "A1 get_cell overload should read source-backed numbers");

    sheet.set_cell("D4", fastxlsx::CellValue::text("a1-overload-new"));
    const fastxlsx::CellValue d4 = sheet.get_cell("D4");
    check(d4.kind() == fastxlsx::CellValueKind::Text &&
            d4.text_value() == "a1-overload-new",
        "A1 set_cell overload should update the materialized sparse store");

    sheet.erase_cell("A2");
    check(sheet.try_cell("A2") == std::nullopt,
        "A1 erase_cell overload should remove the sparse record");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "A1 overload save_as should refresh dimension through the existing handoff");
    check_contains(worksheet_xml, R"(<c r="D4" t="inlineStr">)",
        "A1 overload set_cell should persist the target cell reference");
    check_contains(worksheet_xml, "a1-overload-new",
        "A1 overload set_cell should persist the target text");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "A1 overload erase_cell should persist the erased source cell");
}

void test_public_worksheet_editor_a1_overloads_reject_invalid_references()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-a1-invalid-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    const std::size_t cell_count_before_reads = sheet.cell_count();
    const std::array<std::string_view, 8> invalid_references {
        "", "a1", "1A", "A0", "A01", "XFE1", "A1048577", "A1:B2"};

    for (const std::string_view reference : invalid_references) {
        check(threw_fastxlsx_error([&] { (void)sheet.try_cell(reference); }),
            "A1 try_cell overload should reject invalid references");
        check(threw_fastxlsx_error([&] { (void)sheet.get_cell(reference); }),
            "A1 get_cell overload should reject invalid references");
    }

    check(sheet.cell_count() == cell_count_before_reads,
        "invalid A1 read overloads should not mutate the sparse store");
    check(!sheet.try_cell("XFD1048576").has_value(),
        "A1 try_cell overload should accept the last legal Excel cell reference");
    check(!editor.last_edit_error().has_value(),
        "invalid A1 read overloads should not update last_edit_error");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "invalid A1 read overloads should preserve existing cells");

    const std::size_t cell_count_before_mutation = sheet.cell_count();
    check(threw_fastxlsx_error([&] {
        sheet.set_cell("a1", fastxlsx::CellValue::text("should-not-write"));
    }), "A1 set_cell overload should reject lowercase references");
    check(sheet.cell_count() == cell_count_before_mutation,
        "invalid A1 set_cell should not mutate sparse cell count");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "invalid A1 set_cell should not overwrite source cells");
    check(editor.last_edit_error().has_value(),
        "invalid A1 set_cell should update last_edit_error");

    check(threw_fastxlsx_error([&] { sheet.erase_cell("A1:B2"); }),
        "A1 erase_cell overload should reject range references");
    check(sheet.cell_count() == cell_count_before_mutation,
        "invalid A1 erase_cell should not mutate sparse cell count");
    check(sheet.get_cell("A1").text_value() == "placeholder-a1",
        "invalid A1 erase_cell should not remove source cells");
    check(editor.last_edit_error().has_value(),
        "invalid A1 erase_cell should update last_edit_error");
}

void test_public_worksheet_editor_sparse_cells_snapshot()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-sparse-cells-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sparse-cells-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(4, 4, fastxlsx::CellValue::text("snapshot-new"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.erase_cell(2, 1);

    const std::vector<fastxlsx::WorksheetCellSnapshot> cells = sheet.sparse_cells();
    check(cells.size() == sheet.cell_count(),
        "sparse_cells should return one snapshot per active sparse record");
    check(cells.size() == 4,
        "sparse_cells should include source-backed, edited, and explicit blank records");

    check(cells[0].reference.row == 1 && cells[0].reference.column == 1,
        "sparse_cells should be row-major by row then column");
    check(cells[0].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[0].value.text_value() == "placeholder-a1",
        "sparse_cells should copy source-backed text values");
    check(cells[1].reference.row == 1 && cells[1].reference.column == 2,
        "sparse_cells should keep same-row cells ordered by column");
    check(cells[1].value.kind() == fastxlsx::CellValueKind::Number &&
            cells[1].value.number_value() == 1.0,
        "sparse_cells should copy source-backed numeric values");
    check(cells[2].reference.row == 3 && cells[2].reference.column == 2 &&
            cells[2].value.kind() == fastxlsx::CellValueKind::Blank,
        "sparse_cells should include explicit blank records");
    check(cells[3].reference.row == 4 && cells[3].reference.column == 4 &&
            cells[3].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[3].value.text_value() == "snapshot-new",
        "sparse_cells should include edited cells");

    sheet.set_cell(1, 1, fastxlsx::CellValue::text("changed-after-snapshot"));
    check(cells[0].value.text_value() == "placeholder-a1",
        "sparse_cells should return owning snapshots, not borrowed store references");
    check(!editor.last_edit_error().has_value(),
        "sparse_cells read should not update last_edit_error");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "sparse_cells should not interfere with dirty-session save_as");
    check_contains(worksheet_xml, "changed-after-snapshot",
        "subsequent edits after sparse_cells should still persist");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "sparse_cells should not revive erased source cells");
}

void test_public_worksheet_editor_sparse_cells_range_snapshot()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-sparse-range-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-sparse-range-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    sheet.set_cell(4, 4, fastxlsx::CellValue::text("range-excluded"));
    sheet.set_cell(3, 2, fastxlsx::CellValue::blank());
    sheet.set_cell(3, 3, fastxlsx::CellValue::text("range-new"));
    sheet.erase_cell(2, 1);

    const fastxlsx::CellRange range {1, 2, 3, 3};
    const std::vector<fastxlsx::WorksheetCellSnapshot> cells = sheet.sparse_cells(range);
    check(cells.size() == 3,
        "range sparse_cells should return only active sparse records inside the range");
    check(cells[0].reference.row == 1 && cells[0].reference.column == 2 &&
            cells[0].value.kind() == fastxlsx::CellValueKind::Number &&
            cells[0].value.number_value() == 1.0,
        "range sparse_cells should include source-backed cells in row-major order");
    check(cells[1].reference.row == 3 && cells[1].reference.column == 2 &&
            cells[1].value.kind() == fastxlsx::CellValueKind::Blank,
        "range sparse_cells should include explicit blank records");
    check(cells[2].reference.row == 3 && cells[2].reference.column == 3 &&
            cells[2].value.kind() == fastxlsx::CellValueKind::Text &&
            cells[2].value.text_value() == "range-new",
        "range sparse_cells should include edited records in range");

    const std::vector<fastxlsx::WorksheetCellSnapshot> empty_range =
        sheet.sparse_cells(fastxlsx::CellRange {2, 2, 2, 3});
    check(empty_range.empty(),
        "range sparse_cells should not synthesize missing cells as blank snapshots");

    sheet.set_cell(1, 2, fastxlsx::CellValue::number(2.0));
    check(cells[0].value.number_value() == 1.0,
        "range sparse_cells should return owning snapshots, not borrowed records");
    check(!editor.last_edit_error().has_value(),
        "range sparse_cells read should not update last_edit_error");

    const std::size_t cell_count_before_invalid_ranges = sheet.cell_count();
    const std::array<fastxlsx::CellRange, 4> invalid_ranges {
        fastxlsx::CellRange {3, 3, 1, 1},
        fastxlsx::CellRange {0, 1, 1, 1},
        fastxlsx::CellRange {1, 1, 1048577, 1},
        fastxlsx::CellRange {1, 1, 1, 16385},
    };
    for (const fastxlsx::CellRange invalid_range : invalid_ranges) {
        check(threw_fastxlsx_error([&] { (void)sheet.sparse_cells(invalid_range); }),
            "range sparse_cells should reject invalid CellRange values");
    }
    check(sheet.cell_count() == cell_count_before_invalid_ranges,
        "invalid range sparse_cells calls should not mutate sparse store state");
    check(!editor.last_edit_error().has_value(),
        "invalid range sparse_cells calls should not update last_edit_error");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<dimension ref="A1:D4"/>)",
        "range sparse_cells should not interfere with dirty-session save_as");
    check_contains(worksheet_xml, "range-excluded",
        "cells outside the inspected range should still persist");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "range sparse_cells should not revive erased source cells");
}

void test_public_worksheet_editor_erase_cell_auto_flushes_on_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-erase-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-erase-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");

    check(sheet.cell_count() == 3,
        "public WorksheetEditor should materialize the supported source cells");
    sheet.erase_cell(2, 1);
    check(!sheet.try_cell(2, 1).has_value(),
        "public WorksheetEditor erase_cell should remove the sparse record");
    check(sheet.cell_count() == 2,
        "public WorksheetEditor erase_cell should update sparse cell count");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "public WorksheetEditor erase_cell should persist through save_as");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B1"/>)",
        "public WorksheetEditor erase_cell should shrink the projected dimension");
}

void test_public_worksheet_editor_options_guard_failure_preserves_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-options-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-options-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditorOptions options;
    options.max_cells = 1;

    check(threw_fastxlsx_error([&] {
        (void)editor.worksheet("Data", options);
    }), "public WorksheetEditor should enforce max_cells while materializing source cells");
    check(!editor.has_pending_changes(),
        "failed public WorksheetEditor materialization should not dirty the editor");
    check(editor.pending_change_count() == 0,
        "failed public WorksheetEditor materialization should not queue public edits");
    check(!editor.last_edit_error().has_value(),
        "failed public WorksheetEditor materialization should not update last_edit_error");

    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("after-options-failure")}});
    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "after-options-failure",
        "editor should remain usable after failed public WorksheetEditor materialization");
}

void test_public_worksheet_editor_blocks_same_sheet_patch_operations()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-public-worksheet-mixing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-public-worksheet-mixing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::WorksheetEditor sheet = editor.worksheet("Data");
    sheet.set_cell(1, 1, fastxlsx::CellValue::text("materialized-public-state"));

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("blocked-public")}});
    }), "public replace_sheet_data should reject a materialized same sheet");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "BlockedPublicRename"); }),
        "public rename_sheet should reject a materialized same sheet");
    check(editor.last_edit_error().has_value(),
        "blocked same-sheet operations should record last_edit_error");

    editor.rename_sheet("Untouched", "OtherPublicName");
    check(!editor.last_edit_error().has_value(),
        "successful cross-sheet public edit should clear last_edit_error");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="OtherPublicName")",
        "cross-sheet rename should still save beside public WorksheetEditor edits");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "materialized-public-state",
        "public WorksheetEditor edit should auto-flush beside a cross-sheet edit");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "blocked-public",
        "blocked same-sheet replacement should not leak into output");
    check_not_contains(output_entries.at("xl/workbook.xml"), "BlockedPublicRename",
        "blocked same-sheet rename should not leak into workbook catalog");
}

void test_internal_materialized_session_assignment_from_moved_from_source_clears_target()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-assign-moved-from-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-assign-moved-from-target.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-assign-moved-from-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1,
        fastxlsx::CellValue::text("held-materialized-state"));

    fastxlsx::WorkbookEditor holder = std::move(editor);
    check(threw_fastxlsx_error([&] { (void)editor.worksheet_names(); }),
        "materialized source editor should be moved-from before assignment");

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        target, "Untouched", "Untouched");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        target, "Untouched", 1, 1,
        fastxlsx::CellValue::text("target-materialized-should-clear"));
    target.replace_sheet_data(
        "Data", {{fastxlsx::CellValue::text("target-public-should-clear")}});
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(target) == 1,
        "target should start with one materialized session before moved-from assignment");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 1,
        "target should start with dirty materialized state before moved-from assignment");
    check(target.pending_change_count() == 1,
        "target should start with public queued state before moved-from assignment");

    target = std::move(editor);

    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(target) == 0,
        "assignment from moved-from source should clear target materialized sessions");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 0,
        "assignment from moved-from source should clear target dirty materialized state");
    check(!target.has_pending_changes(),
        "assignment from moved-from source should clear target public pending changes");
    check(target.pending_change_count() == 0,
        "assignment from moved-from source should clear target public pending count");
    check(target.pending_replacement_cell_count() == 0,
        "assignment from moved-from source should clear target replacement diagnostics");
    check(target.pending_replacement_worksheet_names().empty(),
        "assignment from moved-from source should clear target replacement names");
    check(!target.last_edit_error().has_value(),
        "assignment from moved-from source should clear target last_edit_error");
    check(threw_fastxlsx_error([&] { target.save_as(output); }),
        "assignment from moved-from source should leave target unable to save stale state");

    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(holder) == 1,
        "assignment from moved-from source should not disturb prior moved-to holder");
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(holder);
    holder.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "held-materialized-state",
        "prior moved-to holder should still save its materialized payload");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "target-public-should-clear",
        "prior moved-to holder should not receive cleared target public payload");
    check_not_contains(output_entries.at("xl/worksheets/sheet2.xml"),
        "target-materialized-should-clear",
        "prior moved-to holder should not receive cleared target materialized payload");
}

void test_internal_materialized_session_blocks_whole_sheet_replacement()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-mixing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-mixing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("materialized-dirty"));

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("blocked")}});
    }), "replace_sheet_data should be rejected after materializing the same planned sheet");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Data"),
        "blocked materialized replacement should preserve the private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "blocked materialized replacement should preserve private dirty state");
    check(editor.has_pending_changes(),
        "dirty materialized replacement state should make save_as pending");
    check(editor.pending_replacement_cell_count() == 0,
        "blocked materialized replacement should not record replacement cells");
    check(editor.pending_replacement_worksheet_names().empty(),
        "blocked materialized replacement should not add pending replacement names");
    check(editor.last_edit_error().has_value(),
        "blocked materialized replacement should record a public edit diagnostic");

    editor.replace_sheet_data("Untouched", {{fastxlsx::CellValue::text("allowed-other-sheet")}});
    check(editor.has_pending_changes(),
        "materializing one sheet should not block replacement of a different sheet");
    check(editor.pending_replacement_cell_count() == 1,
        "allowed replacement on a different sheet should record its payload");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "blocked",
        "blocked materialized replacement should not leak into the source sheet output");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "materialized-dirty",
        "dirty materialized state should auto-flush on save_as");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "allowed-other-sheet",
        "replacement on a different sheet should still be saved");
}

void test_internal_materialized_session_blocks_materialize_after_public_replacement()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-after-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-after-replace-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("queued-replacement")}});

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
            editor, "Data", "Data");
    }), "materializing a planned sheet with queued replacement data should be rejected");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "blocked materialize-after-replacement should not register a private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "blocked materialize-after-replacement should not create dirty private state");
    check(editor.pending_change_count() == 1,
        "blocked materialize-after-replacement should preserve queued public edit count");
    check(editor.pending_replacement_cell_count() == 1,
        "blocked materialize-after-replacement should preserve replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "test-hook materialize-after-replacement failure should not update public last_edit_error");

    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Untouched", "Untouched");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Untouched"),
        "queued replacement on one sheet should not block materializing another sheet");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "queued-replacement",
        "blocked materialize-after-replacement should preserve staged replacement output");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "blocked materialize-after-replacement should not restore source sheet data");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(output_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "clean materialization of another sheet should not change untouched output");
}

void test_internal_materialized_session_blocks_materialize_after_renamed_public_replacement()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-after-renamed-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-after-renamed-replace-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::text("queued-before-rename")}});
    editor.rename_sheet("Data", "QueuedData");

    check(editor.has_pending_replacement("QueuedData"),
        "rename after replacement should migrate replacement diagnostics to the planned name");
    check(!editor.has_pending_replacement("Data"),
        "rename after replacement should remove replacement diagnostics for the old name");
    {
        const std::vector<std::string> pending_names =
            editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "QueuedData",
            "pending replacement name list should expose the renamed planned sheet");
    }

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
            editor, "QueuedData", "Data");
    }), "materializing a renamed planned sheet with queued replacement data should be rejected");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "blocked renamed materialize-after-replacement should not register a private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "blocked renamed materialize-after-replacement should not create dirty private state");
    check(editor.pending_change_count() == 2,
        "blocked renamed materialize-after-replacement should preserve public edit count");
    check(editor.pending_replacement_cell_count() == 1,
        "blocked renamed materialize-after-replacement should preserve replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "test-hook renamed materialize-after-replacement failure should not update public last_edit_error");

    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Untouched", "Untouched");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Untouched"),
        "renamed queued replacement should not block materializing another sheet");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="QueuedData")",
        "renamed replacement output should keep the planned workbook sheet name");
    check_not_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "renamed replacement output should not restore the source workbook sheet name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "queued-before-rename",
        "blocked renamed materialize-after-replacement should preserve staged replacement output");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "blocked renamed materialize-after-replacement should not restore source sheet data");
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(output_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "clean materialization of another sheet should not change untouched output");
}

void test_internal_materialized_session_blocks_materialize_after_replacement_on_renamed_sheet()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-replace-on-renamed-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-replace-on-renamed-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "RenamedThenReplaced");
    editor.replace_sheet_data("RenamedThenReplaced",
        {{fastxlsx::CellValue::text("replace-after-rename")}});

    check(editor.has_pending_replacement("RenamedThenReplaced"),
        "replacement after rename should track the renamed planned sheet");
    check(!editor.has_pending_replacement("Data"),
        "replacement after rename should not track the old source sheet name");
    check(editor.pending_change_count() == 2,
        "rename plus replacement should queue two public facade edits");

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
            editor, "RenamedThenReplaced", "Data");
    }), "materializing a renamed sheet after queued replacement should be rejected");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "blocked replacement-on-renamed materialize should not register a private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "blocked replacement-on-renamed materialize should not create dirty private state");
    check(editor.pending_change_count() == 2,
        "blocked replacement-on-renamed materialize should preserve public edit count");
    check(editor.pending_replacement_cell_count() == 1,
        "blocked replacement-on-renamed materialize should preserve replacement diagnostics");
    check(!editor.last_edit_error().has_value(),
        "test-hook replacement-on-renamed materialize failure should not update public last_edit_error");

    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Untouched", "Untouched");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Untouched"),
        "replacement on renamed sheet should not block materializing another sheet");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedThenReplaced")",
        "replacement-on-renamed output should keep the planned workbook sheet name");
    check_not_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "replacement-on-renamed output should not restore the source workbook sheet name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "replace-after-rename",
        "blocked replacement-on-renamed materialize should preserve staged replacement output");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "blocked replacement-on-renamed materialize should not restore source sheet data");
}

void test_internal_materialized_session_flushes_dirty_projection_to_patch_plan()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-flush-source.xlsx");
    const std::filesystem::path clean_output =
        artifact("fastxlsx-workbook-editor-materialized-flush-clean-output.xlsx");
    const std::filesystem::path dirty_output =
        artifact("fastxlsx-workbook-editor-materialized-flush-dirty-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(!editor.has_pending_changes(),
        "flushing clean materialized sessions should not queue public edit diagnostics");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "flushing clean materialized sessions should leave no dirty sessions");

    editor.save_as(clean_output);
    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const auto clean_output_entries = fastxlsx::test::read_zip_entries(clean_output);
    check(clean_output_entries == source_entries,
        "clean materialization flush should keep no-op save_as as a source roundtrip");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("flushed-materialized"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "test hook set_cell should mark the materialized session dirty before flush");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.has_pending_changes(),
        "flushing dirty materialized sessions should queue coarse public edit diagnostics");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "successful materialized flush should clear private dirty state");

    editor.save_as(dirty_output);
    const auto dirty_output_entries = fastxlsx::test::read_zip_entries(dirty_output);
    check_contains(dirty_output_entries.at("xl/worksheets/sheet1.xml"), "flushed-materialized",
        "dirty materialized flush should save the projected worksheet cell");
    check_contains(dirty_output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<dimension ref="A1:B2"/>)",
        "dirty materialized flush should save the refreshed sparse-store dimension");
    check(dirty_output_entries.at("xl/worksheets/sheet2.xml") ==
            source_entries.at("xl/worksheets/sheet2.xml"),
        "dirty materialized flush should preserve untouched worksheet bytes");
}

void test_internal_materialized_session_blocks_same_sheet_rename()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-rename-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-rename-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("dirty-before-rename"));

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "BlockedRename"); }),
        "rename_sheet should reject the same planned sheet after materialization");
    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Data"),
        "blocked materialized rename should preserve the private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "blocked materialized rename should preserve dirty private state");
    check(editor.has_pending_changes(),
        "dirty materialized rename state should make save_as pending");
    check(editor.last_edit_error().has_value(),
        "blocked materialized rename should record a public edit diagnostic");
    {
        const std::vector<std::string> names = editor.worksheet_names();
        check(names.size() == 2 && names[0] == "Data" && names[1] == "Untouched",
            "blocked materialized rename should preserve planned catalog names");
    }

    editor.rename_sheet("Untouched", "OtherRenamed");
    check(editor.has_pending_changes(),
        "materializing one sheet should not block renaming a different sheet");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "renaming another sheet should not clear dirty materialized state");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "blocked materialized rename output should keep the original materialized sheet name");
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="OtherRenamed")",
        "rename of a different sheet should still be saved");
    check_not_contains(output_entries.at("xl/workbook.xml"), "BlockedRename",
        "blocked materialized rename should not leak the rejected name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "dirty-before-rename",
        "dirty materialized cells should auto-flush through save_as");
}

void test_internal_materialized_session_flushes_after_rejected_public_operations()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-rejected-public-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-rejected-public-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("dirty-after-rejected-public-op"));

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data(
            "Data", {{fastxlsx::CellValue::text("blocked-public-replacement")}});
    }), "replace_sheet_data should reject a dirty materialized sheet before staging payload");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "BlockedRename"); }),
        "rename_sheet should reject a dirty materialized sheet before catalog mutation");

    check(fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Data"),
        "rejected public operations should preserve the private materialized session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "rejected public operations should preserve dirty materialized state");
    check(editor.has_pending_changes(),
        "dirty materialized state should keep save_as pending after rejected public operations");
    check(editor.pending_replacement_cell_count() == 0,
        "rejected public replacement should not leave staged replacement cells");
    check(editor.pending_replacement_worksheet_names().empty(),
        "rejected public replacement should not leave staged replacement names");
    check(editor.last_edit_error().has_value(),
        "rejected public operations should record a public edit diagnostic");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.pending_change_count() == 1,
        "explicit materialized flush after rejected public operations should queue one handoff");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "explicit materialized flush after rejected public operations should clear dirty state");
    check(editor.pending_replacement_cell_count() == 0,
        "materialized flush should not reuse rejected sheetData replacement diagnostics");
    check(editor.pending_replacement_worksheet_names().empty(),
        "materialized flush should not expose rejected replacement sheet names");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "flush after rejected public operations should preserve the original sheet name");
    check_not_contains(workbook_xml, "BlockedRename",
        "flush after rejected public operations should not leak the rejected sheet name");
    check_contains(worksheet_xml, "dirty-after-rejected-public-op",
        "flush after rejected public operations should save the dirty materialized payload");
    check_not_contains(worksheet_xml, "blocked-public-replacement",
        "flush after rejected public operations should not leak the rejected replacement payload");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "flush after rejected public operations should keep refreshed sparse-store dimension");
}

void test_internal_materialized_session_flushes_after_other_sheet_edit_clears_rejected_error()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-other-edit-after-reject-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-other-edit-after-reject-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("dirty-after-cross-sheet-edit"));

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data(
            "Data", {{fastxlsx::CellValue::text("blocked-before-cross-sheet-edit")}});
    }), "same-sheet replacement should be rejected before a cross-sheet public edit");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "BlockedBeforeOtherEdit"); }),
        "same-sheet rename should be rejected before a cross-sheet public edit");
    check(editor.last_edit_error().has_value(),
        "rejected same-sheet operations should record a public edit diagnostic");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "rejected same-sheet operations should keep the materialized session dirty");

    editor.replace_sheet_data(
        "Untouched", {{fastxlsx::CellValue::text("other-sheet-after-rejected-op")}});

    check(!editor.last_edit_error().has_value(),
        "successful cross-sheet public edit should clear the rejected same-sheet diagnostic");
    check(editor.pending_change_count() == 1,
        "cross-sheet public replacement should queue one public edit before materialized flush");
    check(editor.pending_replacement_cell_count() == 1,
        "cross-sheet public replacement should expose only its own replacement diagnostics");
    {
        const std::vector<std::string> names = editor.pending_replacement_worksheet_names();
        check(names.size() == 1 && names[0] == "Untouched",
            "cross-sheet public replacement should not add diagnostics for the materialized sheet");
    }
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "successful cross-sheet public edit should not clear dirty materialized state");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.pending_change_count() == 2,
        "cross-sheet replacement plus materialized flush should queue two coarse edits");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "materialized flush after cross-sheet public edit should clear dirty state");
    check(editor.pending_replacement_cell_count() == 1,
        "materialized flush should not pollute cross-sheet replacement diagnostics");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "cross-sheet edit plus materialized flush should preserve the materialized sheet name");
    check_contains(workbook_xml, R"(name="Untouched")",
        "cross-sheet edit plus materialized flush should preserve the other sheet name");
    check_not_contains(workbook_xml, "BlockedBeforeOtherEdit",
        "cross-sheet edit plus materialized flush should not leak the rejected rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "dirty-after-cross-sheet-edit",
        "cross-sheet edit plus materialized flush should save the materialized payload");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "blocked-before-cross-sheet-edit",
        "cross-sheet edit plus materialized flush should not leak the rejected same-sheet payload");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "other-sheet-after-rejected-op",
        "cross-sheet public replacement should still save its payload");
}

void test_internal_materialized_session_flushes_after_other_sheet_rename_clears_rejected_error()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-other-rename-after-reject-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-other-rename-after-reject-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("dirty-after-cross-sheet-rename"));

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data(
            "Data", {{fastxlsx::CellValue::text("blocked-before-cross-sheet-rename")}});
    }), "same-sheet replacement should be rejected before a cross-sheet public rename");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "BlockedBeforeOtherRename"); }),
        "same-sheet rename should be rejected before a cross-sheet public rename");
    check(editor.last_edit_error().has_value(),
        "rejected same-sheet operations should record a public edit diagnostic before rename");

    editor.rename_sheet("Untouched", "OtherRenamedAfterRejectedOp");

    check(!editor.last_edit_error().has_value(),
        "successful cross-sheet rename should clear the rejected same-sheet diagnostic");
    check(editor.pending_change_count() == 1,
        "cross-sheet rename should queue one public edit before materialized flush");
    check(editor.pending_replacement_cell_count() == 0,
        "cross-sheet rename should not create sheetData replacement diagnostics");
    check(editor.pending_replacement_worksheet_names().empty(),
        "cross-sheet rename should not create sheetData replacement names");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "successful cross-sheet rename should not clear dirty materialized state");
    {
        const std::vector<std::string> names = editor.worksheet_names();
        check(names.size() == 2 && names[0] == "Data" &&
                names[1] == "OtherRenamedAfterRejectedOp",
            "cross-sheet rename should update only the other planned sheet name");
    }

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.pending_change_count() == 2,
        "cross-sheet rename plus materialized flush should queue two coarse edits");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "materialized flush after cross-sheet rename should clear dirty state");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "cross-sheet rename plus materialized flush should preserve the materialized sheet name");
    check_contains(workbook_xml, R"(name="OtherRenamedAfterRejectedOp")",
        "cross-sheet rename plus materialized flush should save the other sheet rename");
    check_not_contains(workbook_xml, "BlockedBeforeOtherRename",
        "cross-sheet rename plus materialized flush should not leak the rejected rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "dirty-after-cross-sheet-rename",
        "cross-sheet rename plus materialized flush should save the materialized payload");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        "blocked-before-cross-sheet-rename",
        "cross-sheet rename plus materialized flush should not leak the rejected payload");
    check_contains(output_entries.at("xl/worksheets/sheet2.xml"), "keep-me",
        "cross-sheet rename should preserve the renamed worksheet bytes");
}

void test_internal_materialized_session_rejected_public_operations_preserve_flushed_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-rejected-after-flush-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-rejected-after-flush-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("flushed-before-rejected-public-op"));
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.pending_change_count() == 1,
        "initial materialized flush should queue one staged worksheet projection");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "initial materialized flush should leave the private session clean");

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data(
            "Data", {{fastxlsx::CellValue::text("blocked-after-flush")}});
    }), "replace_sheet_data should remain rejected while a clean materialized session exists");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "BlockedAfterFlush"); }),
        "rename_sheet should remain rejected while a clean materialized session exists");

    check(editor.pending_change_count() == 1,
        "rejected public operations after flush should preserve the staged projection count");
    check(editor.pending_replacement_cell_count() == 0,
        "rejected public replacement after flush should not add replacement diagnostics");
    check(editor.pending_replacement_worksheet_names().empty(),
        "rejected public replacement after flush should not add replacement names");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "rejected public operations after flush should keep clean private state clean");
    check(editor.last_edit_error().has_value(),
        "rejected public operations after flush should record a public edit diagnostic");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "rejected public operations after flush should preserve the original sheet name");
    check_not_contains(workbook_xml, "BlockedAfterFlush",
        "rejected public operations after flush should not leak the rejected rename");
    check_contains(worksheet_xml, "flushed-before-rejected-public-op",
        "rejected public operations after flush should preserve the staged projection");
    check_not_contains(worksheet_xml, "blocked-after-flush",
        "rejected public operations after flush should not leak the rejected replacement");
}

void test_internal_materialized_session_failed_save_as_preserves_dirty_and_flushed_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-save-failure-source.xlsx");
    const std::filesystem::path missing_parent_output =
        artifact("fastxlsx-workbook-editor-materialized-save-failure-missing-parent") /
        "output.xlsx";
    const std::filesystem::path directory_output =
        artifact("fastxlsx-workbook-editor-materialized-save-failure-directory-output");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-save-failure-output.xlsx");
    std::filesystem::create_directories(directory_output);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("dirty-survives-failed-save"));

    check(threw_fastxlsx_error([&] { editor.save_as(std::filesystem::path{}); }),
        "failed save_as before flush should throw without touching dirty materialized state");
    check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
        "missing-parent save_as before flush should fail before materialized state changes");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "failed save_as before flush should preserve dirty materialized state");
    check(editor.pending_change_count() == 0,
        "failed save_as before flush should not queue materialized projections");
    check(!editor.last_edit_error().has_value(),
        "failed save_as before flush should not create public last_edit_error");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "explicit flush after failed save_as should clear dirty materialized state");
    check(editor.pending_change_count() == 1,
        "explicit flush after failed save_as should queue one staged projection");

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "failed save_as over source should preserve staged materialized projection");
    check(threw_fastxlsx_error([&] { editor.save_as(directory_output); }),
        "failed save_as to directory should preserve staged materialized projection");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "failed save_as after flush should keep clean materialized state clean");
    check(editor.pending_change_count() == 1,
        "failed save_as after flush should preserve staged projection diagnostics");
    check(!editor.last_edit_error().has_value(),
        "failed save_as after flush should not create public last_edit_error");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "dirty-survives-failed-save",
        "valid save_as after failed saves should write the materialized projection");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "valid save_as after failed saves should keep refreshed sparse-store dimension");
}

void test_internal_materialized_session_reflush_after_failed_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-reflush-after-save-failure-source.xlsx");
    const std::filesystem::path directory_output =
        artifact("fastxlsx-workbook-editor-materialized-reflush-after-save-failure-directory");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-reflush-after-save-failure-output.xlsx");
    std::filesystem::create_directories(directory_output);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("first-before-failed-save"));
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "initial flush before failed save_as should leave the materialized session clean");
    check(editor.pending_change_count() == 1,
        "initial flush before failed save_as should queue one staged projection");

    check(threw_fastxlsx_error([&] { editor.save_as(directory_output); }),
        "save_as to a directory should fail before consuming the staged projection");

    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "failed save_as should keep the flushed materialized session clean");
    check(editor.pending_change_count() == 1,
        "failed save_as should preserve the first staged projection diagnostic");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("second-after-failed-save"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "mutating after failed save_as should dirty the existing materialized session again");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "reflush after failed save_as should clear dirty state again");
    check(editor.pending_change_count() == 2,
        "reflush after failed save_as should record a second coarse handoff");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "second-after-failed-save",
        "reflush after failed save_as should save the later materialized payload");
    check_not_contains(worksheet_xml, "first-before-failed-save",
        "reflush after failed save_as should replace the earlier staged projection");
}

void test_internal_materialized_session_move_reflush_after_failed_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-move-reflush-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-move-reflush-target.xlsx");
    const std::filesystem::path directory_output =
        artifact("fastxlsx-workbook-editor-materialized-move-reflush-directory");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-move-reflush-output.xlsx");
    std::filesystem::create_directories(directory_output);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1,
        fastxlsx::CellValue::text("first-moved-before-failed-save"));
    editor.replace_sheet_data(
        "Untouched", {{fastxlsx::CellValue::text("assigned-public-survives-retry")}});

    fastxlsx::WorkbookEditor moved = std::move(editor);

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        target, "Untouched", "Untouched");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        target, "Untouched", 1, 1,
        fastxlsx::CellValue::text("discarded-target-materialized-retry"));
    target.replace_sheet_data(
        "Data", {{fastxlsx::CellValue::text("discarded-target-public-retry")}});

    target = std::move(moved);

    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(target) == 1,
        "move assignment before failed save retry should keep one assigned materialized session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 1,
        "move assignment before failed save retry should keep assigned dirty state");
    check(target.pending_change_count() == 1,
        "move assignment before failed save retry should keep assigned public edit only");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(target);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 0,
        "first flush after move assignment should clear assigned dirty state");
    check(target.pending_change_count() == 2,
        "first flush after move assignment should stage materialized and public edits");

    check(threw_fastxlsx_error([&] { target.save_as(directory_output); }),
        "directory save_as after moved materialized flush should fail before consuming state");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 0,
        "failed save after moved flush should keep the materialized session clean");
    check(target.pending_change_count() == 2,
        "failed save after moved flush should preserve staged diagnostics");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        target, "Data", 1, 1,
        fastxlsx::CellValue::text("second-moved-after-failed-save"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 1,
        "mutation after moved failed save should re-dirty the assigned materialized session");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(target);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 0,
        "reflush after moved failed save should clear dirty state again");
    check(target.pending_change_count() == 3,
        "reflush after moved failed save should record a new coarse handoff");

    target.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string data_xml = output_entries.at("xl/worksheets/sheet1.xml");
    const std::string untouched_xml = output_entries.at("xl/worksheets/sheet2.xml");
    check_contains(data_xml, "second-moved-after-failed-save",
        "moved reflush after failed save should save the later materialized payload");
    check_not_contains(data_xml, "first-moved-before-failed-save",
        "moved reflush after failed save should replace the earlier staged projection");
    check_contains(untouched_xml, "assigned-public-survives-retry",
        "moved reflush after failed save should preserve assigned cross-sheet public edit");
    check_not_contains(data_xml, "discarded-target-public-retry",
        "moved reflush after failed save should not leak discarded target public edit");
    check_not_contains(untouched_xml, "discarded-target-materialized-retry",
        "moved reflush after failed save should not leak discarded target materialized session");
}

void test_internal_materialized_session_reflush_after_successful_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-reflush-after-success-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-materialized-reflush-after-success-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-materialized-reflush-after-success-second.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("first-before-successful-save"));
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "initial flush before successful save_as should leave materialized session clean");

    editor.save_as(first_output);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_worksheet_xml = first_entries.at("xl/worksheets/sheet1.xml");
    check_contains(first_worksheet_xml, "first-before-successful-save",
        "first successful save_as should write the initial materialized projection");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("second-after-successful-save"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "mutation after successful save_as should re-dirty the materialized session");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "reflush after successful save_as should clear dirty state again");
    check(editor.pending_change_count() == 2,
        "reflush after successful save_as should record a second coarse handoff");

    editor.save_as(second_output);
    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_worksheet_xml = second_entries.at("xl/worksheets/sheet1.xml");
    check_contains(second_worksheet_xml, "second-after-successful-save",
        "second successful save_as should write the later materialized projection");
    check_not_contains(second_worksheet_xml, "first-before-successful-save",
        "second successful save_as should not leak the earlier materialized projection");
    check_contains(first_worksheet_xml, "first-before-successful-save",
        "second successful save_as should not rewrite the first output artifact");
}

void test_internal_materialized_session_move_reflush_after_successful_save_as()
{
    const std::filesystem::path source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-move-reflush-success-source.xlsx");
    const std::filesystem::path target_source =
        write_two_sheet_source(
            "fastxlsx-workbook-editor-materialized-move-reflush-success-target.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-materialized-move-reflush-success-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-materialized-move-reflush-success-second.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1,
        fastxlsx::CellValue::text("first-moved-before-successful-save"));
    editor.replace_sheet_data(
        "Untouched", {{fastxlsx::CellValue::text("assigned-public-survives-success-reuse")}});

    fastxlsx::WorkbookEditor moved = std::move(editor);

    fastxlsx::WorkbookEditor target = fastxlsx::WorkbookEditor::open(target_source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        target, "Untouched", "Untouched");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        target, "Untouched", 1, 1,
        fastxlsx::CellValue::text("discarded-target-materialized-success-reuse"));
    target.replace_sheet_data(
        "Data", {{fastxlsx::CellValue::text("discarded-target-public-success-reuse")}});

    target = std::move(moved);

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(target);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 0,
        "first moved flush before successful save_as should clear dirty state");
    check(target.pending_change_count() == 2,
        "first moved flush before successful save_as should stage assigned public and materialized edits");

    target.save_as(first_output);
    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    const std::string first_data_xml = first_entries.at("xl/worksheets/sheet1.xml");
    const std::string first_untouched_xml = first_entries.at("xl/worksheets/sheet2.xml");
    check_contains(first_data_xml, "first-moved-before-successful-save",
        "first moved successful save_as should write the initial materialized projection");
    check_contains(first_untouched_xml, "assigned-public-survives-success-reuse",
        "first moved successful save_as should preserve the assigned cross-sheet public edit");
    check_not_contains(first_data_xml, "discarded-target-public-success-reuse",
        "first moved successful save_as should not leak discarded target public edit");
    check_not_contains(first_untouched_xml, "discarded-target-materialized-success-reuse",
        "first moved successful save_as should not leak discarded target materialized session");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        target, "Data", 1, 1,
        fastxlsx::CellValue::text("second-moved-after-successful-save"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 1,
        "mutation after moved successful save_as should re-dirty assigned materialized state");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(target);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(target) == 0,
        "reflush after moved successful save_as should clear dirty state again");
    check(target.pending_change_count() == 3,
        "reflush after moved successful save_as should record a new coarse handoff");

    target.save_as(second_output);
    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    const std::string second_data_xml = second_entries.at("xl/worksheets/sheet1.xml");
    const std::string second_untouched_xml = second_entries.at("xl/worksheets/sheet2.xml");
    check_contains(second_data_xml, "second-moved-after-successful-save",
        "second moved successful save_as should write the later materialized projection");
    check_not_contains(second_data_xml, "first-moved-before-successful-save",
        "second moved successful save_as should replace the earlier staged projection");
    check_contains(second_untouched_xml, "assigned-public-survives-success-reuse",
        "second moved successful save_as should keep the assigned cross-sheet public edit");
    check_not_contains(second_data_xml, "discarded-target-public-success-reuse",
        "second moved successful save_as should not leak discarded target public edit");
    check_not_contains(second_untouched_xml, "discarded-target-materialized-success-reuse",
        "second moved successful save_as should not leak discarded target materialized session");

    const auto first_entries_after_second_save =
        fastxlsx::test::read_zip_entries(first_output);
    check(first_entries_after_second_save.at("xl/worksheets/sheet1.xml") == first_data_xml,
        "second moved successful save_as should not rewrite the first output worksheet");
}

void test_internal_materialized_session_reflush_replaces_prior_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-reflush-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-reflush-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("first-flush"));
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "first materialized flush should clear dirty state");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("second-flush"));
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "mutation after first flush should re-dirty the materialized session");
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.pending_change_count() == 2,
        "two dirty materialized flushes should count as two coarse internal edit handoffs");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "second materialized flush should clear dirty state again");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "second-flush",
        "second materialized flush should replace the prior staged projection");
    check_not_contains(worksheet_xml, "first-flush",
        "prior materialized flush payload should not leak after reflush");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B2"/>)",
        "reflush output should keep the refreshed sparse-store dimension");
}

void test_internal_materialized_session_flush_failure_preserves_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-flush-failure-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-flush-failure-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Ghost", "Untouched");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("would-have-flushed"));
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Ghost", 1, 1, fastxlsx::CellValue::text("orphan-flush"));

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(
            editor);
    }), "materialized flush should reject a dirty session whose planned name is absent");
    check(editor.pending_change_count() == 0,
        "failed materialized flush should not queue coarse public edit diagnostics");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 2,
        "failed materialized flush should preserve all dirty sessions");

    check(threw_fastxlsx_error([&] { editor.save_as(output); }),
        "public save_as auto-flush should also reject the missing planned-name projection");
    check(editor.pending_change_count() == 0,
        "failed save_as auto-flush should not queue partial materialized diagnostics");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 2,
        "failed save_as auto-flush should preserve all dirty sessions");
}

void test_internal_materialized_session_flush_uses_planned_name_after_rename()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-flush-renamed-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-flush-renamed-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "RenamedData");
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "RenamedData", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "RenamedData", 1, 1, fastxlsx::CellValue::text("renamed-flush"));
    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);

    check(editor.pending_change_count() == 2,
        "rename plus materialized flush should queue two coarse edit diagnostics");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "planned-name materialized flush after rename should clear dirty state");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedData")",
        "materialized flush after rename should preserve the planned catalog name");
    check_not_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "materialized flush after rename should not restore the source catalog name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "renamed-flush",
        "materialized flush after rename should rewrite the source worksheet part");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<dimension ref="A1:B2"/>)",
        "materialized flush after rename should keep refreshed sparse-store dimension");
}

void test_internal_materialized_session_blank_and_erase_projection()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-erase-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-erase-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");

    fastxlsx::detail::testing_workbook_editor_erase_materialized_cell(
        editor, "Data", 9, 9);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "erasing a missing materialized cell should keep the session clean");

    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::blank());
    fastxlsx::detail::testing_workbook_editor_erase_materialized_cell(
        editor, "Data", 2, 1);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "blank set plus existing-cell erase should dirty the materialized session");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "blank/erase materialized flush should clear dirty state");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<row r="1"><c r="A1"/><c r="B1"><v>1</v></c></row>)",
        "explicit blank should remain as an empty A1 cell while B1 is preserved");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "blank materialized cell should remove prior source text payload");
    check_not_contains(worksheet_xml, "placeholder-a2",
        "erased materialized source cell should not appear in flushed output");
    check_not_contains(worksheet_xml, R"(<row r="2")",
        "erasing the only row-2 source cell should remove the explicit row");
    check_contains(worksheet_xml, R"(<dimension ref="A1:B1"/>)",
        "blank/erase materialized flush should refresh dimension to remaining extents");
}

void test_internal_materialized_session_repeated_materialize_preserves_dirty_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-rematerialize-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-rematerialize-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    fastxlsx::detail::testing_workbook_editor_set_materialized_cell(
        editor, "Data", 1, 1, fastxlsx::CellValue::text("kept-after-rematerialize"));

    fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
        editor, "Data", "Data");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 1,
        "repeated materialization of the same planned sheet should reuse one session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 1,
        "repeated materialization should preserve dirty state instead of reloading source");

    fastxlsx::detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(editor);
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "kept-after-rematerialize",
        "repeated materialization should not discard dirty materialized payload");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "repeated materialization should not reload the original source A1 payload");
}

void test_internal_materialized_session_load_guard_failure_preserves_editor_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-load-guard-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-load-guard-output.xlsx");

    fastxlsx::WorkbookEditorOptions options;
    options.max_replacement_cells = 1;
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source, options);

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
            editor, "Data", "Data");
    }), "guarded materialized source load should fail before registering a session");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "failed materialized source load should not register a private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "failed materialized source load should not create dirty private state");
    check(!editor.has_pending_changes(),
        "failed materialized source load should not queue public edit diagnostics");
    check(editor.pending_change_count() == 0,
        "failed materialized source load should leave public pending count unchanged");
    check(!editor.last_edit_error().has_value(),
        "test-hook materialized source load failure should not update public last_edit_error");

    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("after-load-guard")}});
    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "after-load-guard",
        "editor should remain usable after guarded materialized load failure");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "later valid replacement should not preserve the old source A1 payload");
}

void test_internal_materialized_session_memory_guard_failure_preserves_editor_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-memory-guard-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-memory-guard-output.xlsx");

    fastxlsx::WorkbookEditorOptions options;
    options.replacement_memory_budget_bytes = 1;
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source, options);

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
            editor, "Data", "Data");
    }), "memory-guarded materialized source load should fail before registering a session");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "failed memory-guarded materialized load should not register a private session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "failed memory-guarded materialized load should not create dirty private state");
    check(!editor.has_pending_changes(),
        "failed memory-guarded materialized load should not queue public edit diagnostics");
    check(!editor.last_edit_error().has_value(),
        "memory-guarded test-hook load failure should not update public last_edit_error");

    editor.rename_sheet("Data", "AfterMemoryGuard");
    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="AfterMemoryGuard")",
        "editor should remain usable for rename after memory-guarded materialized load failure");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "rename after materialized load failure should preserve original worksheet data");
}

void test_internal_materialized_session_missing_source_load_preserves_editor_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-materialized-missing-load-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-materialized-missing-load-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] {
        fastxlsx::detail::testing_workbook_editor_materialize_source_sheet(
            editor, "Missing", "Missing");
    }), "missing-source materialized load should fail before registering a session");
    check(fastxlsx::detail::testing_workbook_editor_materialized_session_count(editor) == 0,
        "missing-source materialized load should not register a private session");
    check(!fastxlsx::detail::testing_workbook_editor_has_materialized_session(editor, "Missing"),
        "failed missing-source load should not leave an orphan planned session");
    check(fastxlsx::detail::testing_workbook_editor_dirty_materialized_session_count(editor) == 0,
        "missing-source materialized load should not create dirty private state");
    check(!editor.has_pending_changes(),
        "missing-source materialized load should not queue public edit diagnostics");
    check(editor.pending_change_count() == 0,
        "missing-source materialized load should leave public pending count unchanged");
    check(editor.pending_replacement_cell_count() == 0,
        "missing-source materialized load should leave replacement diagnostics unchanged");
    check(editor.pending_replacement_worksheet_names().empty(),
        "missing-source materialized load should not add pending replacement names");
    check(!editor.last_edit_error().has_value(),
        "test-hook missing-source load failure should not update public last_edit_error");

    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("after-missing-load")}});
    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "after-missing-load",
        "editor should remain usable after missing-source materialized load failure");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "later valid replacement should not preserve the old source A1 payload");
}

void test_pending_change_diagnostics_track_public_edits()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-pending-source.xlsx");

    fastxlsx::WorkbookEditor clean_editor = fastxlsx::WorkbookEditor::open(source);
    check(!clean_editor.has_pending_changes(),
        "newly opened editor should report no pending changes");
    check(clean_editor.pending_change_count() == 0,
        "newly opened editor should report zero pending changes");
    check(clean_editor.pending_replacement_cell_count() == 0,
        "newly opened editor should report zero pending replacement cells");
    check(clean_editor.pending_replacement_worksheet_names().empty(),
        "newly opened editor should report no pending replacement worksheets");
    check(!clean_editor.has_pending_replacement("Data"),
        "newly opened editor should report no pending replacement for Data");
    check(clean_editor.estimated_pending_replacement_memory_usage() == 0,
        "newly opened editor should report zero pending replacement memory");

    check(threw_fastxlsx_error([&] {
        clean_editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(1.0)}});
    }), "rejected replace_sheet_data should throw FastXlsxError");
    check(!clean_editor.has_pending_changes(),
        "rejected replace_sheet_data should not mark the editor dirty");
    check(clean_editor.pending_change_count() == 0,
        "rejected replace_sheet_data should not add pending changes");
    check(clean_editor.pending_replacement_worksheet_names().empty(),
        "rejected replace_sheet_data should not add pending replacement names");

    check(threw_fastxlsx_error([&] { clean_editor.rename_sheet("Data", "Bad/Name"); }),
        "rejected rename_sheet should throw FastXlsxError");
    check(!clean_editor.has_pending_changes(),
        "rejected rename_sheet should not mark the editor dirty");
    check(clean_editor.pending_change_count() == 0,
        "rejected rename_sheet should not add pending changes");

    clean_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(9.0)}});
    check(clean_editor.has_pending_changes(),
        "successful replace_sheet_data should mark the editor dirty");
    check(clean_editor.pending_change_count() > 0,
        "successful replace_sheet_data should expose a coarse pending count");
    check(clean_editor.pending_replacement_cell_count() == 1,
        "successful replace_sheet_data should expose final queued replacement cells");
    {
        const std::vector<std::string> pending_names =
            clean_editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "Data",
            "successful replace_sheet_data should expose the pending replacement sheet");
        check(clean_editor.has_pending_replacement("Data"),
            "successful replace_sheet_data should mark Data as pending replacement");
        check(!clean_editor.has_pending_replacement("Untouched"),
            "successful replace_sheet_data should not mark untouched sheets");
    }
    check(clean_editor.estimated_pending_replacement_memory_usage() > 0,
        "successful replace_sheet_data should expose estimated replacement memory");

    fastxlsx::WorkbookEditor rename_editor = fastxlsx::WorkbookEditor::open(source);
    rename_editor.rename_sheet("Data", "Renamed");
    check(rename_editor.has_pending_changes(),
        "successful rename_sheet should mark the editor dirty");
    check(rename_editor.pending_change_count() > 0,
        "successful rename_sheet should expose a coarse pending count");
    check(rename_editor.pending_replacement_cell_count() == 0,
        "rename_sheet should not add replacement cells");
    check(rename_editor.pending_replacement_worksheet_names().empty(),
        "rename_sheet should not add pending replacement names");
    check(!rename_editor.has_pending_replacement("Renamed"),
        "rename_sheet should not mark the renamed sheet as data-replaced");
    check(rename_editor.estimated_pending_replacement_memory_usage() == 0,
        "rename_sheet should not add replacement memory");
}

void test_last_edit_error_tracks_failed_public_edits()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-last-error-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(!editor.last_edit_error().has_value(),
        "newly opened editor should report no last edit error");

    bool failed = false;
    try {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(1.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "failed replace_sheet_data should record last edit error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "last edit error should match the thrown replace_sheet_data diagnostic");
            check_contains(*last_error, "current planned catalog",
                "last edit error should preserve public planned-catalog context");
        }
    }
    check(failed, "missing sheet replacement should fail");

    check_public_inspection_preserves_last_edit_error(
        editor, editor.last_edit_error());

    editor.rename_sheet("Data", "Report");
    check(!editor.last_edit_error().has_value(),
        "successful rename_sheet should clear last edit error");

    failed = false;
    try {
        editor.rename_sheet("Report", "Bad/Name");
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "failed rename_sheet should record last edit error");
        if (last_error.has_value()) {
            check(*last_error == error.what(),
                "last edit error should match the thrown rename_sheet diagnostic");
            check_contains(*last_error, "Bad/Name",
                "last edit error should include the rejected sheet name");
        }
    }
    check(failed, "invalid rename should fail");

    check_public_inspection_preserves_last_edit_error(
        editor, editor.last_edit_error());

    editor.replace_sheet_data("Report", {{fastxlsx::CellValue::number(2.0)}});
    check(!editor.last_edit_error().has_value(),
        "successful replace_sheet_data should clear last edit error");
}

void test_pending_worksheet_edit_summaries_track_public_facade_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-edit-summary-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(editor.pending_worksheet_edits().empty(),
        "newly opened editor should report no pending worksheet edit summaries");

    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::number(2.0)}});
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "single replacement should report one worksheet edit summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data",
                "replacement summary should report the source sheet name");
            check(summary.planned_name == "Data",
                "replacement summary should report the current planned sheet name");
            check(!summary.renamed,
                "replacement-only summary should not be marked as renamed");
            check(summary.sheet_data_replaced,
                "replacement-only summary should report sheetData replacement");
            check(summary.replacement_cell_count == 2,
                "replacement summary should report final queued cell count");
            check(summary.estimated_replacement_memory_usage > 0,
                "replacement summary should report estimated replacement memory");
        }
    }

    editor.rename_sheet("Data", "Report");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "replace+rename should still report one worksheet edit summary");
        if (summaries.size() == 1) {
            const auto& summary = summaries[0];
            check(summary.source_name == "Data",
                "replace+rename summary should keep the source sheet name");
            check(summary.planned_name == "Report",
                "replace+rename summary should report the planned sheet name");
            check(summary.renamed,
                "replace+rename summary should be marked as renamed");
            check(summary.sheet_data_replaced,
                "replace+rename summary should report sheetData replacement");
            check(summary.replacement_cell_count == 2,
                "replace+rename summary should preserve queued replacement cells");
            check(summary.estimated_replacement_memory_usage > 0,
                "replace+rename summary should preserve replacement memory");
        }
    }

    editor.replace_sheet_data("Report",
        {{fastxlsx::CellValue::number(3.0), fastxlsx::CellValue::number(4.0)},
            {fastxlsx::CellValue::number(5.0)}});
    editor.rename_sheet("Untouched", "Archive");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 2,
            "replacement+rename and rename-only edits should report two summaries");
        if (summaries.size() == 2) {
            const auto& replaced = summaries[0];
            check(replaced.source_name == "Data",
                "summaries should follow source workbook sheet order");
            check(replaced.planned_name == "Report",
                "updated replacement summary should keep the planned name");
            check(replaced.renamed,
                "updated replacement summary should stay marked as renamed");
            check(replaced.sheet_data_replaced,
                "updated replacement summary should report replacement");
            check(replaced.replacement_cell_count == 3,
                "updated replacement summary should report final replacement cells");
            check(replaced.estimated_replacement_memory_usage > 0,
                "updated replacement summary should keep replacement memory");

            const auto& renamed_only = summaries[1];
            check(renamed_only.source_name == "Untouched",
                "rename-only summary should report the source sheet name");
            check(renamed_only.planned_name == "Archive",
                "rename-only summary should report the planned sheet name");
            check(renamed_only.renamed,
                "rename-only summary should be marked as renamed");
            check(!renamed_only.sheet_data_replaced,
                "rename-only summary should not report sheetData replacement");
            check(renamed_only.replacement_cell_count == 0,
                "rename-only summary should report zero replacement cells");
            check(renamed_only.estimated_replacement_memory_usage == 0,
                "rename-only summary should report zero replacement memory");
        }
    }

    editor.replace_sheet_data("Archive", {{fastxlsx::CellValue::text("archived")}});
    {
        const std::vector<std::string> pending_names =
            editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 2,
            "two replaced sheets should report two pending replacement names");
        if (pending_names.size() == 2) {
            check(pending_names[0] == "Report",
                "pending replacement names should follow current planned catalog order");
            check(pending_names[1] == "Archive",
                "pending replacement names should include renamed second sheet in order");
        }
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 2,
            "two replaced renamed sheets should still report two summaries");
        if (summaries.size() == 2) {
            check(summaries[0].source_name == "Data" && summaries[0].planned_name == "Report",
                "first summary should keep source-order Data -> Report mapping");
            check(summaries[0].sheet_data_replaced,
                "first summary should remain marked as replaced");
            check(summaries[1].source_name == "Untouched" &&
                    summaries[1].planned_name == "Archive",
                "second summary should keep source-order Untouched -> Archive mapping");
            check(summaries[1].renamed,
                "second summary should remain marked as renamed");
            check(summaries[1].sheet_data_replaced,
                "second summary should now report replacement after planned-name edit");
            check(summaries[1].replacement_cell_count == 1,
                "second summary should report its final replacement cell count");
            check(summaries[1].estimated_replacement_memory_usage > 0,
                "second summary should report its replacement memory estimate");
        }
    }
}

void test_replace_sheet_data_source_read_failure_preserves_public_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-source-read-failure.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-source-read-failure-output.xlsx");

    const std::string original_source_bytes = fastxlsx::test::read_file(source);
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    std::string corrupted_source_bytes = original_source_bytes;
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
    corrupt_zip_entry_crc_metadata(corrupted_source_bytes, "xl/worksheets/sheet1.xml");
#else
    corrupt_zip_entry_payload(corrupted_source_bytes, "xl/worksheets/sheet1.xml");
#endif
    write_binary_file(source, corrupted_source_bytes);

    bool failed = false;
    try {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(5.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::string message = error.what();
        check_contains(message,
            "current worksheet input for worksheet sheetData replacement output",
            "WorkbookEditor should preserve the internal sheetData current-input boundary");
        check_contains(message, "xl/worksheets/sheet1.xml",
            "WorkbookEditor should preserve the corrupt worksheet entry context");
        check_not_contains(message, "sheetData replacement XML",
            "WorkbookEditor should not mislabel source-entry failures as "
            "replacement payload failures");
#ifndef FASTXLSX_TEST_HAS_MINIZIP_NG
        check_contains(message, "CRC mismatch",
            "WorkbookEditor stored source read failure should preserve the ZIP CRC error");
        check_contains(message, "expected ",
            "WorkbookEditor stored CRC failure should report the expected CRC");
        check_contains(message, "actual ",
            "WorkbookEditor stored CRC failure should report the actual CRC");
#else
        if (message.find("CRC mismatch") != std::string::npos) {
            check_contains(message, "expected ",
                "WorkbookEditor CRC failure should report the expected CRC");
            check_contains(message, "actual ",
                "WorkbookEditor CRC failure should report the actual CRC");
        }
#endif
    }

    check(failed,
        "replace_sheet_data should fail when the source worksheet entry cannot be read");
    check(!editor.has_pending_changes(),
        "source read failure should not mark the public editor dirty");
    check(editor.pending_change_count() == 0,
        "source read failure should not add public pending changes");
    check(editor.pending_replacement_cell_count() == 0,
        "source read failure should not record public replacement cells");
    check(editor.estimated_pending_replacement_memory_usage() == 0,
        "source read failure should not record public replacement memory");
    check(editor.has_worksheet("Data"),
        "source read failure should not disturb the opened source sheet catalog");

    write_binary_file(source, original_source_bytes);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(6.0)}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), R"(<v>6</v>)",
        "editor should remain usable after a source read failure once the source is restored");
}

void check_clean_replace_sheet_data_failure_state(
    const fastxlsx::WorkbookEditor& editor, std::string_view scenario)
{
    check(!editor.has_pending_changes(),
        std::string(scenario) + " should not mark the public editor dirty");
    check(editor.pending_change_count() == 0,
        std::string(scenario) + " should not add public pending changes");
    check(editor.pending_replacement_cell_count() == 0,
        std::string(scenario) + " should not record public replacement cells");
    check(editor.estimated_pending_replacement_memory_usage() == 0,
        std::string(scenario) + " should not record public replacement memory");
    check(editor.has_worksheet("Data"),
        std::string(scenario) + " should not disturb the opened source sheet catalog");
}

void check_sheet_data_current_input_facade_error(
    const std::string& message, std::string_view scenario)
{
    check_contains(message,
        "current worksheet input for worksheet sheetData replacement output",
        std::string(scenario)
            + " should preserve the internal sheetData current-input boundary");
    check_not_contains(message, "sheetData replacement XML",
        std::string(scenario)
            + " should not be mislabeled as replacement payload input");
}

void test_replace_sheet_data_source_xml_failures_preserve_public_state()
{
    const std::filesystem::path missing_source =
        write_two_sheet_source("fastxlsx-workbook-editor-missing-source-sheetdata.xlsx");
    const std::filesystem::path missing_output =
        artifact("fastxlsx-workbook-editor-missing-source-sheetdata-output.xlsx");
    rewrite_package_entry_as_stored(missing_source, "xl/worksheets/sheet1.xml",
        R"(<worksheet><dimension ref="A1"/></worksheet>)");

    fastxlsx::WorkbookEditor missing_editor = fastxlsx::WorkbookEditor::open(missing_source);
    bool failed = false;
    try {
        missing_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(7.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::string message = error.what();
        check_sheet_data_current_input_facade_error(
            message, "missing source sheetData failure");
        check_contains(message, "worksheet XML sheetData element is missing",
            "missing source sheetData failure should explain the missing target");
    }
    check(failed,
        "WorkbookEditor should reject replace_sheet_data when source worksheet has no sheetData");
    check_clean_replace_sheet_data_failure_state(
        missing_editor, "missing source sheetData failure");
    missing_editor.rename_sheet("Data", "MissingChecked");
    missing_editor.save_as(missing_output);
    check_contains(fastxlsx::test::read_zip_entries(missing_output).at("xl/workbook.xml"),
        R"(name="MissingChecked")",
        "editor should remain usable for a valid catalog edit after missing source sheetData");

    const std::filesystem::path malformed_source =
        write_two_sheet_source("fastxlsx-workbook-editor-malformed-source-sheetdata.xlsx");
    const std::filesystem::path malformed_output =
        artifact("fastxlsx-workbook-editor-malformed-source-sheetdata-output.xlsx");
    rewrite_package_entry_as_stored(malformed_source, "xl/worksheets/sheet1.xml",
        R"(<worksheet xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<dimension ref="A1"/>)"
        R"(<sheetData><row r="1"><c r="A1"><v>1</v></c></row>)"
        R"(<autoFilter ref="A1:A1"/>)"
        R"(</worksheet>)");

    fastxlsx::WorkbookEditor malformed_editor =
        fastxlsx::WorkbookEditor::open(malformed_source);
    failed = false;
    try {
        malformed_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(8.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::string message = error.what();
        check_sheet_data_current_input_facade_error(
            message, "malformed source sheetData failure");
        check_contains(message, "worksheet event reader found an invalid worksheet boundary",
            "malformed source sheetData failure should preserve event-reader diagnostics");
    }
    check(failed,
        "WorkbookEditor should reject replace_sheet_data when source sheetData is malformed");
    check_clean_replace_sheet_data_failure_state(
        malformed_editor, "malformed source sheetData failure");
    malformed_editor.rename_sheet("Data", "MalformedChecked");
    malformed_editor.save_as(malformed_output);
    check_contains(fastxlsx::test::read_zip_entries(malformed_output).at("xl/workbook.xml"),
        R"(name="MalformedChecked")",
        "editor should remain usable for a valid catalog edit after malformed source sheetData");
}

void test_replacement_guardrails_and_payload_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-guardrails-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-guardrails-output.xlsx");

    fastxlsx::WorkbookEditorOptions max_cell_options;
    max_cell_options.max_replacement_cells = 1;
    fastxlsx::WorkbookEditor max_cell_editor =
        fastxlsx::WorkbookEditor::open(source, max_cell_options);

    check(threw_fastxlsx_error([&] {
        max_cell_editor.replace_sheet_data("Data",
            {{fastxlsx::CellValue::number(1.0), fastxlsx::CellValue::number(2.0)}});
    }), "replace_sheet_data should enforce max_replacement_cells before commit");
    check(!max_cell_editor.has_pending_changes(),
        "max_replacement_cells failure should not mark the editor dirty");
    check(max_cell_editor.pending_replacement_cell_count() == 0,
        "max_replacement_cells failure should not record replacement cells");
    check(max_cell_editor.estimated_pending_replacement_memory_usage() == 0,
        "max_replacement_cells failure should not record replacement memory");

    max_cell_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(3.0)}});
    check(max_cell_editor.pending_replacement_cell_count() == 1,
        "valid guarded replacement should record one queued cell");
    const std::size_t first_memory =
        max_cell_editor.estimated_pending_replacement_memory_usage();
    check(first_memory > 0,
        "valid guarded replacement should record non-zero estimated memory");

    max_cell_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(4.0)}});
    check(max_cell_editor.pending_change_count() == 2,
        "repeated same-sheet replacement should still count public edit calls");
    check(max_cell_editor.pending_replacement_cell_count() == 1,
        "repeated same-sheet replacement should report only final queued cells");
    check(max_cell_editor.estimated_pending_replacement_memory_usage() > 0,
        "repeated same-sheet replacement should keep final estimated memory");

    max_cell_editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1"><v>4</v></c>)",
        "guarded replacement output should use the final queued payload");
    check_not_contains(worksheet_xml, R"(<v>3</v>)",
        "guarded replacement output should drop stale same-sheet payload");

    fastxlsx::WorkbookEditorOptions memory_options;
    memory_options.replacement_memory_budget_bytes = 1;
    fastxlsx::WorkbookEditor memory_editor =
        fastxlsx::WorkbookEditor::open(source, memory_options);
    check(threw_fastxlsx_error([&] {
        memory_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("too large")}});
    }), "replace_sheet_data should enforce replacement_memory_budget_bytes before commit");
    check(!memory_editor.has_pending_changes(),
        "memory budget failure should not mark the editor dirty");
    check(memory_editor.pending_replacement_cell_count() == 0,
        "memory budget failure should not record replacement cells");
    check(memory_editor.estimated_pending_replacement_memory_usage() == 0,
        "memory budget failure should not record replacement memory");
}

void test_missing_sheet_throws_and_editor_stays_usable()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-missing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-missing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    bool failed = false;
    try {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(1.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        const std::string message = error.what();
        check_contains(message, "current planned catalog",
            "missing sheet failure should name the planned catalog lookup");
        check_contains(message, "Missing",
            "missing sheet failure should include the requested sheet name");
    }
    check(failed, "replacing a missing sheet should throw FastXlsxError");
    check(!editor.has_pending_changes(),
        "missing sheet failure should not mark the editor dirty");
    check(editor.pending_replacement_cell_count() == 0,
        "missing sheet failure should not record replacement cells");
    check(editor.estimated_pending_replacement_memory_usage() == 0,
        "missing sheet failure should not record replacement memory");

    fastxlsx::WorkbookEditorOptions guard_options;
    guard_options.max_replacement_cells = 0;
    fastxlsx::WorkbookEditor guarded_editor =
        fastxlsx::WorkbookEditor::open(source, guard_options);
    failed = false;
    try {
        guarded_editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(2.0)}});
    } catch (const fastxlsx::FastXlsxError& error) {
        failed = true;
        check_contains(error.what(), "current planned catalog",
            "missing sheet preflight should run before replacement payload guardrails");
    }
    check(failed,
        "guarded missing sheet replacement should throw FastXlsxError");
    check(!guarded_editor.has_pending_changes(),
        "guarded missing sheet failure should not mark the editor dirty");
    check(guarded_editor.pending_replacement_cell_count() == 0,
        "guarded missing sheet failure should not record replacement cells");

    // The editor must remain usable after a rejected edit.
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(7.0)}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>7</v></c>)",
        "editor should still apply a valid edit after a rejected one");
}

void test_save_as_over_source_throws()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-overwrite-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(1.0)}});

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as over the source package should throw FastXlsxError");
}

void test_noop_save_as_preserves_source_package_entries()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_document_properties(
            "fastxlsx-workbook-editor-noop-save-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-noop-save-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(!editor.has_pending_changes(),
        "fresh WorkbookEditor should not report pending changes before no-op save_as");
    check(editor.pending_change_count() == 0,
        "fresh WorkbookEditor should not report pending public edit count");
    check(editor.pending_replacement_cell_count() == 0,
        "fresh WorkbookEditor should not report pending replacement cells");
    check(editor.pending_replacement_worksheet_names().empty(),
        "fresh WorkbookEditor should not report pending replacement names");
    check(editor.pending_worksheet_edits().empty(),
        "fresh WorkbookEditor should not report pending worksheet edits");
    check(!editor.last_edit_error().has_value(),
        "fresh WorkbookEditor should not report last_edit_error");

    editor.save_as(output);

    check(!editor.has_pending_changes(),
        "no-op save_as should not create public pending changes");
    check(editor.pending_change_count() == 0,
        "no-op save_as should not create public pending edit count");
    check(!editor.last_edit_error().has_value(),
        "no-op save_as should not create last_edit_error");

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries == source_entries,
        "no-op save_as should preserve decompressed source package entries");

    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    check(reopened.has_worksheet("Data"),
        "no-op save_as output should keep the Data sheet");
    check(reopened.has_worksheet("Untouched"),
        "no-op save_as output should keep the Untouched sheet");
}

void test_noop_save_as_preserves_failed_edit_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-noop-save-after-error-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-noop-save-after-error-copy.xlsx");
    const std::filesystem::path edited_output =
        artifact("fastxlsx-workbook-editor-noop-save-after-error-edited.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Missing",
            {{fastxlsx::CellValue::number(13.0)}});
    }), "missing-sheet edit should fail before no-op save_as");

    const std::optional<std::string> last_error_before_save = editor.last_edit_error();
    check(last_error_before_save.has_value(),
        "failed edit before no-op save_as should record last_edit_error");
    check(!editor.has_pending_changes(),
        "failed edit before no-op save_as should not create pending changes");
    check(editor.pending_change_count() == 0,
        "failed edit before no-op save_as should not create pending edit count");
    check(editor.pending_worksheet_edits().empty(),
        "failed edit before no-op save_as should not create pending summaries");

    editor.save_as(noop_output);

    check(editor.last_edit_error() == last_error_before_save,
        "no-op save_as should preserve a prior failed-edit diagnostic");
    check(!editor.has_pending_changes(),
        "no-op save_as after a failed edit should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after a failed edit should keep pending edit count empty");
    check(editor.pending_worksheet_edits().empty(),
        "no-op save_as after a failed edit should keep pending summaries empty");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "no-op save_as after a failed edit should preserve source entries");

    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("after error")}});
    check(!editor.last_edit_error().has_value(),
        "successful edit after no-op save_as should clear the failed-edit diagnostic");
    editor.save_as(edited_output);

    const auto edited_entries = fastxlsx::test::read_zip_entries(edited_output);
    check_contains(edited_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1" t="inlineStr"><is><t>after error</t></is></c>)",
        "later edit after no-op save_as should still write replacement data");
}

void test_noop_save_as_preserves_failed_rename_diagnostic()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-noop-save-after-rename-error-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-noop-save-after-rename-error-copy.xlsx");
    const std::filesystem::path renamed_output =
        artifact("fastxlsx-workbook-editor-noop-save-after-rename-error-renamed.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Bad/Name"); }),
        "invalid rename should fail before no-op save_as");

    const std::optional<std::string> last_error_before_save = editor.last_edit_error();
    check(last_error_before_save.has_value(),
        "failed rename before no-op save_as should record last_edit_error");
    if (last_error_before_save.has_value()) {
        check_contains(*last_error_before_save, "Bad/Name",
            "failed rename diagnostic should name the rejected sheet");
    }
    check(!editor.has_pending_changes(),
        "failed rename before no-op save_as should not create pending changes");
    check(editor.pending_change_count() == 0,
        "failed rename before no-op save_as should not create pending edit count");
    check(editor.pending_worksheet_edits().empty(),
        "failed rename before no-op save_as should not create pending summaries");

    editor.save_as(noop_output);

    check(editor.last_edit_error() == last_error_before_save,
        "no-op save_as should preserve a prior failed-rename diagnostic");
    check(!editor.has_pending_changes(),
        "no-op save_as after a failed rename should keep the editor clean");
    check(editor.pending_change_count() == 0,
        "no-op save_as after a failed rename should keep pending edit count empty");
    check(editor.pending_worksheet_edits().empty(),
        "no-op save_as after a failed rename should keep pending summaries empty");

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "no-op save_as after a failed rename should preserve source entries");

    editor.rename_sheet("Data", "CleanName");
    check(!editor.last_edit_error().has_value(),
        "successful rename after no-op save_as should clear the failed-rename diagnostic");
    editor.save_as(renamed_output);

    const auto renamed_entries = fastxlsx::test::read_zip_entries(renamed_output);
    check_contains(renamed_entries.at("xl/workbook.xml"), R"(name="CleanName")",
        "later rename after no-op save_as should still update workbook catalog");
}

void test_noop_save_as_keeps_editor_usable_for_later_edits()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-noop-then-edit-source.xlsx");
    const std::filesystem::path noop_output =
        artifact("fastxlsx-workbook-editor-noop-then-edit-copy.xlsx");
    const std::filesystem::path edited_output =
        artifact("fastxlsx-workbook-editor-noop-then-edit-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.save_as(noop_output);
    check(!editor.has_pending_changes(),
        "no-op save_as should keep the editor clean for later edits");
    check(!editor.last_edit_error().has_value(),
        "no-op save_as should not create last_edit_error before later edits");

    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::text("after noop"),
            fastxlsx::CellValue::number(42.0)}});
    editor.rename_sheet("Data", "AfterNoop");

    check(editor.has_pending_changes(),
        "editor should remain usable for edits after no-op save_as");
    check(editor.pending_change_count() == 2,
        "replacement plus rename after no-op save_as should be queued");
    check(editor.pending_replacement_cell_count() == 2,
        "replacement after no-op save_as should expose pending cells");
    check(editor.has_worksheet("AfterNoop"),
        "planned catalog should expose rename after no-op save_as");
    check(editor.has_source_worksheet("Data"),
        "source catalog should remain available after no-op save_as");

    editor.save_as(edited_output);

    const auto noop_entries = fastxlsx::test::read_zip_entries(noop_output);
    check(noop_entries == source_entries,
        "later edits should not mutate the prior no-op save_as output");

    const auto edited_entries = fastxlsx::test::read_zip_entries(edited_output);
    check_contains(edited_entries.at("xl/workbook.xml"), R"(name="AfterNoop")",
        "save_as after no-op save_as should write the later queued rename");
    check_contains(edited_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1" t="inlineStr"><is><t>after noop</t></is></c>)",
        "save_as after no-op save_as should write the later queued text cell");
    check_contains(edited_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="B1"><v>42</v></c>)",
        "save_as after no-op save_as should write the later queued number cell");
}

void test_failed_save_as_preserves_public_facade_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-save-failure-state-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-save-failure-state-output.xlsx");
    const std::filesystem::path missing_parent_output =
        artifact("fastxlsx-workbook-editor-save-failure-missing-parent") / "output.xlsx";
    const std::filesystem::path file_parent =
        artifact("fastxlsx-workbook-editor-save-failure-file-parent.bin");
    const std::filesystem::path file_parent_output = file_parent / "output.xlsx";
    const std::filesystem::path directory_output =
        artifact("fastxlsx-workbook-editor-save-failure-directory-output");
    write_binary_file(file_parent, "not a directory");
    std::filesystem::create_directories(directory_output);

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(11.0)}});
    editor.rename_sheet("Data", "RenamedData");

    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(99.0)}});
    }), "old source name replacement should fail after queued rename");
    const std::optional<std::string> last_error_before_save = editor.last_edit_error();
    check(last_error_before_save.has_value(),
        "pre-save failed edit should leave a public last_edit_error");

    const std::size_t pending_count_before_save = editor.pending_change_count();
    const std::size_t replacement_cells_before_save =
        editor.pending_replacement_cell_count();
    const std::size_t replacement_memory_before_save =
        editor.estimated_pending_replacement_memory_usage();
    const std::vector<std::string> pending_names_before_save =
        editor.pending_replacement_worksheet_names();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_before_save =
        editor.worksheet_catalog();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_save =
        editor.pending_worksheet_edits();

    check(threw_fastxlsx_error([&] { editor.save_as(source); }),
        "save_as over the source package should fail without committing output");
    check(threw_fastxlsx_error([&] { editor.save_as(std::filesystem::path{}); }),
        "save_as with an empty output path should fail without committing output");
    check(threw_fastxlsx_error([&] { editor.save_as(missing_parent_output); }),
        "save_as with a missing parent directory should fail without committing output");
    check(threw_fastxlsx_error([&] { editor.save_as(file_parent_output); }),
        "save_as with a non-directory parent should fail without committing output");
    check(threw_fastxlsx_error([&] { editor.save_as(directory_output); }),
        "save_as to an existing directory should fail without committing output");

    check(editor.pending_change_count() == pending_count_before_save,
        "failed save_as should preserve public pending change count");
    check(editor.pending_replacement_cell_count() == replacement_cells_before_save,
        "failed save_as should preserve pending replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage() ==
            replacement_memory_before_save,
        "failed save_as should preserve replacement memory estimate");
    check(editor.pending_replacement_worksheet_names() == pending_names_before_save,
        "failed save_as should preserve pending replacement worksheet names");
    check(editor.last_edit_error() == last_error_before_save,
        "failed save_as should not replace or clear last_edit_error");

    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_after_save =
        editor.worksheet_catalog();
    check(catalog_after_save.size() == catalog_before_save.size(),
        "failed save_as should preserve worksheet catalog size");
    if (catalog_after_save.size() == 2 && catalog_before_save.size() == 2) {
        check(catalog_after_save[0].source_name == catalog_before_save[0].source_name,
            "failed save_as should preserve catalog source name");
        check(catalog_after_save[0].planned_name == catalog_before_save[0].planned_name,
            "failed save_as should preserve catalog planned name");
        check(catalog_after_save[0].renamed == catalog_before_save[0].renamed,
            "failed save_as should preserve catalog rename flag");
    }

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_after_save =
        editor.pending_worksheet_edits();
    check(summaries_after_save.size() == summaries_before_save.size(),
        "failed save_as should preserve pending summary count");
    if (summaries_after_save.size() == 1 && summaries_before_save.size() == 1) {
        check(summaries_after_save[0].source_name == summaries_before_save[0].source_name,
            "failed save_as should preserve summary source name");
        check(summaries_after_save[0].planned_name == summaries_before_save[0].planned_name,
            "failed save_as should preserve summary planned name");
        check(summaries_after_save[0].renamed == summaries_before_save[0].renamed,
            "failed save_as should preserve summary rename flag");
        check(summaries_after_save[0].sheet_data_replaced ==
                summaries_before_save[0].sheet_data_replaced,
            "failed save_as should preserve summary replacement flag");
        check(summaries_after_save[0].replacement_cell_count ==
                summaries_before_save[0].replacement_cell_count,
            "failed save_as should preserve summary replacement cell count");
        check(summaries_after_save[0].estimated_replacement_memory_usage ==
                summaries_before_save[0].estimated_replacement_memory_usage,
            "failed save_as should preserve summary memory estimate");
    }

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedData")",
        "safe save_as after failed save should keep the queued rename");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>11</v></c>)",
        "safe save_as after failed save should keep the queued sheetData replacement");

    const std::filesystem::path clean_error_output =
        artifact("fastxlsx-workbook-editor-save-failure-clean-error-output.xlsx");
    fastxlsx::WorkbookEditor clean_error_editor = fastxlsx::WorkbookEditor::open(source);
    clean_error_editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(12.0)}});
    check(!clean_error_editor.last_edit_error().has_value(),
        "successful edit before save_as failure should leave last_edit_error empty");

    check(threw_fastxlsx_error([&] {
        clean_error_editor.save_as(std::filesystem::path{});
    }), "save_as failure should throw even when no prior edit failure exists");
    check(!clean_error_editor.last_edit_error().has_value(),
        "failed save_as should not create last_edit_error when none existed");

    clean_error_editor.save_as(clean_error_output);
    const auto clean_error_entries = fastxlsx::test::read_zip_entries(clean_error_output);
    check_contains(clean_error_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>12</v></c>)",
        "safe save_as after clean-error failed save should keep the queued replacement");
}

void test_successful_save_as_preserves_public_facade_state()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-success-save-state-source.xlsx");
    const std::filesystem::path first_output =
        artifact("fastxlsx-workbook-editor-success-save-state-first.xlsx");
    const std::filesystem::path second_output =
        artifact("fastxlsx-workbook-editor-success-save-state-second.xlsx");
    const std::filesystem::path third_output =
        artifact("fastxlsx-workbook-editor-success-save-state-third.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(21.0)}});
    editor.rename_sheet("Data", "SavedData");
    check(threw_fastxlsx_error([&] {
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(99.0)}});
    }), "old source name replacement should fail after queued rename before save");

    const std::size_t pending_count_before_save = editor.pending_change_count();
    const std::size_t replacement_cells_before_save =
        editor.pending_replacement_cell_count();
    const std::size_t replacement_memory_before_save =
        editor.estimated_pending_replacement_memory_usage();
    const std::vector<std::string> pending_names_before_save =
        editor.pending_replacement_worksheet_names();
    const std::optional<std::string> last_error_before_save = editor.last_edit_error();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_before_save =
        editor.worksheet_catalog();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_before_save =
        editor.pending_worksheet_edits();
    check(last_error_before_save.has_value(),
        "pre-save failed edit should leave last_edit_error for successful save_as state test");

    editor.save_as(first_output);

    check(editor.pending_change_count() == pending_count_before_save,
        "successful save_as should preserve public pending change count");
    check(editor.pending_replacement_cell_count() == replacement_cells_before_save,
        "successful save_as should preserve pending replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage() ==
            replacement_memory_before_save,
        "successful save_as should preserve replacement memory estimate");
    check(editor.pending_replacement_worksheet_names() == pending_names_before_save,
        "successful save_as should preserve pending replacement worksheet names");
    check(editor.last_edit_error() == last_error_before_save,
        "successful save_as should not replace or clear last_edit_error");

    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_after_save =
        editor.worksheet_catalog();
    check(catalog_after_save.size() == catalog_before_save.size(),
        "successful save_as should preserve worksheet catalog size");
    if (catalog_after_save.size() == 2 && catalog_before_save.size() == 2) {
        check(catalog_after_save[0].source_name == catalog_before_save[0].source_name,
            "successful save_as should preserve catalog source name");
        check(catalog_after_save[0].planned_name == catalog_before_save[0].planned_name,
            "successful save_as should preserve catalog planned name");
        check(catalog_after_save[0].renamed == catalog_before_save[0].renamed,
            "successful save_as should preserve catalog rename flag");
    }

    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries_after_save =
        editor.pending_worksheet_edits();
    check(summaries_after_save.size() == summaries_before_save.size(),
        "successful save_as should preserve pending summary count");
    if (summaries_after_save.size() == 1 && summaries_before_save.size() == 1) {
        check(summaries_after_save[0].source_name == summaries_before_save[0].source_name,
            "successful save_as should preserve summary source name");
        check(summaries_after_save[0].planned_name == summaries_before_save[0].planned_name,
            "successful save_as should preserve summary planned name");
        check(summaries_after_save[0].renamed == summaries_before_save[0].renamed,
            "successful save_as should preserve summary rename flag");
        check(summaries_after_save[0].sheet_data_replaced ==
                summaries_before_save[0].sheet_data_replaced,
            "successful save_as should preserve summary replacement flag");
        check(summaries_after_save[0].replacement_cell_count ==
                summaries_before_save[0].replacement_cell_count,
            "successful save_as should preserve summary replacement cell count");
        check(summaries_after_save[0].estimated_replacement_memory_usage ==
                summaries_before_save[0].estimated_replacement_memory_usage,
            "successful save_as should preserve summary memory estimate");
    }

    const auto first_entries = fastxlsx::test::read_zip_entries(first_output);
    check_contains(first_entries.at("xl/workbook.xml"), R"(name="SavedData")",
        "successful save_as should write the queued rename");
    check_contains(first_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>21</v></c>)",
        "successful save_as should write the queued replacement");

    editor.save_as(second_output);
    const auto second_entries = fastxlsx::test::read_zip_entries(second_output);
    check(second_entries == first_entries,
        "second save_as without new edits should reuse the preserved pending state");

    editor.replace_sheet_data("SavedData", {{fastxlsx::CellValue::number(31.0)}});
    check(editor.pending_change_count() == pending_count_before_save + 1,
        "follow-up edit after successful save_as should add another pending change");
    check(!editor.last_edit_error().has_value(),
        "follow-up successful edit after save_as should clear the prior edit error");

    editor.save_as(third_output);
    const auto third_entries = fastxlsx::test::read_zip_entries(third_output);
    check_contains(third_entries.at("xl/workbook.xml"), R"(name="SavedData")",
        "follow-up save_as should keep the queued rename");
    check_contains(third_entries.at("xl/worksheets/sheet1.xml"),
        R"(<c r="A1"><v>31</v></c>)",
        "follow-up save_as should write the later replacement");
    check_not_contains(third_entries.at("xl/worksheets/sheet1.xml"), R"(<v>21</v>)",
        "follow-up save_as should drop the prior replacement payload");

    const auto first_entries_after_follow_up =
        fastxlsx::test::read_zip_entries(first_output);
    check(first_entries_after_follow_up == first_entries,
        "follow-up edits should not mutate the earlier successful save_as output");
}

void test_empty_rows_emit_empty_sheet_data()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-empty-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-empty-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, "<sheetData></sheetData>",
        "empty rows should emit an empty sheetData");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "empty replacement should drop old placeholder data");
}

void test_text_uses_inline_strings_and_preserves_shared_strings()
{
    // Build a shared-string source so we can prove the table is preserved, not
    // migrated, when the replacement uses inline strings.
    const std::filesystem::path source =
        artifact("fastxlsx-workbook-editor-shared-source.xlsx");
    {
        fastxlsx::WorkbookWriterOptions options;
        options.string_strategy = fastxlsx::StringStrategy::SharedString;
        fastxlsx::WorkbookWriter writer =
            fastxlsx::WorkbookWriter::create(source, options);
        fastxlsx::WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({fastxlsx::CellView::text("shared-placeholder")});
        writer.close();
    }

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(source_entries.find("xl/sharedStrings.xml") != source_entries.end(),
        "shared-string source should emit a sharedStrings part");
    const std::string shared_strings_before =
        source_entries.at("xl/sharedStrings.xml");

    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-shared-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::text("inline-text")}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(t="inlineStr")",
        "replacement text should be written as an inline string");
    check_contains(worksheet_xml, "inline-text",
        "replacement text should appear in the worksheet");

    check(output_entries.find("xl/sharedStrings.xml") != output_entries.end()
            && output_entries.at("xl/sharedStrings.xml") == shared_strings_before,
        "existing sharedStrings should be preserved, not migrated or pruned");
}

void test_calc_metadata_requests_recalculation_without_inventing_calcchain()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-calc-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-calc-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    check(source_entries.find("xl/calcChain.xml") == source_entries.end(),
        "streaming writer source should not carry a calcChain");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::formula("SUM(A1:A1)")}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(fullCalcOnLoad="1")",
        "data edit should request workbook recalculation on load");

    check(output_entries.find("xl/calcChain.xml") == output_entries.end(),
        "editor should not invent a calcChain when the source has none");
}

void test_rename_sheet_changes_catalog_name_and_preserves_parts()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string data_sheet_before = source_entries.at("xl/worksheets/sheet1.xml");
    const std::string untouched_sheet_before = source_entries.at("xl/worksheets/sheet2.xml");
    const std::string content_types_before = source_entries.at("[Content_Types].xml");
    const std::string package_rels_before = source_entries.at("_rels/.rels");
    const std::string workbook_rels_before = source_entries.at("xl/_rels/workbook.xml.rels");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "Renamed & Data");

    const std::vector<std::string> names = editor.worksheet_names();
    check(names.size() == 2 && names[0] == "Renamed & Data",
        "planned workbook view should expose the queued rename");
    check(names.size() == 2 && names[1] == "Untouched",
        "planned workbook view should keep the untouched sheet name after rename");
    check(!editor.has_worksheet("Data"),
        "planned workbook view should not expose the original sheet name after rename");
    check(editor.has_worksheet("Renamed & Data"),
        "planned workbook view should expose the queued rename before save");
    const std::vector<std::string> source_names = editor.source_worksheet_names();
    check(source_names.size() == 2 && source_names[0] == "Data",
        "source workbook view should stay on the original catalog after rename");
    check(source_names.size() == 2 && source_names[1] == "Untouched",
        "source workbook view should keep the untouched sheet name after rename");
    check(editor.has_source_worksheet("Data"),
        "source workbook view should still expose the original sheet name after rename");
    check(!editor.has_source_worksheet("Renamed & Data"),
        "source workbook view should not expose the planned rename before save");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "worksheet_catalog should keep both sheets after queued rename");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data",
                "worksheet_catalog should keep the source name for renamed sheet");
            check(catalog[0].planned_name == "Renamed & Data",
                "worksheet_catalog should expose the queued planned name");
            check(catalog[0].renamed,
                "worksheet_catalog should mark the queued rename");
            check(catalog[1].source_name == "Untouched",
                "worksheet_catalog should keep the untouched source name");
            check(catalog[1].planned_name == "Untouched",
                "worksheet_catalog should keep the untouched planned name");
            check(!catalog[1].renamed,
                "worksheet_catalog should not mark untouched sheet as renamed");
        }
    }

    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Renamed &amp; Data")",
        "rename should XML-escape the new sheet name in the output catalog");
    check_not_contains(workbook_xml, R"(name="Data")",
        "rename should drop the old sheet name from the output catalog");

    check(output_entries.at("xl/worksheets/sheet1.xml") == data_sheet_before,
        "rename should preserve the renamed sheet's worksheet bytes");
    check(output_entries.at("xl/worksheets/sheet2.xml") == untouched_sheet_before,
        "rename should preserve untouched worksheet bytes");
    check(output_entries.at("[Content_Types].xml") == content_types_before,
        "rename should preserve content types bytes");
    check(output_entries.at("_rels/.rels") == package_rels_before,
        "rename should preserve package relationships bytes");
    check(output_entries.at("xl/_rels/workbook.xml.rels") == workbook_rels_before,
        "rename should preserve workbook relationships bytes");

    // Reopening the output package confirms the new catalog name is the one a
    // reader sees, and the other sheet is unchanged.
    fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
    const std::vector<std::string> reopened_names = reopened.worksheet_names();
    check(reopened_names.size() == 2 && reopened_names[0] == "Renamed & Data",
        "reopened output should expose the renamed sheet in catalog order");
    check(reopened_names.size() == 2 && reopened_names[1] == "Untouched",
        "reopened output should keep the untouched sheet name");
    check(!reopened.has_worksheet("Data"),
        "reopened output should no longer expose the old sheet name");
}

void test_replace_sheet_data_uses_planned_catalog_after_rename()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-planned-catalog-source.xlsx");

    {
        const std::filesystem::path rename_only_output =
            artifact("fastxlsx-workbook-editor-planned-catalog-rename-only-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.rename_sheet("Data", "RenamedOnly");

        check(threw_fastxlsx_error([&] {
            editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(9.0)}});
        }), "replace_sheet_data should reject the old source name after a planned rename");
        check(editor.pending_change_count() == 1,
            "old-name replace failure should not add a public pending change after rename");
        check(editor.pending_replacement_cell_count() == 0,
            "old-name replace failure should not add replacement cells after rename");
        check(editor.estimated_pending_replacement_memory_usage() == 0,
            "old-name replace failure should not add replacement memory after rename");
        check(!editor.has_worksheet("Data"),
            "planned workbook inspection should reject the old name after planned rename");
        check(editor.has_worksheet("RenamedOnly"),
            "planned workbook inspection should expose the planned name before save");
        check(editor.has_source_worksheet("Data"),
            "source workbook inspection should still expose the old name after planned rename");
        check(!editor.has_source_worksheet("RenamedOnly"),
            "source workbook inspection should not expose the planned name before save");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
                editor.worksheet_catalog();
            check(catalog.size() == 2,
                "worksheet_catalog should remain available after old-name replacement failure");
            if (catalog.size() == 2) {
                check(catalog[0].source_name == "Data",
                    "worksheet_catalog should keep source name after old-name failure");
                check(catalog[0].planned_name == "RenamedOnly",
                    "worksheet_catalog should keep planned name after old-name failure");
                check(catalog[0].renamed,
                    "worksheet_catalog should keep rename flag after old-name failure");
            }
        }

        editor.save_as(rename_only_output);
        const auto rename_only_entries = fastxlsx::test::read_zip_entries(rename_only_output);
        check_contains(rename_only_entries.at("xl/workbook.xml"), R"(name="RenamedOnly")",
            "old-name replace failure should preserve the queued catalog rename");
        check_contains(rename_only_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
            "old-name replace failure should preserve the source sheetData");
        check_not_contains(rename_only_entries.at("xl/worksheets/sheet1.xml"), R"(<v>9</v>)",
            "old-name replace failure should not leak rejected replacement cells");
    }

    {
        const std::filesystem::path output =
            artifact("fastxlsx-workbook-editor-planned-catalog-output.xlsx");

        fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
        editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(1.0)}});
        check(editor.pending_replacement_cell_count() == 1,
            "initial replacement should record one pending replacement cell");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
                editor.worksheet_catalog();
            check(catalog.size() == 2 && catalog[0].source_name == "Data" &&
                    catalog[0].planned_name == "Data" && !catalog[0].renamed,
                "replacement-only edit should not change worksheet_catalog");
        }

        editor.rename_sheet("Data", "RenamedData");
        check(editor.pending_change_count() == 2,
            "rename after replacement should add one public pending change");
        check(editor.pending_replacement_cell_count() == 1,
            "rename should migrate the pending replacement diagnostic to the planned name");
        {
            const std::vector<std::string> pending_names =
                editor.pending_replacement_worksheet_names();
            check(pending_names.size() == 1 && pending_names[0] == "RenamedData",
                "rename should migrate pending replacement names to the planned sheet name");
            check(editor.has_pending_replacement("RenamedData"),
                "rename should mark the planned sheet name as pending replacement");
            check(!editor.has_pending_replacement("Data"),
                "rename should stop reporting the old source name as pending replacement");
        }
        check(editor.estimated_pending_replacement_memory_usage() > 0,
            "rename should preserve the pending replacement memory diagnostic");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
                editor.worksheet_catalog();
            check(catalog.size() == 2 && catalog[0].source_name == "Data" &&
                    catalog[0].planned_name == "RenamedData" && catalog[0].renamed,
                "worksheet_catalog should report replace+rename planned catalog state");
        }

        check(threw_fastxlsx_error([&] {
            editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(9.0)}});
        }), "replace_sheet_data should keep resolving through the planned catalog");
        check(editor.pending_change_count() == 2,
            "old-name replace failure should preserve prior replace+rename count");
        check(editor.pending_replacement_cell_count() == 1,
            "old-name replace failure should preserve the prior replacement diagnostic");

        editor.replace_sheet_data("RenamedData",
            {{fastxlsx::CellValue::number(2.0), fastxlsx::CellValue::number(3.0)}});
        check(editor.pending_change_count() == 3,
            "new planned-name replacement should add one public pending change");
        check(editor.pending_replacement_cell_count() == 2,
            "new planned-name replacement should overwrite the stale pre-rename diagnostic");
        {
            const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
                editor.worksheet_catalog();
            check(catalog.size() == 2 && catalog[0].source_name == "Data" &&
                    catalog[0].planned_name == "RenamedData" && catalog[0].renamed,
                "planned-name replacement should not change worksheet_catalog rename mapping");
        }
        {
            const std::vector<std::string> pending_names =
                editor.pending_replacement_worksheet_names();
            check(pending_names.size() == 1 && pending_names[0] == "RenamedData",
                "planned-name replacement should keep one pending replacement name");
        }

        editor.save_as(output);
        const auto output_entries = fastxlsx::test::read_zip_entries(output);
        check_contains(output_entries.at("xl/workbook.xml"), R"(name="RenamedData")",
            "planned-name replacement should preserve the queued rename in output");
        check_not_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
            "planned-name replacement should drop the old catalog name in output");

        const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
        check_contains(worksheet_xml, R"(<c r="A1"><v>2</v></c>)",
            "planned-name replacement should write the final A1 cell");
        check_contains(worksheet_xml, R"(<c r="B1"><v>3</v></c>)",
            "planned-name replacement should write the final B1 cell");
        check_not_contains(worksheet_xml, R"(<v>1</v>)",
            "planned-name replacement should drop the stale pre-rename payload");
        check_not_contains(worksheet_xml, R"(<v>9</v>)",
            "planned-name replacement should drop the rejected old-name payload");

        fastxlsx::WorkbookEditor reopened = fastxlsx::WorkbookEditor::open(output);
        check(reopened.has_worksheet("RenamedData"),
            "reopened output should expose the planned catalog name");
        check(!reopened.has_worksheet("Data"),
            "reopened output should not expose the old source name");
    }
}

void test_rename_back_to_source_name_restores_public_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-back-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-back-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(11.0)}});
    const std::size_t replacement_memory =
        editor.estimated_pending_replacement_memory_usage();
    check(replacement_memory > 0,
        "initial replacement before rename-back should record memory");

    editor.rename_sheet("Data", "Temporary");
    check(editor.has_worksheet("Temporary"),
        "first rename should expose the temporary planned name");
    check(!editor.has_worksheet("Data"),
        "first rename should hide the source name from the planned catalog");
    check(editor.has_pending_replacement("Temporary"),
        "first rename should migrate replacement diagnostics to the planned name");

    editor.rename_sheet("Temporary", "Data");
    check(editor.pending_change_count() == 3,
        "rename-back should count as a public edit without committing");
    check(editor.has_worksheet("Data"),
        "rename-back should restore the source name in the planned catalog");
    check(!editor.has_worksheet("Temporary"),
        "rename-back should remove the temporary planned name");
    check(editor.has_source_worksheet("Data"),
        "rename-back should not change the source catalog view");
    check(editor.pending_replacement_cell_count() == 1,
        "rename-back should preserve the queued replacement cell count");
    check(editor.estimated_pending_replacement_memory_usage() == replacement_memory,
        "rename-back should preserve the replacement memory diagnostic");
    {
        const std::vector<std::string> pending_names =
            editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "Data",
            "rename-back should migrate replacement diagnostics back to the source name");
        check(editor.has_pending_replacement("Data"),
            "rename-back should report the restored source name as data-replaced");
        check(!editor.has_pending_replacement("Temporary"),
            "rename-back should stop reporting the temporary planned name as data-replaced");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "rename-back should preserve the catalog entry count");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data" && catalog[0].planned_name == "Data",
                "rename-back should restore the source-to-planned mapping");
            check(!catalog[0].renamed,
                "rename-back should clear the public renamed flag");
        }
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "rename-back with replacement should keep one edit summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" && summaries[0].planned_name == "Data",
                "rename-back summary should restore source and planned names");
            check(!summaries[0].renamed,
                "rename-back summary should not remain marked as renamed");
            check(summaries[0].sheet_data_replaced,
                "rename-back summary should preserve sheetData replacement");
            check(summaries[0].replacement_cell_count == 1,
                "rename-back summary should preserve replacement cell count");
            check(summaries[0].estimated_replacement_memory_usage == replacement_memory,
                "rename-back summary should preserve replacement memory");
        }
    }

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "rename-back output should use the restored source name");
    check_not_contains(output_entries.at("xl/workbook.xml"), "Temporary",
        "rename-back output should not leak the transient planned name");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), R"(<c r="A1"><v>11</v></c>)",
        "rename-back output should preserve the queued replacement payload");
    check_not_contains(output_entries.at("xl/worksheets/sheet1.xml"), "placeholder-a1",
        "rename-back output should drop the old source sheetData");
}

void test_rename_chain_back_to_source_name_clears_rename_only_summary()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-chain-back-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-chain-back-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TemporaryA");
    editor.rename_sheet("TemporaryA", "TemporaryB");
    editor.rename_sheet("TemporaryB", "Data");

    check(editor.has_pending_changes(),
        "rename-only chain should still report queued public edit calls");
    check(editor.pending_change_count() == 3,
        "rename-only chain back to source should count each successful public edit");
    check(editor.has_worksheet("Data"),
        "rename-only chain back should restore the source name in planned inspection");
    check(!editor.has_worksheet("TemporaryA"),
        "rename-only chain back should remove the first transient planned name");
    check(!editor.has_worksheet("TemporaryB"),
        "rename-only chain back should remove the second transient planned name");
    check(editor.pending_replacement_cell_count() == 0,
        "rename-only chain back should not create replacement diagnostics");
    check(editor.estimated_pending_replacement_memory_usage() == 0,
        "rename-only chain back should not create replacement memory diagnostics");
    check(editor.pending_replacement_worksheet_names().empty(),
        "rename-only chain back should not report replacement sheet names");
    check(editor.pending_worksheet_edits().empty(),
        "rename-only chain back to the source name should clear rename-only summaries");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "rename-only chain back should preserve catalog entry count");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data" && catalog[0].planned_name == "Data",
                "rename-only chain back should restore source-to-planned mapping");
            check(!catalog[0].renamed,
                "rename-only chain back should clear the public renamed flag");
        }
    }

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Untouched"); }),
        "failed rename after chain-back should throw FastXlsxError");
    {
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "failed rename after chain-back should record last_edit_error");
        if (last_error.has_value()) {
            check_contains(*last_error, "Data",
                "failed rename after chain-back diagnostic should include restored source name");
            check_contains(*last_error, "Untouched",
                "failed rename after chain-back diagnostic should include rejected target name");
        }
    }
    check(editor.pending_change_count() == 3,
        "failed rename after chain-back should not add a public pending change");
    check(editor.pending_worksheet_edits().empty(),
        "failed rename after chain-back should preserve empty rename-only summaries");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == 2,
            "failed rename after chain-back should preserve catalog entry count");
        if (catalog.size() == 2) {
            check(catalog[0].source_name == "Data" && catalog[0].planned_name == "Data",
                "failed rename after chain-back should preserve restored catalog mapping");
            check(!catalog[0].renamed,
                "failed rename after chain-back should preserve cleared renamed flag");
        }
    }

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "rename-only chain back output should use the restored source name");
    check_not_contains(workbook_xml, "TemporaryA",
        "rename-only chain back output should not leak the first transient name");
    check_not_contains(workbook_xml, "TemporaryB",
        "rename-only chain back output should not leak the second transient name");
}

void test_replacement_after_rename_chain_back_failure_uses_restored_name()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-chain-replace-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-chain-replace-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.rename_sheet("Data", "TemporaryA");
    editor.rename_sheet("TemporaryA", "TemporaryB");
    editor.rename_sheet("TemporaryB", "Data");

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Untouched"); }),
        "duplicate rename after chain-back should throw FastXlsxError");
    check(editor.last_edit_error().has_value(),
        "duplicate rename after chain-back should leave last_edit_error");
    check(editor.pending_worksheet_edits().empty(),
        "duplicate rename after chain-back should preserve empty summaries");

    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::text("after chain-back failure"),
            fastxlsx::CellValue::number(31.0)}});

    check(!editor.last_edit_error().has_value(),
        "successful replacement after chain-back failure should clear last_edit_error");
    check(editor.pending_change_count() == 4,
        "replacement after three successful renames should add one public edit call");
    check(editor.pending_replacement_cell_count() == 2,
        "replacement after chain-back failure should record final replacement cells");
    {
        const std::vector<std::string> pending_names =
            editor.pending_replacement_worksheet_names();
        check(pending_names.size() == 1 && pending_names[0] == "Data",
            "replacement after chain-back failure should use the restored source name");
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == 1,
            "replacement after chain-back failure should create one summary");
        if (summaries.size() == 1) {
            check(summaries[0].source_name == "Data" && summaries[0].planned_name == "Data",
                "replacement after chain-back failure should keep restored catalog names");
            check(!summaries[0].renamed,
                "replacement after chain-back failure should not reintroduce rename flag");
            check(summaries[0].sheet_data_replaced,
                "replacement after chain-back failure should report sheetData replacement");
            check(summaries[0].replacement_cell_count == 2,
                "replacement after chain-back failure should report replacement cell count");
        }
    }

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    const std::string workbook_xml = output_entries.at("xl/workbook.xml");
    check_contains(workbook_xml, R"(name="Data")",
        "replacement after chain-back failure output should keep restored source name");
    check_not_contains(workbook_xml, "TemporaryA",
        "replacement after chain-back failure output should not leak first transient name");
    check_not_contains(workbook_xml, "TemporaryB",
        "replacement after chain-back failure output should not leak second transient name");

    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml,
        R"(<c r="A1" t="inlineStr"><is><t>after chain-back failure</t></is></c>)",
        "replacement after chain-back failure should write final text cell");
    check_contains(worksheet_xml, R"(<c r="B1"><v>31</v></c>)",
        "replacement after chain-back failure should write final number cell");
    check_not_contains(worksheet_xml, "placeholder-a1",
        "replacement after chain-back failure should remove old sheetData");
}

void test_failed_rename_preserves_pending_replacement_diagnostics()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-failure-diagnostics-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-failure-diagnostics-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(1.0)}});

    const std::size_t memory_after_initial_replacement =
        editor.estimated_pending_replacement_memory_usage();
    const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summary_after_initial_replacement =
        editor.pending_worksheet_edits();
    const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog_after_initial_replacement =
        editor.worksheet_catalog();
    check(editor.pending_change_count() == 1,
        "initial replacement before failed rename should add one public pending change");
    check(editor.pending_replacement_cell_count() == 1,
        "initial replacement before failed rename should record one cell");
    check(memory_after_initial_replacement > 0,
        "initial replacement before failed rename should record memory");

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Untouched"); }),
        "duplicate rename after a replacement should throw FastXlsxError");
    {
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "duplicate rename failure should record a public last_edit_error");
        if (last_error.has_value()) {
            check_contains(*last_error, "Data",
                "duplicate rename last_edit_error should include the source sheet");
            check_contains(*last_error, "Untouched",
                "duplicate rename last_edit_error should include the rejected target sheet");
        }
    }
    check(editor.pending_change_count() == 1,
        "duplicate rename failure should not add a public pending change");
    check(editor.pending_replacement_cell_count() == 1,
        "duplicate rename failure should preserve the old-name replacement diagnostic");
    check(editor.estimated_pending_replacement_memory_usage() == memory_after_initial_replacement,
        "duplicate rename failure should preserve the replacement memory diagnostic");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == catalog_after_initial_replacement.size(),
            "duplicate rename failure should preserve catalog entry count");
        if (catalog.size() == 2 && catalog_after_initial_replacement.size() == 2) {
            check(catalog[0].source_name == catalog_after_initial_replacement[0].source_name,
                "duplicate rename failure should preserve catalog source name");
            check(catalog[0].planned_name == catalog_after_initial_replacement[0].planned_name,
                "duplicate rename failure should preserve catalog planned name");
            check(catalog[0].renamed == catalog_after_initial_replacement[0].renamed,
                "duplicate rename failure should preserve catalog rename flag");
        }
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == summary_after_initial_replacement.size(),
            "duplicate rename failure should preserve summary count");
        if (summaries.size() == 1 && summary_after_initial_replacement.size() == 1) {
            check(summaries[0].source_name == summary_after_initial_replacement[0].source_name,
                "duplicate rename failure should preserve summary source name");
            check(summaries[0].planned_name == summary_after_initial_replacement[0].planned_name,
                "duplicate rename failure should preserve summary planned name");
            check(summaries[0].renamed == summary_after_initial_replacement[0].renamed,
                "duplicate rename failure should preserve summary rename flag");
            check(summaries[0].sheet_data_replaced ==
                    summary_after_initial_replacement[0].sheet_data_replaced,
                "duplicate rename failure should preserve summary replacement flag");
            check(summaries[0].replacement_cell_count ==
                    summary_after_initial_replacement[0].replacement_cell_count,
                "duplicate rename failure should preserve summary cell count");
            check(summaries[0].estimated_replacement_memory_usage ==
                    summary_after_initial_replacement[0].estimated_replacement_memory_usage,
                "duplicate rename failure should preserve summary memory estimate");
        }
    }

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Bad/Name"); }),
        "invalid rename after a replacement should throw FastXlsxError");
    {
        const std::optional<std::string> last_error = editor.last_edit_error();
        check(last_error.has_value(),
            "invalid rename failure should record a public last_edit_error");
        if (last_error.has_value()) {
            check_contains(*last_error, "Data",
                "invalid rename last_edit_error should include the source sheet");
            check_contains(*last_error, "Bad/Name",
                "invalid rename last_edit_error should include the rejected target sheet");
            check_not_contains(*last_error, "Untouched",
                "invalid rename last_edit_error should replace the previous duplicate diagnostic");
        }
    }
    check(editor.pending_change_count() == 1,
        "invalid rename failure should not add a public pending change");
    check(editor.pending_replacement_cell_count() == 1,
        "invalid rename failure should preserve the pending replacement diagnostic");
    check(editor.estimated_pending_replacement_memory_usage() == memory_after_initial_replacement,
        "invalid rename failure should preserve replacement memory");
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetCatalogEntry> catalog =
            editor.worksheet_catalog();
        check(catalog.size() == catalog_after_initial_replacement.size(),
            "invalid rename failure should preserve catalog entry count");
        if (catalog.size() == 2 && catalog_after_initial_replacement.size() == 2) {
            check(catalog[0].source_name == catalog_after_initial_replacement[0].source_name,
                "invalid rename failure should preserve catalog source name");
            check(catalog[0].planned_name == catalog_after_initial_replacement[0].planned_name,
                "invalid rename failure should preserve catalog planned name");
            check(catalog[0].renamed == catalog_after_initial_replacement[0].renamed,
                "invalid rename failure should preserve catalog rename flag");
        }
    }
    {
        const std::vector<fastxlsx::WorkbookEditorWorksheetEditSummary> summaries =
            editor.pending_worksheet_edits();
        check(summaries.size() == summary_after_initial_replacement.size(),
            "invalid rename failure should preserve summary count");
        if (summaries.size() == 1 && summary_after_initial_replacement.size() == 1) {
            check(summaries[0].source_name == summary_after_initial_replacement[0].source_name,
                "invalid rename failure should preserve summary source name");
            check(summaries[0].planned_name == summary_after_initial_replacement[0].planned_name,
                "invalid rename failure should preserve summary planned name");
            check(summaries[0].renamed == summary_after_initial_replacement[0].renamed,
                "invalid rename failure should preserve summary rename flag");
            check(summaries[0].sheet_data_replaced ==
                    summary_after_initial_replacement[0].sheet_data_replaced,
                "invalid rename failure should preserve summary replacement flag");
            check(summaries[0].replacement_cell_count ==
                    summary_after_initial_replacement[0].replacement_cell_count,
                "invalid rename failure should preserve summary cell count");
            check(summaries[0].estimated_replacement_memory_usage ==
                    summary_after_initial_replacement[0].estimated_replacement_memory_usage,
                "invalid rename failure should preserve summary memory estimate");
        }
    }

    editor.replace_sheet_data("Data",
        {{fastxlsx::CellValue::number(4.0), fastxlsx::CellValue::number(5.0)}});
    check(editor.pending_change_count() == 2,
        "valid replacement after failed renames should add one pending change");
    check(editor.pending_replacement_cell_count() == 2,
        "valid replacement after failed renames should overwrite the stale payload");
    check(editor.estimated_pending_replacement_memory_usage() > 0,
        "valid replacement after failed renames should keep replacement memory visible");

    editor.save_as(output);
    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Data")",
        "failed renames should leave the original Data catalog name in output");
    check_not_contains(output_entries.at("xl/workbook.xml"), "Bad/Name",
        "failed invalid rename should not leak the rejected name into the output catalog");

    const std::string worksheet_xml = output_entries.at("xl/worksheets/sheet1.xml");
    check_contains(worksheet_xml, R"(<c r="A1"><v>4</v></c>)",
        "replacement after failed renames should write the final A1 cell");
    check_contains(worksheet_xml, R"(<c r="B1"><v>5</v></c>)",
        "replacement after failed renames should write the final B1 cell");
    check_not_contains(worksheet_xml, R"(<v>1</v>)",
        "replacement after failed renames should drop the stale pre-failure payload");
}

void test_docprops_are_preserved_through_patch()
{
    const std::filesystem::path source =
        write_two_sheet_source_with_document_properties("fastxlsx-workbook-editor-docprops-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-docprops-output.xlsx");

    const auto source_entries = fastxlsx::test::read_zip_entries(source);
    const std::string core_before = source_entries.at("docProps/core.xml");
    const std::string app_before = source_entries.at("docProps/app.xml");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);
    editor.replace_sheet_data("Data", {{fastxlsx::CellValue::number(123.0)}});
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check(output_entries.at("docProps/core.xml") == core_before,
        "patch save should preserve docProps/core.xml bytes");
    check(output_entries.at("docProps/app.xml") == app_before,
        "patch save should preserve docProps/app.xml bytes");
    check_contains(output_entries.at("xl/worksheets/sheet1.xml"), R"(<v>123</v>)",
        "patch save should still apply the requested workbook edit");
}

void test_rename_to_existing_name_throws_and_editor_stays_usable()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-dup-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-dup-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Untouched"); }),
        "renaming to an existing sheet name should throw FastXlsxError");
    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "untouched"); }),
        "renaming to an ASCII case-insensitive duplicate should throw FastXlsxError");

    // The editor must remain usable after a rejected rename.
    editor.rename_sheet("Data", "Renamed");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Renamed")",
        "editor should still apply a valid rename after a rejected one");
}

void test_rename_missing_sheet_throws_and_editor_stays_usable()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-missing-source.xlsx");
    const std::filesystem::path output =
        artifact("fastxlsx-workbook-editor-rename-missing-output.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Missing", "Renamed"); }),
        "renaming a missing sheet should throw FastXlsxError");

    // The editor must remain usable after a rejected rename.
    editor.rename_sheet("Data", "Renamed");
    editor.save_as(output);

    const auto output_entries = fastxlsx::test::read_zip_entries(output);
    check_contains(output_entries.at("xl/workbook.xml"), R"(name="Renamed")",
        "editor should still apply a valid rename after a missing-sheet rejection");
}

void test_rename_to_invalid_name_throws()
{
    const std::filesystem::path source =
        write_two_sheet_source("fastxlsx-workbook-editor-rename-invalid-source.xlsx");

    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(source);

    check(threw_fastxlsx_error([&] { editor.rename_sheet("Data", "Bad/Name"); }),
        "renaming to a sheet name with invalid characters should throw FastXlsxError");
}

} // namespace

int main()
{
    try {
        test_replaces_sheet_data_and_preserves_untouched_parts();
        test_replace_sheet_data_preserves_surrounding_worksheet_metadata();
        test_replace_sheet_data_writes_caller_style_ids_as_is();
        test_replace_sheet_data_distinguishes_blank_cells_from_missing_cells();
        test_worksheet_names_and_has_worksheet();
        test_moved_from_workbook_editor_operations_throw();
        test_moved_to_workbook_editor_preserves_pending_public_state();
        test_clean_moved_to_workbook_editor_preserves_noop_public_state();
        test_moved_to_workbook_editor_preserves_replacement_options();
        test_moved_to_workbook_editor_preserves_replacement_memory_budget();
        test_move_assigned_workbook_editor_replaces_target_with_pending_public_state();
        test_move_assigned_clean_workbook_editor_clears_dirty_target_state();
        test_move_assignment_revives_moved_from_target_workbook_editor();
        test_move_assignment_replaces_target_replacement_options();
        test_move_assignment_replaces_target_replacement_memory_budget();
        test_move_assignment_clears_target_replacement_options_when_source_is_default();
        test_move_assignment_from_moved_from_source_clears_dirty_target_state();
        test_internal_materialized_sessions_move_with_workbook_editor_impl();
        test_public_worksheet_editor_handles_invalidate_after_owner_move();
        test_public_worksheet_editor_handles_invalidate_after_move_assignment();
        test_public_worksheet_editor_set_cell_auto_flushes_on_save_as();
        test_public_try_worksheet_missing_returns_empty_and_preserves_diagnostics();
        test_public_try_worksheet_existing_handle_reads_mutates_and_saves();
        test_public_try_worksheet_reuses_options_and_blocks_replacement_mix();
        test_public_worksheet_editor_has_pending_changes_tracks_dirty_state();
        test_public_workbook_editor_pending_materialized_names_track_dirty_state();
        test_public_workbook_editor_pending_materialized_names_move_with_owner();
        test_public_worksheet_editor_get_cell_missing_and_blank_semantics();
        test_public_worksheet_editor_a1_overloads_read_mutate_and_save();
        test_public_worksheet_editor_a1_overloads_reject_invalid_references();
        test_public_worksheet_editor_sparse_cells_snapshot();
        test_public_worksheet_editor_sparse_cells_range_snapshot();
        test_public_worksheet_editor_erase_cell_auto_flushes_on_save_as();
        test_public_worksheet_editor_options_guard_failure_preserves_state();
        test_public_worksheet_editor_blocks_same_sheet_patch_operations();
        test_internal_materialized_session_assignment_from_moved_from_source_clears_target();
        test_internal_materialized_session_blocks_whole_sheet_replacement();
        test_internal_materialized_session_blocks_materialize_after_public_replacement();
        test_internal_materialized_session_blocks_materialize_after_renamed_public_replacement();
        test_internal_materialized_session_blocks_materialize_after_replacement_on_renamed_sheet();
        test_internal_materialized_session_flushes_dirty_projection_to_patch_plan();
        test_internal_materialized_session_blocks_same_sheet_rename();
        test_internal_materialized_session_flushes_after_rejected_public_operations();
        test_internal_materialized_session_flushes_after_other_sheet_edit_clears_rejected_error();
        test_internal_materialized_session_flushes_after_other_sheet_rename_clears_rejected_error();
        test_internal_materialized_session_rejected_public_operations_preserve_flushed_projection();
        test_internal_materialized_session_failed_save_as_preserves_dirty_and_flushed_state();
        test_internal_materialized_session_reflush_after_failed_save_as();
        test_internal_materialized_session_move_reflush_after_failed_save_as();
        test_internal_materialized_session_reflush_after_successful_save_as();
        test_internal_materialized_session_move_reflush_after_successful_save_as();
        test_internal_materialized_session_reflush_replaces_prior_projection();
        test_internal_materialized_session_flush_failure_preserves_dirty_state();
        test_internal_materialized_session_flush_uses_planned_name_after_rename();
        test_internal_materialized_session_blank_and_erase_projection();
        test_internal_materialized_session_repeated_materialize_preserves_dirty_state();
        test_internal_materialized_session_load_guard_failure_preserves_editor_state();
        test_internal_materialized_session_memory_guard_failure_preserves_editor_state();
        test_internal_materialized_session_missing_source_load_preserves_editor_state();
        test_pending_change_diagnostics_track_public_edits();
        test_last_edit_error_tracks_failed_public_edits();
        test_pending_worksheet_edit_summaries_track_public_facade_state();
        test_replace_sheet_data_source_read_failure_preserves_public_state();
        test_replace_sheet_data_source_xml_failures_preserve_public_state();
        test_replacement_guardrails_and_payload_diagnostics();
        test_missing_sheet_throws_and_editor_stays_usable();
        test_save_as_over_source_throws();
        test_noop_save_as_preserves_source_package_entries();
        test_noop_save_as_preserves_failed_edit_diagnostic();
        test_noop_save_as_preserves_failed_rename_diagnostic();
        test_noop_save_as_keeps_editor_usable_for_later_edits();
        test_failed_save_as_preserves_public_facade_state();
        test_successful_save_as_preserves_public_facade_state();
        test_empty_rows_emit_empty_sheet_data();
        test_text_uses_inline_strings_and_preserves_shared_strings();
        test_calc_metadata_requests_recalculation_without_inventing_calcchain();
        test_rename_sheet_changes_catalog_name_and_preserves_parts();
        test_replace_sheet_data_uses_planned_catalog_after_rename();
        test_rename_back_to_source_name_restores_public_diagnostics();
        test_rename_chain_back_to_source_name_clears_rename_only_summary();
        test_replacement_after_rename_chain_back_failure_uses_restored_name();
        test_failed_rename_preserves_pending_replacement_diagnostics();
        test_docprops_are_preserved_through_patch();
        test_rename_to_existing_name_throws_and_editor_stays_usable();
        test_rename_missing_sheet_throws_and_editor_stays_usable();
        test_rename_to_invalid_name_throws();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "UNEXPECTED EXCEPTION: %s\n", error.what());
        return 1;
    }

    if (g_failures != 0) {
        std::fprintf(stderr, "%d WorkbookEditor check(s) failed\n", g_failures);
        return 1;
    }

    std::printf("All WorkbookEditor tests passed\n");
    return 0;
}
