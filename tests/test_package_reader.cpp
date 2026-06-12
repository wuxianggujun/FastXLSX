#include "../src/package_reader.hpp"
#include "../src/package_writer.hpp"
#include "../src/zip_store_writer.hpp"
#include "zip_test_utils.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#endif

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

void check_contains(std::string_view haystack, std::string_view needle, const char* message)
{
    if (haystack.find(needle) == std::string_view::npos) {
        throw TestFailure(message);
    }
}

std::filesystem::path output_path(std::string_view name)
{
    return fastxlsx::test::artifact_path(name);
}

void write_file(const std::filesystem::path& path, std::string_view data)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw TestFailure("failed to open test package for writing");
    }
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!stream) {
        throw TestFailure("failed to write test package");
    }
}

void create_sparse_file_with_size(const std::filesystem::path& path, std::uint64_t size)
{
#ifdef _WIN32
    const auto native_path = path.native();
    HANDLE file = CreateFileW(native_path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        throw TestFailure("failed to create sparse test file");
    }

    DWORD bytes_returned = 0;
    if (!DeviceIoControl(
            file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &bytes_returned, nullptr)) {
        CloseHandle(file);
        throw TestFailure("failed to mark sparse test file");
    }

    LARGE_INTEGER end_position {};
    end_position.QuadPart = static_cast<LONGLONG>(size);
    if (!SetFilePointerEx(file, end_position, nullptr, FILE_BEGIN)
        || !SetEndOfFile(file)) {
        CloseHandle(file);
        throw TestFailure("failed to size sparse test file");
    }

    if (!CloseHandle(file)) {
        throw TestFailure("failed to close sparse test file");
    }
#else
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw TestFailure("failed to create sparse test file");
    }
    stream.close();

    std::error_code error;
    std::filesystem::resize_file(path, size, error);
    if (error) {
        throw TestFailure("failed to size sparse test file");
    }
#endif

    std::error_code error;
    const std::uint64_t actual_size = std::filesystem::file_size(path, error);
    if (error || actual_size != size) {
        throw TestFailure("sparse test file size mismatch");
    }
}

void corrupt_first_occurrence(std::string& data, std::string_view needle)
{
    const std::size_t offset = data.find(needle);
    if (offset == std::string::npos) {
        throw TestFailure("test ZIP payload marker not found");
    }
    data[offset] = data[offset] == 'X' ? 'Y' : 'X';
}

void write_u16(std::string& data, std::size_t offset, std::uint16_t value)
{
    if (offset + 2 > data.size()) {
        throw TestFailure("test ZIP patch offset out of range");
    }
    data[offset] = static_cast<char>(value & 0xffu);
    data[offset + 1] = static_cast<char>((value >> 8u) & 0xffu);
}

void write_u32(std::string& data, std::size_t offset, std::uint32_t value)
{
    if (offset + 4 > data.size()) {
        throw TestFailure("test ZIP patch offset out of range");
    }
    data[offset] = static_cast<char>(value & 0xffu);
    data[offset + 1] = static_cast<char>((value >> 8u) & 0xffu);
    data[offset + 2] = static_cast<char>((value >> 16u) & 0xffu);
    data[offset + 3] = static_cast<char>((value >> 24u) & 0xffu);
}

std::size_t find_signature(const std::string& data, std::uint32_t signature)
{
    for (std::size_t offset = 0; offset + 4 <= data.size(); ++offset) {
        if (fastxlsx::test::read_u32(data, offset) == signature) {
            return offset;
        }
    }
    throw TestFailure("test ZIP signature not found");
}

std::size_t find_end_of_central_directory(const std::string& data)
{
    if (data.size() < 22) {
        throw TestFailure("test ZIP package is too small");
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

    throw TestFailure("test ZIP end of central directory not found");
}

struct ZipEntryLocation {
    std::size_t central_offset = 0;
    std::size_t local_offset = 0;
    std::size_t data_offset = 0;
    std::uint16_t compression_method = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
};

ZipEntryLocation find_zip_entry_location(const std::string& data, std::string_view name)
{
    const std::size_t eocd_offset = find_end_of_central_directory(data);
    const std::uint16_t entry_count =
        fastxlsx::test::read_u16(data, eocd_offset + 10u);
    std::size_t offset = fastxlsx::test::read_u32(data, eocd_offset + 16u);

    for (std::uint16_t index = 0; index < entry_count; ++index) {
        if (offset + 46u > data.size()
            || fastxlsx::test::read_u32(data, offset) != 0x02014b50u) {
            throw TestFailure("test ZIP central directory entry is invalid");
        }

        const std::uint16_t name_size = fastxlsx::test::read_u16(data, offset + 28u);
        const std::uint16_t extra_size = fastxlsx::test::read_u16(data, offset + 30u);
        const std::uint16_t comment_size = fastxlsx::test::read_u16(data, offset + 32u);
        const std::size_t record_size = 46u + name_size + extra_size + comment_size;
        if (offset + record_size > data.size()) {
            throw TestFailure("test ZIP central directory entry is truncated");
        }

        const std::string entry_name = data.substr(offset + 46u, name_size);
        if (entry_name == name) {
            const std::size_t local_offset =
                fastxlsx::test::read_u32(data, offset + 42u);
            if (local_offset + 30u > data.size()
                || fastxlsx::test::read_u32(data, local_offset) != 0x04034b50u) {
                throw TestFailure("test ZIP local header entry is invalid");
            }

            const std::uint16_t local_name_size =
                fastxlsx::test::read_u16(data, local_offset + 26u);
            const std::uint16_t local_extra_size =
                fastxlsx::test::read_u16(data, local_offset + 28u);
            const std::size_t data_offset =
                local_offset + 30u + local_name_size + local_extra_size;
            return ZipEntryLocation {
                offset,
                local_offset,
                data_offset,
                fastxlsx::test::read_u16(data, offset + 10u),
                fastxlsx::test::read_u32(data, offset + 20u),
                fastxlsx::test::read_u32(data, offset + 24u),
            };
        }

        offset += record_size;
    }

    throw TestFailure("test ZIP entry not found");
}

void expect_open_failure(const std::filesystem::path& path, const char* message)
{
    bool failed = false;
    try {
        (void)fastxlsx::detail::PackageReader::open(path);
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, message);
}

void expect_workbook_sheets_failure(const std::filesystem::path& path, const char* message)
{
    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    bool failed = false;
    try {
        (void)reader.workbook_sheets();
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, message);
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG

void test_package_reader_reads_deflated_entries_with_minizip()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-deflated.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
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
    const std::string opaque_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdOpaqueExternal" Type="https://fastxlsx.invalid/relationships/opaque-audit" Target="https://example.invalid/opaque" TargetMode="External"/>)"
        R"(</Relationships>)";
    std::string unknown_body = "deflated-opaque";
    unknown_body.append(1, '\0');
    unknown_body += "payload";
    unknown_body.append(1, '\0');
    unknown_body += std::string(256, 'X');

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", "<worksheet><sheetData/></worksheet>"},
            {"custom/opaque.bin", unknown_body},
            {"custom/_rels/opaque.bin.rels", opaque_relationships},
        },
        {fastxlsx::detail::PackageWriterBackend::MinizipNg});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    const auto* content_types_entry = reader.find_entry("[Content_Types].xml");
    check(content_types_entry != nullptr,
        "DEFLATE package should expose [Content_Types].xml entry");
    check(content_types_entry->compression_method == 8,
        "minizip PackageReader fixture should use DEFLATE entry method");
    check(reader.read_entry("[Content_Types].xml") == content_types,
        "PackageReader should read deflated content types bytes");

    const auto* unknown_entry = reader.find_entry("custom/opaque.bin");
    check(unknown_entry != nullptr, "PackageReader should index deflated unknown entries");
    check(unknown_entry->compression_method == 8,
        "unknown minizip entry should use DEFLATE method");
    check(unknown_entry->uncompressed_size == unknown_body.size(),
        "deflated unknown entry should report uncompressed size");
    check(reader.read_entry("custom/opaque.bin") == unknown_body,
        "PackageReader should read decompressed unknown entry bytes");

    const fastxlsx::detail::PartName workbook_part("/xl/workbook.xml");
    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque.bin");
    check(reader.part_index().find_part(workbook_part) != nullptr,
        "DEFLATE PackageReader should build the workbook part index");
    const auto* opaque_manifest_part = reader.part_index().find_part(opaque_part);
    check(opaque_manifest_part != nullptr,
        "DEFLATE PackageReader should build unknown part index entries");
    check(opaque_manifest_part->content_type == "application/octet-stream",
        "DEFLATE PackageReader should resolve unknown part content type defaults");

    const auto* workbook_relationships_set = reader.relationships_for(workbook_part);
    check(workbook_relationships_set != nullptr,
        "DEFLATE PackageReader should ingest workbook relationships");
    check(workbook_relationships_set->find_by_id("rId1") != nullptr,
        "DEFLATE PackageReader should preserve workbook relationship ids");
    check(reader.relationships_for(worksheet_part) == nullptr,
        "DEFLATE PackageReader should not invent missing worksheet relationships");

    const auto* opaque_relationships_set = reader.relationships_for(opaque_part);
    check(opaque_relationships_set != nullptr,
        "DEFLATE PackageReader should ingest unknown extension owner relationships");
    const auto* opaque_external =
        opaque_relationships_set->find_by_id("rIdOpaqueExternal");
    check(opaque_external != nullptr,
        "DEFLATE PackageReader should preserve unknown owner relationship ids");
    check(opaque_external->target_mode
            == fastxlsx::detail::Relationship::TargetMode::External,
        "DEFLATE PackageReader should preserve external target mode");

    const fastxlsx::detail::RelationshipGraph graph = reader.relationship_graph();
    check(graph.relationships_for(opaque_part) != nullptr,
        "DEFLATE PackageReader relationship graph should attach unknown owner relationships");
}

void test_package_reader_streams_deflated_entry_chunks_with_minizip()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-deflated-entry-chunks.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    std::string unknown_body;
    for (int index = 0; index < 4096; ++index) {
        unknown_body += "deflated-entry-direct-chunk-source-";
        unknown_body += std::to_string(index);
        unknown_body += '\n';
    }

    fastxlsx::detail::PackageWriterOptions options;
    options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    options.compression_level = fastxlsx::detail::package_writer_max_compression_level;
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/deflated.bin", unknown_body},
        },
        options);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    const auto* deflated_entry = reader.find_entry("custom/deflated.bin");
    check(deflated_entry != nullptr,
        "DEFLATE chunk-source fixture should include target entry");
    check(deflated_entry->compression_method == 8,
        "DEFLATE chunk-source fixture should use method 8");

    fastxlsx::detail::PackageReaderChunkCallback source =
        reader.entry_chunk_source("custom/deflated.bin");

    std::string chunk;
    std::string streamed_body;
    std::size_t chunk_count = 0;
    while (source(chunk)) {
        check(!chunk.empty(), "DEFLATE chunk source should not emit empty chunks");
        streamed_body += chunk;
        ++chunk_count;
    }

    check(streamed_body == unknown_body,
        "PackageReader should stream decompressed DEFLATE entry bytes through chunk source");
    check(chunk_count > 1, "large DEFLATE entry should be delivered in multiple chunks");
}

void test_package_reader_closes_abandoned_deflated_entry_chunk_source()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-abandoned-deflated-entry-chunks.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    std::string unknown_body;
    for (int index = 0; index < 4096; ++index) {
        unknown_body += "abandoned-deflated-entry-direct-chunk-source-";
        unknown_body += std::to_string(index);
        unknown_body += '\n';
    }

    fastxlsx::detail::PackageWriterOptions options;
    options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    options.compression_level = fastxlsx::detail::package_writer_max_compression_level;
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/deflated.bin", unknown_body},
        },
        options);

    {
        const fastxlsx::detail::PackageReader reader =
            fastxlsx::detail::PackageReader::open(path);
        fastxlsx::detail::PackageReaderChunkCallback source =
            reader.entry_chunk_source("custom/deflated.bin");

        std::string chunk;
        check(source(chunk), "abandoned DEFLATE chunk source should read a first chunk");
        check(!chunk.empty(), "abandoned DEFLATE chunk source should emit bytes");
    }

    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    check(removed && !error,
        "abandoned DEFLATE chunk source should close the source package handle");
}

void test_package_reader_extracts_deflated_entry_to_file_with_minizip()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-deflated-entry-extract.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    std::string unknown_body;
    for (int index = 0; index < 4096; ++index) {
        unknown_body += "deflated-entry-file-backed-extract-";
        unknown_body += std::to_string(index);
        unknown_body += '\n';
    }

    fastxlsx::detail::PackageWriterOptions options;
    options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    options.compression_level = fastxlsx::detail::package_writer_max_compression_level;
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/deflated.bin", unknown_body},
        },
        options);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    const auto* deflated_entry = reader.find_entry("custom/deflated.bin");
    check(deflated_entry != nullptr,
        "DEFLATE extract fixture should include target entry");
    check(deflated_entry->compression_method == 8,
        "DEFLATE extract fixture should use method 8");

    const std::filesystem::path extracted =
        output_path("fastxlsx-package-reader-deflated-entry-extracted.bin");
    reader.extract_entry_to_file("custom/deflated.bin", extracted);

    check(fastxlsx::test::read_file(extracted) == unknown_body,
        "PackageReader should extract decompressed DEFLATE entry bytes to file");
}

void test_package_writer_applies_explicit_minizip_compression_levels()
{
    const std::filesystem::path fastest_path =
        output_path("fastxlsx-package-writer-compression-level-0.xlsx");
    const std::filesystem::path smallest_path =
        output_path("fastxlsx-package-writer-compression-level-9.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(</Types>)";
    std::string workbook = "<workbook><payload>";
    workbook.append(32768, 'A');
    workbook += "</payload></workbook>";

    fastxlsx::detail::PackageWriterOptions fastest;
    fastest.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    fastest.compression_level = fastxlsx::detail::package_writer_min_compression_level;
    fastxlsx::detail::write_package(fastest_path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", workbook},
        },
        fastest);

    fastxlsx::detail::PackageWriterOptions smallest;
    smallest.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    smallest.compression_level = fastxlsx::detail::package_writer_max_compression_level;
    fastxlsx::detail::write_package(smallest_path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", workbook},
        },
        smallest);

    const fastxlsx::detail::PackageReader fastest_reader =
        fastxlsx::detail::PackageReader::open(fastest_path);
    const fastxlsx::detail::PackageReader smallest_reader =
        fastxlsx::detail::PackageReader::open(smallest_path);
    check(fastest_reader.read_entry("xl/workbook.xml") == workbook,
        "compression level 0 output should preserve workbook bytes");
    check(smallest_reader.read_entry("xl/workbook.xml") == workbook,
        "compression level 9 output should preserve workbook bytes");

    const std::string fastest_data = fastxlsx::test::read_file(fastest_path);
    const std::string smallest_data = fastxlsx::test::read_file(smallest_path);
    const ZipEntryLocation fastest_entry =
        find_zip_entry_location(fastest_data, "xl/workbook.xml");
    const ZipEntryLocation smallest_entry =
        find_zip_entry_location(smallest_data, "xl/workbook.xml");
    check(fastest_entry.compression_method == 0,
        "compression level 0 minizip output should use stored/no-compression");
    check(smallest_entry.compression_method == 8,
        "compression level 9 minizip output should use DEFLATE");
    check(fastest_entry.uncompressed_size == workbook.size(),
        "compression level 0 central directory should record uncompressed size");
    check(smallest_entry.uncompressed_size == workbook.size(),
        "compression level 9 central directory should record uncompressed size");
    check(fastest_entry.compressed_size == fastest_entry.uncompressed_size,
        "compression level 0 central directory should record stored size");
    check(fastest_entry.compressed_size > smallest_entry.compressed_size,
        "higher compression level should shrink a repetitive workbook payload");
}

void test_package_reader_rejects_corrupt_deflated_entry_crc_on_read()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-deflated-crc-source.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    std::string unknown_body = "deflated-crc-target";
    unknown_body.append(512, 'Z');
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/blob.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::MinizipNg});

    std::string data = fastxlsx::test::read_file(source_path);
    const ZipEntryLocation blob = find_zip_entry_location(data, "custom/blob.bin");
    check(blob.compression_method == 8,
        "corrupt-deflate setup should target a DEFLATE entry");
    check(blob.compressed_size > 0,
        "corrupt-deflate setup should have compressed payload bytes");
    if (blob.data_offset + blob.compressed_size > data.size()) {
        throw TestFailure("test ZIP compressed payload is outside file bounds");
    }
    const std::size_t corrupt_offset = blob.data_offset + blob.compressed_size / 2u;
    data[corrupt_offset] = data[corrupt_offset] == 'X' ? 'Y' : 'X';

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-deflated-crc.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    bool failed = false;
    try {
        (void)reader.read_entry("custom/blob.bin");
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "PackageReader should reject corrupt DEFLATE entry bytes");
}

void test_package_reader_rejects_corrupt_deflated_entry_crc_on_extract()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-deflated-extract-crc-source.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    std::string unknown_body = "deflated-extract-crc-target";
    unknown_body.append(512, 'Z');
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/blob.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::MinizipNg});

    std::string data = fastxlsx::test::read_file(source_path);
    const ZipEntryLocation blob = find_zip_entry_location(data, "custom/blob.bin");
    check(blob.compression_method == 8,
        "corrupt-deflate extract setup should target a DEFLATE entry");
    check(blob.compressed_size > 0,
        "corrupt-deflate extract setup should have compressed payload bytes");
    if (blob.data_offset + blob.compressed_size > data.size()) {
        throw TestFailure("test ZIP compressed payload is outside file bounds");
    }
    const std::size_t corrupt_offset = blob.data_offset + blob.compressed_size / 2u;
    data[corrupt_offset] = data[corrupt_offset] == 'X' ? 'Y' : 'X';

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-deflated-extract-crc.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    bool failed = false;
    try {
        reader.extract_entry_to_file("custom/blob.bin",
            output_path("fastxlsx-package-reader-deflated-extract-crc-output.bin"));
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageReader should reject corrupt DEFLATE entry bytes during extract");
}

void test_package_reader_rejects_corrupt_deflated_entry_crc_on_chunk_source()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-deflated-chunks-crc-source.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(</Types>)";
    std::string unknown_body = "deflated-chunk-crc-target";
    unknown_body.append(512, 'Z');
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", content_types},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/blob.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::MinizipNg});

    std::string data = fastxlsx::test::read_file(source_path);
    const ZipEntryLocation blob = find_zip_entry_location(data, "custom/blob.bin");
    check(blob.compression_method == 8,
        "corrupt-deflate chunk-source setup should target a DEFLATE entry");
    check(blob.compressed_size > 0,
        "corrupt-deflate chunk-source setup should have compressed payload bytes");
    if (blob.data_offset + blob.compressed_size > data.size()) {
        throw TestFailure("test ZIP compressed payload is outside file bounds");
    }
    const std::size_t corrupt_offset = blob.data_offset + blob.compressed_size / 2u;
    data[corrupt_offset] = data[corrupt_offset] == 'X' ? 'Y' : 'X';

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-deflated-chunks-crc.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    fastxlsx::detail::PackageReaderChunkCallback source =
        reader.entry_chunk_source("custom/blob.bin");

    bool failed = false;
    try {
        std::string chunk;
        while (source(chunk)) {
        }
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed,
        "PackageReader should reject corrupt DEFLATE entry bytes during chunk source read");
}

#endif

void test_package_writer_rejects_empty_package_before_output()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-empty-package.xlsx");
    const std::string sentinel = "preserve existing empty-package output";
    write_file(path, sentinel);

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path, {},
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "empty ZIP package",
            "empty package failure should explain the missing entries");
    }

    check(failed, "PackageWriter should reject empty ZIP packages");
    check(fastxlsx::test::read_file(path) == sentinel,
        "empty package should fail before overwriting output");
}

void test_package_writer_rejects_invalid_compression_levels_before_output()
{
    auto check_invalid_level = [](int compression_level, std::string_view output_name) {
        const std::filesystem::path path = output_path(output_name);
        const std::string sentinel = "preserve existing invalid-compression output";
        write_file(path, sentinel);

        fastxlsx::detail::PackageWriterOptions options;
        options.backend = fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap;
        options.compression_level = compression_level;

        bool failed = false;
        try {
            fastxlsx::detail::write_package(path,
                {
                    {"xl/workbook.xml", "<workbook/>"},
                },
                options);
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "ZIP compression level",
                "invalid compression level failure should explain the bad option");
        }

        check(failed, "PackageWriter should reject invalid compression levels");
        check(fastxlsx::test::read_file(path) == sentinel,
            "invalid compression level should fail before overwriting output");
    };

    check_invalid_level(fastxlsx::detail::package_writer_default_compression_level - 1,
        "fastxlsx-package-writer-invalid-compression-low.xlsx");
    check_invalid_level(fastxlsx::detail::package_writer_max_compression_level + 1,
        "fastxlsx-package-writer-invalid-compression-high.xlsx");
}

void test_package_writer_rejects_zip64_entry_count_before_output()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-zip64-entry-count.xlsx");
    const std::string sentinel = "preserve existing zip64-entry-count output";
    write_file(path, sentinel);

    std::vector<fastxlsx::detail::PackageEntry> entries;
    entries.reserve(static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max()) + 1u);
    for (std::uint32_t index = 0;
         index <= static_cast<std::uint32_t>(std::numeric_limits<std::uint16_t>::max());
         ++index) {
        entries.emplace_back("xl/entry" + std::to_string(index) + ".xml", "");
    }

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path, entries,
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "Zip64",
            "entry-count failure should explain Zip64 requirement");
    }

    check(failed, "PackageWriter should reject ZIP32 entry count overflow");
    check(fastxlsx::test::read_file(path) == sentinel,
        "entry-count overflow should fail before overwriting output");
}

void test_package_writer_rejects_zip_entry_name_length_before_output()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-entry-name-length.xlsx");
    const std::string sentinel = "preserve existing entry-name-length output";
    write_file(path, sentinel);

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path,
            {
                {std::string(static_cast<std::size_t>(
                     std::numeric_limits<std::uint16_t>::max()) + 1u,
                     'a'),
                    ""},
            },
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "entry name length",
            "entry-name failure should explain the ZIP field limit");
    }

    check(failed, "PackageWriter should reject entry names beyond ZIP field size");
    check(fastxlsx::test::read_file(path) == sentinel,
        "entry-name overflow should fail before overwriting output");
}

void test_package_writer_rejects_invalid_entry_names_before_output()
{
    struct InvalidEntryNameCase {
        std::string_view suffix;
        std::string entry_name;
    };

    const std::vector<InvalidEntryNameCase> cases = {
        {"empty", ""},
        {"absolute", "/xl/workbook.xml"},
        {"trailing-slash", "xl/workbook.xml/"},
        {"empty-segment", "xl//workbook.xml"},
        {"dot-segment", "xl/./workbook.xml"},
        {"parent-segment", "xl/../workbook.xml"},
        {"backslash", R"(xl\workbook.xml)"},
        {"query", "xl/workbook.xml?version=1"},
        {"fragment", "xl/workbook.xml#sheet"},
        {"null-byte", std::string("xl/workbook\0.xml", 16)},
    };

    for (const InvalidEntryNameCase& test_case : cases) {
        const std::filesystem::path path = output_path(
            "fastxlsx-package-writer-invalid-entry-name-"
            + std::string(test_case.suffix) + ".xlsx");
        const std::string sentinel = "preserve existing invalid-entry-name output";
        write_file(path, sentinel);

        bool failed = false;
        try {
            fastxlsx::detail::write_package(path,
                {
                    {test_case.entry_name, "<payload/>"},
                },
                {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), "ZIP entry name",
                "invalid entry-name failure should explain the ZIP name constraint");
        }

        check(failed, "PackageWriter should reject invalid ZIP entry names");
        check(fastxlsx::test::read_file(path) == sentinel,
            "invalid entry-name failure should fail before overwriting output");
    }
}

void test_package_writer_rejects_duplicate_entry_names_before_output()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-duplicate-entry-name.xlsx");
    const std::string sentinel = "preserve existing duplicate-entry-name output";
    write_file(path, sentinel);

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path,
            {
                {"xl/workbook.xml", "<workbook/>"},
                {"xl/workbook.xml", "<duplicate/>"},
            },
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "duplicate ZIP entry name",
            "duplicate entry-name failure should explain the conflicting ZIP entry");
    }

    check(failed, "PackageWriter should reject duplicate ZIP entry names");
    check(fastxlsx::test::read_file(path) == sentinel,
        "duplicate entry-name failure should fail before overwriting output");
}

void test_package_writer_rejects_zip64_file_chunk_before_output()
{
    const std::filesystem::path chunk_path =
        output_path("fastxlsx-package-writer-zip64-large-chunk.bin");
    create_sparse_file_with_size(
        chunk_path, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1u);

    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-zip64-large-chunk.xlsx");
    const std::string sentinel = "preserve existing zip64-large-chunk output";
    write_file(path, sentinel);

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path,
            {
                {"xl/large.bin",
                    std::vector<fastxlsx::detail::PackageEntryChunk> {
                        fastxlsx::detail::PackageEntryChunk::file(chunk_path)}},
            },
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "Zip64",
            "large file-backed chunk failure should explain Zip64 requirement");
    }

    std::error_code remove_error;
    std::filesystem::remove(chunk_path, remove_error);

    check(failed, "PackageWriter should reject file-backed chunks requiring Zip64");
    check(fastxlsx::test::read_file(path) == sentinel,
        "Zip64-sized file chunk should fail before overwriting output");
}

void test_package_writer_rejects_missing_file_chunk_before_output()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-missing-file-chunk.xlsx");
    const std::string sentinel = "preserve existing missing-file-chunk output";
    write_file(path, sentinel);
    const std::filesystem::path missing_chunk_path = path / "missing.bin";

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path,
            {
                {"xl/missing.bin",
                    std::vector<fastxlsx::detail::PackageEntryChunk> {
                        fastxlsx::detail::PackageEntryChunk::file(missing_chunk_path)}},
            },
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "file-backed ZIP entry chunk",
            "missing file-backed chunk failure should explain the bad chunk path");
    }

    check(failed, "PackageWriter should reject missing file-backed chunks");
    check(fastxlsx::test::read_file(path) == sentinel,
        "missing file-backed chunk should fail before overwriting output");
}

void test_package_writer_rejects_mixed_legacy_data_and_chunks_before_output()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-writer-mixed-data-and-chunks.xlsx");
    const std::string sentinel = "preserve existing mixed-data-and-chunks output";
    write_file(path, sentinel);

    fastxlsx::detail::PackageEntry entry("xl/mixed.xml", "<legacy/>");
    entry.chunks.push_back(
        fastxlsx::detail::PackageEntryChunk::memory("<chunked/>"));

    bool failed = false;
    try {
        fastxlsx::detail::write_package(path, {entry},
            {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), "chunked payload",
            "mixed payload failure should explain the conflicting entry sources");
    }

    check(failed, "PackageWriter should reject entries that mix data and chunks");
    check(fastxlsx::test::read_file(path) == sentinel,
        "mixed data/chunks should fail before overwriting output");
}

void test_package_writer_rejects_invalid_chunk_sources_before_output()
{
    const std::filesystem::path file_chunk_path =
        output_path("fastxlsx-package-writer-invalid-chunk-source.bin");
    write_file(file_chunk_path, "file-backed chunk payload");

    struct InvalidChunkCase {
        std::string_view suffix;
        fastxlsx::detail::PackageEntryChunk chunk;
        std::string_view expected_message;
    };

    fastxlsx::detail::PackageEntryChunk memory_with_path =
        fastxlsx::detail::PackageEntryChunk::memory("<memory/>");
    memory_with_path.path = file_chunk_path;

    fastxlsx::detail::PackageEntryChunk file_with_data =
        fastxlsx::detail::PackageEntryChunk::file(file_chunk_path);
    file_with_data.data = "<ignored-memory/>";

    fastxlsx::detail::PackageEntryChunk unknown_kind =
        fastxlsx::detail::PackageEntryChunk::memory("<unknown/>");
    unknown_kind.kind = static_cast<fastxlsx::detail::PackageEntryChunk::Kind>(99);

    const std::vector<InvalidChunkCase> cases = {
        {"memory-with-path", memory_with_path, "memory and file sources"},
        {"file-with-data", file_with_data, "memory and file sources"},
        {"unknown-kind", unknown_kind, "unsupported ZIP entry chunk kind"},
    };

    for (const InvalidChunkCase& test_case : cases) {
        const std::filesystem::path path = output_path(
            "fastxlsx-package-writer-invalid-chunk-source-"
            + std::string(test_case.suffix) + ".xlsx");
        const std::string sentinel = "preserve existing invalid-chunk-source output";
        write_file(path, sentinel);

        bool failed = false;
        try {
            fastxlsx::detail::write_package(path,
                {
                    {"xl/chunk-source.xml",
                        std::vector<fastxlsx::detail::PackageEntryChunk> {test_case.chunk}},
                },
                {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
        } catch (const std::exception& error) {
            failed = true;
            check_contains(error.what(), test_case.expected_message,
                "invalid chunk-source failure should explain the bad chunk state");
        }

        check(failed, "PackageWriter should reject invalid chunk source state");
        check(fastxlsx::test::read_file(path) == sentinel,
            "invalid chunk source should fail before overwriting output");
    }

    std::error_code remove_error;
    std::filesystem::remove(file_chunk_path, remove_error);
}

void test_package_reader_reads_stored_entries_and_unknown_parts()
{
    const std::filesystem::path path = output_path("fastxlsx-package-reader-stored.xlsx");
    const std::string unknown_body("raw\0bytes", 9);

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/unknown.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    check(reader.entries().size() == 4, "PackageReader entry count mismatch");
    check(reader.path() == path, "PackageReader should retain the source path");

    const auto* content_types = reader.find_entry("[Content_Types].xml");
    check(content_types != nullptr, "PackageReader should find [Content_Types].xml");
    check(content_types->compression_method == 0, "stored entry method mismatch");
    check(content_types->uncompressed_size == 8, "content types entry size mismatch");
    check(reader.read_entry("[Content_Types].xml") == "<Types/>",
        "content types entry body mismatch");

    const auto* unknown = reader.find_entry("custom/unknown.bin");
    check(unknown != nullptr, "PackageReader should find unknown entries");
    check(unknown->compressed_size == unknown_body.size(), "unknown entry size mismatch");
    check(reader.read_entry("custom/unknown.bin") == unknown_body,
        "unknown entry bytes should be readable");

    check(reader.find_entry("xl/missing.xml") == nullptr,
        "missing entries should not be found");
    check(reader.part_index().size() == 2,
        "PackageReader should index non-metadata package parts");
    check(reader.part_index().find_part(fastxlsx::detail::PartName("/xl/workbook.xml"))
            != nullptr,
        "PackageReader should index workbook part even without content type metadata");
    const auto* unknown_part =
        reader.part_index().find_part(fastxlsx::detail::PartName("/custom/unknown.bin"));
    check(unknown_part != nullptr, "PackageReader should index unknown package parts");
    check(unknown_part->write_mode == fastxlsx::detail::PartWriteMode::CopyOriginal,
        "PackageReader indexed parts should default to copy-original planning state");
    check(reader.package_relationships().empty(),
        "empty package relationships should remain empty");

    bool missing_read_failed = false;
    try {
        (void)reader.read_entry("xl/missing.xml");
    } catch (const std::exception&) {
        missing_read_failed = true;
    }
    check(missing_read_failed, "reading a missing entry should fail");
}

void test_package_reader_extracts_stored_entry_to_file()
{
    const std::filesystem::path path = output_path("fastxlsx-package-reader-extract.xlsx");
    std::string unknown_body;
    for (int index = 0; index < 4096; ++index) {
        unknown_body += "stored-entry-streaming-source-";
        unknown_body += std::to_string(index);
        unknown_body += '\n';
    }

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/unknown.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    const std::filesystem::path extracted =
        output_path("fastxlsx-package-reader-extracted-unknown.bin");
    reader.extract_entry_to_file("custom/unknown.bin", extracted);

    check(fastxlsx::test::read_file(extracted) == unknown_body,
        "PackageReader should extract stored entry bytes to a file-backed source");
}

void test_package_reader_streams_stored_entry_chunks()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-entry-chunks.xlsx");
    std::string unknown_body;
    for (int index = 0; index < 4096; ++index) {
        unknown_body += "stored-entry-direct-chunk-source-";
        unknown_body += std::to_string(index);
        unknown_body += '\n';
    }

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/unknown.bin", unknown_body},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    fastxlsx::detail::PackageReaderChunkCallback source =
        reader.entry_chunk_source("custom/unknown.bin");

    std::string chunk;
    std::string streamed_body;
    std::size_t chunk_count = 0;
    while (source(chunk)) {
        check(!chunk.empty(), "PackageReader chunk source should not emit empty chunks");
        streamed_body += chunk;
        ++chunk_count;
    }

    check(streamed_body == unknown_body,
        "PackageReader should stream stored entry bytes through chunk source");
    check(chunk_count > 1, "large stored entry should be delivered in multiple chunks");
}

void test_package_reader_ingests_content_types_and_relationships()
{
    const std::filesystem::path path = output_path("fastxlsx-package-reader-opc.xlsx");

    const std::string content_types = R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName='/xl/drawings/drawing1.xml' ContentType='application/vnd.openxmlformats-officedocument.drawing+xml'/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
        R"(</Relationships>)";
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id='rId1' Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing' Target='../drawings/drawing1.xml'/>)"
        R"(<Relationship Id='rId2' Type='http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink' Target='https://example.test/path?a=1&amp;b=2' TargetMode='External'/>)"
        R"(</Relationships>)";

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"xl/drawings/drawing1.xml", "<xdr:wsDr/>"},
            {"custom/opaque.bin", "opaque"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    check(reader.content_types().defaults().size() == 2,
        "content type defaults should be parsed");
    check(reader.content_types().overrides().size() == 3,
        "content type overrides should be parsed");

    const auto* workbook = reader.part_index().find_part(
        fastxlsx::detail::PartName("/xl/workbook.xml"));
    check(workbook != nullptr, "part index should include workbook");
    check(workbook->content_type
            == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml",
        "workbook content type should come from override");

    const auto* drawing = reader.part_index().find_part(
        fastxlsx::detail::PartName("/xl/drawings/drawing1.xml"));
    check(drawing != nullptr, "part index should include drawing part");
    check(drawing->content_type == "application/vnd.openxmlformats-officedocument.drawing+xml",
        "drawing content type should come from single-quoted override");

    const auto* unknown = reader.part_index().find_part(
        fastxlsx::detail::PartName("/custom/opaque.bin"));
    check(unknown != nullptr, "part index should include unknown part");
    check(unknown->content_type == "application/octet-stream",
        "unknown part content type should be resolved from default");
    check(unknown->preserve_original && !unknown->dirty && !unknown->generated,
        "unknown part should remain copy-original metadata");

    check(reader.package_relationships().size() == 1,
        "package relationships should be parsed");
    check(reader.package_relationships().find_by_id("rId1")->target == "xl/workbook.xml",
        "package relationship target mismatch");

    const auto* workbook_rels = reader.relationships_for(
        fastxlsx::detail::PartName("/xl/workbook.xml"));
    check(workbook_rels != nullptr, "workbook relationships should be attached to workbook");
    check(workbook_rels->size() == 2, "workbook relationship count mismatch");
    check(workbook_rels->find_by_id("rId1")->target == "worksheets/sheet1.xml",
        "workbook worksheet relationship target mismatch");

    const auto* worksheet_rels = reader.relationships_for(
        fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"));
    check(worksheet_rels != nullptr, "worksheet relationships should be attached to worksheet");
    check(worksheet_rels->size() == 2, "worksheet relationship count mismatch");
    const auto* hyperlink = worksheet_rels->find_by_id("rId2");
    check(hyperlink != nullptr, "worksheet external hyperlink relationship should exist");
    check(hyperlink->target == "https://example.test/path?a=1&b=2",
        "relationship target XML entity should be unescaped");
    check(hyperlink->target_mode == fastxlsx::detail::Relationship::TargetMode::External,
        "relationship TargetMode should be parsed");

    const fastxlsx::detail::RelationshipGraph graph = reader.relationship_graph();
    check(graph.package_relationships().size() == 1,
        "relationship graph should include package relationships");
    const auto* graph_worksheet_rels =
        graph.relationships_for(fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"));
    check(graph_worksheet_rels != nullptr,
        "relationship graph should include worksheet relationships");
    check(graph_worksheet_rels->find_by_id("rId2")->target_mode
            == fastxlsx::detail::Relationship::TargetMode::External,
        "relationship graph should preserve external target mode");
}

void test_package_reader_resolves_workbook_sheet_catalog()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-workbook-sheets.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet space.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet3.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet4.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/./workbook.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet%20space.xml"/>)"
        R"(<Relationship Id="rId3" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="/xl/worksheets/sheet3.xml"/>)"
        R"(<Relationship Id="rId4" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="./worksheets/../worksheets/sheet4.xml"/>)"
        R"(</Relationships>)";
    const std::string workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<extLst xmlns:r="urn:fastxlsx:not-relationships"><ext><sheet name="Ignored Outer" sheetId="900" r:id="not-a-sheet-rel"/></ext></extLst>)"
        R"(<extLst><ext><sheets><sheet name="Ignored Decoy Catalog" sheetId="902" rel:id="rId3"/></sheets></ext></extLst>)"
        R"(<sheets>)"
        R"(<extLst><ext><sheet name="Ignored Nested" sheetId="901" rel:id="missingNestedRel"/></ext></extLst>)"
        R"(<sheet name="Sales &amp; QA" sheetId="1" r:id="rId1"/>)"
        R"(<sheet name="Ops &#x2603;" sheetId="2" r:id="rId2"/>)"
        R"(<sheet name="Alt Prefix" sheetId="3" rel:id="rId3"/>)"
        R"(<sheet name="Dot Segments" sheetId="4" r:id="rId4"/>)"
        R"(</sheets>)"
        R"(</workbook>)";

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", workbook},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
            {"xl/worksheets/sheet space.xml", "<worksheet/>"},
            {"xl/worksheets/sheet3.xml", "<worksheet/>"},
            {"xl/worksheets/sheet4.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    const auto* package_workbook_relationship =
        reader.package_relationships().find_by_id("rId1");
    check(package_workbook_relationship != nullptr
            && package_workbook_relationship->target == "xl/./workbook.xml",
        "package reader should preserve dot-segment officeDocument target text");
    const std::vector<fastxlsx::detail::WorkbookSheetReference> sheets =
        reader.workbook_sheets();
    check(sheets.size() == 4,
        "workbook sheet catalog should expose only direct workbook sheets");
    check(sheets[0].name == "Sales & QA",
        "workbook sheet catalog should unescape sheet names");
    check(sheets[0].sheet_id == "1",
        "workbook sheet catalog should preserve sheetId");
    check(sheets[0].relationship_id == "rId1",
        "workbook sheet catalog should preserve relationship id");
    check(sheets[0].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"),
        "workbook sheet catalog should resolve relative worksheet targets");

    const std::string snowman_sheet_name = std::string("Ops ") + "\xe2\x98\x83";
    check(sheets[1].name == snowman_sheet_name,
        "workbook sheet catalog should decode numeric character references");
    check(sheets[1].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet space.xml"),
        "workbook sheet catalog should decode percent-encoded worksheet targets");
    check(sheets[2].name == "Alt Prefix",
        "workbook sheet catalog should accept alternate relationship namespace prefixes");
    check(sheets[2].relationship_id == "rId3",
        "workbook sheet catalog should preserve alternate-prefix relationship ids");
    check(sheets[2].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet3.xml"),
        "workbook sheet catalog should resolve absolute alternate-prefix worksheet targets");
    check(sheets[3].name == "Dot Segments",
        "workbook sheet catalog should preserve dot-segment sheet names");
    check(sheets[3].relationship_id == "rId4",
        "workbook sheet catalog should preserve dot-segment relationship ids");
    check(sheets[3].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet4.xml"),
        "workbook sheet catalog should normalize dot-segment worksheet targets");
    check(reader.worksheet_part_by_sheet_name("Sales & QA")
            == fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"),
        "sheet-name lookup should return the matching worksheet part");
    check(reader.worksheet_part_by_sheet_name(snowman_sheet_name)
            == fastxlsx::detail::PartName("/xl/worksheets/sheet space.xml"),
        "sheet-name lookup should support decoded UTF-8 names");
    check(reader.worksheet_part_by_sheet_name("Alt Prefix")
            == fastxlsx::detail::PartName("/xl/worksheets/sheet3.xml"),
        "sheet-name lookup should support alternate prefixes with absolute worksheet targets");
    check(reader.worksheet_part_by_sheet_name("Dot Segments")
            == fastxlsx::detail::PartName("/xl/worksheets/sheet4.xml"),
        "sheet-name lookup should support normalized dot-segment worksheet targets");
    const std::string planned_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Planned &amp; QA" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    const std::vector<fastxlsx::detail::WorkbookSheetReference> planned_sheets =
        reader.workbook_sheets_from_xml(planned_workbook);
    check(planned_sheets.size() == 1,
        "planned workbook sheet catalog should parse caller-provided workbook XML");
    check(planned_sheets[0].name == "Planned & QA",
        "planned workbook sheet catalog should use planned sheet names");
    check(planned_sheets[0].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"),
        "planned workbook sheet catalog should reuse source workbook relationships");
    check(reader.worksheet_part_by_sheet_name_from_xml("Planned & QA", planned_workbook)
            == fastxlsx::detail::PartName("/xl/worksheets/sheet1.xml"),
        "planned sheet-name lookup should resolve against caller-provided workbook XML");
    const std::string planned_scoped_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships")"
        R"( xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheet name="Planned Ignored Outer" sheetId="900" r:id="rId1"/>)"
        R"(<extLst><ext><sheets>)"
        R"(<sheet name="Planned Ignored Decoy Catalog" sheetId="901" rel:id="rId3"/>)"
        R"(</sheets></ext></extLst>)"
        R"(<sheets>)"
        R"(<extLst><ext>)"
        R"(<sheet name="Planned Ignored Nested" sheetId="902" rel:id="rId2"/>)"
        R"(</ext></extLst>)"
        R"(<sheet name="Planned Direct" sheetId="4" r:id="rId4"/>)"
        R"(</sheets>)"
        R"(</workbook>)";
    const std::vector<fastxlsx::detail::WorkbookSheetReference> planned_scoped_sheets =
        reader.workbook_sheets_from_xml(planned_scoped_workbook);
    check(planned_scoped_sheets.size() == 1,
        "planned workbook sheet catalog should expose only direct workbook sheets");
    check(planned_scoped_sheets[0].name == "Planned Direct",
        "planned workbook sheet catalog should ignore decoy sheet tags");
    check(planned_scoped_sheets[0].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet4.xml"),
        "planned workbook sheet catalog should resolve direct scoped worksheet targets");
    check(reader.worksheet_part_by_sheet_name_from_xml("Planned Direct",
              planned_scoped_workbook)
            == fastxlsx::detail::PartName("/xl/worksheets/sheet4.xml"),
        "planned sheet-name lookup should resolve direct scoped workbook sheets");
    const std::string planned_alternate_prefix_workbook =
        R"(<workbook xmlns:rel="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Planned Alt Prefix" sheetId="3" rel:id="rId3"/></sheets>)"
        R"(</workbook>)";
    const std::vector<fastxlsx::detail::WorkbookSheetReference>
        planned_alternate_prefix_sheets =
            reader.workbook_sheets_from_xml(planned_alternate_prefix_workbook);
    check(planned_alternate_prefix_sheets.size() == 1,
        "planned workbook sheet catalog should accept alternate relationship prefixes");
    check(planned_alternate_prefix_sheets[0].name == "Planned Alt Prefix",
        "planned workbook sheet catalog should preserve alternate-prefix sheet names");
    check(planned_alternate_prefix_sheets[0].relationship_id == "rId3",
        "planned workbook sheet catalog should preserve alternate-prefix relationship ids");
    check(planned_alternate_prefix_sheets[0].part_name
            == fastxlsx::detail::PartName("/xl/worksheets/sheet3.xml"),
        "planned workbook sheet catalog should resolve alternate-prefix worksheet targets");
    check(reader.worksheet_part_by_sheet_name_from_xml("Planned Alt Prefix",
              planned_alternate_prefix_workbook)
            == fastxlsx::detail::PartName("/xl/worksheets/sheet3.xml"),
        "planned sheet-name lookup should support alternate relationship prefixes");

    const auto expect_planned_workbook_failure =
        [&](std::string_view workbook_xml, const char* message) {
            bool planned_failed = false;
            try {
                (void)reader.workbook_sheets_from_xml(workbook_xml);
            } catch (const std::exception&) {
                planned_failed = true;
            }
            check(planned_failed, message);
        };
    const auto expect_planned_sheet_lookup_failure =
        [&](std::string_view sheet_name, std::string_view workbook_xml, const char* message) {
            bool planned_failed = false;
            try {
                (void)reader.worksheet_part_by_sheet_name_from_xml(sheet_name, workbook_xml);
            } catch (const std::exception&) {
                planned_failed = true;
            }
            check(planned_failed, message);
        };
    expect_planned_sheet_lookup_failure("Planned Ignored Outer", planned_scoped_workbook,
        "planned sheet-name lookup should ignore sheet tags outside the sheets catalog");
    expect_planned_sheet_lookup_failure(
        "Planned Ignored Decoy Catalog", planned_scoped_workbook,
        "planned sheet-name lookup should ignore non-root workbook sheets catalogs");
    expect_planned_sheet_lookup_failure("Planned Ignored Nested", planned_scoped_workbook,
        "planned sheet-name lookup should ignore non-direct sheet tags inside sheets");
    const std::string planned_wrong_namespace_workbook =
        R"(<workbook xmlns:x="urn:fastxlsx:not-relationships">)"
        R"(<sheets><sheet name="Planned Wrong Namespace" sheetId="1" x:id="rId1"/></sheets>)"
        R"(</workbook>)";
    expect_planned_workbook_failure(planned_wrong_namespace_workbook,
        "planned workbook sheet catalog should reject wrong-namespace id attributes");
    const std::string planned_unqualified_id_workbook =
        R"(<workbook><sheets>)"
        R"(<sheet name="Planned Plain Id" sheetId="1" id="rId1"/>)"
        R"(</sheets></workbook>)";
    expect_planned_workbook_failure(planned_unqualified_id_workbook,
        "planned workbook sheet catalog should reject unqualified id attributes");
    const std::string planned_missing_relationship_id_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Planned Missing Rel" sheetId="1" r:id="missingRel"/></sheets>)"
        R"(</workbook>)";
    expect_planned_workbook_failure(planned_missing_relationship_id_workbook,
        "planned workbook sheet catalog should reject sheet ids absent from workbook relationships");
    expect_planned_sheet_lookup_failure(
        "Planned Missing Rel", planned_missing_relationship_id_workbook,
        "planned sheet-name lookup should reject sheet ids absent from workbook relationships");
    bool ignored_outer_failed = false;
    try {
        (void)reader.worksheet_part_by_sheet_name("Ignored Outer");
    } catch (const std::exception&) {
        ignored_outer_failed = true;
    }
    check(ignored_outer_failed,
        "sheet-name lookup should ignore sheet tags outside the sheets catalog");
    bool ignored_decoy_catalog_failed = false;
    try {
        (void)reader.worksheet_part_by_sheet_name("Ignored Decoy Catalog");
    } catch (const std::exception&) {
        ignored_decoy_catalog_failed = true;
    }
    check(ignored_decoy_catalog_failed,
        "sheet-name lookup should ignore non-root workbook sheets catalogs");
    bool ignored_nested_failed = false;
    try {
        (void)reader.worksheet_part_by_sheet_name("Ignored Nested");
    } catch (const std::exception&) {
        ignored_nested_failed = true;
    }
    check(ignored_nested_failed,
        "sheet-name lookup should ignore non-direct sheet tags inside sheets");

    bool failed = false;
    try {
        (void)reader.worksheet_part_by_sheet_name("Missing");
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "sheet-name lookup should reject missing sheet names");

    const std::filesystem::path duplicate_name_path =
        output_path("fastxlsx-package-reader-workbook-sheets-duplicate-name.xlsx");
    const std::string duplicate_content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet2.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    const std::string duplicate_workbook_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
        R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet2.xml"/>)"
        R"(</Relationships>)";
    const std::string duplicate_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets>)"
        R"(<sheet name="Duplicated" sheetId="1" r:id="rId1"/>)"
        R"(<sheet name="Duplicated" sheetId="2" r:id="rId2"/>)"
        R"(</sheets>)"
        R"(</workbook>)";
    fastxlsx::detail::write_package(duplicate_name_path,
        {
            {"[Content_Types].xml", duplicate_content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", duplicate_workbook},
            {"xl/_rels/workbook.xml.rels", duplicate_workbook_relationships},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
            {"xl/worksheets/sheet2.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader duplicate_reader =
        fastxlsx::detail::PackageReader::open(duplicate_name_path);
    check(duplicate_reader.workbook_sheets().size() == 2,
        "workbook sheet catalog should expose duplicate sheet names");
    bool ambiguous_failed = false;
    try {
        (void)duplicate_reader.worksheet_part_by_sheet_name("Duplicated");
    } catch (const std::exception&) {
        ambiguous_failed = true;
    }
    check(ambiguous_failed,
        "sheet-name lookup should reject ambiguous duplicate sheet names");
}

void test_package_reader_rejects_invalid_workbook_sheet_catalog()
{
    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>)"
        R"(</Types>)";
    const std::string package_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
        R"(</Relationships>)";

    const std::filesystem::path missing_office_document_path =
        output_path("fastxlsx-package-reader-workbook-sheets-missing-office-document.xlsx");
    fastxlsx::detail::write_package(missing_office_document_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rIdCustom" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml" Target="custom/item1.xml"/>)"
                R"(</Relationships>)"},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(missing_office_document_path,
        "workbook sheet catalog should require a package officeDocument relationship");

    const std::filesystem::path duplicate_office_document_path =
        output_path("fastxlsx-package-reader-workbook-sheets-duplicate-office-document.xlsx");
    fastxlsx::detail::write_package(duplicate_office_document_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
                R"(<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
                R"(</Relationships>)"},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(duplicate_office_document_path,
        "workbook sheet catalog should reject multiple officeDocument relationships");

    const std::filesystem::path external_office_document_path =
        output_path("fastxlsx-package-reader-workbook-sheets-external-office-document.xlsx");
    fastxlsx::detail::write_package(external_office_document_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="https://example.invalid/workbook.xml" TargetMode="External"/>)"
                R"(</Relationships>)"},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(external_office_document_path,
        "workbook sheet catalog should reject external officeDocument targets");

    const std::filesystem::path query_office_document_path =
        output_path("fastxlsx-package-reader-workbook-sheets-query-office-document.xlsx");
    fastxlsx::detail::write_package(query_office_document_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml?version=1"/>)"
                R"(</Relationships>)"},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(query_office_document_path,
        "workbook sheet catalog should reject URI-qualified officeDocument targets");

    const std::string alternate_content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Override PartName="/xl/altWorkbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
        R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
        R"(</Types>)";
    const std::filesystem::path alternate_office_document_path =
        output_path("fastxlsx-package-reader-workbook-sheets-alternate-office-document.xlsx");
    fastxlsx::detail::write_package(alternate_office_document_path,
        {
            {"[Content_Types].xml", alternate_content_types},
            {"_rels/.rels",
                R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/altWorkbook.xml"/>)"
                R"(</Relationships>)"},
            {"xl/altWorkbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/altWorkbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(alternate_office_document_path,
        "workbook sheet catalog should reject non-fixed officeDocument targets");

    const std::filesystem::path missing_id_path =
        output_path("fastxlsx-package-reader-workbook-sheets-missing-id.xlsx");
    fastxlsx::detail::write_package(missing_id_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook><sheets><sheet name="Sheet1" sheetId="1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels", "<Relationships/>"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(missing_id_path,
        "workbook sheet catalog should reject sheets without relationship ids");

    const std::filesystem::path missing_relationship_id_path =
        output_path("fastxlsx-package-reader-workbook-sheets-missing-rel-id.xlsx");
    fastxlsx::detail::write_package(missing_relationship_id_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
                R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="missingRel"/></sheets>)"
                R"(</workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(missing_relationship_id_path,
        "workbook sheet catalog should reject sheet ids missing from workbook relationships");

    const std::filesystem::path unregistered_target_path =
        output_path("fastxlsx-package-reader-workbook-sheets-unregistered-target.xlsx");
    fastxlsx::detail::write_package(unregistered_target_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
                R"(<sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets>)"
                R"(</workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/missing.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(unregistered_target_path,
        "workbook sheet catalog should reject worksheet relationships to unregistered parts");
    const fastxlsx::detail::PackageReader unregistered_target_reader =
        fastxlsx::detail::PackageReader::open(unregistered_target_path);
    const std::string planned_unregistered_target_workbook =
        R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)"
        R"(<sheets><sheet name="Planned Missing Target" sheetId="1" r:id="rId1"/></sheets>)"
        R"(</workbook>)";
    bool planned_unregistered_target_failed = false;
    try {
        (void)unregistered_target_reader.workbook_sheets_from_xml(
            planned_unregistered_target_workbook);
    } catch (const std::exception&) {
        planned_unregistered_target_failed = true;
    }
    check(planned_unregistered_target_failed,
        "planned workbook sheet catalog should reject worksheet relationships to unregistered parts");
    bool planned_unregistered_lookup_failed = false;
    try {
        (void)unregistered_target_reader.worksheet_part_by_sheet_name_from_xml(
            "Planned Missing Target", planned_unregistered_target_workbook);
    } catch (const std::exception&) {
        planned_unregistered_lookup_failed = true;
    }
    check(planned_unregistered_lookup_failed,
        "planned sheet-name lookup should reject worksheet relationships to unregistered parts");

    const std::filesystem::path namespaced_name_path =
        output_path("fastxlsx-package-reader-workbook-sheets-namespaced-name.xlsx");
    fastxlsx::detail::write_package(namespaced_name_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:x="urn:fastxlsx:not-workbook"><sheets><sheet x:name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(namespaced_name_path,
        "workbook sheet catalog should reject namespaced sheet name attributes");

    const std::filesystem::path namespaced_sheet_id_path =
        output_path("fastxlsx-package-reader-workbook-sheets-namespaced-sheet-id.xlsx");
    fastxlsx::detail::write_package(namespaced_sheet_id_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" xmlns:x="urn:fastxlsx:not-workbook"><sheets><sheet name="Sheet1" x:sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(namespaced_sheet_id_path,
        "workbook sheet catalog should reject namespaced sheetId attributes");

    const std::filesystem::path wrong_id_namespace_path =
        output_path("fastxlsx-package-reader-workbook-sheets-wrong-id-namespace.xlsx");
    fastxlsx::detail::write_package(wrong_id_namespace_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook xmlns:x="urn:fastxlsx:not-relationships"><sheets><sheet name="Sheet1" sheetId="1" x:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(wrong_id_namespace_path,
        "workbook sheet catalog should reject non-relationship namespace id attributes");

    const std::filesystem::path unqualified_id_path =
        output_path("fastxlsx-package-reader-workbook-sheets-unqualified-id.xlsx");
    fastxlsx::detail::write_package(unqualified_id_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook><sheets><sheet name="Sheet1" sheetId="1" id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(unqualified_id_path,
        "workbook sheet catalog should reject unqualified id attributes");

    const std::filesystem::path external_path =
        output_path("fastxlsx-package-reader-workbook-sheets-external.xlsx");
    fastxlsx::detail::write_package(external_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="https://example.invalid/sheet.xml" TargetMode="External"/></Relationships>)"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(external_path,
        "workbook sheet catalog should reject external worksheet targets");

    const std::filesystem::path non_worksheet_path =
        output_path("fastxlsx-package-reader-workbook-sheets-non-worksheet.xlsx");
    fastxlsx::detail::write_package(non_worksheet_path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml",
                R"(<workbook><sheets><sheet name="Sheet1" sheetId="1" r:id="rId1"/></sheets></workbook>)"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="styles.xml"/></Relationships>)"},
            {"xl/styles.xml", "<styleSheet/>"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
    expect_workbook_sheets_failure(non_worksheet_path,
        "workbook sheet catalog should reject non-worksheet relationship targets");
}

void test_package_reader_ingests_root_source_relationships_as_metadata()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-root-source-rels.xlsx");

    const std::string root_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml" Target="custom/item1.xml"/>)"
        R"(</Relationships>)";

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/>)"
                R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
                R"(</Types>)"},
            {"_rels/.rels", "<Relationships/>"},
            {"root.xml", "<root/>"},
            {"_rels/root.xml.rels", root_relationships},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    check(reader.part_index().size() == 1,
        "root source relationships should not be indexed as ordinary parts");
    check(reader.part_index().find_part(fastxlsx::detail::PartName("/root.xml")) != nullptr,
        "root source part should be indexed");
    check(reader.part_index().find_part(
              fastxlsx::detail::PartName("/_rels/root.xml.rels")) == nullptr,
        "root source relationships entry should stay metadata-only");
    check(reader.read_entry("_rels/root.xml.rels") == root_relationships,
        "root source relationships bytes should remain readable");

    const auto* root_rels =
        reader.relationships_for(fastxlsx::detail::PartName("/root.xml"));
    check(root_rels != nullptr, "root source relationships should be attached to root part");
    check(root_rels->size() == 1, "root source relationship count mismatch");
    check(root_rels->find_by_id("rId1")->target == "custom/item1.xml",
        "root source relationship target mismatch");

    const fastxlsx::detail::RelationshipGraph graph = reader.relationship_graph();
    const auto* graph_root_rels =
        graph.relationships_for(fastxlsx::detail::PartName("/root.xml"));
    check(graph_root_rels != nullptr,
        "relationship graph should include root source relationships");
    check(graph_root_rels->find_by_id("rId1") != nullptr,
        "relationship graph should preserve root source relationship id");
}

void test_package_reader_ingests_unknown_extension_relationships_as_metadata()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-unknown-extension-rels.xlsx");

    const std::string content_types =
        R"(<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">)"
        R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
        R"(<Default Extension="xml" ContentType="application/xml"/>)"
        R"(<Default Extension="bin" ContentType="application/octet-stream"/>)"
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
    const std::string worksheet_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rId9" Type="https://fastxlsx.invalid/relationships/opaque-extension" Target="../../custom/opaque-extension.bin"/>)"
        R"(</Relationships>)";
    const std::string opaque_relationships =
        R"(<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">)"
        R"(<Relationship Id="rIdOpaqueExternal" Type="https://fastxlsx.invalid/relationships/opaque-extension-audit" Target="https://example.invalid/opaque-extension-audit" TargetMode="External"/>)"
        R"(</Relationships>)";
    const std::string opaque_payload("extension\0payload", 17);

    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", content_types},
            {"_rels/.rels", package_relationships},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels", workbook_relationships},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
            {"xl/worksheets/_rels/sheet1.xml.rels", worksheet_relationships},
            {"custom/opaque-extension.bin", opaque_payload},
            {"custom/_rels/opaque-extension.bin.rels", opaque_relationships},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    const fastxlsx::detail::PartName worksheet_part("/xl/worksheets/sheet1.xml");
    const fastxlsx::detail::PartName opaque_part("/custom/opaque-extension.bin");
    const fastxlsx::detail::PartName opaque_relationships_part(
        "/custom/_rels/opaque-extension.bin.rels");

    check(reader.part_index().find_part(opaque_part) != nullptr,
        "unknown extension owner part should be indexed");
    check(reader.part_index().find_part(opaque_relationships_part) == nullptr,
        "unknown extension source-owned relationships should stay metadata-only");
    check(reader.read_entry("custom/opaque-extension.bin") == opaque_payload,
        "unknown extension owner bytes should remain readable");
    check(reader.read_entry("custom/_rels/opaque-extension.bin.rels")
            == opaque_relationships,
        "unknown extension owner relationships bytes should remain readable");

    const auto* worksheet_rels = reader.relationships_for(worksheet_part);
    check(worksheet_rels != nullptr,
        "worksheet relationships should be attached before graph construction");
    const auto* opaque_link = worksheet_rels->find_by_id("rId9");
    check(opaque_link != nullptr,
        "worksheet should retain the relationship to the unknown extension part");
    check(opaque_link->target == "../../custom/opaque-extension.bin",
        "worksheet relationship target to unknown extension should remain raw");

    const auto* opaque_rels = reader.relationships_for(opaque_part);
    check(opaque_rels != nullptr,
        "unknown extension source-owned relationships should attach to the owner part");
    const auto* external_audit = opaque_rels->find_by_id("rIdOpaqueExternal");
    check(external_audit != nullptr,
        "unknown extension owner relationships should retain their relationship id");
    check(external_audit->target == "https://example.invalid/opaque-extension-audit",
        "unknown extension owner relationship target mismatch");
    check(external_audit->target_mode
            == fastxlsx::detail::Relationship::TargetMode::External,
        "unknown extension owner relationship TargetMode should be parsed");

    const fastxlsx::detail::RelationshipGraph graph = reader.relationship_graph();
    const auto* graph_opaque_rels = graph.relationships_for(opaque_part);
    check(graph_opaque_rels != nullptr,
        "relationship graph should include unknown extension owner relationships");
    check(graph_opaque_rels->find_by_id("rIdOpaqueExternal") != nullptr,
        "relationship graph should preserve unknown extension owner relationship id");
}

void test_package_reader_rejects_duplicate_entries()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-duplicate-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"xl/sheet1.xml", "one"},
            {"xl/sheet2.xml", "two"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::string duplicate_name = "xl/sheet1.xml";
    const std::string original_name = "xl/sheet2.xml";
    check(duplicate_name.size() == original_name.size(),
        "duplicate-entry fixture names should have matching lengths");

    const ZipEntryLocation second_entry = find_zip_entry_location(data, original_name);
    data.replace(second_entry.central_offset + 46u, original_name.size(), duplicate_name);
    data.replace(second_entry.local_offset + 30u, original_name.size(), duplicate_name);

    const std::filesystem::path path = output_path("fastxlsx-package-reader-duplicate.xlsx");
    write_file(path, data);

    expect_open_failure(path, "PackageReader should reject duplicate ZIP entry names");
}

void test_package_reader_rejects_invalid_entry_names()
{
    struct InvalidEntryNameCase {
        std::string_view suffix;
        std::string entry_name;
    };

    const std::vector<InvalidEntryNameCase> cases = {
        {"absolute", "/xl/workbook.xml"},
        {"trailing-slash", "xl/workbook.xml/"},
        {"empty-segment", "xl//workbook.xml"},
        {"dot-segment", "xl/./workbook.xml"},
        {"parent-segment", "xl/../workbook.xml"},
        {"backslash", R"(xl\workbook.xml)"},
        {"query", "xl/workbook.xml?version=1"},
        {"fragment", "xl/workbook.xml#sheet"},
        {"null-byte", std::string("xl/workbook\0.xml", 16)},
    };

    for (const InvalidEntryNameCase& test_case : cases) {
        const std::filesystem::path path = output_path(
            "fastxlsx-package-reader-invalid-entry-name-"
            + std::string(test_case.suffix) + ".xlsx");
        fastxlsx::detail::write_stored_zip(path,
            {
                {"[Content_Types].xml",
                    R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
                {test_case.entry_name, "<workbook/>"},
            });

        expect_open_failure(path,
            "PackageReader should reject invalid ZIP entry names");
    }
}

void test_package_reader_rejects_bad_zip()
{
    const std::filesystem::path path = output_path("fastxlsx-package-reader-bad.xlsx");
    write_file(path, "not a zip");

    expect_open_failure(path, "PackageReader should reject invalid ZIP packages");
}

void test_package_reader_rejects_central_directory_trailing_data_before_eocd()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-central-trailing-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t eocd_offset = find_end_of_central_directory(data);
    data.insert(eocd_offset, "unsupported trailing central directory data");

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-central-trailing.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject unsupported data between central directory and EOCD");
}

#ifndef FASTXLSX_TEST_HAS_MINIZIP_NG
void test_package_reader_rejects_compressed_entries_without_minizip()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-method-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t local_offset = find_signature(data, 0x04034b50u);
    const std::size_t central_offset = find_signature(data, 0x02014b50u);
    write_u16(data, local_offset + 8, 8);
    write_u16(data, central_offset + 10, 8);

    const std::filesystem::path path = output_path("fastxlsx-package-reader-method.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject compressed ZIP entries without minizip-ng");
}
#endif

void test_package_reader_rejects_central_encrypted_flag()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-central-encrypted-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t central_offset = find_signature(data, 0x02014b50u);
    write_u16(data, central_offset + 8,
        static_cast<std::uint16_t>(fastxlsx::test::read_u16(data, central_offset + 8)
            | 0x0001u));

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-central-encrypted.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject central-directory encrypted flags");
}

void test_package_reader_rejects_local_encrypted_flag()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-local-encrypted-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t local_offset = find_signature(data, 0x04034b50u);
    write_u16(data, local_offset + 6,
        static_cast<std::uint16_t>(fastxlsx::test::read_u16(data, local_offset + 6)
            | 0x0001u));

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-local-encrypted.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject local-header encrypted flags");
}

void test_package_reader_rejects_local_header_method_mismatch()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-local-method-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t local_offset = find_signature(data, 0x04034b50u);
    write_u16(data, local_offset + 8, 8);

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-local-method.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject local-header compression method mismatches");
}

void test_package_reader_rejects_local_header_name_mismatch()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-local-name-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t local_offset = find_signature(data, 0x04034b50u);
    const std::size_t local_name_offset = local_offset + 30u;
    data.at(local_name_offset) = data.at(local_name_offset) == '[' ? 'X' : '[';

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-local-name.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject local-header entry name mismatches");
}

void test_package_reader_rejects_local_header_size_mismatch()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-local-size-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t local_offset = find_signature(data, 0x04034b50u);
    const std::uint32_t compressed_size =
        fastxlsx::test::read_u32(data, local_offset + 18);
    write_u32(data, local_offset + 18, compressed_size + 1u);

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-local-size.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject local-header entry size mismatches");
}

void test_package_reader_rejects_central_data_descriptor_flag()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-central-data-descriptor-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t central_offset = find_signature(data, 0x02014b50u);
    write_u16(data, central_offset + 8,
        static_cast<std::uint16_t>(fastxlsx::test::read_u16(data, central_offset + 8)
            | 0x0008u));

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-central-data-descriptor.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject central-directory data descriptor flags");
}

void test_package_reader_rejects_local_data_descriptor_flag()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-local-data-descriptor-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t local_offset = find_signature(data, 0x04034b50u);
    write_u16(data, local_offset + 6,
        static_cast<std::uint16_t>(fastxlsx::test::read_u16(data, local_offset + 6)
            | 0x0008u));

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-local-data-descriptor.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject local-header data descriptor flags");
}

void test_package_reader_rejects_local_header_crc_mismatch()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-local-crc-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    const std::size_t local_offset = find_signature(data, 0x04034b50u);
    const std::uint32_t crc = fastxlsx::test::read_u32(data, local_offset + 14);
    write_u32(data, local_offset + 14, crc ^ 0xffffffffu);

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-local-crc.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject local-header CRC mismatches");
}

void test_package_reader_rejects_missing_content_types()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-missing-content-types.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path, "PackageReader should require [Content_Types].xml");
}

void test_package_reader_rejects_bad_content_types_xml()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-bad-content-types.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml", R"(<Types><Override PartName="/xl/workbook.xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject malformed content type metadata");

    const std::filesystem::path wrong_root_path =
        output_path("fastxlsx-package-reader-content-types-wrong-root.xlsx");
    fastxlsx::detail::write_package(wrong_root_path,
        {
            {"[Content_Types].xml",
                R"(<Metadata><Types><Default Extension="xml" ContentType="application/xml"/></Types></Metadata>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(wrong_root_path,
        "PackageReader should reject content type metadata with a wrong document root");

    const std::filesystem::path nested_decoy_path =
        output_path("fastxlsx-package-reader-content-types-nested-decoy.xlsx");
    fastxlsx::detail::write_package(nested_decoy_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Metadata><Default Extension="xml" ContentType="application/xml"/></Metadata></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(nested_decoy_path,
        "PackageReader should reject nested decoy content type metadata");

    const std::filesystem::path namespaced_attribute_path =
        output_path("fastxlsx-package-reader-content-types-namespaced-attribute.xlsx");
    fastxlsx::detail::write_package(namespaced_attribute_path,
        {
            {"[Content_Types].xml",
                R"(<Types xmlns:x="urn:fastxlsx:test"><Default x:Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(namespaced_attribute_path,
        "PackageReader should reject namespaced content type metadata attributes");

    const std::filesystem::path duplicate_attribute_path =
        output_path("fastxlsx-package-reader-content-types-duplicate-attribute.xlsx");
    fastxlsx::detail::write_package(duplicate_attribute_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml" ContentType="text/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(duplicate_attribute_path,
        "PackageReader should reject duplicate content type metadata attributes");

    const std::filesystem::path text_child_path =
        output_path("fastxlsx-package-reader-content-types-text-child.xlsx");
    fastxlsx::detail::write_package(text_child_path,
        {
            {"[Content_Types].xml",
                R"(<Types>metadata<Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(text_child_path,
        "PackageReader should reject non-whitespace content type metadata text");

    const std::filesystem::path mismatched_qname_path =
        output_path("fastxlsx-package-reader-content-types-mismatched-qname.xlsx");
    fastxlsx::detail::write_package(mismatched_qname_path,
        {
            {"[Content_Types].xml",
                R"(<x:Types xmlns:x="urn:fastxlsx:test"><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(mismatched_qname_path,
        "PackageReader should reject mismatched content type metadata tag names");
}

void test_package_reader_rejects_conflicting_content_type_defaults()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-conflicting-content-type-defaults.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types>)"
                R"(<Default Extension="xml" ContentType="application/xml"/>)"
                R"(<Default Extension=".XML" ContentType="text/xml"/>)"
                R"(</Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject conflicting content type defaults");
}

void test_package_reader_rejects_conflicting_content_type_overrides()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-conflicting-content-type-overrides.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types>)"
                R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
                R"(<Override PartName="/xl/./workbook.xml" ContentType="application/xml"/>)"
                R"(</Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject conflicting content type overrides");
}

void test_package_reader_rejects_bad_relationships_xml()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-bad-relationships.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="type"/></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject malformed relationship metadata");

    const std::filesystem::path wrong_root_path =
        output_path("fastxlsx-package-reader-relationships-wrong-root.xlsx");
    fastxlsx::detail::write_package(wrong_root_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Metadata><Relationships><Relationship Id="rId1" Type="type" Target="target.xml"/></Relationships></Metadata>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(wrong_root_path,
        "PackageReader should reject relationship metadata with a wrong document root");

    const std::filesystem::path nested_decoy_path =
        output_path("fastxlsx-package-reader-relationships-nested-decoy.xlsx");
    fastxlsx::detail::write_package(nested_decoy_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Metadata><Relationship Id="rId1" Type="type" Target="target.xml"/></Metadata></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(nested_decoy_path,
        "PackageReader should reject nested decoy relationship metadata");

    const std::filesystem::path namespaced_id_path =
        output_path("fastxlsx-package-reader-relationships-namespaced-id.xlsx");
    fastxlsx::detail::write_package(namespaced_id_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships xmlns:x="urn:fastxlsx:test"><Relationship x:Id="rId1" Type="type" Target="target.xml"/></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(namespaced_id_path,
        "PackageReader should reject namespaced relationship id attributes");

    const std::filesystem::path namespaced_target_mode_path =
        output_path("fastxlsx-package-reader-relationships-namespaced-target-mode.xlsx");
    fastxlsx::detail::write_package(namespaced_target_mode_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships xmlns:x="urn:fastxlsx:test"><Relationship Id="rId1" Type="type" Target="https://example.invalid/target.xml" x:TargetMode="External"/></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(namespaced_target_mode_path,
        "PackageReader should reject namespaced relationship TargetMode attributes");

    const std::filesystem::path duplicate_attribute_path =
        output_path("fastxlsx-package-reader-relationships-duplicate-attribute.xlsx");
    fastxlsx::detail::write_package(duplicate_attribute_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Id="rId2" Type="type" Target="target.xml"/></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(duplicate_attribute_path,
        "PackageReader should reject duplicate relationship metadata attributes");

    const std::filesystem::path trailing_text_path =
        output_path("fastxlsx-package-reader-relationships-trailing-text.xlsx");
    fastxlsx::detail::write_package(trailing_text_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="type" Target="target.xml"/></Relationships>metadata)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(trailing_text_path,
        "PackageReader should reject non-whitespace relationship metadata text");

    const std::filesystem::path mismatched_qname_path =
        output_path("fastxlsx-package-reader-relationships-mismatched-qname.xlsx");
    fastxlsx::detail::write_package(mismatched_qname_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="type" Target="target.xml"></x:Relationship></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(mismatched_qname_path,
        "PackageReader should reject mismatched relationship metadata tag names");
}

void test_package_reader_rejects_duplicate_relationship_ids()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-duplicate-relationship-ids.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"_rels/.rels",
                R"(<Relationships>)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>)"
                R"(</Relationships>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject duplicate relationship ids inside one .rels part");
}

void test_package_reader_rejects_duplicate_source_relationship_ids()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-duplicate-source-relationship-ids.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/worksheets/sheet1.xml", "<worksheet/>"},
            {"xl/styles.xml", "<styleSheet/>"},
            {"xl/_rels/workbook.xml.rels",
                R"(<Relationships>)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>)"
                R"(<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>)"
                R"(</Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject duplicate relationship ids inside source-owned .rels");
}

void test_package_reader_rejects_relationships_for_missing_source_part()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-orphan-relationships.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/>)"
                R"(<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>)"
                R"(<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>)"
                R"(</Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
            {"xl/worksheets/_rels/sheet1.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing" Target="../drawings/drawing1.xml"/></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject source-owned relationships without the source part");
}

void test_package_reader_rejects_root_relationships_for_missing_source_part()
{
    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-orphan-root-relationships.xlsx");
    fastxlsx::detail::write_package(path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/>)"
                R"(<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>)"
                R"(</Types>)"},
            {"_rels/root.xml.rels",
                R"(<Relationships><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml" Target="custom/item1.xml"/></Relationships>)"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    expect_open_failure(path,
        "PackageReader should reject root source-owned relationships without the source part");
}

void test_package_reader_rejects_corrupt_entry_crc_on_read()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-crc-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/blob.bin", "opaque"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    corrupt_first_occurrence(data, "opaque");

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-crc-read.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    bool failed = false;
    try {
        (void)reader.read_entry("custom/blob.bin");
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "PackageReader should reject corrupt entry bytes by CRC");
}

void test_package_reader_rejects_corrupt_entry_crc_on_extract()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-extract-crc-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/blob.bin", "opaque"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    corrupt_first_occurrence(data, "opaque");

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-extract-crc-read.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    bool failed = false;
    try {
        reader.extract_entry_to_file("custom/blob.bin",
            output_path("fastxlsx-package-reader-extract-crc-output.bin"));
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "PackageReader should reject corrupt entry bytes during extract");
}

void test_package_reader_rejects_corrupt_entry_crc_on_chunk_source()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-entry-chunks-crc-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml", "<Types/>"},
            {"_rels/.rels", "<Relationships/>"},
            {"xl/workbook.xml", "<workbook/>"},
            {"custom/blob.bin", "opaque"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    corrupt_first_occurrence(data, "opaque");

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-entry-chunks-crc-read.xlsx");
    write_file(path, data);

    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);
    fastxlsx::detail::PackageReaderChunkCallback source =
        reader.entry_chunk_source("custom/blob.bin");

    bool failed = false;
    try {
        std::string chunk;
        while (source(chunk)) {
        }
    } catch (const std::exception&) {
        failed = true;
    }
    check(failed, "PackageReader should reject corrupt entry bytes during chunk source read");
}

void test_package_reader_rejects_corrupt_metadata_crc_on_open()
{
    const std::filesystem::path source_path =
        output_path("fastxlsx-package-reader-crc-metadata-source.xlsx");
    fastxlsx::detail::write_package(source_path,
        {
            {"[Content_Types].xml",
                R"(<Types><Default Extension="xml" ContentType="application/xml"/></Types>)"},
            {"xl/workbook.xml", "<workbook/>"},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});

    std::string data = fastxlsx::test::read_file(source_path);
    corrupt_first_occurrence(data, "application/xml");

    const std::filesystem::path path =
        output_path("fastxlsx-package-reader-crc-metadata.xlsx");
    write_file(path, data);

    expect_open_failure(path,
        "PackageReader should reject corrupt metadata entry bytes by CRC");
}

} // namespace

int main()
{
    try {
        test_package_writer_rejects_empty_package_before_output();
        test_package_writer_rejects_invalid_compression_levels_before_output();
        test_package_writer_rejects_zip64_entry_count_before_output();
        test_package_writer_rejects_zip_entry_name_length_before_output();
        test_package_writer_rejects_invalid_entry_names_before_output();
        test_package_writer_rejects_duplicate_entry_names_before_output();
        test_package_writer_rejects_zip64_file_chunk_before_output();
        test_package_writer_rejects_missing_file_chunk_before_output();
        test_package_writer_rejects_mixed_legacy_data_and_chunks_before_output();
        test_package_writer_rejects_invalid_chunk_sources_before_output();
        test_package_reader_reads_stored_entries_and_unknown_parts();
        test_package_reader_extracts_stored_entry_to_file();
        test_package_reader_streams_stored_entry_chunks();
        test_package_reader_ingests_content_types_and_relationships();
        test_package_reader_resolves_workbook_sheet_catalog();
        test_package_reader_rejects_invalid_workbook_sheet_catalog();
        test_package_reader_ingests_root_source_relationships_as_metadata();
        test_package_reader_ingests_unknown_extension_relationships_as_metadata();
        test_package_reader_rejects_duplicate_entries();
        test_package_reader_rejects_invalid_entry_names();
        test_package_reader_rejects_bad_zip();
        test_package_reader_rejects_central_directory_trailing_data_before_eocd();
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
        test_package_reader_reads_deflated_entries_with_minizip();
        test_package_reader_streams_deflated_entry_chunks_with_minizip();
        test_package_reader_closes_abandoned_deflated_entry_chunk_source();
        test_package_reader_extracts_deflated_entry_to_file_with_minizip();
        test_package_writer_applies_explicit_minizip_compression_levels();
        test_package_reader_rejects_corrupt_deflated_entry_crc_on_read();
        test_package_reader_rejects_corrupt_deflated_entry_crc_on_extract();
        test_package_reader_rejects_corrupt_deflated_entry_crc_on_chunk_source();
#else
        test_package_reader_rejects_compressed_entries_without_minizip();
#endif
        test_package_reader_rejects_central_encrypted_flag();
        test_package_reader_rejects_local_encrypted_flag();
        test_package_reader_rejects_local_header_method_mismatch();
        test_package_reader_rejects_local_header_name_mismatch();
        test_package_reader_rejects_local_header_size_mismatch();
        test_package_reader_rejects_central_data_descriptor_flag();
        test_package_reader_rejects_local_data_descriptor_flag();
        test_package_reader_rejects_local_header_crc_mismatch();
        test_package_reader_rejects_missing_content_types();
        test_package_reader_rejects_bad_content_types_xml();
        test_package_reader_rejects_conflicting_content_type_defaults();
        test_package_reader_rejects_conflicting_content_type_overrides();
        test_package_reader_rejects_bad_relationships_xml();
        test_package_reader_rejects_duplicate_relationship_ids();
        test_package_reader_rejects_duplicate_source_relationship_ids();
        test_package_reader_rejects_relationships_for_missing_source_part();
        test_package_reader_rejects_root_relationships_for_missing_source_part();
        test_package_reader_rejects_corrupt_entry_crc_on_read();
        test_package_reader_rejects_corrupt_entry_crc_on_extract();
        test_package_reader_rejects_corrupt_entry_crc_on_chunk_source();
        test_package_reader_rejects_corrupt_metadata_crc_on_open();
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
