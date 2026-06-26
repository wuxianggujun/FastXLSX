#include "zip_store_writer.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <fastxlsx/workbook.hpp>

namespace fastxlsx::detail {
namespace {

struct CentralDirectoryRecord {
    std::string name;
    std::uint32_t crc = 0;
    std::uint32_t size = 0;
    std::uint32_t local_header_offset = 0;
};

struct ZipEntryStats {
    std::uint64_t size = 0;
    std::uint32_t crc = 0;
};

constexpr std::size_t io_buffer_size = 1024 * 1024;
constexpr std::uint64_t max_forward_gap_discard_bytes = 64 * 1024;

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

std::uint16_t checked_u16(std::uint64_t value, const char* field)
{
    if (value > std::numeric_limits<std::uint16_t>::max()) {
        throw FastXlsxError(std::string("ZIP field exceeds 16-bit limit: ") + field);
    }
    return static_cast<std::uint16_t>(value);
}

std::uint32_t checked_u32(std::uint64_t value, const char* field)
{
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw FastXlsxError(std::string("ZIP field exceeds 32-bit limit: ") + field);
    }
    return static_cast<std::uint32_t>(value);
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

class FileChunkReadCache {
public:
    std::uint64_t payload_size(const PackageEntryChunk& chunk)
    {
        ensure_current_file(chunk);
        if (!chunk.has_file_range) {
            return current_file_size_;
        }
        if (chunk.file_offset > current_file_size_
            || chunk.file_size > current_file_size_ - chunk.file_offset) {
            throw FastXlsxError(chunk_file_failure_message(
                "file-backed ZIP entry chunk range exceeds file size", chunk));
        }
        return chunk.file_size;
    }

    std::ifstream& open_for_payload(const PackageEntryChunk& chunk)
    {
        ensure_current_file(chunk);
        const std::uint64_t offset = chunk.has_file_range ? chunk.file_offset : 0;
        if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
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
                discard_forward(chunk, offset - current_stream_offset_);
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
        if (!current_stream_position_valid_) {
            return;
        }
        current_stream_offset_ += size;
    }

    std::vector<char>& scratch_buffer() noexcept
    {
        return scratch_buffer_;
    }

private:
    void discard_forward(const PackageEntryChunk& chunk, std::uint64_t size)
    {
        std::uint64_t discarded = 0;
        while (discarded < size) {
            const std::size_t requested = static_cast<std::size_t>(
                std::min<std::uint64_t>(
                    size - discarded, static_cast<std::uint64_t>(scratch_buffer_.size())));
            stream_.read(scratch_buffer_.data(), static_cast<std::streamsize>(requested));
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

        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(chunk.path, error);
        if (error) {
            throw FastXlsxError(chunk_file_failure_message(
                "failed to stat file-backed ZIP entry chunk", chunk));
        }
        if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::uint64_t>::max())) {
            throw FastXlsxError("file-backed ZIP entry chunk is too large");
        }
        current_file_size_ = static_cast<std::uint64_t>(size);

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
    std::uint64_t current_file_size_ = 0;
    std::uint64_t current_stream_offset_ = 0;
    bool current_stream_position_valid_ = false;
    std::ifstream stream_;
    std::vector<char> scratch_buffer_ = std::vector<char>(io_buffer_size);
};

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

void add_entry_stats_size(ZipEntryStats& stats, std::uint64_t chunk_size)
{
    if (chunk_size > std::numeric_limits<std::uint64_t>::max() - stats.size) {
        throw FastXlsxError("ZIP entry data size overflow while writing");
    }
    stats.size += chunk_size;
}

void update_written_entry_stats(
    ZipEntryStats& stats, Crc32Accumulator& crc, std::string_view data)
{
    crc.update(data);
    add_entry_stats_size(stats, static_cast<std::uint64_t>(data.size()));
}

void seek_output(std::ofstream& stream, std::uint64_t offset, const char* purpose)
{
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        throw FastXlsxError(std::string("ZIP output seek offset is too large while ")
            + purpose);
    }
    stream.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream) {
        throw FastXlsxError(std::string("failed to seek XLSX package while ") + purpose);
    }
}

void patch_local_header_stats(std::ofstream& stream, std::uint32_t local_header_offset,
    std::uint32_t crc, std::uint32_t data_size, std::uint64_t end_offset)
{
    std::string fields;
    append_u32(fields, crc);
    append_u32(fields, data_size);
    append_u32(fields, data_size);

    seek_output(stream, static_cast<std::uint64_t>(local_header_offset) + 14U,
        "patching ZIP local header CRC and sizes");
    stream.write(fields.data(), static_cast<std::streamsize>(fields.size()));
    if (!stream) {
        throw FastXlsxError("failed to patch ZIP local header CRC and sizes");
    }
    seek_output(stream, end_offset, "returning to ZIP output tail");
}

void write_bytes(std::ofstream& stream, std::string_view data, std::uint64_t& offset)
{
    std::size_t written = 0;
    while (written < data.size()) {
        const std::size_t remaining = data.size() - written;
        const auto chunk_size = static_cast<std::streamsize>(
            std::min<std::size_t>(remaining, io_buffer_size));
        stream.write(data.data() + written, chunk_size);
        if (!stream) {
            throw FastXlsxError("failed to write XLSX package");
        }
        written += static_cast<std::size_t>(chunk_size);
        offset += static_cast<std::uint64_t>(chunk_size);
    }
}

void write_file_bytes(std::ofstream& output, FileChunkReadCache& file_cache,
    const PackageEntryChunk& chunk,
    Crc32Accumulator& entry_crc, ZipEntryStats& stats, std::uint64_t& offset)
{
    const std::uint64_t stat_size = file_cache.payload_size(chunk);
    require_expected_chunk_size(chunk, stat_size);

    std::ifstream& input = file_cache.open_for_payload(chunk);

    std::vector<char>& buffer = file_cache.scratch_buffer();
    Crc32Accumulator chunk_crc;
    std::uint64_t read_total = 0;
    const bool has_read_limit = file_chunk_has_read_limit(chunk);
    const std::uint64_t read_limit = file_chunk_read_limit(chunk);
    while (true) {
        if (has_read_limit && read_total == read_limit) {
            if (!chunk.has_file_range) {
                const int next = input.peek();
                if (next != std::char_traits<char>::eof()) {
                    throw FastXlsxError(expected_at_least_size_message(
                        "file-backed ZIP entry chunk produced more bytes than expected",
                        read_limit));
                }
                if (input.bad()) {
                    throw FastXlsxError("failed to read file-backed ZIP entry chunk");
                }
            }
            break;
        }

        std::size_t requested = buffer.size();
        if (has_read_limit) {
            requested = static_cast<std::size_t>(
                std::min<std::uint64_t>(read_limit - read_total,
                    static_cast<std::uint64_t>(buffer.size())));
        }
        input.read(buffer.data(), static_cast<std::streamsize>(requested));
        const std::streamsize read_size = input.gcount();
        if (input.bad()) {
            throw FastXlsxError("failed to read file-backed ZIP entry chunk");
        }
        if (read_size <= 0) {
            if (has_read_limit) {
                throw FastXlsxError(expected_actual_size_message(
                    "file-backed ZIP entry chunk ended before expected bytes",
                    read_limit, read_total));
            }
            break;
        }
        if (read_size > 0) {
            output.write(buffer.data(), read_size);
            if (!output) {
                throw FastXlsxError("failed to write file-backed ZIP entry data");
            }
            file_cache.note_payload_bytes_read(static_cast<std::uint64_t>(read_size));
            const std::string_view data(buffer.data(), static_cast<std::size_t>(read_size));
            update_written_entry_stats(stats, entry_crc, data);
            if (chunk.has_expected_crc32) {
                chunk_crc.update(data);
            }
            offset += static_cast<std::uint64_t>(read_size);
            read_total += static_cast<std::uint64_t>(read_size);
        }
    }
    require_expected_chunk_size(chunk, read_total);
    if (chunk.has_expected_crc32) {
        require_expected_chunk_crc32(chunk, chunk_crc.value());
    }
}

ZipEntryStats write_entry_data(std::ofstream& stream, const PackageEntry& entry,
    std::uint64_t& offset)
{
    ZipEntryStats stats;
    Crc32Accumulator entry_crc;
    FileChunkReadCache file_cache;

    if (entry.chunks.empty()) {
        update_written_entry_stats(stats, entry_crc, entry.data);
        write_bytes(stream, entry.data, offset);
        stats.crc = entry_crc.value();
        return stats;
    }

    for (std::size_t chunk_index = 0; chunk_index < entry.chunks.size(); ++chunk_index) {
        const PackageEntryChunk& chunk = entry.chunks[chunk_index];
        try {
            switch (chunk.kind) {
            case PackageEntryChunk::Kind::Memory:
                require_expected_chunk_size(chunk,
                    static_cast<std::uint64_t>(chunk.data.size()));
                require_expected_chunk_crc32(chunk, crc32(chunk.data));
                update_written_entry_stats(stats, entry_crc, chunk.data);
                write_bytes(stream, chunk.data, offset);
                break;
            case PackageEntryChunk::Kind::File:
                write_file_bytes(stream, file_cache, chunk, entry_crc, stats, offset);
                break;
            default:
                throw FastXlsxError("unsupported ZIP entry chunk kind");
            }
        } catch (const std::exception& error) {
            wrap_zip_entry_chunk_error(entry.name, chunk, chunk_index, error);
        }
    }

    stats.crc = entry_crc.value();
    return stats;
}

} // namespace

std::uint32_t crc32(std::string_view data)
{
    Crc32Accumulator crc;
    crc.update(data);
    return crc.value();
}

void write_stored_zip(const std::filesystem::path& path, const std::vector<PackageEntry>& entries)
{
    if (entries.empty()) {
        throw FastXlsxError("cannot write an empty ZIP package");
    }

    std::vector<char> output_buffer(io_buffer_size);
    std::ofstream stream;
    stream.rdbuf()->pubsetbuf(output_buffer.data(),
        static_cast<std::streamsize>(output_buffer.size()));
    stream.open(path, std::ios::binary);
    if (!stream) {
        throw FastXlsxError("failed to open XLSX path for writing");
    }

    std::uint64_t output_offset = 0;
    std::vector<CentralDirectoryRecord> central_directory;
    central_directory.reserve(entries.size());

    for (const PackageEntry& entry : entries) {
        if (entry.name.empty()) {
            throw FastXlsxError("ZIP entry name cannot be empty");
        }

        const auto local_header_offset = checked_u32(output_offset, "local header offset");
        const auto name_size = checked_u16(entry.name.size(), "file name length");

        std::string local_header;
        append_u32(local_header, 0x04034b50u);
        append_u16(local_header, 20); // version needed to extract
        append_u16(local_header, 0); // flags
        append_u16(local_header, 0); // stored, no compression
        append_u16(local_header, 0); // file mod time
        append_u16(local_header, 0); // file mod date
        append_u32(local_header, 0); // patched after entry data is written
        append_u32(local_header, 0); // patched after entry data is written
        append_u32(local_header, 0); // patched after entry data is written
        append_u16(local_header, name_size);
        append_u16(local_header, 0); // extra field length
        local_header += entry.name;
        write_bytes(stream, local_header, output_offset);
        const ZipEntryStats stats = write_entry_data(stream, entry, output_offset);
        const auto data_size = checked_u32(stats.size, "entry data size");
        patch_local_header_stats(
            stream, local_header_offset, stats.crc, data_size, output_offset);

        central_directory.push_back({entry.name, stats.crc, data_size, local_header_offset});
    }

    const auto central_directory_offset = checked_u32(output_offset, "central directory offset");

    for (const CentralDirectoryRecord& record : central_directory) {
        const auto name_size = checked_u16(record.name.size(), "central file name length");

        std::string central_record;
        append_u32(central_record, 0x02014b50u);
        append_u16(central_record, 20); // version made by
        append_u16(central_record, 20); // version needed to extract
        append_u16(central_record, 0); // flags
        append_u16(central_record, 0); // stored, no compression
        append_u16(central_record, 0); // file mod time
        append_u16(central_record, 0); // file mod date
        append_u32(central_record, record.crc);
        append_u32(central_record, record.size);
        append_u32(central_record, record.size);
        append_u16(central_record, name_size);
        append_u16(central_record, 0); // extra field length
        append_u16(central_record, 0); // file comment length
        append_u16(central_record, 0); // disk number start
        append_u16(central_record, 0); // internal file attributes
        append_u32(central_record, 0); // external file attributes
        append_u32(central_record, record.local_header_offset);
        central_record += record.name;
        write_bytes(stream, central_record, output_offset);
    }

    const auto central_directory_size =
        checked_u32(output_offset - central_directory_offset, "central directory size");
    const auto entry_count = checked_u16(central_directory.size(), "central directory entry count");

    std::string end_of_central_directory;
    append_u32(end_of_central_directory, 0x06054b50u);
    append_u16(end_of_central_directory, 0); // disk number
    append_u16(end_of_central_directory, 0); // central directory disk
    append_u16(end_of_central_directory, entry_count);
    append_u16(end_of_central_directory, entry_count);
    append_u32(end_of_central_directory, central_directory_size);
    append_u32(end_of_central_directory, central_directory_offset);
    append_u16(end_of_central_directory, 0); // comment length
    write_bytes(stream, end_of_central_directory, output_offset);

    if (!stream) {
        throw FastXlsxError("failed to write XLSX package");
    }
}

} // namespace fastxlsx::detail
