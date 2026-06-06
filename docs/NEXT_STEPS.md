# FastXLSX Next Steps

## Purpose

This document summarizes what should be pushed next after the current Phase 1
through Phase 5 foundation work. It is intentionally scoped to project facts
that exist in code, CMake, tests, docs, or local verification.

## Current Verified Baseline

- Main local environment: Visual Studio 2026 / MSVC 2026.
- Verified build path: VS2026 Developer Command Prompt + `NMake Makefiles`.
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
  - Internal OPC `PartName`, `RelationshipSet`, `ContentTypesManifest`,
    `PackageManifest`, minimal workbook manifest builder, and content types /
    relationships serializers.
- Local Excel visual verification has been performed for:
  - `build-nmake/tests/fastxlsx-phase1-minimal.xlsx`
  - `build-nmake/tests/fastxlsx-streaming-smoke.xlsx`

## Immediate Repository Tasks

1. Initialize or connect Git remote.
   - The current directory was not a Git repository during this pass.
   - A local commit can be created after `git init`.
   - Remote push still needs a configured `origin` URL and authentication.

2. Keep generated artifacts out of source control.
   - Ignored build directories include `build/`, `build-*`, `cmake-build-*`,
     `out/`, `dist/`, and `%OPC_BUILD%/`.
   - Local secret files such as `.env`, private keys, and local tool state are
     ignored.

3. Before publishing, inspect staged files.
   - Confirm `.agents/skills/` is included.
   - Confirm generated build outputs, Excel output files, temporary logs, and
     local private state are not included.

## Next Engineering Priorities

### 1. Production ZIP Backend

Status: not implemented.

Next tasks:
- Add `vcpkg.json` after verifying real port names, features, and CMake target
  names for `minizip-ng` and `zlib-ng`.
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

Status: `inlineStr` is implemented; `sharedStrings` is not.

Next tasks:
- Add an explicit string strategy API and implementation for shared strings.
- Keep `inlineStr` as the low-memory default.
- Ensure shared string table growth is documented and tested.

Validation:
- Compare package structure with and without `xl/sharedStrings.xml`.
- Check Excel open compatibility.
- Add size and memory benchmarks before claiming performance benefits.

### 3. Styles and Phase 3 Coverage

Status: write skeletons exist for formula cells, row height, column width,
frozen panes, auto filters, and merged cells. Full Phase 3 is not complete.

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

Status: internal manifest and serializers exist for new-workbook metadata.
Existing-file editing is not implemented.

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

Status: dependency planning only. Do not claim full support yet.

Safe order:
1. Hyperlinks and data validations that only need worksheet XML plus simple
   relationship/content type updates.
2. Tables after table part allocation, content type override, worksheet rels,
   and table XML are in place.
3. Images after media part allocation, drawing part generation, drawing rels,
   worksheet rels, and content types are in place.
4. Chart and VBA passthrough only after existing-package read/copy is proven.

Validation:
- For every new object type, compare against an Excel or `openpyxl` /
  `XlsxWriter` reference workbook by unpacking and comparing XML semantics.
- Use Excel visual verification for representative outputs.
- Do not add Excel, `openpyxl`, or `XlsxWriter` as runtime dependencies.

## Commands

Verified local command:

```powershell
cmd /d /c "call ""D:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S . -B build-nmake -G ""NMake Makefiles"" -DCMAKE_BUILD_TYPE=Release && cmake --build build-nmake && ctest --test-dir build-nmake --output-on-failure --timeout 60"
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
- Key generated `.xlsx` files open in Excel.
- No documentation claims production ZIP, shared strings, complete Phase 3,
  existing-file editing, or complete Phase 5 complex object support before the
  corresponding code and tests exist.
