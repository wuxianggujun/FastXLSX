# FastXLSX Next Steps

## Purpose

This document summarizes what should be pushed next after the current Phase 1
through Phase 5 foundation work. It is intentionally scoped to project facts
that exist in code, CMake, tests, docs, or local verification.

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
- Current public API:
  - `Workbook`
  - `Worksheet`
  - `Cell`
  - `WorkbookWriter`
  - `WorksheetWriter`
  - `CellView`
  - `DataValidationRule`
  - `DataValidationType`
  - `DataValidationOperator`
  - `FastXlsxError`
- Current internal foundations:
  - XML escape and cell/range/sqref helpers.
  - Minimal workbook writer and streaming writer.
  - `StringStrategy::SharedString`, internal shared string table wiring,
    `xl/sharedStrings.xml` package entry generation, and focused structure
    tests are visible in the current files. Treat this as sharedStrings
    进行中, not as a production-ready string strategy.
  - Basic configurable `docProps/core.xml` and `docProps/app.xml` package wiring
    is visible in the current files through `DocumentProperties`,
    `Workbook::set_document_properties()`, and
    `WorkbookWriterOptions::document_properties`. Treat this as core/app
    metadata for new workbooks, not as `docProps/custom.xml`, existing-file
    editing, or a complete document-properties API.
  - Internal `src/package_writer.*` boundary exists for new-workbook package
    output. Default builds delegate to the stored/no-compression
    `src/zip_store_writer.*` bootstrap backend; opt-in minizip builds use
    `minizip-ng[core,zlib]` and DEFLATE.
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
- Local Excel visual verification has been performed for:
  - `build/windows-nmake-release/tests/fastxlsx-phase1-minimal.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-smoke.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-shared-strings.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-data-validations.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-external-hyperlinks.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-internal-hyperlinks.xlsx`
  - `build/windows-nmake-release/tests/fastxlsx-streaming-hyperlink-display-tooltips.xlsx`
  Manual `build-nmake` output may exist locally, but treat it as potentially
  stale unless it was regenerated after the current source change.

## Current Round Focus

Status words in this section are intentionally limited to 计划, 进行中, and
基础. They describe visible project state and next work only; they do not claim
feature completion.

1. sharedStrings - 进行中.
   - Current files show the API option, internal table, package wiring, and
     XML structure tests for `xl/sharedStrings.xml`.
   - Keep `inlineStr` as the low-memory default.
   - The current small benchmark snapshot shows repeated/shared smaller and
     faster than repeated/inline, and unique/shared higher-memory and larger
     than unique/inline. Treat this as a local `500000`-cell trend only.
   - Current local QA also has `tools/verify_shared_strings_excel.ps1` for
     Excel COM read-only validation and `tools/verify_shared_strings_reference.py`
     for `openpyxl` reference semantics. System `py` currently also verifies an
     XlsxWriter reference; Python environments without the module should record
     that branch as skipped.
   - Before treating sharedStrings as a production feature, expand scale,
     backend coverage, reference compatibility, and size/memory data.

2. vcpkg / CMakePresets / CI - 基础.
   - Current root files include a conservative `vcpkg.json`,
     `CMakePresets.json`, and Windows CI workflow.
   - `minizip-ng` package discovery is no longer just exploratory:
     `FASTXLSX_ENABLE_MINIZIP_NG=ON` uses
     `find_package(minizip-ng CONFIG REQUIRED)` and `MINIZIP::minizip-ng`
     through the `windows-nmake-release-minizip` preset.
   - The default preset and CI path intentionally avoid external vcpkg
     dependencies.
   - CI should run structural tests through the CTest preset/test properties
     that carry the 60s timeout; Excel visual verification remains a local
     validation step, not a CI hard dependency.

3. OPC edit plan - 基础.
   - Current internal OPC metadata has part names, relationships, content types,
     package parts, write-mode planning states, internal `PartIndex`,
     internal `RelationshipGraph`, and a content type registry helper.
   - Existing-file editing still needs package reader/writer, production ZIP
     backend, and preservation tests before any complete edit support is
     claimed.

## Repository State

- Local Git repository initialized on branch `main`.
- Public GitHub repository created and pushed:
  `https://github.com/wuxianggujun/FastXLSX`
- `origin/main` is configured as the upstream branch.

## Recommended Push Order

Use this order for the next implementation pushes. Each item should be a small
commit or short series with its own tests and docs update.

1. Build/test documentation hygiene.
   - Align `TASK_PLAN.md`, `NEXT_STEPS.md`, `TESTING_WORKFLOW.md`,
     `DEVELOPMENT_ENVIRONMENT.md`, and build-related skills.
   - Prefer preset paths over stale manual build directories.
   - Record whether CI uses preset/test timeout rather than a command-line
     `--timeout` flag.
   - Verify skills after edits.

2. CI runner and workflow maintenance.
   - Recheck `windows-2025-vs2026` after the 2026-06-08 GitHub image migration
     window starts.
   - Keep `actions/checkout` on a Node.js 24-compatible major. Current workflow
     uses `actions/checkout@v5`.
   - Keep CI on the no-vcpkg NMake preset until dependency work starts.

3. Production ZIP backend hardening.
   - Keep the opt-in minizip backend tested while deciding whether it becomes
     the default.
   - Add compression-level configuration and Zip64 policy only with focused
     tests.
   - Keep existing OpenXML structure tests independent of compression method.

4. Shared strings hardening.
   - Keep `inlineStr` as default.
   - Add memory/size measurements for repeated and unique strings.
   - Compare package XML and Excel open behavior against inline strings.

5. Streaming writer hot-path and benchmark groundwork.
   - Use the opt-in `fastxlsx_bench_streaming_writer` manual benchmark target.
   - Measure row-order write path before adding broad convenience APIs.
   - Keep benchmark work out of default CTest.

6. Phase 3 style and metadata design.
   - Design style registry before broad `styles.xml` output.
   - Decide formula cached-value and calc behavior boundaries.
   - Keep configurable document properties limited to the current core/app
     docProps API until custom properties or existing-file editing are separate
     tasks.

After the P4 opt-in minizip baseline, the default implementation lane is P5
sharedStrings hardening, then P6 benchmark groundwork, then P7 streaming writer
hot-path work. Return to P4 only when the task explicitly chooses the ZIP/backend
lane: compression-level configuration, Zip64/large-entry policy, minizip CI/cache
and release packaging, or the decision to make minizip the default backend.

7. Internal OPC graph groundwork.
   - Internal `PartIndex` / `RelationshipGraph` groundwork now exists.
   - Relationship id allocation and content type registry helpers now exist.
   - Do not expose existing-file edit APIs yet.

8. Existing package preservation.
   - Add reader/writer after production ZIP and OPC graph foundations.
   - Prove unknown and unmodified parts are preserved.
   - Use template workbooks with images, charts, macros, or unknown parts.

9. Streaming-only data validations - 基础 + prompt/error metadata.
   - `WorksheetWriter::add_data_validation()` now writes worksheet-local
     `<dataValidations>` for new workbooks only.
   - The current slice stores lightweight metadata, copies formula and
     prompt/error strings into writer state, writes prompt/error fields as
     worksheet `<dataValidation>` attributes, and does not add package
     relationships, content types, or styles.
   - Keep richer validation semantics, overlap checks, formula parsing, and
     existing-file editing out of scope until separately designed.

10. Streaming-only external and internal hyperlinks - 基础.
    - `WorksheetWriter::add_external_hyperlink()` now writes worksheet
      `<hyperlinks>` plus `xl/worksheets/_rels/sheetN.xml.rels` for new
      workbooks only.
    - `WorksheetWriter::add_internal_hyperlink()` now writes worksheet
      `<hyperlink location="...">` for new workbooks only, without worksheet
      `.rels`, workbook relationships, content type overrides, or `rId`
      consumption.
    - `HyperlinkOptions` now writes optional `display` / `tooltip` attributes
      on external and internal worksheet `<hyperlink>` elements.
    - Keep hyperlink styles, target existence validation, full Excel UI
      behavior, and existing-file editing out of scope until separately
      designed.

11. Streaming-only tables - 基础.
    - `WorksheetWriter::add_table()` and `TableOptions` now write
      `xl/tables/tableN.xml`, worksheet `<tableParts>`, worksheet `.rels`, and
      table content type overrides for new workbooks only.
    - Keep automatic header inference, totals rows, calculated columns,
      sort/filter criteria, custom styles, `styles.xml`, table resize, full
      Excel table UI behavior, and existing-file editing out of scope.

12. Images and passthrough objects.
    - Images need `stb` for image decoding/dimensions, plus media parts,
      drawings, drawing rels, worksheet rels, anchors, and content types.
    - Chart and VBA work starts as preservation, not native generation.

## Push-by-Push Execution Queue

Use this queue when deciding what to implement next. Pick the first item whose
start condition is true. Do not treat later items as supported until their
acceptance checks have passed and the docs have been updated.

### P0 - Task Docs and Agent Context

Start when task order, validation commands, or implementation status becomes
ambiguous.

Do:
- Keep `TASK_PLAN.md`, `NEXT_STEPS.md`, `AGENTS.md`, `README.md`, and project
  skills aligned with current code.
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
- Keep default CI on the no-vcpkg `windows-nmake-release` preset.

Accept when:
- VS2026/NMake configure, build, and `ctest --preset windows-nmake-release`
  pass locally.
- GitHub Actions runs the same preset path and passes.
- Ordinary tests remain protected by the 60s CTest preset/test properties.
- No Node.js 20 deprecation annotation appears for checkout.

Do not claim:
- vcpkg dependency readiness or Excel visual validation from CI alone.

### P2 - Production ZIP Dependency Discovery

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

### P3 - Package Writer Boundary

Status: baseline complete. New workbook output now goes through the internal
`src/package_writer.*` boundary.

Do:
- Keep `src/zip_store_writer.*` as the dependency-free bootstrap implementation
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

### P4 - Production ZIP Backend

Status: minimal opt-in backend landed. Continue with hardening before making it
the default.

Do:
- Keep the verified ZIP/DEFLATE backend wired through
  `FASTXLSX_ENABLE_MINIZIP_NG=ON` and `MINIZIP::minizip-ng`.
- Add compression-level configuration as an explicit performance choice.
- Define Zip64 and large-entry behavior before large-file promises.

Accept when:
- Tests pass for package entries without assuming stored/no-compression ZIP.
- Generated workbooks open in Excel without repair.
- Docs and API comments describe backend and memory behavior for touched public
  API. Internal-only backend work must not expose public compression controls
  just to satisfy this checklist.

Do not claim:
- Large-file performance until benchmarks record scale, time, memory, output
  size, compression setting, and office-suite open result.

### P5 - Shared Strings Hardening

Start after current sharedStrings structure support exists; production tuning is
best after P4, while small structure fixes can happen earlier.

Do:
- Keep `inlineStr` as the default low-memory path.
- Add tests for `count`, `uniqueCount`, escaping, `xml:space`, duplicates, and
  worksheet `t="s"` references.
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
- Current `500000`-cell manual snapshot uses benchmark schema v3 and
  `temporary_worksheet_part_footprint="worksheet-body-file-bytes"`:
  repeated/inline `493 ms`, `4.97266 MB`, `27927834` worksheet body bytes,
  `27931711` output bytes; repeated/shared `392 ms`, `4.98828 MB`,
  `16927834` worksheet body bytes, `16932289` output bytes; unique/inline
  `658 ms`, `4.97266 MB`, `30866774` worksheet body bytes, `30870651`
  output bytes; unique/shared `1045 ms`, `70.1055 MB`, `19316724`
  worksheet body bytes, `33260102` output bytes.

Do not claim:
- sharedStrings as the best default for large data.

### P6 - Benchmark Groundwork

Start before making broad performance claims or adding convenience APIs that
could affect the writer hot path.

Do:
- Use the opt-in manual benchmark target `fastxlsx_bench_streaming_writer`.
- Keep benchmark dependencies behind planned/dev or opt-in configuration.
- Record data scale, string strategy, compression setting, package entry source
  mode, string pattern, temporary worksheet part footprint availability, time,
  peak memory, output size, and Excel/WPS/LibreOffice open result.
- Treat `temporary_worksheet_part_footprint="worksheet-body-file-bytes"` as a
  benchmark-only worksheet body row XML byte count. It does not include
  sharedStrings XML, worksheet header/footer, package assembly buffers,
  ZIP/backend memory, media files, small XML parts, or OS file-system overhead.
- Keep the first slice independent of Google Benchmark; `planned-dev`
  dependencies remain planned until a separate task explicitly wires them.

Accept when:
- Default CTest remains lightweight and under the 60s boundary.
- Benchmark results are reproducible enough to compare future regressions.

Do not claim:
- Benchmark coverage from normal unit tests.
- Google Benchmark integration or 10,000,000-cell results from the manual
  benchmark entry alone.
- Full low-memory behavior from worksheet-body-only footprint results.

### P7 - Streaming Writer Hot Path

Start after P3/P4 boundaries are clear enough that package output will not hide
worksheet-writer memory problems.

Do:
- Keep row-order writes and bounded memory.
- Add numeric/date encoding edge cases and Excel row/column limit tests.
  Current coverage rejects `NaN` / `+Inf` / `-Inf` for numeric cells, rejects
  in-memory and streaming row heights that are zero, negative, or non-finite,
  rejects in-memory rows beyond Excel's 16384-column limit during save(), and
  rejects streaming column widths that are non-positive or non-finite.
  Current `append_row()` row limit coverage uses `FASTXLSX_ENABLE_TEST_HOOKS` to
  inject the internal row counter at `1048576` and verify one rejected append
  without a million-row default CTest loop; broader date and formatting edge
  cases remain follow-up work.
- Track worksheet dimensions incrementally. Current empty-row coverage locks
  streaming `<dimension ref="A1"/>` for no-row and all-empty-row sheets,
  preserves empty `<row r="N"></row>` elements, and keeps a trailing appended
  empty row in the generated dimension. Current in-memory tests also lock empty
  worksheet, single empty row, `XFD1` max-column dimension, and 16385-column
  rejection. Do not introduce a full cell matrix just to mimic Excel `UsedRange`.
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

### P8 - Phase 3 Metadata Tests

Status: basic focused structure and local Excel validation exists for the
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
- Document formula boundaries: write-only formula text unless cached values,
  calculation mode, and calc chain are implemented. Current formula cells only
  add `<calcPr calcId="124519" fullCalcOnLoad="1"/>` to request recalculation on
  load; they do not provide cached values or `calcChain.xml`.

Accept when:
- XML structure tests and local Excel checks are recorded for the touched
  metadata surface.
- Python XLSX reference checks such as `openpyxl` are recorded when workbook
  calculation metadata changes.
- Docs still mark full Phase 3 as planned.

Do not claim:
- Formula calculation, cached formula correctness, or complete style support.

### P9 - Style Registry Design and First Styles

Start after P8 and after the streaming metadata order is clear.

Do:
- Design style ids and registry ownership before broad `styles.xml` output.
- Implement a narrow first style slice only through that registry.
- Document whether each style API is Streaming, Patch, or In-memory.

Accept when:
- Tests cover `xl/styles.xml`, style ids, and worksheet style references.
- Excel visual verification covers representative style output.

Do not claim:
- Full Excel formatting parity from a narrow first slice.

### P10 - Configurable Document Properties API

Status: 基础.

Current foundation:
- Public `DocumentProperties` exists for small new-workbook metadata.
- `Workbook::set_document_properties()` and
  `WorkbookWriterOptions::document_properties` feed `docProps/core.xml` and
  `docProps/app.xml`.
- The current scope does not generate `docProps/custom.xml` and does not edit
  existing XLSX files.

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

Do not claim:
- `docProps/custom.xml`, existing-file editing, arbitrary timestamps, or full
  document-property coverage unless every exposed property is tested.

### P11 - Internal OPC Graph

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

### P12 - Existing Package Reader/Writer

Start after P4 and P11.

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

### P13 - Preservation Fixture Set

Start after P12 has a working reader/writer path.

Do:
- Add template workbooks containing images, charts, macros, and unknown parts.
- Edit unrelated workbook or worksheet metadata.
- Compare before/after packages.

Accept when:
- Untouched drawings, media, charts, macros, and unknown extensions remain in
  the output package.
- Excel opens edited workbooks without repair.

Do not claim:
- Native image, chart, or VBA generation.

### P14 - Streaming-Only Data Validations

Status: 基础 + prompt/error metadata + multi-area `sqref`. The streaming-only
new-workbook worksheet slice is implemented.

Do:
- Keep new-workbook `WorksheetWriter` metadata as the only supported surface.
- Keep worksheet `<dataValidations>` independent of package relationships and
  content type overrides.
- Keep prompt/error metadata worksheet-local: `showInputMessage`,
  `showErrorMessage`, `errorStyle`, `promptTitle`, `prompt`, `errorTitle`, and
  `error` are attributes on `<dataValidation>`.
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
  string omission, false flag omission, and no `.rels` / `styles.xml` /
  content type side effects.
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
- Keep totals rows, calculated columns, sort/filter criteria, custom styles,
  `styles.xml`, table resize, overlap checks, existing-file editing, and full
  Excel table UI behavior out of this first slice.

Accept when:
- Structure tests compare table XML, worksheet relationships, worksheet
  `<tableParts>`, content types, XML escaping, table column attribute escaping,
  owner-local `rId`, coexistence with external hyperlinks under the same
  worksheet relationship owner, table style flags without generating
  `xl/styles.xml`, duplicate names, invalid ranges/options, and
  mutation-after-close.
- Excel visual verification is recorded for
  `build/windows-nmake-release/tests/fastxlsx-streaming-tables.xlsx`; Excel COM
  confirmed `InventoryTable` and `TotalsTable` as `ListObjects` with expected
  ranges and headers, and confirmed `Plain` has no table object.
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
starts only after P13 proves preservation behavior.

Stages:
1. P17.0 - `stb` dependency discovery and image metadata helper. Status:
   basic.
   - Use `stb` for image decoding, dimensions, channels, and pixel access.
   - Keep it opt-in through `FASTXLSX_ENABLE_STB=ON` and `planned-image`.
   - Current code exposes PNG/JPEG `read_image_info()` for file and memory
     input, backed by `stbi_info` when enabled and a clear FastXlsxError when
     disabled. Current tests also cover unsupported memory/file headers, empty
     memory buffer, empty file, and missing file.
   - The `read_image_info()` documentation must describe metadata reading only;
     it must not imply media part creation, drawing XML, relationships, content
     types, anchors, or existing-workbook preservation.
   - This stage alone still does not create media parts, drawing XML,
     relationships, content types, anchors, or existing-workbook preservation.
2. P17.1 - API shape and documentation.
   Status: basic for `WorksheetWriter::add_image()`.
   - Document whether each image API is Streaming, Patch, or In-memory.
   - Public comments must state memory behavior for original image bytes,
     decoded pixels, anchor metadata, drawing/media part state, and package
     finalization.
   - Current `ImageOptions` metadata is limited to drawing XML non-visual
     picture properties: non-empty `name` writes `xdr:cNvPr name`, non-empty
     `description` writes `descr`, empty `name` keeps generated `Picture N`,
     and empty `description` is omitted.
   - Any convenience API must explain why it does not force large worksheets
     into DOM, a full cell matrix, or the row/cell XML hot path.
3. P17.2 - New-workbook-only insertion slice.
   Status: basic for streaming new workbooks.
   - `WorksheetWriter::add_image(path, anchor)` accepts PNG/JPEG files when
     `FASTXLSX_ENABLE_STB=ON`, validates metadata with `read_image_info()`, and
     copies original image bytes into temporary file-backed media entries.
   - The first slice uses a simple two-cell anchor from a 1-based inclusive
     `CellRange`; it writes generated media parts, one drawing part per
     worksheet with images, drawing `.rels`, worksheet `.rels`, worksheet
     `<drawing>` references, and drawing/content type entries.
   - The current metadata increment copies image `name` / `description` strings
     into writer state and writes them only to drawing `xdr:cNvPr` attributes;
     it does not modify image bytes, media filenames, anchors, relationships,
     content types, or worksheet cell text.
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
   - Current image metadata coverage checks `xdr:cNvPr name` / `descr`, XML
     attribute escaping, empty description omission, default `Picture N` names,
     and no extra relationship/content type/media side effects.
   - Use local Excel visual verification for generated `.xlsx` samples when
     Excel is available, confirming no repair dialog and expected image
     position/size.
   - Current local Excel COM verification opened
     `build/windows-nmake-release-image/tests/fastxlsx-streaming-images.xlsx`
     and confirmed 3 sheets, one shape on `Images`, one shape on `SecondImage`,
     zero shapes on `Plain`, first image at `C1:F5`, and second image at
     `A1:B2`.
   - Current local QA for
     `build/windows-nmake-release-image/tests/fastxlsx-streaming-image-metadata.xlsx`
     uses `tools/verify_image_metadata.py` for XML/openpyxl/XlsxWriter checks
     and `tools/verify_image_metadata_excel.ps1` for Excel COM shape name and
     `AlternativeText` checks.
   - When XML structure or Excel repair behavior is unclear, generate an
     equivalent reference workbook with Excel, `openpyxl`, or `XlsxWriter`, then
     unzip both packages and compare OpenXML semantics.
5. P17.4 - Existing-workbook image read/edit/preservation.
   - Start only after P13 fixtures prove unmodified media/drawing/chart/VBA and
     unknown parts remain present and relationships still resolve after
     unrelated edits.

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
  image bytes or decoded pixels into the worksheet row/cell hot path.
- Validate anchors without retaining a full worksheet DOM.

Accept when:
- vcpkg `stb` feature resolution, include path, license, and local CMake
  behavior are verified for the opt-in image metadata helper and image
  insertion structure tests. CI behavior for `planned-image` remains a separate
  hardening task unless a workflow starts running the image preset.
- Public API docs for any image surface describe mode, ordering, memory cost,
  decoded-pixel lifetime, package side effects, and unsupported operations.
- Package structure tests cover media, drawing XML, rels, and content types.
- Image metadata tests cover `xdr:cNvPr name` / `descr` semantics without
  changing media bytes, relationships, anchors, or content types.
- Excel visual verification confirms images display without repair and with the
  expected position and size.
- Reference XML comparison is recorded when structure compatibility is
  uncertain or Excel repairs the generated file.

Do not claim:
- Image editing or broad drawing support beyond the implemented slice.
- Picture support from `stb` dependency availability alone.
- OpenXML image support beyond the narrow `WorksheetWriter::add_image()`
  streaming new-workbook PNG/JPEG slice.
- Complete image metadata, EXIF/PNG/JPEG metadata, accessibility UI parity, or
  existing drawing mutation from `ImageOptions` name/description alone.
- Existing workbook image passthrough or preservation before P13 fixtures prove
  unmodified media/drawing/chart/VBA parts survive edits.

### P18 - Chart and VBA Passthrough

Start only after P12/P13 preservation is proven.

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
     it is not the canonical P4/minizip backend validation path.
   - The current CI workflow runs the no-vcpkg NMake preset first.

3. Before future publishing, inspect staged files.
   - Confirm `.agents/skills/` is included.
   - Confirm generated build outputs, Excel output files, temporary logs, and
     local private state are not included.

## Next Engineering Priorities

### 1. Production ZIP Backend

Status: 基础.

Next tasks:
- Keep verified config package names, imported target names, features, and
  triplet behavior documented for `minizip-ng`, `zlib-ng`, `expat`, and
  `pugixml`.
- Keep `find_package(minizip-ng CONFIG REQUIRED)` behind
  `FASTXLSX_ENABLE_MINIZIP_NG` until CI/cache/release packaging are verified.
- Decide whether the minizip backend should become default after that evidence.
- Add compression level configuration without changing the worksheet XML hot
  path into a DOM path.
- Add Zip64 and entry streaming requirements before large-file benchmarks.

Validation:
- Preserve existing OpenXML structure tests.
- Add tests that do not assume stored/no-compression ZIP entries.
- Re-run Excel visual verification for generated workbooks.

### 2. Shared Strings Strategy

Status: 进行中.

Next tasks:
- Keep the current `StringStrategy::SharedString` path moving through focused
  validation instead of treating the visible foundation as complete support.
- Keep `inlineStr` as the low-memory default.
- Ensure shared string table growth is documented and tested.

Validation:
- Compare package structure with and without `xl/sharedStrings.xml`.
- Check Excel open compatibility.
- Add size and memory benchmarks before claiming performance benefits.

### 3. Styles and Phase 3 Coverage

Status: 基础.

Current foundation:
- Write skeletons exist for formula cells, row height, column width, frozen
  panes, auto filters, and merged cells.
- Full Phase 3 remains 计划.

Next tasks:
- Add a style registry rather than ad hoc style XML fragments.
- Add number formats and basic font/fill/border/alignment support only after
  the registry shape is clear.
- Add calc mode and cached formula behavior decisions before claiming formula
  compatibility beyond write-only formulas.

Validation:
- Test `xl/styles.xml`, style IDs, and worksheet style references.
- Use Excel visual verification for style samples.
- Use reference `.xlsx` files and XML comparison when Excel repairs output.

### 4. OPC Editing Pipeline

Status: 基础.

Current foundation:
- Internal manifest and serializers exist for new-workbook metadata.
- Package part write-mode planning metadata is visible for copy-original,
  generate-small-XML, stream-rewrite, and local-DOM-rewrite decisions.
- Existing-file editing is still 计划; package read/copy/rewrite behavior
  remains 计划 as an end-to-end editor.

Next tasks:
- Add package reader and package writer on the production ZIP backend.
- Use and harden the existing internal `PartIndex` / `RelationshipGraph`
  groundwork when adding reader/writer, preservation tests, and object
  features.
- Add edit manifest write modes:
  - copy original
  - generate small XML
  - stream rewrite
  - local DOM rewrite
- Preserve unknown and unmodified parts byte-for-byte when possible.

Validation:
- Use template workbooks containing unknown parts, drawings, charts, or macros.
- Modify a targeted worksheet part and compare package entries before and after.
- Verify unmodified parts are preserved.

### 5. Phase 5 Complex Objects

Status: 计划. Keep full object support in plan-only language.

Safe order:
1. Data validations as streaming-only worksheet metadata for new workbooks.
   They should not force a worksheet DOM or existing-file editing.
2. Use the existing internal `PartIndex` / `RelationshipGraph` groundwork for
   features that need worksheet `.rels` or cross-part id consistency, and add
   per-feature tests before claiming support.
3. External URL and internal workbook-location hyperlinks now have
   streaming-only new-workbook slices. External links keep worksheet XML and
   worksheet `.rels` in sync; internal links are location-only and do not
   consume `rId` values. Broader hyperlink support still needs separate design
   and tests.
4. Tables now have a streaming-only new-workbook slice after table part
   allocation, content type override, worksheet rels, and table XML were kept
   consistent. Broader table support still needs separate design and tests.
5. Images after `stb` decode/dimension behavior, media part allocation,
   drawing part generation, drawing rels, worksheet rels, anchors, and content
   types are in place.
6. Chart and VBA passthrough only after existing-package read/copy is proven.

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
- `README.md`, `AGENTS.md`, `docs/TASK_PLAN.md`, and project skills reflect
  current implementation facts.
- VS2026/NMake build passes.
- `ctest --preset windows-nmake-release` passes, with the preset/test
  properties enforcing the 60s timeout.
- GitHub Actions CI runner label and workflow behavior are verified or
  corrected, then CI passes.
- Key generated `.xlsx` files open in Excel.
- No documentation claims production ZIP, shared strings, complete Phase 3,
  existing-file editing, or complete Phase 5 complex object support before the
  corresponding code and tests exist.
