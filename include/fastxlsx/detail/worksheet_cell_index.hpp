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
/// one map entry per indexed source cell and therefore is not the default path
/// for every large-sheet edit. It does not materialize cell values, parse
/// styles/sharedStrings, repair metadata, or expose public API.
class WorksheetCellIndex {
public:
    using CellRangeMap = std::map<std::string, WorksheetCellIndexedRange, std::less<>>;

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
        return cells_by_reference_.size();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return cells_by_reference_.empty();
    }

    [[nodiscard]] const CellRangeMap& cells() const noexcept
    {
        return cells_by_reference_;
    }

    // Internal builder hook; validates duplicates and range ordering.
    void add_cell(std::string_view cell_reference, WorksheetCellIndexedRange range);

private:
    CellRangeMap cells_by_reference_;
};

struct WorksheetIndexedCellRewrite {
    std::string cell_reference;
    WorksheetCellIndexedRange source_range;
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

[[nodiscard]] std::string_view worksheet_cell_range_xml(
    std::string_view worksheet_xml,
    const WorksheetCellIndexedRange& range);

} // namespace fastxlsx::detail
