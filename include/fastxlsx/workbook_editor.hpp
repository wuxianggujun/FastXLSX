#pragma once

/// @file workbook_editor.hpp
/// Minimal public Patch-mode facade for editing an existing XLSX workbook.

#include <fastxlsx/cell_value.hpp>
#include <fastxlsx/workbook.hpp>

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

class WorkbookEditor;
class WorksheetEditor;

namespace detail {
#ifdef FASTXLSX_ENABLE_TEST_HOOKS
struct WorkbookEditorPackagePlanAccessor;

using WorkbookEditorSaveAsStagedHook = void (*)();

void testing_set_workbook_editor_save_as_staged_hook(
    WorkbookEditorSaveAsStagedHook hook) noexcept;

void testing_workbook_editor_materialize_source_sheet(
    WorkbookEditor& editor,
    std::string_view planned_name,
    std::string_view source_sheet_name);
void testing_workbook_editor_set_materialized_cell(
    WorkbookEditor& editor,
    std::string_view planned_name,
    std::uint32_t row,
    std::uint32_t column,
    const CellValue& value);
void testing_workbook_editor_erase_materialized_cell(
    WorkbookEditor& editor,
    std::string_view planned_name,
    std::uint32_t row,
    std::uint32_t column);
void testing_workbook_editor_flush_materialized_sessions_to_patch_plan(
    WorkbookEditor& editor);
[[nodiscard]] std::size_t testing_workbook_editor_materialized_session_count(
    const WorkbookEditor& editor) noexcept;
[[nodiscard]] std::size_t testing_workbook_editor_dirty_materialized_session_count(
    const WorkbookEditor& editor) noexcept;
[[nodiscard]] bool testing_workbook_editor_has_materialized_session(
    const WorkbookEditor& editor, std::string_view planned_name) noexcept;
[[nodiscard]] std::vector<std::string> testing_workbook_editor_dirty_materialized_session_names(
    const WorkbookEditor& editor);
#endif
} // namespace detail

/// Public guardrails for the current narrow WorkbookEditor Patch facade.
///
/// API mode: Patch. These limits apply only to replacement rows passed to
/// WorkbookEditor::replace_sheet_data(). They do not materialize source
/// worksheet cells, do not enable random editing, and are not workbook-level
/// memory limits for materialized WorksheetEditor sessions.
struct WorkbookEditorOptions {
    /// Maximum number of explicit CellValue records accepted by one
    /// replace_sheet_data() call.
    ///
    /// Empty row vectors advance no stored cells. Blank CellValue objects inside
    /// a row still count as explicit replacement cells because they can affect
    /// the saved <sheetData> payload.
    std::optional<std::size_t> max_replacement_cells;

    /// Estimated CellStore memory budget for one replace_sheet_data() call.
    ///
    /// This is a conservative budget check based on the temporary sparse store
    /// used to build the replacement <sheetData> chunk source. It is not exact
    /// process RSS tracking and does not include source package bytes, generated
    /// XML chunks, PackageEditor staging files, ZIP writer buffers, or future
    /// save-time assembly costs.
    std::optional<std::size_t> replacement_memory_budget_bytes;
};

/// Controls whether source worksheet cells may be projected into the narrower
/// WorksheetEditor cell model when source-only semantics would be discarded.
enum class WorksheetMaterializationPolicy {
    /// Reject known lossy projections such as rich text formatting, formula
    /// metadata, and cached formula results. This is the safe default.
    RejectKnownLosses,

    /// Explicitly allow supported source cells to be flattened into the current
    /// scalar/formula CellStore model. The discarded source semantics cannot be
    /// recovered when the materialized worksheet is saved.
    AllowLossyProjection,
};

/// Stable semantic category for a strict source-to-WorksheetEditor projection loss.
enum class WorksheetMaterializationLossCategory {
    /// Rich text runs would be flattened into one plain string.
    RichText,

    /// Phonetic guides or phonetic display metadata would be discarded.
    PhoneticMetadata,

    /// Source extension metadata would be discarded.
    ExtensionMetadata,

    /// Formula attributes such as array/shared formula metadata would be discarded.
    FormulaMetadata,

    /// A cached formula result would be discarded because formulas retain text only.
    CachedFormulaResult,
};

/// Public context for one strict WorksheetEditor materialization rejection.
///
/// Row and column are 1-based worksheet coordinates. shared_string_index is the
/// zero-based OOXML sharedStrings index when the rejected source semantics came
/// from a referenced shared string item; otherwise it is std::nullopt. The
/// diagnostic intentionally contains no XML tokens, parser state, part names,
/// relationship ids, or internal CellStore details.
struct WorksheetMaterializationDiagnostic {
    /// Semantic loss that caused strict materialization to reject the source cell.
    WorksheetMaterializationLossCategory category =
        WorksheetMaterializationLossCategory::RichText;

    /// Worksheet name from the current planned workbook catalog.
    std::string worksheet_name;

    /// One-based source row.
    std::uint32_t row = 1;

    /// One-based source column.
    std::uint32_t column = 1;

    /// Zero-based sharedStrings index when the loss came from a referenced item.
    std::optional<std::size_t> shared_string_index;
};

/// Typed failure raised when RejectKnownLosses blocks worksheet materialization.
///
/// This derives from FastXlsxError for source compatibility with existing catch
/// sites. Catch this type when code needs a stable loss category or source-cell
/// context. Throwing it does not register a materialized session, queue an edit,
/// or update WorkbookEditor::last_edit_error().
class WorksheetMaterializationError final : public FastXlsxError {
public:
    explicit WorksheetMaterializationError(
        WorksheetMaterializationDiagnostic diagnostic);

    [[nodiscard]] const WorksheetMaterializationDiagnostic& diagnostic() const noexcept;

private:
    WorksheetMaterializationDiagnostic diagnostic_;
};

/// Guardrails for one materialized worksheet editing session.
///
/// API mode: In-memory / existing-workbook small-file editing. These limits are
/// passed per WorkbookEditor::worksheet() call and apply while loading source
/// worksheet cells into the editor-owned sparse store and while mutating that
/// store. They are intentionally separate from WorkbookEditorOptions, which
/// guards whole-<sheetData> replacement payloads.
struct WorksheetEditorOptions {

    /// Maximum number of sparse cell records allowed in the materialized
    /// worksheet session.
    std::optional<std::size_t> max_cells;

    /// Estimated sparse-store memory budget for the materialized worksheet.
    ///
    /// This is a conservative CellStore estimate, not process RSS. It excludes
    /// source package bytes, generated XML chunks, PackageEditor staging files,
    /// ZIP writer buffers, and save-time package assembly costs.
    std::optional<std::size_t> memory_budget_bytes;

    /// Source-to-CellStore projection policy for the initial materialization.
    ///
    /// The default rejects source constructs whose semantics the current
    /// WorksheetEditor cannot preserve. Callers must opt in explicitly before
    /// rich text or formula metadata/cached results may be flattened.
    WorksheetMaterializationPolicy materialization_policy =
        WorksheetMaterializationPolicy::RejectKnownLosses;
};

/// Coarse public summary of a worksheet-level edit queued in WorkbookEditor.
///
/// API mode: Patch. This value describes only public facade state that will be
/// visible through worksheet_names(), pending replacement diagnostics,
/// materialized WorksheetEditor dirty-state diagnostics, and save_as(). It is
/// not an internal EditPlan entry, package part diff, dependency audit,
/// relationship audit, or semantic workbook diff.
struct WorkbookEditorWorksheetEditSummary {
    /// Worksheet name in the opened source workbook catalog.
    std::string source_name;

    /// Worksheet name in the current planned catalog that save_as() will write.
    std::string planned_name;

    /// True when rename_sheet() has queued a sheet-catalog name change for this
    /// source worksheet.
    bool renamed = false;

    /// True when replace_sheet_data() has queued a whole-<sheetData>
    /// replacement for this planned worksheet name.
    bool sheet_data_replaced = false;

    /// True when replace_cells() has queued targeted cell patches for this
    /// planned worksheet name.
    bool targeted_cells_replaced = false;

    /// True when the materialized WorksheetEditor session for this planned
    /// worksheet name is dirty and waiting for save_as() auto-flush.
    bool materialized_dirty = false;

    /// Explicit replacement cells represented by the final queued
    /// replace_sheet_data() payload for this worksheet. Zero when
    /// sheet_data_replaced is false.
    std::size_t replacement_cell_count = 0;

    /// Estimated sparse-store memory recorded for the final queued replacement
    /// payload for this worksheet. This excludes source package bytes,
    /// generated XML chunks, PackageEditor staging files, ZIP writer buffers,
    /// and save-time package assembly costs.
    std::size_t estimated_replacement_memory_usage = 0;

    /// Unique target cells represented by the final queued replace_cells()
    /// patches for this worksheet. Zero when targeted_cells_replaced is false.
    std::size_t targeted_cell_replacement_count = 0;

    /// Sum of currently staged single-cell replacement XML payload bytes for
    /// final targeted-cell patch targets on this worksheet. This excludes source
    /// worksheet XML, PackageEditor temporary files, ZIP writer buffers, and
    /// save-time package assembly costs.
    std::size_t estimated_targeted_cell_replacement_xml_bytes = 0;

    /// Active sparse cell records currently held by the dirty materialized
    /// WorksheetEditor session. Zero when materialized_dirty is false.
    std::size_t materialized_cell_count = 0;

    /// Estimated sparse-store memory for the dirty materialized WorksheetEditor
    /// session. This is a CellStore estimate, not process RSS, and excludes
    /// source package bytes, generated XML chunks, PackageEditor staging files,
    /// ZIP writer buffers, and save-time package assembly costs.
    std::size_t estimated_materialized_memory_usage = 0;
};

/// Public source-to-planned worksheet catalog entry for WorkbookEditor.
///
/// API mode: Patch. This value shows how one source workbook sheet will appear
/// in the current planned catalog used by worksheet_names(), has_worksheet(),
/// replace_sheet_data(), and save_as(). It does not expose workbook
/// relationships, worksheet part names, package entries, or internal EditPlan
/// state.
struct WorkbookEditorWorksheetCatalogEntry {
    /// Worksheet name in the opened source workbook catalog.
    std::string source_name;

    /// Worksheet name in the current planned catalog.
    std::string planned_name;

    /// True when source_name differs from planned_name because rename_sheet()
    /// has queued a sheet-catalog rename.
    bool renamed = false;
};

/// Public coordinate for a sparse WorksheetEditor cell snapshot.
///
/// API mode: In-memory / existing-workbook small-file inspection. Coordinates
/// are 1-based Excel worksheet row/column indexes. This value is an owning copy
/// returned from WorksheetEditor::sparse_cells(); it does not borrow internal
/// CellStore state and is not an iterator handle.
struct WorksheetCellReference {
    std::uint32_t row = 0;
    std::uint32_t column = 0;
};

/// Public value snapshot for one sparse WorksheetEditor cell.
///
/// API mode: In-memory / existing-workbook small-file inspection. The value is
/// copied out of the materialized sparse store. Explicit blank cells are
/// represented by CellValue::blank(). CellValue carries any materialized
/// source StyleId handle already attached to the sparse record, including
/// styles preserved by value-only updates, clears, and row/column shifts. This
/// snapshot does not expose workbook style table details, relationships, or
/// worksheet metadata beyond the CellValue payload.
struct WorksheetCellSnapshot {
    WorksheetCellReference reference;
    CellValue value;
};

/// Public sparse full-cell update for WorksheetEditor::set_cells().
///
/// API mode: In-memory / existing-workbook small-file mutation. Each update is
/// an explicit 1-based coordinate plus an owning CellValue payload. The batch
/// API consumes these values synchronously; it does not borrow caller storage,
/// parse A1 ranges, allocate dense matrices, or provide streaming random
/// editing semantics.
struct WorksheetCellUpdate {
    WorksheetCellReference reference;
    CellValue value;
};

/// Missing-target behavior for WorkbookEditor::replace_cells().
///
/// API mode: Patch / existing-workbook targeted cell editing. This enum only
/// controls whether targeted patches fail on missing source/planned cells or
/// insert those point cells. It does not enable row/column shifting, table
/// resize, style/sharedStrings migration, relationship repair, or general
/// semantic worksheet editing.
enum class CellPatchMissingCellPolicy {
    /// Every target cell must already exist in the scanned worksheet stream.
    Fail,

    /// Missing target cells are inserted as point edits; missing rows are
    /// synthesized as minimal `<row r="N">` records.
    Insert,
};

/// Public diagnostic for a materialized formula's sheet-qualified reference.
///
/// API mode: In-memory inspection over already-materialized worksheets. This
/// value reports formula text references such as `Data!A1` or
/// `'Other Sheet'!B2` found in materialized WorksheetEditor sessions. It is a
/// dependency-risk diagnostic for narrow edits such as rename_sheet(); it does
/// not evaluate formulas, rewrite formula text, validate all Excel formula
/// grammar, rebuild calcChain, or scan non-materialized worksheet parts.
struct WorkbookEditorFormulaReferenceAudit {
    /// Source workbook sheet containing the formula cell.
    std::string formula_sheet_source_name;

    /// Current planned sheet name containing the formula cell.
    std::string formula_sheet_planned_name;

    /// Coordinate of the materialized formula cell.
    WorksheetCellReference formula_cell;

    /// Materialized formula text as stored by FastXLSX, without a leading '='.
    std::string formula_text;

    /// Raw sheet qualifier text, including quotes when present and trailing '!'.
    std::string sheet_qualifier_text;

    /// Raw reference token after the sheet qualifier, for example `A1`,
    /// `A1:B2`, `A:C`, or `1:3`.
    std::string reference_text;

    /// Raw qualifier plus reference text, for example `Data!A1` or
    /// `'Other Sheet'!A1:B2`.
    std::string qualified_reference_text;

    /// Decoded sheet-name token from the qualifier, excluding quotes and '!'.
    std::string referenced_sheet_name;

    /// True when the qualifier used Excel's single-quoted sheet-name form.
    bool qualifier_quoted = false;

    /// True when the qualifier appears to name an external workbook, such as
    /// `[Book.xlsx]Sheet1!A1` or `'[Book.xlsx]Sheet1'!A1`.
    bool external_workbook_qualifier = false;

    /// True when the qualifier appears to be a 3D sheet range, such as
    /// `Sheet1:Sheet3!A1`. This is reported for audit only; FastXLSX does not
    /// interpret the sheet range semantics.
    bool sheet_range_qualifier = false;

    /// True when referenced_sheet_name matched a source or planned sheet in the
    /// current workbook catalog using the same ASCII case-insensitive rule as
    /// sheet names.
    bool matched_current_workbook_sheet = false;

    /// Matched source sheet name when matched_current_workbook_sheet is true.
    std::string matched_source_sheet_name;

    /// Matched current planned sheet name when matched_current_workbook_sheet
    /// is true.
    std::string matched_planned_sheet_name;

    /// True when the qualifier still names a source sheet whose current planned
    /// name differs, for example after rename_sheet("Data", "RenamedData").
    bool references_renamed_source_name = false;

    /// True when the qualifier names the matched sheet's current planned name.
    bool references_planned_sheet_name = false;
};

/// Public diagnostic for a workbook definedName formula's sheet-qualified reference.
///
/// API mode: Patch / read-only source workbook inspection. This value reports
/// formula text references such as `Data!$A$1` or `'Other Sheet'!$A$1:$B$2`
/// found in source workbook `<definedNames><definedName>...</definedName>`
/// entries. It is a dependency-risk diagnostic for narrow edits such as
/// rename_sheet(); it does not evaluate formulas, rewrite definedName text,
/// validate all Excel formula grammar, rebuild calcChain, or scan worksheet
/// formula cells.
struct WorkbookEditorDefinedNameFormulaReferenceAudit {
    /// The unqualified definedName `name` attribute. Empty only when absent.
    std::string defined_name;

    /// Raw formula text stored in the definedName element body, without a
    /// leading '='.
    std::string formula_text;

    /// True when the definedName has a `localSheetId` attribute.
    bool local_sheet_scope = false;

    /// Raw `localSheetId` attribute text when local_sheet_scope is true.
    std::string local_sheet_id_text;

    /// True when localSheetId resolved to the current workbook catalog order.
    bool local_sheet_scope_resolved = false;

    /// Source sheet name for a resolved localSheetId scope.
    std::string scope_sheet_source_name;

    /// Current planned sheet name for a resolved localSheetId scope.
    std::string scope_sheet_planned_name;

    /// Raw sheet qualifier text, including quotes when present and trailing '!'.
    std::string sheet_qualifier_text;

    /// Raw reference token after the sheet qualifier, for example `A1`,
    /// `$A$1:$B$2`, `A:C`, or `1:3`.
    std::string reference_text;

    /// Raw qualifier plus reference text, for example `Data!$A$1` or
    /// `'Other Sheet'!$A$1:$B$2`.
    std::string qualified_reference_text;

    /// Decoded sheet-name token from the qualifier, excluding quotes and '!'.
    std::string referenced_sheet_name;

    /// True when the qualifier used Excel's single-quoted sheet-name form.
    bool qualifier_quoted = false;

    /// True when the qualifier appears to name an external workbook, such as
    /// `[Book.xlsx]Sheet1!A1` or `'[Book.xlsx]Sheet1'!A1`.
    bool external_workbook_qualifier = false;

    /// True when the qualifier appears to be a 3D sheet range, such as
    /// `Sheet1:Sheet3!A1`. This is reported for audit only; FastXLSX does not
    /// interpret the sheet range semantics.
    bool sheet_range_qualifier = false;

    /// True when referenced_sheet_name matched a source or planned sheet in the
    /// current workbook catalog using the same ASCII case-insensitive rule as
    /// sheet names.
    bool matched_current_workbook_sheet = false;

    /// Matched source sheet name when matched_current_workbook_sheet is true.
    std::string matched_source_sheet_name;

    /// Matched current planned sheet name when matched_current_workbook_sheet
    /// is true.
    std::string matched_planned_sheet_name;

    /// True when the qualifier still names a source sheet whose current planned
    /// name differs, for example after rename_sheet("Data", "RenamedData").
    bool references_renamed_source_name = false;

    /// True when the qualifier names the matched sheet's current planned name.
    bool references_planned_sheet_name = false;
};

/// Formula-reference handling for WorkbookEditor::rename_sheet().
///
/// API mode: Patch / existing-workbook workbook metadata rewrite. The default
/// keeps the current narrow catalog-only behavior and reports formula risks
/// through audit APIs. The opt-in rewrite policies are intentionally narrow and
/// do not evaluate formulas, scan non-materialized worksheet XML, update
/// tables/drawings/charts/hyperlinks, rebuild calcChain, or repair
/// relationships.
enum class WorkbookEditorRenameFormulaPolicy {
    /// Preserve formula text and expose risks through audit diagnostics.
    AuditOnly,

    /// Rewrite direct workbook definedName formula references from the old
    /// sheet name to the new sheet name. In rename chains, references that
    /// still use the sheet's original source name are rewritten too when that
    /// source name differs from both the current old name and the new name.
    /// External workbook qualifiers, 3D sheet ranges, unsupported/nested
    /// definedName XML, worksheet formula cells, and other workbook/worksheet
    /// metadata remain outside this policy.
    RewriteDefinedNames,

    /// Rewrite direct workbook definedName formula references and formula cells
    /// already loaded into WorkbookEditor-owned WorksheetEditor materialized
    /// sessions. This does not materialize or scan source worksheet parts, and
    /// formula cells in non-materialized worksheets remain audit-only.
    RewriteDefinedNamesAndMaterializedWorksheetFormulas,
};

/// Options for WorkbookEditor::rename_sheet().
///
/// API mode: Patch. Options affect only the small `xl/workbook.xml` metadata
/// rewrite queued by rename_sheet() plus, when explicitly requested,
/// already-materialized WorksheetEditor formula cells. They do not materialize
/// worksheets or enable a formula calculation engine.
struct WorkbookEditorRenameOptions {
    WorkbookEditorRenameFormulaPolicy formula_policy =
        WorkbookEditorRenameFormulaPolicy::AuditOnly;
};

/// Borrowed random cell editor for one WorkbookEditor-owned materialized sheet.
///
/// API mode: In-memory / existing-workbook small-file editing. WorksheetEditor
/// is not a standalone owner; it references the open WorkbookEditor that created
/// it. Supported cells are stored sparsely, so legal far-edge coordinates do not
/// imply a dense worksheet matrix or a large-file performance guarantee. Dirty
/// sessions are persisted only when WorkbookEditor::save_as() flushes them into
/// the Patch plan; clean read-only materialization does not rewrite the sheet.
///
/// Materialization is a narrow cell-value projection rather than a lossless
/// worksheet object model. Plain blank, numeric, boolean, scalar string, error,
/// formula-text, inline-string, and workbook-backed shared-string cells can
/// become CellValue records. Source style ids are same-workbook numeric
/// passthrough handles only. Worksheet metadata, relationships, drawings,
/// comments, tables, validations, hyperlinks, macros, and unknown extensions are
/// outside the materialized state and remain governed by Patch preservation.
///
/// WorksheetEditorOptions defaults to
/// WorksheetMaterializationPolicy::RejectKnownLosses. It rejects materialization
/// before session registration when a referenced cell would discard known source
/// semantics, including inline/shared rich text or phonetic/extension metadata,
/// formula metadata, or cached formula results. Callers may explicitly select
/// AllowLossyProjection to flatten those supported cells into plain text or
/// formula text. Discarded semantics cannot be reconstructed by save_as(), and
/// the projection policy participates in session option matching.
/// RejectKnownLosses failures throw WorksheetMaterializationError so callers can
/// inspect a stable semantic category and worksheet/cell context without parsing
/// exception text or depending on internal XML parser details.
///
/// Dirty projection may reuse or append to a narrowly validated source
/// sharedStrings table and otherwise writes inline strings. It does not evaluate
/// formulas, generate cached values, rebuild calcChain, migrate styles, repair
/// relationships, synchronize range-bearing metadata, or provide large-file
/// low-memory random editing. Non-default caller-supplied StyleId values remain
/// unsupported; value-only writes can preserve already materialized source style
/// handles, while erase operations remove sparse records.
class WorksheetEditor {
public:
    /// Returns the planned worksheet name for this borrowed handle.
    [[nodiscard]] std::string_view name() const noexcept;

    /// Returns the sparse-store value for a cell, or std::nullopt if the cell is
    /// not currently represented by the materialized worksheet state.
    ///
    /// Row and column are 1-based Excel coordinates. Invalid coordinates throw
    /// FastXlsxError and do not update WorkbookEditor::last_edit_error().
    /// Invalid read failures also do not dirty a saved materialized session.
    /// An explicit blank cell is returned as CellValue::blank(). This read does
    /// not flush or reload the materialized session.
    [[nodiscard]] std::optional<CellValue> try_cell(
        std::uint32_t row, std::uint32_t column) const;

    /// Returns the sparse-store value for a strict uppercase A1 cell reference.
    ///
    /// The reference must name exactly one cell, such as `A1` or
    /// `XFD1048576`. Lowercase references, ranges, zero or leading-zero rows,
    /// and coordinates outside Excel limits throw FastXlsxError. This
    /// convenience overload parses the reference and then uses the row/column
    /// overload; it does not add range iteration or large-file random access
    /// semantics. Invalid read failures do not update last_edit_error() or
    /// dirty saved sessions. This read does not flush or reload the
    /// materialized session.
    [[nodiscard]] std::optional<CellValue> try_cell(
        std::string_view cell_reference) const;

    /// Returns whether one cell is represented by the materialized sparse store.
    ///
    /// API mode: In-memory / existing-workbook small-file inspection. Row and
    /// column are 1-based Excel coordinates. This checks represented state only:
    /// explicit blank records return true, missing cells return false, and no
    /// CellValue payload is copied. Invalid coordinates throw FastXlsxError and
    /// do not update WorkbookEditor::last_edit_error(). This is not a dense
    /// matrix probe, metadata read, worksheet `<dimension>` check, or large-file
    /// low-memory random access API; it does not mutate, flush, or reload the
    /// materialized session.
    [[nodiscard]] bool contains_cell(std::uint32_t row, std::uint32_t column) const;

    /// Returns whether a strict uppercase A1 cell reference is represented.
    ///
    /// The reference must name exactly one cell, such as `A1` or
    /// `XFD1048576`. Lowercase references, ranges, zero or leading-zero rows,
    /// and coordinates outside Excel limits throw FastXlsxError. This is a
    /// parsing convenience over the row/column overload and preserves the same
    /// represented-state, diagnostic, and non-mutating semantics.
    [[nodiscard]] bool contains_cell(std::string_view cell_reference) const;

    /// Returns the sparse-store value for a cell.
    ///
    /// Missing sparse cells throw FastXlsxError so callers cannot accidentally
    /// conflate "not represented" with an explicit CellValue::blank() record.
    /// Use try_cell() when missing cells are expected. Invalid coordinates also
    /// throw FastXlsxError. This read does not mutate, flush, or reload the
    /// session, does not dirty saved sessions, and does not update
    /// WorkbookEditor::last_edit_error().
    [[nodiscard]] CellValue get_cell(std::uint32_t row, std::uint32_t column) const;

    /// Returns the sparse-store value for a strict uppercase A1 cell reference.
    ///
    /// Missing sparse cells and invalid references throw FastXlsxError. Like the
    /// row/column read overload, this read does not mutate, flush, or reload the
    /// session, does not dirty saved sessions, and does not update
    /// WorkbookEditor::last_edit_error().
    [[nodiscard]] CellValue get_cell(std::string_view cell_reference) const;

    /// Sets or replaces one sparse-store cell value.
    ///
    /// Row and column are 1-based Excel coordinates. `XFD1048576` is the last
    /// valid Excel cell; coordinates outside the worksheet limits are mutation
    /// failures: they do not mutate the sparse store and update the owning
    /// WorkbookEditor::last_edit_error(). They also do not dirty a saved
    /// materialized session. Passing CellValue::blank() creates an explicit
    /// blank record that dirty save_as() projects as an empty cell; inserting a
    /// new explicit blank record is subject to the same sparse-store max_cells
    /// and memory_budget_bytes guardrails as other cell values. Supported
    /// number, boolean, and formula values use the current sparse-store
    /// projection; formulas are not evaluated and do not generate cached
    /// results.
    ///
    /// Non-default StyleId handles are rejected because the first public slice
    /// has no existing-workbook style registry or migration policy. An explicit
    /// default StyleId{0} is accepted and normalized to no style handle. A
    /// rejected call does not mutate or dirty the sparse store.
    void set_cell(std::uint32_t row, std::uint32_t column, const CellValue& value);

    /// Applies sparse full-cell replacements as one preflighted batch.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. Each update
    /// names one explicit 1-based row/column coordinate. The input order is
    /// respected and duplicate coordinates are allowed; later updates win after
    /// the whole batch passes validation. Empty input is a successful no-op that
    /// does not dirty the materialized session and clears prior public edit
    /// diagnostics. Explicit default StyleId{0} handles are normalized to no
    /// style handle and do not serialize as `s="0"`. Invalid coordinates,
    /// caller-supplied non-default StyleId handles, max_cells violations, or
    /// memory_budget_bytes violations reject the entire batch before the active
    /// sparse store is mutated.
    ///
    /// This is a sparse convenience over full-cell replacement, not a dense
    /// range writer, A1 range parser, style-preserving value edit, style
    /// migration/merge API, range metadata recalculation, or large-file
    /// low-memory random-editing path.
    void set_cells(std::span<const WorksheetCellUpdate> cells);

    /// Applies sparse full-cell replacements from a small literal batch.
    ///
    /// This convenience overload consumes the initializer-list synchronously and
    /// delegates to the std::span overload, so validation, duplicate-coordinate
    /// ordering, empty-batch no-op behavior, guardrails, diagnostics, and
    /// non-goals are identical.
    void set_cells(std::initializer_list<WorksheetCellUpdate> cells);

    /// Appends one sparse row after the current maximum represented row.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The appended
    /// row is derived from the materialized sparse store, not from worksheet row
    /// metadata: if the store is empty, values are written to row 1; otherwise
    /// they are written to `max(represented row) + 1`. Values are written to
    /// columns 1..N in input order. Empty input is a successful no-op that does
    /// not create row metadata, does not dirty the materialized session, and
    /// clears prior public edit diagnostics.
    ///
    /// The entire append is preflighted and staged. Explicit default
    /// StyleId{0} handles are normalized to no style handle and do not serialize
    /// as `s="0"`. More than 16,384 values, appending past Excel row
    /// 1,048,576, caller-supplied non-default StyleId handles, max_cells
    /// violations, or memory_budget_bytes violations reject the append before
    /// the active sparse store is mutated. Explicit CellValue::blank() values
    /// are represented as blank cells in the appended row and are subject to the
    /// same sparse-store guardrails.
    /// Appended records are new sparse cells: they do not inherit source StyleId
    /// handles from prior rows, and no style metadata is synthesized.
    ///
    /// This is not row insertion, row metadata creation, table/range metadata
    /// recalculation, style migration/merge, sharedStrings migration, or a
    /// large-file low-memory random-editing path.
    void append_row(std::span<const CellValue> values);

    /// Appends one sparse row from a small literal value list.
    ///
    /// This convenience overload consumes the initializer-list synchronously and
    /// delegates to the std::span overload, so row selection, empty-input no-op
    /// behavior, guardrails, diagnostics, and non-goals are identical.
    void append_row(std::initializer_list<CellValue> values);

    /// Replaces one represented sparse row.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The row is
    /// a 1-based Excel row number. The method removes all currently represented
    /// sparse cells in that row, then writes `values` to columns 1..N in input
    /// order. Passing an empty value list clears the represented row; if the row
    /// has no represented sparse cells, it is a successful no-op that does not
    /// create row metadata, does not dirty the materialized session, and clears
    /// prior public edit diagnostics.
    ///
    /// The entire replacement is preflighted and staged. Explicit default
    /// StyleId{0} handles are normalized to no style handle and do not serialize
    /// as `s="0"`. Invalid row numbers, more than 16,384 values,
    /// caller-supplied non-default StyleId handles, max_cells violations, or
    /// memory_budget_bytes violations reject the replacement before the active
    /// sparse store is mutated. Explicit CellValue::blank() values are
    /// represented as blank cells and are subject to the same sparse-store
    /// guardrails. Because this is full-cell replacement, source StyleId handles
    /// on overwritten target-row cells are dropped; non-target sparse cells keep
    /// their existing style handles.
    ///
    /// This is not row insertion/deletion, row shifting, row metadata editing,
    /// table/range metadata recalculation, style migration/merge,
    /// sharedStrings migration, or a large-file low-memory random-editing path.
    void set_row(std::uint32_t row, std::span<const CellValue> values);

    /// Replaces one represented sparse row from a small literal value list.
    ///
    /// This convenience overload consumes the initializer-list synchronously and
    /// delegates to the std::span overload, so row replacement, row-clear
    /// no-op behavior, guardrails, diagnostics, and non-goals are identical.
    void set_row(std::uint32_t row, std::initializer_list<CellValue> values);

    /// Replaces one represented sparse column.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The column
    /// is a 1-based Excel column number. The method removes all currently
    /// represented sparse cells in that column, then writes `values` to rows
    /// 1..N in input order. Passing an empty value list clears the represented
    /// column; if the column has no represented sparse cells, it is a successful
    /// no-op that does not create column metadata, does not dirty the
    /// materialized session, and clears prior public edit diagnostics.
    ///
    /// The entire replacement is preflighted and staged. Explicit default
    /// StyleId{0} handles are normalized to no style handle and do not serialize
    /// as `s="0"`. Invalid column numbers, more than 1,048,576 values,
    /// caller-supplied non-default StyleId handles, max_cells violations, or
    /// memory_budget_bytes violations reject the replacement before the active
    /// sparse store is mutated. Explicit CellValue::blank() values are
    /// represented as blank cells and are subject to the same sparse-store
    /// guardrails. Because this is full-cell replacement, source StyleId handles
    /// on overwritten target-column cells are dropped; non-target sparse cells
    /// keep their existing style handles.
    ///
    /// This is not column insertion/deletion, column shifting, column metadata
    /// editing, table/range metadata recalculation, style migration/merge,
    /// sharedStrings migration, or a large-file low-memory random-editing path.
    void set_column(std::uint32_t column, std::span<const CellValue> values);

    /// Replaces one represented sparse column from a small literal value list.
    ///
    /// This convenience overload consumes the initializer-list synchronously and
    /// delegates to the std::span overload, so column replacement, column-clear
    /// no-op behavior, guardrails, diagnostics, and non-goals are identical.
    void set_column(std::uint32_t column, std::initializer_list<CellValue> values);

    /// Removes represented sparse cells from one row.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The row is
    /// a 1-based Excel row number. Only active sparse records already present
    /// in that row are removed; missing rows are successful no-ops that do not
    /// dirty the materialized session and clear prior public edit diagnostics.
    /// Invalid row numbers are mutation failures and update
    /// WorkbookEditor::last_edit_error().
    ///
    /// Dirty save_as() omits erased sparse records from projected sheetData and
    /// may shrink the worksheet dimension. This is not row deletion, row
    /// shifting, row metadata editing, dense range deletion, tombstone output,
    /// table/range metadata recalculation, relationship repair, or a large-file
    /// low-memory random-editing path.
    void erase_row(std::uint32_t row);

    /// Removes represented sparse cells from an inclusive row range.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The
    /// row-range bounds are 1-based Excel row numbers and must satisfy
    /// `first_row <= last_row`. Only active sparse records already present in
    /// those rows are removed; missing-only ranges are successful no-ops that
    /// do not dirty the materialized session and clear prior public edit
    /// diagnostics. Invalid or reversed ranges reject before mutating the
    /// sparse store and update WorkbookEditor::last_edit_error().
    ///
    /// Dirty save_as() omits erased sparse records from projected sheetData and
    /// may shrink the worksheet dimension. This is not row deletion, row
    /// shifting, row metadata editing, dense range deletion, tombstone output,
    /// table/range metadata recalculation, relationship repair, or a large-file
    /// low-memory random-editing path.
    void erase_rows(std::uint32_t first_row, std::uint32_t last_row);

    /// Removes represented sparse cells from one column.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The column
    /// is a 1-based Excel column number. Only active sparse records already
    /// present in that column are removed; missing columns are successful
    /// no-ops that do not dirty the materialized session and clear prior public
    /// edit diagnostics. Invalid column numbers are mutation failures and
    /// update WorkbookEditor::last_edit_error().
    ///
    /// Dirty save_as() omits erased sparse records from projected sheetData and
    /// may shrink the worksheet dimension. This is not column deletion, column
    /// shifting, column metadata editing, dense range deletion, tombstone
    /// output, table/range metadata recalculation, relationship repair, or a
    /// large-file low-memory random-editing path.
    void erase_column(std::uint32_t column);

    /// Removes represented sparse cells from an inclusive column range.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The
    /// column-range bounds are 1-based Excel column numbers and must satisfy
    /// `first_column <= last_column`. Only active sparse records already
    /// present in those columns are removed; missing-only ranges are successful
    /// no-ops that do not dirty the materialized session and clear prior public
    /// edit diagnostics. Invalid or reversed ranges reject before mutating the
    /// sparse store and update WorkbookEditor::last_edit_error().
    ///
    /// Dirty save_as() omits erased sparse records from projected sheetData and
    /// may shrink the worksheet dimension. This is not column deletion, column
    /// shifting, column metadata editing, dense range deletion, tombstone
    /// output, table/range metadata recalculation, relationship repair, or a
    /// large-file low-memory random-editing path.
    void erase_columns(std::uint32_t first_column, std::uint32_t last_column);

    /// Inserts sparse rows by shifting represented cells downward.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The
    /// insertion point is a 1-based Excel row number and `row_count` is a count,
    /// not an inclusive end row. `row_count == 0` is a successful no-op after
    /// validating `first_row`; it clears prior public edit diagnostics and does
    /// not dirty the session. For positive counts, every represented sparse
    /// cell with `row >= first_row` is moved down by `row_count`. If no
    /// represented cell is at or below the insertion point, the call is a
    /// successful no-op because this slice has no row metadata model.
    ///
    /// The shift is preflighted as one sparse-store coordinate transform.
    /// Invalid rows, counts outside the Excel row range, or any shifted record
    /// moving past row 1,048,576 reject before the active sparse store is
    /// mutated. Existing CellValue payloads and materialized source StyleId
    /// handles move with their sparse records.
    ///
    /// Dirty save_as() projects the shifted sheetData and refreshed sparse
    /// dimension, and translates relative references in moved formula cells,
    /// while stationary formula cells in the materialized store use the narrow
    /// structural rewriter for references at or after the insertion point. The
    /// structural rewriter preserves `$` markers as output anchors but still
    /// moves affected row references. It does not update tables, autoFilter,
    /// mergeCells, data validations, conditional formatting, hyperlinks,
    /// drawings/charts/VBA, defined names, relationships,
    /// sharedStrings/styles metadata, or calcChain beyond the existing
    /// worksheet rewrite policy.
    /// This is not a complete Excel row-insert operation and not a large-file
    /// low-memory random-editing path.
    void insert_rows(std::uint32_t first_row, std::uint32_t row_count);

    /// Deletes sparse rows by removing represented cells and shifting later cells upward.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The first
    /// argument is a 1-based Excel row number and `row_count` is a count, not an
    /// inclusive end row. `row_count == 0` is a successful no-op after
    /// validating `first_row`; it clears prior public edit diagnostics and does
    /// not dirty the session. For positive counts, represented cells in
    /// `[first_row, first_row + row_count - 1]` are removed and represented
    /// cells below that range move up by `row_count`.
    ///
    /// The whole transform is preflighted. Invalid rows or counts outside the
    /// Excel row range reject before mutation. Cell values and materialized
    /// source StyleId handles move with shifted records.
    ///
    /// Dirty save_as() projects the shifted sheetData and refreshed sparse
    /// dimension only. It translates relative references in moved formula
    /// cells, while stationary formula cells in the materialized store use the
    /// narrow structural rewriter for references affected by the deleted rows.
    /// The structural rewriter preserves `$` markers on surviving references
    /// and emits `#REF!` for deleted references, but it does not recalculate or
    /// repair range metadata, tables, drawings/charts/VBA, relationships,
    /// sharedStrings/styles, or calcChain.
    /// This is not a complete Excel row deletion operation and not a large-file
    /// low-memory random-editing path.
    void delete_rows(std::uint32_t first_row, std::uint32_t row_count);

    /// Inserts sparse columns by shifting represented cells rightward.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The
    /// insertion point is a 1-based Excel column number and `column_count` is a
    /// count. `column_count == 0` is a successful no-op after validating
    /// `first_column`; it clears prior public edit diagnostics and does not
    /// dirty the session. For positive counts, every represented sparse cell with
    /// `column >= first_column` is moved right by `column_count`. If no
    /// represented cell is at or right of the insertion point, the call is a
    /// successful no-op because this slice has no column metadata model.
    ///
    /// The shift is preflighted as one sparse-store coordinate transform.
    /// Invalid columns, counts outside the Excel column range, or any shifted
    /// record moving past column 16,384 (`XFD`) reject before mutation. Existing
    /// CellValue payloads and materialized source StyleId handles move with
    /// their sparse records.
    ///
    /// Dirty save_as() projects the shifted sheetData and refreshed sparse
    /// dimension, and translates relative references in moved formula cells,
    /// while stationary formula cells in the materialized store use the narrow
    /// structural rewriter for references at or after the insertion point. The
    /// structural rewriter preserves `$` markers as output anchors but still
    /// moves affected column references. It does not update tables, autoFilter,
    /// mergeCells, data validations, conditional formatting, hyperlinks,
    /// drawings/charts/VBA, defined names, relationships,
    /// sharedStrings/styles metadata, or calcChain beyond the existing
    /// worksheet rewrite policy.
    /// This is not a complete Excel column-insert operation and not a
    /// large-file low-memory random-editing path.
    void insert_columns(std::uint32_t first_column, std::uint32_t column_count);

    /// Deletes sparse columns by removing represented cells and shifting later cells leftward.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The first
    /// argument is a 1-based Excel column number and `column_count` is a count.
    /// `column_count == 0` is a successful no-op after validating
    /// `first_column`; it clears prior public edit diagnostics and does not
    /// dirty the session. For positive counts, represented cells in
    /// `[first_column, first_column + column_count - 1]` are removed and
    /// represented cells to the right move left by `column_count`.
    ///
    /// The whole transform is preflighted. Invalid columns or counts outside
    /// the Excel column range reject before mutation. Cell values and
    /// materialized source StyleId handles move with shifted records.
    ///
    /// Dirty save_as() projects the shifted sheetData and refreshed sparse
    /// dimension only. It translates relative references in moved formula
    /// cells, while stationary formula cells in the materialized store use the
    /// narrow structural rewriter for references affected by the deleted
    /// columns. The structural rewriter preserves `$` markers on surviving
    /// references and emits `#REF!` for deleted references, but it does not
    /// recalculate or repair range metadata, tables, drawings/charts/VBA,
    /// relationships, sharedStrings/styles, or calcChain.
    /// This is not a complete Excel column deletion operation and not a
    /// large-file low-memory random-editing path.
    void delete_columns(std::uint32_t first_column, std::uint32_t column_count);

    /// Copies represented sparse cells to another location in this worksheet.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. `source` is
    /// a 1-based inclusive range and `destination` names the top-left cell of a
    /// same-sized target footprint. Only sparse records represented inside the
    /// source range are copied; source gaps do not erase existing target cells.
    /// Overlapping copies read from a stable pre-edit snapshot. Copying to the
    /// source top-left cell, or copying a source range with no represented
    /// records, is a successful no-op that does not dirty the session.
    ///
    /// Cell values and materialized workbook-local StyleId handles are copied
    /// with each represented record. Formula text uses the existing narrow
    /// A1-style translator with the source-to-destination row/column delta;
    /// relative references move, absolute anchors remain fixed, and references
    /// translated outside Excel limits become `#REF!`. Formulas are not
    /// evaluated and cached values are not generated.
    ///
    /// Source/destination validation, target-footprint bounds, max_cells, and
    /// memory_budget_bytes are preflighted before active sparse state is
    /// replaced. A rejected copy leaves cells, dirty state, pending/unsaved
    /// diagnostics, and save retry behavior unchanged apart from updating
    /// WorkbookEditor::last_edit_error(). Temporary staging memory grows with
    /// the current sparse store.
    ///
    /// This is sparse overlay copy, not complete Excel copy/paste. It does not
    /// copy or synchronize row/column metadata, merged cells, tables, filters,
    /// validations, conditional formatting, hyperlinks, drawings/charts/VBA,
    /// defined names, relationships, sharedStrings/styles metadata, or
    /// calcChain, and it is not a large-file low-memory random-editing path.
    void copy_cells(CellRange source, WorksheetCellReference destination);

    /// Copies represented sparse cells using strict uppercase A1 references.
    ///
    /// `source_range_reference` accepts one cell (`A1`) or one rectangular
    /// range (`A1:C3`). `destination_cell_reference` names the target top-left
    /// cell. Parsing, sparse overlay, formula translation, style preservation,
    /// guardrails, diagnostics, and non-goals match the CellRange overload.
    void copy_cells(std::string_view source_range_reference,
        std::string_view destination_cell_reference);

    /// Replaces one sparse-store cell value while preserving its current style.
    ///
    /// This is the safe existing-workbook style boundary for value-only edits:
    /// if the target coordinate already has a materialized non-default source
    /// StyleId, the replacement value keeps that same workbook-local handle.
    /// Missing target cells are inserted without a style. Explicit default
    /// StyleId{0} handles are accepted as no caller style: they do not override
    /// an existing source style and do not serialize as `s="0"` on missing
    /// targets. Caller-supplied non-default StyleId handles are still rejected
    /// because this method does not migrate, merge, validate, or create styles.
    /// Use set_cell() when the intended operation is a full cell replacement
    /// that drops any prior style.
    void set_cell_value(
        std::uint32_t row, std::uint32_t column, const CellValue& value);

    /// Applies sparse value-only replacements as one preflighted batch.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. Each update
    /// names one explicit 1-based row/column coordinate. The input order is
    /// respected and duplicate coordinates are allowed; later updates win after
    /// the whole batch passes validation. Existing target cells keep their
    /// current materialized source StyleId, including when an earlier update in
    /// the same batch preserved that handle. Missing target cells are inserted
    /// without a style. Explicit default StyleId{0} handles are accepted as no
    /// caller style and follow the same preserve-existing / insert-unstyled
    /// behavior. Empty input is a successful no-op that does not dirty the
    /// materialized session and clears prior public edit diagnostics.
    /// Invalid coordinates, caller-supplied non-default StyleId handles,
    /// max_cells violations, or memory_budget_bytes violations reject the
    /// entire batch before the active sparse store is mutated.
    ///
    /// This is a sparse value-only convenience, not a dense range writer, A1
    /// range parser, style migration/merge API, range metadata recalculation,
    /// or large-file low-memory random-editing path.
    void set_cell_values(std::span<const WorksheetCellUpdate> cells);

    /// Applies sparse value-only replacements from a small literal batch.
    ///
    /// This convenience overload consumes the initializer-list synchronously and
    /// delegates to the std::span overload, so style-preserving value semantics,
    /// duplicate-coordinate ordering, empty-batch no-op behavior, guardrails,
    /// diagnostics, and non-goals are identical.
    void set_cell_values(std::initializer_list<WorksheetCellUpdate> cells);

    /// Applies a value-only prefix write to one sparse row.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The row is
    /// a 1-based Excel row number. Values are written to columns 1..N in input
    /// order. Existing target cells keep their current materialized source
    /// StyleId; missing target cells are inserted without a style. Explicit
    /// default StyleId{0} handles are accepted as no caller style and follow the
    /// same preserve-existing / insert-unstyled behavior. Cells beyond the input
    /// prefix are left untouched. Empty input is a successful no-op that does
    /// not dirty the materialized session and clears prior public edit
    /// diagnostics.
    ///
    /// The entire prefix write is preflighted and staged. Invalid row numbers,
    /// more than 16,384 values, caller-supplied non-default StyleId handles,
    /// max_cells violations, or memory_budget_bytes violations reject the call
    /// before the active sparse store is mutated. Explicit CellValue::blank()
    /// values are represented as blank cells and preserve an existing target
    /// style when one is present.
    ///
    /// This is a value-only prefix write, not row replacement, row
    /// insertion/deletion, row shifting, dense range writing, style
    /// migration/merge/creation, range metadata recalculation, sharedStrings
    /// migration, or a large-file low-memory random-editing path.
    void set_row_values(std::uint32_t row, std::span<const CellValue> values);

    /// Applies a value-only prefix write to one sparse row from a small literal list.
    ///
    /// This convenience overload consumes the initializer-list synchronously and
    /// delegates to the std::span overload, so style-preserving value semantics,
    /// empty-input no-op behavior, guardrails, diagnostics, and non-goals are
    /// identical.
    void set_row_values(std::uint32_t row, std::initializer_list<CellValue> values);

    /// Applies a value-only prefix write to one sparse column.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The column
    /// is a 1-based Excel column number. Values are written to rows 1..N in
    /// input order. Existing target cells keep their current materialized
    /// source StyleId; missing target cells are inserted without a style.
    /// Explicit default StyleId{0} handles are accepted as no caller style and
    /// follow the same preserve-existing / insert-unstyled behavior. Cells
    /// beyond the input prefix are left untouched. Empty input is a successful
    /// no-op that does not dirty the materialized session and clears prior
    /// public edit diagnostics.
    ///
    /// The entire prefix write is preflighted and staged. Invalid column
    /// numbers, more than 1,048,576 values, caller-supplied non-default StyleId
    /// handles, max_cells violations, or memory_budget_bytes violations reject
    /// the call before the active sparse store is mutated. Explicit
    /// CellValue::blank() values are represented as blank cells and preserve an
    /// existing target style when one is present.
    ///
    /// This is a value-only prefix write, not column replacement, column
    /// insertion/deletion, column shifting, dense range writing, style
    /// migration/merge/creation, range metadata recalculation, sharedStrings
    /// migration, or a large-file low-memory random-editing path.
    void set_column_values(std::uint32_t column, std::span<const CellValue> values);

    /// Applies a value-only prefix write to one sparse column from a small literal list.
    ///
    /// This convenience overload consumes the initializer-list synchronously and
    /// delegates to the std::span overload, so style-preserving value semantics,
    /// empty-input no-op behavior, guardrails, diagnostics, and non-goals are
    /// identical.
    void set_column_values(std::uint32_t column, std::initializer_list<CellValue> values);

    /// Sets or replaces one sparse-store cell value by strict uppercase A1
    /// reference.
    ///
    /// The reference must name exactly one cell and may be the last legal Excel
    /// cell, `XFD1048576`. Invalid references are treated as mutation failures:
    /// they do not mutate the sparse store and update the owning
    /// WorkbookEditor::last_edit_error(). They also do not dirty a saved
    /// materialized session. Passing CellValue::blank() creates an explicit
    /// blank record that dirty save_as() projects as an empty cell, and new
    /// blank records follow the same sparse-store max_cells and
    /// memory_budget_bytes guardrails as the row/column overload. Supported
    /// number, boolean, and formula values use the current sparse-store
    /// projection; formulas are not evaluated and do not generate cached
    /// results. Explicit default StyleId{0} handles are normalized to no style
    /// handle. Non-default StyleId handles follow the row/column overload: they
    /// are rejected before the sparse store is mutated or dirtied.
    void set_cell(std::string_view cell_reference, const CellValue& value);

    /// Replaces one sparse-store cell value by strict uppercase A1 reference
    /// while preserving the target cell's current source style handle.
    ///
    /// The reference parsing, coordinate guardrails, missing-cell insertion
    /// behavior, explicit default StyleId{0} acceptance, and non-default
    /// caller-supplied StyleId rejection follow the row/column set_cell_value()
    /// overload. This remains a value-only convenience, not a public style
    /// migration or style editing API.
    void set_cell_value(std::string_view cell_reference, const CellValue& value);

    /// Clears the value of one represented cell while preserving its style.
    ///
    /// Existing cells are converted to explicit CellValue::blank() records.
    /// If the existing materialized cell has a non-default source StyleId, the
    /// blank keeps that workbook-local handle and dirty save_as() projects an
    /// empty styled `<c>` cell. Existing unstyled cells become unstyled explicit
    /// blanks. A missing target cell is a successful no-op: it does not insert a
    /// blank, does not dirty the session, and clears prior public edit
    /// diagnostics. Invalid coordinates are mutation failures and update the
    /// owning WorkbookEditor::last_edit_error(). This is not erase_cell(), does
    /// not create tombstones, and does not migrate, merge, validate, or create
    /// styles.
    void clear_cell_value(std::uint32_t row, std::uint32_t column);

    /// Clears the value of one represented cell by strict uppercase A1 reference
    /// while preserving the target cell's current source style handle.
    ///
    /// The reference parsing, coordinate guardrails, missing-cell no-op
    /// behavior, and non-tombstone explicit-blank semantics follow the
    /// row/column clear_cell_value() overload.
    void clear_cell_value(std::string_view cell_reference);

    /// Clears every represented sparse cell value while preserving styles.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. Every
    /// active sparse record currently represented by the materialized store is
    /// converted to an explicit blank. Existing source StyleId handles are
    /// preserved per cell. An empty materialized store is a successful no-op
    /// that does not dirty the session and clears prior public edit diagnostics.
    ///
    /// Dirty save_as() keeps represented coordinates as blank `<c>` cells and
    /// may keep the worksheet dimension expanded to those coordinates. This is
    /// not worksheet deletion, sheetData removal, dense range editing,
    /// tombstone output, table/range metadata recalculation,
    /// style migration/merge/creation, or a large-file low-memory random-editing
    /// path.
    void clear_cell_values();

    /// Clears represented values from one sparse row while preserving styles.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The row is
    /// a 1-based Excel row number. Only active sparse records already present
    /// in that row are converted to explicit blanks; missing rows are
    /// successful no-ops that do not dirty the materialized session and clear
    /// prior public edit diagnostics. Existing source StyleId handles are
    /// preserved per represented cell. Invalid row numbers are mutation
    /// failures and update WorkbookEditor::last_edit_error().
    ///
    /// Dirty save_as() keeps the represented coordinates as blank `<c>` cells
    /// and may keep the worksheet dimension expanded to those coordinates.
    /// This is not row deletion, row shifting, row metadata editing, dense
    /// range editing, tombstone output, table/range metadata recalculation,
    /// style migration/merge/creation, or a large-file low-memory
    /// random-editing path.
    void clear_row(std::uint32_t row);

    /// Clears represented values from an inclusive sparse row range while preserving styles.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The
    /// row-range bounds are 1-based Excel row numbers and must satisfy
    /// `first_row <= last_row`. Only active sparse records already present in
    /// those rows are converted to explicit blanks; missing-only ranges are
    /// successful no-ops that do not dirty the materialized session and clear
    /// prior public edit diagnostics. Existing source StyleId handles are
    /// preserved per represented cell. Invalid or reversed ranges reject before
    /// mutating the sparse store and update WorkbookEditor::last_edit_error().
    ///
    /// Dirty save_as() keeps represented coordinates as blank `<c>` cells and
    /// may keep the worksheet dimension expanded to those coordinates. This is
    /// not row deletion, row shifting, row metadata editing, dense range
    /// editing, tombstone output, table/range metadata recalculation, style
    /// migration/merge/creation, or a large-file low-memory random-editing
    /// path.
    void clear_rows(std::uint32_t first_row, std::uint32_t last_row);

    /// Clears represented values from one sparse column while preserving styles.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The column
    /// is a 1-based Excel column number. Only active sparse records already
    /// present in that column are converted to explicit blanks; missing columns
    /// are successful no-ops that do not dirty the materialized session and
    /// clear prior public edit diagnostics. Existing source StyleId handles are
    /// preserved per represented cell. Invalid column numbers are mutation
    /// failures and update WorkbookEditor::last_edit_error().
    ///
    /// Dirty save_as() keeps represented coordinates as blank `<c>` cells and
    /// may keep the worksheet dimension expanded to those coordinates. This is
    /// not column deletion, column shifting, column metadata editing, dense
    /// range editing, tombstone output, table/range metadata recalculation,
    /// style migration/merge/creation, or a large-file low-memory
    /// random-editing path.
    void clear_column(std::uint32_t column);

    /// Clears represented values from an inclusive sparse column range while preserving styles.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The
    /// column-range bounds are 1-based Excel column numbers and must satisfy
    /// `first_column <= last_column`. Only active sparse records already
    /// present in those columns are converted to explicit blanks; missing-only
    /// ranges are successful no-ops that do not dirty the materialized session
    /// and clear prior public edit diagnostics. Existing source StyleId handles
    /// are preserved per represented cell. Invalid or reversed ranges reject
    /// before mutating the sparse store and update
    /// WorkbookEditor::last_edit_error().
    ///
    /// Dirty save_as() keeps represented coordinates as blank `<c>` cells and
    /// may keep the worksheet dimension expanded to those coordinates. This is
    /// not column deletion, column shifting, column metadata editing, dense
    /// range editing, tombstone output, table/range metadata recalculation,
    /// style migration/merge/creation, or a large-file low-memory random-editing
    /// path.
    void clear_columns(std::uint32_t first_column, std::uint32_t last_column);

    /// Clears represented cell values inside a rectangular range while preserving styles.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The
    /// CellRange is 1-based and inclusive, and is validated against Excel
    /// worksheet limits. Only active sparse records already present in the
    /// materialized store are converted to explicit blanks; missing cells inside
    /// the range are not synthesized, and a range with no active cells is a
    /// successful no-op that does not dirty the session. Existing non-default
    /// source style handles are preserved per cell, so dirty save_as() projects
    /// empty styled `<c>` cells only for cells that were already represented.
    /// Invalid ranges are mutation failures and update
    /// WorkbookEditor::last_edit_error(). This is not dense range editing,
    /// erase/tombstone semantics, range metadata recalculation, style
    /// migration/merge/creation.
    void clear_cell_values(CellRange range);

    /// Clears represented cell values inside a strict uppercase A1 range.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The range
    /// reference accepts one cell (`A1`) or one inclusive rectangular range
    /// (`A1:C3`) using uppercase A1 syntax. It delegates to the CellRange
    /// overload after parsing, so only active sparse records already present in
    /// the materialized store are converted to explicit blanks; missing cells
    /// inside the range are not synthesized. Invalid references are mutation
    /// failures and update WorkbookEditor::last_edit_error().
    ///
    /// This is not dense range editing, sheet-qualified reference parsing,
    /// multi-area ranges, whole-row/whole-column references, absolute `$A$1`
    /// parsing, erase/tombstone semantics, range metadata recalculation, style
    /// migration/merge/creation, or a large-file low-memory random-editing
    /// path.
    void clear_cell_values(std::string_view range_reference);

    /// Clears represented cell values at explicit sparse coordinates.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. Each
    /// coordinate is 1-based and validated against Excel worksheet limits. The
    /// input order is respected, duplicate coordinates are allowed, and only
    /// active sparse records already present in the materialized store are
    /// converted to explicit blanks; missing coordinates are no-ops and are not
    /// synthesized. Existing non-default source style handles are preserved per
    /// cell, so dirty save_as() projects empty styled `<c>` cells only for
    /// cells that were already represented. Empty input, or input containing no
    /// represented cells, is a successful no-op that does not dirty the session.
    /// Invalid coordinates reject the entire batch before the active sparse
    /// store is mutated and update WorkbookEditor::last_edit_error().
    ///
    /// This is not dense range editing, erase/tombstone semantics, range
    /// metadata recalculation, style migration/merge/creation, or an A1 range
    /// parser.
    void clear_cell_values(std::span<const WorksheetCellReference> cells);

    /// Clears represented sparse coordinates from a small literal batch.
    ///
    /// This convenience overload consumes the initializer-list synchronously and
    /// delegates to the std::span overload, so coordinate preflight,
    /// missing-cell no-op behavior, style preservation, diagnostics, and
    /// non-goals are identical.
    void clear_cell_values(std::initializer_list<WorksheetCellReference> cells);

    /// Removes every represented sparse cell record.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. All active
    /// sparse records currently represented by the materialized store are
    /// removed. An empty materialized store is a successful no-op that does not
    /// dirty the session and clears prior public edit diagnostics.
    ///
    /// Dirty save_as() omits erased sparse records from projected sheetData and
    /// produces an empty sparse-store worksheet dimension (`A1`) when no records
    /// remain. This is not worksheet deletion, sheetData part removal, row or
    /// column shifting, tombstone output, table/range metadata recalculation,
    /// relationship repair, or a large-file low-memory random-editing path.
    void erase_cells();

    /// Removes sparse-store cell records inside a rectangular range.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The
    /// CellRange is 1-based and inclusive, and is validated against Excel
    /// worksheet limits. Only active sparse records already present in the
    /// materialized store are removed; missing cells inside the range are not
    /// synthesized, and a range with no active cells is a successful no-op that
    /// does not dirty the session. Invalid ranges are mutation failures and
    /// update WorkbookEditor::last_edit_error().
    ///
    /// Dirty save_as() omits erased sparse records from projected sheetData and
    /// may shrink the worksheet dimension. This is not dense range deletion,
    /// row/column delete, tombstone output, range metadata recalculation,
    /// relationship repair, or a large-file low-memory random-editing path.
    void erase_cells(CellRange range);

    /// Removes sparse-store cell records inside a strict uppercase A1 range.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. The range
    /// reference accepts one cell (`A1`) or one inclusive rectangular range
    /// (`A1:C3`) using uppercase A1 syntax. It delegates to the CellRange
    /// overload after parsing, so only active sparse records already present in
    /// the materialized store are removed; missing cells inside the range are
    /// not synthesized and are not represented by tombstones. Invalid
    /// references are mutation failures and update
    /// WorkbookEditor::last_edit_error().
    ///
    /// This is not dense range deletion, sheet-qualified reference parsing,
    /// multi-area ranges, whole-row/whole-column references, absolute `$A$1`
    /// parsing, row/column delete, range metadata recalculation, relationship
    /// repair, or a large-file low-memory random-editing path.
    void erase_cells(std::string_view range_reference);

    /// Removes sparse-store cell records at explicit sparse coordinates.
    ///
    /// API mode: In-memory / existing-workbook small-file mutation. Each
    /// coordinate is 1-based and validated against Excel worksheet limits. The
    /// input order is respected and duplicate coordinates are allowed; after an
    /// earlier duplicate removes a record, later duplicates are no-ops. Missing
    /// coordinates are successful no-ops and are not represented by tombstones.
    /// Empty input, or input containing no represented cells, does not dirty the
    /// session and clears prior public edit diagnostics. Invalid coordinates
    /// reject the entire batch before the active sparse store is mutated and
    /// update WorkbookEditor::last_edit_error().
    ///
    /// Dirty save_as() omits erased sparse records from projected sheetData and
    /// may shrink the worksheet dimension. This is not dense range deletion,
    /// row/column delete, tombstone output, range metadata recalculation,
    /// relationship repair, or a large-file low-memory random-editing path.
    void erase_cells(std::span<const WorksheetCellReference> cells);

    /// Removes sparse-store cell records from a small literal coordinate batch.
    ///
    /// This convenience overload consumes the initializer-list synchronously and
    /// delegates to the std::span overload, so coordinate preflight, duplicate
    /// and missing-cell no-op behavior, diagnostics, and non-goals are
    /// identical.
    void erase_cells(std::initializer_list<WorksheetCellReference> cells);

    /// Removes one sparse-store cell record.
    ///
    /// Row and column are 1-based Excel coordinates. Invalid coordinates are
    /// mutation failures: they do not mutate the sparse store and update the
    /// owning WorkbookEditor::last_edit_error(). They also do not dirty a saved
    /// materialized session. Erasing a missing cell is a successful no-op: it
    /// does not dirty the session and clears prior public edit diagnostics.
    /// Erasing an existing sparse record removes it from dirty save_as()
    /// projection rather than writing a tombstone, and edge erases can shrink
    /// the projected worksheet dimension.
    void erase_cell(std::uint32_t row, std::uint32_t column);

    /// Removes one sparse-store cell record by strict uppercase A1 reference.
    ///
    /// The reference must name exactly one cell and may be `XFD1048576`.
    /// Invalid references are treated as mutation failures: they do not mutate
    /// the sparse store, do not dirty a saved materialized session, and update
    /// the owning WorkbookEditor::last_edit_error(). A valid reference to a
    /// missing cell is a successful no-op: it does not dirty the session and
    /// clears prior public edit diagnostics. Erasing an existing sparse record
    /// removes it from dirty save_as() projection rather than writing a
    /// tombstone, and edge erases can shrink the projected worksheet dimension.
    void erase_cell(std::string_view cell_reference);

    /// Returns whether this materialized worksheet session has dirty sparse
    /// cell edits waiting for WorkbookEditor::save_as() auto-flush.
    ///
    /// API mode: In-memory / existing-workbook small-file inspection. This is a
    /// worksheet-local dirty-state probe only: it does not flush the session,
    /// does not increment WorkbookEditor::pending_change_count(), does not
    /// expose Patch EditPlan state, and does not update
    /// WorkbookEditor::last_edit_error().
    [[nodiscard]] bool has_pending_changes() const;

    /// Returns the number of active sparse cell records in this materialized
    /// worksheet, including explicit blank records.
    ///
    /// This read-only inspection does not flush or reload the materialized
    /// session and does not update WorkbookEditor::last_edit_error().
    [[nodiscard]] std::size_t cell_count() const;

    /// Returns the minimal sparse-store bounding range for represented cells.
    ///
    /// API mode: In-memory / existing-workbook small-file inspection. Empty
    /// materialized stores return std::nullopt. Non-empty stores return a
    /// 1-based inclusive CellRange covering every active sparse record,
    /// including explicit blank records. This is a sparse-state query, not a
    /// worksheet `<dimension>` metadata read/repair, dense range read,
    /// iterator, metadata recalculation, or large-file low-memory random access
    /// API. It does not mutate dirty state, update
    /// WorkbookEditor::last_edit_error(), flush, or reload the materialized
    /// session.
    [[nodiscard]] std::optional<CellRange> used_range() const;

    /// Returns an owning row-major snapshot of all active sparse cell records.
    ///
    /// API mode: In-memory / existing-workbook small-file inspection. This
    /// copies coordinates and CellValue payloads out of the materialized sparse
    /// store, including explicit blank records. It does not expose iterators or
    /// references into the WorkbookEditor session, does not mutate dirty state,
    /// does not update WorkbookEditor::last_edit_error(), and does not add range
    /// iteration, metadata synchronization, or large-file low-memory random
    /// access semantics, and does not flush or reload the materialized session.
    [[nodiscard]] std::vector<WorksheetCellSnapshot> sparse_cells() const;

    /// Returns an owning row-major snapshot of active sparse records in one row.
    ///
    /// API mode: In-memory / existing-workbook small-file inspection. The row
    /// is a 1-based Excel row number. Only active sparse records currently
    /// present in that row are returned; missing cells are not synthesized as
    /// blanks. The result is an owning snapshot, not an iterator or borrowed
    /// reference into the WorkbookEditor session. This is a sparse row
    /// inspection convenience over sparse_cells(CellRange), not dense row read,
    /// row metadata inspection, row iterator, metadata recalculation, or
    /// large-file low-memory random access. Invalid row coordinates throw
    /// FastXlsxError as read failures, but still do not mutate dirty state,
    /// update WorkbookEditor::last_edit_error(), flush, or reload the
    /// materialized session.
    [[nodiscard]] std::vector<WorksheetCellSnapshot> row_cells(std::uint32_t row) const;

    /// Returns an owning row-major snapshot of active sparse records in one column.
    ///
    /// API mode: In-memory / existing-workbook small-file inspection. The
    /// column is a 1-based Excel column number. Only active sparse records
    /// currently present in that column are returned; missing cells are not
    /// synthesized as blanks. The result is ordered by row and is an owning
    /// snapshot, not an iterator or borrowed reference into the WorkbookEditor
    /// session. This is a sparse column inspection convenience over
    /// sparse_cells(CellRange), not dense column read, column metadata
    /// inspection, column iterator, metadata recalculation, or large-file
    /// low-memory random access. Invalid column coordinates throw FastXlsxError
    /// as read failures, but still do not mutate dirty state, update
    /// WorkbookEditor::last_edit_error(), flush, or reload the materialized
    /// session.
    [[nodiscard]] std::vector<WorksheetCellSnapshot> column_cells(
        std::uint32_t column) const;

    /// Returns an owning row-major snapshot of active sparse records inside a
    /// strict uppercase A1 range reference.
    ///
    /// API mode: In-memory / existing-workbook small-file inspection. The
    /// reference must name one cell such as `A1` or one rectangular range such
    /// as `A1:C3`; a single cell is treated as a one-cell range. Lowercase
    /// references, sheet-qualified references, absolute references, whole-row
    /// or whole-column references, reversed ranges, multi-area references,
    /// leading-zero rows, and coordinates outside Excel limits throw
    /// FastXlsxError. Only active sparse records currently present in the
    /// parsed range are returned; missing cells are not synthesized as blanks.
    /// This is a parsing convenience over sparse_cells(CellRange), not a dense
    /// range read, range iterator, range metadata inspection, or large-file
    /// low-memory random access. It does not mutate dirty state, update
    /// WorkbookEditor::last_edit_error(), flush, or reload the materialized
    /// session.
    [[nodiscard]] std::vector<WorksheetCellSnapshot> sparse_cells(
        std::string_view range_reference) const;

    /// Returns owning snapshots for an explicit sparse coordinate batch.
    ///
    /// API mode: In-memory / existing-workbook small-file inspection. Each
    /// coordinate is 1-based and validated against Excel worksheet limits. The
    /// input order is respected, duplicate coordinates are allowed, and only
    /// active sparse records currently present in the materialized store are
    /// returned. Missing coordinates are skipped and are not synthesized as
    /// blank snapshots. This is a convenience over sparse_cells(CellRange),
    /// not a dense batch read, iterator API, metadata inspection, or
    /// large-file low-memory random access path. It does not mutate dirty state,
    /// update WorkbookEditor::last_edit_error(), flush, or reload the
    /// materialized session.
    [[nodiscard]] std::vector<WorksheetCellSnapshot> sparse_cells(
        std::span<const WorksheetCellReference> cells) const;

    /// Returns owning snapshots for a small literal sparse coordinate batch.
    ///
    /// This convenience overload consumes the initializer-list synchronously and
    /// delegates to the std::span overload, so coordinate preflight, duplicate
    /// handling, missing-cell skipping, diagnostics, and non-goals are
    /// identical.
    [[nodiscard]] std::vector<WorksheetCellSnapshot> sparse_cells(
        std::initializer_list<WorksheetCellReference> cells) const;

    /// Returns an owning row-major snapshot of active sparse records inside a
    /// rectangular range.
    ///
    /// API mode: In-memory / existing-workbook small-file inspection. The
    /// CellRange is 1-based and inclusive, and is validated against Excel
    /// worksheet limits. Only active sparse records currently present in the
    /// materialized store are returned; missing cells inside the range are not
    /// synthesized as blanks. This is a filtered snapshot convenience, not a
    /// dense matrix read, streaming iterator, metadata recalculation, or
    /// large-file low-memory random access API. It does not mutate dirty state
    /// and invalid range failures do not dirty saved sessions. It does not
    /// update WorkbookEditor::last_edit_error(), flush, or reload the
    /// materialized session.
    [[nodiscard]] std::vector<WorksheetCellSnapshot> sparse_cells(CellRange range) const;

    /// Returns the current CellStore memory estimate for this materialized
    /// worksheet. This is not process RSS and excludes package/write buffers.
    ///
    /// This read-only inspection does not flush or reload the materialized
    /// session and does not update WorkbookEditor::last_edit_error().
    [[nodiscard]] std::size_t estimated_memory_usage() const;

private:
    friend class WorkbookEditor;

    WorksheetEditor(
        WorkbookEditor* owner, std::string planned_name, std::uint64_t owner_generation);

    [[nodiscard]] const WorkbookEditor& owner() const;
    [[nodiscard]] WorkbookEditor& owner();

    WorkbookEditor* owner_ = nullptr;
    std::string planned_name_;
    std::uint64_t owner_generation_ = 0;
};

/// Edits an existing XLSX workbook through Patch-mode worksheet operations,
/// then writes a new package.
///
/// API mode: Patch. WorkbookEditor opens an existing OpenXML package and exposes
/// a narrow, workbook-language editing surface over the internal part-level Patch
/// engine. It is the first landed public slice of the planned existing-file
/// editing facade and intentionally exposes only:
///
/// - open an existing `.xlsx` by path,
/// - inspect worksheet names,
/// - replace the entire `<sheetData>` of an existing worksheet, addressed by
///   sheet name, from caller-supplied CellValue rows,
/// - replace a bounded set of already-existing cells in a worksheet through the
///   file-backed worksheet transformer without materializing the source
///   worksheet into WorkbookEditor memory,
/// - rename a worksheet's sheet-catalog name (the `<sheets><sheet name="...">`
///   attribute written into the saved package),
/// - inspect sheet-qualified formula references in already-materialized
///   worksheets as a conservative dependency-risk diagnostic,
/// - write the edited workbook to a new path with save_as().
///
/// What this facade does for a replaced sheet:
///
/// - It performs a bounded local rewrite that replaces only the worksheet's
///   `<sheetData>` element while preserving the surrounding worksheet XML
///   (sheet properties, dimensions, views, columns, filters, merged ranges,
///   data validations, conditional formatting, hyperlinks, drawings, and other
///   worksheet metadata are kept as-is, not repaired).
/// - It preserves unknown and unmodified package parts byte-for-byte where the
///   underlying Patch path allows, including `xl/sharedStrings.xml`,
///   `xl/styles.xml`, relationships, content types, and docProps.
/// - It requests workbook recalculation on load (`fullCalcOnLoad`) and removes a
///   stale `xl/calcChain.xml` when present; it never invents a calcChain when the
///   source workbook has none.
///
/// What this facade does for targeted cell replacement/upsert:
///
/// - replace_cells() scans the source or current planned worksheet XML through
///   the worksheet event reader / transformer, replaces matching existing `<c>`
///   elements, refreshes the top-level worksheet dimension from emitted cell
///   references, and stages the rewritten worksheet as a file-backed
///   package-entry chunk. Its default missing-cell policy is strict Fail.
/// - With CellPatchMissingCellPolicy::Insert, replace_cells() uses the same
///   transformer path but inserts missing cells into existing rows or
///   synthesized minimal rows. Both modes are point edits: they do not shift
///   ranges, preserve prior per-cell metadata on overwritten cells, migrate
///   sharedStrings, validate style ids, repair relationships, or recalculate
///   tables/filters/drawings/defined names.
/// - Text replacement cells are written as inline strings. Formula replacement
///   cells write formula text and follow the same fullCalcOnLoad / stale
///   calcChain cleanup policy as worksheet replacement.
/// Memory and scope: replace_sheet_data() rows are buffered in a sparse
/// CellStore and emitted as a pull-based `<sheetData>` chunk source; the
/// internal Patch helper
/// streams source/planned worksheet XML, consumes that replacement source during
/// the output rewrite, and records the rewritten worksheet as a file-backed
/// staged chunk. This is still a bounded template-fill /
/// small-to-medium editing path, not a fully low-memory large-file worksheet
/// transformer. Replacing a very large worksheet's data is rejected by the
/// underlying bounded rewrite limit rather than silently materializing an
/// unbounded worksheet. For large worksheets with a bounded set of existing
/// cells to change, use replace_cells(); choose CellPatchMissingCellPolicy::Fail
/// for strict replacement or Insert for point upsert. The transformer streams
/// the source/planned worksheet entry and only materializes the caller-provided
/// single-cell replacement payloads.
///
/// What this facade does for a renamed sheet:
///
/// - rename_sheet() rewrites only the `<sheets><sheet name="...">` attribute in
///   `xl/workbook.xml` for the saved package. It preserves worksheet parts,
///   workbook relationships, content types, and unknown entries; it does not
///   touch defined names, formulas, tables, drawings, charts, hyperlinks, or
///   relationship targets, so it is a narrow catalog-name rewrite, not a
///   semantic sheet rename. The rename affects the editor's current planned
///   catalog view and the save_as() output catalog. source_worksheet_names()
///   and has_source_worksheet() remain available for inspecting the original
///   workbook view within the open session.
///
/// This facade now also exposes the first narrow WorksheetEditor slice for
/// small existing-workbook random cell edits. That slice is explicit
/// In-memory mode: callers opt in by calling worksheet(), which materializes
/// one existing worksheet into a sparse store owned by WorkbookEditor. Dirty
/// WorksheetEditor edits are automatically flushed into the Patch plan by
/// save_as(). Source worksheet `t="s"` cells are read through the existing
/// workbook `xl/sharedStrings.xml` and materialized as `CellValue::text(...)`
/// for this small-file editor path; dirty save_as() can project text cells
/// through that same workbook sharedStrings part when it has a narrow
/// appendable `<sst>` shape, appending only new plain shared string items while
/// keeping existing indexes stable. If no source sharedStrings part exists, or
/// if the existing table is prefixed/wrong-namespace, count-inconsistent,
/// relationship-stale, malformed, or otherwise outside that narrow boundary,
/// dirty save_as() falls back to inline strings and never creates a new
/// `xl/sharedStrings.xml`. Rich text in sharedStrings is
/// flattened to plain text on import. The compact source materialization
/// boundary is maintained in docs/API_DESIGN_AND_DOCUMENTATION.md and on
/// WorksheetEditor; it remains a narrow same-workbook import/projection
/// contract, not a sharedStrings/style/relationship migration contract. Declared
/// sharedStrings `count` / `uniqueCount` values and otherwise well-formed
/// unknown sharedStrings attributes are not used to drive materialization or
/// repair. Prefixed
/// sharedStrings `sst` / `si` / `t` / `r` element names are matched by
/// local-name for materialization; this is not namespace URI validation,
/// namespace repair, or schema validation. Unsupported sharedStrings
/// item/rich-run local-names still fail fast even when their namespace URI is
/// ignored. Mixed direct `<t>` text and rich `<r>` runs, `rPr` outside a rich
/// run, and text wrappers inside `rPr` are malformed sharedStrings rich
/// metadata and fail during materialization. `rPh` / `phoneticPr` / `extLst`
/// opaque nested markup text is ignored, and self-closing ignored metadata is
/// treated as empty metadata; nested `<si>` decoys, markup nested inside a text
/// wrapper, comments / processing instructions / CDATA inside text wrappers,
/// orphan closing tags, and unclosed ignored metadata still fail fast as
/// malformed source sharedStrings. CDATA / DOCTYPE-like markup declarations are
/// not a supported sharedStrings text import path, and `<?xml ...?>` after the
/// sharedStrings root start is rejected instead of being treated as ordinary
/// processing-instruction trivia; duplicate XML declarations are rejected as
/// malformed source XML, while legal version-only declarations and
/// single-quoted declaration attributes remain accepted when they follow the
/// supported order; declarations missing a supported `version` attribute,
/// unsupported declaration versions, duplicate/unknown declaration attributes,
/// empty or invalid declaration encoding names, declaration `standalone`
/// values other than `yes` or `no`, declaration `encoding` after `standalone`,
/// and XML declarations after leading whitespace text or prolog comment /
/// ordinary PI trivia are rejected. Case-varied XML-like processing-instruction
/// targets such as `<?XML ...?>` or `<?Xml ...?>` are rejected as reserved
/// targets instead of being skipped as ordinary trivia; `<?xml-stylesheet ...?>`
/// remains ordinary PI trivia and is not imported or interpreted, while
/// malformed ordinary PI tokens missing `?>` or lacking a non-empty target are
/// rejected, as are targets starting with an obviously invalid ASCII name-start
/// character, containing an obviously invalid ASCII name continuation
/// character, or lacking whitespace / immediate `?>` after the target.
/// Ordinary targets using legal ASCII name-start characters such as letters,
/// `_`, and `:`, and continuation characters such as digits, `-`, and `.`
/// remain ignored trivia. Empty-data ordinary PIs whose target is followed
/// immediately by `?>` remain ignored trivia.
/// Prefixed source worksheet /
/// sheetData / row / cell element names and inlineStr, rich-run, formula, and
/// value-wrapper element names are likewise matched by local-name for
/// materialization; element namespace URIs are not inspected. Unsupported
/// local-names still fail fast even when their namespace URI is ignored.
/// Workbooks with no sharedStrings part still materialize supported non-shared-string cells,
/// and dirty save_as() does not create `xl/sharedStrings.xml` for them. The
/// sharedStrings relationship/table is resolved on demand only when the
/// selected worksheet actually contains `t="s"` cells; stale or malformed
/// sharedStrings relationship/content-type metadata or payloads do not block
/// supported non-shared-string cells or unrelated inline fallback projection,
/// are not repaired, and still fail if shared string indexes are encountered
/// during source materialization.
/// Source cell `ph` phonetic markers are ignored. Known formula metadata
/// attributes `t` / `ref` / `si` / `aca` / `ca` / `bx` / `dt2D` / `dtr` /
/// `del1` / `del2` / `r1` / `r2` are treated as source metadata, not
/// preserved; unknown formula attributes still fail. Formulas with text are
/// projected as plain formula text, and source-order shared
/// formula followers are projected as translated plain formula text. Unresolved
/// metadata-only shared formula cells are projected from cached scalar values
/// when available.
/// formula_reference_audits() can inspect already-materialized formula cells
/// and report sheet-qualified references plus whether those references still
/// point at a renamed source sheet. It does not scan the whole source package
/// by itself. Formula audit diagnostics do not update last_edit_error(), queue
/// replacements, dirty/create materialized sessions, or change pending edit
/// summaries. Formula text rewriting is available only through the explicit
/// rename_sheet() materialized-formula policy and remains limited to formulas
/// already loaded into WorksheetEditor sessions.
/// Failed worksheet materialization does not queue a dirty session; a later
/// no-op save_as() remains a copy-original package write unless another edit is
/// explicitly queued.
///
/// Non-goals (not implemented by this slice): adding / deleting worksheets,
/// semantic sheet rename that synchronizes defined names / formulas / tables /
/// drawings / relationship targets, caller-supplied worksheet XML input,
/// shared-string index writeback / rebuild / migration, style id migration or
/// style merge, relationship / content-type repair or pruning, rich-text
/// preservation, and large-file low-memory random editing.
class WorkbookEditor {
public:
    /// Opens an existing XLSX workbook for Patch-mode editing.
    ///
    /// The package is read through the internal package reader. Compressed input
    /// is supported only in builds configured with the opt-in minizip-ng backend;
    /// default builds read stored / no-compression packages.
    ///
    /// @param path Existing `.xlsx` package to edit.
    /// @throws FastXlsxError if the package cannot be opened or is malformed.
    [[nodiscard]] static WorkbookEditor open(const std::filesystem::path& path);

    /// Opens an existing XLSX workbook with Patch facade guardrails.
    ///
    /// The options currently apply only to replace_sheet_data() replacement
    /// payload construction. They do not load source worksheet cells into an
    /// in-memory random editor and do not change PackageReader compression
    /// support.
    ///
    /// @param path Existing `.xlsx` package to edit.
    /// @param options Replacement-payload guardrails for this editor.
    /// @throws FastXlsxError if the package cannot be opened or is malformed.
    [[nodiscard]] static WorkbookEditor open(
        const std::filesystem::path& path, WorkbookEditorOptions options);

    ~WorkbookEditor();

    /// Move-constructs an editor, transferring the opened source package,
    /// replacement-payload guardrails, planned catalog, queued public edits,
    /// pending diagnostics, and last_edit_error() state.
    ///
    /// Moving is only ownership transfer for this Patch facade. It does not
    /// save, commit, close, merge package state, or clear queued edits in the
    /// moved-to editor. The moved-from editor is left with no pending changes
    /// or last_edit_error(); inspection and edit/save operations that require
    /// an opened workbook throw FastXlsxError. Existing WorksheetEditor handles
    /// borrowed from the moved-from editor are invalidated; callers must
    /// reacquire handles from the moved-to editor.
    WorkbookEditor(WorkbookEditor&& other) noexcept;

    /// Move-assigns an editor, replacing this editor's opened source package,
    /// replacement-payload guardrails, planned catalog, queued public edits,
    /// pending diagnostics, and last_edit_error() state with the source
    /// editor's state.
    ///
    /// Existing target-side queued edits and diagnostics are discarded rather
    /// than merged or committed. Assigning from a clean opened editor therefore
    /// leaves the target with no pending changes and no last_edit_error(). Move
    /// assignment from an already moved-from editor leaves the target moved-from
    /// / not open and discards target-side queued state. Move assignment does
    /// not save, commit, close, or repair package state. The moved-from editor
    /// is left with no pending changes or last_edit_error(); inspection and
    /// edit/save operations that require an opened workbook throw FastXlsxError.
    /// Existing WorksheetEditor handles borrowed from either the source editor
    /// or the overwritten target editor are invalidated; callers must reacquire
    /// handles from the assigned-to editor.
    WorkbookEditor& operator=(WorkbookEditor&& other) noexcept;

    WorkbookEditor(const WorkbookEditor&) = delete;
    WorkbookEditor& operator=(const WorkbookEditor&) = delete;

    /// Returns the current planned worksheet names in sheet-catalog order.
    ///
    /// This is read-only sheet inspection for the public Patch facade. It
    /// reflects successful rename_sheet() calls queued in this editor, but does
    /// not expose workbook relationships or package parts. Read-only catalog
    /// queries do not materialize worksheets, flush WorksheetEditor sessions,
    /// mutate source package bytes, update last_edit_error(), or repair/rollback
    /// queued catalog edits.
    [[nodiscard]] std::vector<std::string> worksheet_names() const;

    /// Returns whether a worksheet with the given name exists in the current
    /// planned sheet catalog.
    ///
    /// This is a read-only planned-catalog query with the same no-flush and
    /// no-diagnostic-write boundary as worksheet_names().
    [[nodiscard]] bool has_worksheet(std::string_view sheet_name) const;

    /// Returns the opened source workbook's worksheet names in sheet-catalog
    /// order.
    ///
    /// This source view does not reflect successful rename_sheet() calls queued
    /// in this editor. Use worksheet_names() to inspect the current planned
    /// catalog that save_as() will write. Source catalog queries are read-only
    /// inspection of the opened source view; they do not reload materialized
    /// WorksheetEditor sessions or mutate the planned catalog.
    [[nodiscard]] std::vector<std::string> source_worksheet_names() const;

    /// Returns whether a worksheet with the given name exists in the opened
    /// source workbook sheet catalog.
    ///
    /// This is a read-only source-catalog query with the same no-reload and
    /// no-planned-catalog-mutation boundary as source_worksheet_names().
    [[nodiscard]] bool has_source_worksheet(std::string_view sheet_name) const;

    /// Returns whether this editor has queued Patch-mode changes.
    ///
    /// This is a coarse save-as diagnostic for the current public WorkbookEditor
    /// facade. It reports whether successful public edits such as
    /// replace_sheet_data() or rename_sheet() have queued work for save_as(), or
    /// whether any materialized WorksheetEditor session is currently dirty and
    /// will be flushed by save_as().
    /// It does not expose EditPlan entries, dependency audits, relationship
    /// diagnostics, output-plan reasons, or a full unsaved-change model. Failed
    /// edits leave this state unchanged. This read-only diagnostic does not
    /// flush or reload materialized WorksheetEditor sessions, mutate catalog
    /// state, or update last_edit_error(). After a successful save_as(), this
    /// can remain true for retained staged Patch handoffs even when all
    /// materialized WorksheetEditor diagnostics are clean.
    [[nodiscard]] bool has_pending_changes() const noexcept;

    /// Returns whether the editor has changes not included in its most recent
    /// successful save_as() output.
    ///
    /// Unlike has_pending_changes(), this is a save-watermark diagnostic. It is
    /// false for a newly opened editor and becomes false after a successful
    /// save_as(). A later successful Patch edit or dirty WorksheetEditor
    /// mutation makes it true again. Failed edits and failed save_as() calls do
    /// not advance or reset the watermark. Retained staged Patch state may keep
    /// has_pending_changes() true while this method is false.
    [[nodiscard]] bool has_unsaved_changes() const noexcept;

    /// Returns a coarse count of successful changes since the most recent
    /// successful save_as().
    ///
    /// This is a diagnostic watermark count, not a semantic diff or package
    /// part count. Dirty materialized sessions count once per worksheet until
    /// save_as() flushes them. Failed edits and failed save_as() calls leave the
    /// value unchanged.
    [[nodiscard]] std::size_t unsaved_change_count() const noexcept;

    /// Returns a coarse count of successful public edit calls queued in this
    /// facade.
    ///
    /// The value is useful for diagnostics and tests that need to distinguish a
    /// clean editor from one with pending save_as() work. It is not a package
    /// part count, EditPlan size, or stable semantic diff: repeated edits to the
    /// same sheet may replace earlier queued payloads while still incrementing
    /// this public facade diagnostic. This read-only diagnostic does not flush
    /// or reload materialized WorksheetEditor sessions or update
    /// last_edit_error().
    [[nodiscard]] std::size_t pending_change_count() const noexcept;

    /// Returns the number of explicit replacement cells currently represented by
    /// successful replace_sheet_data() calls.
    ///
    /// This is a narrow diagnostic for the public Patch facade. It sums the final
    /// queued replacement payload per sheet name, so a later successful
    /// replace_sheet_data() for the same sheet replaces the earlier payload in
    /// this count. A successful rename_sheet() moves the diagnostic to the
    /// planned sheet name; a failed rename_sheet() leaves it unchanged. It does
    /// not count source workbook cells, renamed sheets, preserved worksheet
    /// metadata, shared strings, styles, relationships, or any future in-memory
    /// editor state.
    [[nodiscard]] std::size_t pending_replacement_cell_count() const noexcept;

    /// Returns the number of unique targeted existing cells currently queued by
    /// successful replace_cells() calls.
    ///
    /// API mode: Patch diagnostics. This counts the final target-cell set per
    /// worksheet; duplicate coordinates in one call or later successful
    /// replace_cells() calls for the same coordinate are counted once with the
    /// latest staged payload. It does not count whole-<sheetData> replacements,
    /// dirty materialized WorksheetEditor cells, source workbook cells, or cells
    /// that were rejected because they were missing from the source/planned
    /// worksheet stream. This method does not flush or reload materialized
    /// sessions or update last_edit_error().
    [[nodiscard]] std::size_t pending_targeted_cell_replacement_count() const noexcept;

    /// Returns the planned worksheet names that currently have queued
    /// replace_sheet_data() payloads.
    ///
    /// Names are reported in the current planned sheet-catalog order. A
    /// successful rename_sheet() moves the pending replacement diagnostic to the
    /// planned new sheet name. Failed edits leave this list unchanged. This is a
    /// public facade diagnostic only; it does not expose internal EditPlan
    /// entries, source workbook cells, preserved worksheet metadata,
    /// relationships, shared strings, styles, or save-time package entries. It
    /// does not flush or reload materialized WorksheetEditor sessions or update
    /// last_edit_error().
    [[nodiscard]] std::vector<std::string> pending_replacement_worksheet_names() const;

    /// Returns planned worksheet names that currently have queued
    /// replace_cells() targeted-cell patches.
    ///
    /// Names are reported in the current planned sheet-catalog order and move
    /// with successful rename_sheet() calls. This is a public facade diagnostic;
    /// it does not expose internal EditPlan entries, source worksheet XML,
    /// relationship audits, or PackageEditor staged chunk locations.
    [[nodiscard]] std::vector<std::string>
    pending_targeted_cell_replacement_worksheet_names() const;

    /// Returns planned worksheet names for dirty materialized WorksheetEditor
    /// sessions that still need save_as() auto-flush.
    ///
    /// API mode: In-memory / existing-workbook small-file diagnostics. Names
    /// are reported in the current planned sheet-catalog order. Clean
    /// materialized sessions are omitted. Renamed sheets use the current
    /// planned name and follow the same clear-after-save / clean-reacquire
    /// lifecycle as unrenamed sheets. If a rename chain returns a sheet to its
    /// source name before materialization, later dirty materialized diagnostics
    /// use the restored source/planned name and do not keep the transient name.
    /// A successful save_as() flushes dirty sessions into the Patch plan and
    /// clears them from this list; this method does not itself flush, increment
    /// pending_change_count(), expose internal EditPlan state, include
    /// whole-<sheetData> replacement payloads, or update last_edit_error(). It
    /// returns an empty vector for a moved-from editor.
    [[nodiscard]] std::vector<std::string> pending_materialized_worksheet_names() const;

    /// Returns the total active sparse cell records in dirty materialized
    /// WorksheetEditor sessions waiting for save_as() auto-flush.
    ///
    /// API mode: In-memory / existing-workbook small-file diagnostics. This is
    /// a workbook-level aggregate over dirty materialized sessions only. Clean
    /// materialized sessions and queued whole-<sheetData> replacements are not
    /// counted. The value includes explicit blank records, matches the sum of
    /// the dirty sessions' WorksheetEditor::cell_count() values, clears after
    /// successful save_as() / clean reacquire for renamed and unrenamed
    /// sessions alike, and returns 0 for a moved-from editor. This method does
    /// not flush, increment pending_change_count(), expose internal EditPlan
    /// state, or update last_edit_error().
    [[nodiscard]] std::size_t pending_materialized_cell_count() const noexcept;

    /// Returns the total CellStore memory estimate for dirty materialized
    /// WorksheetEditor sessions waiting for save_as() auto-flush.
    ///
    /// The estimate matches the sum of dirty sessions'
    /// WorksheetEditor::estimated_memory_usage() values. It is not process RSS
    /// and excludes source package bytes, generated XML chunks, PackageEditor
    /// staging files, ZIP writer buffers, and save-time package assembly costs.
    /// Clean materialized sessions and queued whole-<sheetData> replacements
    /// are not counted. Renamed and unrenamed dirty sessions follow the same
    /// post-save clearing and later-mutation re-dirtying lifecycle. This method
    /// does not flush, increment pending_change_count(), expose internal
    /// EditPlan state, or update last_edit_error().
    [[nodiscard]] std::size_t estimated_pending_materialized_memory_usage()
        const noexcept;

    /// Returns whether the current planned worksheet name has a queued
    /// replace_sheet_data() payload.
    ///
    /// This follows the same planned-name semantics as
    /// pending_replacement_worksheet_names(). It returns false for a moved-from
    /// editor.
    [[nodiscard]] bool has_pending_replacement(std::string_view sheet_name) const noexcept;

    /// Returns whether the current planned worksheet name has queued
    /// replace_cells() targeted-cell patches.
    ///
    /// This follows current planned catalog semantics and returns false for a
    /// moved-from editor.
    [[nodiscard]] bool has_pending_targeted_cell_replacement(
        std::string_view sheet_name) const noexcept;

    /// Returns the estimated sparse-store memory used to prepare final queued
    /// replacement payloads.
    ///
    /// The estimate is the sum of the temporary CellStore estimates recorded at
    /// successful replace_sheet_data() calls. It is useful for checking the
    /// current Patch facade guardrail behavior, but it is not a process RSS
    /// measurement and excludes generated XML chunks, PackageEditor state and
    /// staging files, source package bytes, ZIP writer buffers, and save-time
    /// assembly costs. If rename_sheet() changes a sheet to a temporary planned
    /// name and a later rename_sheet() changes it back to the source name, this
    /// diagnostic follows the restored planned name and does not keep a stale
    /// renamed-sheet entry.
    [[nodiscard]] std::size_t estimated_pending_replacement_memory_usage() const noexcept;

    /// Returns the sum of currently staged single-cell XML payload bytes for
    /// replace_cells() patches.
    ///
    /// This is a narrow input-payload diagnostic for the public Patch facade,
    /// not process RSS. It excludes source worksheet XML, package bytes,
    /// PackageEditor temporary file chunks, ZIP writer buffers, and save-time
    /// package assembly costs.
    [[nodiscard]] std::size_t estimated_pending_targeted_cell_replacement_xml_bytes()
        const noexcept;

    /// Returns the most recent failed public edit diagnostic, if any.
    ///
    /// This optional message is a coarse public facade diagnostic for failed
    /// replace_sheet_data(), replace_cells(), rename_sheet(), or WorksheetEditor
    /// mutation calls.
    /// A later failed public edit replaces the previous message; a successful
    /// public edit clears it. Inspection / pending diagnostic methods and
    /// save_as() do not update it, and a moved-from editor returns std::nullopt.
    /// The message is not an exception stack, internal EditPlan diagnostic,
    /// relationship audit, dependency audit, or package output-plan reason.
    [[nodiscard]] std::optional<std::string> last_edit_error() const;

    /// Returns coarse worksheet-level summaries for pending public edits.
    ///
    /// The returned vector follows source workbook sheet-catalog order and only
    /// includes worksheets with a queued public rename_sheet(),
    /// replace_sheet_data() effect, and/or dirty materialized WorksheetEditor
    /// session waiting for save_as() auto-flush. Each summary reports the source
    /// name, current planned name, whether the sheet was renamed, whether a
    /// final whole-<sheetData> replacement is queued for that planned name, and
    /// whether a dirty materialized session currently exists. As a
    /// current planned-state view, a rename-only chain that returns a sheet to
    /// its source name is omitted from this vector even though
    /// pending_change_count() still counts the successful public edit calls.
    /// Dirty materialized sessions are omitted after successful save_as()
    /// auto-flush unless another queued rename or replacement still applies;
    /// a clean matching reacquire of the saved materialized session also stays
    /// omitted until a later mutation dirties it again. If a queued rename
    /// remains, the summary remains as a rename summary with materialized_dirty
    /// false and zero materialized counts until a later mutation re-dirties the
    /// session. A rejected or failed save_as() leaves the current summary fields
    /// unchanged, including failures after internal materialized staging but
    /// before package-write success. If a rename chain returns to the source
    /// name before materialization, a later dirty materialized summary uses
    /// matching source/planned names and is not marked renamed.
    /// Failed WorksheetEditor mutations do not create dirty materialized
    /// summaries; a later successful mutation after such a failure follows the
    /// same current planned-name rules.
    /// This method does not itself flush, increment pending_change_count(), or
    /// update last_edit_error(). This is a public facade diagnostic; it does not
    /// expose internal EditPlan entries, dependency audits, relationship audits,
    /// preserved metadata, source cell counts, package parts, or save-time
    /// output-plan reasons. It returns an empty vector for a moved-from editor.
    [[nodiscard]] std::vector<WorkbookEditorWorksheetEditSummary> pending_worksheet_edits()
        const;

    /// Returns the source-to-planned worksheet catalog view.
    ///
    /// Entries follow the opened source workbook sheet-catalog order. For each
    /// source worksheet, the entry reports the original source name, the current
    /// planned name that worksheet_names() / save_as() will use, and whether the
    /// name has been changed by a successful rename_sheet() call. This is a
    /// public facade diagnostic; it does not expose workbook relationships,
    /// worksheet part names, package entries, internal EditPlan entries, or
    /// dependency audits. It returns an empty vector for a moved-from editor.
    /// Reading this diagnostic does not materialize worksheets, flush or reload
    /// materialized WorksheetEditor sessions, mutate source or planned package
    /// state, or update last_edit_error().
    [[nodiscard]] std::vector<WorkbookEditorWorksheetCatalogEntry> worksheet_catalog() const;

    /// Returns sheet-qualified formula references from materialized worksheets.
    ///
    /// This is a read-only dependency-risk diagnostic for the small-file
    /// WorksheetEditor path. It scans the formula text currently held in
    /// WorkbookEditor-owned materialized sparse sessions and reports references
    /// whose formula text contains a sheet qualifier, for example `Data!A1`,
    /// `'Other Sheet'!B2`, `Data!A:C`, `[Book.xlsx]Data!A1`, or
    /// `Sheet1:Sheet3!A1`. It also compares ordinary decoded qualifier tokens
    /// with the current source-to-planned worksheet catalog so callers can
    /// detect formula text that still names a source sheet after rename_sheet()
    /// has changed that sheet's planned catalog name. External workbook and 3D
    /// sheet-range qualifiers are classified for audit and are not matched to a
    /// single local workbook sheet.
    ///
    /// This method intentionally does not materialize worksheets, scan
    /// non-materialized worksheet parts, parse the full Excel formula grammar,
    /// evaluate formulas, validate external workbook targets or 3D sheet range
    /// semantics, rewrite formulas, rebuild calcChain, repair defined names, or
    /// update last_edit_error(). It also does not increment pending_change_count(),
    /// queue replacements, dirty materialized sessions, or change pending edit
    /// diagnostics. It returns an empty vector for a moved-from editor.
    [[nodiscard]] std::vector<WorkbookEditorFormulaReferenceAudit>
    formula_reference_audits() const;

    /// Returns sheet-qualified formula references from source worksheet XML.
    ///
    /// API mode: Patch / read-only source workbook inspection. This diagnostic
    /// scans the opened package's source worksheet XML parts using the internal
    /// bounded event reader and reports references from formula cells that carry
    /// explicit `<f>...</f>` formula text. Source-order metadata-only shared
    /// formula followers are also expanded when a preceding shared formula
    /// definition is available. It compares ordinary decoded sheet qualifier
    /// tokens with the current source-to-planned worksheet catalog so callers
    /// can detect formulas in non-materialized worksheets that still name a
    /// source sheet after rename_sheet() has changed that sheet's planned
    /// catalog name. External workbook and 3D sheet-range qualifiers are
    /// classified for audit and are not matched to a single local workbook
    /// sheet.
    ///
    /// This method intentionally does not materialize WorksheetEditor sessions,
    /// rewrite formula text, preserve shared formula metadata, scan unresolved
    /// out-of-order shared formula followers, parse the full Excel formula
    /// grammar, evaluate formulas, validate external workbook targets or 3D
    /// sheet range semantics, rebuild calcChain, repair workbook metadata, or
    /// update last_edit_error(). It also does not increment pending_change_count(),
    /// queue replacements, create materialized sessions, or change pending edit
    /// diagnostics. It returns an empty vector for a moved-from editor.
    [[nodiscard]] std::vector<WorkbookEditorFormulaReferenceAudit>
    source_formula_reference_audits() const;

    /// Returns sheet-qualified formula references from workbook definedNames.
    ///
    /// This is a read-only dependency-risk diagnostic for current workbook
    /// metadata. It materializes only the small `xl/workbook.xml` metadata part
    /// from the current planned editor state when a workbook rewrite is queued,
    /// otherwise from the source package. It scans direct
    /// `<definedNames><definedName>` entries, and reports
    /// sheet-qualified references found in their formula text. It compares
    /// ordinary decoded qualifier tokens with the current source-to-planned
    /// worksheet catalog so callers can detect definedNames that still name a
    /// source sheet after rename_sheet() has changed that sheet's planned
    /// catalog name. When an explicit rename formula policy has already queued
    /// a direct definedName rewrite, this diagnostic reflects that planned
    /// workbook XML instead of reporting stale source definedName text.
    /// External workbook and 3D sheet-range qualifiers are classified for audit
    /// and are not matched to a single local workbook sheet.
    ///
    /// This method intentionally does not materialize worksheets, scan cell
    /// formulas, parse the full Excel formula grammar, evaluate formulas,
    /// validate external workbook targets or 3D sheet range semantics, rewrite
    /// definedNames, rebuild calcChain, repair workbook metadata, or update
    /// last_edit_error(). It also does not increment pending_change_count(),
    /// queue replacements, dirty materialized sessions, or change pending edit
    /// diagnostics. It returns an empty vector for a moved-from editor.
    [[nodiscard]] std::vector<WorkbookEditorDefinedNameFormulaReferenceAudit>
    defined_name_formula_reference_audits() const;

    /// Materializes an existing worksheet for small-file random cell editing.
    ///
    /// API mode: In-memory over an existing workbook. The returned
    /// WorksheetEditor is a borrowed handle into WorkbookEditor-owned sparse
    /// worksheet state. It is valid only while this WorkbookEditor object
    /// remains alive and is not moved or move-assigned; callers should reacquire
    /// a handle after ownership transfer. Successful or failed save_as() does
    /// not invalidate the handle; the same borrowed worksheet can continue to be
    /// used for later mutations and reflushes as long as the owning
    /// WorkbookEditor itself is still the same object. Repeated worksheet() or
    /// try_worksheet() calls for the same planned sheet reuse the same
    /// materialized session when options match, preserving dirty edits instead
    /// of reloading source cells and overwriting the sparse store. Borrowed
    /// handles acquired this way observe the same shared dirty state; after a
    /// successful save_as() clears the session dirty flag, matching reacquire
    /// still reuses the materialized state rather than loading from the source
    /// package again, and does not re-add dirty materialized diagnostics until
    /// a later mutation changes the session. The same clean reacquire rule
    /// applies after a rejected save_as() is followed by a successful save_as().
    /// Mismatched options for an existing materialized session remain a failure
    /// path before and after save_as(), including after failed-save recovery,
    /// and do not dirty saved sessions or update last_edit_error(). Missing
    /// worksheet() lookups throw without discarding, reloading, or dirtying an
    /// existing saved materialized session.
    ///
    /// Source worksheet cells are loaded through the internal event reader into
    /// a sparse CellStore; this does not build a full worksheet matrix. The
    /// loader currently accepts only supported scalar cell shapes. On the
    /// workbook-backed path, valid source `t="s"` cells are resolved through
    /// the workbook's existing `xl/sharedStrings.xml` and materialized as
    /// plain text. The actual shared string item order/text drives
    /// materialization; declared `count` / `uniqueCount` values and otherwise
    /// well-formed unknown sharedStrings attributes are not schema-validated.
    /// Prefixed sharedStrings `sst` / `si` / `t` / `r` element names are
    /// matched by local-name for materialization; this is not namespace URI
    /// validation, namespace repair, or schema validation. Unsupported
    /// sharedStrings item/rich-run local-names still fail fast even when their
    /// namespace URI is ignored. Mixed direct `<t>` text and rich `<r>` runs,
    /// `rPr` outside a rich run, and text wrappers inside `rPr` are malformed
    /// sharedStrings rich metadata and fail during materialization. `rPh` /
    /// `phoneticPr` / `extLst` opaque nested markup text is ignored, and
    /// self-closing ignored metadata is treated as empty metadata; nested
    /// `<si>` decoys, markup nested inside a text wrapper, orphan closing tags,
    /// and unclosed ignored metadata still fail fast as malformed source
    /// sharedStrings. Prefixed source
    /// worksheet / sheetData / row / cell element names and inlineStr,
    /// rich-run, formula, and value-wrapper element names are likewise matched
    /// by local-name for materialization; element namespace URIs are not
    /// inspected. Unsupported local-names still fail fast even when their
    /// namespace URI is ignored.
    /// Workbooks without a sharedStrings part can still materialize supported
    /// non-`t="s"` cells and dirty save does not create a sharedStrings part.
    /// SharedStrings metadata is an on-demand dependency of actual selected
    /// worksheet `t="s"` cells: stale or malformed sharedStrings
    /// relationship/content-type metadata or payloads do not block supported
    /// non-shared-string cells, are not repaired or pruned, and still fail if
    /// shared string indexes are encountered.
    /// Missing referenced sharedStrings parts, invalid sharedStrings
    /// relationship targets, malformed sharedStrings XML/entity/attribute
    /// syntax, invalid shared string indexes, malformed source style
    /// attributes, unsupported source cell metadata, non-whitespace source cell
    /// text outside `<v>` / `<t>` / `<f>` wrappers, and malformed worksheet
    /// XML fail before returning a handle. Explicit source `s` attributes whose
    /// value is exactly `0` (for example `s="0"`, `s='0'`, or `s = "0"`) are
    /// normalized to no style handle rather than preserved. Canonical non-zero
    /// unsigned decimal source style ids are validated against the source
    /// styles.xml `cellXfs` table, materialized as numeric passthrough handles,
    /// and written back by dirty projection while styles.xml is preserved; they
    /// are not migrated or merged. Source style tokens such as empty values,
    /// valueless or unquoted syntax, unterminated attributes, `s="00"`,
    /// `s="+0"`, padded values, entity-encoded values, missing workbook styles
    /// metadata, out-of-range ids, or duplicate attributes are still rejected.
    /// Qualified style-like attributes such as `x:s` are unsupported cell
    /// metadata, not source style attributes.
    ///
    /// Operation mixing: a worksheet with queued replace_sheet_data() or
    /// replace_cells() payloads cannot be materialized, and replace_sheet_data()
    /// / replace_cells() / rename_sheet() reject a worksheet after it has been
    /// materialized. Use one editing mode per worksheet in this first public
    /// slice. Workbook-level request_full_calculation() is intentionally
    /// separate from this per-worksheet edit-mode guard: it may be queued before
    /// or after a catalog rename and clean or dirty materialized sessions, but
    /// it does not make queued Patch payloads composable with a materialized
    /// WorksheetEditor session.
    ///
    /// @param sheet_name Existing worksheet name in the current planned catalog.
    /// @param options Per-materialization sparse-store guardrails.
    /// @throws WorksheetMaterializationError if RejectKnownLosses detects source
    /// semantics that the current WorksheetEditor model cannot preserve.
    /// @throws FastXlsxError if the workbook is not open, the sheet is missing,
    /// a whole-sheet replacement is already queued for this sheet, options do
    /// not match an existing materialized session, or source worksheet loading
    /// otherwise fails. Missing-sheet and materialization failures do not update
    /// last_edit_error().
    [[nodiscard]] WorksheetEditor worksheet(
        std::string_view sheet_name, WorksheetEditorOptions options = {});

    /// Tries to materialize an existing worksheet for small-file random cell
    /// editing.
    ///
    /// API mode: In-memory over an existing workbook. This has the same
    /// borrowed-handle and guardrail semantics as worksheet(), except a missing
    /// current-planned worksheet name returns std::nullopt instead of throwing.
    /// Other failures still throw, including queued same-sheet
    /// replace_sheet_data() or replace_cells() payloads, option mismatches with
    /// an existing materialized session, unsupported source worksheet cell
    /// shapes, or malformed worksheet XML. Missing-sheet and materialization
    /// failures do not update last_edit_error(). A missing-sheet result does not
    /// queue a dirty session or edit; it also does not discard, reload, or dirty
    /// an existing saved materialized session after save_as() or failed-save
    /// recovery. A later no-op save_as() remains copy-original.
    ///
    /// @param sheet_name Existing worksheet name in the current planned catalog.
    /// @param options Per-materialization sparse-store guardrails.
    /// @throws WorksheetMaterializationError if RejectKnownLosses detects source
    /// semantics that the current WorksheetEditor model cannot preserve.
    /// @throws FastXlsxError if the workbook is not open or if a non-missing
    /// materialization failure occurs.
    [[nodiscard]] std::optional<WorksheetEditor> try_worksheet(
        std::string_view sheet_name, WorksheetEditorOptions options = {});

    /// Replaces the entire `<sheetData>` of an existing worksheet from rows of
    /// CellValue.
    ///
    /// `rows[i]` is worksheet row `i + 1`; `rows[i][j]` is column `j + 1`. A
    /// blank CellValue emits an empty cell at its position. Empty row vectors
    /// advance the input row mapping but do not emit an explicit empty row. Text
    /// values are written as inline strings; the existing `xl/sharedStrings.xml`
    /// is preserved rather than migrated. A CellValue's optional StyleId is
    /// written as the cell's style attribute as-is; it is not validated against
    /// the target workbook's style registry. Calling this again for the same
    /// sheet replaces the previously queued data for that sheet.
    /// If rename_sheet() has already queued a catalog rename in this editor,
    /// sheet lookup follows the current planned catalog for Patch output, so
    /// callers should use the planned new name for follow-up replacements.
    /// Missing names are rejected against that current planned catalog before
    /// the replacement payload is built, so a missing-sheet failure does not
    /// spend replacement guardrail budget or record pending replacement
    /// diagnostics.
    ///
    /// @param sheet_name Existing worksheet name to edit.
    /// @param rows Replacement worksheet rows, in row-major order.
    /// @throws FastXlsxError if the sheet name is missing or ambiguous, or if the
    /// generated payload or worksheet exceeds the bounded local-rewrite limit. On
    /// failure no edit state is mutated and the editor remains usable.
    void replace_sheet_data(
        std::string_view sheet_name, const std::vector<std::vector<CellValue>>& rows);

    /// Replaces targeted cells in one worksheet without materializing the whole
    /// worksheet into WorkbookEditor memory.
    ///
    /// API mode: Patch / existing-workbook targeted cell replacement/upsert. Each
    /// update names one 1-based Excel coordinate and a full CellValue payload.
    /// The source or current planned worksheet XML is scanned through the
    /// internal worksheet transformer and the rewritten output is staged as a
    /// PackageEditor-owned file-backed package-entry chunk. This avoids the
    /// whole-worksheet DOM/local-rewrite limit used by replace_sheet_data() and
    /// is the public large-file path for changing a bounded set of point cells.
    /// The default overload is strict existing-cell replacement; the explicit
    /// missing-cell-policy overload can opt into point upsert.
    ///
    /// The default missing-cell policy is CellPatchMissingCellPolicy::Fail:
    /// target cells must already exist in the source or current planned
    /// worksheet stream, and missing targets fail before public diagnostics are
    /// updated. Passing CellPatchMissingCellPolicy::Insert switches the same
    /// transformer to point-upsert mode: missing cells are inserted into
    /// existing rows, and missing rows are synthesized as minimal `<row r="N">`
    /// records. Neither mode shifts rows or columns, preserves prior per-cell
    /// attributes, preserves prior cell style handles on overwritten cells, or
    /// recalculates range metadata. Text values are written as inline strings;
    /// existing `xl/sharedStrings.xml` is preserved rather than migrated. A
    /// CellValue's optional StyleId is written as `s="N"` as-is and is not
    /// validated against the target workbook style table. Formula values write
    /// `<f>` text and cause the underlying Patch plan to request full
    /// recalculation / stale calcChain cleanup; formulas are not evaluated and
    /// no cached values are generated.
    ///
    /// Duplicate coordinates in one call are allowed after validation; later
    /// updates win. Empty input is a successful no-op after validating the
    /// planned worksheet name and mode-mixing guards, and it does not increment
    /// pending_change_count(). Calling replace_cells() again for the same sheet
    /// is allowed and later same-coordinate patches supersede earlier staged
    /// diagnostics. If rename_sheet() has queued a catalog rename, sheet lookup
    /// follows the current planned catalog, so callers should use the planned
    /// new name for follow-up patches.
    ///
    /// Operation mixing: replace_cells(), replace_sheet_data(), and
    /// materialized WorksheetEditor sessions are mutually exclusive per
    /// worksheet in this public facade. Use replace_cells() for large existing
    /// worksheets when a bounded set of point cells must change; use
    /// CellPatchMissingCellPolicy::Insert only when missing target coordinates
    /// should be inserted as point edits; use WorksheetEditor only for
    /// small-file random editing that can afford materialization.
    ///
    /// @param sheet_name Existing worksheet name in the current planned catalog.
    /// @param cells Targeted full-cell replacement batch.
    /// @throws FastXlsxError if the editor is not open, the sheet is missing,
    /// the worksheet has a conflicting edit mode, any coordinate is invalid, any
    /// target cell is missing in the default Fail policy, a replacement cell
    /// payload is malformed, the
    /// source/planned worksheet XML is malformed, or Patch policy rejects the
    /// resulting dependency/relationship audit. On failure no public facade
    /// diagnostic for this patch is updated and the editor remains usable.
    void replace_cells(
        std::string_view sheet_name, std::span<const WorksheetCellUpdate> cells);

    /// Replaces cells using an explicit missing-cell policy.
    ///
    /// Prefer this overload when the call site needs point upsert behavior:
    /// pass CellPatchMissingCellPolicy::Insert so missing-cell behavior is
    /// explicit at the call site.
    ///
    /// @param sheet_name Existing worksheet name in the current planned catalog.
    /// @param cells Targeted full-cell replacement/upsert batch.
    /// @param missing_cell_policy Whether missing target cells fail or are
    /// inserted as point edits.
    /// @throws FastXlsxError with the same validation and no-state-pollution
    /// guarantees as the default replace_cells() overload, except missing
    /// targets follow the selected policy.
    void replace_cells(std::string_view sheet_name,
        std::span<const WorksheetCellUpdate> cells,
        CellPatchMissingCellPolicy missing_cell_policy);

    /// Replaces existing cells from a small literal batch.
    ///
    /// This convenience overload consumes the initializer-list synchronously and
    /// delegates to the std::span overload, so validation, duplicate-coordinate
    /// behavior, mode-mixing guards, diagnostics, and Patch side effects are
    /// identical.
    void replace_cells(
        std::string_view sheet_name, std::initializer_list<WorksheetCellUpdate> cells);

    /// Replaces or inserts cells from a small literal batch using an explicit
    /// missing-cell policy.
    ///
    /// This convenience overload consumes the initializer-list synchronously and
    /// delegates to the std::span overload.
    void replace_cells(std::string_view sheet_name,
        std::initializer_list<WorksheetCellUpdate> cells,
        CellPatchMissingCellPolicy missing_cell_policy);

    /// Replaces an existing workbook image part from a file on disk.
    ///
    /// API mode: Patch / existing-workbook media-part rewrite. This queues a
    /// replacement for one package part and writes the changed bytes during
    /// save_as(); it never edits the source package in place.
    ///
    /// The target must be an existing `xl/media/*` part whose current content
    /// type is PNG or JPEG. The replacement file must decode to the same image
    /// format as the target part. This is a narrow media-part rewrite; it does
    /// not touch worksheet XML, drawings, relationships, anchors, content types,
    /// EXIF/PNG/JPEG metadata, or any drawing geometry. It is not image
    /// insertion, drawing editing, relationship repair/pruning, or orphan
    /// cleanup.
    ///
    /// The source image path is read again during save_as() while this remains
    /// the final queued source for the media part, so the caller must keep the
    /// file accessible until the edited workbook is written. A later successful
    /// replace_image() call for the same media part supersedes the earlier queued
    /// source; only the final queued source participates in save_as().
    ///
    /// @param image_part_name Existing package part path such as
    /// `xl/media/image1.png`.
    /// @param image_path Replacement PNG/JPEG file path.
    /// @throws FastXlsxError if the editor is not open, the target part is
    /// missing or outside the current PNG/JPEG media-part slice, the replacement
    /// file cannot be read, or the replacement format does not match the target.
    /// On failure no edit state is mutated and the editor remains usable.
    void replace_image(std::string_view image_part_name, std::filesystem::path image_path);

    /// Replaces an existing workbook image part from caller-owned bytes.
    ///
    /// API mode: Patch / existing-workbook media-part rewrite. The byte span is
    /// copied into staged replacement storage during the call, so the caller can
    /// release the span immediately after the call returns.
    ///
    /// The target must be an existing `xl/media/*` part whose current content
    /// type is PNG or JPEG. The replacement bytes must decode to the same
    /// image format as the target part. This does not touch worksheet XML,
    /// drawings, relationships, anchors, content types, EXIF/PNG/JPEG metadata,
    /// or any drawing geometry; it is not image insertion, drawing editing,
    /// relationship repair/pruning, or orphan cleanup.
    /// Calling replace_image() again for the same media part replaces the
    /// previously queued source; only the final queued source participates in
    /// save_as().
    ///
    /// @param image_part_name Existing package part path such as
    /// `xl/media/image1.png`.
    /// @param image_bytes Replacement PNG/JPEG bytes. The span is not retained.
    /// @throws FastXlsxError if the editor is not open, the target part is
    /// missing or outside the current PNG/JPEG media-part slice, the replacement
    /// bytes are empty or unreadable, or the replacement format does not match
    /// the target. On failure no edit state is mutated and the editor remains
    /// usable.
    void replace_image(std::string_view image_part_name, std::span<const std::byte> image_bytes);

    /// Renames a worksheet's sheet-catalog name for the saved package.
    ///
    /// This rewrites only the `<sheets><sheet name="...">` attribute in
    /// `xl/workbook.xml`. The worksheet part, workbook relationships, content
    /// types, and unknown entries are preserved; defined names, formulas, tables,
    /// drawings, charts, hyperlinks, and relationship targets are not touched, so
    /// this is a narrow catalog-name rewrite rather than a semantic sheet rename.
    /// The new name is XML-escaped. Duplicate-name checks are conservative and
    /// ASCII case-insensitive. The rename takes effect in worksheet_names() /
    /// has_worksheet() and in the save_as() output; source_worksheet_names() /
    /// has_source_worksheet() keep reporting the original source workbook view
    /// within the open session.
    ///
    /// @param old_name Existing worksheet name to rename.
    /// @param new_name New sheet-catalog name.
    /// @throws FastXlsxError if `old_name` is not present, if `new_name` already
    /// exists (ASCII case-insensitive) or contains invalid characters, or if
    /// `xl/workbook.xml` is unavailable. On failure no edit state is mutated,
    /// pending replacement diagnostics remain under their prior sheet name, and
    /// the editor remains usable. A successful rename back to the source sheet
    /// name clears the public renamed flag and migrates any queued replacement
    /// diagnostics back to that source name; it is still only a catalog-name
    /// rewrite, not semantic sheet rename synchronization.
    void rename_sheet(std::string_view old_name, std::string new_name);

    /// Renames a worksheet's sheet-catalog name with explicit formula policy.
    ///
    /// API mode: Patch. This keeps the same catalog-name rewrite behavior as
    /// rename_sheet(old_name, new_name) unless `options.formula_policy` is
    /// `WorkbookEditorRenameFormulaPolicy::RewriteDefinedNames` or
    /// `WorkbookEditorRenameFormulaPolicy::RewriteDefinedNamesAndMaterializedWorksheetFormulas`.
    /// The first opt-in policy additionally rewrites direct workbook
    /// definedName formula text in `xl/workbook.xml` from the old sheet
    /// qualifier to the new sheet qualifier. In rename chains, it also rewrites
    /// references that still use the sheet's original source name unless the
    /// chain has returned to that source name. The second policy also rewrites
    /// matching formula cells in WorksheetEditor sessions that are already
    /// materialized in this WorkbookEditor and marks those sessions dirty for
    /// save_as() auto-flush. It preserves external workbook references, 3D
    /// sheet ranges, unsupported nested definedName XML failures,
    /// non-materialized worksheet formula cells, tables, drawings, charts,
    /// hyperlinks, relationship targets, sharedStrings, styles, and calcChain.
    ///
    /// This is still not a formula engine, semantic sheet rename, relationship
    /// repair, non-materialized worksheet scan/rewrite, or calcChain rebuild.
    ///
    /// @param old_name Existing worksheet name to rename.
    /// @param new_name New sheet-catalog name.
    /// @param options Explicit formula-reference handling policy.
    /// @throws FastXlsxError on the same rename failures as the default
    /// overload, if opt-in definedName formula rewriting detects malformed
    /// workbook definedName XML, or if opt-in materialized formula rewriting
    /// would violate the materialized session guardrails. On failure no edit
    /// state is mutated.
    void rename_sheet(
        std::string_view old_name,
        std::string new_name,
        WorkbookEditorRenameOptions options);

    /// Requests workbook full-calculation metadata for the saved package.
    ///
    /// API mode: Patch / workbook metadata rewrite. This queues the same
    /// workbook calc metadata helper used by worksheet rewrite paths: it sets
    /// `fullCalcOnLoad="1"` on `xl/workbook.xml` and removes stale
    /// `xl/calcChain.xml` when present. This does not evaluate formulas, repair
    /// relationships, update defined names, or expose the internal calc-chain
    /// policy surface. The request is workbook-scoped metadata and can coexist
    /// with queued sheet-catalog renames plus clean or dirty materialized
    /// WorksheetEditor sessions; it preserves planned sheet names and dirty
    /// materialized diagnostics until save_as(). It does not relax same-sheet
    /// replace_sheet_data() / replace_cells() versus materialized-session
    /// guards.
    void request_full_calculation();

    /// Writes the edited workbook to a new package path.
    ///
    /// This never edits the source package in place. It rejects an output path
    /// that is the source package, empty, an existing directory, or has a
    /// non-directory / missing parent. Worksheets not edited and unknown parts are
    /// copied from the source package; with no queued public edits, save_as()
    /// writes a reader-backed roundtrip copy. Dirty WorksheetEditor sessions are
    /// staged into the Patch plan before package writing, but their public dirty
    /// handoff is committed only after the output package is written successfully.
    /// A rejected or failed save_as() therefore preserves queued edits, dirty
    /// materialized diagnostics, pending/unsaved counts, and last_edit_error().
    /// Output-path preflight runs before materialized staging, including for
    /// rename-back paths that restored a sheet to its source name. A retry
    /// restages current session values and replaces any stale internal staged
    /// projection from the failed attempt.
    /// Successful save_as() is also not a commit/close operation: queued public
    /// edit diagnostics remain visible so callers may save the same planned
    /// state to another output path or continue editing. Consequently,
    /// has_pending_changes() can remain true after a successful save even when
    /// pending materialized diagnostics are empty.
    ///
    /// @param path Output `.xlsx` path; must differ from the source package.
    /// @throws FastXlsxError if the output path is rejected or the package cannot
    /// be written.
    void save_as(const std::filesystem::path& path);

private:
    friend class WorksheetEditor;
#ifdef FASTXLSX_ENABLE_TEST_HOOKS
    friend void detail::testing_workbook_editor_materialize_source_sheet(
        WorkbookEditor& editor,
        std::string_view planned_name,
        std::string_view source_sheet_name);
    friend void detail::testing_workbook_editor_set_materialized_cell(
        WorkbookEditor& editor,
        std::string_view planned_name,
        std::uint32_t row,
        std::uint32_t column,
        const CellValue& value);
    friend void detail::testing_workbook_editor_erase_materialized_cell(
        WorkbookEditor& editor,
        std::string_view planned_name,
        std::uint32_t row,
        std::uint32_t column);
    friend void detail::testing_workbook_editor_flush_materialized_sessions_to_patch_plan(
        WorkbookEditor& editor);
    friend std::size_t detail::testing_workbook_editor_materialized_session_count(
        const WorkbookEditor& editor) noexcept;
    friend std::size_t detail::testing_workbook_editor_dirty_materialized_session_count(
        const WorkbookEditor& editor) noexcept;
    friend bool detail::testing_workbook_editor_has_materialized_session(
        const WorkbookEditor& editor, std::string_view planned_name) noexcept;
    friend std::vector<std::string>
    detail::testing_workbook_editor_dirty_materialized_session_names(
        const WorkbookEditor& editor);
    friend struct detail::WorkbookEditorPackagePlanAccessor;
#endif
    WorkbookEditor();

    void replace_cells_impl(std::string_view sheet_name,
        std::span<const WorksheetCellUpdate> cells,
        CellPatchMissingCellPolicy missing_cell_policy);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint64_t handle_generation_ = 0;
};

} // namespace fastxlsx
