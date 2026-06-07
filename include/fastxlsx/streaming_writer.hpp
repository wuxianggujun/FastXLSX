#pragma once

#include <fastxlsx/document_properties.hpp>
#include <fastxlsx/workbook.hpp>

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

/// A streaming-only worksheet data-validation rule.
///
/// FastXLSX copies formula text into writer-owned storage when the rule is
/// added. Formula text is written as XML text but is not parsed, evaluated, or
/// checked against cell contents. The writer stores one small rule object per
/// call to WorksheetWriter::add_data_validation(); memory grows with rule count,
/// not with worksheet row or cell count.
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

    /// Built-in Excel table style name. Empty string omits `<tableStyleInfo>`.
    std::string style_name = "TableStyleMedium2";

    bool show_first_column = false;
    bool show_last_column = false;
    bool show_row_stripes = true;
    bool show_column_stripes = false;
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

private:
    Type type_;
    double number_value_ = 0.0;
    std::string_view text_value_;
    bool boolean_value_ = false;
};

/// Append-only worksheet writer for large-data paths.
///
/// API mode: Streaming. Rows are consumed in order and previously written rows
/// cannot be modified through this handle. Worksheet rows are not retained as a
/// full cell matrix by this API. Package finalization still assembles current
/// parts at close(); dependency-free builds use the stored ZIP bootstrap, while
/// FASTXLSX_ENABLE_MINIZIP_NG builds use the minizip-ng DEFLATE backend.
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

    /// Records a worksheet-local data validation rule.
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

    /// Records an external hyperlink on one worksheet cell.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. The hyperlink
    /// is emitted as a worksheet `<hyperlink>` element plus a matching
    /// `xl/worksheets/_rels/sheetN.xml.rels` relationship with
    /// `TargetMode="External"`. This API copies the target URL into writer
    /// state, does not write or style the cell value, does not check URL
    /// reachability, and does not support internal workbook links or editing
    /// existing XLSX files.
    ///
    /// @throws FastXlsxError if the cell reference is outside Excel worksheet
    /// limits, the target URL is empty, or the workbook is closed.
    void add_external_hyperlink(
        std::uint32_t row, std::uint32_t column, std::string target_url);

    /// Records a worksheet table range for a new workbook.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. The table is
    /// emitted as an `xl/tables/tableN.xml` part, a worksheet `<tableParts>`
    /// reference, a worksheet `.rels` relationship, and a table content type
    /// override. This API copies table name, column names, and style name into
    /// writer state. It does not inspect row data, infer headers, create
    /// styles.xml, support totals rows, resize existing tables, edit existing
    /// XLSX files, or promise full Excel table UI parity.
    ///
    /// @throws FastXlsxError if the range is invalid, contains only a header
    /// row, column names do not match the range width, names are invalid or
    /// duplicated, or the workbook is closed.
    void add_table(CellRange range, TableOptions options);

    /// Records a PNG/JPEG image anchored to worksheet cells.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. The image file
    /// is validated through the opt-in stb-backed image metadata helper, then
    /// copied to a temporary file-backed media entry so close() can package the
    /// original bytes without retaining them in worksheet row state. The anchor
    /// is written as a simple two-cell drawing anchor spanning the supplied
    /// inclusive cell range. This creates `xl/media/*`, `xl/drawings/*`,
    /// drawing `.rels`, worksheet `.rels`, a worksheet `<drawing>` reference,
    /// and drawing/content type entries. It does not crop, rotate, recompress,
    /// convert formats, mutate existing drawings, or edit existing XLSX files.
    ///
    /// @throws FastXlsxError if the anchor range is invalid, the workbook is
    /// closed, stb support is disabled, the file cannot be read, or the image
    /// format is outside the current PNG/JPEG slice.
    void add_image(const std::filesystem::path& path, CellRange anchor);

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
