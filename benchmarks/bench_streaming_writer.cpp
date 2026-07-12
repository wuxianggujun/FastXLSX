#include <fastxlsx/streaming_writer.hpp>

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
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace {

constexpr std::uint32_t kExcelRowLimit = 1048576;
constexpr std::uint32_t kExcelColumnLimit = 16384;
constexpr std::uint32_t kBenchmarkSheetLimit = 1024;
constexpr std::string_view kBenchmarkSchemaVersion = "5";
constexpr std::string_view kPackageEntrySourceMode = "worksheet-file-backed-chunked";
constexpr std::string_view kTemporaryWorksheetPartFootprint = "worksheet-body-file-bytes";

} // namespace

namespace fastxlsx::detail {

#ifdef FASTXLSX_ENABLE_BENCHMARK_METRICS
void reset_benchmark_metrics() noexcept;
std::uint64_t benchmark_temporary_worksheet_part_footprint_bytes() noexcept;
std::uint64_t benchmark_worksheet_body_buffer_peak_bytes() noexcept;
std::uint64_t benchmark_worksheet_body_flush_count() noexcept;
std::uint64_t benchmark_active_worksheet_temporary_file_count() noexcept;
#endif

} // namespace fastxlsx::detail

namespace {

std::filesystem::path default_output_dir()
{
#ifdef FASTXLSX_BENCH_DEFAULT_OUTPUT_DIR
    return std::filesystem::path(FASTXLSX_BENCH_DEFAULT_OUTPUT_DIR);
#else
    return std::filesystem::current_path();
#endif
}

struct Options {
    std::uint32_t rows = 10000;
    std::uint32_t cols = 10;
    std::uint32_t sheets = 1;
    double string_ratio = 0.0;
    std::string scenario = "numeric";
    std::string string_pattern = "mixed";
    std::string string_strategy = "inline";
    int zip_compression_level = fastxlsx::default_zip_compression_level;
    std::filesystem::path output = default_output_dir() / "fastxlsx-bench-streaming.xlsx";
    std::filesystem::path result = default_output_dir() / "fastxlsx-bench-streaming.json";
};

[[noreturn]] void fail(std::string_view message)
{
    throw std::runtime_error(std::string(message));
}

std::uint32_t parse_u32(std::string_view value, std::string_view name)
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

double parse_ratio(std::string_view value)
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

int parse_compression_level(std::string_view value)
{
    if (value.empty()) {
        fail("--compression-level cannot be empty");
    }

    bool negative = false;
    std::size_t offset = 0;
    if (value.front() == '-') {
        negative = true;
        offset = 1;
    }
    if (offset == value.size()) {
        fail("--compression-level must be -1 or between 0 and 9");
    }

    int parsed = 0;
    for (; offset < value.size(); ++offset) {
        const char ch = value[offset];
        if (ch < '0' || ch > '9') {
            fail("--compression-level must be -1 or between 0 and 9");
        }
        parsed = parsed * 10 + (ch - '0');
        if (parsed > fastxlsx::max_zip_compression_level) {
            fail("--compression-level must be -1 or between 0 and 9");
        }
    }
    if (negative) {
        parsed = -parsed;
    }
    if (parsed != fastxlsx::default_zip_compression_level
        && (parsed < fastxlsx::min_zip_compression_level
            || parsed > fastxlsx::max_zip_compression_level)) {
        fail("--compression-level must be -1 or between 0 and 9");
    }
    return parsed;
}

std::uint64_t checked_cell_count(const Options& options)
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

Options parse_args(int argc, char** argv)
{
    Options options;
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
        } else if (arg == "--string-strategy") {
            options.string_strategy = std::string(next_value());
        } else if (arg == "--compression-level") {
            options.zip_compression_level = parse_compression_level(next_value());
        } else if (arg == "--output") {
            options.output = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--result") {
            options.result = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: fastxlsx_bench_streaming_writer "
                << "--scenario numeric|mixed|strings --rows N --cols N --sheets N "
                << "--string-pattern mixed|repeated|unique "
                << "--string-strategy inline|shared --string-ratio 0..1 "
                << "--compression-level -1|0..9 "
                << "--output file.xlsx --result result.json\n"
                << "Compression: -1 keeps the backend default, 0 stores, "
                << "1 is explicit throughput-first, 6 is zlib-default oriented.\n"
                << "Default output files are written under the benchmark build directory.\n";
            std::exit(0);
        } else {
            fail(std::string("unknown argument: ") + std::string(arg));
        }
    }

    if (options.scenario != "numeric" && options.scenario != "mixed"
        && options.scenario != "strings") {
        fail("--scenario must be numeric, mixed, or strings");
    }
    if (options.string_strategy != "inline" && options.string_strategy != "shared") {
        fail("--string-strategy must be inline or shared");
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
#ifndef FASTXLSX_BENCH_HAS_MINIZIP_NG
    if (options.zip_compression_level > fastxlsx::min_zip_compression_level) {
        fail("--compression-level 1..9 requires a minizip-ng benchmark build");
    }
#endif
    checked_cell_count(options);
    return options;
}

bool should_write_string(const Options& options, std::uint32_t row, std::uint32_t col)
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

bool string_value_is_repeated(std::string_view pattern, std::uint32_t row, std::uint32_t col)
{
    if (pattern == "repeated") {
        return true;
    }
    if (pattern == "unique") {
        return false;
    }
    return ((row + col) % 5) == 0;
}

void observe_string_cell(
    StringDistributionStats& stats, std::string_view pattern, std::uint32_t row, std::uint32_t col)
{
    ++stats.string_cell_count;
    if (string_value_is_repeated(pattern, row, col)) {
        ++stats.repeated_string_cell_count;
    } else {
        ++stats.non_repeated_unique_string_cell_count;
    }
}

std::string make_string_value(
    std::string_view pattern, std::uint32_t sheet, std::uint32_t row, std::uint32_t col)
{
    if (string_value_is_repeated(pattern, row, col)) {
        return "repeat";
    }
    return "s" + std::to_string(sheet) + "-r" + std::to_string(row) + "-c" + std::to_string(col);
}

double make_number_value(std::uint32_t sheet, std::uint32_t row, std::uint32_t col)
{
    return static_cast<double>(sheet) * 1000000.0 + static_cast<double>(row) * 1000.0
        + static_cast<double>(col);
}

std::uint64_t peak_memory_bytes()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters {};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0) {
        return static_cast<std::uint64_t>(counters.PeakWorkingSetSize);
    }
#endif
    return 0;
}

std::string json_escape(std::string_view value)
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

void ensure_parent_directory(const std::filesystem::path& path)
{
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void write_result_json(const Options& options, std::uint64_t generation_ms,
    std::uint64_t package_close_ms, std::uint64_t elapsed_ms, std::uint64_t peak_bytes,
    std::uint64_t output_bytes, std::uint64_t temporary_worksheet_part_footprint_bytes,
    std::uint64_t worksheet_body_buffer_peak_bytes,
    std::uint64_t worksheet_body_flush_count,
    std::uint64_t active_worksheet_temporary_files_after_close,
    const StringDistributionStats& string_distribution)
{
    ensure_parent_directory(options.result);
    std::ofstream out(options.result, std::ios::binary);
    if (!out) {
        fail("failed to open benchmark result file");
    }

    auto compression_label = [](int level) {
#ifdef FASTXLSX_BENCH_HAS_MINIZIP_NG
        if (level == fastxlsx::default_zip_compression_level) {
            return std::string("deflate-default");
        }
        if (level == fastxlsx::min_zip_compression_level) {
            return std::string("store-level-0");
        }
        return std::string("deflate-level-") + std::to_string(level);
#else
        if (level == fastxlsx::min_zip_compression_level) {
            return std::string("store-level-0");
        }
        return std::string("store");
#endif
    };

    const std::uint64_t cells = checked_cell_count(options);
    out << "{\n";
    out << "  \"benchmark_schema_version\": \"" << kBenchmarkSchemaVersion << "\",\n";
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
    out << "  \"string_strategy\": \"" << json_escape(options.string_strategy) << "\",\n";
#ifdef FASTXLSX_BENCH_HAS_MINIZIP_NG
    out << "  \"zip_backend\": \"minizip-ng\",\n";
#else
    out << "  \"zip_backend\": \"stored-bootstrap\",\n";
#endif
    out << "  \"compression\": \"" << compression_label(options.zip_compression_level) << "\",\n";
    out << "  \"compression_level\": " << options.zip_compression_level << ",\n";
    out << "  \"package_entry_source_mode\": \"" << kPackageEntrySourceMode << "\",\n";
    out << "  \"temporary_worksheet_part_footprint\": \"" << kTemporaryWorksheetPartFootprint << "\",\n";
    out << "  \"temporary_worksheet_part_footprint_bytes\": "
        << temporary_worksheet_part_footprint_bytes << ",\n";
    out << "  \"worksheet_body_buffer_limit_bytes\": " << (256U * 1024U) << ",\n";
    out << "  \"worksheet_body_buffer_peak_bytes\": "
        << worksheet_body_buffer_peak_bytes << ",\n";
    out << "  \"worksheet_body_flush_count\": "
        << worksheet_body_flush_count << ",\n";
    out << "  \"active_worksheet_temporary_files_after_close\": "
        << active_worksheet_temporary_files_after_close << ",\n";
    out << "  \"generation_ms\": " << generation_ms << ",\n";
    out << "  \"package_close_ms\": " << package_close_ms << ",\n";
    out << "  \"elapsed_ms\": " << elapsed_ms << ",\n";
    out << "  \"million_cells_per_second\": "
        << (elapsed_ms == 0 ? 0.0
                            : static_cast<double>(cells) / static_cast<double>(elapsed_ms) / 1000.0)
        << ",\n";
    out << "  \"peak_memory_mb\": " << (peak_bytes / (1024.0 * 1024.0)) << ",\n";
    out << "  \"output_bytes\": " << output_bytes << ",\n";
    out << "  \"office_open\": \"not_run\"\n";
    out << "}\n";
}

void run_benchmark(const Options& options)
{
    ensure_parent_directory(options.output);
#ifdef FASTXLSX_ENABLE_BENCHMARK_METRICS
    fastxlsx::detail::reset_benchmark_metrics();
#endif

    fastxlsx::WorkbookWriterOptions writer_options;
    writer_options.string_strategy = options.string_strategy == "shared"
        ? fastxlsx::StringStrategy::SharedString
        : fastxlsx::StringStrategy::InlineString;
    writer_options.zip_compression_level = options.zip_compression_level;

    const auto started = std::chrono::steady_clock::now();
    auto workbook = fastxlsx::WorkbookWriter::create(options.output, writer_options);
    StringDistributionStats string_distribution;

    for (std::uint32_t sheet_index = 1; sheet_index <= options.sheets; ++sheet_index) {
        auto sheet = workbook.add_worksheet("Sheet" + std::to_string(sheet_index));
        std::vector<std::string> string_values;
        std::vector<fastxlsx::CellView> cells;
        string_values.reserve(options.cols);
        cells.reserve(options.cols);

        for (std::uint32_t row = 1; row <= options.rows; ++row) {
            string_values.clear();
            cells.clear();

            for (std::uint32_t col = 1; col <= options.cols; ++col) {
                if (should_write_string(options, row, col)) {
                    observe_string_cell(string_distribution, options.string_pattern, row, col);
                    string_values.push_back(
                        make_string_value(options.string_pattern, sheet_index, row, col));
                    cells.push_back(fastxlsx::CellView::text(string_values.back()));
                } else {
                    cells.push_back(fastxlsx::CellView::number(make_number_value(sheet_index, row, col)));
                }
            }
            sheet.append_row(cells);
        }
    }

    const auto generation_finished = std::chrono::steady_clock::now();
    workbook.close();
    const auto finished = std::chrono::steady_clock::now();
    const auto generation_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            generation_finished - started).count());
    const auto package_close_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            finished - generation_finished).count());
    const auto elapsed_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count());
    const auto peak_bytes = peak_memory_bytes();
    const auto output_bytes = static_cast<std::uint64_t>(std::filesystem::file_size(options.output));
#ifdef FASTXLSX_ENABLE_BENCHMARK_METRICS
    const std::uint64_t temporary_worksheet_part_footprint_bytes =
        fastxlsx::detail::benchmark_temporary_worksheet_part_footprint_bytes();
    const std::uint64_t worksheet_body_buffer_peak_bytes =
        fastxlsx::detail::benchmark_worksheet_body_buffer_peak_bytes();
    const std::uint64_t worksheet_body_flush_count =
        fastxlsx::detail::benchmark_worksheet_body_flush_count();
    const std::uint64_t active_worksheet_temporary_files_after_close =
        fastxlsx::detail::benchmark_active_worksheet_temporary_file_count();
#else
    const std::uint64_t temporary_worksheet_part_footprint_bytes = 0;
    const std::uint64_t worksheet_body_buffer_peak_bytes = 0;
    const std::uint64_t worksheet_body_flush_count = 0;
    const std::uint64_t active_worksheet_temporary_files_after_close = 0;
#endif
    write_result_json(
        options, generation_ms, package_close_ms, elapsed_ms, peak_bytes, output_bytes,
        temporary_worksheet_part_footprint_bytes, worksheet_body_buffer_peak_bytes,
        worksheet_body_flush_count, active_worksheet_temporary_files_after_close,
        string_distribution);
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Options options = parse_args(argc, argv);
        run_benchmark(options);
        std::cout << "Wrote " << options.output.string() << '\n';
        std::cout << "Wrote " << options.result.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
