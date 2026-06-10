#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace fastxlsx {

struct CellRange;

namespace detail {

[[nodiscard]] std::string escape_xml_text(std::string_view value);
[[nodiscard]] std::string escape_xml_attribute(std::string_view value);
[[nodiscard]] std::string format_number(double value);
void append_number(std::string& output, double value);
[[nodiscard]] std::string cell_reference(std::uint32_t row, std::uint32_t column);
[[nodiscard]] std::string range_reference(std::uint32_t first_row,
    std::uint32_t first_column,
    std::uint32_t last_row,
    std::uint32_t last_column);
[[nodiscard]] std::string range_reference(const CellRange& range);
[[nodiscard]] std::string sqref(std::span<const CellRange> ranges);

} // namespace detail

} // namespace fastxlsx
