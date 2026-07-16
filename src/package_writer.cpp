#include "package_writer.hpp"

#include "zip_entry_name.hpp"
#include "zip_store_writer.hpp"

#include <fastxlsx/workbook.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#ifdef FASTXLSX_HAS_MINIZIP_NG
#include <mz.h>
#include <mz_strm.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <ctime>
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
constexpr std::uint64_t staged_file_prefetch_min_bytes = 4U * io_buffer_size;
constexpr std::uint64_t max_forward_gap_discard_bytes = 64 * 1024;
constexpr std::uint64_t zip32_max_u16 = std::numeric_limits<std::uint16_t>::max();
constexpr std::uint64_t zip32_max_u32 = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint16_t stored_compression_method = 0;
constexpr std::uint16_t deflate_compression_method = 8;

std::uint64_t elapsed_microseconds(std::chrono::steady_clock::time_point started) noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count());
}

std::uint64_t process_cpu_microseconds() noexcept
{
#ifdef _WIN32
    FILETIME created {};
    FILETIME exited {};
    FILETIME kernel {};
    FILETIME user {};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) == 0) {
        return 0;
    }

    ULARGE_INTEGER kernel_ticks {};
    kernel_ticks.LowPart = kernel.dwLowDateTime;
    kernel_ticks.HighPart = kernel.dwHighDateTime;
    ULARGE_INTEGER user_ticks {};
    user_ticks.LowPart = user.dwLowDateTime;
    user_ticks.HighPart = user.dwHighDateTime;
    return (kernel_ticks.QuadPart + user_ticks.QuadPart) / 10U;
#else
    const std::clock_t finished = std::clock();
    if (finished == static_cast<std::clock_t>(-1)) {
        return 0;
    }
    return static_cast<std::uint64_t>(
        static_cast<long double>(finished) * 1'000'000.0L
        / static_cast<long double>(CLOCKS_PER_SEC));
#endif
}

std::uint64_t elapsed_process_cpu_microseconds(std::uint64_t started) noexcept
{
    const std::uint64_t finished = process_cpu_microseconds();
    return finished >= started ? finished - started : 0;
}

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

std::uint32_t crc32_matrix_times(
    const std::array<std::uint32_t, 32>& matrix, std::uint32_t vector) noexcept
{
    std::uint32_t result = 0;
    std::size_t matrix_index = 0;
    while (vector != 0) {
        if ((vector & 1u) != 0u) {
            result ^= matrix[matrix_index];
        }
        vector >>= 1u;
        ++matrix_index;
    }
    return result;
}

void crc32_matrix_square(std::array<std::uint32_t, 32>& square,
    const std::array<std::uint32_t, 32>& matrix) noexcept
{
    for (std::size_t index = 0; index < square.size(); ++index) {
        square[index] = crc32_matrix_times(matrix, matrix[index]);
    }
}

std::uint32_t combine_crc32(
    std::uint32_t first, std::uint32_t second, std::uint64_t second_size) noexcept
{
    if (second_size == 0) {
        return first;
    }

    std::array<std::uint32_t, 32> odd {};
    std::array<std::uint32_t, 32> even {};
    odd[0] = 0xedb88320u;
    std::uint32_t row = 1;
    for (std::size_t index = 1; index < odd.size(); ++index) {
        odd[index] = row;
        row <<= 1u;
    }

    crc32_matrix_square(even, odd);
    crc32_matrix_square(odd, even);
    do {
        crc32_matrix_square(even, odd);
        if ((second_size & 1u) != 0u) {
            first = crc32_matrix_times(even, first);
        }
        second_size >>= 1u;
        if (second_size == 0) {
            break;
        }

        crc32_matrix_square(odd, even);
        if ((second_size & 1u) != 0u) {
            first = crc32_matrix_times(odd, first);
        }
        second_size >>= 1u;
    } while (second_size != 0);

    return first ^ second;
}

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
    if (entry.raw_compressed_source.has_value()) {
        return entry.raw_compressed_source->uncompressed_size;
    }
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

void validate_raw_compressed_source(
    const PackageEntry& entry, FileChunkStatCache& stat_cache)
{
    if (!entry.raw_compressed_source.has_value()) {
        return;
    }
    if (!entry.data.empty() || !entry.chunks.empty()) {
        throw FastXlsxError(
            "ZIP entry cannot mix raw compressed source and logical payload");
    }

    const PackageRawCompressedEntrySource& source = *entry.raw_compressed_source;
    if (source.path.empty()) {
        throw FastXlsxError("raw compressed ZIP entry source path cannot be empty");
    }
    if (source.compression_method != stored_compression_method
        && source.compression_method != deflate_compression_method) {
        throw FastXlsxError("unsupported raw compressed ZIP entry method");
    }
    if (source.compression_method == stored_compression_method
        && source.compressed_size != source.uncompressed_size) {
        throw FastXlsxError(
            "stored raw compressed ZIP entry has mismatched compressed/uncompressed sizes");
    }
    if (source.compressed_size > zip32_max_u32) {
        throw FastXlsxError("raw compressed ZIP entry size requires Zip64 support");
    }

    PackageEntryChunk range = PackageEntryChunk::file_range(
        source.path, source.data_offset, source.compressed_size);
    (void)file_chunk_data_size_from_stat(range, stat_cache);
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
        validate_raw_compressed_source(entry, stat_cache);
        validate_entry_chunk_sources(entry, stat_cache);
        if (entry_uncompressed_size(entry, stat_cache) > zip32_max_u32) {
            throw FastXlsxError("ZIP entry uncompressed size requires Zip64 support");
        }
    }
}

#ifdef FASTXLSX_HAS_MINIZIP_NG

struct EntryCrcValidationPlan {
    bool reuse_staged_crc32 = false;
    std::uint32_t expected_crc32 = 0;
    std::uint64_t reused_file_chunk_count = 0;
};

EntryCrcValidationPlan make_entry_crc_validation_plan(const PackageEntry& entry)
{
    EntryCrcValidationPlan plan;
    if (entry.chunks.empty()) {
        return plan;
    }

    std::uint32_t combined_crc32 = 0;
    for (const PackageEntryChunk& chunk : entry.chunks) {
        std::uint64_t chunk_size = 0;
        std::uint32_t chunk_crc32 = 0;
        switch (chunk.kind) {
        case PackageEntryChunk::Kind::Memory:
            chunk_size = static_cast<std::uint64_t>(chunk.data.size());
            chunk_crc32 = chunk.has_expected_crc32
                ? chunk.expected_crc32
                : crc32(chunk.data);
            break;
        case PackageEntryChunk::Kind::File:
            if (!chunk.has_expected_crc32
                || (!chunk.has_file_range && !chunk.has_expected_size)) {
                return {};
            }
            chunk_size = chunk.has_file_range ? chunk.file_size : chunk.expected_size;
            chunk_crc32 = chunk.expected_crc32;
            ++plan.reused_file_chunk_count;
            break;
        default:
            return {};
        }
        combined_crc32 = combine_crc32(combined_crc32, chunk_crc32, chunk_size);
    }

    plan.reuse_staged_crc32 = plan.reused_file_chunk_count != 0;
    plan.expected_crc32 = combined_crc32;
    return plan;
}

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

#ifdef _WIN32

class Win32Handle {
public:
    Win32Handle() = default;

    explicit Win32Handle(HANDLE handle) noexcept
        : handle_(handle)
    {
    }

    ~Win32Handle()
    {
        reset();
    }

    Win32Handle(const Win32Handle&) = delete;
    Win32Handle& operator=(const Win32Handle&) = delete;

    Win32Handle(Win32Handle&& other) noexcept
        : handle_(other.release())
    {
    }

    Win32Handle& operator=(Win32Handle&& other) noexcept
    {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const noexcept
    {
        return handle_;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }

    void reset(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
    {
        if (valid()) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    [[nodiscard]] HANDLE release() noexcept
    {
        const HANDLE handle = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return handle;
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

std::string win32_file_failure_message(
    std::string_view prefix, const PackageEntryChunk& chunk, DWORD error)
{
    return chunk_file_failure_message(prefix, chunk)
        + " (Windows error " + std::to_string(error) + ")";
}

class MinizipOverlappedFileChunkReader {
public:
    MinizipOverlappedFileChunkReader(const PackageEntryChunk& chunk,
        std::vector<char>& first_buffer, std::vector<char>& second_buffer)
        : chunk_(chunk)
    {
        first_buffer.resize(io_buffer_size);
        second_buffer.resize(io_buffer_size);
        slots_[0].buffer = &first_buffer;
        slots_[1].buffer = &second_buffer;

        file_.reset(CreateFileW(chunk.path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN
                | FILE_FLAG_OVERLAPPED,
            nullptr));
        if (!file_.valid()) {
            throw FastXlsxError(win32_file_failure_message(
                "failed to open prefetched file-backed ZIP entry chunk",
                chunk, GetLastError()));
        }

        for (ReadSlot& slot : slots_) {
            slot.event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!slot.event.valid()) {
                throw FastXlsxError(win32_file_failure_message(
                    "failed to create staged file prefetch event",
                    chunk, GetLastError()));
            }
        }

        LARGE_INTEGER measured_size {};
        if (GetFileSizeEx(file_.get(), &measured_size) == 0 || measured_size.QuadPart < 0) {
            throw FastXlsxError(win32_file_failure_message(
                "failed to measure prefetched file-backed ZIP entry chunk",
                chunk, GetLastError()));
        }
        opened_file_size_ = static_cast<std::uint64_t>(measured_size.QuadPart);
        const std::uint64_t offset = chunk.has_file_range ? chunk.file_offset : 0;
        const std::uint64_t payload_size = file_chunk_read_limit(chunk);
        if (offset > opened_file_size_
            || payload_size > opened_file_size_ - offset) {
            throw FastXlsxError(expected_actual_size_message(
                "prefetched file-backed ZIP entry chunk range exceeds file size",
                payload_size, opened_file_size_ >= offset ? opened_file_size_ - offset : 0));
        }
        if (!chunk.has_file_range && opened_file_size_ != payload_size) {
            throw FastXlsxError(expected_actual_size_message(
                "prefetched file-backed ZIP entry chunk changed before read",
                payload_size, opened_file_size_));
        }
    }

    ~MinizipOverlappedFileChunkReader()
    {
        for (ReadSlot& slot : slots_) {
            cancel_and_wait(slot);
        }
    }

    MinizipOverlappedFileChunkReader(const MinizipOverlappedFileChunkReader&) = delete;
    MinizipOverlappedFileChunkReader& operator=(
        const MinizipOverlappedFileChunkReader&) = delete;

    void start(std::size_t slot_index, std::uint64_t offset, std::size_t size,
        PackageWriterEntryTelemetry* telemetry)
    {
        ReadSlot& slot = slots_.at(slot_index);
        if (slot.active || size == 0 || size > slot.buffer->size()
            || size > static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
            throw FastXlsxError("invalid staged file prefetch request");
        }

        if (ResetEvent(slot.event.get()) == 0) {
            throw FastXlsxError(win32_file_failure_message(
                "failed to reset staged file prefetch event",
                chunk_, GetLastError()));
        }
        slot.overlapped = OVERLAPPED {};
        slot.overlapped.Offset = static_cast<DWORD>(offset & 0xffffffffULL);
        slot.overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32U);
        slot.overlapped.hEvent = slot.event.get();
        slot.requested = static_cast<DWORD>(size);
        slot.active = true;

        const auto read_started = telemetry != nullptr
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point {};
        const BOOL started = ReadFile(file_.get(), slot.buffer->data(), slot.requested,
            nullptr, &slot.overlapped);
        const DWORD error = started != 0 ? ERROR_SUCCESS : GetLastError();
        if (telemetry != nullptr) {
            telemetry->input_read_us += elapsed_microseconds(read_started);
            ++telemetry->input_read_calls;
        }
        if (started == 0 && error != ERROR_IO_PENDING) {
            slot.active = false;
            throw FastXlsxError(win32_file_failure_message(
                "failed to start staged file prefetch read", chunk_, error));
        }
    }

    [[nodiscard]] std::size_t finish(
        std::size_t slot_index, PackageWriterEntryTelemetry* telemetry)
    {
        ReadSlot& slot = slots_.at(slot_index);
        if (!slot.active) {
            throw FastXlsxError("staged file prefetch slot is not active");
        }

        DWORD read_size = 0;
        const auto wait_started = telemetry != nullptr
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point {};
        const BOOL completed = GetOverlappedResult(
            file_.get(), &slot.overlapped, &read_size, TRUE);
        if (telemetry != nullptr) {
            const std::uint64_t wait_us = elapsed_microseconds(wait_started);
            telemetry->input_read_us += wait_us;
            telemetry->input_read_wait_us += wait_us;
        }
        slot.active = false;
        if (completed == 0) {
            throw FastXlsxError(win32_file_failure_message(
                "failed to complete staged file prefetch read",
                chunk_, GetLastError()));
        }
        if (read_size != slot.requested) {
            throw FastXlsxError(expected_actual_size_message(
                "prefetched file-backed ZIP entry chunk ended before expected bytes",
                slot.requested, read_size));
        }
        return static_cast<std::size_t>(read_size);
    }

    [[nodiscard]] std::string_view data(std::size_t slot_index, std::size_t size) const
    {
        const ReadSlot& slot = slots_.at(slot_index);
        return {slot.buffer->data(), size};
    }

    void require_file_size_unchanged() const
    {
        if (chunk_.has_file_range) {
            return;
        }
        LARGE_INTEGER measured_size {};
        if (GetFileSizeEx(file_.get(), &measured_size) == 0 || measured_size.QuadPart < 0) {
            throw FastXlsxError(win32_file_failure_message(
                "failed to remeasure prefetched file-backed ZIP entry chunk",
                chunk_, GetLastError()));
        }
        const auto current_size = static_cast<std::uint64_t>(measured_size.QuadPart);
        if (current_size != opened_file_size_) {
            throw FastXlsxError(expected_actual_size_message(
                "prefetched file-backed ZIP entry chunk size changed during read",
                opened_file_size_, current_size));
        }
    }

private:
    struct ReadSlot {
        std::vector<char>* buffer = nullptr;
        Win32Handle event;
        OVERLAPPED overlapped {};
        DWORD requested = 0;
        bool active = false;
    };

    void cancel_and_wait(ReadSlot& slot) noexcept
    {
        if (!slot.active || !file_.valid()) {
            return;
        }
        (void)CancelIoEx(file_.get(), &slot.overlapped);
        DWORD ignored = 0;
        (void)GetOverlappedResult(file_.get(), &slot.overlapped, &ignored, TRUE);
        slot.active = false;
    }

    const PackageEntryChunk& chunk_;
    Win32Handle file_;
    std::array<ReadSlot, 2> slots_;
    std::uint64_t opened_file_size_ = 0;
};

[[nodiscard]] bool should_prefetch_staged_file_chunk(
    const PackageEntryChunk& chunk) noexcept
{
    return file_chunk_has_read_limit(chunk)
        && file_chunk_read_limit(chunk) >= staged_file_prefetch_min_bytes;
}

template <typename Consumer>
void consume_prefetched_minizip_file_chunk(const PackageEntryChunk& chunk,
    std::vector<char>& first_buffer, PackageWriterEntryTelemetry* telemetry,
    Consumer consumer)
{
    std::vector<char> second_buffer;
    MinizipOverlappedFileChunkReader reader(chunk, first_buffer, second_buffer);
    const std::uint64_t read_limit = file_chunk_read_limit(chunk);
    const std::uint64_t first_offset = chunk.has_file_range ? chunk.file_offset : 0;
    std::uint64_t consumed_size = 0;
    std::size_t active_slot = 0;
    std::size_t requested = static_cast<std::size_t>(
        std::min<std::uint64_t>(read_limit, io_buffer_size));
    reader.start(active_slot, first_offset, requested, telemetry);

    while (consumed_size < read_limit) {
        const std::size_t read_size = reader.finish(active_slot, telemetry);
        const std::uint64_t next_consumed = consumed_size + read_size;
        const std::size_t next_slot = 1U - active_slot;
        if (next_consumed < read_limit) {
            requested = static_cast<std::size_t>(std::min<std::uint64_t>(
                read_limit - next_consumed, io_buffer_size));
            reader.start(next_slot, first_offset + next_consumed, requested, telemetry);
        }

        consumer(reader.data(active_slot, read_size));
        consumed_size = next_consumed;
        active_slot = next_slot;
    }

    reader.require_file_size_unchanged();
    require_expected_chunk_size(chunk, consumed_size);
    if (telemetry != nullptr) {
        telemetry->staged_file_read_prefetch = true;
        ++telemetry->prefetched_staged_file_chunk_count;
        telemetry->prefetched_staged_input_bytes += consumed_size;
        telemetry->prefetch_peak_buffer_bytes = std::max<std::uint64_t>(
            telemetry->prefetch_peak_buffer_bytes, 2U * io_buffer_size);
    }
}

#endif

void write_minizip_memory_chunk(void* writer, std::string_view data,
    PackageWriterEntryTelemetry* telemetry)
{
    std::size_t offset = 0;
    while (offset < data.size()) {
        const std::size_t remaining = data.size() - offset;
        const int chunk_size = static_cast<int>(std::min<std::size_t>(
            remaining, static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const auto write_started = telemetry != nullptr
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point {};
        const std::uint64_t write_process_cpu_started = telemetry != nullptr
            ? process_cpu_microseconds()
            : 0;
        const int written = mz_zip_writer_entry_write(
            writer, data.data() + offset, chunk_size);
        if (telemetry != nullptr) {
            telemetry->writer_write_us += elapsed_microseconds(write_started);
            telemetry->writer_write_process_cpu_us +=
                elapsed_process_cpu_microseconds(write_process_cpu_started);
            ++telemetry->writer_write_calls;
            telemetry->input_bytes += static_cast<std::uint64_t>(chunk_size);
        }
        if (written != chunk_size) {
            throw FastXlsxError("minizip-ng failed to write ZIP entry data");
        }
        offset += static_cast<std::size_t>(written);
    }
}

template <typename Consumer>
void consume_minizip_file_chunk(MinizipFileChunkReadCache& file_cache,
    const PackageEntryChunk& chunk, std::vector<char>& buffer,
    PackageWriterEntryTelemetry* telemetry, Consumer consumer)
{
    std::ifstream& stream = file_cache.open_for_payload(chunk, buffer);

    std::uint64_t consumed_size = 0;
    const bool has_read_limit = file_chunk_has_read_limit(chunk);
    const std::uint64_t read_limit = file_chunk_read_limit(chunk);
    while (true) {
        if (has_read_limit && consumed_size == read_limit) {
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
                std::min<std::uint64_t>(read_limit - consumed_size,
                    static_cast<std::uint64_t>(buffer.size())));
        }
        const auto read_started = telemetry != nullptr
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point {};
        stream.read(buffer.data(), static_cast<std::streamsize>(requested));
        const std::streamsize read_size = stream.gcount();
        if (telemetry != nullptr) {
            telemetry->input_read_us += elapsed_microseconds(read_started);
            ++telemetry->input_read_calls;
        }
        if (stream.bad()) {
            throw FastXlsxError("failed to read file-backed ZIP entry chunk");
        }
        if (read_size <= 0) {
            if (has_read_limit) {
                throw FastXlsxError(expected_actual_size_message(
                    "file-backed ZIP entry chunk ended before expected bytes",
                    read_limit, consumed_size));
            }
            break;
        }
        consumer(std::string_view(buffer.data(), static_cast<std::size_t>(read_size)));
        consumed_size += static_cast<std::uint64_t>(read_size);
    }
    file_cache.note_payload_bytes_read(consumed_size);
    require_expected_chunk_size(chunk, consumed_size);
}

void write_minizip_file_chunk(void* writer, MinizipFileChunkReadCache& file_cache,
    const PackageEntryChunk& chunk, std::vector<char>& buffer,
    bool validate_chunk_crc32, PackageWriterEntryTelemetry* telemetry)
{
    Crc32Accumulator chunk_crc;
    consume_minizip_file_chunk(file_cache, chunk, buffer, telemetry,
        [&](std::string_view data) {
        const int chunk_size = static_cast<int>(data.size());
        const auto write_started = telemetry != nullptr
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point {};
        const std::uint64_t write_process_cpu_started = telemetry != nullptr
            ? process_cpu_microseconds()
            : 0;
        const int written = mz_zip_writer_entry_write(writer, data.data(), chunk_size);
        if (telemetry != nullptr) {
            telemetry->writer_write_us += elapsed_microseconds(write_started);
            telemetry->writer_write_process_cpu_us +=
                elapsed_process_cpu_microseconds(write_process_cpu_started);
            ++telemetry->writer_write_calls;
            telemetry->input_bytes += static_cast<std::uint64_t>(data.size());
        }
        if (written != chunk_size) {
            throw FastXlsxError("minizip-ng failed to write file-backed ZIP entry data");
        }
        if (validate_chunk_crc32) {
            chunk_crc.update(data);
        }
    });
    if (validate_chunk_crc32) {
        require_expected_chunk_crc32(chunk, chunk_crc.value());
    }
}

#ifdef _WIN32

void write_minizip_prefetched_file_chunk(void* writer,
    const PackageEntryChunk& chunk, std::vector<char>& buffer,
    bool validate_chunk_crc32, PackageWriterEntryTelemetry* telemetry)
{
    Crc32Accumulator chunk_crc;
    consume_prefetched_minizip_file_chunk(chunk, buffer, telemetry,
        [&](std::string_view data) {
        const int chunk_size = static_cast<int>(data.size());
        const auto write_started = telemetry != nullptr
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point {};
        const std::uint64_t write_process_cpu_started = telemetry != nullptr
            ? process_cpu_microseconds()
            : 0;
        const int written = mz_zip_writer_entry_write(writer, data.data(), chunk_size);
        if (telemetry != nullptr) {
            telemetry->writer_write_us += elapsed_microseconds(write_started);
            telemetry->writer_write_process_cpu_us +=
                elapsed_process_cpu_microseconds(write_process_cpu_started);
            ++telemetry->writer_write_calls;
            telemetry->input_bytes += static_cast<std::uint64_t>(data.size());
        }
        if (written != chunk_size) {
            throw FastXlsxError(
                "minizip-ng failed to write prefetched file-backed ZIP entry data");
        }
        if (validate_chunk_crc32) {
            chunk_crc.update(data);
        }
    });
    if (validate_chunk_crc32) {
        require_expected_chunk_crc32(chunk, chunk_crc.value());
    }
}

#endif

void write_minizip_entry_chunks(
    void* writer, const PackageEntry& entry, std::vector<char>& file_buffer,
    bool reuse_staged_crc32, PackageWriterEntryTelemetry* telemetry)
{
    if (entry.chunks.empty()) {
        write_minizip_memory_chunk(writer, entry.data, telemetry);
        return;
    }

    MinizipFileChunkReadCache file_cache;
    for (std::size_t chunk_index = 0; chunk_index < entry.chunks.size(); ++chunk_index) {
        const PackageEntryChunk& chunk = entry.chunks[chunk_index];
        try {
            switch (chunk.kind) {
            case PackageEntryChunk::Kind::Memory:
                write_minizip_memory_chunk(writer, chunk.data, telemetry);
                break;
            case PackageEntryChunk::Kind::File:
                if (file_buffer.empty()) {
                    file_buffer.resize(io_buffer_size);
                }
#ifdef _WIN32
                if (should_prefetch_staged_file_chunk(chunk)) {
                    write_minizip_prefetched_file_chunk(
                        writer, chunk, file_buffer,
                        chunk.has_expected_crc32 && !reuse_staged_crc32, telemetry);
                    break;
                }
#endif
                write_minizip_file_chunk(
                    writer, file_cache, chunk, file_buffer,
                    chunk.has_expected_crc32 && !reuse_staged_crc32, telemetry);
                break;
            default:
                throw FastXlsxError("unsupported ZIP entry chunk kind");
            }
        } catch (const std::exception& error) {
            wrap_zip_entry_chunk_error(entry.name, chunk, chunk_index, error);
        }
    }
}

void write_minizip_raw_compressed_entry(void* writer,
    const PackageRawCompressedEntrySource& source,
    MinizipFileChunkReadCache& file_cache, std::vector<char>& file_buffer,
    PackageWriterEntryTelemetry* telemetry)
{
    if (file_buffer.empty()) {
        file_buffer.resize(io_buffer_size);
    }
    PackageEntryChunk chunk = PackageEntryChunk::file_range(
        source.path, source.data_offset, source.compressed_size);
    write_minizip_file_chunk(writer, file_cache, chunk, file_buffer, false, telemetry);
}

void diagnose_entry_crc32_mismatch(
    const PackageEntry& entry, std::vector<char>& file_buffer)
{
    if (file_buffer.empty()) {
        file_buffer.resize(io_buffer_size);
    }
    MinizipFileChunkReadCache file_cache;
    for (std::size_t chunk_index = 0; chunk_index < entry.chunks.size(); ++chunk_index) {
        const PackageEntryChunk& chunk = entry.chunks[chunk_index];
        if (chunk.kind != PackageEntryChunk::Kind::File || !chunk.has_expected_crc32) {
            continue;
        }
        try {
            Crc32Accumulator chunk_crc;
            consume_minizip_file_chunk(file_cache, chunk, file_buffer, nullptr,
                [&](std::string_view data) { chunk_crc.update(data); });
            require_expected_chunk_crc32(chunk, chunk_crc.value());
        } catch (const std::exception& error) {
            wrap_zip_entry_chunk_error(entry.name, chunk, chunk_index, error);
        }
    }
}

void validate_written_entry_crc32(void* zip_handle, const PackageEntry& entry,
    const EntryCrcValidationPlan& plan, std::vector<char>& file_buffer)
{
    mz_zip_file* written_file_info = nullptr;
    check_minizip_result(mz_zip_entry_get_info(zip_handle, &written_file_info),
        "get written ZIP entry info");
    if (written_file_info == nullptr) {
        throw FastXlsxError("minizip-ng returned no written ZIP entry info");
    }
    if (written_file_info->crc == plan.expected_crc32) {
        return;
    }

    diagnose_entry_crc32_mismatch(entry, file_buffer);
    throw FastXlsxError("ZIP entry CRC32 changed after staging: expected "
        + std::to_string(plan.expected_crc32) + ", actual "
        + std::to_string(written_file_info->crc));
}

void write_minizip_package(
    const std::filesystem::path& path, const std::vector<PackageEntry>& entries,
    int compression_level, PackageWriterTelemetry* telemetry)
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
    const auto open_started = telemetry != nullptr
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point {};
    check_minizip_result(mz_zip_writer_open_file(writer.get(), output_path.c_str(), 0, 0),
        "open XLSX package");
    void* zip_handle = nullptr;
    check_minizip_result(
        mz_zip_writer_get_zip_handle(writer.get(), &zip_handle), "get ZIP writer handle");
    check_minizip_result(
        mz_zip_set_data_descriptor(zip_handle, 0), "disable ZIP data descriptors");
    if (telemetry != nullptr) {
        telemetry->open_us = elapsed_microseconds(open_started);
        telemetry->entries.reserve(entries.size());
    }

    try {
        FileChunkStatCache stat_cache;
        MinizipFileChunkReadCache raw_file_cache;
        std::vector<char> file_buffer;
        for (const PackageEntry& entry : entries) {
            if (entry.name.empty()) {
                throw FastXlsxError("ZIP entry name cannot be empty");
            }

            PackageWriterEntryTelemetry entry_telemetry;
            PackageWriterEntryTelemetry* active_entry_telemetry = nullptr;
            if (telemetry != nullptr) {
                entry_telemetry.entry_name = entry.name;
                entry_telemetry.raw_compressed_copy = entry.raw_compressed_source.has_value();
                entry_telemetry.requested_compression_level = compression_level;
                active_entry_telemetry = &entry_telemetry;
            }
            const auto entry_started = active_entry_telemetry != nullptr
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point {};
            const std::uint64_t entry_process_cpu_started = active_entry_telemetry != nullptr
                ? process_cpu_microseconds()
                : 0;
            const auto crc_plan_started = active_entry_telemetry != nullptr
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point {};
            const EntryCrcValidationPlan crc_plan =
                make_entry_crc_validation_plan(entry);
            if (active_entry_telemetry != nullptr) {
                active_entry_telemetry->reused_staged_crc32 =
                    crc_plan.reuse_staged_crc32;
                active_entry_telemetry->reused_staged_file_chunk_count =
                    crc_plan.reused_file_chunk_count;
                active_entry_telemetry->staged_crc_validation_us =
                    elapsed_microseconds(crc_plan_started);
            }

            mz_zip_file file_info {};
            file_info.filename = entry.name.c_str();
            file_info.flag = MZ_ZIP_FLAG_UTF8;
            file_info.compression_method = entry.raw_compressed_source.has_value()
                ? entry.raw_compressed_source->compression_method
                : MZ_COMPRESS_METHOD_DEFLATE;
            file_info.modified_date = 0;
            const std::uint64_t uncompressed_size = entry_uncompressed_size(entry, stat_cache);
            file_info.uncompressed_size = static_cast<std::int64_t>(uncompressed_size);
            if (active_entry_telemetry != nullptr) {
                active_entry_telemetry->uncompressed_bytes = uncompressed_size;
            }
            if (entry.raw_compressed_source.has_value()) {
                file_info.crc = entry.raw_compressed_source->crc32;
                mz_zip_writer_set_raw(writer.get(), 1);
            } else {
                mz_zip_writer_set_raw(writer.get(), 0);
            }

            const auto entry_open_started = active_entry_telemetry != nullptr
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point {};
            check_minizip_result(mz_zip_writer_entry_open(writer.get(), &file_info),
                "open ZIP entry");
            if (active_entry_telemetry != nullptr) {
                active_entry_telemetry->open_us = elapsed_microseconds(entry_open_started);
            }

            if (entry.raw_compressed_source.has_value()) {
                write_minizip_raw_compressed_entry(writer.get(),
                    *entry.raw_compressed_source, raw_file_cache, file_buffer,
                    active_entry_telemetry);
            } else {
                write_minizip_entry_chunks(
                    writer.get(), entry, file_buffer, crc_plan.reuse_staged_crc32,
                    active_entry_telemetry);
            }

            const auto entry_close_started = active_entry_telemetry != nullptr
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point {};
            const std::uint64_t entry_close_process_cpu_started =
                active_entry_telemetry != nullptr ? process_cpu_microseconds() : 0;
            check_minizip_result(mz_zip_writer_entry_close(writer.get()), "close ZIP entry");
            if (active_entry_telemetry != nullptr) {
                active_entry_telemetry->close_us = elapsed_microseconds(entry_close_started);
                active_entry_telemetry->close_process_cpu_us =
                    elapsed_process_cpu_microseconds(entry_close_process_cpu_started);
            }
            if (crc_plan.reuse_staged_crc32) {
                const auto crc_validation_started = active_entry_telemetry != nullptr
                    ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point {};
                validate_written_entry_crc32(zip_handle, entry, crc_plan, file_buffer);
                if (active_entry_telemetry != nullptr) {
                    active_entry_telemetry->staged_crc_validation_us +=
                        elapsed_microseconds(crc_validation_started);
                }
            }
            if (active_entry_telemetry != nullptr) {
                if (!active_entry_telemetry->raw_compressed_copy
                    && compression_level != package_writer_min_compression_level) {
                    active_entry_telemetry->deflate_writer_process_cpu_us =
                        active_entry_telemetry->writer_write_process_cpu_us
                        + active_entry_telemetry->close_process_cpu_us;
                }
                active_entry_telemetry->total_us = elapsed_microseconds(entry_started);
                active_entry_telemetry->total_process_cpu_us =
                    elapsed_process_cpu_microseconds(entry_process_cpu_started);
                telemetry->entries.push_back(std::move(entry_telemetry));
            }
        }

        const auto close_started = telemetry != nullptr
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point {};
        check_minizip_result(mz_zip_writer_close(writer.get()), "close XLSX package");
        if (telemetry != nullptr) {
            telemetry->close_us = elapsed_microseconds(close_started);
        }
    } catch (...) {
        (void)mz_zip_writer_close(writer.get());
        throw;
    }
}

#endif

} // namespace

bool package_writer_can_raw_copy_compression_method(
    PackageWriterOptions options, std::uint16_t compression_method) noexcept
{
#ifdef FASTXLSX_HAS_MINIZIP_NG
    if (resolve_backend(options.backend) != PackageWriterBackend::MinizipNg) {
        return false;
    }
    const std::uint16_t output_method =
        options.compression_level == package_writer_min_compression_level
        ? stored_compression_method
        : deflate_compression_method;
    return compression_method == output_method;
#else
    (void)options;
    (void)compression_method;
    return false;
#endif
}

void write_package(const std::filesystem::path& path, const std::vector<PackageEntry>& entries,
    PackageWriterOptions options)
{
    const auto total_started = options.telemetry != nullptr
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point {};
    const std::uint64_t total_process_cpu_started = options.telemetry != nullptr
        ? process_cpu_microseconds()
        : 0;
    validate_options(options);
    validate_package_entries_zip32(entries);

    const PackageWriterBackend backend = resolve_backend(options.backend);
    if (options.telemetry != nullptr) {
        *options.telemetry = PackageWriterTelemetry {};
        options.telemetry->backend = backend;
    }

    switch (backend) {
    case PackageWriterBackend::StoredZipBootstrap:
        if (std::any_of(entries.begin(), entries.end(), [](const PackageEntry& entry) {
                return entry.raw_compressed_source.has_value();
            })) {
            throw FastXlsxError(
                "raw compressed ZIP entry copy requires the minizip-ng backend");
        }
        write_stored_zip(path, entries);
        if (options.telemetry != nullptr) {
            options.telemetry->total_us = elapsed_microseconds(total_started);
            options.telemetry->total_process_cpu_us =
                elapsed_process_cpu_microseconds(total_process_cpu_started);
        }
        return;
    case PackageWriterBackend::MinizipNg:
#ifdef FASTXLSX_HAS_MINIZIP_NG
        write_minizip_package(
            path, entries, options.compression_level, options.telemetry);
        if (options.telemetry != nullptr) {
            options.telemetry->total_us = elapsed_microseconds(total_started);
            options.telemetry->total_process_cpu_us =
                elapsed_process_cpu_microseconds(total_process_cpu_started);
        }
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
