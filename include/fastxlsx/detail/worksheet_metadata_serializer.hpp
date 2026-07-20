#pragma once

#include <fastxlsx/workbook.hpp>
#include <fastxlsx/worksheet_metadata.hpp>

#include <span>
#include <string>

namespace fastxlsx::detail {

/// Serializes one validated worksheet-root auto-filter range.
[[nodiscard]] std::string serialize_worksheet_auto_filter(CellRange range);

/// Validates the shared narrow Streaming/Patch rule shape.
void validate_data_validation_rule(const DataValidationRule& rule);

/// Serializes one validated rule and its non-empty range set.
[[nodiscard]] std::string serialize_data_validation(
    std::span<const CellRange> ranges,
    const DataValidationRule& rule);

} // namespace fastxlsx::detail
