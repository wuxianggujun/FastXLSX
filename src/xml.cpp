#include <fastxlsx/detail/xml.hpp>

#include <algorithm>
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

void validate_cell_coordinate(std::uint32_t row, std::uint32_t column)
{
    if (row == 0 || column == 0) {
        throw fastxlsx::FastXlsxError("cell references are 1-based");
    }
    if (row > max_excel_rows || column > max_excel_columns) {
        throw fastxlsx::FastXlsxError("cell reference exceeds Excel worksheet limits");
    }
}

} // namespace

namespace fastxlsx::detail {

std::string escape_xml_text(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());

    for (const char ch : value) {
        switch (ch) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

std::string escape_xml_attribute(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());

    for (const char ch : value) {
        switch (ch) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '\'':
            escaped += "&apos;";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }

    return escaped;
}

std::string format_number(double value)
{
    if (!std::isfinite(value)) {
        throw FastXlsxError("numeric values must be finite");
    }

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

std::string cell_reference(std::uint32_t row, std::uint32_t column)
{
    validate_cell_coordinate(row, column);

    std::string letters;
    while (column > 0) {
        --column;
        letters.push_back(static_cast<char>('A' + (column % 26)));
        column /= 26;
    }

    std::reverse(letters.begin(), letters.end());
    return letters + std::to_string(row);
}

std::string range_reference(std::uint32_t first_row,
    std::uint32_t first_column,
    std::uint32_t last_row,
    std::uint32_t last_column)
{
    const std::string first = cell_reference(first_row, first_column);
    const std::string last = cell_reference(last_row, last_column);

    if (first_row > last_row || first_column > last_column) {
        throw FastXlsxError("cell range cannot be reversed");
    }

    return first == last ? first : first + ":" + last;
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
