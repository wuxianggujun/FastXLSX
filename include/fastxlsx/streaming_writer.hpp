#pragma once

#include <fastxlsx/document_properties.hpp>
#include <fastxlsx/workbook.hpp>
#include <fastxlsx/worksheet_metadata.hpp>

#include <array>
#include <chrono>
#include <cstddef>
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

class StyleId;
class WorksheetWriter;

namespace detail {
struct WorkbookWriterState;
struct WorksheetWriterState;
[[nodiscard]] StyleId make_source_style_id(std::uint32_t value) noexcept;

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
void testing_set_worksheet_row_count(WorksheetWriter& worksheet, std::uint32_t row_count);
[[nodiscard]] bool testing_worksheet_temporary_resources_released(
    const WorksheetWriter& worksheet) noexcept;
[[nodiscard]] std::size_t testing_worksheet_pending_body_buffer_bytes(
    const WorksheetWriter& worksheet) noexcept;
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
    friend StyleId detail::make_source_style_id(std::uint32_t value) noexcept;

    explicit StyleId(std::uint32_t value, std::uintptr_t owner_token) noexcept;

    std::uint32_t value_ = 0;
    std::uintptr_t owner_token_ = 0;
};

/// Horizontal alignment values for the current narrow streaming style slice.
enum class HorizontalAlignment {
    Left,
    Center,
    Right,
};

/// Vertical alignment values for the current narrow streaming style slice.
enum class VerticalAlignment {
    Top,
    Center,
    Bottom,
};

/// Narrow streaming cell alignment metadata registered through CellStyle.
///
/// API mode: Streaming style metadata for new workbooks. The current alignment
/// slice supports wrap text plus limited horizontal/vertical alignment values.
/// It is serialized as workbook-level `xl/styles.xml` cell-format metadata and
/// does not imply full alignment, row-height calculation, text rotation,
/// indentation, shrink-to-fit, worksheet DOM, or existing-file style
/// preservation.
struct CellAlignment {
    /// Enables OpenXML `wrapText="1"` on the generated cell format.
    bool wrap_text = false;

    /// Optional horizontal alignment. Empty means the style does not change
    /// horizontal alignment.
    std::optional<HorizontalAlignment> horizontal;

    /// Optional vertical alignment. Empty means the style does not change
    /// vertical alignment.
    std::optional<VerticalAlignment> vertical;
};

/// ARGB color written as an eight-digit OpenXML `rgb` value.
///
/// API mode: Streaming worksheet/style metadata. The strongly typed representation
/// avoids accepting partially validated color strings in performance-sensitive
/// writer paths. Values are serialized as uppercase ARGB hex.
struct ArgbColor {
    std::uint8_t alpha = 0xFF;
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

/// Narrow streaming cell font metadata registered through CellStyle.
///
/// API mode: Streaming style metadata for new workbooks. The current font slice
/// supports bold/italic flags plus an optional direct ARGB font color. It is
/// serialized as workbook-level `xl/styles.xml` font metadata and does not imply
/// font size, family, underline, rich text, theme/tint/indexed color handling,
/// worksheet DOM, or existing-file style preservation.
struct CellFont {
    /// Writes OpenXML `<b/>` in the generated font record.
    bool bold = false;

    /// Writes OpenXML `<i/>` in the generated font record.
    bool italic = false;

    /// Optional direct OpenXML `rgb` font color. Empty keeps the default theme
    /// color for custom font records.
    std::optional<ArgbColor> color;
};

/// Narrow streaming cell fill metadata registered through CellStyle.
///
/// API mode: Streaming style metadata for new workbooks. The current fill slice
/// supports only a solid foreground ARGB color. It is serialized as
/// workbook-level `xl/styles.xml` fill metadata and does not imply gradient
/// fills, pattern fills beyond `solid`, theme fills, dxf conditional formatting,
/// worksheet DOM, or existing-file style preservation.
struct CellFill {
    explicit constexpr CellFill(ArgbColor foreground_color) noexcept
        : foreground(foreground_color)
    {
    }

    /// Foreground color written as `<fgColor rgb="..."/>` in a solid fill.
    ArgbColor foreground;
};

/// Narrow streaming cell style registered at workbook scope.
///
/// API mode: Streaming style metadata for new workbooks. The current style
/// slices support custom number formats, narrow wrap-text plus limited
/// horizontal/vertical alignment values, narrow bold/italic/direct-color font
/// metadata, and a narrow solid foreground fill. Styles
/// are copied into workbook state, serialized to `xl/styles.xml` during
/// WorkbookWriter::close(), and referenced by per-cell `s` attributes. This
/// does not create worksheet relationships, a worksheet DOM, a full cell
/// matrix, or existing-file edits.
struct CellStyle {
    /// Custom Excel number format code. Empty means the style does not change
    /// number formatting.
    std::string number_format;

    /// Optional narrow alignment metadata. `wrap_text=true` and the limited
    /// horizontal/vertical enum values are currently supported; an empty
    /// optional or all-default metadata contributes no style property.
    std::optional<CellAlignment> alignment;

    /// Optional narrow font metadata. Bold, italic, and direct ARGB font color
    /// are currently supported; an empty optional or all-default metadata
    /// contributes no style property.
    std::optional<CellFont> font;

    /// Optional narrow fill metadata. When present, the current slice writes a
    /// solid foreground ARGB fill; an empty optional contributes no style
    /// property.
    std::optional<CellFill> fill;
};

/// Common Excel number format codes for streaming style registration.
///
/// API mode: Streaming style metadata for new workbooks. These are display
/// format presets only; they do not encode a date cell type, do not calculate
/// date serial values, and do not affect worksheet row streaming. Register them
/// through WorkbookWriter::add_style(), then attach the returned StyleId to
/// numeric cells with CellView::with_style().
namespace number_format {
/// ISO-like calendar date display, for example `2026-07-06`.
inline constexpr std::string_view date_iso = "yyyy-mm-dd";
/// 24-hour time display with seconds.
inline constexpr std::string_view time_hh_mm_ss = "hh:mm:ss";
/// ISO-like date and 24-hour time display with seconds.
inline constexpr std::string_view date_time_iso = "yyyy-mm-dd hh:mm:ss";
} // namespace number_format

/// Convenience constructors for common streaming number-format styles.
///
/// API mode: Streaming style metadata for new workbooks. The returned CellStyle
/// values only set `number_format`; they allocate no workbook state until passed
/// to WorkbookWriter::add_style(). They do not create worksheet relationships,
/// do not infer date/time values, and do not imply existing-file style support.
namespace style_preset {
[[nodiscard]] inline CellStyle date_iso()
{
    CellStyle style;
    style.number_format = std::string(number_format::date_iso);
    return style;
}

[[nodiscard]] inline CellStyle time_hh_mm_ss()
{
    CellStyle style;
    style.number_format = std::string(number_format::time_hh_mm_ss);
    return style;
}

[[nodiscard]] inline CellStyle date_time_iso()
{
    CellStyle style;
    style.number_format = std::string(number_format::date_time_iso);
    return style;
}
} // namespace style_preset

/// Date/time conversion helpers for numeric Excel cells.
///
/// API mode: Streaming-friendly value helpers. FastXLSX still writes dates and
/// times as numeric cells; callers attach a number-format style separately.
/// These helpers implement Excel's 1900 date system, including the historical
/// serial-60 gap for the nonexistent 1900-02-29. They do not support the 1904
/// date system, do not infer time zones, and do not create styles, shared
/// strings, formulas, relationships, or workbook metadata.
namespace date_time {
/// Converts a valid Gregorian date to an Excel 1900 date-system serial.
///
/// Accepted range is 1900-01-01 through 9999-12-31. 1900-01-01 maps to serial
/// 1, 1900-02-28 maps to 59, and 1900-03-01 maps to 61 to preserve Excel's
/// serial-60 compatibility gap.
///
/// @throws FastXlsxError when the date is invalid or outside the supported
/// range.
[[nodiscard]] double excel_1900_date_serial(std::chrono::year_month_day date);

/// Converts a time-of-day duration to an Excel fractional day.
///
/// Accepted range is [00:00:00, 24:00:00). No time zone, daylight-saving, or
/// calendar-date inference is performed.
///
/// @throws FastXlsxError when the duration is negative or not less than one day.
[[nodiscard]] double excel_1900_time_fraction(std::chrono::milliseconds time_of_day);

/// Converts a date plus time-of-day to an Excel 1900 date-system serial.
///
/// This is equivalent to excel_1900_date_serial(date) plus
/// excel_1900_time_fraction(time_of_day), with the same validation and no
/// time-zone inference.
///
/// @throws FastXlsxError when either argument is outside its supported range.
[[nodiscard]] double excel_1900_date_time_serial(
    std::chrono::year_month_day date, std::chrono::milliseconds time_of_day);
} // namespace date_time

/// Keeps the active ZIP backend default compression policy.
///
/// With the minizip-ng backend this delegates to the minizip/zlib default
/// rather than forcing FastXLSX's measured throughput-first level.
inline constexpr int default_zip_compression_level = -1;
/// Requests no-compression/stored ZIP output.
inline constexpr int min_zip_compression_level = 0;
/// Highest zlib-compatible DEFLATE compression level accepted by the writer.
inline constexpr int max_zip_compression_level = 9;

/// Options for WorkbookWriter.
///
/// API mode: Streaming. Options are captured by WorkbookWriter::create() and are
/// not currently mutable after the writer has been created.
struct WorkbookWriterOptions {
    /// Controls how string cells are represented in worksheet XML.
    StringStrategy string_strategy = StringStrategy::InlineString;

    /// ZIP compression level for Streaming new-workbook output.
    ///
    /// `default_zip_compression_level` keeps the active backend default
    /// (minizip/zlib default when the minizip-ng backend is enabled),
    /// `min_zip_compression_level` requests no-compression/stored output, and
    /// values `1..max_zip_compression_level` request zlib-compatible DEFLATE
    /// levels when the minizip-ng backend is enabled. Callers that prefer
    /// throughput over smaller output can explicitly pass level 1.
    /// Dependency-free stored bootstrap builds can only write
    /// no-compression/stored packages, so positive DEFLATE levels are rejected
    /// before worksheet rows are written.
    ///
    /// This option affects ZIP close-time CPU cost and output size only. It
    /// does not change worksheet row streaming, does not enable Zip64, and does
    /// not edit existing XLSX files.
    int zip_compression_level = default_zip_compression_level;

    /// Document metadata written to `docProps/core.xml` and `docProps/app.xml`.
    ///
    /// This is small workbook metadata copied into WorkbookWriter state during
    /// create(). It does not create custom document properties, does not edit
    /// existing XLSX files, and does not affect worksheet row/cell streaming.
    DocumentProperties document_properties;
};

/// Value kind for a conditional-formatting color-scale endpoint.
///
/// API mode: Streaming worksheet metadata. Minimum and Maximum are serialized
/// without `val`; Number, Percent, and Percentile require finite numeric values.
/// This enum only supports the current color-scale slice and does not represent
/// formula-based conditional formatting or dxf-backed formatting rules.
enum class ColorScaleValueType {
    Minimum,
    Maximum,
    Number,
    Percent,
    Percentile,
};

/// Value kind for a conditional-formatting data-bar endpoint.
///
/// API mode: Streaming worksheet metadata. Minimum and Maximum are serialized
/// without `val`; Number, Percent, and Percentile require finite numeric values.
/// This enum only supports the current basic data-bar slice and does not
/// represent formula-based conditional formatting or dxf-backed rules.
enum class DataBarValueType {
    Minimum,
    Maximum,
    Number,
    Percent,
    Percentile,
};

/// Icon set style for the current narrow conditional-formatting icon-set slice.
///
/// API mode: Streaming worksheet metadata. This enum currently exposes only the
/// stable built-in `3Arrows` OpenXML token. It does not represent custom icons,
/// icon-set extensions, dxf-backed formatting, or complete Excel UI parity.
enum class IconSetStyle {
    ThreeArrows,
};

/// Value kind for conditional-formatting icon-set thresholds.
///
/// API mode: Streaming worksheet metadata. Icon-set thresholds are serialized as
/// finite numeric `<cfvo>` values. This first slice intentionally excludes
/// formula thresholds, minimum/maximum endpoints, custom icons, and extLst data.
enum class IconSetValueType {
    Number,
    Percent,
    Percentile,
};

/// One point of a conditional-formatting color scale.
///
/// API mode: Streaming worksheet metadata. The point is copied into worksheet
/// state when added. FastXLSX writes it as one `<cfvo>` plus one inline
/// `<color rgb="..."/>` in worksheet XML; it does not create styles.xml, dxfs,
/// worksheet relationships, or content type entries.
struct ColorScalePoint {
    /// OpenXML `cfvo` type for this endpoint.
    ColorScaleValueType type = ColorScaleValueType::Minimum;

    /// Numeric `cfvo` value for Number, Percent, and Percentile endpoint types.
    /// Ignored for Minimum and Maximum endpoint types.
    double value = 0.0;

    /// Inline ARGB color for this endpoint.
    ArgbColor color;
};

/// A narrow two-color conditional-formatting color scale.
///
/// API mode: Streaming worksheet metadata for new workbooks. The rule is copied
/// into WorksheetWriter state and serialized as worksheet-local
/// `<conditionalFormatting>` XML during close(). Priorities are assigned by
/// call order per worksheet. This does not evaluate cell values, inspect prior
/// rows, create styles.xml/dxfs, edit existing XLSX files, or promise full Excel
/// conditional-formatting UI parity.
struct TwoColorScaleRule {
    /// Lower endpoint, defaulting to OpenXML `type="min"`.
    ColorScalePoint lower;

    /// Upper endpoint, defaulting to OpenXML `type="max"`.
    ColorScalePoint upper {ColorScaleValueType::Maximum, 0.0, ArgbColor {0xFF, 0xFF, 0xFF, 0xFF}};
};

/// A narrow three-color conditional-formatting color scale.
///
/// API mode: Streaming worksheet metadata for new workbooks. The rule is copied
/// into WorksheetWriter state and serialized as worksheet-local
/// `<conditionalFormatting>` XML during close(). Priorities are assigned by
/// call order per worksheet. This does not evaluate cell values, inspect prior
/// rows, create styles.xml/dxfs, edit existing XLSX files, or promise full Excel
/// conditional-formatting UI parity.
struct ThreeColorScaleRule {
    /// Lower endpoint, defaulting to OpenXML `type="min"`.
    ColorScalePoint lower;

    /// Midpoint, defaulting to OpenXML `type="percentile" val="50"`.
    ColorScalePoint midpoint {
        ColorScaleValueType::Percentile,
        50.0,
        ArgbColor {0xFF, 0xFF, 0xEB, 0x84},
    };

    /// Upper endpoint, defaulting to OpenXML `type="max"`.
    ColorScalePoint upper {ColorScaleValueType::Maximum, 0.0, ArgbColor {0xFF, 0xFF, 0xFF, 0xFF}};
};

/// One endpoint of a basic conditional-formatting data bar.
///
/// API mode: Streaming worksheet metadata. The endpoint is copied into
/// worksheet state when added. FastXLSX writes it as one `<cfvo>` in worksheet
/// XML; it does not create styles.xml, dxfs, worksheet relationships, or content
/// type entries.
struct DataBarEndpoint {
    /// OpenXML `cfvo` type for this endpoint.
    DataBarValueType type = DataBarValueType::Minimum;

    /// Numeric `cfvo` value for Number, Percent, and Percentile endpoint types.
    /// Ignored for Minimum and Maximum endpoint types.
    double value = 0.0;
};

/// A narrow basic conditional-formatting data bar.
///
/// API mode: Streaming worksheet metadata for new workbooks. The rule is copied
/// into WorksheetWriter state and serialized as worksheet-local
/// `<conditionalFormatting>` XML during close(). Priorities are assigned by
/// call order per worksheet, shared with color-scale and icon-set rules. This does not
/// evaluate cell values, create styles.xml/dxfs, edit existing XLSX files, or
/// promise full Excel conditional-formatting UI parity.
struct DataBarRule {
    /// Lower endpoint, defaulting to OpenXML `type="min"`.
    DataBarEndpoint lower;

    /// Upper endpoint, defaulting to OpenXML `type="max"`.
    DataBarEndpoint upper {DataBarValueType::Maximum, 0.0};

    /// Inline ARGB bar color.
    ArgbColor color {0xFF, 0x63, 0x8E, 0xC6};

    /// Whether Excel should display the cell value next to the data bar.
    /// The default is omitted from XML; `false` writes `showValue="0"`.
    bool show_value = true;
};

/// A narrow built-in conditional-formatting icon set.
///
/// API mode: Streaming worksheet metadata for new workbooks. The rule is copied
/// into WorksheetWriter state and serialized as worksheet-local
/// `<conditionalFormatting>` XML during close(). Priorities are assigned by
/// call order per worksheet, shared with color-scale and data-bar rules. This
/// first slice writes only built-in 3-arrow icon sets with three finite numeric
/// thresholds; it does not evaluate cell values, create styles.xml/dxfs, use
/// custom icons, write extLst, edit existing XLSX files, or promise full Excel
/// conditional-formatting UI parity.
struct IconSetRule {
    /// Built-in icon set style. The current slice supports only `3Arrows`.
    IconSetStyle style = IconSetStyle::ThreeArrows;

    /// OpenXML `cfvo` type shared by all thresholds.
    IconSetValueType value_type = IconSetValueType::Percent;

    /// Three finite, strictly ascending thresholds, defaulting to the common 0/33/67 percent split.
    std::array<double, 3> thresholds {0.0, 33.0, 67.0};

    /// Whether Excel should display the cell value next to the icon.
    /// The default is omitted from XML; `false` writes `showValue="0"`.
    bool show_value = true;

    /// Whether Excel should reverse icon order.
    /// The default is omitted from XML; `true` writes `reverse="1"`.
    bool reverse = false;
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
/// metadata and serialized on the existing two-cell anchor. A non-empty
/// external hyperlink URL is serialized as drawing metadata with a drawing-local
/// external hyperlink relationship. These options do not change image bytes,
/// content types, worksheet row/cell streaming, row/column size geometry, or
/// the anchor cell range.
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

    /// Optional external hyperlink URL for the picture object.
    ///
    /// Empty omits hyperlink metadata. A non-empty value creates a drawing-local
    /// external hyperlink relationship and an `a:hlinkClick` child under
    /// `xdr:cNvPr`. FastXLSX copies the string, does not validate URL
    /// reachability, does not write worksheet cell hyperlinks, and does not
    /// create hyperlink styles.
    std::string external_hyperlink_url;

    /// Optional picture hyperlink tooltip.
    ///
    /// Empty omits the tooltip attribute. This is only valid when
    /// external_hyperlink_url is non-empty.
    std::string external_hyperlink_tooltip;
};

/// A cell view consumed immediately by WorksheetWriter.
///
/// API mode: Streaming. Text and formula values are non-owning string views and
/// only need to remain valid for the duration of append_row(). CellView itself
/// does not own string storage; the SharedString strategy may still allocate
/// workbook-level table entries during append_row(). The current cell value
/// surface supports explicit blank cells, but has no dedicated date type;
/// date/time values are still numeric cells supplied through number(). Use the
/// date_time helpers to calculate 1900-system serial values and register a
/// number_format / style_preset style when display formatting is needed.
class CellView {
public:
    /// Non-owning value kind consumed by WorksheetWriter::append_row().
    enum class Type {
        Blank,
        Number,
        String,
        Boolean,
        Formula,
    };

    /// Creates an explicit blank cell view.
    ///
    /// API mode: Streaming. A blank cell writes an empty `<c r="..."/>` element
    /// and participates in row width / dimension tracking. It is distinct from
    /// omitting a cell from the appended row and from text("") empty strings.
    /// Blank cells do not create inline strings, shared strings, formulas,
    /// worksheet relationships, workbook relationships, or content type
    /// side effects. A workbook-owned StyleId may be attached with
    /// with_style(), in which case only the cell `s` attribute is emitted.
    static CellView blank() noexcept;

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
    /// calculation-chain metadata in the current implementation. Empty formula
    /// text is rejected by WorksheetWriter::append_row() before row state is
    /// advanced.
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
    Type type_ = Type::Blank;
    double number_value_ = 0.0;
    std::string_view text_value_;
    bool boolean_value_ = false;
    StyleId style_id_;
};

/// A column-indexed cell view consumed by WorksheetWriter::append_sparse_row().
///
/// API mode: Streaming. `column` is a 1-based Excel column index and `cell` has
/// the same non-owning lifetime rules as CellView. Sparse row entries are
/// consumed only for the duration of append_sparse_row(); the worksheet writer
/// does not retain a row matrix, sort entries, or synthesize omitted cells.
struct SparseCellView {
    std::uint32_t column = 0;
    CellView cell;
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
    /// cell value is not finite, a formula cell has empty text, row height
    /// metadata is non-positive or non-finite, or the writer cannot write
    /// worksheet XML.
    void append_row(std::span<const CellView> cells, RowOptions options = {});

    /// Appends a row from an initializer list.
    ///
    /// This is a convenience wrapper over the span overload and has the same
    /// streaming and string-lifetime rules.
    void append_row(std::initializer_list<CellView> cells, RowOptions options = {});

    /// Appends a row from a sparse, column-indexed cell view range.
    ///
    /// Cells are written only for the provided entries. `SparseCellView::column`
    /// is 1-based, must be within Excel's column limit, and entries must be
    /// strictly increasing with no duplicates. Gaps are omitted rather than
    /// serialized as blank cells; use CellView::blank() when an explicit blank
    /// cell element is required. The row number is assigned automatically by
    /// append order and string_view values only need to remain valid for this
    /// call.
    ///
    /// Memory use is proportional to the number of sparse entries and the
    /// current row XML buffer, not the highest referenced column. Shared strings
    /// and formula recalculation metadata are updated only after validation
    /// succeeds; validation failures occur before row state is advanced.
    ///
    /// @throws FastXlsxError when sparse columns are invalid or not strictly
    /// increasing, row limits are exceeded, a numeric cell value is not finite,
    /// row height metadata is non-positive or non-finite, a style id does not
    /// belong to this workbook, or the writer cannot write worksheet XML.
    void append_sparse_row(std::span<const SparseCellView> cells, RowOptions options = {});

    /// Appends a sparse row from an initializer list.
    ///
    /// This is a convenience wrapper over the span overload and has the same
    /// streaming and string-lifetime rules.
    void append_sparse_row(
        std::initializer_list<SparseCellView> cells, RowOptions options = {});

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
    /// columns; for example (1, 1) freezes the first row and first column. A
    /// zero/zero split clears the current setting.
    /// This records worksheet metadata only and does not inspect or rewrite
    /// previously written row XML.
    ///
    /// @throws FastXlsxError if either split leaves no valid scrollable pane
    /// inside Excel worksheet limits.
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

    /// Records one worksheet-local two-color conditional-formatting color scale.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. The rule is
    /// emitted as worksheet-local `<conditionalFormatting>` XML and does not add
    /// package relationships, content types, styles.xml, dxfs, or cell text.
    /// FastXLSX copies the two endpoints into writer state, validates the range
    /// and finite numeric endpoint values, and assigns priority by call order.
    /// It does not parse formulas, evaluate cell values, normalize overlapping
    /// rules, or edit existing XLSX files.
    ///
    /// @throws FastXlsxError if the range is invalid, the workbook is closed, or
    /// the rule shape is outside the current narrow two-color scale surface.
    void add_conditional_color_scale(CellRange range, TwoColorScaleRule rule);

    /// Records one worksheet-local three-color conditional-formatting color scale.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. The rule is
    /// emitted as worksheet-local `<conditionalFormatting>` XML and does not add
    /// package relationships, content types, styles.xml, dxfs, or cell text.
    /// FastXLSX copies the three points into writer state, validates the range
    /// and finite numeric point values, and assigns priority by call order.
    /// It does not parse formulas, evaluate cell values, normalize overlapping
    /// rules, or edit existing XLSX files.
    ///
    /// @throws FastXlsxError if the range is invalid, the workbook is closed, or
    /// the rule shape is outside the current narrow three-color scale surface.
    void add_conditional_color_scale(CellRange range, ThreeColorScaleRule rule);

    /// Records one two-color conditional-formatting rule for multiple ranges.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. Ranges are
    /// copied into writer state and serialized as one space-separated `sqref`
    /// attribute on a single `<conditionalFormatting>` element. This does not
    /// sort, merge, deduplicate, or overlap-check ranges; memory grows with the
    /// copied range count and number of rules, not with worksheet row or cell
    /// count.
    ///
    /// @throws FastXlsxError if the range list is empty, any range is invalid,
    /// the workbook is closed, or endpoint values are outside the current
    /// finite-value color-scale surface.
    void add_conditional_color_scale(std::span<const CellRange> ranges, TwoColorScaleRule rule);

    /// Records one three-color conditional-formatting rule for multiple ranges.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. Ranges are
    /// copied into writer state and serialized as one space-separated `sqref`
    /// attribute on a single `<conditionalFormatting>` element. This does not
    /// sort, merge, deduplicate, or overlap-check ranges; memory grows with the
    /// copied range count and number of rules, not with worksheet row or cell
    /// count.
    ///
    /// @throws FastXlsxError if the range list is empty, any range is invalid,
    /// the workbook is closed, or point values are outside the current
    /// finite-value color-scale surface.
    void add_conditional_color_scale(std::span<const CellRange> ranges, ThreeColorScaleRule rule);

    /// Convenience overload for multiple conditional-formatting ranges.
    ///
    /// The initializer-list ranges are copied during this call.
    void add_conditional_color_scale(
        std::initializer_list<CellRange> ranges, TwoColorScaleRule rule);

    /// Convenience overload for multiple three-color conditional-formatting ranges.
    ///
    /// The initializer-list ranges are copied during this call.
    void add_conditional_color_scale(
        std::initializer_list<CellRange> ranges, ThreeColorScaleRule rule);

    /// Records one worksheet-local basic conditional-formatting data bar.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. The rule is
    /// emitted as worksheet-local `<conditionalFormatting>` XML and does not add
    /// package relationships, content types, styles.xml, dxfs, or cell text.
    /// FastXLSX copies the two endpoints and bar color into writer state,
    /// validates the range and finite numeric endpoint values, and assigns
    /// priority by call order shared with color-scale rules. This first slice
    /// does not support negative-bar colors, axes, borders, gradients, extLst,
    /// formula endpoints, or existing XLSX editing.
    ///
    /// @throws FastXlsxError if the range is invalid, the workbook is closed, or
    /// the rule shape is outside the current basic data-bar surface.
    void add_conditional_data_bar(CellRange range, DataBarRule rule);

    /// Records one basic conditional-formatting data bar for multiple ranges.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. Ranges are
    /// copied into writer state and serialized as one space-separated `sqref`
    /// attribute on a single `<conditionalFormatting>` element. This does not
    /// sort, merge, deduplicate, or overlap-check ranges; memory grows with the
    /// copied range count and number of rules, not with worksheet row or cell
    /// count.
    ///
    /// @throws FastXlsxError if the range list is empty, any range is invalid,
    /// the workbook is closed, or endpoint values are outside the current
    /// finite-value data-bar surface.
    void add_conditional_data_bar(std::span<const CellRange> ranges, DataBarRule rule);

    /// Convenience overload for multiple basic data-bar ranges.
    ///
    /// The initializer-list ranges are copied during this call.
    void add_conditional_data_bar(std::initializer_list<CellRange> ranges, DataBarRule rule);

    /// Records one worksheet-local built-in 3-arrow conditional-formatting icon set.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. The rule is
    /// emitted as worksheet-local `<conditionalFormatting>` XML and does not add
    /// package relationships, content types, styles.xml, dxfs, metadata parts,
    /// or cell text. FastXLSX copies the three thresholds into writer state,
    /// validates the range and finite ascending values, and assigns priority by
    /// call order shared with color-scale and data-bar rules.
    ///
    /// @throws FastXlsxError if the range is invalid, the workbook is closed, or
    /// the rule shape is outside the current basic icon-set surface.
    void add_conditional_icon_set(CellRange range, IconSetRule rule);

    /// Records one built-in 3-arrow conditional-formatting icon set for multiple ranges.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. Ranges are
    /// copied into writer state and serialized as one space-separated `sqref`
    /// attribute on a single `<conditionalFormatting>` element. This does not
    /// sort, merge, deduplicate, or overlap-check ranges; memory grows with the
    /// copied range count and number of rules, not with worksheet row or cell
    /// count.
    ///
    /// @throws FastXlsxError if the range list is empty, any range is invalid,
    /// the workbook is closed, or threshold values are outside the current
    /// finite ascending icon-set surface.
    void add_conditional_icon_set(std::span<const CellRange> ranges, IconSetRule rule);

    /// Convenience overload for multiple basic icon-set ranges.
    ///
    /// The initializer-list ranges are copied during this call.
    void add_conditional_icon_set(std::initializer_list<CellRange> ranges, IconSetRule rule);

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
    /// `editAs` on the anchor, non-visual `xdr:cNvPr` name/description
    /// attributes, and optional drawing-local external hyperlink metadata.
    /// Empty strings preserve the generated `Picture N` name, omit the
    /// description, and omit hyperlink metadata.
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

    /// Records a PNG/JPEG image from caller-owned memory.
    ///
    /// API mode: Streaming worksheet metadata for new workbooks. The span only
    /// needs to remain valid for the duration of this call. FastXLSX validates
    /// the bytes through the required stb-backed image metadata helper, then
    /// copies the original bytes to a temporary file-backed media entry so
    /// close() can package them without retaining the caller span or putting
    /// image bytes in worksheet row state. This creates the same media,
    /// drawing, relationship, worksheet `<drawing>`, and content type side
    /// effects as add_image(path, anchor). It does not decode a full pixel
    /// buffer, crop, rotate, recompress, convert formats, mutate existing
    /// drawings, or edit existing XLSX files.
    ///
    /// @throws FastXlsxError if the anchor range or ImageOptions offsets are
    /// invalid, the workbook is closed, the memory buffer is empty or unreadable,
    /// or the image format is outside the current PNG/JPEG slice.
    void add_image(std::span<const std::byte> bytes, CellRange anchor);

    /// Records a memory-backed PNG/JPEG image with optional drawing metadata.
    ///
    /// Same behavior as add_image(bytes, anchor), plus ImageOptions
    /// serialization to drawing anchor marker offsets / `xdr:cNvPr` attributes.
    void add_image(std::span<const std::byte> bytes, CellRange anchor, ImageOptions options);

private:
    friend class WorkbookWriter;
#ifdef FASTXLSX_ENABLE_TEST_HOOKS
    friend void detail::testing_set_worksheet_row_count(
        WorksheetWriter& worksheet, std::uint32_t row_count);
    friend bool detail::testing_worksheet_temporary_resources_released(
        const WorksheetWriter& worksheet) noexcept;
    friend std::size_t detail::testing_worksheet_pending_body_buffer_bytes(
        const WorksheetWriter& worksheet) noexcept;
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
    /// workbook is closed. The current slices support custom number formats,
    /// wrap-text plus limited horizontal/vertical alignment,
    /// bold/italic/direct-color font metadata, and solid foreground fills;
    /// border, full fill/pattern control, full alignment, full font control,
    /// rich text, conditional formatting, and existing-file style preservation
    /// remain outside this API. Registering a style does not touch worksheet
    /// row XML until a CellView carries the returned id through with_style().
    ///
    /// @throws FastXlsxError if the writer is uninitialized or closed, or the
    /// style contains no property supported by the current narrow slice.
    StyleId add_style(CellStyle style);

    /// Adds a worksheet and returns its streaming handle.
    ///
    /// Worksheets are assigned workbook relationship ids in add order. Sheet
    /// names are validated against Excel's basic restrictions and must be
    /// unique. Duplicate checks are ASCII case-insensitive. New worksheets
    /// cannot be added after close().
    ///
    /// @throws FastXlsxError if the writer is uninitialized or closed, or if the
    /// worksheet name is invalid or duplicates an existing sheet name
    /// case-insensitively.
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
