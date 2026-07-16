#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace fastxlsx::benchmarks {

struct DirectDeflateTelemetry {
    std::uint64_t total_us = 0;
    std::uint64_t total_process_cpu_us = 0;
    std::uint64_t input_bytes = 0;
    std::uint64_t input_read_calls = 0;
    std::uint64_t input_read_us = 0;
    std::uint64_t input_crc32_us = 0;
    std::uint32_t input_crc32 = 0;
    std::uint64_t deflate_calls = 0;
    std::uint64_t deflate_us = 0;
    std::uint64_t deflate_max_us = 0;
    std::uint64_t output_bytes = 0;
    std::uint64_t output_write_calls = 0;
    std::uint64_t output_write_us = 0;
    std::uint64_t output_write_max_us = 0;
    std::uint64_t input_buffer_bytes = 0;
    std::uint64_t output_buffer_bytes = 0;
    std::uint64_t output_buffer_peak_bytes = 0;
};

DirectDeflateTelemetry write_raw_deflate_file(const std::filesystem::path& input_path,
    const std::filesystem::path& output_path, std::size_t input_buffer_size,
    std::size_t output_buffer_size, int compression_level,
    std::uint64_t expected_input_size, std::uint32_t expected_input_crc32);

} // namespace fastxlsx::benchmarks
