#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {

enum class FormulaReferenceKind {
    Cell,
    CellRange,
    WholeColumnRange,
    WholeRowRange,
};

struct FormulaCellReference {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
    bool row_absolute = false;
    bool column_absolute = false;
};

struct FormulaAxisReference {
    std::uint32_t value = 0;
    bool absolute = false;
};

struct FormulaSheetQualifier {
    bool present = false;
    std::size_t offset = 0;
    std::size_t length = 0;      // Includes the trailing '!'.
    std::size_t name_offset = 0; // Raw sheet token, excluding quotes and '!'.
    std::size_t name_length = 0;
    bool quoted = false;
};

struct FormulaReference {
    FormulaReferenceKind kind = FormulaReferenceKind::Cell;
    std::size_t offset = 0;
    std::size_t length = 0;
    FormulaSheetQualifier sheet;
    // For Cell, `first_cell` is the referenced cell. For CellRange, the two
    // endpoints preserve the source range order.
    FormulaCellReference first_cell;
    FormulaCellReference last_cell;
    FormulaAxisReference first_axis;
    FormulaAxisReference last_axis;
};

struct FormulaTranslationDelta {
    std::int64_t row_delta = 0;
    std::int64_t column_delta = 0;
};

/// Scans formula text for the narrow A1-style references understood by the
/// shared-formula materializer.
///
/// This intentionally does not evaluate formulas or parse the full Excel
/// formula grammar. It skips double-quoted string text, quoted sheet-name
/// tokens, and bracketed external/structured-reference tokens, then reports
/// cell references, A1-style cell ranges, and whole-row / whole-column ranges
/// that are safe to translate for source-order shared formula followers. When
/// present, the raw sheet qualifier span is also exposed for dependency audit;
/// the scanner still does not validate workbook or sheet existence.
[[nodiscard]] std::vector<FormulaReference> scan_formula_references(
    std::string_view formula);

/// Translates the references returned by scan_formula_references().
///
/// Relative cell references and whole-axis ranges are shifted by `delta`.
/// Absolute row/column anchors are preserved. Any translated coordinate that
/// leaves Excel's row/column bounds is replaced by `#REF!`. This is still a
/// formula text rewriter, not a formula evaluator or calcChain rebuilder.
[[nodiscard]] std::string translate_formula_references(
    std::string_view formula, FormulaTranslationDelta delta);

} // namespace fastxlsx::detail
