#include "../src/package_editor.hpp"
#include "../src/package_reader.hpp"
#include "../src/package_writer.hpp"

#include <fastxlsx/workbook_editor.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
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
constexpr std::uint64_t kReplacementCellValueBase = 900999000ULL;
constexpr std::string_view kBenchmarkSchemaVersion = "1";

enum class EditorApi {
    PublicWorkbookEditor,
    InternalPackageEditor,
};

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
    std::uint32_t edits = 1000;
    bool verify_output = true;
    EditorApi editor_api = EditorApi::PublicWorkbookEditor;
    std::filesystem::path source =
        default_output_dir() / "fastxlsx-package-editor-cell-replacement-source.xlsx";
    std::filesystem::path source_body =
        default_output_dir() / "fastxlsx-package-editor-cell-replacement-source-body.xml";
    std::filesystem::path output =
        default_output_dir() / "fastxlsx-package-editor-cell-replacement-output.xlsx";
    std::filesystem::path result =
        default_output_dir() / "fastxlsx-package-editor-cell-replacement-bench.json";
};

struct Timings {
    std::uint64_t source_body_write_ms = 0;
    std::uint64_t source_package_write_ms = 0;
    std::uint64_t open_ms = 0;
    std::uint64_t patch_plan_ms = 0;
    std::uint64_t save_ms = 0;
    std::uint64_t verify_ms = 0;
    std::uint64_t total_edit_ms = 0;
    std::uint64_t total_ms = 0;
};

struct RunStats {
    Timings timings;
    std::uint64_t source_cells = 0;
    std::uint64_t source_body_bytes = 0;
    std::uint64_t source_package_bytes = 0;
    std::uint64_t output_package_bytes = 0;
    std::uint64_t peak_memory_bytes = 0;
    std::size_t output_plan_note_count = 0;
    bool output_verified = false;
    bool output_contains_first_replacement = false;
    bool output_contains_tail_cell = false;
    bool plan_reports_source_entry_chunk_source = false;
    bool plan_reports_file_backed_stream_rewrite = false;
    bool output_plan_staged_replacement_chunks = false;
    bool output_plan_materialized_replacement = false;
    bool public_facade_reports_targeted_cells = false;
    std::uint64_t public_facade_targeted_cell_count = 0;
    std::uint64_t public_facade_replacement_xml_bytes = 0;
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

EditorApi parse_editor_api(std::string_view value)
{
    if (value == "public-workbook-editor" || value == "public") {
        return EditorApi::PublicWorkbookEditor;
    }
    if (value == "internal-package-editor" || value == "internal") {
        return EditorApi::InternalPackageEditor;
    }
    fail("--editor-api must be public-workbook-editor or internal-package-editor");
}

std::string_view editor_api_name(EditorApi api)
{
    switch (api) {
    case EditorApi::PublicWorkbookEditor:
        return "public-workbook-editor";
    case EditorApi::InternalPackageEditor:
        return "internal-package-editor";
    }
    return "unknown";
}

void validate_options(const Options& options)
{
    if (options.rows > kExcelRowLimit) {
        fail("--rows exceeds Excel's row limit");
    }
    if (options.cols > kExcelColumnLimit) {
        fail("--cols exceeds Excel's column limit");
    }
    if (options.edits > checked_cell_count(options)) {
        fail("--edits cannot exceed rows * cols; targets must exist in the source worksheet");
    }
    if (options.source == options.output) {
        fail("--source and --output must be different files");
    }
    if (options.source_body == options.source || options.source_body == options.output) {
        fail("--source-body must be different from --source and --output");
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
        } else if (arg == "--source") {
            options.source = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--source-body") {
            options.source_body = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--output") {
            options.output = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--result") {
            options.result = std::filesystem::path(std::string(next_value()));
        } else if (arg == "--editor-api") {
            options.editor_api = parse_editor_api(next_value());
        } else if (arg == "--no-verify-output") {
            options.verify_output = false;
        } else if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: fastxlsx_bench_package_editor_cell_replacement "
                << "--rows N --cols N --edits N "
                << "--source source.xlsx --source-body body.xml "
                << "--output edited.xlsx --result result.json "
                << "[--editor-api public-workbook-editor|internal-package-editor] "
                << "[--no-verify-output]\n"
                << "The tool builds a stored source package with a file-backed "
                << "worksheet entry, patches existing cells through the public "
                << "WorkbookEditor::replace_cells() facade by default or the "
                << "internal PackageEditor transformer when requested, and "
                << "writes a schema-v1 JSON report.\n";
            std::exit(0);
        } else {
            fail(std::string("unknown argument: ") + std::string(arg));
        }
    }
    validate_options(options);
    return options;
}

void ensure_parent_directory(const std::filesystem::path& path)
{
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

std::uint64_t milliseconds_since(std::chrono::steady_clock::time_point start)
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
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

std::string source_cell_value(std::uint32_t row, std::uint32_t column)
{
    return std::to_string(static_cast<std::uint64_t>(row) * 1000000ULL + column);
}

std::string replacement_cell_value(std::uint32_t index)
{
    return std::to_string(kReplacementCellValueBase + index);
}

std::string dimension_reference(const Options& options)
{
    return "A1:" + cell_reference(options.rows, options.cols);
}

std::string worksheet_prefix(const Options& options)
{
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<dimension ref=\"" + dimension_reference(options) + "\"/>"
        "<sheetData>";
}

std::string worksheet_suffix()
{
    return "</sheetData></worksheet>";
}

std::uint64_t write_source_worksheet_body(const Options& options)
{
    ensure_parent_directory(options.source_body);
    std::ofstream out(options.source_body, std::ios::binary);
    if (!out) {
        fail("failed to open source worksheet body file");
    }

    for (std::uint32_t row = 1; row <= options.rows; ++row) {
        out << "<row r=\"" << row << "\">";
        for (std::uint32_t col = 1; col <= options.cols; ++col) {
            const std::string ref = cell_reference(row, col);
            out << "<c r=\"" << ref << "\"><v>"
                << source_cell_value(row, col) << "</v></c>";
        }
        out << "</row>";
    }

    out.close();
    if (!out) {
        fail("failed to finalize source worksheet body file");
    }
    return static_cast<std::uint64_t>(std::filesystem::file_size(options.source_body));
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
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"xl/workbook.xml\"/>"
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
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
        "Target=\"worksheets/sheet1.xml\"/>"
        "</Relationships>";
}

void write_source_package(const Options& options)
{
    ensure_parent_directory(options.source);
    fastxlsx::detail::write_package(options.source,
        {
            {"[Content_Types].xml", content_types_xml()},
            {"_rels/.rels", package_relationships_xml()},
            {"xl/workbook.xml", workbook_xml()},
            {"xl/_rels/workbook.xml.rels", workbook_relationships_xml()},
            {"xl/worksheets/sheet1.xml",
                {
                    fastxlsx::detail::PackageEntryChunk::memory(worksheet_prefix(options)),
                    fastxlsx::detail::PackageEntryChunk::file(options.source_body),
                    fastxlsx::detail::PackageEntryChunk::memory(worksheet_suffix()),
                }},
        },
        {fastxlsx::detail::PackageWriterBackend::StoredZipBootstrap});
}

std::uint32_t replacement_row_for_index(std::uint32_t index, const Options& options)
{
    return static_cast<std::uint32_t>(index / options.cols) + 1U;
}

std::uint32_t replacement_col_for_index(std::uint32_t index, const Options& options)
{
    return static_cast<std::uint32_t>(index % options.cols) + 1U;
}

std::string replacement_xml_for_index(std::uint32_t index, const Options& options)
{
    const std::string ref =
        cell_reference(replacement_row_for_index(index, options),
            replacement_col_for_index(index, options));
    return "<c r=\"" + ref + "\"><v>" + replacement_cell_value(index) + "</v></c>";
}

std::vector<fastxlsx::detail::WorksheetCellReplacement> make_replacements(
    const Options& options,
    std::vector<std::string>& cell_references,
    std::vector<std::string>& replacement_xml)
{
    cell_references.clear();
    replacement_xml.clear();
    cell_references.reserve(options.edits);
    replacement_xml.reserve(options.edits);

    for (std::uint32_t index = 0; index < options.edits; ++index) {
        cell_references.push_back(cell_reference(
            replacement_row_for_index(index, options),
            replacement_col_for_index(index, options)));
        replacement_xml.push_back(replacement_xml_for_index(index, options));
    }

    std::vector<fastxlsx::detail::WorksheetCellReplacement> replacements;
    replacements.reserve(options.edits);
    for (std::uint32_t index = 0; index < options.edits; ++index) {
        replacements.push_back(fastxlsx::detail::WorksheetCellReplacement {
            cell_references[index],
            fastxlsx::detail::WorksheetCellReplacementPayload::from_materialized_xml(
                replacement_xml[index]),
        });
    }
    return replacements;
}

std::vector<fastxlsx::WorksheetCellUpdate> make_public_replacements(const Options& options)
{
    std::vector<fastxlsx::WorksheetCellUpdate> replacements;
    replacements.reserve(options.edits);
    for (std::uint32_t index = 0; index < options.edits; ++index) {
        replacements.push_back(fastxlsx::WorksheetCellUpdate {
            {replacement_row_for_index(index, options), replacement_col_for_index(index, options)},
            fastxlsx::CellValue::number(
                static_cast<double>(kReplacementCellValueBase + index)),
        });
    }
    return replacements;
}

bool note_contains_all(std::string_view note, std::initializer_list<std::string_view> needles)
{
    for (std::string_view needle : needles) {
        if (note.find(needle) == std::string_view::npos) {
            return false;
        }
    }
    return true;
}

bool has_note_containing(
    const std::vector<std::string>& notes,
    std::initializer_list<std::string_view> needles)
{
    for (const std::string& note : notes) {
        if (note_contains_all(note, needles)) {
            return true;
        }
    }
    return false;
}

const fastxlsx::detail::PackageEditorOutputEntryPlan* find_entry_plan(
    const fastxlsx::detail::PackageEditorOutputPlan& plan,
    std::string_view entry_name)
{
    const auto item = std::find_if(plan.entries.begin(), plan.entries.end(),
        [entry_name](const fastxlsx::detail::PackageEditorOutputEntryPlan& entry) {
            return entry.entry_name == entry_name;
        });
    return item == plan.entries.end() ? nullptr : &*item;
}

void observe_output_plan(
    const fastxlsx::detail::PackageEditorOutputPlan& plan,
    RunStats& stats)
{
    stats.output_plan_note_count = plan.notes.size();
    stats.plan_reports_source_entry_chunk_source =
        has_note_containing(plan.notes,
            {"PackageReader ZIP-entry chunk source", "without materializing"});
    stats.plan_reports_file_backed_stream_rewrite =
        has_note_containing(plan.notes,
            {"file-backed package-entry chunk"});
    const auto* worksheet_entry = find_entry_plan(plan, "xl/worksheets/sheet1.xml");
    if (worksheet_entry != nullptr) {
        stats.output_plan_staged_replacement_chunks =
            worksheet_entry->staged_replacement_chunks;
        stats.output_plan_materialized_replacement =
            worksheet_entry->materialized_replacement;
    }
}

bool stream_contains_all(fastxlsx::detail::PackageReaderChunkCallback source,
    const std::vector<std::string>& needles,
    std::vector<bool>& found)
{
    found.assign(needles.size(), false);
    std::size_t max_needle_size = 0;
    for (const std::string& needle : needles) {
        max_needle_size = std::max(max_needle_size, needle.size());
    }
    if (max_needle_size == 0) {
        return true;
    }

    std::string carry;
    std::string chunk;
    while (source(chunk)) {
        std::string window;
        window.reserve(carry.size() + chunk.size());
        window += carry;
        window += chunk;

        bool all_found = true;
        for (std::size_t index = 0; index < needles.size(); ++index) {
            if (!found[index] && window.find(needles[index]) != std::string::npos) {
                found[index] = true;
            }
            all_found = all_found && found[index];
        }
        if (all_found) {
            return true;
        }

        const std::size_t carry_size = max_needle_size - 1U;
        if (window.size() <= carry_size) {
            carry = std::move(window);
        } else {
            carry.assign(window.end() - static_cast<std::ptrdiff_t>(carry_size), window.end());
        }
    }

    return std::all_of(found.begin(), found.end(), [](bool value) { return value; });
}

void verify_output(const Options& options, RunStats& stats)
{
    const fastxlsx::detail::PackageReader reader =
        fastxlsx::detail::PackageReader::open(options.output);
    std::vector<std::string> markers;
    markers.push_back(replacement_xml_for_index(0, options));

    bool should_check_tail = options.edits < checked_cell_count(options);
    if (should_check_tail) {
        markers.push_back("<c r=\"" + cell_reference(options.rows, options.cols)
            + "\"><v>" + source_cell_value(options.rows, options.cols) + "</v></c>");
    }

    std::vector<bool> found;
    (void)stream_contains_all(
        reader.entry_chunk_source("xl/worksheets/sheet1.xml"), markers, found);
    stats.output_verified = true;
    stats.output_contains_first_replacement = !found.empty() && found[0];
    stats.output_contains_tail_cell = !should_check_tail || (found.size() > 1 && found[1]);

    if (!stats.output_contains_first_replacement) {
        fail("output worksheet does not contain the first replacement cell");
    }
    if (!stats.output_contains_tail_cell) {
        fail("output worksheet does not contain the expected tail source cell");
    }
}

void write_result_json(const Options& options, const RunStats& stats)
{
    ensure_parent_directory(options.result);
    std::ofstream out(options.result, std::ios::binary);
    if (!out) {
        fail("failed to open benchmark result file");
    }

    out << "{\n";
    out << "  \"package_editor_cell_replacement_benchmark_schema_version\": \""
        << kBenchmarkSchemaVersion << "\",\n";
    out << "  \"scenario\": \"source-entry-cell-replacement\",\n";
    out << "  \"rows\": " << options.rows << ",\n";
    out << "  \"cols\": " << options.cols << ",\n";
    out << "  \"source_cells\": " << stats.source_cells << ",\n";
    out << "  \"replacement_count\": " << options.edits << ",\n";
    out << "  \"editor_api\": \"" << editor_api_name(options.editor_api) << "\",\n";
    out << "  \"source_body_write_ms\": " << stats.timings.source_body_write_ms << ",\n";
    out << "  \"source_package_write_ms\": " << stats.timings.source_package_write_ms << ",\n";
    out << "  \"open_ms\": " << stats.timings.open_ms << ",\n";
    out << "  \"patch_plan_ms\": " << stats.timings.patch_plan_ms << ",\n";
    out << "  \"save_ms\": " << stats.timings.save_ms << ",\n";
    out << "  \"verify_ms\": " << stats.timings.verify_ms << ",\n";
    out << "  \"total_edit_ms\": " << stats.timings.total_edit_ms << ",\n";
    out << "  \"total_ms\": " << stats.timings.total_ms << ",\n";
    out << "  \"peak_memory_mb\": " << (stats.peak_memory_bytes / (1024.0 * 1024.0)) << ",\n";
    out << "  \"source_body_bytes\": " << stats.source_body_bytes << ",\n";
    out << "  \"source_package_bytes\": " << stats.source_package_bytes << ",\n";
    out << "  \"output_package_bytes\": " << stats.output_package_bytes << ",\n";
    out << "  \"output_plan_note_count\": " << stats.output_plan_note_count << ",\n";
    out << "  \"plan_reports_source_entry_chunk_source\": "
        << (stats.plan_reports_source_entry_chunk_source ? "true" : "false") << ",\n";
    out << "  \"plan_reports_file_backed_stream_rewrite\": "
        << (stats.plan_reports_file_backed_stream_rewrite ? "true" : "false") << ",\n";
    out << "  \"output_plan_staged_replacement_chunks\": "
        << (stats.output_plan_staged_replacement_chunks ? "true" : "false") << ",\n";
    out << "  \"output_plan_materialized_replacement\": "
        << (stats.output_plan_materialized_replacement ? "true" : "false") << ",\n";
    out << "  \"public_facade_reports_targeted_cells\": "
        << (stats.public_facade_reports_targeted_cells ? "true" : "false") << ",\n";
    out << "  \"public_facade_targeted_cell_count\": "
        << stats.public_facade_targeted_cell_count << ",\n";
    out << "  \"public_facade_replacement_xml_bytes\": "
        << stats.public_facade_replacement_xml_bytes << ",\n";
    out << "  \"output_verified\": "
        << (stats.output_verified ? "true" : "false") << ",\n";
    out << "  \"output_contains_first_replacement\": "
        << (stats.output_contains_first_replacement ? "true" : "false") << ",\n";
    out << "  \"output_contains_tail_cell\": "
        << (stats.output_contains_tail_cell ? "true" : "false") << ",\n";
    out << "  \"source_package_mode\": \"stored-generated-source-file-backed-worksheet-entry\",\n";
    out << "  \"editor_mode\": \""
        << (options.editor_api == EditorApi::PublicWorkbookEditor
                ? "public-workbook-editor-replace-cells"
                : "internal-package-editor-cell-replacement")
        << "\",\n";
    out << "  \"package_entry_source_mode\": \"source-zip-entry-chunk-source\",\n";
    out << "  \"output_entry_mode\": \"file-backed-stream-rewrite\",\n";
    out << "  \"office_open\": \"not_run\",\n";
    out << "  \"source\": \"" << json_escape(options.source.generic_string()) << "\",\n";
    out << "  \"source_body\": \"" << json_escape(options.source_body.generic_string()) << "\",\n";
    out << "  \"output\": \"" << json_escape(options.output.generic_string()) << "\"\n";
    out << "}\n";
}

RunStats run_benchmark(const Options& options)
{
    RunStats stats;
    stats.source_cells = checked_cell_count(options);
    const auto total_started = std::chrono::steady_clock::now();

    auto phase_started = std::chrono::steady_clock::now();
    stats.source_body_bytes = write_source_worksheet_body(options);
    stats.timings.source_body_write_ms = milliseconds_since(phase_started);

    phase_started = std::chrono::steady_clock::now();
    write_source_package(options);
    stats.timings.source_package_write_ms = milliseconds_since(phase_started);
    stats.source_package_bytes =
        static_cast<std::uint64_t>(std::filesystem::file_size(options.source));

    phase_started = std::chrono::steady_clock::now();
    std::optional<fastxlsx::detail::PackageEditor> internal_editor;
    std::optional<fastxlsx::WorkbookEditor> public_editor;
    if (options.editor_api == EditorApi::PublicWorkbookEditor) {
        public_editor = fastxlsx::WorkbookEditor::open(options.source);
    } else {
        internal_editor = fastxlsx::detail::PackageEditor::open(options.source);
    }
    stats.timings.open_ms = milliseconds_since(phase_started);

    phase_started = std::chrono::steady_clock::now();
    if (options.editor_api == EditorApi::PublicWorkbookEditor) {
        std::vector<fastxlsx::WorksheetCellUpdate> replacements =
            make_public_replacements(options);
        public_editor->replace_cells("Data", replacements);
        stats.public_facade_reports_targeted_cells =
            public_editor->has_pending_targeted_cell_replacement("Data");
        stats.public_facade_targeted_cell_count =
            public_editor->pending_targeted_cell_replacement_count();
        stats.public_facade_replacement_xml_bytes =
            public_editor->estimated_pending_targeted_cell_replacement_xml_bytes();
    } else {
        std::vector<std::string> cell_references;
        std::vector<std::string> replacement_xml;
        std::vector<fastxlsx::detail::WorksheetCellReplacement> replacements =
            make_replacements(options, cell_references, replacement_xml);
        internal_editor->replace_worksheet_cells_by_name("Data", replacements);
    }
    stats.timings.patch_plan_ms = milliseconds_since(phase_started);
    if (internal_editor.has_value()) {
        observe_output_plan(internal_editor->planned_output(), stats);
    }

    ensure_parent_directory(options.output);
    phase_started = std::chrono::steady_clock::now();
    if (public_editor.has_value()) {
        public_editor->save_as(options.output);
    } else {
        internal_editor->save_as(options.output);
    }
    stats.timings.save_ms = milliseconds_since(phase_started);
    stats.output_package_bytes =
        static_cast<std::uint64_t>(std::filesystem::file_size(options.output));

    if (options.verify_output) {
        phase_started = std::chrono::steady_clock::now();
        verify_output(options, stats);
        stats.timings.verify_ms = milliseconds_since(phase_started);
    }

    stats.timings.total_ms = milliseconds_since(total_started);
    stats.timings.total_edit_ms =
        stats.timings.open_ms + stats.timings.patch_plan_ms + stats.timings.save_ms;
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
        std::cout << "Patch edit total: " << stats.timings.total_edit_ms << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "package editor cell replacement benchmark failed: "
                  << error.what() << '\n';
        return 1;
    }
}
