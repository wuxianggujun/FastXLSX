#pragma once

#include <fastxlsx/workbook.hpp>

#include <filesystem>
#include <initializer_list>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace fastxlsx {

namespace detail {
struct WorkbookWriterState;
struct WorksheetWriterState;
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
};

/// A cell view consumed immediately by WorksheetWriter.
///
/// API mode: Streaming. Text and formula values are non-owning string views and
/// only need to remain valid for the duration of append_row(). CellView itself
/// does not own string storage; the SharedString strategy may still allocate
/// workbook-level table entries during append_row().
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
    /// FastXLSX writes the formula text but does not parse, evaluate, cache, or
    /// maintain calculation-chain metadata in the current implementation.
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
    /// @throws FastXlsxError when row/column limits are exceeded or the writer
    /// cannot write worksheet XML.
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
    /// limit, or width is not positive.
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

private:
    friend class WorkbookWriter;
    explicit WorksheetWriter(detail::WorksheetWriterState* state) noexcept;

    detail::WorksheetWriterState* state_ = nullptr;
};

/// Streaming-oriented workbook writer.
///
/// API mode: Streaming. Use this writer for ordered worksheet export. It keeps
/// workbook metadata and small worksheet metadata in memory, but row data is
/// consumed as it is appended. Final package entries are currently assembled
/// during close(); FASTXLSX_ENABLE_MINIZIP_NG switches that final ZIP write to
/// minizip-ng/DEFLATE, but does not yet provide true package streaming or Zip64
/// guarantees.
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
    /// content types, relationships, workbook XML, optional sharedStrings.xml,
    /// and worksheet parts through the configured package writer backend.
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
