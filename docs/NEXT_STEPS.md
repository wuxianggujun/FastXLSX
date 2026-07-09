# FastXLSX Next Steps

## Purpose

This document summarizes what should be pushed next after the current
new-workbook foundations. The current product direction is an editable
high-performance XLSX library: Streaming for large new workbooks and
large rewrites, Patch for existing-file editing and preservation, and In-memory
for small-file random editing. It is intentionally scoped to project facts that
exist in code, CMake, tests, docs, or local verification.

For executable subtask boundaries, use [TASK_BREAKDOWN.md](TASK_BREAKDOWN.md).
This file gives ordering; the breakdown file gives the per-task input, output,
parallelism, touched files, acceptance checks, and explicit non-goals.

Current execution order is `C0 -> C7`. Treat `P*` labels only as historical
indexes or capability slices. The current lane has now opened the first C4/F2
public `WorksheetEditor` slice under `WorkbookEditor`: small-file
existing-workbook random cell edits are explicit In-memory mode, and dirty
materialized worksheet sessions flush through `WorkbookEditor::save_as()`.
The `workbook_editor_in_memory` example now also demonstrates reopening the
edited output for sparse/row snapshot inspection and writing a clean no-op
`save_as()` output; this is small-file In-memory workflow evidence, not in-place
save or large-file random editing.
The next editor work should harden this first slice before adding style
migration, sharedStrings migration, broader workbook-level guardrails, or
large-file random editing. This slice can now be exercised by the opt-in
workbook-editor QA runner against both generated
cases and external `.xlsx` fixture directories. `tools/run_workbook_editor_qa.py`
now keeps the existing xlnt smoke scenarios and adds
`external_fixture_materialized_smoke`, which discovers `.xlsx` files under a
caller-provided fixture root, runs the same narrow materialized edit, validates
outputs with ZIP/XML and `openpyxl`, and can optionally invoke the Excel COM
sidecar for no-repair open checks. This is local compatibility evidence for the
covered fixtures only; it is not a runtime dependency, not default CTest/CI, and
not a broad guarantee for unsupported Excel object models.
The internal materialized save-as bridge now has focused tests for dirty-session
flush handoff into the Patch plan, stale planned-name rejection without clearing
dirty diagnostics, append-only sharedStrings projection, existing-string reuse
without rewriting the table, and duplicate appended text de-duplication. This is
WorksheetEditor small-file in-memory persistence evidence, not broad
sharedStrings migration or metadata repair.
The inline materialized flush path now also verifies the final saved worksheet
XML for text, number, boolean, escaped formula, escaped error, and explicit blank
sparse records after the dirty session is flushed into the Patch plan.
The appended sharedStrings materialized flush path now also checks final
worksheet indexes plus appended table text that needs XML escaping or
`xml:space="preserve"`, while remaining append-only rather than a broad
sharedStrings migration.
The unsupported sharedStrings append path now has a direct internal flush
regression too: when the source table is readable but not safe to append, dirty
materialized text is saved as inline strings and the source sharedStrings bytes
are preserved instead of repaired.
The internal flush bridge now also covers multiple dirty materialized worksheet
sessions in one handoff: both sheets flush, clear diagnostics, write their own
worksheet XML, avoid creating sharedStrings, and settle into a byte-stable no-op
save while leaving the source package unchanged.
Those internal flush tests now also require repeated `save_as()` outputs to
stay byte-identical after the dirty sessions are cleared, while preserving the
source package across inline, appended sharedStrings, existing-only
sharedStrings, and duplicate-appended sharedStrings handoff paths.
The stale-target flush rejection path now mirrors that save stability on the
failure side: both the first safe output and the repeated no-op output stay
source-copy while the stale dirty session and dirty diagnostics remain intact.
Shared-string projection provider failures now also preserve retryability:
both sheetData-only and full worksheet projection paths can be retried with a
recovered provider while keeping the dirty session diagnostics intact.
The internal failed-save reflush path now also requires the later safe output to
settle into a byte-stable no-op save while leaving the source package unchanged.
The adjacent failed-save baseline now pins the same source-preservation and
byte-stable no-op contract before flush, after explicit flush, and after the
later safe output.
The move-assignment reflush paths now share that stability gate: failed-save
and successful-save reuse both preserve assigned/discarded source packages,
avoid rewriting prior outputs, and settle into byte-stable no-op saves.
The explicit reflush replacement and stale-planned-name flush failure paths now
also pin source-package immutability; the successful replacement side additionally
settles into a byte-stable no-op save.
Planned-name rename, blank/erase, and repeated materialization flush paths now
share the same source-package immutability and byte-stable no-op save gate.
Materialized load guard failures now also pin recovery behavior: max-cell guard,
memory-budget guard, and missing-source loads leave the source package unchanged
and the later valid edit settles into a byte-stable no-op save.
The public source-success sharedStrings shard now also pins dirty text reuse
through `WorksheetEditor::save_as()`: materialized cells can reuse an existing
source sharedStrings item without rewriting the table, duplicate dirty text
reuses one newly appended item, clean no-op saves stay byte-identical, and the
outputs reopen through the public cell/snapshot APIs.
It also pins source-backed shared-string cells across structural row/column
shifts: insert/delete rows and columns move the materialized text cells,
preserve sharedStrings indexes/table bytes on save, keep untouched worksheets
byte-stable, keep follow-up no-op saves byte-identical, and allow the saved
outputs to be reopened for later text edits that continue appending through the
sharedStrings table.
The source-success max-coordinate shard now also fresh-reopens compact
erase/no-op outputs, restores `XFD1048576` for inline text, formula, error,
scalar/blank, empty-inline, shared-string, and rich shared-string sources, and
requires the restored outputs' clean no-op saves to stay byte-identical.
The public-state append-row / set-row / set-column paths now also carry dirty
error-cell evidence: `append_row()`, `set_row()`, and `set_column()` can add or
replace opaque error tokens beside text, number, formula, and explicit blank
cells, save them as `t="e"`, reopen them through public sparse snapshots, and
keep repeated no-op saves byte-stable. This remains scalar error-cell
projection, not semantic error-token validation or formula evaluation.
Source-backed error cells now also have structural row/column shift evidence:
insert/delete rows and columns move surviving `t="e"` cells in the materialized
`WorksheetEditor` sparse store, save them back as error cells, drop deleted
source error records, fresh-reopen through public sparse views, and keep clean
no-op saves byte-stable. This remains value projection for small-file
In-memory editing, not metadata repair or semantic error-token validation.
Source-backed scalar cells now have the same structural shift evidence:
insert/delete rows and columns move blank, boolean, numeric, and inline-string
source cells through the materialized sparse store, drop deleted source scalar
records, save/reopen through public row/column snapshots, and keep clean no-op
saves byte-stable without introducing sharedStrings. This remains small-file
In-memory value projection, not sharedStrings migration or metadata repair.
Formula structural rewrite coverage now also pins mixed absolute markers:
row/column insertions shift the affected coordinates in references like `$A2`,
`B$2`, and `$C$3`, while preserving the caller's `$` marker text. This remains
lexical formula rewrite evidence, not formula evaluation or dependency graph
scope.
The generated QA lane includes `generated_rename_materialized`, which renames
`Data` to `EditedData`, writes materialized A1/B2 cells, preserves the
untouched sheet, and now also has a no-op save variant requiring the clean
follow-up `save_as()` output to be byte-identical.
The generated QA lane also includes `generated_in_memory_insert_formula`, which
drives a tiny existing workbook through `WorksheetEditor::insert_rows()`, writes
a new materialized formula row, verifies the shifted source-backed formula,
saves, and reopens the output through ZIP/XML, `openpyxl`, optional XlsxWriter,
and optional Excel COM checks.
That row-insert generated QA path now also has full-calculation variants:
`generated_in_memory_full_calc_insert_formula` and its no-op save companion
queue `request_full_calculation()`, require workbook `fullCalcOnLoad="1"`, keep
`xl/calcChain.xml` absent, and still validate the shifted formula row plus
untouched sheet readback.
The remaining moving-formula directions now have matching full-calculation
generated QA variants too: delete-column, insert-column, and delete-row all
queue `request_full_calculation()`, verify translated formulas and untouched
sheet readback, and include byte-stable no-op save companions.
Formula helper coverage now also pins escaped string literal preservation and
orthogonal whole-axis row/column structural rewrite behavior used by
`WorksheetEditor` shifts. It also covers delete-row/delete-column whole-axis
ranges that either become `#REF!` when the deleted axis is referenced or shift
later axes without touching the orthogonal whole-axis references, plus
insert-at-boundary structural rewrites that convert row/column references
shifted past Excel limits to `#REF!`. Cell-range structural coverage now also
pins endpoint-by-endpoint partial delete behavior, fully deleted range
endpoints, reversed range endpoint order preservation, and range endpoints
shifted past Excel limits. It now also covers insertions that split cell ranges
and whole-axis ranges, preserving the lower endpoint while moving the affected
upper endpoint and `$` marker text. The helper now also locks escaped quoted
sheet-name token preservation and zero-count no-op behavior across all
structural edit kinds.
The public stationary-formula shift shard now carries those range-endpoint
cases through `WorksheetEditor` row/column shifts as well, including fully
deleted endpoints and Excel-boundary insertions that rewrite to `#REF!` without
moving the formula cell. It also carries split-range row/column insertions
through public `WorksheetEditor` save/reopen/no-op behavior, with whole-axis
references and `$` marker text preserved in the saved formula.
The corresponding saved-reopen formula audit checks now report the rewritten
split cell ranges and split whole-axis references through both source and
materialized public audit APIs, while confirming the original pre-insert
references are absent.
The public source-success formulas shard now also exercises dirty formula saves
that require XML escaping across ordinary, cached-result, and shared-formula
materialized outputs, with clean no-op save and fresh reopen checks.
It now also pins unresolved metadata-only shared formula followers with cached
scalar values: numeric, `t="str"`, boolean, and error cached payloads
materialize as ordinary scalar cells, dirty projection drops shared formula
metadata/indexes, and no-op saves remain byte-stable.
That cached-scalar path now also re-dirties after the clean no-op, saves a later
error edit, keeps shared formula metadata dropped, and settles into another
byte-stable no-op save.
It now also pins direct mutations of source formula records across ordinary,
shared, array, and dataTable source shapes: value overwrite, full replacement,
explicit blank clear, and erase all drop stale formula metadata/cached values
without mutating the source package.
The same direct-mutation path now fresh-reopens the clean no-op output, applies
a later formula edit, and repeats byte-stable no-op validation so the flattened
formula shape remains reusable as a saved-session input.
The source-success formula shard now mirrors that coverage for sparse batch and
range mutations: `set_cell_values()`, A1-range `clear_cell_values()`,
coordinate-list clear, and A1-range `erase_cells()` all keep formula metadata
and stale cached values dropped across save, no-op, fresh-reopen edit, and
repeat no-op.
The same shard now extends that formula metadata boundary to row/column
convenience mutations: `set_row_values()`, `set_column_values()`,
inclusive row/column clears, and inclusive row/column erases keep shared,
array, and dataTable source metadata plus stale cached values from reviving
across save, no-op, fresh-reopen edit, and repeat no-op.
It now also covers whole-store sparse mutations over the same source formula
shapes: no-argument `clear_cell_values()` keeps every represented coordinate as
an explicit blank, and no-argument `erase_cells()` omits every source formula
record, while both paths keep formula metadata and stale cached values dropped
across save/no-op/fresh-reopen edit/repeat no-op.
The same shard now pins full-cell replacement conveniences on source formula
records too: grouped `set_row()`, `set_column()`, `set_cells()`, and
`append_row()` mutations keep shared, array, and dataTable metadata plus stale
cached values from reviving across save, no-op, fresh-reopen edit, and repeat
no-op.
It also pins structural shifts over source formula records in the same focused
shard: `insert_rows()`, `insert_columns()`, `delete_rows()`, and
`delete_columns()` move materialized ordinary/shared/array/dataTable formula
cells, rewrite moved formula text, keep formula metadata dropped, omit stale
cached `<v>` payloads from shifted formula cells, and preserve metadata-only
numeric fallback cells as ordinary values across save, no-op, fresh-reopen edit,
and repeat no-op.
Those structural-shift source formula cases now also include moved formula
cells with A1 ranges, whole-row / whole-column references, string literals, and
structured-reference text, so the same save/reopen/no-op path covers the narrow
formula translator boundary without expanding into formula evaluation or
metadata synchronization.
It also includes `generated_in_memory_delete_column_formula`, which drives
`WorksheetEditor::delete_columns()` over a tiny existing workbook and verifies
left-shifted source cells plus formula reference translation before the same
save/reopen checks.
The source-failure shard now also covers formula materialization guardrails for
unsupported `<f>` attributes, invalid shared formula `si` values, and formulas
inside shared-string source cells. These cases must fail before materializing
`WorksheetEditor`, preserve public editor state, and leave the editor usable for
a recovery sheetData save.
It now also pins source error cells with missing or empty `<v>` values as
materialization failures, requiring the public editor to remain recoverable and
the recovery output to settle into a byte-stable clean no-op save.
Representative source-failure recovery paths now also assert source-package
immutability and byte-stable clean no-op saves after recovery for invalid
shared-string indexes, malformed worksheet XML, empty formulas, inline text
entity failures, and scalar value-wrapper attribute failures.
The lightweight formula target now also pins structural rewrite token skipping
for multi-bracket structured references such as `Table1[[#Headers],[B1]]` and
`Table1[@[B1]]`, so WorksheetEditor row/column shifts do not rewrite embedded
reference-like text inside those structured-reference tokens.
It now also pins tokenizer recovery for unterminated quoted sheet-name tokens
and preserves existing formula error literals such as `#REF!` / `#N/A` while
translating or structurally rewriting real A1-style references around them.
It now also classifies quoted external workbook qualifiers, including quoted
external 3D sheet ranges, and keeps those external qualifiers out of local
sheet-name rewrite.
The same lightweight formula target now also covers sheet-reference rewrite over
multi-bracket structured references, keeping embedded `Old!A1`-like text intact
while still rewriting real local sheet qualifiers in the same formula.
The same generated QA lane now covers the remaining current shift directions
with `generated_in_memory_insert_column_formula` and
`generated_in_memory_delete_row_formula`.
It also covers `generated_in_memory_clear_erase`, which verifies a source-backed
formula cell cleared to explicit blank, a source-backed text cell erased from
the represented cell set, a new materialized text cell, save, and reopen checks.
The generated lane now also covers `generated_in_memory_append_row_formula`,
which appends a materialized text/number/formula row to an existing worksheet
and verifies source rows plus the appended row after save and reopen.
Both clear/erase and append-row generated lanes now also have no-op save
variants that require the clean follow-up `save_as()` output to be
byte-identical.
It also covers `generated_in_memory_overwrite_formula_text`, which overwrites
source-backed text, number, and formula cells with `WorksheetEditor::set_cell()`
and verifies old payload removal plus preserved rows/sheets after save and
reopen.
The overwrite generated lane now also has a direct no-op save variant that
requires the clean follow-up `save_as()` output to be byte-identical.
It now also covers `generated_in_memory_retry_noop_save`, which exercises the
same overwritten single-sheet final shape through a rejected source-overwrite
`save_as()`, safe retry, and byte-identical no-op save.
It also covers `generated_in_memory_retry_path_equivalent_noop_save`, which
uses a path-equivalent source output path for the rejected save before the same
safe retry and byte-identical no-op output checks.
It also covers `generated_in_memory_retry_reopen_modify_noop_save`, which opens
that safe retry output through a fresh editor, applies second-stage edits, and
requires the final no-op save to be byte-identical to the second-stage output.
It also covers
`generated_in_memory_retry_path_equivalent_reopen_modify_noop_save`, which uses
a path-equivalent source output path for the rejected first save before the
same safe-retry, fresh reopen/edit, and byte-identical no-op checks.
It also covers
`generated_in_memory_retry_path_equivalent_reopen_modify_post_noop_third_save`,
which continues that path-equivalent retry/reopen/no-op path with a third edit
and byte-identical final no-op save.
It now also covers
`generated_in_memory_retry_reopen_modify_post_noop_third_save`, which edits
`Data` again after that clean no-op save and requires the final no-op save to
be byte-identical to the third-stage output.
It also covers `generated_in_memory_reopen_modify_save`, which saves a first
in-memory edit, reopens that output through a fresh `WorkbookEditor`, applies a
second in-memory edit, and verifies the final workbook after save and reopen.
The same generated single-sheet lane now also has
`generated_in_memory_reopen_modify_noop_save`, which requires the final no-op
`save_as()` output to be byte-identical to the second-stage save before running
the same ZIP/XML, `openpyxl`, optional XlsxWriter, and optional Excel COM checks.
It now also covers
`generated_in_memory_reopen_modify_post_noop_third_save`, which edits `Data`
again after that clean no-op save, writes a third-stage output, and requires the
final no-op save to be byte-identical to that third output.
Default public-state coverage now pins the same single-sheet
edit/save/reopen/edit/save/no-op boundary: the original source and intermediate
output remain unchanged, the second-stage output is byte-equivalent to the
final no-op output, and the reopened final workbook exposes clean materialized
diagnostics.
That same regression now also mutates the clean post-no-op editor again, writes
a third output, keeps earlier outputs unchanged, and verifies a third no-op
save is byte-equivalent to the third output.
The shifted saved/reacquired session path now mirrors that post-no-op usability
coverage for a row-shifted `WorksheetEditor`: after a byte-stable clean no-op
save, a later `C3` edit re-dirties the shared handles with aligned public
materialized diagnostics, saves as the next handoff, leaves earlier outputs
unchanged, and fresh-reopens with both shifted `A3` and new `C3` values.
The matching-options row/column shift reacquire path now also has repeated
no-op-save evidence: after the original handle saves a row shift and a matching
reacquire saves a later column shift, both no-op outputs are byte-identical,
source and prior outputs stay unchanged, public state remains stable, and a
fresh editor reopens the combined shifted `Data` state.
The saved row-shift reacquire no-op path now repeats that clean no-op save
before the later post-noop edit: the second no-op output is byte-identical to
the first, reopens with shifted `A3`, and the subsequent `C3` save leaves both
no-op packages unchanged.
The delete-column saved/reacquired no-op path now has the same coverage for a
formula-translated shifted session: after the clean no-op save, a later `D2`
edit re-dirties the shared handles, preserves the translated `B1` formula
diagnostics, saves as the next handoff, leaves earlier outputs unchanged, and
fresh-reopens with shifted `A1`, translated `B1`, shifted `C2`, and new `D2`.
It also repeats the clean no-op save before that later `D2` edit: the second
no-op output is byte-identical to the first, reopens with the translated
delete-column state, and remains unchanged after the post-noop save.
The delete-row and insert-column saved/reacquired no-op paths now complete the
same current shift-family coverage: later `D3` / `F3` edits re-dirty the shared
handles, preserve the translated formula cells, save as the next handoff, leave
earlier outputs unchanged, and fresh-reopen with the shifted source-backed
cells plus the post-noop edits.
They also repeat the clean no-op save before those later `D3` / `F3` edits:
the second no-op outputs are byte-identical to the first no-op outputs,
fresh-reopen with the shifted formula state, and remain unchanged after the
post-noop saves.
The delete-column, delete-row, and insert-column reacquire post-noop outputs now
also pin `sparse_cells()`, range/requested sparse snapshots, `row_cells()`, and
`column_cells()` readback for shifted source cells, translated formulas, shifted
dirty cells, and the later post-noop edit. This is snapshot parity and
materialized handoff evidence, not broader worksheet metadata synchronization.
The optional `try_worksheet()` saved-session path now has the same post-noop
coverage: the row-shifted clean no-op output stays byte-stable, a later `C3`
edit re-dirties the optional reacquired session and original handle with aligned
materialized diagnostics, and the next output fresh-reopens with shifted `A3`
plus the new `C3`.
It now also repeats the clean no-op save before that later `C3` edit: the
second no-op output is byte-identical to the first, fresh-reopens with the
shifted `A3` state, and remains unchanged after the post-noop save.
The optional reacquire post-noop output now also pins full sparse, range,
requested-coordinate, row, and column readback for the shifted source row plus
the later edit. This is `try_worksheet()` handle-inspection parity for the
materialized handoff, not broader metadata synchronization.
The option-mismatch saved-session path now carries the same post-noop reuse
evidence as well: rejected mismatched options and a clean no-op save leave the
row-shifted session reusable, a later matching reacquire can write `C3`, and the
next output fresh-reopens with shifted `A3` plus the new `C3` while the earlier
first/no-op outputs stay unchanged.
It now also writes a second clean no-op output after the rejected mismatched
options path: public state stays stable, the second no-op package is
byte-identical to the first, fresh-reopens with shifted `A3`, and remains
unchanged after the later matching `C3` save.
The option-mismatch post-noop output now also pins full sparse, range,
requested-coordinate, row, and column readback for the shifted source row plus
the later edit. This is handle-inspection parity for the materialized handoff,
not broader metadata synchronization.
The missing-query saved-session path mirrors that evidence too: rejected
missing-sheet lookups and the clean no-op output leave the row-shifted session
reusable, a later matching reacquire writes `C3`, and the next output
fresh-reopens with shifted `A3` plus the new `C3` while earlier outputs remain
unchanged.
It now also repeats the clean no-op save after the rejected missing-sheet
lookups: public state stays stable, the second no-op package is byte-identical
to the first, fresh-reopens with shifted `A3`, and remains unchanged after the
later matching `C3` save.
The missing-query post-noop output now also pins full sparse, range,
requested-coordinate, row, and column readback for the shifted source row plus
the later edit. This is handle-inspection parity for the materialized handoff,
not broader metadata synchronization.
The invalid-read saved-session path now has matching post-noop evidence:
rejected invalid scalar/A1/range/batch/row/column and valid-missing reads plus a
clean no-op output leave the row-shifted session reusable, a later `C3` edit
re-dirties the shared handles with aligned materialized diagnostics, and the
next output fresh-reopens with shifted `A3` plus the new `C3` while earlier
outputs remain unchanged.
The invalid-read post-noop output now also pins full sparse, range,
requested-coordinate, row, and column readback for the shifted source row plus
the later edit. This is handle-inspection parity for the materialized handoff,
not broader metadata synchronization.
The invalid-mutation saved-session path now mirrors that post-noop coverage:
rejected invalid `set_cell` / `erase_cell` calls leave the shifted session
clean, the no-op output preserves the invalid mutation diagnostic, a later
valid `C3` edit clears that diagnostic while re-dirtying the shared handles,
and the next output fresh-reopens without leaking rejected payloads.
The invalid-mutation post-noop output now also pins full sparse, range,
requested-coordinate, row, and column readback for the shifted source row plus
the later edit. This is handle-inspection parity for the materialized handoff,
not broader metadata synchronization.
The invalid-shift saved-session path follows the same pattern: rejected invalid
insert/delete row/column shifts leave the shifted session clean, the no-op
output preserves the invalid shift diagnostic, and a later valid `C3` edit
clears that diagnostic before saving a fresh-reopenable post-noop output.
The invalid-shift post-noop output now also pins full sparse, range,
requested-coordinate, row, and column readback for the shifted source row plus
the later edit across the older handle, reacquired handle, and fresh reopen.
This is handle-inspection parity for the materialized handoff, not broader
metadata synchronization.
The failed-save retry path now has the same post-noop reuse evidence: a
rejected source-overwrite save preserves the dirty shifted session and source
bytes, the safe retry plus clean no-op output leave that saved session reusable,
and a later `C3` edit re-dirties both shared handles before saving a
fresh-reopenable post-noop output while earlier outputs remain unchanged.
The failed-save retry saved-session outputs now also pin full sparse, range,
requested-coordinate, row, and column readback for the combined row/column
shifted source cells across the older handle, reacquired handle, retry handle,
and fresh reopen. This is handle-inspection parity for the materialized
handoff, not broader metadata synchronization.
The path-equivalent failed-save retry path now mirrors that evidence: a rejected
path-equivalent source-overwrite save keeps the dirty shifted session and source
bytes intact, the safe retry/no-op output remains reusable, and a later `C3`
edit saves a fresh-reopenable post-noop output without mutating source, first,
or prior no-op outputs.
The empty-output failed-save retry path now follows the same lane: the rejected
empty output path preserves the dirty shifted session and source bytes, the
safe retry/no-op output remains reusable, and a later `C3` edit saves a
fresh-reopenable post-noop output while source, first, and prior no-op outputs
stay unchanged.
The missing-parent failed-save retry path mirrors it too: the rejected output
path does not create the missing destination, the dirty shifted session remains
usable after the safe retry/no-op save, and a later `C3` edit saves a
fresh-reopenable post-noop output while source, first, and prior no-op outputs
stay unchanged.
The non-directory-parent failed-save retry path now has the same post-noop
coverage: the rejected save preserves the parent file, the safe retry/no-op
output remains reusable, and a later `C3` edit saves a fresh-reopenable
post-noop output while source, first, and prior no-op outputs stay unchanged.
The existing-directory failed-save retry path now completes that filesystem
failure set: the rejected directory remains a directory, the safe retry/no-op
output remains reusable, and a later `C3` edit saves a fresh-reopenable
post-noop output while source, first, and prior no-op outputs stay unchanged.
The clean invalid-to-valid row-shift recovery path now also has post-noop reuse
coverage: rejected invalid row shifts stay clean, the valid recovery save/no-op
output remains reusable, and a later `C3` edit saves a fresh-reopenable
post-noop output while earlier outputs stay unchanged.
The clean invalid-to-valid column-shift recovery path mirrors that coverage:
rejected invalid column shifts stay clean, the valid recovery save/no-op output
remains reusable, and a later `D2` edit saves a fresh-reopenable post-noop
output while earlier outputs stay unchanged.
Structural shift failure roundtrips now also pin zero-count invalid start
validation: row/column zero and past-limit starts reject before the no-op fast
path in both clean and dirty materialized sessions, without mutating sparse
state or later save/no-op stability.
Those clean invalid-to-valid recovery branches now also repeat the clean no-op
save before the later `C3` / `D2` edits: the second no-op outputs are
byte-identical to the first, keep dirty and replacement diagnostics clear,
fresh-reopen with the shifted `A3` / `C1` state, and remain unchanged after the
post-noop saves.
Their post-noop outputs now also fresh-reopen with row/column snapshots for the
shifted source cell plus the later `C3` / `D2` edits.
The dirty invalid-to-valid row-shift recovery path now carries that post-noop
evidence for already-dirty sessions too: invalid shifts preserve the dirty tail,
the valid recovery save/no-op output remains reusable, and a later `C3` edit
saves a fresh-reopenable post-noop output with the shifted dirty tail intact.
The dirty invalid-to-valid column-shift recovery path now mirrors that evidence:
invalid shifts preserve the dirty tail, the valid recovery save/no-op output
remains reusable, and a later `F2` edit saves a fresh-reopenable post-noop
output with the shifted dirty tail intact.
Those already-dirty invalid-to-valid recovery branches now also repeat the
clean no-op save before the later `C3` / `F2` edits: the second no-op outputs
are byte-identical to the first, keep dirty and replacement diagnostics clear,
fresh-reopen with shifted source and dirty-tail cells, and remain unchanged
after the post-noop saves.
Their post-noop outputs now also fresh-reopen with row/column snapshots for the
shifted source cell, shifted dirty tail, and later `C3` / `F2` edits.
The row-shift cross-handle path now also carries post-noop evidence: after both
`Data` and `Untouched` are saved and a clean no-op output is proven byte-stable,
later edits on both materialized handles save a fresh-reopenable output while
the earlier first/no-op outputs stay unchanged.
The same path now repeats the clean no-op before those later edits as well,
proving the repeat no-op package is byte-identical, fresh-reopenable, and still
unchanged after the post-noop save.
That row-shift cross-handle path now also runs a second clean no-op `save_as()`
after the post-noop edit: both materialized handles stay clean, no extra handoff
is recorded, the package bytes remain stable, and both sheets fresh-reopen with
the post-noop edits intact.
The column-shift cross-handle path mirrors that post-noop evidence with later
edits on both `Data` and `Untouched`, preserving the shifted dirty columns and
leaving earlier first/no-op outputs unchanged.
It now repeats the clean no-op before those later column edits too, proving the
repeat no-op package is byte-identical, fresh-reopenable, and still unchanged
after the post-noop save.
That column-shift cross-handle path now also runs the same second clean no-op
`save_as()` after the post-noop edit, keeping both handles clean, retaining the
same handoff count, preserving byte-stable package entries, and fresh-reopening
both sheets with their post-noop edits intact.
The delete-rows cross-handle path now has the same post-noop reuse coverage:
after `Data` rows are removed and both handles are saved/no-op verified, later
edits on both sheets save a fresh-reopenable output without mutating earlier
outputs.
It now repeats the clean no-op before those later delete-row edits too, proving
the repeat no-op package is byte-identical, fresh-reopenable, and still
unchanged after the post-noop save.
That delete-rows cross-handle path now also runs a second clean no-op
`save_as()` after the post-noop edit, keeping both handles clean, retaining the
same handoff count, preserving byte-stable package entries, and fresh-reopening
both sheets with the deleted-row post-noop edits intact.
The delete-columns cross-handle path mirrors that reuse coverage while avoiding
old deleted coordinates: later `D1` edits on both sheets save cleanly and the
old `D2` coordinates remain absent.
It now repeats the clean no-op before those later delete-column edits too,
proving the repeat no-op package is byte-identical, fresh-reopenable, and still
unchanged after the post-noop save.
That delete-columns cross-handle path now also runs a second clean no-op
`save_as()` after the post-noop edit, keeping both handles clean, retaining the
same handoff count, preserving byte-stable package entries, and fresh-reopening
both sheets with the deleted-column post-noop edits intact.
The cross-handle row/column insert/delete post-noop matrix now also snapshots
the original source package and requires it to remain unchanged after the first
multi-handle save, the clean no-op save, the post-noop edit save, and the final
post-noop clean no-op save.
The `delete_rows()` styled source formula path now also continues after its
clean no-op save: a later `E2` edit re-dirties the saved materialized handle,
preserves the shifted styled `D1` formula, writes a fresh-reopenable post-noop
output, and then proves a second clean no-op save is byte-stable.
The `delete_columns()` styled source formula path mirrors that reuse coverage:
a later `D1` edit re-dirties the saved materialized handle, preserves the
shifted styled `C2` formula, writes a fresh-reopenable post-noop output, and
then proves a second clean no-op save is byte-stable.
The `insert_rows()` styled source formula path now has the same post-noop
reuse coverage: a later `E5` edit re-dirties the saved materialized handle,
preserves the shifted styled `D4` formula, writes a fresh-reopenable post-noop
output, and then proves a second clean no-op save is byte-stable.
The `insert_columns()` styled source formula path mirrors that coverage with a
source-backed styled formula shifted to `F2`, a later `G2` edit, a
fresh-reopenable post-noop output, and a byte-stable second clean no-op save.
That insert-column styled source formula path is now also pinned when
`request_full_calculation()` is queued after the dirty materialized shift:
dirty materialized diagnostics stay aligned, `save_as()` writes the shifted
styled `F2` formula with `fullCalcOnLoad="1"`, and no `xl/calcChain.xml` is
invented.
That same after-shift styled insert-column/full-calculation path now covers
rejected exact source overwrite too: after `insert_columns(2, 2)` and then
`request_full_calculation()`, the failed save preserves dirty diagnostics,
the translated `F2` formula/style, shifted source cells, queued workbook
metadata, and source bytes; a safe retry writes `fullCalcOnLoad="1"` with no
`xl/calcChain.xml`.
The full-calculation `insert_rows()` materialized shift path now also pins
reopened `row_cells()` / `column_cells()` snapshots: both the first shifted
output and its clean no-op output expose row-five source/dirty trailing cells
in sparse order and column-four as the styled translated formula.
That same after-shift `insert_rows()` full-calculation path now also reopens
the `Untouched` sheet from the shifted output and both no-op outputs, and the
second no-op save proves the shifted output plus first no-op package remain
unchanged.
The full-calculation `delete_rows()` materialized shift path now mirrors that
snapshot readback: both the shifted output and its clean no-op output expose
row-one as the shifted source row plus styled `#REF!+#REF!` formula, and
column-one as the shifted source rows.
The full-calculation `insert_columns()` materialized shift path now has the
same snapshot readback for styled source formulas: both the shifted output and
its clean no-op output expose row-two sparse ordering after the inserted gap
and column-six as the styled translated `C1+D1` formula.
That same after-shift `insert_columns()` full-calculation path now also reopens
the `Untouched` sheet from the shifted output and both no-op outputs, and the
second no-op save proves the shifted output plus first no-op package remain
unchanged.
The reverse-order full-calculation `delete_columns()` materialized shift path
now has matching column-side snapshot coverage too: both the shifted output
and its clean no-op output expose row-two source/formula ordering after
deleting column A, and column-three as the styled translated `#REF!+A1`
formula.
The after-shift full-calculation delete-side paths now also reopen `Untouched`
from the shifted output and both no-op outputs. `delete_rows()` and
`delete_columns()` both prove the second clean no-op save leaves the shifted
output plus first no-op package unchanged while preserving the existing
translated `#REF!` formula readback.
The after-shift full-calculation insert-side failed-save retry paths now carry
the same second clean no-op contract: after exact source overwrite is rejected,
styled `insert_rows()` and `insert_columns()` safe retries keep source bytes
unchanged, write shifted formulas with `fullCalcOnLoad`, then preserve the safe
retry and first no-op packages across another no-op save with fresh Data,
`Untouched`, and source readback.
The after-shift full-calculation delete-side failed-save retry paths now match
that second clean no-op contract too: after exact source overwrite is rejected,
styled `delete_rows()` and `delete_columns()` safe retries keep source bytes
unchanged, write shifted `#REF!` formulas with `fullCalcOnLoad`, then preserve
the safe retry and first no-op packages across another no-op save with fresh
Data, `Untouched`, and source readback.
The renamed full-calculation formula-audit failed-save retry path now also
uses that second clean no-op contract: after rejected source overwrite and a
safe retry, it preserves source bytes, the shifted output, and the first
no-op package across another no-op save, then fresh-reopens the second no-op
output with the same styled qualified formula references.
The shift-after-rename styled formula failed-save retry path now carries the
same second no-op evidence: the safe retry output, first no-op output, and
source package remain unchanged, and the second no-op output fresh-reopens with
the translated styled formula plus shifted source cells under `RenamedData`.
The shift-after-rename delete-side styled formula failed-save retry paths now
carry the same second no-op evidence too: both `delete_columns()` and
`delete_rows()` preserve the rejected source package, safe retry output, and
first no-op output before fresh-reopening the second no-op output with the
delete-shifted source cells and translated styled `#REF!` formulas under
`RenamedData`.
The non-formula shift-after-rename failed-save retry path now has the same
repeated no-op proof before its later delete mutation: rejected exact,
path-equivalent, empty, missing-parent, non-directory-parent, and
directory-output saves leave the dirty planned session intact, then the safe
retry, first no-op output, and second no-op output preserve source and prior
package bytes while reopening the combined row/column-shifted `RenamedData`
state cleanly.
The baseline non-formula shift-after-rename matching-reacquire path now also
continues past that clean state: after a second byte-stable no-op save, a later
`D3` edit re-dirties both planned-name handles, preserves the shifted `A3` and
`C1` cells, saves a fresh-reopenable post-noop output, and leaves the earlier
source/first/second/no-op packages unchanged.
The non-formula shift-after-rename option-mismatch path now mirrors that
repeated no-op proof: after mismatched worksheet options are rejected and a
matching reacquire applies the later column shift, the first and second no-op
outputs preserve source, first-stage, second-stage, and prior no-op package
bytes while fresh readback still exposes the combined row/column-shifted
`RenamedData` state cleanly.
It now also continues from that repeated no-op checkpoint: a later `D3` edit
clears no diagnostics, re-dirties the shared planned-name handles with aligned
renamed materialized summaries, saves a fresh-reopenable post-noop output, and
keeps source, first-stage, second-stage, and both no-op packages unchanged.
The non-formula shift-after-rename missing-query path now carries the same
repeated no-op evidence: rejected missing planned-name and old source-name
lookups keep the saved planned session clean, and after the later matching
reacquire plus column shift, the first and second no-op outputs preserve source
and prior package bytes while fresh readback still exposes the combined
row/column-shifted `RenamedData` state cleanly.
It now also mirrors the option-mismatch post-noop reuse check: a later `D3`
edit after the repeated no-op save re-dirties the shared planned-name handles,
saves a fresh-reopenable output with shifted `A3` / `C1` plus the new `D3`, and
leaves source, first-stage, second-stage, and both no-op packages unchanged.
The non-formula shift-after-rename invalid-read path now follows the same
pattern: rejected scalar, A1, range, row-snapshot, and column-snapshot reads do
not dirty either planned-name handle, and after the later matching reacquire
plus column shift, repeated no-op saves preserve source and prior package bytes
while fresh readback still exposes the combined row/column-shifted
`RenamedData` state cleanly.
It now also continues from that repeated no-op checkpoint: a later `D3` edit
re-dirties both planned-name handles, saves a fresh-reopenable post-noop output
with shifted `A3` / `C1` plus the new cell, and leaves source, first-stage,
second-stage, and both no-op packages unchanged.
The non-formula shift-after-rename invalid-mutation path now has matching
evidence: rejected `set_cell()` / `erase_cell()` calls may populate diagnostics
but do not dirty either planned-name handle or leak rejected payloads, and the
later valid column shift plus repeated no-op saves preserve source and prior
package bytes while fresh readback still exposes the combined row/column-shifted
`RenamedData` state cleanly.
It now also continues after that repeated no-op checkpoint: a later `D3` edit
keeps diagnostics clear, re-dirties both planned-name handles, saves a
fresh-reopenable post-noop output with shifted `A3` / `C1` plus the new cell,
and still proves rejected mutation payloads never reached the saved package.
The same-handle row/column shift reuse path now carries the repeated no-op
proof as well: one borrowed `WorksheetEditor` can save a row shift, apply and
save a later column shift, then run first and second no-op saves while source
and prior package bytes remain unchanged and fresh readback exposes the combined
row/column-shifted `Data` state cleanly.
It now also continues from that clean repeated no-op state: the same borrowed
handle can write a later `D3` cell, re-dirty the materialized `Data` session,
save a fresh-reopenable post-noop output with shifted `A3` / `C1` plus row and
column snapshots for the new cell, and keep all earlier packages unchanged.
The reacquired row/column shift reuse path now mirrors that post-noop
continuation: after saving a row shift, reacquiring the clean session, applying
and saving a later column shift, and running repeated clean no-op saves, a later
`D3` edit re-dirties both shared handles, saves a fresh-reopenable output with
shifted `A3` / `C1` plus row/column snapshots for the new cell, and leaves
source, first-stage, second-stage, and both no-op packages unchanged. The same
final post-noop output now also pins sparse, range, requested-coordinate, row,
and column readback for the shifted row plus the later edit; this is snapshot
parity for the materialized session handoff, not broader metadata sync.
The failed-save retry + clean reacquire no-op path now also continues from that
saved-session checkpoint: after exact source-overwrite rejection, safe retry,
clean reacquire, and a clean no-op save, a later `D3` edit re-dirties all shared
handles, saves a fresh-reopenable output with shifted `A3` / `C1` plus
row/column snapshots for the new cell, and leaves the source, safe retry, and
no-op packages byte-stable.
The failed-save retry + later delete path now has the same continuation after
its third clean no-op checkpoint: after exact source-overwrite rejection, safe
retry, clean reacquire, deleting the shifted source row, and writing a clean
no-op output, a later `D1` edit re-dirties all shared handles, saves a
fresh-reopenable output with shifted `C1` plus row/column snapshots for the new
cell, and leaves source, first-stage, safe-retry, delete-stage, and no-op
packages unchanged.
The reverse-order full-calculation `insert_rows()` and `insert_columns()`
success paths now also mirror the repeated no-op readback coverage: each queues
`request_full_calculation()` before materialization, flushes the shifted sparse
cells once, fresh-reopens the materialized output, then writes first and second
clean no-op outputs with stable public state, byte-identical package entries,
`fullCalcOnLoad`, no invented `calcChain.xml`, and row/column snapshot readback
of the translated styled formulas.
The non-styled reverse-order `insert_columns()` success path now carries the
same second no-op save contract too: the shifted output, first no-op output, and
second no-op output remain byte-stable, fresh-reopen with the translated
`C1+D1` formula, and keep the `Untouched` sheet readable.
The reverse-order full-calculation `delete_rows()` and `delete_columns()`
success paths now match that repeated no-op coverage too: row deletion
fresh-reopens the shifted output plus first and second no-op outputs, while
column deletion extends its no-op readback with a second clean no-op output.
Both paths keep stable public state, byte-identical package entries,
`fullCalcOnLoad`, no invented `calcChain.xml`, and row/column snapshot readback
of the translated styled `#REF!` formulas.
The reverse-order full-calculation insert-shift failed-save retry paths now also
cover a second clean no-op output after the safe retry and first no-op save. The
styled row/column insert tests keep source overwrite rejection non-mutating,
verify unchanged safe retry and first no-op packages, and fresh-reopen the
second no-op workbook plus the untouched sheet while confirming source bytes
remain unchanged.
The reverse-order full-calculation delete-shift failed-save retry paths now
match that same second no-op coverage: styled row/column delete retries keep
source overwrite rejection non-mutating, preserve the safe retry and first
no-op packages, and fresh-reopen the second no-op workbook plus the untouched
sheet while confirming source bytes remain unchanged.
The after-shift `delete_columns()` full-calculation failed-save retry path now
uses the same reopened snapshot checks after exact source overwrite rejection:
the clean no-op retry output exposes row-two sparse order and column-three as
the styled translated `#REF!+A1` formula.
The after-shift `insert_columns()` full-calculation failed-save retry path now
mirrors that check: after exact source overwrite rejection, the clean no-op
retry output exposes row-two sparse order after the inserted gap and column-six
as the styled translated `C1+D1` formula.
The reverse-order `insert_columns()` full-calculation failed-save retry path
now has the same coverage when `request_full_calculation()` is queued before
materialization, including row-two sparse ordering and column-six styled
formula readback after the safe retry/no-op output.
The `insert_rows()` rich formula-shape path now also has post-noop formula-sheet
reuse coverage: a later `D3` formula save preserves the already translated
`C3` formula and leaves earlier first/no-op outputs unchanged.
The `insert_columns()` rich formula-shape path mirrors that coverage with a
later `F2` formula save that preserves the translated `E2` formula.
They now repeat the clean no-op before those later insert-side formula edits
too, proving the repeat no-op package is byte-identical, fresh-reopenable, and
still unchanged after the post-noop save.
Those insert-side rich formula paths now also run a second clean no-op
`save_as()` after the post-noop edit: the saved-session diagnostics stay clean,
no extra handoff is recorded, the output remains byte-stable, and a fresh reopen
still reads the translated formula plus the later formula edit.
Those insert-side rich formula post-noop saves now also assert replacement
diagnostics stay empty at the formula-edit save point and at the following
clean no-op save.
The `delete_rows()` `#REF!` formula path now has matching post-noop reuse
coverage: a later `D3` formula save preserves the translated `C3` `#REF!`
formula and leaves earlier first/no-op outputs unchanged.
The `delete_columns()` `#REF!` formula path mirrors that coverage with a later
`D1` formula save that preserves the translated `C1` `#REF!` formula.
The rich formula-shape matrix now also covers `delete_rows()`: the moved `C3`
formula preserves relative/absolute/range/qualified/whole-axis/skip-token
translation behavior, the clean no-op output is byte-stable, and a later `D3`
formula save keeps the translated formula intact.
The same matrix now covers `delete_columns()` with a moved `C2` formula and a
later `D2` formula save, pinning the column-deletion side of the same translator
and post-noop reuse behavior.
They now repeat the clean no-op before those later delete-side formula edits
too, proving the repeat no-op package is byte-identical, fresh-reopenable, and
still unchanged after the post-noop save.
Those delete-side rich formula paths now also run a second clean no-op
`save_as()` after the post-noop edit: the saved-session diagnostics stay clean,
no extra handoff is recorded, the output remains byte-stable, and a fresh reopen
still reads the translated formula plus the later formula edit.
Those delete-side rich formula post-noop saves now also assert replacement
diagnostics stay empty at the formula-edit save point and at the following
clean no-op save.
The rich formula-shape row/column insert/delete matrix now also snapshots the
source package before the formula-shift branches and requires it to remain
unchanged after the initial translated-formula shift save, the clean no-op save,
the later formula-edit save, and the final clean no-op save while prior outputs
stay byte-stable.
The delete-side `#REF!` formula paths now carry the same repeated no-op save
coverage after their post-noop edits, preserving the translated `#REF!` formula,
the later formula edit, shifted source cells, and clean diagnostics across a
fresh reopen.
They now repeat the clean no-op before those later `#REF!` formula edits too,
proving the repeat no-op package is byte-identical, fresh-reopenable, and still
unchanged after the post-noop save.
Those delete-side `#REF!` formula post-noop saves now also assert replacement
diagnostics stay empty at the formula-edit save point and at the following
clean no-op save.
Those delete-side `#REF!` formula paths now also snapshot the source package
before row/column deletion and require it to remain unchanged after the initial
translated `#REF!` save, the clean no-op save, the later formula-edit save, and
the final clean no-op save while prior outputs stay byte-stable.
Their fresh-reopen checks now also snapshot row/column views before and after
the later formula edits, pinning shifted `#REF!` formula order and post-noop
formula order without claiming formula evaluation or metadata synchronization.
The live materialized checks now mirror those row/column snapshots before the
initial save and after the later dirty formula edit, so the public sparse views
are pinned on both sides of the save/reopen handoff.
The same delete-side `#REF!` formula paths now also pin global `sparse_cells()`
row-major order for the shifted source/formula cells before save, after
fresh-reopen, and after the later dirty formula edit.
They now mirror those checks through A1 range `sparse_cells()` reads for the
same shifted and post-noop `#REF!` formula snapshots.
They now also exercise coordinate-batch `sparse_cells()` reads with stale
pre-shift coordinates skipped while the shifted and post-noop formula cells stay
visible.
Their live and fresh-reopen checks now also cover `contains_cell()` for shifted
source/formula cells, deleted old coordinates, and the later post-noop formula
edits.
The rich formula-shape matrix now has matching `contains_cell()` checks for
row/column insert/delete outputs and post-noop formula edits, pinning represented
cell presence without expanding into formula evaluation or metadata sync.
The same matrix now pins global `sparse_cells()` row-major snapshots for those
rich formula-shape shifts before save, after fresh reopen, and after the later
formula edits.
It now mirrors those checks through A1 range `sparse_cells()` reads, covering the
same shifted and post-noop formula cells through the range snapshot overload.
The same rich formula-shape matrix now also exercises coordinate-batch
`sparse_cells()` reads so stale pre-shift coordinates are skipped while shifted
and later post-noop formula cells remain visible.
It now mirrors the row/column snapshot checks on live materialized sessions before
the initial save and after later post-noop formula edits, so fresh reopen is not
the only public surface proving represented formula order.
The basic materialized shift reacquire paths now carry the same replacement
diagnostics check after their post-noop edit/save step for row insert, column
insert, row delete, and column delete.
The try-reacquire, option-mismatch, and missing-query reacquire guard paths now
pin the same post-noop replacement diagnostics contract after their shared
handle edit/save step.
The invalid read, invalid mutation, and invalid shift reacquire paths now also
pin that post-noop recovery saves do not queue replacement diagnostics.
The failed-save reacquire guard paths now pin the same replacement diagnostics
contract after safe post-noop recovery saves for source-overwrite,
path-equivalent, empty-output, missing-parent, file-parent, and directory-output
failures.
The invalid-to-valid row/column shift recovery paths now pin that same
replacement diagnostics contract after clean and already-dirty post-noop
recovery saves.
The saved-session shift reacquire no-op paths now carry the same contract before
those later post-noop edits: basic, try-reacquire, guard/query, invalid-read,
safe-retry, and failed-save guard no-op saves all keep replacement diagnostics
empty.
The basic reacquire second safe-save points now use the same clean diagnostics
gate as well: option mismatch, missing `try_worksheet()`, missing worksheet,
catalog query, and diagnostic query recovery saves all clear dirty materialized
names/count/memory, summaries, replacement diagnostics, and `last_edit_error()`.
The row/column insert/delete reacquire second safe-save points now share that
helper too, so shifted sparse saves clear materialized memory diagnostics,
replacement diagnostics, summaries, and `last_edit_error()` before XML readback.
The row-shift reacquire recovery path now also repeats a clean no-op `save_as()`
after the second safe output, proving public save/catalog snapshots stay stable,
the no-op package matches the shifted output, source bytes remain unchanged, and
the no-op workbook fresh-reopens with the translated moved formula.
The column-shift reacquire recovery path now mirrors that clean no-op gate:
the no-op package matches the shifted output, source bytes stay unchanged,
public save/catalog snapshots stay stable, and fresh reopen keeps the shifted
number plus translated formula layout.
The styled row-shift reacquire path now also repeats the clean no-op save:
the no-op package matches the shifted output, source bytes remain unchanged,
public save/catalog snapshots stay stable, and fresh reopen keeps the
translated formula plus original `StyleId`.
The basic delete-row and delete-column reacquire paths now mirror that no-op
save gate: the no-op packages match their shifted outputs, source bytes remain
unchanged, public snapshots stay stable, and fresh reopen preserves the shifted
source-backed cells, dirty tails, translated formulas, and row/column views.
The delete-row and delete-column `#REF!` formula reacquire paths now repeat
that clean no-op save too: formula-reference deletion outputs clear dirty
materialized diagnostics, replacement diagnostics, summaries, and
`last_edit_error()` before fresh reopen, and their no-op outputs match the
shifted packages while preserving the `#REF!` formula row/column readback.
They now also continue past that clean no-op point: later same-handle edits
save fresh post-noop outputs, leave source/shifted/no-op packages unchanged,
and fresh reopen keeps both the translated `#REF!` formulas and the later
cells.
Those post-noop outputs now settle through one more clean no-op save as well,
requiring byte-identical final no-op packages, stable public save/catalog
snapshots, unchanged prior artifacts, and fresh reopen of the translated
`#REF!` formulas plus later cells.
The row-insert saved-session reacquire no-op path now also covers a moved dirty
formula and moved dirty tail cell: after `insert_rows()` translates the formula
and the first `save_as()` flushes it, matching-option reacquire reuses that
saved sparse state, the clean no-op output stays byte-stable, and fresh reopen
keeps the translated formula, shifted source row, and old formula coordinate
absence. This is saved-session lifecycle hygiene only, not broad formula
rewrite, metadata sync, or calcChain rebuild.
That row-insert formula reacquire path now repeats the clean no-op save as
well, proving the second no-op package remains byte-stable while the translated
formula, shifted dirty tail, source workbook, first output, and first no-op
output all remain unchanged.
It now also mirrors the sibling delete-row/delete-column/insert-column paths
after those repeated clean no-op saves: a later `D4` edit re-dirties both shared
handles, saves as a fresh post-noop output, leaves all earlier packages
unchanged, and fresh reopen preserves the translated formula, shifted dirty
tail, row/column views, and new post-noop cell.
The `clear_cell_values()` memory-budget release saved-session path now extends
that no-op diagnostics parity to option-mismatch, missing-query, and invalid-read
no-op saves.
Its base matching-option reacquired no-op save now also repeats once more,
requiring the second package to match the first matching-option no-op while
leaving the saved output, first no-op output, first matching-option no-op
output, and source package unchanged.
The option-mismatch and missing-query no-op saves in that path now also reuse
the saved-handle snapshot and fresh-reopen checks, while proving earlier no-op
packages stay byte-stable.
The invalid-read no-op save in that exact-budget release path now also snapshots
both the original and matching-option reacquired handles: full sparse ordering,
row-one blanks, column-one blanks, saved `D4`, missing `E5`, and clean
materialized diagnostics stay stable. It now fresh-reopens that no-op output and
keeps the earlier missing-query and second no-op packages byte-stable too.
The invalid-mutation no-op save in that exact-budget release path now reuses
those saved-handle snapshots while preserving the expected invalid-reference
diagnostic, fresh-reopening the saved package and keeping the invalid-read and
second no-op packages unchanged.
The invalid-shift no-op save after the recovery output now snapshots both saved
handles too, including the recovered `E5` row/column views and the preserved
shift diagnostic; it also fresh-reopens that no-op package and proves the
earlier recovery/no-op packages stay byte-stable.
The earlier public-state no-op paths now carry the same prior-output stability
check: source-style snapshots, row/column read failures, invalid sparse range
reads, `erase_cell()`, and range erase all prove a second clean no-op save does
not rewrite the first no-op package.
The next early public-state group now carries that same check too: invalid A1
range mutations, invalid cell reads, empty `append_row()`, empty `set_row()`,
and empty `set_column()` all prove the first no-op package remains unchanged
after a repeated clean no-op save.
The follow-on public-state no-op group now does the same for empty
`set_row_values()` / `set_column_values()`, styled `erase_column()` /
`erase_columns()`, and renamed shift reacquire saves.
The earliest dirty-session reuse paths now also pin the second output package
while writing clean no-op outputs for dirty-state reuse and same-handle reuse.
The first row/column shift reacquire no-op trio now pins the second output
package too across base, try-reacquire, and option-mismatch clean saves.
The remaining guard-side row/column shift reacquire no-op saves now pin that
same second-output stability for missing-query, invalid-read, invalid-mutation,
and failed-save recovery paths.
The batch and row/column write no-op paths now also prove the first no-op
package remains unchanged across repeated clean saves for initializer-list
batch writes, append-row writes, styled append-row writes, row replacement, and
column replacement.
The value-batch write no-op paths now carry the same prior-output stability for
styled `set_cell_values()`, row-value writes, styled row-value writes,
column-value writes, and styled column-value writes.
The clear/erase-all no-op paths now extend that prior-output stability to
`clear_cell_values()`, `erase_cells()`, and the erase-all exact memory-budget
release case: the second clean save must leave the first no-op package unchanged.
The row/column recovery, styled clear row/column, and erase row/column no-op
paths now carry the same prior-output stability check, covering represented
sparse record removal and source-style-preserving clears without changing the
first clean no-op package.
The remaining insert/delete row/column no-op shift family now carries the same
check as well, including value-only and cleared-style variants, so repeated
clean saves preserve the first no-op package alongside the materialized output.
The same-sheet guard no-op save in that clear-all exact-budget path now carries
the same saved-handle snapshot coverage for shifted `E6`, absent `E5`/`E7`,
and the preserved guard diagnostic; it also fresh-reopens the no-op package and
keeps the preceding shift/mutation recovery packages byte-stable.
The same-sheet guard recovery save now snapshots both saved handles after the
accepted `E7` write, pinning the `E6`/`E7` column view, clean diagnostics, and
stable materialized counters while keeping the preceding guard/shift output
packages byte-stable.
The renamed-summary path now also snapshots both saved handles after its later
`E5` recovery save, pinning the cleared row-one blanks, `E5` column view, and
clean rename/materialized summaries; that recovery save now also keeps the
earlier first/no-op/diagnostic no-op output packages byte-stable.
The remaining no-op diagnostics gap is now closed for missing-erase guardrail
recovery and materialized last-error replacement recovery outputs: those clean
no-op saves also keep replacement diagnostics empty.
Those guardrail/diagnostic repeated-noop paths now also re-read the prior
output packages after later saves: max-cells and memory-budget budget-release,
missing-erase, blank-overwrite, last-error recovery, and mixed diagnostic
recovery all prove the first no-op package remains unchanged after the second
no-op and post-noop save.
The blank-overwrite guardrail same-editor no-op saves now also pin the clean
diagnostic surface directly: max-cells and memory-budget first/second no-op
outputs keep replacement diagnostics empty and clear `last_edit_error()` while
preserving the already byte-stable packages.
The same prior-output check now covers the adjacent source-load and recovery
no-op family: shift memory guard failure, exact shift-budget recovery scenarios,
options-guard recovery, memory-budget source-load recovery, and successful
mutation recovery after memory-budget / max-cells failures all re-read the
first no-op package after the second no-op save.
The retry guard reopened-output helper now checks the same complete clean
diagnostic surface after saved/no-op recovery outputs: no pending summaries,
no materialized names/count/memory, no replacement diagnostics, and no
`last_edit_error()` leakage before row/column snapshot reads.
The shift-guard saved and no-op reopened outputs now mirror that contract,
including empty replacement diagnostics, materialized counters, pending
summaries, and `last_edit_error()` before their shifted-row readback.
The guard recovery second safe-save points now use the same clean diagnostics
gate for handle-read, invalid-read, invalid-mutation, shift-guard, and
missing-erase recovery saves before their package XML/readback checks.
The core materialized saved-session no-op paths now also keep replacement
diagnostics empty for single-sheet dirty-state reuse, same-handle reuse,
multi-sheet save/retry, and single-/multi-sheet reopen post-noop lifecycle
outputs.
Range-erase reacquire and `clear_cell_values()` renamed memory-budget summary
no-op sequences now carry the same diagnostics parity: replacement diagnostics
stay empty across range erase reacquire, option-mismatch, missing-query,
read-only-query, invalid-read, and second no-op saves.
The range-erase reacquired no-op save now also snapshots both saved live
handles, pinning the lone `C3` row/column view, erased source-cell absence, and
clean materialized diagnostics before the fresh reopen checks.
It now repeats that post-reacquire clean no-op save too, requiring
byte-identical package entries and the same fresh-reopened `C3` projection while
keeping the first reacquired no-op output stable and replacement diagnostics
empty.
The whole-store `erase_cells()` reacquired no-op path now mirrors that saved
live-handle snapshot coverage for the appended `A1:B1` row, including matching
column views, erased dirty-cell absence, and a repeated no-op save that keeps
the first reacquired no-op output unchanged.
The exact-budget `erase_cells()` release no-op path now pins its saved live
handle as well, including the lone recovery `A3` row/column views, erased
source-cell absence, and clean materialized diagnostics.
It now repeats that clean no-op save too, requiring the second package to match
the first no-op output, preserving source bytes, and fresh-reopening the
recovery `A3` projection without reviving erased source cells.
The range and batch exact-budget `erase_cells()` release no-op paths now share
that saved-handle coverage for their recovery `A3` cells and clean summaries.
They now repeat those same-editor clean no-op saves too, requiring the second
package to match the first no-op output while the first materialized output,
first no-op output, source bytes, and same fresh-reopened recovery `A3`
projection remain unchanged.
They now also reacquire those no-op outputs with the original strict
`WorksheetEditorOptions`, keeping the recovery sparse snapshots within budget
while the matching-option clean no-op saves remain byte-stable.
Those matching-option reacquired no-op saves now repeat once more too, requiring
the second reacquired package to match the first while the first reacquired
no-op output, saved input package, and original source bytes remain unchanged.
The exact-budget `clear_row()` and `clear_column()` release no-op paths now
pin saved live-handle row/column snapshots for explicit blanks, preserved
source payloads, recovery cells, and clean materialized diagnostics.
Their matching-option reacquired no-op saves now repeat once more too, requiring
the second package to match the first matching-option no-op while preserving the
saved input package and original source bytes.
The `erase_rows()` and `erase_columns()` exact memory-budget release paths now
also reopen the saved outputs with the original strict options, repeat
byte-stable clean no-op saves, then prove a later small `A3` overwrite can
save and reopen without reviving erased cells or rejected guardrail payloads.
The matching `clear_rows()` and `clear_columns()` exact-budget release no-op
paths now extend that saved-handle coverage across multi-row and multi-column
blank/source/recovery views.
They now also cover same-editor matching-option reacquire plus clean no-op
saves, proving both saved handles stay clean and package entries remain stable.
Those matching-option no-op saves now repeat once more too, requiring the
second package to match the first matching-option no-op while the first
matching-option no-op output, saved input package, and original source bytes
remain unchanged.
Those multi-row and multi-column exact memory-budget release paths now also
reopen their saved outputs with the original strict options, repeat byte-stable
clean no-op saves, then prove a later smaller `D4` overwrite saves and reopens
without reviving cleared payload text or rejected guardrail values.
The sparse range and batch `clear_cell_values()` exact-budget release no-op
paths now carry the same saved-handle row/column snapshot checks for explicit
blanks, preserved source payloads, recovery cells, and clean materialized
diagnostics.
Their matching-option reacquired no-op saves now repeat once more too, requiring
the second package to match the first matching-option no-op while the first
matching-option no-op output, saved input package, and original source bytes
remain unchanged.
They now also reopen the saved outputs with the original strict
`WorksheetEditorOptions`, repeat byte-stable clean no-op saves, then prove a
later smaller `D4` overwrite saves and reopens without reviving cleared payload
text, missing batch coordinates, or rejected guardrail values.
The renamed full-calc formula-audit saved/reacquire no-op paths now carry the
same contract across preserved-state, failed-save recovery, invalid mutation,
invalid read, invalid shift, missing query, option mismatch, and same-sheet
guard recovery outputs.
Rename-after-shift failed-save no-op retry paths now also keep replacement
diagnostics empty for row insert, delete-column, and delete-row styled formula
sessions while preserving shifted formulas, style ids, and source package bytes.
Rename-after-shift planned-session no-op paths now extend that diagnostics
contract to reacquire, failed-save retry, option-mismatch, missing-query,
invalid-read, and invalid-mutation sessions.
Saved-session and reacquire shift clean no-op paths now also keep replacement
diagnostics empty for handle reuse, try-reacquire, option-mismatch,
missing-query, invalid-read, invalid-mutation, and failed-save retry sessions.
The invalid-read, invalid-mutation, and invalid-shift saved-session no-op paths
now also repeat a clean no-op save, fresh-reopen that repeat output, and verify
the later `C3` save leaves both no-op packages unchanged while preserving each
path's expected diagnostic behavior.
The saved-session failed-save retry no-op path now follows the same repeat
no-op contract after the safe retry, preserving source, shifted, retry, and
first no-op packages before the later `C3` post-noop save.
Retry clean-reacquire and invalid-operation no-op shift paths now also assert
replacement diagnostics stay empty while preserving expected invalid-operation
`last_edit_error()` state.
Rejected option-mismatch, missing-query, invalid-read, and invalid-mutation
shift states now also keep replacement diagnostics empty before their existing
no-op save and readback checks.
Renamed full-calc formula-audit saved-reacquire intermediate and no-op states
now keep replacement diagnostics empty while preserving formula audit snapshots,
saved edit summaries, and expected invalid-operation diagnostics.
The full-calc saved-reacquire diagnostics now also have a dedicated helper for
the rename + fullCalc metadata + saved materialized handoff count, covering the
base saved-reacquire path and the same-sheet guard recovery/no-op setup before
later reads, audits, or guard failures.
That helper now spans the adjacent invalid mutation/read/shift, missing-query,
and option-mismatch setup families too, so each rejected operation starts from
the same pinned clean saved-reacquire diagnostics contract.
The failed-save retry branch now enters its later dirty mutation from the same
clean full-calc saved-reacquire diagnostics snapshot before testing rejected
source-overwrite output handling.
That failed-save retry branch now also pins the later no-op output side: after
the safe retry, the no-op save keeps shifted formula-audit references and source
audit state stable, and the no-op package fresh-reopens with the shifted styled
formula plus the retried `C5` text cell.
The adjacent invalid-operation recovery/no-op branches now share that same
no-op formula-audit/readback helper for invalid mutation, invalid read, invalid
shift, missing-query, and option-mismatch recovery saves.
The same-sheet guard recovery branch now uses that helper too, so its post-guard
valid `C5` recovery save and subsequent clean no-op save recheck shifted
materialized formula audits, source-audit stability, and fresh no-op readback.
Pure no-op saved-reacquire branches now have the sibling no-`C5` helper for
invalid mutation, invalid read, invalid shift, missing-query, option-mismatch,
and same-sheet guard outputs, keeping audit/readback parity without inventing
recovery cells.
Delete-side renamed formula-audit no-op outputs now have the same shared
contract shape: the no-op materialized audit still skips `Data!#REF!`, keeps
only the surviving qualified reference, preserves source-scan isolation, and
fresh-reopens the styled `#REF!` formula output.
Insert-side renamed formula-audit no-op outputs now mirror that audit/readback
helper shape for row and column insertion: no-op materialized audits keep both
shifted qualified references, source scans remain isolated, and the no-op
packages fresh-reopen with the styled shifted formulas.
Insert-side renamed formula-audit post-save reacquire checks now use the same
saved-session helper shape: clean reacquired sessions keep materialized
diagnostics empty, preserve both shifted qualified references, and keep source
formula-audit scans isolated on the original source XML.
Delete-side renamed formula-audit post-save reacquire checks now use the same
saved-session helper shape for the `#REF!` paths: clean reacquired sessions keep
materialized diagnostics empty, skip the translated `Data!#REF!` token, preserve
only the surviving qualified reference, and keep source scans isolated.
Full-calculation renamed formula-audit saved-reacquire and no-op readback paths
now share the same fixed two-reference audit helper, keeping the shifted
`Data!A2` / `Data!B2` materialized audit and source-scan isolation contract
aligned before their existing mutation and fresh-reopen checks.
Renamed formula insert-row no-op and rejected-operation paths now also keep
replacement diagnostics empty while preserving translated styled formulas,
snapshot ownership, byte-stable outputs, and fresh readback.
Renamed formula delete-column no-op and rejected-operation paths now keep
replacement diagnostics empty while preserving `#REF!` formula translation,
style ids, byte-stable outputs, and fresh readback.
Renamed formula delete-row no-op and rejected-operation paths now mirror that
replacement-diagnostics contract for `#REF!` translation, style ids,
byte-stable outputs, and fresh readback.
It also covers `generated_in_memory_multi_sheet_save`, which dirties two
materialized worksheets in the same editor session and verifies one `save_as()`
flushes both while preserving an untouched sheet.
The same generated multi-sheet lane now also has
`generated_in_memory_multi_sheet_noop_save`, which requires a follow-up no-op
`save_as()` output to be byte-identical to the first multi-sheet save.
It also covers `generated_in_memory_multi_sheet_retry_save`, which first
attempts the rejected source-overwrite save path, verifies the source workbook
keeps its old payloads, then validates the safe retry output.
The retry lane now also has
`generated_in_memory_multi_sheet_retry_noop_save`, which repeats that rejected
source-overwrite path and requires the safe retry output to be byte-identical
to a follow-up no-op `save_as()` output.
It also covers
`generated_in_memory_multi_sheet_retry_path_equivalent_noop_save`, which uses a
path-equivalent source output path for the rejected multi-sheet retry before
the same safe-retry/no-op byte-stability checks.
It also covers `generated_in_memory_multi_sheet_retry_reopen_modify_save`, which
reopens that safe retry output through a fresh editor, applies second-stage
`Data` and `Summary` edits, and validates the final workbook.
It also covers `generated_in_memory_multi_sheet_retry_reopen_modify_noop_save`,
which repeats that path and then requires a final no-op `save_as()` to be
byte-identical to the second-stage saved output.
It also covers
`generated_in_memory_multi_sheet_retry_path_equivalent_reopen_modify_noop_save`,
which uses a path-equivalent source output path for the rejected multi-sheet
retry before the same safe-retry, fresh reopen/edit, and byte-identical no-op
checks.
It also covers
`generated_in_memory_multi_sheet_retry_path_equivalent_reopen_modify_post_noop_third_save`,
which continues that path-equivalent multi-sheet retry/reopen/no-op path with
another `Data` / `Summary` edit, a third-stage output, and a byte-identical
final no-op save.
It now also covers
`generated_in_memory_multi_sheet_retry_reopen_modify_post_noop_third_save`,
which edits `Data` and `Summary` again after that clean no-op save, writes a
third-stage output, and requires the final no-op save to be byte-identical to
that third output.
Public-state coverage also pins the same multi-worksheet saved-session hygiene:
matching reacquires stay clean after save and a later no-op `save_as()` keeps
the output byte-stable.
It also pins path-equivalent source-overwrite rejection before that same
multi-worksheet retry/reopen/no-op/post-noop path: both dirty materialized
handles remain dirty, the source package bytes stay unchanged, and the later
safe retry still drives the existing end-to-end checks.
That multi-worksheet rejected-save path now also keeps its dirty materialized
diagnostics explicit for exact and path-equivalent failures: both sheet names,
the aggregate cell count, and the aggregate memory estimate stay stable.
The single-worksheet public-state path now mirrors that first-save guard too:
exact and path-equivalent source-overwrite failures preserve the dirty `Data`
session and source bytes before the same retry/reopen/no-op/post-noop flow
continues.
That single-worksheet guard now also pins the full dirty materialized
diagnostics for both rejected save attempts: worksheet names, cell count, and
estimated materialized memory stay stable until the later safe retry.
It also pins the failed-save retry output as a fresh `WorkbookEditor` source:
the reopened editor can dirty both materialized sheets again, save, and then
no-op save byte-stably while leaving the original source and retry output
unchanged.
Fresh-reopen second-stage edits now also keep dirty memory diagnostics explicit
for both the single-worksheet and multi-worksheet paths before their next safe
`save_as()`.
The same reopen point is now pinned as clean before any second-stage edit:
zero pending change count, no dirty materialized diagnostics, and no edit
summaries.
Those second-stage dirty edits also keep `pending_change_count()` at zero until
`save_as()` turns the materialized sessions into staged handoffs.
Their `pending_worksheet_edits()` summaries are pinned at the same point: one
dirty `Data` summary for single-sheet reopen edits, or source-order dirty `Data`
and `Untouched` summaries for multi-sheet reopen edits.
After those edits have been saved, the following no-op `save_as()` keeps dirty
summaries empty while preserving the staged handoff count.
Row/column shift reacquire/no-op coverage now also pins the pre-save dirty
diagnostics for those small-file shifts: `Data` is the only dirty materialized
worksheet, cell/memory estimates match the active session, no staged handoff is
counted yet, and the dirty summary carries no replacement flags.
The same pre-save dirty diagnostics now cover the saved-session rejection
branches for option mismatch, missing worksheet queries, invalid reads, invalid
mutations, and invalid shifts before their first materialized handoff.
Failed-save retry branches now share that first-handoff coverage too, including
source overwrite, path-equivalent source overwrite, empty output, missing parent,
non-directory parent, existing-directory output, and after-retry no-op paths.
Invalid-to-valid row/column shift recovery now uses the same pre-save dirty
summary contract after the recovery shift, for both clean-start and already
dirty sessions.
Formula row/column shifts now pin that contract too before saving translated
formulas, covering rich reference-shape movement and delete-side `#REF!`
translation.
Post-noop third-stage edits now mirror that memory diagnostic coverage before
their later save: single-sheet dirtiness reports the `Data` session estimate,
while multi-sheet dirtiness reports the aggregate `Data` plus `Untouched`
estimate.
Those same post-noop edits now also pin their pending worksheet summaries:
single-sheet exposes one dirty materialized `Data` summary, and multi-sheet
exposes source-order dirty `Data` plus `Untouched` summaries with per-sheet
cell and memory diagnostics.
They also keep `pending_change_count()` stable before the third-stage save:
dirty materialized sessions do not become counted handoffs until the following
`save_as()` flushes them.
That same multi-worksheet public-state path now also mutates both clean sheets
again after the no-op save, writes a third output without changing the retry,
second-stage, or prior no-op outputs, and verifies a third no-op save remains
byte-equivalent to that third output.
It also pins the matching failed-save retry path: a rejected source-overwrite
save keeps both dirty materialized worksheets and source bytes intact, then a
safe retry plus final no-op save remain stable.
That same default coverage now explicitly separates retained staged Patch
handoffs from dirty materialized sessions: after successful retry/no-op saves,
`has_pending_changes()` can remain true while materialized names, counts, memory,
and dirty summaries are clean.
The public Patch facade now also has large-worksheet targeted cell paths:
`WorkbookEditor::replace_cells(sheet, span<WorksheetCellUpdate>)` replaces only
existing cells by default, while
`WorkbookEditor::replace_cells(..., CellPatchMissingCellPolicy::Insert)` performs
a bounded point upsert that can insert missing cells into existing rows or
synthesize minimal missing rows. Both strategies write caller `CellValue`
payloads through the internal worksheet transformer and stage rewritten
worksheet XML as file-backed package-entry chunks. `WorkbookEditor` also has a
public `request_full_calculation()` helper that queues workbook
`fullCalcOnLoad="1"` / stale `calcChain.xml` cleanup without exposing the
internal calc-chain policy surface. These are the recommended public paths when
a bounded set of cells must be edited in a large worksheet and whole-`<sheetData>`
replacement or `WorksheetEditor` materialization is the wrong design. The
boundary remains intentionally strict for these targeted Patch paths:
duplicate inputs are later-wins, text is emitted as inline strings, caller
`StyleId` values are written as-is without validation, formula payloads
request recalculation, and these APIs do not migrate sharedStrings/styles,
resize or recalculate tables/filters/drawings/defined names, prune/repair
relationships, or provide arbitrary indexed random editing. Row/column shift
semantics belong to the small-file materialized `WorksheetEditor` lane, not
these large-worksheet targeted Patch calls. The current public-state regression
coverage now also pins that small-file lane's row/column shift state hygiene:
zero-count shifts, nonzero no-op insert/delete ranges outside represented
sparse cells, validation failures, memory-budget guard failures, and shift
overflow failures clear or preserve diagnostics as specified, do not dirty
clean sessions, and keep no-op `save_as()` output copy-original. The
retry/reacquire shard now also covers insert/delete row and column shifts after
a failed source-overwrite `save_as()`, safe retry, and matching post-save
`worksheet()` reacquire, including moved source-backed cells, formula text
translation, row/column out-of-bounds `#REF!` translation, dirty borrowed
handles, restored-name diagnostics, insert/delete row/column owning snapshot
views, sparse used-range refresh, source-backed styled formula `StyleId`
preservation, saved XML projection, and saved-file reopen readback for the
plain/styled insert-shift, delete-shift sparse snapshot state, and row/column
out-of-bounds `#REF!` formula translations. The same small-file lane now also
has generated QA no-op variants for the moving formula row/column shifts:
`generated_in_memory_insert_formula_noop_save`,
`generated_in_memory_delete_column_formula_noop_save`,
`generated_in_memory_insert_column_formula_noop_save`, and
`generated_in_memory_delete_row_formula_noop_save` all require the follow-up
clean `save_as()` output to be byte-identical after the shifted formula output
is flushed. It also rewrites stationary materialized formula cells when an
insert/delete row or
column operation affects only their referenced coordinates: formula-only
changes dirty the session, flush through `save_as()`, preserve untouched
worksheets, and reopen cleanly without extending into non-materialized
worksheet scans or metadata repair. A follow-up clean no-op save now snapshots
catalog/save-state diagnostics, keeps summaries empty, emits a byte-equivalent
package, and reopens with the same rewritten formula. A rejected exact-source
`save_as()` before that first flush now also preserves the dirty formula-only
session and source package bytes until a later safe save. Public-state coverage
now also snapshots stationary formula-only rewrites through `row_cells()` and
`column_cells()` before save, after the rejected source-overwrite save, on the
saved live handle, and after fresh reopen/no-op reopen. It
also snapshots the baseline materialized-only formula saved-reopen audit output
through `row_cells()` and `column_cells()`, including source row/column records
and the saved `C2` formula on both the reopened output and clean no-op output.
The unsaved dirty materialized-only formula audit path now uses the same
row/column snapshot helper before save, proving audit reads plus snapshot reads
leave the dirty formula session and materialized diagnostics intact.
The same baseline formula lane now also snapshots the same-editor saved-audit
live handle and no-op output reopen, proving audit reads do not dirty the saved
materialized session before the clean no-op save.
That same live-handle snapshot now also pins editor-level saved-state hygiene:
the materialized handoff count remains stable while dirty materialized
diagnostics and dirty summaries stay empty.
The failed-save baseline no-op output now reuses the same row/column snapshot
helper after fresh reopen, pinning source rows/columns and the saved `C2`
formula after the rejected exact-source save is safely retried.
The safe-retry output itself now has the same fresh-reopen row/column snapshot
coverage before the no-op branch, so the first successful package after
failure also proves the source-backed cells and saved formula shape.
It
also carries those row/column snapshots into the stationary saved-reopen audit
outputs, including cell-reference, `#REF!`, range, whole-row, and whole-column
variants plus their no-op output reopen checks. It
also snapshots the adjacent delete-row/delete-column `#REF!` saved-reopen audit
outputs through `row_cells()` and `column_cells()`, including shifted source
cells, skipped empty rows/columns, and their no-op output reopen checks. It
also pins supported cell-range and whole-axis stationary formulas such as
`SUM(A3:B3)+3:3` and `SUM(D1:E1)+D:E`, and verifies
`formula_reference_audits()` observes the rewritten `Data!A4` plus stable
`Data!B1` references without changing dirty diagnostics. Delete-side stationary
formula audits now also skip `Data!#REF!` while keeping the surviving `Data!B1`
reference visible. Stationary formulas with absolute and mixed `$` markers are
now covered on insert/delete rows and columns as lexical structural rewrites:
affected `$A$3` / `C$3` / `$D1` references move while preserving markers, and
deleted absolute references become `#REF!` in saved/reopened output. The
opt-in generated workbook-editor QA runner now mirrors that lane with
`generated_in_memory_stationary_formula_shift`, saving and reopening a
combined row/column insert/delete workbook through ZIP/XML, `openpyxl`,
optional XlsxWriter, and optional Excel COM checks. It also covers
`generated_in_memory_stationary_range_formula_shift`, which saves and reopens
cell-range, whole-row, and whole-column stationary rewrites such as
`SUM(A4:B4)+4:4+SUM(E1:F1)+E:F`. The generated stationary lane now also has
`generated_in_memory_stationary_formula_shift_noop_save` and
`generated_in_memory_stationary_range_formula_shift_noop_save`, requiring the
follow-up clean `save_as()` output to be byte-identical after those structural
formula rewrites. The same stationary generated QA lane now has full-calculation
variants for both cell-reference and range/whole-axis rewrites, requiring
`fullCalcOnLoad="1"`, no invented `xl/calcChain.xml`, `openpyxl` readback, and
byte-stable no-op companions. Generated append-row and source-backed overwrite
formula writes now have the same full-calculation QA lane through
`generated_in_memory_full_calc_append_row_formula` and
`generated_in_memory_full_calc_overwrite_formula_text`, with no-op companions
requiring the same metadata, readback, and byte-stability checks. Failed-save
retry QA now carries that full-calculation lane through exact-source and
path-equivalent source overwrite rejection with
`generated_in_memory_full_calc_retry_noop_save` and
`generated_in_memory_full_calc_retry_path_equivalent_noop_save`, proving the
source workbook stays unchanged before the safe retry and byte-stable no-op
output. The retry-reopen lane now also has full-calculation variants through
`generated_in_memory_full_calc_retry_reopen_modify_noop_save` and
`generated_in_memory_full_calc_retry_path_equivalent_reopen_modify_noop_save`,
covering rejected source saves, safe retry, fresh reopen, second-stage formula
edits, final `fullCalcOnLoad`, and byte-stable no-op output. The same
full-calculation retry-reopen lane now also has post-noop third-stage variants through
`generated_in_memory_full_calc_retry_reopen_modify_post_noop_third_save` and
`generated_in_memory_full_calc_retry_path_equivalent_reopen_modify_post_noop_third_save`,
covering another edit after the clean no-op save plus final byte-stable output.
Multi-sheet retry-reopen/no-op generated QA now has matching full-calculation
variants through `generated_in_memory_full_calc_multi_sheet_retry_reopen_modify_noop_save`
and
`generated_in_memory_full_calc_multi_sheet_retry_path_equivalent_reopen_modify_noop_save`,
covering `Data` / `Summary` edits, preserved `Notes`, final `fullCalcOnLoad`,
and byte-stable no-op output. The same multi-sheet full-calculation lane now
also has post-noop third-stage variants through
`generated_in_memory_full_calc_multi_sheet_retry_reopen_modify_post_noop_third_save`
and
`generated_in_memory_full_calc_multi_sheet_retry_path_equivalent_reopen_modify_post_noop_third_save`,
covering another `Data` / `Summary` edit after the clean no-op save plus final
byte-stable output. The generated QA runner now also centralizes case-directory
path budgeting: explicit aliases remain for known long scenarios, short
scenario names still map directly, and future over-budget generated scenario
names receive deterministic shortened directories with SHA-1 suffixes so long
Windows work directories do not block retry/reopen/no-op QA runs.
That same full-calculation multi-sheet post-noop lane now also has final
fresh-reopen variants through
`generated_in_memory_full_calc_multi_sheet_retry_reopen_modify_post_noop_reopen_modify_save`
and
`generated_in_memory_full_calc_multi_sheet_retry_path_equivalent_reopen_modify_post_noop_reopen_modify_save`:
after proving the post-noop no-op output is byte-stable, the QA tool opens that
file as a fresh `WorkbookEditor` source, edits `Data!F1` and `Summary!E1`, and
saves a final readable output while keeping the scope limited to generated
in-memory QA.
The same lane also has final clean no-op variants through
`generated_in_memory_full_calc_multi_sheet_retry_reopen_modify_post_noop_reopen_modify_noop_save`
and
`generated_in_memory_full_calc_multi_sheet_retry_path_equivalent_reopen_modify_post_noop_reopen_modify_noop_save`,
requiring the final fresh-reopen edited workbook to no-op save byte-identically
after the `Data!F1` / `Summary!E1` save.
The final no-op output is now also reused as another fresh editor source through
`generated_in_memory_full_calc_multi_sheet_retry_reopen_modify_post_noop_reopen_modify_noop_reopen_modify_save`
and
`generated_in_memory_full_calc_multi_sheet_retry_path_equivalent_reopen_modify_post_noop_reopen_modify_noop_reopen_modify_save`,
which edit `Data!G1` and `Summary!F1` after opening that final no-op workbook.
That later fresh-reopen edit output also has clean no-op variants through
`generated_in_memory_full_calc_multi_sheet_retry_reopen_modify_post_noop_reopen_modify_noop_reopen_modify_noop_save`
and
`generated_in_memory_full_calc_multi_sheet_retry_path_equivalent_reopen_modify_post_noop_reopen_modify_noop_reopen_modify_noop_save`,
requiring the `Data!G1` / `Summary!F1` workbook to save again byte-identically.
That no-op output is also reused as one more fresh editor source through
`generated_in_memory_full_calc_multi_sheet_retry_reopen_modify_post_noop_reopen_modify_noop_reopen_modify_noop_reopen_modify_save`
and
`generated_in_memory_full_calc_multi_sheet_retry_path_equivalent_reopen_modify_post_noop_reopen_modify_noop_reopen_modify_noop_reopen_modify_save`,
which edit `Data!H1` and `Summary!G1` after opening the final no-op workbook.
That final edit output now also has clean no-op variants through
`generated_in_memory_full_calc_multi_sheet_retry_reopen_modify_post_noop_reopen_modify_noop_reopen_modify_noop_reopen_modify_noop_save`
and
`generated_in_memory_full_calc_multi_sheet_retry_path_equivalent_reopen_modify_post_noop_reopen_modify_noop_reopen_modify_noop_reopen_modify_noop_save`,
requiring the `Data!H1` / `Summary!G1` workbook to save again byte-identically.
The
source-audit path now keeps scanning original source XML
for `Data!A3` / `Data!B1` while the dirty materialized formula has already
rewritten to `Data!A4+Data!B1`; the delete-side source scan keeps that same
boundary when the materialized formula is `Data!#REF!+Data!B1`. Fresh reopen
coverage now verifies saved-output source/materialized audits see the persisted
`Data!A4` / `Data!B1` references, not the original `Data!A3` source token, and
the delete-side saved-output path keeps only the surviving `Data!B1` reference
while skipping persisted `Data!#REF!`; the column-side saved-output path now
mirrors that boundary for `Data!E1` / `Data!B1` after insert and surviving
`Data!B1` after persisted `Data!#REF!` delete-column output. Range saved-output
coverage also verifies persisted `Data!A4:B4` and `Data!4:4` audit tokens
replace the original `Data!A3:B3` / `Data!3:3` source tokens, and column-range
coverage mirrors that boundary for persisted `Data!E1:F1` / `Data!E:F` replacing
the original `Data!D1:E1` / `Data!D:E` tokens. Delete-row `#REF!` saved-output
coverage now also skips persisted `Data!#REF!` tokens while keeping surviving
`Data!A:A` and shifted `Data!B3` visible; delete-column `#REF!` saved-output
coverage mirrors that boundary for surviving `Data!1:1` and shifted `Data!C2`.
Range dirty-source coverage also keeps `source_formula_reference_audits()` on
the original `Data!A3:B3` / `Data!3:3` and `Data!D1:E1` / `Data!D:E` tokens
while the materialized formulas have already shifted to `Data!A4:B4` /
`Data!4:4` and `Data!E1:F1` / `Data!E:F`. Delete-side dirty-source coverage
now also keeps delete-row/delete-column `#REF!` rewrites on original source
formula coordinates and tokens, rather than the materialized `Data!#REF!` /
`Data!B3` / `Data!C2` tokens. Dirty-only formulas inserted through
`WorksheetEditor::set_cell()` stay out of `source_formula_reference_audits()`
until saved, while `formula_reference_audits()` still reports those in-memory
formula references from the materialized sparse store; after `save_as()` and a
fresh reopen, both source and materialized audits report the saved formula tokens
from the output workbook while keeping the reopened editor clean. Rejected
exact-source saves now preserve that same materialized-only audit split and
leave source package bytes unchanged until a later safe retry writes the formula
into the output workbook. A follow-up no-op save after that safe retry now keeps
public save-state and catalog diagnostics stable, keeps dirty materialized
diagnostics, replacement diagnostics, dirty summaries, and `last_edit_error()`
clean, emits byte-equivalent package entries, and fresh-reopens with the
persisted formula references. Same-editor post-save audits now make the routing
explicit: source audits keep scanning the original source XML, while
materialized audits report the clean saved formula without re-dirtying the
session.
The retry/guard shard also pins that
path-equivalent source-overwrite failures follow the same safe-retry/no-op-save
boundary: after safe retry, matching `worksheet("Data")` reacquire stays clean,
pending materialized diagnostics remain empty, and a later no-op `save_as()`
reuses the retry output byte-for-byte without adding a materialized handoff.
Empty-output-path save failures now follow that same boundary: after safe retry,
matching reacquire stays clean, dirty diagnostics remain empty, and no-op
`save_as()` reuses the retry output byte-for-byte.
Missing-parent, non-directory-parent, and existing-directory output failures now
follow that same boundary too: after safe retry, matching reacquire stays
clean, dirty diagnostics remain empty, and no-op `save_as()` reuses the retry
output byte-for-byte.
The retry/guard shard also pins that
shift no-ops clear stale diagnostics without dirtying reacquired sessions, while
invalid shift ranges preserve the saved sparse store until a later valid shift
clears the diagnostic and flushes; it also reopens saved guard-recovery outputs
for handle reads, invalid reads/mutations, missing erase no-ops, and the later
valid shift to verify clean public state and source-backed cell readback.
The later valid shift guard recovery now also has a clean no-op `save_as()`
proof: the no-op package matches the shifted second output, source bytes remain
unchanged, public save/catalog snapshots plus diagnostics stay stable, and a
fresh reopen preserves shifted `A3` while leaving old `A2` absent.
The retry/projection shard also reopens saved blank/erase, scalar/formula, and
text-escape projection outputs to verify clean public state and value-kind
readback after the saved XML projection. Those reopened-output checks now share
the complete clean diagnostics gate: no dirty materialized sessions, no
replacement diagnostics, and no `last_edit_error`. Each path now also repeats a
clean no-op `save_as()`, confirms byte-stable output and unchanged source
package bytes, and reopens that no-op output. A1 overload edit coverage now also
reopens the second no-op output, verifying repeated byte-stable no-op saves
remain readable with source-backed `A1` / `B1`, erased `A2`, and inserted `D4`.
The public shard helper now mirrors the same targeted-cell Patch diagnostics in
save-state snapshots, clean replacement checks, and read-only inspection
last-error preservation.
The public retry shared helper now carries that same targeted-cell diagnostics
coverage across retry, reacquire, guard, and projection shards.
The facade shared helper now mirrors those targeted-cell diagnostics across the
core, save-as, rename, images, and smoke facade shards.
The source-success, source-failure, and formula-rewrite helpers now carry the
same targeted-cell diagnostics in their save-state and inspection checks.
The projection recovery saves now also assert that replacement diagnostics stay
empty and `last_edit_error()` remains clear immediately after the second safe
save, before the existing XML and fresh-reopen checks.
Explicit blank coverage now mirrors that second-no-op readback shape for
source-backed `A1` / `B1` / `A2`, explicit blank `D4`, and missing `E5`.
A1 overload, explicit blank, and A1-range mutation coverage now also re-read
the first no-op package after the second clean no-op save, proving repeated
clean saves do not rewrite already-emitted read/query outputs.
Single-cell erase coverage now also reopens its second no-op output, verifying
the shrunk `A1:B1` projection, source-backed `A1` / `B1`, and erased `A2`
absence.
The same-handle materialized save path now also reopens the first and second
outputs, verifying the borrowed handle remains reusable while earlier output
artifacts stay clean and isolated from later edits. The second-save no-op output
is now reopened as well, proving the byte-stable no-op package remains readable
as a clean `WorkbookEditor` materialized session with the saved `A1` / `B1`
edits and source-backed `A2`.
The range-erase reacquire path also reopens both saved outputs, pinning the
empty first projection and the later single-cell C3 projection after handle
reacquisition.
Its first-flush second no-op output now also fresh-reopens as an empty sparse
worksheet with no used range and erased A1/B1/A2 absent before reacquisition.
Its post-reacquire no-op output now also fresh-reopens with the same single C3
text cell, C3:C3 bounds, and erased A1/B1/A2 absence as the second save.
The invalid row/column overload recovery path now also fresh-reopens its second
no-op output, verifying the recovered A1 edit, source-backed B1/A2 cells,
`A1:B2` bounds, and missing rejected C1 coordinate remain readable.
The initializer-list batch overload path is also reopened to verify explicit
blank, source-backed, inserted boolean, erased, and missing-only targets survive
the saved sparse projection.
The A1 range clear/erase sparse mutation path is also reopened to pin explicit
blank projection, erased source cells, missing-only no-ops, and outside-range
text after save.
Its second no-op output now also fresh-reopens with the same sparse count,
bounds, blanked B1/C3 records, erased A1/A2 absence, missing B2, and outside D4
text.
The sparse snapshot dirty projection path is also reopened to verify owning
snapshots do not block later edits and saved output rehydrates the edited,
source-backed, blank, inserted, and erased sparse cells cleanly.
Its second no-op output also fresh-reopens with the same sparse count, bounds,
post-snapshot A1 edit, source-backed B1, explicit B3 blank, inserted D4, and
erased A2 absence.
It now also rechecks the first no-op package after the second clean no-op save,
pinning already-emitted full sparse snapshot outputs as immutable artifacts.
It now also calls full `sparse_cells()` directly on the same clean saved handle
after the initial save and both clean no-op saves, pinning row-major full sparse
snapshots while proving those reads do not re-dirty the materialized session.
The bounded `sparse_cells(range)` snapshot path is also reopened to verify
range-limited owning snapshots do not block later edits, outside-range cells
survive, and erased source cells stay absent after save.
Its second no-op output now also fresh-reopens with the same sparse count,
bounds, source-backed A1, post-snapshot B1 edit, explicit B3 blank, in-range C3,
outside-range D4, and erased A2 absence.
It now also rechecks the first no-op package after that second clean no-op save,
pinning already-emitted bounded sparse snapshot outputs as immutable artifacts.
It now also reruns the same `CellRange` snapshot against the clean saved session
and fresh-reopened outputs, pinning row-major in-range snapshots across no-op
saves without including outside-range cells.
The A1-string `sparse_cells("B1:C3")` path is reopened with the same dirty
projection checks, pinning string-range parsing and saved sparse readback
without extending the parser boundary.
Its second no-op output now also fresh-reopens with the same sparse count,
bounds, source-backed A1, post-snapshot B1 edit, explicit B3 blank, in-range C3,
outside-range D4, and erased A2 absence.
It now mirrors the clean saved-session and fresh-reopen range snapshot checks
through the string A1 overload, preserving the same row-major in-range order
across no-op saves.
It now also rechecks the first no-op package after the second clean no-op save,
pinning already-emitted A1-string range snapshot outputs as immutable artifacts.
The coordinate-batch `sparse_cells(span<WorksheetCellReference>)` snapshot path
is also reopened to verify duplicates, skipped missing cells, later edits, and
erased source cells survive saved sparse projection cleanly.
Its second no-op output now also fresh-reopens with the same sparse count,
`A1:D4` bounds, post-snapshot A1 edit, source-backed B1, explicit B3 blank,
inserted D4, and erased A2 absence.
It now also reuses the original coordinate batch against the clean saved session
and fresh-reopened outputs, preserving input order and duplicates while skipping
the erased coordinate across no-op saves.
It now also rechecks the first no-op package after the second clean no-op save,
pinning already-emitted coordinate-batch snapshot outputs as immutable artifacts.
The `used_range()` dirty/empty inspection path is reopened as well, pinning
clean readback for a saved worksheet whose sparse cells were fully erased after
diagnostic-preserving reads.
Its second no-op output now also fresh-reopens as an empty sparse worksheet with
no used range and erased A1/B1/A2 absent.
It now also calls `used_range()` directly on the same clean saved handle after
the initial save and both clean no-op saves, preserving the prior diagnostic and
proving those bounds reads do not re-dirty the materialized session.
It now also rechecks the first no-op package after the second clean no-op save,
pinning already-emitted empty-projection `used_range()` outputs as immutable
artifacts.
The `row_cells()` / `column_cells()` snapshot path is reopened to verify saved
row/column ordering, explicit blank cells, source-backed records, later edits,
and outside-coordinate sparse cells.
Its second no-op output now also fresh-reopens with the same row/column
ordering, source cells, explicit blank, later edits, bounds, and outside D4.
It now also reads `row_cells(1)` and `column_cells(1)` directly from the clean
saved materialized handle after the initial save and both clean no-op saves,
pinning row/column ordering while proving those snapshot reads do not re-dirty
the saved session.
It now also rechecks the first no-op package after the second clean no-op save,
pinning already-emitted row/column snapshot outputs as immutable artifacts.
The `contains_cell()` inspection path now also saves and reopens the dirty
projection, verifying represented blank / inserted / source-backed cells and
erased source cells after diagnostic-preserving invalid reads.
Its second no-op output now also fresh-reopens with the same represented cells,
source values, explicit blank, inserted text, bounds, and erased A2 absence.
It now also calls `contains_cell()` directly on the same clean saved handle
after the initial save and both clean no-op saves, preserving the prior
diagnostic while proving those reads do not re-dirty the materialized session.
It now also rechecks the first no-op package after the second clean no-op save,
pinning already-emitted `contains_cell()` projection outputs as immutable
artifacts.
The handle-level read-only inspection coverage now also snapshots source names,
planned names, and the full worksheet catalog around `used_range()`,
`contains_cell()`, `row_cells()`, `column_cells()`, and invalid
`sparse_cells()` range reads after a prior mutation diagnostic, pinning that
these inspections do not mutate workbook catalog views.
The base cell-read invalid paths now use the same catalog snapshot for A1 and
row/column `try_cell()` / `get_cell()` failures, including the helper that runs
after a prior mutation diagnostic.
They now also re-run direct saved-session `try_cell()` / `get_cell()` reads
after the copy-original save and repeated clean no-op save, preserving the prior
diagnostic while proving valid source-backed reads and missing-cell `try_cell()`
do not re-dirty the materialized worksheet.
The non-renamed shift reacquire failure paths now mirror the renamed-session
coverage by preserving source names, planned names, and worksheet catalog
entries across option mismatch, missing-query, invalid-read, and
invalid-mutation checks after the first saved row/column shift.
Row/column shift guard coverage now also preserves workbook catalog views
around zero-count shifts, nonzero no-op shifts outside represented sparse
records, validation failures, and row/column overflow failures.
Non-renamed delete-row/delete-column styled formula coverage now mirrors the
renamed-session source-backed path: shifted formula cells keep the source
`StyleId`, deleted references become `#REF!`, surviving references move, and
saved/reopened workbooks preserve the same sparse cells without metadata repair.
Operation-mixing guard coverage now also includes same-sheet targeted
`replace_cells()` after dirty, read-only, and saved-clean materialized
`WorksheetEditor` sessions. The failure replaces `last_edit_error()` with the
public targeted-cell wrapper diagnostic, leaves targeted patch diagnostics empty,
preserves borrowed handles/catalog state, and does not leak rejected payloads
into later `save_as()` output.
The reverse targeted Patch to in-memory guard is now tightened as well:
after `replace_cells()` queues a same-sheet targeted edit, both `worksheet()`
and `try_worksheet()` reject materialization without updating
`last_edit_error()`, while targeted cell counts, worksheet names, XML byte
estimates, and pending public edit count remain intact.
The same reverse guard is now pinned for queued whole-`<sheetData>` replacement:
after `replace_sheet_data()` queues a replacement, both `try_worksheet()` and
`worksheet()` reject same-sheet materialization without updating
`last_edit_error()`, preserve replacement diagnostics and pending edit count,
avoid materialized diagnostics, and still save the queued replacement.
The materialized-to-Patch catalog guard now has public coverage too: after a
dirty `WorksheetEditor` session exists, rejected same-sheet `rename_sheet()`
and `replace_sheet_data()` calls preserve the source/planned catalog, leave
replacement diagnostics empty, keep the dirty materialized summary, and a later
safe `save_as()` writes only the materialized cells without leaking the rejected
rename or replacement payload.
That catalog guard matrix now also covers clean materialized sessions:
read-only materialization stays a no-op copy-original save after rejected
same-sheet rename/replacement, while a post-save clean materialized session
keeps its prior projection and can save again without adding a handoff or
leaking the rejected catalog name / replacement payload.
The same catalog guard now has full-calculation metadata coverage: after
`request_full_calculation()` is queued beside dirty or clean materialized
sessions, same-sheet `rename_sheet()` and `replace_sheet_data()` still fail
before catalog or replacement state changes, preserve the queued metadata edit,
and a later safe save writes `fullCalcOnLoad="1"` without leaking rejected
catalog names or replacement payloads.
The workbook full-calculation metadata helper is now pinned against dirty
materialized sessions too: calling `request_full_calculation()` after a
`WorksheetEditor` mutation queues only the workbook metadata edit, preserves the
dirty materialized names/counts/memory until `save_as()`, and the save output
contains both `fullCalcOnLoad="1"` and the materialized sparse projection
without inventing `xl/calcChain.xml`.
The same helper now has clean materialized-session coverage: read-only
materialization followed by `request_full_calculation()` queues the workbook
metadata request, leaves materialized diagnostics and worksheet edit summaries
empty, and saves with the source worksheet bytes preserved while still writing
`fullCalcOnLoad="1"`.
Post-save clean materialized sessions now have the same ordering guard: after a
dirty materialized projection has already been flushed by `save_as()`, a later
`request_full_calculation()` adds only the workbook metadata request; the next
save reuses the prior sparse projection, keeps dirty materialized diagnostics
empty, and does not add another materialized handoff.
The reverse ordering is covered as well: a workbook-level
`request_full_calculation()` queued before `worksheet()` does not block later
materialization or dirty sparse edits, and `save_as()` writes both the existing
workbook metadata request and the later materialized projection.
The clean side of that reverse ordering is pinned too: if `worksheet()` only
reads after a prior `request_full_calculation()`, the clean materialized session
does not contribute dirty diagnostics, summaries, or a materialized handoff, and
the saved worksheet bytes stay source-identical while workbook calc metadata is
rewritten.
The same reverse ordering now covers Patch-to-in-memory guards: if
`request_full_calculation()` is queued before a same-sheet
`replace_sheet_data()` or targeted `replace_cells()`, later `worksheet()` /
`try_worksheet()` calls still reject materialization without updating
`last_edit_error()`, preserve the queued Patch diagnostics and public edit
counts, avoid dirty materialized diagnostics, and `save_as()` still writes both
`fullCalcOnLoad="1"` and the queued Patch payload.
That reverse ordering now covers catalog rename plus materialization as well:
after `request_full_calculation()` and then `rename_sheet()`, `worksheet()` uses
the current planned sheet name, read-only materialization adds no dirty
diagnostics or handoff, and a later dirty `WorksheetEditor` edit saves alongside
the workbook `fullCalcOnLoad="1"` request and renamed catalog entry without
creating `xl/calcChain.xml`. The clean branch also proves the worksheet part
stays byte-for-byte source-identical when no sparse edit is made.
The already-renamed ordering is pinned too: after `rename_sheet()` succeeds, a
later `request_full_calculation()` preserves the planned catalog name for clean
and dirty `WorksheetEditor` materialization. Dirty sparse edits save with the
renamed workbook catalog and `fullCalcOnLoad="1"` without inventing
`xl/calcChain.xml`, while clean materialization keeps the worksheet part
byte-for-byte source-identical and adds no materialized handoff.
That renamed/full-calc ordering now has same-sheet catalog guard coverage:
after a planned rename and full-calculation request, dirty and clean
materialized sessions reject later same-sheet `rename_sheet()` /
`replace_sheet_data()` attempts under the current planned name, preserve the
rename plus calc metadata edit count, and save without leaking rejected catalog
names or replacement payloads.
The targeted Patch guard now follows that renamed/full-calc ordering as well:
after clean or dirty materialization under a planned sheet name, same-sheet
`replace_cells()` is rejected with the public targeted-cell wrapper diagnostic,
leaves targeted diagnostics empty, preserves the rename plus calc metadata
state, and later saves without leaking the rejected targeted payload.
The full-calculation helper is now pinned against an actual row-shift projection
too: after a dirty `WorksheetEditor::insert_rows()` sparse shift, queued
`request_full_calculation()` preserves materialized names/counts/memory until
`save_as()`, and the saved package carries `fullCalcOnLoad="1"` alongside the
shifted styled formula without creating `xl/calcChain.xml`.
That same after-shift insert-row/full-calculation path now covers rejected
exact source overwrite: the failed save preserves dirty diagnostics, the
translated `D4` formula/style, shifted source and dirty trailing rows, queued
workbook metadata, and source bytes before a safe retry writes
`fullCalcOnLoad="1"` with no `xl/calcChain.xml`.
The same retry path now immediately proves clean no-op save stability after the
safe retry: the no-op output is byte-identical, public save/catalog state is
preserved, no extra materialized handoff is recorded, and diagnostics stay
clean.
The row-shift reverse ordering is covered as well: a queued
`request_full_calculation()` before materializing `Data` still allows a later
styled `insert_rows()` shift, writes the translated `D4` formula with
`fullCalcOnLoad="1"`, keeps clean materialization diagnostics empty, and
does not create `xl/calcChain.xml`.
That reverse insert-row styled source formula path now also has rejected source
overwrite coverage: the failed save preserves the queued full-calculation
metadata, dirty materialized diagnostics, translated `D4` formula/style,
shifted in-memory source rows, and source package bytes before a safe retry
writes the same `fullCalcOnLoad="1"` / no-`calcChain.xml` output.
The reverse ordering is now covered for column shifts: a queued
`request_full_calculation()` before `worksheet()` still allows a later dirty
`WorksheetEditor::insert_columns()` sparse shift, and `save_as()` writes the
workbook calc metadata plus shifted numeric/formula/text cells while omitting
old coordinates and `xl/calcChain.xml`.
That column-shift reverse ordering now also covers the styled source formula
path: queued `request_full_calculation()` before materialization survives
`insert_columns(2, 2)`, preserves the shifted formula style at `F2`, translates
the formula to `C1+D1`, and saves with no `xl/calcChain.xml`.
That same reverse styled insert-column path now also covers rejected exact
source overwrite: the failed save preserves the queued full-calculation
metadata, dirty materialized diagnostics, translated `F2` formula/style,
shifted in-memory source cells, and source package bytes; a later safe retry
writes the shifted output with `fullCalcOnLoad="1"` and no `xl/calcChain.xml`.
The after-shift insert-column failed-save retry now also has the same clean
no-op save proof after the safe retry: no new handoff is recorded, diagnostics
stay clean, and the no-op output is byte-identical to the retry output.
The delete-side row-shift branch now has the same full-calculation guard:
after dirty `WorksheetEditor::delete_rows()` shifts source-backed cells and a
styled formula into `#REF!` references, queued `request_full_calculation()`
preserves dirty diagnostics until `save_as()`, writes `fullCalcOnLoad="1"`,
and still does not create `xl/calcChain.xml`.
That same after-shift delete-row/full-calculation path now covers rejected
exact source overwrite: the failed save preserves dirty diagnostics, the
translated `D1` formula/style, shifted source rows, queued workbook metadata,
and source bytes before a safe retry writes `fullCalcOnLoad="1"` with no
`xl/calcChain.xml`.
The after-shift delete-row retry path now also proves clean no-op save
stability after the safe retry, preserving public save/catalog state and
byte-identical output without adding another materialized handoff.
The delete-side row-shift reverse ordering now mirrors that path:
`request_full_calculation()` can be queued before materializing `Data`, and a
later dirty `WorksheetEditor::delete_rows()` still writes the styled `D1`
formula as `#REF!+#REF!` with `fullCalcOnLoad="1"` and no `xl/calcChain.xml`.
That reverse delete-row styled source formula path now also has rejected source
overwrite coverage: the failed save preserves the queued full-calculation
metadata, dirty materialized diagnostics, translated `D1` formula/style,
shifted in-memory source rows, and source package bytes before a safe retry
writes the same `fullCalcOnLoad="1"` / no-`calcChain.xml` output.
The delete-side column-shift reverse ordering is covered too: a queued
`request_full_calculation()` before materialization survives later dirty
`WorksheetEditor::delete_columns()` shifts, including a styled formula whose
deleted-column reference is serialized as `#REF!`, and `save_as()` keeps the
full-calc metadata without creating `xl/calcChain.xml`.
That reverse delete-column styled source formula path now also has rejected
source overwrite coverage: the failed save preserves the queued
full-calculation metadata, dirty materialized diagnostics, translated `C2`
formula/style, shifted in-memory source columns, and source package bytes
before a safe retry writes the same `fullCalcOnLoad="1"` / no-`calcChain.xml`
output.
The before-shift full-calculation failed-save retry paths now also share the
clean no-op save stability proof after their safe retries: reverse styled
`insert_rows()`, `insert_columns()`, `delete_rows()`, and `delete_columns()`
keep public save/catalog state stable, leave dirty and replacement diagnostics
empty, avoid extra handoffs, and emit byte-identical no-op outputs.
Those before-shift no-op outputs are now fresh-reopened as well, proving the
byte-stable packages remain readable with the shifted sparse counts/ranges,
source cells, removed old coordinates, and moved formula text/style intact.
Those before-shift readbacks now also open `Untouched` through a fresh editor,
pinning the same source-backed `keep-me` / `99.0` companion-sheet preservation
contract after `Data`-only shifts.
The before-shift failed-save/no-op paths now also reopen the original source
package after the retry sequence, matching the after-shift source preservation
evidence for the original `D2` styled formula and untouched companion sheet.
The row/column shift and full-calculation shift readback matrix now runs under a
separate `fastxlsx.workbook_editor.public-state-shifts` CTest shard, preserving
coverage while giving the base public-state shard more 60-second timeout margin.
The mutation-heavy write/clear/erase/guardrail half of the baseline
public-state checks now runs under
`fastxlsx.workbook_editor.public-state-edits`, keeping the default CTest
coverage intact while moving the general read/state snapshot shard farther from
the 60-second timeout boundary.
Direct row/column shift tests now also pin pre-save dirty summary diagnostics:
`insert_rows()`, `delete_rows()`, `insert_columns()`, and `delete_columns()`
verify `pending_worksheet_edits()` reports one dirty `Data` materialized summary
with no replacement state and memory/count values matching the active
`WorksheetEditor` before the flush.
The same shift shard now continues the basic `delete_rows()` sparse-shift path
after its first clean no-op save: it writes `D3`, saves and reopens the expanded
`A1:D3` output, and requires the following clean no-op output to stay
byte-identical.
It now does the same for the basic `insert_columns()` sparse-shift path, writing
`F3` after the first no-op save and requiring the expanded `A1:F3` output plus
the following clean no-op output to remain stable on fresh readback.
The basic `delete_columns()` path now has the matching coverage, writing `D2`
after the first no-op save and requiring the expanded `A1:D2` output plus the
following clean no-op output to remain stable on fresh readback.
The styled `insert_rows()` post-noop path now also matches the rest of the
shift post-noop matrix by checking replacement diagnostics stay empty after
the post-noop save and the final clean no-op save.
The remaining styled source-formula post-noop paths now have the same
diagnostics parity for `insert_columns()`, `delete_rows()`, and
`delete_columns()`, including delete-side aggregate materialized memory checks
while those post-noop edits are dirty.
The cross-handle row/column shift post-noop paths now carry that replacement
diagnostics contract too: row insert, column insert, row delete, and column
delete regressions keep replacement diagnostics empty after the multi-handle
post-noop save and after the final clean no-op save.
Full-calculation row/column shift tests now pin that same summary contract while
workbook metadata is already queued: after-shift and before-shift insert/delete
row/column paths keep the dirty `Data` materialized summary aligned with the
shifted sparse store while `pending_change_count()` still reflects the queued
`fullCalcOnLoad` workbook edit.
Full-calculation failed-save retry row/column shift tests now pin the dirty
summary contract on both sides of the rejected exact source overwrite. The
after-shift and before-shift insert/delete row/column paths keep one dirty
`Data` materialized summary with no replacement state, the shifted sparse count,
and the active `WorksheetEditor` memory estimate while the queued
`fullCalcOnLoad` workbook edit remains pending.
The same retry matrix now also checks successful safe retries clear the dirty
summary immediately: after the retry `save_as(output)`, `pending_worksheet_edits()`
is empty alongside zero materialized aggregate diagnostics before the later clean
no-op save.
It also pins replacement diagnostics at that same safe-retry point: replacement
cell counts, replacement memory estimates, and replacement worksheet names remain
empty before the later clean no-op save.
At the same point, `has_pending_changes()` remains true to document the retained
staged workbook metadata plus materialized handoff even though active
materialized and replacement diagnostics are clean.
The remaining public-state formula-audit and saved-session reacquire matrices
now run under separate `fastxlsx.workbook_editor.public-state-formula-audits`
and `fastxlsx.workbook_editor.public-state-reacquire` CTest shards, preserving
coverage while keeping the base public-state shard within the 60-second timeout
budget.
The guardrail, invalid-shift, cross-handle, budget-release, and last-error
recovery tail of that saved-session matrix now runs under
`fastxlsx.workbook_editor.public-state-reacquire-guards`. This is test
organization only: it preserves `--shard=all` coverage and does not change
`WorksheetEditor` public API behavior, sparse-store semantics, or save_as()
handoff rules.
The after-shift delete-column ordering is covered as well: dirty
`WorksheetEditor::delete_columns()` first moves the styled source-backed formula
to `C2` as `#REF!+A1`, and a later `request_full_calculation()` preserves
materialized diagnostics while `save_as()` writes `fullCalcOnLoad="1"` without
creating `xl/calcChain.xml`.
That same after-shift delete-column/full-calculation path now covers rejected
exact source overwrite: the failed save preserves dirty diagnostics, the
translated `C2` formula/style, shifted source columns, queued workbook metadata,
and source bytes before a safe retry writes `fullCalcOnLoad="1"` with no
`xl/calcChain.xml`.
The reverse-order insert-row/full-calculation failed-save retry path now also
checks fresh-reopened no-op output snapshots: row four exposes shifted source
cells and the styled translated `D4` formula in row-major order, while column
four exposes the same `A3+B3` formula/style.
The matching reverse-order delete-row/full-calculation failed-save retry path
now checks the no-op output snapshots as well: row one exposes shifted source
cells and the styled `D1` `#REF!+#REF!` formula, while column four exposes that
same formula/style after fresh reopen.
The after-shift insert-row/full-calculation failed-save retry path now also
checks no-op fresh-reopen snapshots: row five preserves the shifted source
`A5` and dirty `C5` trailing cells in order, while column four exposes the
styled translated `D4` `A3+B3` formula.
The after-shift delete-row/full-calculation failed-save retry path now mirrors
that snapshot coverage: row one exposes shifted source cells and the styled
`D1` `#REF!+#REF!` formula, while column four exposes that same formula/style
after fresh reopen.
The full-calculation-before delete-column failed-save retry path now also has
matching no-op fresh-reopen snapshot coverage: row two exposes shifted source
cells and the styled `C2` `#REF!+A1` formula, while column three exposes that
same formula/style.
The after-shift delete-column retry path now matches the same no-op save
stability contract after safe retry: clean diagnostics, stable public
save/catalog state, no extra handoff, and byte-identical output.
The after-shift full-calculation failed-save retry no-op outputs are now
fresh-reopened as well: styled insert-row, insert-column, delete-row, and
delete-column paths all read back their shifted sparse state, moved formula
text/style, and removed old coordinates, with the insert-row path also pinning
both shifted trailing cells `A5` and `C5`.
Those same after-shift no-op readbacks now also materialize `Untouched` through
a fresh editor, verifying the companion sheet remains clean and source-backed
with `keep-me` / `99.0` after `Data`-only edits.
The after-shift failed-save/no-op paths now also reopen the original source
package after the retry sequence, proving rejected source-overwrite attempts and
later safe/no-op saves leave the source workbook readable with its original
`D2` styled formula and untouched companion sheet.
Formula audit diagnostics now sit on top of that full-calculation mixing state:
`formula_reference_audits()` remains read-only after a dirty shifted qualified
formula and queued `request_full_calculation()`, reporting the shifted
`Data!A2` / `Data!B2` tokens while preserving materialized diagnostics and later
saving both the shifted formula and `fullCalcOnLoad="1"` without calcChain.
Source formula audit diagnostics now share that full-calculation boundary:
`source_formula_reference_audits()` still scans the original worksheet XML while
a dirty shifted materialized session and queued full-calculation metadata are
pending, reporting source `D2` tokens `Data!A1` / `Data!B1` even though the
materialized save path later writes `D3` as `Data!A2+Data!B2` with
`fullCalcOnLoad="1"` and no calcChain.
DefinedName formula audit diagnostics now cover the same full-calculation
mixing boundary: `defined_name_formula_reference_audits()` remains read-only
while a dirty materialized `WorksheetEditor` shift and `request_full_calculation()`
are pending, reports source workbook direct definedName text such as
`Data!$A$1:$B$2`, and later `save_as()` keeps that definedName unchanged while
writing the shifted worksheet cell plus `fullCalcOnLoad="1"` without calcChain.
The renamed variant is now pinned too: after default `rename_sheet("Data",
"RenamedData")`, queued full calculation, and a dirty materialized shift under
the planned sheet name, the definedName audit maps stale source `Data` references
to planned `RenamedData` for diagnostics but `save_as()` still keeps direct
workbook definedNames unchanged unless the caller opted into the explicit
definedName rewrite policy.
The matching renamed source-formula audit boundary is now pinned as well: after
default `rename_sheet("Data", "RenamedData")`, queued full calculation, and a
dirty materialized shift under the planned sheet name,
`source_formula_reference_audits()` still scans original source worksheet XML,
reports source `D2` tokens `Data!A1` / `Data!B1`, maps source `Data` to planned
`RenamedData` as stale source-name diagnostics, and later `save_as()` writes the
renamed catalog, shifted `D3` formula, and `fullCalcOnLoad="1"` without
creating calcChain.
The materialized formula audit side of that renamed boundary is covered too:
`formula_reference_audits()` reports the shifted `D3` tokens `Data!A2` /
`Data!B2`, preserves the queued rename/full-calc/materialized diagnostics, and
still leaves default rename as audit-only stale-qualifier evidence until an
explicit formula rewrite policy is requested.
Its clean no-op output now also fresh-reopens with the same renamed sheet,
shifted styled `D3` formula audit/readback, `Data!A2` / `Data!B2` references,
`fullCalcOnLoad="1"`, and absent calcChain.
Failed-save retry hygiene now covers that same renamed full-calculation formula
state: an exact source-overwrite `save_as()` rejection preserves the shifted
materialized formula audit, the original source-formula audit, materialized
diagnostics, `last_edit_error()`, and source package bytes; the later safe retry
still writes the renamed catalog, shifted formula, and `fullCalcOnLoad="1"`
without creating calcChain.
Mismatched `WorksheetEditorOptions` access now has the same renamed
full-calculation audit hygiene: rejected matching-name `try_worksheet()` /
`worksheet()` calls preserve the dirty shifted formula, source/materialized
formula audits, catalog state, materialized diagnostics, and later save-as
output, without option migration or session cloning.
Missing worksheet lookups now cover the same state hygiene: empty
`try_worksheet("Missing")` / `try_worksheet("Data")` results and throwing
`worksheet("Missing")` / `worksheet("Data")` failures preserve the renamed
full-calculation dirty formula session, source/materialized audits, catalog
state, materialized diagnostics, and later save-as output, without source-name
fallback or aliasing.
Invalid `WorksheetEditor` reads now have the same renamed full-calculation audit
hygiene: row-zero, column-zero, lowercase-A1, overflow, reversed-range,
invalid-batch, and missing-cell reads preserve the dirty shifted formula,
source/materialized audits, catalog state, materialized diagnostics, and later
save-as output, without coordinate repair, relaxed parsing, or session cloning.
Invalid `WorksheetEditor` mutations now pin the adjacent failure mode: rejected
`set_cell()` / `erase_cell()` / `erase_cells()` calls keep the expected invalid
reference diagnostic in `last_edit_error()`, but leave the renamed full-calc
dirty formula session, source/materialized audits, catalog state, materialized
diagnostics, and later save-as output intact, without rollback history or
rejected payload leakage.
Invalid row/column shift preflights now cover the same renamed full-calc audit
state: rejected `insert_rows()` / `delete_rows()` / `insert_columns()` /
`delete_columns()` bounds failures keep the expected shift diagnostic while
preserving the dirty shifted formula, source/materialized audits, catalog state,
materialized diagnostics, and later save-as output, without range clamping or
partial shift retry.
Valid materialized mutation recovery is pinned on top of that diagnostic state:
after a rejected invalid formula payload, a later valid `set_cell()` clears
`last_edit_error()`, keeps the renamed full-calc dirty formula audit state,
adds the recovered text cell, and saves/reopens without leaking the rejected
formula payload or inventing calcChain.
Its clean no-op output now also fresh-reopens with the renamed sheet, shifted
styled formula audit/readback, recovered C5 text, `fullCalcOnLoad="1"`, and
absent calcChain still readable.
Saved-session reacquire now covers the same renamed full-calc formula audit
state: after the first save, matching planned-name reacquire stays clean,
preserves shifted materialized and original source audits without adding
handoffs, keeps old source-name lookup unavailable, and a later valid
`set_cell()` re-dirties the shared session before the second save/reopen keeps
the shifted formula, new text cell, `fullCalcOnLoad="1"`, and no calcChain.
Its clean no-op output after invalid-mutation recovery now also fresh-reopens,
verifying the renamed sheet, shifted styled formula audit/readback, recovered
C5 text, `fullCalcOnLoad="1"`, and absent calcChain remain readable.
The saved/reacquired invalid-read recovery path now has the same no-op output
readability evidence: after rejected read attempts and a valid C5 recovery edit,
the byte-stable clean output fresh-reopens with the renamed sheet, shifted
formula audit/readback, recovered text, `fullCalcOnLoad="1"`, and no calcChain.
The saved/reacquired invalid-shift recovery path now mirrors that evidence:
after rejected row/column shift preflights and a valid C5 recovery edit, the
byte-stable clean output fresh-reopens with the shifted formula audit/readback,
recovered text, `fullCalcOnLoad="1"`, and no calcChain.
The saved/reacquired missing-query recovery path now has matching no-op
readability evidence as well: after missing and old-source sheet lookups plus a
valid C5 recovery edit, the byte-stable clean output fresh-reopens with the
renamed sheet, shifted formula audit/readback, recovered text,
`fullCalcOnLoad="1"`, and no calcChain.
The saved/reacquired option-mismatch recovery path now has the same no-op
readability evidence too: after mismatched `WorksheetEditorOptions` lookups and
a valid C5 recovery edit, the byte-stable clean output fresh-reopens with the
renamed sheet, shifted formula audit/readback, recovered text,
`fullCalcOnLoad="1"`, and no calcChain.
The failed-save retry path is pinned for that saved/reacquired state too:
after a later valid `set_cell()` re-dirties the planned-name session, an exact
source-overwrite `save_as()` rejection preserves dirty diagnostics,
source/materialized audits, source package bytes, and clear
`last_edit_error()`; the following safe retry records the next handoff and
writes the renamed catalog, shifted formula, dirty text cell,
`fullCalcOnLoad="1"`, and no calcChain.
Invalid mutations on the clean saved/reacquired session now have matching
recovery coverage: rejected `set_cell()` / `erase_cell()` calls keep both
planned-name handles clean, preserve the invalid-reference diagnostic and
source/materialized audits without staging rejected payloads, and a later valid
`set_cell()` clears the diagnostic, re-dirties the shared session, then
save/reopen keeps the shifted formula, recovered text cell,
`fullCalcOnLoad="1"`, and no calcChain.
The invalid-mutation no-op save path is covered too: rejected row-zero /
column-overflow `set_cell()` calls and a range-form `erase_cell()` keep the
clean saved/reacquired handles clean, preserve the invalid-reference diagnostic
and source/materialized audits, and a second `save_as()` with no recovery
mutation writes the same renamed/fullCalc shifted worksheet bytes as the
pre-error save without rejected payloads, recovery cells, or calcChain.
Invalid reads on the clean saved/reacquired session are pinned too: rejected
coordinate/A1/range/batch reads and missing-cell `get_cell()` calls keep both
planned-name handles clean, preserve source/materialized audits, leave dirty
diagnostics empty and `last_edit_error()` clear, and a later valid `set_cell()`
re-dirties then saves/reopens with the shifted formula, recovered text,
`fullCalcOnLoad="1"`, and no calcChain.
The invalid-read no-op save path now pins the read-only half of that boundary:
the same invalid row/column/A1/range/batch and missing-cell reads leave
`last_edit_error()` clear and saved edit summaries unchanged, and a second
`save_as()` without a recovery mutation writes the same renamed/fullCalc
shifted worksheet bytes as the pre-error save without recovery cells or
calcChain.
Invalid row/column shifts on that same clean saved/reacquired session now have
the mutation-side recovery coverage as well: rejected shift bounds failures
preserve the shift diagnostic without dirtying either handle or materialized
diagnostics, keep source/materialized audits readable, and a later valid
`set_cell()` clears the diagnostic before save/reopen persists the shifted
formula, recovered text, `fullCalcOnLoad="1"`, and no calcChain.
The invalid-shift no-op save side is pinned as well: the same rejected
`insert_rows()` / `delete_rows()` / `insert_columns()` / `delete_columns()`
bounds failures preserve the shift diagnostic and saved edit summaries while
keeping both clean handles clean, and a second `save_as()` without a recovery
mutation writes the same renamed/fullCalc shifted worksheet bytes as the
pre-error save without recovery cells or calcChain.
Missing-sheet and old-source-name queries on the clean saved/reacquired session
are covered in the same family: empty optional lookups and throwing
`worksheet()` failures leave both planned-name handles clean, keep
`last_edit_error()` clear, preserve source/materialized audits, and a later
valid `set_cell()` still re-dirties then saves/reopens with the shifted formula,
recovered text, `fullCalcOnLoad="1"`, and no calcChain.
Their no-op save side is pinned as well: the same missing-sheet and old-source
queries preserve saved edit summaries, keep `last_edit_error()` clear, and a
second `save_as()` without a recovery mutation writes the same renamed/fullCalc
shifted worksheet bytes as the pre-query save without rejected sheet names,
recovery cells, or calcChain.
Mismatched `WorksheetEditorOptions` access now has the same no-op save
coverage: rejected planned-name option preflights and old-source optional
lookups leave diagnostics clean, preserve saved edit summaries, and a second
`save_as()` without a recovery mutation writes the same renamed/fullCalc
shifted worksheet bytes as the pre-option save.
The `pending_materialized_worksheet_names()` dirty-session save path now
reopens both auto-flushed worksheets, pinning clean multi-sheet readback after
diagnostic and failed-save inspections.
The aggregate materialized cell/memory diagnostics save path now reopens both
auto-flushed worksheets as well, covering explicit blank bounds plus source and
dirty cells after failed-save recovery.
Both diagnostics paths now repeat the clean no-op save too: the second no-op
packages match the first no-op packages, dirty materialized/replacement
diagnostics stay empty, sources remain unchanged, and both worksheets reopen
with the same flushed sparse cells.
The materialized-name move-owner save path is reopened too, verifying the moved
dirty session persists while the discarded target session does not leak into the
clean saved workbook.
It now repeats the clean no-op save too, keeping move-owner diagnostics empty,
preserving both source packages, and reopening both worksheets from the second
no-op output with only the moved dirty payload while keeping the prior
move-owner outputs byte-stable.
The `pending_worksheet_edits()` summary save path now reopens both the
auto-flushed materialized sheet and the replacement-only sheet, pinning clean
readback after mixed summary diagnostics.
It now repeats the clean no-op save too, preserving the retained replacement
summary, keeping materialized diagnostics empty, and reopening both worksheets
from the second output while keeping the prior mixed-summary outputs byte-stable.
The materialized-summary move-owner path now saves and reopens the assigned
editor output, confirming summary ownership transfer persists while discarded
target materialized edits stay absent.
It now repeats a second clean no-op save too, keeping materialized diagnostics
empty, preserving both source packages, and reopening the assigned output again
with only the moved dirty payload while keeping the prior assigned/no-op outputs
byte-stable.
The A1 single-cell overload save path is reopened to verify inserted D4 text,
source-backed cells, refreshed bounds, and erased A2 after save.
The `get_cell()` / `try_cell()` explicit-blank path now saves and reopens D4,
pinning blank persistence while unrelated missing cells remain absent.
The row/column coordinate overload recovery path is reopened after invalid
mutations, verifying the later valid A1 overwrite persists without rejected
payloads.
The `append_row()` max-cells guardrail recovery path is reopened after erasing a
source-backed row to release budget, pinning the saved appended A2 and rejected
payload absence.
The matching `append_row()` max-cells and memory-budget recovery outputs now
also reacquire with the original strict `WorksheetEditorOptions` and repeat
clean no-op saves, proving the recovered sparse append remains readable and
within budget after handoff.
The memory-budget branch now continues past those repeated no-op saves: the
strict-options reacquired handle overwrites the recovered `A2` text with a
shorter value, saves again, and fresh-reopens without reviving the rejected
append payload or exceeding the original sparse-store budget.
The `set_row()` guard paths now reopen both represented-row clearing and
max-cells recovery saves, verifying compact readback plus rejected row absence.
The `set_column()` guard paths now reopen both represented-column clearing and
max-cells recovery saves, verifying compact readback plus rejected column
absence.
The row/column replacement max-cells recovery outputs now also reacquire under
the same strict `max_cells` options and keep clean save/no-op entries stable.
Those strict-options represented row/column max-cells handles now also continue
past the no-op saves: a later in-budget `A1` replacement saves a fresh-reopenable
output while preserving the non-target sparse tail, excluding rejected cells,
and leaving earlier outputs unchanged.
The matching `set_row()` / `set_column()` memory-budget recovery outputs now
also reacquire under the original strict sparse-store budget and keep clean
save/no-op output entries stable.
The `set_row()` memory-budget branch now repeats that strict-options
reacquired clean no-op save as well, proving the repeated output stays
byte-stable while the saved input, first reacquire output, first no-op output,
source package, compact row replacement, and original sparse-store budget all
remain unchanged before the later post-noop edit.
The `set_column()` memory-budget branch now mirrors that repeated
strict-options clean no-op save, keeping the saved input, first reacquire
output, first no-op output, repeated no-op output, source package, compact
column replacement, and original sparse-store budget unchanged before its later
post-noop overwrite.
Those strict-options represented row/column memory-budget handles now also
continue past the no-op saves: a later shorter `A1` replacement stays within the
original sparse-store budget, saves a fresh-reopenable output, keeps the
non-target sparse tail, and leaves rejected payloads plus prior outputs
unchanged.
The `set_row_values()` / `set_column_values()` max-cells recovery saves are
also reopened, pinning value-prefix overwrites, preserved sparse tails, and
rejected prefix targets.
Those value-prefix max-cells recovery outputs now also reacquire with the same
strict `max_cells` options and run a clean no-op `save_as()`, proving the saved
sparse prefix edits remain reusable after handoff.
Those strict-options value-prefix max-cells handles now also continue past the
clean no-op saves: later in-budget `A1` prefix overwrites save and fresh-reopen
while preserving the row/column tail cells, excluding rejected prefix targets,
and leaving prior outputs plus sources unchanged.
The adjacent value-prefix memory-budget recovery outputs now carry the same
strict-options reacquire/no-op contract, including the original sparse-store
memory estimate, byte-stable entries, and unchanged sources.
The `set_row_values()` memory-budget branch now repeats that strict-options
clean no-op save, keeping the saved input, first reacquire output, first no-op
output, repeated no-op output, source package, row-prefix replacement, row tail,
non-target row, and original sparse-store budget unchanged before the later
post-noop overwrite.
The `set_column_values()` memory-budget branch now mirrors that repeated
strict-options clean no-op save, keeping the saved input, first reacquire
output, first no-op output, repeated no-op output, source package,
column-prefix replacement, column tail, non-target column, and original
sparse-store budget unchanged before the later post-noop overwrite.
Those strict-options value-prefix memory-budget handles now also continue past
the no-op saves: both row and column variants overwrite the recovered `A1` text
with a shorter value, save again, and fresh-reopen while preserving the row/column
tail cells and excluding rejected payloads.
Whole-store `clear_cell_values()` / `erase_cells()` saves are now reopened after
both the first projection and post-save handle reuse, covering styled blanks,
empty worksheets, and later value/appended edits.
Public-state also reopens max-cells and memory-budget guardrail recovery
outputs after erasing a source-backed cell to release insertion budget, verifying
clean diagnostics plus the saved erased/inserted sparse coordinates.
Those erased-cell budget-release recoveries now also run a second clean no-op
`save_as()` for both max-cells and memory-budget paths, proving repeated saves
remain byte-stable while sources and recovered sparse readback stay unchanged.
They now also reopen those erased-cell budget-release outputs with the original
strict `WorksheetEditorOptions`, run repeated clean no-op saves, then overwrite
the recovered `D4` cell without exceeding the original max-cells or memory
budgets while prior outputs and rejected payloads remain unchanged.
The adjacent missing-erase guardrail clean-save paths are also reopened to
verify the rejected target stays absent while source-backed cells remain clean.
They now repeat the clean no-op save as well, covering both max-cells and
memory-budget missing-erase recovery while keeping public state, source entries,
and reopened copy-original readback stable.
Those missing-erase outputs now also reacquire with the original strict
`WorksheetEditorOptions` and repeat clean no-op saves, proving copy-original
readback remains budget-valid after the saved-output handoff.
They now continue past that repeated no-op checkpoint as well: both strict
max-cells and memory-budget reacquired sessions overwrite source-backed `A1`,
save a fresh output, keep rejected `D4` absent, and leave the saved/no-op
packages byte-stable.
The formula-translation memory-budget shift failure path now also runs a second
clean no-op `save_as()`, proving the copy-original output and public state stay
stable after the rejected `insert_rows()` mutation.
The adjacent exact-budget shift success path now pins the opposite branch:
`insert_rows()` plus `insert_columns()` only move represented sparse records,
so exact `max_cells` and exact `memory_budget_bytes` budgets remain valid,
dirty diagnostics report the stable sparse store, and the saved/no-op outputs
reopen with shifted row/column snapshots.
The public-edge max-coordinate projection, erase-shrinks, A1-overload erase,
blank, formula, scalar, scalar-erase, and formula-erase paths now also run clean
no-op `save_as()` checks after the sparse `XFD1048576` handoff, proving the
repeated outputs are byte-stable, the source package stays unchanged, and
strict-options reopen can still read or omit the sparse edge cell as expected.
That success path now also reopens the saved output with the same exact
`WorksheetEditorOptions` and performs a clean no-op `save_as()`, proving the
shifted sparse store remains within the original budget after the handoff.
It now repeats that same-budget reacquired no-op save as well, preserving clean
public state, the shifted sparse readback, byte-stable output entries, and the
saved input package across a second clean save.
The same exact-budget reacquired handle now also performs a later budget-valid
overwrite after repeated clean no-op saves, proving the saved `insert_rows()` /
`insert_columns()` sparse shift can become dirty again, save, and reopen without
exceeding the original `max_cells` / `memory_budget_bytes` guardrails.
Delete-side shifts now cover exact-budget release too: after a rejected new-cell
insertion, `delete_rows()` and `delete_columns()` remove represented sparse
records, clear the guardrail diagnostic, allow a smaller recovered insertion
under both exact `max_cells` and exact `memory_budget_bytes`, and save/no-op
reopen without leaking rejected cells or mutating the source package.
The saved outputs now also reacquire with the same exact `WorksheetEditorOptions`
and perform a clean no-op `save_as()`, proving the released sparse budget remains
usable after the first handoff without dirty diagnostics or rejected payloads.
They now repeat that same-budget reacquired no-op save too, keeping all four
delete-row/delete-column max-cells and memory-budget outputs byte-stable while
the saved input packages and recovered sparse readback remain unchanged.
Those same-budget release handles now also overwrite the recovered insertion
after repeated clean no-op saves, proving the saved delete-row/delete-column
outputs can become dirty again, save, and reopen under the original exact
`WorksheetEditorOptions` without reviving rejected payloads.
The original saved-handle no-op path now repeats the clean save as well, with
the same recovered sparse readback and source-package stability checks across
that delete-row/delete-column exact-budget release matrix.
The adjacent source-load options / memory-budget guard recoveries and mutation
max-cells / memory-budget guard recoveries now mirror that second no-op
contract too: repeated clean saves keep package entries byte-stable, preserve
public diagnostics, leave sources unchanged, and fresh-reopen with the recovered
replacement values.
The mutation `max_cells` recovery output now also reacquires with the original
exact `WorksheetEditorOptions`, performs a clean no-op save, then overwrites A1
within the same budget and fresh-reopens without reviving the rejected D4
payload.
The matching mutation `memory_budget_bytes` recovery output now carries the
same same-budget handoff path, using a same-size A1 overwrite after the clean
no-op save to prove the recovered session can become dirty again without
exceeding the original sparse-store memory budget or leaking rejected D4.
The source-load options guard recovery no-op branches now also pin the retained
single-sheet replacement diagnostics and clear `last_edit_error()` contract,
distinguishing replacement handoff state from dirty materialized state.
The same recovery output now reacquires with the original strict `max_cells`
options that rejected the source worksheet, proving the saved replacement can
materialize cleanly under that budget and write a byte-stable no-op output.
That strict-options reacquired output now also performs a later in-budget A1
overwrite after the clean no-op save, proving the saved replacement can become
dirty again, save, and fresh-reopen without reviving the rejected source cells
or exceeding the original `max_cells` guardrail.
The last-edit-error diagnostic replacement path is also reopened after invalid
reference, memory-budget, and invalid-coordinate failures to pin clean public
state plus rejected-payload absence after the later successful overwrite.
It now repeats that recovered clean no-op save too, proving the replacement
diagnostic stays clear, package entries remain stable, source bytes are
unchanged, and the second output still reopens with only the successful value.
That recovered output now also reacquires with the original strict
`WorksheetEditorOptions` and repeats clean no-op saves, proving diagnostics stay
clear and the successful overwrite remains budget-valid after saved-output
handoff.
The same strict-options reacquired output now also performs a later same-budget
A1 overwrite after those repeated no-op saves, then saves and reopens without
reviving stale `last_edit_error()` diagnostics or rejected payloads.
The mixed public-edit diagnostic recovery path also reopens the saved output to
verify copy-original `Data` state and replacement-only `Untouched` state after
failed replacement, rename, and materialized mutation attempts.
It now repeats that clean no-op save as well, preserving public summaries,
keeping `last_edit_error()` clear, proving output entries stable, and reopening
the second output with the same recovered worksheet split.
That recovered mixed output now also reacquires as a clean saved workbook and
repeats clean no-op saves, proving copy-original `Data` and replacement-only
`Untouched` survive saved-output handoff without stale diagnostics or entry
drift.
The same mixed saved-output reacquire path now performs a later `Untouched!A1`
edit after those repeated no-op saves, saves and reopens the final output, and
proves the failed replacement payload, bad rename target, and invalid
`WorksheetEditor` mutation payload stay absent while `Data` remains
copy-original.
Existing-cell blank overwrite after rejected blank insertions is reopened as
well, pinning explicit blank readback without admitting the rejected target.
That same path now repeats a second clean no-op save for both exact `max_cells`
and `memory_budget_bytes` budgets, keeping package entries stable and public
dirty materialized state empty after the saved-session handoff.
It now also reacquires the saved blank-overwrite outputs with the original
strict `WorksheetEditorOptions` and repeats clean no-op saves, proving explicit
blank readback and rejected-target absence survive the saved-output handoff
within the same sparse-store budgets.
Those strict-options blank-overwrite handles now also snapshot sparse, row, and
column views across the clean reacquire, repeated no-op saves, and later
post-noop A1 overwrite handoff for both exact `max_cells` and
`memory_budget_bytes` branches, pinning explicit blank/source-backed readback
parity without broadening blank insertion policy.
Those strict-options reacquired blank outputs now also become dirty again after
the repeated no-op saves: `max_cells` writes a new A1 text value without changing
the represented sparse count, while `memory_budget_bytes` writes a shorter A1
text value that stays inside the original exact memory budget; both final
outputs reopen without reviving the old A1 text or rejected D4 blank.
The single-cell `erase_cell()` auto-flush save now reopens the output to verify
the erased coordinate stays absent while surviving source-backed cells keep the
shrunk bounds.
That saved erase output now also reacquires as a clean `WorksheetEditor` session
and repeats clean no-op saves, proving the erased coordinate remains absent and
package entries stay stable after saved-output handoff.
That same reacquired saved-output path now also overwrites surviving `A1` after
the repeated no-op saves, writes a fresh output, and reopens with `A2` still
absent while the saved and no-op packages remain byte-stable. The final
post-noop output now also snapshots sparse, row, and column views for the
overwritten `A1`, preserved source-backed `B1`, and erased `A2` absence.
The styled `erase_cell()` variant now mirrors that saved-output reacquire path:
the erased styled source cell stays absent, the surviving unstyled neighbor
stays clean, repeated no-op saves keep package entries stable, and a later
post-noop overwrite/save/reopen keeps the erased styled source absent while
the previous saved/no-op outputs remain byte-stable. That final styled output
now also snapshots sparse, row, and column views for the unstyled surviving
cell while the erased styled column stays absent.
The styled `erase_cells()` range variant now follows the same handoff: erased
styled and unstyled targets stay absent, the non-target source cell remains
unstyled, reacquired no-op saves keep the output stable, and a post-noop
overwrite/save/reopen keeps the erased range absent while prior outputs remain
byte-stable. The final post-noop output now also snapshots sparse, row, and
column views for the unstyled surviving cell while the erased styled/unstyled
target columns stay absent; this is readback parity, not style migration.
The original saved handles for those styled `erase_cell()` and `erase_cells()`
paths now also repeat a second clean no-op save, requiring byte-identical
packages, stable save/catalog snapshots, unchanged source bytes, and fresh
reopen of the erased and surviving sparse coordinates.
The single-cell `erase_cell()` saved-output reacquire path and its styled
single/range variants now also re-read prior no-op package files after repeated
clean no-op saves and after the later post-noop dirty handoff, pinning that
those files themselves remain unchanged rather than only matching cached entry
snapshots.
The styled `erase_row()` variant now carries that saved-output handoff too:
the erased styled row remains absent, the surviving row stays unstyled,
reacquired no-op saves stay byte-stable, and a post-noop overwrite/save/reopen
keeps the erased row absent while prior outputs remain stable. That final
post-noop output now also snapshots sparse, row, and column views for the
overwritten surviving row while the erased row remains absent; this is
readback parity, not style migration.
The styled `erase_rows()` inclusive-range variant now mirrors the same saved-output
handoff: erased styled rows stay absent, the surviving row stays unstyled, and
reacquired no-op saves stay byte-stable; it now also overwrites the surviving
row after those no-op saves, saves again, and reopens without reviving the
erased styled rows or mutating the earlier saved/no-op packages. That final
post-noop output now also snapshots sparse, row, and column views for the
overwritten surviving row while the erased rows remain absent; this is
readback parity, not style migration.
The styled `erase_column()` variant now carries the same saved-output handoff:
the erased styled column remains absent, the surviving column stays unstyled,
and reacquired no-op saves stay byte-stable; it now also overwrites the
surviving column cell after those no-op saves, saves again, and reopens without
reviving erased column cells or mutating the earlier saved/no-op packages. That
final post-noop output now also snapshots sparse, row, and column views for the
overwritten surviving column cell while the erased column remains absent; this
is readback parity, not style migration.
The styled `erase_columns()` inclusive-range variant now mirrors that handoff:
erased styled columns stay absent, the surviving column stays unstyled, and
reacquired no-op saves stay byte-stable; it now also overwrites the surviving
column cell after those no-op saves, saves again, and reopens without reviving
the erased styled columns or mutating the earlier saved/no-op packages. That
final post-noop output now also snapshots sparse, row, and column views for the
overwritten surviving column cell while the erased columns remain absent; this
is readback parity, not style migration.
Those styled row/column erase saved-output reacquire paths now also re-read the
first and second no-op package files after repeated clean no-op saves and after
the later post-noop dirty handoff, proving the prior no-op files themselves
remain unchanged instead of only matching cached entry snapshots.
The `erase_row()` / `erase_column()` exact-budget release saves now reopen the
output as well, pinning the inserted replacement coordinates without reviving
erased source cells.
Those exact `max_cells` row/column erase no-op paths now also pin saved-handle
sparse, row, and column snapshots for erased source absence, preserved
non-target cells, recovery cells, and clean diagnostics.
The inclusive `erase_rows()` / `erase_columns()` exact `max_cells` release
paths now mirror that saved-handle coverage after a clean no-op save, proving
the erased source rows/columns stay absent while the recovery cell remains the
only represented sparse record.
The row/column-side exact `max_cells` release outputs now also reacquire with
the original strict `WorksheetEditorOptions` and perform clean no-op saves,
keeping the erased source rows/columns absent and the saved packages byte-stable.
The same row/column erase paths now cover exact `memory_budget_bytes` release:
an oversized insertion fails first, the erase clears that diagnostic and lowers
the sparse memory estimate, then a smaller insertion saves/reopens without
leaking the rejected payload or erased cells.
The inclusive `erase_rows()` / `erase_columns()` paths now mirror that exact
budget release across row/column ranges: after a rejected oversized insertion,
erasing rows 1..2 or columns 1..2 clears diagnostics, drops the sparse store to
empty, and a smaller recovery cell saves/reopens as the only represented cell
without resurrecting erased source cells.
The `erase_row()` and `erase_rows()` exact memory-budget release no-op paths
now also pin saved-handle sparse, row, and column snapshots for erased source
absence, recovery cells, and clean materialized diagnostics.
They now also reacquire the saved outputs with the original strict
`WorksheetEditorOptions`, keeping the same row-side sparse snapshots within
budget while the matching-option clean no-op save remains byte-stable.
The matching `erase_column()` and `erase_columns()` exact memory-budget release
no-op paths now extend that saved-handle coverage to preserved non-target
columns, erased column absence, recovery cells, and clean diagnostics.
They now also reacquire the saved outputs with the original strict
`WorksheetEditorOptions`, keeping the same sparse snapshots within budget while
the matching-option clean no-op save remains byte-stable.
The dirty-state save/reuse path now reopens both the first erased-cell save and
the later post-save mutation output, verifying clean readback across repeated
`save_as()` calls on the same materialized handle.
The row/column shift overflow paths now also save and reopen the preserved
dirty state, proving edge cells remain at their original legal coordinates and
no out-of-bounds shifted cells leak into output.
Public-state
coverage now also verifies that materialized row/column
shifts use the same narrow formula translator for `$` absolute anchors,
A1-style ranges, sheet-qualified references, whole-row/whole-column ranges, and
quoted / structured-reference skip cases while preserving the existing
non-parser / non-evaluator boundary.
Stationary formula structural rewrites now pin the same skip-token boundary for
string literals, structured references, quoted sheet-name tokens, external
workbook brackets, function calls, and name-like tokens.
The lightweight `fastxlsx.formula` lane now mirrors that boundary directly for
structural rewrites, covering function/name-like/R1C1-like text and
quoted/external qualifier token preservation without touching production logic.
The public `WorksheetEditor` wording now also spells out the existing
`row_cells()` / `column_cells()` invalid-coordinate read-failure boundary:
these snapshot reads throw without dirtying the materialized session or
replacing `WorkbookEditor::last_edit_error()`.
The same boundary now has a small standalone public snapshot CTest that creates
a source workbook through `Workbook`, opens it through `WorkbookEditor`, checks
`try_cell()` / `get_cell()` / `contains_cell()` scalar reads plus
`row_cells()` / `column_cells()` snapshots, verifies invalid scalar and snapshot
read failures preserve `last_edit_error()`, sparse counts, and memory estimates,
saves an edit, reopens it, and confirms a clean no-op `save_as()` remains
byte-stable.
That baseline and the styled-source snapshot lane now also reuse the clean
no-op output for a later `F4` text edit: original and no-op packages stay
unchanged, the follow-up output fresh-reopens cleanly, and the styled lane keeps
the original source `StyleId` while the follow-up save byte-stabilizes.
The generic reopened follow-up edit helper now also checks the later text cell
through live and fresh-reopened `row_cells()` / `column_cells()` snapshots. This
extends the existing post-noop reuse evidence across the standalone snapshot
cases that share that helper, without adding a new API, metadata sync, or
large-file random editing claim.
The same shared helper now also checks `contains_cell()` and `sparse_cells()` on
the live reused session and on the fresh-reopened follow-up output, keeping
direct reads, full sparse traversal, and row/column sparse traversal aligned.
That standalone CTest now also covers the other public sparse snapshot overloads:
full `sparse_cells()`, bounded `CellRange`, strict A1 range strings, and
coordinate-batch reads all preserve source values, saved edits, requested-order
batch semantics, and prior diagnostics on invalid read failures.
The standalone snapshot lane now also covers `set_cells()` full-cell sparse
batch saves: duplicate coordinates use later-wins ordering, explicit blanks,
formulas, and booleans persist through `save_as()`, overwritten source and
intermediate batch payloads are omitted, reopened row/column snapshots are
stable, and clean no-op save remains byte-stable.
It now mirrors that save/reopen path for `set_cell_values()` value-only sparse
batches: source-backed cells are updated, non-target dirty cells survive,
missing coordinates are inserted, duplicate coordinates keep later-wins formula
payloads, overwritten source and intermediate values are omitted from XML, and
clean no-op save remains byte-stable.
It now also pins the single-cell `set_cell_value()` value-only save/reopen path:
the row/column overload updates a source-backed cell, the strict A1 overload
inserts a missing cell, non-target dirty/source cells survive, overwritten source
payloads are omitted from XML, and clean no-op save remains byte-stable.
The same standalone snapshot lane now pins `contains_cell()` through a mixed
save/reopen path: source-backed cells, explicit blanks, inserted-and-shifted
cells, erased cells, old shifted coordinates, and never-present sparse gaps all
report the expected represented/missing state before save, after reopen, and
after a clean no-op save.
It now also batches handle-level read-only inspection APIs on that same
save/reopen lane: `name()`, `try_cell()`, `get_cell()`, `contains_cell()`,
`used_range()`, `estimated_memory_usage()`, and requested sparse snapshots stay
non-dirty over source and dirty sessions, preserve dirty materialized diagnostics,
and reopen cleanly after the dirty save plus byte-stable no-op save.
The same standalone snapshot lane now also batches workbook catalog inspection:
`worksheet_names()`, `source_worksheet_names()`, `has_worksheet()`,
`has_source_worksheet()`, and `try_worksheet()` stay read-only over a generated
source workbook, a copy-original clean save, a later reused-handle dirty
`WorksheetEditor` save, fresh reopen, and byte-stable clean no-op save.
It now also pins the catalog lane's materialized diagnostics:
`pending_materialized_worksheet_names()`, aggregate dirty cell count / memory,
`pending_worksheet_edits()`, and `worksheet_catalog()` report the dirty `Data`
session before flush, clear after save/reopen/no-op, and keep source/planned
catalog entries stable without adding replacement diagnostics.
It now also covers the `sparse_cells(initializer_list<WorksheetCellReference>)`
inspection convenience end to end: source and dirty sessions preserve requested
order, duplicate coordinates, and missing-cell skips, invalid literal batches
remain read failures without overwriting prior diagnostics, and save/reopen plus
clean no-op save keep the same owning snapshots.
The same initializer-list snapshot case now also rejects invalid literal
mutation batches for `set_cells()`, `set_cell_values()`, `clear_cell_values()`,
and `erase_cells()` before applying earlier valid-looking entries, preserving
both clean and dirty sparse stores and keeping rejected payloads out of saved
XML.
The invalid snapshot-read helper now also pins workbook-level state stability
across clean and dirty materialized sessions: dirty flags, pending change
counts, dirty materialized aggregate diagnostics, and worksheet summary counts
remain unchanged after failed scalar, range, A1, or coordinate-batch reads.
The same standalone snapshot lane now also batches empty literal no-op mutation
overloads: empty `append_row()`, missing-row/column empty replacements, empty
full/value batch replacements, empty value clears, and empty erases clear prior
diagnostics in clean and dirty sessions, keep clean sessions copy-original,
preserve dirty materialized diagnostics and sparse values, and keep clean
follow-up saves byte-stable.
The same snapshot lane now covers the span-based batch mutator overloads
together: `set_cells(span)`, `set_cell_values(span)`, `clear_cell_values(span)`,
and `erase_cells(span)` preserve later-wins / blank / erase semantics through a
mixed dirty save, fresh reopen, row/column snapshots, and byte-stable clean
no-op save. It also rejects invalid span coordinates before applying earlier
valid-looking entries, keeping both clean and dirty materialized sparse stores
unchanged and preventing rejected payloads from reaching saved XML.
It now also pins a source-backed `erase_cell()` roundtrip: erased sparse records
shrink `used_range()` and dirty materialized cell counts, dirty `save_as()` omits
the erased cells and source text, reopened snapshots expose only the survivor,
the source package remains unchanged, and a clean no-op save stays byte-stable.
The same standalone CTest now contrasts `clear_cell_value()` with erase:
source-backed cells become explicit blank records, `used_range()` and dirty
materialized cell counts stay stable, dirty `save_as()` writes blank `<c>` cells,
reopened snapshots keep those blanks, and clean no-op save output is stable.
It now also pins `append_row()` on the same public snapshot lane: appending a
text/number/boolean row expands sparse bounds, saves and reopens all appended
cells, leaves the source package unchanged, and keeps the clean no-op output
byte-stable. The same lane now also covers the row-limit failure recovery path:
a legal `XFD1048576` dirty edit is preserved when a following `append_row()`
would exceed Excel's maximum row, rejected payloads stay out of saved XML,
reopen sees only the edge cell, and the clean no-op output remains stable.
It now also covers the matching width-limit failure path in clean and dirty
sessions: rows with 16,385 values are rejected before mutation, source or dirty
sparse state is preserved, rejected payloads stay out of saved XML, and no-op
save output remains stable.
The append failure outputs now also have post-noop reuse coverage: clean
width-failure, dirty width-failure, and row-limit failure no-op outputs each
reopen clean, accept a later `F4` text edit, keep the failure and no-op packages
unchanged, fresh-reopen with `F4`, and byte-stabilize after the follow-up save.
The same snapshot lane now covers structural row/column shift failure recovery:
invalid shift spans leave clean sessions copy-original, edge-row/edge-column
overflow failures preserve dirty `C3` and `XFD1048576` sparse cells, saved XML
contains only the preserved dirty state, and clean no-op save output remains
byte-stable.
The direct structural shift output checks now also verify `row_cells()` and
`column_cells()` snapshots for every expected shifted cell and every absent old
coordinate across row/column insert/delete outputs and their follow-up saves.
Those structural-shift follow-up edits now also check live `row_cells()` /
`column_cells()` before save for the shifted cells plus the newly appended `F4`
cell, keeping saved-output and in-session snapshot behavior aligned.
They now also check live full `sparse_cells()` and absent-coordinate reads before
that save, so the shifted store is inspected through the same public traversal
surface before and after handoff.
The shift/no-op recovery outputs now also have post-noop reuse coverage:
structural shift no-op, structural shift failure, and empty literal no-op
outputs in both clean and dirty sessions reopen clean, accept a later `F4` text
edit, keep the original and no-op packages unchanged, fresh-reopen with `F4`,
and byte-stabilize after the follow-up save.
The same standalone lane now also covers `set_row()` plus `set_column()` sparse
replacement: row replacement can add blanks and new columns, column replacement
can overwrite source-backed and prior dirty cells, saved XML omits overwritten
payloads, reopen preserves row/column snapshots, and clean no-op save remains
byte-stable. The same replacement case now rejects row/column zero and
upper-bound overflow coordinates in clean and dirty materialized sessions,
preserves sparse state, clears diagnostics through valid recovery calls, and
keeps rejected payloads out of saved XML.
It now also contrasts the value-only row/column prefix writers:
`set_row_values()` updates only the row prefix while preserving cells beyond it,
`set_column_values()` updates only the column prefix while preserving cells beyond
it, saved XML omits overwritten source/intermediate values, reopen preserves
row/column snapshots, and clean no-op save remains byte-stable. The same public
snapshot case now rejects row/column zero and upper-bound overflow
value-prefix coordinates in clean and dirty materialized sessions, preserving
sparse state and keeping rejected payloads out of saved XML.
The same public snapshot lane now covers the structural-shift group together:
`insert_rows()`, `delete_rows()`, `insert_columns()`, and `delete_columns()`
move or remove represented source-backed and dirty sparse cells, refresh saved
dimensions, omit old shifted coordinates, reopen with stable snapshots, and keep
clean no-op saves byte-stable.
That group now also reuses each clean no-op shifted output as a fresh editor
source: all four shift directions write a later `F4` sparse cell, keep the
shifted/no-op packages unchanged, fresh-reopen with the original shifted cells
plus `F4`, and settle into another byte-stable no-op save.
It now also covers maximum legal delete spans for both axes:
`delete_rows(1, 1048576)` and `delete_columns(1, 16384)` remove all represented
source-backed plus dirty sparse cells, save as an empty sheetData projection,
fresh-reopen as an empty sparse store, and keep the follow-up no-op save
byte-stable.
That same full-axis delete regression now continues from the clean saved handle:
a later `B2` edit reuses the empty sparse store, saves as a fresh single-cell
worksheet, keeps the source/empty/no-op packages unchanged, fresh-reopens, and
settles into another byte-stable no-op save.
It now also exercises a fresh editor reopened from the empty output before the
same-handle reuse: a later `C1` edit saves a separate single-cell package,
leaves the empty outputs unchanged, fresh-reopens, and settles into its own
byte-stable no-op save.
That structural-shift snapshot lane now also covers the clean no-op group in one
case: zero-count row/column shifts clear prior diagnostics, target-outside
row/column shifts leave source-backed cells unchanged, no missing sparse cells
are synthesized, `save_as()` copies the original package entries, and a second
clean save stays byte-stable. The same case now repeats that no-op group over a
dirty materialized session, preserving dirty diagnostics and sparse values,
omitting rejected payloads from saved XML, and keeping the clean follow-up save
byte-stable.
It now also covers zero-count no-ops at the maximum legal row and column
boundaries, proving they use the same no-op path without tripping the positive
shift overflow guards in clean or dirty materialized sessions.
The standalone snapshot lane now also covers `clear_row()` plus
`clear_column()`: represented source-backed and dirty cells become explicit
blank records, non-target sparse cells stay intact, saved XML omits cleared
payloads, reopen preserves row/column blank snapshots, and clean no-op save
remains byte-stable.
It now mirrors that public save/reopen lane for inclusive `clear_rows()` plus
`clear_columns()`: multi-row and multi-column ranges only clear already
represented sparse records, missing cells are not synthesized, non-target cells
survive, saved XML keeps explicit blank `<c>` records, and clean no-op save
remains byte-stable.
The same standalone snapshot lane now covers whole-store
`clear_cell_values()`: source-backed and dirty sparse records all become
explicit blanks, missing interior cells are not synthesized, saved XML omits the
cleared payloads, reopen preserves row/column blank snapshots, and clean no-op
save remains byte-stable.
The basic clear/erase snapshot outputs now also have fresh-reopen edit reuse:
single-cell erase, erase-all, clear-value, and clear-all no-op outputs each
open as clean editor sources, accept a later `D3` text edit, preserve their
baseline/no-op packages unchanged, fresh-reopen with `D3`, and settle into a
byte-stable clean save.
The same snapshot lane now also covers coordinate-batch
`clear_cell_values(...)`: selected source-backed and dirty sparse cells become
explicit blanks, duplicate coordinates are idempotent, missing coordinates are
not synthesized, non-target dirty/source cells survive, and clean no-op save
remains byte-stable.
It now also pins strict A1-range `clear_cell_values("B1:C2")`: only
represented cells inside the parsed range become blanks, missing cells inside
the range are not synthesized, range-external dirty/source cells survive, and
clean no-op save remains byte-stable.
It now adds the matching coordinate-batch `erase_cells(...)` roundtrip:
selected source-backed and dirty sparse cells are removed, duplicate
coordinates stay idempotent, missing coordinates are not synthesized,
non-target dirty/source cells survive, and clean no-op save remains byte-stable.
It now also pins strict A1-range `erase_cells("B1:C2")`: only represented cells
inside the parsed range are removed, missing cells inside the range are not
synthesized, range-external dirty/source cells survive, and clean no-op save
remains byte-stable.
The same lane now adds the `erase_row()` plus `erase_column()` contrast:
represented source-backed and dirty sparse records are removed, the remaining
non-target sparse cell shrinks `used_range()` to a single coordinate, saved XML
omits erased cells and payloads, reopen exposes only the survivor, and clean
no-op save remains byte-stable.
It now also mirrors that contrast for inclusive `erase_rows()` plus
`erase_columns()`: multi-row and multi-column ranges remove only represented
sparse records, missing cells are not synthesized, the surviving non-target cell
shrinks `used_range()` to its coordinate, saved XML omits erased cells, and
clean no-op save remains byte-stable.
The clear/erase snapshot lane now extends fresh-reopen edit reuse to
coordinate-batch, strict A1-range, row/column scalar, and row/column range
clear/erase outputs: each clean no-op output opens as a clean editor source,
accepts a later `F4` text edit, preserves the baseline/no-op packages,
fresh-reopens with `F4`, and settles into a byte-stable clean save.
The append/value snapshot lane now carries the same reuse check for
`append_row()`, sparse batch replacement, row/A1 `set_cell_value()`, and the
`contains_cell()` mixed mutation output: each no-op output is reopened clean,
accepts a later `F4` text edit, keeps the earlier packages unchanged,
fresh-reopens with `F4`, and byte-stabilizes after the follow-up save.
The same post-noop reuse check now extends to inspection-heavy outputs:
inspection mutations, catalog dirty save/no-op, sparse initializer-list batch
mutations, span batch mutations, value-batch writes, row/column replacement,
row/column value-prefix writes, and row/column value-span writes all reopen
clean, accept a later `F4` text edit, preserve earlier packages, fresh-reopen
with `F4`, and settle through a byte-stable follow-up clean save.
The standalone snapshot lane also pins whole-store `erase_cells()`: all
source-backed and dirty sparse records are removed, `used_range()` becomes
empty, saved XML projects the empty worksheet dimension without cell records,
reopen exposes empty row/column snapshots, and clean no-op save remains
byte-stable.
The direct public-state row/column shift saves are also reopened, pinning clean
readback for shifted sparse coordinates, translated formulas, preserved source
styles on moved formulas, rich formula-shape translations, out-of-bounds
`#REF!` translations, and removed old sparse coordinates.
The planned-name rename boundary is pinned for shifts too: after
`rename_sheet("Data", "RenamedData")`, materializing `worksheet("RenamedData")`
and applying `insert_rows()` reports dirty materialized diagnostics under the
planned name, saves the renamed workbook catalog, and reopens only by
`RenamedData` with the shifted sparse cells.
The same renamed shift path now covers same-editor post-save reacquire:
`try_worksheet("RenamedData")` reuses the clean shifted session, the old `Data`
name stays unavailable, a later `insert_columns()` dirties the shared planned
session, and the second output reopens only as `RenamedData` with combined
shifted coordinates.
Renamed planned-name shifts now also cover source-backed styled formulas:
`insert_rows(2, 2)` translates the moved formula from `A1+B1` to `A3+B3`,
preserves its source `StyleId`, saves the formula cell under `RenamedData`, and
reopens clean with the old `Data` name unavailable.
The matching renamed column-insertion formula path is covered too:
`insert_columns(2, 1)` moves the styled formula to `E2`, translates `A1+B1` by
the moved cell's column delta to `B1+C1`, preserves the style id, and
saves/reopens only under `RenamedData` with the shifted source-backed columns.
The materialized formula audit path is pinned on top of the same renamed row
and column shifts: formulas like `Data!A1+Data!B1` are audited at their shifted
formula cell coordinates with shifted `Data!A3` / `Data!B3` or `Data!B1` /
`Data!C1` reference tokens, while `formula_reference_audits()` remains read-only
and still only reports stale source-name risk instead of rewriting formulas.
Those materialized formula audit checks now also snapshot aggregate materialized
memory and full pending worksheet edit summaries, so row/column shift formula
scans cannot mutate dirty diagnostic fields while reporting shifted tokens.
The same insert-side audit is now pinned after a fresh output reopen:
`WorkbookEditor::open(output)` rematerializes the shifted styled formulas, reports
both shifted `Data!A3` / `Data!B3` or `Data!B1` / `Data!C1` tokens, and marks
the stale `Data!` qualifier unmatched because the saved workbook catalog only
contains `RenamedData`.
The insert-side audit now also extends through same-editor post-save matching
reacquire: after `save_as()`, `worksheet("RenamedData")` reuses the clean saved
session, pending materialized diagnostics remain empty, and
`formula_reference_audits()` still reports both shifted qualified tokens.
Fresh output source scans are pinned too: before any worksheet is materialized,
`source_formula_reference_audits()` reads the saved worksheet XML, reports the
same shifted formula tokens for insert-side outputs, reports only the surviving
token for delete-side `#REF!` outputs, and leaves the reopened editor clean.
Those fresh-output reopen checks now also require zero aggregate materialized
memory, empty pending edit summaries, and no `last_edit_error` after both source
scans and post-materialization formula audits.
The read-only source XML formula audit remains isolated from those dirty
materialized shifts: in the same renamed editor, `source_formula_reference_audits()`
still reports the source `D2` formula text `Data!A1+Data!B1` and the original
`Data!A1` / `Data!B1` tokens while preserving dirty materialized diagnostics.
That isolation now also survives a rejected source-overwrite save: after
`save_as(source)` is rejected, the same source scan still reports the original
source formula tokens, keeps the shifted materialized session dirty, and leaves
the later safe `save_as(output)` path intact.
It also survives mismatched `WorksheetEditorOptions`: rejected
`try_worksheet()` / `worksheet()` calls leave the shifted materialized session
dirty, keep source formula scans on the original source XML tokens, and do not
block the later safe save.
Missing-sheet and old-source-name lookups now share that boundary: optional
queries stay empty, throwing lookups fail, source formula scans remain on the
original source XML tokens, and the dirty shifted session can still be saved.
Mismatched options on the clean saved/reacquired session now round out the
query-preflight family: rejected planned-name access with incompatible
`WorksheetEditorOptions` leaves both handles clean, keeps `last_edit_error()`
clear and materialized diagnostics empty, preserves source/materialized audits,
and a later valid `set_cell()` still re-dirties then saves/reopens with the
shifted formula, recovered text, `fullCalcOnLoad="1"`, and no calcChain.
Same-sheet Patch guards are now covered on that clean saved/reacquired session
as well: rejected planned-name `rename_sheet()` and `replace_sheet_data()` calls
record only the guard diagnostic, do not leak rejected names or payloads into
pending diagnostics or output, preserve source/materialized audits, and a later
valid `set_cell()` clears the diagnostic before saving/reopening with the same
full-calculation metadata boundary.
The guard-only save side is pinned too: after the same rejected
same-sheet Patch sequence, a second `save_as()` with no new materialized
mutation keeps both handles clean, keeps the guard diagnostic intact, preserves
saved edit summaries and source/materialized audits, and writes the same
renamed/fullCalc shifted worksheet bytes as the pre-guard save without leaking
rejected sheet names, replacement payloads, recovery cells, or calcChain.
Invalid read preflights now follow the same rule: row-zero reads,
column-overflow A1 reads, and reversed range reads fail without replacing the
source scan or dirty materialized diagnostics.
Invalid mutation preflights are covered separately: rejected row-zero and
column-overflow formula `set_cell()` calls, plus range-form `erase_cell()`
failures, may update `last_edit_error()` but still keep source formula scans on
the original source XML tokens and preserve the dirty shifted materialized
session for a later safe save.
Invalid row/column shift preflights now share that dirty-session boundary:
invalid start coordinates and count ranges fail after a renamed formula shift,
but source formula scans still report the original source XML tokens and the
dirty materialized session remains saveable.
Source-backed materialization guardrail failures now share the same isolation:
failing to materialize an untouched worksheet with a tighter `max_cells` option
does not replace `last_edit_error()`, does not register a partial materialized
session, and still leaves source formula scans on the original source XML tokens.
The recovery path is pinned too: after that guardrail failure, default-options
materialization of the untouched worksheet can still create a clean read-only
session without adding dirty materialized diagnostics or changing the source
formula scan.
Save-as output path preflights now cover the same source/materialized audit
isolation: path-equivalent source overwrite, empty output path, missing parent,
non-directory parent, and existing-directory output failures leave source
formula scans on the original source XML tokens and keep the dirty shifted
materialized session saveable.
The following safe-save retry is pinned as well: after those rejected output
paths, a later safe `save_as(output)` still writes the shifted qualified
formula, leaves the source package and rejected path artifacts unchanged, and
reopens with clean source formula audits over the saved output.
Delete-side `#REF!` formula audits now have the same retry guard: after
rejected source/path/output targets, source scans still report the original
`Data!A1+Data!B2` tokens, the safe save writes only the surviving shifted
reference plus `Data!#REF!`, and the source package plus rejected artifacts
remain untouched.
The delete-side audit boundary now covers mixed `#REF!` translations as well:
after deleting row 1 or column 1, `Data!A1+Data!B2` becomes
`Data!#REF!+Data!B1` or `Data!#REF!+Data!A2`, and the materialized formula audit
reports only the surviving shifted sheet-qualified A1 token instead of treating
`Data!#REF!` as a reference.
Those delete-side materialized audits now reuse the same public-state diagnostic
snapshot helper across dirty sessions and post-save clean reacquire, preserving
aggregate memory, full edit summaries, pending-state, and `last_edit_error`.
The same delete-side audit now extends through post-save matching reacquire:
after `save_as()`, `worksheet("RenamedData")` reuses the clean saved session,
pending materialized diagnostics remain empty, and `formula_reference_audits()`
still reports only the surviving shifted reference.
Source scans are now pinned across that same-editor post-save reacquire
boundary too: insert-side clean saved sessions still leave
`source_formula_reference_audits()` on the original source `Data!A1+Data!B1`
tokens, while delete-side `#REF!` sessions still report the original
`Data!A1+Data!B2` tokens and keep materialized diagnostics empty.
Those source-scan isolation checks now also snapshot the aggregate
`estimated_pending_materialized_memory_usage()` value, so read-only source
formula audits cannot silently alter materialized memory diagnostics.
They now compare full `pending_worksheet_edits()` summaries as well, covering
rename, replacement, dirty materialized, cell-count, and memory summary fields
instead of only the number of summaries.
Insert-side same-editor post-save formula audits now reuse the shared
materialized-audit diagnostic snapshot as well: after row/column insert shifts
are saved and `RenamedData` is reacquired as a clean session,
`formula_reference_audits()` preserves aggregate memory, full edit summaries,
pending-state, and `last_edit_error` while still reporting both shifted
qualified tokens from the materialized store.
The shared fresh-output clean readback helper now pins the same wider public
diagnostic surface: reopened saved outputs must keep aggregate
materialized/replacement memory, pending edit summaries, dirty worksheet-name
diagnostics, and `last_edit_error` empty both after materialization and after
valid readback inspections.
The guardrail and diagnostic-recovery reopen helpers now use the same clean
diagnostic shape around readback: budget-release saves, missing-erase no-op
saves, rejected blank-insertion overwrite saves, last-error recovery, and mixed
public-edit recovery all keep widened editor diagnostics empty before and after
valid saved-output inspections.
The shared row/column shift reopened-output helper now has the same widened
diagnostic contract, covering saved insert/delete row/column shifts,
reacquire/retry paths, invalid-to-valid recovery, rich formula shifts, and
cross-handle shift preservation before and after readback.
Formula-audit diagnostic snapshots now include replacement cell-count and
replacement-memory preservation as well. The fresh reopened formula-audit
helpers also assert empty replacement dirty-name diagnostics around source and
materialized audit calls, keeping the read-only audit path from mutating either
replacement or materialized public-state surfaces.
Those formula-audit snapshots now also pin source worksheet names, planned
worksheet names, and workbook sheet catalog entries across both source and
materialized audit calls, so formula scanning cannot silently mutate catalog
views while reporting shifted tokens.
Fresh reopened formula-audit outputs now reuse that catalog-view snapshot too:
the saved shifted workbook keeps source/planned names and catalog entries stable
around both source scans and post-materialization formula scans.
The shared failure-diagnostic inspection helper now uses the same catalog-view
snapshot across all public read-only inspections as well. Invalid references,
guardrail failures, failed replacements, failed renames, and failed
materialized mutations keep source/planned names and workbook catalog entries
stable while preserving the prior `last_edit_error()`, including across
source/materialized/defined-name formula audit calls.
That same helper now also covers targeted-cell Patch diagnostics: targeted
replacement counts, sheet names, membership checks, and estimated XML bytes are
read-only public inspections that preserve `last_edit_error()` and catalog
views.
The public-state save-state snapshot and clean replacement diagnostics now carry
those targeted-cell diagnostics too, keeping no-op saves and clean In-memory
workbook states from silently accumulating queued targeted Patch payloads.
The max-coordinate public-edge shard now mirrors that targeted-cell diagnostic
coverage in its clean-state helpers, save-state snapshots, and read-only
inspection checks without changing max-coordinate In-memory projection
semantics.
The standalone materialized-session shard now carries the same targeted-cell
diagnostic preservation checks, so borrowed-handle and materialization recovery
coverage also proves clean In-memory sessions do not accumulate targeted Patch
payloads.
The core WorkbookEditor shard now mirrors the same targeted-cell diagnostic
checks in its shared public-state helpers, keeping core move/clean/failure
coverage aligned with the In-memory shards without widening Patch or
materialized-session behavior.
The formula/definedName rewrite shard now mirrors that helper contract: its
shared inspection helper snapshots source/planned worksheet names and full
catalog entries around formula, source-formula, and definedName audit calls,
without broadening default rename behavior or adding definedName repair.
The facade, public-retry, and source-success common helpers now share that
catalog-view inspection contract too, so their public read-only inspection
paths preserve source/planned names and catalog entries while leaving
`last_edit_error()` untouched.
The remaining standalone public WorkbookEditor shards now use the same helper
contract, completing catalog-view preservation coverage for read-only
inspection APIs without changing production semantics.
A fresh `WorkbookEditor::open(output)` over that saved workbook now rematerializes
the same styled `#REF!` formulas and keeps audit state lexical-only: the
surviving `Data!B1` / `Data!A2` tokens are reported, `Data!#REF!` is skipped,
and the stale `Data!` qualifier is unmatched because the reopened workbook
catalog contains only `RenamedData`.
Renamed formula shifts are now pinned across same-editor post-save reacquire:
after saving the `insert_rows(2, 2)` styled formula shift, `try_worksheet("RenamedData")`
reuses the clean formula/style session, old `Data` stays unavailable, a later
`insert_columns(2, 1)` moves the formula to `E4` as `B3+C3`, and the second
output reopens clean under `RenamedData`.
The same styled formula shift is now pinned across a rejected source-overwrite
save: a failed `save_as(source)` keeps the dirty `D4` formula as `A3+B3` with
its `StyleId` under `RenamedData`, leaves the source package unchanged under
`Data`, and a safe retry reopens clean under `RenamedData`.
The saved styled formula session now also covers option-mismatch reacquire:
mismatched `WorksheetEditorOptions` fail without updating diagnostics or
dirtying the clean `RenamedData` session, and a later matching reacquire can
still shift the formula to `E4` as `B3+C3` with the same `StyleId`.
Invalid mutations now cover that saved styled formula state as well: rejected
formula payload writes and invalid erases set diagnostics without dirtying or
leaking rejected formula text, while the next valid shift clears diagnostics and
saves `E4` as `B3+C3` with the original `StyleId`.
Missing-sheet and old-source-name queries now cover the saved styled formula
state too: `Missing` and `Data` lookups fail without diagnostics or dirty
materialized state, and the matching `RenamedData` reacquire can still shift
the formula to `E4` as `B3+C3` with the original `StyleId`.
Invalid reads now cover the same saved styled formula state: invalid cell/range
snapshots and valid-missing `get_cell()` calls leave diagnostics empty, keep
`D4` as `A3+B3`, and a later valid shift still saves `E4` as `B3+C3` with the
original `StyleId`.
Valid snapshot reads now pin that same saved styled formula state: full sparse
snapshots, A1-range snapshots, coordinate-batch snapshots, `row_cells()`, and
`column_cells()` expose `D4` with its `StyleId`, stay diagnostic-clean and
non-dirty, and their owning snapshots remain stable after a later valid column
shift saves `E4` as `B3+C3`.
The corresponding renamed delete-column formula path is pinned too:
`delete_columns(1, 1)` moves the styled formula to `C2`, translates the deleted
`A1` reference to `#REF!` and shifted `B1` to `A1`, preserves the style id, and
saves/reopens only under `RenamedData` while keeping shifted `B2` at `A2`.
That delete-column formula path now also covers same-editor post-save
reacquire: the clean `RenamedData` handle still reads `C2` as `#REF!+A1` with
the original `StyleId`, the old `Data` name stays unavailable, and a later
valid `insert_rows(2, 1)` saves/reopens `C3` as `#REF!+A2`.
That same delete-column formula path now covers failed-save retry hygiene too:
a rejected exact source overwrite keeps the dirty `C2` formula as `#REF!+A1`
with the original `StyleId`, leaves the source workbook unchanged under
`Data`, and a safe retry saves/reopens clean under `RenamedData`.
Delete-column formula option-mismatch hygiene is now pinned after save:
mismatched `WorksheetEditorOptions` fail without diagnostics or dirty state,
the matching `RenamedData` reacquire still reads styled `C2`, and a later
`insert_rows(2, 1)` saves/reopens styled `C3` as `#REF!+A2`.
Delete-column formula invalid-mutation hygiene is now pinned after save too:
rejected formula writes and invalid erases set `last_edit_error()` without
dirtying the saved styled `C2` state or leaking rejected formula payloads, and
a later valid row shift clears diagnostics and saves/reopens styled `C3` as
`#REF!+A2`.
Delete-column formula missing-query hygiene now matches that shape: `Missing`
and old `Data` lookups fail without diagnostics or dirty state, while matching
`RenamedData` reacquire still shifts and saves/reopens styled `C3` as
`#REF!+A2`.
Delete-column formula invalid-read hygiene is now covered after save too:
invalid coordinates/ranges, invalid row/column snapshots, and a valid-missing
`get_cell("D2")` all leave the saved styled `C2` session clean, and matching
reacquire can still save/reopen styled `C3` as `#REF!+A2`.
Delete-column formula snapshot reads now cover the same saved styled session:
full sparse, A1-range, row/column, and coordinate-batch snapshots expose `C2`
with its `StyleId`, stay diagnostic-clean and non-dirty, and remain stable
after a later row shift saves/reopens styled `C3` as `#REF!+A2`.
The symmetric renamed delete-row formula path is pinned as well:
`delete_rows(1, 1)` moves the styled formula to `D1`, translates both row-one
references to `#REF!`, preserves the style id, and saves/reopens only under
`RenamedData` with shifted row-two and row-three cells.
That delete-row formula path now has the matching same-editor post-save
reacquire coverage: the clean `RenamedData` handle keeps `D1` as
`#REF!+#REF!` with the original `StyleId`, old `Data` remains unavailable, and
a later valid `insert_columns(2, 1)` saves/reopens `E1` with the same formula
and style.
The delete-row formula failed-save retry shape is pinned as well: a rejected
exact source overwrite keeps dirty `D1` as `#REF!+#REF!` with the source
`StyleId`, leaves the source workbook under `Data` untouched, and a safe retry
saves/reopens the shifted five-cell state cleanly under `RenamedData`.
Delete-row formula option-mismatch hygiene now mirrors the column case:
mismatched `WorksheetEditorOptions` leave the saved styled `D1` session clean
and diagnostic-free, while matching reacquire can still `insert_columns(2, 1)`
and save/reopen styled `E1` as `#REF!+#REF!`.
Delete-row formula invalid-mutation hygiene now mirrors that shape too:
rejected formula writes and invalid erases set `last_edit_error()` without
dirtying the saved styled `D1` state or leaking rejected formula payloads, and
a later valid column shift clears diagnostics and saves/reopens styled `E1` as
`#REF!+#REF!`.
Delete-row formula missing-query hygiene is pinned too: `Missing` and old
`Data` lookups leave the saved styled `D1` state clean and diagnostic-free, and
matching `RenamedData` reacquire can still save/reopen styled `E1` as
`#REF!+#REF!`.
Delete-row formula invalid-read hygiene now mirrors the column case: invalid
coordinates/ranges, invalid row/column snapshots, and a valid-missing
`get_cell("D2")` leave the saved styled `D1` state clean and diagnostic-free,
and matching `RenamedData` reacquire can still save/reopen styled `E1` as
`#REF!+#REF!`.
Delete-row formula snapshot reads now close that symmetric state-hygiene shape:
full sparse, A1-range, row/column, and coordinate-batch snapshots expose `D1`
with its `StyleId`, stay diagnostic-clean and non-dirty, and remain stable
after a later column shift saves/reopens styled `E1` as `#REF!+#REF!`.
API-facing docs now summarize the same shift boundary: represented sparse
row/column shifts move/delete materialized cells, preserve source-backed
`StyleId` handles on moved formula cells, translate only moved formula text
through the narrow A1-style translator, and remain outside formula evaluation,
metadata synchronization, relationship repair, sharedStrings/styles migration,
calcChain rebuild, and low-memory random editing.
The renamed planned-name shift path is also pinned across a rejected
source-overwrite `save_as()`: after post-save reacquire and a follow-up
`insert_columns()`, exact source overwrite, path-equivalent source overwrite,
empty output path, missing-parent output, non-directory-parent output, and
existing-directory output attempts all preserve dirty diagnostics under
`RenamedData`, keep the source workbook unchanged under `Data`, leave the first
renamed output isolated, and a safe retry reopens only as `RenamedData` with
the combined shifted coordinates.
After that rejected-save retry, same-editor planned-name reacquire is pinned as
well: `try_worksheet("RenamedData")` returns the clean saved combined-shift
session, the old `Data` name remains unavailable, a later `delete_rows(3, 1)`
shrinks all shared handles, and the third output reopens only as `RenamedData`.
The renamed planned-name shift path also covers option-mismatch reacquire:
mismatched `WorksheetEditorOptions` against `RenamedData` fail without updating
`last_edit_error()`, dirtying materialized diagnostics, restoring the old
`Data` name, or blocking a later matching reacquire + shift save that reopens
only as `RenamedData` with combined shifted coordinates.
Missing-sheet queries now cover the same renamed shift boundary: `Missing` and
old `Data` lookups fail cleanly after the first saved `RenamedData` shift,
leave diagnostics and materialized dirty state empty, and a later matching
`RenamedData` reacquire can still save combined shifted coordinates.
Invalid reads now cover that renamed planned-name boundary as well: invalid
row/column, A1, sparse-range, row/column snapshot, coordinate-batch, and valid
missing `get_cell()` reads leave both `RenamedData` handles clean, keep
diagnostics empty, preserve the planned catalog, and still allow a later
matching shift/save cycle.
Invalid mutations now cover the same renamed shift path: invalid `set_cell()`
and `erase_cell()` calls set the public diagnostic without dirtying either
`RenamedData` handle, leaking rejected payloads, restoring old `Data`, or
blocking a later valid shift that clears diagnostics and saves cleanly.
The same-handle row/column shift reuse path now saves an insert-rows
projection, performs a later insert-columns shift on the same borrowed
`WorksheetEditor`, saves again, and reopens both outputs to verify clean state,
output isolation, and shifted sparse readback.
Post-save matching reacquire now has the same structural-shift lifecycle
coverage: after an `insert_rows()` save, a matching `worksheet("Data")`
reacquire reuses the saved clean materialized session, a later
`insert_columns()` through the reacquired handle is visible through the older
handle, and both first/second outputs reopen with isolated shifted sparse state.
The optional `try_worksheet("Data")` matching reacquire path now carries the
same saved-session proof, including later shared-session shift/save and reopened
combined sparse readback.
That optional reacquire path now also covers no-op-save stability: after the
saved insert-row shift is reacquired through `try_worksheet("Data")`, a later
no-op `save_as()` keeps both handles clean, keeps pending materialized
diagnostics empty, and reuses the first saved output byte-for-byte.
That saved-session no-op-save stability now has delete-side row and column
counterparts as well: after `WorksheetEditor::delete_rows()` saves the shifted
source-backed row, translated formula, and dirty tail, and after
`WorksheetEditor::delete_columns()` saves the shifted source-backed number,
translated formula, and dirty tail, clean matching reacquire keeps pending
materialized diagnostics empty and a later no-op `save_as()` reuses the first
saved delete output byte-for-byte.
The direct insert-column side now has the same no-op-save proof:
`WorksheetEditor::insert_columns()` saves shifted source-backed cells, a
translated formula, and a dirty tail, then clean matching reacquire keeps
pending materialized diagnostics empty and a later no-op `save_as()` reuses the
first insert-column output byte-for-byte.
Saved-session option-mismatch failures now cover the same no-op-save boundary:
rejected mismatched `WorksheetEditorOptions` leave `last_edit_error()` clear,
keep the saved shifted handle clean, preserve catalog and materialized
diagnostics, and a later no-op `save_as()` reuses the first shifted output
byte-for-byte.
Missing-sheet query failures now carry the same no-op-save boundary: missing
`try_worksheet("Missing")` and throwing `worksheet("Missing")` leave the saved
shifted handle clean, keep diagnostics and catalog state stable, and a later
no-op `save_as()` reuses the first shifted output byte-for-byte.
Invalid read failures now carry that no-op-save boundary too: rejected
row/column, A1, range, row/column snapshot, coordinate-batch, and valid-missing
`get_cell()` reads keep saved/reacquired shifted handles clean, leave diagnostics
and catalog state stable, and a later no-op `save_as()` reuses the first shifted
output byte-for-byte.
Invalid mutation failures now cover the mutation-side no-op-save boundary:
rejected row/column/A1 `set_cell()` and `erase_cell()` calls preserve the
invalid-reference diagnostic without dirtying saved/reacquired shifted handles,
and a later no-op `save_as()` reuses the first shifted output byte-for-byte
without leaking rejected payloads.
Invalid row/column shift failures now close the same no-op-save shape: rejected
`insert_rows()` / `delete_rows()` / `insert_columns()` / `delete_columns()`
bounds preserve the shift diagnostic without dirtying saved/reacquired handles,
and a later no-op `save_as()` still reuses the first shifted output
byte-for-byte.
The corresponding post-save shift option-mismatch path is pinned too:
mismatched `WorksheetEditorOptions` fail against the saved shifted session
without updating `last_edit_error()`, dirtying materialized diagnostics, losing
the shifted sparse state, or blocking a later matching reacquire + shift save.
Missing-sheet query failures now cover the same post-save shift boundary:
missing `try_worksheet()` / `worksheet()` calls leave the saved shifted session
clean, keep diagnostics empty, add no materialized handoff, and a later matching
reacquire can still perform and save a follow-up shift.
Invalid and missing read failures are pinned for that post-save shift boundary
too: invalid row/column, A1, sparse-range, row/column snapshot, and missing
`get_cell()` reads preserve the clean shifted session, leave diagnostics empty,
and do not block a later matching reacquire shift/save cycle.
Invalid mutation failures now cover the same saved shifted session: invalid
row/column and A1 `set_cell()` / `erase_cell()` calls record the public edit
diagnostic without dirtying materialized state, leaking rejected payloads, or
blocking a later valid shift from clearing diagnostics and saving.
The post-save shift failed-save retry path is pinned as well: after a follow-up
shift dirties the saved session, rejected exact source-overwrite,
path-equivalent source-overwrite, empty-output, missing-parent, and
non-directory-parent, and existing-directory `save_as()` calls preserve both
borrowed handles, dirty materialized diagnostics, handoff count,
source/first-output isolation, and the later safe save still flushes the
combined shifted sparse state.
After that safe retry, same-editor optional reacquire is pinned too: the saved
combined shifted session remains clean and reusable, a later `delete_rows()`
shrinks the sparse state through every borrowed handle, and the third output
reopens with the deleted row absent.
That post-retry clean reacquire path now has no-op-save coverage as well: after
the safe retry writes the combined shifted sparse state, a matching
`worksheet("Data")` reacquire keeps all handles clean, pending materialized
diagnostics empty, and a later no-op `save_as()` reuses the retry output
byte-for-byte.
The direct invalid-to-valid shift recovery path now covers row and column
validation failures followed by a valid shift on the same clean borrowed
`WorksheetEditor`, proving the later mutation clears diagnostics, dirties only
the materialized session, saves, and reopens as clean shifted sparse state.
The same recovery shape now also covers already-dirty materialized sessions:
invalid row/column shifts preserve the dirty sparse store and diagnostics, a
later valid shift clears the diagnostic, moves both source-backed and dirty
cells, and saved outputs reopen cleanly with the shifted sparse state.
Cross-handle dirty shift state is pinned as well: when `Data` and `Untouched`
are both dirty materialized sessions, a row shift on `Data` keeps workbook-level
dirty names/counts scoped to both sheets, leaves the other dirty handle's
coordinates unchanged, auto-flushes both sessions on `save_as()`, and reopens
both saved sheets as clean public state.
The column-direction cross-handle shape is covered too: a `Data` column shift
moves only `Data` source-backed and dirty columns while the already-dirty
`Untouched` handle keeps its own dirty column coordinate, aggregate dirty counts
still sum both sessions, and both saved sheets reopen cleanly.
The delete-row cross-handle path now covers the aggregate-count shrink case:
`Data.delete_rows()` removes represented records from the deleted row, shifts
later Data records upward, keeps the other dirty `Untouched` handle unchanged,
and saves/reopens both sheets with the updated scoped dirty counts.
The delete-column counterpart closes the same cross-handle matrix for columns:
`Data.delete_columns()` removes represented source columns, shifts later Data
source/dirty columns left, keeps an already-dirty `Untouched` column coordinate
unchanged, and verifies saved XML plus reopened clean state for both sheets.
The no-op, validation-failure, and memory-guard copy-original shift outputs are
also reopened in public-state coverage to verify the clean source-backed Data
sheet remains readable after failed or non-mutating shift attempts.
The no-op invalid A1 range, invalid cell read, row/column read failure, and
sparse range read failure save outputs are also reopened to verify copy-original
Data state stays readable after non-mutating diagnostics.
The invalid A1 range mutation path now also reads the same saved clean handle
after the copy-original save and repeated no-op save, checking full sparse,
bounded range, A1 range, row, and column snapshots without dirtying the
materialized session or reviving diagnostics.
The invalid row/column coordinate recovery path now mirrors that saved-handle
shape after the recovered A1 overwrite: the original handle exposes full,
bounded, A1-range, row, and column snapshots after the first save and both
clean no-op saves, with rejected C1 still absent and materialized diagnostics
empty.
The invalid cell-read path now also adds saved-handle sparse, bounded range,
A1-range, row, and column snapshots after its copy-original save and repeated
no-op save, preserving the seeded prior diagnostic while proving read-only
snapshots do not dirty the session.
The row/column read-failure path now also re-runs saved-session `row_cells()` /
`column_cells()` snapshots on the same clean handle after the copy-original save
and repeated clean no-op save, preserving the prior diagnostic while proving
source row/column ordering and missing row/column snapshots do not re-dirty the
materialized worksheet.
The sparse range read-failure path now mirrors that saved-handle check with
bounded `CellRange` and A1-string `sparse_cells()` snapshots after the
copy-original save and repeated clean no-op save, preserving the prior
diagnostic and source sparse ordering without queuing materialized changes.
The benchmark tool
`fastxlsx_bench_workbook_editor` now includes `patch-replace` and
`patch-upsert` scenarios for public facade performance smoke; the lower-level
`fastxlsx_bench_package_editor_cell_replacement` remains useful for transformer
diagnostics. The transformer now also stops targeted replacement lookup on
tail pass-through cells after requested targets have been matched/emitted and,
for strict replace, the source stream has advanced past the last target
coordinate. This lets large worksheets with an early sparse edit set spend less
work on tail pass-through cells. This is a hot-path cleanup only: the current
public Patch path still scans source XML, and metadata repair is still out of
scope. The internal worksheet event reader now exposes absolute source byte
offsets, and a new internal `WorksheetCellIndex` can build a sparse compact
cell-coordinate to source-`<c>` byte-range index from materialized or chunked
worksheet XML. The primary index no longer allocates a `std::map` string entry
per source cell; the old `cells()` map view is only a lazy diagnostic snapshot.
The index can now validate a bounded target set and return source-order rewrite
ranges, while transformer actions expose source XML offsets for source-backed
pass-through / replacement events. There is also an internal materialized
indexed slicer that can splice strict existing-cell replacement payloads by
those ranges when the worksheet XML bytes already exactly match the index.
`PackageEditor` now also has an internal
`PackageEntryChunk` byte-range emitter that can slice validated memory/file
staged chunks, including ranges crossing chunk boundaries, and an internal
chunk-backed indexed slicer prototype that replays strict existing-cell
replacement payloads over those staged chunk ranges. The lower-level
`fastxlsx_bench_package_editor_cell_replacement` tool now exposes this as an
opt-in internal `--rewrite-strategy indexed-staged` benchmark path with
schema-v2 timing fields for index build, indexed emit, and prevalidated staged
worksheet commit. That indexed-staged benchmark now uses a target-only range
planner: it streams the benchmark staged worksheet source, records `<c>` byte
ranges only for requested target cells, and reports `indexed_source_cell_count`
as scanned source cells rather than stored index entries. This removes the
previous O(source cells) indexed memory cost for one-shot sparse edits while
still doing a linear source scan. These are indexed/random-access rewrite
foundations, not public editor APIs, not a default large-sheet algorithm
switch, and not a generalized PackageEditor source-entry ZIP seek layer.
Current reruns on the same 10M-source-cell / 1000-edit benchmark show that
this internal path is already much faster than the transformer baseline. The
prevalidated by-name commit path now avoids a redundant staged worksheet CRC32
scan, dropping `indexed_stage_commit_ms` on the 10M / 1000 indexed-staged run
to about 1 ms. The target-only planner also has an opt-in early-stop mode for
trusted benchmark/prototype source shapes; on the same front-loaded 10M / 1000
indexed-staged run this drops `index_build_ms` to about 2 ms. The ZIP stored
writer can now reuse trusted size/CRC metadata for a single staged chunk header,
while still validating the chunk during data write; the same 10M / 1000 run
recorded `save_ms=3553` and `total_edit_ms=6069`. The next optimization slice
then removed the intermediate full rewritten worksheet file from the benchmark
path: `PackageEntryChunk` now has an internal file-range descriptor, the
indexed-staged benchmark emits source file ranges plus replacement memory chunks
directly, and the stored ZIP writer can combine per-chunk CRC contracts for the
entry local header instead of pre-reading the full entry. The same 10M / 1000
run now records `patch_plan_ms=2090`, `indexed_emit_ms=2082`,
`indexed_stage_commit_ms=4`, `save_ms=3005`, `total_edit_ms=5097`, and
`peak_memory_mb=6.21`, with `output_verified=true`. The latest follow-up moves
the stored ZIP writer to a seekable local-header patch model: it writes a
placeholder local header, streams entry data once while calculating final entry
CRC/size, then patches the local header before writing the central directory.
That lets source file-range descriptors skip descriptor-time CRC prepasses; the
same entry also reuses one input stream and cached file size for repeated ranges
from the same path. The same 10M / 1000 run now records `patch_plan_ms=8`,
`index_build_ms=1`, `indexed_emit_ms=0`, `indexed_stage_commit_ms=4`,
`save_ms=2454`, `verify_ms=2081`, `total_edit_ms=2463`, `total_ms=13120`,
and `peak_memory_mb=6.22`, with `output_verified=true`. The next concrete
follow-up moves the same indexed-staged benchmark source chunks from the
benchmark `source_body` sidecar to the real stored source package worksheet
entry payload range by using `PackageReaderEntry::data_offset` and
`PackageEntryChunk::file_range(source.xlsx, data_offset, uncompressed_size)`.
That run records `patch_plan_ms=18`, `index_build_ms=6`, `indexed_emit_ms=1`,
`indexed_stage_commit_ms=4`, `save_ms=2958`, `verify_ms=2077`,
`total_edit_ms=2983`, `total_ms=12954`, and `peak_memory_mb=6.19`, with
`package_entry_source_mode="source-package-worksheet-entry-direct-range-chunks"`
and `output_verified=true`. This is slightly slower than the body-sidecar
direct-range snapshot but is the more relevant boundary because the edit path
now reads source worksheet bytes from the `.xlsx` payload range itself. The
latest follow-up moves that source-entry range lookup behind internal
`PackageEditor` helpers instead of benchmark-local package-entry parsing:
`source_part_stored_entry_chunks()` and
`source_worksheet_part_stored_entry_chunks_by_name()` expose stored source parts
as file-range chunks over the original `.xlsx` bytes. The benchmark also now has
`--reuse-source` and records `source_fixture_mode`, so edit-path runs can skip
the 400 MB source/body fixture rewrite. Current 10M / 1000 reused-source runs
show `index_build_ms` around `1-2 ms`, `indexed_emit_ms=0`, and
`indexed_stage_commit_ms` in single-digit milliseconds, while `save_ms` varied
between about `4960` and `6769` ms and verifier time varied similarly. This
confirms the remaining hotspot is full-package stored ZIP output, entry CRC,
benchmark verification, and local disk state rather than target planning or
staged commit. `PackageWriter` validation now caches file sizes for repeated
same-path file-range chunks, but this is only fixed-overhead cleanup; it is not
evidence of arbitrary large-file random editing. The latest implementation now
wires a conservative subset of this direct-range path into
`PackageEditor::replace_worksheet_cells()`, so public
`WorkbookEditor::replace_cells()` can automatically use source-entry staged file
ranges for simple stored/no-rels strict replacement. The current 10M / 1000
public facade rerun with output-plan telemetry records `patch_plan_ms=6419`,
`save_ms=2492`, `verify_ms=1908`, `total_edit_ms=8912`, `total_ms=10822`,
and `peak_memory_mb=6.02`, with `output_verified=true`. The JSON now records
`output_plan_observed=true`,
`output_plan_indexed_source_entry_fast_path=true`,
`output_plan_transformer_fallback=false`,
`output_plan_staged_replacement_chunks=true`,
`output_plan_materialized_replacement=false`, `101` source file-range chunks,
`1000` replacement memory chunks, `indexed_source_cell_count=10000000`, and
`indexed_matched_replacement_count=1000`. This proves the public facade is
actually using source-entry direct-range staged chunks under the conservative
conditions, not just reporting `rewrite_strategy="transformer"` from the CLI.
The follow-up now makes those direct-range counters structured output-plan
fields instead of benchmark note parsing: `PackageEditorOutputEntryPlan`
exposes the selected direct-range flag, scanned source cell count, matched
replacement count, and staged output bytes. The no-parser 10M / 1000 rerun
records `patch_plan_ms=9230`, `save_ms=2622`, `verify_ms=2012`,
`total_edit_ms=11857`, `total_ms=13871`, `peak_memory_mb=6.00`, and
`output_verified=true`, with `indexed_source_cell_count=10000000` and
`indexed_matched_replacement_count=1000` coming from structured telemetry.
The next cleanup keeps public early-stop disabled for correctness: once all
requested targets are matched, the later source XML may still contain duplicate
target cells that strict replace must detect before committing. The target-only
planner therefore now uses sorted vector coordinate buckets instead of map/hash
lookup experiments, and the worksheet-cell-index tests pin duplicate normalized
coordinates such as `A1` / `a1`. A verified 10M / 1000 public facade rerun
records `patch_plan_ms=10573`, `save_ms=3821`, `verify_ms=3937`,
`total_edit_ms=14399`, `total_ms=18339`, `peak_memory_mb=6.27`, and
`output_verified=true`; this is safe structural cleanup, not a stable speed
claim, because full source scanning and stored-package IO still dominate.
The latest follow-up adds direct-range phase telemetry to that public path.
The 10M / 1000 rerun
(`build/qa/continue-phase-telemetry-result.json`) records
`patch_plan_ms=4180`, with
`output_plan_indexed_source_entry_target_plan_ms=4175` and the source-range
chunk setup, payload audit, relationship audit, descriptor generation, and
commit phases all in the 0ms bucket for this run. That pins the next real
optimization target: reduce or reuse the target-only source scan without
weakening strict duplicate/missing-target validation. Do not enable unconditional
early-stop in the public facade just to improve the benchmark; valid row-major
fast validation or a reusable source index must preserve the current fallback
and no-state-pollution behavior.
PackageEditor file-backed chunk CRC/copy buffers now use a 1MiB heap buffer
instead of a 64KiB stack buffer. The follow-up 10M / 1000 verified rerun
(`build/qa/continue-package-editor-fileio-1m-result.json`) records
`patch_plan_ms=4340`, `save_ms=2026`, `verify_ms=1953`,
`total_edit_ms=6370`, `total_ms=8326`, `peak_memory_mb=7.39`, and
`output_verified=true`. This is a safe file-IO granularity cleanup; it does not
change the remaining target-only scan bottleneck.
The next hot-path cleanup keeps the shared event-reader validator but lets the
target-only planner disable context-attribute copies. The default event reader
still copies row/cell context for transformer and diagnostic callers; only the
target-only range planner reads row/cell attributes from start events and tracks
active cell state itself. The matching 10M / 1000 public facade rerun records
`patch_plan_ms=8743`, `save_ms=2813`, `verify_ms=2725`,
`total_edit_ms=11557`, `total_ms=14284`, `peak_memory_mb=6.29`, and
`output_verified=true`. Treat this as safe allocation cleanup, not a new SLA:
local package IO and full source scan variance still dominate.
The next source-scan step replaces the target-only planner's generic event
object path with an internal lightweight scanner plus direct tag-name /
attribute parsing for the hot path. It keeps strict missing/duplicate target
validation, top-level dimension detection, and value-wrapper boundary checks,
while leaving the shared worksheet event reader as the default transformer /
diagnostic path. The verified 10M / 1000 public facade rerun records
`patch_plan_ms=7852`, `save_ms=2272`, `verify_ms=2183`,
`total_edit_ms=10128`, `total_ms=12324`, `peak_memory_mb=6.26`, and
`output_verified=true`, with `indexed_source_cell_count=10000000`,
`indexed_matched_replacement_count=1000`, and
`indexed_staged_output_bytes=367269975`.
The follow-up save-path cleanup keeps the same public boundary but reduces
stored ZIP output overhead: source file-range chunks now reuse the active input
stream across small forward gaps instead of always seeking, and stored/minizip
file-copy buffers use 1 MiB heap buffers instead of 64 KiB stack buffers. A
reused-source 10M / 1000 public facade rerun records `patch_plan_ms=6416`,
`save_ms=2152`, `verify_ms=1905`, `total_edit_ms=8572`, `total_ms=10479`,
`peak_memory_mb=8.03`, and `output_verified=true`, with `11` source
file-range chunks and `1000` replacement memory chunks. Treat the extra memory
as deliberate IO buffering, not worksheet materialization.
The next save-path step removes another avoidable copy for stored source
packages: copy-original source entries can now flow into `PackageWriter` as a
direct source-package file-range chunk with the source entry CRC as the expected
CRC. Compressed source entries and test-hook failure coverage still use the old
temp-file fallback. The matching reused-source 10M / 1000 public facade rerun
records `patch_plan_ms=4774`, `save_ms=1951`, `verify_ms=1905`,
`total_edit_ms=6727`, `total_ms=8639`, `peak_memory_mb=7.99`, and
`output_verified=true`, with `package_entry_source_mode` reported as
`source-package-worksheet-entry-direct-range-chunks`. Treat this as stored
source-copy cleanup only; it does not add compressed-source direct range,
Zip64, arbitrary random access, metadata repair, or relationship repair.
The follow-up removes a duplicate source-entry CRC pre-read in that same path:
stored source-entry chunks now carry expected size and CRC directly from
`PackageReaderEntry`, so `PackageEntryChunkReader` no longer has to scan the
entire source worksheet payload just to derive descriptor metadata before the
real target scan. Scan/replay still validates the source bytes against that
CRC. The matching reused-source 10M / 1000 public facade rerun records
`patch_plan_ms=3932`, `save_ms=1818`, `verify_ms=1757`,
`total_edit_ms=5753`, `total_ms=7512`, `peak_memory_mb=7.92`, and
`output_verified=true`, still with
`package_entry_source_mode="source-package-worksheet-entry-direct-range-chunks"`
and `output_entry_mode="indexed-source-entry-direct-range-staged-chunks"`.
This is duplicate IO removal, not a relaxation of source validation.
The next scanner-throughput cleanup increases the internal
`PackageEntryChunkReader` file replay chunk from 64 KiB to 1 MiB. The retained
XML window guard remains `package_editor_cell_replacement_event_window_byte_limit`
and chunk CRC validation is unchanged; this only reduces read callback and
scanner handoff overhead during the required full-source scan. The matching
reused-source 10M / 1000 public facade rerun records `patch_plan_ms=3449`,
`save_ms=1827`, `verify_ms=1725`, `total_edit_ms=5279`, `total_ms=7007`,
`peak_memory_mb=7.94`, and `output_verified=true`.
The next IO cleanup increases `PackageReader` stored/DEFLATE chunk-source reads
from 64 KiB to 1 MiB. This keeps ZIP metadata, entry size, and CRC validation
unchanged, but reduces callback overhead for entry streaming and benchmark
verification. A same-environment A/B records `total_edit_ms=7226` with the
1 MiB reader buffer versus `9099` after a temporary 64 KiB rollback; treat this
as IO/callback cleanup, not a new public editing semantic.
The latest target-only planner cleanup avoids copying every complete source
chunk into the bounded scanner window: the scanner now processes chunk
`string_view`s directly when there is no cross-chunk XML tail, and only retains
the unconsumed tail. Source cell reference parsing also uses ASCII uppercase
folding instead of `std::toupper`. A verified reused-source 10M / 1000 rerun
records `patch_plan_ms=5537`, `save_ms=2681`, `verify_ms=2758`,
`total_edit_ms=8219`, `total_ms=10980`, `peak_memory_mb=7.38`, and
`output_verified=true`; a no-verifier run records `patch_plan_ms=4563` and
`total_edit_ms=6675`. Treat this as scanner/copy hygiene, not a stable SLA.
The next descriptor-level cleanup merges adjacent replacement memory chunks in
the direct-range staged output while keeping source file ranges separate and
retaining expected size / CRC32 validation on the merged chunk. The matching
reused-source 10M / 1000 public facade verified rerun records
`patch_plan_ms=3990`, `save_ms=2037`, `verify_ms=2005`,
`total_edit_ms=6030`, `total_ms=8038`, `peak_memory_mb=7.49`, and
`output_verified=true`; the output plan now reports `21` staged chunks:
`10` replacement memory chunks plus `11` source file-range chunks, with
replacement memory bytes still matching the public facade payload bytes. Treat
this as fixed-overhead reduction only, not a change in editing semantics.
This is still deliberately narrow: planned worksheet inputs, compressed source
entries, upsert mode, `ReferencePolicyAction::Fail`, worksheets without
top-level `<dimension>`, and worksheets with relationships stay on the
transformer fallback; the public path still scans source XML for correctness
and does not provide O(1) random access, sharedStrings/styles migration,
metadata repair, or relationship repair. Next performance work should reduce
full-source scan cost safely and continue attacking stored ZIP save/verification
time without broadening the public API surface.
The current `WorksheetEditor` source loader can now read source `t="s"` cells
through the existing workbook `xl/sharedStrings.xml` and materialize them as
`CellValue::text(...)`; dirty `save_as()` can reuse that same source
sharedStrings part when it has a narrow appendable `<sst>` shape, writes text
cells as stable shared string indexes, and appends only new plain shared string
items. If the workbook has no sharedStrings table, or the existing table is
stale, malformed, prefixed/wrong-namespace, count-inconsistent, or otherwise
outside that append boundary, dirty save falls back to inline strings and never
creates a new sharedStrings part. The failure matrix is also pinned: duplicate
or invalid sharedStrings relationships/targets, missing or wrong-typed parts,
malformed sharedStrings XML, and invalid indexes fail fast for worksheets that
actually require source shared strings instead of being repaired or guessed.
The current `WorksheetEditor` style boundary now has a narrow value-only edit
API: `set_cell_value()`, `set_cell_values()`, `set_row_values()`, and
`set_column_values()` replace cell values while preserving currently
materialized source style handles on overwritten coordinates. They still reject
caller-supplied non-default `StyleId` values, do not create or merge
`xl/styles.xml` entries, and do not synthesize styles for newly inserted cells.
Full cell replacement through `set_cell()` continues to drop prior source style
handles by design. `clear_cell_value()`, no-argument `clear_cell_values()`,
`clear_row()`, `clear_rows()`, `clear_column()`, `clear_columns()`,
`clear_cell_values(CellRange)`, and
`clear_cell_values(span<WorksheetCellReference>)` now cover the matching
"clear contents" case: existing materialized cells become explicit blank cells
while preserving the current source style handle, row / column / range /
coordinate-batch clears affect only already represented sparse records, the
no-argument form clears all currently represented records, missing targets /
missing-only rows / missing-only columns / missing-only ranges /
missing-only coordinate batches and empty stores are successful no-ops, and the
output remains non-tombstone sparse projection.
The styled no-argument `clear_cell_values()` reuse path now also repeats a
second clean no-op save after a post-save value-only edit and save, proving the
reacquired handle remains clean and byte-stable without reviving cleared source
payloads.
No-argument `erase_cells()`, `erase_cells(CellRange)`, and
`erase_cells(span<WorksheetCellReference>)` now cover the matching sparse delete
cases: the no-argument form removes all represented records, the range overload
validates one 1-based inclusive rectangle, the coordinate overload validates
every coordinate before mutation, all variants delete only represented active
sparse records, treat empty / missing-only inputs as successful no-ops, and
write no tombstones or explicit blank cells for erased coordinates.
The no-argument `erase_cells()` whole-store reuse path now also mirrors the
clear-all handoff after post-save reacquire: repeated clean no-op saves stay
byte-stable, leave both saved handles clean, and do not resurrect erased source
or dirty cells.
The range erase save/reacquire state path is also pinned: after
`erase_cells(CellRange)` removes all represented cells and `save_as()` flushes
the materialized session, matching `worksheet()` reacquire reuses the clean
erased sparse state, missing-only range erase remains a no-op, and later
mutation persists without resurrecting erased cells.
The sparse range and coordinate-batch erase paths now also cover exact
`memory_budget_bytes` release: a rejected oversized insertion keeps the session
clean, `erase_cells(CellRange)` or initializer-list `erase_cells(...)` clears
that diagnostic and drops the sparse store to empty, and a smaller recovery cell
saves/reopens without resurrecting erased source cells or rejected payloads.
The no-argument `erase_cells()` whole-store path now completes that exact-budget
release family: after the same rejected oversized insertion, erasing the entire
represented sparse store clears diagnostics, releases the estimate, and saves a
single recovery cell without reviving erased source data.
The row/column clear paths now pin the value-payload side of that budget story:
`clear_row()` and `clear_column()` keep represented records as explicit blanks,
but clearing large source text payloads lowers the sparse memory estimate enough
for a later small insertion in the same exact-budget session. The saved output
reopens with blank target records, preserved non-target cells, and no rejected
payload leakage.
Those row/column exact-budget outputs now also reacquire with the same
`WorksheetEditorOptions` and write a clean no-op save, keeping saved-handle
snapshots, catalog state, diagnostics, and decompressed package entries stable.
The inclusive row/column range variants now carry the same evidence:
`clear_rows()` and `clear_columns()` clear every represented target record to an
explicit blank, preserve records outside the target row/column range, lower the
value-payload estimate, and still allow a later recovery cell to save/reopen
inside the exact budget.
Sparse range and coordinate-batch value clears now match that boundary too:
`clear_cell_values(CellRange)` and coordinate-list `clear_cell_values(...)`
release cleared text payload estimates, keep represented target coordinates as
blanks, skip missing coordinates without synthesis, preserve non-target cells,
and save/reopen a later recovery cell inside the same exact budget.
The range `clear_cell_values(CellRange)` exact-budget output now also reacquires
with the same `WorksheetEditorOptions` and writes a clean no-op save, proving the
blank sparse records and recovery cell remain readable without adding another
materialized handoff.
The coordinate-list `clear_cell_values(...)` exact-budget output now mirrors
that same-options reacquire/no-op contract, including the missing-coordinate
absence check and stable decompressed package entries.
No-argument `clear_cell_values()` now completes the value-clear exact-budget
family: it clears all represented values to explicit blanks, releases all value
payload estimates, and still saves/reopens a later recovery cell without
dropping the blank sparse records. The same exact-budget save path now also
checks post-save state hygiene on the live editor: dirty materialized names,
cell counts, memory, and summaries are cleared, and a same-options
`worksheet()` reacquire is clean while reading the saved blank records plus the
recovery cell. A no-op `save_as()` after that same-options reacquire is also
pinned: it keeps both handles clean, does not add another materialized handoff,
and writes the same decompressed package entries as the first output.
That no-op path now also preserves the public catalog snapshot, keeps
replacement diagnostics empty, snapshots both saved handles, and reopens the
no-op output with the saved blank records plus recovery cell intact.
Mismatched `WorksheetEditorOptions` access against that saved/reacquired session
is now pinned as well: rejected lookups keep diagnostics clear, preserve the
saved blank/recovery cells and catalog state, and a later no-op save still
matches the first output entries.
`WorksheetEditor::set_cells()` now covers the matching sparse batch full-cell
replacement case for small files: every update carries an explicit row/column
coordinate and `CellValue`, duplicate coordinates are allowed with later input
winning, empty batches are no-ops, and any coordinate/style/budget failure
rejects the entire batch before mutating the active materialized session. This
is still not a dense range writer, style-preserving edit, A1 range parser, or
large-file low-memory random-editing path.
Its duplicate-coordinate accounting is now pinned under `max_cells`: repeating
the same missing coordinate inside one `set_cells()` batch counts as one final
inserted sparse record, keeps the later full-cell payload, saves/reopens with
that formula cell, and leaves the earlier duplicate payload absent.
The full-cell batch now also has exact-memory-budget recovery coverage:
an oversized missing-cell update fails before applying earlier entries in the
same `set_cells()` batch, keeps sparse and pending materialized diagnostics
unchanged, copy-original saves unchanged, and then accepts a smaller overwrite
in the same exact-budget session. The recovery and no-op outputs reopen with
the accepted full-cell replacement while excluding both rejected payloads.
The value-only batch and coordinate-clear batch now use the same preflight /
staged sparse-store hygiene: `set_cell_values()` preserves existing source
styles while duplicate coordinates remain later-wins, and
`clear_cell_values(span<WorksheetCellReference>)` clears only represented
coordinates without synthesizing missing cells.
`set_cell_values()` now also has exact-memory-budget batch recovery evidence:
an oversized missing-cell update rejects the whole value-only batch before
applying earlier batch entries, preserves sparse counts/memory and pending
materialized diagnostics, keeps the rejected target absent, and leaves a
copy-original save unchanged. A later smaller overwrite in the same exact-budget
session clears the diagnostic, saves/reopens cleanly, and keeps the failed early
overwrite plus rejected payload out of both recovery and no-op outputs.
The styled source-backed `set_cell_values()` path now also repeats a second
clean no-op `save_as()` after the first no-op output, requiring byte-identical
entries plus live and fresh-reopen snapshots of the preserved source `StyleId`,
later-wins formula, inserted boolean, and untouched source tail.
The sparse batch mutation APIs also have small literal-batch convenience
overloads: `set_cells(initializer_list<WorksheetCellUpdate>)`,
`set_cell_values(initializer_list<WorksheetCellUpdate>)`,
`clear_cell_values(initializer_list<WorksheetCellReference>)`, and
`erase_cells(initializer_list<WorksheetCellReference>)` all synchronously
delegate to the span overloads, preserving the same preflight, duplicate /
missing-coordinate, guardrail, and diagnostic behavior without adding dense
range editing or A1 range parsing.
`WorksheetEditor::append_row()` now covers a small-file sparse append convenience:
it writes input values to columns 1..N on the row after the current maximum
represented sparse row, treats empty input as a no-op, and stages the append so
width, row-limit, style, max_cells, and memory-budget failures do not mutate the
active sparse store. Appended cells are new sparse records that do not inherit
source style handles from existing source rows, while untouched source-backed
cells and the preserved source styles part remain intact. This is not row
insertion, row metadata creation, table/range metadata recalculation,
sharedStrings/styles migration, or large-file low-memory random editing.
The saved append output is reopened in public-state coverage to verify clean
readback for the appended text, number, formula, explicit blank, and preserved
source-backed rows. The styled append path now also repeats a second clean
no-op `save_as()` and requires byte-identical entries plus fresh reopen readback
for the unstyled appended cells and preserved source `StyleId`.
The explicit-default-style append path now also verifies live, fresh-reopen, and
clean no-op `contains_cell()`, `row_cells()`, and `column_cells()` views: the
styled source row remains represented, the appended row is unstyled, explicit
blank cells remain represented, and missing row/column gaps stay absent.
`WorksheetEditor::set_row()` now covers the matching sparse represented-row
replacement convenience for small files: it deletes currently represented cells
in the target row, writes input values to columns 1..N, treats empty input as a
row clear, and treats an empty missing row as a no-op. Invalid rows, width,
style, max_cells, and memory-budget failures are staged and do not mutate the
active sparse store. This is not row insertion/deletion, row shifting, row
metadata editing, table/range metadata recalculation, sharedStrings/styles
migration, or large-file low-memory random editing.
`WorksheetEditor::set_column()` now covers the symmetric sparse represented-column
replacement convenience for small files: it deletes currently represented cells
in the target column, writes input values to rows 1..N, treats empty input as a
column clear, and treats an empty missing column as a no-op. Invalid columns,
height, style, max_cells, and memory-budget failures are staged and do not mutate
the active sparse store. This is not column insertion/deletion, column shifting,
column metadata editing, table/range metadata recalculation, sharedStrings/styles
migration, or large-file low-memory random editing.
Saved row/column replacement outputs are also reopened in public-state coverage
to verify clean readback for replacement text, number, formula, explicit blank,
untouched non-target cells, and non-synthesized sparse gaps.
Public-state coverage now also pins the full-cell replacement style boundary for
`set_row()` / `set_column()`: overwritten source-backed cells drop their prior
`StyleId`, non-target source-backed cells keep their `StyleId`, dirty `save_as()`
preserves the source `styles.xml`, fresh reopen readback matches the materialized
state, and no-op saves keep the clean handle / catalog diagnostics stable.
Those full-cell default-style regressions now also read the live materialized
state and the fresh-reopen/no-op outputs through `contains_cell()`,
`row_cells()`, and `column_cells()`: represented replacement cells, non-target
cells, explicit blanks, and sparse gaps stay consistent across both row-major
and column-major public views.
Public-state now also pins the exact-memory-budget failure path for those
row/column replacements: oversized `set_row()` / `set_column()` payloads fail
before deleting the old target records, preserving sparse counts, memory
estimates, dirty state, and source-backed cells.
Those row/column replacements now also recover in the same exact-budget
session: a smaller replacement clears the diagnostic, stays within the
sparse-store estimate, saves cleanly, reopens with compact replacement bounds,
and keeps replaced tails plus rejected payloads absent.
The value-prefix variants now mirror that exact-memory-budget path:
oversized `set_row_values()` / `set_column_values()` payloads fail before
replacing the first target, preserving prefix/tail cells, sparse counts, memory
estimates, dirty state, and the memory-budget diagnostic.
They now also recover in the same exact-budget session: a smaller prefix write
clears the diagnostic, stays within the sparse-store estimate, saves cleanly,
reopens with preserved tails, and keeps the rejected payload absent.
`WorksheetEditor::set_row_values()` and `set_column_values()` now cover the
style-preserving row/column prefix write convenience for small files: they write
input values to columns 1..N or rows 1..N, preserve source styles on overwritten
target cells, insert missing prefix cells without styles, treat empty input as
a clean no-op, and leave cells outside the input prefix untouched. Invalid
row/column coordinates, width/height, caller-supplied non-default `StyleId`,
max_cells, and memory-budget failures are staged and do not mutate the active
sparse store. This is not row/column replacement, insertion/deletion, shifting,
dense range writing, metadata recalculation, style migration/merge/creation, or
large-file low-memory random editing.
Their saved outputs are also reopened in public-state coverage to verify clean
readback for row/column prefix values, explicit blanks, formulas, untouched
tail cells, and preserved source `StyleId` handles on value-only overwrites.
The styled source-backed row/column value-prefix paths now also repeat a second
clean no-op `save_as()` after the first no-op output, requiring byte-identical
entries and fresh reopen readback of the preserved source `StyleId` plus the
untouched row/column tail.
The styled source-backed row/column clear paths now mirror that repeated
no-op-save contract for `clear_row()`, `clear_rows()`, `clear_column()`, and
`clear_columns()`: after the styled blank output and first no-op save, each path
writes a second clean no-op output, requires byte-identical entries, and
fresh-reopens the workbook to verify preserved source `StyleId` handles,
unstyled blanks, and untouched row/column tails.
The styled `clear_row()` path now also reacquires the saved blank output as a
fresh editor session and repeats clean no-op saves, proving preserved source
`StyleId` blank handles and unstyled blanks survive the saved-output handoff.
The styled `clear_rows()` inclusive-range path now mirrors that saved-output
handoff: preserved source `StyleId` blank handles, unstyled blanks, and clean
no-op saves survive after reacquiring the saved output.
The styled `clear_column()` / `clear_columns()` paths now close the same
saved-output handoff loop for column clears: a fresh editor session can reopen
the styled blank output, repeat clean no-op saves, and preserve non-target
column cells without introducing dirty diagnostics.
The isolated source-style shard now also has a compact `contains_cell()`
regression for `clear_row()`, `clear_rows()`, `clear_column()`, and
`clear_columns()`: live state, saved handles, fresh reopens, and clean no-op
readbacks all keep represented styled/unstyled blank coordinates present while
leaving missing coordinates absent. This is represented-state evidence for the
small-file sparse store, not metadata sync or style-table migration.
That compact clear row/column contains regression now also repeats a second
clean no-op `save_as()` for all four helpers, requiring byte-identical packages,
stable save/catalog snapshots, unchanged source bytes, and fresh reopen of the
represented blank coordinates.
The same isolated shard now mirrors that read-side coverage for `erase_row()`,
`erase_rows()`, `erase_column()`, and `erase_columns()`: erased styled source
records stay absent across live state, saved output, fresh reopen, and clean
no-op save, while non-target source records retain their existing value/style
handles. This is still small-file sparse-store evidence, not worksheet metadata
repair or style-table migration.
That compact erase row/column contains regression now also repeats a second
clean no-op `save_as()` for all four helpers, requiring byte-identical packages,
stable save/catalog snapshots, unchanged source bytes, and fresh reopen of the
erased or surviving sparse coordinates.
The source-style shard also pins the single-cell and `CellRange`
`erase_cells()` `contains_cell()` path: erased styled source records and erased
unstyled neighbors stay absent across live/save/reopen/no-op readbacks, the
surviving non-target styled source cell keeps its materialized `StyleId`, and
the erased-only style id does not leak into saved sheetData.
That single-cell and `CellRange` `erase_cells()` source-style regression now
also repeats a second clean no-op `save_as()`, requiring byte-identical
packages, stable save/catalog snapshots, unchanged source bytes, and fresh
reopen of the erased and surviving sparse coordinates.
The successful sparse row/column erase paths now have the same repeated
no-op-save coverage for `erase_row()`, `erase_rows()`, `erase_column()`, and
`erase_columns()`: after the erase output and first clean no-op save, each path
writes a second no-op output, requires byte-identical entries, and fresh-reopens
to verify erased coordinates stay absent while non-target source cells or
inserted tail records remain represented.
The styled source-backed `erase_row()` / `erase_rows()` variants now also repeat
that second clean no-op save, requiring byte-identical entries and fresh reopen
readback proving erased styled source cells stay absent without leaking their
`StyleId` into the remaining non-target row.
The styled source-backed `erase_column()` / `erase_columns()` variants now carry
the same repeated clean no-op save, requiring byte-identical entries and fresh
reopen readback proving erased styled source cells stay absent without leaking
their `StyleId` into the remaining non-target column.
The base sparse row/column shift success paths now also repeat that second
clean no-op save before their existing post-noop edit checks. `insert_rows()`,
`delete_rows()`, `insert_columns()`, and `delete_columns()` now require the
second no-op package to match the first no-op output and fresh-reopen through
the same shifted-coordinate inspectors before further edits are attempted.
Styled source-backed shift success paths now mirror that repeated clean no-op
save for `insert_columns()`, `delete_rows()`, and `delete_columns()`: after the
styled formula shift output and first no-op save, each path writes a second
no-op output, requires byte-identical entries, fresh-reopens through the styled
shift inspectors, and later post-noop edits prove both no-op packages remain
unchanged.
The value-only styled row/column shift paths now repeat that same clean no-op
save contract for `insert_rows()`, `insert_columns()`, `delete_rows()`, and
`delete_columns()`: after replacing a styled source formula with a value-only
cell and shifting it, each path writes two clean no-op packages, checks stable
entries and untouched source/output bytes, and fresh-reopens both no-op outputs
to prove the preserved source `StyleId` remains readable.
Those same value-only styled shift paths now also perform a later edit/save
through the same clean `WorksheetEditor` handle after the repeated no-op saves,
proving handle reuse while source, first output, and both no-op packages remain
byte-stable.
The cleared styled row/column shift paths now have the matching repeated
clean no-op coverage for `insert_rows()`, `insert_columns()`, `delete_rows()`,
and `delete_columns()`: after clearing the styled source formula value to a
blank while preserving the source `StyleId`, each shifted output writes two
clean no-op packages, compares stable entries, and fresh-reopens through the
cleared-cell inspectors while source and prior output bytes stay unchanged.
Those cleared styled shift paths now also mirror the post-noop edit/save reuse
coverage: after the repeated clean saves, the same `WorksheetEditor` handle can
write a later text cell while the shifted blank keeps its `StyleId` and every
previous package snapshot remains byte-stable.
Full-calculation plus insert-shift success paths now carry the same repeated
no-op-save readback for styled `insert_rows()` and `insert_columns()`: after
the shifted materialized output writes `fullCalcOnLoad` without inventing
`calcChain.xml`, each path performs two clean no-op saves, compares package
entries, and fresh-reopens the second no-op output through the full-calc shift
inspectors.
Those full-calculation insert-shift success paths now also continue after the
second clean no-op save: a later edit through the same `WorksheetEditor` handle
writes a fresh output, keeps the shifted styled formula readable, preserves
`fullCalcOnLoad`, still omits `calcChain.xml`, and leaves source plus prior
outputs byte-stable.
Full-calculation plus delete-shift success paths now have matching repeated
no-op-save readback. The styled `delete_rows()` path writes a second clean
no-op output, while the styled `delete_columns()` path now also fresh-reopens
the materialized output and both no-op outputs, checking translated styled
`#REF!` formulas, stable package entries, `fullCalcOnLoad`, and no invented
`calcChain.xml`.
`WorksheetEditor::row_cells()` and `column_cells()` now cover the matching
small-file sparse row/column inspection convenience: they return owning
row-major `WorksheetCellSnapshot` vectors for active sparse records already
represented in one row or one column, synthesize no missing cells, preserve
existing read diagnostics, and do not dirty, flush, reload, or mutate the
materialized session. They are not dense row/column reads, row/column metadata
inspection, iterators, metadata recalculation, or large-file low-memory random
access.
`WorksheetEditor::sparse_cells(std::string_view)` now adds the read-only strict
uppercase A1 range convenience over the same sparse snapshot path: `A1` is a
single-cell range, `A1:C3` is a rectangular range, and lowercase,
sheet-qualified, absolute, whole-row / whole-column, multi-area, reversed,
leading-zero, and out-of-limit references are rejected without changing
`last_edit_error()`. This is not a dense range read, iterator, metadata
recalculation, or large-file low-memory random access.

`WorksheetEditor::sparse_cells(std::span<const WorksheetCellReference>)` and
its initializer-list overload now add the matching explicit sparse coordinate
batch convenience over the same sparse snapshot path: input order is preserved,
duplicate coordinates are allowed, missing coordinates are skipped, and the
operation does not change `last_edit_error()`. This is not a dense batch read,
iterator, metadata recalculation, or large-file low-memory random access.
`WorksheetEditor::contains_cell()` now adds the matching read-only represented
cell probe for row/column and strict uppercase A1 cell references: source-backed
records, edited records, and explicit blank records return true; missing or
erased cells return false; invalid references throw without changing
`last_edit_error()`. It does not copy `CellValue`, read or repair worksheet
`<dimension>` metadata, expose iterators, or add large-file low-memory random
access.
No-argument `WorksheetEditor::clear_cell_values()` and
`WorksheetEditor::erase_cells()` now complete the whole materialized sparse-store
clear/delete boundary: clear converts all represented records to explicit blanks
with current source style handles preserved, erase removes all represented
records, and empty stores are clean no-ops that clear public edit diagnostics.
They are not worksheet deletion, sheetData part removal, dense range mutation,
metadata recalculation, relationship repair, or large-file low-memory random
editing.
`WorksheetEditor::clear_cell_values(std::string_view)` and
`erase_cells(std::string_view)` now add the matching strict uppercase A1 range
mutation convenience over the existing `CellRange` sparse clear/erase paths:
`A1` and `A1:C3` are accepted; lowercase, sheet-qualified, absolute,
whole-row / whole-column, multi-area, reversed, leading-zero, and out-of-limit
references are rejected and update `last_edit_error()` as mutation failures.
They still only clear or remove represented active sparse records, never
synthesize missing cells, and do not add dense range writes, tombstones,
metadata recalculation, relationship repair, or large-file low-memory random
editing.
`fastxlsx_bench_workbook_editor` now provides the matching opt-in local
performance probe for this public editor path. It generates a stored source
workbook, opens it through `WorkbookEditor`, materializes one `Data` worksheet,
executes `point-set`, `batch-set`, `a1-range-clear`, or `a1-range-erase`, and
writes a schema-v1 JSON report with source generation, open, materialize,
mutation, save, sparse-store estimates, process peak working set, and
input/output package sizes. The tool is manual-only under
`FASTXLSX_BUILD_BENCHMARKS`, not default CTest/CI, and does not prove
large-file low-memory random editing or Office compatibility until a separate
open check is actually run.
`WorksheetEditor::set_cells()` and `set_cell_values()` now use internal
`CellStore` batch preflight + direct commit instead of cloning the full sparse
store for failure atomicity. This reduces batch mutation latency and peak
working set for current small-file in-memory editing, but the end-to-end editor
path is still dominated by source worksheet materialization and `save_as()`.
Large-file editing should therefore continue through worksheet event
reader/transformer streaming Patch work, not by raising in-memory materialized
worksheet limits.
That same internal sparse-edit commit path now also backs existing
`WorksheetEditor` append, row/column replacement, row/column value writes,
row/column clear, range clear/erase, and coordinate-batch clear/erase paths
where those APIs previously cloned or incrementally rewrote the whole
materialized sparse store. This is an implementation and performance-boundary
cleanup only: no new public API is added, existing missing-target no-op
semantics are preserved, and the editor remains the small-file In-memory path.
`fastxlsx_bench_package_editor_cell_replacement` now provides the matching
opt-in local performance probe for that internal Patch direction. It generates
a stored source package whose worksheet entry is assembled from prefix +
file-backed row body + suffix, opens it through internal `PackageEditor`, runs
`replace_worksheet_cells_by_name()`, saves the edited package, and verifies the
output through `PackageReader::entry_chunk_source()` without materializing the
rewritten worksheet. The 2026-06-25 local release snapshot covers 1M / 3M / 5M
source cells with 1000 / 3000 / 5000 edits respectively; all three reports show
`source-zip-entry-chunk-source`, `file-backed-stream-rewrite`,
`staged_replacement_chunks=true`, and `materialized_replacement=false`. The 5M
case recorded `total_edit_ms=17647`, `patch_plan_ms=14609`,
`save_ms=3037`, and `peak_memory_mb=7.31`. This is evidence for the internal
PackageEditor cell-replacement Patch path only; it is still not a public
large-file random editing API, not sharedStrings/styles migration, not
relationship repair, not Zip64 proof, and not broad Office compatibility proof.
After the targeted replacement lookup fast path, the same tool now has a
public-facade matrix for 1M / 3M / 5M cells with 1000 / 3000 / 5000 edits:
the 5M public run recorded `total_edit_ms=17979`, `patch_plan_ms=14952`,
`save_ms=3026`, `peak_memory_mb=8.77`, `package_entry_source_mode` as
`source-zip-entry-chunk-source`, `output_entry_mode` as
`file-backed-stream-rewrite`, and `output_verified=true`. Internal 5M output-plan
evidence in the same run recorded `total_edit_ms=18482`,
`plan_reports_source_entry_chunk_source=true`,
`plan_reports_file_backed_stream_rewrite=true`,
`output_plan_staged_replacement_chunks=true`, and
`output_plan_materialized_replacement=false`. These are opt-in local benchmark
facts, not default CI gates or Office compatibility proof.
The dedicated Excel sidecar
`tools/verify_package_editor_cell_replacement_benchmark_excel.ps1` now opened
the same three local outputs read-only with Excel 16.0 and verified the `Data`
UsedRange, `A1` replacement value, and tail source cell; it writes
`package-editor-cell-replacement-office-report.json` and intentionally leaves
the benchmark JSON `office_open="not_run"`.
`WorksheetEditor::clear_row()` / `clear_rows()` and `clear_column()` /
`clear_columns()` now cover row/column value-only clear convenience for small
files: they keep represented sparse records, convert their values to explicit
blanks, preserve each source style handle, treat missing-only inputs as
successful no-ops, and stage the mutation before replacing the active sparse
store. They do not add row/column deletion, row/column shifting, row/column
metadata editing, dense range editing, tombstone output, style
migration/merge/creation, relationship repair, or large-file low-memory random
editing.
The saved clear outputs are reopened in public-state coverage to verify clean
readback for explicit blanks, preserved source style ids, and non-target sparse
cells after row and column clears.
`WorksheetEditor::erase_row()` and `WorksheetEditor::erase_rows()` now cover
the sparse row delete convenience for small files: they delete only represented
active sparse records from a single row or inclusive row range, treat missing
rows / missing-only ranges as successful no-ops, and stage deletion before
replacing the active sparse store. They do not add row deletion, row shifting,
row metadata editing, dense range deletion, tombstone output, table/range
metadata recalculation, relationship repair, or large-file low-memory random
editing.
`WorksheetEditor::erase_column()` and `WorksheetEditor::erase_columns()` now
cover the symmetric sparse column delete convenience for small files: they
delete only represented active sparse records from one column or inclusive
column range, treat missing columns / missing-only ranges as successful no-ops,
and stage deletion before replacing the active sparse store. They do not add
column deletion, column shifting, column metadata editing, dense range deletion,
tombstone output, table/range metadata recalculation, relationship repair, or
large-file low-memory random editing.
The saved erase outputs are reopened in public-state coverage to verify clean
readback for removed coordinates, remaining source-backed cells, and remaining
dirty sparse cells after row and column erases.
The inclusive row/column range erase saves now also snapshot catalog/save-state
before a clean no-op `save_as()`, proving pending counts, replacement
diagnostics, erased range absence, non-target dirty cells, and reopened bounds
stay stable after the first materialized flush.
The shared renamed no-op-save helpers also snapshot
`pending_worksheet_edits()` so rename/full-calculation formula-audit and
rename-backed clean-shift paths preserve public edit summaries across no-op
saves.
The common public save-state snapshot now includes the same edit-summary vector,
so every `check_workbook_editor_public_save_state_preserved()` no-op regression
also verifies summary stability instead of only pending counts and replacement
diagnostics.
The same snapshot now also captures `has_pending_changes()` and materialized
pending diagnostics, so existing save-state preservation checks pin the public
dirty boolean and materialized aggregate names/counts/memory across no-op saves
without per-test duplicate assertions.
The basic saved-session reacquire no-op save paths now pair that save-state
snapshot with a catalog snapshot, covering handle reuse, `worksheet()` /
`try_worksheet()` reacquire, and row/column sparse-shift projections without
claiming broader catalog mutation support.
The failed-save retry saved-reacquire no-op paths now use the same catalog
snapshot around the final clean save after rejected outputs, covering
source-overwrite and invalid output-path failures while keeping rollback and
repair out of scope.
The path-equivalent source-overwrite branch now carries the same repeat no-op
save coverage: after the safe retry and first clean no-op output, a second
no-op `save_as()` keeps all shared handles clean, preserves public catalog/save
state, reopens with shifted `A3` / `C1`, and remains unchanged after the later
post-noop `C3` save.
That path-equivalent post-noop save now also keeps the safe retry package
byte-stable and fresh-reopens with row/column snapshots for shifted `A3`,
shifted `C1`, and the later `C3` edit.
The empty-output failed-save branch now mirrors that repeat no-op boundary: the
safe retry remains reusable after rejecting an empty output path, the first and
second clean no-op packages stay byte-identical, and the later post-noop `C3`
save leaves both no-op outputs plus the retry output unchanged.
That empty-output post-noop save now also fresh-reopens with row/column snapshots
for shifted `A3`, shifted `C1`, and the later `C3` edit.
The remaining invalid output-path failed-save branches now mirror that repeat
no-op boundary too: missing-parent, non-directory-parent, and existing-directory
outputs preserve their rejected target state, reopen the second no-op output
with shifted `A3` / `C1`, and keep the retry plus both no-op outputs unchanged
after the later post-noop `C3` save.
The missing-parent post-noop save now also fresh-reopens with row/column
snapshots for shifted `A3`, shifted `C1`, and the later `C3` edit while keeping
the rejected output path absent.
The non-directory-parent and existing-directory post-noop saves now carry the
same final reopen row/column snapshots and keep their safe retry packages
byte-stable while preserving the rejected file/directory targets.
The renamed full-calculation formula-audit saved-reacquire no-op paths now also
pair their second clean save-state snapshot with a catalog snapshot after
invalid mutation/read/shift, missing-query, option-mismatch, and same-sheet guard
checks, and now reopen the final clean no-op outputs for those recovery branches
to re-verify renamed-sheet readability without expanding formula repair or
metadata synchronization.
The early renamed shift-after-rename regressions now also pin aggregate dirty
materialized memory diagnostics against the active `WorksheetEditor` estimate
and verify they clear after the materialized flush, without changing guardrail
or low-memory editing policy.
The aggregate diagnostics coverage now also checks that queued replacement
memory remains visible through `estimated_pending_replacement_memory_usage()`
while replacement-only editors still contribute zero materialized cells and
memory.
Pending materialized summary move coverage now carries the same memory estimate
through `WorkbookEditor` move construction and move assignment, so summary
diagnostics preserve both cell count and memory count.
Full-calculation formula-audit dirty-summary coverage now pins the same shifted
fixture memory estimate in both aggregate diagnostics and edit summaries.
The direct shifted formula-audit setup path now carries that same memory estimate
before formula-audit reads, matching the later source-formula audit coverage
without changing formula repair or calculation behavior.
The renamed formula-audit preserve-state helpers now carry that memory estimate
through option, missing-query, invalid-read, invalid-mutation, invalid-shift,
and diagnostic-recovery checks as well.
The saved-reacquire renamed formula-audit recovery branches now use the same
rename-aware memory-summary helper after valid recovery mutations, covering
invalid mutation/read/shift, missing query, option mismatch, and same-sheet guard
paths before their second save.
The renamed row/column formula-audit preflight tests now snapshot dirty
materialized memory before audit reads and require materialization guard
failures, recovery materialization, invalid mutations, and invalid shifts to
preserve that memory alongside the dirty sparse count.
The renamed styled-formula row/column shift and formula-audit paths now also
pin pre-materialization public state: after the catalog rename and before
`WorksheetEditor` acquisition, only the planned sheet name is visible, only the
catalog rename is counted, and replacement/materialized diagnostics remain
empty. This is diagnostics hygiene only, not broader formula repair or
metadata migration.
The same pre-materialization diagnostics contract now covers delete-side
renamed styled-formula and formula-audit paths as well, including basic
delete-row/delete-column formula shifts and delete-row/delete-column
formula-audit `#REF!` scenarios.
Insert-row renamed styled-formula recovery/reacquire paths now share that
contract too: reacquire, failed-save, option-mismatch, invalid-mutation,
missing-query, invalid-read, and snapshot-read paths all prove rename-only
state stays free of replacement/materialized diagnostics before materialization.
Those insert-row renamed styled-formula recovery paths also pin saved-session
reacquire diagnostics before the later column shift, proving the first saved
handoff stays clean until the next mutation.
Delete-row renamed styled-formula recovery paths now share the same saved-session
reacquire diagnostics contract before their later column shift.
Delete-column renamed styled-formula recovery paths now share that contract
before their later row shift as well.
Insert-column renamed styled-formula and formula-audit paths now also pin
saved-session reacquire diagnostics before their next read or audit.
Delete-side renamed formula-audit `#REF!` paths now pin the same saved-session
reacquire diagnostics while preserving the existing surviving-reference audit
behavior.
Insert-row direct renamed styled-formula and formula-audit paths now mirror that
saved-session diagnostics contract before their next read or audit.
Saved-session formula-audit reacquire paths now pin post-save mutation memory in
both aggregate materialized diagnostics and edit summaries, including the
failed-save retry route.
The same-editor materialized-only formula audit path now also saves again after
source/materialized audit inspection, requiring clean diagnostics, stable public
save/catalog snapshots, byte-identical package entries, and saved formula
readback.
The fresh-reopen materialized-only formula audit path now has the matching
clean no-op save coverage after source/materialized audit reads, proving the
newly opened editor keeps pending state empty and writes byte-identical output.
Both materialized-only formula no-op output reopen paths now also re-check clean
editor diagnostics after row/column snapshot reads, proving snapshots do not
re-dirty the saved session.
The stationary formula row-insert saved-reopen audit path now also no-op saves
after verifying the translated `Data!A4` / stable `Data!B1` references, keeping
the old `Data!A3` reference absent and output bytes stable.
The matching delete-row saved-reopen audit path now no-op saves after verifying
`Data!#REF!+Data!B1`, keeping `#REF!` skipped from audits, preserving only
`Data!B1`, and requiring byte-identical output.
The column-insert saved-reopen audit path now applies the same no-op save
coverage after verifying translated `Data!E1` / stable `Data!B1` references,
keeping the old `Data!D1` reference absent and output bytes stable.
The delete-column saved-reopen audit path now mirrors the delete-row no-op
coverage for `Data!#REF!+Data!B1`, keeping `#REF!` skipped from audits,
preserving only `Data!B1`, and requiring byte-identical output.
The four stationary row/column insert/delete saved-reopen no-op outputs now
also re-check clean editor diagnostics after row/column snapshot reads, proving
the snapshot helpers do not re-dirty those saved sessions.
The row-range and column-range saved-reopen audit paths now also no-op save
after verifying translated saved ranges (`Data!A4:B4` / `Data!4:4` and
`Data!E1:F1` / `Data!E:F`), with old ranges absent and output bytes stable.
The mixed delete-row/delete-column `#REF!` saved-reopen audit paths now also
no-op save after preserving surviving references (`Data!A:A` / `Data!B3` and
`Data!1:1` / `Data!C2`) while skipping persisted `Data!#REF!` tokens.
Those range and mixed `#REF!` no-op output reopen paths now also re-check clean
editor diagnostics after row/column snapshot reads, closing the same saved
session snapshot-hygiene gap.
The renamed full-calculation formula no-op output helpers now also re-check
clean editor diagnostics after C5 and shifted source-row readback, keeping
those saved-session reads in the same no-dirty snapshot-hygiene contract.
The adjacent saved/reacquired recovery no-op outputs now repeat that clean
diagnostic check after reopened C5 readback, including invalid-diagnostic,
post-save, and failed-save retry paths.
Early shift-after-rename coverage now applies the same memory-summary checks to
the direct planned-name, saved-session reacquire, and option-mismatch paths.
The direct shift-after-rename reopened-output check now also verifies clean
materialized names/count/memory before reading shifted cells from the saved
package.
The saved/reacquired styled rename-shift path now also reopens its final clean
no-op output, so the byte-stable save is backed by fresh readback evidence for
the renamed sheet, translated styled formula, and shifted sparse records.
Styled formula rename-shift coverage now carries the same materialized memory
diagnostics through reopened clean output and saved/reacquired shared-session
column shifts.
The adjacent failed-save retry and option-mismatch styled-session paths now also
pin clean materialized memory after safe saves/rejected option access, dirty
memory after the later shared shift, and reopened clean diagnostics.
The failed-save retry path now also reopens its final clean no-op output, so the
safe retry's byte-stable save has fresh readback evidence for the renamed sheet,
translated styled formula, shifted source cells, and absent old coordinates.
Those direct reacquire, failed-save, and option-mismatch no-op reopen checks now
also re-check clean editor diagnostics after shifted sparse formula/source
readback, proving the saved-output reads do not re-dirty the session.
The adjacent invalid-mutation, missing-query, and invalid-read no-op reopen
checks now carry the same post-read clean diagnostic assertion after shifted
sparse formula/source readback.
The option-mismatch path now mirrors that final no-op reopen check after its
later shared-session column shift, including clean diagnostics on the reopened
workbook.
The invalid-mutation recovery path now mirrors the final no-op reopen check
after rejected formula mutations and the later valid shared-session shift.
The missing-query recovery path now does the same after rejected missing/old-name
lookups and the later valid shared-session shift.
The invalid-read recovery path now mirrors that no-op reopen check after
rejected scalar, A1, range, batch, row/column, and valid-missing reads followed
by the later valid shared-session shift.
The snapshot-read path now mirrors the no-op reopen check with fresh
`row_cells()` / `column_cells()` readback of the translated styled formula
output.
The delete-column failed-save retry path now has matching no-op reopen evidence
for the translated styled `C2` formula, shifted source cells, and absent
old/deleted coordinates.
The delete-column option-mismatch path now mirrors that no-op reopen evidence
after the matching reacquire and recovery row shift.
The delete-column invalid-mutation and missing-query paths now have the same
fresh no-op reopen coverage after their recovery row shifts.
The delete-column invalid-read and snapshot-read paths now also fresh-reopen
their final clean no-op outputs after the recovery row shift, pinning the
translated styled `C3` formula, shifted source-backed cells, row/column
snapshot readback, and absent old coordinates.
The delete-column reacquire path now mirrors that final no-op output readback
after the clean saved-session reacquire and later recovery row shift.
The matching delete-row failed-save retry path now fresh-reopens its byte-stable
no-op output too, pinning the translated styled `D1` formula, shifted
source-backed rows, clean diagnostics, and absent old coordinates.
The delete-row option-mismatch, invalid-mutation, missing-query, invalid-read,
snapshot-read, and reacquire styled-session paths now mirror the same no-op
output readback after their later column shifts; the snapshot-read case also
checks fresh `row_cells()` / `column_cells()` views of the translated styled
`E1` formula output.
The non-styled renamed planned-session paths now get the same no-op output
readability check across reacquire reuse, failed-save retry, option mismatch,
missing query, invalid reads, and invalid mutations: each fresh-reopens the
byte-stable no-op output and verifies the combined `A1:C3` shifted state.
The single-sheet and multi-sheet materialized reopen/modify/no-op loops now
also fresh-reopen their prior no-op outputs after later third-stage saves,
pinning that subsequent edits do not corrupt already-written clean no-op files.
The baseline multi-sheet materialized no-op path now repeats the clean no-op
save too, keeping all live handles clean, package entries byte-stable, the
first no-op output unchanged, sources unchanged, and both worksheets freshly
reopenable from the second no-op output.
The matching failed-save retry path now repeats the clean no-op save as well,
proving rejected source overwrite recovery leaves first and later no-op
packages byte-stable while staged Patch handoff state and fresh two-sheet
readback stay unchanged.
The multi-sheet retry reopen/modify/no-op loop now repeats its clean no-op save
before the later third-stage edits too, proving both no-op packages remain
readable and byte-stable after subsequent materialized edits are flushed.
The matching invalid-mutation and missing-query styled-session paths now carry
the same clean/dirty/reopened materialized memory checks through their recovery
shifts.
The invalid-read and snapshot-read styled-session paths now carry the same
clean/dirty/reopened materialized memory checks through read-only diagnostics
and recovery shifts.
The delete-column failed-save retry and option-mismatch styled-session paths now
pin the same materialized memory cleanup and recovery-shift diagnostics.
The matching delete-column invalid-mutation and missing-query styled-session
paths now carry those clean/dirty/reopened diagnostics through rejection and
recovery shifts.
The delete-column invalid-read and snapshot-read styled-session paths now pin
the same clean/dirty/reopened materialized memory diagnostics through read-only
checks and recovery shifts.
The delete-column reacquire styled-session path now pins matching clean
reacquire, dirty recovery-shift, no-op save, and reopened materialized memory
diagnostics.
The delete-row direct and failed-save styled-session paths now pin reopened and
safe-retry clean materialized memory diagnostics.
The delete-row option-mismatch styled-session path now pins clean rejected
option access, dirty recovery-shift, no-op save, and reopened materialized
memory diagnostics.
The delete-row invalid-mutation and missing-query styled-session paths now carry
the same clean rejected-operation, dirty recovery-shift, no-op save, and
reopened materialized memory diagnostics.
The delete-row invalid-read and snapshot-read styled-session paths now carry
matching clean/dirty/reopened materialized memory diagnostics through read-only
checks and recovery shifts.
The delete-row reacquire styled-session path now pins matching clean reacquire,
dirty recovery-shift, no-op save, and reopened materialized memory diagnostics.
The non-styled rename-shift saved-session reacquire path now pins first/second
save cleanup and reopened clean materialized memory diagnostics while keeping
the existing dirty shared-session shift memory check.
The adjacent non-styled failed-save retry path now also pins clean materialized
memory after safe saves/reopens and dirty memory for the post-retry delete-row
mutation.
The non-styled option-mismatch path now pins clean materialized memory across
the rejected mismatched options, second save, no-op save, and reopened output.
The non-styled missing-query path now carries the same clean materialized
diagnostics through missing/old-name lookups and adds aggregate dirty memory
for the recovery column shift.
The non-styled invalid-read and invalid-mutation paths now carry matching clean
materialized diagnostics through rejected reads/mutations and add aggregate
dirty memory for their recovery column shifts.
The basic shift handle-reuse path now pins clean aggregate materialized
names/count/memory after both successful `save_as()` calls before the no-op
save stability check.
The basic shift reacquire path now pins clean materialized memory after first
and second saves and aggregate dirty memory for the shared recovery column
shift.
The `try_worksheet()` shift reacquire path now pins the same first/second save
cleanup and aggregate dirty memory for its shared recovery column shift.
Saved-session option-mismatch, missing-query, invalid-read, and
invalid-mutation reacquire paths now pin clean materialized memory around
rejected access and aggregate dirty memory for their recovery column shifts.
The saved-session failed-save retry path now also pins clean materialized
memory after the initial save and safe retry while preserving existing dirty
memory checks around the rejected source-overwrite save.
The failed-save retry reacquire path now pins clean materialized memory after
first/safe/third saves and clean reacquire, plus dirty memory for the
post-retry delete-row mutation.
The rejected-output failed-save variants now pin clean materialized memory after
their initial saves and safe retries for path-equivalent source, empty output,
missing parent, file parent, and directory output cases.
Base sparse row/column shift coverage now pins aggregate dirty materialized
memory diagnostics for direct insert/delete row and column shifts before save.
Base append/set row/column coverage now pins the same aggregate dirty
materialized memory diagnostics for direct sparse mutations before save.
Base clear row/column coverage now pins aggregate dirty materialized memory
diagnostics after represented cells become explicit blanks.
Base erase row/column coverage now pins aggregate dirty materialized memory
diagnostics after represented sparse records are removed.
Whole-store clear/erase reacquire coverage now pins aggregate dirty
materialized memory diagnostics before both the first clear save and later
reused-session follow-up saves.
Direct styled-formula delete-shift coverage now pins aggregate dirty
materialized memory diagnostics alongside `StyleId` preservation and `#REF!`
translation checks.
Full-calculation insert-row setup coverage now pins aggregate dirty
materialized memory before `request_full_calculation()` queues workbook
metadata.
Cross-handle row/column shift and delete coverage now pins aggregate dirty
materialized memory across the mutated `Data` handle and the untouched dirty
handle, including save-time cleanup.
Invalid-to-valid row/column shift recovery coverage now pins aggregate dirty
materialized memory for both clean recovery and already-dirty recovery paths.
Formula-translation shift coverage now pins dirty materialized count and memory
before saving rich reference-shape and out-of-bounds `#REF!` formula shifts.
The rich reference-shape shift readback now also pins `row_cells()` and
`column_cells()` snapshots after fresh reopen and post-noop saves, so saved
insert-row, insert-column, delete-row, and delete-column formula outputs expose
the same sparse ordering as `try_cell()` reads.
Formula-audit fresh-reopen helpers now also re-check clean editor diagnostics
immediately after styled formula `try_cell()` readback, before running the
materialized audit scan.
API docs and the editing model now explicitly spell out that `WorksheetEditor`
read/snapshot APIs are non-flushing sparse-store inspections and cannot queue
Patch handoffs, expose `EditPlan`, synthesize dense cells, or dirty clean saved
sessions.
Shift guard/no-op/overflow coverage now pins aggregate materialized memory for
clean no-op/validation paths and dirty overflow rejection paths.
Shift formula memory-budget failure coverage now pins aggregate materialized
memory at zero after rejected formula translation.
The clean side of those shift guard paths now also pins empty pending
summaries, zero handoff count, and empty replacement diagnostics for
zero-count no-ops, nonzero out-of-range no-ops, validation failures, and
formula-translation memory-budget failures.
Dirty row/column overflow rejection coverage now also pins the retained dirty
materialized summary and preserved shift diagnostic before the later safe save.
Full-calculation-before-shift setup coverage now also pins dirty materialized
memory at zero after `request_full_calculation()` and after clean worksheet
materialization, before insert/delete column shifts dirty the session.
Source-load `max_cells` guard failure coverage now pins materialized names,
dirty cell count, and materialized memory at zero after rejected source
materialization.
Mutation guard recovery coverage now pins aggregate materialized memory after
the valid overwrite that follows rejected memory-budget and `max_cells`
mutations.
Guardrail failure and recovery no-op coverage now also pins empty pending
summaries and empty replacement diagnostics for source-load `max_cells` /
memory-budget failures, mutation-side memory-budget / `max_cells` failures,
and the materialized recovery no-op saves.
Mutation-side memory-budget / `max_cells` recovery now also repeats the
same-budget reacquired clean no-op `save_as()` before the later overwrite,
keeping entries byte-stable, public state clean, and saved inputs unchanged.
Guardrail recovery overwrite coverage now also pins the dirty `Data`
materialized summary before save for source-load memory-budget recovery and
mutation-side memory-budget / `max_cells` recovery.
Source-load memory-budget recovery coverage now pins recovered materialized
dirty count and memory before the first recovery save.
Explicit-blank guard recovery coverage now pins aggregate dirty materialized
memory after successful existing-cell blank overwrites.
Last-edit-error replacement recovery coverage now pins aggregate materialized
memory after the successful overwrite that follows replaced failure diagnostics.
Mixed diagnostic recovery coverage now pins replacement-only recovery isolation
from clean materialized aggregate count and memory.
Erase budget-release recovery coverage now pins aggregate materialized memory
for reduced sparse sessions and later replacement insertions.
The same opt-in workbook-editor QA runner now also has an external image
fixture smoke path: `external_fixture_image_replace_smoke` scans caller
fixtures for `xl/media/*.png|jpg|jpeg`, selects the worksheet containing the
image, replaces that media part through the narrow `WorkbookEditor::replace_image()`
path, and verifies the output with ZIP/XML, `openpyxl`, and optional Excel COM.
The current local xlnt evidence uses `C:\Users\wuxianggujun\CodeSpace\CMakeProjects\xlnt\tests\data\14_images.xlsx`
and confirms a single `xl/media/image1.jpg` replacement while preserving the
rest of the package bytes. This is QA coverage only; it is not a broadened
image-editing API, not default CI, and not a general preservation claim for all
embedded drawing models.
The same source materialization path now reads ordinary formula cells and
source-order shared formula followers into `CellValue::formula(...)`. Shared
formula followers use a narrow A1-style / whole-axis relative-reference
translator from the definition cell to the follower cell; `$` absolute
row/column anchors are kept, whole-row/whole-column ranges are translated within
Excel bounds, out-of-bounds relative references become `#REF!`, and quoted strings, quoted
sheet-name tokens, and bracketed external/structured-reference tokens are not
rewritten. The internal tokenizer now also pins comparison operators
(`<=`, `>=`, `<>`), array-constant punctuation, leading-decimal/exponent
number tokens, and malformed string/bracket recovery spans for audit safety.
The internal scanner exposes raw sheet qualifier spans for unquoted, quoted,
external-workbook, and 3D-like qualifiers as dependency-audit metadata, and the
shared `classify_formula_reference_qualifier()` helper now centralizes
unqualified/local/external/3D/external-3D classification for audits and safe
sheet-name rewrites. It still does not validate sheet names, external workbook
targets, or 3D semantics. Dirty `WorksheetEditor` save writes ordinary
`<f>...</f>` formula
text, treats formula text as authoritative for default/numeric, `t="str"`, and
`t="b"` cached-result formula cells, drops stale cached results, and still does
not preserve shared formula metadata, evaluate formulas, rebuild calcChain, or
implement a complete Excel formula parser. Unresolved metadata-only shared
formula cells continue to fall back to supported cached scalar `<v>` values when
present. The narrow scanner/translator now lives in internal
`include/fastxlsx/detail/formula.hpp` / `src/formula.cpp`, with
`fastxlsx.formula` covering scanner boundaries, raw sheet qualifier spans, and
reference translation. Structural rewrite coverage now also pins quoted and
external-workbook qualifiers through row deletion / column insertion while
leaving structured references and string literals untouched; this is a reusable
foundation for later dependency graphing, sheet-rename formula sync, and
calcChain policy work, not an in-process formula evaluator. The same structural
rewrite guard now covers 3D and external-3D qualifier text as lexical boundaries,
without claiming local sheet semantics or cross-workbook formula synchronization.
The representative source-formula dirty-save outputs now also fresh-reopen
through the public `WorkbookEditor` facade: ordinary formulas with stale cached
values, source error cells, cached-result type variants, and source-order shared
formula followers reopen as clean sparse `WorksheetEditor` state with formula
text / error tokens / later inline edits intact and dirty diagnostics clear.
This is saved-output readback only; it is not formula evaluation, cached-value
generation, broad parser coverage, shared formula metadata preservation, or
calcChain rebuild.
The public editor now exposes the first narrow read-only dependency diagnostic
on top of that scanner: `WorkbookEditor::formula_reference_audits()` scans only
already-materialized `WorksheetEditor` sessions, reports sheet-qualified
formula references including the raw reference token and whether they still
name a source sheet after
`rename_sheet()` changed that sheet's planned name, and intentionally does not
scan non-materialized worksheet parts or rewrite formulas.
It now also classifies external-workbook and 3D sheet-range qualifiers as
audit-only cases so they are not mistaken for one local workbook sheet.
The source-package side of that diagnostic is now separate:
`WorkbookEditor::source_formula_reference_audits()` streams source worksheet XML
parts through the internal event reader and reports explicit `<f>...</f>`
formula text without materializing `WorksheetEditor` sessions. It gives
rename-risk visibility for non-materialized worksheets, but still does not
rewrite those formulas, resolve out-of-order metadata-only shared formula
followers, evaluate formulas, or build a dependency graph. Source-order
metadata-only shared formula followers are expanded only when the corresponding
shared formula definition has already been seen.
The opt-in workbook-editor QA layer now exposes that boundary directly through
`generated_source_formula_audit`: the generated case calls the source audit
before/after a `Data` -> `RenamedData` rename, verifies one stale source-name
formula risk plus external-workbook and 3D sheet-range audit classification,
and checks the saved workbook still keeps the non-materialized formula text
unchanged.
That generated source-audit lane now also has a no-op save variant requiring the
post-rename clean `save_as()` output to remain byte-identical, without changing
the audit-only formula boundary or repairing stale source qualifiers.
The same audit boundary now extends to current workbook defined names through
`WorkbookEditor::defined_name_formula_reference_audits()`: it materializes the
small `xl/workbook.xml` metadata part from the current planned editor state
when a workbook rewrite is queued, otherwise from the source package, scans
direct `<definedNames><definedName>` formula text, reports exact
sheet-qualified reference tokens, maps ordinary sheet qualifiers against the
current source-to-planned catalog, and classifies external-workbook / 3D
sheet-range qualifiers without local-sheet matching. It still does not rewrite
defined names, validate external targets, interpret 3D semantics, or
implement a formula dependency graph.
The audit scanner/matcher and definedName extraction logic now live behind the
internal `detail/formula_reference_audit` semantic API instead of being embedded
in the public `WorkbookEditor` facade implementation.
The same internal semantic API now has a first conservative formula-reference
rewrite foundation: `rewrite_formula_sheet_references()` rewrites only local
sheet-qualified formula references to quoted replacement sheet qualifiers,
skips external-workbook qualifiers, 3D sheet-range qualifiers, structured
references, and quoted string text, and rejects ambiguous rewrite rules instead
of guessing. `rewrite_workbook_defined_name_formula_references()` applies that
same narrow rewrite to direct workbook definedName formula text while preserving
unchanged workbook XML bytes. The first explicit public policy hook now exists:
`WorkbookEditor::rename_sheet(old, new, WorkbookEditorRenameOptions{...})` can
opt into `WorkbookEditorRenameFormulaPolicy::RewriteDefinedNames` to rewrite
direct workbook definedName formula references during the small `xl/workbook.xml`
rewrite, or opt into
`WorkbookEditorRenameFormulaPolicy::RewriteDefinedNamesAndMaterializedWorksheetFormulas`
to rewrite both those direct definedName references and formula cells already
loaded into `WorksheetEditor` materialized sessions during the same public
rename. In a rename chain, those opt-in paths rewrite references that still use
the sheet's original source name as well as references that use the current old
planned name, while avoiding no-op source-name rewrites when the chain returns
to the source name. The default `rename_sheet(old, new)` remains catalog-only
and still
does not rewrite non-materialized worksheet formula cells, tables, drawings,
charts, hyperlinks, relationships, calcChain, external-workbook references, or
3D sheet ranges.
The `WorkbookEditor` implementation has now been split along semantic
boundaries instead of file-only churn: `src/workbook_editor_state.hpp` owns the
private editor state and catalog/pending-summary projections,
`src/workbook_editor_worksheet_facade.cpp` owns the public `WorksheetEditor`
handle methods, and `src/workbook_editor_testing_hooks.cpp` owns
`FASTXLSX_ENABLE_TEST_HOOKS` materialized-session helpers. The remaining
`src/workbook_editor.cpp` is now primarily the public `WorkbookEditor` facade
and cross-feature orchestration. `fastxlsx.workbook_editor_state` covers the
state projection helpers directly; this is architecture hardening, not a new
public API surface.
The definedName formula audit scanner now also rejects mismatched or unclosed
workbook XML tags instead of silently draining the element stack. This keeps
definedName diagnostics fail-fast for malformed workbook metadata; it is not
XML repair, schema validation, formula evaluation, or definedName rewrite.
Array and dataTable formula metadata now follows the same lossy materialization
boundary: source formula text in `<f t="array">` / `<f t="dataTable">`
materializes as plain formula text, metadata-only cells fall back to supported
cached scalar values, and dirty projection does not preserve array/dataTable
formula metadata or stale cached formula results. This is not dynamic array
spill support, data table recalculation, or formula dependency graphing.
The consolidated formula capability matrix is now tracked in
`docs/FORMULA_SUPPORT.md`: FastXLSX supports formula-compatible XLSX editing,
not embedded Excel formula evaluation.
The current regression matrix now also pins multiple followers per `si`,
interleaved shared formula indexes, latest source-order definition behavior for
later followers, function/name-like token boundaries, structured-reference and
whole-row/whole-column range translation, escaped quoted sheet qualifiers,
raw 3D-like sheet qualifier spans, invalid shared formula index forms, public
`WorksheetEditor` clean readback, and dirty save projection without stale
cached formula values. The opt-in workbook-editor QA runner includes
`generated_shared_formula_materialization`, which creates a generated shared
formula source workbook, verifies materialization through the public C++ tool,
then checks the output with ZIP/XML, `openpyxl`, and the Excel COM verifier; it
also includes
`generated_shared_formula_boundary_materialization`, which pins quoted strings,
structured references, name-like tokens, R1C1-like text, whole-row/whole-column
references, bracketed tokens, and sheet-qualified A1 / whole-axis translation
boundaries in a generated source/output smoke; the boundary lane now also has a
no-op save variant requiring the follow-up clean `save_as()` package to be
byte-identical. That boundary case remains ZIP/XML and `openpyxl` validation
only because some tokens are deliberately synthetic parser-boundary inputs, not
an Excel UI compatibility smoke. It also includes
`generated_shared_formula_office_like_materialization`, which pins 2D shared
formula `ref` ranges, multiple `si` groups in one worksheet, ordinary formulas
and values interleaved with shared formula followers, and stale cached formula
result cleanup; the dirty output is checked as ordinary formula elements with
0 shared formula metadata elements, and is opened by the Excel COM verifier.
The generated shared-formula and Office-like materialization lanes now also
have no-op save variants that require the follow-up clean `save_as()` package
to be byte-identical after the dirty materialized output has flushed.
P8.587 strengthens those generated shared-formula QA reports without changing
runtime behavior: each generated shared-formula scenario now records
`checked_formula_cells`, `output_formula_cells`, `openpyxl.formula_cells`,
`formula_output.shared_metadata_removed`, `cached_formula_values_removed`, and
whether the scenario is an Excel UI smoke or a synthetic parser-boundary ZIP/XML
smoke. This makes shared metadata removal, stale cached-value cleanup, and exact
materialized formula text reviewable directly from `report.json`; it is still
not formula evaluation, cached result generation, calcChain rebuild, or a
complete Excel formula parser.
The same Office-like shape is now also covered by the default public
`fastxlsx.workbook_editor.source-success` CTest path, so shared formula
materialization regressions are not limited to opt-in local QA. It also has an
opt-in
`external_source_formula_fixture_audit_smoke` scanner that maps formula-bearing
fixture worksheets to source worksheet XML parts and invokes the read-only C++
`fixture_source_formula_audit` mode, recording source formula audit counts,
rename-risk counts, external-workbook counts, 3D sheet-range counts, and local
match counts. This fixture smoke intentionally allows zero audit references
when a workbook contains only unqualified formulas; the generated
`generated_source_formula_audit` case is the deterministic nonzero assertion.
Current local xlnt read-only evidence covers `18_formulae.xlsx:Sheet1` with 15
formula elements, 3 shared formula elements, 1 definition, 2 metadata-only
followers, and 0 source formula audit references because the fixture formulas
are unqualified.
The 2026-06-22 minizip rerun also covered
`10_comments_hyperlinks_formulae.xlsx:Sheet1`,
`10_comments_hyperlinks_formulae.xlsx:Sheet2`, and
`18_formulae.xlsx:Sheet1` in read-only source formula audit mode; all three
passed with no rename-risk, external, 3D, or local-match references.
It also has an opt-in
`external_formula_fixture_materialized_smoke` scanner that maps workbook sheet
names to worksheet XML parts, records formula/shared-formula counts, and runs
the materialized edit smoke on the exact formula-bearing sheet; `--formula-shared-only`
narrows the run to worksheets with shared formula metadata. Current local xlnt
evidence includes `18_formulae.xlsx:Sheet1` with 15 formula elements, 3 shared
formula elements, 1 definition, and 2 metadata-only followers; the dirty output
target sheet keeps 15 ordinary formula elements and 0 shared formula metadata
elements. The 2026-06-22 minizip rerun reproduced this result and Excel COM
opened the output successfully. The runner now also includes
`external_defined_name_fixture_smoke`: it scans caller-provided fixture
workbooks for direct `xl/workbook.xml` `definedNames`, records workbook-scoped
and `localSheetId` scoped counts plus external/3D-like reference indicators,
runs a materialized-only edit smoke, and verifies the definedName records remain
semantically preserved. Current local xlnt evidence covers
`19_defined_names.xlsx` (6 direct definedName records),
`Issue18_defined_name_with_workbook_scope.xlsx` (1 workbook-scoped record), and
`issue90_debug_test_file.xlsx` (3 local-sheet-scoped print-area records on the
Chinese sheet name `封面`), with ZIP/XML, `openpyxl`, and Excel COM passing.
The 2026-06-22 minizip rerun covered the current xlnt
`19_defined_names.xlsx` fixture: 6 direct definedName records were preserved
and Excel COM opened the output successfully.
The same formula/definedName fixture boundary was then rerun against temporary
Python-generated `openpyxl 3.1.2` and `XlsxWriter 3.2.0` workbooks with
sheet-qualified formulas and direct workbook `definedNames`. The aggregate
report `build\qa\python-writer-formula-fixtures-2026-06-22\report.json`
records 6 cases total: each source-audit case saw 5 formula cells, 6 qualified
references, 2 rename-risk references, and 6 local matches; each dirty
materialized rename output kept 5 ordinary formulas and 0 shared-formula
metadata; each definedName preservation case kept 3 direct records. Excel COM
reported `ok` for all output workbooks, and the temporary source fixture root
was removed after validation.
A focused definedName audit-count rerun now records the public
`WorkbookEditor::defined_name_formula_reference_audits()` output in the Python
fixture report. `build\qa\python-writer-defined-name-audit-2026-06-22\report.json`
covers temporary `openpyxl 3.1.2` and `XlsxWriter 3.2.0` workbooks with 3 local
sheet-qualified definedName references each; both cases report 3 audits, 3
current-workbook matches, and zero rename-risk / external-workbook / 3D
sheet-range references, with Excel COM status `ok`. The temporary source root
was removed after validation.
The scenario deliberately avoids sheet rename because it is a preservation
fixture smoke; the explicit direct definedName rewrite policy is covered by
unit tests instead.
xlnt/OpenXLSX samples remain caller-supplied `--fixture-root` inputs
rather than runtime dependencies or default CI fixtures. Current local
compatibility smoke also covers OpenXLSX benchmark fixtures, xlnt reference
fixtures, xlnt default smoke fixtures, and Python writer benchmark fixtures;
all of them roundtrip through the same narrow rename/materialize edit and Excel
COM verification. The runner now shortens fixture case slugs to avoid Windows
path-length failures on deeply nested fixture roots.
Source sharedStrings text with `xml:space="preserve"` is now pinned at the
public facade as read-only materialized whitespace: plain shared-string text and
simple rich shared-string runs keep leading/trailing whitespace in
`CellValue::text(...)`; dirty `WorksheetEditor` save still writes inline strings
with `xml:space="preserve"` where needed and preserves the source
`xl/sharedStrings.xml` bytes instead of writing back or pruning the table. This
is whitespace materialization/projection only, not rich text preservation,
sharedStrings rebuild, index migration, relationship repair, or a large-file
random-editing claim.
Malformed source sharedStrings item/rich-run structures are also pinned in the
public failure-hygiene suite: text outside `<t>`, nested `<si>`, nested markup
inside `<t>`, and mismatched closing tags fail through `try_worksheet()` /
`worksheet()` without dirtying materialized state, updating `last_edit_error()`,
or blocking later valid Patch edits. This is strict fail-fast hygiene, not XML
repair, schema validation, or broader rich-text support.
The ignored metadata boundary is now explicit as well: source sharedStrings
text under `rPh` / `phoneticPr` / `extLst`, including opaque nested metadata
and root-level `extLst`, is ignored during materialization and dirty inline
projection, while nested `<si>` decoys and markup inside text wrappers still
fail fast. This is not phonetic metadata import, extension object modeling,
sharedStrings writeback, or XML repair.
The same opacity rule now applies to source inline rich text: nested opaque
text under inline `rPh` / `phoneticPr` / `extLst` is ignored and omitted from
dirty projection, while nested `<si>` decoys or markup inside ignored metadata
text wrappers fail through the public and package-backed materialization paths.
This is not inline phonetic metadata import, extension object modeling,
rich-text preservation, or XML repair.
Self-closing ignored metadata is now pinned as the positive edge for both
source sharedStrings and inline rich text, while orphan closing tags and
unclosed ignored metadata fail fast through the same public/package-backed
paths. This prevents malformed metadata scopes from being guessed or balanced
against unrelated closing tags.
The source materialization contract is now indexed in
`docs/API_DESIGN_AND_DOCUMENTATION.md` under "Source dependency
materialization summary"; future source-loading slices should update that
summary instead of continuing to lengthen the F2 matrix row.
README and public Doxygen now point back to that summary as the source-loading
contract index, so future public wording changes should update the same
summary first instead of creating another divergent boundary description.
The sharedStrings text-wrapper markup boundary is now strict as well:
comments, processing instructions, CDATA, and other markup declarations inside
sharedStrings `<t>` wrappers fail fast instead of being silently dropped, and
CDATA / DOCTYPE-like declarations are not treated as a supported sharedStrings
text import path.
P8.488 also pins the sharedStrings XML declaration boundary: `<?xml ...?>`
after the sharedStrings root has started fails fast instead of being treated as
ordinary processing-instruction trivia, both inside `<si>` and after the root.
P8.489 extends that boundary to duplicate XML declarations before the root:
the second declaration fails fast instead of being skipped as another prolog
processing instruction. This is still not a full prolog ordering validator.
P8.490 adds the adjacent ordering edge: XML declarations after prolog comment
or ordinary processing-instruction trivia fail fast, while ordinary processing
instructions after a valid XML declaration remain trivia. P8.491 pins the
remaining XML-declaration-first edge by rejecting XML declarations after
leading whitespace text; sharedStrings payloads without an XML declaration can
still have root-before whitespace trivia. P8.492 adds the narrow declaration
grammar gate: accepted declarations must carry `version="1.0"` or
`version="1.1"`; missing or unsupported versions fail before materialization.
P8.493 pins the remaining narrow declaration metadata hygiene: duplicate or
unknown declaration attributes and `encoding` after `standalone` fail, while
legal `encoding` then `standalone` metadata remains accepted. P8.494 adds
token hygiene for declaration `encoding`: empty values, digit-start names, and
unsupported punctuation fail without implying charset transcoding support.
P8.495 pins the corresponding legal forms: version-only declarations,
single-quoted declaration attributes, supported `version="1.1"`,
`standalone="no"`, and encoding tokens containing `.`, `_`, or `-` remain
accepted, still with no charset transcoding or XML repair.
P8.496 pins the standalone value hygiene on the failure path: duplicate
`standalone`, empty `standalone`, and values other than `yes` / `no` fail as
malformed declarations without changing the legal `standalone="no"` success
case.
P8.497 rejects case-varied XML-like processing-instruction targets such as
`<?XML ...?>` / `<?Xml ...?>` as reserved target decoys instead of ordinary
sharedStrings PI trivia, while keeping non-XML ordinary PI trivia behavior
unchanged.
P8.498 pins the positive side of that boundary: `<?xml-stylesheet ...?>`
remains ordinary ignored PI trivia in source sharedStrings, with no stylesheet
import, relationship handling, or special interpretation.
P8.499 tightens malformed ordinary PI hygiene on the same source sharedStrings
path: processing-instruction trivia must end with `?>`, and unterminated PI-like
tokens now fail fast instead of being guessed or skipped.
P8.500 tightens the same ordinary PI boundary further: empty-target PI-like
tokens such as `<? ?>` now fail fast instead of being accepted as trivia.
P8.501 adds a narrow ASCII target-start guard: ordinary PI targets beginning
with obviously invalid XML name-start characters such as `-` now fail fast,
without turning the source sharedStrings loader into a full XML Name validator.
P8.502 tightens the ordinary PI target/data boundary: targets must be followed
by whitespace or immediate `?>`, so `<?target?data?>` now fails fast instead of
being guessed as ordinary PI trivia.
P8.503 adds a matching ASCII target-continuation guard: ordinary PI targets
containing obviously invalid ASCII XML name characters such as `^` now fail
fast, without adding non-ASCII XML Name validation.
P8.504 pins the positive side of that guard: ordinary PI targets containing
legal ASCII continuation characters such as `.`, `-`, digits, and `:` remain
ignored trivia.
P8.505 pins the matching positive side of the target-start guard: ordinary PI
targets starting with legal ASCII name-start characters such as `_` and `:`
remain ignored trivia.
P8.506 pins the positive immediate-terminator form: empty-data ordinary PI
tokens such as `<?fastxlsx?>` remain ignored trivia.
P8.509 pins source `t="str"` materialization on the public `WorksheetEditor`
path: scalar `<v>` payloads become `CellValue::text(...)`, `t="str"` formula
cells keep the formula text while dropping stale cached values, clean no-op
save stays copy-original, and dirty save projects scalar text as inline strings
and formulas as `<f>` without cached values. This is not date cell import,
error-token validation, formula evaluation, cached-result generation, sharedStrings writeback, style
migration, wrapper metadata preservation, XML repair, or large-file random
editing.
The matching source-load `max_cells` guard recovery path now also reopens the
saved replacement output, verifying clean public state after the failed
materialization and confirming old source cells are not resurrected.
P8.510 adds the matching public memory-budget guardrail evidence for source
materialization: `WorksheetEditorOptions::memory_budget_bytes` failures through
`try_worksheet()` expose the `CellStore` diagnostic, leave no partial
materialized session, pending cell/memory diagnostics, dirty state, or
`last_edit_error()`, and a later default-options materialization can still edit
and save. The saved recovery output is reopened to verify clean public state and
source-backed readback. This remains a sparse-store estimate guardrail, not
process RSS, save-time package assembly accounting, or large-file random
editing.
P8.511 extends that evidence to post-materialization mutations: an exact-budget
`WorksheetEditor` session rejects an oversized `set_cell()` insert with the
same `CellStore` diagnostic, updates `last_edit_error()`, preserves sparse and
pending dirty diagnostics, and still accepts a later in-budget overwrite that
saves normally. The saved recovery output is reopened to verify clean readback
for the overwrite and rejected-coordinate absence. This is mutation-side
sparse-store hygiene, not workbook-level memory budgeting, save-time package
peak accounting, or large-file random editing.
P8.512 pins the symmetric post-materialization cell-count guardrail: an
exact-`max_cells` `WorksheetEditor` session rejects a new-cell `set_cell()`
insert, records the `CellStore max_cells` diagnostic, leaves sparse/pending
dirty state unchanged, and still accepts an overwrite of an existing cell. The
saved recovery output is reopened to verify clean readback and rejected payload
absence. This is not row/column insertion, dense range editing, workbook-level
budgeting, or large-file random editing.
P8.513 closes the next guardrail recovery edge: after exact `max_cells` and
exact `memory_budget_bytes` sessions reject a new-cell insertion, erasing the
existing source-backed A2 record releases sparse count/memory budget, clears the
diagnostic, marks the materialized session dirty, and allows a later D4
insertion to save. This is sparse-record removal only, not tombstones,
style-preserving clear, row/column delete, metadata/range sync, or large-file
random editing.
P8.514 pins the adjacent missing-cell erase edge after those same guardrail
failures: `erase_cell("D4")` targets the still-missing rejected cell, clears the
public mutation diagnostic, keeps sparse and pending materialized diagnostics
clean and unchanged, and a later no-op `save_as()` preserves source bytes while
omitting the rejected text. This is clean no-op diagnostic hygiene only, not
tombstones, explicit blank cells, source mutation, or budget release.
P8.515 pins the explicit blank side of the same budget model:
`set_cell("D4", CellValue::blank())` is a new active sparse record and is
rejected by exact `max_cells` / `memory_budget_bytes` sessions without dirtying
state, while an existing-cell `set_cell("A1", CellValue::blank())` stays within
budget, clears the diagnostic, and saves as an empty cell. This is explicit
blank sparse-record accounting only, not tombstones, style-preserving clear,
workbook-level budgeting, or save-time memory accounting.
P8.516 pins public mutation diagnostic replacement order: an invalid A1
`set_cell()` seeds `last_edit_error()`, a later memory-budget `set_cell()`
replaces it with the `CellStore` guardrail diagnostic, a later invalid
coordinate `erase_cell()` replaces that diagnostic, and a final successful
in-budget mutation clears it. All failed calls leave sparse/pending
materialized state clean and keep rejected payloads out of saved output. This
is last-error facade ordering only, not structured diagnostic history,
save-as diagnostics, materialization-load diagnostics, or large-file random
editing.
P8.517 extends the same latest-error contract across mixed public edit
surfaces: failed `replace_sheet_data("Missing", ...)`, failed
`rename_sheet("Data", "Bad/Name")`, and failed `WorksheetEditor::set_cell("a1",
...)` replace each other's diagnostics in order without dirtying editor or
materialized state, and a later successful `replace_sheet_data("Untouched",
...)` clears the diagnostic and saves only the valid replacement. This is
coarse public facade diagnostic ordering only, not error history, rollback,
save-as/load diagnostics, relationship repair, or semantic dependency sync.
P8.518 adds the representative custom/unknown source cell type boundary:
`t="z"` fails through `try_worksheet()` / `worksheet()` with the unsupported
cell type diagnostic, leaves editor/pending/materialized/`last_edit_error()`
state clean, and the same editor can still save a later valid replacement.
This is fail-fast hygiene only, not custom cell type import, tolerant fallback,
date support, error-token validation, or metadata migration.
P8.519 pins the explicit cell-internal processing-instruction branch:
`<t>a<?fastxlsx hidden?>b</t>` fails through `try_worksheet()` / `worksheet()`
with the cell comments / processing-instructions / unsupported-markup
diagnostic, leaves editor/pending/materialized/`last_edit_error()` state clean,
and does not block a later valid replacement/save. This is fail-fast XML trivia
hygiene only, not PI import, inline text repair, or XML trivia preservation.
P8.520 pins the matching cell-internal unsupported-markup branch:
`<t>a<![CDATA[hidden]]>b</t>` fails through `try_worksheet()` / `worksheet()`
with the cell comments / processing-instructions / unsupported-markup
diagnostic, leaves editor/pending/materialized/`last_edit_error()` state clean,
and does not block a later valid replacement/save. This is fail-fast markup
hygiene only, not CDATA import, inline text repair, or XML trivia preservation.
P8.521 pins the matching cell-internal DOCTYPE-like unsupported-markup branch:
`<t>a<!DOCTYPE fastxlsx>b</t>` fails through `try_worksheet()` / `worksheet()`
with the same diagnostic, leaves editor/pending/materialized/`last_edit_error()`
state clean, and does not block a later valid replacement/save. This is
fail-fast markup-declaration hygiene only, not DOCTYPE import, inline text
repair, XML repair, or XML trivia preservation.
P8.522 pins the adjacent true XML declaration branch inside source cell text:
`<t>a<?xml version="1.0"?>b</t>` fails through `try_worksheet()` /
`worksheet()` with the worksheet event-reader late-declaration diagnostic,
leaves editor/pending/materialized/`last_edit_error()` state clean, and does
not block a later valid replacement/save on an unrelated sheet. This is
fail-fast XML prolog hygiene only, not XML declaration import, inline text
repair, XML repair, or XML trivia preservation.
P8.523 closes the adjacent raw cell-text gap: source `<c r="A1">direct-text</c>`
now fails through `try_worksheet()` / `worksheet()` with the CellStore
value-text-without-wrapper diagnostic, leaves
editor/pending/materialized/`last_edit_error()` state clean, and still permits a later valid
replacement/save. This is value-wrapper fail-fast hygiene only, not direct cell
text import, wrapper inference, blank coercion, or XML repair.
P8.524 closes the row-level raw-text gap: source
`<row r="1">direct-row-text<c ...>` now fails through the same public
materialization facade with a CellStore row-text-outside-cell diagnostic, leaves
editor/pending/materialized/`last_edit_error()` state clean, and still permits a
later valid replacement/save. This is row/cell state-machine fail-fast hygiene
only, not row text import, row repair, cell inference, metadata preservation, or
XML repair.
P8.525 closes the adjacent sheetData raw-text gap: source
`<sheetData>direct-sheet-data-text<row ...>` now fails with a CellStore
sheetData-text-outside-row diagnostic, preserves the same clean failure state,
and still allows later unrelated valid save-as recovery. This is sheetData/row
state-machine fail-fast hygiene only, not sheetData text import, row inference,
metadata preservation, or XML repair.
P8.526 closes the worksheet-root direct raw-text gap without weakening wrapper
metadata tolerance: source `<dimension .../>direct-worksheet-text<sheetData ...>`
now fails with a CellStore worksheet-text-outside-metadata-or-sheetData
diagnostic, while text nested inside ignored wrapper metadata such as
`<sheetPr>ignored text</sheetPr>` remains ignored and dropped by dirty
projection. This is worksheet-root state-machine fail-fast hygiene only, not
wrapper metadata text import, metadata preservation, or XML repair.
P8.527 strengthens the shared public materialization-failure hygiene helper:
after both `try_worksheet()` and `worksheet()` failures it now proves
replacement diagnostics, materialized diagnostics, pending edit summaries,
source/planned worksheet names, `worksheet_catalog()`, and `last_edit_error()`
remain clean, while the later valid replacement/save recovery still works.
This is diagnostic evidence only, not behavior expansion or source repair.
P8.528 extends that complete clean-state check to the failed-materialization
no-op `save_as()` copy-original path: after the failed `try_worksheet()` /
`worksheet()` attempts and after the later no-op save, replacement diagnostics,
materialized diagnostics, pending edit summaries, source/planned catalog views,
and `last_edit_error()` remain clean while the output package still byte-copies
the source entries. This is no-op save-as hygiene only, not source repair or
semantic migration.
P8.529 extends the same diagnostic hygiene to the missing optional worksheet
lookup path after a prior public edit failure: `try_worksheet("Missing")` and
the later no-op `save_as()` keep replacement/materialized diagnostics, pending
edit summaries, source/planned catalog views, and the prior `last_edit_error()`
unchanged while the output package remains byte-for-byte source-copy original.
This is missing-lookup/no-op save-as hygiene only, not missing-sheet creation,
source repair, or semantic migration.
P8.530 applies that same no-op save-as hygiene to the throwing
`worksheet("Missing")` lookup after a prior public edit failure: the thrown
`FastXlsxError` identifies the missing sheet, and replacement/materialized
diagnostics, pending edit summaries, source/planned catalog views, and the
prior `last_edit_error()` remain unchanged through the later byte-for-byte
copy-original save. This is throwing-lookup hygiene only, not missing-sheet
creation, source repair, or semantic migration.
P8.531 strengthens the post-recovery catalog-query regression by routing the
existing P8.432 scenario through a complete saved-materialized-session
clean-state helper: read-only planned/source catalog queries now also prove no
replacement diagnostics, no dirty materialized diagnostics, no pending edit
summaries, unchanged source/planned catalog views, preserved borrowed-handle
cleanliness, and the saved materialized value. This is catalog-query
diagnostic hygiene only, not source reload, catalog repair, commit, undo, or
rollback semantics.
It now also carries the same clean no-op `save_as()` proof: the no-op package
is byte-identical to the second recovery output, source bytes stay unchanged,
public save/catalog snapshots and diagnostics remain stable, and a fresh reopen
starts clean with the saved sparse `A1:B2` state.
P8.532 applies that same complete saved-materialized-session helper to the
post-recovery pending-diagnostic query regression: read-only pending-state and
worksheet-catalog diagnostics now prove preserved prior edit count, unchanged
`last_edit_error()`, empty replacement/materialized diagnostics, empty pending
edit summaries, unchanged source/planned catalog views, clean borrowed handles,
and the saved materialized value. This is diagnostic-query hygiene only, not
diagnostic-triggered flush, source reload, catalog repair, commit, undo, or
rollback semantics.
It now also mirrors the adjacent clean no-op `save_as()` contract: the no-op
package is byte-identical to the second recovery output, source bytes remain
unchanged, public save/catalog snapshots plus diagnostics stay stable, and a
fresh reopen starts clean with the saved sparse `A1:B2` state.
P8.533 applies the same helper to handle-level read APIs after that recovery:
`try_cell()`, `get_cell()`, `cell_count()`, `estimated_memory_usage()`, and
`sparse_cells()` now also prove preserved prior edit count, unchanged
`last_edit_error()`, empty replacement/materialized diagnostics, empty pending
edit summaries, unchanged source/planned catalog views, clean borrowed handles,
and the saved materialized value. This is handle-read hygiene only, not source
reload, catalog repair, commit, undo, rollback, or large-file random editing.
It now also carries the clean no-op `save_as()` proof: the no-op package is
byte-identical to the second recovery output, source bytes remain unchanged,
public save/catalog snapshots plus diagnostics stay stable, and a fresh reopen
starts clean with the saved sparse `A1:B2` state.
P8.534 applies the helper to invalid handle-read failures after the same
recovery: invalid row/column, A1 reference, and range reads keep sparse
cell-count/memory diagnostics stable and preserve prior edit count,
`last_edit_error()`, empty replacement/materialized diagnostics, empty pending
edit summaries, unchanged catalog views, clean borrowed handles, and the saved
materialized value. This is invalid-read hygiene only, not coordinate repair,
clamping, source reload, catalog repair, commit, undo, or rollback semantics.
It now also carries the clean no-op `save_as()` proof: the no-op package is
byte-identical to the second recovery output, source bytes remain unchanged,
public save/catalog snapshots plus diagnostics stay stable, and a fresh reopen
starts clean with the saved sparse `A1:B2` state.
P8.535 applies the helper to invalid handle mutations after that recovery:
invalid `set_cell()` / `erase_cell()` calls keep sparse cell-count/memory
diagnostics stable, preserve the invalid-mutation `last_edit_error()`, keep
replacement/materialized diagnostics and pending edit summaries empty, preserve
catalog views and borrowed handles, and retain the saved materialized value.
The next valid mutation still clears the diagnostic and saves. This is invalid
mutation hygiene only, not coordinate repair, clamping, source reload, catalog
repair, commit, undo, or rollback semantics.
It now also carries the clean no-op `save_as()` proof: the no-op package is
byte-identical to the second recovery output, source bytes remain unchanged,
public save/catalog snapshots plus diagnostics stay stable, and a fresh reopen
starts clean with the saved sparse `A1:B2` state.
P8.536 applies the helper to successful missing-cell erase no-ops after that
recovery: valid row/column and A1 `erase_cell()` calls targeting absent cells
clear a prior mutation diagnostic while keeping sparse cell-count/memory
diagnostics stable, replacement/materialized diagnostics and pending edit
summaries empty, catalog views and borrowed handles preserved, and the saved
materialized value intact. This is missing-erase no-op hygiene only, not erase
tombstones, source reload, catalog repair, commit, undo, or rollback semantics.
It now also carries the clean no-op `save_as()` proof: the no-op package is
byte-identical to the second recovery output, source bytes remain unchanged,
public save/catalog snapshots plus diagnostics stay stable, and a fresh reopen
starts clean with the saved sparse `A1:B2` state.
P8.537 strengthens the positive blank/erase projection after that recovery with
a dirty-materialized recovery helper: `set_cell("A1", CellValue::blank())` and
`erase_cell(2, 1)` still drive the existing explicit blank / source-cell erase
save-as projection, while diagnostics now also prove empty `last_edit_error()`,
empty replacement diagnostics, restored-name dirty materialized aggregate
counts/memory, one dirty `pending_worksheet_edits()` summary, unchanged
source/planned catalog views, transient-name absence, and dirty borrowed
handles. This is dirty-state diagnostic hygiene only, not new blank/erase
behavior, source reload, catalog repair, commit, undo, rollback, or erase
tombstones.
P8.538 applies the same helper to the positive scalar/formula projection after
that recovery: numeric A1, boolean A2, formula C3, and preserved source-backed
B1 still drive the existing save-as projection, while diagnostics now also
prove empty `last_edit_error()`, empty replacement diagnostics, restored-name
dirty materialized aggregate counts/memory, one dirty
`pending_worksheet_edits()` summary, unchanged source/planned catalog views,
transient-name absence, and dirty borrowed handles. This is dirty-state
diagnostic hygiene only, not formula evaluation, cached result generation,
calcChain rebuild, date cell typing, source reload, catalog repair, commit,
undo, rollback, sharedStrings/style migration, or relationship repair.
P8.539 applies the same helper to the positive text-escape projection after
that recovery: whitespace-preserving A1, empty text A2, special-character text
C3, and preserved source-backed B1 still drive the existing inline-string
save-as projection, while diagnostics now also prove empty `last_edit_error()`,
empty replacement diagnostics, restored-name dirty materialized aggregate
counts/memory, one dirty `pending_worksheet_edits()` summary, unchanged
source/planned catalog views, transient-name absence, and dirty borrowed
handles. This is dirty-state diagnostic hygiene only, not new text behavior,
XML repair, text normalization, source reload, catalog repair, commit, undo,
rollback, sharedStrings/style migration, or relationship repair.
P8.540 applies the same helper to the positive max-coordinate projection after
that recovery: legal `XFD1048576` row/column and A1 reads, sparse range
snapshot, preserved source-backed B1/A2, dimension refresh, and sparse max-row
save-as XML remain the semantic focus, while diagnostics now also prove empty
`last_edit_error()`, empty replacement diagnostics, restored-name dirty
materialized aggregate counts/memory, one dirty `pending_worksheet_edits()`
summary, unchanged source/planned catalog views, transient-name absence, and
dirty borrowed handles. This is dirty-state diagnostic hygiene only, not dense
row/column allocation, max-coordinate performance evidence, coordinate repair,
source reload, catalog repair, commit, undo, rollback, sharedStrings/style
migration, or relationship repair.
P8.541 applies the same helper to the max-coordinate erase-shrink projection
after that recovery: erasing the saved `XFD1048576` record still removes the
edge cell, leaves the edge sparse range empty, shrinks the next save-as
dimension to `A1:B2`, and preserves A1/B1/A2, while diagnostics now also prove
empty `last_edit_error()`, empty replacement diagnostics, restored-name dirty
materialized aggregate counts/memory, one dirty `pending_worksheet_edits()`
summary, unchanged source/planned catalog views, transient-name absence, and
dirty borrowed handles. This is dirty-state diagnostic hygiene only, not dense
allocation, max-coordinate performance evidence, coordinate repair, tombstone
or style-preserving clear semantics, source reload, catalog repair, commit,
undo, rollback, sharedStrings/style migration, or relationship repair.
P8.542 applies the same helper to the strict A1 max-coordinate mutation
projection after that recovery: `set_cell("XFD1048576", ...)` still writes the
last legal Excel cell, the next save-as expands dimension to
`A1:XFD1048576`, `erase_cell("XFD1048576")` removes it again, and the following
save-as shrinks dimension back to `A1:B2`, while diagnostics now also prove
empty `last_edit_error()`, empty replacement diagnostics, restored-name dirty
materialized aggregate counts/memory, one dirty `pending_worksheet_edits()`
summary, unchanged source/planned catalog views, transient-name absence, and
dirty borrowed handles. This is dirty-state diagnostic hygiene only, not new A1
behavior, lowercase reference acceptance, range mutation, dense allocation,
max-coordinate performance evidence, coordinate repair, tombstone or
style-preserving clear semantics, source reload, catalog repair, commit, undo,
rollback, sharedStrings/style migration, or relationship repair.
P8.543 applies the same helper to the explicit blank max-coordinate projection
after that recovery: `set_cell("XFD1048576", CellValue::blank())` still creates
an active blank edge record, the next save-as expands dimension to
`A1:XFD1048576` and writes `<c r="XFD1048576"/>`, row/column erase removes it,
and the following save-as shrinks dimension back to `A1:B2`, while diagnostics
now also prove empty `last_edit_error()`, empty replacement diagnostics,
restored-name dirty materialized aggregate counts/memory, one dirty
`pending_worksheet_edits()` summary, unchanged source/planned catalog views,
transient-name absence, dirty borrowed handles, and post-erase reacquired-handle
memory alignment. This is dirty-state diagnostic hygiene only, not new blank
behavior, missing-cell synthesis, dense allocation, max-coordinate performance
evidence, coordinate repair, tombstone or style-preserving clear semantics,
source reload, catalog repair, commit, undo, rollback, sharedStrings/style
migration, or relationship repair.
P8.544 applies the same helper to the formula max-coordinate projection after
that recovery: `set_cell(1048576, 16384, CellValue::formula(...))` still writes
the last legal Excel cell as a formula, row/column and A1 reads expose the same
formula text, sparse edge snapshots remain focused on one active record, and
save-as expands dimension to `A1:XFD1048576` while escaping `<f>` text and
requesting recalculation without cached values, while diagnostics now also
prove empty `last_edit_error()`, empty replacement diagnostics, restored-name
dirty materialized aggregate counts/memory, one dirty
`pending_worksheet_edits()` summary, unchanged source/planned catalog views,
transient-name absence, and dirty borrowed handles. This is dirty-state
diagnostic hygiene only, not formula evaluation, cached result generation or
preservation, calcChain rebuild, defined-name/formula dependency rewrite, dense
allocation, max-coordinate performance evidence, coordinate repair, source
reload, catalog repair, commit, undo, rollback, sharedStrings/style migration,
or relationship repair.
P8.545 applies the same helper to the scalar max-coordinate projection after
that recovery: `set_cell(1048576, 16384, CellValue::number(...))` still writes
the last legal Excel cell as a number, A1 boolean overwrite keeps one active
sparse edge record, row/column and A1 reads expose the current scalar value,
and save-as expands dimension to `A1:XFD1048576` while writing numeric and
boolean XML in separate saves, while diagnostics now also prove empty
`last_edit_error()`, empty replacement diagnostics, restored-name dirty
materialized aggregate counts/memory, one dirty `pending_worksheet_edits()`
summary, unchanged source/planned catalog views, transient-name absence, dirty
borrowed handles, and post-overwrite reacquired-handle memory alignment. This
is dirty-state diagnostic hygiene only, not date cell typing, non-finite number
acceptance, style/number-format migration, boolean coercion, dense allocation,
max-coordinate performance evidence, coordinate repair, source reload, catalog
repair, commit, undo, rollback, sharedStrings/style migration, or relationship
repair.
P8.546 applies the same helper to the saved scalar edge erase-shrink projection
after that recovery: after the max-coordinate number is saved and overwritten
by a saved boolean false, `erase_cell(1048576, 16384)` still removes the edge
record, clears `XFD1048576` readback and the sparse edge range, and the next
save-as shrinks dimension back to `A1:B2` while omitting the prior
number/boolean payloads, while diagnostics now also prove empty
`last_edit_error()`, empty replacement diagnostics, restored-name dirty
materialized aggregate counts/memory, one dirty `pending_worksheet_edits()`
summary, unchanged source/planned catalog views, transient-name absence, dirty
borrowed handles, and post-erase reacquired-handle memory alignment. This is
dirty-state diagnostic hygiene only, not new erase behavior, tombstone output,
scalar-to-blank conversion, style-preserving clear semantics, dense allocation,
max-coordinate performance evidence, coordinate repair, source reload, catalog
repair, commit, undo, rollback, sharedStrings/style migration, or relationship
repair.
P8.547 applies the same helper to the saved formula edge erase-shrink
projection after that recovery: after the escaped formula is saved at
`XFD1048576` without a cached value, `erase_cell("XFD1048576")` still removes
the edge record, clears row/column and A1 readback plus the sparse edge range,
and the next save-as shrinks dimension back to `A1:B2` while omitting the prior
formula payload, while diagnostics now also prove empty `last_edit_error()`,
empty replacement diagnostics, restored-name dirty materialized aggregate
counts/memory, one dirty `pending_worksheet_edits()` summary, unchanged
source/planned catalog views, transient-name absence, dirty borrowed handles,
and post-erase reacquired-handle memory alignment. This is dirty-state
diagnostic hygiene only, not formula evaluation, cached result generation or
preservation, calcChain rebuild, defined-name/formula dependency rewrite,
tombstone output, formula-to-blank conversion, style-preserving clear
semantics, dense allocation, max-coordinate performance evidence, coordinate
repair, source reload, catalog repair, commit, undo, rollback,
sharedStrings/style migration, or relationship repair.
The retry/projection shard's positive blank/erase, scalar/formula, and
text-escape projections now also repeat a clean no-op `save_as()` after the
second safe save-as cleans both borrowed handles. The no-op gates preserve
public save-state and catalog snapshots, keep replacement/materialized
diagnostics and pending edit summaries empty, write byte-stable package
entries, leave the source package unchanged, and fresh-reopen as clean public
state with the same projected values. This is projection reuse evidence only,
not new value semantics, formula evaluation, cached result generation,
sharedStrings/style migration, metadata repair, commit, undo, rollback, or
relationship repair.
Malformed source sharedStrings XML/entity/attribute syntax is now pinned at the
same public facade boundary: unknown or unterminated entities, out-of-range
character references, missing or unquoted attribute values, and truncated tags
caused by unterminated attributes fail without dirtying materialized state or
blocking later valid Patch edits. This validates generic sharedStrings tag
attribute syntax but does not add XML repair, schema validation, attribute
whitelisting, sharedStrings writeback, or migration.
P8.548 applies the shared materialization-failure hygiene helper to the lazy
malformed sharedStrings XML boundary where the selected sheet is `Shared` and
the recovery Patch edit targets `Data`: both `try_worksheet("Shared")` and
`worksheet("Shared")` now prove the root-missing-`sst` diagnostic, empty dirty
state, empty replacement/materialized diagnostics, preserved source/planned
catalog views, unchanged `last_edit_error()`, no target replacement leakage,
and later valid `replace_sheet_data("Data", ...)` save-as usability. This is
diagnostic/test-helper hygiene only, not parser behavior expansion, XML repair,
schema validation, attribute whitelisting, relationship repair,
sharedStrings rebuild/writeback/pruning/index migration, source reload, commit,
undo, rollback, or public API.
P8.549 applies the same helper to the lazy missing sharedStrings target
boundary: the workbook relationship still points at a missing
`missingSharedStrings.xml` part, non-`t="s"` `Data` materialization and dirty
inline save-as remain valid, and both `try_worksheet("Shared")` and
`worksheet("Shared")` prove the missing-target diagnostic, empty dirty state,
empty replacement/materialized diagnostics, preserved catalogs, unchanged
`last_edit_error()`, no target replacement leakage, source package immutability,
and later valid `replace_sheet_data("Data", ...)` save-as usability. This is
diagnostic/test-helper hygiene only, not relationship repair, target repair,
sharedStrings synthesis/rebuild/writeback/pruning/index migration, source
reload, commit, undo, rollback, or public API.
Malformed source sharedStrings relationship targets are also pinned at the
public facade: external, query-qualified, fragment-qualified,
malformed-percent, decoded-null, and package-root-escaping targets fail through
`try_worksheet()` / `worksheet()` without dirtying materialized state or
blocking later valid Patch edits. This is target fail-fast hygiene, not
relationship repair, URI repair, external target materialization, sharedStrings
writeback, or migration.
Source sharedStrings non-critical metadata is now pinned on the positive path:
declared `count` / `uniqueCount` values and otherwise well-formed unknown
attributes on `sst` / `si` / `r` / `t` do not drive materialization. The
public editor uses the actual `<si>` order and text, clean no-op save copies
the source sharedStrings bytes, and dirty save still projects inline strings
while preserving `xl/sharedStrings.xml`. This is not schema validation, count
repair, attribute whitelisting, sharedStrings writeback, or migration.
The opposite optional-dependency boundary is also pinned: if a source workbook
has no `xl/sharedStrings.xml` and the loaded worksheet uses only supported
non-`t="s"` cells, `WorksheetEditor` materializes normally and dirty save does
not create a sharedStrings part, workbook relationship, content type, or
worksheet shared-string indexes. This is absence preservation only, not lazy
repair of malformed declared sharedStrings relationships and not sharedStrings
writeback or migration.
Source sharedStrings are now also resolved lazily for the selected worksheet:
stale or malformed sharedStrings relationship metadata does not block
materializing and saving a supported non-`t="s"` sheet, but the same metadata
still fails fast when the selected worksheet actually contains shared string
indexes. Dirty save preserves those source bytes and does not repair or prune
the stale relationship.
Representative duplicate sharedStrings relationship metadata is covered by the
same lazy boundary: it is bypassed only for selected sheets with no shared
string indexes and remains a load failure for selected `t="s"` sheets.
P8.550 applies the shared materialization-failure hygiene helper to that
duplicate relationship boundary: non-`t="s"` `Data` materialization and dirty
inline save-as still preserve the duplicate relationship bytes, and both
`try_worksheet("Shared")` and `worksheet("Shared")` now prove the
multiple-relationships diagnostic, empty dirty state, empty
replacement/materialized diagnostics, preserved catalogs, unchanged
`last_edit_error()`, no target replacement leakage, and later valid
`replace_sheet_data("Data", ...)` save-as usability. This is diagnostic
hygiene only, not duplicate relationship repair/pruning, sharedStrings
synthesis/rebuild/writeback/pruning/index migration, source reload, commit,
undo, rollback, or public API.
Malformed `xl/sharedStrings.xml` payloads are also covered by the same
on-demand behavior: valid non-`t="s"` sheets do not parse or repair the table,
while selected `t="s"` sheets still fail fast on the malformed sharedStrings
XML.
Wrong sharedStrings content type metadata is covered as well: valid non-`t="s"`
sheets do not validate or repair that content type, while selected `t="s"`
sheets still fail fast before materializing shared string indexes.
P8.551 applies the shared materialization-failure hygiene helper to that wrong
content type boundary: non-`t="s"` `Data` materialization and dirty inline
save-as still preserve the wrong content type metadata, and both
`try_worksheet("Shared")` and `worksheet("Shared")` now prove the
wrong-content-type diagnostic, empty dirty state, empty replacement/materialized
diagnostics, preserved catalogs, unchanged `last_edit_error()`, no target
replacement leakage, source package immutability, and later valid
`replace_sheet_data("Data", ...)` save-as usability. This is
diagnostic/test-helper hygiene only, not content type repair, relationship
repair/pruning, target repair, sharedStrings synthesis/rebuild/writeback,
pruning/index migration, source reload, source mutation, commit, undo, rollback,
or public API.
Those lazy sharedStrings dirty-save outputs now also fresh-reopen through the
public `WorkbookEditor` facade: `Data` materializes cleanly with the preserved
non-`t="s"` source cells plus the new inline edit, while `Shared` still fails
with the original missing-target / duplicate-relationship / malformed-XML /
wrong-content-type sharedStrings diagnostic. This is saved-output readback and
failure-state hygiene only; it is not sharedStrings repair, pruning, migration,
writeback, or source relationship/content-type repair.
The lazy dirty-output readback now also pins reopened `column_cells()`
snapshots beside the existing represented-row readback, so the usable `Data`
sheet remains sparse-view consistent even when another sheet still carries the
original sharedStrings diagnostic. It now also checks the reopened `used_range()`
and `sparse_cells()` snapshot for that usable sheet, keeping the lazy failure
boundary aligned with the legal sharedStrings dirty-output readback without
adding repair or migration behavior.
The same helper now rechecks those `Data` sparse views after the later `Shared`
worksheet materialization failure, proving that the bad sharedStrings metadata
diagnostic does not pollute the already-open clean worksheet handle.
Representative legal sharedStrings dirty-save outputs now fresh-reopen through
the same public facade as clean `WorksheetEditor` state too: ordinary source
shared string indexes with appended text, legal XML declarations, prefixed
sharedStrings, local-name-only namespace edge cases, `xml:space` rich/plain
strings, and inconsistent count / unknown-attribute metadata all read back with
the expected sparse snapshots and direct cell values. This remains saved-output
readback coverage only; it is not sharedStrings index migration, metadata
repair, full XML namespace validation, pruning, or broad sharedStrings
writeback.
The same legal sharedStrings dirty-output readback now also covers reopened
`row_cells()` and `column_cells()` snapshots, keeping row/column sparse views
aligned with `sparse_cells()` and direct cell reads without changing the
sharedStrings migration or repair boundary.
P8.552 strengthens the existing public image media-part replacement diagnostic:
`WorkbookEditor::replace_image()` failures now include the public API name, the
requested target media part, and either the file path or memory byte count before
the underlying root cause. The thrown diagnostic is also recorded in
`last_edit_error()`, failed replacements leave pending state clean, and a later
successful replacement clears the diagnostic. This is diagnostic hardening only,
not image insertion, drawing XML mutation, anchor updates, format conversion,
relationship/content-type repair, source reload, transaction/undo/rollback, or
public API expansion.
P8.553 applies the same public diagnostic hardening to
`WorkbookEditor::replace_sheet_data()`: failures now include the public API
name, requested sheet, replacement input shape (`N` rows / `M` cells), and root
cause. Missing-sheet preflight and max-cell guardrail failures now prove the
thrown diagnostic matches `last_edit_error()`, rejected calls leave pending
state and replacement diagnostics clean, and a later valid replacement clears
the diagnostic. This does not change sheetData XML semantics, sharedStrings /
style migration, worksheet metadata handling, relationship/content-type repair,
source reload, transaction/undo/rollback, or public API shape.
P8.554 pins the file-backed image replacement save-time recovery path:
`WorkbookEditor::replace_image(path)` validates the replacement file during the
call but reads it again during `save_as()`. If that staged file disappears before
save, `save_as()` fails without consuming queued public edit state or creating a
new `last_edit_error()` diagnostic; restoring the file lets a later `save_as()`
write the same queued media replacement bytes. This is recovery evidence only,
not production behavior change, image insertion, drawing XML mutation, format
conversion, relationship/content-type repair, source reload, transaction/undo/
rollback, or public API expansion.
P8.555 pins the memory-backed image replacement ownership path:
`WorkbookEditor::replace_image(span)` copies caller bytes during the call, so
mutating the original caller buffer before `save_as()` does not change the saved
media part. The regression also confirms successful `save_as()` preserves the
queued public edit state and does not create `last_edit_error()` when none
existed. This is byte-lifetime evidence only, not decoded pixel retention, image
insertion, drawing XML mutation, format conversion, relationship/content-type
repair, source reload, transaction/undo/rollback, or public API expansion.
P8.556 extends the file-backed image replacement lifecycle evidence: after a
missing staged file causes `save_as()` to fail and the file is restored, a
successful `save_as()` still preserves the queued public edit state and the same
file-backed replacement can be written again to a second output path. This keeps
`replace_image(path)` aligned with the current `save_as()` non-commit model; it
is not a commit/close API, source reload, transaction/undo/rollback behavior,
image insertion, drawing mutation, or relationship/content-type repair.
P8.557 applies the same reusable-output lifecycle evidence to memory-backed
image replacement: `WorkbookEditor::replace_image(span)` keeps FastXLSX-owned
staged bytes after the first successful `save_as()`, so the same queued
replacement can be written again to a second output path without relying on the
caller buffer. This is memory-backed replacement state hygiene only, not decoded
pixel retention, image insertion, drawing mutation, source reload,
transaction/undo/rollback behavior, or relationship/content-type repair.
P8.558 closes the adjacent file-backed integrity edge: if the staged image file
still exists but its bytes change after `replace_image(path)` records the
queued replacement, `save_as()` fails on the recorded staged chunk CRC contract
without consuming pending public edit state or creating `last_edit_error()`.
Restoring the original staged bytes lets a later `save_as()` write the queued
replacement. This is save-time integrity hygiene only, not file watching,
source reload, commit/close semantics, image insertion, drawing mutation, or
relationship/content-type repair.
P8.559 pins same-media repeated image replacement ordering: a later successful
`WorkbookEditor::replace_image()` for the same `xl/media/*` part replaces the
previous queued source. A superseded file-backed staged image is no longer read
by `save_as()`, while `pending_change_count()` still reflects both public edit
calls. This is queued-source lifecycle evidence only, not transaction/undo
history, image insertion, drawing mutation, or relationship/content-type repair.
P8.560 adds a public editing end-to-end smoke and a dedicated editing test
matrix. The new facade regression combines `rename_sheet()`, materialized
`WorksheetEditor::set_cell()`, `replace_sheet_data()` on a different sheet,
`replace_image()`, and `save_as()` in one flow, then verifies workbook catalog
output, worksheet cell XML, media byte replacement, and preservation of the
picture sheet, drawing XML/rels, package rels, workbook rels, content types, and
docProps. `docs/EDITING_TEST_MATRIX.md` now separates proven public facade
behavior from internal Patch preservation evidence and non-goals. This is test
coverage and documentation only, not semantic object editing, relationship
repair/pruning, transaction/undo/rollback, source reload, or complete workbook
editing.
P8.561 materializes source shared formula followers in the public
`WorksheetEditor` path: source-order definitions keep their formula text, later
followers import translated plain formula text, stale cached follower values are
dropped on dirty save, and the source package remains untouched until
`save_as()`. The internal loader regression also pins `$` absolute anchors,
range endpoint translation, skipped quoted/bracketed tokens, invalid `si`
diagnostics, and `#REF!` output for out-of-bounds relative references. This is
read-only source materialization and lossy dirty projection only, not formula
evaluation, formula dependency graphing, shared formula metadata preservation,
calcChain rebuild, sharedStrings/style migration, or a complete Excel formula
parser.
P8.571 exposes a narrow public formula dependency diagnostic:
`WorkbookEditor::formula_reference_audits()` scans only already-materialized
worksheet sessions, reports sheet-qualified formula references with decoded
sheet names and raw reference tokens, maps them against the current
source/planned catalog, and flags
formula text that still references a renamed source sheet. The facade regression
also proves save-as keeps the formula text unchanged. This is read-only audit
evidence only, not full-workbook formula scanning, formula rewrite, dependency
graphing, calcChain rebuild, or a complete in-process formula evaluator.
P8.572 keeps that same public diagnostic conservative for non-local sheet
qualifiers: external workbook qualifiers such as `[Book.xlsx]Data!A1` and 3D
sheet-range qualifiers such as `Data:Formula!A1` are reported with exact tokens
and explicit classification flags, but are not matched to a single current
workbook sheet and are not validated, dereferenced, or rewritten.
P8.572a extends the source-worksheet diagnostic without changing output
semantics: `WorkbookEditor::source_formula_reference_audits()` now expands
metadata-only shared formula followers when their source-order shared formula
definition has already been seen, using the same narrow A1 translator as
materialized shared formula import. This lets read-only rename-risk audits see
follower formulas such as `Data!B2` without materializing `WorksheetEditor`
sessions. It still does not rewrite source worksheet XML, preserve shared
formula metadata in dirty output, resolve out-of-order followers, evaluate
formulas, rebuild calcChain, or become a formula dependency graph.
P8.573 extends formula dependency diagnostics to source workbook
`definedNames`: `WorkbookEditor::defined_name_formula_reference_audits()`
scans direct `xl/workbook.xml` definedName formula text, reports workbook- and
local-sheet-scoped name context, maps ordinary sheet qualifiers against the
current source/planned catalog, flags source-name references after
`rename_sheet()`, and keeps external-workbook / 3D qualifiers audit-only. It
does not update definedName formulas, repair workbook metadata, validate
external targets, interpret 3D semantics, or become a formula evaluator.
When a small workbook rewrite is queued, the diagnostic now reflects the
current planned `xl/workbook.xml` small metadata instead of the source-only
metadata snapshot, so opt-in definedName rewrites are visible to the audit.
P8.573a adds the first explicit rename-sync opt-in:
`WorkbookEditorRenameFormulaPolicy::RewriteDefinedNames`. It is still a
small-workbook-metadata Patch policy, not a formula evaluator: it rewrites only
direct workbook definedName formula text from the old sheet qualifier to the new
quoted sheet qualifier, skips external-workbook and 3D sheet-range qualifiers,
leaves worksheet formula cells and other workbook/worksheet metadata untouched,
and fails before state mutation on malformed/nested definedName XML.
P8.573b extends that opt-in line without turning it into a formula evaluator:
`WorkbookEditorRenameFormulaPolicy::RewriteDefinedNamesAndMaterializedWorksheetFormulas`
also rewrites matching formula cells that are already loaded into
WorkbookEditor-owned WorksheetEditor materialized sessions, preserves existing
style handles, preflights CellStore guardrails before mutating the package edit
plan, marks changed sessions dirty for `save_as()` auto-flush, and still skips
external-workbook qualifiers, 3D sheet ranges, string literals, and every
non-materialized worksheet formula cell. It does not scan source worksheet XML,
evaluate formulas, build a dependency graph, rebuild calcChain, or synchronize
tables/drawings/charts/hyperlinks/relationships.
The normal materialized formula rewrite save handoff now also has a clean
no-op gate in the dedicated formula-rewrite test target: after the first
`save_as()` flushes the rewritten formula session, dirty materialized
diagnostics return to zero, a second `save_as()` writes byte-identical package
entries, and the no-op output fresh-reopens with the rewritten formula text.
The combined definedName + materialized worksheet formula policy now has the
same gate, proving the small workbook metadata rewrite and dirty
`WorksheetEditor` auto-flush settle into a byte-stable clean no-op output
together.
The guardrail path is now covered as well: if that opt-in materialized formula
rewrite would exceed the existing `WorksheetEditorOptions::memory_budget_bytes`
budget, the public `rename_sheet()` call fails before catalog/package mutation,
keeps the materialized session clean, keeps the pending edit count at zero, and
a no-op `save_as()` still preserves the source workbook formula text.
The multi-session path is now pinned too: the same opt-in policy rewrites all
already-materialized worksheet formula sessions that actually contain matching
formula references, leaves unrelated clean materialized sessions untouched,
aggregates dirty diagnostics only for changed sessions, preserves external
workbook qualifiers, and persists both rewritten worksheets through `save_as()`.
That multi-session path now carries the same clean no-op handoff: after both
changed materialized formula sessions flush, aggregate materialized diagnostics
clear, the second `save_as()` writes byte-identical package entries, and the
no-op output fresh-reopens both rewritten formula sheets.
P8.574 moves formula dependency audit behind a semantic detail boundary:
`include/fastxlsx/detail/formula_reference_audit.hpp` exposes
`audit_formula_references()` and
`audit_workbook_defined_name_formula_references()` as domain operations over
formula text / workbook XML plus a source-to-planned sheet catalog.
`WorkbookEditor` no longer drives `scan_formula_references()` directly; it only
gathers editor-owned state, calls the domain audit operation, and maps detail
results into public audit structs.
P8.575 applies the same rule to existing-workbook image replacement:
`src/workbook_editor_image_edit.*` owns media-part target resolution, PNG/JPEG
content-type / extension matching, and replacement-format validation.
`WorkbookEditor::replace_image()` now only reads caller-provided replacement
bytes, asks the image-edit domain helper to validate target semantics, and
queues the package part rewrite.
P8.576 moves source/planned worksheet catalog state behind
`src/workbook_editor_sheet_catalog.*`: `WorkbookEditorSheetCatalogPlan` owns
source sheet names, chained rename state, source/current lookup,
source-to-planned catalog entries, and revert-to-source-name behavior.
`WorkbookEditor`
now asks this plan for catalog views instead of storing a raw
source-name-to-planned-name map in the facade implementation.
P8.577 moves pending whole-`<sheetData>` replacement diagnostics behind
`src/workbook_editor_pending_edits.*`: the pending-edit state object owns
replacement cell/memory totals, current-catalog ordered pending worksheet
names, same-sheet replacement of diagnostics, and rename migration of pending
payload diagnostics. `WorkbookEditor` still orchestrates materialized sessions
and public edit summaries, but no longer stores raw pending replacement maps in
the facade implementation.
P8.578 moves `WorksheetEditor` cell-access validation behind
`src/workbook_editor_worksheet_access.*`: the helper owns strict uppercase A1
single-cell parsing, row/column and range guardrails, and projection from
internal materialized-cell snapshots to public `WorksheetCellSnapshot` values.
`WorkbookEditor` / `WorksheetEditor` still own lifecycle, session lookup,
mutation ordering, and save-as orchestration.
P8.578a extends that helper shard with public A1 range parser bounds/shape
coverage plus source-`StyleId` snapshot projection, keeping the evidence in the
small `fastxlsx.workbook_editor_worksheet_access` target.
P8.579 moves `WorkbookEditor::save_as()` output path safety behind
`src/workbook_editor_save_as_policy.*`: the helper owns empty output, existing
directory, missing-parent, and source-overwrite rejection. The facade still owns
dirty materialized-session flush ordering and package save orchestration.
P8.580 moves public formula-diagnostic adapter logic behind
`src/workbook_editor_formula_diagnostics.*`: the helper owns conversion from the
source/planned workbook sheet catalog to formula-audit catalog entries,
materialized formula-cell scanning, public audit field mapping, and bounded
source `xl/workbook.xml` reads for definedName formula diagnostics.
`WorkbookEditor` now only supplies editor-owned state and returns the adapter
results. This is semantic implementation-boundary cleanup only; it does not add
formula evaluation, rewrite, dependency graphing, calcChain rebuild, or workbook
metadata repair.
P8.581 moves materialized worksheet edit flush helpers behind
`src/workbook_editor_materialized_edits.*`: the helper owns dirty materialized
worksheet name diagnostics, current-catalog flush target preflight, dirty
session projection handoff to `PackageEditor`, and clearing flushed sessions.
`WorkbookEditor` still owns lifecycle, public edit count aggregation, and
save-as orchestration. The shared missing-planned-sheet diagnostic string now
lives with the workbook sheet catalog helper, so materialized flush and public
facade paths use one message source. This is semantic implementation-boundary
cleanup only; it does not add transaction history, rollback, large-file random
editing, relationship repair, or formula/metadata synchronization.
P8.581a extends the materialized dirty projection provider-skip coverage across
blank, formula, and error cells, proving sharedStrings lookup remains text-only
while sparse XML output stays value-kind specific and formula/error payloads
still use XML text escaping.
P8.582 moves public whole-`<sheetData>` row replacement orchestration behind
`src/workbook_editor_sheet_data_replacement.*`: the helper owns rows-to-`CellStore`
projection, input row/cell diagnostics, current-catalog target preflight,
materialized-session conflict preflight, by-name `<sheetData>` chunk handoff,
and pending payload diagnostic recording. `WorkbookEditor::replace_sheet_data()`
now only wraps public error context, increments the public edit count, and
clears/records the facade diagnostic. This is semantic implementation-boundary
cleanup only; it does not add sharedStrings/style migration, metadata sync,
relationship repair, formula calculation, undo/rollback, or large-file random
editing.
P8.583 moves public sheet rename orchestration behind
`src/workbook_editor_sheet_rename.*`: the helper owns materialized-session
rename preflight, package sheet-catalog rename handoff, source/planned catalog
state update, and pending whole-`<sheetData>` payload diagnostic migration.
`WorkbookEditor::rename_sheet()` now only wraps public error context, increments
the public edit count, maps explicit rename formula policy options, and
clears/records the facade diagnostic. The default overload stays catalog-only;
the new explicit `RewriteDefinedNames` option routes to the internal
definedName formula rewrite helper, and the longer
`RewriteDefinedNamesAndMaterializedWorksheetFormulas` option additionally routes
already-materialized WorksheetEditor formula cells through the same narrow
qualifier rewrite. This does not add non-materialized worksheet formula scanning,
table/drawing/chart relationship updates, sheet add/delete, transaction history,
rollback, or broader workbook relationship repair.
P8.588 adds an opt-in generated formula rename rewrite QA scenario on top of
that public policy. `generated_formula_rename_rewrite` creates a workbook with
direct local formulas, external-workbook references, 3D sheet-range references,
string literals, direct workbook definedNames, and a non-materialized worksheet
formula; it materializes only `Formula`, renames `Data` to `RenamedData` with
`RewriteDefinedNamesAndMaterializedWorksheetFormulas`, and verifies the output
with ZIP/XML, `openpyxl`, and Excel COM. The case proves the narrow opt-in
public rewrite path end-to-end without broadening default `rename_sheet()`,
formula evaluation, calcChain rebuild, or non-materialized worksheet formula
rewrite.
P8.589 adds the paired default-policy generated QA scenario:
`generated_formula_rename_default_audit` uses the same workbook shape but calls
default `rename_sheet("Data", "RenamedData")`. ZIP/XML, `openpyxl`, and Excel
COM verify the catalog rename while direct local definedNames,
already-materialized worksheet formulas, external-workbook references, 3D
sheet-range references, string literals, non-materialized worksheet formulas,
and calcChain absence remain unchanged; formula and definedName rename-risk
audits are still reported. This protects the documented catalog-only default
from accidental silent formula repair.
P8.590 adds the middle-policy generated QA scenario:
`generated_formula_rename_defined_names_only` uses the same workbook shape but
calls `rename_sheet("Data", "RenamedData")` with
`WorkbookEditorRenameFormulaPolicy::RewriteDefinedNames`. ZIP/XML, `openpyxl`,
and Excel COM verify that direct local definedNames are rewritten while
already-materialized worksheet formulas, external-workbook references, 3D
sheet-range references, string literals, non-materialized worksheet formulas,
and calcChain absence remain unchanged; formula rename-risk audits remain
visible while definedName rename risks are cleared.
P8.591 adds `generated_formula_rename_escaped_sheet_name` for the same explicit
`RewriteDefinedNamesAndMaterializedWorksheetFormulas` policy. It renames `Data`
to `Renamed & O'Brien` after materializing only `Formula`, then verifies
workbook catalog XML attribute escaping, definedName XML text escaping, quoted
formula qualifiers with doubled apostrophes, unchanged external-workbook / 3D /
string-literal / non-materialized references, calcChain absence, and Excel COM
read-only compatibility. This is still narrow formula-text rewrite QA, not a
complete Excel formula parser or semantic rename subsystem.
P8.592 adds `generated_formula_rename_chain_rewrite` for chained rename
coverage. It queues `Data -> TemporaryData` with the default catalog-only
rename, then queues `TemporaryData -> FinalData` with
`RewriteDefinedNamesAndMaterializedWorksheetFormulas` after materializing only
`Formula`; ZIP/XML, `openpyxl`, and Excel COM verify that both original
source-name references and current planned-name references are rewritten in
direct definedNames and already-materialized worksheet formulas. External
workbook references, 3D sheet ranges, string literals, non-materialized
worksheet formulas, and calcChain absence stay on the documented boundary.
This is QA hardening for the existing explicit policy, not a semantic formula
evaluator, default formula rewrite, non-materialized worksheet rewrite, or
relationship repair.
The same five generated formula rename lanes now also have no-op save variants
that require the follow-up clean `save_as()` package to be byte-identical after
the rename/materialized formula output has flushed.
P8.593 pins the local workbook-editor QA runner's default executable discovery:
when both default and minizip build-tree copies of
`fastxlsx_workbook_editor_qa_tool.exe` exist, `tools/run_workbook_editor_qa.py`
now picks the newest candidate by file timestamp, and fallback build-tree search
does the same for non-standard build locations. The runner self-test covers both
paths so stale local QA tools do not silently mask new generated scenarios. This
is opt-in QA tooling hygiene only; it does not change production code, CMake
build policy, formula semantics, runtime dependencies, or the documented formula
support boundary.
P8.594 closes a small public `WorksheetEditor::sparse_cells(CellRange)` hygiene
gap: invalid range inspection calls now have explicit public-facade coverage for
preserving a pre-existing `WorkbookEditor::last_edit_error()` diagnostic while
leaving the materialized session clean, public edit counts unchanged, sparse
cell count / memory / snapshots unchanged, and no-op `save_as()` on the
copy-original path. This is read-only failure hygiene for the current
small-file editor facade, not a new range API, large-file random editing path,
or formula capability expansion.
P8.595 applies the same public-facade hygiene to `WorksheetEditor::try_cell()` /
`get_cell()` read failures: invalid row/column coordinates, invalid A1
references, valid-coordinate missing `get_cell()`, and last-legal `try_cell()`
misses now explicitly preserve a pre-existing `last_edit_error()` sentinel while
leaving materialized state, edit counts, sparse counts/memory/snapshots, and the
clean no-op `save_as()` copy-original path unchanged. This remains small-file
read failure hygiene only, not a broader A1 parser, range API, formula feature,
or large-file random editor.
P8.596 pins the same public-facade hygiene for stale borrowed `WorksheetEditor`
handles invalidated by owner move: stale read/write/erase attempts now have
explicit coverage for throwing without replacing the moved-to
`last_edit_error()`, changing dirty materialized names / cell counts / memory
estimates, altering `pending_worksheet_edits()`, or leaking stale writes into
the later `save_as()` output. This is borrowed-handle lifetime failure hygiene
only, not a handle lifetime extension, rollback model, formula feature, or
large-file random editor.
P8.597 completes the symmetric move-assignment case: stale handles from both the
assigned source editor and the overwritten target editor now have public
coverage for throwing without replacing the assigned source `last_edit_error()`,
changing dirty materialized diagnostics or summaries, reviving discarded target
state, or leaking stale source/target writes into output. This remains
borrowed-handle failure hygiene only, not target-state recovery, transactional
undo, formula behavior, or large-file random editing.
P8.598 broadens that stale-handle read surface: owner-move coverage now includes
throwing A1 `get_cell()`, ranged `sparse_cells(CellRange)`, and
`estimated_memory_usage()`, while move-assignment coverage includes source
`get_cell()` / memory reads and overwritten-target ranged sparse / memory
reads. The contract remains that stale handle failures preserve the moved-to
owner diagnostic and dirty materialized summaries; it does not extend
`WorksheetEditor::name()` lifetime semantics or make invalid handles usable.
P8.599 closes the saved-clean variant of the same borrowed-handle lifecycle:
after a dirty `WorksheetEditor` session has been flushed by `save_as()` and
marked clean, stale handles invalidated by owner move or move assignment still
throw without replacing the moved-to / assigned `last_edit_error()`, dirtying
materialized diagnostics, changing the saved materialized handoff count or edit
summaries, reviving overwritten target state, or leaking stale writes into
output. This remains public facade state hygiene only, not a handle lifetime
extension, clean-session commit semantic change, rollback model, formula
behavior, or large-file random editor.
P8.600 pins the remaining clean materialization lifecycle: read-only
`WorksheetEditor` sessions that were materialized from source cells but never
dirtied or flushed are invalidated hygienically by owner move and move
assignment. Stale handle failures preserve the moved-to / assigned diagnostic,
keep public edit counts and dirty materialized diagnostics empty, preserve the
source-backed value on reacquire, and leave no-op `save_as()` as
copy-original. This is still borrowed-handle state hygiene only, not a
materialization caching semantic change, handle lifetime extension, rollback
model, formula behavior, or large-file random editor.
P8.601 pins the same clean materialized session boundary for public same-sheet
Patch operation mixing: a read-only `WorksheetEditor` session now blocks
same-sheet `replace_sheet_data()` and `rename_sheet()` before any Patch state is
queued. The regression verifies the failures replace `last_edit_error()` with
the materialized-session guard diagnostic, leave the borrowed handle clean,
keep public edit counts / dirty materialized diagnostics / edit summaries
empty, preserve source/planned catalogs, and keep no-op `save_as()`
copy-original. This is public facade preflight hygiene only, not relationship
repair, rollback, complete random editing, formula evaluation, or formula
rewrite expansion.
P8.602 closes the saved-clean side of that same preflight boundary: after a
dirty `WorksheetEditor` session has been flushed by `save_as()` and marked
clean, same-sheet `replace_sheet_data()` and `rename_sheet()` still fail before
mutating the planned catalog or queued Patch handoff. The regression preserves
the saved materialized handoff count, edit summaries, clean borrowed handle,
empty dirty materialized diagnostics, and saved output bytes while replacing
`last_edit_error()` with each guard diagnostic. This is still public facade
operation-mixing hygiene only, not rollback, relationship repair,
sharedStrings/styles migration, formula evaluation, or formula rewrite
expansion.
P8.603 completes the positive half of the clean-session operation-mixing matrix:
read-only and saved-clean `Data` materialized sessions now have public coverage
showing cross-sheet Patch operations on `Untouched` still succeed. The tests
cover both rename-then-replace and replace-then-rename ordering, keep the clean
borrowed `Data` handle undirtied, keep dirty materialized diagnostics empty,
and verify output preserves `Data` while writing the other sheet rename and
replacement. This remains same-workbook facade hygiene, not sheet add/delete,
relationship repair, rollback, formula calculation, or broad semantic object
editing.
P8.604 pins the reverse diagnostic ordering for clean same-sheet Patch
preflights: read-only and saved-clean materialized `Data` sessions now fail
same-sheet `rename_sheet()` first and then same-sheet `replace_sheet_data()`,
with the latter guard diagnostic replacing the former. The tests keep the clean
borrowed handle state, dirty materialized diagnostics, saved handoff count, edit
summaries, and retry output unchanged. This is diagnostic replacement hygiene
only, not new operation mixing semantics, rollback, formula evaluation, or
relationship repair.
P8.605 closes the remaining failure-recovery gap: after a clean materialized
`Data` session first records a same-sheet Patch failure, a later successful
cross-sheet Patch operation on `Untouched` must clear `last_edit_error()`
again. The read-only branch now covers same-sheet `replace_sheet_data()` then
cross-sheet `rename_sheet()`, and the saved-clean branch covers same-sheet
`rename_sheet()` then cross-sheet `replace_sheet_data()`. The regression keeps
the borrowed `Data` handle clean, preserves dirty-materialized diagnostics /
hand-off counts / edit summaries where applicable, and verifies the saved
output only reflects the successful cross-sheet edit. This is facade
state-clearing hygiene only, not rollback, relationship repair, sharedStrings /
styles migration, formula evaluation, or formula rewrite expansion.
P8.606 extends that recovery matrix to same-handle `WorksheetEditor`
mutations: after a clean materialized `Data` session records a same-sheet Patch
guard failure, a later valid `set_cell()` or `erase_cell()` on that same
borrowed handle must clear `last_edit_error()` and transition `Data` into the
dirty materialized path. The read-only branch now covers same-sheet
`replace_sheet_data()` followed by `set_cell()`, and the saved-clean branch
covers same-sheet `rename_sheet()` followed by `erase_cell()`. The regression
verifies rejected Patch payloads / names do not leak, dirty materialized
diagnostics point only at `Data`, and the final saved output reflects only the
successful worksheet mutation. This remains public facade diagnostic-clearing
hygiene, not rollback, relationship repair, random-editor expansion,
sharedStrings / styles migration, formula evaluation, or formula rewrite
expansion.
P8.607 pins the no-op side of that same recovery contract: after a clean
materialized `Data` session records a same-sheet Patch guard failure, a valid
`erase_cell()` targeting an already-missing cell must still clear
`last_edit_error()` without dirtying the borrowed handle. The read-only branch
now proves the editor remains copy-original after same-sheet
`replace_sheet_data()` failure plus no-op erase, and the saved-clean branch
proves the prior materialized handoff count and retry output remain unchanged
after same-sheet `rename_sheet()` failure plus no-op erase. This is diagnostic
state hygiene only, not rollback, relationship repair, random-editor
expansion, formula evaluation, or formula rewrite expansion.
P8.608 verifies the no-op erase recovery does not weaken same-sheet Patch
guards: after the no-op `erase_cell()` clears a prior diagnostic and leaves the
clean `Data` handle unchanged, a later same-sheet Patch operation must still
fail and replace `last_edit_error()` with the current guard. The read-only
branch covers replacement failure -> no-op erase -> same-sheet rename failure;
the saved-clean branch covers rename failure -> no-op erase -> same-sheet
replacement failure. Both keep dirty materialized diagnostics empty and output
bytes unchanged. This is guard-preservation and diagnostic-ordering hygiene,
not rollback, relationship repair, random-editor expansion, formula evaluation,
or formula rewrite expansion.
P8.609 extends the recovery matrix to two simultaneous clean materialized
handles. The read-only branch materializes both `Data` and `Untouched`, clears
a `Data` same-sheet Patch diagnostic through a `Data` no-op erase, then proves
`Untouched` same-sheet replacement still fails with its own guard while both
handles stay clean and output remains copy-original. The saved-clean branch
flushes dirty edits on both handles, mutates only `Data` after a `Data` guard
failure, then proves `Untouched` same-sheet replacement still fails without
dirtying `Untouched` or losing the pending `Data` mutation. This is cross-handle
state hygiene only, not rollback, relationship repair, random-editor expansion,
formula evaluation, or formula rewrite expansion.
P8.610 pins the successful mutation side of that two-handle recovery boundary.
The read-only branch clears a `Data` same-sheet replacement diagnostic through a
`Data` no-op erase, then mutates only the clean materialized `Untouched` handle
and verifies dirty diagnostics point only at `Untouched`. The saved-clean branch
first recovers `Data` through a valid `set_cell()`, then mutates `Untouched` and
verifies both dirty handles are tracked in workbook order without changing the
saved handoff count until `save_as()`. This is scoped public
`WorksheetEditor` mutation hygiene only, not rollback, relationship repair,
random-editor expansion, formula evaluation, or formula rewrite expansion.
P8.611 adds retry-state coverage after that two-handle recovery flow: both
read-only and saved-clean branches dirty `Data` and `Untouched` after clearing a
same-sheet guard diagnostic, force `save_as(source)` to fail before dirty
materialized auto-flush, and then prove a later safe `save_as()` still flushes
both handles. The regression preserves dirty names / cell counts / memory,
does not create `last_edit_error()` or partial materialized handoffs on failure,
and keeps rejected same-sheet payloads / names out of the retry output. This is
output-path failure hygiene only, not rollback, transaction history,
relationship repair, random-editor expansion, formula evaluation, or formula
rewrite expansion.
P8.612 continues that retry boundary with a post-save reacquire check: after
the same two-handle recovery flow and a failed `save_as(source)`, the test
reacquires `Data` and `Untouched` from the same editor to prove the saved
materialized session is reused, clean diagnostics remain empty, and a later
mutation on one reacquired handle still dirties only that handle before a
second safe `save_as()`. This is post-save reacquire hygiene after failed-save
retry, not rollback, transaction history, relationship repair, random-editor
expansion, formula evaluation, or formula rewrite expansion.
P8.613 pins the failure-query side of that same state: after failed-save retry
and post-save reacquire, mismatched-option `try_worksheet()` / `worksheet()`
lookups and missing-sheet lookups must not dirty `Data` or `Untouched`, must not
create `last_edit_error()`, must preserve catalog diagnostics and saved
materialized values, and must still allow the next single-handle mutation to
flush through a later safe `save_as()`. This is query hygiene only, not
rollback, transaction history, relationship repair, random-editor expansion,
formula evaluation, or formula rewrite expansion.
P8.614 adds invalid-read coverage on top of the same retry/reacquire boundary:
row/column zero, Excel-limit overflow, malformed A1 references, and invalid
`sparse_cells()` ranges must throw without changing clean handle state,
`last_edit_error()`, dirty materialized diagnostics, workbook catalog views, or
saved sparse store estimates. The follow-up valid mutation still dirties only
the touched reacquired handle and persists through the next safe `save_as()`.
This is read-side validation hygiene only, not rollback, transaction history,
relationship repair, random-editor expansion, formula evaluation, or formula
rewrite expansion.
P8.615 pins the mutation-side validation half of that same boundary: after the
two-handle recovery flow, failed `save_as(source)`, safe retry, and post-save
reacquire, invalid `set_cell()` / `erase_cell()` calls on both original and
reacquired handles must update `last_edit_error()` without dirtying either
clean session, without changing sparse store diagnostics, and without adding
materialized handoffs. A later valid mutation clears the diagnostic, dirties
only the touched reacquired session, and persists through the next safe
`save_as()`. This is invalid-mutation state hygiene only, not rollback,
transaction history, relationship repair, random-editor expansion, formula
evaluation, or formula rewrite expansion.
P8.616 closes the adjacent failed-save edge after that invalid-mutation
diagnostic: the same retry/reacquire regression now attempts `save_as(source)`
after invalid mutations have populated `last_edit_error()`, and proves the
output-path failure does not replace or clear that diagnostic, does not dirty
clean sessions, does not add handoffs, and still leaves the next valid mutation
able to clear diagnostics and persist through a safe `save_as()`. This is
diagnostic preservation around output-path preflight only, not rollback,
transaction history, relationship repair, random-editor expansion, formula
evaluation, or formula rewrite expansion.
P8.617 keeps that coverage unchanged but moves the two-clean retry invalid
mutation injection and clean-session assertions into named test helpers. This
reduces local lambda duplication and makes later retry-hygiene shards easier to
audit. It is test-maintenance scaffolding only, not production behavior change,
public API change, operation-mixing semantic change, relationship repair,
formula evaluation, or formula rewrite expansion.
P8.618 applies the same cleanup to the adjacent invalid-read retry coverage:
the read rejection checks and clean-session assertions now live in named
helpers, while the read-side behavior, saved-value checks, and retry/reacquire
coverage stay unchanged. This is test-maintenance scaffolding only, not
production behavior change or public API change.
P8.619 applies the same helper structure to the two-clean failure-query retry
coverage: mismatched-option and missing-sheet query failures, plus the
clean-session assertions that follow them, are now named helpers. The query
behavior, saved-value checks, follow-up valid mutation, and safe `save_as()`
persistence assertions stay unchanged.
P8.620 keeps the two-clean post-save reacquire behavior unchanged but moves the
repeated clean-session diagnostics after reacquire into a named helper.
Saved-value checks, follow-up single-handle mutation, and safe `save_as()`
persistence assertions stay unchanged. This is test-maintenance scaffolding
only, not production behavior change or public API change.
P8.621 keeps the same two-clean post-save reacquire behavior unchanged but moves
the repeated saved-value checks after reacquire into a named helper. The
clean-state helper, follow-up single-handle mutation, and safe `save_as()`
persistence assertions stay unchanged. This is test-maintenance scaffolding
only, not production behavior change or public API change.
P8.622 broadens that helper cleanup across the two-clean retry family: the
reacquire, query-failure, invalid-read, and invalid-mutation regressions now
share the same saved materialized value helper while their failure injection,
clean-state diagnostics, follow-up valid mutation, rejected-payload checks, and
safe `save_as()` persistence assertions stay unchanged. This is a batched
test-maintenance cleanup only, not production behavior change or public API
change.
P8.623 finishes the next layer of that same family by extracting the repeated
single-dirty-session checks after valid follow-up mutations. Reacquire,
query-failure, invalid-read, and invalid-mutation regressions now share the same
dirty-session helper while their failure injection, saved-value checks,
clean-state diagnostics, and safe `save_as()` persistence assertions stay
unchanged. This is still test-maintenance scaffolding only, not production
behavior change or public API change.
P8.624 continues that batched cleanup by extracting the repeated safe-save
state checks after those follow-up mutations. The retry family now shares one
post-save helper that verifies all materialized handles are clean, diagnostics
remain clear, pending handoff counts advance as expected, and dirty names /
cell counts / memory estimates reset to empty. Output ZIP/XML persistence
assertions stay unchanged; this is still test-maintenance scaffolding only, not
production behavior change or public API change.
P8.625 finishes the adjacent two-handle safe-save cleanup for the same retry
family. The first safe save in read-only flows, the setup save in saved-clean
flows, and the saved-clean recovery save now reuse a shared two-handle helper
that checks clean handles, expected handoff counts, and cleared dirty
materialized diagnostics. Failure injection, reacquire/query/invalid-read /
invalid-mutation coverage, follow-up saves, and output ZIP/XML assertions stay
unchanged; this remains test-maintenance scaffolding only.
P8.626 extracts the ZIP/XML output persistence checks for the same retry
family. Read-only first and follow-up outputs, plus saved-clean recovery and
follow-up outputs, now use shared helpers that keep the Data / Untouched value
checks and rejected replacement / rejected rename / rejected invalid-mutation
payload leak checks together. This reduces duplicated package assertions while
leaving the generated workbooks, public behavior, and public API unchanged.
P8.627 applies the same output-check cleanup to the adjacent non-retry
two-clean recovery and other-mutation coverage. Copy-original, saved-clean
recovery, scoped other-mutation, and failed-save recovery outputs now use
scenario helpers for workbook catalog preservation, persisted Data /
Untouched values, and rejected-payload leak checks. This remains
test-maintenance only, with no production behavior or public API change.
P8.628 follows that with the matching state-assertion cleanup in the same
non-retry two-clean family. Clean-handle preservation, single-dirty
materialized sessions, both-dirty failed-save recovery, and safe-save flush
checks now share scenario helpers as well. This is still test-maintenance only
and does not alter the generated workbooks or public API.
P8.629 finishes the adjacent diagnostic cleanup pass for the same non-retry
coverage. Materialized-session guard diagnostics and preserved sparse
cell/memory checks now use small shared helpers in the no-op recovery,
two-clean recovery, scoped other-mutation, and failed-save cases. This remains
test-maintenance only and does not alter production behavior, generated
workbooks, or public API.
P8.630 continues the same cleanup into the earlier same-sheet recovery blocks.
The read-only and saved-clean cross-sheet success paths, plus the matching
worksheet mutation recovery checks, now share focused state helpers for the
single-sheet clean/dirty assertions. This remains test-maintenance only and
does not alter production behavior, generated workbooks, or public API.
P8.631 reuses that same single-sheet cross-sheet state helper in the adjacent
clean-session cross-sheet Patch coverage. The read-only rename+replacement and
saved-clean replacement+rename cases now share the same clean-handle /
materialized-diagnostics assertions while preserving their value and output
checks. This remains test-maintenance only and does not alter production
behavior, generated workbooks, or public API.
P8.632 trims the adjacent clean-session same-sheet Patch failure block by
reusing a focused rename/replacement guard-sequence helper plus read-only and
saved-clean clean-state helpers. The test still proves the replacement guard
supersedes the rename guard, preserves the borrowed handle state, and keeps
the retry output unchanged. This remains test-maintenance only and does not
alter production behavior, generated workbooks, or public API.
P8.633 reuses a single same-sheet guard-failure helper across the adjacent
failure-recovery and no-op guard regressions. The affected read-only and
saved-clean paths now share the throw + `last_edit_error()` guard check while
leaving recovery, value, and ZIP/XML assertions explicit. This remains
test-maintenance only and does not alter production behavior, generated
workbooks, or public API.
P8.634 extends that helper reuse into the explicit two-clean recovery,
scoped-other-mutation, and failed-save guard checks. The Data and Untouched
read-only/saved-clean branches now share the same same-sheet guard failure
helper while preserving the existing scoped state, dirty-handle, and output
assertions. This remains test-maintenance only and does not alter production
behavior, generated workbooks, or public API.
P8.635 continues that helper reuse into the retry invalid-read and
invalid-mutation setup guard checks. The remaining read-only and saved-clean
setup failures in that family now share the same same-sheet guard failure
helper while leaving the later reacquire, dirty-state, and ZIP/XML assertions
explicit. This remains test-maintenance only and does not alter production
behavior, generated workbooks, or public API.
P8.636 adds a focused retry failed-save dirty-state helper for the two-clean
reacquire, query, invalid-read, and invalid-mutation branches. Those branches
now share the `save_as()` throw, dirty-handle preservation, and pending-count
preservation checks while leaving diagnostic and output assertions explicit.
This remains test-maintenance only and does not alter production behavior,
generated workbooks, or public API.
P8.637 adds a narrow internal materialized-session save-state helper for the
failed-save and reflush regressions. The adjacent dirty-session-count and
pending-count assertions now share one helper across direct and move-assigned
materialized retry paths while keeping public diagnostics and output XML checks
explicit. This remains test-maintenance only and does not alter production
behavior, generated workbooks, or public API.
P8.638 applies the same helper-reuse pattern to the public facade's failed-save
and successful-save state checks. The adjacent `worksheet_catalog()` and
`pending_worksheet_edits()` comparisons now share file-scope equality helpers
across both save-state regressions while keeping the surrounding diagnostics and
ZIP/XML assertions explicit. This remains test-maintenance only and does not
alter production behavior, generated workbooks, or public API.
P8.639 adds a narrow public save-state snapshot helper for the same two
save-state regressions. Pending-change count, replacement cell count, memory
estimate, pending worksheet names, and `last_edit_error()` preservation now
share one helper across failed-save and successful-save paths while keeping the
catalog/edit-summary and ZIP/XML assertions explicit. This remains
test-maintenance only and does not alter production behavior, generated
workbooks, or public API.
P8.640 adds a narrow public no-pending-state helper for the adjacent no-op
save-as regressions after failed edit and failed rename diagnostics. The
pre-save and post-save "keep the editor clean" checks now share one helper
while leaving the diagnostic and source-entry assertions explicit. This remains
test-maintenance only and does not alter production behavior, generated
workbooks, or public API.
P8.641 reuses the same no-pending-state helper in the adjacent clean no-op
save-as recovery regression. The "editor remains clean before later edits"
assertion now shares the helper while keeping `last_edit_error()` and ZIP/XML
follow-up assertions explicit. This remains test-maintenance only and does not
alter production behavior, generated workbooks, or public API.
P8.642 adds a fuller public clean-state helper for fresh and clean no-op
save-as facade states. The helper layers on top of the no-pending-state checks
and centralizes empty replacement cells, replacement memory, replacement
worksheet names, and empty `last_edit_error()` assertions while leaving
ZIP/XML output and follow-up edit checks explicit. This remains
test-maintenance only and does not alter production behavior, generated
workbooks, or public API.
P8.643 reuses the public clean/no-pending helpers in the pending diagnostics
facade test. Newly opened editor checks now use the fuller clean-state helper,
while rejected `replace_sheet_data()` and `rename_sheet()` paths use the
no-pending-state helper and keep replacement-specific assertions explicit. This
remains test-maintenance only and does not alter production behavior, generated
workbooks, or public API.
P8.644 reuses the public no-pending-state helper inside the existing clean
`replace_sheet_data()` failure-state helper. Source XML/current-input failure
coverage keeps its replacement diagnostics and source catalog assertions
explicit while centralizing the public pending-state checks. This remains
test-maintenance only and does not alter production behavior, generated
workbooks, or public API.
P8.645 reuses the public no-pending-state helper in the source-entry read
failure regression for `replace_sheet_data()`. The corrupt-source failure path
still keeps its replacement diagnostics, source catalog check, source restore,
and follow-up save assertion explicit while sharing the public pending-state
checks. This remains test-maintenance only and does not alter production
behavior, generated workbooks, or public API.
P8.646 reuses the clean `replace_sheet_data()` failure-state helper in the
replacement guardrail regression. The max-cell and memory-budget failure paths
now share no-pending, replacement diagnostics, and source catalog assertions,
while the successful guarded replacement and ZIP/XML output checks stay
explicit. This remains test-maintenance only and does not alter production
behavior, generated workbooks, or public API.
P8.647 reuses the same clean `replace_sheet_data()` failure-state helper in the
missing-sheet recovery regression. Plain and guardrail-preflight missing-sheet
failures now share no-pending, replacement diagnostics, and source catalog
assertions while the valid follow-up edit and output checks stay explicit. This
remains test-maintenance only and does not alter production behavior, generated
workbooks, or public API.
P8.648 reuses the public no-pending-state helper in the sheetData failure
diagnostics regression. The missing-sheet and guardrail diagnostic paths keep
their `last_edit_error()` and replacement-cell assertions explicit while sharing
the public no-pending checks. This remains test-maintenance only and does not
alter production behavior, generated workbooks, or public API.
P8.649 reuses the public no-pending-state helper in the materialized formula
rewrite guard failure regression. The formula sheet clean check,
`last_edit_error()`, materialized diagnostics, planned catalog assertions, and
follow-up save/output checks remain explicit. This remains test-maintenance only
and does not alter production behavior, generated workbooks, or public API.
P8.650 reuses the public no-pending-state helper in the last-edit-error
tracking regression's initial failed replace path. The later failed rename path
already has a queued successful rename, so it keeps only the existing
`last_edit_error()` and inspection assertions explicit. This remains
test-maintenance only and does not alter production behavior, generated
workbooks, or public API.
P8.651 reuses the public no-pending-state helper inside the materialization
failure clean-state helper. Replacement diagnostics, materialized-session
diagnostics, source/planned catalog checks, recovery checks, and optional
`last_edit_error()` assertions remain explicit. This remains test-maintenance
only and does not alter production behavior, generated workbooks, or public API.
P8.652 adds a narrow no-replacement-diagnostics helper for states that may have
valid pending rename edits but must not expose sheetData replacement diagnostics.
The pending diagnostics rename-only path and rename-chain-back path now share
that helper while keeping pending-count, planned-catalog, data-replaced, and
summary assertions explicit. This remains test-maintenance only and does not
alter production behavior, generated workbooks, or public API.
P8.653 reuses the no-replacement-diagnostics helper in move and move-assignment
cleanup regressions. Moved-from, clean moved-to, clean-source assignment, and
moved-from-source assignment paths now share the replacement cells/memory/names
checks while keeping pending state, replacement lookup, materialized-session,
catalog, and last-error assertions explicit. This remains test-maintenance only
and does not alter production behavior, generated workbooks, or public API.
P8.654 reuses the no-replacement-diagnostics helper inside the materialization
failure clean-state helper. The Data/recovery `has_pending_replacement()`,
materialized-session, catalog, recovery, and optional `last_edit_error()` checks
remain explicit. This remains test-maintenance only and does not alter
production behavior, generated workbooks, or public API.
P8.655 reuses the no-replacement-diagnostics helper in the remaining public
materialized recovery helpers and the adjacent post-save recovery diagnostic
block that already checked replacement cells, memory, and sheet names together.
Pending counts, replacement lookup probes, materialized-session diagnostics,
catalog checks, and handle/value assertions remain explicit. This remains
test-maintenance only and does not alter production behavior, generated
workbooks, or public API.
P8.656 reuses that same no-replacement-diagnostics helper inside the shared
public clean-state helper itself. Clean-state callers now share the replacement
cells, replacement memory, and replacement sheet-name checks while keeping
generic no-pending state and `last_edit_error()` assertions explicit. This
remains test-maintenance only and does not alter production behavior, generated
workbooks, or public API.
P8.657 adds a narrower no-replacement-payload-size diagnostics helper for
states that only asserted replacement cell count and replacement memory. The
memory-budget move paths, source-read failure state, clean sheetData failure
helper, and old-name-after-rename failure now share those two checks without
adding replacement sheet-name assertions. This remains test-maintenance only and
does not alter production behavior, generated workbooks, or public API.
P8.658 returns to the formula boundary lane with focused internal formula
regressions. `fastxlsx.formula` now pins ASCII case-insensitive local sheet
matching for formula-reference audits, distinguishes case-varied source-name
references from planned-name references after rename, and proves external
workbook qualifiers plus 3D sheet ranges remain audit-only. The same slice pins
case-insensitive local qualifier rewriting, unchanged bytes when no qualifier
matches, prefixed workbook/definedName XML handling, and XML escaping for
rewritten definedName formulas whose replacement sheet name contains `&` or an
apostrophe. This is formula text audit/rewrite boundary hardening only; it does
not add formula evaluation, default rename-time formula synchronization,
non-materialized worksheet formula rewrite, external-link validation, 3D
semantics, dependency graphing, or calcChain rebuild.
P8.659 lifts that case-boundary evidence to the public `WorkbookEditor` facade.
`fastxlsx.workbook_editor.public` now has a generated mixed-case formula /
definedName workbook where `data!` and `DATA!` local references are rewritten
by the explicit
`RewriteDefinedNamesAndMaterializedWorksheetFormulas` policy, while
`[Book.xlsx]data!`, `data:Formula!`, structured-like skipped text, and string
literals remain unchanged. The regression verifies materialized formula
readback, public formula/definedName audits before and after rewrite,
`save_as()` XML output with escaped `Renamed & Data` qualifiers, and reopened
formula text. This is public e2e coverage for the existing opt-in formula text
policy only; it is not default rename synchronization, non-materialized
worksheet rewrite, formula evaluation, external link validation, 3D semantics,
or calcChain rebuild.
The formula-rewrite shard now also drives a quoted materialized worksheet
qualifier through that public policy: `O'Brien` can be renamed to a sheet name
containing an apostrophe and `&`, the formula text is re-escaped as a quoted
local qualifier, external workbook and 3D references remain unchanged, saved
XML escapes `&`, and the clean no-op output stays byte-stable.
P8.660 pins the matching default-policy negative case. The same public
mixed-case workbook now verifies that default `rename_sheet("Data",
"Renamed & Data")` only rewrites the workbook sheet catalog: materialized
worksheet formula text and direct workbook definedName formula text keep their
original `data!` / `DATA!` qualifiers, while public formula and definedName
audits still flag those case-varied references as stale source-name risks
against the planned `Renamed & Data` catalog entry. The saved package preserves
the original formula XML and definedName bodies, and reopen still exposes the
original formula text. This is default audit-only boundary coverage; it is not
default formula rewrite, non-materialized worksheet rewrite, formula
evaluation, external link validation, 3D semantics, dependency graphing, or
calcChain rebuild.
That default-policy coverage now includes quoted sheet qualifiers too:
renaming `O'Brien` to a name containing an apostrophe and `&` leaves both
materialized and non-materialized worksheet formula text unchanged, keeps the
old escaped qualifier in formula/source audits as a stale source-name risk,
persists only the XML-escaped workbook catalog rename, and keeps the clean
no-op save byte-stable for the materialized case.
P8.661 extends that default audit-only evidence to source worksheet formula
diagnostics. Without materializing the `Formula` worksheet, default
`rename_sheet("Data", "Renamed & Data")` followed by
`source_formula_reference_audits()` now verifies that mixed-case `data!` and
`DATA!` references are reported as stale source-name risks, while external
workbook and 3D qualifiers remain classified audit-only. `save_as()` still
preserves the non-materialized worksheet formula XML and only persists the
catalog rename. This is source-read diagnostic coverage only; it is not
non-materialized formula rewrite, default formula synchronization, formula
evaluation, external link validation, 3D semantics, dependency graphing, or
calcChain rebuild.
P8.662 syncs the formula boundary documentation after the default audit-only
case-varied rename coverage. `docs/FORMULA_SUPPORT.md`, `README.md`,
`docs/EDITING_MODEL.md`, `docs/API_DESIGN_AND_DOCUMENTATION.md`, and
`docs/TASK_BREAKDOWN.md` now state the same policy: default `rename_sheet()`
is catalog-only; `formula_reference_audits()`, `source_formula_reference_audits()`,
and `defined_name_formula_reference_audits()` expose stale source-name risks
without changing formula text; case-varied local qualifiers are matched
ASCII case-insensitively while preserving original spelling; source-read
diagnostics do not materialize or rewrite non-materialized worksheet XML. This
is documentation alignment only, with no code, public API, CMake, formula
evaluation, default rewrite, external/3D semantics, dependency graph, or
calcChain rebuild change.
P8.663 pins the read-only public state boundary for case-varied source formula
diagnostics. The existing `fastxlsx.workbook_editor.public` source-read
regression now snapshots `pending_change_count()`, `has_pending_changes()`,
pending replacement names, pending materialized names, and pending edit summary
count after the default catalog-only rename, then verifies
`source_formula_reference_audits()` leaves all of them unchanged while still
reporting stale `data!` / `DATA!` source-name risks. This is diagnostic state
hygiene only; it is not a production behavior change, formula rewrite,
formula evaluation, external/3D semantics, dependency graph, or calcChain
rebuild.
P8.664 syncs that read-only pending-state boundary into public API comments and
design docs. `include/fastxlsx/workbook_editor.hpp` now states that
`formula_reference_audits()`, `source_formula_reference_audits()`, and
`defined_name_formula_reference_audits()` do not increment
`pending_change_count()`, queue replacements, dirty/create materialized
sessions, or change pending edit diagnostics. The API design doc repeats the
same boundary alongside the formula-audit mode descriptions. This is
documentation/comment alignment only; it does not change behavior, public
symbols, CMake, formula rewrite, formula evaluation, external/3D semantics,
dependency graphing, or calcChain handling.
P8.665 extends the concrete pending-state hygiene evidence to workbook
definedName formula diagnostics. The existing
`test_defined_name_formula_reference_audits_report_renamed_source_sheet_risk()`
now snapshots `pending_change_count()`, `has_pending_changes()`, pending
replacement names, pending materialized names, and pending edit summary count
after default `rename_sheet("Data", "RenamedData")`, then verifies
`defined_name_formula_reference_audits()` leaves all of them unchanged while
still reporting stale source-name direct definedName references. This is
diagnostic state hygiene only; it is not a production behavior change,
definedName rewrite, worksheet formula rewrite, formula evaluation,
external/3D semantics, dependency graphing, or calcChain rebuild.
P8.666 closes the matching pending-state hygiene gap for already materialized
worksheet formula diagnostics. The existing
`test_formula_reference_audits_report_renamed_source_sheet_risk()` now
snapshots `pending_change_count()`, `has_pending_changes()`, pending
replacement names, pending materialized names, and pending edit summary count
after default `rename_sheet("Data", "RenamedData")`, then verifies
`formula_reference_audits()` leaves all of them unchanged while still reporting
stale source-name materialized worksheet formula references. This is diagnostic
state hygiene only; it is not a production behavior change, worksheet formula
rewrite, formula evaluation, external/3D semantics, dependency graphing, or
calcChain rebuild.
P8.667 pins the empty-read side of the materialized formula audit contract. The
same public workbook-editor regression now snapshots `pending_change_count()`,
`has_pending_changes()`, pending replacement names, pending materialized names,
and pending edit summary count before any `worksheet()` session is opened, then
verifies `formula_reference_audits()` returns no entries and leaves that public
state unchanged. This is diagnostic state hygiene only; it is not broad source
formula scanning, lazy worksheet materialization, formula rewrite, formula
evaluation, external/3D semantics, dependency graphing, or calcChain rebuild.
P8.668 extends the same public-state evidence to the ordinary source-read
formula audit path. `test_source_formula_reference_audits_report_non_materialized_rename_risk()`
now snapshots pending edit diagnostics and `last_edit_error()` before the first
`source_formula_reference_audits()` call, then verifies the source XML scan
returns the expected non-materialized formula references without changing those
public diagnostics. This is diagnostic state hygiene only; it is not worksheet
materialization, source formula rewrite, formula evaluation, external/3D
semantics, dependency graphing, or calcChain rebuild.
P8.669 closes the same `last_edit_error()` gap in the case-varied source-read
rename regression. After default catalog-only `rename_sheet("Data",
"Renamed & Data")`, the test now snapshots `last_edit_error()` alongside the
pending-state diagnostics and verifies `source_formula_reference_audits()`
still reports stale `data!` / `DATA!` source-name risks without replacing or
creating that public edit diagnostic. This is diagnostic state hygiene only; it
is not source formula rewrite, formula evaluation, external/3D semantics,
dependency graphing, or calcChain rebuild.
P8.670 applies the same `last_edit_error()` hygiene to workbook definedName
formula diagnostics. After default catalog-only `rename_sheet("Data",
"RenamedData")`, the definedName audit regression now snapshots
`last_edit_error()` with the pending-state diagnostics and verifies
`defined_name_formula_reference_audits()` still reports stale source-name
definedName references without replacing that public edit diagnostic. This is
diagnostic state hygiene only; it is not definedName rewrite, formula
evaluation, external/3D semantics, dependency graphing, or calcChain rebuild.
P8.671 closes the matching `last_edit_error()` gap for already-materialized
worksheet formula diagnostics. After default catalog-only
`rename_sheet("Data", "RenamedData")`, the materialized
`formula_reference_audits()` regression now snapshots `last_edit_error()` with
the pending-state diagnostics and verifies stale source-name formula references
are reported without replacing that public edit diagnostic. This is diagnostic
state hygiene only; it is not worksheet formula rewrite, formula evaluation,
external/3D semantics, dependency graphing, or calcChain rebuild.
P8.672 closes the empty-read side of the same public audit contract: before any
`WorksheetEditor` session is opened, `formula_reference_audits()` now snapshots
`last_edit_error()` with the non-materialized pending-state diagnostics and
verifies the empty audit read leaves that public diagnostic unchanged. This is
diagnostic state hygiene only; it is not lazy worksheet materialization,
worksheet formula rewrite, formula evaluation, external/3D semantics,
dependency graphing, or calcChain rebuild.
P8.673 pins the clean initial materialized scan as read-only too. Immediately
after opening the `Formula` worksheet session and before any rename,
`formula_reference_audits()` now snapshots the public pending diagnostics and
`last_edit_error()`, then verifies exposing sheet-qualified formula references
does not dirty or replace that state. This is diagnostic state hygiene only; it
is not formula rewrite, formula evaluation, external/3D semantics, dependency
graphing, or calcChain rebuild.
P8.674 applies the same public state hygiene to source shared-formula audits.
`test_source_formula_reference_audits_translate_shared_formula_followers()` now
snapshots pending diagnostics and `last_edit_error()` before both the initial
and post-rename `source_formula_reference_audits()` calls, proving source-order
shared formula follower expansion and stale source-name reporting do not dirty
or replace public state. This is diagnostic state hygiene only; it is not lazy
worksheet materialization, formula rewrite, formula evaluation, external/3D
semantics, dependency graphing, or calcChain rebuild.
P8.675 syncs that completed formula-audit state hygiene back into API-facing
docs: the `WorkbookEditor` Doxygen overview and
`docs/API_DESIGN_AND_DOCUMENTATION.md` now state that the three formula audit
APIs are read-only diagnostics that do not increment public edit counts, queue
replacements, dirty/create materialized sessions, change pending edit
summaries, or update `last_edit_error()`. This is documentation alignment only;
it does not change public symbols, runtime behavior, or formula semantics.
P8.676 completes the user-facing docs alignment for the same boundary. README's
formula section and the `docs/FORMULA_SUPPORT.md` capability matrix now both
state that formula audit APIs report stale source-name risks without queuing
edits, dirtying/materializing worksheet sessions, changing pending diagnostics,
or updating `last_edit_error()`. This remains documentation-only alignment.
P8.677 extends the materialized formula rewrite guard regression with a recovery
path: after the memory-budget rejection records `last_edit_error()` and a no-op
`save_as()` preserves that diagnostic, a later valid short-name opt-in rewrite
now proves the diagnostic is cleared, the materialized formula is rewritten, and
the rejected target name does not leak into the recovered workbook. This is
failure/retry state hygiene only; it is not broader formula rewrite semantics,
formula evaluation, dependency graphing, or calcChain rebuild.
That recovery branch now also has the matching clean no-op handoff: after the
short-name retry save clears materialized diagnostics, a follow-up `save_as()`
is byte-stable, preserves public save-state/edit summaries, and fresh-reopens
with the recovered formula without leaking the rejected long target.
It now continues once more from that clean no-op state: the same borrowed
`WorksheetEditor` handle can replace the recovered formula, dirty only the
formula sheet again, flush to a fresh workbook, preserve the previous no-op
package, and settle into another byte-stable no-op save/reopen cycle.
That post-noop reuse now also pins diagnostic hygiene: the later successful
formula edit keeps `last_edit_error()` clear through public inspection,
`save_as()`, and the final clean no-op save.
P8.678 broadens that success-side diagnostic hygiene from the guard recovery
case to the representative explicit formula rewrite paths. The definedName-only,
materialized worksheet formula, combined definedName + materialized formula,
case-varied local reference, chained alias, and multi-session materialized
formula rewrite regressions now seed a prior invalid-rename `last_edit_error()`
and prove the subsequent successful opt-in rewrite clears it while preserving
the existing formula rewrite assertions.
P8.679 pins the output-failure retry boundary after a successful combined
formula rewrite. A new regression performs the opt-in definedName +
materialized worksheet formula rewrite, snapshots public edit summaries,
planned catalog, dirty materialized diagnostics, and empty `last_edit_error()`,
then forces a missing-parent `save_as()` failure. The same editor must preserve
the rewritten state and a later safe retry must persist the renamed catalog,
rewritten definedName, and rewritten materialized formula.
That retry boundary now also proves the clean handoff after recovery: the safe
retry clears materialized diagnostics, a following no-op `save_as()` is
byte-stable against the retry output, and the no-op package fresh-reopens with
the rewritten materialized formula.
P8.680 pins the adjacent post-rewrite materialized mutation boundary. After a
successful opt-in formula rewrite dirties the `Formula` worksheet session, an
invalid follow-up mutation must update `last_edit_error()` without reverting or
corrupting the rewritten formula, while a later valid mutation clears the
diagnostic, updates dirty materialized diagnostics, and saves/reopens with both
the rewritten formula and the later cell edit.
That later-mutation path now also carries the clean no-op handoff: after the
save flushes the rewritten formula plus the later materialized edit,
materialized diagnostics clear, the next `save_as()` is byte-stable, and the
no-op package fresh-reopens with both cells intact.
P8.681 pins the corresponding Patch mixing boundary. After opt-in formula
rewrite has dirtied the materialized `Formula` session, same-sheet
`replace_sheet_data("Formula", ...)` must fail with the existing materialized
session guard, preserve the rewritten formula and public rename state, and avoid
queuing replacement diagnostics. A cross-sheet replacement on `Other Sheet`
still succeeds, clears the guard diagnostic, and saves beside the rewritten
formula state.
The same Patch-mixing boundary now carries clean no-op save evidence: after
the cross-sheet replacement and dirty materialized formula flush together,
materialized diagnostics clear while the planned replacement diagnostics remain
stable, the follow-up `save_as()` is byte-stable, and the no-op output
fresh-reopens both the formula and replacement sheets.
It now also continues from that clean no-op state: the same borrowed
`WorksheetEditor` handle can replace the rewritten formula, dirty only
`Formula` again, preserve the cross-sheet replacement diagnostics, write a
fresh post-noop output, and settle into another byte-stable no-op save/reopen
cycle without mutating the earlier no-op package.
That same replacement-mixing post-noop edit now also proves successful reuse
keeps `last_edit_error()` empty across public inspection and both save steps.
P8.682 splits the formula-heavy WorkbookEditor public tests out of the large
`tests/test_workbook_editor.cpp` facade shard. The new
`tests/test_workbook_editor_formula_rewrite.cpp` target owns formula reference
audits, source formula audits, definedName formula audits, and explicit rename
formula rewrite policy regressions under
`fastxlsx.workbook_editor_formula_rewrite`. This is test organization and CTest
budget hygiene only; it does not change public API symbols, runtime behavior,
formula rewrite semantics, formula evaluation, or dependency graph scope.
The formula-rewrite shard now also verifies opt-in materialized worksheet
formula rename rewrite through `WorksheetEditor` save/reopen/no-op output while
preserving string literals, structured-reference bracket text, external
workbook refs, 3D sheet ranges, and name-like tokens.
P8.683 finishes the larger WorkbookEditor test-source split by moving the
former monolithic `facade`, `source-success`, `source-failure-*`, and
`materialized` shard bodies into standalone test executables:
`tests/test_workbook_editor_facade.cpp`,
`tests/test_workbook_editor_source_success.cpp`,
`tests/test_workbook_editor_source_failures.cpp`, and
`tests/test_workbook_editor_materialized_sessions.cpp`. The original
`tests/test_workbook_editor.cpp` now keeps only the core/public/public-edge
shards. This is test layout and CTest budget hygiene only; it does not change
runtime behavior, public API, package output semantics, source materialization
policy, materialized-session behavior, or formula/image/docProps features.
The materialized-sessions shard now also proves both normal and move-assigned
reflush-after-success paths settle into byte-stable clean no-op `save_as()`
outputs after the second materialized projection.
The materialized-edits shard now also pins current-catalog targeting for
materialized handoff helpers: pending dirty names ignore stale source-name
sessions after rename, and flush target validation accepts only current planned
sheet names.
It also covers dirty sheetData projection collection directly: only dirty
materialized sessions are projected, each projection carries its sparse-store
dimension reference, and the chunk callback emits sheetData-only XML.
The same shard now verifies that dirty sheetData projections can carry a
shared-string index provider, so text cells are emitted as stable `t="s"`
indexes while non-text cells stay value-only; this is projection wiring
evidence only, not broad sharedStrings migration.
It also covers the parallel full-worksheet projection path with the same
provider boundary, including the XML declaration, worksheet root, refreshed
dimension, shared-string text cells, and value-only numeric cells.
The failure side of that provider handoff is pinned too: missing index lookups
from both sheetData-only and full-worksheet callbacks propagate `FastXlsxError`
while leaving the materialized session dirty for retry.
The non-text side is covered as well: numeric/boolean-only projections can carry
a shared-string provider without calling it, emit value-only cells, and leave
dirty state untouched until the higher-level flush succeeds.
It now also covers a source-backed materialized session erased down to an empty
sparse store: both sheetData-only and full-worksheet projections keep the dirty
session, report `A1`, and emit empty sheetData/minimal worksheet XML for save-as
handoff.
The follow-up WorkbookEditor facade split keeps the base public facade
diagnostic/state tests in `tests/test_workbook_editor_facade.cpp`, moves
save-as/no-op, rename/planned-catalog, image-replacement, and end-to-end smoke
coverage into `tests/test_workbook_editor_facade_save_as.cpp`,
`tests/test_workbook_editor_facade_rename.cpp`,
`tests/test_workbook_editor_facade_images.cpp`, and
`tests/test_workbook_editor_facade_smoke.cpp`, and shares fixtures through
`tests/test_workbook_editor_facade_common.hpp`. The existing
`fastxlsx.workbook_editor_facade` CTest name remains the core shard, the new
facade shard names are test-organization only, and
`fastxlsx_workbook_editor_facade_tests` is now a build-only aggregate.
The PackageEditor preservation comments shard is now split by object family:
legacy comments stay in `tests/test_package_editor_preservation_comments.cpp`,
threaded comments, persons, and same-path ordering coverage move into
`tests/test_package_editor_preservation_comments_threaded.cpp`,
`tests/test_package_editor_preservation_comments_persons.cpp`, and
`tests/test_package_editor_preservation_comments_ordering.cpp`, with shared
fixtures in `tests/test_package_editor_preservation_comments_common.hpp`. The
existing `fastxlsx.package_editor.preservation-comments` CTest name remains the
legacy-comments shard, and the new names are test-organization only.
The next package-editor test-layout step is now the CellStore shard split:
`tests/test_package_editor_cellstore.cpp` owns the `cellstore-*` CTest names
while `tests/test_package_editor.cpp` keeps the remaining shards. This is the
same kind of CTest budget hygiene only; it does not change PackageEditor
behavior.
The follow-up sheetData layout split now moves `sheetdata`,
`sheetdata-catalog`, `sheetdata-guards`, and `sheetdata-linked` into
`tests/test_package_editor_sheetdata.cpp` under the same CTest names. This is
still test organization only: no runtime behavior, public API, sheetData XML
semantics, relationship repair, or linked-object lifecycle semantics changed.
The next preservation layout split moves `preservation-core`,
`preservation-removal`, `preservation-resources`, `preservation-comments`, and
`preservation-linked` into `tests/test_package_editor_preservation.cpp` under
the same CTest names. This is still CTest budget hygiene only: no runtime
behavior, public API, PackageEditor semantics, relationship repair, linked
object lifecycle, or preservation guarantees changed.
The follow-up preservation executable split then replaces that aggregate source
with five per-shard files:
`tests/test_package_editor_preservation_core.cpp`,
`tests/test_package_editor_preservation_removal.cpp`,
`tests/test_package_editor_preservation_resources.cpp`,
`tests/test_package_editor_preservation_comments.cpp`, and
`tests/test_package_editor_preservation_linked.cpp`. The
`fastxlsx.package_editor.preservation-*` CTest names stay stable, and
`fastxlsx_package_editor_preservation_tests` remains a build-only aggregate.
This remains test-source and CTest budget hygiene only.
The final PackageEditor monolith split removes
`tests/test_package_editor.cpp` by moving the remaining `core`, `c5`, and
`policy` shards into `tests/test_package_editor_core.cpp`,
`tests/test_package_editor_c5.cpp`, and
`tests/test_package_editor_policy.cpp`. The
`fastxlsx.package_editor.core`, `fastxlsx.package_editor.c5`, and
`fastxlsx.package_editor.policy` CTest names stay stable, and
`fastxlsx_package_editor_tests` remains a build-only aggregate. This is still
test organization only.
The policy executable is now split further:
`tests/test_package_editor_policy.cpp` keeps the state/audit/recalculation
core, while
`tests/test_package_editor_policy_save_as_guards.cpp` and
`tests/test_package_editor_policy_invalid_inputs.cpp` own the save-as guard and
invalid-input families. The existing `fastxlsx.package_editor.policy` CTest
name stays stable for the core shard, the new policy shard names are added,
and `fastxlsx_package_editor_policy_tests` remains a build-only aggregate.
This is still test organization only.
PackageReader now follows the same CTest-budget split:
`tests/test_package_reader.cpp` keeps writer guardrails and stored-entry core
coverage, while `tests/test_package_reader_workbook.cpp` owns workbook catalog
and package-backed cell-store loader coverage, and
`tests/test_package_reader_zip_failures.cpp` owns ZIP/backend failure coverage
including the opt-in minizip cases. The existing `fastxlsx.package_reader`
CTest name stays stable for the core shard, the new package-reader shard names
are added, and `fastxlsx_package_reader_tests` remains a build-only aggregate.
This is still test organization only.
The remaining WorkbookEditor monolith split removes
`tests/test_workbook_editor.cpp` by moving `core`, `public`, and `public-edge`
into `tests/test_workbook_editor_core.cpp`,
`tests/test_workbook_editor_public.cpp`, and
`tests/test_workbook_editor_public_edge.cpp`. The
`fastxlsx.workbook_editor.core`, `fastxlsx.workbook_editor.public`, and
`fastxlsx.workbook_editor.public-edge` CTest names stay stable, and
`fastxlsx_workbook_editor_tests` remains a build-only aggregate. This is still
test organization only.
The PackageEditor sheetData executable split removes
`tests/test_package_editor_sheetdata.cpp` by moving `sheetdata`,
`sheetdata-catalog`, `sheetdata-guards`, and `sheetdata-linked` into
`tests/test_package_editor_sheetdata_base.cpp`,
`tests/test_package_editor_sheetdata_catalog.cpp`,
`tests/test_package_editor_sheetdata_guards.cpp`, and
`tests/test_package_editor_sheetdata_linked.cpp`. The existing
`fastxlsx.package_editor.sheetdata*` CTest names stay stable, and
`fastxlsx_package_editor_sheetdata_tests` remains a build-only aggregate. This
is still test organization only.
The next sheetData shard pass splits the base executable further: the core
sheetData / writer-roundtrip tests stay in
`tests/test_package_editor_sheetdata_base.cpp`, while the by-name helper
coverage and planned-workbook catalog coverage now live in
`tests/test_package_editor_sheetdata_by_name.cpp` and
`tests/test_package_editor_sheetdata_planned_catalog.cpp`. The new
`fastxlsx.package_editor.sheetdata-by-name` and
`fastxlsx.package_editor.sheetdata-planned-catalog` CTest names are added, and
`fastxlsx_package_editor_sheetdata_tests` now aggregates all three sheetData
executables. This is still test organization only.
The sheetData catalog executable is now split further:
`tests/test_package_editor_sheetdata_catalog.cpp` keeps catalog rename,
by-name helper, and source workbook catalog failure coverage, while
`tests/test_package_editor_sheetdata_catalog_guards.cpp` owns small-XML /
source-part materialization guardrails and
`tests/test_package_editor_sheetdata_catalog_audits.cpp` owns worksheet
replacement payload / relationship audit policy coverage. The existing
`fastxlsx.package_editor.sheetdata-catalog` CTest name stays stable for the
catalog core shard, the new catalog guard/audit shard names are added, and
`fastxlsx_package_editor_sheetdata_catalog_tests` remains a build-only
aggregate. This is still test organization only.
The follow-up WorkbookEditor public-shard split keeps
`tests/test_workbook_editor_public.cpp` as the base public shard and moves the
retry, public-state, and guardrail bodies into
`tests/test_workbook_editor_public_retry.cpp`,
`tests/test_workbook_editor_public_state.cpp`, and
`tests/test_workbook_editor_public_guards.cpp`. The existing
`fastxlsx.workbook_editor.public` CTest name stays stable, the new
`fastxlsx.workbook_editor.public-retry`,
`fastxlsx.workbook_editor.public-state`, and
`fastxlsx.workbook_editor.public-guards` names are added, and
`fastxlsx_workbook_editor_tests` remains a build-only aggregate. This is still
test organization only.
The public-retry executable is now split further: the base retry cases remain
in `tests/test_workbook_editor_public_retry.cpp`, while reacquire state,
read/mutation guard, and projection coverage move into
`tests/test_workbook_editor_public_retry_reacquire.cpp`,
`tests/test_workbook_editor_public_retry_guards.cpp`, and
`tests/test_workbook_editor_public_retry_projection.cpp`, with shared helpers
in `tests/test_workbook_editor_public_retry_common.hpp`. The original
`fastxlsx.workbook_editor.public-retry` CTest name remains the core retry
shard; the new names are test-organization only.
The core public-retry failed-mutation recovery case now repeats a clean no-op
`save_as()` after the safe recovery output, proving the source package remains
unchanged, the output package XML stays stable, and materialized/replacement
diagnostics plus `last_edit_error()` remain clean.
The same core retry coverage now applies to the rename-back failed-save
dirty-state recovery path: after the safe recovery output, a clean no-op
`save_as()` keeps the source package unchanged, preserves the output package
XML, and leaves materialized/replacement diagnostics plus `last_edit_error()`
clean.
The rename-back failed-save reacquire recovery path now has the same clean
no-op `save_as()` gate after its second safe output, proving both handles stay
clean, source package bytes stay unchanged, output package XML remains stable,
and materialized/replacement diagnostics plus `last_edit_error()` stay clear.
The linked-object PackageEditor preservation shard split keeps
`tests/test_package_editor_preservation_linked.cpp` as the comments /
threaded-comments base shard and moves pivot/cache, external-link, and custom
XML item core coverage into
`tests/test_package_editor_preservation_linked_pivot.cpp`,
`tests/test_package_editor_preservation_linked_external_links.cpp`, and
`tests/test_package_editor_preservation_linked_custom_xml.cpp`. The existing
`fastxlsx.package_editor.preservation-linked` CTest name stays stable, the new
`fastxlsx.package_editor.preservation-linked-pivot`,
`fastxlsx.package_editor.preservation-linked-external-links`, and
`fastxlsx.package_editor.preservation-linked-custom-xml` names are added, and
`fastxlsx_package_editor_preservation_tests` remains a build-only aggregate.
This is still test organization only.
The linked-pivot preservation executable is now split further:
`tests/test_package_editor_preservation_linked_pivot.cpp` keeps the worksheet
rewrite and pivot-table part lifecycle coverage, while
`tests/test_package_editor_preservation_linked_pivot_cache_definition.cpp` and
`tests/test_package_editor_preservation_linked_pivot_cache_records.cpp` own the
cache-definition and cache-records lifecycle families. The existing
`fastxlsx.package_editor.preservation-linked-pivot` CTest name stays stable for
the pivot-table core shard, the new cache shard names are added, and
`fastxlsx_package_editor_preservation_linked_pivot_tests` remains a build-only
aggregate. This is still test organization only.
The linked custom XML preservation executable is now split further:
`tests/test_package_editor_preservation_linked_custom_xml.cpp` keeps worksheet
rewrite plus custom XML item lifecycle coverage, while
`tests/test_package_editor_preservation_linked_custom_xml_properties.cpp` and
`tests/test_package_editor_preservation_linked_custom_xml_ordering.cpp` own
custom XML properties and item/properties ordering coverage, with shared
helpers in
`tests/test_package_editor_preservation_linked_custom_xml_common.hpp`. The
existing `fastxlsx.package_editor.preservation-linked-custom-xml` CTest name
stays stable for the item core shard, the new properties/ordering shard names
are added, and
`fastxlsx_package_editor_preservation_linked_custom_xml_all_tests` remains a
build-only aggregate. This is still test organization only.
The same preservation lane also splits the large core shard further: the
base `tests/test_package_editor_preservation_core.cpp` keeps drawing,
unknown-extension, media, chart, and table coverage, while
`tests/test_package_editor_preservation_core_drawings.cpp` and
`tests/test_package_editor_preservation_core_docparts.cpp` hold the VML /
percent-decoded drawing and doc-part families. The doc-part lane is now split
again: `tests/test_package_editor_preservation_core_docparts.cpp` keeps the
workbook / worksheet families, `tests/test_package_editor_preservation_core_docparts_drawing.cpp`
owns the drawing family, `tests/test_package_editor_preservation_core_docparts_shared.cpp`
owns sharedStrings plus styles, and
`tests/test_package_editor_preservation_core_docparts_vba.cpp` owns VBA.
The existing `fastxlsx.package_editor.preservation-core` CTest name stays
stable, the new `fastxlsx.package_editor.preservation-core-drawings`,
`fastxlsx.package_editor.preservation-core-docparts-drawing`,
`fastxlsx.package_editor.preservation-core-docparts-shared`, and
`fastxlsx.package_editor.preservation-core-docparts-vba` names are added, and
`fastxlsx_package_editor_preservation_tests` remains a build-only aggregate.
This is still test organization only.
The preservation-removal executable now follows the same split pattern:
`tests/test_package_editor_preservation_removal.cpp` keeps the base
unknown-extension, workbook, worksheet, drawing, chart, and media removal
coverage, while
`tests/test_package_editor_preservation_removal_policy.cpp`,
`tests/test_package_editor_preservation_removal_workbook_parts.cpp`, and
`tests/test_package_editor_preservation_removal_drawing_parts.cpp` own the
policy-failure, workbook-owned part, and drawing/VML replacement-order
families. The existing `fastxlsx.package_editor.preservation-removal` CTest
name stays stable for the base shard, the new removal shard names are added,
and `fastxlsx_package_editor_preservation_removal_tests` remains a build-only
aggregate. This is still test organization only.
The base PackageEditor core executable is now split too:
`tests/test_package_editor_core.cpp` keeps no-op save, copy-original,
single-part replacement, staged chunk write, and staged chunk guardrails.
`tests/test_package_editor_core_worksheet.cpp` owns worksheet routing and
worksheet chunk-source replacement, and
`tests/test_package_editor_core_docprops.cpp` owns replacement-state plus
document-properties helpers. `tests/test_package_editor_core_calc.cpp` owns
calc metadata regressions, and `tests/test_package_editor_core_linked.cpp`
owns the linked-fixture plus minizip-backed cases. The existing
`fastxlsx.package_editor.core` CTest name stays stable,
`fastxlsx.package_editor.core-worksheet`,
`fastxlsx.package_editor.core-docprops`,
`fastxlsx.package_editor.core-calc`, and
`fastxlsx.package_editor.core-linked` are added, and
`fastxlsx_package_editor_tests` remains a build-only aggregate. This is still
test organization only.
The CellStore PackageEditor shard now follows the same pattern:
`tests/test_package_editor_cellstore.cpp` keeps `cellstore-core`, while
`tests/test_package_editor_cellstore_chunks.cpp`,
`tests/test_package_editor_cellstore_source.cpp`,
`tests/test_package_editor_cellstore_failures.cpp`, and
`tests/test_package_editor_cellstore_catalog.cpp` own the remaining CellStore
CTest shards. The existing `fastxlsx.package_editor.cellstore-*` CTest names
stay stable, and `fastxlsx_package_editor_cellstore_tests` remains a build-only
aggregate. This is still test organization only.
The StreamingWriter tests now follow the same executable split pattern:
`tests/test_streaming_writer.cpp` keeps the core smoke, docProps, compression,
dimension, bounds, append-state, file-backed, and generic guardrail coverage,
while `tests/test_streaming_writer_styles.cpp`,
`tests/test_streaming_writer_conditional_formatting.cpp`,
`tests/test_streaming_writer_metadata.cpp`,
`tests/test_streaming_writer_images.cpp`, and
`tests/test_streaming_writer_shared_strings.cpp` own the larger feature
families. The existing `fastxlsx.streaming` CTest name stays stable for the
core shard, new `fastxlsx.streaming.*` CTest names cover the split families,
and `fastxlsx_streaming_writer_tests` remains a build-only aggregate. This is
still test organization only.
The PackageEditor C5 lane is also split by executable now:
`tests/test_package_editor_c5.cpp` keeps the core file-backed transformer and
current-worksheet failure diagnostics, while
`tests/test_package_editor_c5_linked.cpp`,
`tests/test_package_editor_c5_chunked.cpp`,
`tests/test_package_editor_c5_guards.cpp`, and
`tests/test_package_editor_c5_large.cpp` own the linked-object, chunked-output,
staged-chunk guard, and large-window families. The existing
`fastxlsx.package_editor.c5` CTest name stays stable for the core shard, new
`fastxlsx.package_editor.c5-*` CTest names cover the split families, and
`fastxlsx_package_editor_c5_tests` remains a build-only aggregate. This is
still test organization only.
P8.584 extends the opt-in workbook-editor fixture QA runner with
`external_defined_name_fixture_smoke`: the Python layer scans external fixture
packages for direct workbook `definedNames`, runs a materialized-only public
editor smoke, compares definedName records before/after at ZIP/XML level,
records the C++ QA tool's public definedName formula-reference audit counters,
and can reuse the Excel COM sidecar. This is compatibility evidence for
supplied fixtures only; it is not name-manager editing, broad rename
synchronization, external link validation, or formula evaluation.
P8.585 adds a public combined editing failed-save recovery regression:
`test_public_workbook_editor_combined_failed_save_as_preserves_state()` queues
the representative mixed public flow from P8.560, then forces
`WorkbookEditor::save_as()` to fail at a missing-parent output path before dirty
materialized sessions are flushed. The test proves pending public counts,
whole-`sheetData` diagnostics, dirty materialized diagnostics, worksheet edit
summaries, planned catalog, borrowed `WorksheetEditor` dirty state, and
`last_edit_error()` survive the failure; a later safe save writes the renamed
catalog, materialized cells, refreshed dimension, sheetData replacement, and
memory-backed image bytes. This is retry-state hygiene for the public facade,
not transaction history, undo/rollback, source mutation, relationship repair,
or broad semantic object editing.
P8.586 strengthens the opt-in `generated_public_e2e` workbook-editor QA helper:
`tools/run_workbook_editor_qa.py --scenario generated_public_e2e` now validates
the combined public-edit output through ZIP/XML workbook catalog mapping,
worksheet/drawing relationship checks, media-byte comparison, and `openpyxl`
readback including sheet values and picture count. This is external QA evidence
for the existing public facade smoke only; it is not a runtime dependency, Excel
formula calculation, relationship repair/pruning, semantic image editing,
transaction history, undo/rollback, source mutation, or a large-file random
editing claim.
The generated public E2E lane now also has a no-op save variant that requires
the follow-up clean `save_as()` package to be byte-identical after the combined
rename/materialized edit, sheetData replacement, and image replacement output
has flushed.
The standalone generated image replacement lane now also has a no-op save
variant that requires byte-identical output after the media-byte replacement
has flushed, while still treating drawing relationships and media bytes as QA
evidence rather than semantic image editing or relationship repair.
C5 direct PackageReader ZIP-entry chunk work remains the large-worksheet
low-memory line.
The WorkbookEditor source-success executable is now split the same way:
`tests/test_workbook_editor_source_success.cpp` keeps the core supported-value,
inline-string, wrapper-metadata, and no-op save coverage, while
`tests/test_workbook_editor_source_success_shared_strings.cpp`,
`tests/test_workbook_editor_source_success_max_coordinate.cpp`, and
`tests/test_workbook_editor_source_success_formulas.cpp` own the sharedStrings,
max-coordinate, and formula families. The existing
`fastxlsx.workbook_editor_source_success` CTest name stays stable for the core
shard, and `fastxlsx_workbook_editor_source_success_tests` remains a
build-only aggregate. This is still test organization only.
Public `try_worksheet()` / `worksheet()` facade failure hygiene is pinned for
representative invalid sharedStrings metadata as well: the editor remains clean,
`last_edit_error()` is unchanged, and later Patch edits can still be saved.
The public-header and planning-doc wording now matches that behavior: valid
workbook-backed `t="s"` source cells materialize as text, malformed
sharedStrings structures/targets or invalid indexes fail fast, non-critical
`count` / `uniqueCount` metadata does not drive materialization, and standalone
worksheet XML/chunk loaders still reject `t="s"` without workbook-level table
context.
Source style id public facade hygiene is also pinned: explicit default `s="0"`
source style attributes materialize as no style handle and dirty projection
omits `s="0"` / `s='0'` / `s = "0"`, while workbook-backed canonical non-zero
source style ids now validate against the source styles.xml `cellXfs` table and
write the same numeric id back when the source styles part is preserved. Missing
or invalid styles metadata and out-of-range ids fail without dirtying the editor
or blocking later Patch edits. This remains same-workbook passthrough only, not
style migration or merge. The normalization is exact-value only: empty,
valueless, unquoted, unterminated, padded, signed, leading-zero, entity-encoded,
or duplicate default-like source style attributes still fail instead of being
coerced to default style, and duplicate exact default-style attributes now have
public facade hygiene coverage. Qualified style-like attributes such as
`x:s="0"` also fail as unsupported cell metadata, not as default-style
normalization.
The generated style passthrough QA lane now also has a no-op save variant that
requires the follow-up clean `save_as()` package to be byte-identical after the
existing non-default style id passthrough output has flushed.
Caller-supplied explicit default `StyleId{0}` on
`WorksheetEditor::set_cell()` is normalized to no style handle: readback and
`sparse_cells()` snapshots do not expose a default style, and dirty save-as
projection omits `s="0"`. This remains default-style normalization only, not
non-default style migration or existing-workbook style registry support.
`WorksheetCellSnapshot` wording now matches that current contract: snapshot
`CellValue` payloads can carry materialized source `StyleId` handles when a
sparse record has one, but snapshots still do not expose workbook style table
details, worksheet metadata, or any style migration/merge surface.
Default public-state coverage now pins that snapshot contract directly:
`sparse_cells()`, bounded range snapshots, strict A1 range snapshots,
coordinate-batch snapshots, `row_cells()`, and `column_cells()` all expose the
source-backed style handle on a styled `A1` value, keep unstyled records
unstyled, then preserve the same style handle on an explicit blank after
clear/save/reopen/no-op.
The lighter public snapshot shard now also carries source-style value-only
coverage: a writer-generated styled source workbook preserves the materialized
`StyleId` through `set_cell_value()`, keeps unstyled cells and newly inserted
cells unstyled, preserves `xl/styles.xml`, fresh-reopens through
`sparse_cells()`, `row_cells()`, and `column_cells()`, and settles into a
byte-stable no-op `save_as()`. This is snapshot/readback evidence only, not
style migration, style merge, or a public style registry for existing files.
The same source-style snapshot regression now also covers the
`sparse_cells(initializer_list<WorksheetCellReference>)` convenience overload:
initializer-list reads expose the styled source handle, skip missing
coordinates, keep unstyled cells unstyled, and preserve the styled blank after
save/reopen/no-op.
The reopened/no-op half of the same regression now also covers strict A1 range
and span-batch snapshot reads: `sparse_cells("A1:B1")` and
`sparse_cells(span<WorksheetCellReference>)` both read the saved styled blank,
keep saved `B1` unstyled, and preserve sparse missing-coordinate skip
semantics.
The same source-style snapshot path now also performs a second clean no-op
`save_as()` after the first byte-stable no-op output, snapshots public
catalog/save-state before that save, requires identical package entries, and
fresh-reopens the second no-op workbook through the same styled snapshot checks.
After that second clean no-op, the same saved materialized handle is now edited
again: value-only A1/B1 changes preserve the styled `A1` handle, keep `B1`
unstyled, leave earlier outputs unchanged, and fresh-reopen the post-noop output
through all covered snapshot overloads.
The post-noop value-edit path now also performs a clean no-op `save_as()` after
the edited output, proves the no-op package entries remain byte-stable,
preserves public catalog/save-state, and fresh-reopens the no-op output through
the same snapshot overload checks.
The same source-style snapshot flow now also rechecks the original source
workbook after the repeated saves: source package entries remain unchanged and a
fresh reopen still sees original styled `A1=1.0`, unstyled `B1`, and unstyled
`A2`.
That post-noop no-op output is now also opened through a fresh editor, edited at
`A2`, saved again, and fresh-reopened to prove the repeated-save output remains
reusable while the source workbook and prior no-op output stay unchanged.
That fresh-reopened edit output now has the same clean no-op save coverage:
public catalog/save-state stay stable, the no-op output is byte-equivalent, and
the source plus earlier no-op output remain unchanged.
That second-generation no-op output is now reopened again, clears the styled
`A1`, and saves a styled blank output to prove the source style handle still
survives value clearing after repeated reopen/no-op cycles.
The same source-style snapshot chain now runs under a dedicated
`fastxlsx.workbook_editor.public-state-source-style` CTest shard, preserving the
coverage while returning timeout margin to the base public-state shard.
That isolated shard now also no-op saves the styled blank output, proving the
clear output remains byte-stable and readable without loading the base
public-state shard.
The isolated source-style shard now also reopens that styled-blank no-op
output, re-edits `A1` to a styled numeric value, and saves/readbacks the result
while proving source and prior no-op bytes stay unchanged.
That re-edit output now has matching clean no-op coverage: public
catalog/save-state remain stable, the no-op package is byte-equivalent, and
fresh readback still sees styled `A1=4.75`.
Value-only explicit-default style coverage now also includes the single-cell,
batch, and prefix helpers: `set_cell_value()`, `set_cell_values()`,
`set_row_values()`, and `set_column_values()` all accept caller-supplied
`StyleId{0}` as no caller style, preserve the materialized source `StyleId` on
overwritten styled targets, keep overwritten unstyled blanks and inserted
formula cells unstyled, preserve `xl/styles.xml` bytes, omit `s="0"` on dirty
save-as output, and keep repeated clean no-op saves byte-stable. This remains
the small-file In-memory value-only style boundary, not style-table migration,
foreign style adoption, or sharedStrings/styles rebuild.
The single-cell, sparse-batch, and row/column prefix value-only
explicit-default style regressions now also assert `contains_cell()` across
live, fresh-reopen, and clean no-op readback checks: source-styled targets,
unstyled blank/untouched cells, and inserted formulas remain represented, while
unrelated missing `D4` stays absent.
Those value-only explicit-default style regressions now also check
`row_cells()` and `column_cells()` across the same live, fresh-reopen, and clean
no-op stages: styled value-only overwrites, unstyled explicit blanks, inserted
formulas, untouched source cells, and sparse row/column gaps project consistently
without changing the small-file In-memory style boundary.
The explicit-default-style single-cell full replacement path now mirrors that
public view coverage: overwritten source-backed A1 drops its prior `StyleId`,
the untouched B1 tail stays unstyled, and missing row/column gaps stay absent
across live, fresh-reopen, and clean no-op `contains_cell()`, `row_cells()`, and
`column_cells()` checks.
The explicit-default-style sparse batch replacement path now has matching
public view coverage for full replacement semantics: duplicate later-wins
`set_cells()` targets, an overwritten unstyled formula, an untouched styled
source cell, and missing sparse gaps stay consistent across live, fresh-reopen,
and clean no-op `contains_cell()`, `row_cells()`, and `column_cells()` checks.
The explicit-default style single-cell, sparse-batch, row/column
full-replacement, and append paths now also repeat the clean no-op save:
public catalog/save-state snapshots stay stable, the second no-op packages are
byte-identical to the first, source and materialized outputs remain unchanged,
and fresh reopen still reads unstyled replacement or appended cells plus
untouched source styles.
The single-cell full-replacement path now continues past that second clean
no-op save as well: the same `WorksheetEditor` handle can replace `A1` with an
explicit-default-style formula, save a fresh post-noop output, keep prior
packages byte-stable, omit `s="0"`, and fresh-reopen the unstyled formula plus
the untouched tail cell. This remains small-file In-memory style-boundary QA,
not caller-supplied non-default style migration.
The sparse-batch full-replacement path now mirrors that post-noop edit/save
coverage: after the second clean no-op package, the same handle can batch
replace `A1` with an explicit-default-style formula and insert unstyled `D1`,
while `B1` remains unstyled, styled source `C1` keeps its `StyleId`, prior
packages stay byte-stable, and fresh reopen observes the expanded sparse row.
The row full-replacement path now follows that same post-noop lane: after the
second clean no-op package, the same handle can replace row 1 with
explicit-default-style formula/text/blank/boolean cells, keep row 2's source
`StyleId`, leave prior packages byte-stable, omit `s="0"`, and fresh-reopen the
expanded row. This remains small-file In-memory style-boundary QA, not
caller-supplied non-default style migration.
The column full-replacement path now mirrors the row lane: after the second
clean no-op package, the same handle can replace column A with
explicit-default-style formula/text/blank/boolean cells, keep column B's source
`StyleId`, leave prior packages byte-stable, omit `s="0"`, and fresh-reopen the
expanded column under the same small-file In-memory boundary.
The value-only row prefix path now has matching post-noop evidence: after the
second clean no-op package, `set_row_values()` can rewrite row 1 with
explicit-default-style formula/text/blank/boolean values, preserve A1's source
`StyleId`, keep newly represented cells unstyled, leave prior packages
byte-stable, omit `s="0"`, and fresh-reopen the expanded row. This remains
style-preserving value replacement, not caller-supplied non-default style
migration.
The value-only column prefix path now mirrors that post-noop evidence: after the
second clean no-op package, `set_column_values()` can rewrite column A with
explicit-default-style formula/text/blank/boolean values, preserve A1's source
`StyleId`, keep newly represented cells unstyled, leave prior packages
byte-stable, omit `s="0"`, and fresh-reopen the expanded column under the same
small-file In-memory style boundary.
Styled sparse clear coverage now also includes the range and coordinate-batch
helpers under the same isolated source-style shard:
`clear_cell_values(CellRange)`, strict A1-range `clear_cell_values()`,
`clear_cell_values(span<WorksheetCellReference>)`, and the initializer-list
overload preserve source `StyleId` handles on represented styled cells, keep
represented unstyled cells as unstyled blanks, skip missing coordinates without
synthesizing new cells, preserve `xl/styles.xml` bytes, omit cleared payloads,
and keep the clean no-op save byte-stable. This remains value clearing over the
small-file In-memory sparse store, not dense range editing, style migration, or
range metadata repair.
The same range/batch clear regression now also asserts `contains_cell()` reports
the represented styled and unstyled blank coordinates and keeps missing batch
coordinates absent across live, fresh-reopen, and clean no-op readback checks.
It now repeats a second clean no-op `save_as()` as well, requiring byte-identical
range/batch clear packages, stable public save/catalog snapshots, fresh reopen
of the cleared sparse cells, and unchanged source bytes.
Caller-supplied non-default `StyleId` values on `WorksheetEditor::set_cell()`
are rejected before sparse-store mutation: the public diagnostic is updated,
the materialized session stays clean, no pending edit is queued, and a later
no-op `save_as()` remains copy-original. This is covered for both row/column
and strict A1 `set_cell()` overloads. This is still rejection-only hygiene, not
style migration, merge, or preservation.
Strict A1 mutation reference rejection now has the same default public-state
save/no-op evidence: lowercase `set_cell("a1", ...)` and range-shaped
`erase_cell("A1:B2")` keep the sparse store clean, preserve the public diagnostic
across copy-original and follow-up no-op saves, and reopen with the source-backed
`Data` sheet unchanged. This remains single-cell A1 validation hygiene, not A1
range mutation expansion or XML repair.
The generated non-default style rejection QA lane now also has a no-op save
variant that requires the follow-up clean `save_as()` package to be
byte-identical after the copy-original recovery output has flushed.
Default public-state coverage now pins the same save hygiene for value-only
row/column style rejection: rejected `set_row_values()` and
`set_column_values()` calls keep the materialized session clean, preserve the
public diagnostic across a copy-original save and follow-up no-op save, and
reopen with the source-backed `Data` sheet unchanged. This remains rejection-only
guardrail coverage, not caller-supplied non-default style writes or style
migration.
The full-cell `set_row()` / `set_column()` rejection paths now have the same
default public-state save/no-op coverage, including retained failure diagnostics,
source-identical recovery output, byte-identical follow-up no-op output, and
clean materialized/replacement diagnostics.
Sparse batch initializer-list rejection now has the same default public-state
save/no-op coverage: rejected `set_cells()` and `set_cell_values()` calls with
caller-supplied non-default `StyleId` values keep source-backed `Data` cells
unchanged, preserve the public failure diagnostic across copy-original and
follow-up no-op saves, and reopen unchanged. This remains rejection-only
guardrail coverage, not dense range editing, caller-supplied non-default style
writes, style migration, or relationship repair.
Single-cell value-only style rejection now has matching default public-state
coverage for both `set_cell_value(row, column, ...)` and strict A1
`set_cell_value("A1", ...)`: rejected non-default `StyleId` payloads keep
source-backed `Data` cells unchanged, preserve the public diagnostic across
copy-original and follow-up no-op saves, and reopen unchanged. This does not
add caller-supplied non-default style writes or style migration.
Single-cell value-only guardrails now also pin row/column and strict A1
validation paths: row-zero and column-overflow
`set_cell_value(row, column, ...)`, lowercase `set_cell_value("a1", ...)`,
column-overflow `set_cell_value("XFE1", ...)`, and `max_cells` rejection for
`set_cell_value("C3", ...)` leave source-backed `Data` cells unchanged, preserve
the public diagnostic across copy-original and follow-up no-op saves, and reopen
unchanged. This remains small-file in-memory validation coverage, not A1 range
mutation expansion, coordinate clamping, or dense allocation.
Full-cell and value-clear coordinate/A1 mutation guardrails now have matching
direct save/no-op evidence: `set_cell()` row-zero / column-overflow rejections,
`set_cell("a1", ...)` lowercase-A1 rejection, `erase_cell()` row-overflow /
column-zero rejections, `erase_cell("A1:B2")` range-reference rejection, and
`clear_cell_value()` row-zero / column-overflow / lowercase-A1 rejections leave
source-backed `Data` cells unchanged, preserve the public diagnostic across
copy-original and follow-up no-op saves, omit rejected payloads from saved
worksheet XML when a payload exists, and reopen unchanged. This is
coordinate/reference validation hygiene, not coordinate clamping, range
expansion, dense allocation, value-clear style migration, or XML repair.
`append_row()` style rejection now has matching default public-state save/no-op
coverage: a rejected caller-supplied non-default `StyleId` append leaves the
sparse count and source `Data` cells unchanged, keeps the appended row absent,
preserves the public diagnostic across copy-original and follow-up no-op saves,
and reopens unchanged. This is still append guardrail hygiene, not row insertion,
row metadata creation, or style migration.
The isolated source-style shard now also groups caller-supplied non-default
`StyleId` rejection public-view checks across full-cell, value-only, row/column,
and append helpers: repeated rejected mutations keep `contains_cell()`,
`sparse_cells()`, `row_cells()`, and `column_cells()` on the source-backed styled
`Data` sheet unchanged across live, copy-original save, and clean no-op save
readback. This is rejection hygiene only, not non-default style writes or style
migration.
That grouped source-style rejection path now also has an invalid-to-valid
recovery branch: after the copy-original/no-op saves, a valid explicit-default
`set_cell()` clears `last_edit_error()`, drops the overwritten source style,
saves/reopens as an unstyled A1 replacement, keeps rejected payloads out of the
worksheet XML, and leaves the follow-up clean no-op output byte-stable.
The same recovery branch now also reuses the recovered clean handle for a
second valid explicit-default value-only edit: `set_cell_value("B1", ...)`
stays diagnostic-clean, saves/reopens with both unstyled recovery values, keeps
rejected payloads absent, preserves the original `styles.xml` bytes, and leaves
the follow-up no-op output byte-stable. This remains failure-recovery hygiene,
not caller-supplied non-default style writes or style migration.
Dirty-session style rejection now has the matching failure-before-state-change
coverage: after a valid explicit-default value-only `B1` edit dirties the
materialized sheet, a rejected caller-supplied non-default `StyleId` full-cell
write keeps that dirty value and source-styled `A1` intact, saves/reopens
without leaking the rejected payload, preserves `styles.xml`, retains the public
diagnostic across save/no-op save, and keeps the no-op package byte-stable.
That dirty-session branch now also proves a later valid explicit-default
`A2` full-cell recovery clears the retained `StyleId` diagnostic, preserves the
source-styled `A1` and prior dirty unstyled `B1`, writes the recovered `A2` as
unstyled, keeps the rejected payload absent, and leaves the recovery no-op
output byte-stable.
`set_cells()` now has the matching sparse-batch full-replacement
dirty-session style-rejection/recovery reuse coverage: an explicit-default
batch can dirty row 2, a rejected non-default `StyleId` batch keeps that dirty
batch and source-styled `A1` intact across save/no-op, and a later
explicit-default batch recovery clears the retained diagnostic, writes only
unstyled recovered batch cells, keeps rejected payloads absent, preserves
`styles.xml`, and leaves the recovery no-op output byte-stable. This remains
sparse full-cell batch failure-recovery hygiene, not caller-supplied
non-default style writes, style migration, or sharedStrings/styles rebuild.
`set_cell_values()` now has the matching sparse-batch value-only
dirty-session style-rejection/recovery reuse coverage: an explicit-default
value batch can dirty row 2, a rejected non-default `StyleId` value batch keeps
that dirty batch and source-styled `A1` intact across save/no-op, and a later
explicit-default value batch recovery clears the retained diagnostic, writes
only unstyled recovered values, keeps rejected payloads absent, preserves
`styles.xml`, and leaves the recovery no-op output byte-stable. This remains
value-only failure-recovery hygiene, not caller-supplied non-default style
writes, style migration, or sharedStrings/styles rebuild.
`set_row()` now has the same dirty-session style-rejection/recovery reuse
coverage: an explicit-default row replacement can dirty row 2, a rejected
non-default `StyleId` row replacement keeps that dirty row and source-styled
`A1` intact across save/no-op, and a later explicit-default row recovery clears
the retained diagnostic, writes only unstyled row-2 replacement cells, keeps the
rejected payload absent, preserves `styles.xml`, and leaves the recovery no-op
output byte-stable. This remains row replacement failure-recovery hygiene, not
caller-supplied non-default style writes or style migration.
`set_column()` now has the symmetric dirty-session style-rejection/recovery
reuse coverage: an explicit-default column replacement can dirty column B, a
rejected non-default `StyleId` replacement keeps that dirty column and
source-styled `A1` intact across save/no-op, and a later explicit-default column
recovery clears the retained diagnostic, writes only unstyled column-B
replacement cells, keeps the rejected payload absent, preserves `styles.xml`,
and leaves the recovery no-op output byte-stable. This remains column
replacement failure-recovery hygiene, not caller-supplied non-default style
writes or style migration.
`set_row_values()` now has the matching value-prefix dirty-session
style-rejection/recovery reuse coverage: an explicit-default value edit can
dirty row 2, a rejected non-default `StyleId` value-prefix edit keeps that dirty
row and source-styled `A1` intact across save/no-op, and a later
explicit-default value-prefix recovery clears the retained diagnostic, writes
only unstyled row-2 value cells, keeps rejected payloads absent, preserves
`styles.xml`, and leaves the recovery no-op output byte-stable. This remains
value-only failure-recovery hygiene, not caller-supplied non-default style
writes, style migration, or sharedStrings/styles rebuild.
`set_column_values()` now has the symmetric value-prefix dirty-session
style-rejection/recovery reuse coverage: an explicit-default value edit can
dirty column B, a rejected non-default `StyleId` value-prefix edit keeps that
dirty column and source-styled `A1` intact across save/no-op, and a later
explicit-default value-prefix recovery clears the retained diagnostic, writes
only unstyled column-B value cells, keeps rejected payloads absent, preserves
`styles.xml`, and leaves the recovery no-op output byte-stable. This remains
value-only failure-recovery hygiene, not caller-supplied non-default style
writes, style migration, or sharedStrings/styles rebuild.
`append_row()` now has the same dirty-session style-rejection/recovery reuse
coverage: an explicit-default value edit can dirty an existing source row, a
rejected non-default `StyleId` append keeps that dirty value, the source-styled
`A1`, and the next append row absent across save/no-op, and a later
explicit-default append recovery clears the retained diagnostic, writes only
unstyled appended cells, keeps rejected payloads absent, preserves
`styles.xml`, and leaves the recovery no-op output byte-stable. This remains
append failure-recovery hygiene, not row insertion semantics, caller-supplied
non-default style writes, style migration, or sharedStrings/styles rebuild.
`append_row()` width validation failure now has the same copy-original/no-op
save coverage: attempts to append more than 16,384 values keep the clean source
session unchanged, retain the width diagnostic across both saves, leave the
would-be appended row absent, and reopen unchanged. This does not add dense
append, coordinate clamping, row insertion, or rollback machinery.
`append_row()` row-limit failure now has the dirty-session counterpart: a
pre-existing sparse `XFD1048576` edit survives the rejected append past row
1,048,576, the saved output expands to `A1:XFD1048576` without leaking the
rejected value, and the follow-up no-op save is byte-identical. This remains
save hygiene for a validation failure, not row insertion, metadata/range
synchronization, or rollback machinery.
`append_row()` exact `max_cells` rejection now has matching copy-original/no-op
coverage: with a clean source-backed session already at the configured cell
budget, the rejected append preserves source cells, keeps `A3` absent, retains
the `max_cells` diagnostic across both saves, and reopens unchanged. This is
budget guardrail hygiene only, not budget auto-sizing or rollback machinery.
The follow-up exact-budget append recovery path now also pins saved-handle
sparse, row, and column snapshots after erasing a source cell and appending a
replacement in the same session, keeping rejected `A3` absent and diagnostics
clean across the no-op save.
`set_cells()` validation and exact `max_cells` rejection now have matching
copy-original/no-op coverage: invalid batch coordinates and a new full sparse
replacement target over the configured cell budget preserve the clean
source-backed `Data` session, retain public diagnostics across both saves, keep
materialized and replacement diagnostics empty, and reopen unchanged. This is
full sparse-batch rejection hygiene only, not coordinate clamping, budget
auto-sizing, or rollback machinery.
`set_cell_values()` validation and exact `max_cells` rejection now have matching
copy-original/no-op coverage: invalid batch coordinates and a new sparse target
over the configured cell budget preserve the clean source-backed `Data`
session, retain public diagnostics across both saves, keep materialized and
replacement diagnostics empty, and reopen unchanged. This is sparse value-batch
rejection hygiene only, not coordinate clamping, budget auto-sizing, or rollback
machinery.
`set_cell_value()` now has the matching single value-only validation and exact
`max_cells` copy-original/no-op coverage: row-zero edits and a new sparse target
over the configured cell budget preserve the clean source-backed `Data`
session, retain public diagnostics across both saves, keep materialized and
replacement diagnostics empty, and reopen unchanged. This is single-cell
value-only rejection hygiene only, not coordinate clamping, budget auto-sizing,
or rollback machinery.
`set_cell_value()` now also has single value-only memory-budget failure and
same-handle recovery coverage: an oversized missing-cell write over an exact
`memory_budget_bytes` budget preserves the clean sparse store, copies the
source package on failure save, then a short in-budget A1 value-only edit clears
diagnostics, saves, reopens, and no-op saves byte-stable without leaking the
rejected D4 payload. This is small-file in-memory budget hygiene only, not
low-memory random editing or rollback machinery.
`set_row()` validation failures now have matching copy-original/no-op coverage
for oversized row payloads, row zero, and row overflow: rejected calls preserve
the clean source-backed `Data` session, retain public diagnostics across both
saves, keep materialized/replacement diagnostics empty, and reopen unchanged.
This is full-row validation hygiene only, not coordinate clamping or rollback
machinery.
`set_column()` column-zero and column-overflow validation failures now have the
same save/no-op coverage: rejected full-column calls preserve the clean
source-backed `Data` session, retain the public diagnostic across both saves,
keep materialized/replacement diagnostics empty, and reopen unchanged. This is
full-column validation hygiene only, not coordinate clamping or rollback.
`set_row_values()` validation and exact `max_cells` rejection now have matching
copy-original/no-op coverage: row zero, row overflow, and a new row-prefix
write over the configured sparse cell budget preserve the clean source-backed
`Data` session, retain public diagnostics across both saves, keep
materialized/replacement diagnostics empty, and reopen unchanged. This is
value-prefix rejection hygiene only, not dense row writes, row insertion,
budget auto-sizing, or rollback.
`set_column_values()` now has the symmetric validation and exact `max_cells`
copy-original/no-op coverage: column zero, column overflow, and a new
column-prefix write over the configured sparse cell budget preserve the clean
source-backed `Data` session, retain public diagnostics across both saves,
keep materialized/replacement diagnostics empty, and reopen unchanged. This is
column value-prefix rejection hygiene only, not dense column writes, column
insertion, budget auto-sizing, or rollback.
`clear_row()` / `clear_rows()` validation failures now have copy-original/no-op
coverage as well: row zero, row overflow, overflow row ranges, and reversed row
ranges preserve the clean source-backed `Data` session, retain public
diagnostics across both saves, keep materialized/replacement diagnostics empty,
and reopen unchanged. This is row clear validation hygiene only, not row
metadata creation, dense materialization, range repair, or rollback.
`clear_column()` / `clear_columns()` now have the symmetric validation
copy-original/no-op coverage: column zero, column overflow, overflow column
ranges, and reversed column ranges preserve the clean source-backed `Data`
session, retain public diagnostics across both saves, keep
materialized/replacement diagnostics empty, and reopen unchanged. This is
column clear validation hygiene only, not column metadata creation, dense
materialization, range repair, or rollback.
`erase_row()` / `erase_rows()` validation failures now have the same
copy-original/no-op coverage: row zero, row overflow, overflow row ranges, and
reversed row ranges preserve the clean source-backed `Data` session, retain
public diagnostics across both saves, keep materialized/replacement diagnostics
empty, and reopen unchanged. This is row erase validation hygiene only, not row
metadata deletion semantics, dense materialization, range repair, or rollback.
`erase_column()` / `erase_columns()` now have the symmetric validation
copy-original/no-op coverage: column zero, column overflow, overflow column
ranges, and reversed column ranges preserve the clean source-backed `Data`
session, retain public diagnostics across both saves, keep
materialized/replacement diagnostics empty, and reopen unchanged. This is
column erase validation hygiene only, not column metadata deletion semantics,
dense materialization, range repair, or rollback.
Valid missing `erase_row()` / `erase_column()` calls now also have default
copy-original/no-op save coverage after clearing a prior edit diagnostic. The
calls stay clean, preserve source-backed `Data`, emit source-identical outputs
across both saves, and reopen unchanged. This is missing row/column erase
clean-save hygiene only, not erase tombstones, missing-cell synthesis, metadata
deletion semantics, or rollback.
Empty whole-store `clear_cell_values()` / `erase_cells()` calls now also have
default no-op save coverage after clearing prior edit diagnostics on an already
empty saved materialized session. The calls stay clean, preserve public
save-state/catalog diagnostics, emit byte-stable outputs, and reopen empty. This
is empty whole-store clear/erase clean-save hygiene only, not tombstones, dense
worksheet deletion, metadata repair, or source reload.
Valid missing `clear_row()` / `clear_column()` calls now also have default
copy-original/no-op save coverage after clearing a prior edit diagnostic. The
calls stay clean, preserve source-backed `Data`, emit source-identical outputs
across both saves, and reopen unchanged. This is missing row/column clear
clean-save hygiene only, not dense row/column materialization, missing-cell
synthesis, metadata creation, or rollback.
Zero-count row/column shifts now also have default copy-original/no-op save
coverage after clearing a prior edit diagnostic. `insert_rows(..., 0)`,
`delete_rows(..., 0)`, `insert_columns(..., 0)`, and `delete_columns(..., 0)`
stay clean, preserve source-backed `Data`, emit source-identical outputs across
both saves, and reopen unchanged. This is zero-count shift clean-save hygiene
only, not structural metadata movement, dimension repair, or relationship sync.
Shift start-coordinate overflow validation now shares that copy-original/no-op
coverage for nonzero shifts: `insert_rows(1048577, 1)`,
`delete_rows(1048577, 1)`, `insert_columns(16385, 1)`, and
`delete_columns(16385, 1)` fail before dirtying state, preserve source-backed
`Data`, emit source-identical outputs across both saves, and reopen unchanged.
This is input validation save hygiene only, not metadata movement, dimension
repair, rollback, or relationship sync.
Pending materialized worksheet-name and aggregate diagnostics now also have
diagnostics-specific no-op save coverage after a two-sheet materialized flush.
The first save clears dirty names, aggregate cell count, and memory diagnostics;
the follow-up clean `save_as()` preserves public save-state/catalog snapshots,
keeps materialized/replacement diagnostics empty, emits byte-stable package
entries, and reopens both sheets unchanged. This is diagnostics save hygiene
only, not commit/close semantics, transaction history, metadata repair, or
relationship sync.
Those diagnostics-specific paths now repeat the clean no-op save as well,
proving the second no-op package remains byte-identical to the first no-op
package while source bytes, diagnostics, public save-state/catalog snapshots,
and reopened worksheet readback all stay stable.
Materialized worksheet-name move ownership now has the same follow-up no-op save
coverage. After move construction and move assignment transfer the dirty `Data`
session and discard the target's dirty `Untouched` session, the saved output and
the clean no-op output stay byte-stable, diagnostics stay empty, and both sheets
reopen with the moved data and untouched source cells intact. This is
move-ownership save hygiene only, not transaction transfer semantics, rollback,
or moved-handle guarantees beyond the current public diagnostics contract.
That move-ownership path now repeats the clean no-op save as well: the second
no-op package matches the first no-op package, both source packages remain
unchanged, public save-state/catalog snapshots stay stable, and both worksheets
fresh-reopen with the moved dirty payload only.
Materialized edit-summary move ownership now mirrors that save hygiene: after
move construction and move assignment transfer the dirty `Data` summary, the
first save clears dirty summaries and the clean no-op save preserves public
save-state/catalog snapshots, keeps diagnostics empty, emits byte-stable
package entries, and reopens both sheets unchanged. This is summary diagnostics
hygiene only, not rollback, moved-handle semantics, metadata repair, or
relationship sync.
That summary move-owner path also rechecks the prior assigned output and first
no-op output after the second clean no-op save, proving later clean saves do not
rewrite already-emitted packages.
Mixed materialized/replacement summaries now also have that no-op save pin:
after dirty `Data` materialized edits are saved beside a queued `Untouched`
replacement, the materialized summary clears, the replacement summary remains
stable, the clean no-op save preserves public save-state/catalog snapshots,
emits byte-stable package entries, and reopens both sheets unchanged. This is
retained-summary save hygiene only, not commit semantics, replacement cleanup,
metadata repair, or relationship sync.
That retained replacement-summary path also rechecks the first materialized
output and first no-op output after the second clean no-op save, proving later
clean saves do not rewrite already-emitted mixed-summary packages.
Move-owned aggregate materialized diagnostics now have the same save/no-op
coverage: moved cell-count and memory diagnostics survive move construction and
move assignment, the first save clears the aggregate diagnostics and drops the
discarded target dirty payload, and the clean no-op save stays byte-stable while
both sheets reopen unchanged. This is aggregate diagnostics hygiene only, not
transaction transfer semantics, rollback, metadata repair, or relationship sync.
The aggregate move-owner path now also repeats that clean no-op save, proving
the second no-op package matches the first, aggregate diagnostics remain empty,
both source packages are preserved, and both worksheets reopen with only the
moved dirty payload while keeping the prior aggregate move outputs byte-stable.
Public row/column `WorksheetEditor` overloads now have an explicit coordinate
guardrail matching the A1 overload boundary: rows and columns must stay within
Excel limits, invalid reads throw without changing `last_edit_error()`, and
invalid `set_cell()` / `erase_cell()` calls update the edit diagnostic without
dirtying or mutating the materialized sparse store. This is input validation
only, not coordinate inference, clamping, dense range reads, or large-file
random access.
Public matching-option reacquire behavior is pinned as well: repeated
`worksheet()` / `try_worksheet()` calls for the same planned sheet reuse the
same materialized sparse-store session, so dirty edits remain visible through
all borrowed handles and `save_as()` flushes the reused session once. Mismatched
options and same-sheet Patch replacement mixing still fail. This is handle
state hygiene only, not transaction history, clean-session commit semantics, or
large-file random editing.
The same reacquire contract is pinned after a successful `save_as()`: once a
dirty session has been flushed and marked clean, later matching
`try_worksheet()` / `worksheet()` calls still reuse that materialized state
instead of reloading the original source worksheet and restoring stale source
values. Later edits through the reacquired handle remain visible through older
handles and flush as a later materialized handoff.
Post-save matching reacquire also keeps dirty materialized diagnostics clean:
`pending_materialized_worksheet_names()`, `pending_materialized_cell_count()`,
and `estimated_pending_materialized_memory_usage()` stay empty/zero until a
later mutation actually dirties the reused session, then clear again after the
next successful `save_as()`.
Post-save option mismatch is pinned as the corresponding failure path:
`try_worksheet()` and `worksheet()` with different `WorksheetEditorOptions`
still fail against an existing saved materialized session, without updating
`last_edit_error()`, dirtying materialized diagnostics, losing saved values, or
blocking later valid matching-option edits.
The failed-save retry/reacquire variant now also repeats a clean no-op
`save_as()` after that later matching-option edit: public save-state/catalog
snapshots stay stable, materialized/replacement diagnostics and pending edit
summaries stay empty, the no-op package matches the second safe output, the
source package remains unchanged, and fresh reopen reads the saved A1, source
B1/A2, and post-mismatch B2 values as clean sparse state.
Missing-sheet lookups against the same saved/reacquired session now match that
boundary: empty optional lookup and throwing lookup preserve diagnostics,
handles, saved cells, catalog state, and no-op-save output stability.
The optional `try_worksheet()` failed-save retry/reacquire branch now backs that
no-op-save boundary directly: after missing and transient-name lookups return
empty and a later matching-option edit saves, the clean no-op output matches
the second safe output, the source package remains unchanged, and fresh reopen
reads the saved A1, source B1/A2, and post-missing-try B2 values as clean sparse
state.
The throwing `worksheet()` missing-query retry/reacquire branch now mirrors that
same proof after old transient-name and missing-name lookups throw: the later
matching-option edit can save, the clean no-op output remains byte-stable, the
source package stays unchanged, and fresh reopen reads the saved A1,
source-backed B1/A2, and post-missing-worksheet B2 values as clean sparse state.
Invalid read preflights now cover the next read-only failure branch on the same
whole-store value-clear session: invalid coordinates, invalid A1/range/batch
reads, invalid row/column snapshots, and valid-but-missing `get_cell()` preserve
diagnostics, handles, saved cells, catalog state, and no-op-save output
stability.
Invalid mutation preflights now cover the adjacent diagnostic branch: rejected
`set_cell()` / `erase_cell()` calls preserve the invalid-reference
`last_edit_error()` while keeping handles, saved cells, catalog state,
materialized diagnostics, rejected payloads, and no-op-save output stable.
The follow-up recovery path is pinned too: a later valid mutation clears that
diagnostic, dirties the shared saved/reacquired session once, expands the
persisted bounds, and reopens clean after the next materialized handoff.
Invalid row/column shift preflights now cover the same recovered session:
rejected structural bounds failures preserve the shift diagnostic without
dirtying handles or changing the recovery output on a later no-op save.
The valid-shift recovery branch is pinned as well: a later `insert_rows()`
clears the shift diagnostic, dirties the shared handles once, moves the
recovered sparse cell, and reopens clean after the next materialized handoff.
Same-sheet Patch operation-mixing guards are now pinned on that recovered
session too: rejected `rename_sheet()` / `replace_sheet_data()` calls preserve
the guard diagnostic, keep handles and catalog state clean, and a no-op save
matches the recovery output without leaking rejected names or payloads.
The recovery side of that same-sheet guard branch is pinned as well: a later
valid materialized `set_cell()` clears the guard diagnostic, dirties the shared
saved/recovered session once, persists an additional recovery cell, and reopens
clean without leaking rejected Patch artifacts.
Post-save worksheet summary diagnostics are pinned too:
`pending_worksheet_edits()` omits dirty-only materialized summaries after a
successful auto-flush, keeps them omitted after clean matching reacquire, adds
them back only when a later mutation dirties the reused session, and clears
them again after the next successful `save_as()`. Prior materialized handoffs
are not exposed as whole-`<sheetData>` replacement summaries.
That summary check now covers the whole-store value-clear exact-budget
recovery chain at field level: each real recovery mutation reports one
dirty-only `Data` summary with matching sparse count and materialized memory,
while invalid preflights and guard-only no-op saves keep summaries empty.
The rename-context variant is pinned as well: if a sheet has a queued public
rename and a dirty materialized session, `save_as()` clears only the
materialized dirty fields while keeping the rename summary visible; clean
matching reacquire keeps it rename-only, and later mutation re-adds the
materialized dirty fields on that same source-order summary.
The whole-store value-clear exact-budget path now has the same rename-context
coverage: planned-name materialization after a queued rename reports combined
rename/materialized summary fields while dirty, save/reacquire returns to a
rename-only summary, and a later valid mutation re-adds the materialized fields
before the next handoff.
Rejected source-overwrite saves are pinned on that path too: `save_as(source)`
fails before flushing the renamed dirty materialized session, preserves the
combined summary and planned dirty diagnostics, leaves the source workbook bytes
unchanged, and does not block the later safe output save.
Mismatched `WorksheetEditorOptions` preflights now follow the same boundary:
planned-name `try_worksheet()` / `worksheet()` rejections leave diagnostics
clear, keep the combined rename/materialized summary and dirty aggregates
unchanged, and allow the later safe save to flush the existing handoff.
Missing-sheet and old source-name lookups now pin the adjacent preflight path:
empty optional lookups and throwing `worksheet()` failures for `Missing` and
the old `Data` source name keep diagnostics clear, preserve the dirty
planned-name materialized session, and still allow the later safe save to flush
the existing handoff.
Read-only public-state queries now cover the same dirty renamed window:
catalog views, source/planned existence checks, pending edit summaries,
dirty materialized names/count, and dirty memory estimates preserve the
combined rename/materialized session, keep the old `Data` planned name
unavailable, leave diagnostics clear, and still allow the later safe save to
flush the existing handoff.
Rejected `save_as()` preflight keeps that combined state intact: source-overwrite
rejection does not flush the renamed dirty session, does not increment the
materialized handoff count, does not update `last_edit_error()`, and leaves
`pending_worksheet_edits()` reporting both `renamed` and `materialized_dirty`
fields until a later safe `save_as()` succeeds.
Clean matching-option reacquire after that safe save now has a renamed
no-op-save check too: a second `save_as()` keeps both borrowed handles clean,
preserves the rename-only summary and empty planned-name materialized
diagnostics, and writes decompressed package entries matching the first renamed
output before any later real mutation. Mismatched `WorksheetEditorOptions`
against that clean saved/reacquired renamed session now follows the same
no-op-save boundary: planned-name access rejections keep diagnostics clear,
leave handles and summaries clean, and a later no-op output still matches the
prior renamed no-op package entries. Missing-sheet and old source-name lookups
are pinned beside it: optional lookups stay empty, throwing lookups fail without
dirtying the saved renamed session, and the next no-op output still matches the
previous renamed no-op package entries.
Read-only catalog and pending-diagnostic queries on that same clean
saved/reacquired renamed session now have the matching no-op-save check:
source/planned name lists, catalog entries, existence checks, pending summaries,
and materialized aggregates preserve the rename-only clean state, and the next
no-op output still matches the previous renamed no-op package entries.
Invalid read preflights are pinned beside those read-only queries: rejected
row/column/A1/range/batch/row_cells/column_cells reads plus missing `get_cell()`
keep diagnostics clear, leave both handles clean, preserve the rename-only
summary and empty materialized diagnostics, and the next no-op output still
matches the previous renamed no-op package entries.
The lighter public materialized-diagnostics shard now covers the adjacent
invalid-mutation/no-op-save boundary for saved renamed sessions: rejected
`set_cell()` / `erase_cell()` calls record the invalid-reference diagnostic
without dirtying clean handles or materialized diagnostics, a no-op save matches
the prior renamed output, and a later valid mutation clears the diagnostic and
re-dirties the planned-name session.
The basic rename-back materialized path now has the same no-op-save stability:
after `Data` is renamed to `TransientData`, renamed back, saved, and reacquired
under the restored source/planned name, a second `save_as()` keeps handles clean,
materialized diagnostics and summaries empty, and decompressed package entries
identical to the first restored-name output without reviving the transient name.
That stability now also covers a clean matching-option reacquire after the first
dirty follow-up on the restored session: a later no-op `save_as()` keeps both
handles clean, leaves pending materialized diagnostics empty, and reuses the
first saved shift output byte-for-byte before the next valid mutation.
Invalid reads on that clean rename-back session are no-op-save safe as well:
row/column/A1/range preflight failures leave `last_edit_error()` clear, keep both
handles clean, leave materialized diagnostics and summaries empty, and a later
`save_as()` still matches the first restored-name package entries.
Invalid mutations now cover the matching write-side preflight: rejected
`set_cell()` / `erase_cell()` calls record the invalid-reference diagnostic
without dirtying handles or materialized diagnostics, a no-op save still matches
the first restored-name output, and the next valid mutation clears the
diagnostic before saving.
Lookup and option preflights on that same clean rename-back session are pinned
beside it: mismatched `WorksheetEditorOptions`, missing-sheet lookups, and
transient planned-name lookups leave diagnostics clear, keep both handles clean,
preserve the restored `Data` catalog, and a no-op save still matches the first
restored-name output.
Read-only catalog, pending-diagnostic, and formula-audit queries now have the
same saved rename-back no-op-save guard: they leave diagnostics clear, preserve
the restored catalog and empty materialized summaries, and a no-op save still
matches the first restored-name output.
Missing-cell erase no-ops cover the diagnostic-cleanup variant: after an
invalid mutation records `last_edit_error()`, valid row/column and A1
`erase_cell()` calls against absent cells clear the diagnostic without dirtying
the clean rename-back session, preserve sparse count/memory, keep missing
targets absent, and a no-op save still matches the first restored-name output.
Missing-cell value-clear no-ops now cover the same clean rename-back branch:
after an invalid mutation records `last_edit_error()`, valid row/column and A1
`clear_cell_value()` calls against absent cells clear the diagnostic without
dirtying either handle, preserve sparse count/memory, keep missing targets
absent without synthesizing blank cells, and a no-op save still matches the
first restored-name output.
The post-cleanup recovery side is pinned as well: after that missing-cell
value-clear no-op and matching no-op save, a later valid `set_cell()` re-dirties
the restored `Data` session, reports matching dirty materialized aggregates and
a restored-name summary, saves as one additional materialized handoff, preserves
the first saved value, writes the later value, keeps missing clear targets
absent, and still does not leak rejected payloads or `TransientData`.
The failed-save side of that recovery is pinned too: the recovered dirty state
survives a rejected source-overwrite `save_as(source)` without flushing, keeps
`last_edit_error()` clear, preserves dirty materialized diagnostics and missing
clear targets, leaves the source package unchanged, and a later safe save still
writes only the saved and recovered values.
The post-retry reacquire side is now covered as well: after that safe retry, a
fresh matching `worksheet("Data")` stays clean, reads both saved values from the
restored materialized session, leaves diagnostics empty, and a later follow-up
mutation/save preserves saved/recovered cells while still excluding rejected
payloads, missing clear targets, and `TransientData`.
Mismatched options after that same safe retry now have a dedicated guard: both
`try_worksheet()` and throwing `worksheet()` reject the mismatched options
without setting `last_edit_error()`, dirtying existing handles, changing
pending handoff counts, or disturbing the restored catalog and saved values;
matching reacquire plus a follow-up save still works afterward.
Missing-sheet and transient-name lookups after the safe retry now match that
hygiene: optional lookups return empty, throwing lookups fail, diagnostics stay
clear, the restored `Data` session stays clean with saved values intact, and a
later matching reacquire plus follow-up save still excludes rejected payloads,
missing clear targets, and `TransientData`.
Invalid reads after the same safe retry are pinned beside those lookup guards:
row/column/A1/range/row-snapshot/column-snapshot failures leave diagnostics
clear, preserve the restored clean `Data` session, keep saved values and sparse
counts intact, and a later matching reacquire plus follow-up save still excludes
rejected payloads, missing clear targets, and `TransientData`.
Invalid mutations after that safe retry now cover the write-side preflight:
rejected row/column/A1/clear operations record diagnostics without dirtying the
clean restored session or changing saved sparse state, a no-op save stays byte
equivalent to the retry output, and the next valid mutation clears diagnostics
before the follow-up save.
Successful missing-cell `clear_cell_value()` no-ops after those retry-side
invalid mutations are pinned as the cleanup branch: they clear the diagnostic,
keep the restored session clean, preserve sparse counts and missing targets,
write a no-op package identical to the retry output, and still allow the next
valid mutation/save to proceed.
Missing-sheet and transient-name lookups after that cleanup are now pinned too:
optional lookups return empty, throwing lookups fail, diagnostics stay clear,
the restored `Data` session remains clean with saved sparse state intact, and
the later no-op save still matches the retry output.
Invalid reads after the same cleanup now cover the read-side failure surface:
row/column/A1/range/row-snapshot/column-snapshot failures leave diagnostics
clear, preserve the restored clean `Data` session and saved sparse state, and
the later no-op save still matches the retry output.
Same-sheet guard snapshot reads now cover the lighter `public-guards` shard:
after a rejected same-sheet replacement, full/range/A1 sparse snapshots,
row/column snapshots, and coordinate-batch snapshots preserve the guard
diagnostic, keep the borrowed handle clean, leave materialized diagnostics and
pending summaries empty, and a no-op save remains a source-entry copy without
leaking the rejected payload.
The scalar read side now has matching guard evidence: existing-cell
`try_cell()` / `get_cell()`, missing-cell `try_cell()`, missing-cell
`get_cell()` failure behavior, `cell_count()`, and `estimated_memory_usage()`
also preserve the guard diagnostic and no-op save state after a rejected
same-sheet replacement.
Invalid reads after that guard are pinned too: invalid row/column scalar
reads, lowercase or overflowing A1 references, invalid/reversed sparse ranges,
and invalid row/column snapshot reads still fail without replacing the guard
diagnostic, dirtying the borrowed handle, or changing the copy-original no-op
save output.
Invalid mutations after the same guard now cover the complementary diagnostic
path: rejected row-zero `set_cell()` and column-overflow `erase_cell()` replace
the prior same-sheet guard diagnostic with the invalid-coordinate diagnostic,
keep the borrowed handle clean, leave materialized diagnostics and pending
summaries empty, and a no-op save remains copy-original without leaking the
rejected replacement or mutation payload.
Invalid reads after that invalid-mutation diagnostic now keep the read-side
contract pinned: row/column/A1/range/row snapshot/column snapshot failures still
do not replace `last_edit_error()`, so the invalid-mutation diagnostic survives
through inspection, no dirty materialized diagnostics appear, and the later
copy-original no-op save continues to exclude both rejected payloads.
Empty-batch mutation no-ops now cover the successful cleanup side of the same
guard path: empty `set_cells()`, `append_row()`, `set_cell_values()`,
`set_row_values()`, `set_column_values()`, coordinate-batch
`clear_cell_values()`, and coordinate-batch `erase_cells()` clear the guard
diagnostic, keep sparse diagnostics unchanged, avoid synthesizing missing cells,
and still save as a copy-original package. The public snapshot shard now also
keeps both the initializer-list and explicit empty `std::span` overloads in the
same clean and dirty no-op loops, including the value-only row/column prefix
empties, so those no-ops preserve prior dirty materialized diagnostics and
settle into the existing byte-stable no-op save checks.
Non-empty row/column `std::span<const CellValue>` overloads now have matching
public snapshot evidence too: `append_row()`, `set_row()`, `set_column()`,
`set_row_values()`, and `set_column_values()` can be mixed in one dirty
materialized session, preserve beyond-prefix row/column cells, save with a
single materialized handoff, fresh-reopen through sparse/row/column snapshots,
and keep the follow-on clean no-op save byte-stable. The same span slice now
also pins clean and dirty guardrail failures for append width, row/column
replacement coordinates, and row/column value-prefix coordinates: rejected
payloads do not mutate sparse state, dirty diagnostics remain intact until a
valid recovery write clears `last_edit_error()`, and saved XML does not leak the
rejected values.
Non-empty missing-only erase no-ops now pin the same cleanup contract:
`erase_cells(CellRange)`, strict A1 range `erase_cells()`, and coordinate-batch
`erase_cells()` over absent targets clear the guard diagnostic, preserve sparse
count/memory, avoid tombstones or missing-cell synthesis, and still save as a
copy-original package without leaking the rejected replacement payload.
Non-empty missing-only value-clear no-ops now pin the parallel cleanup contract:
`clear_cell_values(CellRange)`, strict A1 range `clear_cell_values()`, and
coordinate-batch `clear_cell_values()` over absent targets clear the guard
diagnostic, preserve sparse count/memory, avoid explicit blank or missing-cell
synthesis, and still save as a copy-original package without leaking the
rejected replacement payload.
Range row/column clear/erase snapshot coverage now pins the same preflight
shape for convenience APIs: clean and dirty `clear_rows()` / `clear_columns()`
and `erase_rows()` / `erase_columns()` reject reversed, zero-based, and
overflow bounds before sparse state changes; dirty sessions keep their final
blank/erased shape after failures, a later valid no-op clears
`last_edit_error()`, and the saved package still matches the expected
projection plus byte-stable clean no-op output.
Scalar row/column clear/erase snapshot coverage now mirrors that guardrail for
`clear_row()` / `clear_column()` and `erase_row()` / `erase_column()`: clean
and dirty zero/overflow coordinates fail before sparse state changes, preserve
the final blank/erased projections, clear `last_edit_error()` through a later
valid no-op, and still save the expected byte-stable no-op outputs.
Formula structural rewrite coverage now also ties the tokenizer recovery
boundary to row/column edit rewriting: unterminated string, bracketed, and
quoted-sheet tokens preserve their embedded A1-like text while real references
before those tokens still shift.
Structural shift failure snapshot coverage now also closes the symmetric
delete-coordinate preflight gap: clean and dirty `delete_rows(0, 1)` and
`delete_columns(0, 1)` reject before materialized state changes while preserving
the existing save/reopen no-pollution checks.
Formula sheet-rewrite and audit recovery coverage now mirrors that tokenizer
boundary for rename/formula diagnostics: unterminated string, bracketed, and
quoted-sheet tokens keep embedded A1-like sheet references untouched while real
local sheet references before those tokens are still rewritten or audited.
Single missing-cell value-clear no-ops now cover the scalar cleanup branch too:
row/column and strict A1 `clear_cell_value()` calls after same-sheet guard
failures clear diagnostics, preserve read-only or saved-clean sparse state,
avoid explicit blank synthesis, and leave the later no-op save copy-original or
without an additional materialized handoff.
That scalar value-clear cleanup does not bypass the same-sheet guard: a later
same-sheet `rename_sheet()` or `replace_sheet_data()` still fails after the
no-op clear, keeps the materialized handle clean, preserves sparse diagnostics,
and leaves the no-op save free of both rejected Patch payloads.
The same scalar value-clear no-op is now pinned across two clean materialized
handles: clearing a `Data` diagnostic through missing-cell value-clear does not
dirty `Data`, does not dirty `Untouched`, and a later `Untouched` same-sheet
guard failure reports `Untouched` context while output remains copy-original or
first-saved.
The scoped other-handle mutation branch has the same value-clear coverage:
after a `Data` guard diagnostic is cleared by a missing-cell value-clear no-op,
`Untouched.set_cell()` remains legal, dirties only `Untouched`, and `save_as()`
flushes only that current dirty handle while keeping the rejected `Data` Patch
payload absent.
The failed-save dirty-handle branch now has matching value-clear coverage:
after a `Data` guard diagnostic is cleared by a missing-cell
`clear_cell_value()` no-op, valid `Data` and `Untouched` mutations dirty both
handles, `save_as(source)` fails before flushing them, and a later safe
`save_as()` persists both dirty sessions without leaking the rejected `Data`
Patch payload or rename.
The retry reacquire branch now uses that same value-clear cleanup: read-only and
saved-clean failed-save retry paths clear the initial `Data` diagnostic through
missing-cell `clear_cell_value()`, then dirty and save both handles, reacquire
clean matching-option sessions, and accept a follow-up mutation/save while
keeping rejected `Data` Patch payloads and renames out of both outputs.
The query-failure retry branch now follows the same pattern: after the
missing-cell value-clear cleanup and failed-save recovery, catalog/query and
option-mismatch failures on reacquired sessions leave both handles clean, and a
follow-up mutation/save still persists only the intended dirty handle.
The invalid-read retry branch has matching evidence: after the same
missing-cell value-clear cleanup and failed-save recovery, invalid
row/column/A1/range reads leave reacquired sessions clean before the next
scoped mutation/save.
The post-cleanup invalid-mutation branch is pinned too: after that cleanup,
lookup failures, invalid reads, and no-op retry output, rejected
row/column/A1/clear mutations only record diagnostics. They keep both handles
clean, preserve materialized diagnostics and sparse state, keep a no-op save
byte-equivalent to the retry output, and the later valid mutation clears the
diagnostic before saving.
That late diagnostic cleanup is now pinned directly as well: missing-cell
value-clear no-ops after those rejected mutations clear `last_edit_error()`
without dirtying handles, changing sparse diagnostics, or changing another
no-op save output.
The matching reacquire after that cleanup is now covered before the follow-up
write: a fresh `worksheet("Data")` with matching options reads the saved values
as a clean session, preserves diagnostics/catalog/sparse state, and another
no-op save remains byte-equivalent to the retry output.
The follow-up mutation diagnostics now close that long cleanup chain: the first
valid write after the matching reacquire dirties all borrowed handles, reports
the restored `Data` dirty name, exposes aggregate materialized cell/memory
totals, and emits one materialized-only summary before save clears everything.
The invalid-mutation retry branch now completes that retry set: after the same
missing-cell value-clear cleanup and failed-save recovery, rejected coordinate
and A1 mutations leave reacquired sessions clean, preserve diagnostics across a
failed source-overwrite retry, and still allow the next valid scoped
mutation/save.
The recovery side of that no-op save is covered too: a later valid `set_cell()`
clears no diagnostics because none were present, re-dirties the restored `Data`
session, saves as one additional materialized handoff, preserves the first
saved value, writes the later value, and still keeps `TransientData` absent.
The lower-level materialized diagnostics now pin the same renamed lifecycle:
`pending_materialized_worksheet_names()` reports the current planned sheet name
while dirty, cell/memory aggregates match the borrowed session, successful
`save_as()` and clean reacquire clear those aggregates, and a later mutation
re-adds the planned dirty name until the next save.
That guard is now checked directly against the active borrowed handle memory
estimate across rejected save, option mismatch, missing/source-name lookup,
read-only query, clean-reacquire, later-mutation, and second-save windows, so
summary diagnostics and lower-level aggregates cannot diverge silently in that
renamed whole-store clear path.
Rename-back before materialization is pinned too: if a sheet is renamed to a
temporary planned name and then renamed back to its source name, a later
`WorksheetEditor` dirty session reports the restored source/planned name,
does not keep the transient name in summaries or dirty materialized
diagnostics, and saves without leaking the transient catalog name.
Failed `WorksheetEditor` mutation after that rename-back path is pinned as
well: invalid A1 mutation sets `last_edit_error()` but does not dirty the
materialized session, does not add dirty materialized diagnostics or summaries,
does not revive the transient name, and a later valid mutation recovers through
the restored source/planned name.
Rejected `save_as()` after the same rename-back dirty state is pinned too:
source-overwrite preflight runs before materialized auto-flush, leaves the
borrowed session dirty under the restored source name, does not increment the
materialized handoff count, does not create `last_edit_error()`, does not mutate
the source package, and a later safe `save_as()` flushes the dirty edit without
leaking the transient planned name.
Those safe rename-back recovery saves now also pin the complete clean
diagnostic surface for the basic public-retry shard: dirty materialized names,
cell counts, memory estimates, current summaries, replacement diagnostics, and
`last_edit_error()` are all empty after the recovery save.
Clean reacquire after that failed-save recovery is now pinned: after the safe
`save_as()`, a matching `worksheet("Data")` call reuses the saved materialized
state instead of reloading stale source cells, keeps dirty diagnostics empty
until a later mutation, and can then flush another dirty edit under the restored
source name.
Option mismatch after that failed-save recovery is pinned as the matching
failure path: mismatched `try_worksheet()` / `worksheet()` calls reject the
existing saved materialized session without updating `last_edit_error()`,
dirtying diagnostics or summaries, losing saved values, reviving the transient
name, or blocking later matching-option mutation and save.
Missing `try_worksheet()` after the same recovery is pinned as the no-op
lookup path: missing names, including the old transient name, return empty
without updating `last_edit_error()`, dirtying diagnostics or summaries,
discarding/reloading the saved materialized value, reviving the transient name,
or blocking later matching-option mutation and save.
Missing `worksheet()` after the same recovery is pinned as the throwing lookup
path: missing names, including the old transient name, throw
`FastXlsxError` without updating `last_edit_error()`, dirtying diagnostics or
summaries, discarding/reloading the saved materialized value, reviving the
transient name, or blocking later matching-option mutation and save.
Read-only catalog queries after that recovery are pinned too:
`worksheet_names()` / `has_worksheet()` report the restored planned catalog and
`source_worksheet_names()` / `has_source_worksheet()` report the opened source
catalog without updating `last_edit_error()`, dirtying diagnostics or summaries,
discarding/reloading the saved materialized value, reviving the transient name,
or blocking later matching-option mutation and save.
Read-only pending-state diagnostics after that recovery are pinned as well:
`has_pending_changes()`, `pending_change_count()`, replacement diagnostics,
materialized aggregate diagnostics, `pending_worksheet_edits()`,
`worksheet_catalog()`, and `last_edit_error()` preserve the saved materialized
session, restored source/planned mapping, empty dirty diagnostics, and prior
public edit count without flushing, reloading, reviving the transient name, or
blocking later matching-option mutation and save.
Handle-level `WorksheetEditor` reads after the same recovery are pinned too:
`try_cell()`, `get_cell()`, missing-cell reads, `cell_count()`,
`estimated_memory_usage()`, `sparse_cells()`, and `sparse_cells(range)` preserve
the saved materialized value and unchanged source-backed cells without updating
`last_edit_error()`, dirtying diagnostics or summaries, flushing, reloading
stale source values, reviving the transient name, or blocking later
matching-option mutation and save.
Invalid handle-level reads after that recovery are pinned as read-only failures:
invalid row/column, invalid A1, and invalid `sparse_cells(range)` calls throw
without updating `last_edit_error()`, dirtying diagnostics or summaries,
flushing, reloading stale source values, reviving the transient name, or blocking
later matching-option mutation and save.
Invalid handle-level mutations after that recovery are pinned as diagnostic
mutation failures: invalid row/column and A1 `set_cell()` / `erase_cell()` calls
throw and update `last_edit_error()` without dirtying diagnostics or summaries,
flushing, reloading stale source values, reviving the transient name, retaining
rejected payloads, or blocking a later valid matching-option mutation and save.
Missing-cell erase no-ops after that recovery are pinned too: valid row/column
and A1 `erase_cell()` calls targeting missing cells clear prior mutation
diagnostics without dirtying diagnostics or summaries, flushing, reloading stale
source values, reviving the transient name, creating erase tombstones, or
blocking a later valid matching-option mutation and save.
Positive blank/erase projection after that recovery is pinned as well:
`set_cell("A1", CellValue::blank())` keeps an explicit blank record and
`erase_cell(2, 1)` removes the existing source-backed A2 record; the next
`save_as()` writes `<c r="A1"/>`, preserves B1, omits row 2 / `placeholder-a2`,
refreshes dimension to `A1:B1`, and does not leak the transient planned name.
Positive scalar/formula projection after that recovery is pinned too: number,
boolean, and formula mutations dirty the saved materialized session, then
`save_as()` writes numeric `<v>`, boolean `t="b"` / `1`, escaped formula `<f>`
without cached value, refreshes dimension to `A1:C3`, and keeps the restored
sheet name without leaking the transient planned name.
Positive text escape projection after that recovery is pinned too: text
mutations dirty the saved materialized session, then `save_as()` writes
inline strings, escapes `&`, `<`, and `>`, preserves quotes in element text,
uses `xml:space="preserve"` for leading/trailing whitespace, writes empty text
as `<t></t>`, refreshes dimension to `A1:C3`, and keeps the restored sheet name
without leaking the transient planned name.
Those three retry/projection outputs now also repeat a clean no-op `save_as()`,
stay byte-stable, preserve source package bytes, and reopen through public
`WorksheetEditor` as clean sparse state with the same projected values.
Legal maximum coordinate projection after that recovery is pinned too:
`XFD1048576` can be written sparsely via row/column max values, read back
through row/column and A1 APIs, and saved with dimension `A1:XFD1048576` plus a
single sparse max-row record. This is sparse boundary correctness, not dense
allocation or a large-file performance claim.
Edge erase shrink after that recovery is pinned as well: after a saved
`XFD1048576` sparse record is erased, row/column and A1 reads no longer expose
the edge record, the max-boundary range snapshot is empty, and the next
`save_as()` shrinks dimension to `A1:B2` without writing tombstones or leaking
the transient planned name.
The strict A1 mutation overloads are pinned on the same boundary: after
rename-back failed-save recovery, `set_cell("XFD1048576", ...)` writes the last
legal Excel cell, saves dimension `A1:XFD1048576`, and
`erase_cell("XFD1048576")` removes it and shrinks dimension back to `A1:B2`.
This does not add lowercase reference acceptance, range mutation, dense
allocation, or a large-file performance claim.
Explicit blank projection is pinned on that boundary too:
`set_cell("XFD1048576", CellValue::blank())` remains an active blank sparse
record, saves as `<c r="XFD1048576"/>` with dimension `A1:XFD1048576`, and a
later row/column erase removes it and shrinks dimension back to `A1:B2`. This
does not turn blanks into missing cells before erase and does not add tombstone
output or row metadata repair.
Formula projection is pinned on that boundary too:
`set_cell(1048576, 16384, CellValue::formula(...))` remains an active formula
sparse record, reads back through row/column and A1 APIs, saves escaped `<f>`
text at `XFD1048576` with dimension `A1:XFD1048576`, and does not generate a
cached `<v>` value. This does not add formula evaluation, cached result
preservation, calcChain rebuild, defined-name/formula dependency rewrite, dense
allocation, or a large-file performance claim.
Scalar projection is pinned on that boundary too:
`set_cell(1048576, 16384, CellValue::number(...))` saves a numeric `<v>` at
`XFD1048576`, and a later `set_cell("XFD1048576", CellValue::boolean(false))`
overwrites the same sparse edge record as `t="b"` / `<v>0</v>` while keeping
dimension `A1:XFD1048576`. This does not add date cell typing, non-finite
number acceptance, number-format/style migration, boolean coercion, dense
allocation, or a large-file performance claim.
Saved scalar edge erase shrink is pinned on that boundary too: after the
max-coordinate number has been saved and overwritten by a saved boolean false,
`erase_cell(1048576, 16384)` removes the edge record, clears the max-boundary
range snapshot, and the next `save_as()` shrinks dimension back to `A1:B2`
without writing tombstones, converting the scalar to an explicit blank, or
repairing row metadata.
Saved formula edge erase shrink is pinned on that boundary too: after a formula
has been saved at `XFD1048576` without a cached `<v>` value,
`erase_cell("XFD1048576")` removes the edge record, clears row/column, A1, and
range reads, and the next `save_as()` shrinks dimension back to `A1:B2` without
writing tombstones, converting the formula to an explicit blank, evaluating the
formula, generating cached results, or repairing row metadata.
Fresh source-backed max-coordinate materialization is pinned as well: a source
worksheet that already contains an inline-string `XFD1048576` record
materializes that edge cell cleanly, a read-only no-op `save_as()` still copies
source entries byte-for-byte, the clean no-op output fresh-reopens with the
edge cell present through A1, row, column, and range snapshots, and
`erase_cell("XFD1048576")` removes the source-backed edge record so the next
dirty projection shrinks to `A1:B2` without dense allocation, source reload,
tombstones, wrapper preservation, or row metadata repair.
That source-backed max-coordinate erase path now also has post-noop reuse
evidence: after the erased `XFD1048576` output has settled into a byte-stable
clean no-op save, the same borrowed `WorksheetEditor` can write `XFD1048576`
again, re-expand dimension to `A1:XFD1048576`, preserve source/erase/no-op
packages, fresh-reopen through A1, row, column, and range snapshots, and settle
into another byte-stable no-op `save_as()`. This remains sparse boundary
save/reuse evidence only, not dense allocation, row metadata repair, source
reload, or large-file random editing.
The formula-shaped max-coordinate erase path now mirrors that post-noop reuse:
after an erased source-backed formula edge with a stale cached `<v>` has settled
into a byte-stable clean no-op save, the same `WorksheetEditor` can write a new
formula at `XFD1048576`, re-expand dimension to `A1:XFD1048576`, omit cached
values, preserve source/erase/no-op packages, fresh-reopen through sparse
views, and settle into another byte-stable no-op save. This remains formula-text
projection/reuse evidence only, not formula evaluation, cached value
regeneration, calcChain rebuild, or metadata repair.
The workbook sharedStrings max-coordinate path now has matching post-noop reuse
evidence: after the erased `t="s"` edge and its byte-stable clean no-op save,
the same `WorksheetEditor` can write a new text value back to `XFD1048576`,
re-expand dimension to `A1:XFD1048576`, append a new shared string index while
keeping earlier package outputs unchanged, fresh-reopen through sparse views,
and settle into another byte-stable no-op save. This is append-only
source-sharedStrings projection/reuse evidence only, not sharedStrings
compaction, index migration, table rebuild, relationship repair, or large-file
random editing.
The source-backed scalar max-coordinate cases now mirror that reuse boundary:
after number, boolean, and explicit-blank `XFD1048576` edge cells are erased and
their clean no-op outputs are byte-stable, the same materialized
`WorksheetEditor` can write scalar values back to the last legal cell, re-expand
dimension to `A1:XFD1048576`, preserve source/erase/no-op packages, fresh-reopen
through sparse views, and settle into another byte-stable no-op save. This is
scalar value projection/reuse evidence only, not date typing, non-finite number
acceptance, number-format/style migration, dense allocation, row metadata
repair, or large-file random editing.
The same edge is now pinned for empty inline-string source shapes: `t="inlineStr"`
with an empty `<t></t>` materializes as empty text, `t="inlineStr"` with
`<is/>` and no text materializes as blank, no-op `save_as()` keeps copy-original
bytes, clean no-op outputs fresh-reopen with the edge record still present, and
erasing either `XFD1048576` record shrinks dirty projection to `A1:B2`. This is
not rich text preservation, inline/scalar coercion, XML repair, source reload,
or large-file random editing.
Those empty inline-string source shapes now also reuse the same materialized
handle after erase and a byte-stable clean no-op save: the empty-text edge can be
written back as non-empty inline text, the `<is/>` edge can be written back as an
explicit blank, dimension expands again to `A1:XFD1048576`, source/erase/no-op
packages remain unchanged, fresh reopen sees the restored edge through sparse
views, and a second no-op save remains byte-stable. This is empty inline
materialization/projection reuse evidence only, not rich text preservation,
inline wrapper preservation, XML repair, source reload, or large-file random
editing.
Workbook sharedStrings rich text is pinned at the same edge too: a source
`XFD1048576` `t="s"` record can point at simple rich `<r><t>...</t></r>` runs,
materialize as flattened plain text, ignore phonetic/extension metadata text,
keep source bytes on no-op `save_as()`, fresh-reopen the no-op output with the
flattened edge text, and erase back to `A1:B2` while preserving
`xl/sharedStrings.xml`. This is not rich text preservation, sharedStrings
rebuild/writeback/index migration, relationship repair, or large-file random
editing.
The rich shared-string max-coordinate path now also has post-noop reuse
coverage: after the flattened rich edge is erased and its clean no-op output is
byte-stable, the same materialized `WorksheetEditor` can write a new plain text
edge back to `XFD1048576`, append a new shared string index, re-expand dimension
to `A1:XFD1048576`, preserve source/erase/no-op packages, fresh-reopen through
sparse views, and settle into another byte-stable no-op save. This is flattened
rich-source projection/reuse evidence only, not rich text writeback,
sharedStrings compaction, index migration, relationship repair, or large-file
random editing.
Those source-backed max-coordinate erase projections now also fresh-reopen after
save: inline text, formula, shared-string, scalar, empty-inline, and rich
shared-string edge cases all reopen as clean `A1:B2` sparse stores with
`XFD1048576` absent from A1, row, column, and range snapshots.
They now also repeat a clean no-op `save_as()` after each erase projection:
the no-op packages are byte-identical to the erase outputs, source bytes stay
unchanged, public save/catalog snapshots stay stable, and the no-op outputs
fresh-reopen through the same clean `A1:B2` sparse-store checks.
Unsupported source cell shape failure hygiene is pinned as well: date-like
cells, custom/unknown type tokens, and invalid boolean payloads fail through
the public facade without leaving partial materialized sessions or blocking
later Patch edits.
The invalid source style-id materialization failure no-op save path now also
fresh-reopens the copy-original output: workbook/source catalogs remain visible,
the same materialization diagnostic is preserved, and the failed reads leave no
partial in-memory session state.
Malformed source worksheet XML now has the same public facade hygiene coverage:
missing closing worksheet root fails cleanly without partial materialized state.
The same malformed worksheet still blocks same-sheet Patch preflight, so
recovery is intentionally proven on an unrelated valid sheet rather than
described as XML repair.
Source cell-reference failure hygiene is now pinned at the public facade as
well: missing source cell `r` and row/cell reference mismatch fail cleanly
without partial sessions and do not prevent later valid Patch edits.
Source formula behavior is pinned too: supported formula cells load as
`CellValue::formula(...)`, stale cached scalar values are dropped by the
materialized save-as projection, and empty/duplicate/attribute-bearing or
unsupported inline/shared-string formula shapes fail cleanly without poisoning
the editor. This is formula text import only, not formula evaluation or
calcChain rebuild.
Formula dirty-output fresh-reopen checks now also cover `row_cells()` and
`column_cells()` snapshots for the reopened sparse store, keeping row/column
views aligned with `sparse_cells()` and direct reads after materialized formula
save-as projections.
The base source formula read-only path now also has a clean no-op `save_as()`
gate: materializing formula text and ignoring the stale cached value keeps the
session clean, copies source package bytes unchanged, fresh-reopens through
public sparse views, and leaves the source package untouched. This is still
formula-text readback/copy-original evidence, not formula evaluation, cached
value preservation, or calcChain rebuild.
The base source formula dirty-save path now also has post-dirty no-op evidence:
the later no-op `save_as()` output is byte-stable, the source fixture remains
unchanged, and fresh reopen still reads formula text without stale cached values.
This is formula text projection only, not formula evaluation or calcChain rebuild.
That same base source formula path now continues after the post-dirty no-op
save: the borrowed `WorksheetEditor` can become dirty again with a later formula
cell, write a new projection, preserve source/prior outputs, fresh-reopen
through public sparse views, and settle into another byte-stable no-op
`save_as()`. This is still formula text projection/reuse evidence only, not
formula evaluation, cached value regeneration, or calcChain rebuild.
Cached-result formula variants now carry the same post-dirty no-op check:
numeric/string/boolean/error cached `<v>` values are still omitted from the
byte-stable no-op output, the source package is unchanged, and fresh reopen reads
formula text only.
Those cached-result formula variants now also have a read-only clean no-op
`save_as()` gate before the later edit: materialization stays clean, source
bytes are copied unchanged, the no-op output fresh-reopens through public sparse
views as formula text only, and stale cached values remain source-only until a
dirty projection omits them. This is not cached value preservation, formula
evaluation, or calcChain rebuild.
The cached-result formula path now also continues after the byte-stable
post-dirty no-op save: the same borrowed `WorksheetEditor` can accept a later
formula edit, keep all source cached results omitted, preserve source/prior
packages, fresh-reopen with the expanded formula set, and settle into another
byte-stable no-op `save_as()`. This remains formula-text materialization/reuse
evidence only, not cached value regeneration, formula evaluation, or calcChain
rebuild.
Source error cell materialization now has matching post-dirty no-op evidence:
the no-op output remains byte-stable, the rewritten source package is unchanged,
and fresh reopen still reads the `t="e"` error cells beside later edits.
The same source error cell path now also has a read-only clean no-op
`save_as()` gate before the later edit: source error cells stay clean, copy the
source package bytes unchanged, fresh-reopen through public sparse views, and
leave the source package untouched. Public `Cell::formula()` and
`CellValue::formula()` now reject empty caller formula text, streaming
`CellView::formula("")` is rejected by `append_row()` before row state changes,
and `CellValue::error()` rejects empty caller tokens at the owning value
boundary. This remains scalar formula/error-cell boundary evidence plus
non-empty payload guards, not formula parsing, formula evaluation, calcChain
rebuild, or semantic error-token validation.
The source error cell path now also has post-noop reuse evidence: after the
byte-stable post-dirty no-op save, the same borrowed `WorksheetEditor` can write
another error cell, preserve source/prior packages, fresh-reopen through public
sparse views with both source-backed and later error cells, and settle into
another byte-stable no-op `save_as()`. This is still scalar error-cell
projection/reuse evidence only, not broader error-token semantics or formula
evaluation.
Source shared formula definitions/followers now carry both no-op gates:
read-only materialization stays clean, the clean no-op output copies source
package bytes and fresh-reopens with translated follower text, while the later
flattened plain-formula output remains byte-stable and preserves both source and
prior no-op package bytes.
That base shared-formula path now also continues after the post-dirty no-op
save: the same borrowed `WorksheetEditor` can accept a later formula edit, write
a new flattened projection, preserve source/prior packages, fresh-reopen through
public sparse views, and settle into another byte-stable no-op `save_as()`. This
is shared-formula materialization/reuse evidence only, not shared formula
metadata preservation, formula evaluation, cached value regeneration, or
calcChain rebuild.
Source-order shared formula matrices now carry both no-op gates too: read-only
materialization stays clean, the clean no-op output copies source package bytes
and fresh-reopens with interleaved/latest-definition followers translated as
plain formula text, while the later dirty projection remains byte-stable and
preserves both source and prior no-op package bytes.
That source-order matrix path now also has post-noop reuse evidence: after the
byte-stable post-dirty no-op save, the same borrowed `WorksheetEditor` can write
another formula, preserve source/prior packages, fresh-reopen with the expanded
matrix formulas plus the new formula, and settle into another byte-stable no-op
`save_as()`. This remains source-order shared formula materialization/reuse
evidence only, not shared formula metadata preservation, formula evaluation,
cached value regeneration, or calcChain rebuild.
Office-like 2D shared formula groups now carry both no-op gates as well:
read-only materialization stays clean, the clean no-op output copies source
package bytes and fresh-reopens with both rectangular shared formula groups
translated as plain formula text, while the later flattened output remains
byte-stable, drops stale cached values, and preserves both source and prior
no-op package bytes.
That office-like 2D shared formula path now also has post-noop reuse evidence:
after the byte-stable post-dirty no-op save, the same borrowed
`WorksheetEditor` can write another formula, preserve source/prior packages,
fresh-reopen with both flattened shared formula groups plus the new formula, and
settle into another byte-stable no-op `save_as()`. This remains office-like
shared formula materialization/reuse evidence only, not shared formula metadata
preservation, formula evaluation, cached value regeneration, or calcChain
rebuild.
Array/dataTable metadata fallbacks now carry the same two no-op gates: read-only
materialization stays clean, the clean no-op output copies source package bytes
and fresh-reopens with formula text cells flattened as plain formulas while
metadata-only followers keep cached scalar fallback values. The later dirty
projection omits stale cached values on formula text cells, remains
byte-stable, preserves both source and prior no-op package bytes, and fresh
reopen preserves the sparse projection beside later edits. This keeps lossy
formula projection evidence tied to sparse public state, not formula evaluation
or calcChain rebuild.
That array/dataTable fallback path now also has post-noop reuse evidence: after
the byte-stable post-dirty no-op save, the same borrowed `WorksheetEditor` can
write another formula, keep array/dataTable metadata flattened, preserve
source/prior packages, fresh-reopen with the fallback cells plus the new
formula, and settle into another byte-stable no-op `save_as()`. This remains
lossy formula materialization/reuse evidence only, not array/dataTable metadata
preservation, formula evaluation, cached value regeneration, or calcChain
rebuild.
Source inline text failure hygiene is now pinned at the same facade: unknown XML
entities, unsupported inline `<t>` attributes, duplicate direct inline text
elements, and unknown inline string metadata fail cleanly without partial
sessions. Simple source inline rich text runs now materialize as flattened plain
text; rich formatting is not preserved, and inline phonetic / extension metadata
text is ignored before dirty save projects the value as an ordinary inline
string. Malformed inline rich text shapes, including mixed direct/rich text,
`rPr` outside a run, value wrappers inside `rPr`, and unclosed rich/ignored
metadata, now fail cleanly without partial sessions. This is not rich text
preservation, phonetic metadata import, extension metadata import, a rich-text
object model, or XML repair.
Source row/cell structure hygiene is also pinned: unsupported row/cell metadata
attributes, duplicate or out-of-order rows, out-of-order cells, and invalid
numeric payloads fail cleanly without partial sessions. This is strict
validation, not row/cell sorting, duplicate merge, metadata preservation,
numeric coercion, or repair.
Source value-wrapper hygiene is now pinned too: unsupported scalar `<v>`
attributes, duplicate scalar wrappers, inline/scalar wrapper mismatches, and
non-whitespace direct cell text outside `<v>` / `<t>` / `<f>` wrappers, plus
cell-internal comments / processing instructions / unsupported markup fail
cleanly without partial sessions. This is strict validation, not wrapper repair,
duplicate merge, inline/scalar coercion, direct text import, comment import, or
XML repair.
Source XML/entity/attribute parser hygiene is pinned as well: unterminated
entities, invalid or out-of-range character references, unquoted attributes, and
duplicate source attributes fail cleanly without partial sessions. Recovery is
proven through a later edit to an unrelated valid sheet, not through same-sheet
Patch bypass. This is strict validation, not tolerant entity recovery, invalid
character replacement, attribute repair, duplicate merge, or XML repair.
Source coordinate boundary hygiene is pinned too: out-of-range columns/rows,
zero-row cell references, non-column-first cell references, zero/overflow row
numbers, and non-numeric row numbers fail cleanly without partial sessions.
This is strict validation, not coordinate inference, clamping, sorting,
row-number repair, same-sheet Patch bypass, or XML repair.
Source row/cell state-machine hygiene is now pinned as well: row elements
outside `sheetData`, nested rows, cells outside row elements, nested cells,
non-whitespace worksheet text outside wrapper metadata or `sheetData`,
non-whitespace sheetData text outside rows, and non-whitespace row text outside
cells fail cleanly without partial sessions. Recovery is proven through an
unrelated valid sheet, not through row inference, state-machine recovery, direct
worksheet/sheetData/row text import, or same-sheet Patch repair.
Supported source value materialization has positive coverage too: self-closing
source cells and inline-string cells without text become explicit blank records,
`t="b"` source cells become booleans, and empty inline text remains
`CellValue::text("")`. This documents the current supported-value floor without
implying date support, error-token validation, rich-text preservation, style/sharedStrings
migration, or metadata synchronization.
Those supported-value dirty projections now also fresh-reopen through the public
sparse views for explicit blanks, booleans, empty text, scalar/formula fallbacks,
`t="str"` cells, flattened inline rich text, and prefixed local-name wrappers.
The checks bind saved XML projections to `used_range()`, `sparse_cells()`,
`row_cells()`, `column_cells()`, and direct reads without expanding into
rich-text preservation, cached formula preservation, or metadata synchronization.
The base supported-values projection now also has both no-op gates: read-only
materialization stays clean, the clean no-op output copies source package bytes
and fresh-reopens with blank, boolean, empty-text, plain formula, and
cached-scalar fallback records. The later follow-up `save_as()` output is
byte-stable, the source and prior no-op packages remain unchanged, and fresh
reopen still sees those records beside the later inline edit.
It now also reuses the same clean materialized handle after that byte-stable
post-dirty no-op save: a later boolean edit extends the sparse projection to
`A1:J3`, preserves source/no-op/dirty/no-op package bytes, fresh-reopens through
the public sparse views, and settles into another byte-stable no-op save. This
is supported scalar/formula value materialization reuse only, not sharedStrings
migration, cached formula preservation, rich-text preservation, metadata repair,
or large-file random editing.
Source `t="str"` cells now carry both no-op gates: read-only materialization
stays clean, the clean no-op output copies source package bytes and
fresh-reopens with scalar text, plain formula, and numeric sibling records.
The later dirty projection remains byte-stable, preserves the source and prior
no-op packages, and fresh reopen still sees the inline edit. This is not
sharedStrings migration or cached formula preservation.
The same `t="str"` path now also reuses its clean materialized handle after the
byte-stable post-dirty no-op save: a later numeric edit extends the regenerated
projection to `A1:E3`, keeps `t="str"` source tokens out of dirty outputs, avoids
creating sharedStrings, preserves all prior package outputs, fresh-reopens
through sparse views, and settles into another byte-stable no-op save. This is
scalar-string materialization/projection reuse only, not cached-value
preservation, source token round-tripping, sharedStrings migration, metadata
repair, or large-file random editing.
Flattened source inline rich text now has both no-op gates too: read-only
materialization stays clean, the clean no-op output copies source package bytes
and fresh-reopens with the flattened plain text, and the later dirty projection
remains byte-stable while preserving the source and prior no-op packages. This
is not rich-text formatting preservation.
It now also reuses that flattened inline-rich materialized handle after the
byte-stable post-dirty no-op save: a later text edit extends the regenerated
projection to `A1:C3`, keeps rich formatting / phonetic markup omitted, avoids
creating sharedStrings, preserves source/no-op/dirty/no-op package bytes,
fresh-reopens through sparse views, and settles into another byte-stable no-op
save. This is flattened plain-text projection reuse only, not rich text
round-tripping, formatting preservation, phonetic metadata preservation,
sharedStrings migration, metadata repair, or large-file random editing.
Prefixed source worksheet local-name wrappers now carry both no-op gates:
read-only materialization stays clean, the clean no-op output copies source
package bytes and fresh-reopens with normalized sparse cells, and the later
dirty projection remains byte-stable while preserving source plus prior
copy-original output. This is not namespace repair or a promise to preserve
cell/value element prefixes inside regenerated `sheetData`.
That prefixed inline-source path now also reuses the same clean materialized
handle after the byte-stable post-dirty no-op save: a later escaped inline text
edit extends the regenerated projection to `A1:E3`, keeps regenerated
cell/value elements unprefixed, avoids sharedStrings, preserves source/no-op/
dirty/no-op package bytes, fresh-reopens through sparse views, and settles into
another byte-stable no-op save. This remains local-name materialization and
projection reuse only, not namespace repair, prefix preservation inside
regenerated `sheetData`, sharedStrings migration, metadata repair, or
large-file random editing.
Normalized source `s=0` cells now have both no-op gates: read-only
materialization stays clean, the clean no-op output copies source package bytes
and fresh-reopens with unstyled text cells, and explicit default-style
attributes remain omitted after the later dirty projection. The follow-up
`save_as()` output is byte-stable, and source plus prior no-op packages stay
unchanged. This is not non-default style migration or style registry repair.
That default-style normalization path now also reuses the same clean
materialized handle after the byte-stable post-dirty no-op save: a later
escaped text edit extends the regenerated projection to `A1:F2`, still omits
all normalized `s=0` spellings, avoids sharedStrings, preserves the source /
no-op / dirty / no-op packages, fresh-reopens through sparse views, and settles
into another byte-stable no-op save. This remains default-style coercion and
projection reuse only, not caller-supplied non-default style writes,
non-default style migration, style registry repair, metadata repair, or
large-file random editing.
Empty source worksheet materialization now has both no-op gates as well:
worksheets with no `sheetData` and worksheets with self-closing `<sheetData/>`
load as empty sparse stores, stay clean, and the clean no-op output copies
source package bytes while fresh-reopening as empty. Later dirty projections
still save through the standalone CellStore worksheet projection, the follow-up
`save_as()` output is byte-stable, source plus prior no-op packages stay
unchanged, and fresh reopen still sees only the inserted sparse cell. This does
not imply XML repair, clean-session commit semantics, source wrapper metadata
preservation, same-sheet Patch bypass, or large-file random editing.
Those empty-source variants now also reuse the same clean materialized handle
after the byte-stable post-dirty no-op save: a later escaped text edit extends
the regenerated projection to `B2:C3`, keeps the original placeholder absent,
avoids sharedStrings, preserves source/no-op/dirty/no-op package bytes,
fresh-reopens through sparse views, and settles into another byte-stable no-op
save. This remains standalone sparse CellStore projection reuse only, not XML
repair, clean-session commit semantics, source wrapper metadata preservation,
same-sheet Patch bypass, metadata repair, or large-file random editing.
Worksheet root and `sheetData` boundary failure hygiene is pinned too: markup
before the worksheet root, duplicate `sheetData`, duplicate worksheet roots, and
trailing text fail cleanly without partial sessions. This is strict validation,
not XML repair, duplicate merge, tolerant root recovery, same-sheet Patch
bypass, or wrapper metadata preservation.
Source wrapper metadata projection behavior now has both no-op gates:
worksheet-level `sheetPr`, `dimension`, `sheetViews`, `sheetFormatPr`, `cols`,
and `autoFilter` beside supported cells do not block read-only public
materialization, the clean no-op output copies source package bytes, and fresh
reopen still sees the source sparse cell. A later dirty `WorksheetEditor` save
rewrites `sheetData` from the sparse CellStore while preserving those source
wrapper elements around it. The follow-up `save_as()` remains byte-stable,
source plus prior no-op package bytes stay unchanged, and fresh reopen still
sees the same sparse cells. This is wrapper preservation only, not wrapper
metadata synchronization, range recalculation, relationship repair, or the
internal sheetData Patch API.
That wrapper-metadata path now also reuses the same clean materialized handle
after the byte-stable post-dirty no-op save: a later escaped text edit extends
the regenerated sparse projection to `A1:C3`, keeps `sheetPr`, `sheetViews`,
`sheetFormatPr`, `cols`, and `autoFilter` around the rewritten `sheetData`,
avoids sharedStrings, preserves source/no-op/dirty/no-op package bytes,
fresh-reopens through sparse views, and settles into another byte-stable no-op
save. This remains wrapper-preserving projection reuse only, not wrapper
metadata synchronization, range recalculation, relationship repair, metadata
repair, or the internal sheetData Patch API.
Representative relationship-bearing wrapper metadata now follows the same
two-gate no-op boundary: source `<hyperlinks>` and `<tableParts>` do not block
supported cell materialization, the clean no-op output copies source package
bytes and fresh-reopens with the supported sparse cells, dirty projection
preserves those worksheet XML references, and the source worksheet `.rels` plus
linked table part stay as opaque preserved package artifacts. The follow-up
`save_as()` is byte-stable, source plus prior no-op package bytes stay
unchanged, and fresh reopen still sees the supported sparse cells. This is not
hyperlink/table semantic editing, relationship pruning/repair, table range
repair, or the internal sheetData Patch API.
That relationship-wrapper path now also reuses the same clean materialized
handle after the byte-stable post-dirty no-op save: a later escaped text edit
extends the regenerated sparse projection to `A1:D4`, keeps `<hyperlinks>`,
`<tableParts>`, relationship references, the worksheet `.rels`, and linked
table part as opaque preserved artifacts, avoids sharedStrings, preserves
source/no-op/dirty/no-op package bytes, fresh-reopens through sparse views, and
settles into another byte-stable no-op save. This remains relationship-bearing
wrapper projection reuse only, not hyperlink/table semantic editing,
relationship pruning/repair, table range repair, metadata repair, or the
internal sheetData Patch API.
Representative range/reference wrapper metadata now has both no-op gates too:
source `<mergeCells>`, `<dataValidations>`, `<conditionalFormatting>`,
`<ignoredErrors>`, `<pageMargins>`, and `<pageSetup>` do not block supported
text/number/boolean materialization, the clean no-op output copies source
package bytes and fresh-reopens with the supported sparse cells, and dirty
`WorksheetEditor` save preserves them around the regenerated sparse
`sheetData`. The follow-up `save_as()` is byte-stable, source plus prior no-op
package bytes stay unchanged, and fresh reopen still sees the supported sparse
cells. This is not merged-cell editing, validation/conditional-formatting
import, page setup synchronization, range recalculation, metadata repair,
relationship repair, or semantic range-object editing.
That range/reference wrapper path now also reuses the same clean materialized
handle after the byte-stable post-dirty no-op save: a later escaped text edit
extends the regenerated sparse projection to `A1:D4`, keeps the original
`mergeCells`, `dataValidations`, `conditionalFormatting`, `ignoredErrors`,
`pageMargins`, and `pageSetup` XML around the rewritten `sheetData`, avoids
sharedStrings, preserves source/no-op/dirty/no-op package bytes, fresh-reopens
through sparse views, and settles into another byte-stable no-op save. This
remains range/reference wrapper projection reuse only, not merged-cell editing,
validation/conditional-formatting import, page setup synchronization, range
recalculation, metadata repair, relationship repair, or semantic range-object
editing.
Source comments and processing instructions outside cells now have both no-op
gates too: they do not block supported cell materialization, the clean no-op
output copies source package bytes and fresh-reopens with the supported sparse
cell, and dirty `WorksheetEditor` save preserves wrapper-level comments /
processing instructions while replacing comments and processing instructions
inside the source `sheetData`. The follow-up `save_as()` is byte-stable, source
plus prior no-op package bytes stay unchanged, and fresh reopen still sees the
supported sparse cells. This is not comment import, processing-instruction
semantic support, comments-part editing, broad XML trivia preservation, XML
repair, relationship repair, or a change to cell-internal comment / PI
rejection.
That comment/processing-instruction wrapper path now also reuses the same clean
materialized handle after the byte-stable post-dirty no-op save: a later escaped
text edit extends the regenerated sparse projection to `A1:C3`, keeps
wrapper-level comments and processing instructions outside the rewritten
`sheetData`, continues to omit source `sheetData` trivia, avoids sharedStrings,
preserves source/no-op/dirty/no-op package bytes, fresh-reopens through sparse
views, and settles into another byte-stable no-op save. This remains wrapper
trivia projection reuse only, not comment import, processing-instruction
semantic support, comments-part editing, broad XML trivia preservation, XML
repair, or relationship repair.
These wrapper/default-style/empty-source dirty outputs now also fresh-reopen
through the public sparse views: normalized `s=0`, empty worksheets that acquire
a single sparse edit, wrapper metadata, relationship-bearing metadata,
range/reference metadata, and comments/processing-instruction wrappers all
verify `used_range()`, `sparse_cells()`, `row_cells()`, `column_cells()`, and
direct reads after `save_as()`.
Clean read-only materialized sessions are pinned as no-op save state too:
opening a `WorksheetEditor`, reading source shared string cells, and leaving the
sheet clean does not queue pending edits or dirty materialized names, and
`WorkbookEditor::save_as()` keeps the source package copy-original roundtrip
instead of flushing the standalone sparse worksheet projection. The copy-original
output now also fresh-reopens through public `WorksheetEditor` sparse views to
verify source shared string values, `used_range()`, `row_cells()`,
`column_cells()`, direct reads, and clean state. This is not clean-session
commit semantics, in-place save, transaction snapshot, sharedStrings migration,
or relationship repair.
That clean read-only copy-original path now also repeats a second no-op
`save_as()` in the same session: the second output is byte-identical to the
first output and source package, source and first output remain unchanged, and
fresh reopen still sees the same sparse cells. This is still not clean-session
commit semantics, in-place save, transaction history, or sharedStrings
migration.
That clean read-only copy-original path now also proves the same materialized
handle remains reusable after the repeated no-op save: a later escaped text edit
turns the session dirty, the dirty save reuses source shared-string indexes for
existing cells, appends the new escaped text to `xl/sharedStrings.xml`, keeps
source and both prior copy-original no-op outputs unchanged, fresh-reopens
through sparse views, and then settles into another byte-stable no-op save.
This is narrow same-workbook sharedStrings append evidence, not broad
sharedStrings index migration, sharedStrings cleanup, relationship repair,
clean-session commit semantics, or in-place save.
Prefixed source sharedStrings are now pinned on the same read-only
materialization path: `sst` / `si` / `t` / `r` element names may be prefixed and
are matched by local-name for public `WorksheetEditor` materialization, no-op
copy-original save, dirty inline projection, and source sharedStrings byte
preservation. This is not namespace URI validation, namespace repair, schema
validation, sharedStrings migration/writeback, or rich text preservation.
The sharedStrings no-op copy-original outputs now also fresh-reopen through the
public sparse views for source sharedStrings with legal prolog trivia and
legal XML declaration variants, simple rich-run flattening with ignored
phonetic metadata, `./sharedStrings.xml` relationship targets, prefixed
local-names, deliberately wrong namespace URIs, `xml:space` whitespace, and
inconsistent count /
unknown-attribute metadata, verifying clean `WorksheetEditor` state plus
`used_range()`, `sparse_cells()`, `row_cells()`, `column_cells()`, and direct
reads.
The base source sharedStrings read-only path now also repeats a second clean
no-op `save_as()` in the same session: first and second no-op packages match
the rewritten source bytes, source and first output remain unchanged, and the
second output fresh-reopens through the same public sparse views. This is still
copy-original/readback evidence, not sharedStrings migration/writeback or
in-place save.
The base source sharedStrings fixture now also proves dirty-save stability: an
in-memory edit appends to the existing string table, the dirty output
fresh-reopens through public sparse views, a follow-up no-op `save_as()` is
byte-stable, and the source / prior no-op packages remain unchanged. This is
not broad sharedStrings migration, schema repair, or in-place save.
The lazy invalid sharedStrings metadata paths now also prove post-dirty no-op
stability: after editing a sheet with no `t="s"` cells while workbook
sharedStrings metadata is missing-target, duplicated, malformed, or wrong
content type, dirty outputs preserve the bad source metadata, fresh-reopen with
the expected deferred diagnostic for shared-string sheets, a follow-up no-op
`save_as()` is byte-stable, and source / dirty packages remain unchanged. This
is deferred failure hygiene and dirty-session reuse evidence only, not
sharedStrings repair, relationship repair, schema/content-type repair, or broad
migration.
Those same lazy invalid sharedStrings paths now also continue after that
post-dirty no-op save: the borrowed `WorksheetEditor` can write another inline
text cell on the non-index sheet, keep the bad sharedStrings metadata opaque,
leave source / prior dirty / prior no-op packages unchanged, fresh-reopen with
the expanded sparse row and the same deferred diagnostic, and settle into
another byte-stable no-op save. This is still small-file In-memory
dirty-session handoff evidence, not metadata repair, string-table migration, or
relationship cleanup.
That same base fixture now continues after the post-dirty no-op save: the
borrowed `WorksheetEditor` can append a second plain shared string, preserve
the first appended index, advance `count` / `uniqueCount`, leave the earlier
dirty/no-op packages unchanged, and settle into another byte-stable no-op
save/reopen cycle. This remains narrow small-file In-memory sharedStrings
append evidence, not full sharedStrings migration.
The base rich sharedStrings fixture now also carries dirty save/reopen
evidence: a later in-memory edit appends a plain shared string, preserves source
rich-run markup bytes, leaves the source and prior no-op output unchanged,
fresh-reopens with flattened values through the public sparse views, and then
proves a follow-up no-op `save_as()` output is byte-stable. This is not
rich-text formatting preservation or broad sharedStrings migration.
That rich sharedStrings fixture now also continues after the post-dirty no-op
save: the same borrowed `WorksheetEditor` can append another text cell, preserve
the earlier rich/plain shared-string indexes and prior package bytes, advance
`count` / `uniqueCount`, fresh-reopen with the flattened sparse values, and
settle into another byte-stable no-op `save_as()`. This remains narrow
small-file In-memory sharedStrings reuse evidence, not sharedStrings rebuild,
rich-text formatting preservation, relationship repair, or in-place save.
The legal XML declaration sharedStrings cases now carry the same post-dirty
no-op evidence: dirty output reopens through public sparse views, the follow-up
no-op `save_as()` is byte-stable, and source/no-op packages remain unchanged.
This is not XML declaration repair or broad sharedStrings migration.
Those legal XML declaration cases now also have post-noop reuse coverage across
the declaration variants: after the byte-stable post-dirty no-op save, the same
borrowed `WorksheetEditor` can append another shared string, update count
metadata, keep source/prior packages unchanged, fresh-reopen with the expanded
sparse values, and settle into another byte-stable no-op `save_as()`. This is
still declaration materialization/save reuse evidence, not XML declaration
repair, sharedStrings writeback, or broad migration.
The same post-dirty no-op evidence now covers prefixed sharedStrings and
`xml:space` sharedStrings: dirty outputs fresh-reopen through public sparse
views, follow-up no-op `save_as()` outputs are byte-stable, and source/no-op
packages remain unchanged. This is still not namespace repair, rich-text
formatting preservation, or broad sharedStrings migration.
The prefixed sharedStrings path now also has post-noop reuse evidence: after the
byte-stable post-dirty no-op save, the same borrowed `WorksheetEditor` can write
another text cell, keep source sharedStrings bytes and prior packages unchanged,
fresh-reopen with the expanded sparse values, and settle into another
byte-stable no-op `save_as()`. This remains local-name materialization/reuse
evidence only, not namespace repair, sharedStrings writeback, or broad
sharedStrings migration.
The `xml:space` sharedStrings path now has the same post-noop reuse coverage:
after a byte-stable post-dirty no-op save, the same borrowed `WorksheetEditor`
can append whitespace-preserving text through sharedStrings, update count
metadata, fresh-reopen with the expanded sparse values, and settle into another
byte-stable no-op `save_as()`. This remains whitespace materialization and
small-file save/reuse evidence, not rich-text preservation, sharedStrings
rebuild, or broad migration.
The same save/reopen stability evidence now also covers wrong-namespace
local-name materialization and inconsistent count / unknown-attribute
sharedStrings metadata. Dirty outputs remain byte-stable across a follow-up
no-op `save_as()` and fresh-reopen through public sparse views. This is not
namespace URI validation, schema/count repair, or sharedStrings migration.
The wrong-namespace local-name path now also has post-noop reuse coverage:
after the byte-stable post-dirty no-op save, the same borrowed
`WorksheetEditor` can write another inline text cell, refresh the represented
used range, keep source sharedStrings bytes and prior packages unchanged,
fresh-reopen with the expanded sparse values, and settle into another
byte-stable no-op `save_as()`. This remains namespace-URI non-validation and
small-file save/reuse evidence, not namespace repair, schema validation, or
sharedStrings migration.
The inconsistent count / unknown-attribute sharedStrings metadata path now has
matching post-noop reuse coverage: after the byte-stable post-dirty no-op save,
the same borrowed `WorksheetEditor` can write another inline text cell, keep the
source sharedStrings bytes and prior packages unchanged, fresh-reopen with the
expanded sparse values, and settle into another byte-stable no-op `save_as()`.
This remains materialization/save reuse evidence only, not count repair,
unknown-attribute preservation, sharedStrings writeback, or broad migration.
Prefixed source worksheet XML is now pinned for the same narrow local-name
materialization boundary: worksheet, `sheetData`, row, cell, inlineStr wrapper,
rich-run, formula, and value-wrapper element names may be prefixed and still
materialize through public `WorksheetEditor` and package-backed `CellStore`.
Clean no-op save remains copy-original, while dirty public save uses the
standalone sparse-store worksheet projection and drops source prefixes,
ignored inline phonetic / extension text, and stale cached formula values. This
is not namespace URI validation, namespace repair, schema validation, metadata
preservation, XML repair, or rich-text formatting preservation.
The namespace boundary is now explicit too: this local-name materialization path
does not inspect element namespace URIs. Supported worksheet and sharedStrings
local-names bound to a deliberately non-spreadsheetml URI are covered by public
`WorksheetEditor` and package-backed `CellStore` tests. This documents
namespace-URI non-validation only; it is not namespace-aware OpenXML support,
schema validation, namespace repair, XML repair, or a general malformed-package
tolerance policy.
The reverse boundary is pinned as well: wrong-namespace unsupported local-names
remain fail-fast. Unsupported source cell metadata and inline string metadata
bound to a non-spreadsheetml URI fail through the same public and package-backed
failure-hygiene paths, leaving editor state clean and copy-original package
output unchanged. This is not malformed-package tolerance or metadata import.
The same boundary now covers `xl/sharedStrings.xml`: unsupported item-level and
rich-run local-names fail even when their elements use a non-spreadsheetml
namespace URI, while existing simple rich-run flattening, `rPr` formatting
metadata ignoring, and `rPh` / `phoneticPr` / `extLst` ignored metadata remain
supported. This is sharedStrings parser hardening, not schema validation,
namespace repair, sharedStrings writeback, or a rich-text object model.
Malformed sharedStrings rich metadata is now pinned more tightly as well:
mixed direct `<t>` text and rich `<r>` runs, `rPr` outside a rich run, and text
wrappers inside `rPr` fail before materialization. Simple rich-run flattening
and ignored formatting/phonetic/extension metadata still work; this is
fail-fast hygiene, not rich-text preservation, tolerant mixed-mode import, or
schema validation.

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
  - `fastxlsx.package_reader.workbook`
  - `fastxlsx.package_reader.zip-failures`
  - `fastxlsx.package_editor.core`
  - `fastxlsx.package_editor.core-worksheet`
  - `fastxlsx.package_editor.core-docprops`
  - `fastxlsx.package_editor.core-calc`
  - `fastxlsx.package_editor.core-linked`
  - `fastxlsx.package_editor.c5`
  - `fastxlsx.package_editor.preservation-core`
  - `fastxlsx.package_editor.preservation-core-drawings`
  - `fastxlsx.package_editor.preservation-core-docparts`
  - `fastxlsx.package_editor.preservation-core-docparts-drawing`
  - `fastxlsx.package_editor.preservation-core-docparts-shared`
  - `fastxlsx.package_editor.preservation-core-docparts-vba`
  - `fastxlsx.package_editor.preservation-removal`
  - `fastxlsx.package_editor.preservation-removal-policy`
  - `fastxlsx.package_editor.preservation-removal-workbook-parts`
  - `fastxlsx.package_editor.preservation-removal-drawing-parts`
  - `fastxlsx.package_editor.preservation-resources`
  - `fastxlsx.package_editor.preservation-comments`
  - `fastxlsx.package_editor.preservation-comments-threaded`
  - `fastxlsx.package_editor.preservation-comments-persons`
  - `fastxlsx.package_editor.preservation-comments-ordering`
  - `fastxlsx.package_editor.preservation-linked`
  - `fastxlsx.package_editor.preservation-linked-pivot`
  - `fastxlsx.package_editor.preservation-linked-pivot-cache-definition`
  - `fastxlsx.package_editor.preservation-linked-pivot-cache-records`
  - `fastxlsx.package_editor.preservation-linked-external-links`
  - `fastxlsx.package_editor.preservation-linked-custom-xml`
  - `fastxlsx.package_editor.preservation-linked-custom-xml-properties`
  - `fastxlsx.package_editor.preservation-linked-custom-xml-ordering`
  - `fastxlsx.package_editor.policy`
  - `fastxlsx.package_editor.policy-save-as-guards`
  - `fastxlsx.package_editor.policy-invalid-inputs`
  - `fastxlsx.package_editor.cellstore-core`
  - `fastxlsx.package_editor.cellstore-chunks`
  - `fastxlsx.package_editor.cellstore-source`
  - `fastxlsx.package_editor.cellstore-failures`
  - `fastxlsx.package_editor.cellstore-catalog`
  - `fastxlsx.package_editor.sheetdata`
  - `fastxlsx.package_editor.sheetdata-by-name`
  - `fastxlsx.package_editor.sheetdata-planned-catalog`
  - `fastxlsx.package_editor.sheetdata-catalog`
  - `fastxlsx.package_editor.sheetdata-catalog-guards`
  - `fastxlsx.package_editor.sheetdata-catalog-audits`
  - `fastxlsx.package_editor.sheetdata-guards`
  - `fastxlsx.package_editor.sheetdata-linked`
  - `fastxlsx.package_editor.sheetdata-linked-object-parts`
  - `fastxlsx.package_editor.sheetdata-linked-background-vml`
  - `fastxlsx.package_editor.policy`
  - `fastxlsx.workbook_editor.core`
  - `fastxlsx.workbook_editor.public`
  - `fastxlsx.workbook_editor.public-edge`
  - `fastxlsx.workbook_editor.public-retry`
  - `fastxlsx.workbook_editor.public-retry-reacquire`
  - `fastxlsx.workbook_editor.public-retry-guards`
  - `fastxlsx.workbook_editor.public-retry-projection`
  - `fastxlsx.workbook_editor.public-state`
  - `fastxlsx.workbook_editor.public-state-edits`
  - `fastxlsx.workbook_editor.public-state-source-style`
  - `fastxlsx.workbook_editor.public-state-shifts`
  - `fastxlsx.workbook_editor.public-state-formula-audits`
  - `fastxlsx.workbook_editor.public-state-reacquire`
  - `fastxlsx.workbook_editor.public-state-reacquire-guards`
  - `fastxlsx.workbook_editor.public-guards`
  - `fastxlsx.workbook_editor_source_success`
  - `fastxlsx.workbook_editor_source_success_shared_strings`
  - `fastxlsx.workbook_editor_source_success_max_coordinate`
  - `fastxlsx.workbook_editor_source_success_formulas`
  - `fastxlsx.workbook_editor_source_failures`
  - `fastxlsx.workbook_editor_materialized_sessions`
  - `fastxlsx.workbook_editor_facade`
  - `fastxlsx.workbook_editor_facade-save-as`
  - `fastxlsx.workbook_editor_facade-rename`
  - `fastxlsx.workbook_editor_facade-images`
  - `fastxlsx.workbook_editor_facade-smoke`
  - `fastxlsx.workbook_editor_formula_rewrite`
  - `fastxlsx.image`
- The source-success `WorksheetEditor` materialization regressions are now
  split into core, sharedStrings, max-coordinate, and formulas executables.
  The existing `fastxlsx.workbook_editor_source_success` CTest name remains the
  core shard; the added shard names are test-organization only and do not
  change source materialization semantics or public API behavior.
- The max-coordinate `WorksheetEditor` public save-as regressions now run under
  `fastxlsx.workbook_editor.public-edge`, keeping the general public facade
  shard inside the default 60s CTest budget without changing product API
  semantics.
- The source-backed max-coordinate `WorksheetEditor` regressions now also cover
  formulas at `XFD1048576`: read-only materialization ignores stale cached
  scalar values and no-op `save_as()` keeps copy-original bytes, while
  `erase_cell()` removes the edge formula and shrinks dirty projection back to
  `A1:B2`. This is not formula evaluation, cached-result preservation after
  dirty projection, calcChain rebuild, source reload, or large-file random
  editing.
- The same source-backed edge now covers workbook shared strings at
  `XFD1048576`: read-only materialization resolves `t="s"` through source
  `xl/sharedStrings.xml`, no-op `save_as()` keeps copy-original bytes, and
  `erase_cell()` removes the edge while dirty projection writes remaining text
  as inline strings and preserves the source sharedStrings part. This is not
  sharedStrings rebuild, index migration, writeback, pruning, rich text
  preservation, source reload, or large-file random editing.
- Source-backed max-coordinate coverage now also includes scalar and blank
  source cells at `XFD1048576`: number, boolean, and explicit blank records
  materialize through row/column and A1 APIs, no-op `save_as()` keeps
  copy-original bytes, and `erase_cell()` removes each edge so dirty projection
  shrinks to `A1:B2`. This is not dense allocation, source reload, row metadata
  repair, tombstone output, blank conversion, or large-file random editing.
- Source-backed max-coordinate coverage now also includes empty inline-string
  source cells at `XFD1048576`: empty `<t></t>` materializes as empty text,
  inlineStr without text materializes as blank, no-op `save_as()` keeps
  copy-original bytes, and `erase_cell()` removes each edge so dirty projection
  shrinks to `A1:B2`. This is not rich text preservation, XML repair,
  inline/scalar coercion, source reload, or large-file random editing.
- Source-backed max-coordinate coverage now also includes rich workbook
  sharedStrings at `XFD1048576`: simple rich runs flatten to plain text,
  phonetic/extension metadata text is ignored, no-op `save_as()` keeps
  copy-original bytes, and `erase_cell()` removes the edge while dirty
  projection preserves the source sharedStrings part and writes remaining text
  as inline strings. This is not rich text preservation, sharedStrings rebuild,
  writeback, index migration, relationship repair, source reload, or large-file
  random editing.
- Source-backed max-coordinate erase outputs now also carry follow-up clean
  no-op save coverage across inline text, formula, shared-string, scalar,
  empty-inline, and rich shared-string cases: the second packages are
  byte-identical to the erase outputs, source packages stay unchanged, and the
  no-op outputs fresh-reopen with the same compact `A1:B2` sparse state.
  The corresponding read-only no-op saves now also explicitly prove the source
  packages stay unchanged while copy-original outputs fresh-reopen at the
  `XFD1048576` edge.
- The mixed diagnostic recovery public-state path now also snapshots the
  untouched source-backed `Data` sheet through reopened `row_cells()` and
  `column_cells()` views while the recovered `Untouched` sheet exposes only its
  replacement cell through both row and column snapshots. This is saved-output
  readback coverage for clean `WorksheetEditor` inspection, not a broader
  recovery, metadata repair, or cross-sheet edit composition guarantee.
- The last-error recovery public-state path now also snapshots reopened
  `column_cells()` for the overwritten `A1`, preserved source-backed `A2`, and
  source-backed `B1`, keeping row/column readback parity with the saved recovery
  output. This is clean inspection coverage only, not structured diagnostic
  history or broader metadata repair.
- That last-error recovery path now also runs the same sparse, row, and column
  snapshot checks on the strict-options reacquired live handle, its repeated
  clean no-op saves, and its later post-noop overwrite handoff. This pins
  reacquired handle inspection parity only; it does not add structured
  diagnostic history or broader metadata repair.
- The missing-erase guardrail clean-output path now also snapshots reopened
  `column_cells()` for the preserved source-backed `A1` / `A2` column and
  source-backed `B1`, while the rejected `D4` column remains absent. This is
  saved-output sparse readback coverage only, not guardrail policy expansion.
- That missing-erase guardrail path now also applies the same sparse, row, and
  column snapshot checks to strict-options reacquired live handles, their
  repeated clean no-op saves, and the later post-noop `A1` overwrite handoff for
  both max-cells and memory-budget branches. This is clean handle readback
  parity only, not a broader missing-cell erase policy.
- The blank-overwrite guardrail recovery path now mirrors that column snapshot
  coverage for explicit blank `A1`, preserved source-backed `A2`, and
  source-backed `B1`, while the rejected blank `D4` column stays absent. This is
  readback parity for clean saved outputs, not a new blank-cell policy.
- That blank-overwrite guardrail path now also applies sparse, row, and column
  snapshot checks to strict-options reacquired live handles, their repeated
  clean no-op saves, and the later post-noop `A1` overwrite handoff for both
  max-cells and memory-budget branches. This is handle inspection parity only,
  not broader blank insertion or metadata repair policy.
- The erased-cell budget-release recovery path now also snapshots reopened
  source-backed `A1` / `B1` through `row_cells()` and `column_cells()`, while
  erased source-backed `A2` remains absent and inserted `D4` keeps its existing
  row/column readback. This is sparse-store recovery evidence only.
- That erased-cell budget-release path now mirrors those sparse, row, and column
  snapshots on the strict-options reacquired live handles, their repeated clean
  no-op saves, and the later post-noop handoff for both max-cells and
  memory-budget recoveries. This is handle readback parity only, not a broader
  guardrail or metadata repair policy.
- Current public API:
  - `Workbook`
  - `Worksheet`
  - `Cell`
  - `CellRange`
  - `RowOptions`
  - `CellValue`
  - `CellValueKind`
  - `DocumentProperties`
  - `WorkbookWriter`
  - `WorkbookWriterOptions`
  - `StringStrategy`
  - `WorksheetWriter`
  - `CellView`
  - `StyleId`
  - `CellAlignment`
  - `HorizontalAlignment`
  - `VerticalAlignment`
  - `CellFont`
  - `CellFill`
  - `CellStyle`
  - `DataValidationRule`
  - `DataValidationType`
  - `DataValidationOperator`
  - `DataValidationErrorStyle`
  - `HyperlinkOptions`
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
  - `TableOptions`
  - `TableTotalsFunction`
  - `ImageFormat`
  - `ImageInfo`
  - `ImagePixels`
  - `ImageEditAs`
  - `ImageAnchorOffset`
  - `ImageOptions`
  - `read_image_info()`
  - `read_image_pixels()`
  - `Workbook::add_worksheet()`
  - `Workbook::set_document_properties()`
  - `Workbook::rename_worksheet()`
  - `Workbook::remove_worksheet()`
  - `WorkbookWriterOptions::document_properties`
  - `WorkbookWriter::add_style()`
  - `CellView::with_style()`
  - `WorksheetWriter::add_data_validation()`
  - `WorksheetWriter::add_external_hyperlink()`
  - `WorksheetWriter::add_internal_hyperlink()`
  - `WorksheetWriter::add_table()`
  - `WorksheetWriter::add_conditional_color_scale()`
  - `WorksheetWriter::add_conditional_data_bar()`
  - `WorksheetWriter::add_conditional_icon_set()`
  - `WorksheetWriter::add_image()`
  - `WorkbookEditorOptions`
  - `WorkbookEditor`
  - `WorkbookEditor::open()`
  - `WorkbookEditor::open(path, options)`
  - `WorkbookEditor::worksheet_names()`
  - `WorkbookEditor::has_worksheet()`
  - `WorkbookEditor::has_pending_changes()`
  - `WorkbookEditor::pending_change_count()`
  - `WorkbookEditor::pending_replacement_cell_count()`
  - `WorkbookEditor::pending_replacement_worksheet_names()`
  - `WorkbookEditor::pending_materialized_worksheet_names()`
  - `WorkbookEditor::pending_materialized_cell_count()`
  - `WorkbookEditor::estimated_pending_materialized_memory_usage()`
  - `WorkbookEditor::has_pending_replacement()`
  - `WorkbookEditor::estimated_pending_replacement_memory_usage()`
  - `WorkbookEditor::last_edit_error()`
  - `WorkbookEditorWorksheetCatalogEntry`
  - `WorkbookEditor::worksheet_catalog()`
  - `WorkbookEditorWorksheetEditSummary`
  - `WorkbookEditor::pending_worksheet_edits()`
  - `WorkbookEditor::replace_sheet_data()`
  - `WorkbookEditor::rename_sheet()`
  - `WorksheetEditorOptions`
  - `WorkbookEditor::worksheet()`
  - `WorkbookEditor::try_worksheet()`
  - `WorksheetEditor`
  - `WorksheetEditor::name()`
  - `WorksheetEditor::try_cell()`
  - `WorksheetEditor::get_cell()`
  - `WorksheetEditor::set_cell()`
  - `WorksheetEditor::set_cells()`
  - `WorksheetEditor::set_cells(initializer_list<WorksheetCellUpdate>)`
  - `WorksheetEditor::append_row()`
  - `WorksheetEditor::append_row(initializer_list<CellValue>)`
  - `WorksheetEditor::set_row()`
  - `WorksheetEditor::set_row(initializer_list<CellValue>)`
  - `WorksheetEditor::set_column()`
  - `WorksheetEditor::set_column(initializer_list<CellValue>)`
  - `WorksheetEditor::erase_row()`
  - `WorksheetEditor::erase_rows()`
  - `WorksheetEditor::erase_column()`
  - `WorksheetEditor::erase_columns()`
  - `WorksheetEditor::set_cell_value()`
  - `WorksheetEditor::set_cell_values()`
  - `WorksheetEditor::set_cell_values(initializer_list<WorksheetCellUpdate>)`
  - `WorksheetEditor::clear_cell_value()`
  - `WorksheetEditor::clear_cell_values()`
  - `WorksheetEditor::clear_row()`
  - `WorksheetEditor::clear_rows()`
  - `WorksheetEditor::clear_column()`
  - `WorksheetEditor::clear_columns()`
  - `WorksheetEditor::clear_cell_values(CellRange)`
  - `WorksheetEditor::clear_cell_values(std::string_view)`
  - `WorksheetEditor::clear_cell_values(span<WorksheetCellReference>)`
  - `WorksheetEditor::clear_cell_values(initializer_list<WorksheetCellReference>)`
  - `WorksheetEditor::erase_cell()`
  - `WorksheetEditor::erase_cells()`
  - `WorksheetEditor::erase_cells(CellRange)`
  - `WorksheetEditor::erase_cells(std::string_view)`
  - `WorksheetEditor::erase_cells(span<WorksheetCellReference>)`
  - `WorksheetEditor::erase_cells(initializer_list<WorksheetCellReference>)`
  - `WorksheetEditor` strict uppercase single-cell A1 overloads
  - `WorksheetCellReference`
  - `WorksheetCellUpdate`
  - `WorksheetCellSnapshot`
  - `WorksheetEditor::has_pending_changes()`
  - `WorksheetEditor::sparse_cells()`
  - `WorksheetEditor::sparse_cells(CellRange)`
  - `WorksheetEditor::cell_count()`
  - `WorksheetEditor::estimated_memory_usage()`
  - `WorkbookEditor::save_as()`
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
    reader input groundwork only. It also has an internal
    `scan_worksheet_events_from_chunks()` chunk/window scanner with
    `WorksheetEventReaderOptions::max_window_bytes`; chunk-mode event views are
    callback-lifetime only, and tests cover cross-token chunk boundaries,
    source XML larger than the retained window, and oversized incomplete token
    rejection. It also exposes internal
    `scan_worksheet_events_from_chunk_source()` for pull-based file/reader
    sources; tests cover source callbacks matching full-buffer token output.
    This is still no public API, no full XML parser/schema validation, no
    relationship repair, and no PackageEditor stream rewrite by itself; the
    transformer chunk adapters are separate internal foundations.
  - Internal worksheet transformer action-model first slice in
    `include/fastxlsx/detail/worksheet_transformer.hpp` and
    `src/worksheet_transformer.cpp`, covered by `fastxlsx.worksheet_transformer`.
    It maps bounded cell replacement selectors onto event-reader tokens and
    emits source-order `PassThrough` / `ReplaceCell` actions plus missing-target
    diagnostics. It now also has an internal
    `emit_cell_replacement_worksheet()` chunk emitter that forwards pass-through
    source XML chunks and caller replacement cell XML through callback, with a
    narrow payload preflight that requires a `<c>` / `*:c` root and matching
    unqualified `r` attribute before action emission. It also exposes internal
    `scan_cell_replacement_actions_from_chunks()` /
    `emit_cell_replacement_worksheet_from_chunks()` for consuming the P8.19
    event-reader chunk/window stream, plus pull-based
    `scan_cell_replacement_actions_from_chunk_source()` /
    `emit_cell_replacement_worksheet_from_chunk_source()` for file/reader
    sources. Chunk-mode and source-mode action views are callback-lifetime only.
    Treat this as action/output-chunk and transformer input groundwork only: no
    public API, no full cell schema validation, no dependency repair, and no
    PackageEditor/EditPlan commit by itself.
  - Internal bounded PackageEditor cell-replacement handoff in
    `src/package_editor.hpp` and `src/package_editor.cpp`, covered by
    `fastxlsx.package_editor`. For source package worksheet entries,
    `replace_worksheet_cells()` and `replace_worksheet_cells_by_name()` now use
    `PackageReader::entry_chunk_source()` to scan source ZIP entries through
    chunk-source readers for root validation, dependency/dimension analysis,
    relationship-id audit, and output writing. Stored entries stream directly
    from the source ZIP payload with incremental CRC; in minizip builds,
    DEFLATE entries stream decompressed chunks through `entry_read` with EOF
    size/CRC validation, and abandoned DEFLATE chunk-source callbacks
    best-effort close their minizip entry/package handles. `extract_entry_to_file()`
    now consumes the same chunk source for DEFLATE entries; `read_entry()` also
    consumes the chunk source before returning a materialized string. Current planned staged worksheet chunks also feed the same chunk-source readers.
    Ordinary queued
    planned replacement strings now feed
    a string-view chunk-source reader; the string may already have been
    materialized by the prior planned replacement helper, so this is not a full
    planned-input low-memory pipeline. The handoff no longer materializes the
    full rewritten worksheet XML string. It scans the source
    action stream and replacement
    payloads first, computes top-level worksheet
    `<dimension>`, audits preserved source metadata plus replacement cell
    payloads, and skips old target cell payloads that will be removed from
    output. Relationship-id audit is also based on the rewritten action stream,
    so stale references inside replaced old cells do not pollute audit state.
    The second pass streams the dimension-refreshed output to a
    `PackageEditor`-owned temporary file-backed `PackageEntryChunk`, and
    `save_as()` forwards that chunk to `PackageWriter`. Invalid replacement
    cell payloads and audit-heavy replacement payload policy failures have
    PackageEditor-layer no-state-pollution coverage; the file-backed handoff also
    has coverage for `PackageReader` re-open, dimension refresh, old-target audit
    skip, linked-object fixture preservation/audit visibility, and temporary file
    cleanup after the editor is destroyed. `fastxlsx.package_reader` also covers
    direct stored entry chunk-source readback, CRC failure, and abandoned
    DEFLATE chunk-source handle cleanup, plus DEFLATE extract-to-file readback
    and corrupt-payload failure, and multi-chunk DEFLATE `read_entry()`
    materialization. There are also large source worksheet
    and large queued planned-string regressions where worksheet XML exceeds
    `package_editor_cell_replacement_materialized_input_byte_limit` and still
    completes cell replacement through chunk-source scanning. The
    linked-object regression covers
    worksheet `.rels`, drawing/media/chart/table/VML/percent-decoded drawing,
    sharedStrings plus owner `.rels`, styles, VBA, reachable unknown extension
    bytes plus owner `.rels`, workbook definedNames, PNG default content type,
    calcChain cleanup, and output re-read through `PackageReader`. Treat this
    as source-entry ZIP-entry chunk-source scanning for stored/minizip DEFLATE
    entries, planned staged-chunk chunk-source scanning, queued planned-string
    chunk-source scanning from an already-held string, and output-side
    file-backed stream handoff only: no public API, no low-memory DEFLATE
    `read_entry()` API behavior because it still returns `std::string`, no complete compressed-input streaming, no complete
    planned-input low-memory transformer, no
    broad range metadata recalculation, no sharedStrings/style migration, no
    relationship repair/pruning, no object semantic editing, and no full
    low-memory large-file editing claim. Planned-output notes now distinguish
    source-entry, planned staged-chunk, and queued planned-string `chunk-source`
    paths.
  - Internal package-entry chunked replacement source foundation in
    `src/package_editor.hpp` and `src/package_editor.cpp`, covered by
    `fastxlsx.package_editor`. `PackageEditor::replace_part_chunks()` records an
    existing package part as a `StreamRewrite` replacement backed by
    `PackageEntryChunk` memory/file chunks, and `save_as()` forwards those chunks
    to `PackageWriter` without flattening them into one string. The internal
    package writer now rejects entries that mix legacy `data` payload and
    chunked payload, invalid memory/file chunk-source combinations, and unknown
    chunk kinds before opening the output path, so staged chunks do not
    silently discard a second payload source. Treat this as a
    staged package-entry payload foundation only: no public API, no payload
    merge/repair, no full worksheet stream writer, no dependency repair, and no
    relationship/range metadata repair.
  - Internal worksheet replacement chunk handoff in `src/package_editor.hpp` and
    `src/package_editor.cpp`, covered by `fastxlsx.package_editor`.
    `PackageEditor::replace_worksheet_part_chunks()` reuses the current
    materialized worksheet XML validation, dependency audit, relationship audit,
    and calc metadata path, then records the target worksheet payload as
    `PackageEntryChunk` memory/file chunks for `save_as()`. Follow-up cell
    replacement can now consume those planned staged chunks through
    chunk-source readers, but the staged worksheet replacement helper itself
    still uses materialized validation/audit input. Treat this as a worksheet
    staged payload bridge only: no public API, no low-memory replacement
    validation/audit, no full worksheet stream writer, and no dependency or
    relationship repair.
  - Internal `CellPosition`, `CellRecord`, and worksheet-local sparse
    `CellStore` in `include/fastxlsx/detail/cell_store.hpp` and
    `src/cell_store.cpp`, plus internal `CellStoreOptions` for first-slice
    `max_cells` / `memory_budget_bytes` enforcement, plus an internal
    `cell_store_to_sheet_data_xml()` helper for standalone `<sheetData>`
    payload emission and a focused by-name `PackageEditor` handoff regression.
    Treat this as a P7 foundation slice only: it feeds the current narrow
    `WorkbookEditor::replace_sheet_data()` Patch facade, but it is still not a
    random cell editing API, workbook-level guardrail system, or full in-memory
    save-as / Patch handoff.
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
    defined names, support Zip64, or expose a public editing API.
    When the source is DEFLATE input, this path preserves unmodified part
    payload semantics. Minizip-enabled PackageEditor regressions now cover
    ordinary workbook replacement, unknown-extension target replacement, and the
    workbook calc metadata helper from a DEFLATE source, plus worksheet
    replacement with calcChain cleanup while
    linked payloads and unknown extension owner `.rels` re-ingest through output
    `PackageReader` / `RelationshipGraph`. It does not preserve source ZIP compression method,
    timestamps, extra fields, or compressed bytes.
    Beyond that contract, the same path now has a large body of narrow Patch
    preservation/audit regressions whose per-fixture detail lives in
    `docs/PATCH_PRESERVATION_COVERAGE.md`. Classified, those cover: (1)
    byte-preservation of worksheet `.rels`, drawing XML/`.rels`, media, chart,
    table, untouched sharedStrings/styles, VBA, and reachable unknown extension
    parts under the narrow worksheet replacement path; (2) worksheet-owned
    object fixtures (background picture, header/footer VML, printerSettings,
    OLE/control) preserved or audited under `sheetData` local rewrite, with
    explicit removal and same-path ordering; (3) linked-part fixtures
    (comments, threaded comments/persons, pivot table/cache, external links,
    custom XML item/properties, drawing/VML/percent-decoded drawing, media,
    table, sharedStrings, styles, VBA, chart) covering ordinary `replace_part()`,
    explicit removal, and remove/replace ordering, each preserving inbound
    relationships and content type audit without relationship/content-type
    repair; (4) `planned_output()` aggregate snapshots for those replace/remove/
    ordering states; (5) DEFLATE-source, no-op `save_as()` roundtrip, and
    no-state-pollution coverage for failure paths, including workbook
    `definedNames` preservation and narrow `ReferencePolicy` boundaries
    (linked-object failure, calcChain preserve/rebuild rejection, malformed or
    missing workbook metadata, request-recalculation output, and queued docProps
    preservation across a later linked worksheet rewrite failure). These remain
    internal Patch preservation/audit visibility only, not public API,
    relationship/content-type repair, orphan cleanup, semantic editing, or full
    object lifecycle support. This does not make image/chart/table/VBA
    passthrough complete.
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
  - A 2026-06-20 Python writer comparison helper exists:
    `tools/run_python_writer_benchmarks.py` benchmarks `xlsxwriter`
    constant-memory and `openpyxl` write-only writers as opt-in local
    reference tools. The same round records `100000 x 10 x 1 = 1000000`
    cell FastXLSX vs Python writer evidence in `docs/PERFORMANCE_TARGETS.md`.
  - P11.4 now adds the matching FastXLSX minizip/DEFLATE `100000 x 10 x 1`
    matrix, closing the stored-vs-compressed file-size fairness gap for the
    Python comparison. It records 7 cases, all `openpyxl` verified, with
    `office_open` still left to separate Office/WPS/LibreOffice validation.
  - P11.5 now adds OpenXLSX / xlnt independent C++ reference writer adapters
    behind `FASTXLSX_BUILD_REFERENCE_BENCHMARKS`,
    `windows-nmake-release-reference-benchmark`, and vcpkg
    `reference-benchmarks`. These libraries remain benchmark/reference targets
    only, not FastXLSX runtime, CMake default, CTest, or CI default
    dependencies.
  - P11.6 now records the benchmark scale ladder: 100k is adapter smoke, 1M is
    strategy screening / same-machine comparison, 10M and 50M are FastXLSX
    scaling and resource-boundary gates, and 100M+ / multi-sheet belongs to
    release readiness. The current 1M numbers should answer strategy questions,
    not be used as complete large-file proof.
  - P11.7 now records a FastXLSX-only 10M/50M scale ladder: 10M covers
    numeric-inline, mixed-inline, repeated-shared, and unique-inline under both
    stored and minizip/DEFLATE; 50M covers numeric-inline under both backends.
    The 50M numeric stored path ran in 28.7s at about 1.74 M cells/s and
    4.50 MB peak; the 50M numeric DEFLATE path ran in 63.4s at about
    0.79 M cells/s and 5.11 MB peak, shrinking output from 1702.30 MiB to
    140.06 MiB. `openpyxl` / Office validation was intentionally not run for
    this large scale probe.
  - P11.8 now exposes Streaming new-workbook compression level through
    `WorkbookWriterOptions::zip_compression_level`, adds benchmark
    `--compression-level`, and lets the matrix runner sweep multiple levels.
    The 10M minizip sweep shows level 1 as throughput-first, level 3 as a
    repeated-string tradeoff candidate, level 6 / backend default as
    smaller-file oriented, and level 9 as currently not useful. The 50M numeric
    level 1 point ran in 29.9s at about 1.67 M cells/s, 5.13 MB peak, and
    208.15 MiB output. Keep default `-1` on the backend default path; level 1
    remains a caller-selected fast profile, not the library default.
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

1. Queue reset - 基础.
   - The active next-task queue is `C0 -> C1 -> C2 -> C3 -> C4 -> C5 -> C6 -> C7`.
   - `P*` labels remain historical indices only; they are not the default
     execution order.
   - Current execution focus is API / feature design first: pick the smallest
     F0-F4 functional lane from `TASK_BREAKDOWN.md` before adding more code.
     Treat C6 performance / benchmark work as a support and validation line
     unless a selected feature changes the streaming hot path, resource
     boundary, ZIP backend, or release performance wording.
   - C0 validates the current code/docs baseline, C1 hardens the current
     `WorkbookEditor` Patch facade, C2 now reopens only for new preservation /
     dependency-audit gaps, C3 keeps the public editor decision gate, C4 has
     the public Patch guardrail / diagnostics first slice, C5 is the
     large-worksheet rewrite lane whose next input-side step is event-reader
     chunk/window consumption, C6 is the support line, and C7 is the release /
     packaging gate.
   - Keep docs, AGENTS, skills, and `TASK_BREAKDOWN.md` aligned with this
     queue and keep roadmap symbols separate from implemented code.

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
     creation, current narrow `WorkbookEditor` for existing-file Patch, and
     future `WorksheetEditor` / `WorkbookEditor` extensions for random small-file
     editing.
   - Keep low-level `PackageReader`, `PackageEditor`, `EditPlan`,
     `PartIndex`, and `RelationshipGraph` internal unless a later task proves a
     stable low-level public API is needed.
   - Define the `CellView` / `Cell` / `CellValue` split before adding
     random cell APIs.

4. In-memory small-file editing - 进行中 / internal foundations.
   - Keep this behind the F2 design gate until load/materialization guardrails,
     cell-store ownership, mutation semantics, and save-as handoff are explicit.
   - F2.1 internal source-backed worksheet-to-`CellStore` materialization has
     landed for bounded worksheet XML chunks/strings and `PackageReader`
     sheet-name lookup; F2.2 now has internal `try_cell()` semantics for
     missing vs explicit blank; F2.3 now has a first internal source-loaded
     `CellStore` -> by-name `sheetData` Patch handoff smoke, plus a focused
     blank-vs-erase projection smoke; F2.4 now has the first loader dependency
     guardrail regressions rejecting malformed source style attributes,
      unsupported cell types, and invalid boolean payloads. Explicit source `s`
      values are now normalized to no style handle only when the unqualified
      value is exactly `0` (`s="0"`, `s='0'`, or `s = "0"`); canonical
      non-zero unsigned decimal source style ids validate against source
      styles.xml `cellXfs`, materialize as numeric passthrough handles, and are
      written back when the source styles part is preserved. Empty, valueless,
      unquoted, unterminated, padded, signed,
      leading-zero, entity-encoded, or duplicate source style attributes still
      fail, with duplicate exact default-style attributes covered through the
      public facade hygiene path. Qualified style-like attributes such as
      `x:s="0"` stay unsupported metadata. Workbook-backed source sharedStrings
      now materialize valid `t="s"` cells as plain text. Non-critical
      `count` / `uniqueCount` metadata and well-formed unknown attributes do
      not drive materialization; malformed sharedStrings structures/targets or
      invalid indexes still fail fast, and standalone generic worksheet loaders
      still reject `t="s"` because they have no workbook-level table context.
      These source dependency/shape failures all avoid exposing a partial
      `CellStore`. A failed public `WorksheetEditor` materialization
      also leaves no dirty session or pending edit, so a later no-op
      `WorkbookEditor::save_as()` remains a copy-original package write.
       The package-backed by-name success path now also has a mixed semantic
       smoke for number, boolean, inline text entity decoding, formula text,
       explicit blank, and zero numeric cells flowing through the existing
       `CellStore` chunk-source `sheetData` handoff.
       Error cells (`t="e"`) now materialize as opaque `CellValueKind::Error`
       tokens, while date-like cells (`t="d"`) remain unsupported cell type
       failures.
      The source-backed package path now also has a state-hygiene regression:
      if a loadable source cell is followed by unsupported source semantics,
      loading fails before any caller-visible `CellStore` is returned and a
      separately opened `PackageEditor` remains copy-original with no calc
      policy, note, manifest, or output-plan pollution.
      The same package-path state-hygiene coverage now includes workbook sheet
      catalog targets whose worksheet ZIP entry is missing: loading fails
      before inventing a worksheet part or mutating `PackageEditor` state.
      Package-backed `CellStore` loader failures are now contextualized with the
      workbook sheet name, and when a worksheet part resolves successfully, the
      worksheet part and ZIP entry as well.
       A corrupt worksheet ZIP payload / CRC failure in the package-backed loader
       now preserves that context and leaves separately opened `PackageEditor`
       state copy-original.
       A corrupt workbook catalog payload / CRC failure during package-backed
       sheet lookup now preserves the requested sheet, materialized catalog
       read failure, `xl/workbook.xml` ZIP entry, and underlying CRC diagnostics
       without dirtying separately opened `PackageEditor` state.
       Duplicate sheet-name lookup diagnostics now have focused reader coverage
       for both the requested sheet name and the underlying ambiguous catalog
       error.
       The same duplicate sheet-name package path now has `PackageEditor`
       state-hygiene coverage: failed `CellStore` loading does not dirty edit
       state and a no-op save preserves both worksheet parts, calcChain, and
       unknown bytes.
       Missing sheet relationship ids in the workbook catalog now have the same
       package-path state-hygiene coverage: failed `CellStore` loading preserves
       the requested sheet diagnostic and leaves workbook `.rels`, worksheet,
       calcChain, and unknown bytes copy-original.
       Non-worksheet sheet relationship types now have matching package-path
       state-hygiene coverage: failed `CellStore` loading preserves the
       requested sheet diagnostic and leaves workbook `.rels`, worksheet,
       calcChain, and unknown bytes copy-original.
       Sheet relationships that resolve to a registered non-worksheet part now
       have the same package-path state-hygiene coverage, including preserving
       calcChain copy-original state instead of attempting content-type repair.
       External or URI-qualified sheet relationship targets now also have the
       same package-path state-hygiene coverage; the loader preserves the
       catalog diagnostics and does not materialize external/URI targets.
       The matching `PackageReader` workbook sheet catalog tests now pin the
       exact diagnostics for external targets, URI-qualified targets,
       non-worksheet relationship types, and registered non-worksheet targets.
       Malformed percent-encoded sheet relationship targets are now pinned on
       both `PackageReader` and package-backed `CellStore` loader paths:
       incomplete escapes, invalid escapes, and decoded null bytes fail before
       materialization and preserve separately opened editor state.
       Package root `officeDocument` catalog failures now have matching
       package-backed loader state-hygiene coverage for missing, duplicate,
       external, URI-qualified, non-fixed, and malformed-percent workbook
       entrypoint relationships.
       The matching `PackageReader` workbook catalog tests now pin exact
       diagnostics for those package root `officeDocument` failures, and the
       package-backed loader matrix covers incomplete percent escapes and
       decoded null bytes as well as invalid escapes.
       Workbook `<sheet>` catalog attribute failures now also have precise
       `PackageReader` diagnostics and matching package-backed loader
       state-hygiene coverage for missing/wrong-namespace relationship ids,
       unqualified `id`, namespaced `name`, and namespaced `sheetId`.
       Workbook sheet relationship resolution now also has precise reader
       diagnostics for sheet ids absent from workbook `.rels` and relationships
       targeting unregistered package parts, including planned workbook XML and
       planned sheet-name lookup paths.
       Planned workbook XML catalog failures now also pin exact diagnostics for
       wrong-namespace relationship ids, unqualified `id`, missing workbook
       relationships, and ignored non-catalog / nested decoy sheet-name lookups.
       Direct workbook sheet-name lookup now also pins exact missing-sheet and
       ambiguous duplicate-sheet diagnostics, including decoy / nested sheet
       tags that must remain ignored.
       Direct `CellStore` sheetData chunk-source emission now also pins
       caller-supplied non-default `StyleId` serialization as `s="N"` and
       continues to omit explicit default `StyleId{}`; this is not source style
       preservation, migration, merge, or validation. The same coverage now
       verifies styled cells still flow as individual row/cell chunks, not a
       reintroduced full `<sheetData>` string helper.
       The internal `PackageEditor` by-name `CellStore` handoff now also covers
       a writer-owned style handle end-to-end: replacement sheetData writes the
       caller-supplied `s="1"`, explicit default `StyleId{}` is omitted,
       `xl/styles.xml` stays byte-preserved, and style dependency audit reaches
       both `EditPlan` and `planned_output()`.
       Source-loaded `CellStore` now also distinguishes unreferenced styles
       preservation from unsupported source cell style references: a package may
       contain `xl/styles.xml` and styled unrelated sheets, while unstyled target
       sheet cells load without invented style handles and save-as preserves
       styles bytes copy-original.
       The same distinction now exists for unreferenced sharedStrings: a package
       may contain `xl/sharedStrings.xml` from unrelated sheets, while a target
       sheet with no `t="s"` cells loads into `CellStore`, emits inline text for
       new string values, and preserves sharedStrings bytes copy-original.
       These unreferenced styles/sharedStrings fixtures now also pin workbook
       `.rels` preservation for the existing styles/sharedStrings relationships;
       this is copy-original preservation, not relationship synthesis or repair.
       The same fixtures now also pin OPC metadata re-read visibility: output
       `PackageReader` keeps the relevant content type override and
       `RelationshipGraph` can re-read the workbook styles/sharedStrings
       relationships after save-as.
      Package-backed by-name loading now also pins prefixed source
      sharedStrings local-name materialization: `sst` / `si` / `t` / `r` markup
      may be prefixed, materializes through `CellStore`, and still saves via
      inline projection while preserving the source sharedStrings part. This is
      not namespace validation/repair or sharedStrings migration/writeback.
       Internal `CellStore` mutation guardrails also cover coordinate-validation
       failures preserving existing sparse records.
      Loader XML entity decoding guardrails now reject unknown entities,
      unterminated entities, invalid character references, and out-of-range
      character references before returning a `CellStore`.
      Loader materialization now also has focused coverage for enforcing
      `CellStoreOptions::max_cells` and `memory_budget_bytes` while loading
      source worksheet XML.
      Returned source-loaded `CellStore` instances now also preserve those
      guardrails for later sparse-store mutations: exact `max_cells` rejects a
      subsequent insert, and exact `memory_budget_bytes` rejects an oversized
      overwrite without changing the loaded payload.
      Package-backed by-name loading now has the same guardrail-persistence
      coverage, proving `load_cell_store_from_workbook_sheet()` does not drop
      the supplied `CellStoreOptions` after resolving and streaming the source
      worksheet part.
      Package-backed by-name loading now also has source-shape failure
      state-hygiene coverage for duplicate same-cell-reference input rejected
      by the current source-order guardrail, duplicate explicit rows,
      out-of-order explicit rows, and out-of-order cells; these failures keep
      `PackageEditor` plans, calc policy, manifest modes, and saved output bytes
      copy-original.
      Package-backed by-name loading now also has metadata/formula failure
      state-hygiene coverage for unsupported row/cell metadata attributes,
      scalar/inline-text value-wrapper attributes, duplicate/empty/attributed
      formula elements, unsupported inline string metadata, and malformed
      inline rich text metadata such as mixed direct/rich text, `rPr` outside a
      run, and value wrappers inside `rPr`; simple inline rich text now flattens
      to plain text instead of remaining a rejection path. This is still not
      preservation, migration, or XML repair.
      Package-backed by-name loading now also has payload/state-machine failure
      state-hygiene coverage for shared string indexes, unsupported cell type
      tokens, invalid boolean payloads, duplicate scalar/inline-text wrappers,
      direct raw cell text outside value wrappers, cell-contained comments /
      processing instructions / CDATA, worksheet-root raw text outside wrapper
      metadata or sheetData, sheetData raw text outside rows, row raw text
      outside cells, nested cell input rejected at the event-reader boundary,
      and cells outside rows; these remain
      failure-before-edit-state guardrails.
      Package-backed by-name loading now also has coordinate/numeric failure
      state-hygiene coverage for missing or malformed cell references, row/column
      overflow, row/cell mismatches, invalid explicit row numbers, formula
      elements in boolean cells, and non-finite numeric payloads.
      Package-backed by-name loading now also has attribute/parser failure
      state-hygiene coverage for unquoted or unterminated cell reference
      attributes and duplicate row/cell key attributes.
      Package-backed by-name loading now also has XML entity failure
      state-hygiene coverage for unknown entities, unterminated entities,
      invalid character references, and Unicode range overflow.
      Package-backed by-name loading now also has cell-type/inline-shape failure
      state-hygiene coverage for missing/empty error-cell values, date-like
      cells, inline payloads in non-inline cells, and ordinary values in
      inline-string cells.
      Duplicate source cell references now also fail before returning a
      `CellStore`, instead of letting sparse-store insertion silently overwrite
      the earlier source XML payload.
      Duplicate explicit row `r` values now also fail before returning a
      `CellStore`, instead of merging ambiguous source row elements.
      Out-of-order explicit row `r` values now fail before materialization
      instead of relying on sparse-store sorted emission to rewrite source order.
      Out-of-order source cell references now also fail before materialization
      instead of relying on sparse-store sorted emission to rewrite source cell
      order.
      Duplicate inspected key attributes such as row/cell `r` and cell `t` now
      fail before materialization instead of silently using the first attribute
      value.
      Duplicate value wrappers inside one source cell now also fail for scalar
      `<v>`, formula `<f>`, and inline text `<t>` shapes instead of being
      concatenated into ambiguous materialized values.
      Known source formula metadata attributes `t`, `ref`, `si`, `aca`, `ca`,
      `bx`, `dt2D`, `dtr`, `del1`, `del2`, `r1`, and `r2` now load lossily:
      formula cells with text are projected as plain formula text, source-order
      shared formula followers can materialize translated plain formula text,
      and unresolved metadata-only shared formula cells can materialize
      supported cached scalar `<v>` values. Empty formula text, invalid shared
      formula indexes, and unknown formula attributes still fail before
      materialization, so this is not shared/array/dataTable formula metadata
      preservation or a full formula parser.
      Cells outside row elements now also fail before materialization, keeping
      source-backed loading scoped to row-contained cells.
      Unsupported source row/cell metadata attributes still fail before
      materialization, except source cell `ph` phonetic markers are accepted and
      ignored instead of being projected into the sparse `CellStore`.
      Unsupported scalar `<v>` and inline text `<t>` value-wrapper attributes
      now also fail before materialization; inline text `xml:space` remains
      loadable as plain semantic text only.
      Simple inline-string rich text runs now flatten to plain text; inline
      `rPh` / `phoneticPr` / `extLst` metadata text is ignored. Malformed rich
      text, unsupported rich-run shapes, and unknown inline metadata still fail
      before materialization.
      Direct raw cell text outside value wrappers, worksheet-root raw text
      outside wrapper metadata or sheetData, sheetData raw text outside rows,
      row raw text outside cells, cell-contained comments, processing
      instructions, and unsupported markup such as CDATA now also fail before
      materialization instead of being silently dropped from inline text or
      other source worksheet payloads.
      Focused loader coverage now also pins invalid boolean payload rejection.
      Malformed cell reference guardrails now also cover missing `r`,
      out-of-range `r`, zero-row refs, last-row overflow refs,
      non-column-first refs, unquoted attributes, unterminated attributes, and
      invalid row `r` values.
      Inline-string shape guardrails now reject `<is><t>` payloads on non-inline
      cells and ordinary `<v>` payloads on `t="inlineStr"` cells.
      Loader state-machine guardrails now reject nested cells.
      Tombstone / style-preservation policy is now explicitly frozen for the
      next gate: `erase_cell()` removes the sparse record, `CellValue::blank()`
      is the explicit blank replacement cell, and workbook-backed source style
      ids are same-workbook numeric passthrough after validating source
      styles.xml `cellXfs`, not migrated or merged. Explicit
      default source style references are normalized to no style handle, but
      only for unqualified `s` values exactly equal to `0` (`s="0"`, `s='0'`,
      or `s = "0"`); canonical non-zero unsigned decimal source style ids are
      written back when the source styles part is preserved. Empty, valueless,
      unquoted, unterminated, padded, signed, leading-zero, entity-encoded,
      duplicate, or qualified source style attributes remain load failures. The
      internal loader declaration now
      mirrors these source
      materialization guardrails, including cached formula value omission,
      entity/attribute failures, duplicate references/wrappers, and load-time
      options.
      Package-backed load-time `CellStoreOptions` failures now also have
      state-hygiene coverage: `max_cells` and `memory_budget_bytes` overflows
      after an earlier loadable source cell fail with sheet/part/ZIP context,
      leave `PackageEditor` plans and calc policy unchanged, and keep aggregate
      output-plan decisions copy-original.
      P8.285 gate audit now treats this as enough internal evidence for public
      API design drafting, not enough evidence for a public `WorksheetEditor`
      header.
      P8.286 now drafts the first public `WorksheetEditor` slice as
      In-memory / existing-workbook small-file editing, with explicit
      worksheet-only materialization, draft load options, narrow
      `try_cell()` / `set_cell()` / `erase_cell()` semantics, source dependency
      failure policy, save-as handoff blockers, and operation mixing rules.
      P8.287 adds the acceptance matrix and open-decision checklist, including
      row/column-first coordinates, `try_cell()` before `get_cell()`,
      non-default `StyleId` rejection as the recommended first style policy, and
      rejection of ambiguous rename / whole-sheet replacement mixing on
      materialized sheets.
      P8.288 adds future Doxygen and README wording drafts in the API design
      document, explicitly marked as unavailable in current public headers.
      P8.289 adds the save-as public facade acceptance design: dirty tracking
      starts at successful cell mutations, materialization alone is not pending
      edit state, public tests must cover set/blank/erase projection, package
      preservation, calc policy, dimension policy, output path guards,
      operation mixing, and staged diagnostics before a header is added.
      P8.290 adds the implementation preflight checklist for naming, handle
      ownership, first-slice scope, style/source dependency policy,
      dimension/calc policy, diagnostics, public facade tests, test-shard
      budget, and validation gates.
      P8.291 splits package-editor F2 CellStore coverage into the
      `fastxlsx.package_editor.cellstore-*` shard family and sheetData catalog /
      guardrail / linked-object coverage into `fastxlsx.package_editor.sheetdata-catalog`,
      `fastxlsx.package_editor.sheetdata-guards`, and
      `fastxlsx.package_editor.sheetdata-linked`, keeping `sheetdata` focused
      on base sheetData / by-name coverage and leaving room for targeted future
      public facade tests.
      P8.292 records the review-only public-header diff: current public headers
      still expose the landed `WorkbookEditor` Patch facade only; no public
      `WorksheetEditor`, `WorksheetEditorOptions`, `try_worksheet()`, or random
      cell-editing facade exists yet.
      P8.294 splits the formerly heavy package-editor `preservation-core`
      coverage into `fastxlsx.package_editor.preservation-removal`,
      `fastxlsx.package_editor.preservation-resources`, and
      `fastxlsx.package_editor.preservation-comments`, keeping preservation
      coverage aligned with the 60s CTest shard budget before more public-editor
      gate work is added.
      The preservation-removal executable now follows the same split pattern:
      base removal keeps the unknown-extension, workbook, worksheet, drawing,
      chart, and media removals; the new policy-failure, workbook-owned part,
      and drawing/VML shards cover the remaining families while keeping the
      original `fastxlsx.package_editor.preservation-removal` CTest name
      stable for the base shard.
      P8.295 extends the no-op `WorkbookEditor::save_as()` failed-edit
      diagnostic regression to failed `rename_sheet()` as the other current
      public edit path; no-op output stays a source-entry copy, the failed
      rename diagnostic is preserved, and a later valid rename remains usable.
      P8.296 syncs the README with that state: package-editor tests are described
      as split `fastxlsx.package_editor.*` shards, and no-op `save_as()` is
      documented as preserving prior failed `replace_sheet_data()` or
      `rename_sheet()` diagnostics until the next successful public edit.
      P8.297 finishes the moved-from `WorkbookEditor` public diagnostics
      boundary by checking false/zero/empty results for pending-state methods
      that are specified not to throw.
      P8.298 covers the complementary moved-to `WorkbookEditor` boundary:
      queued replacement, queued rename, failed-edit diagnostic, edit summaries,
      and save-as output all survive move construction without turning move into
      commit/close semantics.
      P8.299 covers the matching move-assignment boundary: assigned queued
      replacement, queued rename, failed-edit diagnostic, and save-as output
      replace the target editor's previous queued state without merging,
      committing, or leaking discarded edits.
      P8.300 covers the clean-source move-assignment boundary: assigning a
      clean opened editor over a dirty target clears the target's queued
      replacement, rename, edit summaries, and `last_edit_error()`, and no-op
      `save_as()` copies the assigned source package.
      P8.301 syncs the public `WorkbookEditor` move constructor and move
      assignment Doxygen plus the API design public-header audit with those
      regressions, explicitly documenting ownership transfer, target-state
      replacement, moved-from diagnostics, and non-commit semantics.
      P8.302 covers clean move construction: a moved-to editor with no queued
      public edits keeps source/planned catalog inspection, empty diagnostics,
      and no-op `save_as()` source-package roundtrip behavior.
      P8.303 covers move assignment into a moved-from target: the target can
      receive an assigned editor session with queued edits and diagnostics, then
      save the assigned state, instead of remaining permanently moved-from.
      P8.304 covers move construction with `WorkbookEditorOptions`: replacement
      guardrails such as `max_replacement_cells` survive ownership transfer and
      remain part of the documented editor session state.
      P8.305 covers move assignment with `WorkbookEditorOptions`: the assigned
      source editor's replacement guardrails replace the target's previous
      guardrails instead of being merged or retained.
      P8.306 / P8.307 extend the same move-construction and move-assignment
      coverage to `replacement_memory_budget_bytes`, including no-op save-as
      state hygiene after guarded replacement failures.
      P8.308 covers assignment from a default-options source editor over a
      strict-options target, proving target-side replacement guardrails are
      cleared rather than retained.
      P8.309 covers assignment from an already moved-from source editor over a
      dirty target: the target becomes moved-from / not open, stale target
      queued edits, diagnostics, and options are discarded, and `save_as()`
      throws instead of leaking the discarded target state.
      P8.310 records the future `WorksheetEditor` implementation gate checklist:
      before any public header is added, the task must explicitly decide the
      separate worksheet materialization options type, handle lifetime /
      invalidation behavior, diagnostics surface, operation mixing policy, and
      save-as handoff tests.
      P8.311 resolves the first diagnostics decision: worksheet
      materialization failures throw `FastXlsxError` and must not update
      current edit-only `last_edit_error()`; `try_worksheet()` returns empty
      only for missing sheet names.
      P8.312 resolves the options naming and call-site decision:
      `WorksheetEditorOptions` is the per-materialization options type
      for `worksheet(name, options)` / `try_worksheet(name, options)`, with
      `max_cells` and `memory_budget_bytes` separate from current
      `WorkbookEditorOptions` replacement-payload guardrails.
      P8.313 resolves the first handle lifetime and operation-mixing decision:
      `WorkbookEditor` owns materialized worksheet state, returned handles are
      borrowed and must be reacquired after moving the owner, repeated
      materialization requires matching options, and ambiguous rename /
      whole-sheet replacement mixing is rejected before state changes.
      P8.314 resolves the save-as handoff behavior decision: materialization
      alone is not pending edit state, dirty sparse stores persist only through
      `WorkbookEditor::save_as()`, top-level worksheet `<dimension>` must be
      refreshed for emitted cell extents before any public header, and other
      range metadata remains audit / fail / preserve only.
      P8.315 adds the first implementation evidence for that dimension rule:
      internal `cell_store_dimension_reference()` computes sparse-store emitted
      extents, including erase-driven shrink and explicit blank records, without
      changing current Patch facade `replace_sheet_data()` semantics.
      P8.316 connects that dimension projection to an internal
      `cell_store_worksheet_chunk_source()` that emits a minimal full worksheet
      XML chunk stream for future in-memory save-as handoff evidence, still
      without changing current Patch facade wrapper preservation semantics; the
      helper is also covered through an internal package-editor by-name full
      worksheet replacement smoke.
      P8.317 adds one focused current-facade operation-mixing regression:
      replacement + rename to a temporary planned name + rename back to the
      source name restores the planned catalog mapping, migrates pending
      replacement diagnostics back to the source name, clears the public
      `renamed` flag in summaries, and does not leak the transient name into
      saved output.
      P8.318 adds the complementary rename-only chain regression:
      `Data -> TemporaryA -> TemporaryB -> Data` restores the source-to-planned
      catalog mapping, leaves `pending_worksheet_edits()` empty because there is
      no final planned-state edit, keeps replacement diagnostics empty, and does
      not leak transient planned names into saved output.
      P8.319 verifies the same restored planned name remains usable after a
      later failed duplicate rename: a follow-up `replace_sheet_data("Data",
      ...)` clears the prior edit diagnostic, reports replacement diagnostics
      under the restored source name, and saves only the final replacement
      payload.
      P8.320 closes the wording-only review gate: README / API docs / task docs
      agreed at that point that the implemented public surface remained the
      narrow `WorkbookEditor` Patch facade, while `WorksheetEditorOptions`,
      `worksheet()` / `try_worksheet()`, `get_cell()` / `set_cell()` /
      `erase_cell()` were not yet public header symbols.
      P8.378 later supersedes that historical boundary for
      `WorksheetEditorOptions`, `worksheet()`, `try_cell()`, `set_cell()`, and
      `erase_cell()`; P8.379 supersedes it for `try_worksheet()` and
      `get_cell()`.
      P8.321 adds the first concrete post-wording internal evidence: a
      source-loaded `CellStore` handed to by-name `<sheetData>` Patch after a
      queued sheet-catalog rename rejects the old source name without mutating
      the rename state, then succeeds when called with the planned new name.
      P8.322 adds the same planned-name guardrail for the full worksheet
      `CellStore` chunk projection path, including staged `StreamRewrite` and
      refreshed top-level worksheet `<dimension>` evidence.
      P8.323 verifies both old-name failure paths reject before consuming their
      prepared `CellStore` chunk sources, so the same source can still be used
      by the planned-name retry.
      P8.324 adds the next operation-mixing guardrail: a source-loaded
      `CellStore` follow-up `<sheetData>` patch after a queued whole-worksheet
      replacement uses the planned worksheet wrapper, preserves queued wrapper
      metadata, and does not resurrect source-only payload.
      P8.325 adds the paired full worksheet projection rule: a later full
      worksheet `CellStore` handoff supersedes the prior queued worksheet
      wrapper, stages `StreamRewrite`, and refreshes `<dimension>`.
      P8.326 adds the combined rename + queued-worksheet case: source-loaded
      `CellStore` `<sheetData>` handoff must use the planned sheet name, old
      source-name failure must not consume chunks, and the successful planned
      name path still patches the queued wrapper.
      P8.327 adds the paired full worksheet projection case for that same
      combined setup: old source-name failure still consumes zero chunks, while
      the planned-name path stages `StreamRewrite`, replaces the queued wrapper,
      preserves the renamed catalog, and refreshes `<dimension>`.
      P8.328 adds save-as failure hygiene on that combined staged state:
      source-overwrite rejection preserves the staged chunks / pending edits and
      a later safe output path still saves.
      P8.329 extends that guard to path-equivalent source overwrite attempts,
      proving non-identical path strings that resolve to the source package also
      preserve the same staged state before a safe retry.
      P8.330 adds the same combined staged-state save-as hygiene for empty
      output paths: the guard fails before staged chunks / pending rewrite state
      are dropped, and a later safe retry still saves.
      P8.331 adds the same guard for missing output parent paths.
      P8.332 / P8.333 add the same guard for non-directory output parents and
      existing-directory output paths.
      P8.334 adds writer/backend failure hygiene for the same combined staged
      state: failed output writing preserves staged chunks, existing output
      bytes, and temp-file cleanup before a safe retry.
      P8.335 proves a successful safe `save_as()` also keeps staged chunks
      reusable for a second safe output path.
      P8.336 adds source-copy temp-size failure hygiene for the same combined
      staged state: changed save-time copy-original temp files do not overwrite
      output or drop staged chunks before a safe retry. P8.337 adds the matching
      missing source-copy temp-file failure hygiene. P8.338 adds the matching
      source-copy temp CRC failure hygiene. P8.339 adds a workbook planned-removal
      preflight guard for source-loaded full worksheet chunks. P8.340 adds an
      invalid planned workbook catalog preflight guard for the same chunk
      handoff. P8.341 adds the matching wrong-namespace planned catalog id
      guard. P8.342 adds the matching plain unqualified planned catalog id
      guard. P8.343 adds the matching unregistered planned worksheet target
      guard. P8.344 refreshes the F2 gate: do not keep adding same-family
      planned-catalog negative tests by default; the next useful action is the
      first public `WorksheetEditor` implementation task plan and public tests,
      while keeping the header closed.
   - The next narrow candidate should stay behind the same F2 gate and add
     only a newly evidenced internal guardrail gap, or deliberately open the
     public header implementation task with tests for missing/materialization
     failures, handle lifetime, option matching, rename / whole-sheet
     replacement mixing rejection, dimension refresh, save-as persistence, and
     diagnostics. Do not add a public `WorksheetEditor` header as a wording-only
     change.
   - It may use sparse cell storage or local DOM where appropriate for small
     workbooks, but must document memory growth and must not become the
     large-data default path.

5. Writer/backend hardening - 进行中 / 基础.
   - sharedStrings remains 进行中: keep `inlineStr` as the low-memory default,
     and expand benchmark/reference evidence before widening support wording.
   - vcpkg / CMakePresets / CI remains 基础: `stb` is default, minizip is opt-in,
     Excel visual verification remains local, and benchmark jobs stay opt-in.
   - Python writer, FastXLSX minizip/DEFLATE, and OpenXLSX/xlnt adapter
     evidence now exist as opt-in support benchmark lines. Use
     `docs/PERFORMANCE_TARGETS.md` as the canonical data record before making
     broader performance claims.

## Repository State

- Local Git repository initialized on branch `main`.
- Public GitHub repository created and pushed:
  `https://github.com/wuxianggujun/FastXLSX`
- `origin/main` is configured as the upstream branch.

## Recommended Push Order

Use this order for the next implementation pushes. Each item should be a small
commit or short series with its own tests and docs update.

1. Task docs, skills, and branch/worktree context.
   - Align `TASK_PLAN.md`, `TASK_BREAKDOWN.md`, `NEXT_STEPS.md`, `AGENTS.md`,
     and FastXLSX skills with the editable XLSX library positioning.
   - Reconcile concurrent agent output before overlapping file edits.
   - Keep the cross-language reference notes current in `ARCHITECTURE.md`.
     Feature tasks should name the reference libraries they borrow from and
     the architecture limits they intentionally avoid.
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
   - F2.1 internal source-backed worksheet materialization and F2.2
     `try_cell()` missing-vs-blank semantics are now the baseline; F2.3 first
     source-loaded `CellStore` handoff smoke proves the existing by-name
     `sheetData` Patch helper can consume mutated source-backed sparse cells;
     focused blank-vs-erase coverage now fixes current projection behavior:
     explicit blank writes an empty cell, erase omits the cell. F2.4 first
      guardrail coverage now rejects malformed source worksheet style
      attributes, standalone shared string indexes, unsupported cell types, and
      invalid boolean payloads before pretending migration or repair exists;
       explicit source `s="0"` is normalized to no style handle, and canonical
       non-zero source style ids validate source styles.xml `cellXfs` before
       same-workbook passthrough. These
      failure paths
      do not expose a partial `CellStore`, and `CellStore` coordinate validation
      failures now also have no-state-pollution coverage.
      Error cells (`t="e"`) now materialize as opaque tokens; missing/empty
      error payloads and date-like cells (`t="d"`) remain explicit failures.
      The package/source-backed loader path now also proves an unsupported
      semantic after an earlier loadable cell does not expose a partial
      `CellStore` or mutate `PackageEditor` state.
      It also covers missing worksheet package entries resolved through the
      workbook sheet catalog, keeping no-op `PackageEditor` output copy-original.
      Loader diagnostics now carry sheet-name context and, after successful
      catalog resolution, worksheet part / ZIP-entry context for package-backed
      source loading failures.
      Missing sheet-name lookup diagnostics now have focused reader coverage for
      both the requested sheet name and the underlying workbook catalog miss.
      Corrupt worksheet entry CRC failures are covered on that same path and do
      not dirty `PackageEditor` state.
      XML entity decoding failures now also fail before returning a `CellStore`.
      Loader option guardrails now cover `max_cells` and memory-budget failures
      during source worksheet materialization.
      Duplicate source cell references now fail before returning a `CellStore`,
      rather than using last-write-wins sparse-store overwrite behavior.
      Duplicate explicit row numbers now fail as source-shape guardrails.
      Out-of-order explicit row numbers now fail as source-shape guardrails.
      Out-of-order source cell references now fail as source-shape guardrails.
      Duplicate inspected row/cell reference and cell type attributes now fail
      as key-attribute guardrails.
      Duplicate scalar/formula/inline-text wrappers inside one source cell now
      fail as source-shape guardrails.
      Known source formula metadata attributes `t`, `ref`, `si`, `aca`, `ca`,
      `bx`, `dt2D`, `dtr`, `del1`, `del2`, `r1`, and `r2` are accepted as
      lossy metadata: formula text is projected as plain formula text,
      source-order shared formula followers are materialized as translated plain
      formula text, and unresolved metadata-only shared formulas can materialize
      supported cached scalar values. Unknown formula attributes, invalid shared
      formula indexes, and empty formula text still fail as formula-shape
      guardrails.
      Cells outside row elements now fail as row-scope guardrails.
      Unsupported source row/cell metadata attributes still fail as metadata
      guardrails, except source cell `ph` phonetic markers are tolerated and
      ignored.
      Unsupported scalar and inline-text value-wrapper attributes now fail as
      value-wrapper guardrails, with `xml:space` still accepted for plain text.
      Unknown inline string child metadata still fails as a guardrail; simple
      inline rich text runs flatten to plain text and inline phonetic /
      extension metadata text is ignored. Mixed direct/rich inline text, `rPr`
      outside a run, value wrappers inside `rPr`, and unclosed rich/ignored
      inline metadata remain fail-fast malformed rich-text guardrails.
      Cell-contained comments, processing instructions, and unsupported markup
      now fail as source cell markup guardrails.
      Invalid boolean payload rejection is covered by the focused loader test as
      well as the broader source dependency/shape policy wording.
      Malformed cell reference, attribute, and row-coordinate failures are now
      covered by the same focused loader test.
      Inline-string/cell-type mismatch failures are covered as shape guardrails.
      Nested-cell failures are now covered as loader state-machine guardrails.
      Tombstone / style-preservation policy wording now keeps delete-vs-blank
      and source style handling explicit before any public editor API is added.
      Explicit default source style references are covered as default-style
      normalization to no style handle, not as preserved or migrated style
      metadata, and only unqualified `s` attributes whose value is exactly `0`
      (`s="0"`, `s='0'`, or `s = "0"`) are accepted; empty, valueless,
      unquoted, unterminated, padded, signed, leading-zero, entity-encoded, or
      duplicate default-like source style attributes stay fail-fast. Qualified
      style-like attributes such as `x:s` are rejected as unsupported cell
      metadata, not style tokens. The internal loader comment now matches the covered source
      materialization guardrails.
     Keep this internal and do not widen public API yet.
   - Next add only focused source materialization/state-hygiene or save-as
     handoff coverage before adding `WorkbookEditor::worksheet()` or
     `WorksheetEditor`.
   - Document memory growth, size limits, failure-before-state-change, and when
     callers should choose Streaming or Patch instead.

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
  `Workbook` for small new-workbook convenience creation, current narrow
  `WorkbookEditor` for existing-file Patch, and future `WorksheetEditor` /
  `WorkbookEditor` extensions for small-file random edit workflows.
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
  `Workbook`, current narrow `WorkbookEditor`, or future editor extensions.
- The docs explicitly say which current Patch classes are internal and which
  names are only future public design targets.

Do not claim:
- Full editor readiness, random cell editing API, or public `PackageEditor` from
  this design task or the narrow `WorkbookEditor` Patch slice. `CellValue`
  exists as a standalone value type, and `CellStore` exists as an internal
  foundation; neither means random editor readiness.
- That one unified facade can hide Streaming/Patch/In-memory performance costs.

Status update: the P4.0 design freeze has since been realized as a first public
Patch-mode slice. `WorkbookEditor` now exists as a public facade
(`include/fastxlsx/workbook_editor.hpp`, `src/workbook_editor.cpp`,
`tests/test_workbook_editor.cpp`, CTest family `fastxlsx.workbook_editor.*`) exposing
`open()`, `worksheet_names()`, `has_worksheet()`, `replace_sheet_data()`,
`rename_sheet()`, `save_as()`, and coarse diagnostics such as
`last_edit_error()`, `worksheet_catalog()`, and `pending_worksheet_edits()` over
the internal by-name `<sheetData>` Patch path plus narrow sheet-catalog rename.
Recent facade hardening also pins failed `rename_sheet()` state hygiene:
duplicate / invalid rename failures update `last_edit_error()` for the current
failure while preserving pending replacement diagnostics, catalog mapping, and
edit summaries. Failed `save_as()` now has the same public facade state hygiene:
source-overwrite, empty output path, missing-parent output, non-directory-parent
output, and existing-directory output rejection do not clear queued
replacement/rename state, do not change catalog/edit-summary diagnostics, and
do not overwrite or create `last_edit_error()`.
No-op `save_as()` is now covered as a reader-backed roundtrip copy with no
queued public edits: decompressed package entries remain equal to the source and
the facade does not create pending diagnostics. Follow-up coverage also verifies
that a no-op save preserves a prior failed `replace_sheet_data()` or
`rename_sheet()` `last_edit_error()` without creating pending state, and does
not close or commit the editor; callers can still queue a later replacement /
rename and save to another output path.
The `last_edit_error()` contract is also pinned across all current public
inspection / pending diagnostic methods: these calls do not clear, replace, or
create the last failed edit diagnostic.
Successful `save_as()` is now also covered as a reusable output operation, not a
commit: pending replacement / rename diagnostics, catalog summaries, and
`last_edit_error()` remain visible so the same planned state can be saved again.
Pending replacement names and worksheet edit summaries are also covered across
two renamed sheets: replacement names follow the current planned catalog order,
while edit summaries keep source workbook sheet-catalog order.
README / API-facing docs now mirror the same ordering and `last_edit_error()`
inspection-invariance contract, so the public example matches the tested facade
semantics without exposing internal `EditPlan` or package output details.
Styled replacement payloads are also fixed at the public
facade boundary: caller-supplied non-default `StyleId` values are serialized as
`s="N"` as-is, explicit default `StyleId{}` omits `s="0"`, and source
`xl/styles.xml` is byte-preserved rather than migrated or merged. Explicit
`CellValue::blank()` replacement cells write empty `<c/>` cells, while empty row
vectors remain missing rows rather than explicit blank rows. This is the narrow
whole-sheet-data / catalog-name slice only;
caller-supplied worksheet XML and sharedStrings/style migration remain future
design targets, and `PackageEditor` stays internal/test-only. `WorksheetEditor`,
`get_cell()` / `set_cell()`, and random cell editing now have a first public
small-file slice; further widening still needs targeted implementation and
tests.
The P8.320 wording gate kept README / API docs / task docs aligned with that
boundary before the public `WorksheetEditor` slice landed.
P8.321 adds one internal planned-catalog handoff regression for that path:
source-loaded `CellStore` data must be handed off by current planned sheet name
after a queued rename, and an old-name failure must preserve the queued rename
state. This remains internal evidence, not public `WorksheetEditor` support.
P8.322 extends the same rule to the full worksheet projection handoff, proving
the planned-name path still refreshes worksheet `<dimension>` while preserving
the same no-public-header boundary.
P8.323 tightens failure hygiene: old-source-name preflight failures must not
drain prepared sheetData or full-worksheet projection chunk sources before the
caller retries with the planned name.
P8.324 adds the follow-up planned-input transform case: source-loaded
`CellStore` data can patch `<sheetData>` after a queued whole-worksheet
replacement while preserving the queued worksheet wrapper and avoiding
source-only payload resurrection. This is still internal `PackageEditor` /
`CellStore` evidence, not public `WorksheetEditor` support.
P8.325 adds the paired full worksheet projection case: source-loaded
`CellStore` full worksheet chunks after a queued whole-worksheet replacement
replace the prior planned wrapper, stage `StreamRewrite`, refresh
`<dimension>`, and preserve calc cleanup / unknown bytes.
P8.326 adds the combined planned-name case: after queued rename plus queued
whole-worksheet replacement, source-loaded `CellStore` `<sheetData>` handoff
rejects the old source name before chunk consumption and succeeds by planned
name while preserving the queued wrapper.
P8.327 adds the paired full worksheet projection case for the same combined
setup: old source-name failure still drains no prepared chunks, and the planned
name success path replaces the queued wrapper, preserves the renamed workbook
catalog, stages `StreamRewrite`, refreshes `<dimension>`, and keeps calc cleanup
/ unknown bytes.
P8.328 adds save-as failure hygiene for that combined staged state:
source-overwrite rejection does not drop staged chunks, calc policy, notes, or
pending rewrite state, and a later safe output path still persists the workbook.
P8.329 extends the same save-as hygiene to path-equivalent source overwrite
attempts: a non-identical output path that resolves to the source package is
rejected without dropping staged chunks or pending rewrite state, and a later
safe output path still persists the workbook.
P8.330 adds empty-output-path save-as hygiene for the same combined staged
state: the guard rejects before dropping staged chunks or pending rewrite state,
and a later safe output path still persists the workbook.
P8.331 adds missing-parent output-path save-as hygiene for the same combined
staged state, again preserving staged chunks and pending rewrite state before a
safe retry.
P8.332 and P8.333 add the same save-as hygiene for non-directory output parents
and existing-directory output paths.
P8.334 adds writer/backend failure hygiene for the same combined staged state:
the failed writer path preserves staged chunks, existing output bytes, and
temporary-file cleanup, and a later safe output path still persists the workbook.
P8.335 adds successful-save persistence evidence: after one safe `save_as()`,
the same staged full worksheet chunks can be reused for a second safe output
path with the same workbook / worksheet semantics.
P8.336 adds source-copy temp-size failure hygiene for the same combined staged
state: changed save-time copy-original temp files preserve staged chunks,
existing output bytes, and temporary-file cleanup before a later safe retry.
P8.337 adds the matching missing source-copy temp-file failure hygiene, with the
same staged chunk, existing output, and temp cleanup guarantees.
P8.338 adds source-copy temp CRC failure hygiene for the same combined staged
state: same-size payload mutation is rejected without dropping staged chunks,
overwriting existing output bytes, or leaking temporary files.
P8.339 adds a workbook planned-removal operation-mixing guard: source-loaded full
worksheet chunks fail by-name catalog preflight before chunk consumption when
`/xl/workbook.xml` has already been explicitly removed.
P8.340 adds the paired invalid planned-catalog guard: source-loaded full
worksheet chunks fail by-name catalog preflight before chunk consumption when
the planned workbook sheet relationship id is missing from workbook `.rels`.
P8.341 adds the namespace-filter companion guard: source-loaded full worksheet
chunks fail by-name catalog preflight before chunk consumption when the planned
sheet id attribute is present only in the wrong XML namespace.
P8.342 adds the unqualified-id companion guard: source-loaded full worksheet
chunks fail by-name catalog preflight before chunk consumption when the planned
sheet id attribute is present only as a plain unqualified `id`.
P8.343 adds the unregistered-target companion guard: source-loaded full
worksheet chunks fail by-name catalog preflight before chunk consumption when
the planned workbook relationship resolves to a worksheet part absent from the
package manifest.

P8.344 refreshes the F2 public-header implementation gate: P8.321-P8.343 are
now treated as enough same-family internal catalog / operation-mixing evidence
for the next task to become a focused public `WorksheetEditor` implementation
plan and test list. More internal negative tests should be added only for a new
behavior gap. This still does not expose `WorksheetEditor`,
`WorksheetEditorOptions`, `worksheet()`, `try_worksheet()`, random cell editing,
relationship repair, sharedStrings/style migration, or in-place save.

P8.345 splits that first implementation task: start with private
`WorkbookEditor`-owned materialized worksheet state and internal materialization
/ mutation / save-as projection helpers, then add operation-mixing guards, and
only then expose `WorksheetEditorOptions`, borrowed `WorksheetEditor` handles,
`worksheet()`, and optional `try_worksheet()` with public tests. The required
tests are missing-vs-unsupported materialization failures, option matching,
move invalidation/reacquire behavior, operation-mixing rejection, set / blank /
erase projection, dimension refresh, output guard hygiene, and diagnostics
stage separation.

P8.346 adds the first private state holder for that sequence:
`detail::MaterializedWorksheetSession` owns one planned sheet name, one
`CellStore`, the materialization options snapshot exposed by the store, and a
dirty flag. Unit coverage proves successful set / existing erase mark dirty,
missing erase is a clean no-op, failed mutations preserve dirty state and sparse
records, and repeated materialization can compare options. This remains
internal-only and does not expose public random editing or save-as handoff.

P8.347 adds the session-level projection bridge: the private materialized
session can now create a full worksheet chunk source from its current
`CellStore`. Unit coverage verifies refreshed dimension, dirty set-cell payload
output, and erased-record omission. This is still only an internal save-as
handoff building block and does not wire public persistence.

P8.348 adds the private materialized session registry foundation:
`detail::MaterializedWorksheetSessionRegistry` owns multiple internal sessions
by planned sheet name, provides mutation-free materialization preflight,
reuses matching repeated materialization without replacing dirty state, rejects
mismatched options before registry mutation, and exposes dirty-session
bookkeeping for a later save-as projection task. This is still not public
`WorksheetEditor` support, not package loading, and not `WorkbookEditor::save_as()`
wiring.

P8.349 adds the registry-level dirty projection enumeration:
`dirty_worksheet_chunk_sources()` returns planned sheet names and full worksheet
chunk callbacks for dirty materialized sessions only. Unit coverage verifies
clean sessions are skipped, dirty projections follow planned-name registry
order, and each callback emits refreshed dimension plus sparse payload. This is
still not a package edit queue and not public random-edit persistence.

P8.350 adds the private operation-mixing preflight foundation:
`preflight_no_materialized_session(planned_name, operation_name)` rejects a
future whole-sheet operation once that planned sheet has an internal
materialized session, while allowing non-materialized sheets and preserving
registry / dirty state on failure. This helper is not yet wired into current
`WorkbookEditor` public methods and does not expose public operation-mixing
semantics.

P8.351 adds the package-backed one-sheet materialization handoff:
`materialize_from_workbook_sheet()` loads exactly one source worksheet through
the existing `load_cell_store_from_workbook_sheet()` path into a clean internal
session keyed by planned sheet name. Matching repeated materialization reuses
the existing session without re-reading the package, while mismatched options,
missing source sheets, and load guardrail failures leave registry state
unchanged. This is still internal-only and not public `WorksheetEditor`
persistence.

P8.352 wires that private registry into `WorkbookEditor::Impl` without opening
the public header. `fastxlsx.workbook_editor` test hooks now verify source-backed
private materialization, dirty session state, move construction / move
assignment transfer with `Impl`, and same-sheet whole-`<sheetData>`
operation-mixing rejection while another sheet remains editable. The public API
still has no `WorksheetEditor`, `WorksheetEditorOptions`, `worksheet()`,
`try_worksheet()`, public `get_cell()`, `set_cell()`, or `erase_cell()`;
dirty materialized sessions still do not persist through public `save_as()`.

P8.353 adds a test-hook-only dirty materialized-session flush smoke. The helper
explicitly projects dirty private materialized sessions through the existing
by-name full worksheet chunk-source Patch helper, then clears private dirty
state after successful staged handoff. Coverage proves clean materialization
flush stays a no-op source roundtrip, while dirty flush followed by current
`save_as()` writes the projected worksheet with refreshed sparse-store dimension
and preserves an untouched worksheet byte-for-byte. This is still not automatic
public `save_as()` persistence and not public `WorksheetEditor` support.

P8.354 wires the same private operation-mixing policy into
`WorkbookEditor::rename_sheet()`: once a planned sheet has been internally
materialized, same-sheet catalog rename is rejected before catalog mutation,
pending public edit mutation, or dirty-session changes. Coverage also proves a
different sheet can still be renamed and saved. This remains test-hook-only
internal evidence; no public materialized worksheet handle is exposed.

P8.355 adds repeated flush hygiene for the same internal handoff: a dirty
materialized session can flush, become clean, be modified again, and flush
again. The final saved worksheet contains the second projection, omits the stale
first projection, and keeps refreshed sparse-store dimension. This is still
internal evidence only and not public automatic materialized-session
persistence.

P8.356 adds failure hygiene for that same internal handoff. Dirty materialized
flush now preflights all dirty projection planned names before staging any
worksheet rewrite. Coverage proves a dirty valid session plus a dirty orphan
planned-name session fails without clearing dirty state, without incrementing
public pending-change diagnostics, and without partially staging the earlier
valid worksheet projection. This remains test-hook-only evidence, not public
`WorksheetEditor` persistence.

P8.357 adds the matching planned-catalog positive path. After public
`rename_sheet("Data", "RenamedData")`, a test-hook-only materialized session
keyed by `RenamedData` can flush through the by-name worksheet chunk-source
Patch helper. The saved workbook keeps the renamed catalog entry and writes the
projected worksheet part with refreshed dimension. This is still not semantic
sheet rename synchronization or public materialized editing.

P8.358 adds internal blank-vs-erase projection evidence. A test-hook-only
materialized erase hook verifies that erasing a missing cell keeps a clean
session clean, while setting source `A1` to explicit blank and erasing source
`A2` flushes as an empty `A1` cell, preserves source `B1`, removes row 2, and
refreshes dimension. This is still not public `set_cell()` / `erase_cell()`,
not tombstones, and not row deletion semantics.

P8.359 adds repeated-materialization hygiene. Calling the test-hook-only source
materialization twice for the same planned sheet now has WorkbookEditor-level
coverage proving the existing dirty private session is reused instead of
reloaded from source, so dirty `A1` survives the second materialization and
flushes to output. This is still only future handle-reacquire evidence.

P8.360 adds guarded materialized source-load failure hygiene. A test-hook-only
load rejected by the internal `CellStoreOptions` guard leaves the private
registry empty, dirty state clean, public pending diagnostics unchanged, and
`last_edit_error()` unset; the same editor can still perform a later valid
public `replace_sheet_data()` and `save_as()`. Current `WorkbookEditorOptions`
are still public replacement-payload guardrails, not future
`WorksheetEditorOptions`.

P8.361 adds the matching memory-budget failure hygiene. A test-hook-only source
load rejected by the internal `memory_budget_bytes` guard leaves the same clean
state and the editor remains usable for a later catalog-only rename/save. This
budget is still an internal CellStore estimate, not a public RSS guarantee.

P8.362 adds missing-source load failure hygiene. A test-hook-only source
materialization for an absent source sheet fails without registering an orphan
planned session, dirtying private state, changing public pending diagnostics, or
setting public `last_edit_error()`; the same editor can still perform a later
valid public replacement/save. This does not expose public `WorksheetEditor` or
turn arbitrary planned names into public random-edit targets.

P8.363 adds the reverse operation-mixing guard for queued public replacements:
after `replace_sheet_data()` has staged payload for a planned sheet, the
test-hook-only materialization path rejects materializing that same sheet instead
of reloading source bytes and making future dirty flushes ambiguous. The failure
does not create private state or public diagnostics, preserves the queued
replacement, and still allows materializing a different sheet.

P8.364 extends that guard across planned catalog rename. After
`replace_sheet_data("Data", ...)` followed by `rename_sheet("Data",
"QueuedData")`, pending replacement diagnostics migrate to `QueuedData`, and
test-hook-only materialization of `QueuedData` is rejected before private state
mutation. The queued replacement/rename still saves correctly, and a different
sheet can still be materialized cleanly.

P8.365 covers the opposite public operation order: `rename_sheet()` first, then
`replace_sheet_data()` against the renamed planned sheet. Test-hook-only
materialization of that renamed planned name is still rejected before private
state mutation, preserving the queued public replacement and keeping the
reject-first policy independent of public operation order.

P8.366 adds rejected-public-operation flush hygiene. After a dirty
test-hook-only materialized session rejects same-sheet `replace_sheet_data()`
and `rename_sheet()` before public state mutation, the dirty private session can
still be explicitly flushed through the internal materialized-session handoff.
The final output contains the materialized payload, keeps the original sheet
name, and does not leak the rejected replacement payload or rejected rename.

P8.367 adds the post-flush version of that guard. After an explicit
materialized-session flush has already staged a worksheet projection, later
same-sheet public `replace_sheet_data()` and `rename_sheet()` calls remain
reject-first because the private materialized session still exists. The staged
projection survives, the clean private session stays clean, and rejected
payload/name data does not leak into output.

P8.368 adds cross-sheet public edit hygiene after rejected same-sheet
operations. A successful `replace_sheet_data()` on a different sheet clears the
prior public `last_edit_error()`, preserves the dirty materialized session, and
can be followed by the explicit internal materialized flush so both sheet
changes save without leaking rejected same-sheet payload/name data.

P8.369 adds the catalog-only cross-sheet variant. A successful
`rename_sheet()` on a different sheet after rejected same-sheet materialized
operations clears the prior public error, preserves the dirty materialized
session, and can be followed by explicit flush so the other-sheet rename and
materialized payload both save.

P8.370 is the next API/documentation gate slice: keep the public header closed,
publish the current public/internal/future API status matrix, and use the
post-P8.369 `WorksheetEditor` preflight checklist before any public
random-cell editing symbols are added. This is review-only unless a follow-up
implementation task adds public symbols together with public tests.

P8.371 returns to implementation evidence behind that gate. A dirty
test-hook-only materialized session plus a queued cross-sheet public
replacement now survive `WorkbookEditor` move construction and move assignment,
can be explicitly flushed after assignment, and save the assigned source state
without leaking the discarded target editor's materialized session or queued
public replacement. This is still internal state-hygiene evidence, not public
`WorksheetEditor` handle lifetime support or automatic dirty-session
persistence.

P8.372 adds the matching save-as failure hygiene for private materialized
state. Failed `save_as()` before explicit flush preserves the dirty
materialized session and does not queue a projection; failed `save_as()` after
explicit flush preserves the staged projection and clean private session. A
later valid `save_as()` still writes the materialized payload. This remains
internal recovery evidence and does not add automatic public materialized
session persistence.

P8.373 covers retry mutation after a failed save. Once a materialized session
has been explicitly flushed and `save_as()` fails on an invalid output path, the
same private session can be mutated again, re-flushed, and saved; the later
projection replaces the earlier staged worksheet payload. This is still
test-hook-only recovery evidence, not public handle transaction semantics.

P8.374 combines the ownership-transfer and retry paths. A dirty private
materialized session plus a queued cross-sheet public replacement can move into
another `WorkbookEditor`, be explicitly flushed, survive a failed `save_as()`,
then be mutated and re-flushed again. The final output keeps the assigned
cross-sheet public edit, writes the later materialized projection, and does not
leak discarded target-editor state. This remains internal state-hygiene
evidence, not public `WorksheetEditor` move or transaction semantics.

P8.375 adds the successful-save reuse sibling. After an explicitly flushed
private materialized projection is saved successfully, the same editor/session
can be modified again, re-flushed, and saved to a second output path. The second
output uses the later projection while the first output artifact remains the
earlier saved package. This is internal reuse evidence, not public commit,
close, undo, or automatic flush semantics.

P8.376 combines ownership transfer with that successful-save reuse path. A
moved / move-assigned private materialized session plus assigned cross-sheet
public replacement can flush, save, mutate again, re-flush, and save a second
output. The second output carries the later materialized projection and the
assigned public replacement, the first output remains unchanged, and discarded
target-editor state does not leak. This remains internal lifecycle evidence,
not public `WorksheetEditor` move/reacquire or transaction semantics.

P8.377 adds the moved-from assignment cleanup negative for the same private
state family. Assigning from an already moved-from `WorkbookEditor` clears the
target editor's materialized sessions, dirty materialized state, queued public
edits, replacement diagnostics, and `last_edit_error()` instead of leaving stale
target state saveable. The prior moved-to holder remains valid and can still
flush/save its materialized payload. This is internal cleanup evidence, not
public materialized handle invalidation semantics.

P8.378 opens the first public `WorksheetEditor` slice. Public headers now expose
`WorksheetEditorOptions`, `WorkbookEditor::worksheet(name, options)`, and a
borrowed `WorksheetEditor` handle with `name()`, `try_cell()`, `set_cell()`,
`erase_cell()`, `cell_count()`, and `estimated_memory_usage()`. The mode is
explicit In-memory / existing-workbook small-file editing. `save_as()` now first
preflights output paths, then auto-flushes dirty materialized sessions into the
Patch plan before writing. Public tests cover source cell reads, set/erase
roundtrip through save-as, per-materialization max-cells guard failure hygiene,
same-sheet operation-mixing rejection, and cross-sheet Patch coexistence.

P8.379 extends that narrow public slice with
`WorkbookEditor::try_worksheet(name, options)` and `WorksheetEditor::get_cell()`.
`try_worksheet()` returns `std::nullopt` only for a missing current-planned sheet;
other materialization failures still throw and do not update `last_edit_error()`.
Missing `try_worksheet()` lookup also remains a pure optional path: it preserves
prior diagnostics, queues no pending edit or dirty materialized session, and
does not disturb later no-op `save_as()` copy-original output.
`get_cell()` throws on a missing sparse record so missing cells stay distinct
from explicit `CellValue::blank()` records. This still does not add non-default
style id support, sharedStrings/style migration, semantic metadata sync,
relationship repair, or large-file low-memory random editing.

P8.380 adds strict uppercase A1 cell-reference overloads for
`WorksheetEditor::try_cell()`, `get_cell()`, `set_cell()`, and `erase_cell()`.
They accept only single-cell references such as `A1` and `XFD1048576`, reuse the
row/column semantics, and reject lowercase references, ranges, zero or
leading-zero rows, zero columns, and out-of-limit coordinates. This is a
convenience API only; range iteration and broad metadata editing remain out of
scope.

P8.381 adds `WorksheetCellReference`, `WorksheetCellSnapshot`, and
`WorksheetEditor::sparse_cells()` as a read-only owning row-major snapshot of
the active materialized sparse records, including explicit blank cells. It does
not expose internal iterators, borrow CellStore lifetime, add range iteration,
mutate dirty state, update `last_edit_error()`, or synchronize worksheet
metadata.

P8.382 adds `WorksheetEditor::sparse_cells(CellRange)` as a filtered owning
row-major snapshot over active sparse records inside a 1-based inclusive range.
It reuses existing `CellRange` validation, does not synthesize missing cells as
blank snapshots, and does not mutate dirty state or update `last_edit_error()`.
This is not a dense range read, broader range iterator, streaming sparse
iterator, metadata recalculation, or large-file low-memory random access API.

P8.383 adds `WorksheetEditor::has_pending_changes()` as worksheet-local
dirty-state inspection for the borrowed materialized session. It reports whether
the session has sparse cell edits waiting for `WorkbookEditor::save_as()`
auto-flush, without flushing, incrementing `WorkbookEditor::pending_change_count()`,
exposing internal Patch state, or updating `last_edit_error()`.

P8.384 locks down borrowed handle lifetime after `WorkbookEditor` ownership
transfer. Existing `WorksheetEditor` handles fail on later session access after
owner move construction or move assignment; callers must reacquire from the
moved-to / assigned-to editor. Reacquired handles can continue editing the
transferred materialized session. This does not add detached worksheet ownership,
automatic handle retargeting, reference counting, or thread-safety guarantees.

P8.385 adds `WorkbookEditor::pending_materialized_worksheet_names()` as a
workbook-level dirty materialized-session diagnostic. It returns current planned
sheet names for dirty `WorksheetEditor` sessions in planned catalog order,
omits clean materialized sessions, clears after successful `save_as()`
auto-flush, and does not itself flush, increment `pending_change_count()`,
expose internal Patch state, include whole-`<sheetData>` replacements, or update
`last_edit_error()`.

P8.386 extends `WorkbookEditorWorksheetEditSummary` and
`WorkbookEditor::pending_worksheet_edits()` so the same source-order summary can
also report dirty materialized `WorksheetEditor` sessions. The new summary
fields are `materialized_dirty`, `materialized_cell_count`, and
`estimated_materialized_memory_usage`. Clean materialized sessions remain
omitted, failed `save_as()` preserves the dirty materialized summary, and
successful `save_as()` removes it after auto-flush unless the same worksheet
still has a queued rename or whole-`<sheetData>` replacement. This remains a
diagnostic surface only: it does not trigger flush, increment
`pending_change_count()`, expose `EditPlan`, or add sharedStrings/styles
migration, relationship repair, or large-file random editing.

P8.387 adds `WorkbookEditor::pending_materialized_cell_count()` and
`WorkbookEditor::estimated_pending_materialized_memory_usage()` as workbook-level
aggregate diagnostics over dirty materialized `WorksheetEditor` sessions. They
sum the dirty sessions' active sparse cell records and `CellStore` memory
estimates, omit clean materialized sessions and queued whole-`<sheetData>`
replacement payloads, preserve dirty aggregate state across failed `save_as()`,
and clear after successful `save_as()` auto-flush. They do not flush, increment
`pending_change_count()`, expose `EditPlan`, update `last_edit_error()`, or
change whole-sheet replacement diagnostics.

P8.388 pins `WorksheetEditor` borrowed-handle lifetime around
`WorkbookEditor::save_as()`: successful or failed `save_as()` does not delete or
invalidate an existing handle while the owning `WorkbookEditor` object is
unchanged. The same handle may continue reading, become dirty again after a
post-save mutation, and reflush on a later `save_as()`. Owner move construction
and move assignment remain the invalidation boundary that requires callers to
reacquire handles.

The detailed sections below keep their historical labels for traceability. Use
the authoritative execution order above for actual next-task selection.

### P4.1 - Patch MVP Use Case Freeze

Status: complete documentation gate.

The first Patch MVP is frozen as an internal by-name worksheet `<sheetData>`
patch. That user story now has the narrow public `WorkbookEditor` facade: open
an existing `.xlsx`, select an existing worksheet by sheet name, replace that
worksheet's `<sheetData>` from `CellValue` rows, optionally rename the sheet
catalog entry, and `save_as()` a new package. The underlying implementation
still delegates to internal `PackageEditor::replace_worksheet_sheet_data_by_name()`
and `rename_sheet_catalog_entry()`.

Accept when:
- `TASK_BREAKDOWN.md`, `TASK_PLAN.md`, and this file agree that the MVP is the
  internal by-name `<sheetData>` patch, not a generic metadata rewrite choice.
- The docs state that the helper is a bounded local rewrite and reuses existing
  calcChain remove / `fullCalcOnLoad`, relationship/content-type audit, and
  unknown/unmodified part preservation behavior.
- Non-goals are explicit: public `PackageEditor`, random cell editing,
  sharedStrings index migration, style id migration/style merge, relationship
  repair/pruning, table/drawing semantic sync, range repair, dimension
  recalculation, and large-file streaming worksheet transformation.

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

Status: minimal opt-in backend and Streaming compression-level configuration
landed. Continue with hardening before making minizip the default backend.

Do:
- Keep the verified ZIP/DEFLATE backend wired through
  `FASTXLSX_ENABLE_MINIZIP_NG=ON` and `MINIZIP::minizip-ng`.
- Keep `PackageWriterOptions::compression_level` internal to the package writer
  boundary and `WorkbookWriterOptions::zip_compression_level` as the public
  Streaming new-workbook option: `-1` means backend default, `0` requests
  no-compression/stored output, `1..9` selects zlib-compatible minizip DEFLATE
  levels, and stored bootstrap builds reject positive levels. Do not remap
  `-1` to level 1; callers that want throughput-first output should pass level 1
  explicitly.
- Keep the current no-Zip64 and file-backed chunk guardrails: reject empty
  entry lists, package entry counts above `65535`, entry names beyond the
  16-bit ZIP field, invalid entry names, duplicate entry names, missing or
  inaccessible file-backed chunks, and single entry uncompressed sizes above
  `UINT32_MAX` before opening the output path.
- Define real Zip64 and large-entry behavior before large-file promises.

Accept when:
- Tests pass for package entries without assuming stored/no-compression ZIP.
- Generated workbooks open in Excel without repair.
- Docs and API comments describe backend, compression-level, and memory behavior
  for touched public API.

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
- Current 2026-06-20 minizip/DEFLATE 1M-cell matrix is also recorded there:
  it proves compressed-output size is now comparable with Python writer outputs,
  but the CPU cost is explicit and `office_open` remains separate validation.
- Current 2026-06-20 OpenXLSX/xlnt adapter baseline is recorded there at
  100k cells. Treat it as workbook-API reference evidence, not a complete 1M
  C++ reference matrix, because OpenXLSX unique strings did not complete in the
  local waiting window.
- Current scale interpretation is also recorded there: 1M cells can expose
  hot-path viability, sharedStrings risk, DEFLATE cost and early reference
  library bottlenecks; it cannot prove 10M/50M linear scaling, close-time peak
  memory, Zip64, long-run resource stability, or full Office/WPS/LibreOffice
  compatibility. If C6 benchmark work resumes, run FastXLSX `10M -> 50M`
  first and keep OpenXLSX/xlnt behind timeout-based reference tiers.
- Current P11.7 FastXLSX scale ladder is recorded there too. Treat it as
  FastXLSX hot-path and compression-cost trend evidence: useful for deciding
  the next optimization target, but still not Zip64, 100M+ release readiness,
  or full Office-suite compatibility evidence.
- Current P11.8 compression-level sweep is recorded there too. Treat level 1
  as the current throughput-first recommendation, level 3/6 as size tradeoff
  candidates depending on string repetition, and level 9 as not recommended
  by current data. Keep this as opt-in benchmark evidence and keep default
  compression on backend default rather than level 1; this is not an
  Office-suite compatibility result.

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
- Current `tools/summarize_benchmark_results.py --self-test` covers summary-only
  collection from schema-v4 result / matrix report inputs, directory summary JSON
  skip, non-benchmark JSON warnings, conservative warning generation, Markdown
  rendering, and Markdown / JSON output writes. It does not invoke a benchmark
  executable, generate workbooks, or replace Office/openpyxl validation.

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
- Run `py tools/summarize_benchmark_results.py --self-test` when changing the
  benchmark summary helper. It only checks summary parsing/rendering/output
  guardrails and does not count as performance evidence. The helper now prefers
  `benchmark-matrix-report.json` over sibling raw case JSON when a directory is
  passed, so matrix directories are not double-counted.
- Keep benchmark dependencies behind planned/dev or opt-in configuration.
- Keep OpenXLSX/xlnt behind the `reference-benchmarks` vcpkg feature and
  `FASTXLSX_BUILD_REFERENCE_BENCHMARKS=ON`; they are not runtime dependencies.
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
  streaming row numbers, in-memory/streaming style id attributes,
  sharedStrings string-cell indexes, and streaming table XML integer attributes
  (`<tableParts count>`, table `id`, `<tableColumns count>`, and
  `<tableColumn id>`). Streaming `xl/styles.xml` unsigned integer attributes
  also use it for counts, custom `numFmtId`, and custom `xf` style ids; this is
  a local append helper. Worksheet suffix metadata counts/priorities
  (`mergeCells` count, conditional formatting priorities, and
  `dataValidations` count) also use it without changing metadata ordering or
  package side effects. Worksheet prefix metadata unsigned integer attributes
  for frozen-pane splits and column width min/max bounds also use it without
  changing freeze-pane or column-width semantics. Streaming `xl/workbook.xml`
  sheet catalog `sheetId` attributes also use it without changing sheet ordering,
  sheet names, relationship ids, or worksheet part paths. Streaming drawing XML
  geometry values for two-cell marker coordinates, marker EMU offsets, and
  intrinsic image EMU size also use it without changing anchor ranges,
  relationship ids, media/drawing paths, content types, or image insertion
  semantics. This is not benchmark
  evidence, sharedStrings strategy change, table/styles/metadata feature
  expansion, sheet catalog mutation, relationship rewrite, full styles or
  conditional formatting completion, row/column resize geometry work,
  full image support, existing drawing mutation, or broader date encoding.
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
- Document every style API as Streaming / new-workbook-only until broader
  existing-file style editing exists.

Accept when:
- CTest covers `xl/styles.xml`, style ids, worksheet `s="N"` references,
  custom `numFmtId`, XML attribute escape, wrap-text + limited horizontal/vertical
  `applyAlignment` / `<alignment .../>`, bold/italic/direct-color font records,
  `<font><color rgb="..."/>`, `fontId` reuse, `applyFont="1"`, solid fill records, `fillId` reuse, `applyFill="1"`,
  sharedStrings + styles relationship ordering,
  default `s="0"` omission, explicit `StyleId{}` clearing of a previously
  styled cell back to default output, styles workbook relationships coexisting
  with worksheet-local hyperlink/table relationships without shifting worksheet
  `rId` allocation, invalid foreign `StyleId` state hygiene, and invalid
  `add_style()` registration failure no-state-pollution for the workbook
  style registry, plus all-default optional alignment/font metadata ignored
  when combined with another effective style property, and styled formula cells
  preserving `s="N"` while still requesting workbook full recalculation without
  creating `xl/calcChain.xml`.
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
  are preserved while core/app metadata is regenerated. The internal
  `planned_output()` snapshot also exposes the generated core/app entries,
  preserved custom properties / unknown entries, metadata rewrites, and absence
  of removal / relationship-target audit pollution. It is not a public
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
- Local Python QA helper `tools/verify_hyperlinks.py` checks package XML,
  worksheet-owned `.rels`, owner-local `rId` behavior, external `target`,
  internal `location`, `display` / `tooltip`, no content type/style/calc
  side effects, and `openpyxl` hyperlink semantics for the external, internal,
  and display/tooltip sample workbooks.
- Local Excel COM helper `tools/verify_hyperlinks_excel.ps1` opens the same
  external, internal, and display/tooltip sample workbooks read-only and checks
  hyperlink counts, `Address`, `SubAddress`, `ScreenTip`, and unchanged cell
  text / `TextToDisplay`.
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
- Local unified table QA is now captured by `tools/verify_tables.py` and
  `tools/verify_tables_excel.ps1`. The Python helper checks package XML,
  content types, worksheet relationships, owner-local `rId`, totals metadata,
  style flags, column attribute escaping, same-worksheet overlap rejection, and
  `openpyxl` semantics, then optionally creates a `XlsxWriter` reference
  workbook under `build/qa/tables/`. The Excel COM helper read-only opens the
  totals, style flags, column escape, and overlap samples and verifies visible
  `ListObjects`, ranges, totals row, headers, style flags, and adjacent/cross
  worksheet table visibility.

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
   - Preservation baseline is now covered by public-facade regression: unrelated
     edits keep source media/drawing parts and relationships intact. The
     remaining work is real existing-workbook image editing, not more
     preservation-only evidence.

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

Current status:
- Basic CMake install/export packaging is in place for `FastXLSX::fastxlsx`.
  Installed packages include `FastXLSXConfig.cmake`,
  `FastXLSXConfigVersion.cmake`, exported targets, `fastxlsx.lib`, the
  top-level public headers, `LICENSE`, `THIRD_PARTY_NOTICES.md`, and
  `CHANGELOG.md`.
- Install rules intentionally install only top-level public headers under
  `include/fastxlsx`; internal `include/fastxlsx/detail` headers are not part of
  the installed release surface.
- Default install/export validation passed with
  `windows-nmake-release`, `ctest --preset windows-nmake-release
  --output-on-failure`, install to
  `build/qa/install-fastxlsx-release-docs-clean`, and a local consumer using
  `find_package(FastXLSX CONFIG REQUIRED)` / `FastXLSX::fastxlsx`.
- Opt-in minizip install/export validation passed with
  `windows-nmake-release-minizip`, `ctest --preset
  windows-nmake-release-minizip --output-on-failure`, install to
  `build/qa/install-fastxlsx-release-minizip`, and a local consumer using the
  installed package plus the resolved vcpkg dependency prefix. In that installed
  config, `FastXLSXConfig.cmake` correctly requires
  `find_dependency(minizip-ng CONFIG)`.
- `FASTXLSX_BUILD_EXAMPLES=ON` currently compiles the
  `fastxlsx_minimal_writer_example`, `fastxlsx_streaming_writer_example`, and
  `fastxlsx_workbook_editor_in_memory_example` targets against the public
  umbrella header.
- Release docs now include `CHANGELOG.md`, `THIRD_PARTY_NOTICES.md`,
  `docs/DEPENDENCIES.md`, and `docs/DEVELOPMENT_ENVIRONMENT.md`.

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
- Stable API or release readiness until remote CI, release artifact policy, and
  final staged-file review are complete.
- Native chart/VBA generation or editing, complete existing-workbook object
  lifecycle, relationship repair/pruning, or orphan cleanup from preservation
  and release-packaging evidence alone.

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
  DEFLATE entries. It accepts data-descriptor entries by using
  central-directory sizes/CRC as authoritative and still validating payload
  CRC while reading. It rejects encrypted entries, local header method/name
  mismatches, and local header CRC/size mismatches when no data descriptor is
  present, validates stored entry CRC before returning bytes, rejects
  conflicting content type defaults/overrides and
  duplicate relationship ids within one `.rels` owner, reject namespaced
  metadata attributes except namespace declarations, reject duplicate
  unqualified metadata attributes, reject non-whitespace metadata text,
  reject start/end tag QName mismatches, and ingest content types / relationships into internal
  `PartIndex` / `RelationshipGraph` views. It can also resolve the internal
  workbook sheet catalog by first validating package `_rels/.rels` contains
  exactly one internal `officeDocument` relationship; missing, duplicate,
  external, or URI-qualified targets fail during lookup, while fixed,
  root-level, and alternate internal workbook part names are resolved from
  the package root.
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
  `../drawings/vmlDrawing1.vml#shape1`, plus internal `planned_output()`
  visibility for the corresponding `xl/drawings/vmlDrawing1.vml`
  copy-original entry, URI-qualified legacyDrawing relationship metadata, and
  preserved legacy drawing caller-review note;
  worksheet-owned background picture and header/footer VML drawing preservation
  under the same `sheetData` Patch lane: the output keeps the `<picture>` /
  `<legacyDrawingHF>` references, worksheet `.rels` `image` / `vmlDrawing`
  relationships, `xl/media/background.png` bytes,
  `xl/drawings/vmlDrawingHF1.vml` bytes, the PNG content type default, and the
  VML content type override, and planned output exposes those parts as
  relationship-derived copy-original audit metadata. The internal
  `planned_output()` snapshot now also covers the boundary for this state:
  fullCalcOnLoad / `CalcChainAction::Remove`, worksheet and workbook
  `LocalDomRewrite`, content types / package relationships / workbook
  relationships / worksheet relationships copy-original entries, background
  picture / header-footer VML copy-original relationship metadata, preserved
  picture/VML caller-review notes, no relationship target audit, no worksheet
  relationship-id audit, no removed parts or package entries, and no invented
  `xl/calcChain.xml`; this is not image, VML, or header/footer semantic
  editing, calcChain rebuild, relationship repair/pruning, orphan cleanup,
  content type repair, public API, or complete object preservation;
  worksheet-owned printerSettings opaque part preservation under the same
  `sheetData` Patch lane: the output keeps the `<pageSetup r:id>` reference,
  worksheet `.rels` `printerSettings` relationship,
  `xl/printerSettings/printerSettings1.bin` bytes, and the printerSettings
  content type override, and planned output exposes that part as
  relationship-derived copy-original audit metadata. The internal
  `planned_output()` snapshot now also covers the boundary for this state:
  fullCalcOnLoad / `CalcChainAction::Remove`, worksheet and workbook
  `LocalDomRewrite`, content types / package relationships / workbook
  relationships / worksheet relationships copy-original entries,
  printerSettings copy-original relationship metadata, preserved pageSetup
  caller-review notes, no relationship target audit, no removed parts or
  package entries, and no invented `xl/calcChain.xml`; this is not printer
  settings semantic editing, calcChain rebuild, relationship repair/pruning,
  orphan cleanup, content type repair, public API, or complete object lifecycle
  support;
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
  audit metadata. The internal `planned_output()` snapshot now also covers the
  boundary for this state: fullCalcOnLoad / `CalcChainAction::Remove`,
  worksheet and workbook `LocalDomRewrite`, content types / package
  relationships / workbook relationships / worksheet relationships
  copy-original entries, OLE / control copy-original relationship metadata,
  preserved OLE/control caller-review notes, no relationship target audit, no
  worksheet relationship-id audit, no removed parts or package entries, and no
  invented `xl/calcChain.xml`; this is not OLE / ActiveX / control semantic
  editing, calcChain rebuild, relationship repair/pruning, orphan cleanup,
  content type repair, public API, or complete object preservation;
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
  docProps generated-small-XML additions including custom properties
  preservation output-plan visibility, worksheet calcChain omission with
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
  ordinary item replacement output-plan audit for active custom XML item
  local-DOM rewrite, owner `.rels` copy-original audit, preserved package
  relationships, content types, workbook, worksheet, properties part, unknown
  entry, empty removed audit, and no invented properties owner `.rels`, custom
  XML explicit item removal output-plan audit for omitted item and owner `.rels`,
  removed-part / removed owner `.rels` audit, package inbound customXml
  relationship metadata, preserved package relationships, content types,
  workbook, worksheet, properties part, unknown entry, and no invented
  properties owner `.rels`, custom XML properties
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
  Internal `planned_output()` coverage for this ordinary replacement state now
  exposes the active `xl/workbook.xml` `LocalDomRewrite` entry, source-owned
  workbook `.rels` copy-original audit, preserved content types / package
  relationships / worksheet / worksheet `.rels` / drawing / drawing `.rels` /
  chart / media / table / VML / percent-decoded drawing / sharedStrings /
  sharedStrings owner `.rels` / styles / VBA / calcChain / unknown extension
  entries, and empty `removed_parts` / `removed_package_entries`. This is Patch
  audit visibility only, not a public output planner, workbook deletion
  semantics, sheet catalog sync, relationship/content type repair, or public
  API.
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
  Internal `planned_output()` coverage for this ordinary replacement state now
  exposes the active `xl/drawings/drawing1.xml` `LocalDomRewrite` entry,
  source-owned drawing `.rels` copy-original audit, preserved content types /
  package relationships / workbook relationships / workbook / worksheet /
  worksheet `.rels` / chart / media / table / VML / percent-decoded drawing /
  sharedStrings / sharedStrings owner `.rels` / styles / VBA / calcChain /
  unknown extension entries, and empty `removed_parts` /
  `removed_package_entries`. This is Patch audit visibility only, not a public
  output planner, drawing mutation, image editing, chart editing, or relationship
  repair.
  Internal `planned_output()` coverage for ordinary unknown extension
  replacement over the same fixture now exposes the active
  `custom/opaque-extension.bin` `StreamRewrite` entry, source-owned unknown
  owner `.rels` copy-original audit, preserved content types / package
  relationships / workbook / workbook `.rels` / worksheet / worksheet `.rels` /
  drawing / drawing `.rels` / chart / media / table / VML / percent-decoded
  drawing / sharedStrings / sharedStrings owner `.rels` / styles / VBA /
  calcChain entries, and empty `removed_parts` / `removed_package_entries`.
  This is Patch audit visibility only, not a public output planner, unknown
  extension semantic editing, custom relationship repair, or public API.
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
  copy-original baseline. Internal output-plan coverage now also exposes the
  active media `StreamRewrite`, replacement reason, preserved content types /
  package relationships / workbook / workbook `.rels` / worksheet / worksheet
  `.rels` / drawing / drawing `.rels` / chart / table / VML /
  percent-decoded drawing / sharedStrings / sharedStrings owner `.rels` /
  styles / VBA / calcChain / unknown extension / unknown owner `.rels` entries,
  source-owned metadata context for drawing `.rels` and the unknown owner
  `.rels`, empty removal / relationship-target audits, and no invented media
  owner `.rels`. This is Patch audit visibility only, not a public output
  planner, image decoding, drawing mutation, or existing-workbook image editing.
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
  baseline. The internal output-plan snapshot now also exposes the active table
  `LocalDomRewrite`, content types / worksheet `.rels` copy-original decisions,
  preserved worksheet/drawing/media/unknown entries, empty removal /
  relationship-target audits, and no invented table owner `.rels`. This is not
  table resize, calculated columns, totals generation, or existing-workbook table
  editing.
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
  The internal `planned_output()` snapshot now also covers that ordinary
  replacement state: the active `xl/sharedStrings.xml` `StreamRewrite` entry,
  source-owned sharedStrings owner `.rels` copy-original audit, preserved
  content types / package relationships / workbook relationships / workbook /
  worksheet / styles / table / media / unknown extension entries, and empty
  `removed_parts` / `removed_package_entries`. This is audit visibility only,
  not a public output planner, sharedStrings migration, or metadata repair.
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
  The internal `planned_output()` snapshot now also covers that ordinary
  replacement state: the active `xl/styles.xml` `LocalDomRewrite` entry,
  preserved content types / package relationships / workbook relationships /
  workbook / worksheet / sharedStrings / sharedStrings owner `.rels` / table /
  media / VBA / calcChain / unknown extension entries, absence of invented
  `xl/_rels/styles.xml.rels`, and empty `removed_parts` /
  `removed_package_entries`. This is audit visibility only, not a public output
  planner, style id migration, style merge, or metadata repair.
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
  Internal `planned_output()` coverage for this ordinary replacement state now
  exposes the active `xl/charts/chart1.xml` `LocalDomRewrite` entry, preserved
  content types / package relationships / workbook relationships / workbook /
  worksheet / worksheet `.rels` / drawing / drawing `.rels` / media / table /
  sharedStrings / sharedStrings owner `.rels` / styles / VBA / calcChain /
  unknown extension entries, empty `removed_parts` / `removed_package_entries`,
  and no invented chart owner `.rels`. This is Patch audit visibility only, not
  a public output planner, chart reference repair, chart semantic merge, or
  metadata repair.
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
  Internal `planned_output()` coverage for this ordinary replacement state now
  exposes the active `xl/vbaProject.bin` `StreamRewrite` entry, preserved
  content types / package relationships / workbook relationships / workbook /
  worksheet / worksheet `.rels` / drawing / drawing `.rels` / chart / media /
  table / sharedStrings / sharedStrings owner `.rels` / styles / calcChain /
  unknown extension entries, empty `removed_parts` / `removed_package_entries`,
  and no invented `xl/_rels/vbaProject.bin.rels`. This is Patch audit
  visibility only, not a public output planner, macro editing API, relationship
  repair, content type repair, or signature preservation.
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
  Internal `planned_output()` coverage for this ordinary replacement state now
  exposes the active `xl/drawings/vmlDrawing1.vml` `LocalDomRewrite` entry,
  preserved content types / package relationships / workbook relationships /
  workbook / worksheet / worksheet `.rels` / drawing / drawing `.rels` / chart /
  media / table / percent-decoded drawing / sharedStrings / sharedStrings owner
  `.rels` / styles / VBA / calcChain / unknown extension entries, empty
  `removed_parts` / `removed_package_entries`, and no invented VML owner
  `.rels`. This is Patch audit visibility only, not a public output planner,
  VML shape editing, legacy drawing mutation, or relationship repair.
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
  Internal `planned_output()` coverage for this ordinary replacement state now
  exposes the active `xl/drawings/drawing space.xml` `LocalDomRewrite` entry,
  preserved content types / package relationships / workbook relationships /
  workbook / worksheet / worksheet `.rels` / drawing / drawing `.rels` / chart /
  media / table / VML / sharedStrings / sharedStrings owner `.rels` / styles /
  VBA / calcChain / unknown extension entries, empty `removed_parts` /
  `removed_package_entries`, and no invented percent-decoded drawing owner
  `.rels`. This is Patch audit visibility only, not a public output planner,
  percent-encoded target repair, relationship rewrite/repair, drawing mutation,
  or full drawing support.
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
  by-name cell replacement file-backed handoff preserves the same linked-object
  fixture at the internal Patch audit/preservation layer while refreshing
  dimension, cleaning calcChain metadata, re-opening output through
  `PackageReader`, and removing temporary XML files after editor destruction.
  This remains audit / preservation visibility, not relationship repair/pruning,
  object semantic editing, public API, complete PackageReader input streaming,
  or complete low-memory large-file editing.
  It also confirms
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
- Keep the current public Patch / In-memory slices aligned with code and
  tests before widening either API.
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

Status: the first public `WorksheetEditor` slice is already implemented, and
the current small-workbook `Workbook` convenience surface is in place for the
narrow creation path. Any later widening must be explicitly chosen.

Next tasks:
- Keep the current `Workbook` convenience surface aligned with code and tests;
  only widen it when there is a concrete need. Small-workbook sheet lookup,
  rename old-name lookup, and remove lookup now match the duplicate-name rule:
  ASCII case-insensitive, with stored sheet name casing preserved. Invalid,
  duplicate, and overlong sheet-name failures must remain failure-before-state-change
  guardrails so later valid small-workbook edits can proceed. Sheet removal is
  now pinned to subtract erased buffered cells from diagnostics and to avoid
  stale formula-only `<calcPr>` metadata.
- Document memory growth and size guardrails. This path now exposes
  `Workbook::cell_count()`, `Workbook::estimated_memory_usage()`,
  `Worksheet::cell_count()`, and `Worksheet::estimated_memory_usage()` as
  diagnostic helpers for the small in-memory creation path, but it is still not
  the large worksheet low-memory path; these estimates are not process RSS,
  hard budgets, save-time package assembly peaks, or large-export progress.
- Keep the current public `WorksheetEditor` slice aligned with code and tests
  before adding style migration, sharedStrings migration, or broader
  workbook-level guardrails.
- Share serialization and package semantics with Streaming/Patch where
  practical: styles, sharedStrings, relationships, document properties, and
  calc metadata.

Validation:
- Small workbook edit/save tests cover cell edits, sheet edits, workbook and
  worksheet count / memory diagnostics, invalid ranges, sharedStrings/styles
  side effects, and preservation when opening an existing package.
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
- CMake install/export passes for default and opt-in minizip presets.
- Installed-package consumers compile and link with
  `find_package(FastXLSX CONFIG REQUIRED)` and `FastXLSX::fastxlsx`; minizip
  consumers must also prove the installed config can resolve
  `find_dependency(minizip-ng CONFIG)`.
- GitHub Actions CI runner label and workflow behavior are verified or
  corrected, then CI passes.
- Key generated `.xlsx` files open in Excel.
- No documentation claims production ZIP, shared strings, complete Phase 3,
  existing-file editing, or complete Phase 5 complex object support before the
  corresponding code and tests exist.
