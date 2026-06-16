#pragma once

#include <fastxlsx/cell_value.hpp>
#include <fastxlsx/detail/worksheet_event_reader.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace fastxlsx::detail {

class CellStore;
class PackageReader;

/// Worksheet-local sparse coordinate for the internal in-memory editor store.
struct CellPosition {
    std::uint32_t row = 1;
    std::uint32_t column = 1;
};

[[nodiscard]] bool operator<(const CellPosition& left, const CellPosition& right) noexcept;

/// Compact internal cell record for future in-memory editor storage.
///
/// This is intentionally separate from public Cell and CellValue objects. It
/// keeps only the value kind, compact scalar payload, optional style handle, and
/// owned text/formula payload needed by the first sparse-store slice.
struct CellRecord {
    CellValueKind kind = CellValueKind::Blank;
    double number_value = 0.0;
    bool boolean_value = false;
    std::string text_value;
    std::optional<StyleId> style_id;

    [[nodiscard]] static CellRecord from_value(const CellValue& value);
    [[nodiscard]] CellValue to_value() const;
};

/// Creates a pull-based chunk source for the internal sparse store's standalone
/// `<sheetData>` payload.
///
/// The returned callback references `store`; callers must keep the store alive
/// and unmodified until the callback is fully consumed. This is the low-copy
/// handoff used by the current WorkbookEditor Patch facade before the payload is
/// staged by PackageEditor. It still emits inline strings and does not migrate
/// sharedStrings, merge styles, update calcChain, repair relationships, or
/// generate a full worksheet part.
[[nodiscard]] WorksheetInputChunkCallback cell_store_sheet_data_chunk_source(
    const CellStore& store);

/// Creates a pull-based chunk source for a minimal worksheet XML part projected
/// from the internal sparse store.
///
/// The output contains an XML declaration, a worksheet root, a refreshed
/// top-level `<dimension>` derived from active store records, and the
/// standalone `<sheetData>` payload from cell_store_sheet_data_chunk_source().
/// The returned callback references `store`; callers must keep the store alive
/// and unmodified until the callback is fully consumed. This is only an
/// internal in-memory save-as handoff building block. It does not preserve or
/// recalculate worksheet metadata, migrate sharedStrings, merge styles, repair
/// relationships, evaluate formulas, rebuild calcChain, or expose a public
/// WorksheetEditor.
[[nodiscard]] WorksheetInputChunkCallback cell_store_worksheet_chunk_source(
    const CellStore& store);

/// Returns the worksheet `<dimension>` reference implied by the active sparse
/// records in `store`.
///
/// Empty stores return `A1`. Non-empty stores return the min/max rectangular
/// extent of emitted records, including explicit blank records. This is an
/// internal building block for future in-memory save-as dimension refresh; it
/// does not recalculate tables, defined names, drawings, hyperlinks, data
/// validations, conditional formatting, or other range-bearing metadata.
[[nodiscard]] std::string cell_store_dimension_reference(const CellStore& store);

/// Internal size and memory guardrails for the first CellStore slice.
///
/// These limits are intentionally internal. They are budget checks for future
/// in-memory editor materialization, not exact process RSS controls and not a
/// public WorkbookEditor options contract.
struct CellStoreOptions {
    std::optional<std::size_t> max_cells;
    std::optional<std::size_t> memory_budget_bytes;
};

/// Internal sparse worksheet cell store for future small-file editing.
///
/// API mode: internal P7 foundation. The store records only cells explicitly
/// loaded or edited by a future in-memory editor. It does not allocate a full
/// worksheet matrix, evaluate formulas, migrate shared strings, merge styles,
/// repair relationships, or serialize worksheet XML.
class CellStore {
public:
    explicit CellStore(CellStoreOptions options = {});

    void set_cell(std::uint32_t row, std::uint32_t column, const CellValue& value);
    void erase_cell(std::uint32_t row, std::uint32_t column);

    /// Returns the active sparse record for a cell, or nullptr when the cell is
    /// missing from the store. An explicit blank cell is returned as a non-null
    /// record with CellValueKind::Blank.
    [[nodiscard]] const CellRecord* try_cell(
        std::uint32_t row, std::uint32_t column) const;

    /// Backward-compatible alias for try_cell().
    [[nodiscard]] const CellRecord* find_cell(
        std::uint32_t row, std::uint32_t column) const;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t cell_count() const noexcept;
    [[nodiscard]] std::size_t estimated_memory_usage() const noexcept;

    [[nodiscard]] const CellStoreOptions& options() const noexcept;
    [[nodiscard]] const std::map<CellPosition, CellRecord>& records() const noexcept;

private:
    CellStoreOptions options_;
    std::map<CellPosition, CellRecord> cells_;
};

/// Loads supported cell payloads from worksheet XML into an internal sparse
/// CellStore.
///
/// This is a first source-backed materialization slice for future small-file
/// editor work. It consumes worksheet events without building a worksheet DOM
/// and currently accepts only explicit cell references with number, boolean,
/// inline-string, formula, and explicit blank cells. Text and formula payloads
/// are normalized to semantic text by decoding XML entity references; cached
/// formula values are intentionally ignored. Shared-string indexes, style
/// attributes, unsupported cell types, non-finite numeric values, invalid
/// boolean values, formula cells with non-numeric cell types, missing or invalid
/// cell references, row/cell reference mismatches, duplicate cell references,
/// duplicate supported value wrappers, duplicate explicit row numbers,
/// duplicate key attributes inspected by this loader, malformed inspected
/// attributes, XML entity decoding failures, out-of-order explicit row numbers,
/// out-of-order cell references, formula elements with attributes, empty
/// formula text, cells outside row elements, and unsupported row/cell metadata,
/// value-wrapper attributes, inline rich text / phonetic metadata, or
/// cell-contained comments / processing instructions / unsupported markup fail
/// before a store is returned. The supplied CellStoreOptions are enforced during
/// loading. This does not migrate sharedStrings, merge styles, repair
/// relationships, recalculate formulas, preserve cached formula results, or
/// expose a public WorksheetEditor.
[[nodiscard]] CellStore load_cell_store_from_worksheet_chunks(
    const WorksheetInputChunkCallback& read_next_chunk,
    CellStoreOptions options = {},
    WorksheetEventReaderOptions reader_options = {});

/// Convenience wrapper for loading worksheet XML already available as a
/// string_view. The same boundaries and XML entity decoding behavior as
/// load_cell_store_from_worksheet_chunks() apply.
[[nodiscard]] CellStore load_cell_store_from_worksheet_xml(
    std::string_view worksheet_xml,
    CellStoreOptions options = {},
    WorksheetEventReaderOptions reader_options = {});

/// Convenience wrapper for loading a worksheet from a PackageReader by sheet
/// name.
///
/// This resolves the workbook catalog, locates the worksheet part by name, and
/// streams that part through the same chunk-backed loader used by
/// load_cell_store_from_worksheet_chunks(). It keeps the source-backed path
/// internal and does not materialize a worksheet DOM. Failures are wrapped with
/// sheet-name, worksheet part, and ZIP-entry context where available.
[[nodiscard]] CellStore load_cell_store_from_workbook_sheet(
    const PackageReader& reader, std::string_view sheet_name,
    CellStoreOptions options = {}, WorksheetEventReaderOptions reader_options = {});

} // namespace fastxlsx::detail
