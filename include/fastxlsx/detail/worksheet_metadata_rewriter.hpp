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

struct WorksheetAutoFilterRewritePlan {
    bool has_existing_auto_filter = false;
    std::uint64_t source_offset = 0;
    std::uint64_t source_end_offset = 0;
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
