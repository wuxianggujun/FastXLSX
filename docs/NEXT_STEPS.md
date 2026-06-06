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
  - `FastXlsxError`
- Current internal foundations:
  - XML escape and cell/range/sqref helpers.
  - Minimal workbook writer and streaming writer.
  - `StringStrategy::SharedString`, internal shared string table wiring,
    `xl/sharedStrings.xml` package entry generation, and focused structure
    tests are visible in the current files. Treat this as sharedStrings
    进行中, not as a production-ready string strategy.
  - Basic `docProps/core.xml` and `docProps/app.xml` package wiring and static
    XML generation are visible in the current files. Treat this as minimal
    metadata output, not as a complete document-properties API.
  - Internal OPC `PartName`, `RelationshipSet`, `ContentTypesManifest`,
    `PackageManifest`, `PartWriteMode`, package-part edit state metadata,
    minimal workbook manifest builder, and content types / relationships
    serializers, including the docProps small XML builders.
- Local Excel visual verification has been performed for:
  - `build-nmake/tests/fastxlsx-phase1-minimal.xlsx`
  - `build-nmake/tests/fastxlsx-streaming-smoke.xlsx`

## Current Round Focus

Status words in this section are intentionally limited to 计划, 进行中, and
基础. They describe visible project state and next work only; they do not claim
feature completion.

1. sharedStrings - 进行中.
   - Current files show the API option, internal table, package wiring, and
     XML structure tests for `xl/sharedStrings.xml`.
   - Keep `inlineStr` as the low-memory default.
   - Before treating sharedStrings as a production feature, record the default
     CTest result, Excel visual open result, reference XML comparison against
     Excel / `openpyxl` / `XlsxWriter` where useful, and size/memory data.

2. vcpkg / CMakePresets / CI - 基础.
   - Current root files include a conservative `vcpkg.json`,
     `CMakePresets.json`, and Windows CI workflow.
   - Local `vcpkg search` and feature dry-run verified planned port/feature
     resolution, but exported CMake target names still need verification in a
     clean dependency task.
   - The default preset and CI path intentionally avoid external vcpkg
     dependencies.
   - CI should run structural tests and CTest with `--timeout 60`; Excel visual
     verification remains a local validation step, not a CI hard dependency.

3. OPC edit plan - 基础.
   - Current internal OPC metadata has part names, relationships, content types,
     package parts, and write-mode planning states.
   - Existing-file editing still needs package reader/writer, part index,
     relationship graph, production ZIP backend, and preservation tests before
     any complete edit support is claimed.

## Repository State

- Local Git repository initialized on branch `main`.
- Public GitHub repository created and pushed:
  `https://github.com/wuxianggujun/FastXLSX`
- `origin/main` is configured as the upstream branch.

## Immediate Repository Tasks

1. Keep generated artifacts out of source control.
   - Ignored build directories include `build/`, `build-*`, `cmake-build-*`,
     `out/`, `dist/`, and `%OPC_BUILD%/`.
   - Local secret files such as `.env`, private keys, and local tool state are
     ignored.

2. Use the conservative engineering entry points.
   - Prefer `cmake --preset windows-nmake-release` for local smoke builds.
   - Use `windows-nmake-release-vcpkg` only after `VCPKG_ROOT` is configured
     and dependency/toolchain behavior is being verified.
   - The current CI workflow runs the no-vcpkg NMake preset first.

3. Before future publishing, inspect staged files.
   - Confirm `.agents/skills/` is included.
   - Confirm generated build outputs, Excel output files, temporary logs, and
     local private state are not included.

## Next Engineering Priorities

### 1. Production ZIP Backend

Status: 计划.

Next tasks:
- Keep the existing `vcpkg.json` conservative until CMake target names are
  verified.
- Verify config package names, imported target names, features, and triplet
  behavior for `minizip-ng`, `zlib-ng`, `expat`, and `pugixml`.
- Add `find_package` and link dependencies only after package/target
  verification is complete.
- Introduce a package writer abstraction that can replace the current internal
  stored ZIP bootstrap.
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
- Build `PartIndex` and `RelationshipGraph`.
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
2. Internal PartIndex / RelationshipGraph groundwork before features that need
   worksheet `.rels` or cross-part id consistency.
3. Hyperlinks after relationship graph support can keep worksheet XML and
   worksheet `.rels` in sync; external URL hyperlinks should come before
   broader hyperlink support.
4. Tables after table part allocation, content type override, worksheet rels,
   and table XML are in place.
5. Images after media part allocation, drawing part generation, drawing rels,
   worksheet rels, and content types are in place.
6. Chart and VBA passthrough only after existing-package read/copy is proven.

Validation:
- For every new object type, compare against an Excel or `openpyxl` /
  `XlsxWriter` reference workbook by unpacking and comparing XML semantics.
- Use Excel visual verification for representative outputs.
- Do not add Excel, `openpyxl`, or `XlsxWriter` as runtime dependencies.

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
- `ctest --timeout 60` passes.
- GitHub Actions CI runner label and workflow behavior are verified or
  corrected, then CI passes.
- Key generated `.xlsx` files open in Excel.
- No documentation claims production ZIP, shared strings, complete Phase 3,
  existing-file editing, or complete Phase 5 complex object support before the
  corresponding code and tests exist.
