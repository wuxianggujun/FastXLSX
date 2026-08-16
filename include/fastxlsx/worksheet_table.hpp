#pragma once

/// @file worksheet_table.hpp
/// Public narrow worksheet table metadata shared by Streaming and Patch APIs.

#include <optional>
#include <string>
#include <vector>

namespace fastxlsx {

/// Built-in Excel totals-row aggregate metadata for a table column.
///
/// FastXLSX writes the matching OpenXML metadata only. It does not calculate
/// totals, generate formula text, or rewrite the totals-row cell payload.
enum class TableTotalsFunction {
    Sum,
    Count,
    Average,
    Maximum,
    Minimum,
    Product,
    CountNumbers,
    StandardDeviation,
    Variance,
};

/// A narrow worksheet table definition.
///
/// Streaming uses this definition when creating a new workbook. Patch uses
/// the same definition when adding or replacing table metadata in an existing
/// worksheet. The library does not inspect header cells, calculate totals, or
/// synchronize the range with later structural cell edits.
struct TableOptions {
    /// Workbook-wide table display name. The current slice accepts
    /// conservative ASCII identifiers only: first character must be a letter
    /// or underscore, followed by letters, digits, or underscores.
    std::string name;

    /// Header names written to `<tableColumns>`. The count must match the
    /// table range width. Names must be non-empty and unique within the table.
    std::vector<std::string> column_names;

    /// Shows the final row in the supplied range as an Excel totals row.
    /// The caller must provide the totals-row cells and include that row in the
    /// table range; FastXLSX writes metadata only.
    bool show_totals_row = false;

    /// Optional per-column totals-row function metadata. Empty omits functions;
    /// otherwise the vector size must match column_names. A visible totals row
    /// requires at least one non-empty function.
    std::vector<std::optional<TableTotalsFunction>> column_totals_functions;

    /// Optional per-column totals-row label metadata. Empty omits labels;
    /// otherwise the vector size must match column_names. An empty string omits
    /// the corresponding column attribute.
    std::vector<std::string> column_totals_labels;

    /// Built-in Excel table style name. Empty omits `<tableStyleInfo>`; FastXLSX
    /// does not create or modify styles.xml for this metadata.
    std::string style_name = "TableStyleMedium2";

    bool show_first_column = false;
    bool show_last_column = false;
    bool show_row_stripes = true;
    bool show_column_stripes = false;
};

} // namespace fastxlsx
