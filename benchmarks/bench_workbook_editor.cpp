#include <fastxlsx/streaming_writer.hpp>
#include <fastxlsx/workbook_editor.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
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
constexpr std::string_view kEditorBenchmarkSchemaVersion = "2";

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
    std::uint32_t edits = 1000;
    std::string scenario = "batch-set";
    std::filesystem::path source = default_output_dir() / "fastxlsx-editor-source.xlsx";
    std::filesystem::path output = default_output_dir() / "fastxlsx-editor-edited.xlsx";
    std::filesystem::path result = default_output_dir() / "fastxlsx-editor-bench.json";
};

struct Timings {
    std::uint64_t source_write_ms = 0;
    std::uint64_t open_ms = 0;
    std::uint64_t materialize_ms = 0;
    std::uint64_t mutation_ms = 0;
    std::uint64_t save_ms = 0;
    std::uint64_t total_editor_ms = 0;
    std::uint64_t total_ms = 0;
};

struct RunStats {
    Timings timings;
    std::uint64_t source_cells = 0;
    std::uint64_t touched_coordinates = 0;
    std::uint64_t inserted_coordinates = 0;
    std::uint64_t source_bytes = 0;
    std::uint64_t output_bytes = 0;
    std::uint64_t peak_memory_bytes = 0;
    bool materialized_worksheet = false;
    std::size_t materialized_cells_before = 0;
    std::size_t materialized_cells_after = 0;
    std::size_t estimated_memory_before = 0;
    std::size_t estimated_memory_after = 0;
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
        parsed = parsed * 10U + static_cast<std::uint64_t>(ch - '0');
        if (parsed > std::numeric_limits<std::uint32_t>::max()) {
            fail(std::string(name) + " is too large");
        }
    }
    if (parsed == 0) {
        fail(std::string(name) + " must be greater than zero");
    }
    return static_cast<std::uint32_t>(parsed);
}

std::uint64_t checked_cell_count(const Options& options)
{
    const std::uint64_t rows = options.rows;
    const std::uint64_t cols = options.cols;
    if (rows > std::numeric_limits<std::uint64_t>::max() / cols) {
        fail("--rows * --cols overflows cell count");
    }
    return rows * cols;
}

bool is_in_memory_scenario(std::string_view scenario)
{
    return scenario == "point-set" || scenario == "batch-set"
        || scenario == "a1-range-clear" || scenario == "a1-range-erase";
}

bool is_patch_replace_scenario(std::string_view scenario)
{
    return scenario == "patch-replace";
}

bool is_patch_upsert_scenario(std::string_view scenario)
{
    return scenario == "patch-upsert";
}

bool is_patch_scenario(std::string_view scenario)
{
    return is_patch_replace_scenario(scenario) || is_patch_upsert_scenario(scenario);
}

std::string_view editor_mode_for_scenario(std::string_view scenario)
{
    if (is_patch_replace_scenario(scenario)) {
        return "existing-workbook-patch-targeted-cell-replace";
    }
    if (is_patch_upsert_scenario(scenario)) {
        return "existing-workbook-patch-targeted-cell-upsert";
    }
    return "existing-workbook-in-memory-sparse";
}

void validate_options(const Options& options)
{
    if (!is_in_memory_scenario(options.scenario) && !is_patch_scenario(options.scenario)) {
        fail("--scenario must be point-set, batch-set, a1-range-clear, "
             "a1-range-erase, patch-replace, or patch-upsert");
    }
    if (options.rows > kExcelRowLimit) {
        fail("--rows exceeds Excel's row limit");
    }
    if (options.cols > kExcelColumnLimit) {
        fail("--cols exceeds Excel's column limit");
    }
    const std::uint64_t cells = checked_cell_count(options);
    if (!is_patch_upsert_scenario(options.scenario) && options.edits > cells) {
        fail("--edits cannot exceed rows * cols; this benchmark uses unique target coordinates");
    }
    if (is_patch_upsert_scenario(options.scenario)) {
        const std::uint64_t existing_edits = options.edits / 2U;
        const std::uint64_t inserted_edits =
            static_cast<std::uint64_t>(options.edits) - existing_edits;
        if (existing_edits > cells) {
            fail("patch-upsert existing replacement half cannot exceed rows * cols");
        }
        if (inserted_edits > kExcelRowLimit - options.rows) {
            fail("patch-upsert inserted row count would exceed Excel's row limit");
        }
    }
    if (options.source == options.output) {
        fail("--source and --output must be different files");
    }
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
        } else if (arg == "--edits") {
            options.edits = parse_u32(next_value(), "--edits");
        } else if (arg == "--scenario") {
            options.scenario = std::string(next_value());
        } else if (arg == "--source") {
            options.source = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--output") {
            options.output = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--result") {
            options.result = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: fastxlsx_bench_workbook_editor "
                << "--scenario point-set|batch-set|a1-range-clear|a1-range-erase|"
                   "patch-replace|patch-upsert "
                << "--rows N --cols N --edits N "
                << "--source source.xlsx --output edited.xlsx --result result.json\n"
                << "The tool generates a stored source workbook, opens it through "
                << "WorkbookEditor, then either materializes the Data sheet for "
                << "in-memory scenarios or uses targeted Patch replace/upsert for "
                << "patch-* scenarios, and saves a new workbook.\n";
            std::exit(0);
        } else {
            fail(std::string("unknown argument: ") + std::string(arg));
        }
    }
    validate_options(options);
    return options;
}

std::uint64_t milliseconds_since(std::chrono::steady_clock::time_point start)
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
}

void ensure_parent_directory(const std::filesystem::path& path)
{
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
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

double make_source_number(std::uint32_t row, std::uint32_t col)
{
    return static_cast<double>(row) * 1000.0 + static_cast<double>(col);
}

double make_edit_number(std::uint32_t index)
{
    return 900000000.0 + static_cast<double>(index);
}

fastxlsx::WorksheetCellReference coordinate_for_index(
    std::uint32_t index, std::uint32_t rows, std::uint32_t cols)
{
    const std::uint64_t zero_based = index;
    return fastxlsx::WorksheetCellReference {
        static_cast<std::uint32_t>((zero_based / cols) % rows) + 1U,
        static_cast<std::uint32_t>(zero_based % cols) + 1U,
    };
}

std::string column_name(std::uint32_t column)
{
    std::string name;
    while (column > 0) {
        const std::uint32_t remainder = (column - 1U) % 26U;
        name.push_back(static_cast<char>('A' + remainder));
        column = (column - 1U) / 26U;
    }
    std::reverse(name.begin(), name.end());
    return name;
}

std::string cell_reference(std::uint32_t row, std::uint32_t column)
{
    return column_name(column) + std::to_string(row);
}

std::string a1_range_for_touched_coordinates(const Options& options)
{
    const std::uint32_t touched_rows =
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(options.edits) + options.cols - 1U)
            / options.cols);
    return "A1:" + cell_reference(touched_rows, options.cols);
}

std::uint64_t a1_touched_coordinate_count(const Options& options)
{
    const std::uint64_t touched_rows =
        (static_cast<std::uint64_t>(options.edits) + options.cols - 1U) / options.cols;
    return touched_rows * options.cols;
}

void write_source_workbook(const Options& options)
{
    ensure_parent_directory(options.source);
    fastxlsx::WorkbookWriterOptions writer_options;
    writer_options.zip_compression_level = fastxlsx::min_zip_compression_level;

    auto workbook = fastxlsx::WorkbookWriter::create(options.source, writer_options);
    auto sheet = workbook.add_worksheet("Data");
    std::vector<fastxlsx::CellView> cells;
    cells.reserve(options.cols);

    for (std::uint32_t row = 1; row <= options.rows; ++row) {
        cells.clear();
        for (std::uint32_t col = 1; col <= options.cols; ++col) {
            cells.push_back(fastxlsx::CellView::number(make_source_number(row, col)));
        }
        sheet.append_row(cells);
    }
    workbook.close();
}

void run_point_set(
    fastxlsx::WorksheetEditor& sheet, const Options& options, std::uint64_t& touched)
{
    for (std::uint32_t index = 0; index < options.edits; ++index) {
        const fastxlsx::WorksheetCellReference ref =
            coordinate_for_index(index, options.rows, options.cols);
        sheet.set_cell_value(ref.row, ref.column, fastxlsx::CellValue::number(make_edit_number(index)));
        ++touched;
    }
}

void run_batch_set(
    fastxlsx::WorksheetEditor& sheet, const Options& options, std::uint64_t& touched)
{
    std::vector<fastxlsx::WorksheetCellUpdate> updates;
    updates.reserve(options.edits);
    for (std::uint32_t index = 0; index < options.edits; ++index) {
        const fastxlsx::WorksheetCellReference ref =
            coordinate_for_index(index, options.rows, options.cols);
        updates.push_back(fastxlsx::WorksheetCellUpdate {
            ref, fastxlsx::CellValue::number(make_edit_number(index))});
    }
    sheet.set_cell_values(updates);
    touched = updates.size();
}

void run_a1_range_clear(
    fastxlsx::WorksheetEditor& sheet, const Options& options, std::uint64_t& touched)
{
    sheet.clear_cell_values(a1_range_for_touched_coordinates(options));
    touched = a1_touched_coordinate_count(options);
}

void run_a1_range_erase(
    fastxlsx::WorksheetEditor& sheet, const Options& options, std::uint64_t& touched)
{
    sheet.erase_cells(a1_range_for_touched_coordinates(options));
    touched = a1_touched_coordinate_count(options);
}

std::vector<fastxlsx::WorksheetCellUpdate> make_existing_cell_updates(
    const Options& options, std::uint32_t count)
{
    std::vector<fastxlsx::WorksheetCellUpdate> updates;
    updates.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const fastxlsx::WorksheetCellReference ref =
            coordinate_for_index(index, options.rows, options.cols);
        updates.push_back(fastxlsx::WorksheetCellUpdate {
            ref, fastxlsx::CellValue::number(make_edit_number(index))});
    }
    return updates;
}

void run_patch_replace(
    fastxlsx::WorkbookEditor& editor, const Options& options, std::uint64_t& touched)
{
    std::vector<fastxlsx::WorksheetCellUpdate> updates =
        make_existing_cell_updates(options, options.edits);
    editor.replace_cells("Data", updates);
    touched = updates.size();
}

void run_patch_upsert(fastxlsx::WorkbookEditor& editor,
    const Options& options,
    std::uint64_t& touched,
    std::uint64_t& inserted)
{
    const std::uint32_t existing_count = options.edits / 2U;
    const std::uint32_t inserted_count = options.edits - existing_count;
    std::vector<fastxlsx::WorksheetCellUpdate> updates =
        make_existing_cell_updates(options, existing_count);
    updates.reserve(options.edits);
    for (std::uint32_t index = 0; index < inserted_count; ++index) {
        updates.push_back(fastxlsx::WorksheetCellUpdate {
            fastxlsx::WorksheetCellReference {options.rows + index + 1U, 1U},
            fastxlsx::CellValue::number(make_edit_number(existing_count + index)),
        });
    }
    editor.replace_cells("Data", updates, fastxlsx::CellPatchMissingCellPolicy::Insert);
    touched = updates.size();
    inserted = inserted_count;
}

void write_result_json(const Options& options, const RunStats& stats)
{
    ensure_parent_directory(options.result);
    std::ofstream out(options.result, std::ios::binary);
    if (!out) {
        fail("failed to open benchmark result file");
    }

    out << "{\n";
    out << "  \"workbook_editor_benchmark_schema_version\": \""
        << kEditorBenchmarkSchemaVersion << "\",\n";
    out << "  \"scenario\": \"" << json_escape(options.scenario) << "\",\n";
    out << "  \"rows\": " << options.rows << ",\n";
    out << "  \"cols\": " << options.cols << ",\n";
    out << "  \"source_cells\": " << stats.source_cells << ",\n";
    out << "  \"requested_edits\": " << options.edits << ",\n";
    out << "  \"touched_coordinates\": " << stats.touched_coordinates << ",\n";
    out << "  \"inserted_coordinates\": " << stats.inserted_coordinates << ",\n";
    out << "  \"source_write_ms\": " << stats.timings.source_write_ms << ",\n";
    out << "  \"open_ms\": " << stats.timings.open_ms << ",\n";
    out << "  \"materialize_ms\": " << stats.timings.materialize_ms << ",\n";
    out << "  \"mutation_ms\": " << stats.timings.mutation_ms << ",\n";
    out << "  \"save_ms\": " << stats.timings.save_ms << ",\n";
    out << "  \"total_editor_ms\": " << stats.timings.total_editor_ms << ",\n";
    out << "  \"total_ms\": " << stats.timings.total_ms << ",\n";
    out << "  \"materialized_worksheet\": "
        << (stats.materialized_worksheet ? "true" : "false") << ",\n";
    out << "  \"materialized_cells_before\": " << stats.materialized_cells_before << ",\n";
    out << "  \"materialized_cells_after\": " << stats.materialized_cells_after << ",\n";
    out << "  \"estimated_memory_before_bytes\": " << stats.estimated_memory_before << ",\n";
    out << "  \"estimated_memory_after_bytes\": " << stats.estimated_memory_after << ",\n";
    out << "  \"peak_memory_mb\": " << (stats.peak_memory_bytes / (1024.0 * 1024.0)) << ",\n";
    out << "  \"source_bytes\": " << stats.source_bytes << ",\n";
    out << "  \"output_bytes\": " << stats.output_bytes << ",\n";
    out << "  \"source_package_mode\": \"stored-generated-source\",\n";
    out << "  \"editor_mode\": \""
        << editor_mode_for_scenario(options.scenario) << "\",\n";
    out << "  \"office_open\": \"not_run\",\n";
    out << "  \"source\": \"" << json_escape(options.source.generic_string()) << "\",\n";
    out << "  \"output\": \"" << json_escape(options.output.generic_string()) << "\"\n";
    out << "}\n";
}

RunStats run_benchmark(const Options& options)
{
    RunStats stats;
    stats.source_cells = checked_cell_count(options);
    const auto total_started = std::chrono::steady_clock::now();

    auto phase_started = std::chrono::steady_clock::now();
    write_source_workbook(options);
    stats.timings.source_write_ms = milliseconds_since(phase_started);
    stats.source_bytes = static_cast<std::uint64_t>(std::filesystem::file_size(options.source));

    phase_started = std::chrono::steady_clock::now();
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(options.source);
    stats.timings.open_ms = milliseconds_since(phase_started);

    phase_started = std::chrono::steady_clock::now();
    if (is_patch_scenario(options.scenario)) {
        stats.timings.materialize_ms = 0;
        phase_started = std::chrono::steady_clock::now();
        if (is_patch_replace_scenario(options.scenario)) {
            run_patch_replace(editor, options, stats.touched_coordinates);
        } else {
            run_patch_upsert(
                editor, options, stats.touched_coordinates, stats.inserted_coordinates);
        }
    } else {
        fastxlsx::WorksheetEditorOptions editor_options;
        editor_options.max_cells = static_cast<std::size_t>(stats.source_cells);

        fastxlsx::WorksheetEditor sheet = editor.worksheet("Data", editor_options);
        stats.materialized_worksheet = true;
        stats.timings.materialize_ms = milliseconds_since(phase_started);
        stats.materialized_cells_before = sheet.cell_count();
        stats.estimated_memory_before = sheet.estimated_memory_usage();

        phase_started = std::chrono::steady_clock::now();
        if (options.scenario == "point-set") {
            run_point_set(sheet, options, stats.touched_coordinates);
        } else if (options.scenario == "batch-set") {
            run_batch_set(sheet, options, stats.touched_coordinates);
        } else if (options.scenario == "a1-range-clear") {
            run_a1_range_clear(sheet, options, stats.touched_coordinates);
        } else if (options.scenario == "a1-range-erase") {
            run_a1_range_erase(sheet, options, stats.touched_coordinates);
        }
        stats.materialized_cells_after = sheet.cell_count();
        stats.estimated_memory_after = sheet.estimated_memory_usage();
    }
    stats.timings.mutation_ms = milliseconds_since(phase_started);

    ensure_parent_directory(options.output);
    phase_started = std::chrono::steady_clock::now();
    editor.save_as(options.output);
    stats.timings.save_ms = milliseconds_since(phase_started);
    stats.output_bytes = static_cast<std::uint64_t>(std::filesystem::file_size(options.output));
    stats.timings.total_ms = milliseconds_since(total_started);
    stats.timings.total_editor_ms =
        stats.timings.open_ms + stats.timings.materialize_ms + stats.timings.mutation_ms +
        stats.timings.save_ms;
    stats.peak_memory_bytes = peak_memory_bytes();

    write_result_json(options, stats);
    return stats;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const Options options = parse_args(argc, argv);
        const RunStats stats = run_benchmark(options);
        std::cout << "Wrote " << options.source.string() << '\n';
        std::cout << "Wrote " << options.output.string() << '\n';
        std::cout << "Wrote " << options.result.string() << '\n';
        std::cout << "Editor total: " << stats.timings.total_editor_ms << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "workbook editor benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
