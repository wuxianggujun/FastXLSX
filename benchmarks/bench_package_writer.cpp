#include "../src/package_writer.hpp"
#include "package_writer_direct_deflate.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace {

constexpr std::string_view kBenchmarkSchemaVersion = "2";
constexpr std::uint32_t kExcelRowLimit = 1048576;
constexpr std::uint32_t kExcelColumnLimit = 16384;

std::filesystem::path default_output_dir()
{
#ifdef FASTXLSX_BENCH_DEFAULT_OUTPUT_DIR
    return std::filesystem::path(FASTXLSX_BENCH_DEFAULT_OUTPUT_DIR);
#else
    return std::filesystem::current_path();
#endif
}

struct Options {
    std::uint32_t rows = 100000;
    std::uint32_t cols = 10;
    std::string pattern = "numeric";
    std::string backend = "minizip";
    int compression_level = 1;
    std::size_t file_io_buffer_size =
        fastxlsx::detail::package_writer_default_file_io_buffer_size;
    std::size_t deflate_output_buffer_size = 32U * 1024U;
    bool prepare_only = false;
    std::uint64_t payload_size = 0;
    std::uint32_t payload_crc32 = 0;
    std::filesystem::path payload =
        default_output_dir() / "fastxlsx-package-writer-payload.xml";
    std::filesystem::path output =
        default_output_dir() / "fastxlsx-package-writer-output.xlsx";
    std::filesystem::path result =
        default_output_dir() / "fastxlsx-package-writer-bench.json";
};

[[noreturn]] void fail(std::string_view message)
{
    throw std::runtime_error(std::string(message));
}

std::uint64_t parse_u64(std::string_view value, std::string_view name)
{
    if (value.empty()) {
        fail(std::string(name) + " cannot be empty");
    }
    std::uint64_t parsed = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') {
            fail(std::string(name) + " must be a non-negative integer");
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
            fail(std::string(name) + " is too large");
        }
        parsed = parsed * 10U + digit;
    }
    return parsed;
}

std::uint32_t parse_u32(std::string_view value, std::string_view name)
{
    const std::uint64_t parsed = parse_u64(value, name);
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        fail(std::string(name) + " is too large");
    }
    return static_cast<std::uint32_t>(parsed);
}

int parse_compression_level(std::string_view value)
{
    const std::uint32_t parsed = parse_u32(value, "--compression-level");
    if (parsed < 1U || parsed > 9U) {
        fail("--compression-level must be between 1 and 9");
    }
    return static_cast<int>(parsed);
}

std::size_t parse_buffer_kib(std::string_view value)
{
    const std::uint64_t kib = parse_u64(value, "--file-buffer-kib");
    if (kib > std::numeric_limits<std::size_t>::max() / 1024U) {
        fail("--file-buffer-kib is too large");
    }
    const std::size_t bytes = static_cast<std::size_t>(kib * 1024U);
    if (bytes < fastxlsx::detail::package_writer_min_file_io_buffer_size
        || bytes > fastxlsx::detail::package_writer_max_file_io_buffer_size
        || (bytes & (bytes - 1U)) != 0U) {
        fail("--file-buffer-kib must select a power of two from 64 KiB to 4096 KiB");
    }
    return bytes;
}

std::size_t parse_deflate_output_buffer_kib(std::string_view value)
{
    const std::uint64_t kib = parse_u64(value, "--deflate-output-kib");
    if (kib > std::numeric_limits<std::size_t>::max() / 1024U) {
        fail("--deflate-output-kib is too large");
    }
    const std::size_t bytes = static_cast<std::size_t>(kib * 1024U);
    if (bytes < 16U * 1024U || bytes > 4U * 1024U * 1024U
        || (bytes & (bytes - 1U)) != 0U) {
        fail("--deflate-output-kib must select a power of two from 16 KiB to 4096 KiB");
    }
    return bytes;
}

Options parse_options(int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        const auto next_value = [&]() -> std::string_view {
            if (++index >= argc) {
                fail(std::string(argument) + " requires a value");
            }
            return argv[index];
        };

        if (argument == "--rows") {
            options.rows = parse_u32(next_value(), argument);
        } else if (argument == "--cols") {
            options.cols = parse_u32(next_value(), argument);
        } else if (argument == "--pattern") {
            options.pattern = std::string(next_value());
        } else if (argument == "--backend") {
            options.backend = std::string(next_value());
        } else if (argument == "--compression-level") {
            options.compression_level = parse_compression_level(next_value());
        } else if (argument == "--file-buffer-kib") {
            options.file_io_buffer_size = parse_buffer_kib(next_value());
        } else if (argument == "--deflate-output-kib") {
            options.deflate_output_buffer_size =
                parse_deflate_output_buffer_kib(next_value());
        } else if (argument == "--payload-size") {
            options.payload_size = parse_u64(next_value(), argument);
        } else if (argument == "--payload-crc32") {
            options.payload_crc32 = parse_u32(next_value(), argument);
        } else if (argument == "--payload") {
            options.payload = std::filesystem::path(next_value());
        } else if (argument == "--output") {
            options.output = std::filesystem::path(next_value());
        } else if (argument == "--result") {
            options.result = std::filesystem::path(next_value());
        } else if (argument == "--prepare-only") {
            options.prepare_only = true;
        } else if (argument == "--help" || argument == "-h") {
            std::cout
                << "Usage: fastxlsx_bench_package_writer [--rows N] [--cols N] "
                   "[--pattern numeric|mixed-inline|formula] [--compression-level 1..9] "
                   "[--backend minizip|direct-zlib-raw|direct-zlib-one-pass] "
                   "[--file-buffer-kib 64..4096] [--payload-size N] "
                   "[--deflate-output-kib 16..4096] "
                   "[--payload-crc32 N] [--payload PATH] [--output PATH] "
                   "[--result PATH] [--prepare-only]\n";
            std::exit(0);
        } else {
            fail(std::string("unknown argument: ") + std::string(argument));
        }
    }

    if (options.rows == 0 || options.rows > kExcelRowLimit) {
        fail("--rows must be between 1 and Excel's row limit");
    }
    if (options.cols == 0 || options.cols > kExcelColumnLimit) {
        fail("--cols must be between 1 and Excel's column limit");
    }
    if (options.pattern != "numeric" && options.pattern != "mixed-inline"
        && options.pattern != "formula") {
        fail("--pattern must be numeric, mixed-inline, or formula");
    }
    if (options.backend != "minizip" && options.backend != "direct-zlib-raw"
        && options.backend != "direct-zlib-one-pass") {
        fail("--backend must be minizip, direct-zlib-raw, or direct-zlib-one-pass");
    }
#ifndef FASTXLSX_BENCH_HAS_ZLIB
    if (options.backend != "minizip") {
        fail("direct zlib backends require a minizip/zlib benchmark build");
    }
#endif
#ifndef FASTXLSX_BENCH_HAS_DIRECT_ZLIB_PROFILING
    if (options.backend == "direct-zlib-one-pass") {
        fail("direct-zlib-one-pass requires FASTXLSX_ENABLE_DIRECT_ZLIB_PROFILING=ON");
    }
#endif
    const std::uint64_t cells = static_cast<std::uint64_t>(options.rows) * options.cols;
    if (cells > std::numeric_limits<std::uint32_t>::max()) {
        fail("rows * cols is too large for this benchmark");
    }
    if (!options.prepare_only && options.payload_size == 0) {
        fail("timed runs require --payload-size");
    }
    return options;
}

void ensure_parent_directory(const std::filesystem::path& path)
{
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
}

std::string json_escape(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

const std::array<std::uint32_t, 256>& crc32_table()
{
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> values {};
        for (std::uint32_t index = 0; index < values.size(); ++index) {
            std::uint32_t crc = index;
            for (int bit = 0; bit < 8; ++bit) {
                crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xedb88320U : crc >> 1U;
            }
            values[index] = crc;
        }
        return values;
    }();
    return table;
}

class Crc32Accumulator {
public:
    void update(std::string_view bytes)
    {
        const auto& table = crc32_table();
        for (const unsigned char byte : bytes) {
            crc_ = table[(crc_ ^ byte) & 0xffU] ^ (crc_ >> 8U);
        }
    }

    [[nodiscard]] std::uint32_t value() const noexcept
    {
        return crc_ ^ 0xffffffffU;
    }

private:
    std::uint32_t crc_ = 0xffffffffU;
};

std::string column_name(std::uint32_t column)
{
    std::string result;
    while (column != 0) {
        const std::uint32_t remainder = (column - 1U) % 26U;
        result.push_back(static_cast<char>('A' + remainder));
        column = (column - 1U) / 26U;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

std::string cell_reference(std::uint32_t row, std::uint32_t column)
{
    return column_name(column) + std::to_string(row);
}

struct PayloadMetadata {
    std::uint64_t bytes = 0;
    std::uint32_t crc32 = 0;
    std::uint64_t generation_ms = 0;
};

void write_payload_piece(std::ofstream& output, Crc32Accumulator& crc,
    std::uint64_t& bytes, std::string_view piece)
{
    output.write(piece.data(), static_cast<std::streamsize>(piece.size()));
    if (!output) {
        fail("failed to write package-writer payload");
    }
    crc.update(piece);
    bytes += static_cast<std::uint64_t>(piece.size());
}

PayloadMetadata generate_payload(const Options& options)
{
    ensure_parent_directory(options.payload);
    std::ofstream output(options.payload, std::ios::binary | std::ios::trunc);
    if (!output) {
        fail("failed to create package-writer payload");
    }

    const auto started = std::chrono::steady_clock::now();
    Crc32Accumulator crc;
    std::uint64_t bytes = 0;
    const std::string dimension = "A1:" + cell_reference(options.rows, options.cols);
    const std::string prefix =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<dimension ref=\"" + dimension + "\"/><sheetData>";
    write_payload_piece(output, crc, bytes, prefix);

    std::string row_xml;
    row_xml.reserve(static_cast<std::size_t>(options.cols) * 80U + 64U);
    for (std::uint32_t row = 1; row <= options.rows; ++row) {
        row_xml.clear();
        row_xml += "<row r=\"" + std::to_string(row) + "\">";
        for (std::uint32_t column = 1; column <= options.cols; ++column) {
            const std::uint64_t cell_index =
                (static_cast<std::uint64_t>(row) - 1U) * options.cols + column;
            const std::string reference = cell_reference(row, column);
            if (options.pattern == "mixed-inline" && cell_index % 3U == 0U) {
                row_xml += "<c r=\"" + reference
                    + "\" t=\"inlineStr\"><is><t>value-"
                    + std::to_string(cell_index % 1000U) + "</t></is></c>";
            } else if (options.pattern == "formula" && cell_index % 10U == 0U) {
                row_xml += "<c r=\"" + reference
                    + "\"><f>1+1</f><v>2</v></c>";
            } else {
                row_xml += "<c r=\"" + reference + "\"><v>"
                    + std::to_string(cell_index) + "</v></c>";
            }
        }
        row_xml += "</row>";
        write_payload_piece(output, crc, bytes, row_xml);
    }
    write_payload_piece(output, crc, bytes, "</sheetData></worksheet>");
    output.close();
    if (!output) {
        fail("failed to close package-writer payload");
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);
    return {bytes, crc.value(), static_cast<std::uint64_t>(elapsed.count())};
}

std::string content_types_xml()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "</Types>";
}

std::string package_relationships_xml()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>"
        "</Relationships>";
}

std::string workbook_xml()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"Data\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
        "</workbook>";
}

std::string workbook_relationships_xml()
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
        "</Relationships>";
}

double peak_working_set_mb()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters {};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
        return 0.0;
    }
    return static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0);
#else
    return 0.0;
#endif
}

const fastxlsx::detail::PackageWriterEntryTelemetry& worksheet_telemetry(
    const fastxlsx::detail::PackageWriterTelemetry& telemetry)
{
    const auto entry = std::find_if(telemetry.entries.begin(), telemetry.entries.end(),
        [](const fastxlsx::detail::PackageWriterEntryTelemetry& value) {
            return value.entry_name == "xl/worksheets/sheet1.xml";
        });
    if (entry == telemetry.entries.end()) {
        fail("package writer did not report worksheet telemetry");
    }
    return *entry;
}

void write_prepare_result(const Options& options, const PayloadMetadata& metadata)
{
    ensure_parent_directory(options.result);
    std::ofstream output(options.result, std::ios::binary | std::ios::trunc);
    if (!output) {
        fail("failed to write package-writer preparation result");
    }
    output << "{\n"
        << "  \"package_writer_benchmark_schema_version\": \""
        << kBenchmarkSchemaVersion << "\",\n"
        << "  \"mode\": \"prepare\",\n"
        << "  \"rows\": " << options.rows << ",\n"
        << "  \"cols\": " << options.cols << ",\n"
        << "  \"source_cells\": "
        << static_cast<std::uint64_t>(options.rows) * options.cols << ",\n"
        << "  \"pattern\": \"" << json_escape(options.pattern) << "\",\n"
        << "  \"payload_bytes\": " << metadata.bytes << ",\n"
        << "  \"payload_crc32\": " << metadata.crc32 << ",\n"
        << "  \"payload_generation_ms\": " << metadata.generation_ms << ",\n"
        << "  \"payload\": \"" << json_escape(options.payload.generic_string())
        << "\"\n}\n";
}

void write_benchmark_result(const Options& options, std::uint64_t pipeline_total_us,
    std::uint64_t output_bytes, double peak_memory_mb,
    const fastxlsx::detail::PackageWriterTelemetry& telemetry,
    const std::optional<fastxlsx::benchmarks::DirectDeflateTelemetry>& direct_deflate,
    bool temporary_compressed_file_removed)
{
    const auto& entry = worksheet_telemetry(telemetry);
    const fastxlsx::benchmarks::DirectDeflateTelemetry direct =
        direct_deflate.value_or(fastxlsx::benchmarks::DirectDeflateTelemetry {});
    ensure_parent_directory(options.result);
    std::ofstream output(options.result, std::ios::binary | std::ios::trunc);
    if (!output) {
        fail("failed to write package-writer benchmark result");
    }
    output << "{\n"
        << "  \"package_writer_benchmark_schema_version\": \""
        << kBenchmarkSchemaVersion << "\",\n"
        << "  \"mode\": \"timed-write\",\n"
        << "  \"rows\": " << options.rows << ",\n"
        << "  \"cols\": " << options.cols << ",\n"
        << "  \"source_cells\": "
        << static_cast<std::uint64_t>(options.rows) * options.cols << ",\n"
        << "  \"pattern\": \"" << json_escape(options.pattern) << "\",\n"
        << "  \"backend\": \"" << json_escape(options.backend) << "\",\n"
        << "  \"compression_level\": " << options.compression_level << ",\n"
        << "  \"file_io_buffer_bytes\": " << options.file_io_buffer_size << ",\n"
        << "  \"direct_deflate_output_buffer_bytes\": "
        << (direct_deflate.has_value()
                ? options.deflate_output_buffer_size
                : entry.direct_zlib_output_buffer_bytes)
        << ",\n"
        << "  \"payload_bytes\": " << options.payload_size << ",\n"
        << "  \"payload_crc32\": " << options.payload_crc32 << ",\n"
        << "  \"write_ms\": " << pipeline_total_us / 1000U << ",\n"
        << "  \"pipeline_total_us\": " << pipeline_total_us << ",\n"
        << "  \"pipeline_total_process_cpu_us\": "
        << telemetry.total_process_cpu_us + direct.total_process_cpu_us << ",\n"
        << "  \"output_bytes\": " << output_bytes << ",\n"
        << "  \"peak_memory_mb\": " << peak_memory_mb << ",\n"
        << "  \"direct_deflate_total_us\": " << direct.total_us << ",\n"
        << "  \"direct_deflate_total_process_cpu_us\": "
        << direct.total_process_cpu_us << ",\n"
        << "  \"direct_deflate_input_bytes\": " << direct.input_bytes << ",\n"
        << "  \"direct_deflate_input_read_calls\": "
        << direct.input_read_calls << ",\n"
        << "  \"direct_deflate_input_read_us\": " << direct.input_read_us << ",\n"
        << "  \"direct_deflate_input_crc32_us\": " << direct.input_crc32_us << ",\n"
        << "  \"direct_deflate_input_crc32\": " << direct.input_crc32 << ",\n"
        << "  \"direct_deflate_calls\": " << direct.deflate_calls << ",\n"
        << "  \"direct_deflate_us\": " << direct.deflate_us << ",\n"
        << "  \"direct_deflate_max_us\": " << direct.deflate_max_us << ",\n"
        << "  \"direct_deflate_output_bytes\": " << direct.output_bytes << ",\n"
        << "  \"direct_deflate_output_write_calls\": "
        << direct.output_write_calls << ",\n"
        << "  \"direct_deflate_output_write_us\": "
        << direct.output_write_us << ",\n"
        << "  \"direct_deflate_output_write_max_us\": "
        << direct.output_write_max_us << ",\n"
        << "  \"direct_deflate_input_buffer_bytes\": "
        << direct.input_buffer_bytes << ",\n"
        << "  \"direct_deflate_output_buffer_peak_bytes\": "
        << direct.output_buffer_peak_bytes << ",\n"
        << "  \"temporary_compressed_file_removed\": "
        << (temporary_compressed_file_removed ? "true" : "false") << ",\n"
        << "  \"package_writer_total_us\": " << telemetry.total_us << ",\n"
        << "  \"package_writer_total_process_cpu_us\": "
        << telemetry.total_process_cpu_us << ",\n"
        << "  \"package_writer_open_us\": " << telemetry.open_us << ",\n"
        << "  \"package_writer_close_us\": " << telemetry.close_us << ",\n"
        << "  \"target_entry_total_us\": " << entry.total_us << ",\n"
        << "  \"target_entry_total_process_cpu_us\": "
        << entry.total_process_cpu_us << ",\n"
        << "  \"target_entry_open_us\": " << entry.open_us << ",\n"
        << "  \"target_entry_close_us\": " << entry.close_us << ",\n"
        << "  \"target_entry_close_process_cpu_us\": "
        << entry.close_process_cpu_us << ",\n"
        << "  \"target_entry_raw_compressed_copy\": "
        << (entry.raw_compressed_copy ? "true" : "false") << ",\n"
        << "  \"target_entry_direct_zlib_raw\": "
        << (entry.direct_zlib_raw ? "true" : "false") << ",\n"
        << "  \"target_entry_direct_zlib_output_bytes\": "
        << entry.direct_zlib_output_bytes << ",\n"
        << "  \"target_entry_direct_zlib_output_buffer_bytes\": "
        << entry.direct_zlib_output_buffer_bytes << ",\n"
        << "  \"target_entry_direct_zlib_output_buffer_peak_bytes\": "
        << entry.direct_zlib_output_buffer_peak_bytes << ",\n"
        << "  \"target_entry_direct_zlib_deflate_calls\": "
        << entry.direct_zlib_deflate_calls << ",\n"
        << "  \"target_entry_direct_zlib_deflate_us\": "
        << entry.direct_zlib_deflate_us << ",\n"
        << "  \"target_entry_direct_zlib_engine_process_cpu_us\": "
        << entry.direct_zlib_engine_process_cpu_us << ",\n"
        << "  \"target_entry_direct_zlib_deflate_max_us\": "
        << entry.direct_zlib_deflate_max_us << ",\n"
        << "  \"target_entry_direct_zlib_crc32_us\": "
        << entry.direct_zlib_crc32_us << ",\n"
        << "  \"target_entry_input_bytes\": " << entry.input_bytes << ",\n"
        << "  \"target_entry_input_read_calls\": " << entry.input_read_calls << ",\n"
        << "  \"target_entry_input_read_us\": " << entry.input_read_us << ",\n"
        << "  \"target_entry_input_read_wait_us\": "
        << entry.input_read_wait_us << ",\n"
        << "  \"target_entry_writer_write_calls\": "
        << entry.writer_write_calls << ",\n"
        << "  \"target_entry_writer_write_us\": " << entry.writer_write_us << ",\n"
        << "  \"target_entry_writer_write_process_cpu_us\": "
        << entry.writer_write_process_cpu_us << ",\n"
        << "  \"target_entry_writer_write_input_peak_bytes\": "
        << entry.writer_write_input_peak_bytes << ",\n"
        << "  \"target_entry_writer_write_max_us\": "
        << entry.writer_write_max_us << ",\n"
        << "  \"target_entry_deflate_writer_process_cpu_us\": "
        << entry.deflate_writer_process_cpu_us << ",\n"
        << "  \"target_entry_staged_crc_validation_us\": "
        << entry.staged_crc_validation_us << ",\n"
        << "  \"target_entry_reused_staged_crc32\": "
        << (entry.reused_staged_crc32 ? "true" : "false") << ",\n"
        << "  \"target_entry_staged_file_read_prefetch\": "
        << (entry.staged_file_read_prefetch ? "true" : "false") << ",\n"
        << "  \"target_entry_prefetched_staged_file_chunk_count\": "
        << entry.prefetched_staged_file_chunk_count << ",\n"
        << "  \"target_entry_prefetched_staged_input_bytes\": "
        << entry.prefetched_staged_input_bytes << ",\n"
        << "  \"target_entry_prefetch_peak_buffer_bytes\": "
        << entry.prefetch_peak_buffer_bytes << ",\n"
        << "  \"office_open\": \"not_run\",\n"
        << "  \"payload\": \"" << json_escape(options.payload.generic_string())
        << "\",\n"
        << "  \"output\": \"" << json_escape(options.output.generic_string())
        << "\"\n}\n";
}

std::vector<fastxlsx::detail::PackageEntry> package_entries(
    fastxlsx::detail::PackageEntry worksheet_entry)
{
    std::vector<fastxlsx::detail::PackageEntry> entries;
    entries.emplace_back("[Content_Types].xml", content_types_xml());
    entries.emplace_back("_rels/.rels", package_relationships_xml());
    entries.emplace_back("xl/workbook.xml", workbook_xml());
    entries.emplace_back("xl/_rels/workbook.xml.rels", workbook_relationships_xml());
    entries.push_back(std::move(worksheet_entry));
    return entries;
}

void run_timed_write(const Options& options)
{
    std::error_code error;
    const std::uint64_t measured_payload_size =
        std::filesystem::file_size(options.payload, error);
    if (error || measured_payload_size != options.payload_size) {
        fail("payload size does not match --payload-size");
    }

    ensure_parent_directory(options.output);
    fastxlsx::detail::PackageWriterTelemetry telemetry;
    fastxlsx::detail::PackageWriterOptions writer_options;
    writer_options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    writer_options.compression_level = options.compression_level;
    writer_options.file_io_buffer_size = options.file_io_buffer_size;
    writer_options.deflate_engine = options.backend == "direct-zlib-one-pass"
        ? fastxlsx::detail::PackageWriterDeflateEngine::DirectZlibRaw
        : fastxlsx::detail::PackageWriterDeflateEngine::MinizipNg;
    writer_options.deflate_output_buffer_size = options.deflate_output_buffer_size;
    writer_options.telemetry = &telemetry;

    std::optional<fastxlsx::benchmarks::DirectDeflateTelemetry> direct_deflate;
    std::filesystem::path temporary_compressed_path = options.output;
    temporary_compressed_path += ".worksheet.deflate";
    bool temporary_compressed_file_removed = options.backend != "direct-zlib-raw";
    const auto pipeline_started = std::chrono::steady_clock::now();
    try {
        std::vector<fastxlsx::detail::PackageEntry> entries;
        if (options.backend == "direct-zlib-raw") {
            direct_deflate = fastxlsx::benchmarks::write_raw_deflate_file(
                options.payload, temporary_compressed_path, options.file_io_buffer_size,
                options.deflate_output_buffer_size, options.compression_level,
                options.payload_size, options.payload_crc32);
            fastxlsx::detail::PackageRawCompressedEntrySource raw_source;
            raw_source.path = temporary_compressed_path;
            raw_source.compressed_size = direct_deflate->output_bytes;
            raw_source.uncompressed_size = options.payload_size;
            raw_source.crc32 = options.payload_crc32;
            raw_source.compression_method = 8;
            entries = package_entries(fastxlsx::detail::PackageEntry::raw_compressed_copy(
                "xl/worksheets/sheet1.xml", std::move(raw_source)));
        } else {
            fastxlsx::detail::PackageEntryChunk worksheet_chunk =
                fastxlsx::detail::PackageEntryChunk::file(options.payload);
            worksheet_chunk.has_expected_size = true;
            worksheet_chunk.expected_size = options.payload_size;
            worksheet_chunk.has_expected_crc32 = true;
            worksheet_chunk.expected_crc32 = options.payload_crc32;
            entries = package_entries(fastxlsx::detail::PackageEntry(
                "xl/worksheets/sheet1.xml",
                std::vector<fastxlsx::detail::PackageEntryChunk> {std::move(worksheet_chunk)}));
        }
        fastxlsx::detail::write_package(options.output, entries, writer_options);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary_compressed_path, ignored);
        throw;
    }
    const std::uint64_t pipeline_total_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - pipeline_started).count());

    if (direct_deflate.has_value()) {
        temporary_compressed_file_removed =
            std::filesystem::remove(temporary_compressed_path, error);
        if (error || !temporary_compressed_file_removed) {
            fail("failed to remove direct zlib benchmark temporary output");
        }
    }

    const std::uint64_t output_bytes = std::filesystem::file_size(options.output, error);
    if (error || output_bytes == 0) {
        fail("package writer produced no output");
    }
    write_benchmark_result(options, pipeline_total_us, output_bytes, peak_working_set_mb(),
        telemetry, direct_deflate, temporary_compressed_file_removed);
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Options options = parse_options(argc, argv);
        if (options.prepare_only) {
            const PayloadMetadata metadata = generate_payload(options);
            write_prepare_result(options, metadata);
            std::cout << "Prepared " << options.payload << " (" << metadata.bytes
                      << " bytes)\n";
            return 0;
        }
        run_timed_write(options);
        std::cout << "Wrote " << options.output << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
