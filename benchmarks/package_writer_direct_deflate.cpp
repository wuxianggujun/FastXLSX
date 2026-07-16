#include "package_writer_direct_deflate.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#ifdef FASTXLSX_BENCH_HAS_ZLIB
#include <zlib.h>
#endif

namespace fastxlsx::benchmarks {
namespace {

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
    const std::clock_t value = std::clock();
    if (value == static_cast<std::clock_t>(-1)) {
        return 0;
    }
    return static_cast<std::uint64_t>(
        static_cast<long double>(value) * 1'000'000.0L
        / static_cast<long double>(CLOCKS_PER_SEC));
#endif
}

std::uint64_t elapsed_process_cpu_microseconds(std::uint64_t started) noexcept
{
    const std::uint64_t finished = process_cpu_microseconds();
    return finished >= started ? finished - started : 0;
}

class ScopedOutputRemoval {
public:
    explicit ScopedOutputRemoval(std::filesystem::path path)
        : path_(std::move(path))
    {
    }

    ~ScopedOutputRemoval()
    {
        if (!keep_) {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }

    void keep() noexcept
    {
        keep_ = true;
    }

private:
    std::filesystem::path path_;
    bool keep_ = false;
};

[[noreturn]] void fail(std::string message)
{
    throw std::runtime_error(std::move(message));
}

} // namespace

DirectDeflateTelemetry write_raw_deflate_file(const std::filesystem::path& input_path,
    const std::filesystem::path& output_path, std::size_t input_buffer_size,
    std::size_t output_buffer_size, int compression_level,
    std::uint64_t expected_input_size, std::uint32_t expected_input_crc32)
{
#ifndef FASTXLSX_BENCH_HAS_ZLIB
    (void)input_path;
    (void)output_path;
    (void)input_buffer_size;
    (void)output_buffer_size;
    (void)compression_level;
    (void)expected_input_size;
    (void)expected_input_crc32;
    fail("direct zlib benchmark backend is unavailable in this build");
#else
    if (input_buffer_size == 0 || output_buffer_size == 0
        || input_buffer_size > std::numeric_limits<uInt>::max()
        || output_buffer_size > std::numeric_limits<uInt>::max()) {
        fail("direct zlib benchmark buffer size is invalid");
    }
    if (compression_level < 1 || compression_level > 9) {
        fail("direct zlib benchmark compression level must be between 1 and 9");
    }

    DirectDeflateTelemetry telemetry;
    telemetry.input_buffer_bytes = input_buffer_size;
    telemetry.output_buffer_bytes = output_buffer_size;
    const auto total_started = std::chrono::steady_clock::now();
    const std::uint64_t total_process_cpu_started = process_cpu_microseconds();

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        fail("failed to open direct zlib benchmark input");
    }
    if (!output_path.parent_path().empty()) {
        std::filesystem::create_directories(output_path.parent_path());
    }
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        fail("failed to create direct zlib benchmark output");
    }
    ScopedOutputRemoval remove_failed_output(output_path);

    std::vector<unsigned char> input_buffer(input_buffer_size);
    std::vector<unsigned char> output_buffer(output_buffer_size);
    z_stream stream {};
    const int init_result = deflateInit2(&stream, compression_level, Z_DEFLATED,
        -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    if (init_result != Z_OK) {
        fail("failed to initialize direct raw DEFLATE stream: "
            + std::to_string(init_result));
    }
    bool stream_initialized = true;
    auto end_stream = [&]() noexcept {
        if (stream_initialized) {
            (void)deflateEnd(&stream);
            stream_initialized = false;
        }
    };

    uLong crc32 = ::crc32(0L, Z_NULL, 0);
    stream.next_out = output_buffer.data();
    stream.avail_out = static_cast<uInt>(output_buffer.size());
    auto flush_output = [&]() {
        const std::size_t produced = output_buffer.size() - stream.avail_out;
        if (produced == 0) {
            return;
        }
        const auto write_started = std::chrono::steady_clock::now();
        output.write(reinterpret_cast<const char*>(output_buffer.data()),
            static_cast<std::streamsize>(produced));
        const std::uint64_t write_us = elapsed_microseconds(write_started);
        ++telemetry.output_write_calls;
        telemetry.output_write_us += write_us;
        telemetry.output_write_max_us = std::max(telemetry.output_write_max_us, write_us);
        telemetry.output_bytes += produced;
        telemetry.output_buffer_peak_bytes = std::max<std::uint64_t>(
            telemetry.output_buffer_peak_bytes, produced);
        if (!output) {
            end_stream();
            fail("failed to write direct raw DEFLATE output");
        }
        stream.next_out = output_buffer.data();
        stream.avail_out = static_cast<uInt>(output_buffer.size());
    };

    auto deflate_once = [&](int flush) {
        if (stream.avail_out == 0) {
            flush_output();
        }
        const auto deflate_started = std::chrono::steady_clock::now();
        const int result = deflate(&stream, flush);
        const std::uint64_t deflate_us = elapsed_microseconds(deflate_started);
        ++telemetry.deflate_calls;
        telemetry.deflate_us += deflate_us;
        telemetry.deflate_max_us = std::max(telemetry.deflate_max_us, deflate_us);
        if (stream.avail_out == 0 || result == Z_STREAM_END) {
            flush_output();
        }
        return result;
    };

    try {
        while (true) {
            const auto read_started = std::chrono::steady_clock::now();
            input.read(reinterpret_cast<char*>(input_buffer.data()),
                static_cast<std::streamsize>(input_buffer.size()));
            const std::streamsize read_size = input.gcount();
            telemetry.input_read_us += elapsed_microseconds(read_started);
            if (input.bad()) {
                fail("failed to read direct zlib benchmark input");
            }
            if (read_size <= 0) {
                break;
            }
            ++telemetry.input_read_calls;
            telemetry.input_bytes += static_cast<std::uint64_t>(read_size);
            const auto crc_started = std::chrono::steady_clock::now();
            crc32 = ::crc32(crc32, input_buffer.data(), static_cast<uInt>(read_size));
            telemetry.input_crc32_us += elapsed_microseconds(crc_started);

            stream.next_in = input_buffer.data();
            stream.avail_in = static_cast<uInt>(read_size);
            while (stream.avail_in > 0) {
                const int result = deflate_once(Z_NO_FLUSH);
                if (result != Z_OK) {
                    fail("direct raw DEFLATE input step failed: "
                        + std::to_string(result));
                }
            }
        }

        while (true) {
            const int result = deflate_once(Z_FINISH);
            if (result == Z_STREAM_END) {
                break;
            }
            if (result != Z_OK) {
                fail("direct raw DEFLATE finish step failed: "
                    + std::to_string(result));
            }
        }

        const int end_result = deflateEnd(&stream);
        stream_initialized = false;
        if (end_result != Z_OK) {
            fail("failed to close direct raw DEFLATE stream: "
                + std::to_string(end_result));
        }
        output.flush();
        output.close();
        if (!output) {
            fail("failed to close direct raw DEFLATE output");
        }
    } catch (...) {
        end_stream();
        throw;
    }

    telemetry.input_crc32 = static_cast<std::uint32_t>(crc32);
    if (telemetry.input_bytes != expected_input_size) {
        fail("direct raw DEFLATE input size mismatch");
    }
    if (telemetry.input_crc32 != expected_input_crc32) {
        fail("direct raw DEFLATE input CRC32 mismatch");
    }
    if (telemetry.output_bytes == 0) {
        fail("direct raw DEFLATE produced no output");
    }

    telemetry.total_us = elapsed_microseconds(total_started);
    telemetry.total_process_cpu_us =
        elapsed_process_cpu_microseconds(total_process_cpu_started);
    remove_failed_output.keep();
    return telemetry;
#endif
}

} // namespace fastxlsx::benchmarks
