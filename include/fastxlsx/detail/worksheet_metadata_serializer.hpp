#pragma once

#include <fastxlsx/workbook.hpp>
#include <fastxlsx/worksheet_metadata.hpp>

#include <span>
#include <string>
#include <string_view>

namespace fastxlsx::detail {

/// Serializes one validated worksheet-root auto-filter range.
[[nodiscard]] std::string serialize_worksheet_auto_filter(CellRange range);

/// Validates one merged range shared by Streaming and Patch metadata paths.
void validate_merged_cell_range(CellRange range);

/// Serializes one validated worksheet-root mergeCell element.
[[nodiscard]] std::string serialize_worksheet_merged_cell(
    CellRange range, std::string_view element_prefix = {});

/// Validates the shared narrow Streaming/Patch rule shape.
void validate_data_validation_rule(const DataValidationRule& rule);

/// Serializes one validated rule and its non-empty range set.
[[nodiscard]] std::string serialize_data_validation(
    std::span<const CellRange> ranges,
    const DataValidationRule& rule);

} // namespace fastxlsx::detail
