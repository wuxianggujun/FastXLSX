#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace fastxlsx_reference_benchmark {

constexpr std::uint32_t kExcelRowLimit = 1048576;
constexpr std::uint32_t kExcelColumnLimit = 16384;
constexpr std::uint32_t kBenchmarkSheetLimit = 1024;
constexpr std::string_view kSchemaVersion = "1";

inline std::filesystem::path default_output_dir()
{
#ifdef FASTXLSX_REFERENCE_BENCH_DEFAULT_OUTPUT_DIR
    return std::filesystem::path(FASTXLSX_REFERENCE_BENCH_DEFAULT_OUTPUT_DIR);
#else
    return std::filesystem::current_path();
#endif
}

struct Options {
    std::string library;
    std::string library_mode = "workbook-api";
    std::uint32_t rows = 10000;
    std::uint32_t cols = 10;
    std::uint32_t sheets = 1;
    double string_ratio = 0.0;
    std::string scenario = "numeric";
    std::string string_pattern = "mixed";
    std::filesystem::path output = default_output_dir() / "reference-bench.xlsx";
    std::filesystem::path result = default_output_dir() / "reference-bench.json";
};

struct StringDistributionStats {
    std::uint64_t string_cell_count = 0;
    std::uint64_t repeated_string_cell_count = 0;
    std::uint64_t non_repeated_unique_string_cell_count = 0;

    [[nodiscard]] std::uint64_t unique_string_value_count() const noexcept
    {
        return non_repeated_unique_string_cell_count + (repeated_string_cell_count > 0 ? 1 : 0);
    }

    [[nodiscard]] std::uint64_t duplicate_string_cell_count() const noexcept
    {
        return string_cell_count - unique_string_value_count();
    }

    [[nodiscard]] double dedup_ratio() const noexcept
    {
        if (string_cell_count == 0) {
            return 0.0;
        }
        return static_cast<double>(duplicate_string_cell_count())
            / static_cast<double>(string_cell_count);
    }
};

[[noreturn]] inline void fail(std::string_view message)
{
    throw std::runtime_error(std::string(message));
}

inline std::uint32_t parse_u32(std::string_view value, std::string_view name)
{
    std::uint64_t parsed = 0;
    if (value.empty()) {
        fail(std::string(name) + " cannot be empty");
    }
    for (const char ch : value) {
        if (ch < '0' || ch > '9') {
            fail(std::string(name) + " must be a positive integer");
        }
        parsed = parsed * 10 + static_cast<std::uint64_t>(ch - '0');
        if (parsed > std::numeric_limits<std::uint32_t>::max()) {
            fail(std::string(name) + " is too large");
        }
    }
    if (parsed == 0) {
        fail(std::string(name) + " must be greater than zero");
    }
    return static_cast<std::uint32_t>(parsed);
}

inline double parse_ratio(std::string_view value)
{
    std::string text(value);
    std::size_t consumed = 0;
    double parsed = 0.0;
    try {
        parsed = std::stod(text, &consumed);
    } catch (const std::exception&) {
        fail("--string-ratio must be between 0 and 1");
    }
    if (consumed != text.size() || !std::isfinite(parsed) || parsed < 0.0 || parsed > 1.0) {
        fail("--string-ratio must be between 0 and 1");
    }
    return parsed;
}

inline std::uint64_t checked_cell_count(const Options& options)
{
    const std::uint64_t rows = options.rows;
    const std::uint64_t cols = options.cols;
    const std::uint64_t sheets = options.sheets;
    const auto max = std::numeric_limits<std::uint64_t>::max();
    if (rows > max / cols || rows * cols > max / sheets) {
        fail("--rows * --cols * --sheets overflows cell count");
    }
    return rows * cols * sheets;
}

inline Options parse_args(int argc, char** argv, std::string library, std::string library_mode)
{
    Options options;
    options.library = std::move(library);
    options.library_mode = std::move(library_mode);
    options.output = default_output_dir() / ("reference-bench-" + options.library + ".xlsx");
    options.result = default_output_dir() / ("reference-bench-" + options.library + ".json");

    for (int index = 1; index < argc; ++index) {
        const std::string_view arg = argv[index];
        auto next_value = [&]() -> std::string_view {
            if (index + 1 >= argc) {
                fail(std::string(arg) + " requires a value");
            }
            ++index;
            return argv[index];
        };

        if (arg == "--rows") {
            options.rows = parse_u32(next_value(), "--rows");
        } else if (arg == "--cols") {
            options.cols = parse_u32(next_value(), "--cols");
        } else if (arg == "--sheets") {
            options.sheets = parse_u32(next_value(), "--sheets");
        } else if (arg == "--string-ratio") {
            options.string_ratio = parse_ratio(next_value());
        } else if (arg == "--scenario") {
            options.scenario = std::string(next_value());
        } else if (arg == "--string-pattern") {
            options.string_pattern = std::string(next_value());
        } else if (arg == "--output") {
            options.output = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--result") {
            options.result = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: reference writer adapter "
                << "--scenario numeric|mixed|strings --rows N --cols N --sheets N "
                << "--string-pattern mixed|repeated|unique --string-ratio 0..1 "
                << "--output file.xlsx --result result.json\n";
            std::exit(0);
        } else {
            fail(std::string("unknown argument: ") + std::string(arg));
        }
    }

    if (options.scenario != "numeric" && options.scenario != "mixed"
        && options.scenario != "strings") {
        fail("--scenario must be numeric, mixed, or strings");
    }
    if (options.string_pattern != "mixed" && options.string_pattern != "repeated"
        && options.string_pattern != "unique") {
        fail("--string-pattern must be mixed, repeated, or unique");
    }
    if (options.scenario == "numeric") {
        options.string_ratio = 0.0;
    } else if (options.scenario == "strings") {
        options.string_ratio = 1.0;
    }
    if (options.cols > kExcelColumnLimit) {
        fail("--cols exceeds Excel's column limit");
    }
    if (options.rows > kExcelRowLimit) {
        fail("--rows exceeds Excel's row limit");
    }
    if (options.sheets > kBenchmarkSheetLimit) {
        fail("--sheets exceeds the benchmark tool limit");
    }
    checked_cell_count(options);
    return options;
}

inline bool should_write_string(const Options& options, std::uint32_t row, std::uint32_t col)
{
    if (options.scenario == "numeric") {
        return false;
    }
    if (options.scenario == "strings") {
        return true;
    }
    if (options.string_ratio <= 0.0) {
        return false;
    }
    if (options.string_ratio >= 1.0) {
        return true;
    }
    const double raw_bucket = 1.0 / options.string_ratio;
    if (!std::isfinite(raw_bucket)
        || raw_bucket >= static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    const auto bucket = std::max<std::uint32_t>(static_cast<std::uint32_t>(raw_bucket), 1);
    return ((row + col) % bucket) == 0;
}

inline bool string_value_is_repeated(
    std::string_view pattern, std::uint32_t row, std::uint32_t col)
{
    if (pattern == "repeated") {
        return true;
    }
    if (pattern == "unique") {
        return false;
    }
    return ((row + col) % 5) == 0;
}

inline void observe_string_cell(
    StringDistributionStats& stats, std::string_view pattern, std::uint32_t row, std::uint32_t col)
{
    ++stats.string_cell_count;
    if (string_value_is_repeated(pattern, row, col)) {
        ++stats.repeated_string_cell_count;
    } else {
        ++stats.non_repeated_unique_string_cell_count;
    }
}

inline std::string make_string_value(
    std::string_view pattern, std::uint32_t sheet, std::uint32_t row, std::uint32_t col)
{
    if (string_value_is_repeated(pattern, row, col)) {
        return "repeat";
    }
    return "s" + std::to_string(sheet) + "-r" + std::to_string(row) + "-c" + std::to_string(col);
}

inline double make_number_value(std::uint32_t sheet, std::uint32_t row, std::uint32_t col)
{
    return static_cast<double>(sheet) * 1000000.0 + static_cast<double>(row) * 1000.0
        + static_cast<double>(col);
}

inline std::string cell_reference(std::uint32_t row, std::uint32_t col)
{
    std::string name;
    std::uint32_t column = col;
    while (column > 0) {
        --column;
        name.push_back(static_cast<char>('A' + (column % 26)));
        column /= 26;
    }
    std::reverse(name.begin(), name.end());
    name += std::to_string(row);
    return name;
}

inline std::uint64_t peak_memory_bytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters {};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
        return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
    }
#endif
    return 0;
}

inline std::string json_escape(std::string_view value)
{
    std::string escaped;
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
            escaped += ch;
            break;
        }
    }
    return escaped;
}

inline void ensure_parent_directory(const std::filesystem::path& path)
{
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

inline void write_result_json(const Options& options, std::uint64_t elapsed_ms,
    std::uint64_t peak_bytes, std::uint64_t output_bytes,
    const StringDistributionStats& string_distribution)
{
    ensure_parent_directory(options.result);
    std::ofstream out(options.result, std::ios::binary);
    if (!out) {
        fail("failed to open benchmark result file");
    }

    const std::uint64_t cells = checked_cell_count(options);
    out << "{\n";
    out << "  \"reference_benchmark_schema_version\": \"" << kSchemaVersion << "\",\n";
    out << "  \"library\": \"" << json_escape(options.library) << "\",\n";
    out << "  \"library_mode\": \"" << json_escape(options.library_mode) << "\",\n";
    out << "  \"scenario\": \"" << json_escape(options.scenario) << "\",\n";
    out << "  \"rows\": " << options.rows << ",\n";
    out << "  \"cols\": " << options.cols << ",\n";
    out << "  \"sheets\": " << options.sheets << ",\n";
    out << "  \"cells\": " << cells << ",\n";
    out << "  \"string_ratio\": " << options.string_ratio << ",\n";
    out << "  \"string_pattern\": \"" << json_escape(options.string_pattern) << "\",\n";
    out << "  \"string_cells\": " << string_distribution.string_cell_count << ",\n";
    out << "  \"unique_string_values\": " << string_distribution.unique_string_value_count() << ",\n";
    out << "  \"duplicate_string_cells\": " << string_distribution.duplicate_string_cell_count() << ",\n";
    out << "  \"string_dedup_ratio\": " << string_distribution.dedup_ratio() << ",\n";
    out << "  \"elapsed_ms\": " << elapsed_ms << ",\n";
    out << "  \"peak_memory_mb\": " << (peak_bytes / (1024.0 * 1024.0)) << ",\n";
    out << "  \"output_bytes\": " << output_bytes << ",\n";
    out << "  \"output\": \"" << json_escape(options.output.string()) << "\",\n";
    out << "  \"office_open\": \"not_run\"\n";
    out << "}\n";
}

class ScopedTimer {
public:
    ScopedTimer() : started_(std::chrono::steady_clock::now()) {}

    [[nodiscard]] std::uint64_t elapsed_ms() const
    {
        const auto finished = std::chrono::steady_clock::now();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(finished - started_).count());
    }

private:
    std::chrono::steady_clock::time_point started_;
};

} // namespace fastxlsx_reference_benchmark
