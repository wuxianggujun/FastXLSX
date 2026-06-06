#include <fastxlsx/streaming_writer.hpp>

#include <fastxlsx/detail/opc.hpp>
#include <fastxlsx/detail/xml.hpp>

#include "package_writer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <locale>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fastxlsx {
namespace {

constexpr std::uint32_t max_excel_rows = 1048576;
constexpr std::uint32_t max_excel_columns = 16384;

struct ColumnWidth {
    std::uint32_t first_column = 1;
    std::uint32_t last_column = 1;
    double width = 0.0;
};

struct DataValidation {
    CellRange range;
    DataValidationRule rule;
};

struct ExternalHyperlink {
    std::uint32_t row = 1;
    std::uint32_t column = 1;
    std::string target_url;
};

struct WorksheetTable {
    CellRange range;
    TableOptions options;
};

std::string format_number(double value)
{
    std::array<char, 64> buffer {};
    const auto [ptr, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error == std::errc()) {
        return {buffer.data(), ptr};
    }

    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(15) << value;
    return stream.str();
}

bool needs_space_preserve(std::string_view value)
{
    return !value.empty()
        && (value.front() == ' ' || value.front() == '\t' || value.front() == '\n'
            || value.front() == '\r' || value.back() == ' ' || value.back() == '\t'
            || value.back() == '\n' || value.back() == '\r');
}

void append_text_element(std::string& xml, std::string_view value)
{
    if (needs_space_preserve(value)) {
        xml += "<t xml:space=\"preserve\">";
    } else {
        xml += "<t>";
    }
    xml += detail::escape_xml_text(value);
    xml += "</t>";
}

void validate_sheet_name(std::string_view name)
{
    if (name.empty()) {
        throw FastXlsxError("worksheet name cannot be empty");
    }
    if (name.size() > 31) {
        throw FastXlsxError("worksheet name exceeds Excel's 31-character limit");
    }

    for (const char ch : name) {
        switch (ch) {
        case ':':
        case '\\':
        case '/':
        case '?':
        case '*':
        case '[':
        case ']':
            throw FastXlsxError("worksheet name contains a character rejected by Excel");
        default:
            break;
        }
    }
}

std::string_view data_validation_type_name(DataValidationType type)
{
    switch (type) {
    case DataValidationType::Whole:
        return "whole";
    case DataValidationType::Decimal:
        return "decimal";
    case DataValidationType::List:
        return "list";
    case DataValidationType::Date:
        return "date";
    case DataValidationType::Time:
        return "time";
    case DataValidationType::TextLength:
        return "textLength";
    case DataValidationType::Custom:
        return "custom";
    }

    throw FastXlsxError("unknown data validation type");
}

std::string_view data_validation_operator_name(DataValidationOperator operator_type)
{
    switch (operator_type) {
    case DataValidationOperator::Between:
        return "between";
    case DataValidationOperator::NotBetween:
        return "notBetween";
    case DataValidationOperator::Equal:
        return "equal";
    case DataValidationOperator::NotEqual:
        return "notEqual";
    case DataValidationOperator::GreaterThan:
        return "greaterThan";
    case DataValidationOperator::LessThan:
        return "lessThan";
    case DataValidationOperator::GreaterThanOrEqual:
        return "greaterThanOrEqual";
    case DataValidationOperator::LessThanOrEqual:
        return "lessThanOrEqual";
    }

    throw FastXlsxError("unknown data validation operator");
}

std::string worksheet_relationship_id(std::size_t index)
{
    return "rId" + std::to_string(index + 1);
}

bool is_ascii_letter(char ch) noexcept
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool is_ascii_digit(char ch) noexcept
{
    return ch >= '0' && ch <= '9';
}

char ascii_lower(char ch) noexcept
{
    if (ch >= 'A' && ch <= 'Z') {
        return static_cast<char>(ch - 'A' + 'a');
    }
    return ch;
}

std::string ascii_lower_copy(std::string_view value)
{
    std::string lowered;
    lowered.reserve(value.size());
    for (const char ch : value) {
        lowered.push_back(ascii_lower(ch));
    }
    return lowered;
}

bool looks_like_excel_cell_reference(std::string_view value)
{
    std::uint32_t column = 0;
    std::size_t offset = 0;
    while (offset < value.size() && is_ascii_letter(value[offset])) {
        column = column * 26 + static_cast<std::uint32_t>(ascii_lower(value[offset]) - 'a' + 1);
        if (column > max_excel_columns) {
            return false;
        }
        ++offset;
    }

    if (offset == 0 || offset == value.size()) {
        return false;
    }

    std::uint32_t row = 0;
    for (; offset < value.size(); ++offset) {
        if (!is_ascii_digit(value[offset])) {
            return false;
        }
        row = row * 10 + static_cast<std::uint32_t>(value[offset] - '0');
        if (row > max_excel_rows) {
            return false;
        }
    }

    return row > 0 && column > 0;
}

void validate_table_name(std::string_view name)
{
    if (name.empty()) {
        throw FastXlsxError("table name cannot be empty");
    }
    if (!(is_ascii_letter(name.front()) || name.front() == '_')) {
        throw FastXlsxError("table name must start with an ASCII letter or underscore");
    }
    for (const char ch : name) {
        if (!(is_ascii_letter(ch) || is_ascii_digit(ch) || ch == '_')) {
            throw FastXlsxError("table name must contain only ASCII letters, digits, and underscores");
        }
    }
    if (looks_like_excel_cell_reference(name)) {
        throw FastXlsxError("table name cannot look like an Excel cell reference");
    }
}

std::uint32_t range_width(CellRange range)
{
    return range.last_column - range.first_column + 1;
}

void validate_table_options(CellRange range, const TableOptions& options)
{
    (void)detail::range_reference(range);
    if (range.last_row == range.first_row) {
        throw FastXlsxError("table range must include at least one data row after the header");
    }
    validate_table_name(options.name);

    const std::uint32_t width = range_width(range);
    if (options.column_names.size() != width) {
        throw FastXlsxError("table column name count must match the table range width");
    }

    std::set<std::string> seen_column_names;
    for (const std::string& column_name : options.column_names) {
        if (column_name.empty()) {
            throw FastXlsxError("table column names cannot be empty");
        }
        if (!seen_column_names.insert(ascii_lower_copy(column_name)).second) {
            throw FastXlsxError("table column names must be unique within a table");
        }
    }
}

bool data_validation_operator_requires_formula2(DataValidationOperator operator_type) noexcept
{
    return operator_type == DataValidationOperator::Between
        || operator_type == DataValidationOperator::NotBetween;
}

void validate_data_validation_rule(const DataValidationRule& rule)
{
    if (rule.formula1.empty()) {
        throw FastXlsxError("data validation formula1 cannot be empty");
    }

    if (rule.type == DataValidationType::List || rule.type == DataValidationType::Custom) {
        if (rule.operator_type.has_value()) {
            throw FastXlsxError("list and custom data validations do not accept an operator");
        }
        if (!rule.formula2.empty()) {
            throw FastXlsxError("list and custom data validations do not accept formula2");
        }
        return;
    }

    if (!rule.operator_type.has_value()) {
        throw FastXlsxError("data validation operator is required for this type");
    }

    if (data_validation_operator_requires_formula2(*rule.operator_type)) {
        if (rule.formula2.empty()) {
            throw FastXlsxError("between data validations require formula2");
        }
    } else if (!rule.formula2.empty()) {
        throw FastXlsxError("single-formula data validation operator cannot use formula2");
    }
}

std::filesystem::path make_temp_path()
{
    static std::atomic<std::uint64_t> counter {0};
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    return std::filesystem::temp_directory_path()
        / ("fastxlsx-stream-" + std::to_string(tick) + "-" + std::to_string(id) + ".xml");
}

} // namespace

namespace detail {

struct WorkbookWriterState;

struct SharedStringTable {
    std::size_t count = 0;
    std::vector<std::string> values;
    std::unordered_map<std::string, std::size_t> index_by_value;
};

struct WorksheetWriterState {
    WorksheetWriterState(std::string sheet_name, WorkbookWriterState* workbook_state)
        : workbook(workbook_state)
        , name(std::move(sheet_name))
        , body_path(make_temp_path())
        , body(body_path, std::ios::binary)
    {
        if (!body) {
            throw FastXlsxError("failed to create temporary worksheet XML");
        }
    }

    ~WorksheetWriterState()
    {
        if (body.is_open()) {
            body.close();
        }
        std::error_code ignored;
        std::filesystem::remove(body_path, ignored);
    }

    WorksheetWriterState(const WorksheetWriterState&) = delete;
    WorksheetWriterState& operator=(const WorksheetWriterState&) = delete;

    WorkbookWriterState* workbook = nullptr;
    std::string name;
    std::filesystem::path body_path;
    std::ofstream body;
    std::uint32_t row_count = 0;
    std::uint32_t max_column = 0;
    std::vector<ColumnWidth> column_widths;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> frozen_panes;
    std::optional<CellRange> auto_filter;
    std::vector<CellRange> merged_ranges;
    std::vector<DataValidation> data_validations;
    std::vector<ExternalHyperlink> external_hyperlinks;
    std::vector<WorksheetTable> tables;
    std::string row_buffer;
};

struct WorkbookWriterState {
    std::filesystem::path output_path;
    WorkbookWriterOptions options;
    std::vector<std::unique_ptr<WorksheetWriterState>> worksheets;
    SharedStringTable shared_strings;
    bool closed = false;
};

} // namespace detail

namespace {

bool uses_shared_strings(const detail::WorkbookWriterState& workbook) noexcept
{
    return workbook.options.string_strategy == StringStrategy::SharedString;
}

void ensure_mutable_worksheet(const detail::WorksheetWriterState* state)
{
    if (state == nullptr) {
        throw FastXlsxError("worksheet writer is not attached to a workbook");
    }
    if (state->workbook != nullptr && state->workbook->closed) {
        throw FastXlsxError("cannot modify worksheet after workbook close");
    }
}

bool worksheet_has_relationships(const detail::WorksheetWriterState& worksheet) noexcept
{
    return !worksheet.external_hyperlinks.empty() || !worksheet.tables.empty();
}

bool workbook_has_table_name(
    const detail::WorkbookWriterState& workbook, std::string_view table_name) noexcept
{
    const std::string candidate = ascii_lower_copy(table_name);
    for (const auto& worksheet : workbook.worksheets) {
        for (const WorksheetTable& table : worksheet->tables) {
            if (ascii_lower_copy(table.options.name) == candidate) {
                return true;
            }
        }
    }
    return false;
}

std::size_t shared_string_index(detail::WorkbookWriterState& workbook, std::string_view value)
{
    auto& table = workbook.shared_strings;
    ++table.count;

    std::string key(value);
    if (const auto existing = table.index_by_value.find(key);
        existing != table.index_by_value.end()) {
        return existing->second;
    }

    const std::size_t index = table.values.size();
    table.values.push_back(key);
    table.index_by_value.emplace(std::move(key), index);
    return index;
}

void write_cell(std::string& xml, std::uint32_t row, std::uint32_t column, const CellView& cell,
    detail::WorksheetWriterState& worksheet)
{
    const std::string reference = detail::cell_reference(row, column);
    switch (cell.type()) {
    case CellView::Type::Number:
        xml += "<c r=\"";
        xml += reference;
        xml += "\"><v>";
        xml += format_number(cell.number_value());
        xml += "</v></c>";
        break;
    case CellView::Type::String:
        xml += "<c r=\"";
        xml += reference;
        if (worksheet.workbook != nullptr && uses_shared_strings(*worksheet.workbook)) {
            xml += "\" t=\"s\"><v>";
            xml += std::to_string(shared_string_index(*worksheet.workbook, cell.text_value()));
            xml += "</v></c>";
        } else {
            xml += "\" t=\"inlineStr\"><is>";
            append_text_element(xml, cell.text_value());
            xml += "</is></c>";
        }
        break;
    case CellView::Type::Boolean:
        xml += "<c r=\"";
        xml += reference;
        xml += "\" t=\"b\"><v>";
        xml += cell.boolean_value() ? "1" : "0";
        xml += "</v></c>";
        break;
    case CellView::Type::Formula:
        xml += "<c r=\"";
        xml += reference;
        xml += "\"><f>";
        xml += detail::escape_xml_text(cell.text_value());
        xml += "</f></c>";
        break;
    }
}

std::string worksheet_dimension(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.row_count == 0 || worksheet.max_column == 0) {
        return "A1";
    }
    return "A1:" + detail::cell_reference(worksheet.row_count, worksheet.max_column);
}

std::string build_workbook(const std::vector<std::unique_ptr<detail::WorksheetWriterState>>& worksheets)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" )";
    xml += R"(xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">)";
    xml += "<sheets>";
    for (std::size_t index = 0; index < worksheets.size(); ++index) {
        xml += R"(<sheet name=")";
        xml += detail::escape_xml_attribute(worksheets[index]->name);
        xml += R"(" sheetId=")";
        xml += std::to_string(index + 1);
        xml += R"(" r:id="rId)";
        xml += std::to_string(index + 1);
        xml += R"("/>)";
    }
    xml += "</sheets></workbook>";
    return xml;
}

void write_stream(std::ofstream& stream, std::string_view xml, const char* error_message)
{
    stream.write(xml.data(), static_cast<std::streamsize>(xml.size()));
    if (!stream) {
        throw FastXlsxError(error_message);
    }
}

void write_shared_strings_file(
    const std::filesystem::path& path, const detail::SharedStringTable& shared_strings)
{
    std::ofstream stream(path, std::ios::binary);
    if (!stream) {
        throw FastXlsxError("failed to create temporary sharedStrings XML");
    }

    write_stream(stream,
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)",
        "failed to write temporary sharedStrings XML");
    write_stream(stream,
        R"(<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" count=")",
        "failed to write temporary sharedStrings XML");
    write_stream(stream, std::to_string(shared_strings.count),
        "failed to write temporary sharedStrings XML");
    write_stream(stream, R"(" uniqueCount=")", "failed to write temporary sharedStrings XML");
    write_stream(stream, std::to_string(shared_strings.values.size()),
        "failed to write temporary sharedStrings XML");
    write_stream(stream, R"(">)", "failed to write temporary sharedStrings XML");

    for (const std::string& value : shared_strings.values) {
        std::string item_xml;
        item_xml.reserve(value.size() + 32);
        item_xml += "<si>";
        append_text_element(item_xml, value);
        item_xml += "</si>";
        write_stream(stream, item_xml, "failed to write temporary sharedStrings XML");
    }

    write_stream(stream, "</sst>", "failed to write temporary sharedStrings XML");
}

std::string build_sheet_views(const detail::WorksheetWriterState& worksheet)
{
    if (!worksheet.frozen_panes.has_value()) {
        return {};
    }

    const auto [row_split, column_split] = *worksheet.frozen_panes;
    std::string xml;
    xml += "<sheetViews><sheetView workbookViewId=\"0\"><pane";
    if (column_split > 0) {
        xml += " xSplit=\"";
        xml += std::to_string(column_split);
        xml += "\"";
    }
    if (row_split > 0) {
        xml += " ySplit=\"";
        xml += std::to_string(row_split);
        xml += "\"";
    }
    xml += " topLeftCell=\"";
    xml += detail::cell_reference(row_split + 1, column_split + 1);
    xml += "\" activePane=\"bottomRight\" state=\"frozen\"/></sheetView></sheetViews>";
    return xml;
}

std::string build_columns(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.column_widths.empty()) {
        return {};
    }

    std::string xml = "<cols>";
    for (const ColumnWidth& width : worksheet.column_widths) {
        xml += "<col min=\"";
        xml += std::to_string(width.first_column);
        xml += "\" max=\"";
        xml += std::to_string(width.last_column);
        xml += "\" width=\"";
        xml += format_number(width.width);
        xml += "\" customWidth=\"1\"/>";
    }
    xml += "</cols>";
    return xml;
}

std::string build_merge_cells(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.merged_ranges.empty()) {
        return {};
    }

    std::string xml = "<mergeCells count=\"";
    xml += std::to_string(worksheet.merged_ranges.size());
    xml += "\">";
    for (const CellRange& range : worksheet.merged_ranges) {
        xml += "<mergeCell ref=\"";
        xml += detail::range_reference(range);
        xml += "\"/>";
    }
    xml += "</mergeCells>";
    return xml;
}

std::string build_data_validations(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.data_validations.empty()) {
        return {};
    }

    std::string xml = "<dataValidations count=\"";
    xml += std::to_string(worksheet.data_validations.size());
    xml += "\">";
    for (const DataValidation& validation : worksheet.data_validations) {
        xml += "<dataValidation type=\"";
        xml += data_validation_type_name(validation.rule.type);
        xml += "\"";
        if (validation.rule.allow_blank) {
            xml += " allowBlank=\"1\"";
        }
        if (validation.rule.operator_type.has_value()) {
            xml += " operator=\"";
            xml += data_validation_operator_name(*validation.rule.operator_type);
            xml += "\"";
        }
        xml += " sqref=\"";
        xml += detail::range_reference(validation.range);
        xml += "\"><formula1>";
        xml += detail::escape_xml_text(validation.rule.formula1);
        xml += "</formula1>";
        if (!validation.rule.formula2.empty()) {
            xml += "<formula2>";
            xml += detail::escape_xml_text(validation.rule.formula2);
            xml += "</formula2>";
        }
        xml += "</dataValidation>";
    }
    xml += "</dataValidations>";
    return xml;
}

std::string build_hyperlinks(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.external_hyperlinks.empty()) {
        return {};
    }

    std::string xml = "<hyperlinks>";
    for (std::size_t index = 0; index < worksheet.external_hyperlinks.size(); ++index) {
        const ExternalHyperlink& hyperlink = worksheet.external_hyperlinks[index];
        xml += "<hyperlink ref=\"";
        xml += detail::cell_reference(hyperlink.row, hyperlink.column);
        xml += "\" r:id=\"";
        xml += worksheet_relationship_id(index);
        xml += "\"/>";
    }
    xml += "</hyperlinks>";
    return xml;
}

std::string table_relationship_id(const detail::WorksheetWriterState& worksheet, std::size_t table_index)
{
    return worksheet_relationship_id(worksheet.external_hyperlinks.size() + table_index);
}

std::string build_table_parts(const detail::WorksheetWriterState& worksheet)
{
    if (worksheet.tables.empty()) {
        return {};
    }

    std::string xml = "<tableParts count=\"";
    xml += std::to_string(worksheet.tables.size());
    xml += "\">";
    for (std::size_t index = 0; index < worksheet.tables.size(); ++index) {
        xml += "<tablePart r:id=\"";
        xml += table_relationship_id(worksheet, index);
        xml += "\"/>";
    }
    xml += "</tableParts>";
    return xml;
}

std::string build_worksheet_prefix(const detail::WorksheetWriterState& worksheet)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main")";
    if (worksheet_has_relationships(worksheet)) {
        xml += R"( xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships")";
    }
    xml += ">";
    xml += R"(<dimension ref=")";
    xml += worksheet_dimension(worksheet);
    xml += "\"/>";
    xml += build_sheet_views(worksheet);
    xml += build_columns(worksheet);
    xml += "<sheetData>";
    return xml;
}

std::string build_worksheet_suffix(const detail::WorksheetWriterState& worksheet)
{
    std::string xml;
    xml += "</sheetData>";
    if (worksheet.auto_filter.has_value()) {
        xml += "<autoFilter ref=\"";
        xml += detail::range_reference(*worksheet.auto_filter);
        xml += "\"/>";
    }
    xml += build_merge_cells(worksheet);
    xml += build_data_validations(worksheet);
    xml += build_hyperlinks(worksheet);
    xml += build_table_parts(worksheet);
    xml += "</worksheet>";
    return xml;
}

detail::PackageEntry build_worksheet_entry(std::string entry_name, detail::WorksheetWriterState& worksheet)
{
    if (worksheet.body.is_open()) {
        worksheet.body.close();
    }

    std::vector<detail::PackageEntryChunk> chunks;
    chunks.reserve(3);
    chunks.push_back(detail::PackageEntryChunk::memory(build_worksheet_prefix(worksheet)));
    chunks.push_back(detail::PackageEntryChunk::file(worksheet.body_path));
    chunks.push_back(detail::PackageEntryChunk::memory(build_worksheet_suffix(worksheet)));
    return {std::move(entry_name), std::move(chunks)};
}

std::string table_entry_name(std::size_t table_index)
{
    return "xl/tables/table" + std::to_string(table_index + 1) + ".xml";
}

std::string build_worksheet_relationships(
    const detail::WorksheetWriterState& worksheet, std::size_t first_table_index)
{
    detail::RelationshipSet relationships;
    constexpr std::string_view hyperlink_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink";
    constexpr std::string_view table_type =
        "http://schemas.openxmlformats.org/officeDocument/2006/relationships/table";

    for (std::size_t index = 0; index < worksheet.external_hyperlinks.size(); ++index) {
        relationships.add(worksheet_relationship_id(index),
            std::string(hyperlink_type),
            worksheet.external_hyperlinks[index].target_url,
            detail::Relationship::TargetMode::External);
    }

    for (std::size_t index = 0; index < worksheet.tables.size(); ++index) {
        relationships.add(table_relationship_id(worksheet, index),
            std::string(table_type),
            "../tables/table" + std::to_string(first_table_index + index + 1) + ".xml");
    }

    return detail::serialize_relationships(relationships);
}

std::string worksheet_relationship_entry_name(std::size_t worksheet_index)
{
    const std::string sheet_name = "sheet" + std::to_string(worksheet_index + 1) + ".xml";
    return "xl/worksheets/_rels/" + sheet_name + ".rels";
}

std::string build_table_xml(const WorksheetTable& table, std::size_t table_index)
{
    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id=")";
    xml += std::to_string(table_index + 1);
    xml += R"(" name=")";
    xml += detail::escape_xml_attribute(table.options.name);
    xml += R"(" displayName=")";
    xml += detail::escape_xml_attribute(table.options.name);
    xml += R"(" ref=")";
    xml += detail::range_reference(table.range);
    xml += R"(" totalsRowShown="0">)";
    xml += R"(<autoFilter ref=")";
    xml += detail::range_reference(table.range);
    xml += R"("/>)";
    xml += R"(<tableColumns count=")";
    xml += std::to_string(table.options.column_names.size());
    xml += R"(">)";
    for (std::size_t index = 0; index < table.options.column_names.size(); ++index) {
        xml += R"(<tableColumn id=")";
        xml += std::to_string(index + 1);
        xml += R"(" name=")";
        xml += detail::escape_xml_attribute(table.options.column_names[index]);
        xml += R"("/>)";
    }
    xml += "</tableColumns>";
    if (!table.options.style_name.empty()) {
        xml += R"(<tableStyleInfo name=")";
        xml += detail::escape_xml_attribute(table.options.style_name);
        xml += R"(" showFirstColumn=")";
        xml += table.options.show_first_column ? "1" : "0";
        xml += R"(" showLastColumn=")";
        xml += table.options.show_last_column ? "1" : "0";
        xml += R"(" showRowStripes=")";
        xml += table.options.show_row_stripes ? "1" : "0";
        xml += R"(" showColumnStripes=")";
        xml += table.options.show_column_stripes ? "1" : "0";
        xml += R"("/>)";
    }
    xml += "</table>";
    return xml;
}

std::size_t count_tables(const std::vector<std::unique_ptr<detail::WorksheetWriterState>>& worksheets)
{
    std::size_t count = 0;
    for (const auto& worksheet : worksheets) {
        count += worksheet->tables.size();
    }
    return count;
}

std::vector<std::size_t> table_start_indexes(
    const std::vector<std::unique_ptr<detail::WorksheetWriterState>>& worksheets)
{
    std::vector<std::size_t> starts;
    starts.reserve(worksheets.size());
    std::size_t next = 0;
    for (const auto& worksheet : worksheets) {
        starts.push_back(next);
        next += worksheet->tables.size();
    }
    return starts;
}

class TemporaryFile {
public:
    TemporaryFile()
        : path_(make_temp_path())
    {
    }

    ~TemporaryFile()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    TemporaryFile(TemporaryFile&& other) noexcept
        : path_(std::move(other.path_))
    {
        other.path_.clear();
    }

    TemporaryFile& operator=(TemporaryFile&& other) noexcept
    {
        if (this != &other) {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
            path_ = std::move(other.path_);
            other.path_.clear();
        }
        return *this;
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

} // namespace

CellView CellView::number(double value) noexcept
{
    CellView cell;
    cell.type_ = Type::Number;
    cell.number_value_ = value;
    return cell;
}

CellView CellView::text(std::string_view value) noexcept
{
    CellView cell;
    cell.type_ = Type::String;
    cell.text_value_ = value;
    return cell;
}

CellView CellView::boolean(bool value) noexcept
{
    CellView cell;
    cell.type_ = Type::Boolean;
    cell.boolean_value_ = value;
    return cell;
}

CellView CellView::formula(std::string_view value) noexcept
{
    CellView cell;
    cell.type_ = Type::Formula;
    cell.text_value_ = value;
    return cell;
}

CellView::Type CellView::type() const noexcept
{
    return type_;
}

double CellView::number_value() const noexcept
{
    return number_value_;
}

std::string_view CellView::text_value() const noexcept
{
    return text_value_;
}

bool CellView::boolean_value() const noexcept
{
    return boolean_value_;
}

WorksheetWriter::WorksheetWriter() noexcept = default;

WorksheetWriter::WorksheetWriter(detail::WorksheetWriterState* state) noexcept
    : state_(state)
{
}

void WorksheetWriter::append_row(std::span<const CellView> cells, RowOptions options)
{
    ensure_mutable_worksheet(state_);
    if (cells.size() > max_excel_columns) {
        throw FastXlsxError("row exceeds Excel's column limit");
    }
    if (state_->row_count >= max_excel_rows) {
        throw FastXlsxError("worksheet exceeds Excel's row limit");
    }
    if (options.height.has_value() && *options.height <= 0.0) {
        throw FastXlsxError("row height must be positive");
    }

    ++state_->row_count;
    state_->max_column = std::max(state_->max_column, static_cast<std::uint32_t>(cells.size()));

    std::string& row_xml = state_->row_buffer;
    row_xml.clear();
    const std::size_t expected_row_size = 32 + cells.size() * 48;
    if (row_xml.capacity() < expected_row_size) {
        row_xml.reserve(expected_row_size);
    }

    row_xml += "<row r=\"";
    row_xml += std::to_string(state_->row_count);
    if (options.height.has_value()) {
        row_xml += "\" ht=\"";
        row_xml += format_number(*options.height);
        row_xml += "\" customHeight=\"1";
    }
    row_xml += "\">";
    for (std::uint32_t column = 0; column < cells.size(); ++column) {
        write_cell(row_xml, state_->row_count, column + 1, cells[column], *state_);
    }
    row_xml += "</row>";

    state_->body.write(row_xml.data(), static_cast<std::streamsize>(row_xml.size()));
    if (!state_->body) {
        throw FastXlsxError("failed to write worksheet row XML");
    }
}

void WorksheetWriter::append_row(std::initializer_list<CellView> cells, RowOptions options)
{
    append_row(std::span<const CellView>(cells.begin(), cells.size()), options);
}

void WorksheetWriter::set_column_width(std::uint32_t first_column, std::uint32_t last_column, double width)
{
    ensure_mutable_worksheet(state_);
    if (first_column == 0 || last_column == 0 || first_column > last_column
        || last_column > max_excel_columns || width <= 0.0) {
        throw FastXlsxError("invalid column width range");
    }
    state_->column_widths.push_back({first_column, last_column, width});
}

void WorksheetWriter::freeze_panes(std::uint32_t row_split, std::uint32_t column_split)
{
    ensure_mutable_worksheet(state_);
    if (row_split > max_excel_rows || column_split > max_excel_columns) {
        throw FastXlsxError("invalid freeze pane split");
    }
    state_->frozen_panes = std::make_pair(row_split, column_split);
}

void WorksheetWriter::set_auto_filter(CellRange range)
{
    ensure_mutable_worksheet(state_);
    (void)detail::range_reference(range);
    state_->auto_filter = range;
}

void WorksheetWriter::merge_cells(CellRange range)
{
    ensure_mutable_worksheet(state_);
    (void)detail::range_reference(range);
    if (range.first_row == range.last_row && range.first_column == range.last_column) {
        throw FastXlsxError("merged range must include more than one cell");
    }
    state_->merged_ranges.push_back(range);
}

void WorksheetWriter::add_data_validation(CellRange range, DataValidationRule rule)
{
    ensure_mutable_worksheet(state_);
    (void)detail::range_reference(range);
    validate_data_validation_rule(rule);
    state_->data_validations.push_back({range, std::move(rule)});
}

void WorksheetWriter::add_external_hyperlink(
    std::uint32_t row, std::uint32_t column, std::string target_url)
{
    ensure_mutable_worksheet(state_);
    (void)detail::cell_reference(row, column);
    if (target_url.empty()) {
        throw FastXlsxError("external hyperlink target URL cannot be empty");
    }
    state_->external_hyperlinks.push_back({row, column, std::move(target_url)});
}

void WorksheetWriter::add_table(CellRange range, TableOptions options)
{
    ensure_mutable_worksheet(state_);
    validate_table_options(range, options);
    if (state_->workbook != nullptr && workbook_has_table_name(*state_->workbook, options.name)) {
        throw FastXlsxError("table names must be unique within a workbook");
    }
    state_->tables.push_back({range, std::move(options)});
}

WorkbookWriter::WorkbookWriter() = default;
WorkbookWriter::~WorkbookWriter() = default;
WorkbookWriter::WorkbookWriter(WorkbookWriter&&) noexcept = default;
WorkbookWriter& WorkbookWriter::operator=(WorkbookWriter&&) noexcept = default;

WorkbookWriter::WorkbookWriter(std::unique_ptr<detail::WorkbookWriterState> state) noexcept
    : state_(std::move(state))
{
}

WorkbookWriter WorkbookWriter::create(std::filesystem::path path, WorkbookWriterOptions options)
{
    auto state = std::make_unique<detail::WorkbookWriterState>();
    state->output_path = std::move(path);
    state->options = options;
    return WorkbookWriter(std::move(state));
}

WorksheetWriter WorkbookWriter::add_worksheet(std::string name)
{
    if (!state_) {
        throw FastXlsxError("workbook writer is not initialized");
    }
    if (state_->closed) {
        throw FastXlsxError("cannot add worksheet after close");
    }
    validate_sheet_name(name);

    std::set<std::string> existing_names;
    for (const auto& worksheet : state_->worksheets) {
        existing_names.insert(worksheet->name);
    }
    if (existing_names.contains(name)) {
        throw FastXlsxError("worksheet names must be unique");
    }

    state_->worksheets.push_back(
        std::make_unique<detail::WorksheetWriterState>(std::move(name), state_.get()));
    return WorksheetWriter(state_->worksheets.back().get());
}

void WorkbookWriter::close()
{
    if (!state_) {
        throw FastXlsxError("workbook writer is not initialized");
    }
    if (state_->closed) {
        return;
    }
    if (state_->worksheets.empty()) {
        throw FastXlsxError("workbook must contain at least one worksheet");
    }
    std::vector<detail::PackageEntry> entries;
    const bool write_shared_strings = uses_shared_strings(*state_);
    detail::PackageManifest manifest =
        detail::make_minimal_workbook_manifest(state_->worksheets.size(), write_shared_strings);
    const std::size_t table_count = count_tables(state_->worksheets);
    constexpr std::string_view content_type_table =
        "application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml";
    for (std::size_t index = 0; index < table_count; ++index) {
        manifest.add_part(
            detail::PartName("/" + table_entry_name(index)), std::string(content_type_table))
            .set_write_mode(detail::PartWriteMode::GenerateSmallXml);
    }

    const auto* workbook_relationships =
        manifest.relationships_for(detail::PartName("/xl/workbook.xml"));
    if (workbook_relationships == nullptr) {
        throw FastXlsxError("workbook relationships missing from package manifest");
    }

    const auto worksheet_relationship_count =
        std::count_if(state_->worksheets.begin(), state_->worksheets.end(),
            [](const auto& worksheet) { return worksheet_has_relationships(*worksheet); });
    entries.reserve(6 + state_->worksheets.size() + worksheet_relationship_count
        + table_count + (write_shared_strings ? 1 : 0));
    entries.push_back({"[Content_Types].xml", detail::serialize_content_types(manifest.content_types())});
    entries.push_back({"_rels/.rels", detail::serialize_relationships(manifest.package_relationships())});
    entries.push_back({"docProps/core.xml", detail::build_core_properties()});
    entries.push_back({"docProps/app.xml", detail::build_extended_properties()});
    entries.push_back({"xl/workbook.xml", build_workbook(state_->worksheets)});
    entries.push_back(
        {"xl/_rels/workbook.xml.rels", detail::serialize_relationships(*workbook_relationships)});

    std::optional<TemporaryFile> shared_strings_file;
    if (write_shared_strings) {
        shared_strings_file.emplace();
        write_shared_strings_file(shared_strings_file->path(), state_->shared_strings);
        entries.emplace_back("xl/sharedStrings.xml",
            std::vector<detail::PackageEntryChunk> {
                detail::PackageEntryChunk::file(shared_strings_file->path())});
    }

    const std::vector<std::size_t> first_table_indexes = table_start_indexes(state_->worksheets);
    for (std::size_t index = 0; index < state_->worksheets.size(); ++index) {
        entries.push_back(build_worksheet_entry(
            "xl/worksheets/sheet" + std::to_string(index + 1) + ".xml",
            *state_->worksheets[index]));
        if (worksheet_has_relationships(*state_->worksheets[index])) {
            entries.emplace_back(worksheet_relationship_entry_name(index),
                build_worksheet_relationships(*state_->worksheets[index], first_table_indexes[index]));
        }
        for (std::size_t table_index = 0; table_index < state_->worksheets[index]->tables.size();
            ++table_index) {
            const std::size_t package_table_index = first_table_indexes[index] + table_index;
            entries.emplace_back(table_entry_name(package_table_index),
                build_table_xml(state_->worksheets[index]->tables[table_index], package_table_index));
        }
    }

    detail::write_package(state_->output_path, entries);
    state_->closed = true;
}

} // namespace fastxlsx
