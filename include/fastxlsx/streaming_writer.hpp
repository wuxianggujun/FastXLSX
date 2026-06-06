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
enum class StringStrategy {
    /// Write string cells as inlineStr. This is the only implemented strategy
    /// today and keeps writer state independent of the number of unique strings.
    InlineString,
};

/// Options for WorkbookWriter.
struct WorkbookWriterOptions {
    StringStrategy string_strategy = StringStrategy::InlineString;
};

/// A cell view consumed immediately by WorksheetWriter.
///
/// API mode: Streaming. Text and formula values are non-owning string views and
/// only need to remain valid for the duration of append_row(). This avoids
/// forced string allocation in the hot row-writing path.
class CellView {
public:
    enum class Type {
        Number,
        String,
        Boolean,
        Formula,
    };

    static CellView number(double value) noexcept;
    static CellView text(std::string_view value) noexcept;
    static CellView boolean(bool value) noexcept;
    static CellView formula(std::string_view value) noexcept;

    [[nodiscard]] Type type() const noexcept;
    [[nodiscard]] double number_value() const noexcept;
    [[nodiscard]] std::string_view text_value() const noexcept;
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
/// cannot be modified through this handle. The current ZIP backend is still the
/// Phase 1 stored ZIP bootstrap, so package compression and Zip64 are not
/// production-ready yet, but worksheet rows are not retained as a full cell
/// matrix by this API.
class WorksheetWriter {
public:
    WorksheetWriter() noexcept;

    /// Appends a row from a contiguous cell view range.
    ///
    /// @throws FastXlsxError when row/column limits are exceeded or the writer
    /// cannot write worksheet XML.
    void append_row(std::span<const CellView> cells, RowOptions options = {});

    /// Appends a row from an initializer list.
    void append_row(std::initializer_list<CellView> cells, RowOptions options = {});

    /// Records a column width metadata range.
    ///
    /// This metadata is serialized before sheetData and does not require
    /// random access to previously written cells.
    void set_column_width(std::uint32_t first_column, std::uint32_t last_column, double width);

    /// Records a frozen pane split.
    void freeze_panes(std::uint32_t row_split, std::uint32_t column_split);

    /// Records an auto-filter range.
    void set_auto_filter(CellRange range);

    /// Records a merged-cell range.
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
/// consumed as it is appended. The current stored ZIP bootstrap still buffers
/// final package entries during close(); minizip-ng/zlib-ng integration remains
/// the production backend task.
class WorkbookWriter {
public:
    WorkbookWriter();
    ~WorkbookWriter();

    WorkbookWriter(WorkbookWriter&&) noexcept;
    WorkbookWriter& operator=(WorkbookWriter&&) noexcept;

    WorkbookWriter(const WorkbookWriter&) = delete;
    WorkbookWriter& operator=(const WorkbookWriter&) = delete;

    static WorkbookWriter create(std::filesystem::path path, WorkbookWriterOptions options = {});

    WorksheetWriter add_worksheet(std::string name = "Sheet1");
    void close();

private:
    explicit WorkbookWriter(std::unique_ptr<detail::WorkbookWriterState> state) noexcept;

    std::unique_ptr<detail::WorkbookWriterState> state_;
};

} // namespace fastxlsx
