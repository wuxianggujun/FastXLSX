#include <fastxlsx/fastxlsx.hpp>

#include "image_test_bytes.hpp"
#include "zip_test_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fastxlsx::CellValue;
using fastxlsx::CellView;
using fastxlsx::FastXlsxError;
using fastxlsx::StyleId;
using fastxlsx::WorkbookEditor;
using fastxlsx::WorkbookWriter;
using fastxlsx::WorkbookWriterOptions;
using fastxlsx::WorksheetEditor;
using fastxlsx::WorksheetWriter;

struct CliOptions {
    std::string scenario;
    std::filesystem::path source;
    std::filesystem::path output;
    std::filesystem::path report;
    std::filesystem::path replacement_image;
    std::string sheet_name;
    std::string rename_to;
};

struct Report {
    std::string scenario;
    std::string status = "ok";
    std::filesystem::path source;
    std::filesystem::path output;
    std::filesystem::path report_path;
    std::filesystem::path replacement_image;
    std::string source_sheet_name;
    std::string renamed_sheet_name;
    std::string image_part_name;
    std::string error_message;
    std::vector<std::string> mutations;
    std::vector<std::string> notes;
};

[[nodiscard]] std::filesystem::path repository_root()
{
    return std::filesystem::path(FASTXLSX_REPOSITORY_ROOT);
}

[[nodiscard]] std::filesystem::path repository_asset(std::string_view relative_path)
{
    return repository_root() / std::filesystem::path(relative_path);
}

void ensure_parent_directory(const std::filesystem::path& path)
{
    if (path.empty()) {
        return;
    }
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

void write_bytes_to_file(
    const std::filesystem::path& path,
    std::span<const std::byte> bytes)
{
    ensure_parent_directory(path);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("failed to create file: " + path.string());
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!stream) {
        throw std::runtime_error("failed to write file: " + path.string());
    }
}

[[nodiscard]] std::filesystem::path
resolve_generated_source(const CliOptions& options, std::string_view fallback_name)
{
    if (!options.source.empty()) {
        ensure_parent_directory(options.source);
        return options.source;
    }
    return fastxlsx::test::artifact_path(fallback_name);
}

[[nodiscard]] std::filesystem::path
resolve_output_path(const CliOptions& options, std::string_view fallback_name)
{
    if (!options.output.empty()) {
        ensure_parent_directory(options.output);
        return options.output;
    }
    return fastxlsx::test::artifact_path(fallback_name);
}

[[nodiscard]] std::filesystem::path resolve_replacement_png_path(const CliOptions& options)
{
    if (!options.replacement_image.empty()) {
        return options.replacement_image;
    }

    const std::filesystem::path asset = repository_asset("docs/assets/donation/weixin.png");
    if (std::filesystem::exists(asset)) {
        return asset;
    }

    throw std::runtime_error(
        "missing replacement PNG asset; pass --replacement-image explicitly");
}

[[nodiscard]] std::string json_escape(std::string_view value)
{
    std::ostringstream escaped;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\':
            escaped << "\\\\";
            break;
        case '"':
            escaped << "\\\"";
            break;
        case '\b':
            escaped << "\\b";
            break;
        case '\f':
            escaped << "\\f";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (ch < 0x20u) {
                static constexpr char kHex[] = "0123456789abcdef";
                escaped << "\\u00" << kHex[(ch >> 4u) & 0x0fu] << kHex[ch & 0x0fu];
            } else {
                escaped << static_cast<char>(ch);
            }
            break;
        }
    }
    return escaped.str();
}

[[nodiscard]] std::string json_quote(std::string_view value)
{
    return "\"" + json_escape(value) + "\"";
}

void append_json_string_field(
    std::ostringstream& json,
    std::string_view key,
    const std::string& value,
    bool& first)
{
    if (!first) {
        json << ",\n";
    }
    first = false;
    json << "  " << json_quote(key) << ": " << json_quote(value);
}

void append_json_path_field(
    std::ostringstream& json,
    std::string_view key,
    const std::filesystem::path& value,
    bool& first)
{
    if (value.empty()) {
        return;
    }
    append_json_string_field(json, key, value.string(), first);
}

void append_json_vector_field(
    std::ostringstream& json,
    std::string_view key,
    const std::vector<std::string>& values,
    bool& first)
{
    if (values.empty()) {
        return;
    }
    if (!first) {
        json << ",\n";
    }
    first = false;
    json << "  " << json_quote(key) << ": [";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0u) {
            json << ", ";
        }
        json << json_quote(values[index]);
    }
    json << "]";
}

[[nodiscard]] std::string build_report_json(const Report& report)
{
    std::ostringstream json;
    json << "{\n";
    bool first = true;
    append_json_string_field(json, "scenario", report.scenario, first);
    append_json_string_field(json, "status", report.status, first);
    append_json_path_field(json, "source", report.source, first);
    append_json_path_field(json, "output", report.output, first);
    append_json_path_field(json, "report", report.report_path, first);
    append_json_path_field(json, "replacement_image", report.replacement_image, first);
    append_json_string_field(json, "source_sheet_name", report.source_sheet_name, first);
    append_json_string_field(json, "renamed_sheet_name", report.renamed_sheet_name, first);
    append_json_string_field(json, "image_part_name", report.image_part_name, first);
    append_json_string_field(json, "error_message", report.error_message, first);
    append_json_vector_field(json, "mutations", report.mutations, first);
    append_json_vector_field(json, "notes", report.notes, first);
    json << "\n}\n";
    return json.str();
}

void write_report(const Report& report)
{
    const std::string json = build_report_json(report);
    if (!report.report_path.empty()) {
        ensure_parent_directory(report.report_path);
        std::ofstream stream(report.report_path, std::ios::binary | std::ios::trunc);
        if (!stream) {
            throw std::runtime_error("failed to write report: " + report.report_path.string());
        }
        stream << json;
        if (!stream) {
            throw std::runtime_error("failed to flush report: " + report.report_path.string());
        }
    }
    std::cout << json;
}

[[nodiscard]] std::string to_lower_ascii(std::string_view text)
{
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return lowered;
}

[[nodiscard]] bool is_missing_sheet_error(const FastXlsxError& error)
{
    const std::string lowered = to_lower_ascii(error.what());
    return lowered.find("worksheet") != std::string::npos
        && lowered.find("not found") != std::string::npos;
}

[[nodiscard]] CliOptions parse_args(int argc, char** argv)
{
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg(argv[index]);
        auto require_value = [&](std::string_view flag) -> std::string {
            if (index + 1 >= argc) {
                throw std::runtime_error("missing value for " + std::string(flag));
            }
            ++index;
            return argv[index];
        };

        if (arg == "--scenario") {
            options.scenario = require_value(arg);
        } else if (arg == "--source") {
            options.source = require_value(arg);
        } else if (arg == "--output") {
            options.output = require_value(arg);
        } else if (arg == "--report") {
            options.report = require_value(arg);
        } else if (arg == "--replacement-image") {
            options.replacement_image = require_value(arg);
        } else if (arg == "--sheet") {
            options.sheet_name = require_value(arg);
        } else if (arg == "--rename-to") {
            options.rename_to = require_value(arg);
        } else {
            throw std::runtime_error("unknown argument: " + std::string(arg));
        }
    }

    if (options.scenario.empty()) {
        throw std::runtime_error("missing required --scenario");
    }
    return options;
}

std::filesystem::path write_two_sheet_source(const std::filesystem::path& path)
{
    ensure_parent_directory(path);

    WorkbookWriter writer = WorkbookWriter::create(path);
    {
        WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({CellView::text("placeholder-a1"), CellView::number(1.0)});
        data.append_row({CellView::text("placeholder-a2")});
    }
    {
        WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({CellView::text("keep-me"), CellView::number(99.0)});
    }
    writer.close();
    return path;
}

std::filesystem::path write_two_sheet_source_with_image(const std::filesystem::path& path)
{
    ensure_parent_directory(path);

    WorkbookWriter writer = WorkbookWriter::create(path);
    {
        WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({CellView::text("placeholder-a1"), CellView::number(1.0)});
        data.append_row({CellView::text("placeholder-a2")});
    }
    {
        WorksheetWriter pictures = writer.add_worksheet("Pictures");
        pictures.append_row({CellView::text("image-sheet")});
        pictures.add_image(fastxlsx::test::tiny_png_bytes(), {1, 1, 2, 2});
    }
    writer.close();
    return path;
}

std::filesystem::path write_public_editing_e2e_source(const std::filesystem::path& path)
{
    ensure_parent_directory(path);

    WorkbookWriterOptions options;
    options.document_properties.creator = "WorkbookEditor E2E";
    options.document_properties.last_modified_by = "WorkbookEditor E2E";
    options.document_properties.title = "Public editing E2E";
    options.document_properties.subject = "FastXLSX";
    options.document_properties.description = "WorkbookEditor public editing smoke source";
    options.document_properties.keywords = "FastXLSX,WorkbookEditor,editing";
    options.document_properties.category = "tests";
    options.document_properties.application = "FastXLSX";
    options.document_properties.app_version = "1.0";

    WorkbookWriter writer = WorkbookWriter::create(path, options);
    {
        WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({CellView::text("placeholder-a1"), CellView::number(1.0)});
        data.append_row({CellView::text("placeholder-a2")});
    }
    {
        WorksheetWriter replace_me = writer.add_worksheet("ReplaceMe");
        replace_me.append_row({CellView::text("replace-old"), CellView::number(5.0)});
    }
    {
        WorksheetWriter pictures = writer.add_worksheet("Pictures");
        pictures.append_row({CellView::text("image-sheet")});
        pictures.add_image(fastxlsx::test::tiny_png_bytes(), {1, 1, 2, 2});
    }
    writer.close();
    return path;
}

std::filesystem::path write_styled_source(
    const std::filesystem::path& path,
    StyleId& number_style)
{
    ensure_parent_directory(path);

    WorkbookWriter writer = WorkbookWriter::create(path);
    number_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
    WorksheetWriter data = writer.add_worksheet("Data");
    data.append_row({CellView::text("old styled").with_style(number_style),
        CellView::text("old plain")});
    writer.close();
    return path;
}

Report run_generated_rename_materialized(const CliOptions& options)
{
    Report report;
    report.scenario = options.scenario;
    report.source = write_two_sheet_source(
        resolve_generated_source(options, "fastxlsx-workbook-editor-qa-generated-source.xlsx"));
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-generated-output.xlsx");
    report.report_path = options.report;
    report.source_sheet_name = "Data";
    report.renamed_sheet_name = "EditedData";
    report.mutations = {
        "rename_sheet:Data->EditedData",
        "worksheet(EditedData).set_cell(A1,text)",
        "worksheet(EditedData).set_cell(B2,number)",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    editor.rename_sheet("Data", "EditedData");
    WorksheetEditor edited = editor.worksheet("EditedData");
    edited.set_cell(1, 1, CellValue::text("materialized-edit"));
    edited.set_cell(2, 2, CellValue::number(42.0));
    editor.save_as(report.output);
    report.notes = {
        "EditedData!A1 should be materialized-edit",
        "EditedData!B2 should be 42",
        "Untouched sheet should remain preserved",
    };
    return report;
}

Report run_generated_style_passthrough(const CliOptions& options)
{
    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;

    StyleId number_style;
    report.source = write_styled_source(
        resolve_generated_source(options, "fastxlsx-workbook-editor-qa-style-source.xlsx"),
        number_style);
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-style-output.xlsx");
    report.source_sheet_name = "Data";
    report.mutations = {
        "replace_sheet_data:Data",
        "style_passthrough:non_default_style",
        "style_normalization:default_style_zero",
    };
    report.notes = {
        "Data!A1 should be 9.5 with style 1/number format 0.00",
        "Data!B1 should be explicit default without s=\"0\"",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    editor.replace_sheet_data("Data",
        {{CellValue::number(9.5).with_style(number_style),
            CellValue::text("explicit default").with_style(StyleId {})}});
    editor.save_as(report.output);
    return report;
}

Report run_generated_image_replace(const CliOptions& options)
{
    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = write_two_sheet_source_with_image(
        resolve_generated_source(options, "fastxlsx-workbook-editor-qa-image-source.xlsx"));
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-image-output.xlsx");
    report.replacement_image = resolve_replacement_png_path(options);
    report.image_part_name = "xl/media/image1.png";
    report.mutations = {"replace_image:xl/media/image1.png"};
    report.notes = {
        "Pictures worksheet and drawing XML should remain readable",
        "xl/media/image1.png bytes should match replacement_image",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    editor.replace_image(report.image_part_name, report.replacement_image);
    editor.save_as(report.output);
    return report;
}

Report run_generated_public_e2e(const CliOptions& options)
{
    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = write_public_editing_e2e_source(
        resolve_generated_source(options, "fastxlsx-workbook-editor-qa-e2e-source.xlsx"));
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-e2e-output.xlsx");
    report.replacement_image = resolve_replacement_png_path(options);
    report.source_sheet_name = "Data";
    report.renamed_sheet_name = "EditedData";
    report.image_part_name = "xl/media/image1.png";
    report.mutations = {
        "rename_sheet:Data->EditedData",
        "worksheet(EditedData).set_cell(A1,text)",
        "worksheet(EditedData).set_cell(B2,number)",
        "replace_sheet_data:ReplaceMe",
        "replace_image:xl/media/image1.png",
    };
    report.notes = {
        "EditedData!A1 should be materialized-edit",
        "EditedData!B2 should be 42",
        "ReplaceMe!A1 should be sheetdata-final",
        "ReplaceMe!B1 should be 7",
        "Pictures media part should match replacement_image",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    editor.rename_sheet("Data", "EditedData");
    WorksheetEditor edited_data = editor.worksheet("EditedData");
    edited_data.set_cell(1, 1, CellValue::text("materialized-edit"));
    edited_data.set_cell(2, 2, CellValue::number(42.0));
    editor.replace_sheet_data("ReplaceMe",
        {{CellValue::text("sheetdata-final"), CellValue::number(7.0)}});
    editor.replace_image(report.image_part_name, report.replacement_image);
    editor.save_as(report.output);
    return report;
}

Report run_generated_non_default_style_rejection(const CliOptions& options)
{
    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = write_two_sheet_source(resolve_generated_source(
        options, "fastxlsx-workbook-editor-qa-style-reject-source.xlsx"));
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-style-reject-output.xlsx");
    report.source_sheet_name = "Data";
    report.mutations = {"worksheet(Data).set_cell(C3,styled_text):expect_reject"};
    report.notes = {
        "WorkbookEditor should reject non-default StyleId values in WorksheetEditor",
        "A no-op save after rejection should still produce a copy-original workbook",
    };

    StyleId non_default_style;
    {
        const std::filesystem::path style_provider =
            fastxlsx::test::artifact_path("fastxlsx-workbook-editor-qa-style-provider.xlsx");
        WorkbookWriter writer = WorkbookWriter::create(style_provider);
        non_default_style = writer.add_style(fastxlsx::CellStyle {"0.00"});
        WorksheetWriter data = writer.add_worksheet("StyleProvider");
        data.append_row({CellView::number(1.0).with_style(non_default_style)});
        writer.close();
    }

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    WorksheetEditor sheet = editor.worksheet("Data");
    try {
        sheet.set_cell(3, 3, CellValue::text("must-not-write").with_style(non_default_style));
        throw std::runtime_error("expected WorksheetEditor::set_cell() to reject non-default style");
    } catch (const FastXlsxError& error) {
        report.status = "expected_rejection_observed";
        report.error_message = error.what();
    }
    editor.save_as(report.output);
    return report;
}

[[nodiscard]] std::string pick_fixture_sheet_name(
    const CliOptions& options,
    const WorkbookEditor& editor)
{
    if (!options.sheet_name.empty()) {
        return options.sheet_name;
    }

    const std::vector<std::string> source_names = editor.source_worksheet_names();
    if (source_names.empty()) {
        throw std::runtime_error("fixture workbook has no worksheets");
    }
    return source_names.front();
}

Report run_fixture_rename_materialized(const CliOptions& options)
{
    if (options.source.empty()) {
        throw std::runtime_error("fixture_rename_materialized requires --source");
    }

    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = options.source;
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-fixture-output.xlsx");
    report.renamed_sheet_name = options.rename_to.empty() ? "QA_Renamed" : options.rename_to;
    report.mutations = {
        "rename_sheet:<fixture>",
        "worksheet(<fixture_renamed>).set_cell(A1,text)",
        "worksheet(<fixture_renamed>).set_cell(B2,number)",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    report.source_sheet_name = pick_fixture_sheet_name(options, editor);
    editor.rename_sheet(report.source_sheet_name, report.renamed_sheet_name);
    WorksheetEditor sheet = editor.worksheet(report.renamed_sheet_name);
    sheet.set_cell(1, 1, CellValue::text("fixture-materialized-edit"));
    sheet.set_cell(2, 2, CellValue::number(42.0));
    editor.save_as(report.output);

    report.notes = {
        report.renamed_sheet_name + "!A1 should be fixture-materialized-edit",
        report.renamed_sheet_name + "!B2 should be 42",
        "Source sharedStrings/styles parts should stay copy-preserved when present",
    };
    return report;
}

Report run_fixture_materialized_only(const CliOptions& options)
{
    if (options.source.empty()) {
        throw std::runtime_error("fixture_materialized_only requires --source");
    }

    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = options.source;
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-fixture-materialized-output.xlsx");
    report.mutations = {
        "worksheet(<fixture>).set_cell(A1,text)",
        "worksheet(<fixture>).set_cell(B2,number)",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    report.source_sheet_name = pick_fixture_sheet_name(options, editor);
    WorksheetEditor sheet = editor.worksheet(report.source_sheet_name);
    sheet.set_cell(1, 1, CellValue::text("fixture-materialized-edit"));
    sheet.set_cell(2, 2, CellValue::number(42.0));
    editor.save_as(report.output);

    report.notes = {
        report.source_sheet_name + "!A1 should be fixture-materialized-edit",
        report.source_sheet_name + "!B2 should be 42",
        "Worksheet name should remain unchanged for materialized-only fallback",
    };
    return report;
}

Report run_scenario(const CliOptions& options)
{
    if (options.scenario == "generated_rename_materialized") {
        return run_generated_rename_materialized(options);
    }
    if (options.scenario == "generated_style_passthrough") {
        return run_generated_style_passthrough(options);
    }
    if (options.scenario == "generated_image_replace") {
        return run_generated_image_replace(options);
    }
    if (options.scenario == "generated_public_e2e") {
        return run_generated_public_e2e(options);
    }
    if (options.scenario == "generated_non_default_style_rejection") {
        return run_generated_non_default_style_rejection(options);
    }
    if (options.scenario == "fixture_rename_materialized") {
        return run_fixture_rename_materialized(options);
    }
    if (options.scenario == "fixture_materialized_only") {
        return run_fixture_materialized_only(options);
    }

    throw std::runtime_error("unknown scenario: " + options.scenario);
}

} // namespace

int main(int argc, char** argv)
{
    Report report;
    try {
        const CliOptions options = parse_args(argc, argv);
        report.scenario = options.scenario;
        report.source = options.source;
        report.output = options.output;
        report.report_path = options.report;
        report.replacement_image = options.replacement_image;
        report.source_sheet_name = options.sheet_name;
        report.renamed_sheet_name = options.rename_to;
        report = run_scenario(options);
        write_report(report);
        return 0;
    } catch (const std::exception& error) {
        if (report.scenario.empty()) {
            try {
                const CliOptions options = parse_args(argc, argv);
                report.scenario = options.scenario;
                report.report_path = options.report;
                report.source = options.source;
                report.output = options.output;
                report.replacement_image = options.replacement_image;
            } catch (...) {
            }
        }
        report.status = "failed";
        report.error_message = error.what();
        try {
            write_report(report);
        } catch (...) {
        }
        std::cerr << "fastxlsx_workbook_editor_qa_tool failed: " << error.what() << "\n";
        return 1;
    }
}
