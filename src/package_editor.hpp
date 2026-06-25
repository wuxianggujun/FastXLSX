#pragma once

#include "package_reader.hpp"
#include "package_writer.hpp"

#include <fastxlsx/detail/formula_reference_audit.hpp>
#include <fastxlsx/detail/worksheet_transformer.hpp>
#include <fastxlsx/document_properties.hpp>

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fastxlsx::detail {

// Current internal sheetData patch helper uses chunk-source input and
// file-backed staged output, but keeps the replacement payload guard until a
// true streaming worksheet transformer exists. Source/planned worksheet input is
// separately constrained by the event-reader retained-window guard below.
inline constexpr std::size_t package_editor_sheet_data_replacement_payload_byte_limit =
    4U * 1024U * 1024U;

// Current cell replacement uses this as the event-reader retained-window guard.
// Source package worksheet entries and planned staged chunks are scanned through
// chunk-source readers. Ordinary non-worksheet part replacements can still be
// queued as strings, but worksheet part replacement paths hand off planned
// worksheet XML as staged chunks after validation/audit or ordinary staging.
inline constexpr std::size_t package_editor_cell_replacement_event_window_byte_limit =
    4U * 1024U * 1024U;

// Workbook catalog/calc helpers intentionally materialize workbook.xml as a
// small metadata part. Keep that boundary explicit so it cannot become a broad
// package-part string fallback.
inline constexpr std::size_t package_editor_workbook_xml_materialization_byte_limit =
    4U * 1024U * 1024U;

// Active/generated content types, relationships, and docProps rewrites are
// intentionally materialized as small metadata XML. Copy-original source
// metadata entries use file-backed chunks instead.
inline constexpr std::size_t package_editor_metadata_xml_materialization_byte_limit =
    4U * 1024U * 1024U;

// Ordinary replace_part() is only an internal small-XML convenience for
// workbook/core/app source parts and generated metadata. Other existing source
// package parts use staged chunks so object payloads and worksheet dependencies
// do not re-enter materialized replacement state.

struct PackagePartReplacement {
    PartName part_name;
    // Bounded workbook/core/app or generated small XML. Other source parts use chunks.
    std::string materialized_small_xml;
    std::vector<PackageEntryChunk> chunks;
    PartWriteMode write_mode = PartWriteMode::LocalDomRewrite;
    std::string reason;
};

struct PackageEntryReplacement {
    std::string entry_name;
    // Materialized payload for active metadata entries only:
    // [Content_Types].xml, package relationships, and source-owned .rels.
    std::string materialized_data;
};

struct PackageEditorOutputEntryPlan {
    std::string entry_name;
    PartWriteMode write_mode = PartWriteMode::CopyOriginal;
    bool source_entry = false;
    bool package_part = false;
    bool generated = false;
    bool copied_from_source = false;
    bool omitted = false;
    bool file_backed_source_copy = false;
    bool staged_replacement_chunks = false;
    bool materialized_replacement = false;
    std::string file_backed_source_copy_reason;
    std::string materialized_replacement_reason;
    PackageEntryAuditKind audit_kind = PackageEntryAuditKind::Generic;
    std::string part_name;
    std::string owner_part;
    std::string relationship_owner_part;
    std::string relationship_id;
    std::string relationship_type;
    std::string relationship_target;
    std::vector<RemovedPartInboundRelationshipAudit> inbound_relationships;
    std::string reason;
};

struct PackageEditorOutputPlan {
    std::vector<PackageEditorOutputEntryPlan> entries;
    bool full_calculation_on_load = false;
    CalcChainAction calc_chain_action = CalcChainAction::Preserve;
    std::vector<std::string> notes;
    std::vector<EditPlanRemovedPart> removed_parts;
    std::vector<EditPlanRemovedPackageEntry> removed_package_entries;
    std::vector<RelationshipTargetAudit> relationship_target_audits;
    std::vector<WorksheetRelationshipReferenceAudit> worksheet_relationship_reference_audits;
    std::vector<WorksheetPayloadDependencyAudit> worksheet_payload_dependency_audits;
    std::vector<WorkbookPayloadDependencyAudit> workbook_payload_dependency_audits;
};

enum class SheetCatalogRenameFormulaPolicy {
    AuditOnly,
    RewriteDefinedNames,
};

struct SheetCatalogRenameOptions {
    SheetCatalogRenameFormulaPolicy formula_policy =
        SheetCatalogRenameFormulaPolicy::AuditOnly;
    std::vector<FormulaSheetReferenceRewrite> extra_formula_rewrites;
};

#ifdef FASTXLSX_ENABLE_TEST_HOOKS
using PackageEditorSourceCopyTempFilesHook =
    void (*)(std::span<const std::filesystem::path> temporary_source_files);
void testing_set_package_editor_source_copy_temp_files_hook(
    PackageEditorSourceCopyTempFilesHook hook) noexcept;

struct ReplacementCellPayloadScannerTestResult {
    std::size_t start_tag_count = 0;
    std::size_t formula_tag_count = 0;
    std::size_t relationship_reference_tag_count = 0;
};

ReplacementCellPayloadScannerTestResult testing_scan_replacement_cell_payload_start_tags(
    const WorksheetCellReplacementPayload& payload);

struct SheetDataStartTagScannerTestResult {
    std::size_t start_tag_count = 0;
    std::size_t formula_tag_count = 0;
    std::size_t shared_string_cell_tag_count = 0;
};

SheetDataStartTagScannerTestResult testing_scan_sheet_data_start_tags_from_chunks(
    std::span<const std::string_view> chunks,
    std::size_t max_window_bytes);

struct RelationshipReferenceScannerTestResult {
    std::vector<std::string> elements;
    std::vector<std::string> relationship_ids;
};

RelationshipReferenceScannerTestResult testing_scan_worksheet_relationship_references_from_chunks(
    std::span<const std::string_view> chunks);

[[nodiscard]] std::string testing_read_package_entry_chunks_to_string(
    std::vector<PackageEntryChunk> chunks);
[[nodiscard]] std::string testing_read_first_package_entry_chunk_for_lifecycle(
    std::vector<PackageEntryChunk> chunks);
#endif

// Internal PackageEditor foundation for the Patch path.
//
// This first slice opens an existing stored/no-compression package through
// PackageReader, records part-level replacement intent and metadata-entry audit
// in an EditPlan, and writes a new package by copying untouched entry bytes.
// Ordinary part replacement rejects OPC metadata package entries such as
// `[Content_Types].xml` and `.rels`; those entries are only changed by the
// narrow metadata-aware helpers that also update the package-entry audit.
// If an ordinary part replacement targets a part previously generated by a
// metadata-aware helper, the ordinary replacement becomes the final part bytes
// while metadata entries such as content types and package relationships keep
// their helper-managed audit. If a metadata-aware helper later takes ownership
// of a previously ordinary-replaced part, the helper output becomes final and
// stale ordinary replacement state is discarded. The core/app docProps helper
// can also take ownership after explicit removal by restoring generated payload
// output; it does not restore source-owned docProps `.rels` entries.
// A later ordinary replacement can also restore a previously removed source
// package part as the active final state, clearing the stale removal/omission
// audit, reusing any source-owned `.rels` entry as copy-original metadata, and
// returning content types to source/copy-original state when the restored part
// brings back the source override.
// Worksheet replacement can update the small workbook/content-type/relationship
// metadata needed for the conservative calcChain-remove/fullCalcOnLoad policy,
// while untouched source worksheet relationships and linked object parts are
// copied as-is. Preserved source-owned `.rels` entries can also be recorded as
// copy-original package-entry audit metadata in the EditPlan with structured
// source-owner context, including workbook `.rels` when workbook metadata
// changes but relationships do not. If ordinary workbook replacement follows a
// worksheet rewrite that requested recalculation, the replacement workbook XML
// is kept consistent with the existing fullCalcOnLoad / calcChain plan and does
// not downgrade already-rewritten workbook `.rels` audit metadata.
// Worksheet replacement also performs audit-only checks between relationship
// ids referenced by replacement worksheet XML and the preserved worksheet
// `.rels` entry. Missing ids, stale unreferenced ids, and known worksheet
// element/type mismatches are reported as notes and structured
// WorksheetRelationshipReferenceAudit records under the default preserve policy.
// `ReferencePolicyAction::Fail` rejects those mismatches before Patch state
// changes. It also rejects worksheet replacement payload dependencies that are
// currently audit-only, such as shared string indexes, style ids, formulas, and
// worksheet-local reference/object metadata. The sheetData helper applies that
// payload guard only to the replacement <sheetData> payload itself so preserved
// wrapper metadata is not over-blocked. This slice does not validate namespaces,
// repair, prune, or regenerate worksheet relationships.
// Removed parts also omit their source-owned `.rels` entry in this narrow path
// so the output does not keep relationships for an absent owner part. Explicit
// removal is limited to source package entries and rewrites content types only
// when the removed part had an override; inbound relationships from other parts
// are audited and left untouched under the default policy. Opt-in
// `ReferencePolicyAction::Fail` rejects removals with known inbound
// relationships before Patch state changes. This
// slice rejects saving over the source package because the reader-backed
// copy path is not an atomic in-place editor. It
// is not a public editing API, Zip64 support, relationship pruning/orphan
// cleanup, broad object preservation proof, or a full relationship/content-type
// mutator.
class PackageEditor {
public:
    [[nodiscard]] static PackageEditor open(std::filesystem::path path);
    ~PackageEditor();

    PackageEditor(const PackageEditor&) = delete;
    PackageEditor& operator=(const PackageEditor&) = delete;
    PackageEditor(PackageEditor&& other);
    PackageEditor& operator=(PackageEditor&& other);

    [[nodiscard]] const PackageReader& reader() const noexcept;
    [[nodiscard]] const PackageManifest& manifest() const noexcept;
    [[nodiscard]] const EditPlan& edit_plan() const noexcept;
    // Internal diagnostic view over the current workbook metadata. This
    // materializes the planned workbook XML when a small workbook rewrite is
    // queued, otherwise it reads the source workbook XML. It is intentionally
    // limited to workbook.xml-sized metadata and is not a general part
    // materialization API.
    [[nodiscard]] std::string current_workbook_xml_for_diagnostics(
        std::string_view purpose) const;

    void replace_part(PartName part_name, std::string materialized_small_xml,
        PartWriteMode write_mode, std::string reason = {});
    // Internal staged package-entry source foundation for non-worksheet stream
    // rewrite work. The caller supplies already-produced chunks for an existing
    // non-small package part; this records a StreamRewrite part replacement and
    // lets save_as() hand the chunks to PackageWriter without flattening them
    // into one string. Worksheet parts are routed through
    // replace_worksheet_part_chunks() so generic staged chunks cannot bypass
    // worksheet root validation, dependency/relationship audit, or calc metadata
    // handling. Workbook/core/app small XML parts are intentionally kept on
    // replace_part()'s materialized small-XML path. It is not a public Patch API,
    // XML validator, dependency repair, or calc metadata helper for
    // non-worksheet parts.
    void replace_part_chunks(
        PartName part_name, std::vector<PackageEntryChunk> chunks, std::string reason = {});
    void remove_part(PartName part_name, std::string reason = {},
        const ReferencePolicy& policy = {});
    // Internal chunk-source variant for complete worksheet replacement. The
    // source callback is consumed once after target/workbook/calc-policy
    // preflight while writing a PackageEditor-owned file-backed staged chunk and
    // running worksheet root validation plus dependency/relationship audit.
    // Calc metadata handling and save_as() then reuse the prevalidated staged
    // chunks. This avoids forcing internal callers with pull-based worksheet XML
    // sources to first materialize a std::string or prebuild a PackageEntryChunk
    // vector, and avoids reopening the staged chunk just to validate/audit it.
    void replace_worksheet_part_from_chunk_source(PartName worksheet_part,
        const WorksheetInputChunkCallback& read_next_chunk,
        const ReferencePolicy& policy = {}, std::string reason = {});
    // Internal staged-output variant for worksheet replacement. Validation and
    // dependency/relationship audit read the provided PackageEntry chunks
    // through chunk-source readers before the chunks are recorded for save_as().
    // This is still an internal staged payload bridge, not a public low-memory
    // worksheet transformer.
    void replace_worksheet_part_chunks(PartName worksheet_part,
        std::vector<PackageEntryChunk> chunks, const ReferencePolicy& policy = {},
        std::string reason = {});
    // Internal Patch convenience for workbook-targeted worksheet replacement.
    // Resolves the sheet name through the source workbook sheet catalog, or
    // through the currently planned `/xl/workbook.xml` small XML when a
    // materialized workbook replacement is queued, and workbook relationships,
    // then delegates to replace_worksheet_part_from_chunk_source(). It can target
    // a sheet name that exists only in the planned workbook catalog. If
    // `/xl/workbook.xml` has been removed, it fails before consuming caller
    // worksheet chunks instead of falling back to source workbook XML. A
    // defensive failure still exists for impossible staged-workbook internal
    // state; staged workbook chunks are rejected at replace_part_chunks(). It
    // does not rename, add, delete, or repair sheets.
    // Internal by-name chunk-source variant for complete worksheet replacement.
    // Resolves the sheet name through the same planned/source workbook catalog
    // path as the direct by-name Patch resolver, then delegates to
    // replace_worksheet_part_from_chunk_source().
    void replace_worksheet_part_from_chunk_source_by_name(std::string_view sheet_name,
        const WorksheetInputChunkCallback& read_next_chunk,
        const ReferencePolicy& policy = {}, std::string reason = {});
    // Internal by-name staged-output variant for worksheet replacement. Resolves
    // the sheet name through the same planned/source workbook catalog path as
    // the chunk-source by-name helper, then validates and audits the provided
    // chunks through replace_worksheet_part_chunks(). This avoids reintroducing
    // a materialized worksheet string entry point for by-name callers.
    void replace_worksheet_part_chunks_by_name(std::string_view sheet_name,
        std::vector<PackageEntryChunk> chunks, const ReferencePolicy& policy = {},
        std::string reason = {});
    // Internal Patch helper for template-style data updates. Replaces only the
    // worksheet <sheetData> element while preserving the surrounding worksheet
    // XML through a bounded local rewrite, then delegates calcChain/fullCalcOnLoad
    // and preservation handling to the worksheet staged-chunk commit path. The
    // current helper streams the source/planned worksheet and replacement
    // sheetData through
    // chunk-source readers and records the rewritten worksheet XML as
    // PackageEditor-owned file-backed staged chunks. Replacement sheetData
    // payloads above package_editor_sheet_data_replacement_payload_byte_limit are
    // still refused, but the file-backed rewritten worksheet output is not
    // rejected just because its final size exceeds that payload guard; this is
    // still not the future large-file streaming worksheet transformer.
    // Replacement sheetData root
    // validation, the bounded payload-size guard, output insertion, payload
    // dependency audit, and relationship-id scanning consume caller replacement
    // chunks directly while writing the rewritten worksheet staged chunk. This
    // path no longer stages or replays a separate replacement sheetData chunk.
    // Preserved
    // worksheet-local metadata such as
    // sheet properties, dimensions, views, default formatting, columns,
    // protection, filters, validations, print/page setup, header/footer,
    // breaks, phonetic metadata, ignored errors, drawings, worksheet object
    // references, extensions, and table references is recorded as audit notes
    // for caller review, but not repaired. It is not random cell editing or
    // semantic sync for sharedStrings, styles, tables, drawings, filters,
    // columns, print/page setup, protection, ignored errors, object
    // relationships, extensions, or merged ranges.
    void replace_worksheet_sheet_data_from_chunk_source(PartName worksheet_part,
        const WorksheetInputChunkCallback& read_next_sheet_data_chunk,
        const ReferencePolicy& policy = {});
    // Internal Patch convenience for workbook-targeted template fills. Resolves
    // the sheet name through the source workbook sheet catalog, or through the
    // currently planned `/xl/workbook.xml` small XML when a materialized workbook
    // replacement is queued, and workbook relationships, then delegates to
    // replace_worksheet_sheet_data_from_chunk_source(). It can target a sheet
    // name that exists only in the planned workbook catalog. If
    // `/xl/workbook.xml` has been removed, it fails before consuming caller
    // sheetData chunks instead of falling back to source workbook XML. A
    // defensive failure still exists for impossible staged-workbook internal
    // state; staged workbook chunks are rejected at replace_part_chunks(). It
    // does not rename, add, delete, or repair sheets.
    void replace_worksheet_sheet_data_from_chunk_source_by_name(std::string_view sheet_name,
        const WorksheetInputChunkCallback& read_next_sheet_data_chunk,
        const ReferencePolicy& policy = {});
    // Internal handoff from the P8 worksheet transformer foundation. Source
    // package entries are scanned through PackageReader ZIP-entry chunk sources
    // for root validation, dependency/dimension analysis, relationship-id audit,
    // and the output pass. Stored entries stream directly from the source ZIP
    // payload; minizip builds stream DEFLATE entries through decompressed
    // PackageReader chunk sources.
    // Planned staged package-entry chunks are also scanned through chunk-source
    // readers. Worksheet replacement helpers write caller-provided worksheet XML
    // chunk sources to PackageEditor-owned staged chunks and run follow-up
    // transforms over those chunks, so follow-up cell replacement does not
    // retain that planned worksheet payload as a replacement string. Ordinary
    // replace_part() is no longer a worksheet replacement entry point.
    // The rewritten output is streamed into a PackageEditor-owned temporary file
    // chunk instead of materializing the rewritten worksheet string. It is still
    // not a public Patch API, relationship repair, sharedStrings/style
    // migration, or a fully low-memory planned-input pipeline.
    void replace_worksheet_cells(PartName worksheet_part,
        std::span<const WorksheetCellReplacement> replacements,
        const ReferencePolicy& policy = {});
    // Internal Patch upsert variant for targeted cells. Existing cells are
    // replaced; missing cells are inserted into source-order rows, and missing
    // rows are synthesized as minimal `<row r="N">` records. It still streams
    // source/planned worksheet XML through the transformer and does not repair
    // range-bearing metadata, sharedStrings, styles, relationships, or formulas.
    void replace_or_insert_worksheet_cells(PartName worksheet_part,
        std::span<const WorksheetCellReplacement> replacements,
        const ReferencePolicy& policy = {});
    // Internal by-name convenience for replace_worksheet_cells(); it reuses the
    // same source/planned workbook catalog resolver as the other by-name Patch
    // helpers and does not add, delete, rename, or repair sheets.
    void replace_worksheet_cells_by_name(std::string_view sheet_name,
        std::span<const WorksheetCellReplacement> replacements,
        const ReferencePolicy& policy = {});
    // Internal by-name convenience for replace_or_insert_worksheet_cells().
    void replace_or_insert_worksheet_cells_by_name(std::string_view sheet_name,
        std::span<const WorksheetCellReplacement> replacements,
        const ReferencePolicy& policy = {});
    // Internal Patch helper for the workbook sheet catalog. Rewrites only the
    // direct `<sheets><sheet name="...">` attribute in `/xl/workbook.xml` while
    // preserving worksheet parts, workbook relationships, content types, and
    // unknown entries. It does not update defined names, formulas, tables,
    // drawings, charts, hyperlinks, or relationship targets, so callers must
    // treat it as a narrow catalog-name mutation rather than full sheet rename.
    // New-name duplicate checks are conservative and ASCII case-insensitive.
    void rename_sheet_catalog_entry(std::string_view old_name, std::string new_name,
        const ReferencePolicy& policy = {}, SheetCatalogRenameOptions options = {});
    // Internal small-part Patch helper. Rewrites workbook calc metadata only,
    // optionally omitting stale calcChain payload/metadata, and leaves worksheet
    // parts and linked objects copy-original.
    void request_full_calculation(
        CalcChainAction calc_chain_action = CalcChainAction::Remove);
    void set_document_properties(const DocumentProperties& properties);
    [[nodiscard]] PackageEditorOutputPlan planned_output() const;
    [[nodiscard]] std::vector<PackageEditorOutputEntryPlan> planned_output_entries()
        const;
    void save_as(const std::filesystem::path& path,
        PackageWriterOptions options = {PackageWriterBackend::StoredZipBootstrap}) const;

private:
    explicit PackageEditor(PackageReader reader);
    void replace_worksheet_cells_impl(PartName worksheet_part,
        std::span<const WorksheetCellReplacement> replacements,
        const ReferencePolicy& policy,
        WorksheetCellReplacementMode mode);
    void replace_worksheet_part_prevalidated_chunks(PartName worksheet_part,
        std::vector<PackageEntryChunk> chunks, const ReferencePolicy& policy,
        std::vector<std::string> payload_notes,
        std::vector<WorksheetPayloadDependencyAudit> payload_audits,
        std::vector<std::string> relationship_reference_notes,
        std::vector<WorksheetRelationshipReferenceAudit> relationship_reference_audits,
        std::string replacement_reason, bool enforce_payload_policy = true);

    PackageReader reader_;
    PackageManifest manifest_;
    EditPlan edit_plan_;
    std::vector<PackagePartReplacement> replacements_;
    std::vector<PackageEntryReplacement> entry_replacements_;
    std::vector<std::string> omitted_entries_;
    std::vector<std::filesystem::path> temporary_files_;
};

} // namespace fastxlsx::detail
