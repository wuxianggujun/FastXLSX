#pragma once

/// @file workbook_editor.hpp
/// Minimal public Patch-mode facade for editing an existing XLSX workbook.

#include <fastxlsx/cell_value.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx {

/// Public guardrails for the current narrow WorkbookEditor Patch facade.
///
/// API mode: Patch. These limits apply only to replacement rows passed to
/// WorkbookEditor::replace_sheet_data(). They do not materialize source
/// worksheet cells, do not enable random editing, and are not workbook-level
/// memory limits for a future WorksheetEditor.
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
    /// used to build the replacement <sheetData> payload. It is not exact process
    /// RSS tracking and does not include source package bytes, the generated XML
    /// string, ZIP writer buffers, or future save-time assembly costs.
    std::optional<std::size_t> replacement_memory_budget_bytes;
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
/// Memory and scope: a replaced worksheet's XML is materialized in memory and the
/// replacement rows are buffered before serialization. This is a bounded
/// template-fill / small-to-medium editing path, not a low-memory large-file
/// worksheet transformer. Replacing a very large worksheet's data is rejected by
/// the underlying bounded rewrite limit rather than silently materializing an
/// unbounded worksheet.
///
/// What this facade does for a renamed sheet:
///
/// - rename_sheet() rewrites only the `<sheets><sheet name="...">` attribute in
///   `xl/workbook.xml` for the saved package. It preserves worksheet parts,
///   workbook relationships, content types, and unknown entries; it does not
///   touch defined names, formulas, tables, drawings, charts, hyperlinks, or
///   relationship targets, so it is a narrow catalog-name rewrite, not a
///   semantic sheet rename. The rename affects only the save_as() output
///   catalog; worksheet_names() and has_worksheet() keep reporting the source
///   workbook view within the open session.
///
/// Non-goals (not implemented by this slice): random cell read/write
/// (`get_cell` / `set_cell`), adding / deleting worksheets, semantic sheet
/// rename that synchronizes defined names / formulas / tables / drawings /
/// relationship targets, caller-supplied worksheet XML input, shared-string
/// index migration, style id migration or style merge, relationship /
/// content-type repair or pruning, and large-file streaming worksheet
/// rewriting.
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

    WorkbookEditor(WorkbookEditor&& other) noexcept;
    WorkbookEditor& operator=(WorkbookEditor&& other) noexcept;
    WorkbookEditor(const WorkbookEditor&) = delete;
    WorkbookEditor& operator=(const WorkbookEditor&) = delete;

    /// Returns the source workbook's worksheet names in sheet-catalog order.
    ///
    /// This is read-only sheet inspection. It reflects the source workbook and
    /// does not expose workbook relationships or package parts.
    [[nodiscard]] std::vector<std::string> worksheet_names() const;

    /// Returns whether a worksheet with the given name exists in the source
    /// workbook sheet catalog.
    [[nodiscard]] bool has_worksheet(std::string_view sheet_name) const;

    /// Returns whether this editor has queued Patch-mode changes.
    ///
    /// This is a coarse save-as diagnostic for the current public WorkbookEditor
    /// facade. It reports whether successful public edits such as
    /// replace_sheet_data() or rename_sheet() have queued work for save_as().
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
    /// this count. It does not count source workbook cells, renamed sheets,
    /// preserved worksheet metadata, shared strings, styles, relationships, or
    /// any future in-memory editor state.
    [[nodiscard]] std::size_t pending_replacement_cell_count() const noexcept;

    /// Returns the estimated sparse-store memory used to prepare final queued
    /// replacement payloads.
    ///
    /// The estimate is the sum of the temporary CellStore estimates recorded at
    /// successful replace_sheet_data() calls. It is useful for checking the
    /// current Patch facade guardrail behavior, but it is not a process RSS
    /// measurement and excludes generated XML strings, PackageEditor state,
    /// source package bytes, ZIP writer buffers, and save-time assembly costs.
    [[nodiscard]] std::size_t estimated_pending_replacement_memory_usage() const noexcept;

    /// Replaces the entire `<sheetData>` of an existing worksheet from rows of
    /// CellValue.
    ///
    /// `rows[i]` is worksheet row `i + 1`; `rows[i][j]` is column `j + 1`. A
    /// blank CellValue emits an empty cell at its position. Text values are
    /// written as inline strings; the existing `xl/sharedStrings.xml` is
    /// preserved rather than migrated. A CellValue's optional StyleId is written
    /// as the cell's style attribute as-is; it is not validated against the
    /// target workbook's style registry. Calling this again for the same sheet
    /// replaces the previously queued data for that sheet.
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
    /// ASCII case-insensitive. The rename takes effect in the save_as() output;
    /// worksheet_names() and has_worksheet() keep reporting the source workbook
    /// view within the open session.
    ///
    /// @param old_name Existing worksheet name to rename.
    /// @param new_name New sheet-catalog name.
    /// @throws FastXlsxError if `old_name` is not present, if `new_name` already
    /// exists (ASCII case-insensitive) or contains invalid characters, or if
    /// `xl/workbook.xml` is unavailable. On failure no edit state is mutated and
    /// the editor remains usable.
    void rename_sheet(std::string_view old_name, std::string new_name);

    /// Writes the edited workbook to a new package path.
    ///
    /// This never edits the source package in place. It rejects an output path
    /// that is the source package, empty, an existing directory, or has a
    /// non-directory / missing parent. Worksheets not edited and unknown parts are
    /// copied from the source package.
    ///
    /// @param path Output `.xlsx` path; must differ from the source package.
    /// @throws FastXlsxError if the output path is rejected or the package cannot
    /// be written.
    void save_as(const std::filesystem::path& path) const;

private:
    WorkbookEditor();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fastxlsx
