#pragma once

/// @file workbook_editor.hpp
/// Minimal public Patch-mode facade for editing an existing XLSX workbook.

#include <fastxlsx/cell_value.hpp>
#include <fastxlsx/workbook.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx {

class WorkbookEditor;
class WorksheetEditor;

namespace detail {
#ifdef FASTXLSX_ENABLE_TEST_HOOKS
void testing_workbook_editor_materialize_source_sheet(
    WorkbookEditor& editor,
    std::string_view planned_name,
    std::string_view source_sheet_name);
void testing_workbook_editor_set_materialized_cell(
    WorkbookEditor& editor,
    std::string_view planned_name,
    std::uint32_t row,
    std::uint32_t column,
    const CellValue& value);
void testing_workbook_editor_erase_materialized_cell(
    WorkbookEditor& editor,
    std::string_view planned_name,
    std::uint32_t row,
    std::uint32_t column);
void testing_workbook_editor_flush_materialized_sessions_to_patch_plan(
    WorkbookEditor& editor);
[[nodiscard]] std::size_t testing_workbook_editor_materialized_session_count(
    const WorkbookEditor& editor) noexcept;
[[nodiscard]] std::size_t testing_workbook_editor_dirty_materialized_session_count(
    const WorkbookEditor& editor) noexcept;
[[nodiscard]] bool testing_workbook_editor_has_materialized_session(
    const WorkbookEditor& editor, std::string_view planned_name) noexcept;
[[nodiscard]] std::vector<std::string> testing_workbook_editor_dirty_materialized_session_names(
    const WorkbookEditor& editor);
#endif
} // namespace detail

/// Public guardrails for the current narrow WorkbookEditor Patch facade.
///
/// API mode: Patch. These limits apply only to replacement rows passed to
/// WorkbookEditor::replace_sheet_data(). They do not materialize source
/// worksheet cells, do not enable random editing, and are not workbook-level
/// memory limits for materialized WorksheetEditor sessions.
struct WorkbookEditorOptions {
    /// Maximum number of explicit CellValue records accepted by one
    /// replace_sheet_data() call.
    ///
    /// Empty row vectors advance no stored cells. Blank CellValue objects inside
    /// a row still count as explicit replacement cells because they can affect
    /// the saved <sheetData> payload.
    std::optional<std::size_t> max_replacement_cells;

    /// Estimated CellStore memory budget for one replace_sheet_data() call.
    ///
    /// This is a conservative budget check based on the temporary sparse store
    /// used to build the replacement <sheetData> chunk source. It is not exact
    /// process RSS tracking and does not include source package bytes, generated
    /// XML chunks, PackageEditor staging files, ZIP writer buffers, or future
    /// save-time assembly costs.
    std::optional<std::size_t> replacement_memory_budget_bytes;
};

/// Guardrails for one materialized worksheet editing session.
///
/// API mode: In-memory / existing-workbook small-file editing. These limits are
/// passed per WorkbookEditor::worksheet() call and apply while loading source
/// worksheet cells into the editor-owned sparse store and while mutating that
/// store. They are intentionally separate from WorkbookEditorOptions, which
/// guards whole-<sheetData> replacement payloads.
struct WorksheetEditorOptions {
    /// Maximum number of sparse cell records allowed in the materialized
    /// worksheet session.
    std::optional<std::size_t> max_cells;

    /// Estimated sparse-store memory budget for the materialized worksheet.
    ///
    /// This is a conservative CellStore estimate, not process RSS. It excludes
    /// source package bytes, generated XML chunks, PackageEditor staging files,
    /// ZIP writer buffers, and save-time package assembly costs.
    std::optional<std::size_t> memory_budget_bytes;
};

/// Coarse public summary of a worksheet-level edit queued in WorkbookEditor.
///
/// API mode: Patch. This value describes only public facade state that will be
/// visible through worksheet_names(), pending replacement diagnostics,
/// materialized WorksheetEditor dirty-state diagnostics, and save_as(). It is
/// not an internal EditPlan entry, package part diff, dependency audit,
/// relationship audit, or semantic workbook diff.
struct WorkbookEditorWorksheetEditSummary {
    /// Worksheet name in the opened source workbook catalog.
    std::string source_name;

    /// Worksheet name in the current planned catalog that save_as() will write.
    std::string planned_name;

    /// True when rename_sheet() has queued a sheet-catalog name change for this
    /// source worksheet.
    bool renamed = false;

    /// True when replace_sheet_data() has queued a whole-<sheetData>
    /// replacement for this planned worksheet name.
    bool sheet_data_replaced = false;

    /// True when the materialized WorksheetEditor session for this planned
    /// worksheet name is dirty and waiting for save_as() auto-flush.
    bool materialized_dirty = false;

    /// Explicit replacement cells represented by the final queued
    /// replace_sheet_data() payload for this worksheet. Zero when
    /// sheet_data_replaced is false.
    std::size_t replacement_cell_count = 0;

    /// Estimated sparse-store memory recorded for the final queued replacement
    /// payload for this worksheet. This excludes source package bytes,
    /// generated XML chunks, PackageEditor staging files, ZIP writer buffers,
    /// and save-time package assembly costs.
    std::size_t estimated_replacement_memory_usage = 0;

    /// Active sparse cell records currently held by the dirty materialized
    /// WorksheetEditor session. Zero when materialized_dirty is false.
    std::size_t materialized_cell_count = 0;

    /// Estimated sparse-store memory for the dirty materialized WorksheetEditor
    /// session. This is a CellStore estimate, not process RSS, and excludes
    /// source package bytes, generated XML chunks, PackageEditor staging files,
    /// ZIP writer buffers, and save-time package assembly costs.
    std::size_t estimated_materialized_memory_usage = 0;
};

/// Public source-to-planned worksheet catalog entry for WorkbookEditor.
///
/// API mode: Patch. This value shows how one source workbook sheet will appear
/// in the current planned catalog used by worksheet_names(), has_worksheet(),
/// replace_sheet_data(), and save_as(). It does not expose workbook
/// relationships, worksheet part names, package entries, or internal EditPlan
/// state.
struct WorkbookEditorWorksheetCatalogEntry {
    /// Worksheet name in the opened source workbook catalog.
    std::string source_name;

    /// Worksheet name in the current planned catalog.
    std::string planned_name;

    /// True when source_name differs from planned_name because rename_sheet()
    /// has queued a sheet-catalog rename.
    bool renamed = false;
};

/// Public coordinate for a sparse WorksheetEditor cell snapshot.
///
/// API mode: In-memory / existing-workbook small-file inspection. Coordinates
/// are 1-based Excel worksheet row/column indexes. This value is an owning copy
/// returned from WorksheetEditor::sparse_cells(); it does not borrow internal
/// CellStore state and is not an iterator handle.
struct WorksheetCellReference {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

/// Public value snapshot for one sparse WorksheetEditor cell.
///
/// API mode: In-memory / existing-workbook small-file inspection. The value is
/// copied out of the materialized sparse store. Explicit blank cells are
/// represented by CellValue::blank(). This snapshot does not expose styles,
/// formulas beyond their CellValue payload, relationships, or worksheet
/// metadata.
struct WorksheetCellSnapshot {
    WorksheetCellReference reference;
    CellValue value;
};

/// Borrowed random cell editor for one WorkbookEditor-owned materialized sheet.
///
/// API mode: In-memory / existing-workbook small-file editing. WorksheetEditor
/// is not a standalone owner; it references the open WorkbookEditor session that
/// produced it. It allows supported cells to be read, set, and erased in a
/// sparse store. Dirty edits are persisted only when the owning
/// WorkbookEditor::save_as() flushes the store into the Patch plan.
///
/// This first slice writes text as inline strings, formulas as formula text, and
/// booleans/numbers as scalar cells. It does not migrate sharedStrings indexes,
/// validate or merge non-default style ids, evaluate formulas, rebuild
/// calcChain, update tables/drawings/defined names/range metadata, or repair
/// relationships. Non-default StyleId values are rejected by set_cell() until a
/// public existing-workbook style policy exists.
class WorksheetEditor {
public:
    /// Returns the planned worksheet name for this borrowed handle.
    [[nodiscard]] std::string_view name() const noexcept;

    /// Returns the sparse-store value for a cell, or std::nullopt if the cell is
    /// not currently represented by the materialized worksheet state.
    ///
    /// An explicit blank cell is returned as CellValue::blank().
    [[nodiscard]] std::optional<CellValue> try_cell(
        std::uint32_t row, std::uint32_t column) const;

    /// Returns the sparse-store value for a strict uppercase A1 cell reference.
    ///
    /// The reference must name exactly one cell, such as `A1` or
    /// `XFD1048576`. Lowercase references, ranges, zero or leading-zero rows,
    /// and coordinates outside Excel limits throw FastXlsxError. This
    /// convenience overload parses the reference and then uses the row/column
    /// overload; it does not add range iteration or large-file random access
    /// semantics.
    [[nodiscard]] std::optional<CellValue> try_cell(
        std::string_view cell_reference) const;

    /// Returns the sparse-store value for a cell.
    ///
    /// Missing sparse cells throw FastXlsxError so callers cannot accidentally
    /// conflate "not represented" with an explicit CellValue::blank() record.
    /// Use try_cell() when missing cells are expected. This read does not mutate
    /// the session and does not update WorkbookEditor::last_edit_error().
    [[nodiscard]] CellValue get_cell(std::uint32_t row, std::uint32_t column) const;

    /// Returns the sparse-store value for a strict uppercase A1 cell reference.
    ///
    /// Missing sparse cells and invalid references throw FastXlsxError. Like the
    /// row/column read overload, this read does not mutate the session and does
    /// not update WorkbookEditor::last_edit_error().
    [[nodiscard]] CellValue get_cell(std::string_view cell_reference) const;

    /// Sets or replaces one sparse-store cell value.
    ///
    /// Non-default StyleId handles are rejected because the first public slice
    /// has no existing-workbook style registry or migration policy. A rejected
    /// call does not mutate the sparse store.
    void set_cell(std::uint32_t row, std::uint32_t column, const CellValue& value);

    /// Sets or replaces one sparse-store cell value by strict uppercase A1
    /// reference.
    ///
    /// The reference must name exactly one cell. Invalid references are treated
    /// as mutation failures: they do not mutate the sparse store and update the
    /// owning WorkbookEditor::last_edit_error().
    void set_cell(std::string_view cell_reference, const CellValue& value);

    /// Removes one sparse-store cell record.
    ///
    /// Erasing a missing cell is a no-op and does not dirty the session.
    void erase_cell(std::uint32_t row, std::uint32_t column);

    /// Removes one sparse-store cell record by strict uppercase A1 reference.
    ///
    /// Invalid references are treated as mutation failures: they do not mutate
    /// the sparse store and update the owning WorkbookEditor::last_edit_error().
    void erase_cell(std::string_view cell_reference);

    /// Returns whether this materialized worksheet session has dirty sparse
    /// cell edits waiting for WorkbookEditor::save_as() auto-flush.
    ///
    /// API mode: In-memory / existing-workbook small-file inspection. This is a
    /// worksheet-local dirty-state probe only: it does not flush the session,
    /// does not increment WorkbookEditor::pending_change_count(), does not
    /// expose Patch EditPlan state, and does not update
    /// WorkbookEditor::last_edit_error().
    [[nodiscard]] bool has_pending_changes() const;

    /// Returns the number of active sparse cell records in this materialized
    /// worksheet, including explicit blank records.
    [[nodiscard]] std::size_t cell_count() const;

    /// Returns an owning row-major snapshot of all active sparse cell records.
    ///
    /// API mode: In-memory / existing-workbook small-file inspection. This
    /// copies coordinates and CellValue payloads out of the materialized sparse
    /// store, including explicit blank records. It does not expose iterators or
    /// references into the WorkbookEditor session, does not mutate dirty state,
    /// does not update WorkbookEditor::last_edit_error(), and does not add range
    /// iteration, metadata synchronization, or large-file low-memory random
    /// access semantics.
    [[nodiscard]] std::vector<WorksheetCellSnapshot> sparse_cells() const;

    /// Returns an owning row-major snapshot of active sparse records inside a
    /// rectangular range.
    ///
    /// API mode: In-memory / existing-workbook small-file inspection. The
    /// CellRange is 1-based and inclusive, and is validated against Excel
    /// worksheet limits. Only active sparse records currently present in the
    /// materialized store are returned; missing cells inside the range are not
    /// synthesized as blanks. This is a filtered snapshot convenience, not a
    /// dense matrix read, streaming iterator, metadata recalculation, or
    /// large-file low-memory random access API. It does not mutate dirty state
    /// and does not update WorkbookEditor::last_edit_error().
    [[nodiscard]] std::vector<WorksheetCellSnapshot> sparse_cells(CellRange range) const;

    /// Returns the current CellStore memory estimate for this materialized
    /// worksheet. This is not process RSS and excludes package/write buffers.
    [[nodiscard]] std::size_t estimated_memory_usage() const;

private:
    friend class WorkbookEditor;

    WorksheetEditor(
        WorkbookEditor* owner, std::string planned_name, std::uint64_t owner_generation);

    [[nodiscard]] const WorkbookEditor& owner() const;
    [[nodiscard]] WorkbookEditor& owner();

    WorkbookEditor* owner_ = nullptr;
    std::string planned_name_;
    std::uint64_t owner_generation_ = 0;
};

/// Edits an existing XLSX workbook by replacing whole-sheet data, then writes a
/// new package.
///
/// API mode: Patch. WorkbookEditor opens an existing OpenXML package and exposes
/// a narrow, workbook-language editing surface over the internal part-level Patch
/// engine. It is the first landed public slice of the planned existing-file
/// editing facade and intentionally exposes only:
///
/// - open an existing `.xlsx` by path,
/// - inspect worksheet names,
/// - replace the entire `<sheetData>` of an existing worksheet, addressed by
///   sheet name, from caller-supplied CellValue rows,
/// - rename a worksheet's sheet-catalog name (the `<sheets><sheet name="...">`
///   attribute written into the saved package),
/// - write the edited workbook to a new path with save_as().
///
/// What this facade does for a replaced sheet:
///
/// - It performs a bounded local rewrite that replaces only the worksheet's
///   `<sheetData>` element while preserving the surrounding worksheet XML
///   (sheet properties, dimensions, views, columns, filters, merged ranges,
///   data validations, conditional formatting, hyperlinks, drawings, and other
///   worksheet metadata are kept as-is, not repaired).
/// - It preserves unknown and unmodified package parts byte-for-byte where the
///   underlying Patch path allows, including `xl/sharedStrings.xml`,
///   `xl/styles.xml`, relationships, content types, and docProps.
/// - It requests workbook recalculation on load (`fullCalcOnLoad`) and removes a
///   stale `xl/calcChain.xml` when present; it never invents a calcChain when the
///   source workbook has none.
///
/// Memory and scope: replacement rows are buffered in a sparse CellStore and
/// emitted as a pull-based `<sheetData>` chunk source; the internal Patch helper
/// streams source/planned worksheet XML, consumes that replacement source during
/// the output rewrite, and records the rewritten worksheet as a file-backed
/// staged chunk. This is still a bounded template-fill /
/// small-to-medium editing path, not a fully low-memory large-file worksheet
/// transformer. Replacing a very large worksheet's data is rejected by the
/// underlying bounded rewrite limit rather than silently materializing an
/// unbounded worksheet.
///
/// What this facade does for a renamed sheet:
///
/// - rename_sheet() rewrites only the `<sheets><sheet name="...">` attribute in
///   `xl/workbook.xml` for the saved package. It preserves worksheet parts,
///   workbook relationships, content types, and unknown entries; it does not
///   touch defined names, formulas, tables, drawings, charts, hyperlinks, or
///   relationship targets, so it is a narrow catalog-name rewrite, not a
///   semantic sheet rename. The rename affects the editor's current planned
///   catalog view and the save_as() output catalog. source_worksheet_names()
///   and has_source_worksheet() remain available for inspecting the original
///   workbook view within the open session.
///
/// This facade now also exposes the first narrow WorksheetEditor slice for
/// small existing-workbook random cell edits. That slice is explicit
/// In-memory mode: callers opt in by calling worksheet(), which materializes
/// one existing worksheet into a sparse store owned by WorkbookEditor. Dirty
/// WorksheetEditor edits are automatically flushed into the Patch plan by
/// save_as().
///
/// Non-goals (not implemented by this slice): adding / deleting worksheets,
/// semantic sheet rename that synchronizes defined names / formulas / tables /
/// drawings / relationship targets, caller-supplied worksheet XML input,
/// shared-string index migration, style id migration or style merge,
/// relationship / content-type repair or pruning, and large-file low-memory
/// random editing.
class WorkbookEditor {
public:
    /// Opens an existing XLSX workbook for Patch-mode editing.
    ///
    /// The package is read through the internal package reader. Compressed input
    /// is supported only in builds configured with the opt-in minizip-ng backend;
    /// default builds read stored / no-compression packages.
    ///
    /// @param path Existing `.xlsx` package to edit.
    /// @throws FastXlsxError if the package cannot be opened or is malformed.
    [[nodiscard]] static WorkbookEditor open(const std::filesystem::path& path);

    /// Opens an existing XLSX workbook with Patch facade guardrails.
    ///
    /// The options currently apply only to replace_sheet_data() replacement
    /// payload construction. They do not load source worksheet cells into an
    /// in-memory random editor and do not change PackageReader compression
    /// support.
    ///
    /// @param path Existing `.xlsx` package to edit.
    /// @param options Replacement-payload guardrails for this editor.
    /// @throws FastXlsxError if the package cannot be opened or is malformed.
    [[nodiscard]] static WorkbookEditor open(
        const std::filesystem::path& path, WorkbookEditorOptions options);

    ~WorkbookEditor();

    /// Move-constructs an editor, transferring the opened source package,
    /// replacement-payload guardrails, planned catalog, queued public edits,
    /// pending diagnostics, and last_edit_error() state.
    ///
    /// Moving is only ownership transfer for this Patch facade. It does not
    /// save, commit, close, merge package state, or clear queued edits in the
    /// moved-to editor. The moved-from editor is left with no pending changes
    /// or last_edit_error(); inspection and edit/save operations that require
    /// an opened workbook throw FastXlsxError. Existing WorksheetEditor handles
    /// borrowed from the moved-from editor are invalidated; callers must
    /// reacquire handles from the moved-to editor.
    WorkbookEditor(WorkbookEditor&& other) noexcept;

    /// Move-assigns an editor, replacing this editor's opened source package,
    /// replacement-payload guardrails, planned catalog, queued public edits,
    /// pending diagnostics, and last_edit_error() state with the source
    /// editor's state.
    ///
    /// Existing target-side queued edits and diagnostics are discarded rather
    /// than merged or committed. Assigning from a clean opened editor therefore
    /// leaves the target with no pending changes and no last_edit_error(). Move
    /// assignment from an already moved-from editor leaves the target moved-from
    /// / not open and discards target-side queued state. Move assignment does
    /// not save, commit, close, or repair package state. The moved-from editor
    /// is left with no pending changes or last_edit_error(); inspection and
    /// edit/save operations that require an opened workbook throw FastXlsxError.
    /// Existing WorksheetEditor handles borrowed from either the source editor
    /// or the overwritten target editor are invalidated; callers must reacquire
    /// handles from the assigned-to editor.
    WorkbookEditor& operator=(WorkbookEditor&& other) noexcept;

    WorkbookEditor(const WorkbookEditor&) = delete;
    WorkbookEditor& operator=(const WorkbookEditor&) = delete;

    /// Returns the current planned worksheet names in sheet-catalog order.
    ///
    /// This is read-only sheet inspection for the public Patch facade. It
    /// reflects successful rename_sheet() calls queued in this editor, but does
    /// not expose workbook relationships or package parts.
    [[nodiscard]] std::vector<std::string> worksheet_names() const;

    /// Returns whether a worksheet with the given name exists in the current
    /// planned sheet catalog.
    [[nodiscard]] bool has_worksheet(std::string_view sheet_name) const;

    /// Returns the opened source workbook's worksheet names in sheet-catalog
    /// order.
    ///
    /// This source view does not reflect successful rename_sheet() calls queued
    /// in this editor. Use worksheet_names() to inspect the current planned
    /// catalog that save_as() will write.
    [[nodiscard]] std::vector<std::string> source_worksheet_names() const;

    /// Returns whether a worksheet with the given name exists in the opened
    /// source workbook sheet catalog.
    [[nodiscard]] bool has_source_worksheet(std::string_view sheet_name) const;

    /// Returns whether this editor has queued Patch-mode changes.
    ///
    /// This is a coarse save-as diagnostic for the current public WorkbookEditor
    /// facade. It reports whether successful public edits such as
    /// replace_sheet_data() or rename_sheet() have queued work for save_as(), or
    /// whether any materialized WorksheetEditor session is currently dirty and
    /// will be flushed by save_as().
    /// It does not expose EditPlan entries, dependency audits, relationship
    /// diagnostics, output-plan reasons, or a full unsaved-change model. Failed
    /// edits leave this state unchanged.
    [[nodiscard]] bool has_pending_changes() const noexcept;

    /// Returns a coarse count of successful public edit calls queued in this
    /// facade.
    ///
    /// The value is useful for diagnostics and tests that need to distinguish a
    /// clean editor from one with pending save_as() work. It is not a package
    /// part count, EditPlan size, or stable semantic diff: repeated edits to the
    /// same sheet may replace earlier queued payloads while still incrementing
    /// this public facade diagnostic.
    [[nodiscard]] std::size_t pending_change_count() const noexcept;

    /// Returns the number of explicit replacement cells currently represented by
    /// successful replace_sheet_data() calls.
    ///
    /// This is a narrow diagnostic for the public Patch facade. It sums the final
    /// queued replacement payload per sheet name, so a later successful
    /// replace_sheet_data() for the same sheet replaces the earlier payload in
    /// this count. A successful rename_sheet() moves the diagnostic to the
    /// planned sheet name; a failed rename_sheet() leaves it unchanged. It does
    /// not count source workbook cells, renamed sheets, preserved worksheet
    /// metadata, shared strings, styles, relationships, or any future in-memory
    /// editor state.
    [[nodiscard]] std::size_t pending_replacement_cell_count() const noexcept;

    /// Returns the planned worksheet names that currently have queued
    /// replace_sheet_data() payloads.
    ///
    /// Names are reported in the current planned sheet-catalog order. A
    /// successful rename_sheet() moves the pending replacement diagnostic to the
    /// planned new sheet name. Failed edits leave this list unchanged. This is a
    /// public facade diagnostic only; it does not expose internal EditPlan
    /// entries, source workbook cells, preserved worksheet metadata,
    /// relationships, shared strings, styles, or save-time package entries.
    [[nodiscard]] std::vector<std::string> pending_replacement_worksheet_names() const;

    /// Returns planned worksheet names for dirty materialized WorksheetEditor
    /// sessions that still need save_as() auto-flush.
    ///
    /// API mode: In-memory / existing-workbook small-file diagnostics. Names
    /// are reported in the current planned sheet-catalog order. Clean
    /// materialized sessions are omitted. A successful save_as() flushes dirty
    /// sessions into the Patch plan and clears them from this list; this method
    /// does not itself flush, increment pending_change_count(), expose internal
    /// EditPlan state, include whole-<sheetData> replacement payloads, or update
    /// last_edit_error(). It returns an empty vector for a moved-from editor.
    [[nodiscard]] std::vector<std::string> pending_materialized_worksheet_names() const;

    /// Returns the total active sparse cell records in dirty materialized
    /// WorksheetEditor sessions waiting for save_as() auto-flush.
    ///
    /// API mode: In-memory / existing-workbook small-file diagnostics. This is
    /// a workbook-level aggregate over dirty materialized sessions only. Clean
    /// materialized sessions and queued whole-<sheetData> replacements are not
    /// counted. The value includes explicit blank records, matches the sum of
    /// the dirty sessions' WorksheetEditor::cell_count() values, and returns 0
    /// for a moved-from editor. This method does not flush, increment
    /// pending_change_count(), expose internal EditPlan state, or update
    /// last_edit_error().
    [[nodiscard]] std::size_t pending_materialized_cell_count() const noexcept;

    /// Returns the total CellStore memory estimate for dirty materialized
    /// WorksheetEditor sessions waiting for save_as() auto-flush.
    ///
    /// The estimate matches the sum of dirty sessions'
    /// WorksheetEditor::estimated_memory_usage() values. It is not process RSS
    /// and excludes source package bytes, generated XML chunks, PackageEditor
    /// staging files, ZIP writer buffers, and save-time package assembly costs.
    /// Clean materialized sessions and queued whole-<sheetData> replacements
    /// are not counted. This method does not flush, increment
    /// pending_change_count(), expose internal EditPlan state, or update
    /// last_edit_error().
    [[nodiscard]] std::size_t estimated_pending_materialized_memory_usage()
        const noexcept;

    /// Returns whether the current planned worksheet name has a queued
    /// replace_sheet_data() payload.
    ///
    /// This follows the same planned-name semantics as
    /// pending_replacement_worksheet_names(). It returns false for a moved-from
    /// editor.
    [[nodiscard]] bool has_pending_replacement(std::string_view sheet_name) const noexcept;

    /// Returns the estimated sparse-store memory used to prepare final queued
    /// replacement payloads.
    ///
    /// The estimate is the sum of the temporary CellStore estimates recorded at
    /// successful replace_sheet_data() calls. It is useful for checking the
    /// current Patch facade guardrail behavior, but it is not a process RSS
    /// measurement and excludes generated XML chunks, PackageEditor state and
    /// staging files, source package bytes, ZIP writer buffers, and save-time
    /// assembly costs. If rename_sheet() changes a sheet to a temporary planned
    /// name and a later rename_sheet() changes it back to the source name, this
    /// diagnostic follows the restored planned name and does not keep a stale
    /// renamed-sheet entry.
    [[nodiscard]] std::size_t estimated_pending_replacement_memory_usage() const noexcept;

    /// Returns the most recent failed public edit diagnostic, if any.
    ///
    /// This optional message is a coarse public facade diagnostic for failed
    /// replace_sheet_data() or rename_sheet() calls. Successful public edits
    /// clear it. Inspection / pending diagnostic methods and save_as() do not
    /// update it, and a moved-from editor returns std::nullopt. The message is
    /// not an exception stack, internal EditPlan diagnostic, relationship audit,
    /// dependency audit, or package output-plan reason.
    [[nodiscard]] std::optional<std::string> last_edit_error() const;

    /// Returns coarse worksheet-level summaries for pending public edits.
    ///
    /// The returned vector follows source workbook sheet-catalog order and only
    /// includes worksheets with a queued public rename_sheet(),
    /// replace_sheet_data() effect, and/or dirty materialized WorksheetEditor
    /// session waiting for save_as() auto-flush. Each summary reports the source
    /// name, current planned name, whether the sheet was renamed, whether a
    /// final whole-<sheetData> replacement is queued for that planned name, and
    /// whether a dirty materialized session currently exists. As a
    /// current planned-state view, a rename-only chain that returns a sheet to
    /// its source name is omitted from this vector even though
    /// pending_change_count() still counts the successful public edit calls.
    /// Dirty materialized sessions are omitted after successful save_as()
    /// auto-flush unless another queued rename or replacement still applies.
    /// This method does not itself flush, increment pending_change_count(), or
    /// update last_edit_error(). This is a public facade diagnostic; it does not
    /// expose internal EditPlan entries, dependency audits, relationship audits,
    /// preserved metadata, source cell counts, package parts, or save-time
    /// output-plan reasons. It returns an empty vector for a moved-from editor.
    [[nodiscard]] std::vector<WorkbookEditorWorksheetEditSummary> pending_worksheet_edits()
        const;

    /// Returns the source-to-planned worksheet catalog view.
    ///
    /// Entries follow the opened source workbook sheet-catalog order. For each
    /// source worksheet, the entry reports the original source name, the current
    /// planned name that worksheet_names() / save_as() will use, and whether the
    /// name has been changed by a successful rename_sheet() call. This is a
    /// public facade diagnostic; it does not expose workbook relationships,
    /// worksheet part names, package entries, internal EditPlan entries, or
    /// dependency audits. It returns an empty vector for a moved-from editor.
    [[nodiscard]] std::vector<WorkbookEditorWorksheetCatalogEntry> worksheet_catalog() const;

    /// Materializes an existing worksheet for small-file random cell editing.
    ///
    /// API mode: In-memory over an existing workbook. The returned
    /// WorksheetEditor is a borrowed handle into WorkbookEditor-owned sparse
    /// worksheet state. It is valid only while this WorkbookEditor object
    /// remains alive and is not moved or move-assigned; callers should reacquire
    /// a handle after ownership transfer. Repeated worksheet() calls for the
    /// same planned sheet reuse the same materialized session when options
    /// match, preserving dirty edits.
    ///
    /// Source worksheet cells are loaded through the internal event reader into
    /// a sparse CellStore; this does not build a full worksheet matrix. The
    /// loader currently accepts only supported scalar cell shapes and rejects
    /// shared-string cells, source style attributes, unsupported source cell
    /// metadata, and malformed worksheet XML before returning a handle.
    ///
    /// Operation mixing: a worksheet with a queued replace_sheet_data() payload
    /// cannot be materialized, and replace_sheet_data() / rename_sheet() reject
    /// a worksheet after it has been materialized. Use one editing mode per
    /// worksheet in this first public slice.
    ///
    /// @param sheet_name Existing worksheet name in the current planned catalog.
    /// @param options Per-materialization sparse-store guardrails.
    /// @throws FastXlsxError if the workbook is not open, the sheet is missing,
    /// a whole-sheet replacement is already queued for this sheet, options do
    /// not match an existing materialized session, or source worksheet loading
    /// fails. Materialization failures do not update last_edit_error().
    [[nodiscard]] WorksheetEditor worksheet(
        std::string_view sheet_name, WorksheetEditorOptions options = {});

    /// Tries to materialize an existing worksheet for small-file random cell
    /// editing.
    ///
    /// API mode: In-memory over an existing workbook. This has the same
    /// borrowed-handle and guardrail semantics as worksheet(), except a missing
    /// current-planned worksheet name returns std::nullopt instead of throwing.
    /// Other failures still throw FastXlsxError, including queued same-sheet
    /// replace_sheet_data() payloads, option mismatches with an existing
    /// materialized session, unsupported source worksheet cell shapes, or
    /// malformed worksheet XML. Missing-sheet and materialization failures do
    /// not update last_edit_error().
    ///
    /// @param sheet_name Existing worksheet name in the current planned catalog.
    /// @param options Per-materialization sparse-store guardrails.
    /// @throws FastXlsxError if the workbook is not open or if a non-missing
    /// materialization failure occurs.
    [[nodiscard]] std::optional<WorksheetEditor> try_worksheet(
        std::string_view sheet_name, WorksheetEditorOptions options = {});

    /// Replaces the entire `<sheetData>` of an existing worksheet from rows of
    /// CellValue.
    ///
    /// `rows[i]` is worksheet row `i + 1`; `rows[i][j]` is column `j + 1`. A
    /// blank CellValue emits an empty cell at its position. Empty row vectors
    /// advance the input row mapping but do not emit an explicit empty row. Text
    /// values are written as inline strings; the existing `xl/sharedStrings.xml`
    /// is preserved rather than migrated. A CellValue's optional StyleId is
    /// written as the cell's style attribute as-is; it is not validated against
    /// the target workbook's style registry. Calling this again for the same
    /// sheet replaces the previously queued data for that sheet.
    /// If rename_sheet() has already queued a catalog rename in this editor,
    /// sheet lookup follows the current planned catalog for Patch output, so
    /// callers should use the planned new name for follow-up replacements.
    /// Missing names are rejected against that current planned catalog before
    /// the replacement payload is built, so a missing-sheet failure does not
    /// spend replacement guardrail budget or record pending replacement
    /// diagnostics.
    ///
    /// @param sheet_name Existing worksheet name to edit.
    /// @param rows Replacement worksheet rows, in row-major order.
    /// @throws FastXlsxError if the sheet name is missing or ambiguous, or if the
    /// generated payload or worksheet exceeds the bounded local-rewrite limit. On
    /// failure no edit state is mutated and the editor remains usable.
    void replace_sheet_data(
        std::string_view sheet_name, const std::vector<std::vector<CellValue>>& rows);

    /// Renames a worksheet's sheet-catalog name for the saved package.
    ///
    /// This rewrites only the `<sheets><sheet name="...">` attribute in
    /// `xl/workbook.xml`. The worksheet part, workbook relationships, content
    /// types, and unknown entries are preserved; defined names, formulas, tables,
    /// drawings, charts, hyperlinks, and relationship targets are not touched, so
    /// this is a narrow catalog-name rewrite rather than a semantic sheet rename.
    /// The new name is XML-escaped. Duplicate-name checks are conservative and
    /// ASCII case-insensitive. The rename takes effect in worksheet_names() /
    /// has_worksheet() and in the save_as() output; source_worksheet_names() /
    /// has_source_worksheet() keep reporting the original source workbook view
    /// within the open session.
    ///
    /// @param old_name Existing worksheet name to rename.
    /// @param new_name New sheet-catalog name.
    /// @throws FastXlsxError if `old_name` is not present, if `new_name` already
    /// exists (ASCII case-insensitive) or contains invalid characters, or if
    /// `xl/workbook.xml` is unavailable. On failure no edit state is mutated,
    /// pending replacement diagnostics remain under their prior sheet name, and
    /// the editor remains usable. A successful rename back to the source sheet
    /// name clears the public renamed flag and migrates any queued replacement
    /// diagnostics back to that source name; it is still only a catalog-name
    /// rewrite, not semantic sheet rename synchronization.
    void rename_sheet(std::string_view old_name, std::string new_name);

    /// Writes the edited workbook to a new package path.
    ///
    /// This never edits the source package in place. It rejects an output path
    /// that is the source package, empty, an existing directory, or has a
    /// non-directory / missing parent. Worksheets not edited and unknown parts are
    /// copied from the source package; with no queued public edits, save_as()
    /// writes a reader-backed roundtrip copy. A rejected or failed save_as() does
    /// not clear queued public edits and does not update last_edit_error().
    /// Successful save_as() is also not a commit/close operation: queued public
    /// edit diagnostics remain visible so callers may save the same planned
    /// state to another output path or continue editing.
    ///
    /// @param path Output `.xlsx` path; must differ from the source package.
    /// @throws FastXlsxError if the output path is rejected or the package cannot
    /// be written.
    void save_as(const std::filesystem::path& path);

private:
    friend class WorksheetEditor;
#ifdef FASTXLSX_ENABLE_TEST_HOOKS
    friend void detail::testing_workbook_editor_materialize_source_sheet(
        WorkbookEditor& editor,
        std::string_view planned_name,
        std::string_view source_sheet_name);
    friend void detail::testing_workbook_editor_set_materialized_cell(
        WorkbookEditor& editor,
        std::string_view planned_name,
        std::uint32_t row,
        std::uint32_t column,
        const CellValue& value);
    friend void detail::testing_workbook_editor_erase_materialized_cell(
        WorkbookEditor& editor,
        std::string_view planned_name,
        std::uint32_t row,
        std::uint32_t column);
    friend void detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(
        WorkbookEditor& editor);
    friend std::size_t detail::testing_workbook_editor_materialized_session_count(
        const WorkbookEditor& editor) noexcept;
    friend std::size_t detail::testing_workbook_editor_dirty_materialized_session_count(
        const WorkbookEditor& editor) noexcept;
    friend bool detail::testing_workbook_editor_has_materialized_session(
        const WorkbookEditor& editor, std::string_view planned_name) noexcept;
    friend std::vector<std::string>
    detail::testing_workbook_editor_dirty_materialized_session_names(
        const WorkbookEditor& editor);
#endif
    WorkbookEditor();

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint64_t handle_generation_ = 0;
};

} // namespace fastxlsx
