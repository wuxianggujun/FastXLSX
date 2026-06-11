#pragma once

/// @file workbook_editor.hpp
/// Minimal public Patch-mode facade for editing an existing XLSX workbook.

#include <fastxlsx/cell_value.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx {

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
/// Non-goals (not implemented by this slice): random cell read/write
/// (`get_cell` / `set_cell`), adding / deleting / renaming worksheets,
/// caller-supplied worksheet XML input, shared-string index migration, style id
/// migration or style merge, relationship / content-type repair or pruning, and
/// large-file streaming worksheet rewriting.
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
