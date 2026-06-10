#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
#endif

namespace fastxlsx::test {

// Directory for all test-generated artifacts (xlsx, png, bin, ...).
// Routed to a dedicated subdirectory under the system temp path so that
// running test executables directly (current_path() == repo root) does not
// litter the project tree. Created on first use.
inline const std::filesystem::path& artifact_dir()
{
    static const std::filesystem::path dir = [] {
        std::error_code ec;
        std::filesystem::path base = std::filesystem::temp_directory_path(ec);
        if (ec) {
            base = std::filesystem::current_path();
        }
        std::filesystem::path path = base / "fastxlsx-test-artifacts";
        std::filesystem::create_directories(path, ec);
        return path;
    }();
    return dir;
}

// Resolve a test artifact path by file name inside artifact_dir().
inline std::filesystem::path artifact_path(std::string_view name)
{
    return artifact_dir() / std::filesystem::path(name);
}

inline void insert_zip_entry(
    std::map<std::string, std::string>& entries, std::string name, std::string body)
{
    auto [_, inserted] = entries.emplace(std::move(name), std::move(body));
    if (!inserted) {
        throw std::runtime_error("duplicate ZIP entry");
    }
}

inline std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("failed to open generated xlsx");
    }
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

inline std::uint16_t read_u16(const std::string& data, std::size_t offset)
{
    return static_cast<std::uint16_t>(
        (static_cast<unsigned char>(data[offset + 1]) << 8u)
        | static_cast<unsigned char>(data[offset]));
}

inline std::uint32_t read_u32(const std::string& data, std::size_t offset)
{
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 3])) << 24u)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 2])) << 16u)
        | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset + 1])) << 8u)
        | static_cast<std::uint32_t>(static_cast<unsigned char>(data[offset]));
}

inline std::map<std::string, std::string> read_stored_zip_entries(
    const std::filesystem::path& path)
{
    const std::string data = read_file(path);
    if (data.size() < 22) {
        throw std::runtime_error("zip is too small");
    }

    std::size_t eocd_offset = std::string::npos;
    for (std::size_t offset = data.size() - 22; offset != static_cast<std::size_t>(-1); --offset) {
        if (read_u32(data, offset) == 0x06054b50u) {
            eocd_offset = offset;
            break;
        }
        if (offset == 0) {
            break;
        }
    }
    if (eocd_offset == std::string::npos) {
        throw std::runtime_error("zip end of central directory not found");
    }

    const std::uint16_t entry_count = read_u16(data, eocd_offset + 10);
    const std::uint32_t central_offset = read_u32(data, eocd_offset + 16);
    std::size_t offset = central_offset;

    std::map<std::string, std::string> entries;
    for (std::uint16_t index = 0; index < entry_count; ++index) {
        if (read_u32(data, offset) != 0x02014b50u) {
            throw std::runtime_error("central directory signature mismatch");
        }

        const std::uint16_t method = read_u16(data, offset + 10);
        const std::uint32_t compressed_size = read_u32(data, offset + 20);
        const std::uint32_t uncompressed_size = read_u32(data, offset + 24);
        const std::uint16_t name_length = read_u16(data, offset + 28);
        const std::uint16_t extra_length = read_u16(data, offset + 30);
        const std::uint16_t comment_length = read_u16(data, offset + 32);
        const std::uint32_t local_offset = read_u32(data, offset + 42);
        const std::string name = data.substr(offset + 46, name_length);

        if (method != 0) {
            throw std::runtime_error("bootstrap ZIP reader only supports stored entries");
        }
        if (compressed_size != uncompressed_size) {
            throw std::runtime_error("stored entry sizes should match");
        }
        if (read_u32(data, local_offset) != 0x04034b50u) {
            throw std::runtime_error("local header signature mismatch");
        }

        const std::uint16_t local_name_length = read_u16(data, local_offset + 26);
        const std::uint16_t local_extra_length = read_u16(data, local_offset + 28);
        const std::size_t body_offset = local_offset + 30u + local_name_length + local_extra_length;
        insert_zip_entry(entries, name, data.substr(body_offset, uncompressed_size));

        offset += 46u + name_length + extra_length + comment_length;
    }

    return entries;
}

#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG

inline std::string path_to_utf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

inline std::string minizip_error(const char* operation, int result)
{
    return std::string(operation) + " (minizip-ng error " + std::to_string(result) + ")";
}

struct MinizipReaderDeleter {
    void operator()(void* reader) const
    {
        if (reader != nullptr) {
            mz_zip_reader_delete(&reader);
        }
    }
};

inline std::map<std::string, std::string> read_minizip_entries(
    const std::filesystem::path& path)
{
    std::unique_ptr<void, MinizipReaderDeleter> reader(mz_zip_reader_create());
    if (!reader) {
        throw std::runtime_error("failed to create minizip-ng reader");
    }

    const std::string input_path = path_to_utf8(path);
    int result = mz_zip_reader_open_file(reader.get(), input_path.c_str());
    if (result != MZ_OK) {
        throw std::runtime_error(minizip_error("failed to open generated xlsx", result));
    }

    std::map<std::string, std::string> entries;
    result = mz_zip_reader_goto_first_entry(reader.get());
    while (result == MZ_OK) {
        mz_zip_file* file_info = nullptr;
        result = mz_zip_reader_entry_get_info(reader.get(), &file_info);
        if (result != MZ_OK || file_info == nullptr || file_info->filename == nullptr) {
            throw std::runtime_error(minizip_error("failed to read ZIP entry info", result));
        }
        const std::string name = file_info->filename;

        const int length = mz_zip_reader_entry_save_buffer_length(reader.get());
        if (length < 0) {
            throw std::runtime_error(minizip_error("failed to get ZIP entry length", length));
        }

        std::string data(static_cast<std::size_t>(length), '\0');
        result = mz_zip_reader_entry_save_buffer(reader.get(), data.data(), length);
        if (result != MZ_OK) {
            throw std::runtime_error(minizip_error("failed to read ZIP entry data", result));
        }

        insert_zip_entry(entries, name, std::move(data));
        result = mz_zip_reader_goto_next_entry(reader.get());
    }

    if (result != MZ_END_OF_LIST) {
        throw std::runtime_error(minizip_error("failed to iterate ZIP entries", result));
    }

    result = mz_zip_reader_close(reader.get());
    if (result != MZ_OK) {
        throw std::runtime_error(minizip_error("failed to close generated xlsx", result));
    }

    return entries;
}

#endif

inline std::map<std::string, std::string> read_zip_entries(const std::filesystem::path& path)
{
#ifdef FASTXLSX_TEST_HAS_MINIZIP_NG
    return read_minizip_entries(path);
#else
    return read_stored_zip_entries(path);
#endif
}

} // namespace fastxlsx::test
