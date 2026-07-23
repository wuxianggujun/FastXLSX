#pragma once

#include <fastxlsx/workbook.hpp>
#include <fastxlsx/worksheet_metadata.hpp>

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

/// Source shape represented by one shared-string run callback.
enum class SharedStringRunKind {
    /// One simple direct `<si><t>...</t></si>` item projected as one run.
    SimpleText,

    /// One explicit `<si><r>...</r></si>` rich-text run.
    RichText,
};

/// One shared-string item boundary value.
///
/// index is the zero-based workbook-local shared-string index. The value owns
/// no borrowed data and may be copied beyond the callback.
struct SharedStringRunItemView {
    std::uint32_t index = 0;
};

/// Narrow owning formatting projected for one shared-string run.
///
/// direct_argb_color, when present, stores an OpenXML eight-digit `rgb` value
/// as `0xAARRGGBB`. Theme and tint inheritance are not resolved.
struct SharedStringRunFormat {
    bool bold = false;
    bool italic = false;
    std::optional<std::uint32_t> direct_argb_color;
};

/// One forward-only shared-string run callback view.
///
/// text is borrowed and valid only for the duration of the current on_run
/// callback. Copy it when it must outlive the callback. The indexes, kind, and
/// format are owning values. run_index is zero-based within item_index.
struct SharedStringRunView {
    std::uint32_t item_index = 0;
    std::uint32_t run_index = 0;
    SharedStringRunKind kind = SharedStringRunKind::SimpleText;
    std::string_view text;
    SharedStringRunFormat format;
};

/// Callbacks used by WorkbookReader::read_shared_string_runs().
///
/// All callbacks are optional. On successful traversal, each item is emitted
/// synchronously as one on_item_start callback, one or more on_run callbacks,
/// then one on_item_end callback. Simple items are represented by one
/// SimpleText run. A parser/package failure may follow callbacks already emitted
/// for earlier items or the active item; consumers that need atomic results must
/// publish them only after the traversal returns successfully. Exceptions
/// propagate unchanged, the active package-entry stream is released during
/// unwinding, and a later traversal starts over.
struct SharedStringRunReadCallbacks {
    std::function<void(const SharedStringRunItemView&)> on_item_start;
    std::function<void(const SharedStringRunView&)> on_run;
    std::function<void(const SharedStringRunItemView&)> on_item_end;
};

/// Guardrails for one bounded shared-string run traversal.
struct SharedStringRunReaderOptions {
    /// Maximum bytes retained by the sharedStrings XML token window.
    std::size_t max_xml_window_bytes = 64U * 1024U;

    /// Maximum decoded text bytes accepted across one item.
    std::size_t max_item_text_bytes = 64U * 1024U;

    /// Maximum decoded text bytes retained for the active run.
    std::size_t max_run_text_bytes = 64U * 1024U;

    /// Maximum simple/rich runs accepted and emitted for one item.
    std::size_t max_runs_per_item = 64U * 1024U;

    /// Maximum XML element nesting depth retained by the structural stack.
    std::size_t max_xml_nesting_depth = 64U;
};

/// Summary returned after one successful shared-string run traversal.
struct SharedStringRunReadSummary {
    std::uint64_t item_count = 0;
    std::uint64_t run_count = 0;
    std::size_t peak_item_text_bytes = 0;
    std::size_t peak_run_text_bytes = 0;
    std::size_t peak_runs_per_item = 0;
    std::size_t peak_xml_nesting_depth = 0;
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

/// Fill pattern projected by the narrow style-components reader.
enum class CellFormatFillPattern {
    None,
    Gray125,
    Solid,
};

/// One zero-based font record projected from the styles part.
///
/// All fields are callback-independent values. direct_argb_color, when present,
/// stores an OpenXML eight-digit `rgb` value as `0xAARRGGBB`. The narrow
/// projection supports bold, italic, direct ARGB color, and the fixed default
/// font metadata emitted by FastXLSX. It does not resolve theme colors or expose
/// font name, size, family, scheme, underline, strike, or rich-text semantics.
struct CellFormatFontView {
    std::uint32_t index = 0;
    bool bold = false;
    bool italic = false;
    std::optional<std::uint32_t> direct_argb_color;
};

/// One zero-based fill record projected from the styles part.
///
/// All fields are callback-independent values. A Solid record carries a direct
/// ARGB foreground color as `0xAARRGGBB`; None and Gray125 records do not carry
/// a foreground color. Gradient, theme, tint, and other pattern semantics are
/// outside this narrow projection.
struct CellFormatFillView {
    std::uint32_t index = 0;
    CellFormatFillPattern pattern = CellFormatFillPattern::None;
    std::optional<std::uint32_t> foreground_argb_color;
};

/// Callbacks used by WorkbookReader::read_style_components().
///
/// Callbacks are optional and run synchronously in styles source order. Values
/// contain no borrowed strings and may be copied beyond the callback. User
/// exceptions propagate unchanged; the active package-entry stream is released
/// during unwinding and a later traversal starts over.
struct StyleComponentReadCallbacks {
    std::function<void(const CellFormatFontView&)> on_font;
    std::function<void(const CellFormatFillView&)> on_fill;
};

/// Guardrails for one bounded styles font/fill traversal.
struct StyleComponentReaderOptions {
    /// Maximum bytes retained by the styles XML token window.
    std::size_t max_xml_window_bytes = 64U * 1024U;

    /// Maximum XML element nesting depth retained by the structural stack.
    std::size_t max_xml_nesting_depth = 64U;

    /// Maximum font records accepted and emitted in one traversal.
    std::size_t max_font_count = 64U * 1024U;

    /// Maximum fill records accepted and emitted in one traversal.
    std::size_t max_fill_count = 64U * 1024U;
};

/// Summary returned after one successful styles font/fill traversal.
struct StyleComponentReadSummary {
    std::uint64_t font_count = 0;
    std::uint64_t fill_count = 0;
    std::size_t peak_xml_nesting_depth = 0;
};

/// Owning projection of the primary worksheet frozen pane.
///
/// The split values are counts of frozen rows and columns. `topLeftCell` and
/// `activePane`, when present in the source XML, are audited for supported
/// syntax but are intentionally not exposed by this narrow companion.
struct WorksheetFrozenPaneView {
    std::uint32_t row_split = 0;
    std::uint32_t column_split = 0;
};

/// Owning projection of one worksheet-root auto-filter.
///
/// This is the worksheet-root element only. Table-local filter metadata is a
/// separate part and is not traversed by this companion.
struct WorksheetAutoFilterView {
    CellRange range;
};

/// Owning projection of one worksheet-root merged-cell range.
///
/// `index` is zero-based in source order within the worksheet's mergeCells
/// container. A merged range always covers at least two cells.
struct WorksheetMergedCellView {
    std::uint64_t index = 0;
    CellRange range;
};

/// Callbacks used by WorkbookReader::read_worksheet_metadata().
///
/// Callbacks are synchronous and source ordered: a primary frozen pane (when
/// present), the worksheet-root auto-filter (when present), then merged-cell
/// ranges. All callback fields contain owning values, so they may be retained
/// after the callback returns. A later parser/package failure may follow
/// callbacks already delivered for earlier metadata; callers needing an atomic
/// collected result must publish it only after successful return. Exceptions
/// propagate unchanged; a later call on the same WorkbookReader starts a fresh
/// package-entry traversal.
struct WorksheetMetadataReadCallbacks {
    std::function<void(const WorksheetFrozenPaneView&)> on_frozen_pane;
    std::function<void(const WorksheetAutoFilterView&)> on_auto_filter;
    std::function<void(const WorksheetMergedCellView&)> on_merged_cell;
};

/// Guardrails for one bounded worksheet metadata traversal.
struct WorksheetMetadataReaderOptions {
    /// Maximum bytes retained by the worksheet XML token window.
    std::size_t max_xml_window_bytes = 64U * 1024U;

    /// Maximum metadata element nesting depth retained by the structural
    /// stack. This is independent of worksheet size.
    std::size_t max_xml_nesting_depth = 64U;

    /// Maximum source bytes accepted for one A1/range reference or pane
    /// top-left cell attribute.
    std::size_t max_range_reference_bytes = 256U;

    /// Maximum worksheet sheetView elements audited in one traversal.
    std::size_t max_sheet_view_count = 1024U;

    /// Maximum merged-cell elements retained for overlap auditing.
    std::size_t max_merged_cell_count = 64U * 1024U;
};

/// Summary returned after one successful bounded worksheet metadata traversal.
struct WorksheetMetadataReadSummary {
    std::uint64_t frozen_pane_count = 0;
    std::uint64_t auto_filter_count = 0;
    std::uint64_t merged_cell_count = 0;
    std::size_t peak_xml_nesting_depth = 0;
    std::size_t peak_range_reference_bytes = 0;
    std::size_t peak_sheet_view_count = 0;
    std::size_t peak_retained_merged_cell_count = 0;
};

/// Owning projection of one worksheet-local data-validation rule.
///
/// `index` is zero-based in source order within the worksheet-root
/// dataValidations container. The range list and DataValidationRule strings
/// own their data and may be copied beyond the callback.
struct WorksheetDataValidationView {
    std::uint64_t index = 0;
    std::vector<CellRange> ranges;
    DataValidationRule rule;
};

/// Callbacks used by WorkbookReader::read_worksheet_data_validations().
///
/// The optional callback runs synchronously once per complete validation in
/// source order. A later parser/package failure may follow callbacks already
/// delivered; callers needing an atomic collected result must publish it only
/// after successful return. User exceptions propagate unchanged, and a later
/// call on the same WorkbookReader starts a fresh package-entry traversal.
struct WorksheetDataValidationReadCallbacks {
    std::function<void(const WorksheetDataValidationView&)> on_data_validation;
};

/// Guardrails for one bounded worksheet data-validation traversal.
struct WorksheetDataValidationReaderOptions {
    /// Maximum bytes retained by the worksheet XML token window.
    std::size_t max_xml_window_bytes = 64U * 1024U;

    /// Maximum worksheet metadata nesting depth retained by the structural stack.
    std::size_t max_xml_nesting_depth = 64U;

    /// Maximum dataValidation records accepted and emitted in one traversal.
    std::size_t max_validation_count = 64U * 1024U;

    /// Maximum ranges retained for one active dataValidation record.
    std::size_t max_ranges_per_validation = 64U * 1024U;

    /// Maximum decoded bytes accepted for one `sqref` attribute.
    std::size_t max_sqref_bytes = 64U * 1024U;

    /// Maximum decoded bytes retained for each formula1/formula2 value.
    std::size_t max_formula_text_bytes = 64U * 1024U;

    /// Maximum decoded bytes retained for each prompt/error title or text field.
    std::size_t max_metadata_text_bytes = 32U * 1024U;
};

/// Summary returned after one successful worksheet data-validation traversal.
struct WorksheetDataValidationReadSummary {
    std::uint64_t validation_count = 0;
    std::uint64_t range_count = 0;
    std::size_t peak_ranges_per_validation = 0;
    std::size_t peak_sqref_bytes = 0;
    std::size_t peak_formula_text_bytes = 0;
    std::size_t peak_metadata_text_bytes = 0;
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
/// read_shared_string_runs() is an independent traversal of the same part. It
/// preserves item/run boundaries and narrow owning run formatting while also
/// representing a simple item as one default-format run. It does not change the
/// stricter read_shared_strings() projection or join results to worksheet cells.
///
/// read_cell_formats() is another separate bounded companion. It emits custom
/// number-format definitions and narrow cellXfs records without building a
/// complete styles registry. Worksheet style indexes, font/fill references,
/// and number-format references remain explicitly caller-resolved.
///
/// read_style_components() separately projects the writer-compatible font and
/// fill subset. It does not make component ids portable or automatically join
/// component records to cellXfs or worksheet style indexes.
///
/// read_worksheet_metadata() is an independent bounded companion. It audits
/// worksheet structure and projects only the primary frozen pane, the
/// worksheet-root auto-filter, and worksheet-root merged-cell ranges. It does
/// not construct a worksheet DOM, dense matrix, CellStore, relationship graph,
/// or Patch/In-memory handoff.
///
/// read_worksheet_data_validations() separately projects the writer-compatible
/// worksheet-root data-validation subset as owning range/rule values. It does
/// not create a full validation object model or connect the values to Patch or
/// In-memory state.
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

    /// Traverses simple and narrow rich shared-string runs with bounded memory.
    ///
    /// A fresh sharedStrings package-entry source is owned only for this call.
    /// The unique internal relationship, normalized target, part presence, and
    /// content type receive the same audit as read_shared_strings(). Successful
    /// return closes the entry after CRC/size validation; callback, parser, or
    /// package failures release it during unwinding and a later call may retry.
    ///
    /// Valid callbacks run in item/index source order as item start, one or more
    /// runs, and item end. A direct `<t>` item becomes one SimpleText run. Rich
    /// `<r>` records remain distinct RichText runs and project bold, italic, and
    /// optional direct ARGB color while accepting only fixed Calibri 11,
    /// family 2, minor-scheme, or theme-1 default metadata. Run text is borrowed
    /// only for on_run; boundary indexes and run formatting are owning values.
    /// Earlier callbacks, including a start or run for the active item, may have
    /// executed before a later parser/package failure; successful return is the
    /// completion signal for callers that require an atomic collected result.
    ///
    /// XML window, item/run text, runs-per-item, and nesting limits bound parser
    /// state. Phonetic/extension metadata, mixed simple/rich item shapes,
    /// underline/strike and other run properties, non-default font metadata,
    /// theme/tint inheritance, malformed containers, and unsupported markup
    /// fail explicitly rather than being flattened.
    ///
    /// This read-only traversal does not build a shared-string table, resolve
    /// worksheet shared-string indexes, mutate the package, materialize a
    /// worksheet, or hand values to Patch or In-memory editors.
    ///
    /// @throws FastXlsxError if the reader is moved from, an option is zero, the
    /// relationship/content-type/target audit fails, package reading fails, or
    /// sharedStrings XML is malformed or outside the narrow projection. User
    /// callback exceptions propagate unchanged.
    [[nodiscard]] SharedStringRunReadSummary read_shared_string_runs(
        const SharedStringRunReadCallbacks& callbacks = {},
        SharedStringRunReaderOptions options = {}) const;

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

    /// Traverses narrow font and fill records with bounded memory.
    ///
    /// A fresh styles package-entry source is owned only for this call. The
    /// styles relationship, normalized target, part presence, and content type
    /// receive the same audit as read_cell_formats(). Successful return closes
    /// the entry after CRC/size validation; callback, parser, or package failures
    /// release it during unwinding and a later call may retry from the beginning.
    ///
    /// Font and fill indexes are zero-based workbook-local values. Font records
    /// project bold/italic and optional direct ARGB color while accepting only
    /// the fixed default size/name/family/scheme/theme metadata emitted by the
    /// current writer. Fill records project none, gray125, or solid direct-ARGB
    /// patterns. Unsupported font properties, non-default theme metadata,
    /// gradients, other pattern/color forms, malformed containers, and count
    /// mismatches fail explicitly rather than being flattened.
    ///
    /// All callback fields are owning scalar values. Memory is bounded by the
    /// XML window, nesting depth, current component record, and configured
    /// callback-count limits; the method does not retain a component table.
    /// This read-only traversal does not build a styles DOM/registry, resolve
    /// cellXfs or WorksheetCellView::style_index automatically, mutate the
    /// package, or hand styles to Patch or In-memory editors.
    ///
    /// @throws FastXlsxError if the reader is moved from, an option is zero, the
    /// relationship/content-type/target audit fails, package reading fails, or
    /// styles XML is malformed or outside the narrow projection. User callback
    /// exceptions propagate unchanged.
    [[nodiscard]] StyleComponentReadSummary read_style_components(
        const StyleComponentReadCallbacks& callbacks = {},
        StyleComponentReaderOptions options = {}) const;

    /// Traverses narrow worksheet metadata in source order with bounded memory.
    ///
    /// The worksheet relationship/part is opened afresh for this call. The
    /// primary `workbookViewId="0"` direct frozen pane is projected before
    /// sheetData, followed by the worksheet-root autoFilter and mergeCells
    /// records after sheetData. Other workbook views are audited but are not
    /// projected. Callback values own their scalar/range data and may be copied
    /// beyond the callback. Callback, parser, and package failures release the
    /// active entry during unwinding; a later call retries from the beginning.
    ///
    /// The projection accepts only frozen panes with integer row/column splits,
    /// a single worksheet-root autoFilter with a valid A1 range, and
    /// non-overlapping multi-cell mergeCell ranges. Split/frozenSplit panes,
    /// primary-view pivotSelection, duplicate target containers, malformed
    /// nesting/QName, schema-order violations, count mismatches, and guardrail
    /// violations fail explicitly. `topLeftCell` and `activePane` are audited
    /// but not exposed. Table-local filters and all non-target metadata remain
    /// outside this read-only companion.
    ///
    /// This method does not seek, materialize a worksheet, mutate the package,
    /// or alter relationships, content types, manifest, Patch state, or
    /// In-memory state.
    ///
    /// @throws FastXlsxError if the reader is moved from, the worksheet is
    /// absent, an option is zero, package reading fails, or worksheet XML is
    /// malformed/unsupported. User callback exceptions propagate unchanged.
    [[nodiscard]] WorksheetMetadataReadSummary read_worksheet_metadata(
        std::string_view sheet_name,
        const WorksheetMetadataReadCallbacks& callbacks = {},
        WorksheetMetadataReaderOptions options = {}) const;

    /// Traverses worksheet-root data validations in source order with bounded memory.
    ///
    /// A fresh worksheet package-entry source is owned only for this call. Each
    /// complete direct `<dataValidation>` child is projected as a zero-based
    /// source index, one or more owning CellRange values, and an owning shared
    /// DataValidationRule. Entity-decoded formula, prompt, error, and title text
    /// is retained only for the active rule. Successful return is the completion
    /// signal when callbacks have been collected atomically.
    ///
    /// The narrow projection requires an explicit writer-compatible type,
    /// non-empty `sqref`, formula1, and the same operator/formula2 shape accepted
    /// by Streaming/Patch serialization. Container count, QName, direct-child
    /// nesting, worksheet suffix schema order, boolean/enumeration syntax, and
    /// configured XML/range/text limits are audited. Unsupported `imeMode`,
    /// extension/foreign attributes or children, and non-default container
    /// prompt-window metadata fail explicitly instead of being flattened.
    ///
    /// This read-only method does not evaluate formulas, validate cell values,
    /// detect range overlap, seek, materialize a worksheet, mutate relationships,
    /// content types or manifest state, or hand values to Patch/In-memory APIs.
    ///
    /// @throws FastXlsxError if the reader is moved from, the worksheet is
    /// absent, an option is zero, package reading fails, or worksheet validation
    /// XML is malformed or outside the narrow projection. User callback
    /// exceptions propagate unchanged.
    [[nodiscard]] WorksheetDataValidationReadSummary
    read_worksheet_data_validations(
        std::string_view sheet_name,
        const WorksheetDataValidationReadCallbacks& callbacks = {},
        WorksheetDataValidationReaderOptions options = {}) const;

private:
    struct Impl;

    explicit WorkbookReader(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_;
};

} // namespace fastxlsx
