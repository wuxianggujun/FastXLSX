#pragma once

#include <fastxlsx/workbook.hpp>
#include <fastxlsx/worksheet_metadata.hpp>

#include <span>
#include <string>

namespace fastxlsx::detail {

/// Validates the shared narrow Streaming/Patch rule shape.
void validate_data_validation_rule(const DataValidationRule& rule);

/// Serializes one validated rule and its non-empty range set.
[[nodiscard]] std::string serialize_data_validation(
    std::span<const CellRange> ranges,
    const DataValidationRule& rule);

} // namespace fastxlsx::detail
