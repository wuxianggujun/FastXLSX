#pragma once

#include <fastxlsx/document_properties.hpp>
#include <fastxlsx/workbook.hpp>

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx {

class WorksheetWriter;

namespace detail {
struct WorkbookWriterState;
struct WorksheetWriterState;

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
void testing_set_worksheet_row_count(WorksheetWriter& worksheet, std::uint32_t row_count);
#endif
} // namespace detail

/// String storage strategy for streaming worksheet writes.
///
/// The strategy is chosen once in WorkbookWriterOptions and applies to the whole
/// workbook. It is intentionally explicit because string storage changes memory
/// use, package structure, and output size.
enum class StringStrategy {
    /// Write string cells as inlineStr.
    ///
    /// This keeps writer state independent of the number of unique strings and
    /// is the default strategy for large streaming exports.
    InlineString,

    /// Write string cells through xl/sharedStrings.xml.
    ///
    /// This can reduce output size for repeated strings, but keeps a workbook
    /// level table of unique strings and total string-cell count until close().
    /// The table copies unique string values; use InlineString when bounded
    /// memory is more important than deduplicating repeated text.
    SharedString,
};

/// Workbook-owned cell style identifier.
///
/// API mode: Streaming. The default-constructed id is the workbook default
/// style. Non-default ids are returned by WorkbookWriter::add_style() and are
/// valid only for the owning WorkbookWriter. A CellView stores only this small
/// handle; append_row() validates that non-default ids exist before mutating
/// worksheet row state.
class StyleId {
public:
    /// Returns the default workbook style id.
    StyleId() noexcept = default;

    /// Returns the numeric OpenXML cellXfs index used in generated worksheets.
    ///
    /// The value is exposed for diagnostics and structure tests. Non-zero
    /// values should still be treated as workbook-local handles, not stable
    /// ids that can be copied across workbooks.
    [[nodiscard]] std::uint32_t value() const noexcept;

private:
    friend class WorkbookWriter;
    friend class WorksheetWriter;

    explicit StyleId(std::uint32_t value, std::uintptr_t owner_token) noexcept;

    std::uint32_t value_ = 0;
    std::uintptr_t owner_token_ = 0;
};

/// Narrow streaming cell style registered at workbook scope.
///
/// API mode: Streaming style metadata for new workbooks. The first style slice
/// supports custom number formats only. Styles are copied into workbook state,
/// serialized to `xl/styles.xml` during WorkbookWriter::close(), and referenced
/// by per-cell `s` attributes. This does not create worksheet relationships, a
/// worksheet DOM, a full cell matrix, or existing-file edits.
struct CellStyle {
    /// Custom Excel number format code. Empty strings are rejected by the
    /// current first slice because no other style properties are supported yet.
    std::string number_format;
};

/// Options for WorkbookWriter.
///
/// API mode: Streaming. Options are captured by WorkbookWriter::create() and are
/// not currently mutable after the writer has been created.
struct WorkbookWriterOptions {
    /// Controls how string cells are represented in worksheet XML.
    StringStrategy string_strategy = StringStrategy::InlineString;

    /// Document metadata written to `docProps/core.xml` and `docProps/app.xml`.
    ///
    /// This is small workbook metadata copied into WorkbookWriter state during
    /// create(). It does not create custom document properties, does not edit
    /// existing XLSX files, and does not affect worksheet row/cell streaming.
    DocumentProperties document_properties;
};

/// Worksheet data-validation value type.
///
/// API mode: Streaming worksheet metadata. Data validation rules are copied
/// into WorksheetWriter state and serialized as worksheet-local
/// `<dataValidation>` XML during WorkbookWriter::close(). This does not create
/// relationships, content types, styles, formula parsing, or existing-workbook
/// edits.
enum class DataValidationType {
    Whole,
    Decimal,
    List,
    Date,
    Time,
    TextLength,
    Custom,
};

/// Optional comparison operator for worksheet data validation.
///
/// API mode: Streaming worksheet metadata. Operators are serialized as OpenXML
/// attributes only when supplied by DataValidationRule. List and custom rules do
/// not accept operators in the current narrow API.
enum class DataValidationOperator {
    Between,
    NotBetween,
    Equal,
    NotEqual,
    GreaterThan,
    LessThan,
    GreaterThanOrEqual,
    LessThanOrEqual,
};

/// Optional Excel error-alert style for worksheet data validation.
///
/// API mode: Streaming worksheet metadata. The style is serialized only when
/// DataValidationRule::error_style is set. It does not create styles.xml and
/// does not validate cell values.
enum class DataValidationErrorStyle {
    Stop,
    Warning,
    Information,
};

/// A streaming-only worksheet data-validation rule.
///
/// FastXLSX copies formula text into writer-owned storage when the rule is
/// added. Formula text is written as XML text but is not parsed, evaluated, or
/// checked against cell contents. Optional prompt/error strings are serialized
/// as `<dataValidation>` attributes and are not interpreted as Excel UI state.
/// The writer stores one small rule object per call to
/// WorksheetWriter::add_data_validation(); memory grows with rule count and
/// metadata string length, not with worksheet row or cell count.
struct DataValidationRule {
    /// Validation kind written as the OpenXML `type` attribute.
    DataValidationType type = DataValidationType::List;

    /// Optional comparison operator written as the OpenXML `operator`
    /// attribute. Required for between/notBetween formulas and rejected for
    /// list/custom rules in the current implementation.
    std::optional<DataValidationOperator> operator_type;

    /// First formula or list source. Required by the current implementation and
    /// copied into WorksheetWriter state.
    std::string formula1;

    /// Second formula for between/notBetween operators. Must be empty for
    /// single-formula operators.
    std::string formula2;

    /// Writes `allowBlank="1"` when true. Omitted when false.
    bool allow_blank = false;

    /// Writes `showDropDown="1"` for list validations when true. OpenXML uses
    /// the inverted name: this hides Excel's in-cell dropdown arrow. Omitted
    /// when false.
    bool hide_dropdown_arrow = false;

    /// Writes `showInputMessage="1"` when true. Omitted when false. Prompt
    /// text can still be stored when this flag is false.
    bool show_input_message = false;

    /// Writes `showErrorMessage="1"` when true. Omitted when false. Error text
    /// can still be stored when this flag is false.
    bool show_error_message = false;

    /// Optional error-alert style written as `errorStyle`. Omitted when empty.
    std::optional<DataValidationErrorStyle> error_style;

    /// Optional input prompt title written as `promptTitle`. Empty strings are
    /// omitted.
    std::string prompt_title;

    /// Optional input prompt text written as `prompt`. Empty strings are
    /// omitted.
    std::string prompt;

    /// Optional error alert title written as `errorTitle`. Empty strings are
    /// omitted.
    std::string error_title;

    /// Optional error alert text written as `error`. Empty strings are omitted.
    std::string error;
};

/// Built-in Excel totals-row aggregate metadata for a table column.
///
/// API mode: Streaming worksheet table metadata. This writes only the
/// OpenXML `totalsRowFunction` attribute. FastXLSX does not calculate totals,
/// generate formula text, or rewrite totals row cells.
enum class TableTotalsFunction {
    Sum,
    Count,
    Average,
    Maximum,
    Minimum,
    Product,
    CountNumbers,
    StandardDeviation,
    Variance,
};

/// A streaming-only worksheet table definition.
///
/// API mode: Streaming worksheet metadata for new workbooks. FastXLSX stores
/// one lightweight table object per call to WorksheetWriter::add_table() and
/// emits a table part plus worksheet relationship during close(). Column names
/// are copied into writer state; the writer does not inspect previously written
/// header cells, infer column names, or keep a full worksheet cell matrix.
struct TableOptions {
    /// Workbook-wide table display name. The first slice accepts conservative
    /// ASCII identifiers only: first character must be a letter or underscore,
    /// followed by letters, digits, or underscores.
    std::string name;

    /// Header names written to `<tableColumns>`. The count must match the table
    /// range width. Names are copied and must be non-empty and unique within
    /// the table in the current implementation.
    std::vector<std::string> column_names;

    /// Shows the final row in the supplied range as a one-row Excel totals row.
    ///
    /// This writes table metadata only. The caller must append the totals row
    /// cells and include that row in the table range before calling
    /// WorksheetWriter::add_table(). FastXLSX does not generate totals formulas,
    /// totals-row cell text, styles.xml, or calculated columns.
    bool show_totals_row = false;

    /// Optional per-column totals-row function metadata.
    ///
    /// Empty means no column totals functions. When supplied, the vector size
    /// must match column_names; empty optionals leave the corresponding column
    /// without a function. Visible totals rows require at least one function
    /// metadata entry for Excel compatibility. FastXLSX only writes the
    /// OpenXML attribute; it does not calculate, validate, or rewrite totals
    /// row cell values.
    std::vector<std::optional<TableTotalsFunction>> column_totals_functions;

    /// Optional per-column totals-row label metadata.
    ///
    /// Empty means no column totals labels. When supplied, the vector size must
    /// match column_names; empty strings omit the corresponding OpenXML
    /// attribute. Labels require visible totals row metadata, and visible totals
    /// rows still require at least one totals function metadata entry for Excel
    /// compatibility. FastXLSX only writes the `totalsRowLabel` attribute and
    /// does not write the cell text for the caller.
    std::vector<std::string> column_totals_labels;

    /// Built-in Excel table style name. Empty string omits `<tableStyleInfo>`.
    std::string style_name = "TableStyleMedium2";

    bool show_first_column = false;
    bool show_last_column = false;
    bool show_row_stripes = true;
    bool show_column_stripes = false;
};

/// How a two-cell image anchor should behave when cells move or resize.
///
/// API mode: Streaming worksheet drawing metadata for new workbooks. This only
/// selects the OpenXML `xdr:twoCellAnchor editAs` attribute. It does not change
/// the anchor cell range, marker offsets, image bytes, relationships, content
/// types, or worksheet row/cell streaming.
enum class ImageEditAs {
    /// Move and size with cells. Serialized as `editAs="twoCell"`.
    TwoCell,

    /// Move with cells but do not size with cells. Serialized as
    /// `editAs="oneCell"`.
    OneCell,

    /// Do not move or size with cells. Serialized as `editAs="absolute"`.
    Absolute,
};

/// Marker offset for a two-cell image anchor, in EMUs.
///
/// API mode: Streaming worksheet drawing metadata for new workbooks. Offsets
/// are copied into WorksheetWriter state and serialized as `xdr:colOff` /
/// `xdr:rowOff` on the existing two-cell anchor markers. They do not create
/// new anchor element types, inspect row/column sizes, or change image bytes.
struct ImageAnchorOffset {
    /// Horizontal marker offset in EMUs. Must be non-negative and within the
    /// OpenXML coordinate range.
    std::int64_t column_emu = 0;

    /// Vertical marker offset in EMUs. Must be non-negative and within the
    /// OpenXML coordinate range.
    std::int64_t row_emu = 0;
};

/// Optional metadata for an inserted worksheet image.
///
/// API mode: Streaming worksheet metadata for new workbooks. Non-empty strings
/// are copied into WorksheetWriter state and serialized only as drawing
/// non-visual picture properties. Marker offsets are copied as lightweight EMU
/// metadata and serialized on the existing two-cell anchor. These options do
/// not change image bytes, relationships, content types, worksheet row/cell
/// streaming, row/column size geometry, or the anchor cell range.
struct ImageOptions {
    /// OpenXML two-cell anchor placement behavior.
    ImageEditAs edit_as = ImageEditAs::TwoCell;

    /// Offset applied to the starting two-cell anchor marker.
    ImageAnchorOffset from_offset;

    /// Offset applied to the ending two-cell anchor marker.
    ImageAnchorOffset to_offset;

    /// Optional drawing object name written as `xdr:cNvPr name`.
    ///
    /// Empty keeps the generated `Picture N` name. The caller is responsible
    /// for choosing names that are meaningful and unique enough for consumers.
    std::string name;

    /// Optional drawing object description written as `xdr:cNvPr descr`.
    ///
    /// Empty omits the attribute. This is metadata only and does not create
    /// alt-text UI guarantees across spreadsheet applications.
    std::string description;
};

/// Optional hyperlink display metadata.
///
/// API mode: Streaming worksheet metadata for new workbooks. Empty strings are
/// omitted. Non-empty strings are copied into writer state and emitted as
/// worksheet `<hyperlink>` attributes. These options do not write cell text,
/// create hyperlink styles, validate targets, or edit existing XLSX files.
struct HyperlinkOptions {
    /// Optional display text written as the OpenXML `display` attribute.
    std::string display;

    /// Optional screen-tip text written as the OpenXML `tooltip` attribute.
    std::string tooltip;
};

/// A cell view consumed immediately by WorksheetWriter.
///
/// API mode: Streaming. Text and formula values are non-owning string views and
/// only need to remain valid for the duration of append_row(). CellView itself
/// does not own string storage; the SharedString strategy may still allocate
/// workbook-level table entries during append_row(). The current cell value
/// surface has no dedicated date type; date/time values must be supplied by the
/// caller as Excel serial numbers through number(), and number formatting /
/// styles are not generated by CellView.
class CellView {
public:
    /// Non-owning value kind consumed by WorksheetWriter::append_row().
    enum class Type {
        Number,
        String,
        Boolean,
        Formula,
    };

    /// Creates a numeric cell view.
    ///
    /// The payload must be finite. CellView construction is noexcept, so
    /// WorksheetWriter::append_row() performs the validation before mutating
    /// row state. This is also the current path for date/time serial values;
    /// no date-specific cell encoding or style generation is performed.
    static CellView number(double value) noexcept;

    /// Creates a string cell view.
    ///
    /// The view is consumed during append_row(). With InlineString the text is
    /// escaped into worksheet XML immediately; with SharedString the unique value
    /// is copied into the workbook-level shared string table as needed.
    static CellView text(std::string_view value) noexcept;

    /// Creates a boolean cell view.
    static CellView boolean(bool value) noexcept;

    /// Creates a write-only formula cell view.
    ///
    /// FastXLSX writes the formula text and generated workbooks with formula
    /// cells request full recalculation on load through workbook calculation
    /// metadata. It does not parse, evaluate, cache, or maintain
    /// calculation-chain metadata in the current implementation.
    static CellView formula(std::string_view value) noexcept;

    /// Returns a copy of this view with a workbook-owned style id.
    ///
    /// Style id `0` clears the style back to the workbook default. Non-zero ids
    /// must come from WorkbookWriter::add_style() on the same workbook; the
    /// writer validates this during append_row() before row state is advanced.
    [[nodiscard]] CellView with_style(StyleId style_id) const noexcept;

    /// Returns the value kind consumed by WorksheetWriter::append_row().
    [[nodiscard]] Type type() const noexcept;

    /// Returns the numeric payload when type() is Type::Number.
    [[nodiscard]] double number_value() const noexcept;

    /// Returns the text or formula view consumed during append_row().
    ///
    /// This accessor does not extend the string_view lifetime and does not
    /// allocate writer state.
    [[nodiscard]] std::string_view text_value() const noexcept;

    /// Returns the boolean payload when type() is Type::Boolean.
    [[nodiscard]] bool boolean_value() const noexcept;

    /// Returns the workbook-owned style id carried by this cell view.
    [[nodiscard]] StyleId style_id() const noexcept;

private:
    Type type_;
    double number_value_ = 0.0;
    std::string_view text_value_;
    bool boolean_value_ = false;
    StyleId style_id_;
};

/// Append-only worksheet writer for large-data paths.
///
/// API mode: Streaming. Rows are consumed in order and previously written rows
/// cannot be modified through this handle. Worksheet rows are not retained as a
/// full cell matrix by this API. Package finalization still assembles current
/// parts at close(); the default ZIP backend uses the stored bootstrap writer,
/// while FASTXLSX_ENABLE_MINIZIP_NG builds use the minizip-ng DEFLATE backend.
///
/// A default-constructed WorksheetWriter is detached and will throw
/// FastXlsxError from mutating operations. Valid handles are returned by
/// WorkbookWriter::add_worksheet() and are intended to be used before the owning
/// WorkbookWriter is closed or destroyed. Mutating a worksheet after close() is
/// not supported.
class WorksheetWriter {
public:
    /// Creates a detached worksheet writer handle.
    WorksheetWriter() noexcept;

    /// Appends a row from a contiguous cell view range.
    ///
    /// Cells are written in column order starting at column A. The row number is
    /// assigned automatically by append order; callers cannot seek backward or
    /// rewrite prior rows. Text/formula string_view values only need to remain
    /// valid for this call.
    ///
    /// @throws FastXlsxError when row/column limits are exceeded, a numeric
    /// cell value is not finite, row height metadata is non-positive or
    /// non-finite, or the writer cannot write worksheet XML.
    void append_row(std::span<const CellView> cells, RowOptions options = {});

    /// Appends a row from an initializer list.
    ///
    /// This is a convenience wrapper over the span overload and has the same
    /// streaming and string-lifetime rules.
    void append_row(std::initializer_list<CellView> cells, RowOptions options = {});

    /// Records a column width metadata range.
    ///
    /// This metadata is serialized before sheetData and does not require
    /// random access to previously written cells. Ranges are emitted in call
    /// order; the current implementation does not normalize or merge
    /// overlapping column-width ranges.
    ///
    /// @throws FastXlsxError if the range is reversed, outside Excel's column
    /// limit, or width is not positive or not finite.
    void set_column_width(std::uint32_t first_column, std::uint32_t last_column, double width);

    /// Records a frozen pane split.
    ///
    /// The last call wins because the current worksheet model stores a single
    /// frozen pane setting. Split counts are zero-based counts of frozen rows and
    /// columns; for example (1, 1) freezes the first row and first column.
    /// This records worksheet metadata only and does not inspect or rewrite
    /// previously written row XML.
    ///
    /// @throws FastXlsxError if either split exceeds Excel worksheet limits.
    void freeze_panes(std::uint32_t row_split, std::uint32_t column_split);

    /// Records an auto-filter range.
    ///
    /// The last call wins because the current worksheet model stores a single
    /// auto-filter range. The range is validated as a 1-based inclusive Excel
    /// cell range.
    /// This records worksheet metadata only and does not inspect or rewrite
    /// previously written row XML.
    ///
    /// @throws FastXlsxError if the range is reversed or outside Excel worksheet
    /// limits.
    void set_auto_filter(CellRange range);

    /// Records a merged-cell range.
    ///
    /// Ranges are appended and emitted during close(); the current implementation
    /// does not check overlap against other merged ranges or existing cell
    /// values. The writer keeps the merge range list until close(), so memory
    /// grows with the number of recorded merged ranges, not with worksheet cell
    /// contents.
    ///
    /// @throws FastXlsxError if the range is invalid, outside Excel worksheet
    /// limits, or contains only one cell.
    void merge_cells(CellRange range);

    /// Records a worksheet-local data validation rule for one range.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. The rule is
    /// emitted as `<dataValidations>` in the worksheet XML and does not add
    /// package relationships or content types. FastXLSX copies formula strings
    /// into writer state, performs range and narrow-rule-shape validation, and
    /// does not parse formulas, validate existing cell values, check overlapping
    /// validations, or promise Excel UI completeness.
    ///
    /// @throws FastXlsxError if the range is invalid, the workbook is closed, or
    /// the rule shape is outside the current narrow data-validation surface.
    void add_data_validation(CellRange range, DataValidationRule rule);

    /// Records one worksheet-local data validation rule for multiple ranges.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. Ranges are
    /// copied into writer state and serialized as one space-separated `sqref`
    /// attribute on a single `<dataValidation>` element. This does not check
    /// overlapping ranges and has the same formula, relationship, content type,
    /// style, and existing-file limits as the single-range overload. Memory
    /// grows with the copied range count and rule text, not with worksheet row
    /// or cell count.
    ///
    /// @throws FastXlsxError if the range list is empty, any range is invalid,
    /// the workbook is closed, or the rule shape is outside the current narrow
    /// data-validation surface.
    void add_data_validation(std::span<const CellRange> ranges, DataValidationRule rule);

    /// Convenience overload for multiple data-validation ranges.
    ///
    /// The initializer-list ranges are copied during this call.
    void add_data_validation(std::initializer_list<CellRange> ranges, DataValidationRule rule);

    /// Records an external hyperlink on one worksheet cell.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. The hyperlink
    /// is emitted as a worksheet `<hyperlink>` element plus a matching
    /// `xl/worksheets/_rels/sheetN.xml.rels` relationship with
    /// `TargetMode="External"`. This API copies the target URL into writer
    /// state, does not write or style the cell value, does not check URL
    /// reachability, and does not edit existing XLSX files. Optional display
    /// and tooltip strings are serialized as hyperlink attributes only; they do
    /// not create styles or write the underlying cell value.
    ///
    /// @throws FastXlsxError if the cell reference is outside Excel worksheet
    /// limits, the target URL is empty, or the workbook is closed.
    void add_external_hyperlink(
        std::uint32_t row, std::uint32_t column, std::string target_url,
        HyperlinkOptions options = {});

    /// Records an internal workbook hyperlink on one worksheet cell.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. The hyperlink
    /// is emitted as a worksheet `<hyperlink>` element with a `location`
    /// attribute and does not create worksheet relationships, workbook
    /// relationships, or content type overrides. This API copies the location
    /// text into writer state, does not write or style the cell value, does not
    /// validate that the target sheet or cell exists, and does not edit
    /// existing XLSX files. Optional display and tooltip strings are serialized
    /// as hyperlink attributes only; they do not create styles or write the
    /// underlying cell value.
    ///
    /// @throws FastXlsxError if the cell reference is outside Excel worksheet
    /// limits, the location is empty, or the workbook is closed.
    void add_internal_hyperlink(
        std::uint32_t row, std::uint32_t column, std::string location,
        HyperlinkOptions options = {});

    /// Records a worksheet table range for a new workbook.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. The table is
    /// emitted as an `xl/tables/tableN.xml` part, a worksheet `<tableParts>`
    /// reference, a worksheet `.rels` relationship, and a table content type
    /// override. This API copies table name, column names, style name, optional
    /// totals-row visibility, optional per-column totals function metadata, and
    /// optional per-column totals labels into writer state. It does not inspect
    /// row data, infer headers, calculate totals, generate formula text, write
    /// totals row cell labels, create styles.xml, resize existing tables, edit
    /// existing XLSX files, or promise full Excel table UI parity.
    ///
    /// @throws FastXlsxError if the range is invalid, contains only a header
    /// row, enables totals-row metadata without room for a data row, column
    /// names do not match the range width, names are invalid or duplicated,
    /// overlaps an existing table range in the same worksheet, or the workbook
    /// is closed.
    void add_table(CellRange range, TableOptions options);

    /// Records a PNG/JPEG image anchored to worksheet cells.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. The image file
    /// is validated through the required stb-backed image metadata helper, then
    /// copied to a temporary file-backed media entry so close() can package the
    /// original bytes without retaining them in worksheet row state. The anchor
    /// is written as a simple two-cell drawing anchor spanning the supplied
    /// inclusive cell range. This creates `xl/media/*`, `xl/drawings/*`,
    /// drawing `.rels`, worksheet `.rels`, a worksheet `<drawing>` reference,
    /// and drawing/content type entries. It does not crop, rotate, recompress,
    /// convert formats, calculate row/column geometry, mutate existing drawings,
    /// or edit existing XLSX files.
    /// Optional ImageOptions values are copied into writer state and written
    /// only as drawing metadata: EMU offsets on the existing two-cell markers,
    /// `editAs` on the anchor, and non-visual `xdr:cNvPr` name/description
    /// attributes. Empty strings preserve the generated `Picture N` name and
    /// omit the description.
    ///
    /// @throws FastXlsxError if the anchor range or ImageOptions offsets are
    /// invalid, the workbook is closed, the file cannot be read, or the image
    /// format is outside the current PNG/JPEG slice.
    void add_image(const std::filesystem::path& path, CellRange anchor);

    /// Records a PNG/JPEG image with optional drawing non-visual metadata.
    ///
    /// Same behavior as add_image(path, anchor), plus ImageOptions serialization
    /// to drawing anchor marker offsets / `xdr:cNvPr` attributes.
    void add_image(const std::filesystem::path& path, CellRange anchor, ImageOptions options);

private:
    friend class WorkbookWriter;
#ifdef FASTXLSX_ENABLE_TEST_HOOKS
    friend void detail::testing_set_worksheet_row_count(
        WorksheetWriter& worksheet, std::uint32_t row_count);
#endif
    explicit WorksheetWriter(detail::WorksheetWriterState* state) noexcept;

    detail::WorksheetWriterState* state_ = nullptr;
};

/// Streaming-oriented workbook writer.
///
/// API mode: Streaming. Use this writer for ordered worksheet export. It keeps
/// workbook metadata and small worksheet metadata in memory, but row data is
/// consumed as it is appended. During close(), worksheet row bodies are passed
/// to the internal package writer as file-backed entry chunks so the writer
/// does not rebuild a full worksheet XML string. Small workbook/package XML
/// parts are still assembled in memory. FASTXLSX_ENABLE_MINIZIP_NG switches the
/// final ZIP write to minizip-ng/DEFLATE, but this does not provide true package
/// streaming or Zip64 guarantees.
class WorkbookWriter {
public:
    /// Creates an empty, uninitialized writer.
    ///
    /// Use create() for normal file output. Mutating an uninitialized writer
    /// throws FastXlsxError.
    WorkbookWriter();

    /// Destroys the writer and its temporary worksheet files.
    ///
    /// The destructor does not finalize the XLSX package; callers must call
    /// close() explicitly to write the workbook.
    ~WorkbookWriter();

    WorkbookWriter(WorkbookWriter&&) noexcept;
    WorkbookWriter& operator=(WorkbookWriter&&) noexcept;

    WorkbookWriter(const WorkbookWriter&) = delete;
    WorkbookWriter& operator=(const WorkbookWriter&) = delete;

    /// Creates a streaming workbook writer for the target path.
    ///
    /// The output file is written when close() succeeds. Existing files at the
    /// path are replaced through the internal package writer backend. The
    /// writer is move-only so ownership of temporary worksheet state is
    /// explicit.
    static WorkbookWriter create(std::filesystem::path path, WorkbookWriterOptions options = {});

    /// Registers a workbook-owned cell style and returns its style id.
    ///
    /// API mode: Streaming style metadata for new workbooks. The style is
    /// copied into workbook state and written to `xl/styles.xml` only when the
    /// workbook is closed. The first slice supports custom number formats; font,
    /// fill, border, alignment, rich text, conditional formatting, and
    /// existing-file style preservation remain outside this API. Registering a
    /// style does not touch worksheet row XML until a CellView carries the
    /// returned id through with_style().
    ///
    /// @throws FastXlsxError if the writer is uninitialized or closed, or the
    /// style contains no property supported by the current narrow slice.
    StyleId add_style(CellStyle style);

    /// Adds a worksheet and returns its streaming handle.
    ///
    /// Worksheets are assigned workbook relationship ids in add order. Sheet
    /// names are validated against Excel's basic restrictions and must be
    /// unique. New worksheets cannot be added after close().
    ///
    /// @throws FastXlsxError if the writer is uninitialized or closed, or if the
    /// worksheet name is invalid or duplicated.
    WorksheetWriter add_worksheet(std::string name = "Sheet1");

    /// Finalizes workbook XML and writes the XLSX package.
    ///
    /// close() is idempotent after a successful close. Before closing, all rows
    /// have been written to temporary worksheet XML files; close() assembles
    /// content types, relationships, and workbook XML, then writes worksheet
    /// parts as prefix/body-file/suffix package entry chunks. When SharedString
    /// is enabled, sharedStrings.xml is written through a temporary file-backed
    /// package entry, while the unique shared-string table remains workbook
    /// state until close().
    ///
    /// @throws FastXlsxError if the writer is uninitialized, contains no
    /// worksheets, cannot read temporary worksheet XML, or cannot write the
    /// package.
    void close();

private:
    explicit WorkbookWriter(std::unique_ptr<detail::WorkbookWriterState> state) noexcept;

    std::unique_ptr<detail::WorkbookWriterState> state_;
};

} // namespace fastxlsx
