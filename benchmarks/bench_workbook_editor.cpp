#include "../src/workbook_editor_package_diagnostics.hpp"

#include <fastxlsx/document_properties.hpp>
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
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#ifdef DocumentProperties
#undef DocumentProperties
#endif
#endif

namespace {

constexpr std::uint32_t kExcelRowLimit = 1048576;
constexpr std::uint32_t kExcelColumnLimit = 16384;
constexpr std::string_view kEditorBenchmarkSchemaVersion = "6";

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
    std::string source_pattern = "numeric";
    int source_compression_level = fastxlsx::min_zip_compression_level;
    int output_compression_level = 0;
    bool source_external_hyperlink = false;
    bool reuse_source = false;
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
    std::size_t output_plan_entry_count = 0;
    std::size_t copied_entry_count = 0;
    std::size_t rewritten_entry_count = 0;
    std::size_t omitted_entry_count = 0;
    std::size_t raw_compressed_copy_entry_count = 0;
    std::uint64_t raw_compressed_copy_bytes = 0;
    std::vector<std::string> copied_entry_names;
    std::vector<std::string> raw_compressed_copy_entry_names;
    std::vector<std::string> rewritten_entry_names;
    std::vector<std::string> omitted_entry_names;
    bool single_pass_worksheet_transform = false;
    std::uint64_t single_pass_scanned_source_cell_count = 0;
    std::uint64_t single_pass_matched_replacement_count = 0;
    std::uint64_t single_pass_inserted_cell_count = 0;
    std::uint64_t single_pass_staged_output_bytes = 0;
    std::uint64_t single_pass_transform_ms = 0;
    std::uint64_t single_pass_transform_us = 0;
    std::uint64_t single_pass_source_scan_action_us = 0;
    std::uint64_t single_pass_source_parsed_event_count = 0;
    std::uint64_t single_pass_source_callback_event_count = 0;
    std::uint64_t single_pass_source_coalesced_input_event_count = 0;
    std::uint64_t single_pass_source_coalesced_output_event_count = 0;
    std::uint64_t single_pass_transform_action_callback_count = 0;
    std::uint64_t single_pass_output_append_call_count = 0;
    std::uint64_t single_pass_output_flush_count = 0;
    std::uint64_t single_pass_output_peak_buffer_bytes = 0;
    std::uint64_t single_pass_relationship_scan_us = 0;
    std::uint64_t single_pass_relationship_scan_input_call_count = 0;
    std::uint64_t single_pass_relationship_scan_input_bytes = 0;
    std::uint64_t single_pass_relationship_scan_boundary_carry_count = 0;
    std::uint64_t single_pass_relationship_scan_slow_path_tag_count = 0;
    std::uint64_t single_pass_temporary_write_us = 0;
    std::uint64_t single_pass_commit_ms = 0;
    std::uint64_t package_writer_total_us = 0;
    std::uint64_t package_writer_open_us = 0;
    std::uint64_t package_writer_close_us = 0;
    bool target_worksheet_entry_telemetry = false;
    bool target_worksheet_entry_raw_compressed_copy = false;
    std::uint64_t target_worksheet_entry_uncompressed_bytes = 0;
    std::uint64_t target_worksheet_entry_input_bytes = 0;
    std::uint64_t target_worksheet_entry_input_read_calls = 0;
    std::uint64_t target_worksheet_entry_writer_write_calls = 0;
    std::uint64_t target_worksheet_entry_total_us = 0;
    std::uint64_t target_worksheet_entry_open_us = 0;
    std::uint64_t target_worksheet_entry_input_read_us = 0;
    std::uint64_t target_worksheet_entry_writer_write_us = 0;
    std::uint64_t target_worksheet_entry_close_us = 0;
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

std::uint32_t parse_nonnegative_u32(std::string_view value, std::string_view name)
{
    std::uint64_t parsed = 0;
    if (value.empty()) {
        fail(std::string(name) + " cannot be empty");
    }
    for (const char ch : value) {
        if (ch < '0' || ch > '9') {
            fail(std::string(name) + " must be a non-negative integer");
        }
        parsed = parsed * 10U + static_cast<std::uint64_t>(ch - '0');
        if (parsed > std::numeric_limits<std::uint32_t>::max()) {
            fail(std::string(name) + " is too large");
        }
    }
    return static_cast<std::uint32_t>(parsed);
}

int parse_source_compression_level(std::string_view value)
{
    const std::uint32_t parsed = parse_nonnegative_u32(value, "--source-compression-level");
    if (parsed > static_cast<std::uint32_t>(fastxlsx::max_zip_compression_level)) {
        fail("--source-compression-level must be between 0 and 9");
    }
    return static_cast<int>(parsed);
}

int parse_output_compression_level(std::string_view value)
{
    if (value == "-1") {
        return -1;
    }
    const std::uint32_t parsed = parse_nonnegative_u32(value, "--output-compression-level");
    if (parsed > 9U) {
        fail("--output-compression-level must be -1 or between 0 and 9");
    }
    return static_cast<int>(parsed);
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
    return scenario == "noop-copy" || scenario == "document-properties"
        || is_patch_replace_scenario(scenario) || is_patch_upsert_scenario(scenario);
}

std::string_view editor_mode_for_scenario(std::string_view scenario)
{
    if (is_patch_replace_scenario(scenario)) {
        return "existing-workbook-patch-targeted-cell-replace";
    }
    if (is_patch_upsert_scenario(scenario)) {
        return "existing-workbook-patch-targeted-cell-upsert";
    }
    if (scenario == "document-properties") {
        return "existing-workbook-patch-document-properties";
    }
    if (scenario == "noop-copy") {
        return "existing-workbook-patch-copy-original";
    }
    return "existing-workbook-in-memory-sparse";
}

void validate_options(const Options& options)
{
    if (!is_in_memory_scenario(options.scenario) && !is_patch_scenario(options.scenario)) {
        fail("--scenario must be point-set, batch-set, a1-range-clear, "
             "a1-range-erase, noop-copy, document-properties, patch-replace, or patch-upsert");
    }
    if (options.rows > kExcelRowLimit) {
        fail("--rows exceeds Excel's row limit");
    }
    if (options.cols > kExcelColumnLimit) {
        fail("--cols exceeds Excel's column limit");
    }
    if (options.source_pattern != "numeric"
        && options.source_pattern != "mixed-inline"
        && options.source_pattern != "mixed-shared"
        && options.source_pattern != "formula") {
        fail("--source-pattern must be numeric, mixed-inline, mixed-shared, or formula");
    }
    const std::uint64_t cells = checked_cell_count(options);
    if (options.scenario == "noop-copy" && options.edits != 0) {
        fail("noop-copy requires --edits 0");
    }
    if (options.scenario != "noop-copy" && options.edits == 0) {
        fail("non-noop scenarios require --edits greater than zero");
    }
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
            options.edits = parse_nonnegative_u32(next_value(), "--edits");
        } else if (arg == "--scenario") {
            options.scenario = std::string(next_value());
        } else if (arg == "--source-pattern") {
            options.source_pattern = std::string(next_value());
        } else if (arg == "--source-external-hyperlink") {
            options.source_external_hyperlink = true;
        } else if (arg == "--source") {
            options.source = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--output") {
            options.output = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--result") {
            options.result = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--source-compression-level") {
            options.source_compression_level = parse_source_compression_level(next_value());
        } else if (arg == "--output-compression-level") {
            options.output_compression_level = parse_output_compression_level(next_value());
        } else if (arg == "--reuse-source") {
            options.reuse_source = true;
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: fastxlsx_bench_workbook_editor "
                << "--scenario point-set|batch-set|a1-range-clear|a1-range-erase|"
                   "noop-copy|document-properties|patch-replace|patch-upsert "
                << "--rows N --cols N --edits N "
                << "[--source-pattern numeric|mixed-inline|mixed-shared|formula] "
                   "[--source-external-hyperlink] "
                << "--source source.xlsx --output edited.xlsx --result result.json "
                << "[--source-compression-level 0..9] "
                   "[--output-compression-level -1..9] [--reuse-source]\n"
                << "The tool generates or reuses a source workbook, opens it through "
                << "WorkbookEditor, then either materializes the Data sheet for "
                << "in-memory scenarios or uses copy-original, document-properties, "
                << "or targeted Patch replace/upsert, and saves a new workbook.\n";
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

std::uint64_t existing_file_size(const std::filesystem::path& path, std::string_view purpose)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        fail("failed to measure " + std::string(purpose) + ": " + path.string());
    }
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::uint64_t>::max())) {
        fail(std::string(purpose) + " is too large: " + path.string());
    }
    return static_cast<std::uint64_t>(size);
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

void write_json_string_array(std::ostream& out,
    std::string_view name,
    const std::vector<std::string>& values,
    bool trailing_comma)
{
    out << "  \"" << name << "\": [";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        out << "\"" << json_escape(values[index]) << "\"";
    }
    out << "]" << (trailing_comma ? "," : "") << "\n";
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
    writer_options.zip_compression_level = options.source_compression_level;
    if (options.source_pattern == "mixed-shared") {
        writer_options.string_strategy = fastxlsx::StringStrategy::SharedString;
    }

    auto workbook = fastxlsx::WorkbookWriter::create(options.source, writer_options);
    auto sheet = workbook.add_worksheet("Data");
    std::vector<fastxlsx::CellView> cells;
    cells.reserve(options.cols);
    std::vector<std::string> text_storage;
    text_storage.reserve(options.cols);

    for (std::uint32_t row = 1; row <= options.rows; ++row) {
        cells.clear();
        text_storage.clear();
        for (std::uint32_t col = 1; col <= options.cols; ++col) {
            if ((options.source_pattern == "mixed-inline"
                    || options.source_pattern == "mixed-shared")
                && (row + col) % 3U == 0) {
                text_storage.push_back("group-" + std::to_string((row + col) % 32U));
                cells.push_back(fastxlsx::CellView::text(text_storage.back()));
            } else if (options.source_pattern == "formula" && col % 4U == 0) {
                text_storage.push_back(cell_reference(row, col - 1U) + "*2");
                cells.push_back(fastxlsx::CellView::formula(text_storage.back()));
            } else {
                cells.push_back(fastxlsx::CellView::number(make_source_number(row, col)));
            }
        }
        sheet.append_row(cells);
    }
    if (options.source_external_hyperlink) {
        sheet.add_external_hyperlink(
            1, 1, "https://example.com/fastxlsx-benchmark");
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

void run_document_properties(fastxlsx::WorkbookEditor& editor)
{
    fastxlsx::DocumentProperties properties;
    properties.creator = "FastXLSX Benchmark";
    properties.last_modified_by = "FastXLSX Benchmark";
    properties.title = "FastXLSX Patch Benchmark";
    properties.subject = "Document properties rewrite";
    properties.application = "FastXLSX Benchmark";
    properties.app_version = "1.0";
    editor.set_document_properties(std::move(properties));
}

fastxlsx::detail::PackageWriterOptions package_writer_options_for_benchmark(
    int compression_level)
{
    fastxlsx::detail::PackageWriterOptions options;
    options.compression_level = compression_level;
    if (compression_level == fastxlsx::min_zip_compression_level) {
        options.backend = fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap;
    } else if (compression_level == fastxlsx::default_zip_compression_level) {
        options.backend = fastxlsx::detail::PackageWriterBackend::Auto;
    } else {
        options.backend = fastxlsx::detail::PackageWriterBackend::MinizipNg;
    }
    return options;
}

void observe_output_plan(
    const fastxlsx::WorkbookEditor& editor, int compression_level, RunStats& stats)
{
    const fastxlsx::detail::PackageEditorOutputPlan plan =
        fastxlsx::detail::WorkbookEditorPackagePlanAccessor::planned_output(
            editor, package_writer_options_for_benchmark(compression_level));
    stats.output_plan_entry_count = plan.entries.size();
    for (const fastxlsx::detail::PackageEditorOutputEntryPlan& entry : plan.entries) {
        if (entry.single_pass_worksheet_transform) {
            stats.single_pass_worksheet_transform = true;
            stats.single_pass_scanned_source_cell_count =
                entry.single_pass_scanned_source_cell_count;
            stats.single_pass_matched_replacement_count =
                entry.single_pass_matched_replacement_count;
            stats.single_pass_inserted_cell_count =
                entry.single_pass_inserted_cell_count;
            stats.single_pass_staged_output_bytes =
                entry.single_pass_staged_output_bytes;
            stats.single_pass_transform_ms = entry.single_pass_transform_ms;
            stats.single_pass_transform_us = entry.single_pass_transform_us;
            stats.single_pass_source_parsed_event_count =
                entry.single_pass_source_parsed_event_count;
            stats.single_pass_source_callback_event_count =
                entry.single_pass_source_callback_event_count;
            stats.single_pass_source_coalesced_input_event_count =
                entry.single_pass_source_coalesced_input_event_count;
            stats.single_pass_source_coalesced_output_event_count =
                entry.single_pass_source_coalesced_output_event_count;
            stats.single_pass_transform_action_callback_count =
                entry.single_pass_transform_action_callback_count;
            stats.single_pass_output_append_call_count =
                entry.single_pass_output_append_call_count;
            stats.single_pass_output_flush_count =
                entry.single_pass_output_flush_count;
            stats.single_pass_output_peak_buffer_bytes =
                entry.single_pass_output_peak_buffer_bytes;
            stats.single_pass_relationship_scan_us =
                entry.single_pass_relationship_scan_us;
            stats.single_pass_relationship_scan_input_call_count =
                entry.single_pass_relationship_scan_input_call_count;
            stats.single_pass_relationship_scan_input_bytes =
                entry.single_pass_relationship_scan_input_bytes;
            stats.single_pass_relationship_scan_boundary_carry_count =
                entry.single_pass_relationship_scan_boundary_carry_count;
            stats.single_pass_relationship_scan_slow_path_tag_count =
                entry.single_pass_relationship_scan_slow_path_tag_count;
            stats.single_pass_temporary_write_us =
                entry.single_pass_temporary_write_us;
            const std::uint64_t measured_sink_us =
                stats.single_pass_relationship_scan_us
                + stats.single_pass_temporary_write_us;
            stats.single_pass_source_scan_action_us =
                stats.single_pass_transform_us > measured_sink_us
                ? stats.single_pass_transform_us - measured_sink_us
                : 0;
            stats.single_pass_commit_ms = entry.single_pass_commit_ms;
        }
        if (entry.omitted) {
            stats.omitted_entry_names.push_back(entry.entry_name);
            continue;
        }
        if (entry.copied_from_source) {
            stats.copied_entry_names.push_back(entry.entry_name);
            if (entry.raw_compressed_source_copy) {
                stats.raw_compressed_copy_entry_names.push_back(entry.entry_name);
                stats.raw_compressed_copy_bytes += entry.raw_compressed_source_bytes;
            }
        } else {
            stats.rewritten_entry_names.push_back(entry.entry_name);
        }
    }
    std::sort(stats.copied_entry_names.begin(), stats.copied_entry_names.end());
    std::sort(stats.raw_compressed_copy_entry_names.begin(),
        stats.raw_compressed_copy_entry_names.end());
    std::sort(stats.rewritten_entry_names.begin(), stats.rewritten_entry_names.end());
    std::sort(stats.omitted_entry_names.begin(), stats.omitted_entry_names.end());
    stats.copied_entry_count = stats.copied_entry_names.size();
    stats.raw_compressed_copy_entry_count =
        stats.raw_compressed_copy_entry_names.size();
    stats.rewritten_entry_count = stats.rewritten_entry_names.size();
    stats.omitted_entry_count = stats.omitted_entry_names.size();
}

void observe_package_writer_telemetry(
    const fastxlsx::detail::PackageWriterTelemetry& telemetry, RunStats& stats)
{
    stats.package_writer_total_us = telemetry.total_us;
    stats.package_writer_open_us = telemetry.open_us;
    stats.package_writer_close_us = telemetry.close_us;
    const auto target = std::find_if(telemetry.entries.begin(), telemetry.entries.end(),
        [](const fastxlsx::detail::PackageWriterEntryTelemetry& entry) {
            return entry.entry_name == "xl/worksheets/sheet1.xml";
        });
    if (target == telemetry.entries.end()) {
        return;
    }

    stats.target_worksheet_entry_telemetry = true;
    stats.target_worksheet_entry_raw_compressed_copy = target->raw_compressed_copy;
    stats.target_worksheet_entry_uncompressed_bytes = target->uncompressed_bytes;
    stats.target_worksheet_entry_input_bytes = target->input_bytes;
    stats.target_worksheet_entry_input_read_calls = target->input_read_calls;
    stats.target_worksheet_entry_writer_write_calls = target->writer_write_calls;
    stats.target_worksheet_entry_total_us = target->total_us;
    stats.target_worksheet_entry_open_us = target->open_us;
    stats.target_worksheet_entry_input_read_us = target->input_read_us;
    stats.target_worksheet_entry_writer_write_us = target->writer_write_us;
    stats.target_worksheet_entry_close_us = target->close_us;
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
    out << "  \"source_fixture_mode\": \""
        << (options.reuse_source ? "reused-existing-source" : "generated-source")
        << "\",\n";
    out << "  \"source_pattern\": \"" << options.source_pattern << "\",\n";
    out << "  \"source_external_hyperlink\": "
        << (options.source_external_hyperlink ? "true" : "false") << ",\n";
    out << "  \"source_compression_level\": " << options.source_compression_level << ",\n";
    out << "  \"output_compression_level\": " << options.output_compression_level << ",\n";
    out << "  \"output_plan_entry_count\": " << stats.output_plan_entry_count << ",\n";
    out << "  \"copied_entry_count\": " << stats.copied_entry_count << ",\n";
    out << "  \"rewritten_entry_count\": " << stats.rewritten_entry_count << ",\n";
    out << "  \"omitted_entry_count\": " << stats.omitted_entry_count << ",\n";
    out << "  \"raw_compressed_copy_entry_count\": "
        << stats.raw_compressed_copy_entry_count << ",\n";
    out << "  \"raw_compressed_copy_bytes\": "
        << stats.raw_compressed_copy_bytes << ",\n";
    out << "  \"single_pass_worksheet_transform\": "
        << (stats.single_pass_worksheet_transform ? "true" : "false") << ",\n";
    out << "  \"single_pass_scanned_source_cell_count\": "
        << stats.single_pass_scanned_source_cell_count << ",\n";
    out << "  \"single_pass_matched_replacement_count\": "
        << stats.single_pass_matched_replacement_count << ",\n";
    out << "  \"single_pass_inserted_cell_count\": "
        << stats.single_pass_inserted_cell_count << ",\n";
    out << "  \"single_pass_staged_output_bytes\": "
        << stats.single_pass_staged_output_bytes << ",\n";
    out << "  \"single_pass_transform_ms\": "
        << stats.single_pass_transform_ms << ",\n";
    out << "  \"single_pass_transform_us\": "
        << stats.single_pass_transform_us << ",\n";
    out << "  \"single_pass_source_scan_action_us\": "
        << stats.single_pass_source_scan_action_us << ",\n";
    out << "  \"single_pass_source_parsed_event_count\": "
        << stats.single_pass_source_parsed_event_count << ",\n";
    out << "  \"single_pass_source_callback_event_count\": "
        << stats.single_pass_source_callback_event_count << ",\n";
    out << "  \"single_pass_source_coalesced_input_event_count\": "
        << stats.single_pass_source_coalesced_input_event_count << ",\n";
    out << "  \"single_pass_source_coalesced_output_event_count\": "
        << stats.single_pass_source_coalesced_output_event_count << ",\n";
    out << "  \"single_pass_transform_action_callback_count\": "
        << stats.single_pass_transform_action_callback_count << ",\n";
    out << "  \"single_pass_output_append_call_count\": "
        << stats.single_pass_output_append_call_count << ",\n";
    out << "  \"single_pass_output_flush_count\": "
        << stats.single_pass_output_flush_count << ",\n";
    out << "  \"single_pass_output_peak_buffer_bytes\": "
        << stats.single_pass_output_peak_buffer_bytes << ",\n";
    out << "  \"single_pass_relationship_scan_us\": "
        << stats.single_pass_relationship_scan_us << ",\n";
    out << "  \"single_pass_relationship_scan_input_call_count\": "
        << stats.single_pass_relationship_scan_input_call_count << ",\n";
    out << "  \"single_pass_relationship_scan_input_bytes\": "
        << stats.single_pass_relationship_scan_input_bytes << ",\n";
    out << "  \"single_pass_relationship_scan_boundary_carry_count\": "
        << stats.single_pass_relationship_scan_boundary_carry_count << ",\n";
    out << "  \"single_pass_relationship_scan_slow_path_tag_count\": "
        << stats.single_pass_relationship_scan_slow_path_tag_count << ",\n";
    out << "  \"single_pass_temporary_write_us\": "
        << stats.single_pass_temporary_write_us << ",\n";
    out << "  \"single_pass_commit_ms\": "
        << stats.single_pass_commit_ms << ",\n";
    out << "  \"package_writer_total_us\": " << stats.package_writer_total_us << ",\n";
    out << "  \"package_writer_open_us\": " << stats.package_writer_open_us << ",\n";
    out << "  \"package_writer_close_us\": " << stats.package_writer_close_us << ",\n";
    out << "  \"target_worksheet_entry_telemetry\": "
        << (stats.target_worksheet_entry_telemetry ? "true" : "false") << ",\n";
    out << "  \"target_worksheet_entry_raw_compressed_copy\": "
        << (stats.target_worksheet_entry_raw_compressed_copy ? "true" : "false") << ",\n";
    out << "  \"target_worksheet_entry_uncompressed_bytes\": "
        << stats.target_worksheet_entry_uncompressed_bytes << ",\n";
    out << "  \"target_worksheet_entry_input_bytes\": "
        << stats.target_worksheet_entry_input_bytes << ",\n";
    out << "  \"target_worksheet_entry_input_read_calls\": "
        << stats.target_worksheet_entry_input_read_calls << ",\n";
    out << "  \"target_worksheet_entry_writer_write_calls\": "
        << stats.target_worksheet_entry_writer_write_calls << ",\n";
    out << "  \"target_worksheet_entry_total_us\": "
        << stats.target_worksheet_entry_total_us << ",\n";
    out << "  \"target_worksheet_entry_open_us\": "
        << stats.target_worksheet_entry_open_us << ",\n";
    out << "  \"target_worksheet_entry_input_read_us\": "
        << stats.target_worksheet_entry_input_read_us << ",\n";
    out << "  \"target_worksheet_entry_writer_write_us\": "
        << stats.target_worksheet_entry_writer_write_us << ",\n";
    out << "  \"target_worksheet_entry_close_us\": "
        << stats.target_worksheet_entry_close_us << ",\n";
    write_json_string_array(out, "copied_entry_names", stats.copied_entry_names, true);
    write_json_string_array(out, "raw_compressed_copy_entry_names",
        stats.raw_compressed_copy_entry_names, true);
    write_json_string_array(out, "rewritten_entry_names", stats.rewritten_entry_names, true);
    write_json_string_array(out, "omitted_entry_names", stats.omitted_entry_names, true);
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
    if (options.reuse_source) {
        stats.source_bytes = existing_file_size(options.source, "source package");
    } else {
        write_source_workbook(options);
        stats.timings.source_write_ms = milliseconds_since(phase_started);
        stats.source_bytes = existing_file_size(options.source, "source package");
    }

    phase_started = std::chrono::steady_clock::now();
    fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open(options.source);
    stats.timings.open_ms = milliseconds_since(phase_started);

    phase_started = std::chrono::steady_clock::now();
    if (is_patch_scenario(options.scenario)) {
        stats.timings.materialize_ms = 0;
        phase_started = std::chrono::steady_clock::now();
        if (is_patch_replace_scenario(options.scenario)) {
            run_patch_replace(editor, options, stats.touched_coordinates);
        } else if (is_patch_upsert_scenario(options.scenario)) {
            run_patch_upsert(
                editor, options, stats.touched_coordinates, stats.inserted_coordinates);
        } else if (options.scenario == "document-properties") {
            run_document_properties(editor);
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
    observe_output_plan(editor, options.output_compression_level, stats);

    ensure_parent_directory(options.output);
    phase_started = std::chrono::steady_clock::now();
    fastxlsx::WorkbookEditorSaveOptions save_options;
    save_options.zip_compression_level = options.output_compression_level;
    fastxlsx::detail::PackageWriterTelemetry package_writer_telemetry;
    fastxlsx::detail::WorkbookEditorPackagePlanAccessor::set_package_writer_telemetry(
        editor, &package_writer_telemetry);
    try {
        editor.save_as(options.output, save_options);
    } catch (...) {
        fastxlsx::detail::WorkbookEditorPackagePlanAccessor::set_package_writer_telemetry(
            editor, nullptr);
        throw;
    }
    fastxlsx::detail::WorkbookEditorPackagePlanAccessor::set_package_writer_telemetry(
        editor, nullptr);
    observe_package_writer_telemetry(package_writer_telemetry, stats);
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
