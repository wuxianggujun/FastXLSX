# Existing-workbook Editing Test Matrix

This matrix records what the current FastXLSX editing tests prove today. It is
not a feature promise for complete XLSX editing.

## Default verification entry points

```powershell
cmake --build --preset windows-nmake-release --target fastxlsx_workbook_editor_tests
ctest --preset windows-nmake-release -R "fastxlsx\.workbook_editor\.facade" --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

After local runs, check that no leftover test/build processes are still active:

```powershell
$procs = Get-Process fastxlsx_bench*,fastxlsx_streaming*,fastxlsx_package*,fastxlsx_workbook*,ctest,cmake,nmake,cl -ErrorAction SilentlyContinue
if ($procs) { $procs | Select-Object ProcessName,Id,CPU,WorkingSet64 } else { 'no matching processes' }
```

## Public `WorkbookEditor` facade evidence

| Area | Evidence | Proven today | Not claimed |
| --- | --- | --- | --- |
| No-op `save_as()` | `fastxlsx.workbook_editor.facade` no-op save-as regressions | A reader-backed source package can be copied when no public edits are queued, including after representative failed public edits. | Atomic in-place editing, commit/close semantics, or source package mutation. |
| `replace_sheet_data()` | Public facade replacements and planned-catalog tests | Existing worksheet `<sheetData>` can be replaced by sheet name, including after a planned rename, while surrounding package metadata remains bounded by the current Patch helper. | Random cell editing, sharedStrings/style migration, table/range repair, or large-file low-memory worksheet rewriting. |
| `rename_sheet()` | Public facade rename tests | Direct workbook catalog sheet-name rewrites are covered, including duplicate/invalid/missing-name failures and preservation of unrelated package parts. Explicit formula policies cover direct workbook definedName rewrite and already-materialized WorksheetEditor formula rewrite. Source formula audit also scans non-materialized worksheet formulas, including source-order shared formula followers, without rewriting them. | Non-materialized worksheet formula rewrite/synchronization, table/chart/hyperlink relationship synchronization, or full sheet add/delete/move. |
| Generated formula rename rewrite QA | `py tools\run_workbook_editor_qa.py --scenario generated_formula_rename_rewrite --excel-verify` | The opt-in local QA runner now verifies public `rename_sheet(..., RewriteDefinedNamesAndMaterializedWorksheetFormulas)` end-to-end with ZIP/XML, `openpyxl`, and Excel COM: direct local definedNames and already-materialized worksheet formulas are rewritten, while external-workbook references, 3D sheet ranges, string literals, and non-materialized worksheet formulas remain unchanged. | Default rename formula rewrite, formula evaluation, cached result generation, calcChain rebuild, non-materialized worksheet scan/rewrite, full formula parser, runtime Python dependency, or default CI fixture expansion. |
| Generated escaped sheet-name formula rename QA | `py tools\run_workbook_editor_qa.py --scenario generated_formula_rename_escaped_sheet_name --excel-verify` | The same explicit rewrite policy is verified when the new sheet name is `Renamed & O'Brien`: workbook catalog / definedName XML escaping, quoted formula qualifiers, doubled apostrophes, unchanged external-workbook / 3D / string-literal / non-materialized formulas, calcChain absence, and Excel COM read-only open. | Broader formula grammar support, default formula rewrite, formula evaluation, cached result generation, calcChain rebuild, non-materialized worksheet rewrite, runtime Python dependency, or default CI fixture expansion. |
| Generated definedNames-only rename QA | `py tools\run_workbook_editor_qa.py --scenario generated_formula_rename_defined_names_only --excel-verify` | The middle-policy local QA runner verifies `rename_sheet(..., RewriteDefinedNames)` end-to-end with ZIP/XML, `openpyxl`, and Excel COM: direct local definedNames are rewritten, but already-materialized worksheet formulas, external-workbook references, 3D sheet ranges, string literals, and non-materialized worksheet formulas remain unchanged while formula rename-risk audits stay visible. | Worksheet formula rewrite under definedNames-only policy, formula evaluation, cached result generation, calcChain rebuild, non-materialized worksheet rewrite, full formula parser, runtime Python dependency, or default CI fixture expansion. |
| Generated formula rename default QA | `py tools\run_workbook_editor_qa.py --scenario generated_formula_rename_default_audit --excel-verify` | The paired local QA runner verifies default `rename_sheet()` remains catalog-only with ZIP/XML, `openpyxl`, and Excel COM: the sheet catalog is renamed, but direct local definedNames, already-materialized worksheet formulas, external-workbook references, 3D sheet ranges, string literals, and non-materialized worksheet formulas remain unchanged while rename-risk audits are still reported. | Silent default formula repair, formula evaluation, cached result generation, calcChain rebuild, non-materialized worksheet rewrite, full formula parser, runtime Python dependency, or default CI fixture expansion. |
| `replace_image()` | Public media-part replacement tests | Existing `xl/media/*` bytes can be replaced with same-format PNG/JPEG bytes from file or memory. The queued source lifecycle, CRC guard, repeated same-part later-wins behavior, and pending diagnostics are covered. | Image insertion, drawing XML mutation, anchor update, format conversion, relationship/content-type repair, or decoded pixel retention. |
| `WorksheetEditor` materialized edits | Public small-file materialization tests | A selected worksheet can be materialized, inspected, edited sparsely, and auto-flushed through `WorkbookEditor::save_as()`. Default style normalization, source sharedStrings materialization, and source-failure hygiene are covered in representative cases. | Dense range editing, row/column insertion, workbook-level guardrails, sharedStrings/style writeback migration, or large-file random editing. |
| Combined public smoke | `test_public_workbook_editor_editing_end_to_end_smoke()` | One public flow now combines `rename_sheet()`, materialized `WorksheetEditor::set_cell()`, `replace_sheet_data()` on a different sheet, `replace_image()`, and `save_as()`. It verifies workbook catalog changes, cell XML output, media byte replacement, and preservation of the picture sheet, drawing XML/rels, package rels, workbook rels, content types, and docProps. | Transaction history, semantic object editing, relationship repair/pruning, source reload, undo/rollback, or complete workbook editing. |
| Combined public external QA | `py tools\run_workbook_editor_qa.py --scenario generated_public_e2e` | The opt-in local QA runner validates the same public flow with ZIP/XML workbook catalog mapping, worksheet/drawing relationship checks, replacement image byte comparison, `openpyxl` readback of edited values, and `Pictures` sheet image-count smoke. | Runtime Python dependency, Excel formula calculation, semantic image/drawing editing, relationship repair/pruning, transaction history, undo/rollback, or large-file random editing. |
| Generated shared-formula QA | `py tools\run_workbook_editor_qa.py --scenario generated_shared_formula_materialization --scenario generated_shared_formula_office_like_materialization --scenario generated_shared_formula_boundary_materialization` | The opt-in local QA runner now reports exact `checked_formula_cells`, output formula cells, `openpyxl.formula_cells`, shared-formula metadata removal, cached formula value removal, and whether each scenario is an Excel UI smoke or synthetic ZIP/XML boundary smoke. | Formula evaluation, cached result generation, calcChain rebuild, full formula parser, shared formula metadata preservation, runtime Python dependency, or default CI fixture expansion. |
| Combined failed-save recovery | `test_public_workbook_editor_combined_failed_save_as_preserves_state()` | The same mixed public edit surfaces survive an output-path preflight failure: pending counts, replacement diagnostics, dirty materialized diagnostics, worksheet summaries, planned catalog, borrowed handle dirty state, and `last_edit_error()` remain stable, and a later safe `save_as()` writes the queued rename, dirty cells, refreshed dimension, sheetData replacement, and memory-backed image bytes. | Transaction history, durable commit semantics, rollback/undo API, source package mutation, source reload, relationship repair/pruning, or semantic object editing. |
| Failure hygiene | Public facade failure tests | Representative failures update or preserve `last_edit_error()` according to the public API contract and do not leak partial pending state. Failed `save_as()` keeps queued work reusable. | Transactional rollback model or durable commit semantics. |

## Internal preservation evidence

The broader linked-object and unknown-part preservation evidence still lives in
internal `PackageEditor` / `PackageReader` regressions, not in the public facade
contract. Those tests cover representative worksheet `.rels`, drawing/media,
chart, table, VML, VBA, sharedStrings/styles, custom XML, comments, and unknown
extension preservation paths. Treat them as Patch pipeline evidence only, not as
complete semantic editing support.

## When to extend this matrix

Add a row here when a public editing behavior becomes externally visible or when
a local QA helper becomes the recommended verification path. Keep every row tied
to a concrete CTest, helper, or fixture, and state the non-goals beside the
positive evidence.
