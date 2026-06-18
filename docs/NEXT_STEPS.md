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

Current execution order is `C0 -> C7`. Treat `P*` labels only as historical
indexes or capability slices. The current lane has now opened the first C4/F2
public `WorksheetEditor` slice under `WorkbookEditor`: small-file
existing-workbook random cell edits are explicit In-memory mode, and dirty
materialized worksheet sessions flush through `WorkbookEditor::save_as()`.
The next editor work should harden this first slice before adding style
migration, sharedStrings migration, broader workbook-level guardrails, or
large-file random editing. The current `WorksheetEditor` source loader can now
read source `t="s"` cells through the existing workbook `xl/sharedStrings.xml`
and materialize them as `CellValue::text(...)`; `save_as()` still writes the
materialized sparse store as inline strings and preserves the source
sharedStrings part instead of rebuilding, migrating, or writing it back. The
failure matrix is also pinned: duplicate or invalid sharedStrings
relationships/targets, missing or wrong-typed parts, malformed sharedStrings
XML, and invalid indexes fail fast instead of being repaired or guessed.
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
and formulas as `<f>` without cached values. This is not date/error cell import,
formula evaluation, cached-result generation, sharedStrings writeback, style
migration, wrapper metadata preservation, XML repair, or large-file random
editing.
P8.510 adds the matching public memory-budget guardrail evidence for source
materialization: `WorksheetEditorOptions::memory_budget_bytes` failures through
`try_worksheet()` expose the `CellStore` diagnostic, leave no partial
materialized session, pending cell/memory diagnostics, dirty state, or
`last_edit_error()`, and a later default-options materialization can still edit
and save. This remains a sparse-store estimate guardrail, not process RSS,
save-time package assembly accounting, or large-file random editing.
P8.511 extends that evidence to post-materialization mutations: an exact-budget
`WorksheetEditor` session rejects an oversized `set_cell()` insert with the
same `CellStore` diagnostic, updates `last_edit_error()`, preserves sparse and
pending dirty diagnostics, and still accepts a later in-budget overwrite that
saves normally. This is mutation-side sparse-store hygiene, not workbook-level
memory budgeting, save-time package peak accounting, or large-file random
editing.
P8.512 pins the symmetric post-materialization cell-count guardrail: an
exact-`max_cells` `WorksheetEditor` session rejects a new-cell `set_cell()`
insert, records the `CellStore max_cells` diagnostic, leaves sparse/pending
dirty state unchanged, and still accepts an overwrite of an existing cell. This
is not row/column insertion, dense range editing, workbook-level budgeting, or
large-file random editing.
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
date/error support, or metadata migration.
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
Malformed source sharedStrings XML/entity/attribute syntax is now pinned at the
same public facade boundary: unknown or unterminated entities, out-of-range
character references, missing or unquoted attribute values, and truncated tags
caused by unterminated attributes fail without dirtying materialized state or
blocking later valid Patch edits. This validates generic sharedStrings tag
attribute syntax but does not add XML repair, schema validation, attribute
whitelisting, sharedStrings writeback, or migration.
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
Malformed `xl/sharedStrings.xml` payloads are also covered by the same
on-demand behavior: valid non-`t="s"` sheets do not parse or repair the table,
while selected `t="s"` sheets still fail fast on the malformed sharedStrings
XML.
Wrong sharedStrings content type metadata is covered as well: valid non-`t="s"`
sheets do not validate or repair that content type, while selected `t="s"`
sheets still fail fast before materializing shared string indexes.
C5 direct PackageReader ZIP-entry chunk work remains the large-worksheet
low-memory line.
Public `try_worksheet()` / `worksheet()` facade failure hygiene is pinned for
representative invalid sharedStrings metadata as well: the editor remains clean,
`last_edit_error()` is unchanged, and later Patch edits can still be saved.
The public-header and planning-doc wording now matches that behavior: valid
workbook-backed `t="s"` source cells materialize as text, malformed
sharedStrings structures/targets or invalid indexes fail fast, non-critical
`count` / `uniqueCount` metadata does not drive materialization, and standalone
worksheet XML/chunk loaders still reject `t="s"` without workbook-level table
context.
Source style id public facade hygiene is also pinned: non-default source style
ids still throw through `try_worksheet()` / `worksheet()` without dirtying the
editor or blocking later Patch edits, while explicit default `s="0"` source
style attributes now materialize as no style handle and dirty projection omits
`s="0"` / `s='0'` / `s = "0"`. This remains default-style normalization only,
not style migration or merge. The normalization is exact-value only: empty,
valueless, unquoted, unterminated, padded, signed, leading-zero, entity-encoded,
or duplicate default-like source style attributes still fail instead of being
coerced to default style, and duplicate exact default-style attributes now have
public facade hygiene coverage. Qualified style-like attributes such as
`x:s="0"` also fail as unsupported cell metadata, not as default-style
normalization.
Caller-supplied explicit default `StyleId{0}` on
`WorksheetEditor::set_cell()` is normalized to no style handle: readback and
`sparse_cells()` snapshots do not expose a default style, and dirty save-as
projection omits `s="0"`. This remains default-style normalization only, not
non-default style migration or existing-workbook style registry support.
Caller-supplied non-default `StyleId` values on `WorksheetEditor::set_cell()`
are rejected before sparse-store mutation: the public diagnostic is updated,
the materialized session stays clean, no pending edit is queued, and a later
no-op `save_as()` remains copy-original. This is covered for both row/column
and strict A1 `set_cell()` overloads. This is still rejection-only hygiene, not
style migration, merge, or preservation.
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
Post-save worksheet summary diagnostics are pinned too:
`pending_worksheet_edits()` omits dirty-only materialized summaries after a
successful auto-flush, keeps them omitted after clean matching reacquire, adds
them back only when a later mutation dirties the reused session, and clears
them again after the next successful `save_as()`. Prior materialized handoffs
are not exposed as whole-`<sheetData>` replacement summaries.
The rename-context variant is pinned as well: if a sheet has a queued public
rename and a dirty materialized session, `save_as()` clears only the
materialized dirty fields while keeping the rename summary visible; clean
matching reacquire keeps it rename-only, and later mutation re-adds the
materialized dirty fields on that same source-order summary.
Rejected `save_as()` preflight keeps that combined state intact: source-overwrite
rejection does not flush the renamed dirty session, does not increment the
materialized handoff count, does not update `last_edit_error()`, and leaves
`pending_worksheet_edits()` reporting both `renamed` and `materialized_dirty`
until a later safe `save_as()` succeeds.
The lower-level materialized diagnostics now pin the same renamed lifecycle:
`pending_materialized_worksheet_names()` reports the current planned sheet name
while dirty, cell/memory aggregates match the borrowed session, successful
`save_as()` and clean reacquire clear those aggregates, and a later mutation
re-adds the planned dirty name until the next save.
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
source entries byte-for-byte, and `erase_cell("XFD1048576")` removes the
source-backed edge record so the next dirty projection shrinks to `A1:B2`
without dense allocation, source reload, tombstones, wrapper preservation, or
row metadata repair.
The same edge is now pinned for empty inline-string source shapes: `t="inlineStr"`
with an empty `<t></t>` materializes as empty text, `t="inlineStr"` with
`<is/>` and no text materializes as blank, no-op `save_as()` keeps copy-original
bytes, and erasing either `XFD1048576` record shrinks dirty projection to
`A1:B2`. This is not rich text preservation, inline/scalar coercion, XML repair,
source reload, or large-file random editing.
Workbook sharedStrings rich text is pinned at the same edge too: a source
`XFD1048576` `t="s"` record can point at simple rich `<r><t>...</t></r>` runs,
materialize as flattened plain text, ignore phonetic/extension metadata text,
keep source bytes on no-op `save_as()`, and erase back to `A1:B2` while
preserving `xl/sharedStrings.xml`. This is not rich text preservation,
sharedStrings rebuild/writeback/index migration, relationship repair, or
large-file random editing.
Unsupported source cell shape failure hygiene is pinned as well: source error
cells, date-like cells, and invalid boolean payloads fail through the public
facade without leaving partial materialized sessions or blocking later Patch
edits.
Malformed source worksheet XML now has the same public facade hygiene coverage:
missing closing worksheet root fails cleanly without partial materialized state.
The same malformed worksheet still blocks same-sheet Patch preflight, so
recovery is intentionally proven on an unrelated valid sheet rather than
described as XML repair.
Source cell-reference failure hygiene is now pinned at the public facade as
well: missing source cell `r` and row/cell reference mismatch fail cleanly
without partial sessions and do not prevent later valid Patch edits.
Source formula behavior is pinned too: formula cells load as
`CellValue::formula(...)`, stale cached scalar values are dropped by the
materialized save-as projection, and empty/duplicate/attribute-bearing or
non-numeric formula shapes fail cleanly without poisoning the editor. This is
formula text import only, not formula evaluation or calcChain rebuild.
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
outside `sheetData`, nested rows, cells outside row elements, and nested cells
fail cleanly without partial sessions. Recovery is proven through an unrelated
valid sheet, not through row inference, state-machine recovery, or same-sheet
Patch repair.
Supported source value materialization has positive coverage too: self-closing
source cells and inline-string cells without text become explicit blank records,
`t="b"` source cells become booleans, and empty inline text remains
`CellValue::text("")`. This documents the current supported-value floor without
implying date/error support, rich-text preservation, style/sharedStrings
migration, cached formula preservation, or metadata synchronization.
Empty source worksheet materialization is pinned as well: worksheets with no
`sheetData` and worksheets with self-closing `<sheetData/>` load as empty sparse
stores, stay clean until mutation, and later save through the standalone
CellStore worksheet projection. This does not imply XML repair, source wrapper
metadata preservation, same-sheet Patch bypass, or large-file random editing.
Worksheet root and `sheetData` boundary failure hygiene is pinned too: markup
before the worksheet root, duplicate `sheetData`, duplicate worksheet roots, and
trailing text fail cleanly without partial sessions. This is strict validation,
not XML repair, duplicate merge, tolerant root recovery, same-sheet Patch
bypass, or wrapper metadata preservation.
Source wrapper metadata projection behavior is pinned as well: worksheet-level
`sheetPr`, `dimension`, `sheetViews`, `sheetFormatPr`, `cols`, and `autoFilter`
beside supported cells do not block read-only public materialization, but a
later dirty `WorksheetEditor` save writes the standalone sparse CellStore
projection and drops those source wrapper elements. This is not wrapper
metadata preservation, synchronization, range recalculation, relationship
repair, or the internal sheetData Patch preservation path.
Representative relationship-bearing wrapper metadata follows the same public
dirty-projection boundary: source `<hyperlinks>` and `<tableParts>` do not
block supported cell materialization, dirty projection drops those worksheet
XML references, and the source worksheet `.rels` plus linked table part stay as
opaque preserved package artifacts. This is not hyperlink/table semantic
editing, relationship pruning/repair, table range repair, or the internal
sheetData Patch preservation path.
Representative range/reference wrapper metadata is now pinned on the same
path: source `<mergeCells>`, `<dataValidations>`, `<conditionalFormatting>`,
`<ignoredErrors>`, `<pageMargins>`, and `<pageSetup>` do not block supported
text/number/boolean materialization, but dirty `WorksheetEditor` save drops
them through the standalone sparse CellStore projection. This is not merged-cell
editing, validation/conditional-formatting import, page setup preservation,
range recalculation, metadata synchronization, or the internal sheetData Patch
preservation path.
Source comments and processing instructions outside cells now have the same
projection-boundary coverage: they do not block supported cell materialization,
but dirty `WorksheetEditor` save drops them through the standalone sparse
CellStore projection. This is not comment import, processing-instruction
preservation, comments-part editing, XML trivia preservation, relationship
repair, or a change to cell-internal comment / PI rejection.
Clean read-only materialized sessions are pinned as no-op save state too:
opening a `WorksheetEditor`, reading source shared string cells, and leaving the
sheet clean does not queue pending edits or dirty materialized names, and
`WorkbookEditor::save_as()` keeps the source package copy-original roundtrip
instead of flushing the standalone sparse worksheet projection. This is not
clean-session commit semantics, in-place save, transaction snapshot,
sharedStrings migration, or relationship repair.
Prefixed source sharedStrings are now pinned on the same read-only
materialization path: `sst` / `si` / `t` / `r` element names may be prefixed and
are matched by local-name for public `WorksheetEditor` materialization, no-op
copy-original save, dirty inline projection, and source sharedStrings byte
preservation. This is not namespace URI validation, namespace repair, schema
validation, sharedStrings migration/writeback, or rich text preservation.
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
  - `fastxlsx.package_editor.core`
  - `fastxlsx.package_editor.c5`
  - `fastxlsx.package_editor.preservation-core`
  - `fastxlsx.package_editor.preservation-removal`
  - `fastxlsx.package_editor.preservation-resources`
  - `fastxlsx.package_editor.preservation-comments`
  - `fastxlsx.package_editor.preservation-linked`
  - `fastxlsx.package_editor.cellstore-core`
  - `fastxlsx.package_editor.cellstore-chunks`
  - `fastxlsx.package_editor.cellstore-source`
  - `fastxlsx.package_editor.cellstore-failures`
  - `fastxlsx.package_editor.cellstore-catalog`
  - `fastxlsx.package_editor.sheetdata`
  - `fastxlsx.package_editor.sheetdata-catalog`
  - `fastxlsx.package_editor.sheetdata-guards`
  - `fastxlsx.package_editor.sheetdata-linked`
  - `fastxlsx.package_editor.policy`
  - `fastxlsx.workbook_editor.core`
  - `fastxlsx.workbook_editor.public`
  - `fastxlsx.workbook_editor.public-edge`
  - `fastxlsx.workbook_editor.source-success`
  - `fastxlsx.workbook_editor.source-failure`
  - `fastxlsx.workbook_editor.materialized`
  - `fastxlsx.workbook_editor.facade`
  - `fastxlsx.image`
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
  - `Workbook::set_document_properties()`
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
  - `WorksheetEditor::erase_cell()`
  - `WorksheetEditor` strict uppercase A1 cell overloads
  - `WorksheetCellReference`
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
    defined names, support Zip64/data descriptors, or expose a public editing API.
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
     guardrail regressions rejecting non-default source style ids, unsupported
      cell types, and invalid boolean payloads. Explicit source `s` values are
      now normalized to no style handle only when the unqualified value is
      exactly `0` (`s="0"`, `s='0'`, or `s = "0"`); empty, valueless,
      unquoted, unterminated, padded, signed, leading-zero, entity-encoded, or
      duplicate default-like source style attributes still fail, with duplicate
      exact default-style attributes covered through the public facade hygiene
      path. Qualified style-like attributes such as `x:s="0"` stay unsupported
      metadata. Workbook-backed source sharedStrings
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
       Error cells (`t="e"`) and date-like cells (`t="d"`) are now pinned as
       unsupported cell type failures, not loaded values.
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
      processing instructions / CDATA, nested cell input rejected at the
      event-reader boundary, and cells outside rows; these remain
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
      state-hygiene coverage for error cells, date-like cells, inline payloads
      in non-inline cells, and ordinary values in inline-string cells.
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
      Source formula attributes and empty formula text now also fail before
      materialization, keeping the loader limited to plain formula text rather
      than shared/metadata formula migration.
      Cells outside row elements now also fail before materialization, keeping
      source-backed loading scoped to row-contained cells.
      Unsupported source row/cell metadata attributes now also fail before
      materialization instead of being silently dropped from the sparse
      `CellStore`.
      Unsupported scalar `<v>` and inline text `<t>` value-wrapper attributes
      now also fail before materialization; inline text `xml:space` remains
      loadable as plain semantic text only.
      Unsupported inline-string rich text runs and phonetic metadata now also
      fail before materialization instead of being flattened into plain text.
      Direct raw cell text outside value wrappers, cell-contained comments,
      processing instructions, and unsupported markup such as CDATA now also
      fail before materialization instead of being silently dropped from inline
      text or other source cell payloads.
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
      is the explicit blank replacement cell, and non-default source style ids
      still fail instead of being preserved, migrated, or merged. Explicit
      default source style references are normalized to no style handle, but
      only for unqualified `s` values exactly equal to `0` (`s="0"`, `s='0'`,
      or `s = "0"`); empty, valueless, unquoted, unterminated, padded, signed,
      leading-zero, entity-encoded, duplicate, or qualified default-like source
      style attributes remain load failures. The internal loader declaration now
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
      P8.311 resolves the first diagnostics decision: future worksheet
      materialization failures should throw `FastXlsxError` and must not update
      current edit-only `last_edit_error()`; `try_worksheet()`, if added, may
      return empty only for missing sheet names.
      P8.312 resolves the options naming and call-site decision:
      `WorksheetEditorOptions` is the future per-materialization options type
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
     and FastXLSX skills with the editable-engine positioning.
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
      guardrail coverage now rejects non-default source worksheet style ids,
      standalone shared string indexes, unsupported cell types, and invalid
      boolean payloads before pretending migration, repair, or preservation
      exists; explicit source `s="0"` is normalized to no style handle. These
      failure paths
      do not expose a partial `CellStore`, and `CellStore` coordinate validation
      failures now also have no-state-pollution coverage.
      Error cells (`t="e"`) and date-like cells (`t="d"`) remain explicit
      unsupported cell type failures.
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
      Source formula attributes and empty formula text now fail as formula-shape
      guardrails.
      Cells outside row elements now fail as row-scope guardrails.
      Unsupported source row/cell metadata attributes now fail as metadata
      guardrails.
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
`WorksheetEditor`, `get_cell()` / `set_cell()`, random
cell editing, caller-supplied worksheet XML, and sharedStrings/style migration
remain future design targets, and `PackageEditor` stays internal/test-only.
The P8.320 wording gate keeps README / API docs / task docs aligned with that
boundary: no public `WorksheetEditor` symbols should be added until the next
task supplies implementation and tests for materialization failure hygiene,
handle lifetime, option matching, operation-mixing rejection, refreshed
dimension output, and `save_as()` persistence.
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
  guardrails and does not count as performance evidence.
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
