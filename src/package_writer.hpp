#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace fastxlsx::detail {

struct PackageEntryChunk {
    enum class Kind {
        Memory,
        File,
    };

    Kind kind = Kind::Memory;
    std::string data;
    std::filesystem::path path;
    bool has_file_range = false;
    std::uint64_t file_offset = 0;
    std::uint64_t file_size = 0;
    bool has_expected_size = false;
    std::uint64_t expected_size = 0;
    bool has_expected_crc32 = false;
    std::uint32_t expected_crc32 = 0;

    [[nodiscard]] static PackageEntryChunk memory(std::string value)
    {
        PackageEntryChunk chunk;
        chunk.kind = Kind::Memory;
        chunk.data = std::move(value);
        chunk.has_expected_size = true;
        chunk.expected_size = static_cast<std::uint64_t>(chunk.data.size());
        return chunk;
    }

    [[nodiscard]] static PackageEntryChunk file(std::filesystem::path value)
    {
        PackageEntryChunk chunk;
        chunk.kind = Kind::File;
        chunk.path = std::move(value);
        return chunk;
    }

    [[nodiscard]] static PackageEntryChunk file_range(
        std::filesystem::path value, std::uint64_t offset, std::uint64_t size)
    {
        PackageEntryChunk chunk;
        chunk.kind = Kind::File;
        chunk.path = std::move(value);
        chunk.has_file_range = true;
        chunk.file_offset = offset;
        chunk.file_size = size;
        chunk.has_expected_size = true;
        chunk.expected_size = size;
        return chunk;
    }
};

struct PackageRawCompressedEntrySource {
    std::filesystem::path path;
    std::uint64_t data_offset = 0;
    std::uint64_t compressed_size = 0;
    std::uint64_t uncompressed_size = 0;
    std::uint32_t crc32 = 0;
    std::uint16_t compression_method = 0;
};

struct PackageEntry {
    std::string name;
    std::string data;
    std::vector<PackageEntryChunk> chunks;
    std::optional<PackageRawCompressedEntrySource> raw_compressed_source;

    PackageEntry() = default;

    PackageEntry(std::string entry_name, std::string entry_data)
        : name(std::move(entry_name))
        , data(std::move(entry_data))
    {
    }

    PackageEntry(std::string entry_name, std::vector<PackageEntryChunk> entry_chunks)
        : name(std::move(entry_name))
        , chunks(std::move(entry_chunks))
    {
    }

    [[nodiscard]] static PackageEntry raw_compressed_copy(
        std::string entry_name, PackageRawCompressedEntrySource source)
    {
        PackageEntry entry;
        entry.name = std::move(entry_name);
        entry.raw_compressed_source = std::move(source);
        return entry;
    }
};

enum class PackageWriterBackend {
    Auto,
    StoredZipBootstrap,
    MinizipNg,
};

inline constexpr int package_writer_default_compression_level = -1;
inline constexpr int package_writer_min_compression_level = 0;
inline constexpr int package_writer_max_compression_level = 9;

struct PackageWriterEntryTelemetry {
    std::string entry_name;
    bool raw_compressed_copy = false;
    bool reused_staged_crc32 = false;
    bool staged_file_read_prefetch = false;
    int requested_compression_level = package_writer_default_compression_level;
    std::uint64_t uncompressed_bytes = 0;
    std::uint64_t input_bytes = 0;
    std::uint64_t input_read_calls = 0;
    std::uint64_t writer_write_calls = 0;
    std::uint64_t reused_staged_file_chunk_count = 0;
    std::uint64_t prefetched_staged_file_chunk_count = 0;
    std::uint64_t prefetched_staged_input_bytes = 0;
    std::uint64_t prefetch_peak_buffer_bytes = 0;
    std::uint64_t total_us = 0;
    std::uint64_t total_process_cpu_us = 0;
    std::uint64_t open_us = 0;
    std::uint64_t input_read_us = 0;
    std::uint64_t input_read_wait_us = 0;
    std::uint64_t writer_write_us = 0;
    std::uint64_t writer_write_process_cpu_us = 0;
    std::uint64_t staged_crc_validation_us = 0;
    std::uint64_t close_us = 0;
    std::uint64_t close_process_cpu_us = 0;
    // CPU envelope of minizip DEFLATE write/close calls, including backend bookkeeping.
    std::uint64_t deflate_writer_process_cpu_us = 0;
};

struct PackageWriterTelemetry {
    PackageWriterBackend backend = PackageWriterBackend::Auto;
    std::uint64_t total_us = 0;
    std::uint64_t total_process_cpu_us = 0;
    std::uint64_t open_us = 0;
    std::uint64_t close_us = 0;
    std::vector<PackageWriterEntryTelemetry> entries;
};

struct PackageWriterOptions {
    PackageWriterBackend backend = PackageWriterBackend::Auto;
    int compression_level = package_writer_default_compression_level;
    PackageWriterTelemetry* telemetry = nullptr;
};

[[nodiscard]] bool package_writer_can_raw_copy_compression_method(
    PackageWriterOptions options, std::uint16_t compression_method) noexcept;

// Internal package writer boundary. Auto selects the production minizip-ng
// backend when the dependency is enabled; otherwise it keeps the Phase 1
// stored/no-compression ZIP bootstrap for dependency-free builds. The
// compression level is an internal minizip-ng option: -1 keeps the backend
// default, 0 requests minizip no-compression/stored output, and 1..9 select
// zlib-compatible DEFLATE levels. The stored bootstrap remains
// stored/no-compression.
void write_package(const std::filesystem::path& path, const std::vector<PackageEntry>& entries,
    PackageWriterOptions options = {});

} // namespace fastxlsx::detail
