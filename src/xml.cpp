#include <fastxlsx/detail/xml.hpp>

#include <array>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

#include <fastxlsx/workbook.hpp>

namespace {

constexpr std::uint32_t max_excel_rows = 1048576;
constexpr std::uint32_t max_excel_columns = 16384;

void validate_finite_number(double value)
{
    if (!std::isfinite(value)) {
        throw fastxlsx::FastXlsxError("numeric values must be finite");
    }
}

bool append_number_with_to_chars(std::string& output, double value)
{
    std::array<char, 64> buffer {};
    const auto [ptr, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc()) {
        return false;
    }
    output.append(buffer.data(), ptr);
    return true;
}

void append_unsigned_decimal_unchecked(std::string& output, std::uint32_t value)
{
    std::array<char, 10> buffer {};
    const auto [ptr, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc()) {
        throw fastxlsx::FastXlsxError("failed to format unsigned integer");
    }
    output.append(buffer.data(), ptr);
}

std::string fallback_format_number(double value)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(15) << value;
    return stream.str();
}

void validate_cell_coordinate(std::uint32_t row, std::uint32_t column)
{
    if (row == 0 || column == 0) {
        throw fastxlsx::FastXlsxError("cell references are 1-based");
    }
    if (row > max_excel_rows || column > max_excel_columns) {
        throw fastxlsx::FastXlsxError("cell reference exceeds Excel worksheet limits");
    }
}

void append_column_reference(std::string& output, std::uint32_t column)
{
    std::array<char, 8> letters {};
    std::size_t count = 0;
    while (column > 0) {
        --column;
        letters[count] = static_cast<char>('A' + (column % 26));
        ++count;
        column /= 26;
    }
    while (count > 0) {
        --count;
        output.push_back(letters[count]);
    }
}

void append_cell_reference_unchecked(
    std::string& output, std::uint32_t row, std::uint32_t column)
{
    append_column_reference(output, column);
    append_unsigned_decimal_unchecked(output, row);
}

} // namespace

namespace fastxlsx::detail {

std::string escape_xml_text(std::string_view value)
{
    std::string escaped;
    append_escaped_xml_text(escaped, value);
    return escaped;
}

std::string escape_xml_attribute(std::string_view value)
{
    std::string escaped;
    append_escaped_xml_attribute(escaped, value);
    return escaped;
}

void append_escaped_xml_text(std::string& output, std::string_view value)
{
    output.reserve(output.size() + value.size());

    for (const char ch : value) {
        switch (ch) {
        case '&':
            output += "&amp;";
            break;
        case '<':
            output += "&lt;";
            break;
        case '>':
            output += "&gt;";
            break;
        default:
            output.push_back(ch);
            break;
        }
    }
}

void append_escaped_xml_attribute(std::string& output, std::string_view value)
{
    output.reserve(output.size() + value.size());

    for (const char ch : value) {
        switch (ch) {
        case '&':
            output += "&amp;";
            break;
        case '<':
            output += "&lt;";
            break;
        case '>':
            output += "&gt;";
            break;
        case '"':
            output += "&quot;";
            break;
        case '\'':
            output += "&apos;";
            break;
        default:
            output.push_back(ch);
            break;
        }
    }
}

std::string format_number(double value)
{
    validate_finite_number(value);

    std::string formatted;
    if (append_number_with_to_chars(formatted, value)) {
        return formatted;
    }

    return fallback_format_number(value);
}

void append_number(std::string& output, double value)
{
    validate_finite_number(value);
    if (append_number_with_to_chars(output, value)) {
        return;
    }

    output += fallback_format_number(value);
}

void append_unsigned_decimal(std::string& output, std::uint32_t value)
{
    append_unsigned_decimal_unchecked(output, value);
}

void append_cell_reference(std::string& output, std::uint32_t row, std::uint32_t column)
{
    validate_cell_coordinate(row, column);
    append_cell_reference_unchecked(output, row, column);
}

std::string cell_reference(std::uint32_t row, std::uint32_t column)
{
    std::string reference;
    reference.reserve(8);
    append_cell_reference(reference, row, column);
    return reference;
}

std::string range_reference(std::uint32_t first_row,
    std::uint32_t first_column,
    std::uint32_t last_row,
    std::uint32_t last_column)
{
    validate_cell_coordinate(first_row, first_column);
    validate_cell_coordinate(last_row, last_column);
    if (first_row > last_row || first_column > last_column) {
        throw FastXlsxError("cell range cannot be reversed");
    }

    std::string reference;
    reference.reserve(17);
    append_cell_reference_unchecked(reference, first_row, first_column);
    if (first_row != last_row || first_column != last_column) {
        reference.push_back(':');
        append_cell_reference_unchecked(reference, last_row, last_column);
    }
    return reference;
}

std::string range_reference(const CellRange& range)
{
    return range_reference(range.first_row, range.first_column, range.last_row, range.last_column);
}

std::string sqref(std::span<const CellRange> ranges)
{
    std::string reference;
    for (const CellRange& range : ranges) {
        if (!reference.empty()) {
            reference.push_back(' ');
        }
        reference += range_reference(range);
    }
    return reference;
}

} // namespace fastxlsx::detail
