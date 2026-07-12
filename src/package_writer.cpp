#include "package_writer.hpp"

#include "zip_entry_name.hpp"
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
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {
namespace {

constexpr std::size_t io_buffer_size = 1024 * 1024;
constexpr std::uint64_t max_forward_gap_discard_bytes = 64 * 1024;
constexpr std::uint64_t zip32_max_u16 = std::numeric_limits<std::uint16_t>::max();
constexpr std::uint64_t zip32_max_u32 = std::numeric_limits<std::uint32_t>::max();

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

void validate_options(PackageWriterOptions options)
{
    if (options.compression_level == package_writer_default_compression_level) {
        return;
    }

    if (options.compression_level < package_writer_min_compression_level
        || options.compression_level > package_writer_max_compression_level) {
        throw FastXlsxError("ZIP compression level must be -1 or between 0 and 9");
    }
}

const std::array<std::uint32_t, 256>& crc32_table()
{
    static constexpr std::uint32_t polynomial = 0xedb88320u;
    static std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values {};
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

class Crc32Accumulator {
public:
    void update(std::string_view data)
    {
        const auto& table = crc32_table();
        for (const unsigned char byte : data) {
            value_ = (value_ >> 8u) ^ table[(value_ ^ byte) & 0xffu];
        }
    }

    [[nodiscard]] std::uint32_t value() const noexcept
    {
        return value_ ^ 0xffffffffu;
    }

private:
    std::uint32_t value_ = 0xffffffffu;
};

void add_entry_uncompressed_size(std::uint64_t& size, std::uint64_t chunk_size)
{
    if (chunk_size > std::numeric_limits<std::uint64_t>::max() - size) {
        throw FastXlsxError("ZIP entry uncompressed size overflow");
    }
    size += chunk_size;
}

void require_expected_chunk_size(const PackageEntryChunk& chunk, std::uint64_t actual_size)
{
    if (chunk.has_expected_size && chunk.expected_size != actual_size) {
        throw FastXlsxError("ZIP entry chunk size changed after staging: expected "
            + std::to_string(chunk.expected_size) + " bytes, actual "
            + std::to_string(actual_size) + " bytes");
    }
}

void require_expected_chunk_crc32(const PackageEntryChunk& chunk, std::uint32_t actual_crc32)
{
    if (chunk.has_expected_crc32 && chunk.expected_crc32 != actual_crc32) {
        throw FastXlsxError("ZIP entry chunk CRC32 changed after staging: expected "
            + std::to_string(chunk.expected_crc32) + ", actual "
            + std::to_string(actual_crc32));
    }
}

std::string expected_at_least_size_message(
    std::string_view prefix, std::uint64_t expected_size)
{
    const std::string actual_size =
        expected_size == std::numeric_limits<std::uint64_t>::max()
        ? "more than " + std::to_string(expected_size)
        : "at least " + std::to_string(expected_size + 1U);
    return std::string(prefix) + ": expected " + std::to_string(expected_size)
        + " bytes, read " + actual_size + " bytes";
}

std::string expected_actual_size_message(
    std::string_view prefix, std::uint64_t expected_size, std::uint64_t actual_size)
{
    return std::string(prefix) + ": expected " + std::to_string(expected_size)
        + " bytes, actual " + std::to_string(actual_size) + " bytes";
}

std::string chunk_file_failure_message(std::string_view prefix, const PackageEntryChunk& chunk)
{
    std::string message(prefix);
    if (chunk.has_file_range) {
        message += "; range offset ";
        message += std::to_string(chunk.file_offset);
        message += ", length ";
        message += std::to_string(chunk.file_size);
        message += " bytes";
    }
    if (chunk.has_expected_size) {
        message += "; expected ";
        message += std::to_string(chunk.expected_size);
        message += " bytes";
    }
    return message;
}

class FileChunkStatCache {
public:
    std::uint64_t file_size(const PackageEntryChunk& chunk)
    {
        const auto cached = file_sizes_.find(chunk.path);
        if (cached != file_sizes_.end()) {
            return cached->second;
        }

        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(chunk.path, error);
        if (error) {
            throw FastXlsxError(chunk_file_failure_message(
                "failed to stat file-backed ZIP entry chunk", chunk));
        }
        if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::uint64_t>::max())) {
            throw FastXlsxError("file-backed ZIP entry chunk is too large");
        }
        const std::uint64_t measured_size = static_cast<std::uint64_t>(size);
        file_sizes_.emplace(chunk.path, measured_size);
        return measured_size;
    }

private:
    std::map<std::filesystem::path, std::uint64_t> file_sizes_;
};

std::uint64_t file_chunk_data_size_from_stat(
    const PackageEntryChunk& chunk, FileChunkStatCache& stat_cache)
{
    const std::uint64_t measured_size = stat_cache.file_size(chunk);
    if (!chunk.has_file_range) {
        return measured_size;
    }
    if (chunk.file_offset > measured_size
        || chunk.file_size > measured_size - chunk.file_offset) {
        throw FastXlsxError(chunk_file_failure_message(
            "file-backed ZIP entry chunk range exceeds file size", chunk));
    }
    return chunk.file_size;
}

std::uint64_t file_chunk_read_limit(const PackageEntryChunk& chunk)
{
    if (chunk.has_file_range) {
        return chunk.file_size;
    }
    return chunk.expected_size;
}

bool file_chunk_has_read_limit(const PackageEntryChunk& chunk)
{
    return chunk.has_file_range || chunk.has_expected_size;
}

std::string zip_entry_chunk_context(
    std::string_view entry_name, const PackageEntryChunk& chunk, std::size_t chunk_index)
{
    std::string context = "ZIP entry '";
    context += entry_name;
    context += "' chunk ";
    context += std::to_string(chunk_index);
    switch (chunk.kind) {
    case PackageEntryChunk::Kind::Memory:
        context += " (memory)";
        break;
    case PackageEntryChunk::Kind::File:
        context += " (file '";
        context += chunk.path.generic_string();
        if (chunk.has_file_range) {
            context += "' range ";
            context += std::to_string(chunk.file_offset);
            context += "+";
            context += std::to_string(chunk.file_size);
        } else {
            context += "'";
        }
        context += ")";
        break;
    default:
        context += " (unknown)";
        break;
    }
    return context;
}

void wrap_zip_entry_chunk_error(
    std::string_view entry_name,
    const PackageEntryChunk& chunk,
    std::size_t chunk_index,
    const std::exception& error)
{
    throw FastXlsxError(zip_entry_chunk_context(entry_name, chunk, chunk_index)
        + ": " + error.what());
}

std::uint64_t entry_uncompressed_size(
    const PackageEntry& entry, FileChunkStatCache& stat_cache)
{
    if (entry.chunks.empty()) {
        return entry.data.size();
    }

    std::uint64_t size = 0;
    for (std::size_t chunk_index = 0; chunk_index < entry.chunks.size(); ++chunk_index) {
        const PackageEntryChunk& chunk = entry.chunks[chunk_index];
        try {
            switch (chunk.kind) {
            case PackageEntryChunk::Kind::Memory:
                require_expected_chunk_size(chunk,
                    static_cast<std::uint64_t>(chunk.data.size()));
                add_entry_uncompressed_size(size, chunk.data.size());
                break;
            case PackageEntryChunk::Kind::File: {
                const std::uint64_t measured_size =
                    file_chunk_data_size_from_stat(chunk, stat_cache);
                require_expected_chunk_size(chunk, measured_size);
                add_entry_uncompressed_size(size, measured_size);
                break;
            }
            }
        } catch (const std::exception& error) {
            wrap_zip_entry_chunk_error(entry.name, chunk, chunk_index, error);
        }
    }
    return size;
}

void validate_entry_chunk_sources(const PackageEntry& entry, FileChunkStatCache& stat_cache)
{
    for (std::size_t chunk_index = 0; chunk_index < entry.chunks.size(); ++chunk_index) {
        const PackageEntryChunk& chunk = entry.chunks[chunk_index];
        try {
            switch (chunk.kind) {
            case PackageEntryChunk::Kind::Memory:
                if (!chunk.path.empty()) {
                    throw FastXlsxError("ZIP entry chunk cannot mix memory and file sources");
                }
                if (chunk.has_file_range) {
                    throw FastXlsxError("ZIP entry memory chunk cannot carry a file range");
                }
                require_expected_chunk_size(chunk,
                    static_cast<std::uint64_t>(chunk.data.size()));
                require_expected_chunk_crc32(chunk, crc32(chunk.data));
                break;
            case PackageEntryChunk::Kind::File:
                if (!chunk.data.empty()) {
                    throw FastXlsxError("ZIP entry chunk cannot mix memory and file sources");
                }
                (void)file_chunk_data_size_from_stat(chunk, stat_cache);
                break;
            default:
                throw FastXlsxError("unsupported ZIP entry chunk kind");
            }
        } catch (const std::exception& error) {
            wrap_zip_entry_chunk_error(entry.name, chunk, chunk_index, error);
        }
    }
}

void validate_package_entries_zip32(const std::vector<PackageEntry>& entries)
{
    if (entries.empty()) {
        throw FastXlsxError("cannot write an empty ZIP package");
    }

    if (entries.size() > zip32_max_u16) {
        throw FastXlsxError("ZIP entry count requires Zip64 support");
    }

    std::set<std::string_view> seen_entry_names;
    FileChunkStatCache stat_cache;
    for (const PackageEntry& entry : entries) {
        if (entry.name.empty()) {
            throw FastXlsxError("ZIP entry name cannot be empty");
        }
        if (entry.name.size() > zip32_max_u16) {
            throw FastXlsxError("ZIP entry name length exceeds 16-bit ZIP field limit");
        }
        validate_zip_entry_name(entry.name);
        if (!seen_entry_names.emplace(entry.name).second) {
            throw FastXlsxError(std::string("duplicate ZIP entry name: ") + entry.name);
        }
        if (!entry.data.empty() && !entry.chunks.empty()) {
            throw FastXlsxError("ZIP entry cannot mix legacy data and chunked payload");
        }
        validate_entry_chunk_sources(entry, stat_cache);
        if (entry_uncompressed_size(entry, stat_cache) > zip32_max_u32) {
            throw FastXlsxError("ZIP entry uncompressed size requires Zip64 support");
        }
    }
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

class MinizipFileChunkReadCache {
public:
    std::ifstream& open_for_payload(
        const PackageEntryChunk& chunk, std::vector<char>& scratch_buffer)
    {
        ensure_current_file(chunk);
        const std::uint64_t offset = chunk.has_file_range ? chunk.file_offset : 0;
        if (offset > static_cast<std::uint64_t>(
                std::numeric_limits<std::streamoff>::max())) {
            throw FastXlsxError(chunk_file_failure_message(
                "file-backed ZIP entry chunk range offset is too large", chunk));
        }

        stream_.clear();
        if (current_stream_position_valid_) {
            if (offset == current_stream_offset_) {
                return stream_;
            }
            if (offset > current_stream_offset_
                && offset - current_stream_offset_ <= max_forward_gap_discard_bytes) {
                discard_forward(
                    chunk, offset - current_stream_offset_, scratch_buffer);
                return stream_;
            }
        }

        stream_.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!stream_) {
            throw FastXlsxError(chunk_file_failure_message(
                "failed to seek file-backed ZIP entry chunk range", chunk));
        }
        current_stream_offset_ = offset;
        current_stream_position_valid_ = true;
        return stream_;
    }

    void note_payload_bytes_read(std::uint64_t size) noexcept
    {
        if (current_stream_position_valid_) {
            current_stream_offset_ += size;
        }
    }

private:
    void discard_forward(const PackageEntryChunk& chunk,
        std::uint64_t size, std::vector<char>& scratch_buffer)
    {
        std::uint64_t discarded = 0;
        while (discarded < size) {
            const std::size_t requested = static_cast<std::size_t>(
                std::min<std::uint64_t>(size - discarded,
                    static_cast<std::uint64_t>(scratch_buffer.size())));
            stream_.read(scratch_buffer.data(), static_cast<std::streamsize>(requested));
            const std::streamsize read_size = stream_.gcount();
            if (stream_.bad()) {
                throw FastXlsxError(chunk_file_failure_message(
                    "failed to read skipped file-backed ZIP entry chunk bytes", chunk));
            }
            if (read_size <= 0) {
                throw FastXlsxError(expected_actual_size_message(
                    "file-backed ZIP entry chunk changed before requested range",
                    size, discarded));
            }
            const auto read_bytes = static_cast<std::uint64_t>(read_size);
            discarded += read_bytes;
            current_stream_offset_ += read_bytes;
        }
    }

    void ensure_current_file(const PackageEntryChunk& chunk)
    {
        if (has_current_path_ && current_path_ == chunk.path && stream_.is_open()) {
            return;
        }

        stream_.close();
        stream_.clear();
        current_path_ = chunk.path;
        has_current_path_ = true;
        stream_.open(chunk.path, std::ios::binary);
        if (!stream_) {
            throw FastXlsxError(chunk_file_failure_message(
                "failed to open file-backed ZIP entry chunk", chunk));
        }
        current_stream_offset_ = 0;
        current_stream_position_valid_ = true;
    }

    bool has_current_path_ = false;
    std::filesystem::path current_path_;
    std::uint64_t current_stream_offset_ = 0;
    bool current_stream_position_valid_ = false;
    std::ifstream stream_;
};

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

void write_minizip_file_chunk(void* writer, MinizipFileChunkReadCache& file_cache,
    const PackageEntryChunk& chunk, std::vector<char>& buffer)
{
    std::ifstream& stream = file_cache.open_for_payload(chunk, buffer);

    Crc32Accumulator chunk_crc;
    std::uint64_t written_size = 0;
    const bool has_read_limit = file_chunk_has_read_limit(chunk);
    const std::uint64_t read_limit = file_chunk_read_limit(chunk);
    while (true) {
        if (has_read_limit && written_size == read_limit) {
            if (!chunk.has_file_range) {
                const int next = stream.peek();
                if (next != std::char_traits<char>::eof()) {
                    throw FastXlsxError(expected_at_least_size_message(
                        "file-backed ZIP entry chunk produced more bytes than expected",
                        read_limit));
                }
                if (stream.bad()) {
                    throw FastXlsxError("failed to read file-backed ZIP entry chunk");
                }
            }
            break;
        }

        std::size_t requested = buffer.size();
        if (has_read_limit) {
            requested = static_cast<std::size_t>(
                std::min<std::uint64_t>(read_limit - written_size,
                    static_cast<std::uint64_t>(buffer.size())));
        }
        stream.read(buffer.data(), static_cast<std::streamsize>(requested));
        const std::streamsize read_size = stream.gcount();
        if (stream.bad()) {
            throw FastXlsxError("failed to read file-backed ZIP entry chunk");
        }
        if (read_size <= 0) {
            if (has_read_limit) {
                throw FastXlsxError(expected_actual_size_message(
                    "file-backed ZIP entry chunk ended before expected bytes",
                    read_limit, written_size));
            }
            break;
        }
        const int chunk_size = static_cast<int>(read_size);
        const int written = mz_zip_writer_entry_write(writer, buffer.data(), chunk_size);
        if (written != chunk_size) {
            throw FastXlsxError("minizip-ng failed to write file-backed ZIP entry data");
        }
        chunk_crc.update(std::string_view(buffer.data(), static_cast<std::size_t>(read_size)));
        written_size += static_cast<std::uint64_t>(read_size);
    }
    file_cache.note_payload_bytes_read(written_size);
    require_expected_chunk_size(chunk, written_size);
    require_expected_chunk_crc32(chunk, chunk_crc.value());
}

void write_minizip_entry_chunks(void* writer, const PackageEntry& entry)
{
    if (entry.chunks.empty()) {
        write_minizip_memory_chunk(writer, entry.data);
        return;
    }

    std::vector<char> file_buffer(io_buffer_size);
    MinizipFileChunkReadCache file_cache;
    for (std::size_t chunk_index = 0; chunk_index < entry.chunks.size(); ++chunk_index) {
        const PackageEntryChunk& chunk = entry.chunks[chunk_index];
        try {
            switch (chunk.kind) {
            case PackageEntryChunk::Kind::Memory:
                write_minizip_memory_chunk(writer, chunk.data);
                break;
            case PackageEntryChunk::Kind::File:
                write_minizip_file_chunk(writer, file_cache, chunk, file_buffer);
                break;
            default:
                throw FastXlsxError("unsupported ZIP entry chunk kind");
            }
        } catch (const std::exception& error) {
            wrap_zip_entry_chunk_error(entry.name, chunk, chunk_index, error);
        }
    }
}

void write_minizip_package(
    const std::filesystem::path& path, const std::vector<PackageEntry>& entries,
    int compression_level)
{
    if (entries.empty()) {
        throw FastXlsxError("cannot write an empty ZIP package");
    }

    std::unique_ptr<void, MinizipWriterDeleter> writer(mz_zip_writer_create());
    if (!writer) {
        throw FastXlsxError("failed to create minizip-ng writer");
    }

    const int minizip_compression_level =
        compression_level == package_writer_default_compression_level
        ? MZ_COMPRESS_LEVEL_DEFAULT
        : compression_level;

    mz_zip_writer_set_compress_method(writer.get(), MZ_COMPRESS_METHOD_DEFLATE);
    mz_zip_writer_set_compress_level(writer.get(), minizip_compression_level);

    const std::string output_path = path_to_utf8(path);
    check_minizip_result(mz_zip_writer_open_file(writer.get(), output_path.c_str(), 0, 0),
        "open XLSX package");
    void* zip_handle = nullptr;
    check_minizip_result(
        mz_zip_writer_get_zip_handle(writer.get(), &zip_handle), "get ZIP writer handle");
    check_minizip_result(
        mz_zip_set_data_descriptor(zip_handle, 0), "disable ZIP data descriptors");

    try {
        FileChunkStatCache stat_cache;
        for (const PackageEntry& entry : entries) {
            if (entry.name.empty()) {
                throw FastXlsxError("ZIP entry name cannot be empty");
            }

            mz_zip_file file_info {};
            file_info.filename = entry.name.c_str();
            file_info.flag = MZ_ZIP_FLAG_UTF8;
            file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;
            file_info.modified_date = 0;
            file_info.uncompressed_size =
                static_cast<std::int64_t>(entry_uncompressed_size(entry, stat_cache));

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
    validate_options(options);
    validate_package_entries_zip32(entries);

    switch (resolve_backend(options.backend)) {
    case PackageWriterBackend::StoredZipBootstrap:
        write_stored_zip(path, entries);
        return;
    case PackageWriterBackend::MinizipNg:
#ifdef FASTXLSX_HAS_MINIZIP_NG
        write_minizip_package(path, entries, options.compression_level);
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
