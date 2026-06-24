#pragma once

// Common helpers for PackageReader tests.

#include "../src/package_reader.hpp"
#include "../src/package_writer.hpp"
#include "../src/zip_store_writer.hpp"
#include <fastxlsx/detail/cell_store.hpp>
#include <fastxlsx/detail/materialized_worksheet_session.hpp>
#include <fastxlsx/fastxlsx.hpp>
#include "zip_test_utils.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
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

void check_zip_entry_crc_mismatch_diagnostics(
    std::string_view error, std::string_view entry_name, const char* context)
{
    check_contains(error, "ZIP entry '", context);
    check_contains(error, entry_name, context);
    check_contains(error, "' CRC mismatch", context);
    check_contains(error, "expected ", context);
    check_contains(error, "actual ", context);
}

void check_zip_entry_chunk_source_progress_diagnostics(std::string_view error,
    std::size_t read_attempt,
    std::size_t emitted_chunks,
    std::uint64_t emitted_bytes,
    std::uint64_t last_chunk_bytes,
    const char* context)
{
    check_contains(error,
        std::string("ZIP entry chunk-source read attempt ") + std::to_string(read_attempt),
        context);
    check_contains(error,
        std::string("after emitting ") + std::to_string(emitted_chunks) + " chunk",
        context);
    check_contains(error, std::to_string(emitted_bytes) + " bytes", context);
    if (emitted_chunks > 0) {
        check_contains(error,
            std::string("last chunk ") + std::to_string(last_chunk_bytes) + " bytes",
            context);
    }
}

void check_zip_entry_chunk_consumer_progress_diagnostics(std::string_view error,
    std::string_view operation,
    std::size_t read_attempt,
    std::size_t consumed_chunks,
    std::uint64_t consumed_bytes,
    std::uint64_t last_chunk_bytes,
    const char* context)
{
    check_contains(error,
        std::string(operation) + " read attempt " + std::to_string(read_attempt),
        context);
    check_contains(error,
        std::string("after consuming ") + std::to_string(consumed_chunks) + " chunk",
        context);
    check_contains(error, std::to_string(consumed_bytes) + " bytes", context);
    if (consumed_chunks > 0) {
        check_contains(error,
            std::string("last chunk ") + std::to_string(last_chunk_bytes) + " bytes",
            context);
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

fastxlsx::detail::PackageReaderChunkCallback make_package_reader_test_chunk_source(
    std::vector<std::string> chunks)
{
    return [chunks, index = std::size_t {0}](std::string& output_chunk) mutable -> bool {
        output_chunk.clear();
        if (index == chunks.size()) {
            return false;
        }
        output_chunk = chunks[index++];
        return true;
    };
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

void append_u16(std::string& data, std::uint16_t value)
{
    data.push_back(static_cast<char>(value & 0xffu));
    data.push_back(static_cast<char>((value >> 8u) & 0xffu));
}

void append_u32(std::string& data, std::uint32_t value)
{
    data.push_back(static_cast<char>(value & 0xffu));
    data.push_back(static_cast<char>((value >> 8u) & 0xffu));
    data.push_back(static_cast<char>((value >> 16u) & 0xffu));
    data.push_back(static_cast<char>((value >> 24u) & 0xffu));
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

std::string add_data_descriptor_to_entry(std::string data, std::string_view name)
{
    const ZipEntryLocation location = find_zip_entry_location(data, name);
    const std::uint32_t crc32 =
        fastxlsx::test::read_u32(data, location.central_offset + 16u);

    write_u16(data, location.local_offset + 6u,
        static_cast<std::uint16_t>(
            fastxlsx::test::read_u16(data, location.local_offset + 6u) | 0x0008u));
    write_u32(data, location.local_offset + 14u, 0u);
    write_u32(data, location.local_offset + 18u, 0u);
    write_u32(data, location.local_offset + 22u, 0u);
    write_u16(data, location.central_offset + 8u,
        static_cast<std::uint16_t>(
            fastxlsx::test::read_u16(data, location.central_offset + 8u) | 0x0008u));

    std::string descriptor(16u, '\0');
    write_u32(descriptor, 0u, 0x08074b50u);
    write_u32(descriptor, 4u, crc32);
    write_u32(descriptor, 8u, location.compressed_size);
    write_u32(descriptor, 12u, location.uncompressed_size);

    data.insert(location.data_offset + location.compressed_size, descriptor);
    const std::size_t eocd_offset = find_end_of_central_directory(data);
    const std::uint32_t central_offset =
        fastxlsx::test::read_u32(data, eocd_offset + 16u);
    write_u32(data, eocd_offset + 16u,
        central_offset + static_cast<std::uint32_t>(descriptor.size()));
    return data;
}

std::string make_directory_local_header(std::string_view name)
{
    if (name.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw TestFailure("test ZIP directory entry name is too long");
    }

    std::string record;
    append_u32(record, 0x04034b50u);
    append_u16(record, 20u);
    append_u16(record, 0u);
    append_u16(record, 0u);
    append_u16(record, 0u);
    append_u16(record, 0u);
    append_u32(record, 0u);
    append_u32(record, 0u);
    append_u32(record, 0u);
    append_u16(record, static_cast<std::uint16_t>(name.size()));
    append_u16(record, 0u);
    record.append(name);
    return record;
}

std::string make_directory_central_record(
    std::string_view name, std::uint32_t local_header_offset)
{
    if (name.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw TestFailure("test ZIP directory entry name is too long");
    }

    std::string record;
    append_u32(record, 0x02014b50u);
    append_u16(record, 20u);
    append_u16(record, 20u);
    append_u16(record, 0u);
    append_u16(record, 0u);
    append_u16(record, 0u);
    append_u16(record, 0u);
    append_u32(record, 0u);
    append_u32(record, 0u);
    append_u32(record, 0u);
    append_u16(record, static_cast<std::uint16_t>(name.size()));
    append_u16(record, 0u);
    append_u16(record, 0u);
    append_u16(record, 0u);
    append_u16(record, 0u);
    append_u32(record, 0x10u);
    append_u32(record, local_header_offset);
    record.append(name);
    return record;
}

std::string add_zip_directory_entry(std::string data, std::string_view name)
{
    const std::size_t eocd_offset = find_end_of_central_directory(data);
    const std::uint16_t entry_count = fastxlsx::test::read_u16(data, eocd_offset + 10u);
    const std::uint32_t central_size = fastxlsx::test::read_u32(data, eocd_offset + 12u);
    const std::uint32_t central_offset = fastxlsx::test::read_u32(data, eocd_offset + 16u);

    const std::string local_record = make_directory_local_header(name);
    data.insert(central_offset, local_record);

    const std::uint32_t directory_local_offset = central_offset;
    const std::uint32_t new_central_offset =
        central_offset + static_cast<std::uint32_t>(local_record.size());
    const std::string central_record =
        make_directory_central_record(name, directory_local_offset);
    data.insert(new_central_offset + central_size, central_record);

    const std::size_t new_eocd_offset =
        eocd_offset + local_record.size() + central_record.size();
    const std::uint16_t new_entry_count = static_cast<std::uint16_t>(entry_count + 1u);
    write_u16(data, new_eocd_offset + 8u, new_entry_count);
    write_u16(data, new_eocd_offset + 10u, new_entry_count);
    write_u32(data, new_eocd_offset + 12u,
        central_size + static_cast<std::uint32_t>(central_record.size()));
    write_u32(data, new_eocd_offset + 16u, new_central_offset);
    return data;
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

void expect_open_failure_contains(
    const std::filesystem::path& path, std::string_view needle, const char* message)
{
    bool failed = false;
    try {
        (void)fastxlsx::detail::PackageReader::open(path);
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), needle, message);
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

void expect_workbook_sheets_failure_contains(
    const std::filesystem::path& path, std::string_view needle, const char* message)
{
    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(path);

    bool failed = false;
    try {
        (void)reader.workbook_sheets();
    } catch (const std::exception& error) {
        failed = true;
        check_contains(error.what(), needle, message);
    }
    check(failed, message);
}
