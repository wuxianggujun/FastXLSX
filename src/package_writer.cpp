#include "package_writer.hpp"

#include "zip_store_writer.hpp"

#include <fastxlsx/workbook.hpp>

#ifdef FASTXLSX_HAS_MINIZIP_NG
#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <string_view>

namespace fastxlsx::detail {
namespace {

constexpr std::size_t io_buffer_size = 64 * 1024;

PackageWriterBackend resolve_backend(PackageWriterBackend backend)
{
    if (backend != PackageWriterBackend::Auto) {
        return backend;
    }

#ifdef FASTXLSX_HAS_MINIZIP_NG
    return PackageWriterBackend::MinizipNg;
#else
    return PackageWriterBackend::StoredZipBootstrap;
#endif
}

#ifdef FASTXLSX_HAS_MINIZIP_NG

std::string path_to_utf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

void check_minizip_result(int result, const char* operation)
{
    if (result != MZ_OK) {
        throw FastXlsxError(std::string("minizip-ng failed to ") + operation + " (error "
            + std::to_string(result) + ")");
    }
}

struct MinizipWriterDeleter {
    void operator()(void* writer) const
    {
        if (writer != nullptr) {
            mz_zip_writer_delete(&writer);
        }
    }
};

std::uint64_t entry_uncompressed_size(const PackageEntry& entry)
{
    if (entry.chunks.empty()) {
        return entry.data.size();
    }

    std::uint64_t size = 0;
    for (const PackageEntryChunk& chunk : entry.chunks) {
        switch (chunk.kind) {
        case PackageEntryChunk::Kind::Memory:
            size += chunk.data.size();
            break;
        case PackageEntryChunk::Kind::File: {
            std::error_code error;
            const auto file_size = std::filesystem::file_size(chunk.path, error);
            if (error) {
                throw FastXlsxError("failed to stat file-backed ZIP entry chunk");
            }
            size += static_cast<std::uint64_t>(file_size);
            break;
        }
        }
    }
    return size;
}

void write_minizip_memory_chunk(void* writer, std::string_view data)
{
    std::size_t offset = 0;
    while (offset < data.size()) {
        const std::size_t remaining = data.size() - offset;
        const int chunk_size = static_cast<int>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int written =
            mz_zip_writer_entry_write(writer, data.data() + offset, chunk_size);
        if (written != chunk_size) {
            throw FastXlsxError("minizip-ng failed to write ZIP entry data");
        }
        offset += static_cast<std::size_t>(written);
    }
}

void write_minizip_file_chunk(void* writer, const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw FastXlsxError("failed to open file-backed ZIP entry chunk");
    }

    std::array<char, io_buffer_size> buffer {};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read_size = stream.gcount();
        if (read_size > 0) {
            const int chunk_size = static_cast<int>(read_size);
            const int written = mz_zip_writer_entry_write(writer, buffer.data(), chunk_size);
            if (written != chunk_size) {
                throw FastXlsxError("minizip-ng failed to write file-backed ZIP entry data");
            }
        }
    }

    if (stream.bad()) {
        throw FastXlsxError("failed to read file-backed ZIP entry chunk");
    }
}

void write_minizip_entry_chunks(void* writer, const PackageEntry& entry)
{
    if (entry.chunks.empty()) {
        write_minizip_memory_chunk(writer, entry.data);
        return;
    }

    for (const PackageEntryChunk& chunk : entry.chunks) {
        switch (chunk.kind) {
        case PackageEntryChunk::Kind::Memory:
            write_minizip_memory_chunk(writer, chunk.data);
            break;
        case PackageEntryChunk::Kind::File:
            write_minizip_file_chunk(writer, chunk.path);
            break;
        }
    }
}

void write_minizip_package(const std::filesystem::path& path, const std::vector<PackageEntry>& entries)
{
    if (entries.empty()) {
        throw FastXlsxError("cannot write an empty ZIP package");
    }

    std::unique_ptr<void, MinizipWriterDeleter> writer(mz_zip_writer_create());
    if (!writer) {
        throw FastXlsxError("failed to create minizip-ng writer");
    }

    mz_zip_writer_set_compress_method(writer.get(), MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(writer.get(), MZ_COMPRESS_LEVEL_DEFAULT);

    const std::string output_path = path_to_utf8(path);
    check_minizip_result(mz_zip_writer_open_file(writer.get(), output_path.c_str(), 0, 0),
        "open XLSX package");
    void* zip_handle = nullptr;
    check_minizip_result(
        mz_zip_writer_get_zip_handle(writer.get(), &zip_handle), "get ZIP writer handle");
    check_minizip_result(
        mz_zip_set_data_descriptor(zip_handle, 0), "disable ZIP data descriptors");

    try {
        for (const PackageEntry& entry : entries) {
            if (entry.name.empty()) {
                throw FastXlsxError("ZIP entry name cannot be empty");
            }

            mz_zip_file file_info {};
            file_info.filename = entry.name.c_str();
            file_info.flag = MZ_ZIP_FLAG_UTF8;
            file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
            file_info.modified_date = 0;
            file_info.uncompressed_size = static_cast<std::int64_t>(entry_uncompressed_size(entry));

            check_minizip_result(mz_zip_writer_entry_open(writer.get(), &file_info),
                "open ZIP entry");

            write_minizip_entry_chunks(writer.get(), entry);

            check_minizip_result(mz_zip_writer_entry_close(writer.get()), "close ZIP entry");
        }

        check_minizip_result(mz_zip_writer_close(writer.get()), "close XLSX package");
    } catch (...) {
        (void)mz_zip_writer_close(writer.get());
        throw;
    }
}

#endif

} // namespace

void write_package(const std::filesystem::path& path, const std::vector<PackageEntry>& entries,
    PackageWriterOptions options)
{
    switch (resolve_backend(options.backend)) {
    case PackageWriterBackend::StoredZipBootstrap:
        write_stored_zip(path, entries);
        return;
    case PackageWriterBackend::MinizipNg:
#ifdef FASTXLSX_HAS_MINIZIP_NG
        write_minizip_package(path, entries);
        return;
#else
        throw FastXlsxError("minizip-ng package writer backend is not enabled");
#endif
    case PackageWriterBackend::Auto:
        break;
    }

    throw FastXlsxError("unsupported package writer backend");
}

} // namespace fastxlsx::detail
