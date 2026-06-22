# Formula Support

FastXLSX does **not** implement a complete Excel formula engine.

Current formula support is formula-compatible XLSX editing: FastXLSX can write,
read, materialize, audit, and narrowly rewrite selected formula text, then ask
Excel-compatible applications to recalculate by setting workbook calculation
metadata. It does not evaluate formulas in-process.

Recommended positioning:

> FastXLSX supports formula-compatible XLSX editing, not an embedded Excel
> calculation engine.

## Current supported behavior

| Area | Current behavior |
| --- | --- |
| Formula tokenizer | Internal `tokenize_formula()` exposes a lexical foundation for formula editing/audit work. It preserves source spans for string literals, quoted sheet-name tokens, bracketed external/structured-reference tokens, function/identifier text, numbers, comparison operators such as `<=` / `>=` / `<>`, punctuation including array-constant braces/separators, and the narrow A1-style reference tokens currently understood by FastXLSX. Incomplete string/bracket spans are preserved as single tokens for audit safety instead of being repaired. |
| Reference qualifier classifier | Internal `classify_formula_reference_qualifier()` decodes quoted sheet names and classifies scanned references as unqualified, local sheet, external workbook, 3D sheet range, or external-workbook 3D sheet range. This is reused by formula audits and safe sheet-name rewrites. |
| New workbook formula cells | `Cell::formula(...)`, `CellView::formula(...)`, and `CellValue::formula(...)` write worksheet `<f>` formula text. Callers pass formula text without a leading `=`. |
| Recalculation request | Workbooks containing formula cells request recalculation with `fullCalcOnLoad`. Patch/edit paths also clean stale `calcChain.xml` metadata when the current policy requires it. |
| Existing workbook formula read | `WorksheetEditor` can materialize supported source formula cells as `CellValueKind::Formula`. Stale cached `<v>` values are not treated as authoritative results. |
| Shared formula read | Source shared formula definitions and source-order followers are materialized as ordinary formula text when the definition has already been seen. The translator handles a narrow A1-style relative-reference subset. |
| Array/dataTable formula metadata | Formula text can be imported lossily as plain formula text. Known metadata is accepted only to preserve compatibility at the materialized value boundary. |
| Formula reference audits | `WorkbookEditor::formula_reference_audits()`, `source_formula_reference_audits()`, and `defined_name_formula_reference_audits()` expose local sheet references, rename risks, external-workbook qualifiers, and 3D-like sheet-range qualifiers as diagnostics. The definedName audit uses the current planned `xl/workbook.xml` when a small workbook rewrite is queued, otherwise the source workbook metadata. |
| Rename-time formula rewrite | `WorkbookEditorRenameFormulaPolicy::RewriteDefinedNames` rewrites direct workbook definedName formulas. `RewriteDefinedNamesAndMaterializedWorksheetFormulas` also rewrites already-materialized worksheet formula cells. In rename chains, the opt-in path rewrites both the current old planned sheet name and the original source sheet name when they differ, while still using the narrow sheet-qualified reference rewriter. |

## Important boundaries

| Item | Boundary |
| --- | --- |
| Formula evaluation | Not implemented. FastXLSX does not calculate `SUM`, `VLOOKUP`, dynamic arrays, volatile functions, date math, or any Excel function result. |
| Cached formula values | Not generated, not trusted as fresh results, and dirty projection drops stale cached results. |
| Full formula parser | Not implemented. The current parser/auditor is a narrow scanner for reference diagnostics and selected sheet-name rewrites. |
| Tokenizer recovery | The tokenizer preserves malformed/incomplete lexical spans for diagnostics. It does not repair formulas or guarantee that Excel accepts the formula. |
| Shared formula preservation | Dirty materialized output is flattened to ordinary `<f>...</f>` formula cells. It does not preserve shared formula `si` / `ref` metadata. Untouched worksheet parts can still be preserved by copy-original paths. |
| Array formulas / data tables / dynamic arrays | Metadata is not preserved after dirty output, no spill range engine exists, and data table recalculation is not implemented. |
| External workbook and 3D references | Classified for audit only. FastXLSX does not validate external workbook targets, evaluate 3D references, or repair linked workbooks. |
| Dependency graph | Not implemented. FastXLSX does not build Excel's calculation dependency graph. |
| `calcChain.xml` rebuild | Not implemented. Rebuild requests are rejected; supported edit paths either preserve or remove stale calcChain metadata according to policy. |
| Full sheet rename formula sync | Not implemented. Default `rename_sheet()` does not rewrite formulas. Explicit policies only cover direct definedNames and already-materialized worksheet formulas. |

## Shared formula materialization details

When a source worksheet stores a shared formula:

- the definition cell formula text is imported directly;
- a source-order follower can be expanded when its shared formula definition was
  already seen;
- relative A1 references are translated by the row/column offset from the
  definition cell to the follower cell;
- absolute references with `$` keep their absolute row/column component;
- out-of-range translated references become `#REF!`;
- quoted string text, quoted sheet-name token text, bracketed external or
  structured-reference token text, function names, named ranges, whole-row /
  whole-column references, and structured-reference contents remain outside the
  full-parser boundary.

This is enough for common Excel/LibreOffice-style shared formula storage shapes,
but it is still not a complete formula grammar.

## Why there is no full formula engine yet

A real Excel formula engine is a separate large subsystem:

- function catalog and compatibility quirks;
- dependency graph and recalculation order;
- volatile functions and workbook calculation settings;
- date/time serial behavior and locale-independent parsing;
- array formulas, dynamic arrays, data tables, external links, and 3D references;
- cached value generation and `calcChain.xml` rebuild.

For an XLSX editing library, the practical first-class requirement is usually to
preserve or rewrite formula text safely and let Excel-compatible applications
recalculate. Implementing a full calculation engine should be treated as a
separate long-term feature, not as a hidden side effect of the editor.

## Verification entry points

Default CTest coverage includes formula scanner/materialization and editor
integration tests:

```powershell
ctest --preset windows-nmake-release -R "fastxlsx\.(formula|workbook_editor\.)" --output-on-failure
```

Focused local QA scenarios:

```powershell
py tools\run_workbook_editor_qa.py `
  --scenario generated_source_formula_audit `
  --work-dir build\qa\workbook-editor-source-formula-audit `
  --excel-verify

py tools\run_workbook_editor_qa.py `
  --scenario generated_formula_rename_rewrite `
  --work-dir build\qa\workbook-editor-formula-rename-rewrite `
  --excel-verify

py tools\run_workbook_editor_qa.py `
  --scenario generated_shared_formula_materialization `
  --work-dir build\qa\workbook-editor-shared-formula-materialization `
  --excel-verify

py tools\run_workbook_editor_qa.py `
  --scenario generated_shared_formula_office_like_materialization `
  --work-dir build\qa\workbook-editor-shared-formula-office-like-materialization `
  --excel-verify

py tools\run_workbook_editor_qa.py `
  --scenario generated_shared_formula_boundary_materialization `
  --work-dir build\qa\workbook-editor-shared-formula-boundaries
```

The boundary scenario intentionally uses synthetic parser-boundary formula text,
so it is validated with ZIP/XML and `openpyxl`; it is not an Excel UI
compatibility smoke.

The generated shared-formula QA reports now include explicit formula evidence:
`formula_output.shared_metadata_removed`, `cached_formula_values_removed`,
`stale_cached_values_removed`, `checked_formula_cells`, `output_formula_cells`,
and `openpyxl.formula_cells`. This keeps the shared-formula materialization
claim auditable from the JSON report without manually unpacking the workbook.

`generated_formula_rename_rewrite` is the focused local QA for the explicit
rename formula policy. It materializes only the `Formula` sheet, renames
`Data` to `RenamedData` with
`RewriteDefinedNamesAndMaterializedWorksheetFormulas`, and verifies by ZIP/XML,
`openpyxl`, and Excel COM that direct local definedName and materialized
worksheet formula references are rewritten while external-workbook references,
3D sheet-range references, string literals, and the non-materialized
`Unmaterialized` worksheet formula remain unchanged. This is still opt-in
formula text rewrite evidence only, not default rename behavior or formula
evaluation.

External fixture smoke can target xlnt/OpenXLSX or other sample workbooks kept
outside this repository:

```powershell
py tools\run_workbook_editor_qa.py `
  --fixture-root C:\path\to\xlnt\tests\data `
  --scenario external_formula_fixture_materialized_smoke `
  --fixture-glob "*formula*.xlsx" `
  --formula-shared-only `
  --work-dir build\qa\workbook-editor-shared-formula-fixtures
```

These fixture runs are local QA evidence only. They are not vendored runtime
dependencies and are not part of default CI.

2026-06-22 local fixture evidence used the minizip-enabled QA tool against a
temporary xlnt sparse checkout, then removed that checkout. Reports:

- `build\qa\xlnt-shared-formula-fixtures-2026-06-22\report.json`:
  `18_formulae.xlsx:Sheet1` had 15 formula elements, 3 shared formula
  elements, 1 shared definition, 2 metadata-only followers; dirty output kept
  15 ordinary formula elements and 0 shared formula metadata elements, with
  Excel COM status `ok`.
- `build\qa\xlnt-source-formula-fixtures-2026-06-22\report.json`:
  three formula-bearing xlnt worksheets scanned read-only with no failures.
- `build\qa\xlnt-defined-name-fixtures-2026-06-22\report.json`:
  `19_defined_names.xlsx` preserved 6 direct definedName records, with Excel
  COM status `ok`.

The same formula-compatible boundary was also rerun against temporary
Python-generated fixtures from `openpyxl 3.1.2` and `XlsxWriter 3.2.0`.
`build\qa\python-writer-formula-fixtures-2026-06-22\report.json` recorded 6
cases total: two source-formula-audit cases, two dirty materialized rename
cases, and two definedName preservation cases. Each formula workbook had 5
formula cells on the target sheet; source audits saw 6 qualified references,
2 rename-risk references, and Excel COM status `ok`; dirty rename outputs kept
5 ordinary formulas and 0 shared-formula metadata; definedName preservation
kept 3 direct definedName records per workbook, with Excel COM status `ok`.
The temporary source fixture root was removed after validation.

A focused definedName audit rerun is recorded in
`build\qa\python-writer-defined-name-audit-2026-06-22\report.json`. It uses
temporary `openpyxl 3.1.2` and `XlsxWriter 3.2.0` workbooks with 3 local
sheet-qualified definedName references each, and confirms the QA layer reports
the public audit counts: 3 definedName references, 3 current-workbook sheet
matches, and zero rename-risk, external-workbook, or 3D sheet-range references
per workbook. This remains diagnostic evidence only; it does not add formula
evaluation, name-manager editing, or default rename-time formula rewriting.
