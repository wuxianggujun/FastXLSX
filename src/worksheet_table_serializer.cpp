#include <fastxlsx/detail/worksheet_table_serializer.hpp>

#include <fastxlsx/detail/xml.hpp>

#include <algorithm>
#include <set>

namespace fastxlsx::detail {
namespace {

constexpr std::uint32_t max_excel_rows = 1048576U;
constexpr std::uint32_t max_excel_columns = 16384U;

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
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
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
        column = column * 26U
            + static_cast<std::uint32_t>(ascii_lower(value[offset]) - 'a' + 1);
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
        row = row * 10U + static_cast<std::uint32_t>(value[offset] - '0');
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
            throw FastXlsxError(
                "table name must contain only ASCII letters, digits, and underscores");
        }
    }
    if (looks_like_excel_cell_reference(name)) {
        throw FastXlsxError("table name cannot look like an Excel cell reference");
    }
}

std::string_view table_totals_function_name(TableTotalsFunction function)
{
    switch (function) {
    case TableTotalsFunction::Sum:
        return "sum";
    case TableTotalsFunction::Count:
        return "count";
    case TableTotalsFunction::Average:
        return "average";
    case TableTotalsFunction::Maximum:
        return "max";
    case TableTotalsFunction::Minimum:
        return "min";
    case TableTotalsFunction::Product:
        return "product";
    case TableTotalsFunction::CountNumbers:
        return "countNums";
    case TableTotalsFunction::StandardDeviation:
        return "stdDev";
    case TableTotalsFunction::Variance:
        return "var";
    }
    throw FastXlsxError("unknown table totals function");
}

} // namespace

bool worksheet_table_ranges_overlap(CellRange left, CellRange right) noexcept
{
    return left.first_row <= right.last_row && right.first_row <= left.last_row
        && left.first_column <= right.last_column && right.first_column <= left.last_column;
}

bool worksheet_table_names_equal(
    std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
    }
    return true;
}

void validate_worksheet_table(CellRange range, const TableOptions& options)
{
    (void)range_reference(range);
    if (range.last_row == range.first_row) {
        throw FastXlsxError("table range must include at least one data row after the header");
    }
    if (options.show_totals_row && range.last_row <= range.first_row + 1U) {
        throw FastXlsxError(
            "table range with totals row metadata must include header, data, and totals rows");
    }
    validate_table_name(options.name);

    const std::uint32_t width = range.last_column - range.first_column + 1U;
    if (options.column_names.size() != width) {
        throw FastXlsxError("table column name count must match the table range width");
    }
    if (!options.column_totals_functions.empty()
        && options.column_totals_functions.size() != width) {
        throw FastXlsxError("table totals function count must match the table range width");
    }
    if (!options.column_totals_labels.empty()
        && options.column_totals_labels.size() != width) {
        throw FastXlsxError("table totals label count must match the table range width");
    }
    if (!options.show_totals_row
        && (!options.column_totals_functions.empty()
            || !options.column_totals_labels.empty())) {
        throw FastXlsxError("table totals metadata requires visible totals row metadata");
    }
    if (options.show_totals_row) {
        const bool has_totals_function = std::any_of(
            options.column_totals_functions.begin(),
            options.column_totals_functions.end(),
            [](const auto& function) { return function.has_value(); });
        if (!has_totals_function) {
            throw FastXlsxError("visible table totals rows require at least one totals function");
        }
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

std::string serialize_worksheet_table(
    CellRange range, const TableOptions& options, std::uint32_t table_id)
{
    validate_worksheet_table(range, options);
    if (table_id == 0) {
        throw FastXlsxError("worksheet table id must be positive");
    }

    std::string xml;
    xml += R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>)";
    xml += R"(<table xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" id=")";
    append_unsigned_decimal(xml, table_id);
    xml += R"(" name=")";
    append_escaped_xml_attribute(xml, options.name);
    xml += R"(" displayName=")";
    append_escaped_xml_attribute(xml, options.name);
    xml += R"(" ref=")";
    xml += range_reference(range);
    xml += options.show_totals_row ? R"(" totalsRowCount="1">)"
                                   : R"(" totalsRowShown="0">)";

    CellRange auto_filter_range = range;
    if (options.show_totals_row) {
        --auto_filter_range.last_row;
    }
    xml += R"(<autoFilter ref=")";
    xml += range_reference(auto_filter_range);
    xml += R"("/><tableColumns count=")";
    append_unsigned_decimal(xml, options.column_names.size());
    xml += R"(">)";
    for (std::size_t index = 0; index < options.column_names.size(); ++index) {
        xml += R"(<tableColumn id=")";
        append_unsigned_decimal(xml, index + 1U);
        xml += R"(" name=")";
        append_escaped_xml_attribute(xml, options.column_names[index]);
        if (!options.column_totals_labels.empty()
            && !options.column_totals_labels[index].empty()) {
            xml += R"(" totalsRowLabel=")";
            append_escaped_xml_attribute(xml, options.column_totals_labels[index]);
        }
        if (!options.column_totals_functions.empty()
            && options.column_totals_functions[index].has_value()) {
            xml += R"(" totalsRowFunction=")";
            xml += table_totals_function_name(*options.column_totals_functions[index]);
        }
        xml += R"("/>)";
    }
    xml += "</tableColumns>";
    if (!options.style_name.empty()) {
        xml += R"(<tableStyleInfo name=")";
        append_escaped_xml_attribute(xml, options.style_name);
        xml += R"(" showFirstColumn=")";
        xml += options.show_first_column ? "1" : "0";
        xml += R"(" showLastColumn=")";
        xml += options.show_last_column ? "1" : "0";
        xml += R"(" showRowStripes=")";
        xml += options.show_row_stripes ? "1" : "0";
        xml += R"(" showColumnStripes=")";
        xml += options.show_column_stripes ? "1" : "0";
        xml += R"("/>)";
    }
    xml += "</table>";
    return xml;
}

} // namespace fastxlsx::detail
