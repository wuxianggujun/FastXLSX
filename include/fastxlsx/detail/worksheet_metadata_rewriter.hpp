#pragma once

#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include <cstdint>
#include <filesystem>
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

} // namespace fastxlsx::detail
