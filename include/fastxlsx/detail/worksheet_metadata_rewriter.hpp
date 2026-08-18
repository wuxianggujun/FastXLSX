#pragma once

#include <fastxlsx/detail/worksheet_event_reader.hpp>
#include <fastxlsx/detail/worksheet_metadata_serializer.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace fastxlsx::detail {

struct WorksheetInternalHyperlinkRewrite {
    std::string cell_reference;
    std::string location;
    std::string display;
    std::string tooltip;
};

struct WorksheetExternalHyperlinkRewrite {
    std::string cell_reference;
    std::string target;
    std::string relationship_id;
    std::string display;
    std::string tooltip;
};

struct WorksheetDataValidationRewritePlan {
    enum class Action {
        InsertContainerBefore,
        AppendBeforeContainerClose,
        ExpandSelfClosingContainer,
    };

    Action action = Action::InsertContainerBefore;
    std::uint64_t source_offset = 0;
    std::uint64_t container_start_offset = 0;
    std::uint64_t new_count = 1;
};

struct WorksheetDataValidationRemovalPlan {
    enum class Action {
        RemoveChild,
        RemoveContainer,
    };

    Action action = Action::RemoveChild;
    std::uint64_t source_offset = 0;
    std::uint64_t source_end_offset = 0;
    std::uint64_t container_start_offset = 0;
    std::uint64_t new_count = 0;
};

struct WorksheetConditionalFormatRewritePlan {
    std::uint64_t source_offset = 0;
    std::uint32_t priority = 1;
    std::string element_prefix;
};

struct WorksheetConditionalFormatRemovalPlan {
    std::uint64_t source_offset = 0;
    std::uint64_t source_end_offset = 0;
    std::string element_prefix;
};

struct WorksheetTablePartRewritePlan {
    enum class Action {
        InsertContainerBefore,
        AppendBeforeContainerClose,
        ExpandSelfClosingContainer,
        RemoveChild,
        RemoveContainer,
    };

    Action action = Action::InsertContainerBefore;
    std::uint64_t source_offset = 0;
    std::uint64_t source_end_offset = 0;
    std::uint64_t container_start_offset = 0;
    std::uint64_t new_count = 1;
    std::string element_prefix;
};

struct WorksheetAutoFilterRewritePlan {
    bool has_existing_auto_filter = false;
    std::uint64_t source_offset = 0;
    std::uint64_t source_end_offset = 0;
};

enum class WorksheetFreezePaneRewriteOperation {
    Set,
    Clear,
};

struct WorksheetFreezePaneRewritePlan {
    enum class Action {
        InsertSheetViewsBefore,
        ExpandSheetViewsContainer,
        AppendPrimarySheetView,
        ExpandPrimarySheetView,
        InsertPaneBefore,
        ReplacePane,
        RemovePane,
    };

    Action action = Action::InsertSheetViewsBefore;
    std::uint64_t source_offset = 0;
    std::uint64_t source_end_offset = 0;
    std::string element_prefix;
};

enum class WorksheetMergedCellRewriteOperation {
    Merge,
    Unmerge,
};

struct WorksheetMergedCellRewritePlan {
    enum class Action {
        InsertContainerBefore,
        AppendBeforeContainerClose,
        ExpandSelfClosingContainer,
        RemoveChild,
        RemoveContainer,
    };

    Action action = Action::InsertContainerBefore;
    std::uint64_t source_offset = 0;
    std::uint64_t source_end_offset = 0;
    std::uint64_t container_start_offset = 0;
    std::uint64_t new_count = 1;
    std::string element_prefix;
};

enum class WorksheetInternalHyperlinkRewriteAction {
    InsertContainerBefore,
    AppendBeforeContainerClose,
    ExpandSelfClosingContainer,
};

struct WorksheetInternalHyperlinkRewritePlan {
    WorksheetInternalHyperlinkRewriteAction action =
        WorksheetInternalHyperlinkRewriteAction::InsertContainerBefore;
    std::uint64_t source_offset = 0;
};

struct WorksheetLegacyDrawingRewritePlan {
    enum class Action {
        InsertBefore,
        PreserveExisting,
        RemoveExisting,
    };

    Action action = Action::InsertBefore;
    std::uint64_t source_offset = 0;
};

/// Inspects one worksheet stream and selects an exact insertion boundary.
///
/// The scan rejects a hyperlink whose target cell overlaps an existing
/// hyperlink ref, malformed hyperlink containers, and worksheet suffix metadata
/// whose schema position cannot be ordered safely. It does not retain worksheet
/// XML or any cell matrix.
[[nodiscard]] WorksheetInternalHyperlinkRewritePlan
plan_worksheet_internal_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetInternalHyperlinkRewrite& hyperlink);

/// Streams the same worksheet source to a staged file while applying a plan
/// returned by plan_worksheet_internal_hyperlink_rewrite().
void write_worksheet_internal_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetInternalHyperlinkRewrite& hyperlink,
    const WorksheetInternalHyperlinkRewritePlan& plan,
    const std::filesystem::path& output_path);

[[nodiscard]] WorksheetInternalHyperlinkRewritePlan
plan_worksheet_external_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetExternalHyperlinkRewrite& hyperlink);

void write_worksheet_external_hyperlink_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetExternalHyperlinkRewrite& hyperlink,
    const WorksheetInternalHyperlinkRewritePlan& plan,
    const std::filesystem::path& output_path);

/// Audits worksheet suffix ordering and either selects a schema-safe insertion
/// boundary for one note VML reference, verifies an editable existing
/// reference, or selects that reference for removal.
[[nodiscard]] WorksheetLegacyDrawingRewritePlan
plan_worksheet_legacy_drawing_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view relationship_id,
    bool allow_existing_reference,
    bool remove_existing_reference);

/// Streams a worksheet to a staged file while inserting, preserving, or
/// removing the legacyDrawing reference selected by
/// plan_worksheet_legacy_drawing_rewrite().
void write_worksheet_legacy_drawing_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view relationship_id,
    const WorksheetLegacyDrawingRewritePlan& plan,
    const std::filesystem::path& output_path);

/// Selects a schema-safe append/insert boundary for one data-validation rule.
/// Existing container count metadata must be absent or match the direct child
/// count; mismatches fail instead of being silently repaired.
[[nodiscard]] WorksheetDataValidationRewritePlan
plan_worksheet_data_validation_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk);

/// Streams the source worksheet to a staged file while appending one serialized
/// rule according to plan_worksheet_data_validation_rewrite().
void write_worksheet_data_validation_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view data_validation_xml,
    const WorksheetDataValidationRewritePlan& plan,
    const std::filesystem::path& output_path);

/// Selects one zero-based direct dataValidation child for exact removal. The
/// caller must first complete the strict bounded data-validation projection so
/// unsupported rule semantics fail before this structural byte-range plan.
[[nodiscard]] WorksheetDataValidationRemovalPlan
plan_worksheet_data_validation_removal(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::uint64_t validation_index);

/// Streams the source worksheet to a staged file while removing the planned
/// dataValidation child, or the whole container when it was the final child.
void write_worksheet_data_validation_removal(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetDataValidationRemovalPlan& plan,
    const std::filesystem::path& output_path);

/// Audits worksheet-root conditionalFormatting containers and all direct cfRule
/// priorities, then selects the schema-safe boundary for one additional
/// conditionalFormatting element. Unknown rule payloads are preserved.
[[nodiscard]] WorksheetConditionalFormatRewritePlan
plan_worksheet_conditional_format_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk);

/// Streams the source worksheet to a staged file while inserting one complete
/// serialized conditionalFormatting element at the planned schema boundary.
void write_worksheet_conditional_format_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view conditional_format_xml,
    const WorksheetConditionalFormatRewritePlan& plan,
    const std::filesystem::path& output_path);

/// Selects one zero-based conditionalFormatting container for exact removal.
/// The caller must first complete the strict bounded conditional-format
/// projection, which guarantees one writer-compatible rule per container.
[[nodiscard]] WorksheetConditionalFormatRemovalPlan
plan_worksheet_conditional_format_removal(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::uint64_t conditional_format_index);

/// Streams the source worksheet to a staged file while removing the complete
/// planned conditionalFormatting container without renumbering other rules.
void write_worksheet_conditional_format_removal(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetConditionalFormatRemovalPlan& plan,
    const std::filesystem::path& output_path);

/// Streams the source worksheet to a staged file while replacing the complete
/// planned conditionalFormatting container without changing its source-order
/// position. The caller supplies a complete serialized single-rule container.
void write_worksheet_conditional_format_replacement(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view conditional_format_xml,
    const WorksheetConditionalFormatRemovalPlan& plan,
    const std::filesystem::path& output_path);

/// Selects a schema-safe append/insert boundary for one linked table part.
/// Existing tableParts count and direct r:id children are audited rather than
/// repaired, and the worksheet element prefix is retained in the plan.
[[nodiscard]] WorksheetTablePartRewritePlan
plan_worksheet_table_part_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk);

/// Audits one worksheet tableParts container and selects the exact direct
/// tablePart whose namespace-resolved relationship id matches the target.
/// The last child removes the complete container; otherwise count is decremented.
[[nodiscard]] WorksheetTablePartRewritePlan
plan_worksheet_table_part_removal(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view relationship_id);

/// Adds one tablePart relationship reference and ensures the worksheet root
/// carries the standard OpenXML relationships namespace binding for prefix r,
/// or applies an exact removal plan without changing root namespace bindings.
void write_worksheet_table_part_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view relationship_id,
    const WorksheetTablePartRewritePlan& plan,
    const std::filesystem::path& output_path);

/// Locates an existing worksheet-root autoFilter or a schema-safe insertion
/// boundary. Table-part autoFilter elements are outside this worksheet stream.
[[nodiscard]] WorksheetAutoFilterRewritePlan
plan_worksheet_auto_filter_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk);

/// Replaces, inserts, or removes the worksheet-root autoFilter selected by the
/// plan. An empty auto_filter_xml removes an existing element.
void write_worksheet_auto_filter_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view auto_filter_xml,
    const WorksheetAutoFilterRewritePlan& plan,
    const std::filesystem::path& output_path);

/// Audits primary sheet-view metadata and plans one frozen-pane set/clear.
/// Clear returns no plan when workbookViewId=0 has no direct frozen pane.
[[nodiscard]] std::optional<WorksheetFreezePaneRewritePlan>
plan_worksheet_freeze_pane_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::uint32_t row_split,
    std::uint32_t column_split,
    WorksheetFreezePaneRewriteOperation operation);

/// Streams one planned primary sheet-view frozen-pane mutation.
void write_worksheet_freeze_pane_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view pane_xml,
    const WorksheetFreezePaneRewritePlan& plan,
    const std::filesystem::path& output_path);

/// Audits mergeCells metadata and plans one strict merge/unmerge mutation.
/// Unmerge returns no plan when the exact range is absent and disjoint.
[[nodiscard]] std::optional<WorksheetMergedCellRewritePlan>
plan_worksheet_merged_cell_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    CellRange range,
    WorksheetMergedCellRewriteOperation operation);

/// Streams one planned mergeCells mutation to a file-backed worksheet part.
void write_worksheet_merged_cell_rewrite(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::string_view merge_cell_xml,
    const WorksheetMergedCellRewritePlan& plan,
    const std::filesystem::path& output_path);

} // namespace fastxlsx::detail
