#pragma once

#include <fastxlsx/detail/worksheet_cell_index.hpp>
#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {

/// Caller-provided single-cell replacement payload for the internal transformer slice.
///
/// The backing is caller-owned XML chunks, wrapped behind an explicit payload
/// type so the transformer and PackageEditor do not pass naked replacement
/// strings through action/plan state. This is a single-cell payload, not a
/// worksheet XML fallback.
class WorksheetCellReplacementPayload {
public:
    using ChunkCallback = std::function<void(std::string_view)>;

    WorksheetCellReplacementPayload() = default;

    [[nodiscard]] static WorksheetCellReplacementPayload from_materialized_xml(
        std::string_view materialized_xml) noexcept
    {
        return WorksheetCellReplacementPayload(materialized_xml);
    }

    [[nodiscard]] static WorksheetCellReplacementPayload from_chunks(
        std::span<const std::string_view> chunks);

    [[nodiscard]] std::size_t byte_size() const noexcept
    {
        return byte_size_;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return byte_size_ == 0;
    }

    void for_each_chunk(const ChunkCallback& callback) const
    {
        for (const std::string_view chunk : chunks_) {
            if (!chunk.empty()) {
                callback(chunk);
            }
        }
    }

    /// Materializes only the bounded single-cell payload for transformer preflight.
    ///
    /// Output and audit paths should replay chunks through for_each_chunk() so
    /// action/plan state can remain chunk-oriented.
    [[nodiscard]] std::string materialize_for_preflight() const;

private:
    explicit WorksheetCellReplacementPayload(std::string_view materialized_xml)
        : chunks_ {materialized_xml}
        , byte_size_(materialized_xml.size())
    {
    }

    std::vector<std::string_view> chunks_;
    std::size_t byte_size_ = 0;
};

/// Caller-provided cell selector plus replacement payload.
///
/// The selector view and every payload chunk view must remain alive and stable
/// for the duration of the chunk/chunk-source transformer call. The payload
/// must expose a cell element root with an unqualified r attribute matching
/// cell_reference. This narrow preflight still does not validate the full cell
/// schema, migrate sharedStrings/styles, repair relationships, or write
/// worksheet XML.
struct WorksheetCellReplacement {
    std::string_view cell_reference;
    WorksheetCellReplacementPayload replacement_payload;
};

enum class WorksheetCellReplacementMode {
    ReplaceExisting,
    ReplaceOrInsert,
};

enum class WorksheetTransformActionKind {
    PassThrough,
    ReplaceCell,
    InsertCell,
};

/// Non-owning transform action view emitted in source worksheet order.
struct WorksheetTransformAction {
    WorksheetTransformActionKind kind = WorksheetTransformActionKind::PassThrough;
    WorksheetEventKind event_kind = WorksheetEventKind::Unsupported;
    std::string_view raw_xml;
    std::string_view element_name;
    std::string_view row_number;
    std::string_view cell_reference;
    WorksheetCellReplacementPayload replacement_payload;
    bool self_closing = false;
    /// Absolute decompressed worksheet XML byte offset for source-backed
    /// actions. Synthetic insert/pass-through actions use zero.
    std::uint64_t raw_xml_offset = 0;
    /// Parsed cell coordinate when the transformer already needed it for
    /// source-order upsert decisions. Zero means the action did not carry one.
    std::uint32_t source_cell_row = 0;
    std::uint32_t source_cell_column = 0;
};

struct WorksheetTransformSummary {
    std::size_t matched_replacement_count = 0;
    std::size_t inserted_cell_count = 0;
    std::vector<std::string> missing_cell_references;
};

struct WorksheetCellReplacementCoordinate {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

struct WorksheetCellReplacementTarget {
    std::string_view cell_reference;
    WorksheetCellReplacementCoordinate coordinate;
    WorksheetCellReplacementPayload replacement_payload;
};

/// Non-owning lookup plan for repeated cell-replacement passes over the same
/// caller replacement set.
///
/// The payload chunk views still point at caller-owned `WorksheetCellReplacement`
/// backing. The plan is intended to avoid rebuilding selector lookup and
/// reparsing already preflighted single-cell payloads across analysis/output
/// passes; it is not a worksheet payload owner.
struct WorksheetCellReplacementPlan {
    std::map<std::string_view, WorksheetCellReplacementPayload, std::less<>>
        replacement_payloads_by_reference;
    std::vector<WorksheetCellReplacementTarget> targets_by_position;
};

using WorksheetTransformActionCallback = std::function<void(const WorksheetTransformAction&)>;
using WorksheetOutputChunkCallback = std::function<void(std::string_view)>;

/// Maximum caller-materialized XML accepted for one replacement cell payload.
///
/// This bounds the remaining cell-level materialized replacement edge. It is
/// not a worksheet XML limit and does not make replacement cell XML a public
/// low-memory API.
inline constexpr std::size_t worksheet_replacement_cell_xml_materialization_byte_limit =
    256U * 1024U;

/// Preflights caller replacement selectors and single-cell replacement XML
/// payloads, then builds a non-owning lookup plan that can be reused by
/// multiple source worksheet passes.
[[nodiscard]] WorksheetCellReplacementPlan make_worksheet_cell_replacement_plan(
    std::span<const WorksheetCellReplacement> replacements);

/// Emits source-order transform actions from a pull-based worksheet chunk source.
///
/// Action string views are valid only for the duration of the callback because
/// they may point into the event-reader retained window. Replacement payload
/// views still point to caller-owned bounded single-cell XML chunks, but
/// output/audit callers should replay them as chunks instead of reading a raw
/// string field.
[[nodiscard]] WorksheetTransformSummary scan_cell_replacement_actions_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetCellReplacementPlan& replacement_plan,
    const WorksheetTransformActionCallback& callback,
    WorksheetEventReaderOptions reader_options = {},
    WorksheetCellReplacementMode mode = WorksheetCellReplacementMode::ReplaceExisting);

/// Emits rewritten worksheet XML chunks while consuming a pull-based source.
///
/// This is used by PackageEditor file-backed source entries to avoid
/// materializing the source worksheet XML before the transformer runs.
/// Replacement payloads are emitted by replaying their payload chunks.
[[nodiscard]] WorksheetTransformSummary emit_cell_replacement_worksheet_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    const WorksheetCellReplacementPlan& replacement_plan,
    const WorksheetOutputChunkCallback& callback,
    WorksheetEventReaderOptions reader_options = {},
    WorksheetCellReplacementMode mode = WorksheetCellReplacementMode::ReplaceExisting);

/// Emits a strict existing-cell replacement from a materialized worksheet XML
/// buffer by slicing source byte ranges from a prebuilt `WorksheetCellIndex`.
///
/// This is an internal indexed rewrite primitive. It requires a materialized
/// worksheet XML source whose bytes exactly match the index, and it only handles
/// existing-cell replacement. It does not refresh dimensions, insert missing
/// cells/rows, audit relationships, repair metadata, migrate sharedStrings /
/// styles, or patch package entries.
[[nodiscard]] WorksheetTransformSummary emit_indexed_cell_replacement_worksheet(
    std::string_view worksheet_xml,
    const WorksheetCellIndex& index,
    const WorksheetCellReplacementPlan& replacement_plan,
    const WorksheetOutputChunkCallback& callback);

} // namespace fastxlsx::detail
