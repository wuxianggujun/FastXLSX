#pragma once

#include <fastxlsx/workbook.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx {

/// Semantic value projected by the bounded worksheet reader.
///
/// API mode: Streaming read / existing workbook. Formula text is exposed
/// separately on WorksheetCellView, so this enum describes the ordinary cell
/// value or a formula's cached result. Shared strings remain workbook-local
/// indexes in the first reader slice; they are not resolved into an in-memory
/// shared-string table.
enum class WorksheetCellValueKind {
    Blank,
    Number,
    Text,
    Boolean,
    Error,
    SharedStringIndex,
};

/// One forward-only worksheet row callback view.
struct WorksheetRowView {
    /// One-based worksheet row number.
    std::uint32_t row = 0;
};

/// One forward-only worksheet cell callback view.
///
/// reference, text_value, and formula_text are borrowed views valid only for
/// the duration of the current on_cell callback. Copy data that must outlive the
/// callback. Number, boolean, shared-string index, style index, row, and column
/// are ordinary values.
///
/// A formula cell exposes has_formula=true and decoded formula_text. Its cached
/// scalar, when present, is projected through has_value/value_kind and the same
/// typed fields as a non-formula cell. FastXLSX does not evaluate formulas or
/// generate cached values.
struct WorksheetCellView {
    /// One-based cell row parsed from the source A1 reference.
    std::uint32_t row = 0;

    /// One-based cell column parsed from the source A1 reference.
    std::uint32_t column = 0;

    /// Source A1 cell reference. Callback-lifetime only.
    std::string_view reference;

    /// Whether the cell contains an ordinary value or cached formula result.
    bool has_value = false;

    /// Projected value kind. Blank is used when has_value is false.
    WorksheetCellValueKind value_kind = WorksheetCellValueKind::Blank;

    /// Numeric payload when value_kind is Number.
    double number_value = 0.0;

    /// Decoded Text or Error payload. Callback-lifetime only.
    std::string_view text_value;

    /// Boolean payload when value_kind is Boolean.
    bool boolean_value = false;

    /// Syntactically validated workbook-local sharedStrings index.
    ///
    /// The first reader slice verifies the index token and the presence of a
    /// workbook sharedStrings relationship, but does not load the table or
    /// validate the index against its item count.
    std::uint32_t shared_string_index = 0;

    /// Whether this cell contains formula markup.
    bool has_formula = false;

    /// Decoded formula text. Callback-lifetime only and never evaluated.
    std::string_view formula_text;

    /// Whether the source cell explicitly carries an `s` style reference.
    bool has_style = false;

    /// Opaque workbook-local cellXfs index from the source `s` attribute.
    ///
    /// The first reader slice validates integer syntax but does not load
    /// styles.xml or validate the index against cellXfs count.
    std::uint32_t style_index = 0;
};

/// Callbacks used by WorkbookReader::read_worksheet().
///
/// All callbacks are optional. When present they are invoked synchronously in
/// source order as row start, zero or more cells, and row end. Exceptions thrown
/// by callbacks propagate unchanged; the active package-entry stream is closed
/// during stack unwinding and the WorkbookReader remains reusable.
struct WorksheetReadCallbacks {
    std::function<void(const WorksheetRowView&)> on_row_start;
    std::function<void(const WorksheetCellView&)> on_cell;
    std::function<void(const WorksheetRowView&)> on_row_end;
};

/// Guardrails for one bounded worksheet traversal.
struct WorksheetReaderOptions {
    /// Maximum bytes retained by the worksheet XML token window.
    ///
    /// A single XML tag or text token that cannot be completed within this
    /// window is rejected. This limit is independent of worksheet size.
    std::size_t max_xml_window_bytes = 64U * 1024U;

    /// Maximum decoded text retained for the active cell.
    ///
    /// Formula text plus scalar/inline cached text is counted together. Reader
    /// memory is therefore bounded by this limit, max_xml_window_bytes, the
    /// package reader's fixed input buffer, and small workbook metadata.
    std::size_t max_cell_text_bytes = 64U * 1024U;
};

/// Summary returned after one successful worksheet traversal.
struct WorksheetReadSummary {
    std::uint64_t row_count = 0;
    std::uint64_t cell_count = 0;
    std::size_t peak_cell_text_bytes = 0;
};

/// Existing-workbook bounded-memory worksheet reader.
///
/// API mode: Streaming read. WorkbookReader indexes small package/workbook
/// metadata at open(), then opens a fresh worksheet ZIP-entry stream for each
/// read_worksheet() call. Rows and cells are delivered once in source order;
/// there is no seek, worksheet DOM, dense matrix, mutation, Patch staging, or
/// In-memory WorksheetEditor handoff.
///
/// The first public slice supports blank, finite numeric, boolean, simple
/// inline/text/date-token, error, shared-string-index, simple formula text,
/// cached scalar, and opaque style-index projection. Rich inline strings,
/// phonetic/extension cell metadata, formula attributes such as shared/array
/// formula metadata, unsupported cell attributes, and malformed/out-of-order
/// worksheet structure fail explicitly instead of being flattened.
class WorkbookReader {
public:
    /// Opens and indexes an existing XLSX package.
    ///
    /// This reads ZIP/package metadata plus the small workbook catalog, but does
    /// not open or materialize worksheet entries.
    ///
    /// @throws FastXlsxError if the package, OPC metadata, workbook catalog, or
    /// worksheet relationships are missing, malformed, or unsupported.
    [[nodiscard]] static WorkbookReader open(std::filesystem::path path);

    ~WorkbookReader();

    WorkbookReader(WorkbookReader&&) noexcept;
    WorkbookReader& operator=(WorkbookReader&&) noexcept;

    WorkbookReader(const WorkbookReader&) = delete;
    WorkbookReader& operator=(const WorkbookReader&) = delete;

    /// Returns the opened source path, or an empty path for a moved-from reader.
    [[nodiscard]] const std::filesystem::path& source_path() const noexcept;

    /// Returns source worksheet names in workbook order.
    ///
    /// The returned reference remains valid until this reader is moved from or
    /// destroyed.
    [[nodiscard]] const std::vector<std::string>& worksheet_names() const noexcept;

    /// Returns whether the source workbook contains an exact matching name.
    [[nodiscard]] bool has_worksheet(std::string_view sheet_name) const noexcept;

    /// Traverses one worksheet in source order with bounded memory.
    ///
    /// A fresh package-entry source is owned only for this call. Callback string
    /// views are valid only during their callback. Successful return closes the
    /// entry after CRC/size validation; callback/parser/package failures release
    /// it during unwinding and a later call may retry from the beginning.
    ///
    /// @throws FastXlsxError if the reader is moved from, the sheet is absent,
    /// options are zero, package reading fails, or worksheet XML/projection is
    /// malformed, unsupported, duplicated, or out of source order. User callback
    /// exceptions propagate unchanged.
    [[nodiscard]] WorksheetReadSummary read_worksheet(
        std::string_view sheet_name,
        const WorksheetReadCallbacks& callbacks = {},
        WorksheetReaderOptions options = {}) const;

private:
    struct Impl;

    explicit WorkbookReader(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace fastxlsx
