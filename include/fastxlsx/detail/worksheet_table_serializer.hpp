#pragma once

#include <fastxlsx/workbook.hpp>
#include <fastxlsx/worksheet_table.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace fastxlsx::detail {

/// Validates the narrow table shape shared by Streaming and Patch lifecycle APIs.
void validate_worksheet_table(CellRange range, const TableOptions& options);

[[nodiscard]] bool worksheet_table_ranges_overlap(
    CellRange left, CellRange right) noexcept;

[[nodiscard]] bool worksheet_table_names_equal(
    std::string_view left, std::string_view right) noexcept;

/// Serializes one validated worksheet table part with a positive workbook id.
[[nodiscard]] std::string serialize_worksheet_table(
    CellRange range, const TableOptions& options, std::uint32_t table_id);

} // namespace fastxlsx::detail
