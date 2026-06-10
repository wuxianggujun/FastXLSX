#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {

/// Caller-provided replacement candidate for the first internal P8 transformer slice.
///
/// Both views must remain alive for the duration of
/// scan_cell_replacement_actions(). The replacement XML must expose a cell
/// element root with an unqualified r attribute matching cell_reference. This
/// narrow preflight still does not validate the full cell schema, migrate
/// sharedStrings/styles, repair relationships, or write worksheet XML.
struct WorksheetCellReplacement {
    std::string_view cell_reference;
    std::string_view replacement_cell_xml;
};

enum class WorksheetTransformActionKind {
    PassThrough,
    ReplaceCell,
};

/// Non-owning transform action view emitted in source worksheet order.
struct WorksheetTransformAction {
    WorksheetTransformActionKind kind = WorksheetTransformActionKind::PassThrough;
    std::string_view raw_xml;
    std::string_view row_number;
    std::string_view cell_reference;
    std::string_view replacement_cell_xml;
};

struct WorksheetTransformSummary {
    std::size_t matched_replacement_count = 0;
    std::vector<std::string> missing_cell_references;
};

using WorksheetTransformActionCallback = std::function<void(const WorksheetTransformAction&)>;
using WorksheetOutputChunkCallback = std::function<void(std::string_view)>;

/// Emits source-order transform actions for a bounded set of cell replacements.
///
/// This is an internal action-model foundation for P8. It maps replacement
/// selectors onto worksheet event-reader tokens and reports missing selectors,
/// but it does not build the rewritten worksheet, update dimensions, request
/// recalculation, or commit anything to PackageEditor/EditPlan.
[[nodiscard]] WorksheetTransformSummary scan_cell_replacement_actions(
    std::string_view worksheet_xml,
    std::span<const WorksheetCellReplacement> replacements,
    const WorksheetTransformActionCallback& callback);

/// Emits rewritten worksheet XML chunks for the current replacement action model.
///
/// This streams pass-through source chunks and caller replacement cell XML
/// through callback. The function is intentionally still internal and narrow:
/// it does not update dimensions, run dependency repair, write a package entry,
/// or commit any PackageEditor/EditPlan state.
[[nodiscard]] WorksheetTransformSummary emit_cell_replacement_worksheet(
    std::string_view worksheet_xml,
    std::span<const WorksheetCellReplacement> replacements,
    const WorksheetOutputChunkCallback& callback);

} // namespace fastxlsx::detail
