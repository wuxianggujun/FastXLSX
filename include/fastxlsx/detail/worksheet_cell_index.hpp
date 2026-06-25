#pragma once

#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {

/// Absolute byte range for one source worksheet `<c>` element.
///
/// Offsets are measured against the decompressed worksheet XML byte stream
/// consumed by the event reader. They are not ZIP-entry offsets and are valid
/// only for the exact worksheet XML source that was indexed.
struct WorksheetCellIndexedRange {
    std::uint64_t start_offset = 0;
    std::uint64_t end_offset = 0;

    [[nodiscard]] friend bool operator==(
        const WorksheetCellIndexedRange& left,
        const WorksheetCellIndexedRange& right) noexcept
    {
        return left.start_offset == right.start_offset
            && left.end_offset == right.end_offset;
    }
};

/// Internal sparse cell-reference -> source-byte-range index.
///
/// This is a foundation for indexed/random-access worksheet rewrites. It keeps
/// one compact coordinate/range entry per indexed source cell and therefore is
/// still an opt-in internal path for bounded random access. It does not
/// materialize cell values, parse styles/sharedStrings, repair metadata, or
/// expose public API.
class WorksheetCellIndex {
public:
    using CellRangeMap = std::map<std::string, WorksheetCellIndexedRange, std::less<>>;

    struct CellEntry {
        std::uint32_t row = 0;
        std::uint32_t column = 0;
        WorksheetCellIndexedRange range;
    };

    [[nodiscard]] static WorksheetCellIndex build_from_chunk_source(
        const WorksheetInputChunkCallback& read_next_chunk,
        WorksheetEventReaderOptions options = {});

    [[nodiscard]] static WorksheetCellIndex build_from_xml(
        std::string_view worksheet_xml,
        WorksheetEventReaderOptions options = {});

    [[nodiscard]] const WorksheetCellIndexedRange* find(
        std::string_view cell_reference) const noexcept;

    [[nodiscard]] std::size_t cell_count() const noexcept
    {
        return cells_by_position_.size();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return cells_by_position_.empty();
    }

    /// Returns a diagnostic compatibility snapshot keyed by canonical A1 text.
    ///
    /// The primary index is coordinate-based; this map is materialized lazily so
    /// benchmark and rewrite paths do not allocate one string/map node per cell.
    [[nodiscard]] const CellRangeMap& cells() const;

    // Internal builder hook; validates the range/reference and appends an entry.
    void add_cell(std::string_view cell_reference, WorksheetCellIndexedRange range);

    // Sorts the compact index and rejects duplicate source cell coordinates.
    void finalize();

private:
    std::vector<CellEntry> cells_by_position_;
    bool cells_are_sorted_ = true;
    mutable CellRangeMap cells_snapshot_;
    mutable bool cells_snapshot_valid_ = false;
};

struct WorksheetIndexedCellRewrite {
    std::string cell_reference;
    WorksheetCellIndexedRange source_range;
};

struct WorksheetTargetedCellRewritePlan {
    std::vector<WorksheetIndexedCellRewrite> rewrites;
    std::uint64_t scanned_source_cell_count = 0;
};

/// Validates a target set against a source cell index and returns source-order
/// rewrite ranges.
///
/// This is an internal planning primitive for future indexed worksheet
/// rewrites. It does not rewrite XML, patch package entries, migrate
/// sharedStrings/styles, repair metadata, or expose a public random-access API.
[[nodiscard]] std::vector<WorksheetIndexedCellRewrite> plan_indexed_cell_rewrites(
    const WorksheetCellIndex& index,
    std::span<const std::string_view> cell_references);

/// Streams source worksheet XML and records byte ranges only for requested
/// target cells.
///
/// This target-only planner keeps memory proportional to requested targets
/// instead of source cell count. It parses source cell references while scanning
/// but intentionally does not build a global source-cell duplicate index; use
/// `WorksheetCellIndex` when the caller needs full source validation.
[[nodiscard]] WorksheetTargetedCellRewritePlan plan_targeted_cell_rewrites_from_chunk_source(
    const WorksheetInputChunkCallback& read_next_chunk,
    std::span<const std::string_view> cell_references,
    WorksheetEventReaderOptions options = {});

[[nodiscard]] std::string_view worksheet_cell_range_xml(
    std::string_view worksheet_xml,
    const WorksheetCellIndexedRange& range);

} // namespace fastxlsx::detail
