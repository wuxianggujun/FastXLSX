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
    std::string image_part_name;
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
    int source_formula_audit_count = 0;
    int source_formula_rename_risk_count = 0;
    int source_formula_external_count = 0;
    int source_formula_sheet_range_count = 0;
    int source_formula_matched_count = 0;
    std::vector<std::string> source_formula_audit_references;
    int defined_name_audit_count = 0;
    int defined_name_audit_rename_risk_count = 0;
    int defined_name_audit_external_count = 0;
    int defined_name_audit_sheet_range_count = 0;
    int defined_name_audit_matched_count = 0;
    std::vector<std::string> defined_name_audit_references;
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

[[nodiscard]] std::string to_lower_ascii(std::string_view text);

[[nodiscard]] bool is_jpeg_image_part_name(std::string_view image_part_name)
{
    const std::string lowered = to_lower_ascii(image_part_name);
    return lowered.ends_with(".jpg") || lowered.ends_with(".jpeg");
}

[[nodiscard]] std::filesystem::path resolve_replacement_image_path(
    const CliOptions& options, std::string_view image_part_name)
{
    if (!options.replacement_image.empty()) {
        return options.replacement_image;
    }

    const std::filesystem::path asset = is_jpeg_image_part_name(image_part_name)
        ? repository_asset("docs/assets/donation/zhifubao.jpg")
        : repository_asset("docs/assets/donation/weixin.png");
    if (std::filesystem::exists(asset)) {
        return asset;
    }

    throw std::runtime_error("missing replacement image asset; pass --replacement-image explicitly");
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

void append_json_number_field(
    std::ostringstream& json,
    std::string_view key,
    int value,
    bool& first)
{
    if (!first) {
        json << ",\n";
    }
    first = false;
    json << "  " << json_quote(key) << ": " << value;
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
    append_json_number_field(
        json, "source_formula_audit_count", report.source_formula_audit_count, first);
    append_json_number_field(json, "source_formula_rename_risk_count",
        report.source_formula_rename_risk_count, first);
    append_json_number_field(
        json, "source_formula_external_count", report.source_formula_external_count, first);
    append_json_number_field(json, "source_formula_sheet_range_count",
        report.source_formula_sheet_range_count, first);
    append_json_number_field(
        json, "source_formula_matched_count", report.source_formula_matched_count, first);
    append_json_vector_field(json, "source_formula_audit_references",
        report.source_formula_audit_references, first);
    append_json_number_field(
        json, "defined_name_audit_count", report.defined_name_audit_count, first);
    append_json_number_field(json, "defined_name_audit_rename_risk_count",
        report.defined_name_audit_rename_risk_count, first);
    append_json_number_field(
        json, "defined_name_audit_external_count", report.defined_name_audit_external_count, first);
    append_json_number_field(json, "defined_name_audit_sheet_range_count",
        report.defined_name_audit_sheet_range_count, first);
    append_json_number_field(
        json, "defined_name_audit_matched_count", report.defined_name_audit_matched_count, first);
    append_json_vector_field(json, "defined_name_audit_references",
        report.defined_name_audit_references, first);
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
        } else if (arg == "--image-part") {
            options.image_part_name = require_value(arg);
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

std::filesystem::path write_formula_reference_source(const std::filesystem::path& path)
{
    ensure_parent_directory(path);

    WorkbookWriter writer = WorkbookWriter::create(path);
    {
        WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({CellView::number(1.0)});
    }
    {
        WorksheetWriter other = writer.add_worksheet("Other Sheet");
        other.append_row({CellView::number(2.0)});
    }
    {
        WorksheetWriter apostrophe = writer.add_worksheet("O'Brien");
        apostrophe.append_row({CellView::number(3.0)});
    }
    {
        WorksheetWriter formulas = writer.add_worksheet("Formula");
        formulas.append_row({CellView::formula(
            R"(Data!A1+'Other Sheet'!A1+'O''Brien'!A1+[Book.xlsx]Data!A1+Data:Formula!A1+"Data!Z9")")});
    }
    writer.close();

    return path;
}

std::filesystem::path write_formula_rename_rewrite_source(const std::filesystem::path& path)
{
    ensure_parent_directory(path);

    WorkbookWriter writer = WorkbookWriter::create(path);
    {
        WorksheetWriter data = writer.add_worksheet("Data");
        data.append_row({CellView::number(1.0), CellView::number(2.0)});
    }
    {
        WorksheetWriter other = writer.add_worksheet("Other Sheet");
        other.append_row({CellView::number(10.0)});
    }
    {
        WorksheetWriter formulas = writer.add_worksheet("Formula");
        formulas.append_row({CellView::formula("Data!A1")});
        formulas.append_row({CellView::formula("'Data'!$A$1")});
        formulas.append_row({CellView::formula("[Book.xlsx]Data!A1")});
        formulas.append_row({CellView::formula("Data:Formula!A1")});
        formulas.append_row({CellView::formula(R"(Data!A1+"Data!A1")")});
    }
    {
        WorksheetWriter unmaterialized = writer.add_worksheet("Unmaterialized");
        unmaterialized.append_row({CellView::formula("Data!A1")});
    }
    writer.close();

    auto entries = fastxlsx::test::read_zip_entries(path);
    std::string& workbook_xml = entries.at("xl/workbook.xml");
    const std::string defined_names_xml =
        R"(<definedNames>)"
        R"(<definedName name="ReportRange">Data!$A$1:$B$2</definedName>)"
        R"(<definedName name="QuotedDataRef">'Data'!$A$1</definedName>)"
        R"(<definedName name="ScopedOther" localSheetId="2">'Other Sheet'!$A$1</definedName>)"
        R"(<definedName name="ExternalRef">[Book.xlsx]Data!A1</definedName>)"
        R"(<definedName name="ThreeDRef">Data:Formula!A1</definedName>)"
        R"(<definedName name="LiteralText">"Data!A1"</definedName>)"
        R"(</definedNames>)";
    const std::size_t calc_begin = workbook_xml.find("<calcPr");
    if (calc_begin != std::string::npos) {
        workbook_xml.insert(calc_begin, defined_names_xml);
    } else {
        const std::size_t workbook_end = workbook_xml.find("</workbook>");
        if (workbook_end == std::string::npos) {
            throw std::runtime_error("formula rename rewrite source workbook.xml is missing </workbook>");
        }
        workbook_xml.insert(workbook_end, defined_names_xml);
    }
    fastxlsx::test::write_stored_zip_entries(path, entries);
    return path;
}

std::filesystem::path write_shared_formula_source(const std::filesystem::path& path)
{
    ensure_parent_directory(path);

    {
        WorkbookWriter writer = WorkbookWriter::create(path);
        WorksheetWriter data = writer.add_worksheet("SharedFormula");
        data.append_row({CellView::number(1.0), CellView::number(2.0),
            CellView::formula("A1+B1")});
        data.append_row({CellView::number(4.0), CellView::number(5.0),
            CellView::formula("A2+B2")});
        data.append_row({CellView::number(7.0), CellView::number(8.0),
            CellView::formula("A3+B3")});
        WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({CellView::text("keep-shared-formula-qa")});
        writer.close();
    }

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<dimension ref="A1:E4"/>)"
        R"(<sheetData>)"
        R"(<row r="1"><c r="A1"><v>1</v></c><c r="B1"><v>2</v></c><c r="C1"><f t="shared" ref="C1:C3" si="10">A1+B1</f><v>3</v></c><c r="D1"><f t="shared" ref="D1:D3" si="11">SUM(A1:B1)+$A1+A$1+$A$1</f><v>5</v></c></row>)"
        R"(<row r="2"><c r="A2"><v>4</v></c><c r="B2"><v>5</v></c><c r="C2"><f t="shared" si="10"/><v>9</v></c><c r="D2"><f t="shared" si="11"/><v>14</v></c></row>)"
        R"(<row r="3"><c r="A3"><v>7</v></c><c r="B3"><v>8</v></c><c r="C3"><f t="shared" si="10"/><v>15</v></c><c r="D3"><f t="shared" si="11"/><v>23</v></c></row>)"
        R"(</sheetData>)"
        R"(</worksheet>)";
    fastxlsx::test::rewrite_package_entry_as_stored(
        path, "xl/worksheets/sheet1.xml", worksheet_xml);
    return path;
}

std::filesystem::path write_shared_formula_boundary_source(const std::filesystem::path& path)
{
    ensure_parent_directory(path);

    {
        WorkbookWriter writer = WorkbookWriter::create(path);
        WorksheetWriter data = writer.add_worksheet("SharedBoundaries");
        data.append_row({CellView::number(1.0), CellView::number(2.0),
            CellView::formula("A1+B1")});
        data.append_row({CellView::number(3.0), CellView::number(4.0),
            CellView::formula("A2+B2")});
        WorksheetWriter other = writer.add_worksheet("Other Sheet");
        other.append_row({CellView::number(10.0)});
        WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({CellView::text("keep-shared-formula-boundary-qa")});
        writer.close();
    }

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<dimension ref="A1:F4"/>)"
        R"(<sheetData>)"
        R"(<row r="1"><c r="A1"><v>1</v></c><c r="B1"><v>2</v></c>)"
        R"(<c r="C1"><f t="shared" ref="C1:C2" si="30">A1+SharedBoundaries!A1+'Other Sheet'!A1+SUM(A1:B1)+LOG10(A1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(1:1)&amp;&quot;A1&quot;+[Book.xlsx]Sheet1!A1</f><v>0</v></c>)"
        R"(<c r="D1"><f t="shared" ref="D1:E2" si="31">C1+D$1+$C1+$C$1</f><v>0</v></c></row>)"
        R"(<row r="2"><c r="A2"><v>3</v></c><c r="B2"><v>4</v></c><c r="C2"><f t="shared" si="30"/><v>999</v></c><c r="E2"><f t="shared" si="31"/><v>888</v></c></row>)"
        R"(</sheetData>)"
        R"(</worksheet>)";
    fastxlsx::test::rewrite_package_entry_as_stored(
        path, "xl/worksheets/sheet1.xml", worksheet_xml);
    return path;
}

std::filesystem::path write_shared_formula_office_like_source(const std::filesystem::path& path)
{
    ensure_parent_directory(path);

    {
        WorkbookWriter writer = WorkbookWriter::create(path);
        WorksheetWriter data = writer.add_worksheet("OfficeLikeShared");
        data.append_row({CellView::number(1.0), CellView::number(2.0),
            CellView::formula("A1+B1")});
        data.append_row({CellView::number(10.0), CellView::number(20.0)});
        data.append_row({CellView::number(100.0), CellView::number(200.0)});
        WorksheetWriter untouched = writer.add_worksheet("Untouched");
        untouched.append_row({CellView::text("keep-office-like-shared-formula-qa")});
        writer.close();
    }

    const std::string worksheet_xml =
        R"(<?xml version="1.0" encoding="UTF-8"?>)"
        R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">)"
        R"(<dimension ref="A1:H6"/>)"
        R"(<sheetData>)"
        R"(<row r="1"><c r="A1"><v>1</v></c><c r="B1"><v>2</v></c>)"
        R"(<c r="C1"><f t="shared" ref="C1:D3" si="40">A1+B1</f><v>9901</v></c>)"
        R"(<c r="D1"><f t="shared" si="40"/><v>9902</v></c>)"
        R"(<c r="E1"><f>A1*2</f><v>9903</v></c></row>)"
        R"(<row r="2"><c r="A2"><v>10</v></c><c r="B2"><v>20</v></c>)"
        R"(<c r="C2"><f t="shared" si="40"/><v>9904</v></c>)"
        R"(<c r="D2"><f t="shared" si="40"/><v>9905</v></c>)"
        R"(<c r="E2" t="inlineStr"><is><t>between-shared-groups</t></is></c>)"
        R"(<c r="F2"><f t="shared" ref="F2:G3" si="41">SUM($A2:B2)+C$1</f><v>9906</v></c>)"
        R"(<c r="G2"><f t="shared" si="41"/><v>9907</v></c></row>)"
        R"(<row r="3"><c r="A3"><v>100</v></c><c r="B3"><v>200</v></c>)"
        R"(<c r="C3"><f t="shared" si="40"/><v>9908</v></c>)"
        R"(<c r="D3"><f t="shared" si="40"/><v>9909</v></c>)"
        R"(<c r="F3"><f t="shared" si="41"/><v>9910</v></c>)"
        R"(<c r="G3"><f t="shared" si="41"/><v>9911</v></c></row>)"
        R"(</sheetData>)"
        R"(</worksheet>)";
    fastxlsx::test::rewrite_package_entry_as_stored(
        path, "xl/worksheets/sheet1.xml", worksheet_xml);
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

void require_formula_cell(
    WorksheetEditor& sheet,
    std::string_view reference,
    std::string_view expected)
{
    const std::optional<CellValue> value = sheet.try_cell(reference);
    if (!value.has_value() || value->kind() != fastxlsx::CellValueKind::Formula
        || value->text_value() != expected) {
        std::ostringstream message;
        message << "shared formula materialization mismatch at " << reference
                << ": expected " << expected;
        if (value.has_value()) {
            message << ", got kind=" << static_cast<int>(value->kind())
                    << " text=" << value->text_value();
        } else {
            message << ", got empty";
        }
        throw std::runtime_error(message.str());
    }
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

void summarize_source_formula_audits(
    Report& report,
    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit>& audits)
{
    report.source_formula_audit_count = static_cast<int>(audits.size());
    report.source_formula_rename_risk_count = 0;
    report.source_formula_external_count = 0;
    report.source_formula_sheet_range_count = 0;
    report.source_formula_matched_count = 0;
    report.source_formula_audit_references.clear();
    report.source_formula_audit_references.reserve(audits.size());

    for (const fastxlsx::WorkbookEditorFormulaReferenceAudit& audit : audits) {
        if (audit.references_renamed_source_name) {
            ++report.source_formula_rename_risk_count;
        }
        if (audit.external_workbook_qualifier) {
            ++report.source_formula_external_count;
        }
        if (audit.sheet_range_qualifier) {
            ++report.source_formula_sheet_range_count;
        }
        if (audit.matched_current_workbook_sheet) {
            ++report.source_formula_matched_count;
        }
        report.source_formula_audit_references.push_back(audit.qualified_reference_text);
    }
}

void summarize_defined_name_audits(
    Report& report,
    const std::vector<fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit>& audits)
{
    report.defined_name_audit_count = static_cast<int>(audits.size());
    report.defined_name_audit_rename_risk_count = 0;
    report.defined_name_audit_external_count = 0;
    report.defined_name_audit_sheet_range_count = 0;
    report.defined_name_audit_matched_count = 0;
    report.defined_name_audit_references.clear();
    report.defined_name_audit_references.reserve(audits.size());

    for (const fastxlsx::WorkbookEditorDefinedNameFormulaReferenceAudit& audit : audits) {
        if (audit.references_renamed_source_name) {
            ++report.defined_name_audit_rename_risk_count;
        }
        if (audit.external_workbook_qualifier) {
            ++report.defined_name_audit_external_count;
        }
        if (audit.sheet_range_qualifier) {
            ++report.defined_name_audit_sheet_range_count;
        }
        if (audit.matched_current_workbook_sheet) {
            ++report.defined_name_audit_matched_count;
        }
        report.defined_name_audit_references.push_back(
            audit.defined_name + ":" + audit.qualified_reference_text);
    }
}

Report run_generated_source_formula_audit(const CliOptions& options)
{
    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = write_formula_reference_source(resolve_generated_source(
        options, "fastxlsx-workbook-editor-qa-source-formula-audit-source.xlsx"));
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-source-formula-audit-output.xlsx");
    report.source_sheet_name = "Formula";
    report.renamed_sheet_name = "RenamedData";
    report.mutations = {
        "source_formula_reference_audits:before_rename",
        "rename_sheet:Data->RenamedData",
        "source_formula_reference_audits:after_rename",
    };
    report.notes = {
        "Source worksheet formula audit should not materialize WorksheetEditor sessions",
        "Data!A1 should be flagged as a stale source-name formula risk after rename",
        "External workbook and 3D sheet-range qualifiers stay audit-only",
        "Saved output should keep original formula text unchanged",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    if (!editor.formula_reference_audits().empty()) {
        throw std::runtime_error(
            "source formula audit QA unexpectedly materialized worksheet sessions");
    }

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> initial_audits =
        editor.source_formula_reference_audits();
    if (initial_audits.size() != 5u) {
        throw std::runtime_error("source formula audit QA expected 5 initial references");
    }

    editor.rename_sheet("Data", "RenamedData");
    if (!editor.formula_reference_audits().empty()) {
        throw std::runtime_error(
            "source formula audit QA should not populate materialized formula diagnostics");
    }

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> renamed_audits =
        editor.source_formula_reference_audits();
    summarize_source_formula_audits(report, renamed_audits);
    if (report.source_formula_audit_count != 5) {
        throw std::runtime_error("source formula audit QA expected 5 renamed references");
    }
    if (report.source_formula_rename_risk_count != 1) {
        throw std::runtime_error("source formula audit QA expected one rename-risk reference");
    }
    if (report.source_formula_external_count != 1) {
        throw std::runtime_error("source formula audit QA expected one external reference");
    }
    if (report.source_formula_sheet_range_count != 1) {
        throw std::runtime_error("source formula audit QA expected one 3D sheet-range reference");
    }
    if (report.source_formula_matched_count != 3) {
        throw std::runtime_error("source formula audit QA expected three local matched references");
    }

    editor.save_as(report.output);
    return report;
}

Report run_generated_formula_rename_rewrite(const CliOptions& options)
{
    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = write_formula_rename_rewrite_source(resolve_generated_source(
        options, "fastxlsx-workbook-editor-qa-formula-rename-rewrite-source.xlsx"));
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-formula-rename-rewrite-output.xlsx");
    report.source_sheet_name = "Data";
    report.renamed_sheet_name = "RenamedData";
    report.mutations = {
        "worksheet(Formula).try_cell(A1:A5):materialize_formula_cells",
        "rename_sheet:Data->RenamedData:RewriteDefinedNamesAndMaterializedWorksheetFormulas",
        "save_as",
    };
    report.notes = {
        "Opt-in rename formula policy should rewrite direct local definedName references",
        "Opt-in rename formula policy should rewrite only already-materialized worksheet formulas",
        "External workbook references, 3D sheet-range references, string literals, and non-materialized worksheet formulas should remain unchanged",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    WorksheetEditor formula_sheet = editor.worksheet("Formula");
    require_formula_cell(formula_sheet, "A1", "Data!A1");
    require_formula_cell(formula_sheet, "A2", "'Data'!$A$1");
    require_formula_cell(formula_sheet, "A3", "[Book.xlsx]Data!A1");
    require_formula_cell(formula_sheet, "A4", "Data:Formula!A1");
    require_formula_cell(formula_sheet, "A5", R"(Data!A1+"Data!A1")");
    if (formula_sheet.has_pending_changes()) {
        throw std::runtime_error(
            "formula rename rewrite QA read-only materialization dirtied Formula sheet");
    }

    fastxlsx::WorkbookEditorRenameOptions rename_options;
    rename_options.formula_policy =
        fastxlsx::WorkbookEditorRenameFormulaPolicy::
            RewriteDefinedNamesAndMaterializedWorksheetFormulas;
    editor.rename_sheet("Data", "RenamedData", rename_options);

    require_formula_cell(formula_sheet, "A1", "'RenamedData'!A1");
    require_formula_cell(formula_sheet, "A2", "'RenamedData'!$A$1");
    require_formula_cell(formula_sheet, "A3", "[Book.xlsx]Data!A1");
    require_formula_cell(formula_sheet, "A4", "Data:Formula!A1");
    require_formula_cell(formula_sheet, "A5", R"('RenamedData'!A1+"Data!A1")");
    if (!formula_sheet.has_pending_changes()) {
        throw std::runtime_error(
            "formula rename rewrite QA did not mark rewritten Formula sheet dirty");
    }

    summarize_source_formula_audits(report, editor.formula_reference_audits());
    summarize_defined_name_audits(report, editor.defined_name_formula_reference_audits());
    if (report.source_formula_rename_risk_count != 0) {
        throw std::runtime_error(
            "formula rename rewrite QA should not leave materialized local rename risks");
    }
    if (report.source_formula_external_count != 1 || report.source_formula_sheet_range_count != 1) {
        throw std::runtime_error(
            "formula rename rewrite QA should preserve one external and one 3D materialized reference");
    }
    if (report.defined_name_audit_rename_risk_count != 0) {
        throw std::runtime_error(
            "formula rename rewrite QA should not leave definedName local rename risks");
    }
    if (report.defined_name_audit_external_count != 1
        || report.defined_name_audit_sheet_range_count != 1) {
        throw std::runtime_error(
            "formula rename rewrite QA should preserve one external and one 3D definedName reference");
    }

    editor.save_as(report.output);
    return report;
}

Report run_generated_formula_rename_defined_names_only(const CliOptions& options)
{
    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = write_formula_rename_rewrite_source(resolve_generated_source(
        options, "fastxlsx-workbook-editor-qa-formula-rename-defined-names-source.xlsx"));
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-formula-rename-defined-names-output.xlsx");
    report.source_sheet_name = "Data";
    report.renamed_sheet_name = "RenamedData";
    report.mutations = {
        "worksheet(Formula).try_cell(A1:A5):materialize_formula_cells",
        "rename_sheet:Data->RenamedData:RewriteDefinedNames",
        "save_as",
    };
    report.notes = {
        "RewriteDefinedNames should rewrite direct local definedName references",
        "RewriteDefinedNames should not rewrite already-materialized worksheet formulas",
        "External workbook references, 3D sheet-range references, string literals, and non-materialized worksheet formulas should remain unchanged",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    WorksheetEditor formula_sheet = editor.worksheet("Formula");
    require_formula_cell(formula_sheet, "A1", "Data!A1");
    require_formula_cell(formula_sheet, "A2", "'Data'!$A$1");
    require_formula_cell(formula_sheet, "A3", "[Book.xlsx]Data!A1");
    require_formula_cell(formula_sheet, "A4", "Data:Formula!A1");
    require_formula_cell(formula_sheet, "A5", R"(Data!A1+"Data!A1")");
    if (formula_sheet.has_pending_changes()) {
        throw std::runtime_error(
            "definedNames-only formula rename QA read-only materialization dirtied Formula sheet");
    }

    fastxlsx::WorkbookEditorRenameOptions rename_options;
    rename_options.formula_policy =
        fastxlsx::WorkbookEditorRenameFormulaPolicy::RewriteDefinedNames;
    editor.rename_sheet("Data", "RenamedData", rename_options);

    require_formula_cell(formula_sheet, "A1", "Data!A1");
    require_formula_cell(formula_sheet, "A2", "'Data'!$A$1");
    require_formula_cell(formula_sheet, "A3", "[Book.xlsx]Data!A1");
    require_formula_cell(formula_sheet, "A4", "Data:Formula!A1");
    require_formula_cell(formula_sheet, "A5", R"(Data!A1+"Data!A1")");
    if (formula_sheet.has_pending_changes()) {
        throw std::runtime_error(
            "definedNames-only formula rename QA dirtied materialized Formula sheet");
    }

    summarize_source_formula_audits(report, editor.formula_reference_audits());
    summarize_defined_name_audits(report, editor.defined_name_formula_reference_audits());
    if (report.source_formula_rename_risk_count != 3) {
        throw std::runtime_error(
            "definedNames-only formula rename QA expected three materialized local rename risks");
    }
    if (report.source_formula_external_count != 1 || report.source_formula_sheet_range_count != 1) {
        throw std::runtime_error(
            "definedNames-only formula rename QA should preserve one external and one 3D materialized reference");
    }
    if (report.defined_name_audit_rename_risk_count != 0) {
        throw std::runtime_error(
            "definedNames-only formula rename QA should not leave definedName local rename risks");
    }
    if (report.defined_name_audit_external_count != 1
        || report.defined_name_audit_sheet_range_count != 1) {
        throw std::runtime_error(
            "definedNames-only formula rename QA should preserve one external and one 3D definedName reference");
    }

    editor.save_as(report.output);
    return report;
}

Report run_generated_formula_rename_default_audit(const CliOptions& options)
{
    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = write_formula_rename_rewrite_source(resolve_generated_source(
        options, "fastxlsx-workbook-editor-qa-formula-rename-default-source.xlsx"));
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-formula-rename-default-output.xlsx");
    report.source_sheet_name = "Data";
    report.renamed_sheet_name = "RenamedData";
    report.mutations = {
        "worksheet(Formula).try_cell(A1:A5):materialize_formula_cells",
        "rename_sheet:Data->RenamedData:AuditOnly",
        "save_as",
    };
    report.notes = {
        "Default rename_sheet should stay catalog-only",
        "Default rename_sheet should not rewrite direct local definedName formulas",
        "Default rename_sheet should not rewrite already-materialized worksheet formulas",
        "Formula and definedName audits should still report stale source-name risks",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    WorksheetEditor formula_sheet = editor.worksheet("Formula");
    require_formula_cell(formula_sheet, "A1", "Data!A1");
    require_formula_cell(formula_sheet, "A2", "'Data'!$A$1");
    require_formula_cell(formula_sheet, "A3", "[Book.xlsx]Data!A1");
    require_formula_cell(formula_sheet, "A4", "Data:Formula!A1");
    require_formula_cell(formula_sheet, "A5", R"(Data!A1+"Data!A1")");

    editor.rename_sheet("Data", "RenamedData");

    require_formula_cell(formula_sheet, "A1", "Data!A1");
    require_formula_cell(formula_sheet, "A2", "'Data'!$A$1");
    require_formula_cell(formula_sheet, "A3", "[Book.xlsx]Data!A1");
    require_formula_cell(formula_sheet, "A4", "Data:Formula!A1");
    require_formula_cell(formula_sheet, "A5", R"(Data!A1+"Data!A1")");
    if (formula_sheet.has_pending_changes()) {
        throw std::runtime_error(
            "default formula rename QA dirtied materialized Formula sheet");
    }

    summarize_source_formula_audits(report, editor.formula_reference_audits());
    summarize_defined_name_audits(report, editor.defined_name_formula_reference_audits());
    if (report.source_formula_rename_risk_count != 3) {
        throw std::runtime_error(
            "default formula rename QA expected three materialized local rename risks");
    }
    if (report.source_formula_external_count != 1 || report.source_formula_sheet_range_count != 1) {
        throw std::runtime_error(
            "default formula rename QA should preserve one external and one 3D materialized reference");
    }
    if (report.defined_name_audit_rename_risk_count != 2) {
        throw std::runtime_error(
            "default formula rename QA expected two definedName local rename risks");
    }
    if (report.defined_name_audit_external_count != 1
        || report.defined_name_audit_sheet_range_count != 1) {
        throw std::runtime_error(
            "default formula rename QA should preserve one external and one 3D definedName reference");
    }

    editor.save_as(report.output);
    return report;
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

Report run_generated_shared_formula_materialization(const CliOptions& options)
{
    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = write_shared_formula_source(resolve_generated_source(
        options, "fastxlsx-workbook-editor-qa-shared-formula-source.xlsx"));
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-shared-formula-output.xlsx");
    report.source_sheet_name = "SharedFormula";
    report.mutations = {
        "worksheet(SharedFormula).try_cell(C1:C3,D1:D3):expect_formula_materialized",
        "worksheet(SharedFormula).set_cell(E4,text)",
    };
    report.notes = {
        "C1:C3 and D1:D3 should be ordinary formula cells after materialization",
        "Shared formula metadata and stale cached values should be absent from dirty output",
        "Untouched sheet should remain preserved",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    WorksheetEditor sheet = editor.worksheet("SharedFormula");
    require_formula_cell(sheet, "C1", "A1+B1");
    require_formula_cell(sheet, "C2", "A2+B2");
    require_formula_cell(sheet, "C3", "A3+B3");
    require_formula_cell(sheet, "D1", "SUM(A1:B1)+$A1+A$1+$A$1");
    require_formula_cell(sheet, "D2", "SUM(A2:B2)+$A2+A$1+$A$1");
    require_formula_cell(sheet, "D3", "SUM(A3:B3)+$A3+A$1+$A$1");
    if (sheet.has_pending_changes() || editor.has_pending_changes()) {
        throw std::runtime_error("read-only shared formula materialization dirtied editor state");
    }

    sheet.set_cell(4, 5, CellValue::text("shared-formula-qa-edit"));
    editor.save_as(report.output);
    return report;
}

Report run_generated_shared_formula_boundary_materialization(const CliOptions& options)
{
    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = write_shared_formula_boundary_source(resolve_generated_source(
        options, "fastxlsx-workbook-editor-qa-shared-formula-boundary-source.xlsx"));
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-shared-formula-boundary-output.xlsx");
    report.source_sheet_name = "SharedBoundaries";
    report.mutations = {
        "worksheet(SharedBoundaries).try_cell(C1:C2,D1,E2):expect_formula_boundaries",
        "worksheet(SharedBoundaries).set_cell(F4,text)",
    };
    report.notes = {
        "Shared formula boundary followers should become ordinary formula text",
        "Quoted strings, structured references, names, and R1C1-like text should not be rewritten",
        "Sheet-qualified A1 references and whole-row/whole-column ranges should still translate under the narrow materializer rule",
    };

    constexpr std::string_view expected_c1 =
        R"(A1+SharedBoundaries!A1+'Other Sheet'!A1+SUM(A1:B1)+LOG10(A1)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(1:1)&"A1"+[Book.xlsx]Sheet1!A1)";
    constexpr std::string_view expected_c2 =
        R"(A2+SharedBoundaries!A2+'Other Sheet'!A2+SUM(A2:B2)+LOG10(A2)+A1foo+_A1+A1_+R1C1+Table1[A1]+SUM(A:A)+SUM(2:2)&"A1"+[Book.xlsx]Sheet1!A2)";

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    WorksheetEditor sheet = editor.worksheet("SharedBoundaries");
    require_formula_cell(sheet, "C1", expected_c1);
    require_formula_cell(sheet, "C2", expected_c2);
    require_formula_cell(sheet, "D1", "C1+D$1+$C1+$C$1");
    require_formula_cell(sheet, "E2", "D2+E$1+$C2+$C$1");
    if (sheet.has_pending_changes() || editor.has_pending_changes()) {
        throw std::runtime_error(
            "read-only shared formula boundary materialization dirtied editor state");
    }

    sheet.set_cell(4, 6, CellValue::text("shared-formula-boundary-edit"));
    editor.save_as(report.output);
    return report;
}

Report run_generated_shared_formula_office_like_materialization(const CliOptions& options)
{
    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = write_shared_formula_office_like_source(resolve_generated_source(
        options, "fastxlsx-workbook-editor-qa-shared-formula-office-like-source.xlsx"));
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-shared-formula-office-like-output.xlsx");
    report.source_sheet_name = "OfficeLikeShared";
    report.mutations = {
        "worksheet(OfficeLikeShared).try_cell(C1:D3,F2:G3,E1):expect_office_like_shared_formulas",
        "worksheet(OfficeLikeShared).set_cell(H6,text)",
    };
    report.notes = {
        "Two shared formula si groups should coexist on the same worksheet",
        "2D shared formula refs should materialize every follower as ordinary formula text",
        "Interleaved ordinary formulas and values should survive dirty projection",
        "Stale cached formula values should be absent from dirty output",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    WorksheetEditor sheet = editor.worksheet("OfficeLikeShared");
    require_formula_cell(sheet, "C1", "A1+B1");
    require_formula_cell(sheet, "D1", "B1+C1");
    require_formula_cell(sheet, "C2", "A2+B2");
    require_formula_cell(sheet, "D2", "B2+C2");
    require_formula_cell(sheet, "C3", "A3+B3");
    require_formula_cell(sheet, "D3", "B3+C3");
    require_formula_cell(sheet, "E1", "A1*2");
    require_formula_cell(sheet, "F2", "SUM($A2:B2)+C$1");
    require_formula_cell(sheet, "G2", "SUM($A2:C2)+D$1");
    require_formula_cell(sheet, "F3", "SUM($A3:B3)+C$1");
    require_formula_cell(sheet, "G3", "SUM($A3:C3)+D$1");
    if (sheet.has_pending_changes() || editor.has_pending_changes()) {
        throw std::runtime_error(
            "read-only office-like shared formula materialization dirtied editor state");
    }

    sheet.set_cell(6, 8, CellValue::text("office-like-shared-formula-edit"));
    editor.save_as(report.output);
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
    report.image_part_name =
        options.image_part_name.empty() ? "xl/media/image1.png" : options.image_part_name;
    report.source = write_two_sheet_source_with_image(
        resolve_generated_source(options, "fastxlsx-workbook-editor-qa-image-source.xlsx"));
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-image-output.xlsx");
    report.replacement_image = resolve_replacement_image_path(options, report.image_part_name);
    report.mutations = {std::string("replace_image:") + report.image_part_name};
    report.notes = {
        "Pictures worksheet and drawing XML should remain readable",
        report.image_part_name + " bytes should match replacement_image",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    editor.replace_image(report.image_part_name, report.replacement_image);
    editor.save_as(report.output);
    return report;
}

Report run_fixture_image_replace(const CliOptions& options)
{
    if (options.source.empty()) {
        throw std::runtime_error("fixture_image_replace requires --source");
    }
    if (options.image_part_name.empty()) {
        throw std::runtime_error("fixture_image_replace requires --image-part");
    }

    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = options.source;
    report.output = resolve_output_path(
        options, "fastxlsx-workbook-editor-qa-fixture-image-output.xlsx");
    report.replacement_image =
        resolve_replacement_image_path(options, options.image_part_name);
    report.source_sheet_name = options.sheet_name;
    report.image_part_name = options.image_part_name;
    report.mutations = {std::string("replace_image:") + report.image_part_name};
    report.notes = {
        "The selected media part bytes should match replacement_image",
        "Unchanged workbook parts should remain copy-preserved",
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
    report.replacement_image =
        resolve_replacement_image_path(options, report.image_part_name);
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
    summarize_defined_name_audits(report, editor.defined_name_formula_reference_audits());
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
    summarize_defined_name_audits(report, editor.defined_name_formula_reference_audits());
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

Report run_fixture_source_formula_audit(const CliOptions& options)
{
    if (options.source.empty()) {
        throw std::runtime_error("fixture_source_formula_audit requires --source");
    }

    Report report;
    report.scenario = options.scenario;
    report.report_path = options.report;
    report.source = options.source;
    report.source_sheet_name = options.sheet_name;
    report.renamed_sheet_name = options.rename_to;
    report.mutations = {"source_formula_reference_audits:<fixture>"};
    report.notes = {
        "This scenario is read-only and does not write an output workbook",
        "Explicit source worksheet <f> formula text contributes audit references",
        "Source-order metadata-only shared formula followers are expanded when their definition is available",
    };

    WorkbookEditor editor = WorkbookEditor::open(report.source);
    if (!options.sheet_name.empty() && !options.rename_to.empty()) {
        editor.rename_sheet(options.sheet_name, options.rename_to);
        report.mutations.push_back(
            "rename_sheet:" + options.sheet_name + "->" + options.rename_to);
    }

    const std::vector<fastxlsx::WorkbookEditorFormulaReferenceAudit> audits =
        editor.source_formula_reference_audits();
    summarize_source_formula_audits(report, audits);
    return report;
}

Report run_scenario(const CliOptions& options)
{
    if (options.scenario == "generated_rename_materialized") {
        return run_generated_rename_materialized(options);
    }
    if (options.scenario == "generated_source_formula_audit") {
        return run_generated_source_formula_audit(options);
    }
    if (options.scenario == "generated_formula_rename_rewrite") {
        return run_generated_formula_rename_rewrite(options);
    }
    if (options.scenario == "generated_formula_rename_defined_names_only") {
        return run_generated_formula_rename_defined_names_only(options);
    }
    if (options.scenario == "generated_formula_rename_default_audit") {
        return run_generated_formula_rename_default_audit(options);
    }
    if (options.scenario == "generated_shared_formula_materialization") {
        return run_generated_shared_formula_materialization(options);
    }
    if (options.scenario == "generated_shared_formula_boundary_materialization") {
        return run_generated_shared_formula_boundary_materialization(options);
    }
    if (options.scenario == "generated_shared_formula_office_like_materialization") {
        return run_generated_shared_formula_office_like_materialization(options);
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
    if (options.scenario == "fixture_source_formula_audit") {
        return run_fixture_source_formula_audit(options);
    }
    if (options.scenario == "fixture_image_replace") {
        return run_fixture_image_replace(options);
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
