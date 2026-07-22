#pragma once

#include <fastxlsx/workbook.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
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

/// One forward-only shared-string item callback view.
///
/// text is a borrowed view valid only for the duration of the current on_item
/// callback. Copy it when the value must outlive the callback. index is the
/// zero-based workbook-local index used by worksheet cells with t="s".
struct SharedStringItemView {
    std::uint32_t index = 0;
    std::string_view text;
};

/// Callbacks used by WorkbookReader::read_shared_strings().
///
/// The callback is optional and is invoked synchronously once per item in
/// source/index order. Exceptions propagate unchanged; the active package-entry
/// stream is released during unwinding and a later traversal starts over.
struct SharedStringReadCallbacks {
    std::function<void(const SharedStringItemView&)> on_item;
};

/// Guardrails for one bounded sharedStrings traversal.
struct SharedStringReaderOptions {
    /// Maximum bytes retained by the sharedStrings XML token window.
    ///
    /// A single XML tag or text token that cannot be completed within this
    /// window is rejected. This limit is independent of the part's total size.
    std::size_t max_xml_window_bytes = 64U * 1024U;

    /// Maximum decoded text retained for the active shared-string item.
    std::size_t max_item_text_bytes = 64U * 1024U;
};

/// Summary returned after one successful sharedStrings traversal.
struct SharedStringReadSummary {
    std::uint64_t item_count = 0;
    std::size_t peak_item_text_bytes = 0;
};

/// Horizontal alignment projected from one narrow cellXfs record.
enum class CellFormatHorizontalAlignment {
    Left,
    Center,
    Right,
};

/// Vertical alignment projected from one narrow cellXfs record.
enum class CellFormatVerticalAlignment {
    Top,
    Center,
    Bottom,
};

/// One custom number-format definition emitted by read_cell_formats().
///
/// format_code is a borrowed decoded view valid only for the duration of the
/// current on_number_format callback. The id is workbook-local and is not a
/// portable handle for another workbook.
struct NumberFormatView {
    std::uint32_t id = 0;
    std::string_view format_code;
};

/// Narrow alignment metadata owned by one CellFormatView value.
///
/// Optional fields preserve the distinction between an absent OpenXML
/// attribute and an explicit false/default value.
struct CellFormatAlignmentView {
    std::optional<bool> wrap_text;
    std::optional<CellFormatHorizontalAlignment> horizontal;
    std::optional<CellFormatVerticalAlignment> vertical;
};

/// One zero-based workbook-local cellXfs record.
///
/// All fields are callback-independent values. number_format_id, font_id, and
/// fill_id remain opaque workbook-local references; this bounded companion does
/// not build or resolve complete number-format, font, or fill tables. Custom
/// number-format definitions are instead emitted separately through
/// CellFormatReadCallbacks::on_number_format.
struct CellFormatView {
    /// Zero-based source index referenced by worksheet cell `s` attributes.
    std::uint32_t index = 0;

    std::uint32_t number_format_id = 0;
    std::uint32_t font_id = 0;
    std::uint32_t fill_id = 0;

    std::optional<bool> apply_number_format;
    std::optional<bool> apply_font;
    std::optional<bool> apply_fill;
    std::optional<bool> apply_alignment;

    std::optional<CellFormatAlignmentView> alignment;
};

/// Callbacks used by WorkbookReader::read_cell_formats().
///
/// Callbacks are optional and run synchronously in source order. User
/// exceptions propagate unchanged; the active package-entry stream is released
/// during unwinding and a later traversal starts over.
struct CellFormatReadCallbacks {
    std::function<void(const NumberFormatView&)> on_number_format;
    std::function<void(const CellFormatView&)> on_cell_format;
};

/// Guardrails for one bounded styles/cellXfs traversal.
struct CellFormatReaderOptions {
    /// Maximum bytes retained by the styles XML token window.
    std::size_t max_xml_window_bytes = 64U * 1024U;

    /// Maximum decoded bytes retained for one active custom format code.
    std::size_t max_format_code_bytes = 64U * 1024U;

    /// Maximum XML element nesting depth retained by the structural stack.
    std::size_t max_xml_nesting_depth = 64U;

    /// Maximum custom number-format ids retained for duplicate detection.
    std::size_t max_custom_number_format_count = 64U * 1024U;
};

/// Summary returned after one successful styles/cellXfs traversal.
struct CellFormatReadSummary {
    std::uint64_t custom_number_format_count = 0;
    std::uint64_t cell_format_count = 0;
    std::size_t peak_format_code_bytes = 0;
    std::size_t peak_xml_nesting_depth = 0;
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
///
/// read_shared_strings() is a separate forward-only companion. It resolves and
/// audits the workbook sharedStrings relationship, target part, and content
/// type, then projects simple `<si><t>...</t></si>` items. Rich runs, phonetic
/// metadata, extensions, and other item markup fail explicitly instead of
/// being flattened. It does not build a shared-string table or automatically
/// resolve indexes emitted by read_worksheet().
///
/// read_cell_formats() is another separate bounded companion. It emits custom
/// number-format definitions and narrow cellXfs records without building a
/// complete styles registry. Worksheet style indexes, font/fill references,
/// and number-format references remain explicitly caller-resolved.
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

    /// Traverses sharedStrings items in zero-based index order with bounded memory.
    ///
    /// A fresh package-entry source is owned only for this call. Callback text
    /// is valid only during its callback. The relationship must be unique and
    /// internal, its target must exist, and the target must carry the standard
    /// sharedStrings content type. Successful return closes the entry after
    /// CRC/size validation; callback/parser/package failures release it during
    /// unwinding and a later call may retry from the beginning.
    ///
    /// The current projection accepts only one simple text element per item.
    /// It rejects rich runs, phonetic data, extension metadata, nested markup,
    /// malformed XML, and item indexes beyond uint32_t. Root count/uniqueCount
    /// attributes are syntax-checked when present but are not cross-validated
    /// against worksheet references.
    ///
    /// @throws FastXlsxError if the reader is moved from, options are zero, the
    /// relationship/content-type/target audit fails, package reading fails, or
    /// sharedStrings XML is malformed or unsupported. User callback exceptions
    /// propagate unchanged.
    [[nodiscard]] SharedStringReadSummary read_shared_strings(
        const SharedStringReadCallbacks& callbacks = {},
        SharedStringReaderOptions options = {}) const;

    /// Traverses custom number formats and cellXfs records with bounded memory.
    ///
    /// A fresh styles package-entry source is owned only for this call. The
    /// styles relationship must be unique and internal, its normalized target
    /// must exist, and the target must carry the standard styles content type.
    /// Successful return closes the entry after CRC/size validation; callback,
    /// parser, or package failures release it during unwinding and a later call
    /// may retry from the beginning.
    ///
    /// Custom number formats are emitted separately from cellXfs records;
    /// format_code is callback-lifetime only. Cell format fields are owning
    /// scalar values, but number-format/font/fill ids remain opaque workbook-
    /// local references. The current projection accepts zero border/base-style
    /// references and narrow wrap/left-center-right/top-center-bottom alignment.
    /// Borders, protection, extension children, other alignment semantics, and
    /// other cell-format metadata fail explicitly instead of being flattened.
    /// Container counts are syntax-checked and matched to emitted direct
    /// records, but ids are not cross-resolved against complete component
    /// tables or worksheet references.
    ///
    /// This read-only traversal does not seek, build a styles DOM/registry,
    /// resolve WorksheetCellView::style_index automatically, mutate the package,
    /// or hand styles to Patch or In-memory editors.
    ///
    /// @throws FastXlsxError if the reader is moved from, an option is zero, the
    /// relationship/content-type/target audit fails, package reading fails, or
    /// styles XML is malformed or outside the narrow projection. User callback
    /// exceptions propagate unchanged.
    [[nodiscard]] CellFormatReadSummary read_cell_formats(
        const CellFormatReadCallbacks& callbacks = {},
        CellFormatReaderOptions options = {}) const;

private:
    struct Impl;

    explicit WorkbookReader(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace fastxlsx
