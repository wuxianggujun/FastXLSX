#pragma once

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx {

/// Error raised when FastXLSX cannot build XML, validate workbook state, or
/// write the XLSX package.
class FastXlsxError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// A 1-based inclusive worksheet cell range.
///
/// API mode: value type shared by Streaming and small in-memory helpers. The
/// range does not imply random worksheet access; it is metadata for XML features
/// such as auto filters and merged cells.
struct CellRange {
    std::uint32_t first_row = 1;
    std::uint32_t first_column = 1;
    std::uint32_t last_row = 1;
    std::uint32_t last_column = 1;
};

/// Optional row metadata consumed when a row is appended.
///
/// API mode: Streaming. Row options travel with the row so writers do not need
/// a random-access row map after prior rows have been emitted.
struct RowOptions {
    std::optional<double> height;
};

/// A minimal worksheet cell value for the Phase 1 streaming-oriented write API.
///
/// This type intentionally carries only number, inline string, boolean, and
/// write-only formula values. Styles, shared strings, dates, and rich text are
/// outside the current implementation and should be added without making large
/// worksheet writes depend on a DOM or full random-access cell matrix.
class Cell {
public:
    enum class Type {
        Number,
        String,
        Boolean,
        Formula,
    };

    /// Creates a numeric cell.
    static Cell number(double value);

    /// Creates an inline string cell.
    ///
    /// Phase 1 writes strings as inlineStr to avoid a shared string table. A
    /// future shared-string strategy must be an explicit performance choice
    /// because it grows state with the number of unique strings.
    static Cell text(std::string value);

    /// Creates a boolean cell.
    static Cell boolean(bool value);

    /// Creates a write-only formula cell.
    ///
    /// API mode: Streaming-compatible metadata. FastXLSX writes the formula
    /// text to worksheet XML but does not parse it, evaluate it, manage
    /// calcChain, or provide a cached value in the current implementation.
    static Cell formula(std::string value);

    [[nodiscard]] Type type() const noexcept;
    [[nodiscard]] double number_value() const noexcept;
    [[nodiscard]] const std::string& string_value() const noexcept;
    [[nodiscard]] bool boolean_value() const noexcept;

private:
    explicit Cell(double value);
    explicit Cell(std::string value);
    Cell(Type type, std::string value);
    explicit Cell(bool value);

    Type type_;
    double number_value_ = 0.0;
    std::string string_value_;
    bool boolean_value_ = false;
};

namespace detail {

struct WorksheetRowData {
    std::vector<Cell> cells;
    RowOptions options;
};

} // namespace detail

/// A worksheet exposed through an append-only, streaming-oriented API.
///
/// API mode: Streaming. Rows must be appended in order and previously appended
/// rows cannot be randomly modified through this API. The Phase 1 implementation
/// buffers rows in memory until save() so OpenXML structure and compatibility can
/// be validated before the ZIP/minizip-ng layer is finalized. Do not treat this
/// temporary buffer as the long-term large-file architecture.
class Worksheet {
public:
    /// Appends one row to the worksheet.
    ///
    /// The API does not expose random cell mutation and does not promise a full
    /// worksheet matrix for future large-file paths. For Phase 1, cells are kept
    /// in an internal row buffer and written as sheetData during Workbook::save().
    void append_row(std::initializer_list<Cell> cells);

    /// Appends one row to the worksheet from a vector-like source.
    ///
    /// This overload is convenient for generated rows while preserving ordered
    /// row consumption. The input cells are copied into the Phase 1 buffer.
    void append_row(const std::vector<Cell>& cells);

    /// Appends one row with row metadata.
    ///
    /// API mode: Streaming-oriented. Row options are stored with the appended
    /// row in the Phase 1 buffer and are intended to map directly to row XML in
    /// the future streaming writer.
    void append_row(const std::vector<Cell>& cells, RowOptions options);

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] std::uint32_t row_count() const noexcept;

private:
    friend class Workbook;

    explicit Worksheet(std::string name);

    std::string name_;
    std::vector<detail::WorksheetRowData> rows_;
};

/// Minimal XLSX workbook writer.
///
/// API mode: Streaming-oriented creation. The public API is intentionally
/// append-only at the worksheet level so later large-data writers can replace
/// the Phase 1 in-memory buffer without changing callers into DOM-style random
/// access. Workbook::save() currently writes a stored ZIP package for bootstrap
/// validation; production compression/minizip-ng integration is tracked as a
/// follow-up task.
class Workbook {
public:
    /// Creates an empty workbook.
    static Workbook create();

    /// Adds a worksheet and returns it.
    ///
    /// Sheet names are validated against Excel's basic restrictions and must be
    /// unique. Phase 1 supports writing workbook/worksheet package parts only;
    /// styles, drawings, formulas, and document properties are not generated.
    Worksheet& add_worksheet(std::string name = "Sheet1");

    /// Saves the workbook as an XLSX file.
    ///
    /// The file is an OpenXML package with workbook relationships, content
    /// types, and worksheet sheetData. Existing files at path are replaced by
    /// the underlying file stream. Throws FastXlsxError when validation or
    /// package writing fails.
    void save(const std::filesystem::path& path) const;

    [[nodiscard]] const std::vector<Worksheet>& worksheets() const noexcept;

private:
    std::vector<Worksheet> worksheets_;
};

} // namespace fastxlsx
