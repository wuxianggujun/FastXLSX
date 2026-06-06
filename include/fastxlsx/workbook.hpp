#pragma once

#include <fastxlsx/document_properties.hpp>

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx {

/// Error raised for FastXLSX validation and package-writing failures.
///
/// FastXLSX currently exposes a single exception type for public API failures
/// such as invalid worksheet names, invalid cell/range references, empty
/// workbooks, XML generation errors, and ZIP/package write errors. The exception
/// text is intended for diagnostics; no stable numeric error-code API exists in
/// the current public surface.
class FastXlsxError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// A 1-based inclusive worksheet cell range.
///
/// API mode: value type shared by Streaming and small in-memory helpers. The
/// range does not imply random worksheet access; it is metadata for XML features
/// such as auto filters and merged cells. APIs that serialize a range validate
/// it against Excel worksheet limits when called or when the XML reference is
/// generated.
struct CellRange {
    std::uint32_t first_row = 1;
    std::uint32_t first_column = 1;
    std::uint32_t last_row = 1;
    std::uint32_t last_column = 1;
};

/// Optional row metadata consumed when a row is appended.
///
/// API mode: Streaming. Row options travel with the row so writers do not need
/// a random-access row map after prior rows have been emitted. A height value is
/// serialized as row height metadata; WorksheetWriter rejects non-positive
/// or non-finite heights in append_row(). The small in-memory Workbook path
/// rejects non-positive or non-finite height metadata when save() serializes
/// worksheet XML.
struct RowOptions {
    std::optional<double> height;
};

/// A minimal owning worksheet cell value for Workbook's Phase 1 write API.
///
/// This type intentionally carries only number, inline string, boolean, and
/// write-only formula values. Styles, shared strings, dates, and rich text are
/// outside the current implementation and should be added without making large
/// worksheet writes depend on a DOM or full random-access cell matrix.
///
/// API mode: small in-memory creation path. Cell owns string/formula text and is
/// copied into Worksheet's row buffer when appended. For large ordered exports,
/// prefer CellView with WorkbookWriter so row data can be consumed without
/// retaining a full worksheet matrix.
class Cell {
public:
    /// Stored value kind used during worksheet XML generation.
    enum class Type {
        Number,
        String,
        Boolean,
        Formula,
    };

    /// Creates a numeric cell.
    ///
    /// Numeric payloads must be finite. The small in-memory Workbook path
    /// reports non-finite numbers as FastXlsxError when save() serializes the
    /// worksheet XML.
    static Cell number(double value);

    /// Creates an inline string cell.
    ///
    /// Workbook writes strings as inlineStr and does not create
    /// xl/sharedStrings.xml. Shared-string output is currently an explicit
    /// WorkbookWriter option because it grows workbook-level state with the
    /// number of unique strings.
    static Cell text(std::string value);

    /// Creates a boolean cell.
    static Cell boolean(bool value);

    /// Creates a write-only formula cell.
    ///
    /// API mode: Streaming-compatible metadata. FastXLSX writes the formula
    /// text to worksheet XML but does not parse it, evaluate it, manage
    /// calcChain, or provide a cached value in the current implementation.
    static Cell formula(std::string value);

    /// Returns the stored value kind for the Phase 1 in-memory cell.
    [[nodiscard]] Type type() const noexcept;

    /// Returns the numeric payload when type() is Type::Number.
    [[nodiscard]] double number_value() const noexcept;

    /// Returns the owned string or formula payload for string/formula cells.
    [[nodiscard]] const std::string& string_value() const noexcept;

    /// Returns the boolean payload when type() is Type::Boolean.
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
/// API mode: small in-memory creation path with a streaming-oriented,
/// append-only worksheet surface. Rows must be appended in order and previously
/// appended rows cannot be randomly modified through this API. The Phase 1
/// implementation buffers rows in memory until save() so OpenXML structure and
/// compatibility can be validated. Use WorkbookWriter for large ordered exports
/// that should not retain row data.
class Worksheet {
public:
    /// Appends one row to the worksheet.
    ///
    /// The API does not expose random cell mutation and does not promise a full
    /// worksheet matrix for future large-file paths. For Phase 1, cells are kept
    /// in an internal row buffer and written as sheetData during Workbook::save().
    /// Strings and formulas are copied before the call returns.
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
    /// the streaming writer. This in-memory path currently stores the supplied
    /// metadata and serializes it during Workbook::save(); non-positive or
    /// non-finite row heights are rejected during serialization.
    void append_row(const std::vector<Cell>& cells, RowOptions options);

    /// Returns the worksheet name stored in the in-memory workbook model.
    [[nodiscard]] const std::string& name() const noexcept;

    /// Returns the number of rows currently held by the Phase 1 in-memory buffer.
    ///
    /// This is not a streaming cursor for large exports.
    [[nodiscard]] std::uint32_t row_count() const noexcept;

private:
    friend class Workbook;

    explicit Worksheet(std::string name);

    std::string name_;
    std::vector<detail::WorksheetRowData> rows_;
};

/// Minimal XLSX workbook writer.
///
/// API mode: small in-memory creation path with a streaming-oriented,
/// append-only worksheet surface. The public API is intentionally append-only at
/// the worksheet level so later large-data writers can avoid DOM-style random
/// access. Phase 1 buffers rows until save(); use WorkbookWriter for large
/// ordered exports that should not retain row data. Workbook::save() writes
/// through the internal package writer boundary: dependency-free builds use the
/// stored ZIP bootstrap, while builds configured with FASTXLSX_ENABLE_MINIZIP_NG
/// use the minizip-ng DEFLATE backend. Both paths still assemble small package
/// entries in memory; Zip64 and true package streaming are not public guarantees.
class Workbook {
public:
    /// Creates an empty workbook.
    static Workbook create();

    /// Replaces workbook document metadata for generated `docProps` parts.
    ///
    /// API mode: small in-memory workbook metadata. FastXLSX copies the values
    /// and serializes them into `docProps/core.xml` and `docProps/app.xml` when
    /// save() is called. This does not create custom document properties, does
    /// not edit existing XLSX files, and does not affect worksheet row storage.
    void set_document_properties(DocumentProperties properties);

    /// Adds a worksheet and returns it.
    ///
    /// Sheet names are validated against Excel's basic restrictions and must be
    /// unique. Worksheets are stored in the workbook's internal vector, so a
    /// reference returned from an earlier call can be invalidated by later
    /// add_worksheet() calls that reallocate the vector. Phase 1 supports
    /// writing workbook/worksheet package parts only; styles, drawings, custom
    /// document properties, and calculation metadata are not generated. Basic
    /// configurable core/app docProps are generated by save().
    ///
    /// @throws FastXlsxError if the name is empty, exceeds Excel's 31-character
    /// limit, contains a character rejected by Excel, or duplicates an existing
    /// worksheet name.
    Worksheet& add_worksheet(std::string name = "Sheet1");

    /// Saves the workbook as an XLSX file.
    ///
    /// The file is an OpenXML package with workbook relationships, content
    /// types, and worksheet sheetData. Existing files at path are replaced by
    /// the underlying package writer. This method is the only finalization step
    /// for the in-memory path; it does not edit or preserve parts from an
    /// existing workbook.
    ///
    /// Current implementations assemble the supported package entries in memory
    /// through the internal package writer; this is not true package streaming,
    /// Zip64, or existing-file preservation.
    ///
    /// @throws FastXlsxError if the workbook has no worksheets, generated XML is
    /// invalid for the supported limits, a numeric value is not finite, a row
    /// height is not positive and finite, or the package cannot be written.
    void save(const std::filesystem::path& path) const;

    /// Returns the workbook's current worksheet buffer.
    ///
    /// This accessor exposes the in-memory path's current state for inspection.
    /// It is not a streaming cursor and should not be used as a large-file data
    /// model.
    [[nodiscard]] const std::vector<Worksheet>& worksheets() const noexcept;

private:
    std::vector<Worksheet> worksheets_;
    DocumentProperties document_properties_;
};

} // namespace fastxlsx
