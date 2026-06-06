#include "zip_store_writer.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <string_view>

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

constexpr std::size_t io_buffer_size = 64 * 1024;

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

void update_stats_from_file(ZipEntryStats& stats, Crc32Accumulator& crc,
    const std::filesystem::path& path)
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
            const auto chunk_size = static_cast<std::size_t>(read_size);
            crc.update(std::string_view(buffer.data(), chunk_size));
            stats.size += chunk_size;
        }
    }

    if (stream.bad()) {
        throw FastXlsxError("failed to read file-backed ZIP entry chunk");
    }
}

ZipEntryStats compute_entry_stats(const PackageEntry& entry)
{
    ZipEntryStats stats;
    Crc32Accumulator crc;

    if (entry.chunks.empty()) {
        crc.update(entry.data);
        stats.size = entry.data.size();
        stats.crc = crc.value();
        return stats;
    }

    for (const PackageEntryChunk& chunk : entry.chunks) {
        switch (chunk.kind) {
        case PackageEntryChunk::Kind::Memory:
            crc.update(chunk.data);
            stats.size += chunk.data.size();
            break;
        case PackageEntryChunk::Kind::File:
            update_stats_from_file(stats, crc, chunk.path);
            break;
        }
    }

    stats.crc = crc.value();
    return stats;
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

void write_file_bytes(std::ofstream& output, const std::filesystem::path& path, std::uint64_t& offset)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw FastXlsxError("failed to open file-backed ZIP entry chunk");
    }

    std::array<char, io_buffer_size> buffer {};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read_size = input.gcount();
        if (read_size > 0) {
            output.write(buffer.data(), read_size);
            if (!output) {
                throw FastXlsxError("failed to write file-backed ZIP entry data");
            }
            offset += static_cast<std::uint64_t>(read_size);
        }
    }

    if (input.bad()) {
        throw FastXlsxError("failed to read file-backed ZIP entry chunk");
    }
}

void write_entry_data(std::ofstream& stream, const PackageEntry& entry, std::uint64_t& offset)
{
    if (entry.chunks.empty()) {
        write_bytes(stream, entry.data, offset);
        return;
    }

    for (const PackageEntryChunk& chunk : entry.chunks) {
        switch (chunk.kind) {
        case PackageEntryChunk::Kind::Memory:
            write_bytes(stream, chunk.data, offset);
            break;
        case PackageEntryChunk::Kind::File:
            write_file_bytes(stream, chunk.path, offset);
            break;
        }
    }
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

    std::ofstream stream(path, std::ios::binary);
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
        const ZipEntryStats stats = compute_entry_stats(entry);
        const auto data_size = checked_u32(stats.size, "entry data size");

        std::string local_header;
        append_u32(local_header, 0x04034b50u);
        append_u16(local_header, 20); // version needed to extract
        append_u16(local_header, 0); // flags
        append_u16(local_header, 0); // stored, no compression
        append_u16(local_header, 0); // file mod time
        append_u16(local_header, 0); // file mod date
        append_u32(local_header, stats.crc);
        append_u32(local_header, data_size);
        append_u32(local_header, data_size);
        append_u16(local_header, name_size);
        append_u16(local_header, 0); // extra field length
        local_header += entry.name;
        write_bytes(stream, local_header, output_offset);
        write_entry_data(stream, entry, output_offset);

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
