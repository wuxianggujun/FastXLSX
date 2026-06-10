# FastXLSX Next Steps

## Purpose

This document summarizes what should be pushed next after the current
new-workbook foundations. The current product direction is an editable
high-performance XLSX/OpenXML engine: Streaming for large new workbooks and
large rewrites, Patch for existing-file editing and preservation, and In-memory
for small-file random editing. It is intentionally scoped to project facts that
exist in code, CMake, tests, docs, or local verification.

For executable subtask boundaries, use [TASK_BREAKDOWN.md](TASK_BREAKDOWN.md).
This file gives ordering; the breakdown file gives the per-task input, output,
parallelism, touched files, acceptance checks, and explicit non-goals.

## Current Verified Baseline

- Main local environment: Visual Studio 2026 / MSVC 2026.
- Verified build path: VS2026 Developer Command Prompt + `NMake Makefiles`.
- Local `vcpkg` is available through `VCPKG_ROOT`.
- Local CMake is `3.31.1`. The current recommended and verified generator is
  `NMake Makefiles` from a VS2026 Developer Command Prompt. Other generators
  visible from `cmake --help` are not treated as FastXLSX validation paths.
- Repository engineering entry points now include `vcpkg.json`,
  `CMakePresets.json`, and `.github/workflows/ci.yml`.
- Current CTest entries:
  - `fastxlsx.unit`
  - `fastxlsx.streaming`
  - `fastxlsx.opc`
  - `fastxlsx.worksheet_event_reader`
  - `fastxlsx.worksheet_transformer`
  - `fastxlsx.package_reader`
  - `fastxlsx.package_editor`
  - `fastxlsx.image`
- Current public API:
  - `Workbook`
  - `Worksheet`
  - `Cell`
  - `CellValue`
  - `CellValueKind`
  - `WorkbookWriter`
  - `WorksheetWriter`
  - `CellView`
  - `StyleId`
  - `CellAlignment`
  - `CellFont`
  - `CellFill`
  - `CellStyle`
  - `DataValidationRule`
  - `DataValidationType`
  - `DataValidationOperator`
  - `ArgbColor`
  - `ColorScaleValueType`
  - `ColorScalePoint`
  - `TwoColorScaleRule`
  - `ThreeColorScaleRule`
  - `DataBarValueType`
  - `DataBarEndpoint`
  - `DataBarRule`
  - `IconSetStyle`
  - `IconSetValueType`
  - `IconSetRule`
  - `ImageFormat`
  - `ImageInfo`
  - `ImagePixels`
  - `read_image_info()`
  - `read_image_pixels()`
  - `WorkbookWriter::add_style()`
  - `CellView::with_style()`
  - `WorksheetWriter::add_conditional_color_scale()`
  - `WorksheetWriter::add_conditional_data_bar()`
  - `WorksheetWriter::add_conditional_icon_set()`
  - `FastXlsxError`
- Current internal foundations:
  - XML escape and cell/range/sqref helpers.
  - Minimal workbook writer and streaming writer.
  - Internal worksheet event reader first slice in
    `include/fastxlsx/detail/worksheet_event_reader.hpp` and
    `src/worksheet_event_reader.cpp`, covered by `fastxlsx.worksheet_event_reader`.
    It emits source-order non-owning token views for XML declaration /
    processing instruction / comment, worksheet root, raw metadata,
    `sheetData`, row, cell, raw text separators, cell value wrapper markup, and
    value text boundaries. Treat this as P8
    reader input groundwork only: no public API, no full XML parser/schema
    validation, no relationship repair, no transformer, and no PackageEditor
    stream rewrite handoff.
  - Internal worksheet transformer action-model first slice in
    `include/fastxlsx/detail/worksheet_transformer.hpp` and
    `src/worksheet_transformer.cpp`, covered by `fastxlsx.worksheet_transformer`.
    It maps bounded cell replacement selectors onto event-reader tokens and
    emits source-order `PassThrough` / `ReplaceCell` actions plus missing-target
    diagnostics. It now also has an internal
    `emit_cell_replacement_worksheet()` chunk emitter that forwards pass-through
    source XML chunks and caller replacement cell XML through callback, with a
    narrow payload preflight that requires a `<c>` / `*:c` root and matching
    unqualified `r` attribute before action emission. Treat this as
    action/output-chunk groundwork only: no public API, no package-entry staged
    stream writer, no full cell schema validation, no dependency repair, and no
    PackageEditor/EditPlan commit.
  - Internal bounded PackageEditor cell-replacement handoff in
    `src/package_editor.hpp` and `src/package_editor.cpp`, covered by
    `fastxlsx.package_editor`. `replace_worksheet_cells()` and
    `replace_worksheet_cells_by_name()` materialize the current planned worksheet
    XML, run the P8 chunk emitter, then delegate calcChain/fullCalcOnLoad and audit
    handling to the existing worksheet replacement path. The bounded handoff now
    refreshes the top-level worksheet `<dimension>` from emitted cell refs,
    replacing stale dimension metadata or inserting a missing dimension before
    commit, and hands the dimension-refreshed output to the staged worksheet chunk
    path so the target worksheet is planned as `StreamRewrite`. Invalid
    replacement cell payloads now have PackageEditor-layer no-state-pollution
    coverage for non-cell roots, missing or qualified-only `r` attributes, and
    selector / `r` mismatches. Treat this as a bounded staged-output fixture and
    preflight failure hygiene only: no public API, no low-memory worksheet
    transformer, no broad range metadata recalculation, no sharedStrings/style
    migration, no relationship repair, and no low-memory large-file editing claim.
  - Internal package-entry chunked replacement source foundation in
    `src/package_editor.hpp` and `src/package_editor.cpp`, covered by
    `fastxlsx.package_editor`. `PackageEditor::replace_part_chunks()` records an
    existing package part as a `StreamRewrite` replacement backed by
    `PackageEntryChunk` memory/file chunks, and `save_as()` forwards those chunks
    to `PackageWriter` without flattening them into one string. Treat this as a
    staged package-entry payload foundation only: no public API, no
    cell-replacement low-memory handoff, no full worksheet stream writer, no
    dependency repair, and no relationship/range metadata repair.
  - Internal worksheet replacement chunk handoff in `src/package_editor.hpp` and
    `src/package_editor.cpp`, covered by `fastxlsx.package_editor`.
    `PackageEditor::replace_worksheet_part_chunks()` reuses the current
    materialized worksheet XML validation, dependency audit, relationship audit,
    and calc metadata path, then records the target worksheet payload as
    `PackageEntryChunk` memory/file chunks for `save_as()`. Treat this as a
    worksheet staged payload bridge only: no public API, no low-memory
    validation/audit, no cell-replacement low-memory stream writer, no full worksheet
    stream writer, and no dependency or relationship repair.
  - Internal `CellPosition`, `CellRecord`, and worksheet-local sparse
    `CellStore` in `include/fastxlsx/detail/cell_store.hpp` and
    `src/cell_store.cpp`, plus internal `CellStoreOptions` for first-slice
    `max_cells` / `memory_budget_bytes` enforcement, plus an internal
    `cell_store_to_sheet_data_xml()` helper for standalone `<sheetData>`
    payload emission and a focused by-name `PackageEditor` handoff regression.
    Treat this as a P7 foundation slice only: no public `WorkbookEditor`, no
    random cell editing API, no workbook-level guardrails, and no full save-as /
    Patch handoff.
  - `StringStrategy::SharedString`, internal shared string table wiring,
    `xl/sharedStrings.xml` package entry generation, and focused structure
    tests are visible in the current files. Treat this as sharedStrings
    进行中, not as a production-ready string strategy.
  - Current structure tests also cover the no-string-cell edge: enabling
    `StringStrategy::SharedString` without appending string cells must not emit
    an empty `xl/sharedStrings.xml`, sharedStrings content type, workbook
    relationship, `t="s"`, or `inlineStr`.
  - Basic configurable `docProps/core.xml` and `docProps/app.xml` package wiring
    is visible in the current files through `DocumentProperties`,
    `Workbook::set_document_properties()`, and
    `WorkbookWriterOptions::document_properties`. Treat this as core/app
    metadata for new workbooks, not as `docProps/custom.xml`, existing-file
    editing, or a complete document-properties API.
  - Streaming-only number-format, wrap-text + limited horizontal/vertical alignment,
    bold/italic/direct-color font, and solid fill
    styles are visible through `StyleId`, `CellAlignment`, `CellFont`, `CellFill`, `CellStyle`,
    `WorkbookWriter::add_style()`, `CellView::with_style()`, generated
    `xl/styles.xml`, workbook styles relationship, and focused structure tests.
    Treat this as the styles foundation for custom number formats and narrow
    `wrapText` plus limited horizontal/vertical alignment,
    bold/italic/direct ARGB font metadata, and solid foreground fill, not as full font control,
    full fill/pattern control, border/full alignment, date cell type, dxf-backed conditional
    formatting, rich text, or existing-file style preservation. The current
    two-/three-color color scale, basic data bar, and basic 3Arrows icon set slices are worksheet metadata,
    not styles/dxfs support.
  - Streaming-only two-/three-color conditional color scales, basic data bars,
    and basic 3Arrows icon sets are visible through
    `ArgbColor`, `ColorScaleValueType`, `ColorScalePoint`, `TwoColorScaleRule`,
    `ThreeColorScaleRule`, `DataBarValueType`, `DataBarEndpoint`, `DataBarRule`,
    `IconSetStyle`, `IconSetValueType`, `IconSetRule`,
    `WorksheetWriter::add_conditional_color_scale()`, and
    `WorksheetWriter::add_conditional_data_bar()`, and
    `WorksheetWriter::add_conditional_icon_set()`. Treat this as worksheet-local
    colorScale/dataBar/iconSet metadata only: no `styles.xml`, no `dxfs`, no worksheet relationships,
    no content type overrides, no formula rules,
    and no existing-file editing.
  - Internal `src/package_writer.*` boundary exists for new-workbook package
    output. Default builds delegate to the stored/no-compression
    `src/zip_store_writer.*` bootstrap backend; opt-in minizip builds use
    `minizip-ng[core,zlib]` and DEFLATE.
  - Internal `src/package_reader.*` now has the first `PackageReader` ZIP
    entry reader slice. It indexes and reads stored/no-compression package
    entries by name, including unknown entries. In
    `FASTXLSX_ENABLE_MINIZIP_NG=ON` builds it can also read DEFLATE entries
    through minizip-ng; default builds still reject compressed input. It
    validates decompressed payload CRC, and rejects malformed, duplicate,
    invalid ZIP entry names (absolute paths, trailing slash, backslash, query
    or fragment components, empty segment, dot segment, or parent segment), local header
    CRC/method/name/size mismatch, corrupt metadata/payload, encrypted, data
    descriptor, multi-disk, Zip64, and source-owned `.rels` entries whose owner part is
    absent, including root-level `_rels/foo.xml.rels` owner relationships. It also ingests
    `[Content_Types].xml` and `.rels` small OPC metadata into internal
    `PartIndex` / `RelationshipGraph` views, rejecting conflicting content type
    defaults/overrides, duplicate relationship ids within one `.rels` owner,
    namespaced metadata attributes except namespace declarations,
    duplicate unqualified metadata attributes,
    non-whitespace metadata text,
    start/end tag QName mismatches,
    nested decoy metadata roots where the first real XML element is not
    `Types` / `Relationships`, and metadata declarations hidden under
    unsupported non-root direct-child elements,
    as reader validation rather than content-type or relationship repair. Current reader-only coverage also
    verifies unknown extension owner `.rels` metadata ingestion and
    `RelationshipGraph` attachment without relying on an editor roundtrip. It does not copy unchanged parts
    or write edited packages.
  - Internal `src/package_editor.*` now has the first existing-package
    copy/replace slice. It opens a package through the current `PackageReader`
    boundary, records a
    replacement for an existing part in an `EditPlan`, writes a new package,
    and copies untouched entry bytes including unknown entries. `save_as()`
    rejects exact or path-equivalent writes over the source package because the
    reader-backed copy path is not atomic in-place editing, and it now also
    rejects empty, missing-parent, non-directory-parent, or existing directory output paths before materializing output entries.
    Those guard rejections have no-state-pollution coverage showing queued part
    replacements, structured audit snapshots, calc policy, and removal audits
    remain active in `EditPlan`, manifest, and planned output. The same guard now
    covers queued worksheet replacement with `fullCalcOnLoad` / calcChain
    removal intent: exact/path-equivalent rejection keeps the worksheet rewrite,
    workbook metadata rewrite, calcChain omission, package-entry audit, and
    planned output snapshot active, and a later safe `save_as()` still writes
    the queued rewrite while preserving untouched or unknown bytes. It also has a
    narrow core/app document-properties generated-small-XML path that can add
    missing `docProps/core.xml` / `docProps/app.xml` entries, including when both
    core/app parts are absent, update package relationships and
    `[Content_Types].xml`, and preserve untouched entries.
    It also has a narrow worksheet replacement path that can replace an existing
    `/xl/worksheets/sheetN.xml`, omit stale `xl/calcChain.xml`, remove the
    calcChain content type override, remove the workbook calcChain
    relationship, and set workbook `fullCalcOnLoad="1"` by default. It also
    cleans stale calcChain metadata when the `xl/calcChain.xml` payload is
    absent but a content type override or workbook relationship remains, without
    inventing a removed-part audit or creating the payload. This is not general
    relationship/content-type repair. The full-worksheet replacement payload
    now has a pre-state-change root guard: the replacement XML must be one
    `<worksheet>` root element, with local-name matching for prefixed forms, or
    the helper fails without changing the EditPlan, manifest, package-entry
    audit, calc policy, or output bytes. This is not XML schema validation,
    namespace repair, or XML repair. Successful full-worksheet replacement now
    also audit-scans the replacement payload for shared string indexes, style
    id references, formula cells, range/reference worksheet metadata such as
    sheetPr, sheetCalcPr, dimension, sheetViews, customSheetViews,
    sheetFormatPr, cols, sheetProtection, protectedRanges, sortState,
    autoFilter, mergeCells, scenarios, dataConsolidate, customProperties,
    cellWatches, smartTags, webPublishItems, dataValidations, conditionalFormatting,
    ignoredErrors, printOptions, pageMargins, pageSetup, headerFooter,
    rowBreaks, colBreaks, phoneticPr, and extLst, and relationship-bearing
    worksheet metadata such as hyperlinks, drawing, legacyDrawing, picture,
    legacyDrawingHF, pageSetup `r:id` printerSettings references,
    oleObjects, controls, and tableParts. Those notes and
    structured `WorksheetPayloadDependencyAudit`
    records are copied into `EditPlan` and planned output so callers can review
    `xl/sharedStrings.xml`, `xl/styles.xml`, workbook calc
    metadata, calcChain policy, range/reference metadata, worksheet `.rels`,
    and linked parts; they do not migrate sharedStrings indexes, merge styles,
    evaluate formulas, rebuild calcChain, recalculate dimensions, repair sheet
    views/ranges, or repair relationships. The relationship-id audit also
    compares namespace-qualified `*:id` references from known worksheet elements
    with the preserved worksheet `.rels`, adding notes and structured
    `WorksheetRelationshipReferenceAudit` records for missing worksheet `.rels`,
    missing ids, stale unreferenced ids, and element/type mismatches while
    leaving the preserved `.rels` bytes untouched. The structured audit is
    propagated through internal `EditPlan` and `PackageEditorOutputPlan`; it is
    not namespace validation, relationship pruning, relationship repair, or
    linked-part regeneration. It now treats `*:id` as a worksheet relationship
    reference only when the prefix is bound to the officeDocument relationships
    namespace; alternate prefixes are accepted, while unqualified `id` and
    wrong-namespace `x:id` are ignored. This is still a narrow Patch audit
    scanner, not namespace validation, XML repair, relationship repair/pruning,
    or public API. The current relationship-id audit also treats
    `<pageSetup r:id="...">` as a printerSettings relationship reference,
    records missing id and type-mismatch notes / structured audits, and does not
    synthesize worksheet relationships or printerSettings parts. A regression
    now covers a source package with no
    worksheet `.rels`: a replacement `<drawing r:id="...">` records
    `MissingRelationships`, does not synthesize worksheet relationships on
    output, and still preserves unknown bytes. The source-catalog by-name Patch
    regressions now also cover absolute and dot-segment package
    `officeDocument` targets that resolve to the fixed `/xl/workbook.xml`
    entrypoint plus workbook-owned absolute and dot-segment worksheet targets
    resolving to existing worksheet parts; full worksheet by-name replacement
    and by-name `sheetData` both preserve the worksheet target text and unknown
    extension payload, while calcChain cleanup may still
    rewrite workbook `.rels`. This is still target resolution for the internal
    Patch helper, not arbitrary workbook location support, relationship repair,
    pruning, or a public API.
    The `ReferencePolicyAction::Fail` regression now
    also feeds an audit-heavy full-worksheet replacement payload containing
    shared string indexes, style id references, formula cells, range/reference
    metadata including sheetPr/dimension/sheetViews/sheetFormatPr/cols/
    scenarios/dataConsolidate/customProperties/cellWatches/smartTags/
    webPublishItems/printOptions/pageMargins/pageSetup, hyperlinks, drawing,
    and tableParts, and verifies those
    audit-only payload notes / `WorksheetPayloadDependencyAudit` records do not
    pollute `EditPlan`, relationship target audit state, manifest write modes,
    calc policy, or copied output bytes.
    Under `CalcChainAction::Preserve`,
    worksheet replacement keeps a prior queued ordinary calcChain replacement as
    the final `xl/calcChain.xml` payload while still rewriting workbook
    `fullCalcOnLoad` metadata and copy-original auditing calcChain owner
    relationships. This is not calcChain rebuild, formula evaluation, or
    relationship repair. It also has an internal workbook-only
    `PackageEditor::request_full_calculation()` helper that rewrites only the
    small `/xl/workbook.xml` calc metadata to request `fullCalcOnLoad="1"` and
    can remove or preserve calcChain payload/metadata via
    `CalcChainAction::Remove` or `CalcChainAction::Preserve`. The remove branch
    also cleans metadata-only stale calcChain state when the payload is absent
    but the content type override or workbook relationship remains, without
    creating a payload or removed-part audit. Rebuild is still
    unimplemented and failure leaves edit-plan, manifest, package-entry audit,
    and output state unchanged. This is not formula evaluation, worksheet
    rewriting, public editing API, or general relationship/content-type repair.
    Deflated-source coverage checks unrelated unknown owner `.rels` through
    aggregate `planned_output()` copy-original visibility and output roundtrip
    preservation, not as an edit-plan side-effect audit of the workbook calc
    helper.
    The helper now updates only a `calcPr` that is a direct child of the
    workbook root, preserves nested `extLst` / custom-extension decoys, and
    inserts a root-prefixed direct-child `calcPr` before the real workbook
    closing tag when none exists; this is not XML schema validation,
    namespace repair, or a workbook metadata DOM.
    Narrow sequence regressions now also cover prior ordinary workbook and
    calcChain replacements followed by `request_full_calculation()`: the helper
    consumes queued workbook XML, preserves its non-calc workbook metadata,
    normalizes `fullCalcOnLoad` to `1`, and removes calcChain
    payload/content-type/workbook relationship state without reviving prior
    calcChain replacement bytes or reverting to source workbook bytes.
    Under `CalcChainAction::Preserve`, a prior queued calcChain replacement
    remains the final `xl/calcChain.xml` payload while workbook
    `fullCalcOnLoad` is updated and calcChain owner relationships are
    copy-original audited; this is not calcChain rebuild or formula evaluation.
    A combined
    regression also covers generating core/app docProps and replacing a worksheet
    in one edit, including relationship/content-type state merging, calcChain
    removal, stale calcChain owner `.rels` omission, workbook metadata rewrite,
    unknown-entry preservation, and package-entry edit-plan audit for
    `[Content_Types].xml`, package `_rels/.rels`, workbook `.rels`, and removed
    calcChain owner `.rels`, plus copy-original audit for present preserved source-owned
    `.rels` entries such as ordinary owner-part replacement for root-level
    `_rels/foo.xml.rels`, worksheet/drawing relationships, calcChain owner
    relationships under `CalcChainAction::Preserve`, and workbook `.rels` when
    workbook metadata is rewritten while relationships stay byte-preserved. It does
    not add worksheets or semantically sync sharedStrings/styles/tables/drawings/
    defined names, support Zip64/data descriptors, or expose a public editing API.
    When the source is DEFLATE input, this path preserves unmodified part
    payload semantics. Minizip-enabled PackageEditor regressions now cover
    ordinary workbook replacement, unknown-extension target replacement, and the
    workbook calc metadata helper from a DEFLATE source, plus worksheet
    replacement with calcChain cleanup while
    linked payloads and unknown extension owner `.rels` re-ingest through output
    `PackageReader` / `RelationshipGraph`. It does not preserve source ZIP compression method,
    timestamps, extra fields, or compressed bytes.
    Current structure tests also cover byte preservation for worksheet `.rels`,
    drawing XML, drawing `.rels`, media bytes, chart XML, table XML,
    untouched `xl/sharedStrings.xml`, untouched `xl/styles.xml`, VBA bytes,
    and a reachable unknown extension part plus its owner `.rels` under this
    narrow worksheet replacement path, including when replacement worksheet XML omits source `<drawing>` /
    `<tableParts>` references. A registered comments-part fixture now verifies
    that worksheet rewrite preserves `xl/comments/comment1.xml` and the source
    worksheet `.rels` as copy-original, keeps the comments content type override,
    and roundtrips through `PackageReader` / `RelationshipGraph`. This is not
    comments editing, threaded comments, notes UI, relationship repair, orphan
    cleanup, or public API. A threaded comments / persons fixture now verifies
    that worksheet rewrite preserves `xl/threadedComments/threadedComment1.xml`,
    `xl/persons/person.xml`, the source worksheet `.rels`, and workbook `.rels`
    as copy-original, and roundtrips those relationships through
    `PackageReader` / `RelationshipGraph`. This is not comments / threaded
    comments editing, notes UI, relationship repair, orphan cleanup, or public
    API. The same threaded comments / persons fixture now covers ordinary
    `replace_part("/xl/threadedComments/threadedComment1.xml", ...)` and
    explicit removal: replacement rewrites only threaded comments XML while
    preserving legacy comments, persons, worksheet-owned legacy/threaded inbound
    relationships, the workbook-owned persons relationship, content type
    overrides, and unknown entries; removal omits the threaded comments part and
    its content type override while preserving the inbound worksheet
    relationship pointing at the missing part, the persons part / workbook
    relationship, legacy comments, and unknown entries. This is not threaded
    comments model mutation, persons/schema repair, relationship pruning or
    repair, orphan cleanup, notes UI, or public API. The same fixture now covers
    ordinary `replace_part("/xl/persons/person.xml", ...)` and explicit
    removal: replacement rewrites only persons XML while preserving the
    workbook-owned inbound persons relationship, threaded comments, legacy
    comments, worksheet relationships, content type overrides, and unknown
    entries; removal omits the persons part and removes the persons content type
    override while preserving the workbook relationship pointing at the missing
    part, threaded comments, legacy comments, worksheet, and unknown entries.
    This is not persons/schema repair, threaded comments model mutation,
    relationship pruning or repair, orphan cleanup, notes UI, or public API. A
    same-path ordering regression now covers both the threaded comments part
    and the persons part: later ordinary replacement restores the active part,
    clears stale removed-part audit, returns `[Content_Types].xml` to
    source/copy-original audit, and does not invent the corresponding owner
    `.rels`. The internal threaded-comments ordinary replacement
    `planned_output()` snapshot now exposes the active threaded comments part
    `LocalDomRewrite`, preserved content types / package relationships /
    workbook / workbook `.rels` / worksheet / worksheet `.rels` / legacy
    comments / persons part / unknown entry, and no invented threaded comments
    owner `.rels`. This is Patch audit only, not threaded comments model
    mutation, persons/schema repair, notes UI, relationship repair, orphan
    cleanup, or public API; later threaded-comments remove-then-replace
    `planned_output()` now exposes the active threaded comments part local-DOM
    rewrite, content types copy-original audit, preserved package/workbook/worksheet
    `.rels`, legacy comments, persons part, unknown entry, clears output-plan
    removed_parts / removed_package_entries, and no invented threaded comments
    owner `.rels`. This is Patch audit only, not threaded
    comments undo, semantic merge, relationship repair, orphan cleanup, or public API; later
    threaded-comments removal records removed-part and worksheet
    inbound relationship audit, omits the threaded comments part, removes its
    content type override, and preserves the worksheet inbound relationship,
    persons part / workbook relationship, legacy comments, and unknown entries;
    later persons removal records removed-part and workbook inbound relationship
    audit, omits the persons part, removes the persons content type override,
    and preserves the workbook inbound relationship, threaded comments, legacy
    comments, worksheet, and unknown entries. This is not transactional undo,
    threaded comments/persons semantic merging, persons/schema repair,
    relationship pruning or repair, content type repair, orphan cleanup, notes
    UI, or public API. The internal persons remove-then-replace `planned_output()`
    now exposes the active persons part local-DOM rewrite, content types
    copy-original audit, preserved package/workbook/worksheet `.rels`, threaded
    comments, legacy comments, unknown entry, clears output-plan removed_parts /
    removed_package_entries, and no invented persons owner `.rels`. This is Patch audit only, not persons/schema undo, semantic merge,
    relationship repair, orphan cleanup, or public API. The internal
    `planned_output()` snapshot now also exposes the single omitted threaded
    comments part plus matching removed_parts target/reason/inbound audit,
    worksheet-owned inbound threadedComment relationship metadata, content types
    rewrite, preserved worksheet/workbook `.rels` plus persons part copy-original
    audit, empty removed_package_entries, and no invented threaded comments owner
    `.rels`. The internal output-plan snapshot also exposes the
    single omitted persons part plus matching removed_parts target/reason/inbound
    audit, workbook-owned inbound persons relationship metadata, content types
    rewrite, preserved workbook/worksheet `.rels` plus threaded comments part
    copy-original audit, empty removed_package_entries, and no invented persons owner `.rels`. A
    pivot table / pivot cache fixture now verifies that worksheet rewrite
    preserves `xl/pivotTables/pivotTable1.xml`,
    `xl/pivotCache/pivotCacheDefinition1.xml`,
    `xl/pivotCache/pivotCacheRecords1.xml`, the source worksheet `.rels`,
    pivot table owner `.rels`, pivot cache definition owner `.rels`, and workbook
    `.rels` as copy-original, and roundtrips those relationships through
    `PackageReader` / `RelationshipGraph`. This is not pivot table editing,
    pivot cache rebuild, relationship repair, orphan cleanup, or public API.
    The same worksheet rewrite path now also has an internal `planned_output()`
    snapshot for fullCalcOnLoad / `CalcChainAction::Remove`, worksheet
    `StreamRewrite`, workbook `LocalDomRewrite`, package/workbook/worksheet
    `.rels` copy-original decisions, pivot table / pivot cache definition /
    pivot cache records relationship context, content types and unknown entry
    copy-original preservation, and no invented records owner `.rels`. This is
    Patch audit only, not pivot cache rebuild, records refresh, relationship
    repair/pruning, orphan cleanup, or public API.
    The same pivot table / pivot cache fixture now covers ordinary
    `replace_part("/xl/pivotTables/pivotTable1.xml", ...)` and explicit removal:
    replacement rewrites only pivot table XML while preserving the worksheet-owned
    inbound pivotTable relationship, the pivot-table-owned cache-definition
    relationship, pivot cache definition / records parts, the cache-definition
    owner `.rels`, workbook `<pivotCaches>`, the workbook-owned pivot cache
    relationship, content type overrides, and unknown entries; removal omits the
    pivot table part and its owner `.rels`, removes the pivot table content type
    override, and preserves the inbound worksheet relationship pointing at the
    missing part, workbook pivot cache metadata, the pivot cache definition /
    records chain, and unknown entries. This is not pivot table semantic editing,
    pivot cache rebuild, cache-record refresh, relationship pruning or repair,
    orphan cleanup, owner `.rels` repair, or public API.
    The same path now covers pivot table same-path ordering too: a later
    ordinary replacement restores the active pivot table part, clears stale
    removed-part and removed owner `.rels` audit, restores owner `.rels`
    copy-original audit, and returns `[Content_Types].xml` to source/copy-original
    audit; a later explicit removal clears the active replacement, records
    removed-part and removed owner `.rels` audit, omits the pivot table part and
    owner `.rels`, removes the pivot table content type override, and preserves
    the inbound worksheet relationship pointing at the missing part, workbook
    pivot cache metadata, the pivot cache definition / records chain, and unknown
    entries. This is not transactional undo, pivot table semantic merging,
    pivot cache rebuild, relationship pruning or repair, content type repair,
    orphan cleanup, or public API.
    Internal `planned_output()` coverage for the remove-then-replace restore
    state now exposes the active pivot table `LocalDomRewrite` entry, the pivot
    table owner `.rels` copy-original `SourceRelationships` audit, the
    source/copy-original content types audit, and preserved package/worksheet/
    workbook relationships, the pivot cache definition / records chain, and
    unknown entries. Coverage for the replace-then-remove final-removal state
    still exposes the omitted pivot table part, omitted owner `.rels`, worksheet
    inbound pivotTable relationship audit, content types rewrite, preserved
    worksheet/workbook relationships, the pivot cache definition / records
    chain, and unknown entries. This is Patch audit only, not pivot table
    semantic editing, pivot cache rebuild, relationship pruning or repair,
    orphan cleanup, or public API.
    The same fixture now covers ordinary
    `replace_part("/xl/pivotCache/pivotCacheDefinition1.xml", ...)` and explicit
    removal: replacement rewrites only pivot cache definition XML while preserving
    workbook/pivot-table inbound relationships, pivot cache records, the
    cache-definition owner `.rels`, content type overrides, and unknown entries;
    removal omits the pivot cache definition part and its owner `.rels`, removes
    the cache definition content type override, and preserves workbook/pivot-table
    inbound relationships, the pivot table, pivot cache records, the worksheet,
    and unknown entries. This is not pivot cache rebuild, cache-record refresh,
    relationship pruning or repair, orphan cleanup, owner `.rels` repair, or
    public API.
    The same path now covers pivot cache definition same-path ordering too:
    a later ordinary replacement restores the active cache definition, clears
    stale removed-part and removed owner `.rels` audit, restores owner `.rels`
    copy-original audit, and returns `[Content_Types].xml` to source/copy-original
    audit; a later explicit removal clears the active replacement, records
    removed-part and removed owner `.rels` audit, omits the cache definition part
    and owner `.rels`, and preserves workbook / pivot table inbound relationships
    plus the pivot table, cache records, worksheet, and unknown entries. This is
    not transactional undo, pivot cache semantic merging, relationship pruning
    or repair, content type repair, orphan cleanup, or public API. Internal
    `planned_output()` coverage for the remove-then-replace restore state now
    exposes the active pivot cache definition `LocalDomRewrite` entry, the owner
    `.rels` copy-original `SourceRelationships` audit, the source/copy-original
    content types audit, and preserved package/worksheet/workbook relationships,
    pivot table/cache records, and unknown entries. Coverage for the
    replace-then-remove final-removal state still exposes the omitted cache
    definition part, omitted owner `.rels`, workbook / pivot table inbound
    pivotCacheDefinition relationship audit, content types rewrite, and preserved
    workbook/worksheet/pivot table/cache records/unknown entries. This is Patch
    audit only, not pivot cache rebuild, cache-record refresh, relationship
    pruning or repair, content type repair, orphan cleanup, or public API.
    The same fixture now covers ordinary
    `replace_part("/xl/pivotCache/pivotCacheRecords1.xml", ...)` and explicit
    removal: replacement rewrites only pivot cache records XML while preserving
    the cache-definition-owned inbound relationship, pivot cache definition,
    pivot table, workbook / worksheet relationships, content type overrides,
    and unknown entries; removal omits the pivot cache records part, removes the
    records content type override, and preserves the cache-definition-owned
    inbound relationship pointing at the missing records part, pivot cache
    definition, pivot table, workbook, worksheet, and unknown entries. This is
    not pivot cache records refresh, pivot cache rebuild, relationship pruning
    or repair, orphan cleanup, or public API.
    The same path now covers pivot cache records same-path ordering too:
    a later ordinary replacement restores the active pivot cache records part,
    clears stale removed-part audit, returns `[Content_Types].xml` to
    source/copy-original audit, and does not invent a records owner `.rels`;
    a later explicit removal clears the active replacement, records removed-part
    and cache-definition inbound relationship audit, omits the records part,
    removes the records content type override, and preserves the
    cache-definition owner `.rels` inbound relationship pointing at the missing
    records part, pivot cache definition, pivot table, workbook, worksheet, and
    unknown entries. This is not transactional undo, pivot cache records
    semantic merging, relationship pruning or repair, content type repair,
    orphan cleanup, or public API. Internal `planned_output()` coverage for the
    remove-then-replace restore state now exposes the active pivot cache records
    `StreamRewrite` entry, source/copy-original content types audit, preserved
    package/worksheet/workbook relationships, pivot table/cache definition chain,
    unknown entries, and no invented records owner `.rels`. Coverage for the
    replace-then-remove final-removal state still exposes the omitted records
    part, cache-definition inbound pivotCacheRecords relationship audit, content
    types rewrite, preserved cache definition owner `.rels`, and no invented
    records owner `.rels`. This is Patch audit only, not pivot cache records
    refresh, pivot cache rebuild, relationship pruning or repair, content type
    repair, orphan cleanup, or public API.
    A workbook external links fixture now verifies that worksheet rewrite,
    while rewriting `xl/workbook.xml` calc metadata, preserves workbook
    `<externalReferences>`, the workbook `.rels` externalLink relationship,
    `xl/externalLinks/externalLink1.xml`, the externalLink owner `.rels`, the
    external `externalLinkPath` target, the content type override, and an
    unknown entry, and roundtrips those relationships through `PackageReader` /
    `RelationshipGraph`. This is not external links editing, external data
    refresh, path validation, relationship repair, orphan cleanup, or public API.
    The worksheet rewrite `planned_output()` snapshot now also exposes the
    fullCalcOnLoad request, `CalcChainAction::Remove`, the worksheet
    `StreamRewrite`, the workbook `LocalDomRewrite`, workbook `.rels`
    copy-original preservation, the externalLink part plus owner `.rels`
    copy-original preservation, content types copy-original preservation, and
    unknown entry preservation, without adding relationship target audits. This
    remains Patch audit only, not external links editing or relationship repair.
    The same workbook external links fixture now covers ordinary
    `replace_part("/xl/externalLinks/externalLink1.xml", ...)` and explicit
    removal: replacement rewrites only externalLink XML while preserving the
    workbook-owned inbound externalLink relationship, the externalLink-owned
    external `externalLinkPath` target, the content type override, the
    worksheet, and unknown entries; removal omits the externalLink part and its
    owner `.rels`, removes the externalLink content type override, and preserves
    workbook `<externalReferences>`, the inbound workbook relationship pointing
    at the missing part, the worksheet, and unknown entries. This is not
    external links semantic editing, external data refresh, path validation,
    relationship pruning or repair, orphan cleanup, owner `.rels` repair, or
    public API.
    The same externalLink path now covers remove-then-ordinary-replace and
    ordinary-replace-then-remove ordering. The restore path clears stale
    removed-part / removed owner `.rels` audit, restores the active externalLink
    part, restores the owner `.rels` copy-original audit, and returns content
    types to source/copy-original audit. The final-removal path clears the active
    replacement, records removed-part / removed owner `.rels` audit, omits the
    externalLink part and owner `.rels`, and preserves the workbook inbound
    relationship, worksheet, and unknown entries. This is not transactional undo,
    external links semantic merge, relationship pruning or repair, content type
    repair, orphan cleanup, or public API.
    Internal `planned_output()` coverage for the remove-then-replace restore
    state now exposes the active externalLink `LocalDomRewrite` entry, the
    externalLink owner `.rels` copy-original `SourceRelationships` audit, the
    source/copy-original content types audit, and preserved package/workbook
    relationships, workbook, worksheet, and unknown entries. Coverage for the
    replace-then-remove final-removal state still exposes the omitted
    externalLink part, omitted owner `.rels`, workbook inbound externalLink
    relationship audit, content types rewrite, and preserved package/workbook
    relationships, workbook, worksheet, and unknown entries. This is Patch
    audit only, not external links semantic editing, external data refresh,
    relationship pruning or repair, orphan cleanup, or public API.
    A custom XML fixture now verifies that worksheet rewrite preserves the
    package `_rels/.rels` customXml relationship, `customXml/item1.xml`, the
    custom XML item owner `.rels`, `customXml/itemProps1.xml`, the custom XML
    properties content type override, and an unknown entry, and roundtrips those
    relationships through `PackageReader` / `RelationshipGraph`. This is not
    custom XML editing, schema/data binding, relationship repair, orphan cleanup,
    or public API.
    The worksheet rewrite `planned_output()` snapshot now also exposes the
    fullCalcOnLoad request, `CalcChainAction::Remove`, the worksheet
    `StreamRewrite`, the workbook `LocalDomRewrite`, package relationship
    copy-original preservation, custom XML item / item-owner `.rels` /
    properties part copy-original preservation, content types copy-original
    preservation, and unknown entry preservation, without adding relationship
    target audits or inventing properties owner `.rels`. This is Patch audit
    only, not custom XML editing, schema/data binding, or relationship repair.
    The same custom XML fixture now covers ordinary
    `replace_part("/customXml/item1.xml", ...)`: only the custom XML item is
    rewritten while the package `_rels/.rels` customXml inbound relationship,
    the custom XML item owner `.rels` / customXmlProps relationship,
    `customXml/itemProps1.xml`, the custom XML properties content type override,
    the default XML content type, and the unknown entry stay on the copy-original
    baseline and roundtrip through `PackageReader` / `RelationshipGraph`. This is
    not custom XML semantic editing, schema/data binding, relationship repair,
    content type repair, orphan cleanup, or public API.
    The same custom XML fixture now covers explicit `customXml/item1.xml`
    removal: output omits the custom XML item and its source-owned owner `.rels`,
    preserves the package `_rels/.rels` customXml inbound relationship, preserves
    `customXml/itemProps1.xml`, the custom XML properties content type override,
    the default XML content type, and the unknown entry, and does not rewrite
    `[Content_Types].xml`. This is not custom XML deletion semantics,
    schema/data binding, relationship pruning or repair, content type repair,
    orphan cleanup, or public API.
    The same custom XML path now has ordering regressions for remove-then-
    ordinary-replace and ordinary-replace-then-remove. The restore path clears
    stale removed-part and removed owner `.rels` audit, restores the active
    custom XML item and owner `.rels` copy-original audit, and still avoids
    rewriting `[Content_Types].xml`. The final-removal path clears the active
    replacement, records removed-part and removed owner `.rels` audit, omits the
    custom XML item and owner `.rels`, and preserves the package inbound
    relationship, properties part, default XML content type, and unknown entry.
    Internal `planned_output()` coverage for this restore state now exposes the
    active custom XML item `LocalDomRewrite` entry, owner `.rels` copy-original
    `SourceRelationships` audit, and preserved package relationships, content
    types, workbook, worksheet, properties part, and unknown entry. Internal
    `planned_output()` coverage for this final-removal state still exposes the
    omitted custom XML item, omitted source-owned owner `.rels`, package inbound
    customXml relationship audit, and preserved package relationships, content
    types, workbook, worksheet, properties part, and unknown entry.
    This is not transactional undo, custom XML semantic merge, relationship
    pruning or repair, content type repair, orphan cleanup, or public API.
    The same custom XML fixture now covers ordinary replacement and explicit
    removal of `customXml/itemProps1.xml`. Replacement only rewrites the
    properties part and preserves the custom XML item, item-owned `.rels` /
    customXmlProps inbound relationship, package customXml relationship,
    properties content type override, and unknown entry. Removal omits the
    properties part and removes the properties content type override, but keeps
    the custom XML item, the item-owned `.rels` inbound customXmlProps
    relationship pointing at the missing properties part, the package customXml
    relationship, default XML content type, and unknown entry. This is not
    custom XML properties editing, schema/data binding, relationship pruning or
    repair, content type repair, orphan cleanup, or public API.
    Internal `planned_output()` coverage for the ordinary replacement state now
    exposes the active properties part `LocalDomRewrite`, preserved content
    types / package relationships, preserved custom XML item / item owner
    `.rels` / workbook / worksheet / unknown entry, and no invented properties
    owner `.rels`. This is Patch audit only, not custom XML properties semantic
    editing, schema/data binding, relationship pruning or repair, content type
    repair, orphan cleanup, transactional undo, or public API.
    The same properties-part path now has ordering regressions for
    remove-then-ordinary-replace and ordinary-replace-then-remove. The restore
    path clears stale removed-part audit, restores the active properties part,
    restores the properties content type override/content-types copy-original
    audit, and keeps the item-owned `.rels`. The final-removal path clears the
    active replacement, records removed-part audit, omits the properties part,
    removes the properties content type override, and keeps the inbound
    customXmlProps relationship in the item-owned `.rels`. This is not
    transactional undo, custom XML properties semantic merge, relationship
    pruning or repair, content type repair, orphan cleanup, or public API.
    Internal `planned_output()` coverage for this properties final-removal state
    now exposes the omitted properties part, item-owned inbound customXmlProps
    relationship audit, content types rewrite, preserved custom XML item / item
    owner `.rels` / package relationships / workbook / worksheet / unknown entry,
    and no invented properties owner `.rels`. This is Patch audit only, not
    custom XML properties deletion semantics, relationship pruning or repair,
    content type repair, orphan cleanup, or public API.
    Internal `planned_output()` coverage for the restore state now exposes the
    active properties part `LocalDomRewrite`, restored content types
    copy-original audit, preserved custom XML item / item owner `.rels` /
    package relationships / workbook / worksheet / unknown entry, and no
    invented properties owner `.rels`. This is Patch audit only, not custom XML
    properties semantic merge, relationship pruning or repair, content type
    repair, orphan cleanup, transactional undo, or public API.
    The same custom XML fixture now covers cross-path ordering where
    `customXml/item1.xml` is removed before `customXml/itemProps1.xml` is
    ordinary-replaced. The later properties replacement only rewrites the
    properties payload, keeps the custom XML item and item-owned `.rels` removal
    audits, continues to omit the item and owner `.rels` in output, and preserves
    the package customXml inbound relationship, properties content type override,
    default XML content type, and unknown entry. This is not custom XML
    dependency repair, relationship pruning or repair, content type repair,
    orphan cleanup, transactional undo, or public API.
    Internal `planned_output()` coverage for this cross-path state now exposes
    the omitted custom XML item, omitted source-owned owner `.rels`, package
    inbound customXml relationship audit, active properties part local-DOM rewrite,
    preserved package relationships / content types / workbook / worksheet /
    unknown entry, and no invented properties owner `.rels`. This is Patch audit
    only, not custom XML dependency repair, relationship pruning or repair,
    content type repair, orphan cleanup, transactional undo, or public API.
    The reverse cross-path ordering is now covered too: `customXml/itemProps1.xml`
    is removed before `customXml/item1.xml` is ordinary-replaced. The later item
    replacement only rewrites the item payload, keeps the removed properties-part
    audit/content-types rewrite, continues to omit the properties part and its
    content type override, and preserves the customXmlProps relationship in the
    item-owned `.rels`, the package customXml inbound relationship, default XML
    content type, and unknown entry. This is not custom XML dependency repair,
    relationship pruning or repair, content type repair, orphan cleanup,
    transactional undo, or public API.
    Internal `planned_output()` coverage for this reverse cross-path state now
    exposes the omitted properties part, item-owned inbound customXmlProps
    relationship audit, content types rewrite, active custom XML item local-DOM
    rewrite, preserved item owner `.rels` / package relationships / workbook /
    worksheet / unknown entry, and no invented properties owner `.rels`. This is
    Patch audit only, not custom XML dependency repair, relationship pruning or
    repair, content type repair, orphan cleanup, transactional undo, or public API.
    The same fixture now covers ordinary
    `replace_part("/xl/comments/comment1.xml", ...)`: only comments XML is
    rewritten while the inbound worksheet `.rels` comments relationship,
    comments content type override, workbook XML / workbook `.rels`, worksheet, and
    unknown entry stay on the copy-original baseline, without inventing comments
    owner `.rels`. This is not comments model mutation, threaded comments, notes
    UI, relationship repair, orphan cleanup, or public API. Internal
    `planned_output()` coverage for this ordinary replacement state now exposes
    the active comments part local-DOM rewrite, preserved content types /
    package relationships / workbook / workbook `.rels` / worksheet / worksheet
    `.rels` / unknown entry, and no invented comments owner `.rels`. This is
    Patch audit only, not comments model mutation, notes UI, relationship
    repair, orphan cleanup, or public API. The same fixture now covers explicit
    `xl/comments/comment1.xml` removal: output omits the comments part, removes
    the comments content type override, preserves the inbound worksheet `.rels`
    comments relationship, and does not invent comments owner `.rels` omission.
    This is not comments deletion semantics, threaded comments, notes UI,
    relationship pruning/repair, orphan cleanup, or public API. The same fixture
    also covers remove-then-ordinary-replace ordering: a later `replace_part()`
    restores the active comments replacement, clears stale removed-part audit,
    returns `[Content_Types].xml` to source/copy-original audit, preserves inbound
    worksheet `.rels`, and still does not invent comments owner `.rels`. This is
    not transactional undo, comments semantic merge, relationship repair, orphan
    cleanup, or public API. Internal `planned_output()` coverage for this
    remove-then-replace state now exposes the active comments part local-DOM
    rewrite, content types copy-original audit, preserved
    package/workbook/worksheet `.rels` and unknown entry, clears output-plan
    removed_parts / removed_package_entries, and no invented comments owner
    `.rels`. This is Patch audit only, not comments undo,
    semantic merge, relationship repair, orphan cleanup, or public API. It now
    also covers replace-then-remove ordering: a
    later explicit removal clears the active comments replacement, records
    removed-part audit, omits the comments part, removes the comments content
    type override, preserves inbound worksheet `.rels`, and still does not
    invent comments owner `.rels`. This is not comments deletion semantics,
    transactional undo, relationship pruning/repair, orphan cleanup, or public
    API. The internal `planned_output()` snapshot now also exposes the single
    omitted comments part plus matching removed_parts target/reason/inbound
    audit, worksheet-owned inbound comments relationship metadata, content types
    rewrite, preserved package/workbook/worksheet `.rels` copy-original audit,
    empty removed_package_entries, and no invented comments owner `.rels`.
    They also cover workbook `definedNames`
    preservation during workbook metadata rewrite and
    narrow `ReferencePolicy` boundaries for linked-object failure,
    calcChain preserve, rebuild rejection, malformed workbook metadata preflight failure,
    missing `xl/workbook.xml` worksheet-rewrite precondition failure,
    request-recalculation fullCalcOnLoad output, and core/app docProps package
    relationship target conflicts failing without edit-plan entries/notes, manifest,
    package-entry audit, or copied-output pollution. A queued core/app docProps
    metadata edit is also preserved when a later linked worksheet rewrite fails
    under `ReferencePolicyAction::Fail`.
    This does not make image/chart/table/VBA passthrough complete.
  - Internal OPC `PartName`, `RelationshipSet`, `ContentTypesManifest`,
    `PackageManifest`, `PartWriteMode`, package-part edit state metadata,
    minimal workbook manifest builder, and content types / relationships
    serializers, including the docProps small XML builders.
  - Numeric XML output now has explicit boundaries: non-finite numeric cell
    values are rejected before serialization writes invalid worksheet XML;
    in-memory and streaming row heights must be positive and finite; streaming
    column widths must be positive and finite.
  - A 2026-06-07 local manual benchmark snapshot exists for sharedStrings:
    `strings`, `50000 x 10 x 1 = 500000` cells, repeated/unique string patterns,
    inline/shared string strategies, stored-bootstrap ZIP, plus separate local
    Excel COM read-only open checks for all four generated files.
  - A 2026-06-10 schema-v4 local benchmark matrix also exists for sharedStrings:
    `strings`, `10000 x 10 x 1 = 100000` cells per case, repeated/unique
    string patterns, inline/shared strategies, stored-bootstrap ZIP, and
    `openpyxl` read-only checks for all four generated files.
- Local Excel visual verification has been performed for:
  - `build/windows-nmake-release/tests/fastxlsx-phase1-minimal.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-smoke.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-shared-strings.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-data-validations.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-external-hyperlinks.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-internal-hyperlinks.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-hyperlink-display-tooltips.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-styles-number-formats.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-styles-shared-strings.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-styles-fonts.xlsx`
  Manual `build-nmake` output may exist locally, but treat it as potentially
  stale unless it was regenerated after the current source change.

## Current Round Focus

Status words in this section are intentionally limited to 计划, 进行中, and
基础. They describe visible project state and next work only; they do not claim
feature completion.

1. Phase plan reset - 基础.
   - The next queue should no longer say "P5 sharedStrings -> P6 benchmark ->
     P7 hot path" as the default lane.
   - Current priority is split into executable steps: P3 package read/copy/write
     foundation, P4.0 API surface unification, then a narrow P4 Patch MVP.
     Writer/backend performance hardening continues as supporting work.
   - Keep docs, AGENTS, skills, and `TASK_BREAKDOWN.md` aligned with this
     positioning and keep roadmap symbols separate from implemented code.

2. Editing architecture foundation - 计划 with internal groundwork 基础.
   - Current internal OPC metadata has part names, relationships, content types,
     package parts, write-mode planning states, internal `PartIndex`,
     internal `RelationshipGraph`, a content type registry helper, and the first
     internal `EditPlan` / `DependencyAnalyzer` / `ReferencePolicy` /
     `PartRewritePlanner` planning slice.
   - Next work should build on the internal stored-entry / metadata-ingestion
     `PackageReader` slice and the internal `PackageEditor` copy/replace slice
     by keeping the internal/public boundary clear before adding relationship /
     content type mutation and calc policy in a narrow Patch MVP.
   - Do not expose complete existing-file editing until preservation tests prove
     unknown and unmodified parts survive.

3. API surface unification - 基础.
   - The public facade vocabulary is now frozen for current planning:
     `WorkbookWriter` for Streaming, `Workbook` for small new-workbook
     creation, and future `WorkbookEditor` / `WorksheetEditor` for
     existing-file editing.
   - Keep low-level `PackageReader`, `PackageEditor`, `EditPlan`,
     `PartIndex`, and `RelationshipGraph` internal unless a later task proves a
     stable low-level public API is needed.
   - Define the `CellView` / `Cell` / `CellValue` split before adding
     random cell APIs.

4. In-memory small-file editing - 计划.
   - Add a separate random editing surface for small workbooks after the Patch
     save contract is clear.
   - It may use cell maps or local DOM where appropriate, but must document
     memory growth and must not become the large-data default path.

5. Writer/backend hardening - 进行中 / 基础.
   - sharedStrings remains 进行中: keep `inlineStr` as the low-memory default,
     and expand benchmark/reference evidence before widening support wording.
   - vcpkg / CMakePresets / CI remains 基础: `stb` is default, minizip is opt-in,
     Excel visual verification remains local, and benchmark jobs stay opt-in.

6. Styles number formats + limited alignment + bold/italic/direct-color fonts + solid fills - 基础.
   - Current files show workbook-local `StyleId`, `CellAlignment`, `CellFont`, `CellFill`, `CellStyle`,
     `WorkbookWriter::add_style()`, `CellView::with_style()`, generated
     `xl/styles.xml`, workbook styles relationship, and focused CTest coverage.
   - The current slices support custom number formats, narrow
     `CellAlignment::wrap_text`, limited `HorizontalAlignment` / `VerticalAlignment`,
     narrow `CellFont::bold` / `italic` plus optional direct ARGB `color`, and narrow
     `CellFill` solid foreground ARGB.
     Duplicate complete styles reuse the same style id; equal number-format
     strings reuse the same custom `numFmtId` across different style
     combinations; equal bold/italic/direct-color font combinations reuse the same `fontId`;
     equal foreground ARGB fills reuse the same `fillId`;
     default cells omit `s="0"`.
   - Current local QA uses `tools/verify_styles_number_formats.py` for
     package XML / `openpyxl` / optional `XlsxWriter` checks and
     `tools/verify_styles_excel.ps1` for Excel COM read-only visible checks,
     including `fastxlsx-streaming-styles-fonts.xlsx` bold, italic, direct
     color, number+color, and default-cell scenarios.
   - Before expanding support wording, add separate tasks and tests for
     full font control, full fill/pattern control, borders/full alignment, date serial helper policy, conditional
     formatting, rich text, and existing-file style preservation.

## Repository State

- Local Git repository initialized on branch `main`.
- Public GitHub repository created and pushed:
  `https://github.com/wuxianggujun/FastXLSX`
- `origin/main` is configured as the upstream branch.

## Recommended Push Order

Use this order for the next implementation pushes. Each item should be a small
commit or short series with its own tests and docs update.

1. Task docs, skills, and branch/worktree context.
   - Align `TASK_PLAN.md`, `TASK_BREAKDOWN.md`, `NEXT_STEPS.md`, `ROADMAP.md`,
     `AGENTS.md`, and FastXLSX skills with the editable-engine positioning.
   - Reconcile concurrent agent output before overlapping file edits.
   - Keep `TECHNICAL_COMPARISON.md` as the cross-language XLSX reference
     matrix. Feature tasks should name the reference libraries they borrow from
     and the architecture limits they intentionally avoid.
   - Keep generated artifacts, local build outputs, and private state out of
     commits.

2. Package / Patch architecture foundation.
   - Specify the current internal `PackageReader` stored-entry /
     metadata-ingestion contract plus `PackageEditor`, `EditPlan`,
     `DependencyAnalyzer`, `ReferencePolicy`, and `PartRewritePlanner` as the
     Patch architecture targets.
   - Define Patch write modes: copy original, generate small XML, stream
     rewrite, and local DOM rewrite, plus removed-part audit for explicit removals.
   - Define dependency analysis for sheet edits before public edit APIs land.

3. API surface unification design.
   - Keep the frozen public facade naming and shared value-type vocabulary as
     the gate before widening Patch or In-memory APIs.
   - Keep `PackageReader`, `PackageEditor`, `EditPlan`, `PartIndex`, and
     `RelationshipGraph` as internal Patch foundation unless a separate task
     proves a stable low-level public API is needed.
   - Define `CellView` / `Cell` / `CellValue` boundaries and consistent
     method names such as `add_worksheet`, `worksheet`, `append_row`,
     `set_cell`, `save`, and `save_as`.

4. Patch MVP.
   - Open an existing workbook, build a part index and relationship graph, copy
     unchanged parts, and rewrite one targeted sheet or small metadata part.
   - Sync relationships/content types, preserve unknown parts, and document
     `calcChain.xml` plus `fullCalcOnLoad` behavior.

5. Preservation fixtures.
   - Use template workbooks with drawings/images, charts, macros,
     sharedStrings/styles, workbook definedNames, and unknown extension parts.
   - Edit unrelated parts and compare input/output packages before claiming
     safe existing-file editing.

6. In-memory small-file editor.
   - Add random cell/sheet editing for small workbooks after the Patch save
     contract is clear.
   - Document memory growth, size limits, and when callers should choose
     Streaming or Patch instead.

7. Sheet-local dependency handling.
   - Add conservative policies for tables, hyperlinks, validations, merged
     cells, auto filters, drawings/images, styles, sharedStrings, defined names,
     formulas, workbook calc metadata, and `calcChain.xml`.
   - Unsupported linked edits should preserve, request recalculation, or fail
     explicitly.

8. Writer/backend performance hardening.
   - Continue opt-in minizip hardening, compression-level policy, Zip64 policy,
     sharedStrings measurements, benchmark groundwork, and streaming hot-path
     work as supporting lanes.
   - Do not use these lanes to postpone the Patch MVP indefinitely.

9. Existing streaming-only feature slices.
   - Keep data validations, hyperlinks, tables, conditional formatting, styles,
     document properties, and images accurately documented as new-workbook-only
     where that is still true.
   - Add Patch behavior for these features only after preservation and
     dependency analysis are proven.

10. Complex object preservation before generation.
   - Chart/VBA/image/drawing work starts with passthrough preservation for
     existing workbooks.
   - Native generation/editing is a later feature-specific task.

11. Release packaging.
    - Start only after the selected public surface has code, tests, docs, local
      validation, and CI evidence.

## Push-by-Push Execution Queue

Use the authoritative order below when deciding what to implement next. The
feature-specific sections after P1 keep detailed current facts and validation
notes, but their old numeric labels are no longer the priority order. Pick the
first item whose start condition is true. Do not treat later items as supported
until their acceptance checks have passed and the docs have been updated.

Authoritative execution order:
1. P0 - task docs, AGENTS, skills, and concurrent-session context.
2. P1 - CI and local build hygiene when workflow or preset paths drift.
3. P2 - editing architecture contract: internal `EditPlan`,
   `DependencyAnalyzer`, `ReferencePolicy`, `PartRewritePlanner`, and part
   write modes plus package-entry audit metadata are now a planning foundation;
   `PackageReader` has a stored ZIP
   entry reader plus metadata-ingestion foundation, while internal
   `PackageEditor` has a copy/replace foundation plus narrow docProps,
   worksheet replacement, exact/path-equivalent source-overwrite rejection, and
   empty-output / missing-parent / non-directory-parent / existing-directory output rejection
   regressions, plus malformed workbook metadata/calc metadata and invalid
   replacement no-state-pollution coverage for edit-plan entries/notes,
   structured payload/removal/calc-policy snapshots, aggregate
   `planned_output()` / legacy output-entry preview, manifest
   write-mode, copied-output hygiene, and
   queued docProps preservation across linked worksheet policy failure.
4. P3 - package read/copy/write foundation: harden existing ZIP/package entry
   reading plus `PartIndex` / `RelationshipGraph` ingestion and copy/replace
   behavior.
5. P4.0 - API surface unification design: freeze the public facade naming,
   shared value types, and internal/public boundary before widening Patch or
   In-memory APIs.
6. P4 - Patch MVP: targeted sheet or small-part rewrite, relationship/content
   type sync, unknown part preservation, and calc policy.
7. P5 - preservation fixture set for images/drawings, charts, sharedStrings/styles,
   workbook definedNames, VBA, and unknown extensions.
8. P6 - sheet dependency analyzer and conservative reference policies.
9. P7 - In-memory small-file editor with documented size/memory limits.
10. P8 - controlled large worksheet editing: replace sheet, range patch,
   template fill, event reader -> transformer -> stream writer.
11. P9 - production ZIP/backend and package writer hardening.
12. P10 - sharedStrings hardening and memory/size evidence.
13. P11 - benchmark groundwork.
14. P12 - streaming writer hot-path work.
15. P13 - Phase 3 metadata/styles hardening.
16. P14+ - existing streaming-only object slices and later existing-file object
   editing, gated by P3-P6 preservation and dependency analysis.

### P0 - Task Docs and Agent Context

Start when task order, validation commands, or implementation status becomes
ambiguous.

Do:
- Keep `TASK_PLAN.md`, `TASK_BREAKDOWN.md`, `NEXT_STEPS.md`, `AGENTS.md`,
  `README.md`, and project skills aligned with current code.
- Prefer `build/windows-nmake-release` preset artifacts for fresh validation.
- Record stale manual build directories as local-only, not canonical outputs.

Accept when:
- `git diff --check` passes.
- Edited skills pass `quick_validate.py`.
- Referenced files, commands, and sample paths exist or are explicitly marked
  as planned/local-only.

Do not claim:
- New runtime behavior, new API support, or production readiness from doc-only
  edits.

### P1 - CI and Local Build Hygiene

Start after P0, or whenever CI runner labels, CMake presets, or timeout behavior
drift.

Do:
- Recheck `windows-2025-vs2026` and the GitHub runner image status after the
  2026-06-08 migration window starts.
- Update `.github/workflows/ci.yml` only after verifying the replacement runner
  or action version.
- Keep checkout on `actions/checkout@v5` unless GitHub Actions compatibility
  evidence requires another version.
- Keep default CI on the vcpkg-backed `windows-nmake-release` preset.
- Keep optional backend CI in a separate vcpkg preset job for
  `windows-nmake-release-minizip`.

Accept when:
- VS2026/NMake configure, build, and `ctest --preset windows-nmake-release`
  pass locally.
- GitHub Actions runs the same preset path and passes.
- GitHub Actions vcpkg paths pass for default `stb` and opt-in minizip when
  workflow or dependency paths are touched.
- Ordinary tests remain protected by the 60s CTest preset/test properties.
- No Node.js 20 deprecation annotation appears for checkout.

Do not claim:
- vcpkg dependency readiness or Excel visual validation from CI alone.
- full image/minizip production readiness from opt-in CI alone.

### P4.0 - API Surface Unification Design

Start after P3 has enough package read/copy/write facts to describe the Patch
boundary, and before exposing or broadening public existing-file edit APIs.

Do:
- Define the three public-facing facades:
  `WorkbookWriter` for large new-workbook streaming export,
  `Workbook` for small new-workbook convenience creation, and future
  `WorkbookEditor` / `WorksheetEditor` for existing-file editing and small-file
  random edit workflows.
- Keep `PackageReader`, `PackageEditor`, `EditPlan`, `DependencyAnalyzer`,
  `PartIndex`, and `RelationshipGraph` internal until a later task explicitly
  proves a stable low-level public API is needed.
- Define shared public value types and reuse rules for `CellRange`, `StyleId`,
  `CellStyle`, `DocumentProperties`, `HyperlinkOptions`, `ImageOptions`, and
  `CellValue`.
- Define the `Cell` / `CellView` / `CellValue` split:
  `CellView` is streaming-only and non-owning; `Cell` is an owning convenience
  value for small new workbooks; `CellValue` is the owning semantic value shared
  by future editor and in-memory APIs; `CellRecord` / `CellStore` now have a
  first internal sparse-store slice and remain non-public implementation
  details.
- Add an API matrix that maps common concepts across Streaming, simple
  new-workbook creation, Patch, and In-memory paths: add worksheet, get
  worksheet, append row, set cell, save, save-as, style, range, and error
  behavior.
- Record naming rules before implementation tasks add new methods: prefer
  `add_worksheet`, `worksheet`, `append_row`, `set_cell`, `save`, and `save_as`
  consistently; avoid public synonyms that mean the same thing.

Accept when:
- `docs/API_DESIGN_AND_DOCUMENTATION.md`, `docs/ARCHITECTURE.md`,
  `docs/TASK_PLAN.md`, and `docs/TASK_BREAKDOWN.md` agree on the public
  facades and internal/public boundary.
- Future Patch MVP tasks know whether a new API belongs to `WorkbookWriter`,
  `Workbook`, or future `WorkbookEditor`.
- The docs explicitly say which current Patch classes are internal and which
  names are only future public design targets.

Do not claim:
- A new implemented `WorkbookEditor`, random cell editing API, or public
  `PackageEditor` from this design task alone. `CellValue` exists as a
  standalone value type, and `CellStore` exists as an internal foundation;
  neither means editor readiness.
- That one unified facade can hide Streaming/Patch/In-memory performance costs.

The detailed sections below keep their historical labels for traceability. Use
the authoritative execution order above for actual next-task selection.

### P4.1 - Patch MVP Use Case Freeze

Status: complete documentation gate.

The first Patch MVP is frozen as an internal by-name worksheet `<sheetData>`
patch. The future user story is `WorkbookEditor`-shaped: open an existing
`.xlsx`, select an existing worksheet by sheet name, replace that worksheet's
`<sheetData>` / `<sheetData/>` payload with caller-generated XML, and `save_as()`
a new package. Current implementation remains internal
`PackageEditor::replace_worksheet_sheet_data_by_name()`.

Accept when:
- `TASK_BREAKDOWN.md`, `TASK_PLAN.md`, and this file agree that the MVP is the
  internal by-name `<sheetData>` patch, not a generic metadata rewrite choice.
- The docs state that the helper is a bounded local rewrite and reuses existing
  calcChain remove / `fullCalcOnLoad`, relationship/content-type audit, and
  unknown/unmodified part preservation behavior.
- Non-goals are explicit: public `WorkbookEditor`, public `PackageEditor`,
  random cell editing, sharedStrings index migration, style id migration/style
  merge, relationship repair/pruning, table/drawing semantic sync, range
  repair, dimension recalculation, and large-file streaming worksheet
  transformation.

Do not claim:
- Existing-file editing is public API.
- The caller can set arbitrary cells directly.
- The helper migrates shared strings/styles or repairs object relationships.

Current implementation evidence:
- `PackageEditor::replace_worksheet_sheet_data_by_name()` is the internal MVP
  entrypoint.
- Package boundary hardening, no-calcChain relationship/content-type side
  effects, calcChain remove/preserve/rebuild rejection behavior, and a
  writer-source end-to-end fixture now have CTest coverage.
- Fixed local QA entry `tools/verify_patch_mvp_excel.ps1` opens the
  writer-roundtrip and template-fill Patch MVP outputs read-only through Excel
  COM and verifies target-sheet replacement plus untouched-sheet preservation
  smoke values.
- Remaining work should move into smaller P5 preservation fixtures and P6 sheet
  dependency-policy slices instead of expanding this into a public editor.

### Historical P2 Detail - Production ZIP Dependency Discovery

Status: baseline complete for the minizip backend. `minizip-ng[core,zlib]` now
has a verified CMake package and imported target:
`find_package(minizip-ng CONFIG REQUIRED)` / `MINIZIP::minizip-ng`.

Do:
- Keep the verified vcpkg ports, feature names, config package names, imported
  CMake targets, and license obligations documented for `minizip-ng`, `zlib-ng`,
  and fallback `zlib`.
- Keep the record that current `minizip-ng[zlib]` metadata resolves through vcpkg
  `zlib`, not `zlib-ng`; decide whether `zlib-ng` remains a separate future
  compression option or whether the minizip route should use plain `zlib`.
- Treat `expat` and `pugixml` as runtime dependency discovery siblings, not as
  proof that XML reader/DOM editing is implemented.
- Keep exact findings in dependency docs when adding or changing
  `find_package` calls.
- Keep dependency work separate from OpenXML feature changes.

Accept when:
- A clean configure proves the selected packages and CMake targets resolve.
- Docs identify which dependencies are runtime, dev-only, or optional.

Do not claim:
- Compression, Zip64, or package streaming before code uses the verified
  backend.

### Historical P3 Detail - Package Writer Boundary

Status: baseline complete. New workbook output now goes through the internal
`src/package_writer.*` boundary.

Do:
- Keep `src/zip_store_writer.*` as the stored ZIP bootstrap implementation
  behind that boundary.
- Keep `PackageWriterBackend::Auto` selecting minizip when
  `FASTXLSX_ENABLE_MINIZIP_NG` is enabled and stored bootstrap otherwise.
- Keep OpenXML structure tests independent of compression method.

Current foundation:
- `src/package_writer.hpp` / `src/package_writer.cpp` define internal
  `PackageEntry`, `PackageWriterOptions`, `PackageWriterBackend`, and
  `write_package()`.
- `Workbook::save()` and `WorkbookWriter::close()` now call `write_package()`
  instead of calling the ZIP bootstrap directly.

Accept when:
- Existing `.xlsx` structure tests pass through the new boundary.
- Excel can still open the representative generated samples.

Do not claim:
- Public package editing, true package streaming, or Zip64 from this internal
  boundary alone.

### Historical P4 Detail - Production ZIP Backend

Status: minimal opt-in backend and internal compression-level configuration
landed. Continue with hardening before making it the default.

Do:
- Keep the verified ZIP/DEFLATE backend wired through
  `FASTXLSX_ENABLE_MINIZIP_NG=ON` and `MINIZIP::minizip-ng`.
- Keep `PackageWriterOptions::compression_level` internal to the package writer
  boundary: `-1` means backend default, `0` requests minizip
  no-compression/stored output, `1..9` selects zlib-compatible minizip DEFLATE
  levels, and stored bootstrap output remains stored/no-compression.
- Keep the current no-Zip64 and file-backed chunk guardrails: reject empty
  entry lists, package entry counts above `65535`, entry names beyond the
  16-bit ZIP field, invalid entry names, duplicate entry names, missing or
  inaccessible file-backed chunks, and single entry uncompressed sizes above
  `UINT32_MAX` before opening the output path.
- Define real Zip64 and large-entry behavior before large-file promises.

Accept when:
- Tests pass for package entries without assuming stored/no-compression ZIP.
- Generated workbooks open in Excel without repair.
- Docs and API comments describe backend and memory behavior for touched public
  API. Internal-only backend work must not expose public compression controls
  just to satisfy this checklist.

Do not claim:
- Large-file performance until benchmarks record scale, time, memory, output
  size, compression setting, and office-suite open result.

### Historical P5 Detail - Shared Strings Hardening

Start after current sharedStrings structure support exists; production tuning is
best after P4, while small structure fixes can happen earlier.

Do:
- Keep `inlineStr` as the default low-memory path.
- Add tests for `count`, `uniqueCount`, escaping, `xml:space`, duplicates, and
  worksheet `t="s"` references.
- Keep the empty shared string table path clean: if `SharedString` mode sees no
  string cells, do not write a dead sharedStrings part or relationship.
- Keep `tools/verify_shared_strings_absence.py` and
  `tools/verify_shared_strings_absence_excel.ps1` aligned with the generated
  absence sample whenever the sample shape changes.
- Measure repeated-string and mostly-unique-string behavior with the opt-in
  benchmark `--string-pattern repeated|unique` inputs before widening support
  wording.
- Current local small benchmark snapshot is recorded in `docs/PERFORMANCE_TARGETS.md`;
  expand scale and backends before changing production wording.

Accept when:
- CTest passes.
- Shared string samples open in Excel.
- Reference XML comparison is recorded when structure compatibility is in
  question.
- Memory/size data is recorded before any production-ready wording.
- Current `500000`-cell manual snapshot uses historical benchmark schema v3 and
  `temporary_worksheet_part_footprint="worksheet-body-file-bytes"`:
  repeated/inline `493 ms`, `4.97266 MB`, `27927834` worksheet body bytes,
  `27931711` output bytes; repeated/shared `392 ms`, `4.98828 MB`,
  `16927834` worksheet body bytes, `16932289` output bytes; unique/inline
  `658 ms`, `4.97266 MB`, `30866774` worksheet body bytes, `30870651`
  output bytes; unique/shared `1045 ms`, `70.1055 MB`, `19316724`
  worksheet body bytes, `33260102` output bytes.
- Current `100000`-cell schema-v4 matrix snapshot is recorded in
  `docs/PERFORMANCE_TARGETS.md`: repeated/inline `84 ms`, `5.04297 MB`,
  `5487834` worksheet body bytes, `5491711` output bytes; repeated/shared
  `61 ms`, `5.0625 MB`, `3287834` worksheet body bytes, `3292289` output
  bytes; unique/inline `111 ms`, `5.03125 MB`, `5986774` worksheet body bytes,
  `5990651` output bytes; unique/shared `325 ms`, `18.2383 MB`, `3676724`
  worksheet body bytes, `6380102` output bytes. `openpyxl` read-only checks
  verified each generated workbook's `Sheet1` first/last cells; `office_open`
  remains the benchmark tool's `not_run` field.
- Current `tools/run_benchmark_matrix.py --self-test` covers runner-only case
  parsing, string distribution expectations, expected cell values, and matrix
  report shape without invoking a benchmark executable or writing workbook
  artifacts.

Do not claim:
- sharedStrings as the best default for large data.

### Historical P6 Detail - Benchmark Groundwork

Start before making broad performance claims or adding convenience APIs that
could affect the writer hot path.

Do:
- Use the opt-in manual benchmark target `fastxlsx_bench_streaming_writer`.
- Use `tools/run_benchmark_matrix.py` when a repeatable local small matrix is
  needed; pass one already-built benchmark exe at a time and keep stored/minizip
  results in separate output dirs.
- Run `py tools/run_benchmark_matrix.py --self-test` as a quick runner guard
  when changing benchmark matrix logic. It does not replace actual benchmark,
  openpyxl, or Office validation.
- Keep benchmark dependencies behind planned/dev or opt-in configuration.
- Record data scale, string strategy, compression setting, package entry source
  mode, string pattern, input string distribution counts, temporary worksheet
  part footprint availability, time, peak memory, output size, and
  Excel/WPS/LibreOffice open result.
- Treat `temporary_worksheet_part_footprint="worksheet-body-file-bytes"` as a
  benchmark-only worksheet body row XML byte count. It does not include
  sharedStrings XML, worksheet header/footer, package assembly buffers,
  ZIP/backend memory, media files, small XML parts, or OS file-system overhead.
- Keep the first slice independent of Google Benchmark; `planned-dev`
  dependencies remain planned until a separate task explicitly wires them.

Accept when:
- Default CTest remains lightweight and under the 60s boundary.
- Benchmark results are reproducible enough to compare future regressions.
- Matrix reports keep raw schema-v4 per-case JSON and `office_open="not_run"`.
  The separate Office validation helper writes the sidecar
  `benchmark-matrix-office-report.json` and does not rewrite the matrix report
  or per-case JSON.

Do not claim:
- Benchmark coverage from normal unit tests.
- Google Benchmark integration or 10,000,000-cell results from the manual
  benchmark entry alone.
- Full low-memory behavior from worksheet-body-only footprint results.

### Historical P7 Detail - Streaming Writer Hot Path

Start after P3/P4 boundaries are clear enough that package output will not hide
worksheet-writer memory problems.

Do:
- Keep row-order writes and bounded memory.
- Add numeric/date encoding edge cases and Excel row/column limit tests.
  Current coverage rejects `NaN` / `+Inf` / `-Inf` for numeric cells, rejects
  in-memory and streaming row heights that are zero, negative, or non-finite,
  rejects in-memory rows beyond Excel's 16384-column limit during save(), and
  rejects streaming column widths that are non-positive or non-finite.
  Current numeric XML output uses shared internal `detail::append_number()` /
  `detail::format_number()` helpers across in-memory, CellStore, and streaming
  paths; append-oriented paths avoid per-cell temporary string construction
  while preserving finite-only `std::to_chars` output. Current cell reference
  XML output uses shared internal `detail::append_cell_reference()` /
  `detail::cell_reference()` helpers so row/cell append paths avoid per-cell
  temporary reference strings. Current unsigned integer XML append uses
  internal `detail::append_unsigned_decimal()` for cell reference row suffixes,
  streaming row numbers, in-memory/streaming style id attributes, and
  sharedStrings string-cell indexes; this is a local append helper, not
  benchmark evidence, sharedStrings strategy change, or broader date encoding.
  Current sharedStrings duplicate lookup uses transparent `std::string_view`
  lookup in the workbook-scope index map, so repeated strings avoid an owning
  temporary key before reusing the existing index. The index map stores
  `std::string_view` keys into stable unique-string storage instead of a second
  owning key copy for each unique shared string.
  Current XML text and attribute escaping uses shared internal
  `detail::append_escaped_xml_text()` /
  `detail::append_escaped_xml_attribute()` helpers across in-memory, CellStore,
  streaming row/formula/metadata XML, and small OPC serializers; the older
  string-returning helpers remain available for replacement paths that require
  owned strings.
  Current `append_row()` row limit coverage uses `FASTXLSX_ENABLE_TEST_HOOKS` to
  inject the internal row counter at `1048576` and verify one rejected append
  without a million-row default CTest loop; broader date and formatting edge
  cases remain follow-up work.
- Track worksheet dimensions incrementally. Current empty-row coverage locks
  streaming `<dimension ref="A1"/>` for no-row and all-empty-row sheets,
  preserves empty `<row r="N"></row>` elements, and keeps a trailing appended
  empty row in the generated dimension. Current in-memory tests also lock empty
  worksheet, single empty row, `XFD1` max-column dimension, and 16385-column
  rejection. Current streaming tests also lock legal `XFD1` max-column output,
  legal sparse `1048576` max-row output through the test-only hook, and
  failed-append state hygiene. Do not introduce a full cell matrix just to mimic
  Excel `UsedRange`.
- Add or update Doxygen comments for public APIs touched by the change.

Accept when:
- Structure tests pass for dimensions, cell references, value types, and string
  strategy.
- Excel visual checks are recorded where useful, but XML structure remains the
  authority for dimension semantics when Excel `UsedRange` is narrower.
- API comments state Streaming mode, ordering, lifetime, memory behavior, and
  unsupported random access.
- Benchmarks or follow-up benchmark tasks exist for performance-sensitive paths.

Do not claim:
- Low memory if the implementation retains a full worksheet cell matrix.
- `FASTXLSX_ENABLE_TEST_HOOKS` or `testing_set_worksheet_row_count()` as public
  API, benchmark coverage, or proof of million-row export performance.

### Historical P8 Detail - Phase 3 Metadata Tests

Status: basic focused structure and fixed local QA helpers exist for the
current streaming metadata skeleton. Continue when this surface changes or when
styles/formula calculation work begins.

Do:
- Keep structure tests for existing metadata writer paths current. The current
  `fastxlsx.streaming` slice checks formula XML escaping, row height, column
  width records, last-call-wins frozen panes, last-call-wins auto filters,
  merged ranges, suffix ordering, workbook calcPr full-recalculation metadata,
  and absence of relationship/content-type side effects.
- Keep Excel visual samples current for visible formula cells, row/column
  sizing, frozen panes, auto filters, and merged cells. Current local Excel COM
  validation opened
  `build/windows-nmake-release/tests/fastxlsx-streaming-phase3-metadata.xlsx`.
- Keep `tools/verify_phase3_metadata.py` and
  `tools/verify_phase3_metadata_excel.ps1` current when this sample shape
  changes. These helpers are local QA only; do not add them to default CTest/CI.
- Document formula boundaries: write-only formula text unless cached values,
  calculation mode, and calc chain are implemented. Current formula cells only
  add `<calcPr calcId="124519" fullCalcOnLoad="1"/>` to request recalculation on
  load; they do not provide cached values or `calcChain.xml`.

Accept when:
- XML structure tests and local Excel checks are recorded for the touched
  metadata surface.
- Python XLSX reference checks such as `openpyxl` are recorded when workbook
  calculation metadata changes.
- The Phase 3 metadata helper report is refreshed when the sample changes.
- Docs still mark full Phase 3 as planned.

Do not claim:
- Formula calculation, cached formula correctness, or complete style support.

### Historical P9 Detail - Style Registry Design and First Styles

Status: 基础 for streaming-only custom number format, wrap-text + limited horizontal/vertical alignment, bold/italic/direct-color font, and solid fill styles.

Current foundation:
- `StyleId` is a workbook-local handle; default `StyleId{}` is style `0`.
- `CellAlignment` currently exposes `wrap_text` plus optional limited
  `HorizontalAlignment::{Left,Center,Right}` and
  `VerticalAlignment::{Top,Center,Bottom}`; `CellFont` currently exposes
  `bold` / `italic` plus optional direct ARGB `color`; `CellFill` currently exposes only solid foreground
  `ArgbColor`; `CellStyle` stores `number_format` plus optional narrow alignment,
  font, and fill metadata.
- `WorkbookWriter::add_style(CellStyle)` copies style metadata into workbook
  state, rejects empty styles, de-duplicates repeated complete styles, and
  reuses the same custom `numFmtId` for equal number-format strings across
  different style combinations, the same `fontId` for equal
  bold/italic/direct-color font combinations, and the same `fillId` for equal
  foreground ARGB fills. Direct font color is serialized in `xl/styles.xml` as
  `<font><color rgb="..."/>` inside the generated font record.
- `CellView::with_style(StyleId)` carries the style id into append-row cell XML.
- `WorksheetWriter::append_row()` validates non-default style ids before
  advancing row count, dimensions, sharedStrings state, or formula recalculation
  metadata.
- `WorkbookWriter::close()` writes `xl/styles.xml`, a styles content type
  override, and a workbook relationship only when non-default styles are
  registered.

Do:
- Keep style ids workbook-local and opaque. Expose `StyleId::value()` only for
  diagnostics and structure tests.
- Keep `xl/styles.xml` as a small workbook-level part. Do not create worksheet
  `.rels` for styles.
- Keep style validation before row-state mutation.
- Add future full-font/full-fill/border/full-alignment slices through the same registry, not
  through ad hoc cell XML fragments.
- Document every style API as Streaming / new-workbook-only until existing-file
  style preservation exists.

Accept when:
- CTest covers `xl/styles.xml`, style ids, worksheet `s="N"` references,
  custom `numFmtId`, XML attribute escape, wrap-text + limited horizontal/vertical
  `applyAlignment` / `<alignment .../>`, bold/italic/direct-color font records,
  `<font><color rgb="..."/>`, `fontId` reuse, `applyFont="1"`, solid fill records, `fillId` reuse, `applyFill="1"`,
  sharedStrings + styles relationship ordering,
  default `s="0"` omission, and invalid foreign `StyleId` state hygiene.
- Local QA runs:
  `tools/verify_styles_number_formats.py` for package XML / `openpyxl` /
  optional `XlsxWriter`, and `tools/verify_styles_excel.ps1` for Excel COM
  read-only NumberFormat, WrapText, HorizontalAlignment, VerticalAlignment,
  Font.Bold, Font.Italic, Font.Color, Interior.Pattern, and Interior.Color
  checks, including `fastxlsx-streaming-styles-fonts.xlsx`.

Do not claim:
- Full font control, full fill/pattern control, border, full alignment, rich text, dxf-backed conditional formatting,
  date cell type, existing-file style preservation, or full Excel formatting
  parity from the number-format, limited alignment, bold/italic/direct-color font,
  and solid fill slices.

### Historical P10 Detail - Configurable Document Properties API

Status: 基础 + fixed local QA helpers.

Current foundation:
- Public `DocumentProperties` exists for small new-workbook metadata.
- `Workbook::set_document_properties()` and
  `WorkbookWriterOptions::document_properties` feed `docProps/core.xml` and
  `docProps/app.xml`.
- The public document-properties API remains new-workbook-only. The internal
  `PackageEditor` now has a narrow existing-package core/app docProps
  generated-small-XML path that can add or replace `docProps/core.xml` and
  `docProps/app.xml` while syncing package relationships and content types.
  This does not generate `docProps/custom.xml`; the internal Patch regression
  now verifies that an existing `docProps/custom.xml`, its custom-properties
  package relationship, its content type override, and unrelated unknown bytes
  are preserved while core/app metadata is regenerated. It is not a public
  document-property editing API or custom-properties editor.
- Fixed local QA helpers now exist:
  `tools/verify_document_properties.py` checks ZIP/XML and `openpyxl`
  core-property semantics for the in-memory and streaming samples, and
  `tools/verify_document_properties_excel.ps1` opens both samples read-only in
  local Excel COM for workbook-open and smoke-sheet validation.

Start after the static `docProps/core.xml` and `docProps/app.xml` baseline stays
stable.

Do:
- Keep the small-part metadata API separate from worksheet hot paths.
- Maintain Doxygen comments describing side effects on `docProps` parts and
  package content types/relationships.

Accept when:
- Structure tests check `docProps/core.xml`, `docProps/app.xml`, relationships,
  and content types.
- Excel opens samples and displays expected document metadata where applicable.
- Python/XML and Excel helper output is recorded when document-property behavior
  changes.

Do not claim:
- `docProps/custom.xml` creation/editing, public existing-file document-property
  editing, arbitrary timestamps, or full document-property coverage unless every
  exposed property is tested. Current internal coverage is preservation-only for
  an existing custom properties part.

### Historical P11 Detail - Internal OPC Graph

Status: 基础.

Current foundation:
- `include/fastxlsx/detail/opc.hpp` exposes internal `ContentTypeRegistry`,
  `PartIndex`, and `RelationshipGraph` in `fastxlsx::detail`.
- `src/opc.cpp` implements owner-scoped relationship sets, automatic `rIdN`
  allocation per owner, source-part registration checks, and content type
  default/override helpers.
- `tests/test_opc.cpp` covers part lookup, duplicate/conflict handling,
  relationship ownership, id uniqueness, external relationships, registry
  lookup, write-mode defaults, and error paths.
- `tests/test_package_reader.cpp` covers stored ZIP entry reading plus
  content-types/relationships ingestion into internal part and relationship
  views. `tests/test_package_editor.cpp` covers the current internal
  copy/replace foundation, core/app docProps generated-small-XML, worksheet
  replacement calcChain/fullCalcOnLoad behavior, the combined docProps +
  worksheet replacement path, helper/ordinary replacement ordering in both
  directions for docProps, prior ordinary calcChain replacement overridden by
  workbook-only and worksheet calcChain removal, prior ordinary calcChain
  replacement preserved as final payload under workbook-only and worksheet
  `CalcChainAction::Preserve`, prior ordinary workbook replacement overridden by
  worksheet fullCalcOnLoad metadata rewrite, ReferencePolicy boundaries, and
  unknown/linked part byte preservation for untouched entries.

Do:
- Keep this internal until package reader/writer and preservation tests exist.
- Use it as groundwork before broader hyperlink/table/image support, chart/VBA
  passthrough, or existing-file editing.

Accept when:
- `ctest --preset windows-nmake-release` passes.
- No public existing-file edit API is exposed yet.

Do not claim:
- Package editing, broader hyperlink support, or object preservation from
  internal graph helpers alone.

### Historical P12 Detail - Existing Package Reader/Writer

Historical start condition: after the old production ZIP backend and internal
OPC graph lanes. Under the updated authoritative order, use P3/P4 for package
read/copy/write and Patch MVP work.

Do:
- Read package entries, `[Content_Types].xml`, package relationships, and part
  relationships.
- Write packages by copying unmodified parts and regenerating only targeted
  parts.
- Keep large worksheets and large shared strings streaming-only.

Accept when:
- Tests prove unknown and unmodified parts remain present.
- Relationships still resolve after a targeted rewrite.
- Edited workbooks open in Excel without repair.

Do not claim:
- Complete editing until preservation and compatibility samples cover the
  relevant feature class.

### Historical P13 Detail - Preservation Fixture Set

Historical start condition: after the old existing package reader/writer path.
Under the updated authoritative order, this is P5 preservation fixture work.

Do:
- Add template workbooks containing images, charts, macros, and unknown parts.
- Edit unrelated workbook or worksheet metadata.
- Compare before/after packages.

Accept when:
- Untouched worksheet `.rels`, drawings, media, charts, macros, and unknown
  extensions remain in the output package, even when a replacement worksheet no
  longer references the original drawing/table objects.
- Excel opens edited workbooks without repair.

Do not claim:
- Native image, chart, or VBA generation.

### P14 - Streaming-Only Data Validations

Status: 基础 + prompt/error/dropdown metadata + multi-area `sqref`. The
streaming-only new-workbook worksheet slice is implemented.

Do:
- Keep new-workbook `WorksheetWriter` metadata as the only supported surface.
- Keep worksheet `<dataValidations>` independent of package relationships and
  content type overrides.
- Keep prompt/error metadata worksheet-local: `showInputMessage`,
  `showErrorMessage`, `errorStyle`, `promptTitle`, `prompt`, `errorTitle`, and
  `error` are attributes on `<dataValidation>`.
- Keep dropdown arrow metadata worksheet-local and list-only:
  `hide_dropdown_arrow` writes OpenXML's inverted `showDropDown="1"` attribute
  to hide the in-cell dropdown arrow. Omitted means the default visible arrow.
- Keep multi-area data validation as one rule with a copied range list and one
  space-separated `sqref`; `count` remains the number of `<dataValidation>`
  elements, not the number of areas.
- Extend the narrow rule surface only with structure tests, Doxygen comments,
  and Excel/reference XML validation.

Accept when:
- Current tests cover `count`, `sqref`, `type`, `operator`, `allowBlank`,
  `formula1`, `formula2`, invalid ranges, invalid rule shapes, XML escaping,
  package relationship absence, and mutation-after-close behavior.
- Tests also cover validation-only namespace behavior and `formula2` XML text
  escaping: validation-only worksheets do not declare `xmlns:r`, and `formula2`
  escapes `&`, `<`, and `>`.
- Tests also cover prompt/error attributes, XML attribute escaping, prompt-only
  and error-only rules, `stop` / `warning` / `information` error styles, empty
  string omission, false flag omission, list-only `showDropDown="1"`, and no
  `.rels` / `styles.xml` / content type side effects.
- Tests also cover multi-area `sqref` serialization, empty range list rejection,
  invalid range rejection inside a multi-range list, and no relationship id
  consumption by data validations.
- Coexistence tests cover suffix ordering with relationship-backed metadata:
  `<dataValidations>` stays before `<hyperlinks>` and `<tableParts>`, and data
  validations do not consume worksheet-local `rId` values before hyperlinks and
  tables.
- Local Excel visual verification is recorded for
  `build/windows-nmake-release/tests/fastxlsx-streaming-data-validations.xlsx`.
- Local Excel COM and Python QA are recorded for
  `build/windows-nmake-release/tests/fastxlsx-streaming-data-validation-prompts.xlsx`
  through `tools/verify_data_validation_prompts_excel.ps1` and
  `tools/verify_data_validation_prompts.py`.
- Local Excel COM and Python QA are also recorded for
  `build/windows-nmake-release/tests/fastxlsx-streaming-data-validation-multi-range.xlsx`
  through the same helpers using `-MultiRangePath` / `--multi-range-input`.

Do not claim:
- Full data validation semantics, formula parsing, value validation, overlap
  checks, range sorting/merging/deduplication, complete Excel UI behavior, or
  existing-file editing.

### P15 - Hyperlinks

Status: 基础 for streaming-only external URL and internal workbook-location
hyperlinks in new workbooks.

Do:
- Keep `WorksheetWriter::add_external_hyperlink()` as a new-workbook Streaming
  metadata API.
- Keep `WorksheetWriter::add_internal_hyperlink()` as a new-workbook Streaming
  metadata API.
- Store only lightweight cell-reference and target URL state; memory grows with
  link count plus URL/location text length.
- Write external hyperlinks as worksheet `<hyperlinks>` and worksheet-owned
  `.rels` together.
- Write internal hyperlinks as worksheet `location` attributes only; do not
  create worksheet `.rels`, workbook relationships, content type overrides, or
  consume worksheet-local `rId` values.
- Use `HyperlinkOptions` for optional display and tooltip metadata. Non-empty
  values are copied into writer state and written as worksheet `<hyperlink>`
  attributes only.
- Keep picture/object hyperlinks separate from worksheet cell hyperlinks.
  Picture hyperlinks belong to drawing XML and drawing-owned `.rels`, not
  worksheet `<hyperlinks>`.
- Keep hyperlink styles, URL reachability checks, internal target existence
  checks, named range semantics, and existing-file editing out of these first
  slices.

Accept when:
- Tests prove worksheet XML `r:id` values match worksheet `.rels`.
- Tests prove internal-only worksheets write `location` without `r:id`, do not
  declare `xmlns:r`, and do not create worksheet `.rels`.
- Tests prove mixed external/internal hyperlinks keep external `rId` values
  stable and do not leak internal locations into worksheet `.rels`.
- Package relationship parts and content types are correct, including no
  workbook relationship pollution and no content type override for `.rels`.
- Tests cover target XML escaping, multiple hyperlinks in one worksheet,
  worksheet-owner-local `rId` allocation across worksheets, plain sheets without
  `.rels`, invalid row/column references, empty target URLs, empty internal
  locations, and mutation-after-close.
- Tests cover `display` / `tooltip` attribute serialization for external and
  internal hyperlinks, XML attribute escaping, display-only, tooltip-only,
  explicitly empty options being omitted, unchanged relationship behavior, and
  no `styles.xml`.
- Local `openpyxl` 3.1.2 and Excel COM validation is recorded for
  `build/windows-nmake-release/tests/fastxlsx-streaming-hyperlink-display-tooltips.xlsx`.
  Excel COM validates `ScreenTip`, external `Address`, internal `SubAddress`,
  hyperlink counts, and unchanged cell text; `TextToDisplay` remains the cell
  text in these samples.
- Excel visual verification is recorded for
  `build/windows-nmake-release/tests/fastxlsx-streaming-external-hyperlinks.xlsx`.
- Excel visual verification is recorded for
  `build/windows-nmake-release/tests/fastxlsx-streaming-internal-hyperlinks.xlsx`;
  local `openpyxl` 3.1.2 also confirmed internal `location` semantics and a
  reference workbook with no worksheet `.rels`.

Do not claim:
- Full hyperlink support from these basic slices.
- Hyperlink styles, target existence validation, named range semantics,
  existing-file editing, unknown part preservation, or complete Excel UI parity.

### P16 - Tables

Status: 基础 for streaming-only new-workbook tables.

Do:
- Keep `WorksheetWriter::add_table()` as a new-workbook Streaming metadata API.
- Store only lightweight range, table name, column name, and style flag state;
  do not read written row XML or infer headers.
- Allocate table parts, content type overrides, worksheet relationships, and
  worksheet `<tableParts>` references.
- Keep worksheet relationship ids owner-local and compatible with hyperlinks.
- Allow only `TableOptions::show_totals_row` for totals-row visibility metadata,
  `column_totals_functions` for caller-supplied `totalsRowFunction`
  attributes, and `column_totals_labels` for caller-supplied `totalsRowLabel`
  attributes.
- Allow only same-worksheet table-vs-table range overlap rejection. Keep
  generated totals formulas, calculated columns, sort/filter criteria, custom
  styles, `styles.xml`, table resize, conflict checks against non-table
  worksheet metadata/objects, existing-file editing, and full Excel table UI
  behavior out of this first slice.

Accept when:
- Structure tests compare table XML, worksheet relationships, worksheet
  `<tableParts>`, content types, XML escaping, table column attribute escaping,
  owner-local `rId`, coexistence with external hyperlinks under the same
  worksheet relationship owner, table style flags without generating
  `xl/styles.xml`, `show_totals_row` true/false/default metadata,
  caller-supplied `totalsRowFunction` and `totalsRowLabel`, absence of generated
  formulas / empty label attributes, duplicate names, invalid ranges/options,
  same-worksheet table range overlap rejection, adjacent table allowance,
  cross-worksheet same-range allowance, and mutation-after-close.
- Excel visual verification is recorded for
  `build/windows-nmake-release/tests/fastxlsx-streaming-tables.xlsx`; Excel COM
  confirmed `InventoryTable` and `TotalsTable` as `ListObjects` with expected
  ranges and headers, confirmed `TotalsTable.ShowTotals=True` with totals row
  range `A3:B3`, and confirmed `Plain` has no table object.
- Excel COM, `openpyxl`, and an unpacked local `XlsxWriter` reference also
  confirm `fastxlsx-streaming-table-column-escape.xlsx` preserves table column
  headers containing `"`, `'`, `&`, `<`, and `>` without generating
  `xl/styles.xml` in the FastXLSX package.

Do not claim:
- Full table support, automatic header inference, custom style support,
  existing-file table editing, unknown part preservation, or Excel table feature
  parity.

### P17 - Images

Dependency discovery, API design, and the first new-workbook-only insertion
slice are now started. Existing-workbook image read/edit/preservation still
starts only after package reader/writer and preservation fixtures prove
preservation behavior.

Stages:
1. P17.0 - `stb` dependency discovery and image metadata/pixel helpers. Status:
   basic.
   - Use `stb` for image decoding, dimensions, channels, and pixel access.
   - Keep it as a default vcpkg manifest dependency.
   - Current code exposes PNG/JPEG `read_image_info()` for file and memory
     input, backed by `stbi_info`. Current tests also cover unsupported
     memory/file headers, empty
     memory buffer, empty file, and missing file.
   - Current code also exposes `ImagePixels` and `read_image_pixels(path|span)`
     for PNG/JPEG decode into an owned full pixel buffer. Current tests cover
     PNG/JPEG file and memory pixel decode plus empty, missing, and unsupported
     inputs.
   - The `read_image_info()` documentation must describe metadata reading only;
     it must not imply media part creation, drawing XML, relationships, content
     types, anchors, or existing-workbook preservation.
   - This stage alone still does not create media parts, drawing XML,
     relationships, content types, anchors, format conversion, drawing editing,
     or existing-workbook preservation; `read_image_pixels()` allocates the full
     decoded pixel buffer and is not used by the `WorksheetWriter::add_image()`
     streaming insertion hot path.
2. P17.1 - API shape and documentation.
   Status: basic for `WorksheetWriter::add_image()` path and memory-source overloads.
   - Document whether each image API is Streaming, Patch, or In-memory.
   - Public comments must state memory behavior for original image bytes,
     decoded pixels, anchor metadata, drawing/media part state, and package
     finalization.
   - Current memory-source image overloads accept `std::span<const std::byte>`;
     the caller-owned span only needs to remain valid during the call. FastXLSX
     validates metadata with `read_image_info(bytes)`, immediately copies the
     original bytes to a temporary file-backed media entry, and does not retain
     the span or a decoded pixel buffer.
   - Current `ImageOptions` metadata is limited to drawing XML two-cell marker /
     non-visual picture properties plus optional picture external hyperlink
     metadata: `from_offset` / `to_offset` write EMU values to marker
     `xdr:colOff` / `xdr:rowOff`, `edit_as` writes
     `xdr:twoCellAnchor editAs`, non-empty `name` writes `xdr:cNvPr name`,
     non-empty `description` writes `descr`, non-empty
     `external_hyperlink_url` writes `a:hlinkClick` under `xdr:cNvPr` and a
     drawing-local external hyperlink relationship, and
     `external_hyperlink_tooltip` writes the optional `tooltip` attribute.
     Empty `name` keeps generated `Picture N`, empty `description` is omitted,
     and empty hyperlink URL omits hyperlink metadata.
   - Any convenience API must explain why it does not force large worksheets
     into DOM, a full cell matrix, or the row/cell XML hot path.
3. P17.2 - New-workbook-only insertion slice.
   Status: basic for streaming new workbooks.
   - `WorksheetWriter::add_image(path, anchor)` accepts PNG/JPEG files by
     default, validates metadata with `read_image_info()`, and copies original
     image bytes into temporary file-backed media entries.
   - `WorksheetWriter::add_image(bytes, anchor)` accepts caller-owned PNG/JPEG
     memory spans, validates metadata with `read_image_info(bytes)`, copies the
     original bytes into the same temporary file-backed media entry path, and
     rejects empty buffers or unsupported image headers. It does not call
     `read_image_pixels()` or retain decoded pixels.
   - The first slice uses a simple two-cell anchor from a 1-based inclusive
     `CellRange`; it writes generated media parts, one drawing part per
     worksheet with images, drawing `.rels`, worksheet `.rels`, worksheet
     `<drawing>` references, and drawing/content type entries.
   - The current metadata increment copies image from/to marker EMU offsets,
     `edit_as`, `name` / `description`, and optional external picture hyperlink
     strings into writer state. It writes marker metadata to
     `xdr:colOff` / `xdr:rowOff`, `xdr:twoCellAnchor editAs`, and
     `xdr:cNvPr`; when `external_hyperlink_url` is non-empty, it also writes
     `a:hlinkClick` and a drawing-local hyperlink relationship. It does not
     modify image bytes, media filenames, anchor cell ranges, content types, or
     worksheet cell text.
   - It does not crop, rotate, recompress, convert formats, mutate existing
     drawings, edit existing XLSX files, or prove existing-workbook image
     preservation.
4. P17.3 - Compatibility and reference validation.
   Status: basic local validation for the new-workbook-only insertion slice.
   - Use structure tests for media, drawing XML, relationship parts, content
     types, and worksheet drawing references.
   - Current `fastxlsx.streaming` image tests verify `xl/media/image*.png|jpg`,
     `xl/drawings/drawing*.xml`, drawing `.rels`, worksheet `.rels`, worksheet
     `<drawing>`, owner-local relationship ids, PNG/JPEG content type defaults,
     JPEG drawing EMU sizing, JPEG media relationship targets, and drawing
     content type overrides. Mixed PNG/JPEG coverage also checks one worksheet
     sharing a single drawing part with multiple anchors, global media numbering,
     and drawing-owner-local image relationship ids.
   - Current mixed-object relationship coverage checks multiple external
     hyperlinks, one drawing, and multiple tables under the same worksheet
     relationship owner, plus owner-local `rId` reset across worksheets and
     drawing-local image relationship ids.
   - Current anchor boundary coverage checks maximum legal Excel row/column
     marker serialization, including 0-based drawing marker values such as
     `<xdr:col>16383</xdr:col>` and `<xdr:row>1048575</xdr:row>`.
   - Current image metadata coverage checks `xdr:twoCellAnchor editAs` values
     `oneCell`, `absolute`, and default `twoCell`; `xdr:cNvPr name` / `descr`;
     XML attribute escaping; empty description omission; default `Picture N`
     names; and no extra relationship/content type/media side effects.
   - Current memory-source image coverage writes
     `build/windows-nmake-release/tests/fastxlsx-streaming-memory-images.xlsx`;
     structure tests verify PNG/JPEG media bytes, caller-buffer mutation safety,
     shared drawing part output, drawing/worksheet relationships, content types,
     anchor offsets, `cNvPr` metadata, and intrinsic EMU sizes.
   - Current picture hyperlink coverage writes
     `build/windows-nmake-release/tests/fastxlsx-streaming-image-hyperlinks.xlsx`;
     structure tests verify `a:hlinkClick` under `xdr:cNvPr`, drawing-local
     external hyperlink relationships with `TargetMode="External"`, XML
     attribute escaping for URL and tooltip, worksheet relationship absence for
     object hyperlinks, and stable `a:blip r:embed` image relationship ids.
   - Use local Excel visual verification for generated `.xlsx` samples when
     Excel is available, confirming no repair dialog and expected image
     position/size.
   - Current local Excel COM verification opened
     `build/windows-nmake-release/tests/fastxlsx-streaming-images.xlsx`
     and confirmed 3 sheets, one shape on `Images`, one shape on `SecondImage`,
     zero shapes on `Plain`, first image at `C1:F5`, and second image at
     `A1:B2`.
   - Current local QA for
     `build/windows-nmake-release/tests/fastxlsx-streaming-image-metadata.xlsx`
     uses `tools/verify_image_metadata.py` for XML/openpyxl/XlsxWriter checks
     and `tools/verify_image_metadata_excel.ps1` for Excel COM shape name and
     `AlternativeText` / `Placement` / marker-offset geometry checks.
     The same helpers now accept `--basic-input` / `-BasicPath` for
     `fastxlsx-streaming-images.xlsx` and `--mixed-object-input` /
     `-MixedObjectPath` for `fastxlsx-streaming-mixed-object-rels.xlsx`, covering
     basic media/drawing relationships and mixed hyperlink/drawing/table
     owner-local relationship ids. They also accept `--memory-input` /
     `-MemoryPath` for `fastxlsx-streaming-memory-images.xlsx`, covering
     memory-source media bytes, package XML, `openpyxl` smoke,
     `XlsxWriter` reference generation, and Excel COM anchors. They also accept
     `--hyperlink-input` / `-HyperlinkPath` for
     `fastxlsx-streaming-image-hyperlinks.xlsx`, covering drawing XML
     `a:hlinkClick`, drawing-local hyperlink `.rels`, `XlsxWriter` reference
     generation, `openpyxl` smoke, and Excel COM shape hyperlink checks.
     They also accept `--mixed-hyperlink-input` /
     `-MixedHyperlinkPath` for
     `fastxlsx-streaming-image-hyperlink-mixed-objects.xlsx`, covering mixed
     picture hyperlinks, worksheet cell hyperlinks, and tables.
     `openpyxl` may skip JPEG image loading and does not expose picture
     hyperlink metadata, so XML and Excel COM remain authoritative for JPEG
     media/drawing counts and picture hyperlink semantics.
   - When XML structure or Excel repair behavior is unclear, generate an
     equivalent reference workbook with Excel, `openpyxl`, or `XlsxWriter`, then
     unzip both packages and compare OpenXML semantics.
5. P17.4 - Existing-workbook image read/edit/preservation.
   - Start only after preservation fixtures prove unmodified
     media/drawing/chart/VBA and unknown parts remain present and relationships
     still resolve after unrelated edits.

Do:
- Use `stb` as the image decoding, dimension-reading, channel, and pixel-access
  dependency for image reading tasks.
- Keep decoding separate from OpenXML packaging: `stb` does not manage media
  part names, relationship ids, content types, drawing XML, or anchors.
- Allocate media parts only in an implementation slice that also writes the
  required drawing and relationship parts.
- Generate drawing parts, drawing relationships, worksheet relationships,
  worksheet drawing references, and content type entries together.
- Keep current image media bytes file-backed in package entries; do not move
  image bytes or decoded pixels into the worksheet row/cell hot path. For
  memory-source image overloads, do not retain the caller-owned span after the
  call returns.
- Validate anchors without retaining a full worksheet DOM.

Accept when:
- vcpkg default manifest dependency `stb` resolution, include path, license, and local
  CMake behavior are verified for the image metadata/pixel helpers and image insertion
  structure tests. CI behavior for the default vcpkg path remains a hardening
  task until the workflow has passed.
- Public API docs for any image surface describe mode, ordering, memory cost,
  decoded-pixel lifetime, package side effects, and unsupported operations.
- `read_image_pixels()` docs and tests describe the owned full decoded buffer
  allocation and its separation from streaming image insertion.
- Package structure tests cover media, drawing XML, rels, and content types.
- Image metadata tests cover `xdr:twoCellAnchor editAs` and `xdr:cNvPr name` /
  `descr` semantics, plus optional `a:hlinkClick` picture hyperlink semantics,
  without changing media bytes, anchor coordinates, content types, worksheet
  `<hyperlinks>`, or worksheet hyperlink relationships.
- Excel visual verification confirms images display without repair and with the
  expected position and size.
- Reference XML comparison is recorded when structure compatibility is
  uncertain or Excel repairs the generated file.

Do not claim:
- Image editing or broad drawing support beyond the implemented slice.
- Picture support from `stb` dependency availability alone.
- OpenXML image support beyond the narrow `WorksheetWriter::add_image()`
  streaming new-workbook PNG/JPEG path/memory-source slice.
- Format conversion, drawing editing, existing-workbook preservation, or
  low-memory streaming insertion behavior from `ImagePixels` /
  `read_image_pixels()`.
- Arbitrary stream, URL, base64, or existing-workbook image source support from
  the memory-source overload alone.
- `oneCellAnchor` / `absoluteAnchor` element support, row/column resize geometry
  calculation from `ImageOptions::from_offset` / `to_offset`, complete image
  metadata, EXIF/PNG/JPEG metadata, accessibility UI parity, or existing drawing
  mutation from `ImageOptions` alone.
- Cell hyperlink style, worksheet cell text, internal workbook picture links,
  target reachability checks, complete hyperlink UI parity, or existing-file
  hyperlink editing from `ImageOptions::external_hyperlink_url`.
- Existing workbook image passthrough or preservation before preservation
  fixtures prove unmodified media/drawing/chart/VBA parts survive edits.

### P18 - Chart and VBA Passthrough

Start only after the package reader/writer path and preservation fixtures are
proven.

Do:
- Preserve existing chart and VBA parts while editing unrelated parts.
- Keep the scope as passthrough until native generation/editing is separately
  designed.

Accept when:
- Before/after package comparison proves chart/VBA parts and relationships
  remain intact.
- Excel opens edited files without repair and expected chart/macro parts remain
  available.

Do not claim:
- Native chart generation, chart editing, VBA generation, or VBA editing.

### P19 - Release Packaging

Start only after the targeted public surface has code, tests, local validation,
and documentation.

Do:
- Add install/export packaging rules.
- Decide versioning and changelog workflow.
- Ensure public headers have Doxygen comments.
- Keep `LICENSE` and third-party notices aligned with actually linked
  dependencies.

Accept when:
- Fresh clone configure/build/test passes.
- CI is green.
- README examples compile against public headers.
- No generated files, local build outputs, or private state are staged.

Do not claim:
- Stable API or release readiness if public comments or validation are missing.

## Immediate Repository Tasks

1. Keep generated artifacts out of source control.
   - Ignored build directories include `build/`, `build-*`, `cmake-build-*`,
     `out/`, `dist/`, and `%OPC_BUILD%/`.
   - Local secret files such as `.env`, private keys, and local tool state are
     ignored.

2. Use the conservative engineering entry points.
   - Prefer `cmake --preset windows-nmake-release` for local smoke builds.
   - Use `windows-nmake-release-minizip` after `VCPKG_ROOT` is configured when
     verifying the opt-in minizip backend.
   - Use `windows-nmake-release-vcpkg` only for generic vcpkg toolchain smoke;
     it is not the canonical minizip backend validation path.
   - The current CI workflow runs the vcpkg-backed NMake preset first and runs
     minizip/image opt-in vcpkg presets in a separate matrix.

3. Before future publishing, inspect staged files.
   - Confirm `.agents/skills/` is included.
   - Confirm generated build outputs, Excel output files, temporary logs, and
     local private state are not included.

## Next Engineering Priorities

### 1. OPC Editing Pipeline

Status: 基础.

Current foundation:
- Internal manifest and serializers exist for new-workbook metadata.
- Package part write-mode planning metadata is visible for copy-original,
  generate-small-XML, stream-rewrite, local-DOM-rewrite, and explicit
  registered-part removal decisions.
- Internal `PackageReader` can index and read stored/no-compression package
  entries by name, including unknown entries; minizip-enabled builds can read
  DEFLATE entries. It rejects encrypted/data descriptor entries and local
  header CRC/method/name/size mismatches, validate stored entry CRC before
  returning bytes, reject conflicting content type defaults/overrides and
  duplicate relationship ids within one `.rels` owner, reject namespaced
  metadata attributes except namespace declarations, reject duplicate
  unqualified metadata attributes, reject non-whitespace metadata text,
  reject start/end tag QName mismatches, and ingest content types / relationships into internal
  `PartIndex` / `RelationshipGraph` views. It can also resolve the internal
  workbook sheet catalog by first validating package `_rels/.rels` contains
  exactly one internal `officeDocument` relationship; the current narrow
  resolver only accepts targets resolving to `/xl/workbook.xml`, and missing,
  duplicate, external, URI-qualified, or non-fixed targets fail during lookup.
  Relative, absolute, and dot-segment package targets such as
  `xl/./workbook.xml` are resolved from the package root without modeling the
  package root as a real `PartName`.
  It then reads direct `<sheet>` children of workbook
  `<sheets>`, following workbook-owned worksheet relationships, ignoring
  `<sheet>` tags outside that catalog or nested under non-sheet catalog
  children, decoding XML attribute values and percent-encoded relationship
  targets, requiring `name` and `sheetId` to be unqualified workbook sheet
  attributes, and requiring sheet relationship ids to use the
  officeDocument relationships XML namespace (alternate prefixes are accepted,
  unqualified or wrong-namespace `id` attributes are rejected), and mapping a
  sheet name to an existing worksheet part;
  missing namespace-valid `r:id`, sheet relationship ids absent from workbook
  `.rels`, worksheet relationships resolving to unregistered parts, external
  sheet targets, and non-worksheet targets are rejected during lookup before
  by-name Patch state changes, and duplicate sheet-name lookups fail as ambiguous. This is
  Patch target resolution, not sheet add/delete/rename
  or a public workbook model, and it does not copy/write edited packages.
- Internal `PackageEditor` can replace one existing part and copy untouched
  entry bytes into a new stored package, including unknown entries. It can also
  perform the current core/app document-properties generated-small-XML path
  with package relationship/content type sync, and the current worksheet
  replacement narrow path with calcChain removal, workbook fullCalcOnLoad,
  workbook relationship, and content type sync. A combined-operation regression
  covers the docProps path plus worksheet replacement in one edit, including
  relationship/content-type merging, calcChain removal, stale calcChain owner
  `.rels` omission, metadata-only stale calcChain cleanup when the payload is
  absent, workbook metadata rewrite, unknown-entry preservation, and
  exact/path-equivalent source-overwrite rejection, and empty-output /
  missing-parent / non-directory-parent / existing-directory output rejection. The metadata-only cleanup is
  limited to stale content type / workbook relationship state; it does not create
  `xl/calcChain.xml` or imply general metadata repair.
  The same internal path now has `replace_worksheet_sheet_data()`, which replaces
  only the existing worksheet `<sheetData>` element while preserving surrounding
  worksheet XML metadata and then reusing worksheet replacement for calcChain /
  fullCalcOnLoad and preservation side effects. Its tests cover metadata
  preservation for sheetPr, sheetCalcPr, dimension, sheetViews,
  customSheetViews, sheetFormatPr, cols, sheetProtection, protectedRanges,
  sortState, autoFilter, mergeCells, scenarios, dataConsolidate,
  customProperties, cellWatches, smartTags, webPublishItems, dataValidations,
  conditionalFormatting,
  hyperlinks, ignoredErrors, printOptions, pageMargins, pageSetup,
  headerFooter, rowBreaks, colBreaks, phoneticPr, drawing, legacyDrawing,
  picture, legacyDrawingHF, oleObjects, controls, tableParts, and extLst; audit notes for
  preserved worksheet-local ranges/references;
  bounded local rewrite guardrails because the current helper materializes the
  planned worksheet XML; source or queued worksheet XML, replacement
  `<sheetData>` payloads, and rewritten worksheet XML above
  `package_editor_sheet_data_local_rewrite_byte_limit` fail before state
  changes, and the direct/by-name regression verifies `EditPlan`, manifest,
  package-entry audit, calc policy, planned output, copied output bytes, and
  unknown extension preservation remain unchanged; successful plans also expose
  bounded-local-rewrite notes/reasons so this is not confused with the future
  large-file streaming worksheet transformer;
  `PackageReader` / `RelationshipGraph` roundtrip coverage for the preserved
  worksheet `.rels` legacyDrawing `rId7` target
  `../drawings/vmlDrawing1.vml#shape1`;
  worksheet-owned background picture and header/footer VML drawing preservation
  under the same `sheetData` Patch lane: the output keeps the `<picture>` /
  `<legacyDrawingHF>` references, worksheet `.rels` `image` / `vmlDrawing`
  relationships, `xl/media/background.png` bytes,
  `xl/drawings/vmlDrawingHF1.vml` bytes, the PNG content type default, and the
  VML content type override, and planned output exposes those parts as
  relationship-derived copy-original audit metadata; this is not image, VML, or
  header/footer semantic editing, relationship repair/pruning, orphan cleanup,
  content type repair, public API, or complete object preservation;
  worksheet-owned printerSettings opaque part preservation under the same
  `sheetData` Patch lane: the output keeps the `<pageSetup r:id>` reference,
  worksheet `.rels` `printerSettings` relationship,
  `xl/printerSettings/printerSettings1.bin` bytes, and the printerSettings
  content type override, and planned output exposes that part as
  relationship-derived copy-original audit metadata; this is not printer
  settings semantic editing, relationship repair/pruning, orphan cleanup,
  content type repair, public API, or complete object lifecycle support;
  explicit removal coverage for the same fixture now omits
  `xl/media/background.png` while preserving the PNG default without promoting
  the media part to an override, omits `xl/drawings/vmlDrawingHF1.vml` while
  removing the VML content type override, preserves the worksheet `.rels`
  inbound relationships that still point at the missing background picture or
  header/footer VML part, and exposes structured removed-part inbound
  relationship audit metadata in `EditPlan` / planned output; this is Patch
  audit / no-pruning visibility, not image, VML, or header/footer deletion
  semantics, relationship repair/pruning, orphan cleanup, content type repair,
  public API, or complete object lifecycle support;
  same-path ordering coverage for the same background/VML fixture:
  remove-then-ordinary-replace for `xl/media/background.png` clears stale
  removed-part audit, restores an active background picture replacement, keeps
  the PNG default / content-types source-copy state without promoting the media
  part to an override, and preserves the worksheet `.rels` inbound relationship;
  ordinary replacement followed by explicit removal of
  `xl/drawings/vmlDrawingHF1.vml` clears the active VML replacement, records
  removed-part inbound audit, omits the VML part, removes the VML content type
  override, and preserves the worksheet `.rels` inbound relationship plus the
  sibling background picture part. The aggregate `planned_output()` snapshot now
  also covers both final states: the background-picture remove-then-replace path
  exposes the active picture `LocalDomRewrite`, content-types metadata
  copy-original, sibling header/footer VML preservation, no stale
  removals, no relationship target audits, no fullCalcOnLoad request,
  `CalcChainAction::Preserve`, and no invented picture owner `.rels`; the
  header/footer VML replace-then-remove path exposes the omitted VML part,
  removed-part inbound audit, content-types metadata rewrite, sibling
  background-picture preservation, no relationship target audits, no
  fullCalcOnLoad request, `CalcChainAction::Preserve`, and no invented VML
  owner `.rels`; this is Patch same-path state hygiene / audit
  visibility, not transactional undo, image/VML/header-footer semantic merge or
  deletion, relationship repair/pruning, orphan cleanup, content type repair,
  public API, or complete object lifecycle support;
  worksheet-owned registered OLE opaque part and control-property part
  preservation under the same `sheetData` Patch lane: the output keeps the
  `<oleObjects>` / `<controls>` references, worksheet `.rels` `oleObject` /
  `control` relationships, `xl/embeddings/oleObject1.bin` bytes,
  `xl/ctrlProps/control1.xml` bytes, and corresponding content type overrides,
  and planned output exposes those parts as relationship-derived copy-original
  audit metadata; this is not OLE / ActiveX / control semantic editing,
  relationship repair/pruning, orphan cleanup, content type repair, public API,
  or complete object preservation;
  explicit removal coverage for the same OLE/control fixture: omits
  `xl/embeddings/oleObject1.bin` while removing the OLE content type override,
  omits `xl/ctrlProps/control1.xml` while removing the control-properties
  content type override, preserves the worksheet `.rels` inbound relationships
  that still point at the missing OLE object or control properties part, and
  exposes structured removed-part inbound relationship audit metadata in
  `EditPlan` / planned output; this is Patch audit / no-pruning visibility, not
  OLE / ActiveX / control deletion semantics, relationship repair/pruning,
  orphan cleanup, content type repair, public API, or complete object lifecycle
  support;
  same-path ordering coverage for the same worksheet-owned object fixture:
  remove-then-ordinary-replace for `xl/embeddings/oleObject1.bin` clears stale
  removed-part audit, restores an active OLE replacement, restores the OLE
  content type override / `[Content_Types].xml` source-copy audit state, and
  preserves the worksheet `.rels` inbound relationship; ordinary replacement
  followed by explicit removal of `xl/ctrlProps/control1.xml` clears the active
  control replacement, records removed-part inbound audit, omits the
  control-properties part, removes the control-properties content type override,
  and preserves the worksheet `.rels` inbound relationship plus sibling OLE
  part. The aggregate `planned_output()` snapshot now also covers both final
  states: the OLE remove-then-replace path exposes the active OLE
  `LocalDomRewrite`, content-types metadata / `ContentTypes` audit role,
  sibling control preservation, no stale removals, no relationship target
  audits, no fullCalcOnLoad request, `CalcChainAction::Preserve`, and no
  invented OLE owner `.rels`; the control replace-then-remove path exposes the
  omitted control part, removed-part inbound audit, content-types metadata
  rewrite, sibling OLE preservation, no relationship target audits, no
  fullCalcOnLoad request, `CalcChainAction::Preserve`, and no invented control
  owner `.rels`; this is Patch same-path state hygiene / audit visibility, not
  transactional undo, OLE / ActiveX / control semantic merge or deletion,
  relationship repair/pruning, orphan cleanup, content type repair, public API,
  or complete object lifecycle support;
  explicit removal with a malformed unrelated relationship target now verifies
  notes-only EditPlan / planned-output audit, an omitted target part,
  copy-original metadata entries, no structured relationship target / worksheet
  reference audit, no package-entry rewrite/omission, and unchanged calc policy;
  this is not relationship repair;
  a source worksheet with self-closing `<sheetData/>`, which is replaced by
  normal `<sheetData>...</sheetData>` while preserving dimension / autoFilter,
  default calcChain removal, workbook fullCalcOnLoad, and unknown bytes;
  a replacement payload that is itself self-closing `<sheetData/>`, which
  clears the old rows/cells while preserving the worksheet wrapper, keeps the
  self-closing `sheetData` in the output, and follows the same calcChain /
  fullCalcOnLoad / unknown-bytes path;
  prefixed source and replacement XML such as `<x:worksheet>` /
  `<x:sheetData>`, where matching is by local name and output preserves the
  literal prefixes without implying namespace repair;
  the internal sheet-name convenience path, which resolves the target via the
  reader workbook sheet catalog before delegating to the same part-level
  `sheetData` replacement path and fails missing or ambiguous duplicate names
  without state pollution;
  a real FastXLSX writer roundtrip, where `WorkbookWriter` creates a two-sheet
  source package, `PackageReader` resolves the writer sheet catalog,
  `PackageEditor::replace_worksheet_sheet_data_by_name()` patches only the
  target `<sheetData>`, and `PackageReader` reopens the saved package to verify
  the untouched worksheet, `[Content_Types].xml`, package `_rels/.rels`,
  workbook `.rels`, and core/app docProps bytes are preserved; it also verifies
  writer-generated worksheet XML declaration/prolog byte preservation through the
  sheetData patch, with the `<worksheet>` root kept immediately after that prolog.
  The source now also
  exercises writer-generated `xl/sharedStrings.xml` and `xl/styles.xml`,
  verifying those parts, their content type overrides, and workbook
  relationships are byte-preserved while replacement `t="s"` / `s` references
  are exposed through structured `WorksheetPayloadDependencyAudit` entries.
  Workbook XML requests `fullCalcOnLoad="1"`, and no `xl/calcChain.xml` is
  created when the source writer package had none; this remains an audit and
  preservation proof, not sharedStrings index migration or style id migration;
  the same resolver now also backs internal
  `replace_worksheet_part_by_name()`, which delegates to the existing worksheet
  replacement path and keeps the same calcChain/fullCalcOnLoad and preservation
  side effects; the source-catalog regression also covers dot-segment package
  `officeDocument` and workbook-owned worksheet targets for this full-replacement
  path; invalid package `officeDocument` entrypoints fail before
  by-name Patch state changes and do not imply arbitrary workbook part-location
  support;
  if a planned `/xl/workbook.xml` exists in the same edit, either as an ordinary
  workbook replacement or after that replacement is taken over by
  `request_full_calculation()` / worksheet-rewrite fullCalcOnLoad metadata
  helpers, both by-name helpers resolve against the current planned workbook
  sheet catalog; old source sheet names fail before state changes, while new
  planned sheet names can still target existing worksheet parts through source
  workbook relationships; the planned-catalog regressions now cover both
  ordinary planned workbook state and helper-managed planned workbook metadata
  state, across by-name worksheet replacement and by-name `sheetData`, when
  source workbook `.rels` uses a dot-segment worksheet target such as
  `./worksheets/../worksheets/sheetN.xml`, preserving that target text on output
  while calcChain cleanup may still rewrite workbook `.rels`; missing or
  namespace-invalid planned sheet id attributes still fail without state
  pollution: alternate prefixes bound to the officeDocument relationships
  namespace are accepted, while wrong-namespace `x:id` and unqualified `id` are
  treated as missing; namespace-valid planned sheet ids absent from workbook
  `.rels`, and planned sheet relationships resolving to unregistered worksheet
  parts, also fail before by-name Patch state changes across both by-name
  helpers; a reader-only regression also directly covers the internal
  `PackageReader::workbook_sheets_from_xml()` planned-workbook XML path with
  the same direct-catalog and namespace rules, exposing only direct
  `<sheets><sheet>` entries, ignoring decoy outer/nested sheet tags, accepting
  alternate prefixes bound to the officeDocument relationships namespace, and
  treating wrong-namespace or unqualified `id` attributes as missing, plus
  rejecting planned sheet ids absent from workbook relationships and planned
  worksheet relationships resolving to unregistered parts; this is
  only a narrow planned workbook catalog resolver,
  not sheet rename/delete, sheet catalog mutation, relationship repair, or a
  public API;
  if `/xl/workbook.xml` has been explicitly removed in the same planned edit,
  both by-name helpers now fail before catalog resolution, preserve the existing
  part-removal and owner `.rels` omission state, and do not fall back to the
  source workbook catalog or resurrect workbook metadata;
  internal `PackageEditor::rename_sheet_catalog_entry()` now covers only the
  workbook sheet catalog `name` attribute rewrite: it resolves against the
  current planned `/xl/workbook.xml`, rewrites that workbook part as
  `LocalDomRewrite`, preserves worksheet parts, workbook `.rels`, content
  types, calcChain, and unknown entries, and records an audit note that
  definedNames, formulas, tables, drawings, charts, hyperlinks, relationship
  targets, sharedStrings, styles, and calcChain are not synchronized. Current
  regression coverage includes planned workbook XML, output-plan visibility for
  the final workbook `LocalDomRewrite`, preserved content types / workbook
  `.rels` / worksheet / calcChain / unknown entry, structured sheet catalog /
  definedNames audit, output preservation, XML escaping, and failure without
  state pollution for invalid planned workbook catalogs whose planned sheet
  relationship id is absent from workbook `.rels` or resolves to an
  unregistered worksheet part. Those failures keep the queued workbook
  replacement, audits, manifest, calc policy, package-entry audit, and output
  bytes unchanged. Existing failure coverage also includes missing old names,
  exact or
  ASCII case-insensitive duplicate new names, invalid names, direct
  `definedNames` under `ReferencePolicyAction::Fail`, and prior
  planned workbook removal. This is not full sheet rename/add/delete,
  relationship repair, calc metadata rewrite, or a public API;
  full-worksheet replacement-payload audit notes and structured
  `WorksheetPayloadDependencyAudit` records for shared string indexes,
  style id references, formula cells, range/reference worksheet metadata such as
  sheetPr, sheetCalcPr, dimension, sheetViews, customSheetViews,
  sheetFormatPr, cols, sheetProtection, protectedRanges, sortState,
  autoFilter, mergeCells, scenarios, dataConsolidate, customProperties,
  cellWatches, smartTags, webPublishItems, dataValidations, conditionalFormatting,
  ignoredErrors, printOptions, pageMargins, pageSetup, headerFooter,
  rowBreaks, colBreaks, phoneticPr, and extLst, and relationship-bearing
  worksheet metadata such as hyperlinks, drawing, legacyDrawing, picture,
  legacyDrawingHF, oleObjects, controls, and tableParts;
  the existing `sheetData` helper also keeps its replacement-payload audit notes
  and structured payload dependency records for shared string indexes, style id
  references, and formula cells, plus
  invalid/malformed replacement XML and missing `sheetData` no-state-pollution.
  Current tests also cover malformed source `sheetData`
  where the start tag exists but the closing tag is missing, and verify this
  fails without changing EditPlan, manifest, package-entry audit, calc policy,
  or copied output bytes; this is not XML repair. Current tests also cover
  composition after a queued worksheet replacement, proving the helper patches
  the current planned worksheet bytes for both normal `<sheetData>` and
  self-closing `<sheetData/>`, and does not resurrect source-only worksheet metadata.
  These notes only prompt caller review of
  `xl/sharedStrings.xml`, `xl/styles.xml`, workbook calc metadata, calcChain
  policy, worksheet `.rels`, and linked parts; they do not migrate sharedStrings
  indexes, merge styles, evaluate formulas, rebuild calcChain, or repair
  relationships. It is still
  not a public API, random cell editor, range repair, dataValidations /
  conditionalFormatting / hyperlinks / table / drawing semantic sync,
  sharedStrings/styles migration, or large-worksheet streaming transformer.
  Invalid replacement failures now also verify no state pollution in edit-plan
  entries/notes, manifest write modes, and copied output bytes.
  Ordinary `replace_part()` now rejects `[Content_Types].xml`, package
  `_rels/.rels`, and source-owned `.rels` metadata entries as ordinary part
  replacement targets, keeping those changes on the narrow metadata-aware
  helper/package-entry audit path rather than the generic part path.
  That rejection path now also checks edit-plan entries/notes, package-entry
  audit, calc policy, manifest write modes, and copied output stay unchanged.
  Internal `PartRewritePlanner` can now plan explicit removal of a registered
  part as removed-part audit metadata, and `EditPlan` has part-level set/remove
  mutual-exclusion coverage, so restoring a previously removed part clears the
  removed-part audit entry instead of leaving conflicting plan state. Rewriting
  an existing relationship-derived entry also clears stale relationship
  metadata. Package-entry set/remove now has the same mutual-exclusion
  regression for internal metadata-entry audit records. The
  worksheet calcChain-removal path now consumes the removed-part audit produced
  by `PartRewritePlanner::plan_worksheet_stream_rewrite()`
  before applying the narrow content-type/workbook-relationship side effects.
  The editor now preserves source content type defaults/overrides while building
  its internal manifest. Direct part replacements, generated docProps parts,
  and worksheet replacements now mirror write-mode / dirty / generated /
  preserve-original state into the internal manifest for Patch auditing; worksheet
  replacements also mirror workbook metadata rewrites as `LocalDomRewrite`. The
  direct replacement path now also has repeated-replacement coverage proving the
  final replacement bytes, write mode, edit-plan reason, manifest state, and
  preserved source-owned `.rels` audit are upserted rather than duplicated.
  Internal `PackageEditor::remove_part()` now has a narrow explicit registered-part
  removal slice: it accepts only ordinary source package parts, omits the target
  part and its source-owned owner `.rels` when present, records removed-part and
  removed package-entry audit, and rewrites `[Content_Types].xml` only when a
  target override existed. It deliberately does not prune inbound relationships
  from other parts and is not object deletion or transactional editing. The
  removed-part audit now keeps structured inbound package/source relationship
  metadata (owner entry/part, id, type, raw target, normalized target part) plus
  readable reasons when a relationship still points at the removed part, making
  that narrow no-pruning behavior explicit in the Patch trace.
  Malformed percent relationship targets encountered while scanning for inbound
  removed-part references now become EditPlan / planned output audit notes; the
  source `.rels` bytes are still preserved and unrelated explicit part removal
  is not blocked. The planned output snapshot also exposes the removed target
  omission and copy-original metadata entries without adding structured
  relationship target audits.
  A sibling workbook-removal regression covers `xl/workbook.xml`: the output
  omits the workbook part and its source-owned workbook `.rels`, removes the
  workbook content type override, preserves the inbound package
  `officeDocument` relationship, and does not prune worksheet/drawing/table/
  sharedStrings/styles/VBA/calcChain or unknown extension downstream/source
  parts. This is no-pruning traceability, not workbook deletion, sheet catalog
  sync, relationship repair, or complete workbook editing.
  A sibling worksheet-removal regression covers `xl/worksheets/sheet1.xml`: the
  output omits the worksheet part and its source-owned worksheet `.rels`, removes
  the worksheet content type override, preserves the inbound workbook
  relationship, and does not prune drawing/table/sharedStrings/styles/VBA/
  calcChain or unknown extension downstream/source parts. This is no-pruning
  traceability, not sheet delete, workbook sheet catalog sync, relationship
  repair, or complete worksheet editing.
  A sibling drawing-removal regression covers `xl/drawings/drawing1.xml`: the
  output omits the drawing part and its source-owned drawing `.rels`, removes the
  drawing content type override, preserves direct and URI-qualified inbound
  worksheet relationships, and does not prune chart/media or other downstream
  parts. This is no-pruning traceability, not drawing mutation, object deletion,
  relationship repair, or complete drawing support.
  A sibling media-removal regression covers the default-typed
  `xl/media/image1.png` case: the output omits the media entry, preserves the
  PNG default content type and inbound drawing relationship, and does not invent
  a media owner `.rels` omission. This is no-pruning traceability, not image
  editing or relationship repair.
  A sibling table-removal regression covers `xl/tables/table1.xml`: the output
  omits the table entry, removes the table content type override, preserves the
  inbound worksheet relationship, and does not invent a table owner `.rels`
  omission. This is no-pruning traceability, not table resize, table editing, or
  relationship repair.
  A sibling sharedStrings-removal regression covers `xl/sharedStrings.xml`: the
  output omits the sharedStrings part and its owner `.rels`, removes the
  sharedStrings content type override, and preserves the inbound workbook
  relationship. This is no-pruning traceability, not sharedStrings index
  migration, string-table rebuild, worksheet cell-reference sync, relationship
  repair, or existing-file sharedStrings semantic editing.
  A sibling styles-removal regression covers `xl/styles.xml`: the output omits
  the styles part, removes the styles content type override, preserves the
  inbound workbook relationship, and does not invent a styles owner `.rels`
  omission. This is no-pruning traceability, not style id migration, style
  merging, cell `s` reference sync, relationship repair, or existing-file style
  preservation.
  A sibling VBA-removal regression covers `xl/vbaProject.bin`: the output omits
  the VBA project part, removes the VBA content type override, preserves the
  inbound workbook relationship, and does not invent a VBA owner `.rels`
  omission. This is no-pruning traceability, not macro generation, VBA semantic
  editing, signature preservation, relationship repair, or complete macro
  support.
  A sibling VML-removal regression covers `xl/drawings/vmlDrawing1.vml`: the
  output omits the VML drawing part, removes the VML content type override,
  preserves the URI-qualified inbound worksheet relationship, and does not invent
  a VML owner `.rels` omission. This is no-pruning traceability, not VML shape
  editing, legacy drawing mutation, relationship repair, or complete VML/drawing
  support.
  A sibling percent-decoded drawing removal regression covers
  `xl/drawings/drawing space.xml`: the output omits the target drawing part,
  removes the drawing content type override, preserves the source worksheet
  `.rels` target `../drawings/drawing%20space.xml`, and does not invent
  `xl/drawings/_rels/drawing space.xml.rels`. This is no-pruning traceability,
  not percent-encoded target repair, relationship rewrite, drawing mutation, or
  complete drawing support.
  The removal regression now also covers later `remove_part()` overriding a prior
  ordinary replacement, clearing stale replacement state, and using removed-part
  audit plus content type cleanup as the final state. Invalid removal attempts
  now have no-state-pollution coverage for edit-plan entries/notes, package-entry
  audit, removed audit, calc policy, manifest write modes, and copied output bytes.
  The reverse ordering is now covered too: a later ordinary replacement can
  restore a previously removed source package part as the active final state,
  clear stale removed-part / removed owner `.rels` audit plus omitted entry state,
  and preserve source-owned `.rels` as copy-original metadata. Override-bearing
  restored parts now also cover `[Content_Types].xml` returning to source bytes /
  copy-original audit. This is not transactional undo, relationship repair,
  content type repair, semantic merge, or public editing API.
  Another regression verifies docProps generated-small-XML parts can be
  superseded by later ordinary part replacements while content-types and package
  relationships audit remains helper-managed. A sibling regression verifies the
  docProps helper can also take over a prior explicit core/app part removal,
  clear stale removed/omitted payload state, and restore the generated part
  entry in the output package.
  A sibling regression verifies a later docProps generated-small-XML helper can
  take ownership after explicit core properties removal by clearing stale
  removed-part / omitted-payload state and restoring generated core/app payloads;
  it also verifies the output still omits the source-owned docProps `.rels`
  and keeps the removed package-entry audit. This does not imply transactional undo.
  Another workbook-specific reverse-order regression verifies that ordinary
  `replace_part("/xl/workbook.xml", ...)` after explicit workbook removal restores
  the active workbook replacement, restores source-owned workbook `.rels` as
  copy-original audit, preserves the inbound package `officeDocument`
  relationship, and returns `[Content_Types].xml` to source bytes /
  copy-original audit. This is not transactional undo or relationship repair.
  The reverse workbook-specific ordering is now covered too: a later explicit
  removal after ordinary `replace_part("/xl/workbook.xml", ...)` clears the
  active workbook replacement, records removed-part and workbook owner `.rels`
  omission audit, omits the workbook part and owner `.rels`, removes the
  workbook content type override, preserves the package `_rels/.rels`
  officeDocument relationship pointing at the missing workbook, and preserves
  worksheet/drawing/table/sharedStrings/styles/VBA/calcChain/unknown downstream
  parts. This is not workbook deletion semantics, sheet catalog sync,
  relationship/content type repair, orphan cleanup, transactional undo, or
  public API.
  A worksheet-specific sibling regression verifies that ordinary
  `replace_part("/xl/worksheets/sheet1.xml", ...)` after explicit worksheet
  removal restores the active worksheet replacement, restores source-owned
  worksheet `.rels` as copy-original audit, preserves the inbound workbook
  worksheet relationship, and returns `[Content_Types].xml` to source bytes /
  copy-original audit. This is not sheet delete, transactional undo, or
  relationship repair.
  The reverse worksheet-specific ordering is now covered too: a later explicit
  removal after ordinary `replace_part("/xl/worksheets/sheet1.xml", ...)`
  clears the active worksheet replacement, records removed-part and worksheet
  owner `.rels` omission audit, omits the worksheet part and owner `.rels`,
  removes the worksheet content type override, preserves the workbook inbound
  worksheet relationship pointing at the missing worksheet, and preserves
  drawing/chart/media/table/sharedStrings/styles/VBA/calcChain/unknown
  downstream/source parts. This is not sheet delete, workbook sheet catalog
  sync, relationship/content type repair, orphan cleanup, transactional undo,
  or public API.
  A drawing-specific sibling regression verifies that ordinary
  `replace_part("/xl/drawings/drawing1.xml", ...)` after explicit drawing
  removal restores the active drawing replacement, restores source-owned drawing
  `.rels` as copy-original audit, preserves direct and URI-qualified inbound
  worksheet drawing relationships, and returns `[Content_Types].xml` to source
  bytes / copy-original audit. This is not drawing mutation, object deletion,
  transactional undo, or relationship repair.
  The reverse drawing-specific ordering is now covered too: a later explicit
  removal after ordinary `replace_part("/xl/drawings/drawing1.xml", ...)`
  clears the active drawing replacement, records removed-part and drawing owner
  `.rels` omission audit, omits the drawing part and owner `.rels`, removes the
  drawing content type override, preserves direct and URI-qualified inbound
  worksheet drawing relationships, and preserves chart/media/table/VML/
  percent-decoded drawing/sharedStrings/styles/VBA/calcChain/unknown
  downstream/source parts. This is not drawing mutation, object deletion,
  relationship/content type repair, orphan cleanup, transactional undo, or
  public API.
  A sharedStrings-specific sibling regression verifies that ordinary
  `replace_part("/xl/sharedStrings.xml", ...)` after explicit sharedStrings
  removal restores the active sharedStrings replacement, restores source-owned
  sharedStrings `.rels` as copy-original audit, preserves the inbound workbook
  sharedStrings relationship, and returns `[Content_Types].xml` to source bytes /
  copy-original audit. This is not sharedStrings index migration, string-table
  rebuild, worksheet cell-reference sync, transactional undo, relationship
  repair, content type repair, semantic merge, or public editing API.
  A styles-specific sibling regression verifies that ordinary
  `replace_part("/xl/styles.xml", ...)` after explicit styles removal restores
  the active styles replacement, preserves the inbound workbook styles
  relationship, does not invent styles owner `.rels`, and returns
  `[Content_Types].xml` to source bytes / copy-original audit. This is not style
  id migration, style merge, cell `s` reference sync, transactional undo,
  relationship repair, content type repair, semantic merge, existing-file style
  preservation, or public editing API.
  Another ordering regression verifies a later ordinary workbook replacement
  after worksheet rewrite keeps the existing `fullCalcOnLoad` / calcChain policy
  and does not downgrade already-rewritten workbook `.rels` audit metadata.
  The
  worksheet-replacement fixture confirms calcChain override removal does not
  promote PNG media defaults to image overrides.
  Internal `EditPlan` package-entry audit now records current content-types,
  package-relationship, workbook-relationship, calcChain owner `.rels` omission,
  and preserved source-owned `.rels` copy-original side effects when present, including ordinary
  owner-part replacement for root-level `_rels/foo.xml.rels`, calcChain
  owner `.rels` preservation under `CalcChainAction::Preserve` and workbook `.rels`
  preservation during workbook metadata rewrite. The audit is now structured as
  content-types, package-relationships, or source-owned relationships, and only
  source-owned relationship entries carry `owner_part`; the internal plan entry
  validates the matching entry path for each kind, without making those entries
  public API. The root-level owner replacement fixture now also verifies those
  preserved source-owned `.rels` bytes are re-ingested by `PackageReader` /
  `RelationshipGraph` as owner relationships after roundtrip. Separate
  reader-only coverage now verifies the same owner `.rels` ingestion shape for
  an unknown extension part before copy/replace.
  A no-op `PackageEditor::save_as()` roundtrip now separately verifies that all
  source entries in the linked-object fixture preserve entry order, stored entry
  method / CRC / uncompressed size, and bytes, with the initial edit plan staying
  at copy-original part entries and no metadata package-entry side effects. This
  is an unmodified-package copy baseline, not broad safe-editing preservation.
  Internal `PackageEditor::planned_output()` now exposes the aggregate output
  plan snapshot consumed by `save_as()`, including entry-level decision order,
  global `full_calculation_on_load` / `calc_chain_action`, audit notes,
  structured `removed_parts` / `removed_package_entries`, and structured
  `relationship_target_audits` /
  `worksheet_relationship_reference_audits`. The compatibility
  `planned_output_entries()` wrapper still returns the same entry list. Entry
  decisions include `source_entry` / `generated` flags, `package_part` /
  `part_name` classification, write mode, copied-from-source / omitted flags,
  package-entry audit kind, owner part, relationship-derived
  owner/id/type/target fields, omitted removed-part inbound relationship audits,
  and reason.
  Current tests cover no-op copy-original,
  docProps generated-small-XML additions, worksheet calcChain omission with
  workbook metadata rewrites, sheetData Patch MVP output-plan snapshots for
  worksheet stream-rewrite, workbook metadata rewrite, calcChain omission,
  metadata-entry audit, preserved source-owned `.rels`, and
  relationship-derived linked parts including printerSettings, ordinary unknown extension replacement
  output snapshots for target part stream-rewrite, owner `.rels` copy-original
  audit, and untouched linked parts, unknown extension replace-then-remove
  final-removal output-plan audit for omitted unknown part, source-owned owner
  `.rels` omission, inbound worksheet relationship metadata, and preserved
  content types / package relationships, and explicit workbook removal owner `.rels`
  omission, plus linked worksheet rewrite relationship-derived output audit,
  request-recalculation preserve-policy output snapshots, drawing replace-then-
  remove final-removal output-plan audit for omitted drawing part / owner
  `.rels` and inbound worksheet relationships, VML drawing replace-then-remove
  final-removal output-plan audit for omitted VML part, URI-qualified inbound
  worksheet relationship metadata, removed_parts target/reason/inbound audit,
  content types rewrite, empty removed_package_entries, and no invented owner
  `.rels`, VML drawing remove-then-replace output-plan audit for active VML
  drawing local-DOM rewrite, content types copy-original audit, preserved
  package/workbook/worksheet/drawing relationships, preserved linked/unknown
  entries, empty removed_parts and removed_package_entries, and no invented
  owner `.rels`, percent-decoded drawing
  replace-then-remove final-removal output-plan audit for omitted decoded drawing part,
  removed_parts target/reason/inbound audit, encoded inbound worksheet relationship
  metadata, content types rewrite, empty removed_package_entries, and no invented
  owner `.rels`, percent-decoded drawing remove-then-replace
  output-plan audit for active decoded drawing local-DOM rewrite, content types
  copy-original audit, preserved package/workbook/worksheet/drawing relationships,
  preserved linked/unknown entries, and no invented owner `.rels`, media replace-then-remove
  final-removal output-plan audit for omitted default-typed media part,
  removed_parts target/reason/inbound audit, drawing inbound relationship
  metadata, preserved content types / drawing `.rels`, empty
  removed_package_entries, and no invented media owner `.rels`, table replace-then-remove final-removal
  output-plan audit for omitted table part, worksheet inbound relationship
  metadata, content types rewrite, and no invented table owner `.rels`, pivot
  worksheet rewrite output-plan audit for fullCalcOnLoad /
  `CalcChainAction::Remove`, worksheet `StreamRewrite`, workbook
  `LocalDomRewrite`, package/workbook/worksheet `.rels` copy-original,
  pivot table/cache relationship context, content types and unknown entry
  copy-original preservation, and no invented records owner `.rels`,
  pivot table remove-then-replace output-plan audit for active pivot table
  local-DOM rewrite, owner `.rels` copy-original audit, content types
  copy-original audit, preserved package/worksheet/workbook relationships, and
  preserved pivot cache definition / records chain, pivot table
  replace-then-remove final-removal output-plan audit for omitted pivot table
  part, owner `.rels`, worksheet inbound pivotTable relationship metadata,
  content types rewrite, and preserved pivot cache definition / records chain,
  pivot cache definition remove-then-replace output-plan audit for active cache
  definition local-DOM rewrite, owner `.rels` copy-original audit, content types
  copy-original audit, and preserved workbook/worksheet/pivot table/cache records/
  unknown entries, pivot cache definition replace-then-remove final-removal
  output-plan audit for omitted cache definition part, owner `.rels`, workbook /
  pivot table inbound pivotCacheDefinition relationship metadata, content types
  rewrite, and preserved workbook/worksheet/pivot table/cache records/unknown
  entries, pivot cache records remove-then-replace output-plan audit for active
  records stream-rewrite, content types copy-original audit, preserved pivot
  table/cache definition chain, and no invented records owner `.rels`, pivot
  cache records replace-then-remove final-removal output-plan audit for omitted
  records part, cache-definition inbound pivotCacheRecords relationship metadata,
  content types rewrite, preserved cache definition owner `.rels`, and no
  invented records owner `.rels`,
  comments ordinary replacement output-plan audit for active comments part
  local-DOM rewrite, preserved content types / package relationships / workbook /
  workbook `.rels` / worksheet / worksheet `.rels` / unknown entry, and no
  invented comments owner `.rels`,
  comments replace-then-remove final-removal output-plan audit for omitted
  comments part, removed_parts target/reason/inbound audit, worksheet inbound
  relationship metadata, content types rewrite, preserved package/workbook/worksheet
  `.rels`, empty removed_package_entries, and no invented comments owner `.rels`,
  comments remove-then-replace output-plan audit for active comments part
  local-DOM rewrite, content types copy-original audit, preserved
  package/workbook/worksheet `.rels` plus unknown entry, empty removed_parts /
  removed_package_entries, and no invented comments owner `.rels`,
  threaded comments ordinary replacement output-plan audit for active threaded
  comments part local-DOM rewrite, preserved content types / package
  relationships / workbook / workbook `.rels` / worksheet / worksheet `.rels` /
  legacy comments / persons part / unknown entry, and no invented threaded
  comments owner `.rels`,
  threaded comments replace-then-remove final-removal output-plan audit for
  omitted threaded comments part, removed_parts target/reason/inbound audit,
  worksheet inbound threadedComment relationship metadata, content types rewrite,
  preserved worksheet/workbook `.rels` plus persons part, empty
  removed_package_entries, and no invented threaded comments owner `.rels`, threaded comments
  remove-then-replace output-plan audit for active threaded comments part local-DOM
  rewrite, content types copy-original audit, preserved package/workbook/worksheet
  `.rels`, legacy comments, persons part, unknown entry, empty removed_parts /
  removed_package_entries, and no invented threaded comments owner `.rels`,
  persons replace-then-remove final-removal output-plan audit for omitted
  persons part, removed_parts target/reason/inbound audit, workbook inbound
  relationship metadata, content types rewrite, preserved workbook/worksheet
  `.rels` plus threaded comments part, empty removed_package_entries, and no
  invented persons owner `.rels`, persons remove-then-replace output-plan audit
  for active persons part local-DOM rewrite, content types copy-original audit,
  preserved package/workbook/worksheet `.rels`, threaded comments, legacy
  comments, unknown entry, empty removed_parts / removed_package_entries, and
  no invented persons owner `.rels`,
  sharedStrings remove-then-replace output-plan audit for active sharedStrings
  stream-rewrite, source-owned owner `.rels` copy-original audit, content types
  copy-original audit, preserved workbook relationships, and empty
  removed_parts / removed_package_entries, sharedStrings replace-then-remove
  final-removal output-plan audit for omitted sharedStrings part, matching
  removed_parts target/reason/inbound audit, source-owned owner `.rels`
  omission, removed_package_entries owner-omission audit, workbook inbound
  relationship metadata, and content types rewrite, styles remove-then-replace output-plan
  audit for active styles local-DOM rewrite, content types copy-original audit,
  preserved workbook relationships, empty removed_parts / removed_package_entries,
  and no invented owner `.rels`, styles
  replace-then-remove final-removal output-plan audit for omitted styles part,
  matching removed_parts target/reason/inbound audit, workbook inbound
  relationship metadata, content types rewrite, empty removed_package_entries,
  and no invented owner `.rels`,
  VBA project remove-then-replace output-plan audit for active VBA project
  stream-rewrite, content types copy-original audit, preserved package/workbook
  relationships, empty removed_parts / removed_package_entries, and no invented
  owner `.rels`, VBA project replace-then-remove
  final-removal output-plan audit for omitted VBA project part, matching
  removed_parts target/reason/inbound audit, workbook inbound relationship
  metadata, content types rewrite, empty removed_package_entries, and no invented
  owner `.rels`,
  chart remove-then-replace output-plan audit for active chart stream-rewrite,
  content types copy-original audit, preserved drawing relationships, empty
  removed_parts / removed_package_entries, and no invented owner `.rels`, chart
  replace-then-remove final-removal output-plan audit for omitted chart part,
  matching removed_parts target/reason/inbound audit, drawing-owned direct and
  URI-qualified inbound relationship metadata, content types rewrite, empty
  removed_package_entries, and no invented owner `.rels`,
  custom XML item remove-then-replace output-plan audit for active custom XML
  item local-DOM rewrite, owner `.rels` copy-original audit, and preserved
  package relationships, content types, workbook, worksheet, properties part,
  and unknown entry, custom XML item replace-then-remove final-removal
  output-plan audit for omitted item and owner `.rels`, package inbound customXml
  relationship metadata, and preserved package relationships, content types,
  workbook, worksheet, properties part, and unknown entry, custom XML properties
  ordinary replacement output-plan audit for active properties part local-DOM
  rewrite, preserved content types / package relationships, preserved custom XML
  item / item owner `.rels` / workbook / worksheet / unknown entry, and no
  invented properties owner `.rels`, custom XML properties
  remove-then-replace output-plan audit for active properties part local-DOM
  rewrite, restored content types copy-original audit, preserved custom XML item /
  item owner `.rels`, package relationships, workbook, worksheet, and unknown
  entry, and no invented properties owner `.rels`, and explicit chart removal
  inbound audit. This is
  internal Patch audit visibility; metadata entries such as
  `[Content_Types].xml`, package `_rels/.rels`, and source-owned `.rels` are not
  exposed as package parts, and this is not a public editor API or general
  relationship/content-type mutator.
  The ordinary single-part replacement regression now also verifies that the
  rewritten entry stays in the source entry order while untouched entries keep
  their stored method / CRC / uncompressed size and bytes. This is narrow
  part-level rewrite copy-original evidence.
  An ordinary workbook replacement over the linked-object fixture now verifies
  that rewriting only `xl/workbook.xml` records workbook `.rels` as
  copy-original package-entry audit while worksheet, drawing, media,
  sharedStrings, styles, VBA, calcChain, and unknown extension entries keep the
  same copy-original baseline.
  The same workbook path now also verifies ordinary-replace-then-remove
  ordering: later explicit removal clears the active workbook replacement,
  records removed-part and workbook owner `.rels` omission audit, omits
  `xl/workbook.xml` and `xl/_rels/workbook.xml.rels`, removes the workbook
  content type override, preserves the package officeDocument relationship, and
  preserves downstream/source parts. This is not workbook deletion semantics,
  sheet catalog sync, relationship/content type repair, orphan cleanup,
  transactional undo, or public API.
  An ordinary drawing replacement over the linked-object fixture now verifies
  that rewriting only `xl/drawings/drawing1.xml` records drawing `.rels` as
  copy-original package-entry audit while chart, media, and unknown extension
  entries keep the same copy-original baseline. This is not drawing mutation,
  image editing, or chart editing.
  An ordinary unknown extension replacement over the same fixture now verifies
  that rewriting only `custom/opaque-extension.bin` records its owner `.rels`
  as copy-original package-entry audit while workbook, worksheet, drawing,
  chart, and media entries keep the same copy-original baseline. This is not
  semantic unknown extension editing, custom relationship repair, or public API.
  A repeated ordinary replacement over that same unknown extension now verifies
  final bytes, manifest write mode, edit-plan reason, and owner `.rels` audit are
  upserted to the last replacement state, while the owner `.rels` remains
  copy-original and no removed-part or removed package-entry audit is created.
  This is not transactional editing or unknown extension semantic merging.
  The same fixture now covers unknown extension remove-then-ordinary-replace
  ordering: a later replacement restores the active unknown extension part,
  clears stale removed-part and removed owner `.rels` audit, restores owner
  `.rels` copy-original package-entry audit, preserves the inbound unknown
  relationship in worksheet `.rels`, keeps other linked/source entries on the
  copy-original baseline, and does not rewrite `[Content_Types].xml`. This is
  not unknown extension semantic merge, custom relationship repair, metadata
  repair, transactional undo, or public API.
  The same path now also covers unknown extension ordinary-replace-then-remove
  ordering: a later removal clears the active replacement, records removed-part
  and removed owner `.rels` audit, omits the unknown extension part and owner
  `.rels`, preserves the worksheet inbound relationship that still points at
  the missing part, keeps other linked/source entries and the default `bin`
  content type, and does not rewrite `[Content_Types].xml`. This is not unknown
  extension deletion semantics, custom relationship repair, metadata repair,
  relationship pruning/repair, content type repair, orphan cleanup,
  transactional undo, or public API.
  An ordinary media replacement over the same fixture now verifies that
  rewriting only `xl/media/image1.png` preserves drawing `.rels`, keeps the PNG
  default content type from being promoted to an override, and leaves workbook,
  worksheet, drawing, chart, and unknown extension entries on the same
  copy-original baseline. This is not image decoding, drawing mutation, or
  existing-workbook image editing.
  The same path now covers default-typed media remove-then-ordinary-replace
  ordering: a later replacement restores the active media part, clears stale
  removed-part audit, keeps the PNG default content type without promoting
  `xl/media/image1.png` to an override, preserves inbound drawing `.rels`, and
  does not invent media owner `.rels`. This is not transactional undo, image
  semantic merging, relationship repair, content type repair, or full image
  preservation.
  The same path now also covers media ordinary-replace-then-remove ordering: a
  later removal clears the active media replacement, records removed-part audit
  and inbound drawing relationship metadata, omits `xl/media/image1.png`, keeps
  the PNG default content type without promoting the media part to an override,
  preserves inbound drawing `.rels`, and does not invent media owner `.rels`.
  The internal output-plan snapshot also exposes the omitted media part,
  matching removed_parts target/reason/inbound audit, drawing inbound audit,
  content types / drawing `.rels` copy-original decisions, empty
  removed_package_entries, and absence of any invented media owner `.rels`.
  This is not transactional undo, image semantic merging, relationship pruning/repair, content type repair,
  existing-workbook image editing, or full image preservation.
  An ordinary table replacement over the same fixture now verifies that
  rewriting only `xl/tables/table1.xml` preserves worksheet `.rels`, keeps the
  table content type override readable, and leaves workbook, worksheet, drawing,
  chart, media, and unknown extension entries on the same copy-original
  baseline. This is not table resize, calculated columns, totals generation, or
  existing-workbook table editing.
  The same path now covers table remove-then-ordinary-replace ordering: a later
  replacement restores the active table part, clears stale removed-part audit,
  returns `[Content_Types].xml` to source/copy-original audit, preserves the
  inbound worksheet `.rels` table relationship, and does not invent table owner
  `.rels`. This is not table resize, calculated columns, totals generation,
  transactional undo, relationship repair, content type repair, or
  existing-workbook table editing.
  The same path now covers table ordinary-replace-then-remove ordering: a later
  explicit removal clears the active table replacement, records removed-part
  audit and inbound worksheet relationship metadata, omits the table part from
  output, removes the table content type override, preserves the inbound
  worksheet `.rels` table relationship, and does not invent table owner `.rels`.
  The internal output-plan snapshot also exposes the omitted table part,
  worksheet inbound audit, content types local-DOM rewrite, and absence of any
  invented table owner `.rels`. This is not table delete semantics, table
  resize, calculated columns, totals generation, transactional undo,
  relationship pruning/repair, content type repair, or existing-workbook table
  editing.
  An ordinary sharedStrings replacement over the same fixture now verifies that
  rewriting only `xl/sharedStrings.xml` preserves workbook `.rels`,
  sharedStrings owner `.rels`, and the sharedStrings content type override, and
  leaves styles, table, media, VBA, and unknown extension entries on the same
  copy-original baseline. This is not sharedStrings index migration,
  string-table rebuild, worksheet cell-reference sync, or existing-workbook
  sharedStrings semantic editing.
  The same path now covers sharedStrings ordinary-replace-then-remove ordering:
  a later removal clears the active sharedStrings replacement, records
  removed-part audit, omits `xl/sharedStrings.xml` and its source-owned owner
  `.rels` from output, removes the sharedStrings content type override, and
  preserves the inbound workbook `.rels` sharedStrings relationship. It does not
  prune worksheet `t="s"` references or rebuild the string table. This is not
  sharedStrings index migration, string-table rebuild, worksheet cell-reference
  sync, transactional undo, relationship pruning or repair, content type repair,
  existing-file sharedStrings semantic editing, or public API.
  The internal output-plan snapshot also exposes the final omitted
  `xl/sharedStrings.xml` part, source-owned owner `.rels` omission, workbook
  inbound relationship metadata, and content types rewrite for that final
  removal; this is audit visibility only, not metadata repair or public API.
  An ordinary styles replacement over the same fixture now verifies that
  rewriting only `xl/styles.xml` preserves workbook `.rels`, keeps the styles
  content type override readable, does not invent `xl/_rels/styles.xml.rels`,
  and leaves sharedStrings, table, media, VBA, and unknown extension entries on
  the same copy-original baseline. This is not style id migration, style merge,
  cell `s` reference sync, existing-file style preservation, or full style editing.
  The same path now covers styles ordinary-replace-then-remove ordering: a later
  removal clears the active styles replacement, records removed-part audit, omits
  `xl/styles.xml` from output, removes the styles content type override, preserves
  the inbound workbook `.rels` styles relationship, and does not invent
  `xl/_rels/styles.xml.rels`. It does not migrate style ids or rewrite cell `s`
  references. This is not style merge, existing-file style preservation,
  transactional undo, relationship pruning or repair, content type repair, full
  style editing, or public API.
  The same fixture now covers chart remove-then-ordinary-replace ordering: a
  later replacement restores the active chart part, clears stale removed-part
  audit, returns `[Content_Types].xml` to source/copy-original audit, preserves
  direct and URI-qualified inbound chart relationships in drawing `.rels`,
  keeps other linked/unknown source entries on the copy-original baseline, and
  does not invent chart owner `.rels`. This is not chart semantic merge, chart
  reference repair, relationship repair, content type repair, transactional
  undo, existing-workbook chart editing, or public API.
  Internal `planned_output()` coverage for this restore state now exposes the
  active chart `StreamRewrite` entry, source/copy-original content types audit,
  preserved inbound drawing `.rels`, preserved linked/unknown entries, empty
  removed_parts and removed_package_entries, and no invented chart owner `.rels`.
  This is Patch audit only, not a public output planner or chart editing API.
  An ordinary chart replacement over the same fixture now verifies that
  rewriting only `xl/charts/chart1.xml` preserves drawing `.rels` chart and
  URI-qualified chart relationships, keeps the chart content type override
  readable, does not invent chart owner `.rels`, and leaves media, table,
  sharedStrings, styles, VBA, and unknown extension entries on the same
  copy-original baseline. This is not chart reference migration, series/cache
  update, drawing mutation, existing-workbook chart editing, or full chart support.
  The same fixture now covers chart ordinary-replace-then-remove ordering:
  later removal clears the active chart replacement, records removed-part audit
  plus direct and URI-qualified inbound drawing relationship metadata, omits
  `xl/charts/chart1.xml`, removes the chart content type override, preserves
  inbound drawing `.rels` and other linked/unknown source entries, and does not
  invent chart owner `.rels`. This is not chart delete semantics, chart
  reference repair, relationship pruning or repair, content type repair,
  transactional undo, semantic merge, existing-workbook chart editing, or
  public API.
  The internal output-plan snapshot also exposes the omitted
  `xl/charts/chart1.xml` part, matching removed_parts target/reason/inbound
  audit, drawing-owned direct and URI-qualified inbound relationship metadata,
  content types rewrite, empty removed_package_entries, and no invented chart
  owner `.rels`; this is audit visibility only, not chart editing, relationship
  repair, metadata repair, or public API.
  An ordinary VBA project replacement over the same fixture now verifies that
  rewriting only `xl/vbaProject.bin` preserves workbook `.rels` and the VBA
  content type override, does not invent `xl/_rels/vbaProject.bin.rels`, and
  leaves worksheet, drawing, chart, media, table, sharedStrings, styles,
  calcChain, and unknown extension entries on the same copy-original baseline.
  This is not macro generation, VBA semantic editing, signature preservation,
  workbook relationship repair, or full macro support.
  The same path now covers VBA project remove-then-ordinary-replace ordering: a
  later replacement restores the active VBA project part, clears stale
  removed-part audit, returns `[Content_Types].xml` to source/copy-original
  audit, preserves the inbound workbook `.rels` VBA relationship, and does not
  invent `xl/_rels/vbaProject.bin.rels`. This is not macro generation, VBA
  semantic editing, signature preservation, transactional undo, workbook
  relationship repair, content type repair, or full macro support.
  Internal `planned_output()` coverage for this restore state now exposes the
  active VBA project `StreamRewrite` entry, source/copy-original content types
  audit, preserved package/workbook relationships, preserved linked/unknown
  entries, empty removed_parts and removed_package_entries, and no invented VBA
  owner `.rels`. This is Patch audit only, not a public output planner or macro
  editing API.
  The same path now covers VBA project ordinary-replace-then-remove ordering: a
  later removal clears the active VBA replacement, records removed-part audit,
  omits the VBA project part from output, removes the VBA content type override,
  preserves the inbound workbook `.rels` VBA relationship, and does not invent
  `xl/_rels/vbaProject.bin.rels`. This is not macro generation, VBA semantic
  editing, signature preservation, transactional undo, workbook relationship
  repair, content type repair, or full macro support.
  The internal output-plan snapshot also exposes the omitted `xl/vbaProject.bin`
  part, matching removed_parts target/reason/inbound audit, workbook inbound VBA
  relationship metadata, content types rewrite, empty removed_package_entries,
  and no invented `xl/_rels/vbaProject.bin.rels`; this is audit visibility only,
  not macro generation, signature preservation, metadata repair, or public API.
  An ordinary VML drawing replacement over the same fixture now verifies that
  rewriting only `xl/drawings/vmlDrawing1.vml` preserves worksheet `.rels`
  URI-qualified `vmlDrawing` relationship and the VML content type override,
  does not invent `xl/drawings/_rels/vmlDrawing1.vml.rels`, and leaves workbook,
  worksheet, drawing, chart, media, table, sharedStrings, styles, VBA, calcChain,
  and unknown extension entries on the same copy-original baseline. This is not
  VML shape editing, legacy drawing mutation, relationship repair, or full
  VML/drawing support.
  The same path now covers VML drawing remove-then-ordinary-replace ordering: a
  later replacement restores the active VML drawing part, clears stale
  removed-part audit, returns `[Content_Types].xml` to source/copy-original
  audit, preserves the URI-qualified inbound worksheet `.rels` vmlDrawing
  relationship, and does not invent VML owner `.rels`. Internal
  `planned_output()` now also snapshots this restore state as an active VML
  drawing `LocalDomRewrite` entry with content types copy-original audit,
  preserved package/workbook/worksheet/drawing relationships, preserved linked/
  unknown entries, empty removed_parts and removed_package_entries, and no
  invented owner `.rels`. This is not public output planning, VML shape editing,
  legacy drawing mutation, transactional undo, relationship repair, content
  type repair, or full VML/drawing support.
  The same path now covers VML drawing ordinary-replace-then-remove ordering: a
  later removal clears the active VML drawing replacement, records removed-part
  audit, omits the VML drawing part from output, removes the VML content type
  override, preserves the URI-qualified inbound worksheet `.rels` vmlDrawing
  relationship, and does not invent VML owner `.rels`. This is not VML shape
  editing, legacy drawing mutation, transactional undo, relationship pruning or
  repair, content type repair, or full VML/drawing support.
  The internal output-plan snapshot also exposes the omitted
  `xl/drawings/vmlDrawing1.vml` part, matching removed_parts
  target/reason/inbound audit, URI-qualified worksheet inbound relationship
  metadata, content types rewrite, empty removed_package_entries, and no
  invented VML owner `.rels`; this is audit visibility only, not VML editing,
  relationship repair, metadata repair, or public API.
  An ordinary percent-decoded drawing replacement over the same fixture now verifies
  that rewriting only `xl/drawings/drawing space.xml` preserves the worksheet `.rels`
  source target `../drawings/drawing%20space.xml` and drawing content type override,
  does not invent `xl/drawings/_rels/drawing space.xml.rels`, and leaves workbook,
  worksheet, drawing, chart, media, table, VML, sharedStrings, styles, VBA, calcChain,
  and unknown extension entries on the same copy-original baseline. This is not
  percent-encoded target repair, relationship rewrite, drawing mutation, or full
  drawing support.
  The same path now covers percent-decoded drawing remove-then-ordinary-replace
  ordering: a later replacement restores the active decoded drawing part, clears
  stale removed-part audit, returns `[Content_Types].xml` to source/copy-original
  audit, preserves the original encoded inbound worksheet `.rels` target
  `../drawings/drawing%20space.xml`, and does not invent
  `xl/drawings/_rels/drawing space.xml.rels`. Internal `planned_output()` now
  also snapshots this restore state as an active decoded drawing `LocalDomRewrite`
  entry with content types copy-original audit, preserved package/workbook/
  worksheet/drawing relationships, preserved linked/unknown entries, and no
  invented owner `.rels`. This is not public output planning, percent-encoded
  target repair, relationship rewrite, drawing mutation, transactional undo,
  relationship repair, content type repair, or full drawing support.
  The same path now covers percent-decoded drawing ordinary-replace-then-remove
  ordering: a later removal clears the active decoded drawing replacement,
  records removed-part audit, omits the decoded drawing part from output, removes
  the drawing content type override, preserves the original encoded inbound
  worksheet `.rels` target `../drawings/drawing%20space.xml`, and does not invent
  `xl/drawings/_rels/drawing space.xml.rels`. This is not percent-encoded target
  repair, relationship rewrite, drawing mutation, transactional undo, relationship
  pruning or repair, content type repair, or full drawing support.
  A first linked-object preservation fixture now covers worksheet `.rels`,
  drawing, drawing `.rels`, media, chart, table, untouched `xl/sharedStrings.xml`,
  sharedStrings owner `.rels`, untouched `xl/styles.xml`, VBA, and a reachable
  unknown extension part plus its owner `.rels`, plus workbook `definedNames`
  preservation during workbook metadata rewrite. It also confirms
  worksheet-owned and drawing-owned external, URI-qualified, invalid, and
  unresolved relationship target audit notes and structured `RelationshipTargetAudit`
  fields propagate through the existing-file `PackageEditor` edit plan without
  creating or repairing package parts.
  Additional `ReferencePolicy` regressions cover no-state-pollution failure,
  calcChain preserve output, rebuild rejection, malformed workbook metadata
  preflight failure, missing `xl/workbook.xml` worksheet-rewrite precondition
  failure, inbound-linked drawing removal failure, and request-recalculation
  fullCalcOnLoad output intent.
  These failure-path checks now include edit-plan notes and manifest write-mode
  hygiene for the narrow Patch state surface, plus preservation of an already
  queued ordinary workbook replacement when a later linked worksheet rewrite or
  inbound-linked removal strict failure occurs; the inbound-linked removal
  failure also snapshots worksheet/workbook payload audits, removal audits,
  calc policy, aggregate `planned_output()`, and legacy source-copy output
  preview. The base linked worksheet policy failure path now also snapshots
  aggregate `planned_output()` as source copy-original before writing output.
  The queued ordinary workbook replacement failure path now also
  snapshots aggregate `planned_output()` for the workbook rewrite and preserved
  source entries.
  The docProps generated-small-XML path also rejects conflicting core/app
  package relationship targets without state or copied-output pollution; queued
  docProps followed by linked worksheet policy failure now also snapshots
  aggregate `planned_output()` for generated docProps and preserved source
  entries.
- Internal `DependencyAnalyzer` can now conservatively traverse known internal
  relationship targets reachable from worksheet relationships, for example
  worksheet -> table and worksheet -> drawing -> image/chart, and include those parts in the dependency
  summary. Current regressions also cover audit notes discovered through drawing-owned
  `.rels`, while skipping external hyperlink targets as package parts and recording
  an external-target audit note. It also flags URI-qualified, invalid, and
  unresolved internal relationship targets for package structure review, includes
  URI-qualified base targets when they resolve to registered package parts,
  normalizes absolute internal targets as package part paths,
  decodes percent-encoded internal targets before part-name normalization so
  registered decoded targets become dependencies, and avoids inventing package
  parts for invalid percent escapes or unresolved targets. Relationship
  target audit notes include the owner part, relationship id, relationship type,
  original target, and the normalized base target when one is available; this is audit
  traceability, not target validation or relationship repair.
  Unknown relationship types whose normalized internal targets resolve to
  registered parts are conservatively included as dependency / copy-original
  audit decisions without implying custom relationship semantics or editing.
  `PartRewritePlanner` uses that analysis to annotate copy-original `EditPlan`
  entries with concrete dependency reasons, including relationship ids,
  relationship types, normalized target part paths, and workbook calcPr / definedNames review
  context.
  `DependencyAnalysis`, worksheet `EditPlan`, and the existing-file `PackageEditor`
  edit plan also keep structured relationship audit fields:
  `RelationshipTargetAudit` records owner part, relationship id/type, original
  target, normalized target, and note text, while relationship-derived
  `PartDependency` records owner part, relationship id/type, and original
  target. Relationship-derived copy-original `EditPlanEntry` records now also
  preserve those owner/id/type/target fields as internal Patch audit metadata;
  static workbook/sharedStrings/styles dependencies still use reason text
  without relationship metadata.
  `EditPlan` now upserts repeated `RelationshipTargetAudit` records by
  owner/id/type/raw target/normalized target, upserts repeated
  `WorksheetRelationshipReferenceAudit` records by worksheet/kind/element/id/
  expected type/actual type, and de-duplicates identical audit notes, so
  repeated linked worksheet rewrites do not accumulate duplicate Patch audit
  metadata. No-state-pollution regressions now also snapshot
  `relationship_target_audits()` and `worksheet_relationship_reference_audits()`
  for linked policy failure, calcChain rebuild rejection, missing sheet-name
  worksheet/sheetData lookup, invalid worksheet XML / sheetData source,
  malformed/missing workbook metadata, malformed workbook calc metadata,
  invalid replacement, metadata-entry replacement, and invalid removal failure
  paths; malformed workbook metadata/calc metadata, invalid/metadata replacement,
  and invalid removal also check aggregate `planned_output()` and legacy
  output-entry preview source-copy
  snapshots; invalid/metadata replacement and invalid removal also snapshot
  worksheet/workbook payload audits, removal audits, and calc policy. The
  inbound-linked removal policy failure now carries the same planned-output and
  payload/removal/calc-policy snapshots, including preservation of a queued
  ordinary workbook replacement. This is audit traceability
  hygiene, not relationship validation, repair, or pruning.
  End-to-end editing with sharedStrings/styles/tables/drawings/defined-name
  dependency sync, drawing/image/chart/table editing, and broad preservation remains 计划.

Next tasks:
- Complete `P4.0` from `TASK_BREAKDOWN.md` before widening public Patch or
  In-memory APIs.
- Build on the internal `EditPlan`, `DependencyAnalyzer`, `ReferencePolicy`,
  and `PartRewritePlanner` planning foundation, including explicit registered-part
  removal planning, without exposing it as complete
  existing-file editing.
- Extend the internal `PackageEditor` and package writer/copy pipeline beyond
  the current calcChain/content-type/workbook-rels and sheetData-local-rewrite
  slices toward broader dependency sync and preservation fixtures.
- Use and harden the existing internal `PartIndex` / `RelationshipGraph`
  groundwork when adding reader/writer, preservation tests, and object
  features.
- Add edit manifest write modes:
  - copy original
  - generate small XML
  - stream rewrite
  - local DOM rewrite
- Keep explicit part removals as removed-part audit metadata, not as an
  automatic relationship-pruning promise.
- Preserve unknown and unmodified parts byte-for-byte when possible.
- Prove one narrow Patch MVP after `P4.0` before exposing broad edit APIs.

Validation:
- Use template workbooks containing unknown parts, drawings, charts, or macros.
- Modify a targeted worksheet part and compare package entries before and after.
- Verify unmodified parts are preserved.

### 2. In-memory Small-File Editing

Status: 计划.

Next tasks:
- Define a small-workbook random editing API for sheet inspection,
  `get_cell()`, `set_cell()`, `erase_cell()`, sheet rename/add/delete, and
  save-as.
- Document memory growth and size guardrails. This path may use a cell map or
  local DOM, but it is not the large worksheet low-memory path.
- Share serialization and package semantics with Streaming/Patch where
  practical: styles, sharedStrings, relationships, document properties, and
  calc metadata.

Validation:
- Small workbook edit/save tests cover cell edits, sheet edits, invalid ranges,
  sharedStrings/styles side effects, and preservation when opening an existing
  package.
- Public API comments clearly state In-memory mode and when to choose
  Streaming or Patch instead.

### 3. Sheet Dependency and Phase 3/5 Linkage

Status: 计划, with several new-workbook slices 基础.

Next tasks:
- Define sheet-edit dependency analysis for worksheet `.rels`, sharedStrings,
  styles, tables, hyperlinks, validations, merged cells, autoFilter, drawings,
  defined names, workbook calc metadata, and `calcChain.xml`.
- Keep current Phase 3/5 streaming-only slices documented as new-workbook-only
  until Patch behavior exists.
- For each richer object feature, compare at least two relevant reference
  ecosystems when useful: one API-experience reference and one OpenXML
  structure/reference-output source.
- Add full font/fill/border/alignment, richer conditional formatting, table
  resize, hyperlink styles, and object edits only as separate feature slices.

Validation:
- Existing-file feature edits require before/after package comparison and Excel
  open checks.
- Streaming-only new-workbook slices keep their focused structure tests and
  local QA helpers.

### 4. Writer and Performance Hardening

Status: 进行中 / 基础.

Next tasks:
- Keep opt-in minizip backend, compression-level policy, Zip64 policy,
  sharedStrings measurements, benchmark groundwork, file-backed/chunked entries,
  and streaming hot-path work moving as supporting lanes.
- Keep `inlineStr` as the low-memory default until sharedStrings has broader
  memory/size evidence.
- Do not treat writer performance as a substitute for existing-file editing.

Validation:
- Preserve existing OpenXML structure tests.
- Benchmarks record scale, backend, compression setting, string strategy, time,
  peak memory, output size, and office-suite open result.
- Re-run Excel visual verification for representative generated workbooks when
  behavior changes.

### 5. Phase 5 Complex Objects

Status: 基础 for several streaming-only new-workbook slices. Keep complete
object support, existing-file editing, and preservation support in plan-only
language.

Safe order:
1. Preserve existing chart/VBA/image/drawing/unknown parts when editing an
   unrelated part.
2. Data validations as streaming-only worksheet metadata for new workbooks.
   They should not force a worksheet DOM or existing-file editing.
3. Use the existing internal `PartIndex` / `RelationshipGraph` groundwork for
   features that need worksheet `.rels` or cross-part id consistency, and add
   per-feature tests before claiming support.
4. External URL and internal workbook-location hyperlinks now have
   streaming-only new-workbook slices. External links keep worksheet XML and
   worksheet `.rels` in sync; internal links are location-only and do not
   consume `rId` values. Broader hyperlink support still needs separate design
   and tests.
5. Tables now have a streaming-only new-workbook slice after table part
   allocation, content type override, worksheet rels, and table XML were kept
   consistent. Broader table support still needs separate design and tests.
6. Two-/three-color conditional color scales, basic data bars, and basic
   3Arrows icon sets now have streaming-only new-workbook worksheet metadata
   slices. Icon set QA now includes a percentile-threshold sample in addition
   to the default percent, numeric-priority, and multi-range samples. Broader
   conditional formatting still needs separate design and tests for
   formula/cellIs rules, advanced/custom icon sets, advanced data bars,
   dxf-backed styles, conflict handling, and existing-file editing.
7. Images after `stb` decode/dimension behavior, media part allocation,
   drawing part generation, drawing rels, worksheet rels, anchors, and content
   types are in place.
8. Chart and VBA native generation/editing only after passthrough preservation
   is proven.

Validation:
- For every new object type, compare against an Excel or `openpyxl` /
  `XlsxWriter` reference workbook by unpacking and comparing XML semantics.
- Use Excel visual verification for representative outputs.
- Do not add Excel, `openpyxl`, or `XlsxWriter` as runtime dependencies.
- Use `stb` for image decode/dimension work only; do not use it as a substitute
  for OpenXML drawing/media package logic.

## Commands

Preferred local preset command from a VS2026 Developer Command Prompt:

```powershell
cmake --list-presets
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

Optional vcpkg toolchain smoke command after setting `VCPKG_ROOT`:

```powershell
cmake --preset windows-nmake-release-vcpkg
cmake --build --preset windows-nmake-release-vcpkg
ctest --preset windows-nmake-release-vcpkg
```

Canonical opt-in minizip backend command after setting `VCPKG_ROOT`:

```powershell
cmake --preset windows-nmake-release-minizip
cmake --build --preset windows-nmake-release-minizip
ctest --preset windows-nmake-release-minizip
```

Skill validation command pattern:

```powershell
$env:PYTHONUTF8 = '1'
py C:\Users\wuxianggujun\.codex\skills\.system\skill-creator\scripts\quick_validate.py .agents\skills\fastxlsx-cmake-build
```

## Release Readiness Checklist

- Git remote configured and push verified.
- `LICENSE` included.
- `.gitignore` excludes build outputs, local tool state, and secret files.
- `README.md`, `AGENTS.md`, `docs/TASK_PLAN.md`, `docs/TASK_BREAKDOWN.md`,
  and project skills reflect current implementation facts.
- VS2026/NMake build passes.
- `ctest --preset windows-nmake-release` passes, with the preset/test
  properties enforcing the 60s timeout.
- GitHub Actions CI runner label and workflow behavior are verified or
  corrected, then CI passes.
- Key generated `.xlsx` files open in Excel.
- No documentation claims production ZIP, shared strings, complete Phase 3,
  existing-file editing, or complete Phase 5 complex object support before the
  corresponding code and tests exist.
