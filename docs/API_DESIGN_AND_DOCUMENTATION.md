# API 设计与文档注释

## 目标

FastXLSX 的 API 可以追求易用，但不能为了易用性牺牲大文件性能主线，也不能把
编辑能力降级为 streaming writer 的附属补丁。

API 设计必须和项目定位对齐：FastXLSX 是可编辑的高性能 XLSX/OpenXML 引擎，而不是
单纯 high-performance writer。

- 大数据写入优先暴露 row iterator / chunk writer。
- 大型 worksheet 不能被高层 API 迫使进入 DOM 或完整 cell matrix。
- 便利 API 必须明确适用范围；如果只适合小文件或 in-memory 模式，要在文档注释里写明。
- 性能热路径不能因为 API 包装而落到通用 XML serializer 或全量对象模型上。
- 已有文件编辑 API 必须说明 part-level rewrite、未知 part 保留、联动 part 更新和
  公式重算策略。

## API 统一原则

FastXLSX 的 public API 必须看起来属于同一个库。分层是为了性能和编辑语义，不是为了
把内部模块名逐层暴露给用户。

统一 API 的目标不是把 Streaming、Patch 和 In-memory 全部塞进一个巨大 `Workbook`
类，而是统一概念、命名、值类型、错误模型和文档口径：

- 大文件新建导出使用 `WorkbookWriter` / `WorkbookWriterOptions` / `StringStrategy` / `WorksheetWriter` / `CellView`。
- 小文件新建和简单生成继续使用 `Workbook` / `Worksheet` / `Cell` / `RowOptions`。
- 已有文件编辑当前已有窄 public `WorkbookEditor` Patch facade；后续更宽编辑能力仍应
  优先扩展 `WorkbookEditor` / `WorksheetEditor` 这类 workbook / worksheet 概念门面，
  而不是让普通用户直接面对 OPC package 概念。
- 当前内部 `PackageReader` / `PackageEditor` / `EditPlan` /
  `DependencyAnalyzer` / `RelationshipGraph` 先作为 Patch 底座和审计模型保留在
  internal/detail 边界；不要因为内部 Patch MVP 已经可用，就急着把这些类型作为
  稳定 public API 发布。

推荐的 public API 形状：

```cpp
// Streaming: 大文件新建导出，按行顺序写入。
auto writer = WorkbookWriter::create(path);
auto sheet = writer.add_worksheet("Data");
sheet.append_row({CellView::text("A")});
writer.close();

// Small new workbook: 小文件便捷创建，可在 save() 前持有较小状态。
auto wb = Workbook::create();
auto& small_sheet = wb.add_worksheet("Data");
small_sheet.append_row({Cell::text("A")});
wb.save(path);

// Existing-file editing (已落地 Patch 切片): 打开已有 workbook，整体替换某个
// worksheet 的 <sheetData>，定点替换已存在 cells，或用 enum policy 定点 upsert
// 缺失 cells/rows，
// 再输出到新路径。
auto editor = WorkbookEditor::open(input_path);
editor.replace_sheet_data("Data", {
    {CellValue::text("A"), CellValue::number(1.0)},
});
editor.replace_cells("Data", {
    {{1, 1}, CellValue::text("patched")},
});
editor.replace_cells("Data",
    {
        {{500001, 1}, CellValue::number(123.0)},
    },
    CellPatchMissingCellPolicy::Insert);
editor.save_as(output_path);

// Future random editing (尚未实现的设计草案): 小文件随机读写单元格。
auto editable_sheet = editor.worksheet("Data");      // future
editable_sheet.set_cell("A1", CellValue::text("A")); // future
```

统一命名规则：

- 创建 sheet 使用 `add_worksheet(name)`。
- 查找已有 sheet 使用 `worksheet(name)` 或 `try_worksheet(name)`，不要混用
  `get_sheet()`、`sheet_by_name()`、`select_sheet()` 等同义词。
- 追加行使用 `append_row(...)`；随机写单元格使用 `set_cell(...)`，只能出现在
  In-memory / future editor 语义里。
- 保存新文件使用 `save(path)` 或 writer 的 `close()`；已有文件编辑输出使用
  `save_as(path)`，避免暗示当前支持原地 atomic 覆盖。
- 能用 workbook / worksheet / cell / range 语言表达的 public API，不应要求用户先理解
  part、relationship owner、content type override 或 package entry。

统一值类型规则：

- `CellRange` 是跨 Streaming metadata、图片 anchor、未来 Patch / In-memory range
  操作都应复用的 public range 值。
- `StyleId`、`CellStyle`、`DocumentProperties`、`HyperlinkOptions`、
  `ImageOptions`、`ImageAnchorOffset` 等应保持 workbook / worksheet 语义，不要泄漏底层 part 名。
- `CellValue` 是当前已落地的 owning 单元格语义值：number、text、boolean、formula、
  blank 以及可选 style reference。它可以被 `Cell`、当前
  `WorkbookEditor::replace_sheet_data()` rows 输入、`WorkbookEditor::replace_cells()`
  targeted-cell 输入、`WorksheetEditor::set_cell()` / random-edit 扩展和 In-memory
  editor 共同使用。当前已有 internal `CellStore`
  首个稀疏存储切片、internal guardrail 首片和 standalone `<sheetData>` emission
  首片，但这仍不表示 `WorksheetEditor`、random cell editing 或完整 save-as handoff
  已经实现。
- `Cell` 可以继续作为小文件新建路径的 owning convenience value 或 `CellValue` 的
  轻量包装，但不要把它定义成所有模式的内部存储单元。
- `CellView` 必须保持 Streaming-only 的非 owning view：它可以引用调用方短生命周期
  数据，只在 `append_row()` 调用期间被消费，不应进入 Patch / In-memory 的长期状态。

简化后的 cell 边界：

```text
CellView  -> Streaming 输入视图，非 owning，热路径轻量
Cell      -> 小文件创建便利值，owning，不代表大型 worksheet 内部存储
CellValue -> 已落地 owning 语义值，适合作为 editor / in-memory / API 边界
CellRecord / CellStore -> 已有 internal sparse-store / sheetData emission 首片，不作为 public API 暴露
```

Public API 概念矩阵：

```text
概念            Streaming              Small new workbook       Current Patch / Future editor
创建入口        WorkbookWriter          Workbook                 WorkbookEditor
sheet 入口      add_worksheet           add_worksheet / remove_worksheet / inspection helpers
                                                               worksheet_names / has_worksheet / worksheet / try_worksheet
追加行          append_row(CellView)    append_row(Cell)         replace_sheet_data(rows)；future append_row
随机写 cell     不支持                  不作为大文件路径承诺      replace_cells(existing cells or CellPatchMissingCellPolicy::Insert point upsert) / WorksheetEditor::set_cell
读取 cell       不支持                  可作为小文件能力规划      WorksheetEditor::get_cell / try_cell
保存            close                   save                     save_as
范围值          CellRange               CellRange                CellRange
样式句柄        StyleId                 StyleId                  StyleId
错误            FastXlsxError           FastXlsxError            FastXlsxError
```

该矩阵是命名和职责约束。`CellValue` 已有独立 public value header、实现和测试；
`CellStore` / `CellRecord` 已有 internal detail 首片，且已有 internal `<sheetData>`
payload emission helper。`WorkbookEditor` 现已落地首个 public Patch 切片
（`include/fastxlsx/workbook_editor.hpp`、`src/workbook_editor.cpp`、
`tests/test_workbook_editor.cpp`，CTest family `fastxlsx.workbook_editor.*`）：已覆盖
`open()`、`worksheet_names()` / `has_worksheet()` sheet inspection、按 sheet name
做整表 `<sheetData>` 替换的 `replace_sheet_data(rows)`、窄 sheet-catalog 改名的
`rename_sheet(old_name, new_name)`、targeted Patch 的 `replace_cells()` /
`CellPatchMissingCellPolicy::{Fail,Insert}`、`save_as()`，以及首个 small-file
In-memory `WorksheetEditor` 切片：`WorksheetEditorOptions`、
`WorkbookEditor::worksheet(name, options)`、
`WorkbookEditor::try_worksheet(name, options)`、
`WorksheetEditor::try_cell()` / `get_cell()` / `set_cell()` / `set_cells()` /
`set_cells(initializer_list<WorksheetCellUpdate>)` /
`append_row()` / `append_row(initializer_list<CellValue>)` /
`set_row()` / `set_row(initializer_list<CellValue>)` /
`set_column()` / `set_column(initializer_list<CellValue>)` /
`erase_row()` / `erase_rows()` / `erase_column()` / `erase_columns()` /
`insert_rows()` / `delete_rows()` / `insert_columns()` / `delete_columns()` /
`set_cell_value()` / `set_cell_values()` /
`set_row_values()` / `set_row_values(initializer_list<CellValue>)` /
`set_column_values()` / `set_column_values(initializer_list<CellValue>)` /
`clear_cell_value()` /
`clear_row()` / `clear_rows()` / `clear_column()` / `clear_columns()` /
`clear_cell_values()` /
`set_cell_values(initializer_list<WorksheetCellUpdate>)` /
`clear_cell_values(CellRange)` / `clear_cell_values(std::string_view)` /
`clear_cell_values(span<WorksheetCellReference>)` /
`clear_cell_values(initializer_list<WorksheetCellReference>)` /
`erase_cell()` / `erase_cells()` / `erase_cells(CellRange)` /
`erase_cells(std::string_view)` /
`erase_cells(span<WorksheetCellReference>)` /
`erase_cells(initializer_list<WorksheetCellReference>)`、
`has_pending_changes()`、`sparse_cells()`、`sparse_cells(CellRange)`、
`sparse_cells(std::string_view)` strict uppercase A1 range overload、
`sparse_cells(span<WorksheetCellReference>)` /
`sparse_cells(initializer_list<WorksheetCellReference>)` explicit sparse
coordinate batch overloads、
`contains_cell()`、`used_range()`、`row_cells()`、`column_cells()`、`cell_count()` 和
`estimated_memory_usage()`。Small new-workbook
`Workbook::rename_worksheet()` / `Workbook::remove_worksheet()` 也已落地，只修改
当前待生成 workbook 中的 in-memory sheet buffer，并不编辑已有 XLSX；`Workbook` /
`Worksheet` 上的 `cell_count()` 和 `estimated_memory_usage()` 只是该 buffered creation
path 的诊断近似值，不是进程 RSS、硬预算、save-time package assembly peak 或
large-export progress API；small `Workbook` 的 sheet lookup、rename old-name lookup
和 remove lookup 与 duplicate-name rule 一致，都是 ASCII case-insensitive。尚未落地的
更宽能力 —— existing-file 添加 / 删除
worksheet、semantic sheet rename、sharedStrings / styles 迁移、relationship repair 和
大 worksheet 低内存 random edit —— 仍必须标明为未来 public design target。

### 当前 public / internal / future API 状态矩阵

这张矩阵是当前 API 推进的发布门。新增功能前先把目标放进其中一类，
避免把 internal evidence 误写成 public capability。

| Area | 当前状态 | 已落地 public 入口 | 暂不宣称 / 未实现 |
| --- | --- | --- | --- |
| New workbook streaming | 已公开，继续低风险打磨 | `WorkbookWriter`, `WorksheetWriter`, `CellView`, styles, validations, hyperlinks, tables, conditional formatting, images | 不支持随机回写历史行；不把 convenience API 放进 row hot path |
| Small new workbook | 已公开，适合小文件创建 | `Workbook`, `Worksheet`, `Cell`, `Workbook::add_worksheet()`, `Workbook::save()`, `worksheet_count()`, `worksheet_names()`, `has_worksheet()`, `worksheet()`, `try_worksheet()`, `rename_worksheet()`, `remove_worksheet()`, `Workbook::cell_count()`, `Workbook::estimated_memory_usage()`, `Worksheet::cell_count()`, `Worksheet::estimated_memory_usage()` | 不承诺大文件低内存；不作为 existing-file editor；lookup / rename old-name / remove name 和 duplicate-name rule 均为 ASCII case-insensitive；`rename_worksheet()` / `remove_worksheet()` 只修改待生成 workbook 中的 buffered sheet，不是 existing-file sheet edit/delete；size diagnostics 是近似观测，不是 RSS、硬预算或 large-export progress |
| Existing workbook Patch facade | 已公开窄切片 | `WorkbookEditor::open()`, source/planned catalog inspection, `CellPatchMissingCellPolicy`, `replace_sheet_data()`, `replace_cells()`, `replace_image()`, `rename_sheet()`, `request_full_calculation()`, `save_as()`, coarse diagnostics | 不公开 `PackageEditor` / `EditPlan`；`replace_cells()` 默认只替换已存在 cells，显式 `CellPatchMissingCellPolicy::Insert` 只做 point upsert，能插入缺失 cells / 合成 minimal rows，但不 shift 行列、不 resize tables/filters/drawings/defined names；`request_full_calculation()` 只提供 workbook fullCalcOnLoad / stale calcChain cleanup，不做 relationship repair、sharedStrings/style migration、drawing/image semantic editing |
| Existing workbook In-memory worksheet editor | 已公开首个 small-file 切片 | `WorksheetEditorOptions`, source-load and mutation `max_cells` / `memory_budget_bytes` guardrails plus max-cells and memory-budget mutation failure/recovery hygiene, missing-erase diagnostic cleanup, explicit blank insertion guardrail coverage, mutation diagnostic replacement/clear ordering, and mixed public edit diagnostic replacement/clear ordering, `WorkbookEditor::worksheet()`, `WorkbookEditor::try_worksheet()`, `WorkbookEditor::formula_reference_audits()`, `WorksheetEditor::name()`, `try_cell()`, `get_cell()`, `set_cell()`, `set_cells()`, `append_row()`, `set_row()`, `set_column()`, `erase_row()`, `erase_rows()`, `erase_column()`, `erase_columns()`, `insert_rows()`, `delete_rows()`, `insert_columns()`, `delete_columns()`, `set_cell_value()`, `set_cell_values()`, `set_row_values()`, `set_column_values()`, `clear_cell_value()`, `clear_cell_values()`, `clear_row()`, `clear_rows()`, `clear_column()`, `clear_columns()`, `clear_cell_values(CellRange)`, `clear_cell_values(span<WorksheetCellReference>)`, `erase_cell()`, `erase_cells()`, `erase_cells(span<WorksheetCellReference>)`, strict row/column coordinate guardrails, strict uppercase A1 overloads, default `StyleId{0}` normalization to no style handle, row/column / A1 / sparse batch caller non-default `StyleId` rejection no-state-pollution, sparse batch full-cell replacement with preflighted duplicate-coordinate later-wins semantics, sparse append-row next-represented-row semantics with empty-input no-op and staged width / row-limit / guardrail failures, sparse set-row represented-row replacement/clear semantics with missing-row no-op and staged width / style / guardrail failures, sparse set-column represented-column replacement/clear semantics with missing-column no-op and staged height / style / guardrail failures, represented sparse row/column insert/delete coordinate shifts with moved formula-cell relative-reference translation and no broader metadata sync, sparse row / row-range / column / column-range / whole-store value clear with source-style preservation, sparse row / row-range / column / column-range / whole-store erase with missing-only no-op semantics, sparse batch value-only replacement with source-style preservation, explicit blank clears through single-cell / whole-store / row / column / range / coordinate-batch clear APIs, sparse coordinate batch erase with missing-only no-op semantics, source explicit `s="0"` / `s='0'` normalization to no style handle, workbook-backed canonical non-zero source style id `cellXfs` validation and numeric passthrough, style-preserving value-only updates and explicit blank clears through `set_cell_value()` / `set_cell_values()` / `set_row_values()` / `set_column_values()` / `clear_cell_value()` / `clear_cell_values()` / `clear_row()` / `clear_rows()` / `clear_column()` / `clear_columns()` / `clear_cell_values(CellRange)` / `clear_cell_values(span<WorksheetCellReference>)`, source `t="str"` scalar-string and formula-string materialization, source `t="e"` opaque error-token materialization, `WorksheetCellReference`, `WorksheetCellUpdate`, `WorksheetCellSnapshot`, `WorkbookEditorFormulaReferenceAudit`, `has_pending_changes()`, `sparse_cells()`, `sparse_cells(CellRange)`, `row_cells()`, `column_cells()`, sparse row/column owning snapshots without missing-cell synthesis, pre-/post-save matching-option `worksheet()` / `try_worksheet()` session reacquire reuse, post-save materialized summary and aggregate diagnostic lifecycle, renamed-sheet planned-name materialized diagnostics, materialized formula sheet-reference audit for rename risk without formula rewrite, guarded source sharedStrings materialization plus narrow append projection/reuse and absent-table optional-dependency, lazy selected-sheet dependency, and non-critical metadata boundaries, relationship-target, XML/entity/attribute, custom source cell-type, and direct raw cell/worksheet/sheetData/row-text fail-fast hygiene, source inline/sharedStrings rich-text flattening, prefixed source worksheet/inlineStr local-name materialization, namespace-URI non-validation coverage for supported local-names, wrong-namespace unsupported local-name failure hygiene including sharedStrings item/rich-run local-names, malformed source sharedStrings and inline rich metadata failure hygiene, source wrapper metadata dirty-projection boundary, cell-external comment/PI dirty-projection boundary, clean read-only materialized no-op save copy-original boundary, failed-materialization no-op save copy-original boundary, missing `try_worksheet()` no-op save copy-original boundary, `cell_count()`, `estimated_memory_usage()` | 不支持 caller-supplied 非默认 style id 写入、style migration / merge、sharedStrings broad rebuild / migration、sharedStrings XML repair / schema count repair、sharedStrings relationship repair/pruning、external sharedStrings target materialization、date/custom cell materialization、Excel error-token validation、formula evaluation or formula rewrite、namespace URI validation/repair、unsupported local-name import/tolerance、rich-text preservation、malformed rich metadata repair、source wrapper metadata preservation / sync、comment import / XML trivia preservation、tombstone clear API、完整 Excel row/column structural edits、row/column metadata creation、coordinate inference / clamping、semantic metadata sync、formula/table/range/drawing/chart/VBA/relationship synchronization、clean-session commit semantics、transaction history、dense range writes beyond active sparse-record clear and sparse batch replacement/clear APIs, dense range reads、dense row/column reads、streaming sparse iterators 或 large-file low-memory random editing；`memory_budget_bytes` 是 sparse-store estimate guardrail，不是 process RSS 或 save-time package assembly peak |
| Materialized worksheet foundation | internal + public handoff 底座 | `CellStore`, materialized-session registry, chunked projection, `WorkbookEditor::save_as()` dirty-session auto-flush | internal test hooks 仍不是 public API；不公开 source chunk lifetimes、EditPlan 或 PackageEditor |
| Existing-file semantic objects | preserve/audit/fail 为主 | internal preservation and audit coverage for drawings, media, tables, comments, VBA, custom XML, pivot/external links, unknown entries | 不做语义编辑、range/table/chart sync、orphan cleanup、relationship pruning/repair |
| Formula / calculation | 写入和重算请求基础 | formula cell XML, `fullCalcOnLoad`, calcChain cleanup policy in Patch paths；能力矩阵见 `docs/FORMULA_SUPPORT.md` | 不计算公式、不写 cached values、不 rebuild `calcChain.xml` |
| Large worksheet rewrite | internal foundation | event reader, transformer action model, chunk-source PackageEditor handoff | 没有 public low-memory worksheet transformer API |

Matrix addendum: P8.724 extends the Existing workbook In-memory worksheet
editor row with `WorksheetEditor::erase_cells(CellRange)` for sparse rectangular
range erase. It deletes only represented active sparse records inside a
validated 1-based inclusive `CellRange`, treats missing-only ranges as
successful no-ops, and does not add dense range deletion, tombstones,
structural row/column delete, relationship repair, or large-file low-memory
random edit.

Matrix addendum: P8.726 adds initializer-list convenience overloads for the
sparse batch APIs: `set_cells()`, `set_cell_values()`, coordinate-batch
`clear_cell_values()`, and coordinate-batch `erase_cells()`. These overloads are
small literal-batch UX only; they synchronously delegate to the existing span
overloads and preserve identical preflight, duplicate/missing-coordinate,
guardrail, diagnostic, and non-goal boundaries.

Matrix addendum: P8.727 adds `WorksheetEditor::append_row()` for small-file
In-memory editing. It appends values to columns 1..N on the row after the current
maximum represented sparse row, treats empty input as a no-op, and stages the
whole append so width, Excel row-limit, style, max_cells, and memory-budget
failures do not mutate the active sparse store. It is not row insertion, row
metadata creation, table/range metadata recalculation, sharedStrings/styles
migration, or large-file low-memory random editing.

Matrix addendum: P8.728 adds `WorksheetEditor::set_row()` for small-file
In-memory editing. It replaces one represented sparse row by deleting existing
records in that row and writing values to columns 1..N; empty input clears an
existing represented row and is a no-op for a missing row. The operation is
staged so invalid row, width, style, max_cells, and memory-budget failures do
not mutate the active sparse store. It is not row insertion/deletion, row
shifting, row metadata editing, table/range metadata recalculation,
sharedStrings/styles migration, or large-file low-memory random editing.

Matrix addendum: P8.729 extends the same small-file In-memory row with
`WorksheetEditor::erase_row()` and `WorksheetEditor::erase_rows()`. These APIs
delete only represented active sparse records from a 1-based row or inclusive
row range, treat missing rows / missing-only ranges as successful no-ops, stage
state changes before replacing the active store, and do not add row deletion,
row shifting, row metadata editing, dense range deletion, relationship repair,
or large-file low-memory random editing.

Matrix addendum: P8.730 extends the same small-file In-memory row with
`WorksheetEditor::erase_column()` and `WorksheetEditor::erase_columns()`. These
APIs delete only represented active sparse records from a 1-based column or
inclusive column range, treat missing columns / missing-only ranges as
successful no-ops, stage state changes before replacing the active store, and
do not add column deletion, column shifting, column metadata editing, dense
range deletion, relationship repair, or large-file low-memory random editing.

Matrix addendum: P8.731 adds `WorksheetEditor::set_column()` for small-file
In-memory editing. It replaces one represented sparse column by deleting
existing records in that column and writing values to rows 1..N; empty input
clears an existing represented column and is a no-op for a missing column. The
operation is staged so invalid column, height, style, max_cells, and
memory-budget failures do not mutate the active sparse store. It is not column
insertion/deletion, column shifting, column metadata editing, table/range
metadata recalculation, sharedStrings/styles migration, or large-file
low-memory random editing.

Matrix addendum: P8.732 adds `WorksheetEditor::clear_row()` /
`clear_rows()` and `clear_column()` / `clear_columns()` for small-file
In-memory value-only editing. These APIs keep represented sparse records,
convert their values to explicit blanks, preserve each current source style
handle, treat missing-only row/column inputs as successful no-ops, and stage
state changes before replacing the active store. They are not row/column
deletion, row/column shifting, row/column metadata editing, dense range
editing, tombstone output, style migration/merge/creation, relationship repair,
or large-file low-memory random editing.

Matrix addendum: P8.733 adds `WorksheetEditor::set_row_values()` and
`WorksheetEditor::set_column_values()` for small-file row/column prefix
value-only writes. They write to columns 1..N or rows 1..N, preserve source
style handles on overwritten prefix cells, insert missing prefix cells without
styles, leave prefix-outside sparse cells untouched, treat empty input as a
clean no-op, and stage coordinate / width-height / caller-style / guardrail
failures before active-store mutation. They do not add row/column replacement,
insert/delete, shifting, dense range writes, metadata recalculation, style
migration/merge/creation, sharedStrings migration, or large-file random
editing.

Matrix addendum: P8.734 adds `WorksheetEditor::row_cells()` and
`WorksheetEditor::column_cells()` for small-file sparse row/column inspection.
They return owning `WorksheetCellSnapshot` vectors for already represented
records in one row or one column, preserve row-major ordering, synthesize no
missing cells, do not update `last_edit_error()`, and do not dirty or reload the
materialized session. They are conveniences over `sparse_cells(CellRange)`, not
dense row/column reads, row/column metadata inspection, iterators, metadata
recalculation, or large-file random access.

Matrix addendum: P8.735 adds `WorksheetEditor::sparse_cells(std::string_view)`
as a strict uppercase A1 read-only range convenience over
`sparse_cells(CellRange)`. It accepts `A1` and rectangular `A1:C3` references,
rejects lowercase, sheet-qualified, absolute, whole-row / whole-column,
multi-area, reversed, leading-zero, and out-of-limit references, returns only
active sparse records without missing-cell synthesis, preserves read
diagnostics, and is not dense range read, iterator, metadata recalculation, or
large-file random access.

Matrix addendum: P8.736 adds `WorksheetEditor::sparse_cells(std::span<const
WorksheetCellReference>)` and the matching initializer-list overload for
small-file sparse coordinate batch inspection. They return owning snapshots for
already represented cells in input order, skip missing coordinates, allow
duplicate coordinates, preserve read diagnostics, and are not dense batch reads,
iterator APIs, metadata recalculation, or large-file random access.

Matrix addendum: P8.769 adds `WorksheetEditor::contains_cell()` for row/column
and strict uppercase A1 represented-state inspection. It returns true for
source-backed records, edited records, and explicit blank records, false for
missing or erased cells, copies no `CellValue`, preserves `last_edit_error()` on
invalid reads, and is not worksheet `<dimension>` metadata inspection, dense
matrix access, iterator exposure, or large-file low-memory random access.

Matrix addendum: P8.770 adds no-argument
`WorksheetEditor::clear_cell_values()` and `WorksheetEditor::erase_cells()` as
whole materialized sparse-store operations. The clear operation converts every
represented record to an explicit blank while preserving current source style
handles; the erase operation removes every represented record. Empty stores are
clean no-ops that clear public edit diagnostics. These are not worksheet
deletion, sheetData part removal, dense range mutation, metadata recalculation,
relationship repair, or large-file low-memory random access.

Matrix addendum: P8.771 adds `WorksheetEditor::insert_rows()` /
`delete_rows()` and `WorksheetEditor::insert_columns()` / `delete_columns()` for
small-file In-memory represented sparse row/column shifts. Insert helpers move
represented cells at or after the insertion coordinate downward/rightward;
delete helpers remove represented cells inside the count span and move later
represented cells upward/leftward. `count == 0` is a successful no-op after
validating the first coordinate. Each operation is preflighted as a full sparse
coordinate transform, so invalid coordinates, count overflow, Excel row/column
limit overflow, `max_cells`, or `memory_budget_bytes` failures do not mutate the
active store. Dirty save-as refreshes emitted sparse `<sheetData>` and
`<dimension>` only. This does not add complete Excel structural row/column
edits, row/column metadata, formula/table/range recalculation, drawing/chart/VBA
sync, relationship repair, sharedStrings/styles migration, calcChain rebuild,
or large-file low-memory random editing.

Matrix addendum: P8.736 adds
`WorksheetEditor::clear_cell_values(std::string_view)` and
`WorksheetEditor::erase_cells(std::string_view)` as strict uppercase A1 range
mutation conveniences over the existing `CellRange` sparse clear/erase paths.
They accept `A1` and rectangular `A1:C3` references, reject lowercase,
sheet-qualified, absolute, whole-row / whole-column, multi-area, reversed,
leading-zero, and out-of-limit references, update `last_edit_error()` on
invalid mutation inputs, and only clear or remove represented active sparse
records without missing-cell synthesis. They do not add dense range
write/delete, A1 range writes, tombstones, structural row/column delete, range metadata
recalculation, relationship repair, or large-file random editing.

因此接下来的 API 推进重点是继续保持三条路径清晰：`WorkbookEditor` 统一承载
existing-file facade，但 whole-`<sheetData>` replacement 属于 Patch，`WorksheetEditor`
属于 small-file In-memory。后续只有当 public header、实现、public tests 和文档注释
同时准备好时，才允许继续扩大 current API。

### 当前 API / 功能设计门

下一轮功能推进先过设计门，再进入代码。每个 public API 或功能切片必须先回答：

- 所属 facade：`WorkbookWriter`、`Workbook`、当前 `WorkbookEditor`，还是 future
  `WorksheetEditor`。
- 所属模式：Streaming、Patch、In-memory；是否进入 worksheet row/cell 热路径。
- 状态视图：面向 source catalog、current planned catalog，还是 pending replacement
  diagnostics；rename / replacement 组合后如何解释。
- Guardrail：cell 数、payload size、memory budget、ZIP entry / chunk input 边界；
  是否可能隐式物化大 worksheet。
- Existing-file policy：对 sharedStrings、styles、formulas、relationships、
  ranges、tables、drawings、images、comments、VBA、custom XML 和 unknown extensions
  是 preserve、audit、fail 还是 edit。
- 失败语义：是否保证 failure-before-state-change；错误信息是否区分 source current-input
  和 replacement payload input。
- 验收证据：需要 public header / Doxygen、README 示例、单元测试、package roundtrip、
  Excel/openpyxl smoke、benchmark 或 memory evidence 中的哪些项。

当前推荐的功能设计顺序：

1. `WorkbookEditor` facade 收口：source-vs-planned 示例、pending replacement
   diagnostics 示例和 save-as smoke，而不是暴露 internal `EditPlan`。
2. `WorkbookEditor` coarse diagnostics 窄扩展：先设计 renamed sheet summary、
   replacement summary 或 source/planned catalog diff 的 public value 形状。
3. `WorksheetEditor` 首片后续：在已公开的 `worksheet()` / `try_worksheet()` /
   `try_cell()` / `get_cell()` / `set_cell()` / `erase_cell()` 基础上，继续扩展前先冻结
   handle failure semantics、style policy、sharedStrings / formula / range metadata
   的 preserve / fail / audit 边界。
4. Existing-file feature policy matrix：先把 preserve / audit / fail / edit 边界写清，
   再决定是否开放 styles、sharedStrings、hyperlinks、tables、images 等语义编辑。
5. Streaming API polish：只补不破坏 row-order hot path 的示例、Doxygen 和低风险功能。

### P7.1 Current Patch Facade And Future Editor Draft

> 落地进度：`WorkbookEditor` 的 existing-file Patch 子集已落地；P8.378 又打开了
> 首个 small-file In-memory `WorksheetEditor` public 切片。当前 `worksheet()`
> 会显式 materialize 一个已有 sheet，返回 borrowed handle，并由
> `WorkbookEditor::save_as()` 自动 flush dirty sparse store。

P7.1 记录当前已落地的 `WorkbookEditor` existing-file facade；它仍不把 internal
`PackageEditor` 变成 public API。当前 facade 下有两条明确路径：Patch 路径覆盖
whole-`<sheetData>` 替换、targeted cell replacement/upsert、media part replacement
和窄 sheet-catalog 改名；In-memory 路径覆盖一个已存在 worksheet 的小文件随机 cell
编辑首片。两者都必须继续把 OPC part、relationship owner 和 content type override
隐藏在内部 Patch / In-memory 底座之后。

`WorkbookEditor` 的当前 public Patch subset：

- `WorkbookEditor::open(input_path)`：打开已有 workbook。
- `WorkbookEditor::open(input_path, options)`：打开已有 workbook，并为当前
  `replace_sheet_data()` replacement payload 构建设置窄 guardrails；这些 options
  不加载 source worksheet cells，不提供 random editing，也不是 future In-memory
  workbook / worksheet limits。
- `worksheet_names()` / `has_worksheet(name)`：只读 current planned sheet inspection；
  成功 `rename_sheet()` 后会反映 queued catalog name，不暴露 workbook relationships
  或 package parts。
- `source_worksheet_names()` / `has_source_worksheet(name)`：只读 opened source workbook
  sheet inspection；不反映 queued rename，用于需要原始 source catalog 视图的调用方。
- `has_pending_changes()` / `pending_change_count()`：粗粒度 pending-save 诊断。
  `has_pending_changes()` 也会把 dirty `WorksheetEditor` materialized session 视为
  save-as pending；`pending_change_count()` 仍只统计已 queued 的 public edit / flush
  handoff 次数，不把尚未 flush 的 dirty sparse store 计入次数。它不是 internal
  `EditPlan`、dependency audit 或 output-plan reason 的直接公开。
- `pending_replacement_cell_count()` /
  `pending_replacement_worksheet_names()` / `has_pending_replacement()` /
  `estimated_pending_replacement_memory_usage()`：只统计最终 queued
  `replace_sheet_data()` replacement payload per planned sheet；成功
  `rename_sheet()` 会迁移这组 public diagnostics，避免后续按 planned 新名替换时重复
  统计 stale pre-rename payload；失败的 duplicate / invalid `rename_sheet()` 不迁移或
  污染这组 diagnostics，后续仍可按原 sheet name 覆盖 replacement payload；不统计
  source workbook cells、renamed-only sheets、sharedStrings/styles、relationships、
  ZIP/package writer buffers 或 save-time assembly 成本。
  当前回归还固定连续 rename 的可逆 planned-catalog 语义：含 queued replacement 的
  sheet 先改到临时 planned name，再改回 source name 时，replacement diagnostics
  会迁回 source name，`worksheet_catalog()` / `pending_worksheet_edits()` 不再标记
  renamed，输出也不会泄漏临时 planned name。
- `pending_targeted_cell_replacement_count()` /
  `pending_targeted_cell_replacement_worksheet_names()` /
  `has_pending_targeted_cell_replacement()` /
  `estimated_pending_targeted_cell_replacement_xml_bytes()`：只统计最终 queued
  `replace_cells()` targeted-cell replacement/upsert patches per planned sheet。Duplicate
  coordinates collapse to the latest payload, later successful calls can replace
  same-coordinate diagnostics, and successful `rename_sheet()` moves this
  diagnostic set to the new planned sheet name. The byte estimate is only the
  caller single-cell XML payload size; it is not source worksheet XML,
  `PackageEditor` staged file size, ZIP buffers, process RSS, or save-time
  package assembly peak.
- `pending_materialized_worksheet_names()`：只统计 dirty materialized
  `WorksheetEditor` sessions，按 current planned catalog order 返回 planned sheet
  names；clean materialized sessions 不返回，successful `save_as()` auto-flush 后清空。
  Renamed sheets report the current planned name; clean post-save reacquire does
  not re-add the name, and a later mutation re-adds it until the next save. If
  a rename chain returns to the source name before materialization, later dirty
  materialized diagnostics use that restored source/planned name instead of the
  transient name.
  它不触发 flush、不增加 `pending_change_count()`、不包含 whole-`<sheetData>`
  replacement payloads、不暴露 internal `EditPlan`，也不更新 `last_edit_error()`。
- `pending_materialized_cell_count()` /
  `estimated_pending_materialized_memory_usage()`：同样只聚合 dirty materialized
  `WorksheetEditor` sessions。前者返回 active sparse cell record 总数，包含
  explicit blank records；后者返回 dirty sessions 的 `CellStore` memory estimate
  总和，不是进程 RSS，也不包含 source package bytes、generated XML chunks、
  `PackageEditor` staging files、ZIP writer buffers 或 save-time package assembly
  costs。clean materialized sessions 和 queued whole-`<sheetData>` replacements 不计入。
  Renamed and unrenamed sessions share the same save/clean-reacquire clearing
  lifecycle and later-mutation re-dirtying behavior.
  这两个方法不触发 flush、不增加 `pending_change_count()`、不暴露 internal
  `EditPlan`，也不更新 `last_edit_error()`。
- `last_edit_error()`：返回最近一次失败的 public edit（当前包括
  `replace_sheet_data()`、`replace_cells()`、`rename_sheet()` 和 `WorksheetEditor::set_cell()` /
  `erase_cell()` mutation failure）的可读诊断；成功 public edit 会清空它，
  public inspection / pending diagnostic methods、`WorksheetEditor::try_cell()` /
  size inspection 和 `save_as()` 不更新它。Materialization / load failure 不更新它。它不是
  exception stack、internal `EditPlan` diagnostic、relationship / dependency audit 或
  output-plan reason。Failed `WorksheetEditor` mutations do not create dirty
  materialized diagnostics or summaries; a later successful mutation follows
  the current planned-name mapping.
  当前回归还固定 replacement 后 failed rename 的状态卫生：duplicate / invalid
  rename 失败会把该字段更新为当前失败诊断，但不会迁移 pending replacement
  diagnostics、catalog mapping 或 edit summaries。failed `save_as()` 不创建、更新或清空
  该字段；调用方可以修正输出路径后继续使用已 queued 的 public edits。
- `pending_worksheet_edits()`：返回 `WorkbookEditorWorksheetEditSummary` 列表，按
  source workbook sheet-catalog order 汇总当前 queued public worksheet-level edits 和
  dirty materialized `WorksheetEditor` sessions：source name、planned name、是否
  catalog rename、是否 whole-`<sheetData>` replacement、是否 targeted cell
  replacement/upsert、final queued replacement cell count / memory estimate、targeted-cell
  count / XML payload byte estimate，以及 `materialized_dirty`、materialized sparse cell count
  和 materialized memory estimate。clean materialized sessions 不返回；successful
  `save_as()` auto-flush 后，dirty materialized summary 会消失，除非同一 worksheet
  仍有 rename / whole-`<sheetData>` replacement summary。它是粗粒度 public facade
  summary，不触发 flush、不增加 `pending_change_count()`、不更新
  `last_edit_error()`，也不是 internal `EditPlan`、package part diff、dependency /
  relationship audit、preserved metadata 列表、source cell count 或 save-time output plan。
- `worksheet_catalog()`：返回 `WorkbookEditorWorksheetCatalogEntry` 列表，按 source
  workbook sheet-catalog order 展示 source name 到 current planned name 的完整映射，
  以及该 entry 是否被 `rename_sheet()` 改名；它用于解释 `worksheet_names()` /
  `has_worksheet()` / `save_as()` 当前会看到的 planned catalog，不暴露 workbook
  relationships、worksheet part names、package entries、internal `EditPlan` 或
  dependency audit。
- `formula_reference_audits()`：只读扫描已经 materialized 的 `WorksheetEditor`
  sessions 中的 formula cell，返回 `WorkbookEditorFormulaReferenceAudit` 列表，记录
  formula 所在 source/planned sheet、cell 坐标、formula text、原始 sheet qualifier、
  raw reference token、qualified reference text、解码后的 referenced sheet name，
  external-workbook / 3D sheet-range qualifier 分类，以及该 qualifier 是否仍指向一个已被
  `rename_sheet()` 改名的 source sheet。它不 materialize 新 sheet、不扫描整包
  worksheet XML、不解析完整 Excel 公式语法、不求值、不改写公式、不 rebuild
  calcChain、不验证外部 workbook target 或 3D 引用语义，也不更新
  `last_edit_error()`，不递增 `pending_change_count()`、不排队 replacement、不弄脏
  materialized sessions，也不改变 pending edit diagnostics。
- `source_formula_reference_audits()`：只读扫描 source worksheet formula XML，返回与
  materialized audit 相同语义的 source-read diagnostics。它不 materialize
  `WorksheetEditor` session、不把 source worksheet XML 排入 rewrite、不改写公式、不更新
  `last_edit_error()`，不递增 `pending_change_count()`、不创建 replacement diagnostics、
  不创建 materialized diagnostics，也不新增 pending edit summaries；默认
  `rename_sheet()` 后只报告 stale source-name 风险。
- `defined_name_formula_reference_audits()`：只读扫描 current planned
  workbook `xl/workbook.xml` 中 direct `<definedNames><definedName>` formula
  text（有 queued small workbook rewrite 时使用 planned XML，否则使用 source
  XML），返回
  `WorkbookEditorDefinedNameFormulaReferenceAudit` 列表，记录 definedName 名称、
  `localSheetId` scope、formula text、原始 sheet qualifier、raw reference token、
  qualified reference text、解码后的 referenced sheet name、external-workbook / 3D
  sheet-range qualifier 分类，以及该 qualifier 是否仍指向一个已被
  `rename_sheet()` 改名的 source sheet。它只 materialize 小型 workbook metadata part，
  不 materialize worksheet、不扫描 cell formulas、不解析完整 Excel 公式语法、不求值、
  不改写 definedNames、不修复 workbook metadata、不 rebuild calcChain、不验证外部
  workbook target 或 3D 引用语义，也不更新 `last_edit_error()`。Malformed workbook
  metadata with mismatched or unclosed XML tags fails the diagnostic scan
  instead of being balanced or repaired. 它也不递增 `pending_change_count()`、不排队
  replacement、不弄脏 materialized sessions、不改变 pending edit diagnostics。
- 默认 `rename_sheet()` 保持 catalog-only：只改 workbook sheet catalog，不改
  materialized worksheet formulas、non-materialized source worksheet formulas 或 direct
  definedName formula text。上述 audit API 会把 `data!` / `DATA!` 这类 case-varied
  local qualifiers 按 ASCII case-insensitive 映射到 source/planned catalog，并在
  diagnostics 中保留公式原始拼写作为 stale source-name 风险证据。
- Internal formula sync foundation：`detail::rewrite_formula_sheet_references()`
  可以按显式 rewrite rule 更新本地 sheet-qualified formula references，并统一写为
  quoted replacement sheet qualifier；它跳过 external workbook qualifiers、3D
  sheet-range qualifiers、structured references 和 quoted string text，ambiguous
  rules 会失败。`detail::rewrite_workbook_defined_name_formula_references()`
  只把同一窄 rewrite 应用到 source workbook direct definedName formula text，
  未变化的 workbook XML bytes 保持不动。这些 helper 仍是 internal building blocks，
  不是 public `rename_sheet()` 默认同步策略，不做公式求值、不解析完整 Excel grammar、
  不修复 workbook XML、不 rebuild calcChain。public `rename_sheet()` 只有在显式
  `RewriteDefinedNamesAndMaterializedWorksheetFormulas` policy 下，才把同一窄 rewrite
  应用于已经 materialized 的 WorksheetEditor formula cells；它不 materialize 或扫描
  未载入 worksheet XML。
- `replace_sheet_data(name, rows)`：按 sheet name 替换整个 `<sheetData>`，输入是
  `CellValue` 行；这是 bounded local rewrite，不是 random cell editing。若当前 editor
  已成功 `rename_sheet()`，follow-up replacement lookup 使用 planned 新 sheet name，
  与 `worksheet_names()` / `has_worksheet()` 的 current planned inspection 一致。缺失
  sheet name 先按 current planned catalog 失败，再进入 replacement payload 构建，避免
  missing-sheet 场景先消耗 replacement guardrail / memory diagnostic 路径。
  `CellValue::blank()` 作为 explicit replacement cell 会写出 empty `<c/>`，可携带
  caller-supplied non-default style id；空 row vector 只推进输入 row mapping，不写出
  explicit empty `<row>`。
  `CellValue` 携带的 non-default `StyleId` 会按 numeric value 原样写成 `s="N"`；
  explicit default `StyleId{}` 不写 `s="0"`；source `xl/styles.xml` 只 byte-preserve，
  不校验该 id 是否属于 source workbook，不迁移或合并 styles。
- `replace_cells(name, span<WorksheetCellUpdate>)`：按 current planned sheet name
  替换一批 `<c>` cells，输入是 full `CellValue` payload。默认
  `CellPatchMissingCellPolicy::Fail` 要求所有目标 cell 已经存在；显式调用
  `replace_cells(name, cells, CellPatchMissingCellPolicy::Insert)` 时，缺失 cells 会作为
  point upsert 插入到既有 rows 或合成 minimal missing rows。它复用 internal
  worksheet transformer / `PackageEditor::replace_worksheet_cells_by_name()` /
  `replace_or_insert_worksheet_cells_by_name()`，扫描 source 或 current planned worksheet
  XML，跳过被替换旧 cell payload，并把 rewritten worksheet staged 为 file-backed
  package-entry chunks。missing target（Fail policy 下）、invalid coordinate、
  same-sheet whole-`<sheetData>` replacement 或 materialized `WorksheetEditor` session
  都必须 fail-before-public-diagnostic-update。Duplicate coordinates are
  later-wins. Text emits inline strings; formula
  payloads request full recalculation and stale calcChain cleanup; caller
  non-default `StyleId` values are written as-is without style table validation.
  该 API 不 shift ranges、不保留 overwritten cell 的旧 metadata、不迁移
  sharedStrings/styles、不同步 tables/filters/drawings/defined names、也不修复或 pruning
  relationships；Insert policy 也不是 row/column insert、table resize 或 range metadata
  recalculation。
- `replace_image(image_part_name, path/span)`：只替换当前 package 中已有 PNG/JPEG
  `xl/media/*` part 的 bytes。file path overload 会在 `replace_image(path)` 阶段验证
  图片格式，并在每次 `save_as()` 写包时重新读取同一个 staged file；该 staged file
  还必须保持 staged size/CRC 一致，丢失或内容变化都会让 `save_as()` 失败但保留
  queued public edit state。若同一 media part 后续再次成功 `replace_image()`，后一次
  replacement 会覆盖前一次 queued source，只有最终 queued source 参与 `save_as()`。
  memory span
  overload 会在调用期间复制 caller bytes，后续 `save_as()` 不依赖 caller buffer；
  已复制的 staged bytes 由 FastXLSX 持有，并在 queued state 保留期间可跨多次
  `save_as()` 复用。
  两个 overload 都不编辑 worksheet XML、drawing XML、anchors、relationships、content
  types、EXIF/PNG/JPEG metadata，也不做 image insertion、target discovery 或 orphan
  cleanup。
- `rename_sheet(old_name, new_name)`：只重写 workbook sheet catalog 的 `name`
  attribute，不同步 defined names、formulas、tables、drawings 或 relationship targets。
- `worksheet(name, options)` / `try_worksheet(name, options)`：显式 materialize
  当前 planned catalog 中一个已有 worksheet，返回 borrowed `WorksheetEditor` handle。
  `try_worksheet()` 只在 sheet missing 时返回 empty optional；options mismatch、已有
  `replace_sheet_data()` payload、source worksheet loader 不支持的 cell shape 或 malformed
  source worksheet XML 仍抛 `FastXlsxError`。`WorksheetEditorOptions::max_cells` /
  `memory_budget_bytes` 是 per-materialization sparse-store guardrails，和
  `WorkbookEditorOptions` replacement-payload guardrails 分离。重复调用同一 planned
  sheet 且 options 匹配时复用同一 session；options 不匹配、sheet missing、已有
  `replace_sheet_data()` payload 或 source worksheet loader 不支持的 cell shape 会失败。
  Materialization 本身不是 edit，不增加 `pending_change_count()`，失败也不更新
  `last_edit_error()`。
- `WorksheetEditor`：当前 public 首片提供 `name()`、`try_cell(row, column)`、
  `get_cell(row, column)`、`set_cell(row, column, CellValue)`、
  `set_cells(span<WorksheetCellUpdate>)`、
  `append_row(span<const CellValue>)`、
  `set_row(row, span<const CellValue>)`、
  `set_column(column, span<const CellValue>)`、
  `erase_row(row)`、`erase_rows(first_row, last_row)`、
  `erase_column(column)`、`erase_columns(first_column, last_column)`、
  `set_cell_value(row, column, CellValue)`、
  `set_cell_values(span<WorksheetCellUpdate>)`、
  `clear_cell_value(row, column)`、`clear_row(row)`、
  `clear_rows(first_row, last_row)`、`clear_column(column)`、
  `clear_columns(first_column, last_column)`、`clear_cell_values()`、
  `clear_cell_values(CellRange)`、
  `clear_cell_values(std::string_view)`、
  `clear_cell_values(span<WorksheetCellReference>)`、`erase_cell(row, column)`、
  `erase_cells()`、`erase_cells(CellRange)`、`erase_cells(std::string_view)`、
  `erase_cells(span<WorksheetCellReference>)`、除
  `set_cells()`、`set_cell_values()` 和 clear/erase batch/range 以外的 single-cell
  read/mutation API 的 strict uppercase A1 string overload、
  `has_pending_changes()`、`sparse_cells()`、`sparse_cells(CellRange)`、
  `sparse_cells(std::string_view)` strict uppercase A1 range overload、
  `sparse_cells(span<WorksheetCellReference>)` /
  `sparse_cells(initializer_list<WorksheetCellReference>)` explicit sparse
  coordinate batch overloads、
  `row_cells()`、`column_cells()`、`cell_count()` 和
  `estimated_memory_usage()`。
  `try_cell()` 对 missing sparse record 返回 empty；`get_cell()` 对 missing sparse
  record 抛 `FastXlsxError`；explicit blank 返回 `CellValue::blank()`。
  single-cell A1 overload 只接受单个 uppercase cell reference，例如 `A1` 或
  `XFD1048576`；lowercase、range、zero 或 leading-zero row、zero column
  和超出 Excel 上限的引用会被拒绝。`sparse_cells(std::string_view)` 是
  read-only strict uppercase A1 range convenience，接受 `A1` 或 `A1:B2`，
  但拒绝 lowercase、sheet-qualified、absolute、whole-row / whole-column、
  multi-area、reversed、leading-zero 和 out-of-limit references。
  `clear_cell_values(std::string_view)` 和 `erase_cells(std::string_view)` 复用
  同一 strict uppercase A1 range 语法做 sparse mutation convenience；它们仍只
  清空或删除 already represented active records，不把 missing cells 合成为
  explicit blanks 或 tombstones。
  `sparse_cells()` 返回 owning row-major `WorksheetCellSnapshot` vector，包含
  explicit blank records。`sparse_cells(CellRange)` 和
  `sparse_cells(std::string_view)` 返回 1-based inclusive range 内已经存在的
  active sparse records，不合成 missing cells。`sparse_cells(span<WorksheetCellReference>)`
  和 initializer-list overload 按 caller 输入顺序返回已经 represented 的 owning
  snapshots，允许重复坐标并跳过 missing coordinates。`used_range()` 返回 represented
  sparse records 的 optional 1-based inclusive bounding `CellRange`，empty materialized
  store 返回 `std::nullopt`，explicit blank records 计入边界。`row_cells()` /
  `column_cells()` 返回单个 row 或 column 内已经 represented 的 active sparse records，
  同样不合成 missing cells、不更新 `last_edit_error()`。`contains_cell()` 返回当前
  materialized sparse store 的 represented-state boolean，explicit blank records 计为
  represented，missing / erased cells 计为 false，且不复制 `CellValue`。这些 reads 都不暴露
  内部 iterator/lifetime，不是 worksheet `<dimension>` metadata read/repair、dense
  range / row / column read 或 streaming sparse iterator，不修改 dirty state，也不
  更新 `last_edit_error()`。
  `has_pending_changes()` 是 worksheet-local dirty-state inspection：它只报告当前
  materialized session 是否等待 `WorkbookEditor::save_as()` auto-flush，不触发 flush，
  不增加 `pending_change_count()`，不暴露 internal Patch `EditPlan`，也不更新
  `last_edit_error()`。
  `erase_cell()` 删除 sparse record；
  `CellValue::blank()` 表示 explicit blank replacement cell。`set_cell()` 允许默认
  style / no-style value，拒绝 non-default `StyleId`，因为 existing-workbook styles
  registry、style id validation / migration / merge 仍未公开。
  `set_cells(span<WorksheetCellUpdate>)` 是稀疏 full-cell replacement batch：
  它先验证每个显式 row/column 坐标和 caller-supplied style id，再在 staged sparse
  store 上执行 `max_cells` / `memory_budget_bytes` guardrail；任一失败都会拒绝整个
  batch 且不修改 materialized session。空 batch 是 successful no-op，duplicate
  coordinates 按输入顺序处理且后者覆盖前者。它不接受 A1 range string，不分配 dense
  matrix，不保留被覆盖 cell 的 source style，也不是 large-file low-memory random edit。
  `append_row(span<const CellValue>)` 是稀疏 append convenience：它按当前
  represented 最大 row 追加到下一行，值写入 columns 1..N，empty input 是 no-op；
  它预先拒绝超 16,384 columns、超过 Excel row 1,048,576、caller-supplied
  non-default style id，并通过 staged store 继承 `max_cells` / `memory_budget_bytes`
  guardrails。它不是 row insertion、row metadata creation、table/range metadata
  recalculation 或 large-file low-memory random edit。
  `set_row(row, span<const CellValue>)` 是稀疏 represented-row replacement：它先删除
  目标 row 已 represented 的 sparse records，再把输入值写到 columns 1..N；empty input
  清空已有 represented row，对 missing row 则是 successful no-op。它预先拒绝非法
  row、超 16,384 columns、caller-supplied non-default style id，并通过 staged store
  继承 `max_cells` / `memory_budget_bytes` guardrails。它不是 row insertion/deletion、
  row shifting、row metadata edit、table/range metadata recalculation 或 large-file
  low-memory random edit。
  `set_column(column, span<const CellValue>)` 是稀疏 represented-column replacement：
  它先删除目标 column 已 represented 的 sparse records，再把输入值写到 rows 1..N；
  empty input 清空已有 represented column，对 missing column 则是 successful no-op。
  它预先拒绝非法 column、超 1,048,576 rows、caller-supplied non-default style id，
  并通过 staged store 继承 `max_cells` / `memory_budget_bytes` guardrails。它不是
  column insertion/deletion、column shifting、column metadata edit、table/range
  metadata recalculation 或 large-file low-memory random edit。
  `clear_row(row)` / `clear_rows(first_row, last_row)` 和
  `clear_column(column)` / `clear_columns(first_column, last_column)` 是 sparse
  row/column value-clear：它们只把目标 row/column 或 inclusive row/column range 内
  已经 represented 的 active sparse records 转成 explicit blanks，并保留各自 current
  source style handle；missing-only 输入是 successful no-op，reversed range 在状态变更前失败。
  它们不是 row/column deletion、row/column shifting、row/column metadata edit、
  dense range editing、tombstone output、style migration/merge/creation、
  relationship repair 或 large-file low-memory random edit。
  `erase_row(row)` / `erase_rows(first_row, last_row)` 和
  `erase_column(column)` / `erase_columns(first_column, last_column)` 是 sparse
  row/column record erase：它们只删除目标 row/column 或 inclusive row/column range
  内已经 represented 的 active sparse records，missing-only 输入是 successful no-op，
  reversed range 在状态变更前失败。它们不是 row/column deletion、row/column
  shifting、row/column metadata edit、dense range deletion、tombstone output、
  table/range metadata recalculation、relationship repair 或 large-file low-memory
  random edit。
  `insert_rows(first_row, row_count)` / `delete_rows(first_row, row_count)` 和
  `insert_columns(first_column, column_count)` / `delete_columns(first_column, column_count)`
  是 small-file represented sparse row/column shift helpers。insert helpers 只移动
  insertion coordinate 之后已经 represented 的 sparse records；delete helpers 删除 count
  span 内已经 represented 的 sparse records，并移动 span 之后的 represented records。
  `count == 0` 是 successful no-op；非法坐标、count 越界、shift 后超过 Excel 行列上限、
  `max_cells` 或 `memory_budget_bytes` 都会在状态变更前失败。它们只更新 sparse
  store 和 dirty save-as 投影的 `<sheetData>` / `<dimension>`，不是完整 Excel
  row/column structural edit，不同步 formulas、tables、autoFilter、mergeCells、
  data validations、conditional formatting、hyperlinks、drawings/charts/VBA、defined
  names、relationships、sharedStrings/styles 或 calcChain。
  `set_cell_values(span<WorksheetCellUpdate>)` 是稀疏 value-only batch：它使用同样的
  batch preflight / staged sparse store 语义，但每个命中的已有 target 会保留当前
  materialized source style handle；missing target 以 no-style 记录插入，duplicate
  coordinates 仍按输入顺序 later-wins。它不是 style migration / merge API。
  `clear_cell_values(CellRange)` 使用 1-based inclusive range guardrail，只把
  range 内已有 active sparse records 清成 explicit blanks 并保留各自 current source
  style handle；missing cells 不合成，missing-only range 是 successful no-op。它不是
  dense range writer、tombstone API、range metadata recalculation、
  style migration/merge/creation 或 large-file low-memory random edit。
  `clear_cell_values(std::string_view)` 只是该路径的 strict uppercase A1 range parser
  convenience，语义仍是 sparse clear。
  `clear_cell_values(span<WorksheetCellReference>)` 是显式坐标 batch clear：invalid
  coordinate 会在状态变更前拒绝整个 batch；已有 sparse records 会变为保留 style 的
  explicit blank，missing coordinates 不合成 records，missing-only batch 是
  successful no-op。no-arg `clear_cell_values()` 对当前 materialized store 中所有
  represented records 执行同样的 style-preserving explicit blank 语义；empty store 是
  clean no-op。
  `erase_cells(CellRange)` 使用同样的 1-based inclusive range guardrail，只删除
  range 内已有 active sparse records；missing cells 不合成 tombstone，missing-only
  range 是 successful no-op。它不是 dense range deletion、
  structural row/column delete、range metadata recalculation、relationship repair 或 large-file
  low-memory random edit。
  `erase_cells(std::string_view)` 只是该路径的 strict uppercase A1 range parser
  convenience，语义仍是 sparse erase。
  `erase_cells(span<WorksheetCellReference>)` 是显式坐标 batch remove：invalid
  coordinate 会在状态变更前拒绝整个 batch；已有 sparse records 会被删除，
  duplicate coordinates 在首次删除后成为 no-op，missing coordinates 不合成
  tombstone，missing-only batch 是 successful no-op。no-arg `erase_cells()` 删除当前
  materialized store 中所有 represented records；empty store 是 clean no-op。
- `save_as(output_path)`：输出到新路径；不承诺原地 atomic overwrite，也不绕过现有
  Patch save-as guard。没有 queued public edits 时，它只写 reader-backed roundtrip
  copy，保留解压后的 source package entries；这不是 public `PackageEditor`、in-place
  save 或完整 preservation API。no-op `save_as()` 不会把 editor 置为 closed /
  committed 状态；调用方仍可继续 queue public edits 并另存，但这不是事务 history、
  undo 或自动清空 pending state。成功 `save_as()` 也不会消费 queued public edit
  diagnostics，调用方可把同一 planned state 另存到第二个输出路径；若 queued edit
  最终包含 file-backed `replace_image(path)`，该 staged file 也必须继续可读且匹配
  staged size/CRC，直到调用方不再复用该 queued state；若 queued edit 最终包含
  memory-backed
  `replace_image(span)`，其 staged bytes 由 FastXLSX 持有，可随同一 queued state
  重复写入多个输出路径。`save_as()` 会先做
  output path guard preflight，再把 dirty `WorksheetEditor` sessions flush 到 Patch plan；
  path guard 失败不清 dirty state。flush 成功后 dirty session 会变 clean，但同一
  owner 下已有的 `WorksheetEditor` borrowed handle 不会被删除或失效；后续 mutation
  可通过同一 handle 再次 dirty，并由下一次 `save_as()` reflush。只有 owner move /
  move assignment 会使旧 handle 失效并要求 caller reacquire。输出路径被拒绝或写出失败时，不清空
  queued replacement / rename，不改变 `worksheet_catalog()` / `pending_worksheet_edits()`，
  也不创建或覆盖 `last_edit_error()`。

`WorkbookEditor` / `WorksheetEditor` 的后续 extension 草案：

- 更宽 `save_as(output_path)` diagnostics：区分 queued Patch handoff、dirty
  materialized session 和 flushed materialized projection。

`WorksheetEditor` 当前首片和候选扩展：

- 当前：`name()`、`try_cell(row, column)`、`get_cell(row, column)`、
  `set_cell(row, column, CellValue)`、`set_cell_value(row, column, CellValue)`、
  `set_cells(span<WorksheetCellUpdate>)`、`append_row(span<const CellValue>)`、
  `set_row(row, span<const CellValue>)`、
  `set_column(column, span<const CellValue>)`、
  `set_row_values(row, span<const CellValue>)`、
  `set_column_values(column, span<const CellValue>)`、
  `erase_row(row)`、`erase_rows(first_row, last_row)`、
  `erase_column(column)`、`erase_columns(first_column, last_column)`、
  `clear_cell_value(row, column)`、`clear_row(row)`、
  `clear_rows(first_row, last_row)`、`clear_column(column)`、
  `clear_columns(first_column, last_column)`、`clear_cell_values()`、
  `clear_cell_values(CellRange)`、
  `clear_cell_values(std::string_view)`、`erase_cell(row, column)`、
  `erase_cells()`、`erase_cells(CellRange)`、`erase_cells(std::string_view)`、
  `erase_cells(span<WorksheetCellReference>)`、
  strict uppercase single-cell A1 overload、
  `has_pending_changes()` dirty-state inspection、
  `sparse_cells()` owning snapshot、`sparse_cells(CellRange)` filtered owning
  snapshot、`sparse_cells(std::string_view)` strict A1 range owning snapshot、
  `sparse_cells(span<WorksheetCellReference>)` /
  `sparse_cells(initializer_list<WorksheetCellReference>)` explicit sparse
  coordinate batch owning snapshots、
  `row_cells()` / `column_cells()` sparse row/column owning snapshots、
  `cell_count()`、`estimated_memory_usage()`。
- 候选：broader range iteration / streaming sparse record iterator / dense
  range read / dense range write。
- `insert_rows(...)`、`delete_rows(...)`：已作为当前 public small-file
  represented sparse row shift helpers 落地；仍需要持续证明
  dimension/range metadata/operation mixing，不要把它们写成完整 Excel
  structural row/column edit 或 metadata sync。

`WorksheetEditor` 进入 public header 前必须冻结的 contract：

- Load / materialization：future `WorkbookEditor::worksheet(name)` 只能显式
  materialize caller 请求的小 worksheet，不得在 `open()` 时默认加载所有 source
  worksheet cells。加载受 workbook-level 和 worksheet-level `max_cells` /
  `memory_budget_bytes` 约束；超限要在产生 visible edit state 前失败，错误信息必须
  说明触发的 limit kind、当前估算值和建议路径（大规模顺序写用 Streaming，已有
  workbook 局部替换或模板填充用 Patch）。
- Ownership：`WorksheetEditor` 持有或引用的长期 cell state 必须是 internal
  `CellStore` / `CellRecord` 这类 compact sparse model，不能保存 public `CellView`
  或把 `Cell` 当作长期 worksheet matrix。public 边界使用 `CellValue`，内部可以转换
  成更紧凑的 record / pool representation。
- Read semantics：`try_cell(ref)` 返回 empty 表示 sparse store 没有该 cell；
  `get_cell(ref)` 可以选择对 missing cell 抛出 `FastXlsxError` 或返回
  `CellValue::blank()`，但 public 设计必须在 header 注释中固定一种语义，不能让
  missing 和 explicit blank 混在一起。
- Mutation semantics：`set_cell(ref, value)` 覆盖该 cell 的 active value；
  `set_cell_value(ref, value)` 覆盖 value 但保留目标 current source style handle；
  `set_row_values(row, values)` / `set_column_values(column, values)` 执行
  value-only row/column prefix 写入，分别覆盖 columns 1..N 或 rows 1..N，
  保留已有 prefix target 的 source style handle，缺失 prefix cell 以 no-style
  record 插入，input prefix 外的 sparse cells 不受影响，empty input 是 clean
  no-op；
  `clear_cell_value(ref)` 把现有 active record 转成显式 blank 并保留该 style，
  missing target 是 successful no-op；`clear_row()` / `clear_rows()` 和
  `clear_column()` / `clear_columns()` 对 row/column 内已有 active records 执行同样
  value-clear 语义，missing-only row/column 输入不合成 cells；`clear_cell_values()` 对全部
  represented records 执行同样 value-clear 语义；`clear_cell_values(CellRange)` /
  `clear_cell_values(std::string_view)` 对 range 内已有 active records 执行同样
  value-clear 语义，missing cells 不合成；`erase_cell(ref)` 和
  no-arg `erase_cells()` / `erase_cells(CellRange)` / `erase_cells(std::string_view)` /
  `erase_cells(span<WorksheetCellReference>)` 删除 active record；
  `CellValue::blank()` 表示 caller 明确写入 blank replacement cell。当前 dirty
  projection 写空 `<c>`，styled blank 写 `s="N"`，但不会生成 tombstone。
- Existing-file handoff：source-backed in-memory edits 仍通过 `save_as(output_path)`
  写新 package；未修改 entries 和 unknown entries 默认 copy-original。cell edits
  投影到 worksheet rewrite 时必须暴露或内部记录 sharedStrings、styles、formulas、
  calc metadata、range metadata、worksheet `.rels` 和 linked parts 的 preserve /
  audit / fail 策略。
- Failure hygiene：load、set、erase 和 save-as preflight 只要失败，就不能部分应用
  mutation；若某个阶段无法保证 no-state-pollution，必须在 API 注释和测试里写清楚
  recovery guidance。

Current F2 gate audit:
- P8.378 opens the first public `WorksheetEditor` slice. The implementation
  evidence now covers public source-backed materialization, `try_cell()` reads,
  `set_cell()` / `erase_cell()` mutation, per-materialization max-cells
  guardrail failure hygiene, same-sheet Patch operation mixing rejection, and
  `WorkbookEditor::save_as()` dirty-session auto-flush.
- P8.379 extends the same narrow public slice with `WorkbookEditor::try_worksheet()`
  and `WorksheetEditor::get_cell()`. `try_worksheet()` returns empty only for
  missing current-planned sheet names; non-missing materialization failures still
  throw without updating `last_edit_error()`. P8.411 pins that a missing
  `try_worksheet()` result also preserves prior diagnostics and leaves later
  no-op `save_as()` on the copy-original path. `get_cell()` throws on missing
  sparse records and returns explicit `CellValue::blank()` records unchanged.
- P8.380 adds strict uppercase A1 string overloads for
  `WorksheetEditor::try_cell()`, `get_cell()`, `set_cell()`, and `erase_cell()`.
  They parse only single-cell references and then reuse row/column semantics;
  they do not add ranges, sparse iteration, metadata repair, or large-file
  random access.
- P8.381 adds `WorksheetEditor::sparse_cells()` plus
  `WorksheetCellReference` / `WorksheetCellSnapshot` as an owning row-major
  snapshot of active sparse records. This is read-only inspection for small
  materialized worksheets; it does not expose internal iterators, borrow store
  lifetime, add range iteration, mutate dirty state, update `last_edit_error()`,
  or synchronize worksheet metadata.
- P8.382 adds `WorksheetEditor::sparse_cells(CellRange)` as a filtered owning
  snapshot over active sparse records inside a 1-based inclusive range. It
  reuses `CellRange` validation, returns no synthetic blank cells for missing
  coordinates, and keeps the same read-only state hygiene: no dirty-state
  mutation and no `last_edit_error()` update. It is not a dense matrix read,
  broader range iterator, streaming sparse iterator, metadata recalculation, or
  large-file random access path.
- P8.383 adds `WorksheetEditor::has_pending_changes()` as a worksheet-local
  dirty-state probe for the borrowed materialized session. It reports whether
  sparse cell edits still need `WorkbookEditor::save_as()` auto-flush, does not
  flush, does not increment `WorkbookEditor::pending_change_count()`, does not
  expose internal Patch state, and does not update `last_edit_error()`.
- P8.384 locks down public borrowed-handle invalidation after `WorkbookEditor`
  ownership transfer. Handles acquired before move construction or move
  assignment are rejected on later session access; callers must reacquire from
  the moved-to / assigned-to editor. Reacquired handles can read and mutate the
  transferred materialized session. This is enforced with an owner generation
  guard so an overwritten target handle cannot accidentally attach to a new
  same-name assigned session.
- P8.385 adds `WorkbookEditor::pending_materialized_worksheet_names()` as a
  workbook-level dirty-materialized-session diagnostic. It returns planned sheet
  names for dirty `WorksheetEditor` sessions in current planned catalog order,
  omits clean materialized sessions, clears after successful `save_as()`
  auto-flush, and does not itself flush, increment
  `WorkbookEditor::pending_change_count()`, expose internal Patch state, include
  whole-`<sheetData>` replacement payloads, or update `last_edit_error()`.
- P8.386 extends `WorkbookEditorWorksheetEditSummary` /
  `WorkbookEditor::pending_worksheet_edits()` with dirty materialized-session
  diagnostics. Summaries now include `materialized_dirty`, active materialized
  sparse cell count, and materialized memory estimate for dirty
  `WorksheetEditor` sessions, while preserving source catalog order and the
  existing Patch-count semantics.
- P8.387 adds `WorkbookEditor::pending_materialized_cell_count()` and
  `WorkbookEditor::estimated_pending_materialized_memory_usage()` as
  workbook-level aggregate diagnostics over dirty materialized sessions only.
  They omit clean sessions and whole-`<sheetData>` replacement payloads, clear
  after successful `save_as()` auto-flush, survive failed `save_as()`, and do
  not flush, increment `pending_change_count()`, expose `EditPlan`, or update
  `last_edit_error()`.
- P8.388 pins public borrowed-handle lifetime around `save_as()`: successful or
  failed `WorkbookEditor::save_as()` does not invalidate an existing
  `WorksheetEditor` handle while the owning `WorkbookEditor` object is unchanged.
  The same handle can be reused for post-save small-file edits and later
  reflushes; owner move / move assignment remains the invalidation boundary.
- P8.389 adds read-only source sharedStrings materialization for
  `WorksheetEditor`: workbook-backed source `t="s"` cells are resolved through
  the existing `xl/sharedStrings.xml` part and exposed as `CellValue::text(...)`.
  Simple rich-text shared string runs are flattened to plain text, invalid or
  out-of-range indexes fail during materialization, and standalone worksheet
  XML/chunk loaders still reject `t="s"` because they have no workbook-level
  string table context. `save_as()` still projects materialized text as inline
  strings and preserves the source sharedStrings bytes; this is not
  sharedStrings rebuild, writeback, index migration, or rich-text preservation.
- P8.390 hardens the negative matrix around that read-only source
  materialization. Workbook-backed loading now has focused regressions for
  duplicate sharedStrings relationships, external/query/fragment targets,
  missing parts, wrong content types, malformed `xl/sharedStrings.xml`, and
  missing/empty/non-numeric/out-of-range indexes. These are fail-fast
  guardrails; they do not add tolerant XML repair, relationship repair,
  sharedStrings rebuild/writeback/migration, or rich-text preservation.
- P8.716 narrows dirty `WorksheetEditor` sharedStrings projection into a
  same-workbook append boundary: if the source workbook has one valid,
  appendable ordinary `<sst>` table, dirty text cells can save as stable
  `t="s"` indexes and new plain strings are appended to `xl/sharedStrings.xml`.
  If there is no source table or the table is stale, malformed, prefixed,
  count-inconsistent, or otherwise outside that safe shape, dirty save falls
  back to inline strings and never creates or repairs sharedStrings metadata.
- P8.391 pins the public facade state hygiene for invalid source sharedStrings
  metadata. `WorkbookEditor::try_worksheet()` and `worksheet()` now have public
  regressions for duplicate sharedStrings relationships, missing target parts,
  wrong content types, and malformed `xl/sharedStrings.xml`; the calls throw
  with the underlying diagnostic, preserve source/planned catalog inspection,
  leave no pending edits or dirty materialized sessions, do not update
  `last_edit_error()`, and the editor remains usable for later Patch edits.
  This still is not a public diagnostics object, repair, rebuild, writeback,
  migration, or rich-text preservation.
- P8.392 reconciles public-header and F2 planning wording with the current
  sharedStrings behavior: valid workbook-backed source `t="s"` cells are
  materialized as plain text, malformed sharedStrings structures/targets or
  invalid indexes fail fast, non-critical `count` / `uniqueCount` metadata is
  not used to drive materialization, and standalone generic worksheet XML/chunk
  loaders still reject `t="s"` due to missing workbook-level table context.
- P8.393 adds public facade state-hygiene regressions for non-default source
  style id load failures. `try_worksheet()` and `worksheet()` still throw with
  the style-id diagnostic, leave no pending edits or dirty materialized
  sessions, do not update `last_edit_error()`, and allow later Patch edits to
  save. P8.466 supersedes the former explicit-default failure boundary:
  source `s="0"` / `s='0'` now materializes as no style handle and dirty
  projection omits both forms. Later workbook-backed source style support
  validates canonical non-zero ids against source styles.xml `cellXfs` and
  preserves the same numeric ids on dirty projection; it is still not style
  migration or merge. P8.467 pins
  that this exception is exact-value only: empty, padded, signed, leading-zero,
  entity-encoded, or duplicate default-like source style attributes still fail
  instead of being coerced to default style. P8.468 lifts duplicate exact default-style
  attributes (`s="0" s="0"`) to the same public facade hygiene path with the
  parser duplicate-attribute diagnostic; this is not duplicate-attribute repair
  or style import. P8.469 pins qualified style-like attributes such as
  `x:s="0"` as unsupported source cell metadata, not default-style attributes;
  they fail through the same public facade hygiene path and do not imply
  namespace repair or namespace-aware style import. P8.470 adds public success
  coverage for the other exact XML quote form: source `s='0'` materializes as
  no style handle beside `s="0"`, and dirty projection writes neither style
  attribute form. P8.471 pins the remaining syntax boundary: XML whitespace
  around `=` is accepted when the unqualified `s` value is still exactly `0`,
  while valueless, unquoted, or unterminated default-style attributes fail
  cleanly before a partial public materialization session is created.
- P8.394 adds the same public facade state-hygiene coverage for unsupported
  source cell shapes: date-like cells (`t="d"`) and invalid boolean payloads
  throw through `try_worksheet()` / `worksheet()`
  without dirtying materialized state, updating `last_edit_error()`, or blocking
  later Patch edits. This is fail-before-handle behavior, not semantic import.
- P8.395 factors the repeated public materialization-failure hygiene assertions
  in `fastxlsx.workbook_editor` and adds malformed source worksheet XML public
  facade coverage. A missing closing worksheet root now fails cleanly through
  `try_worksheet()` / `worksheet()` without leaving partial materialized state.
  The same malformed target worksheet still blocks same-sheet Patch preflight,
  so recovery is asserted through an unrelated valid sheet rather than implying
  XML repair or same-sheet bypass.
- P8.396 adds public facade state-hygiene coverage for source cell-reference
  failures: missing cell `r` and row/cell reference mismatch throw through
  `try_worksheet()` / `worksheet()` without dirtying materialized state,
  without updating `last_edit_error()`, and without blocking later valid Patch
  edits. This is validation-only fail-before-handle behavior, not coordinate
  repair or inference.
- P8.397 pins source formula behavior at the public facade: source formula cells
  materialize as `CellValue::formula(...)`, stale cached scalar values are
  ignored on the materialized save-as projection, and malformed formula shapes
  fail through `try_worksheet()` / `worksheet()` without dirtying state. This is
  formula-text import only, not formula evaluation, shared/array formula
  support, cached-value preservation, or calcChain rebuild.
- P8.397a splits the remaining public editor implementation by semantic
  responsibility: `src/workbook_editor_state.hpp` owns private editor state and
  public projection helpers, `src/workbook_editor_worksheet_facade.cpp` owns
  `WorksheetEditor` handle behavior, and
  `src/workbook_editor_testing_hooks.cpp` owns test-only materialized-session
  hooks. `fastxlsx.workbook_editor_state` covers state projection helpers
  directly. This is internal maintainability hardening, not a new public API,
  not a behavior change, and not broad workbook editing completion.
- P8.397b hardens the workbook definedName formula diagnostic scanner by
  rejecting mismatched and unclosed workbook XML tag structure. The scanner
  still targets direct `definedNames` formula diagnostics only; it does not
  repair XML, validate the full workbook schema, evaluate formulas, rewrite
  definedNames, or build a formula dependency graph.
- P8.397c adds the first internal formula sheet-reference rewrite foundation:
  local sheet-qualified formula references and direct workbook definedName
  formula text can be rewritten by explicit old/new sheet-name rules, while
  external-workbook qualifiers, 3D sheet-range qualifiers, structured
  references, and quoted string text are skipped. Ambiguous rewrite rules fail
  instead of guessing. This is not a formula engine, not public
  `rename_sheet()` behavior, not worksheet formula-cell rewrite, and not
  calcChain rebuild.
- P8.398 adds public facade state-hygiene coverage for source inline text/XML
  entity failures: unknown XML entities, unsupported inline `<t>` attributes,
  duplicate direct inline text elements, and unknown inline string metadata
  throw through `try_worksheet()` / `worksheet()` without dirtying materialized
  state. This is strict inline-text import only, not rich text formatting
  preservation, phonetic metadata import, tolerant XML entity recovery, or XML
  repair.
- P8.474 adds public facade source inline rich text flattening: simple
  `<is><r><t>...</t></r>...</is>` runs materialize as plain text, run
  formatting is ignored, inline phonetic / extension metadata text is ignored,
  and dirty save projects the flattened text as an ordinary inline string. This
  is not rich text preservation, phonetic metadata import, extension metadata
  import, or a full rich-text object model.
- P8.475 pins malformed source inline rich text failure hygiene: mixed direct
  `<t>` and rich-run text, `rPr` outside a rich run, value wrappers inside
  `rPr`, and unclosed rich/ignored metadata fail through `try_worksheet()` /
  `worksheet()` without dirtying materialized state. This is validation-only
  fail-fast behavior, not XML repair, rich text preservation, or richer inline
  metadata import.
- P8.476 pins prefixed source sharedStrings local-name materialization:
  workbook-backed `xl/sharedStrings.xml` payloads using prefixed `sst` / `si` /
  `t` / `r` element names materialize through the public `WorksheetEditor` and
  package-backed `CellStore` paths, including simple rich-run flattening,
  clean no-op copy-original save, dirty inline projection, and source
  sharedStrings byte preservation. This is local-name matching only, not
  namespace URI validation, namespace repair, schema validation, sharedStrings
  migration/writeback, or rich text preservation.
- P8.477 pins prefixed source worksheet / inlineStr local-name
  materialization: source worksheet XML using prefixed worksheet, `sheetData`,
  row, cell, inlineStr wrapper, rich-run, formula, and value-wrapper element
  names materializes through the public `WorksheetEditor` and package-backed
  `CellStore` paths. Clean no-op save remains copy-original; dirty public save
  uses the standalone sparse-store worksheet projection and drops source
  prefixes, ignored inline phonetic / extension text, and stale cached formula
  values. This is local-name matching only, not namespace URI validation,
  namespace repair, schema validation, metadata preservation, XML repair, or a
  rich-text object model.
- P8.478 pins that this local-name behavior does not inspect element namespace
  URIs. Public `WorksheetEditor` and package-backed `CellStore` regressions now
  materialize supported worksheet and sharedStrings local-names even when those
  elements are bound to a deliberately non-spreadsheetml URI. This is
  namespace-URI non-validation evidence, not namespace-aware OpenXML support,
  namespace repair, schema validation, XML repair, or a recommendation to
  accept malformed producer output silently in broader APIs.
- P8.479 pins the reverse boundary: ignoring element namespace URIs for
  supported local-names does not relax unsupported local-name handling. Public
  `WorksheetEditor` and package-backed `CellStore` failure regressions cover
  wrong-namespace unsupported cell metadata and inline string metadata; both
  fail fast without dirtying editor/package state. This is strict local-name
  state-machine hygiene, not malformed-package tolerance or namespace repair.
- P8.480 extends that reverse boundary to `xl/sharedStrings.xml`: unsupported
  wrong-namespace sharedStrings item and rich-run local-names now fail through
  the sharedStrings parser instead of being treated as transparent containers.
  Supported simple rich runs, `rPr` formatting metadata, and ignored
  `rPh` / `phoneticPr` / `extLst` metadata remain supported. This is
  sharedStrings parser hardening, not schema validation, namespace repair, or a
  rich-text object model.
- P8.481 tightens malformed source sharedStrings rich metadata shapes: mixed
  direct `<t>` text and rich `<r>` runs, `rPr` outside a rich run, and text
  wrappers inside `rPr` fail through the public and package-backed
  materialization paths without dirtying state. Simple rich-run flattening and
  ignored formatting/phonetic/extension metadata remain unchanged. This is
  fail-fast hygiene, not rich-text preservation or tolerant mixed-mode import.
- P8.482 pins the source sharedStrings ignored metadata opacity boundary:
  nested opaque text under `rPh` / `phoneticPr` / `extLst`, including
  root-level `extLst`, is ignored and does not leak into materialized text or
  dirty inline projection, while nested `<si>` decoys and markup nested inside
  text wrappers still fail fast with no state pollution. This is not phonetic
  metadata import, extension object modeling, XML repair, or sharedStrings
  writeback.
- P8.483 applies the same opacity boundary to source inline rich text:
  opaque nested text under inline `rPh` / `phoneticPr` / `extLst` is ignored
  and omitted from dirty projection, while nested `<si>` decoys and markup
  nested inside ignored metadata text wrappers fail fast through the public and
  package-backed materialization paths. This is not inline phonetic metadata
  import, extension object modeling, XML repair, or rich-text preservation.
- P8.484 pins the self-closing and malformed-closing edge for ignored metadata:
  self-closing `rPh` / `phoneticPr` / `extLst` remains accepted as empty
  ignored metadata for both source sharedStrings and inline rich text, while
  orphan closing tags and unclosed ignored metadata fail through the public and
  package-backed paths. This is fail-fast source materialization hygiene, not
  XML repair or tolerant package recovery.
- P8.399 adds public facade state-hygiene coverage for source row/cell
  structure and numeric-payload failures: unsupported row/cell metadata
  attributes, duplicate row numbers, out-of-order row numbers, out-of-order cell
  references, and invalid numeric payloads throw through `try_worksheet()` /
  `worksheet()` without dirtying materialized state. This is strict validation
  only, not sorting, duplicate merge, metadata preservation, numeric coercion,
  or repair.
- P8.400 adds public facade state-hygiene coverage for unsupported source
  value-wrapper shapes: scalar `<v>` attributes, duplicate scalar `<v>`
  wrappers, inline-string metadata in non-inline cells, scalar `<v>` wrappers in
  `t="inlineStr"` cells, cell-internal comments / processing instructions /
  unsupported markup, and direct raw cell text outside value wrappers throw
  through `try_worksheet()` / `worksheet()` without dirtying materialized
  state. This is strict validation only, not wrapper repair, duplicate merge,
  inline/scalar coercion, direct text import, comment import, or XML repair.
- P8.401 adds public facade state-hygiene coverage for source XML/entity and
  attribute parser failures: unterminated entities, invalid/out-of-range XML
  character references, unquoted cell attributes, duplicate cell reference
  attributes, and duplicate cell type attributes throw through
  `try_worksheet()` / `worksheet()` without dirtying materialized state. This
  is strict validation only, not tolerant entity recovery, invalid character
  replacement, attribute repair, duplicate merge, same-sheet Patch bypass, or
  XML repair.
- P8.402 adds public facade state-hygiene coverage for source coordinate and
  row-number boundary failures: out-of-range cell columns/rows, zero-row cell
  references, non-column-first cell references, zero row numbers, row-number
  overflow, and non-numeric row numbers throw through `try_worksheet()` /
  `worksheet()` without dirtying materialized state. This is strict validation
  only, not coordinate inference, clamping, sorting, row-number repair,
  same-sheet Patch bypass, or XML repair.
- P8.403 adds public facade state-hygiene coverage for source row/cell
  state-machine failures: row elements outside `sheetData`, nested rows, cells
  outside rows, and nested cells throw through `try_worksheet()` /
  `worksheet()` without dirtying materialized state. This is strict validation
  only, not row/cell nesting repair, implicit row or `sheetData` scope
  inference, state-machine recovery, same-sheet Patch bypass, or XML repair.
- P8.404 adds positive public materialization coverage for supported source
  values: self-closing cells and inline-string cells without text become
  explicit blank records, `t="b"` cells become `CellValue::boolean(...)`, and
  empty inline `<t></t>` becomes `CellValue::text("")`. The save projection
  still writes through the sparse `CellStore`; this is not date support,
  Excel error-token validation,
  type coercion, style/sharedStrings migration, rich-text preservation, cached
  formula preservation, or metadata synchronization.
- P8.405 adds positive public materialization coverage for empty source
  worksheets: worksheets with no `sheetData` and worksheets with self-closing
  `<sheetData/>` materialize as empty sparse stores, remain clean until
  mutation, and can later save through the standalone CellStore worksheet
  projection. This is not XML repair, row/cell scope inference for malformed
  non-empty input, same-sheet Patch bypass, source wrapper metadata
  preservation, relationship repair, or large-file random editing.
- P8.406 adds public facade state-hygiene coverage for worksheet root and
  `sheetData` boundary failures: markup before the worksheet root, duplicate
  `sheetData`, duplicate worksheet roots, and trailing text after the worksheet
  root throw through `try_worksheet()` / `worksheet()` without dirtying
  materialized state. This is strict validation only, not XML repair, tolerant
  root recovery, duplicate merge, same-sheet Patch bypass, wrapper metadata
  preservation, or relationship repair.
- P8.407 adds public facade projection-boundary coverage for source worksheet
  wrapper metadata: `sheetPr`, `dimension`, `sheetViews`, `sheetFormatPr`,
  `cols`, and `autoFilter` beside supported cells do not block read-only
  materialization, but a later dirty save writes the standalone sparse
  CellStore projection and drops those source wrapper elements. This is not
  wrapper metadata preservation, synchronization, range recalculation,
  relationship repair, or the internal sheetData Patch preservation path.
- P8.472 extends the same dirty-projection boundary to representative
  relationship-bearing wrapper metadata: source `<hyperlinks>` and
  `<tableParts>` do not block supported cell materialization, dirty projection
  drops those worksheet XML references, and the source worksheet `.rels` plus
  linked table part remain opaque preserved package artifacts. This is not
  hyperlink/table semantic editing, relationship pruning/repair, table range
  repair, or the internal sheetData Patch preservation path.
- P8.473 extends the same dirty-projection boundary to representative
  range/reference wrapper metadata: source `<mergeCells>`,
  `<dataValidations>`, `<conditionalFormatting>`, `<ignoredErrors>`,
  `<pageMargins>`, and `<pageSetup>` do not block supported
  text/number/boolean materialization, but dirty projection drops those
  worksheet XML elements. This is not merged-cell editing,
  validation/conditional-formatting import, page setup preservation, range
  recalculation, metadata synchronization, or the internal sheetData Patch
  preservation path.
- P8.408 adds public facade projection-boundary coverage for source comments
  and processing instructions outside cells: those XML trivia nodes do not
  block supported cell materialization, but dirty save writes the standalone
  sparse CellStore projection and drops them. This is not comment import,
  processing-instruction preservation, comments-part editing, XML trivia
  preservation, relationship repair, or a change to cell-internal comment / PI
  rejection.
- P8.409 adds public no-op save coverage after read-only `WorksheetEditor`
  materialization: clean materialized sessions are not pending edits, expose no
  dirty materialized names, and `WorkbookEditor::save_as()` keeps the
  copy-original package roundtrip instead of flushing a standalone worksheet
  projection. This is not clean-session commit semantics, in-place save,
  transaction snapshot, wrapper/comment preservation during dirty projection,
  sharedStrings migration, or relationship repair.
- P8.410 adds public no-op save coverage after failed `WorksheetEditor`
  materialization: rejected non-default source style id metadata still fails
  fast, but the failed `try_worksheet()` / `worksheet()` attempts do not leave partial
  sessions, pending edits, dirty materialized names, or `last_edit_error()`
  state, and a later no-op `save_as()` remains a copy-original package write.
  This is not tolerant style import, style migration, recovery materialization,
  XML repair, semantic validation during no-op copy, or relationship repair.
- P8.414 normalizes caller-supplied explicit default `StyleId{0}` in
  `WorksheetEditor::set_cell()` values to no style handle. Public readback,
  `try_cell()`, `sparse_cells()`, and dirty save-as output all expose the
  normalized no-style cell and omit `s="0"`. This is not non-default style
  migration, existing-workbook style registry support, or style preservation.
- P8.507 pins caller-supplied non-default `StyleId` rejection in
  `WorksheetEditor::set_cell()`: the failed call updates the public edit
  diagnostic, does not mutate the sparse store, does not dirty the materialized
  session, and leaves a later no-op `save_as()` on the copy-original path. This
  is still rejection-only hygiene, not style migration, style merge, style
  preservation, or existing-workbook style registry support.
- P8.508 extends that non-default `StyleId` rejection proof to the strict A1
  `WorksheetEditor::set_cell()` overload. A valid A1 reference such as `A1`
  still routes to the same style boundary, preserves the existing sparse cell,
  keeps the materialized session clean, and leaves no-op save on the
  copy-original path. This is overload parity only, not broader A1 parsing,
  style migration, style merge, or style preservation.
- P8.509 materializes source `t="str"` cells through the same public
  `WorksheetEditor` path: scalar `<v>` payloads become text cells, `t="str"`
  formula cells keep formula text and drop stale cached values, clean no-op save
  stays copy-original, and dirty save projects text as inline strings and
  formulas as `<f>` without cached values. This is not date cell
  materialization, error-token validation, formula evaluation, cached-result preservation,
  sharedStrings/style migration, wrapper metadata preservation, XML repair, or
  large-file random editing.
- P8.510 pins the public source-load `WorksheetEditorOptions::memory_budget_bytes`
  failure path: `try_worksheet()` propagates the `CellStore` memory-budget
  diagnostic, leaves no partial materialized session, pending cells, dirty
  state, or `last_edit_error()`, and a later default-options materialization can
  still edit and save. This is sparse-store estimate hygiene, not process RSS,
  ZIP/package assembly peak accounting, or a large-file random-editing claim.
- P8.511 pins the matching mutation-side `WorksheetEditorOptions::memory_budget_bytes`
  hygiene: after exact-budget materialization, an oversized `set_cell()` insert
  fails with the `CellStore` diagnostic, updates `last_edit_error()`, leaves
  sparse and pending dirty diagnostics unchanged, and a later in-budget
  overwrite clears the diagnostic and saves. This is not workbook-level memory
  budgeting, exact RSS accounting, save-time package peak accounting, dense
  range editing, or large-file random editing.
- P8.512 pins the symmetric mutation-side `WorksheetEditorOptions::max_cells`
  hygiene: after exact-count materialization, a new-cell `set_cell()` insert
  fails with the `CellStore max_cells` diagnostic, updates `last_edit_error()`,
  leaves sparse and pending dirty diagnostics unchanged, and a later overwrite
  of an existing cell clears the diagnostic and saves. This is not row/column
  insertion, dense range editing, workbook-level budgeting, streaming random
  editing, or large-file random editing.
- P8.513 pins guardrail recovery after `erase_cell()`: exact `max_cells` and
  exact `memory_budget_bytes` sessions first reject a new-cell insertion; after
  erasing existing source-backed A2, sparse count/memory budget is released,
  the diagnostic clears, dirty materialized diagnostics reflect the smaller
  store, and a later D4 insertion saves. This is sparse-record removal only,
  not tombstones, style-preserving clear, structural row/column delete,
  metadata/range sync, workbook-level budgeting, or large-file random editing.
- P8.514 pins missing-cell `erase_cell()` after guardrail failure: exact
  `max_cells` and exact `memory_budget_bytes` insertion failures set the public
  diagnostic, a following `erase_cell("D4")` targets the still-missing rejected
  cell, clears `last_edit_error()`, leaves sparse and pending materialized
  diagnostics clean/unchanged, and no-op `save_as()` preserves source A2 while
  omitting rejected text. This is clean no-op diagnostic hygiene only, not
  tombstones, explicit blank cells, budget release, source mutation, or
  large-file random editing.
- P8.515 pins explicit blank insertion accounting: exact `max_cells` and exact
  `memory_budget_bytes` sessions reject `set_cell("D4", CellValue::blank())`
  as a new active sparse record, preserve clean sparse/pending diagnostics, and
  keep D4 missing; an existing-cell `set_cell("A1", CellValue::blank())`
  succeeds, clears the diagnostic, reads back as explicit blank, and saves as
  empty `<c r="A1"/>`. This is explicit blank sparse-record budgeting only, not
  tombstones, style-preserving clear, workbook-level budgeting, or save-time
  package memory accounting.
- P8.516 pins public mutation diagnostic replacement order: invalid A1
  `set_cell()` sets `last_edit_error()`, a later memory-budget `set_cell()`
  replaces that message with the `CellStore` guardrail diagnostic, a later
  invalid-coordinate `erase_cell()` replaces the memory diagnostic, and a final
  successful in-budget mutation clears it. All failed calls preserve clean
  sparse/pending materialized state and keep rejected payloads out of output.
  This is coarse last-error facade ordering only, not structured diagnostic
  history, save-as diagnostics, load/materialization diagnostics, or large-file
  random editing.
- P8.517 pins mixed public edit diagnostic replacement order:
  `replace_sheet_data("Missing", ...)`, invalid `rename_sheet("Data",
  "Bad/Name")`, and invalid `WorksheetEditor::set_cell("a1", ...)` replace
  each other's `last_edit_error()` messages in latest-failure order without
  dirtying editor/materialized state; a later successful public replacement
  clears the diagnostic and saves only the valid payload. This is a coarse
  public facade last-error contract, not structured diagnostic history,
  rollback, save-as/load diagnostics, relationship repair, or semantic
  dependency sync.
- P8.518 pins representative custom/unknown source cell type failure hygiene:
  source `t="z"` fails through `try_worksheet()` / `worksheet()` with the
  unsupported cell type diagnostic, leaves editor/pending/materialized state and
  `last_edit_error()` clean, and permits a later valid replacement/save. This
  is fail-fast source-shape hygiene only, not custom type import, tolerant
  fallback, date support, error-token validation, or metadata migration.
- P8.519 pins the explicit cell-internal processing-instruction failure
  branch: source `<t>a<?fastxlsx hidden?>b</t>` fails through
  `try_worksheet()` / `worksheet()` with the cell comments /
  processing-instructions / unsupported-markup diagnostic, leaves
  editor/pending/materialized state and `last_edit_error()` clean, and permits
  a later valid replacement/save. This is XML trivia fail-fast hygiene only,
  not PI import, inline text repair, or XML trivia preservation.
- P8.520 pins the matching cell-internal unsupported-markup failure branch:
  source `<t>a<![CDATA[hidden]]>b</t>` fails through `try_worksheet()` /
  `worksheet()` with the cell comments / processing-instructions /
  unsupported-markup diagnostic, leaves editor/pending/materialized state and
  `last_edit_error()` clean, and permits a later valid replacement/save. This
  is XML markup fail-fast hygiene only, not CDATA import, inline text repair,
  or XML trivia preservation.
- P8.521 pins the matching cell-internal DOCTYPE-like unsupported-markup
  failure branch: source `<t>a<!DOCTYPE fastxlsx>b</t>` fails through
  `try_worksheet()` / `worksheet()` with the same diagnostic, leaves
  editor/pending/materialized state and `last_edit_error()` clean, and permits
  a later valid replacement/save. This is XML markup-declaration fail-fast
  hygiene only, not DOCTYPE import, inline text repair, XML repair, or XML
  trivia preservation.
- P8.522 pins the adjacent true XML declaration failure branch inside source
  cell text: source `<t>a<?xml version="1.0"?>b</t>` fails through
  `try_worksheet()` / `worksheet()` with the worksheet event-reader
  late-declaration diagnostic, leaves editor/pending/materialized state and
  `last_edit_error()` clean, and permits a later valid replacement/save on an
  unrelated sheet. This is XML prolog fail-fast hygiene only, not XML
  declaration import, inline text repair, XML repair, or XML trivia
  preservation.
- P8.523 closes the adjacent direct raw cell-text gap: source
  `<c r="A1">direct-text</c>` now fails through `try_worksheet()` /
  `worksheet()` with the CellStore value-text-without-wrapper diagnostic,
  leaves editor/pending/materialized state and `last_edit_error()` clean, and
  permits a later valid replacement/save. This is value-wrapper fail-fast
  hygiene only, not direct cell text import, wrapper inference, blank coercion,
  or XML repair.
- P8.524 closes the row-level direct raw-text gap: source
  `<row r="1">direct-row-text<c ...>` now fails through `try_worksheet()` /
  `worksheet()` with the CellStore row-text-outside-cell diagnostic, leaves
  editor/pending/materialized state and `last_edit_error()` clean, and permits a
  later valid replacement/save. This is row/cell state-machine fail-fast hygiene
  only, not row text import, row repair, cell inference, metadata preservation,
  or XML repair.
- P8.525 closes the sheetData-level direct raw-text gap: source
  `<sheetData>direct-sheet-data-text<row ...>` now fails through
  `try_worksheet()` / `worksheet()` with the CellStore
  sheetData-text-outside-row diagnostic, leaves editor/pending/materialized
  state and `last_edit_error()` clean, and permits a later valid
  replacement/save. This is sheetData/row state-machine fail-fast hygiene only,
  not sheetData text import, row inference, metadata preservation, or XML
  repair.
- P8.526 closes the worksheet-root direct raw-text gap while preserving wrapper
  metadata tolerance: source `<dimension .../>direct-worksheet-text<sheetData ...>`
  now fails through `try_worksheet()` / `worksheet()` with the CellStore
  worksheet-text-outside-metadata-or-sheetData diagnostic, while text nested
  inside ignored wrapper metadata remains ignored and dropped by dirty
  projection. This is worksheet-root state-machine fail-fast hygiene only, not
  wrapper metadata text import, wrapper metadata preservation, or XML repair.
- P8.527 strengthens the shared public materialization-failure hygiene helper:
  after both `try_worksheet()` and `worksheet()` failures, replacement
  diagnostics, dirty materialized diagnostics, pending edit summaries,
  source/planned worksheet names, `worksheet_catalog()`, and
  `last_edit_error()` remain clean, and later valid replacement/save recovery
  still works. This is diagnostic evidence only, not source repair or behavior
  expansion.
- P8.528 carries the same clean-state contract through no-op `save_as()` after
  failed materialization: failed handle acquisition plus later copy-original
  save keeps replacement/materialized diagnostics, pending edit summaries,
  source/planned catalog views, and `last_edit_error()` clean while the output
  package remains byte-for-byte source-copy original. This is no-op save-as
  hygiene only, not source repair, semantic migration, or relationship repair.
- P8.529 carries the same clean-state contract through missing optional
  `try_worksheet()` lookup after a prior public edit failure: the lookup plus
  later copy-original save keeps replacement/materialized diagnostics, pending
  edit summaries, source/planned catalog views, and the prior
  `last_edit_error()` unchanged while the output package remains
  byte-for-byte source-copy original. This is missing-lookup/no-op save-as
  hygiene only, not missing-sheet creation, source repair, semantic migration,
  or relationship repair.
- P8.530 carries the same clean-state contract through throwing
  `worksheet("Missing")` lookup after a prior public edit failure: the thrown
  `FastXlsxError` identifies the missing sheet, and the later copy-original
  save keeps replacement/materialized diagnostics, pending edit summaries,
  source/planned catalog views, and the prior `last_edit_error()` unchanged.
  This is throwing-lookup/no-op save-as hygiene only, not missing-sheet
  creation, source repair, semantic migration, or relationship repair.
- P8.531 strengthens the post-recovery catalog-query path by applying a
  complete saved-materialized-session clean-state helper after read-only
  planned/source catalog queries. The regression now verifies preserved
  `last_edit_error()`, prior public edit count, empty replacement and dirty
  materialized diagnostics, empty `pending_worksheet_edits()`, unchanged
  source/planned catalog views, borrowed-handle cleanliness, and the saved
  materialized value. This is catalog-query diagnostic hygiene only, not source
  reload, catalog repair, source mutation, commit, undo, rollback, or
  diagnostic-triggered flush semantics.
- P8.532 applies the same saved-materialized-session helper to read-only
  pending-state and worksheet-catalog diagnostics after rename-back failed-save
  recovery. The regression now verifies preserved prior edit count,
  `last_edit_error()`, empty replacement/materialized diagnostics, empty
  pending edit summaries, unchanged source/planned catalog views, borrowed
  handle cleanliness, and the saved materialized value. This is
  diagnostic-query hygiene only, not diagnostic-triggered flush, source reload,
  catalog repair, source mutation, commit, undo, or rollback semantics.
- P8.533 applies the same helper to handle-level read APIs after that recovery:
  `try_cell()`, `get_cell()`, `cell_count()`, `estimated_memory_usage()`, and
  `sparse_cells()` now verify preserved prior edit count, `last_edit_error()`,
  empty replacement/materialized diagnostics, empty pending edit summaries,
  unchanged source/planned catalog views, borrowed handle cleanliness, and the
  saved materialized value. This is handle-read hygiene only, not source reload,
  catalog repair, source mutation, commit, undo, rollback, or large-file random
  editing.
- P8.534 applies the same helper to invalid handle-read failures after that
  recovery: invalid row/column reads, invalid A1 references, and invalid range
  snapshots keep sparse `cell_count()` and `estimated_memory_usage()` stable and
  preserve prior edit count, `last_edit_error()`, empty diagnostics, catalog
  views, borrowed handles, and the saved materialized value. This is invalid
  read hygiene only, not coordinate inference, clamping, source reload, catalog
  repair, source mutation, commit, undo, or rollback semantics.
- P8.535 applies the same helper to invalid handle-mutation failures after that
  recovery: invalid `set_cell()` and `erase_cell()` calls keep sparse
  `cell_count()` and `estimated_memory_usage()` stable, preserve the expected
  invalid-mutation `last_edit_error()`, keep replacement/materialized
  diagnostics and pending edit summaries empty, preserve catalog views and
  borrowed handles, and retain the saved materialized value. The later valid
  mutation still clears the diagnostic and saves. This is invalid-mutation
  hygiene only, not coordinate inference, clamping, source reload, catalog
  repair, source mutation, commit, undo, or rollback semantics.
- P8.536 applies the same helper to successful missing-cell erase no-ops after
  that recovery: valid row/column and A1 `erase_cell()` calls targeting absent
  cells clear a prior mutation diagnostic, keep sparse `cell_count()` and
  `estimated_memory_usage()` stable, keep replacement/materialized diagnostics
  and pending edit summaries empty, preserve catalog views and borrowed handles,
  and retain the saved materialized value. This is missing-erase no-op hygiene
  only, not erase tombstones, source reload, catalog repair, source mutation,
  commit, undo, rollback, or diagnostic-triggered flush semantics.
- P8.537 adds the dirty-state counterpart for positive blank/erase mutations
  after that recovery: explicit blank A1 and erased source-backed A2 now also
  prove empty edit/replacement diagnostics, dirty materialized aggregate
  counts/memory, one restored-name dirty edit summary, unchanged catalog views,
  transient-name absence, and dirty borrowed handles before save-as. This is
  helper/diagnostic hygiene for existing projection behavior only, not source
  reload, catalog repair, source mutation, commit, undo, rollback,
  sharedStrings/style migration, relationship repair, or erase tombstones.
- P8.538 applies the same dirty-state helper to positive scalar/formula
  mutations after that recovery: numeric A1, boolean A2, formula C3, and
  preserved source-backed B1 now share the same empty edit/replacement
  diagnostics, restored-name dirty aggregate counts/memory, one dirty summary,
  unchanged catalogs, transient-name absence, and dirty borrowed-handle checks
  before save-as. This is helper/diagnostic hygiene for existing projection
  behavior only, not formula evaluation, cached result generation, calcChain
  rebuild, date cell typing, source reload, catalog repair, source mutation,
  commit, undo, rollback, sharedStrings/style migration, or relationship
  repair.
- P8.539 applies the same dirty-state helper to positive text-escape mutations
  after that recovery: whitespace-preserving A1, empty text A2,
  special-character text C3, and preserved source-backed B1 now share the same
  empty edit/replacement diagnostics, restored-name dirty aggregate
  counts/memory, one dirty summary, unchanged catalogs, transient-name absence,
  and dirty borrowed-handle checks before save-as. This is helper/diagnostic
  hygiene for existing projection behavior only, not new text behavior, XML
  repair, source reload, catalog repair, source mutation, commit, undo,
  rollback, sharedStrings/style migration, or relationship repair.
- P8.540 applies the same dirty-state helper to positive max-coordinate
  mutations after that recovery: legal `XFD1048576` row/column and A1 reads,
  sparse range snapshotting, preserved source-backed B1/A2, dimension refresh,
  and sparse max-row XML output now share the same empty edit/replacement
  diagnostics, restored-name dirty aggregate counts/memory, one dirty summary,
  unchanged catalogs, transient-name absence, and dirty borrowed-handle checks
  before save-as. This is helper/diagnostic hygiene for existing projection
  behavior only, not dense allocation, max-coordinate performance evidence,
  coordinate repair, source reload, catalog repair, source mutation, commit,
  undo, rollback, sharedStrings/style migration, or relationship repair.
- P8.415 pins row/column overload coordinate guardrails for
  `WorksheetEditor::try_cell()`, `get_cell()`, `set_cell()`, and `erase_cell()`.
  Invalid row/column reads throw without updating `last_edit_error()`, invalid
  mutations update the public edit diagnostic without dirtying the sparse
  store, and the last legal Excel coordinate is accepted. This is validation
  only, not coordinate inference, clamping, dense reads, or large-file random
  access.
- P8.417 pins public matching-option session reacquire behavior for
  `WorkbookEditor::worksheet()` and `try_worksheet()`. Reacquiring the same
  planned sheet with the same `WorksheetEditorOptions` returns another borrowed
  handle to the existing materialized sparse store, preserves dirty edits,
  exposes mutations through all handles, and lets `save_as()` flush the reused
  session once. This is state hygiene only, not transaction history,
  clean-session commit semantics, or large-file random editing.
- P8.418 extends the same matching-option reacquire contract after a successful
  `save_as()`: a clean saved materialized session is still reused by later
  `worksheet()` / `try_worksheet()` calls, so callers read the flushed
  materialized values instead of reloading stale source cells; later edits
  through the reacquired handle remain visible through older handles and flush
  as a subsequent materialized handoff. This is not an in-place commit model,
  transaction history, source-package mutation, or large-file random editing.
- P8.419 pins the public dirty-diagnostic boundary around that post-save
  reacquire path: matching clean reacquire keeps
  `pending_materialized_worksheet_names()`,
  `pending_materialized_cell_count()`, and
  `estimated_pending_materialized_memory_usage()` empty/zero; a later mutation
  through the reacquired handle populates those dirty-session diagnostics, and
  the next successful `save_as()` clears them again. This is diagnostic state
  hygiene only, not a transaction or commit model.
- P8.420 pins the corresponding post-save option-mismatch failure path:
  `try_worksheet()` and `worksheet()` with different `WorksheetEditorOptions`
  still reject an existing saved materialized session, do not update
  `last_edit_error()`, do not dirty materialized diagnostics, preserve saved
  materialized values, and leave later matching-option edits/save usable.
  This is option identity hygiene only, not a dynamic session reconfiguration
  API.
- P8.421 pins `pending_worksheet_edits()` around the same post-save session
  lifecycle: dirty-only materialized summaries disappear after successful
  auto-flush, stay absent after clean matching reacquire, reappear only after a
  later mutation dirties the reused session, and clear again after the next
  successful `save_as()`. A prior materialized handoff is not reported as a
  whole-`<sheetData>` replacement summary. This is summary diagnostic hygiene
  only, not transaction history, commit state, or replacement migration.
- P8.422 pins the renamed-sheet variant of that summary lifecycle: a queued
  public rename keeps the source-order worksheet summary visible after
  materialized auto-flush, but the summary clears `materialized_dirty`,
  `materialized_cell_count`, and `estimated_materialized_memory_usage` until a
  later mutation re-dirties the reused renamed session. This is rename-context
  diagnostic hygiene only, not sheet-rename dependency repair or commit
  semantics.
- P8.423 pins the rejected-save preflight side of that renamed summary state:
  source-overwrite rejection happens before materialized auto-flush, preserves
  both the queued rename and dirty materialized summary fields, does not add a
  materialized handoff to `pending_change_count()`, and does not update
  `last_edit_error()`. This is path-preflight state hygiene only, not atomic
  save/rollback semantics.
- P8.424 pins the lower-level materialized diagnostics for the same renamed
  post-save lifecycle: dirty renamed sessions are reported under the current
  planned sheet name, aggregate cell/memory diagnostics match the borrowed
  session, successful `save_as()` and clean matching reacquire clear those
  dirty aggregates, and a later mutation re-adds them until the next save. This
  is public diagnostic hygiene only, not sheet-rename dependency repair or a
  broader commit model.
- P8.425 pins the rename-back-before-materialization variant: after a sheet is
  renamed to a temporary planned name and then back to its source name, a later
  dirty `WorksheetEditor` session reports matching source/planned names, is not
  marked `renamed`, uses the restored source name in dirty materialized
  diagnostics, and saves without leaking the transient name. This is
  current-planned diagnostic hygiene only, not undo/transaction semantics.
- P8.426 pins the failure/recovery side of that same rename-back path: an
  invalid A1 `WorksheetEditor::set_cell()` after rename-back sets
  `last_edit_error()` but keeps the materialized session clean, keeps dirty
  materialized diagnostics and summaries empty, preserves the restored planned
  catalog, and a later valid mutation recovers under the restored source name.
  This is failed-mutation state hygiene only, not broad undo or transaction
  semantics.
- P8.427 pins the rejected-save side of that same rename-back path:
  source-overwrite `save_as()` preflight happens before materialized auto-flush,
  preserves the dirty borrowed session and restored source/planned summary,
  does not add a materialized handoff count, does not create
  `last_edit_error()`, leaves the source package unchanged, and a later safe
  `save_as()` flushes the dirty edit without leaking the transient name. This
  is path-preflight state hygiene only, not atomic save/rollback semantics.
- P8.428 pins clean reacquire after that failed-save recovery: once the later
  safe `save_as()` flushes the dirty rename-back session, matching
  `worksheet("Data")` reuses the saved materialized state instead of reloading
  stale source cells, keeps dirty diagnostics and summaries empty until the
  next mutation, and can then flush another dirty edit under the restored source
  name. This is session reuse hygiene only, not source mutation, transaction
  history, or commit semantics.
- P8.429 pins the corresponding option-mismatch failure path after that
  recovery: mismatched `try_worksheet()` / `worksheet()` calls still reject the
  existing saved materialized session without updating `last_edit_error()`,
  dirtying materialized diagnostics or summaries, losing saved values, reviving
  the transient name, or blocking later matching-option mutation and save. This
  is option identity hygiene only, not dynamic session reconfiguration,
  transaction rollback, source mutation, or sheet-rename dependency repair.
- P8.430 pins the missing-lookup no-op path after that same recovery:
  `try_worksheet()` for missing names, including the old transient planned
  name, returns empty without updating `last_edit_error()`, dirtying
  materialized diagnostics or summaries, discarding or reloading the saved
  materialized value, reviving the transient name, or blocking later
  matching-option mutation and save. This is lookup hygiene only, not hidden
  sheet resurrection, source reload, dynamic catalog repair, or transaction
  semantics.
- P8.431 pins the matching missing-lookup throwing path after that recovery:
  `worksheet()` for missing names, including the old transient planned name,
  throws `FastXlsxError` without updating `last_edit_error()`, dirtying
  materialized diagnostics or summaries, discarding or reloading the saved
  materialized value, reviving the transient name, or blocking later
  matching-option mutation and save. This is throwing lookup hygiene only, not
  diagnostic-write behavior, hidden sheet resurrection, source reload, dynamic
  catalog repair, or transaction semantics.
- P8.432 pins read-only catalog queries after that same recovery:
  `worksheet_names()` / `has_worksheet()` report the restored planned catalog,
  while `source_worksheet_names()` / `has_source_worksheet()` report the opened
  source catalog, without updating `last_edit_error()`, dirtying materialized
  diagnostics or summaries, discarding or reloading the saved materialized
  value, reviving the transient name, or blocking later matching-option mutation
  and save. This is catalog inspection hygiene only, not source reload, dynamic
  catalog repair, source mutation, commit, undo, or rollback semantics.
- P8.433 pins read-only pending-state diagnostics after that recovery:
  `has_pending_changes()`, `pending_change_count()`, replacement diagnostics,
  materialized aggregate diagnostics, `has_pending_replacement()`,
  `pending_worksheet_edits()`, `worksheet_catalog()`, and `last_edit_error()`
  preserve the saved materialized session, restored source/planned mapping,
  empty dirty diagnostics, and prior public edit count without flushing,
  reloading, reviving the transient name, or blocking later matching-option
  mutation and save. This is diagnostic-query hygiene only, not
  diagnostic-triggered flush, source reload, dynamic catalog repair, source
  mutation, commit, undo, or rollback semantics.
- P8.434 pins handle-level `WorksheetEditor` reads after that recovery:
  `try_cell()`, `get_cell()`, missing-cell reads, `cell_count()`,
  `estimated_memory_usage()`, `sparse_cells()`, and `sparse_cells(range)`
  preserve the saved materialized value, unchanged source-backed cells, clean
  handles, empty dirty diagnostics, and restored planned catalog name without
  flushing, reloading stale source values, reviving the transient name, or
  blocking later matching-option mutation and save. This is handle read hygiene
  only, not read-triggered flush, source reload, dense range reads, source
  mutation, commit, undo, or rollback semantics.
- P8.435 pins invalid handle-level reads after that recovery: invalid
  row/column coordinates, invalid A1 references, and invalid
  `sparse_cells(range)` calls throw `FastXlsxError` while preserving the saved
  materialized session, clean handles, empty dirty diagnostics, empty
  `last_edit_error()`, and restored planned catalog name. A later matching
  reacquire/mutation/save still works under `Data`. This is invalid-read
  hygiene only, not read-triggered diagnostics, coordinate repair/clamping,
  source reload, source mutation, commit, undo, or rollback semantics.
- P8.436 pins invalid handle-level mutations after that recovery: invalid
  row/column and A1 `set_cell()` / `erase_cell()` calls throw `FastXlsxError`
  and update `last_edit_error()` while preserving the saved materialized
  session, clean handles, empty dirty diagnostics, and restored planned catalog
  name. A later valid mutation clears the diagnostic, writes under `Data`, and
  omits rejected invalid payloads. This is invalid-mutation hygiene only, not
  invalid-payload retention, coordinate repair/clamping, source reload, source
  mutation, commit, undo, or rollback semantics.
- P8.437 pins valid missing-cell erase no-ops after that recovery: row/column
  and A1 `erase_cell()` calls targeting absent cells clear prior edit
  diagnostics while preserving the saved materialized session, clean handles,
  empty dirty diagnostics, and restored planned catalog name. A later valid
  mutation/save still works under `Data`. This is missing-erase no-op hygiene
  only, not erase tombstones, explicit blank cells, source reload, source
  mutation, commit, undo, or rollback semantics.
- P8.438 pins positive blank/erase projection after that recovery:
  `set_cell("A1", CellValue::blank())` remains an explicit blank record and
  `erase_cell(2, 1)` removes the existing source-backed A2 record. The next
  `save_as()` writes `<c r="A1"/>`, preserves B1, omits row 2 /
  `placeholder-a2`, refreshes dimension to `A1:B1`, and does not leak the
  transient planned name. This is sparse-store projection hygiene only, not
  erase tombstones, source wrapper metadata preservation, style/sharedStrings
  migration, source reload, commit, undo, or rollback semantics.
- P8.537 strengthens that blank/erase projection with a dirty-materialized
  recovery helper: after explicit blank A1 and source-backed A2 erase, public
  diagnostics now also prove empty `last_edit_error()`, empty replacement
  diagnostics, restored-name dirty materialized aggregate count/memory, one
  dirty `pending_worksheet_edits()` summary, unchanged source/planned catalog
  views, transient-name absence, and dirty borrowed handles. This is
  dirty-state diagnostic hygiene only, not new blank/erase behavior, source
  reload, catalog repair, source mutation, commit, undo, rollback, or erase
  tombstones.
- P8.439 pins positive scalar/formula projection after that recovery: number,
  boolean, and formula mutations remain valid dirty sparse-store edits after
  the safe-save/reacquire path. The next `save_as()` writes numeric `<v>`,
  boolean `t="b"` / `1`, escaped formula `<f>` without cached value, preserves
  untouched source-backed B1, refreshes dimension to `A1:C3`, and does not leak
  the transient planned name. This is current `CellValue` projection hygiene
  only, not formula evaluation, cached formula result generation/preservation,
  calcChain rebuild, date cell typing, style/sharedStrings migration, source
  reload, commit, undo, or rollback semantics.
- P8.538 strengthens that scalar/formula projection with the dirty-materialized
  recovery helper: after numeric A1, boolean A2, formula C3, and source-backed
  B1 preservation, public diagnostics now also prove empty `last_edit_error()`,
  empty replacement diagnostics, restored-name dirty materialized aggregate
  count/memory, one dirty `pending_worksheet_edits()` summary, unchanged
  source/planned catalog views, transient-name absence, and dirty borrowed
  handles. This is dirty-state diagnostic hygiene only, not formula evaluation,
  cached result generation, calcChain rebuild, date cell typing, source reload,
  catalog repair, source mutation, commit, undo, rollback, sharedStrings/style
  migration, or relationship repair.
- P8.440 pins positive text escape projection after that recovery:
  leading/trailing-whitespace text, empty text, and special-character text
  remain valid dirty sparse-store edits after the safe-save/reacquire path. The
  next `save_as()` writes inline strings, escapes `&`, `<`, and `>`, preserves
  quotes in element text, emits `xml:space="preserve"` where required, writes
  empty text as `<t></t>`, preserves untouched source-backed B1, refreshes
  dimension to `A1:C3`, and does not leak the transient planned name. This is
  current inline-string text projection hygiene only, not rich text
  preservation, phonetic metadata preservation, sharedStrings migration or
  writeback, source wrapper metadata preservation, source reload, commit, undo,
  or rollback semantics.
- P8.539 strengthens that text-escape projection with the dirty-materialized
  recovery helper: after whitespace-preserving A1, empty text A2,
  special-character C3, and source-backed B1 preservation, public diagnostics
  now also prove empty `last_edit_error()`, empty replacement diagnostics,
  restored-name dirty materialized aggregate count/memory, one dirty
  `pending_worksheet_edits()` summary, unchanged source/planned catalog views,
  transient-name absence, and dirty borrowed handles. This is dirty-state
  diagnostic hygiene only, not new text behavior, XML repair, text
  normalization, source reload, catalog repair, source mutation, commit, undo,
  rollback, sharedStrings/style migration, or relationship repair.
- P8.540 strengthens that max-coordinate projection with the
  dirty-materialized recovery helper: after legal `XFD1048576` mutation,
  row/column and A1 reads, sparse range snapshotting, and source-backed B1/A2
  preservation, public diagnostics now also prove empty `last_edit_error()`,
  empty replacement diagnostics, restored-name dirty materialized aggregate
  count/memory, one dirty `pending_worksheet_edits()` summary, unchanged
  source/planned catalog views, transient-name absence, and dirty borrowed
  handles. This is dirty-state diagnostic hygiene only, not dense allocation,
  max-coordinate performance evidence, coordinate repair, source reload,
  catalog repair, source mutation, commit, undo, rollback, sharedStrings/style
  migration, or relationship repair.
- P8.441 pins legal maximum coordinate projection after that recovery:
  `XFD1048576` remains a valid sparse-store edit after the safe-save/reacquire
  path. It can be written via row/column max values, read back through
  row/column and A1 APIs, inspected through a one-cell range snapshot, and saved
  with dimension `A1:XFD1048576` plus a single sparse max-row record. This is
  sparse boundary correctness only, not dense row/column allocation, a
  million-row benchmark, large-file low-memory random editing, coordinate
  repair/clamping, source reload, commit, undo, or rollback semantics.
- P8.442 pins edge-record erase shrink after that recovery: after a saved
  `XFD1048576` sparse record is erased, row/column and A1 reads no longer expose
  the edge record, the max-boundary range snapshot is empty, and the next
  `save_as()` shrinks dimension to `A1:B2` while preserving remaining A1/B1/A2
  records. This is sparse dimension recomputation only, not tombstone output,
  dense allocation, row metadata repair/synthesis, source wrapper preservation,
  large-file low-memory random editing, source reload, commit, undo, or
  rollback semantics.
- P8.541 strengthens that edge-record erase shrink with the dirty-materialized
  recovery helper: after the saved `XFD1048576` record is erased, public
  diagnostics now also prove empty `last_edit_error()`, empty replacement
  diagnostics, restored-name dirty materialized aggregate count/memory, one
  dirty `pending_worksheet_edits()` summary, unchanged source/planned catalog
  views, transient-name absence, dirty borrowed handles, and reacquired-handle
  memory alignment. This is dirty-state diagnostic hygiene only, not dense
  allocation, max-coordinate performance evidence, coordinate repair,
  tombstone/style-preserving clear semantics, source reload, catalog repair,
  source mutation, commit, undo, rollback, sharedStrings/style migration, or
  relationship repair.
- P8.443 pins the strict A1 mutation overloads on the same boundary:
  `set_cell("XFD1048576", ...)` writes the last legal Excel cell after the
  safe-save/reacquire path and `erase_cell("XFD1048576")` removes it again.
  The row/column APIs observe the same saved state, the set save emits
  dimension `A1:XFD1048576`, and the erase save shrinks to `A1:B2`. This is A1
  overload parity only, not lowercase reference acceptance, range mutation,
  dense materialization, tombstone output, row metadata repair/synthesis,
  large-file low-memory random editing, source reload, commit, undo, or
  rollback semantics.
- P8.542 strengthens that strict A1 set/erase path with the
  dirty-materialized recovery helper: after the A1 max-coordinate set and after
  the following A1 max-coordinate erase, public diagnostics now also prove empty
  `last_edit_error()`, empty replacement diagnostics, restored-name dirty
  materialized aggregate count/memory, one dirty `pending_worksheet_edits()`
  summary, unchanged source/planned catalog views, transient-name absence,
  dirty borrowed handles, and the post-erase reacquired-handle memory alignment.
  This is dirty-state diagnostic hygiene only, not new A1 behavior, lowercase
  reference acceptance, range mutation, dense allocation, max-coordinate
  performance evidence, coordinate repair, tombstone/style-preserving clear
  semantics, source reload, catalog repair, source mutation, commit, undo,
  rollback, sharedStrings/style migration, or relationship repair.
- P8.444 pins explicit blank projection on the same max-coordinate boundary:
  `set_cell("XFD1048576", CellValue::blank())` remains an active sparse record,
  reads back as `CellValueKind::Blank`, saves as `<c r="XFD1048576"/>`, and
  extends dimension to `A1:XFD1048576`. A later row/column erase removes that
  blank record and shrinks the next save to `A1:B2`. This is explicit blank
  projection only, not missing-cell synthesis, tombstone output, row metadata
  repair/synthesis, dense materialization, large-file low-memory random
  editing, source reload, commit, undo, or rollback semantics.
- P8.543 strengthens that explicit blank set/erase path with the
  dirty-materialized recovery helper: after the max-coordinate blank set and
  after the following row/column max-coordinate erase, public diagnostics now
  also prove empty `last_edit_error()`, empty replacement diagnostics,
  restored-name dirty materialized aggregate count/memory, one dirty
  `pending_worksheet_edits()` summary, unchanged source/planned catalog views,
  transient-name absence, dirty borrowed handles, and the post-erase
  reacquired-handle memory alignment. This is dirty-state diagnostic hygiene
  only, not new blank behavior, missing-cell synthesis, dense allocation,
  max-coordinate performance evidence, coordinate repair,
  tombstone/style-preserving clear semantics, source reload, catalog repair,
  source mutation, commit, undo, rollback, sharedStrings/style migration, or
  relationship repair.
- P8.445 pins formula projection on the same max-coordinate boundary:
  `set_cell(1048576, 16384, CellValue::formula(...))` remains an active sparse
  formula record, reads back through row/column and A1 APIs, saves escaped
  `<f>` text at `XFD1048576`, extends dimension to `A1:XFD1048576`, and does
  not generate a cached `<v>` value. This is formula payload projection only,
  not formula evaluation, cached result generation/preservation, calcChain
  rebuild, defined-name/formula dependency rewrite, dense materialization,
  large-file low-memory random editing, source reload, commit, undo, or
  rollback semantics.
- P8.544 strengthens that formula projection with the dirty-materialized
  recovery helper: after the max-coordinate formula set, public diagnostics now
  also prove empty `last_edit_error()`, empty replacement diagnostics,
  restored-name dirty materialized aggregate count/memory, one dirty
  `pending_worksheet_edits()` summary, unchanged source/planned catalog views,
  transient-name absence, and dirty borrowed handles. This is dirty-state
  diagnostic hygiene only, not formula evaluation, cached result generation or
  preservation, calcChain rebuild, defined-name/formula dependency rewrite,
  dense allocation, max-coordinate performance evidence, coordinate repair,
  source reload, catalog repair, source mutation, commit, undo, rollback,
  sharedStrings/style migration, or relationship repair.
- P8.446 pins scalar projection on the same max-coordinate boundary:
  `set_cell(1048576, 16384, CellValue::number(...))` remains an active sparse
  numeric record and saves a scalar `<v>` at `XFD1048576`; a later
  `set_cell("XFD1048576", CellValue::boolean(false))` overwrites the same sparse
  edge record as `t="b"` / `<v>0</v>`. This is current number/boolean payload
  projection only, not date cell typing, non-finite number acceptance,
  number-format/style migration, boolean coercion, dense materialization,
  large-file low-memory random editing, source reload, commit, undo, or
  rollback semantics.
- P8.545 strengthens that scalar number/boolean projection with the
  dirty-materialized recovery helper: after the numeric max-coordinate set and
  after the following A1 boolean overwrite, public diagnostics now also prove
  empty `last_edit_error()`, empty replacement diagnostics, restored-name dirty
  materialized aggregate count/memory, one dirty `pending_worksheet_edits()`
  summary, unchanged source/planned catalog views, transient-name absence,
  dirty borrowed handles, and the post-overwrite reacquired-handle memory
  alignment. This is dirty-state diagnostic hygiene only, not date cell typing,
  non-finite number acceptance, style/number-format migration, boolean
  coercion, dense allocation, max-coordinate performance evidence, coordinate
  repair, source reload, catalog repair, source mutation, commit, undo,
  rollback, sharedStrings/style migration, or relationship repair.
- P8.447 pins saved scalar edge erase shrink on the same boundary: after the
  max-coordinate number has been saved and overwritten by a saved boolean false,
  `erase_cell(1048576, 16384)` removes the edge record, clears the max-boundary
  range snapshot, and the next save shrinks dimension to `A1:B2`. This is erase
  shrink for a saved scalar sparse record only, not tombstone output,
  scalar-to-blank conversion, row metadata repair, dense materialization,
  large-file low-memory random editing, source reload, commit, undo, or
  rollback semantics.
- P8.546 strengthens that saved scalar edge erase-shrink projection with the
  dirty-materialized recovery helper: after the saved number / saved boolean
  edge states, public diagnostics now also prove empty `last_edit_error()`,
  empty replacement diagnostics, restored-name dirty materialized aggregate
  count/memory, one dirty `pending_worksheet_edits()` summary, unchanged
  source/planned catalog views, transient-name absence, dirty borrowed handles,
  and the post-erase reacquired-handle memory alignment. This is dirty-state
  diagnostic hygiene only, not new erase behavior, tombstone output,
  scalar-to-blank conversion, style-preserving clear semantics, dense
  allocation, max-coordinate performance evidence, coordinate repair, source
  reload, catalog repair, source mutation, commit, undo, rollback,
  sharedStrings/style migration, or relationship repair.
- P8.448 pins saved formula edge erase shrink on the same boundary: after an
  escaped formula has been saved at `XFD1048576` without a cached `<v>` value,
  `erase_cell("XFD1048576")` removes the edge record, clears row/column, A1,
  and range reads, and the next save shrinks dimension to `A1:B2`. This is
  erase shrink for a saved formula sparse record only, not formula evaluation,
  cached result generation/preservation, calcChain rebuild, defined-name or
  formula dependency rewrite, tombstone output, formula-to-blank conversion,
  row metadata repair, dense materialization, large-file low-memory random
  editing, source reload, commit, undo, or rollback semantics.
- P8.547 strengthens that saved formula edge erase-shrink projection with the
  dirty-materialized recovery helper: after the saved escaped formula edge
  state and later `erase_cell("XFD1048576")`, public diagnostics now also
  prove empty `last_edit_error()`, empty replacement diagnostics, restored-name
  dirty materialized aggregate count/memory, one dirty
  `pending_worksheet_edits()` summary, unchanged source/planned catalog views,
  transient-name absence, dirty borrowed handles, and the post-erase
  reacquired-handle memory alignment. This is dirty-state diagnostic hygiene
  only, not formula evaluation, cached result generation or preservation,
  calcChain rebuild, defined-name/formula dependency rewrite, tombstone output,
  formula-to-blank conversion, style-preserving clear semantics, dense
  allocation, max-coordinate performance evidence, coordinate repair, source
  reload, catalog repair, source mutation, commit, undo, rollback,
  sharedStrings/style migration, or relationship repair.
- P8.449 keeps the workbook-editor public test budget stable by moving the
  max-coordinate public regression family into
  `fastxlsx.workbook_editor.public-edge`; this is CTest organization only, not
  a product API or runtime behavior change.
- P8.450 pins fresh source-backed max-coordinate materialization:
  an existing source worksheet `XFD1048576` inline-string record is
  materialized cleanly, read through row/column and A1 APIs, preserved by a
  read-only no-op copy-original `save_as()`, and removable through
  `erase_cell("XFD1048576")` so the next dirty projection shrinks to `A1:B2`.
  This is sparse source materialization only, not dense allocation, source
  reload, source wrapper preservation after dirty projection, row metadata
  repair, large-file low-memory random editing, tombstone output, commit, undo,
  or rollback semantics.
- P8.451 pins the same source-backed edge for formula cells:
  an existing source `XFD1048576` formula is materialized as
  `CellValueKind::Formula`, stale cached scalar `<v>` values are ignored by the
  materialized value, read-only no-op `save_as()` still copies source bytes
  including the cached value, and `erase_cell("XFD1048576")` removes the edge
  formula so the next dirty projection shrinks to `A1:B2` without the formula
  or cached scalar. This is not formula evaluation, cached result generation or
  dirty-projection preservation, calcChain rebuild, dependency rewrite, source
  reload, dense allocation, large-file low-memory random editing, commit, undo,
  or rollback semantics.
- P8.452 pins the same source-backed edge for workbook sharedStrings cells:
  an existing source `XFD1048576` `t="s"` cell is resolved through
  `xl/sharedStrings.xml` and materialized as `CellValueKind::Text`, read-only
  no-op `save_as()` still copies source bytes including source shared-string
  indexes, and `erase_cell("XFD1048576")` removes the edge so the next dirty
  projection shrinks to `A1:B2` with remaining text written as inline strings
  while preserving the source sharedStrings part bytes. This is not
  sharedStrings rebuild, writeback, pruning, index migration, rich text
  preservation, relationship repair, source reload, dense allocation,
  large-file low-memory random editing, commit, undo, or rollback semantics.
- P8.453 pins the same source-backed edge for scalar and blank cells:
  existing source number, boolean, and explicit blank cells at `XFD1048576`
  materialize through row/column and A1 APIs, read-only no-op `save_as()` still
  copies source bytes, and `erase_cell("XFD1048576")` removes each edge so the
  next dirty projection shrinks to `A1:B2` without the erased edge payload.
  This is not dense allocation, source reload, wrapper preservation after dirty
  projection, row metadata repair, coordinate repair, tombstone output, blank
  conversion, large-file low-memory random editing, commit, undo, or rollback
  semantics.
- P8.454 pins the same source-backed edge for empty inline-string cells:
  existing source `t="inlineStr"` cells at `XFD1048576` with empty `<t></t>`
  materialize as empty text, inlineStr cells with `<is/>` and no text
  materialize as blank, read-only no-op `save_as()` still copies source bytes,
  and `erase_cell("XFD1048576")` removes each edge so the next dirty projection
  shrinks to `A1:B2` without the erased edge payload. This is not rich text
  preservation, phonetic metadata import, inline/scalar coercion, XML repair,
  source reload, dense allocation, large-file low-memory random editing,
  commit, undo, or rollback semantics.
- P8.455 pins the same source-backed edge for simple rich sharedStrings:
  an existing source `XFD1048576` `t="s"` cell can point at a shared string item
  with multiple rich text runs and still materialize as flattened plain text;
  phonetic and extension metadata text is ignored, read-only no-op `save_as()`
  still copies source bytes including rich sharedStrings markup, and
  `erase_cell("XFD1048576")` removes the edge so the next dirty projection
  shrinks to `A1:B2` while preserving source `xl/sharedStrings.xml` bytes. This
  is not rich text preservation, phonetic metadata import, extension metadata
  import, sharedStrings rebuild, writeback, pruning, index migration,
  relationship repair, source reload, dense allocation, large-file low-memory
  random editing, commit, undo, or rollback semantics.
- P8.456 pins source sharedStrings `xml:space` whitespace at the public facade:
  plain shared-string text and simple rich shared-string runs with
  `xml:space="preserve"` materialize with leading/trailing whitespace intact,
  clean no-op `save_as()` still copies source bytes, and a later dirty save
  projects the flattened text as inline strings with `xml:space="preserve"`
  where needed while preserving the source `xl/sharedStrings.xml` bytes. This
  is not source sharedStrings writeback, pruning, index migration, rich text
  preservation, relationship repair, large-file low-memory random editing,
  commit, undo, or rollback semantics.
- P8.457 pins malformed sharedStrings item/rich-run structure failure hygiene at
  the public facade: text outside `<t>`, nested `<si>`, nested markup inside
  `<t>`, and mismatched closing tags fail through `try_worksheet()` /
  `worksheet()` without dirtying materialized state or blocking later valid
  Patch edits. This is not XML repair, tolerant schema recovery, broader
  rich-text support, sharedStrings migration/rebuild/writeback, relationship
  repair, large-file low-memory random editing, commit, undo, or rollback
  semantics.
- P8.458 pins malformed sharedStrings XML/entity/attribute failure hygiene at
  the public facade: unknown or unterminated XML entities, out-of-range
  character references, attributes without values, unquoted attribute values,
  and truncated tags caused by unterminated attributes fail through
  `try_worksheet()` / `worksheet()` without dirtying materialized state or
  blocking later valid Patch edits. The parser validates generic tag attribute
  syntax but does not add XML repair, schema validation, attribute whitelisting,
  sharedStrings migration/rebuild/writeback, relationship repair, large-file
  low-memory random editing, commit, undo, or rollback semantics.
- P8.548 strengthens the lazy malformed sharedStrings XML public facade
  diagnostics by reusing the shared materialization-failure hygiene helper when
  the selected failing sheet is `Shared` and the recovery edit targets `Data`.
  Both `try_worksheet("Shared")` and `worksheet("Shared")` prove the
  root-missing-`sst` diagnostic, empty dirty materialized/replacement state,
  preserved source/planned catalogs, unchanged `last_edit_error()`, no target
  replacement leakage, and later valid `replace_sheet_data("Data", ...)`
  save-as usability. This is diagnostic/test-helper hygiene only, not parser
  behavior expansion, XML repair, schema validation, attribute whitelisting,
  relationship repair, sharedStrings migration/rebuild/writeback, source
  reload, commit, undo, rollback, or public API.
- P8.549 strengthens the lazy missing sharedStrings target public facade
  diagnostics with the same helper: after the workbook relationship target is
  changed to missing `missingSharedStrings.xml`, non-`t="s"` `Data`
  materialization and dirty inline save-as remain valid, while both
  `try_worksheet("Shared")` and `worksheet("Shared")` prove the missing-target
  diagnostic, empty dirty materialized/replacement state, preserved catalogs,
  unchanged `last_edit_error()`, no target replacement leakage, and later valid
  `replace_sheet_data("Data", ...)` save-as usability. This is
  diagnostic/test-helper hygiene only, not relationship repair, target repair,
  sharedStrings synthesis/rebuild/writeback/pruning/index migration, source
  reload, commit, undo, rollback, or public API.
- P8.459 pins malformed sharedStrings relationship target failure hygiene at
  the public facade: external targets, query-qualified targets,
  fragment-qualified targets, incomplete or invalid percent escapes, decoded
  null bytes, and package-root escapes fail through `try_worksheet()` /
  `worksheet()` without dirtying materialized state or blocking later valid
  Patch edits. This is relationship-target fail-fast evidence only, not
  relationship repair, URI repair, external target materialization,
  sharedStrings migration/rebuild/writeback, large-file low-memory random
  editing, commit, undo, or rollback semantics.
- P8.460 pins source sharedStrings non-critical metadata behavior on the
  positive path: inconsistent root `count` / `uniqueCount` values and otherwise
  well-formed unknown attributes on `sst` / `si` / `r` / `t` do not drive
  materialization. The public editor follows actual `<si>` order/text, keeps
  clean no-op save copy-original, and dirty save still projects inline strings
  while preserving the source `xl/sharedStrings.xml` bytes. This is not
  sharedStrings schema validation, count repair, attribute whitelisting,
  sharedStrings migration/rebuild/writeback, large-file low-memory random
  editing, commit, undo, or rollback semantics.
- P8.461 pins the absent sharedStrings optional-dependency boundary for
  supported non-`t="s"` source cells: a workbook with no `xl/sharedStrings.xml`,
  no workbook sharedStrings relationship, and no sharedStrings content type can
  still materialize supported blank/boolean/inline cells. Dirty save continues
  to write inline strings and does not create a sharedStrings part,
  relationship, content type, or worksheet shared-string indexes. This is
  absence preservation only, not lazy repair of malformed declared
  sharedStrings relationships, sharedStrings migration/rebuild/writeback,
  large-file low-memory random editing, commit, undo, or rollback semantics.
- P8.462 pins lazy selected-worksheet sharedStrings resolution: a workbook can
  carry a stale sharedStrings relationship and still materialize/save a
  selected sheet that contains only supported non-`t="s"` cells, while a
  selected sheet with actual shared string indexes still triggers the same
  fail-fast sharedStrings target/content/XML/index validation. Dirty save
  preserves the stale source relationship and `xl/sharedStrings.xml` bytes; it
  does not repair, prune, migrate, rebuild, or write back sharedStrings.
- P8.463 extends that lazy boundary to representative malformed workbook
  metadata: duplicate sharedStrings relationships are bypassed for selected
  non-`t="s"` sheets but still fail fast for selected sheets with shared string
  indexes. Dirty save preserves the duplicate relationship bytes; it does not
  repair, prune, migrate, rebuild, or write back sharedStrings.
- P8.550 strengthens the lazy duplicate sharedStrings relationship public
  facade diagnostics with the shared materialization-failure hygiene helper:
  non-`t="s"` `Data` materialization and dirty inline save-as still preserve
  duplicate relationship bytes, while both `try_worksheet("Shared")` and
  `worksheet("Shared")` prove the multiple-relationships diagnostic, empty
  dirty materialized/replacement state, preserved catalogs, unchanged
  `last_edit_error()`, no target replacement leakage, and later valid
  `replace_sheet_data("Data", ...)` save-as usability. This is diagnostic
  hygiene only, not duplicate relationship repair/pruning, target repair,
  sharedStrings synthesis/rebuild/writeback/pruning/index migration, source
  reload, commit, undo, rollback, or public API.
- P8.464 extends the same on-demand boundary to malformed sharedStrings table
  XML: selected non-`t="s"` sheets do not parse or repair a malformed
  `xl/sharedStrings.xml`, but selected sheets with shared string indexes still
  fail fast on the malformed payload. Dirty save preserves the malformed
  sharedStrings bytes; it does not repair, prune, migrate, rebuild, or write
  back sharedStrings.
- P8.465 extends the same on-demand boundary to wrong sharedStrings content
  type metadata: selected non-`t="s"` sheets do not validate or repair the
  sharedStrings part content type, but selected sheets with shared string
  indexes still fail fast before materializing. Dirty save preserves the wrong
  content type metadata; it does not repair, prune, migrate, rebuild, or write
  back sharedStrings.
- P8.551 strengthens the lazy wrong sharedStrings content type public facade
  diagnostics with the shared materialization-failure hygiene helper:
  non-`t="s"` `Data` materialization and dirty inline save-as still preserve the
  wrong content type metadata, while both `try_worksheet("Shared")` and
  `worksheet("Shared")` prove the wrong-content-type diagnostic, empty dirty
  materialized/replacement state, preserved catalogs, unchanged
  `last_edit_error()`, no target replacement leakage, source package
  immutability, and later valid `replace_sheet_data("Data", ...)` save-as
  usability. This is diagnostic/test-helper hygiene only, not content type
  repair, relationship repair/pruning, target repair, sharedStrings
  synthesis/rebuild/writeback/pruning/index migration, source reload, source
  mutation, commit, undo, rollback, or public API.
- P8.466 normalizes source explicit default style attributes: selected
  worksheet cells with exact `s="0"` / `s='0'` materialize as unstyled values
  and dirty projection omits both forms; later source-style passthrough support
  validates canonical non-zero unsigned decimal source style ids against the
  source styles.xml `cellXfs` table, materializes them, and writes the same
  numeric ids back when the source styles part is preserved. This is not style
  id migration, style merge, stylesheet semantic editing, or formatting parity.
- P8.467 pins the strict token boundary for that default-style exception:
  selected source cells accept exact `s="0"` / `s='0'` only; empty,
  leading-zero, signed, whitespace-padded, entity-encoded, and duplicate
  default-like source style attributes remain fail-fast materialization
  failures. This is not numeric coercion, XML entity decoding for style ids,
  style preservation, style migration, style merge, or stylesheet validation.
- P8.468 adds public facade hygiene for duplicate source style attributes:
  duplicate `s="0" s="0"` fails through `try_worksheet()` / `worksheet()`
  without creating a partial materialized session, dirtying the editor, updating
  `last_edit_error()`, or blocking a later valid Patch edit. This is parser
  strictness, not duplicate-attribute repair or style deduplication.
- P8.469 pins qualified source style-like attributes as unsupported cell
  metadata: `x:s="0"` is rejected through the public facade without partial
  materialization, dirty state, `last_edit_error()` updates, or later Patch edit
  blockage. This is not namespace repair, namespace-aware style import, or a
  broad metadata tolerance policy.
- P8.470 lifts single-quoted exact default-style attributes into public success
  coverage: `s='0'` materializes as no style handle beside `s="0"`, and dirty
  projection omits both quote forms. This is exact XML quote-form support, not
  tolerant numeric parsing, whitespace trimming, entity decoding, or style
  import.
- P8.471 pins the source style attribute syntax boundary: `s = "0"` stays
  accepted because the attribute value is exactly `0`, while valueless
  `s`, unquoted `s=0`, and unterminated style attribute syntax fail through the
  same source-load/public-facade hygiene path. This is strict XML attribute
  parsing, not tolerant style-id coercion, XML repair, or style import.
- The exposed mutation semantics remain intentionally narrow: `erase_cell()`
  removes the sparse record, `CellValue::blank()` is the explicit blank
  replacement cell, and non-default `StyleId` is rejected instead of being
  preserved, migrated, or merged.
- Do not widen the API just because the facade is now open. Add public methods
  only for a concrete caller workflow with Doxygen, tests, and documented
  failure semantics.

Historical public-header audit after P8.310:
- This gate was true before P8.378: public headers exposed only the
  `WorkbookEditor` Patch facade. It is now superseded for `WorksheetEditor`,
  `WorksheetEditorOptions`, `WorkbookEditor::worksheet()`, `try_cell()`,
  `set_cell()` and `erase_cell()`, which are current public API. P8.379 further
  supersedes it for `WorkbookEditor::try_worksheet()` and `WorksheetEditor::get_cell()`.
- `WorkbookEditor` move construction and move assignment are documented as
  ownership transfer only. They transfer or replace the opened source package,
  replacement-payload guardrails, planned catalog, queued public edits,
  diagnostics, and `last_edit_error()` state without save/commit/close
  semantics; move assignment discards old target-side queued state instead of
  merging it.
- The no-op move-construction path is covered: moving a clean opened editor
  keeps catalog inspection, empty diagnostics, and no-op `save_as()` source
  package roundtrip behavior on the moved-to editor.
- Move assignment into a moved-from target is covered: the target can receive a
  valid assigned editor session with queued public edits and diagnostics, then
  save the assigned state, instead of remaining permanently moved-from.
- Move construction with `WorkbookEditorOptions` is covered: replacement
  guardrails such as `max_replacement_cells` survive ownership transfer and
  continue to reject oversized replacement payloads before pending state is
  queued. The same moved-to boundary is covered for
  `replacement_memory_budget_bytes`, including no-op save-as state hygiene after
  a guarded replacement failure.
- Move assignment with `WorkbookEditorOptions` is covered: the assigned source
  editor's replacement guardrails replace the target editor's prior guardrails,
  so target-side options are not retained or merged after assignment. The same
  replacement boundary is covered for `replacement_memory_budget_bytes`.
- Assignment from a default-options source editor is covered: previously strict
  target-side `max_replacement_cells` and `replacement_memory_budget_bytes`
  guardrails are cleared rather than retained.
- Assignment from an already moved-from source editor is covered: the target is
  left moved-from / not open, target-side queued replacement, rename,
  diagnostics, options, and `last_edit_error()` state are discarded, and
  operations that require an opened workbook throw instead of saving stale
  target state.
- P8.371 adds internal materialized-session move/save evidence: a dirty
  test-hook-only materialized session plus a queued cross-sheet public
  replacement survive `WorkbookEditor` move construction / move assignment, can
  be explicitly flushed after assignment, and save the assigned source state
  without leaking the discarded target editor's materialized or public queued
  edits. This remains internal evidence, not public `WorksheetEditor` handle
  lifetime support.
- P8.372 adds internal materialized-session save-as failure hygiene: failed
  `save_as()` preflight before explicit flush preserves dirty private
  materialized state without queuing a projection, and failed `save_as()` after
  explicit flush preserves the staged projection and clean private state. A
  later valid `save_as()` still writes the materialized payload.
- P8.373 adds the retry-mutation variant: after a materialized projection has
  been flushed and `save_as()` fails, the same private session can be modified
  again, re-flushed, and the later projection replaces the earlier staged
  worksheet payload.
- P8.374 combines the move and retry boundaries: a moved / move-assigned dirty
  materialized session with a queued cross-sheet public edit can be explicitly
  flushed, survive a failed `save_as()`, be mutated again, and re-flushed. The
  later materialized projection saves beside the assigned public edit without
  leaking discarded target-editor state.
- P8.375 adds the successful-save sibling: after an explicitly flushed private
  materialized projection is saved successfully, the same editor/session can be
  modified again, re-flushed, and saved to a second output path. The later
  output uses the newer projection without rewriting the earlier output
  artifact.
- P8.376 combines ownership transfer with successful-save reuse: a moved /
  move-assigned materialized session plus assigned cross-sheet public edit can
  flush, save successfully, mutate again, re-flush, and save a second output.
  The second output uses the later projection, the first output artifact remains
  unchanged, and discarded target-editor state does not leak.
- P8.377 adds the moved-from assignment negative: assigning from a moved-from
  `WorkbookEditor` source clears the target editor's private materialized
  sessions, dirty materialized state, queued public edits, replacement
  diagnostics, and `last_edit_error()` instead of saving stale target state. The
  prior moved-to holder remains valid and can still flush/save its materialized
  payload.
- The current `set_cell()` / `erase_cell()` declarations live only in
  `include/fastxlsx/detail/cell_store.hpp`; they are internal sparse-store
  helpers and must not be treated as public editor API.
- Existing public `worksheet` mentions are current `Workbook::add_worksheet()`,
  `WorkbookWriter::add_worksheet()`, `WorkbookEditor::has_worksheet()`, and
  source/planned catalog diagnostics, not a materializing random-edit handle.

Public `WorksheetEditor` header preflight checklist after P8.371:
- Public-header gate must still return no matches for `WorksheetEditor`,
  `WorksheetEditorOptions`, `WorkbookEditor::worksheet()`,
  `WorkbookEditor::try_worksheet()`, `get_cell()`, `set_cell()`, and
  `erase_cell()` until the implementation task deliberately opens the header.
- Existing `WorkbookEditor` Doxygen must keep random cell read/write, public
  `PackageEditor`, relationship repair, sharedStrings/style migration,
  formula evaluation, calcChain rebuild, and range metadata recalculation listed
  as non-goals.
- Same-sheet operation mixing remains reject-first: dirty or flushed
  materialized sessions must not silently accept public
  `replace_sheet_data()` / `rename_sheet()` for that sheet.
- Cross-sheet public operations may remain allowed only with tests proving they
  clear stale public edit diagnostics without dropping dirty materialized state
  or staged projections.
- Public diagnostics must stay split: `last_edit_error()` is edit-operation
  state, not materialization/load state.
- The first implementation task may add public symbols only together with
  public tests for handle reacquire after move, option matching, set / explicit
  blank / erase projection, save-as path guard hygiene, dimension refresh, and
  package preservation.
- Internal P8.371 move/save evidence reduces the private state-risk around
  `WorkbookEditor` ownership transfer, but it does not replace the required
  public handle reacquire / invalidation tests once `WorksheetEditor` exists.
- Internal P8.372 save-as failure evidence reduces the private staged-state
  risk, but public `WorksheetEditor` still needs explicit public tests for
  save-as path guard failures and recovery.
- Internal P8.373 retry evidence reduces the private reflush/retry risk after
  failed saves, but it still does not define public automatic flush-on-save or
  public handle transaction semantics.
- Internal P8.374 evidence reduces the combined ownership-transfer plus retry
  risk, but it still does not define public `WorksheetEditor` move/reacquire
  semantics or public transaction guarantees.
- Internal P8.375 evidence reduces the successful-save reuse risk, but it still
  does not define public commit/close/undo semantics or automatic flush-on-save.
- Internal P8.376 evidence reduces the combined ownership-transfer plus
  successful-save reuse risk, but it still does not define public
  `WorksheetEditor` move/reacquire, commit, close, or transaction semantics.
- Internal P8.377 evidence reduces moved-from assignment cleanup risk, but it
  still does not define public materialized handle invalidation semantics.

Draft `WorksheetEditor` first public slice:
- Mode: In-memory / existing-workbook small-file editing. It is not a
  Streaming writer extension and not a low-memory large worksheet editor.
- Entry point: `WorkbookEditor::worksheet(name, options)` materializes exactly
  the requested current-planned worksheet, and `try_worksheet(name, options)`
  returns an empty handle / optional for missing names. `WorkbookEditor::open()`
  must not load all worksheet cells by default.
- Options: the first public design should separate current
  `WorkbookEditorOptions` replacement-payload limits from future
  `WorksheetEditorOptions` materialization / mutation limits. The options are
  passed per `worksheet(name, options)` / `try_worksheet(name, options)` call,
  not stored in `WorkbookEditor::open()`. Initial field names are `max_cells`
  and `memory_budget_bytes`; unset values are implementation defaults, not
  stable capacity promises.
- First methods: `name()`, `try_cell(row, column)`, `set_cell(row, column,
  CellValue)`, `set_cell_value(row, column, CellValue)`,
  `set_row_values(row, span<const CellValue>)`,
  `set_column_values(column, span<const CellValue>)`,
  `clear_cell_value(row, column)`, `clear_row(row)`,
  `clear_rows(first_row, last_row)`, `clear_column(column)`,
  `clear_columns(first_column, last_column)`, `clear_cell_values(CellRange)`,
  `erase_cell(row, column)`, `cell_count()`, and
  `estimated_memory_usage()`. A string A1-reference overload can be added as a
  convenience only if it reuses the same coordinate validation and failure
  semantics. Resolved by P8.379: `get_cell()` is included and throws on missing
  cells; it must not blur missing with explicit blank.
- Read semantics: `try_cell()` returning empty means missing sparse record;
  returning `CellValue::blank()` means an explicit blank record exists.
- Mutation semantics: `set_cell()` replaces the active value;
  `set_cell_value()` / `set_cell_values()` / `set_row_values()` /
  `set_column_values()` replace only values while preserving the current source
  style handle on overwritten targets; row/column value-prefix writes leave
  prefix-outside sparse cells untouched and insert missing prefix cells without
  styles; `clear_cell_value()` / `clear_row()` / `clear_rows()` /
  `clear_column()` / `clear_columns()` / `clear_cell_values(CellRange)` write
  style-preserving explicit blanks for existing represented cells; `erase_cell()`
  removes the sparse record; `CellValue::blank()` writes an explicit blank
  replacement cell. Tombstones and metadata edits are still deferred; later
  represented sparse row/column insert/delete helpers do not change the broader
  metadata-sync boundary.
- Style policy: until an existing-file style registry / validation story exists,
  the first public `WorksheetEditor` design should either reject non-default
  `StyleId` on `set_cell()` or document it as a caller-supplied raw workbook
  style id with no validation. Do not claim style migration or merge.
- Source dependency policy: workbook-backed source cells using valid shared
  string indexes are materialized through the existing `xl/sharedStrings.xml`
  as plain text. Declared sharedStrings `count` / `uniqueCount` and
  well-formed unknown sharedStrings attributes are non-driving metadata; actual
  `<si>` order/text is used. Prefixed source sharedStrings `sst` / `si` / `t`
  / `r` element names are matched by local-name for materialization; this is
  not namespace URI validation or repair. Element namespace URIs are not
  inspected on this current local-name path. Prefixed source worksheet,
  `sheetData`, row, cell, inlineStr wrapper, rich-run, formula, and
  value-wrapper element names are also matched by local-name for read-only
  materialization. Source style ids, unsupported cell types,
  unsupported metadata, malformed references, XML/entity/parser failures,
  invalid sharedStrings relationship targets, malformed sharedStrings
  XML/entity/attribute syntax or item structures, and invalid shared string
  indexes remain load failures before a caller-visible worksheet editor is
  returned.
- Save-as handoff: materialized cell edits must still save through
  `WorkbookEditor::save_as(output_path)`. Unmodified entries and unknown entries
  remain copy-original; modified worksheet cells flow through a worksheet
  rewrite / `sheetData` handoff that records sharedStrings, styles, formulas,
  calc metadata, range metadata, worksheet `.rels`, and linked-part policy as
  preserve / audit / fail. This remains a blocker until covered by focused
  public tests.
- Mixing rules: the first public slice should avoid ambiguous state by rejecting
  whole-sheet `replace_sheet_data()` or `rename_sheet()` operations on a sheet
  after a `WorksheetEditor` for that sheet has been materialized, unless the
  implementation can prove handle lifetime and pending-edit migration
  semantics.
- Failure hygiene: load, read preflight, set, erase, and save-as preflight
  failures must be failure-before-state-change. A failure must not partially
  materialize a worksheet, partially mutate sparse records, or silently drop
  pending edits.

Draft `WorksheetEditor` acceptance matrix:

| Area | First-slice decision | Required evidence before header |
| --- | --- | --- |
| API mode | In-memory / existing-workbook small-file editing only. | Doxygen and README wording must not describe large worksheet low-memory random access. |
| Materialization | `worksheet(name, options)` explicitly loads one current-planned worksheet. | Missing sheet, unsupported source payload, and load-limit failures return before a caller-visible editor exists. |
| Options | Use separate future load/materialization options, not current replacement-payload-only `WorkbookEditorOptions`. | Tests must prove `max_cells` and `memory_budget_bytes` apply while loading and while mutating the materialized store. |
| Handle lifetime | First slice should reject ambiguous mixing: no `rename_sheet()` or whole-sheet `replace_sheet_data()` on a materialized sheet unless handle invalidation is fully specified. | Public tests must cover rejected mixing without changing pending edits or materialized cells. |
| Coordinates | Row/column overloads are the primary contract; A1 string overloads are optional follow-up convenience. | Coordinate validation failures must preserve existing sparse records. |
| Read API | `try_cell()` returns empty for missing records; `get_cell()` throws on missing records. | Tests distinguish missing from explicit blank. |
| Mutation API | `set_cell()` overwrites the active record; `erase_cell()` removes it; `CellValue::blank()` writes explicit blank. | Tests must prove no-state-pollution on invalid coordinates, limit failures, and unsupported values. |
| Styles | Current first slice: both `WorksheetEditor::set_cell()` overloads accept no-style/default `StyleId{0}` values and reject non-default `StyleId` values until a style registry / validation story exists. | Public tests prove row/column and A1 rejection do not mutate or dirty the store and no-op save stays copy-original; docs must keep saying no style migration or merge. |
| Source dependencies | Valid workbook-backed shared string indexes materialize as text, including prefixed source sharedStrings `sst` / `si` / `t` / `r` element names matched by local-name, `xml:space` whitespace, and simple rich shared string runs flattened to plain text; ignored sharedStrings metadata text under `rPh` / `phoneticPr` / `extLst` remains non-materialized even with opaque nested markup, while nested `<si>` decoys and markup inside text wrappers remain fail-fast; declared source sharedStrings `count` / `uniqueCount` values and otherwise well-formed unknown sharedStrings attributes are ignored as non-critical metadata while actual `<si>` order/text drives materialization; workbooks with no sharedStrings part can materialize supported non-`t="s"` cells and dirty save does not create sharedStrings metadata; stale or duplicate sharedStrings relationship metadata, malformed sharedStrings table payloads, and wrong sharedStrings content type metadata are resolved lazily and only block selected worksheets that actually contain `t="s"` cells; source explicit default style attributes whose unqualified `s` value is exactly `0` (`s="0"`, `s='0'`, or `s = "0"`) materialize as no style handle and dirty projection omits those forms; canonical non-zero unsigned decimal source style ids materialize as numeric passthrough handles and dirty projection writes them back when the source styles part is preserved; empty, valueless, unquoted, unterminated, padded, signed, leading-zero, entity-encoded, duplicate, and qualified source style attributes remain load failures; source formula cells materialize as formula text while cached values are ignored, including default/numeric, `t="str"`, `t="b"`, and `t="e"` cached-result formula cells; known source formula metadata attributes `t` / `ref` / `si` / `aca` / `ca` / `bx` / `dt2D` / `dtr` / `del1` / `del2` / `r1` / `r2` are accepted only as lossy source metadata; source shared formula definitions and source-order followers materialize as plain formula text, with followers using a narrow A1-style / whole-axis relative-reference translator that honors `$` absolute row/column anchors, translates whole-row/whole-column ranges, emits `#REF!` for out-of-bounds translated references, and skips double-quoted strings, quoted sheet-name tokens, and bracketed external/structured-reference tokens; metadata-only shared formula cells without a resolved source-order definition can still materialize supported cached scalar `<v>` values, while formula elements that never resolve to a materializable value remain load failures; formula text is projected as plain `<f>text</f>` without preserving shared formula metadata or cached formula results; materialized formula sheet qualifiers can now be reported through a public read-only audit, but not rewritten; source `t="str"` scalar cells without formulas materialize as text; source `t="e"` scalar error cells materialize as opaque `CellValueKind::Error` tokens and dirty projection writes `t="e"` `<v>` payloads without validating Excel error-token semantics; source error cells with missing or empty `<v>` remain load failures; source blank, boolean, plain inline string, empty inline string, simple inline rich-text run cells, ignored inline `rPh` / `phoneticPr` / `extLst` metadata text including opaque nested markup, source cell `ph` phonetic markers, and prefixed worksheet / `sheetData` / row / cell / inlineStr / rich-run / formula / value-wrapper element names materialize into current `CellValue` variants by local-name; element namespace URIs are not inspected on this local-name materialization path, so supported local-names bound to a non-spreadsheetml URI may still materialize; unsupported local-names remain fail-fast even when bound to a non-spreadsheetml URI, including unsupported sharedStrings item/rich-run local-names; malformed sharedStrings rich metadata such as mixed direct/rich text, `rPr` outside a run, or text inside `rPr` remains load-failure behavior; malformed inline rich text metadata, including nested `<si>` decoys or markup inside ignored metadata text wrappers, still fails fast; empty source worksheets materialize as empty sparse stores; worksheet wrapper metadata, representative relationship-bearing wrapper metadata, representative range/reference wrapper metadata, and cell-external comments / processing instructions beside supported cells may be ignored during read-only materialization but are dropped by dirty standalone projection; source worksheet `.rels` / linked table parts may remain as opaque preserved package artifacts and are not pruned or repaired; malformed source style attributes, unknown source formula attributes, empty formula text, unsupported cell types such as date/custom type tokens, invalid missing or empty error cell values, unsupported metadata, malformed references, XML/entity/parser failures, worksheet root / `sheetData` boundary failures, row/cell structure failures, row/cell state-machine failures, unsupported value-wrapper shapes, invalid numeric payloads, and invalid sharedStrings metadata/relationship targets/indexes/structures/XML entities/attributes remain load failures. | P8.389-P8.411, P8.452-P8.506, P8.509, P8.518, P8.519, P8.520, P8.521, P8.522, P8.523, P8.561, P8.567, P8.568, P8.569, P8.570, and P8.571 cover sharedStrings materialization, rich sharedStrings flattening, source sharedStrings whitespace projection, prefixed sharedStrings local-name materialization, ignored sharedStrings metadata opacity, prefixed worksheet/inlineStr local-name materialization, ignored inline metadata opacity, namespace-URI non-validation on supported local-names, wrong-namespace unsupported local-name failure hygiene including sharedStrings item/rich-run local-names, malformed sharedStrings item/rich-run structure and rich metadata failure hygiene, sharedStrings XML/entity/attribute and XML declaration/PI placement/grammar failure hygiene, sharedStrings relationship target failure hygiene, source sharedStrings non-critical metadata boundary, absent sharedStrings optional-dependency boundary, selected-sheet lazy sharedStrings dependency boundary, representative duplicate relationship lazy boundary, malformed table payload lazy boundary, wrong content type lazy boundary, source explicit default style normalization, source style id numeric passthrough, strict style-token boundary, duplicate style-attribute public facade hygiene, qualified style-like metadata rejection, single-quoted exact default-style public success, style attribute syntax boundary, source formula materialization, lossy source formula metadata handling, known formula metadata attribute compatibility, formula cached-result type materialization, source error cell materialization, metadata-only shared formula cached-scalar materialization, source shared formula follower translation, raw sheet qualifier scanner spans for later formula dependency audit, public materialized formula-reference audit for renamed-sheet risk, source cell `ph` phonetic marker ignoring, source `t="str"` scalar/formula string materialization, source blank/boolean/empty-inline materialization, source inline rich-text flattening, malformed source inline rich metadata failure hygiene, empty source worksheet materialization, wrapper metadata dirty-projection boundary, relationship-bearing wrapper metadata dirty-projection/no-pruning boundary, range/reference wrapper metadata dirty-projection boundary, cell-external comment / PI dirty-projection boundary, clean read-only materialized no-op copy-original save, failed materialization no-op copy-original save, missing `try_worksheet()` no-op copy-original save, and representative public facade failure hygiene for invalid sharedStrings metadata, malformed source style attributes, unsupported source cell shapes including custom `t="z"` tokens, malformed source XML, malformed source cell references, malformed formula shapes, inline text/XML entity failures, row/cell ordering or metadata failures, invalid numeric payloads, unsupported value-wrapper / direct raw cell text / cell-internal comment, processing-instruction, CDATA, DOCTYPE-like unsupported-markup, and XML-declaration-after-root failures, XML/entity/attribute parser failures, source coordinate / row-number boundary failures, source row/cell state-machine failures, and worksheet root / `sheetData` boundary failures; remaining unsupported dependency groups still need focused public facade evidence before broadening behavior. |
| Source dependency addenda | Keep detailed source materialization notes in the summary below instead of further lengthening this matrix row. P8.484 additionally pins self-closing ignored metadata as the legal empty case and orphan/unclosed ignored metadata as malformed for source sharedStrings and inline rich text. P8.495 pins legal source sharedStrings XML declaration forms: version-only declarations, single-quoted attributes, `version="1.1"`, `standalone="no"`, and valid encoding token punctuation remain accepted without charset transcoding. P8.496 pins standalone declaration value hygiene: duplicate standalone metadata, empty standalone values, and values other than `yes` or `no` remain malformed. P8.497 rejects case-varied XML-like processing-instruction targets such as `<?XML ...?>` as reserved target decoys instead of ordinary PI trivia. P8.498 pins `<?xml-stylesheet ...?>` as ordinary ignored PI trivia, with no stylesheet import or interpretation. P8.499 rejects malformed ordinary PI tokens missing the `?>` terminator. P8.500 rejects ordinary PI tokens with an empty target such as `<? ?>`. P8.501 rejects ordinary PI targets that start with an obviously invalid ASCII name-start character such as `-`. P8.502 rejects ordinary PI targets not followed by whitespace or immediate `?>`, such as `<?target?data?>`. P8.503 rejects ordinary PI targets containing obviously invalid ASCII name continuation characters such as `^`. P8.504 pins legal ASCII target continuation characters, including `.`, `-`, digits, and `:`, as still accepted ordinary PI trivia. P8.505 pins legal ASCII target starts, including `_` and `:`, as still accepted ordinary PI trivia. P8.506 pins empty-data ordinary PI tokens where the target is followed immediately by `?>`, such as `<?fastxlsx?>`, as still accepted ordinary PI trivia. | This addendum is documentation hygiene only; it does not broaden source tolerance, XML repair, namespace validation, rich-text preservation, sharedStrings/style migration, or relationship repair. |
| Source dependency addenda P8.524 | P8.524 additionally pins direct source row raw text outside cells as a public materialization failure: `<row r="1">direct-row-text<c ...>` fails before a worksheet handle is returned and does not poison later unrelated valid save-as recovery. | This is row/cell state-machine fail-fast hygiene only; it does not import row text, infer cells, repair rows, preserve metadata, repair XML, or broaden wrapper-metadata tolerance. |
| Source dependency addenda P8.525 | P8.525 additionally pins direct source sheetData raw text outside rows as a public materialization failure: `<sheetData>direct-sheet-data-text<row ...>` fails before a worksheet handle is returned and does not poison later unrelated valid save-as recovery. | This is sheetData/row state-machine fail-fast hygiene only; it does not import sheetData text, infer rows, repair sheetData, preserve metadata, repair XML, or broaden wrapper-metadata tolerance. |
| Source dependency addenda P8.526 | P8.526 additionally pins direct worksheet-root raw text outside wrapper metadata or `sheetData` as a public materialization failure, while preserving the existing behavior that text nested inside ignored wrapper metadata is ignored and later dropped by dirty projection. | This is worksheet-root state-machine fail-fast hygiene only; it does not import wrapper metadata text, preserve wrapper metadata, repair XML, or broaden comment/PI import. |
| Source dependency addenda P8.527 | P8.527 strengthens the shared public materialization-failure hygiene helper so `try_worksheet()` and `worksheet()` failures also prove replacement diagnostics, materialized diagnostics, pending edit summaries, source/planned worksheet names, `worksheet_catalog()`, and `last_edit_error()` remain clean before later valid recovery. | This is diagnostic evidence for existing fail-fast behavior only; it does not add source repair, metadata preservation, relationship repair, sharedStrings/styles migration, or new public API. |
| Source dependency addenda P8.528 | P8.528 extends that complete clean-state check through the later no-op `save_as()` copy-original path after failed materialization, while keeping the byte-level source-entry copy assertion. | This is no-op save-as diagnostic hygiene only; it does not repair source XML, migrate sharedStrings/styles, recalculate metadata, prune relationships, or add public API. |
| Source dependency addenda P8.529 | P8.529 extends the complete clean-state check to missing optional `try_worksheet()` lookup after a prior public edit failure, proving the lookup and later no-op `save_as()` preserve replacement/materialized diagnostics, pending edit summaries, source/planned catalog views, and the prior `last_edit_error()` while keeping byte-level source-entry copy output. | This is missing-lookup/no-op save-as hygiene only; it does not create sheets, repair source XML, migrate sharedStrings/styles, recalculate metadata, prune relationships, or add public API. |
| Source dependency addenda P8.530 | P8.530 extends the complete clean-state check to missing throwing `worksheet()` lookup after a prior public edit failure, proving the exception identifies the missing sheet and the later no-op `save_as()` preserves replacement/materialized diagnostics, pending edit summaries, source/planned catalog views, and the prior `last_edit_error()` while keeping byte-level source-entry copy output. | This is throwing-lookup/no-op save-as hygiene only; it does not create sheets, repair source XML, migrate sharedStrings/styles, recalculate metadata, prune relationships, or add public API. |
| Source dependency addenda P8.531 | P8.531 applies a complete saved-materialized-session clean-state helper to the rename-back failed-save recovery catalog-query path, proving read-only planned/source catalog queries preserve prior edit count, `last_edit_error()`, replacement/materialized diagnostics, pending edit summaries, catalog views, borrowed handles, and the saved cell value. | This is catalog-query diagnostic hygiene only; it does not reload source data, repair catalogs, mutate source packages, commit, undo, rollback, flush diagnostics, or add public API. |
| Source dependency addenda P8.532 | P8.532 applies the same complete saved-materialized-session clean-state helper to read-only pending-state and worksheet-catalog diagnostics after rename-back failed-save recovery, proving they preserve prior edit count, `last_edit_error()`, replacement/materialized diagnostics, pending edit summaries, catalog views, borrowed handles, and the saved cell value. | This is diagnostic-query hygiene only; it does not reload source data, repair catalogs, mutate source packages, commit, undo, rollback, flush diagnostics, or add public API. |
| Source dependency addenda P8.533 | P8.533 applies the same complete saved-materialized-session clean-state helper to handle-level read APIs after rename-back failed-save recovery, proving `try_cell()`, `get_cell()`, `cell_count()`, `estimated_memory_usage()`, and `sparse_cells()` preserve prior edit count, `last_edit_error()`, replacement/materialized diagnostics, pending edit summaries, catalog views, borrowed handles, and the saved cell value. | This is handle-read hygiene only; it does not reload source data, repair catalogs, mutate source packages, commit, undo, rollback, flush diagnostics, enable dense worksheet reads, or add public API. |
| Source dependency addenda P8.534 | P8.534 applies the same complete saved-materialized-session clean-state helper to invalid handle-read failures after rename-back failed-save recovery, proving invalid row/column reads, invalid A1 references, and invalid range snapshots preserve sparse counts/memory, prior edit count, `last_edit_error()`, replacement/materialized diagnostics, pending edit summaries, catalog views, borrowed handles, and the saved cell value. | This is invalid-read hygiene only; it does not infer or clamp coordinates, reload source data, repair catalogs, mutate source packages, commit, undo, rollback, flush diagnostics, or add public API. |
| Source dependency addenda P8.535 | P8.535 applies the same complete saved-materialized-session clean-state helper to invalid handle-mutation failures after rename-back failed-save recovery, proving invalid `set_cell()` / `erase_cell()` calls preserve sparse counts/memory, the expected invalid-mutation `last_edit_error()`, replacement/materialized diagnostics, pending edit summaries, catalog views, borrowed handles, and the saved cell value; the next valid mutation still clears the diagnostic and saves. | This is invalid-mutation hygiene only; it does not infer or clamp coordinates, reload source data, repair catalogs, mutate source packages, commit, undo, rollback, flush diagnostics, or add public API. |
| Source dependency addenda P8.536 | P8.536 applies the same complete saved-materialized-session clean-state helper to successful missing-cell erase no-ops after rename-back failed-save recovery, proving valid row/column and A1 `erase_cell()` calls targeting absent cells clear a prior mutation diagnostic while preserving sparse counts/memory, replacement/materialized diagnostics, pending edit summaries, catalog views, borrowed handles, and the saved cell value. | This is missing-erase no-op hygiene only; it does not create erase tombstones, reload source data, repair catalogs, mutate source packages, commit, undo, rollback, flush diagnostics, or add public API. |
| Source dependency addenda P8.537 | P8.537 adds a dirty-materialized recovery helper to the positive blank/erase projection after rename-back failed-save recovery, proving explicit blank A1 and erased source-backed A2 keep empty edit/replacement diagnostics, restored-name dirty materialized counts/memory, one dirty pending edit summary, unchanged catalog views, transient-name absence, and dirty borrowed handles before save-as. | This is dirty-state diagnostic hygiene only; it does not add new blank/erase behavior, source reload, catalog repair, source mutation, commit, undo, rollback, erase tombstones, sharedStrings/style migration, relationship repair, or public API. |
| Source dependency addenda P8.538 | P8.538 reuses the dirty-materialized recovery helper for the positive scalar/formula projection after rename-back failed-save recovery, proving numeric A1, boolean A2, formula C3, and preserved source-backed B1 keep empty edit/replacement diagnostics, restored-name dirty materialized counts/memory, one dirty pending edit summary, unchanged catalog views, transient-name absence, and dirty borrowed handles before save-as. | This is dirty-state diagnostic hygiene only; it does not add formula evaluation, cached result generation/preservation, calcChain rebuild, date cell typing, source reload, catalog repair, source mutation, commit, undo, rollback, sharedStrings/style migration, relationship repair, or public API. |
| Source dependency addenda P8.539 | P8.539 reuses the dirty-materialized recovery helper for the positive text-escape projection after rename-back failed-save recovery, proving whitespace-preserving A1, empty text A2, special-character text C3, and preserved source-backed B1 keep empty edit/replacement diagnostics, restored-name dirty materialized counts/memory, one dirty pending edit summary, unchanged catalog views, transient-name absence, and dirty borrowed handles before save-as. | This is dirty-state diagnostic hygiene only; it does not add new text behavior, XML repair, text normalization, source reload, catalog repair, source mutation, commit, undo, rollback, sharedStrings/style migration, relationship repair, or public API. |
| Source dependency addenda P8.540 | P8.540 reuses the dirty-materialized recovery helper for the positive max-coordinate projection after rename-back failed-save recovery, proving legal `XFD1048576`, sparse range snapshotting, preserved source-backed B1/A2, dimension refresh, and sparse max-row XML output keep empty edit/replacement diagnostics, restored-name dirty materialized counts/memory, one dirty pending edit summary, unchanged catalog views, transient-name absence, and dirty borrowed handles before save-as. | This is dirty-state diagnostic hygiene only; it does not add dense allocation, max-coordinate performance evidence, coordinate repair, source reload, catalog repair, source mutation, commit, undo, rollback, sharedStrings/style migration, relationship repair, or public API. |
| Source dependency addenda P8.541 | P8.541 reuses the dirty-materialized recovery helper for the max-coordinate erase-shrink projection after rename-back failed-save recovery, proving erased `XFD1048576`, empty edge sparse range, preserved A1/B1/A2, dimension shrink, and reacquired-handle memory alignment keep empty edit/replacement diagnostics, restored-name dirty materialized counts/memory, one dirty pending edit summary, unchanged catalog views, transient-name absence, and dirty borrowed handles before save-as. | This is dirty-state diagnostic hygiene only; it does not add dense allocation, max-coordinate performance evidence, coordinate repair, tombstones, style-preserving clear semantics, source reload, catalog repair, source mutation, commit, undo, rollback, sharedStrings/style migration, relationship repair, or public API. |
| Source dependency addenda P8.542 | P8.542 reuses the dirty-materialized recovery helper for strict A1 max-coordinate mutations after rename-back failed-save recovery, proving `set_cell("XFD1048576", ...)` expands the sparse projection and `erase_cell("XFD1048576")` shrinks it again while both states keep empty edit/replacement diagnostics, restored-name dirty materialized counts/memory, one dirty pending edit summary, unchanged catalog views, transient-name absence, dirty borrowed handles, and post-erase reacquired-handle memory alignment before save-as. | This is dirty-state diagnostic hygiene only; it does not add lowercase reference acceptance, range mutation, dense allocation, max-coordinate performance evidence, coordinate repair, tombstones, style-preserving clear semantics, source reload, catalog repair, source mutation, commit, undo, rollback, sharedStrings/style migration, relationship repair, or public API. |
| Source dependency addenda P8.543 | P8.543 reuses the dirty-materialized recovery helper for explicit blank max-coordinate mutations after rename-back failed-save recovery, proving `set_cell("XFD1048576", CellValue::blank())` creates an active blank edge record and row/column `erase_cell(1048576, 16384)` shrinks it again while both states keep empty edit/replacement diagnostics, restored-name dirty materialized counts/memory, one dirty pending edit summary, unchanged catalog views, transient-name absence, dirty borrowed handles, and post-erase reacquired-handle memory alignment before save-as. | This is dirty-state diagnostic hygiene only; it does not add new blank behavior, missing-cell synthesis, dense allocation, max-coordinate performance evidence, coordinate repair, tombstones, style-preserving clear semantics, source reload, catalog repair, source mutation, commit, undo, rollback, sharedStrings/style migration, relationship repair, or public API. |
| Source dependency addenda P8.544 | P8.544 reuses the dirty-materialized recovery helper for formula max-coordinate mutations after rename-back failed-save recovery, proving `set_cell(1048576, 16384, CellValue::formula(...))`, row/column and A1 readback, sparse edge snapshots, formula XML escaping, dimension expansion, recalculation request, and no cached value output keep empty edit/replacement diagnostics, restored-name dirty materialized counts/memory, one dirty pending edit summary, unchanged catalog views, transient-name absence, and dirty borrowed handles before save-as. | This is dirty-state diagnostic hygiene only; it does not add formula evaluation, cached result generation or preservation, calcChain rebuild, defined-name/formula dependency rewrite, dense allocation, max-coordinate performance evidence, coordinate repair, source reload, catalog repair, source mutation, commit, undo, rollback, sharedStrings/style migration, relationship repair, or public API. |
| Source dependency addenda P8.545 | P8.545 reuses the dirty-materialized recovery helper for scalar max-coordinate mutations after rename-back failed-save recovery, proving `set_cell(1048576, 16384, CellValue::number(...))`, A1 boolean overwrite, row/column and A1 readback, sparse edge snapshots, dimension expansion, numeric/boolean XML output, and post-overwrite memory alignment keep empty edit/replacement diagnostics, restored-name dirty materialized counts/memory, one dirty pending edit summary, unchanged catalog views, transient-name absence, and dirty borrowed handles before save-as. | This is dirty-state diagnostic hygiene only; it does not add date cell typing, non-finite number acceptance, style/number-format migration, boolean coercion, dense allocation, max-coordinate performance evidence, coordinate repair, source reload, catalog repair, source mutation, commit, undo, rollback, sharedStrings/style migration, relationship repair, or public API. |
| Source dependency addenda P8.546 | P8.546 reuses the dirty-materialized recovery helper for saved scalar max-coordinate erase-shrink after rename-back failed-save recovery, proving a saved `CellValue::number(...)`, saved boolean overwrite, row/column `erase_cell(1048576, 16384)`, removed A1/row-column readback, empty sparse edge snapshots, dimension shrink, scalar payload omission, and post-erase memory alignment keep empty edit/replacement diagnostics, restored-name dirty materialized counts/memory, one dirty pending edit summary, unchanged catalog views, transient-name absence, and dirty borrowed handles before save-as. | This is dirty-state diagnostic hygiene only; it does not add new erase behavior, tombstone output, scalar-to-blank conversion, style-preserving clear semantics, dense allocation, max-coordinate performance evidence, coordinate repair, source reload, catalog repair, source mutation, commit, undo, rollback, sharedStrings/style migration, relationship repair, or public API. |
| Source dependency addenda P8.547 | P8.547 reuses the dirty-materialized recovery helper for saved formula max-coordinate erase-shrink after rename-back failed-save recovery, proving a saved escaped `CellValue::formula(...)` at `XFD1048576` with no cached value, A1 `erase_cell("XFD1048576")`, removed A1/row-column readback, empty sparse edge snapshots, dimension shrink, formula payload omission, and post-erase memory alignment keep empty edit/replacement diagnostics, restored-name dirty materialized counts/memory, one dirty pending edit summary, unchanged catalog views, transient-name absence, and dirty borrowed handles before save-as. | This is dirty-state diagnostic hygiene only; it does not add formula evaluation, cached result generation or preservation, calcChain rebuild, defined-name/formula dependency rewrite, tombstone output, formula-to-blank conversion, style-preserving clear semantics, dense allocation, max-coordinate performance evidence, coordinate repair, source reload, catalog repair, source mutation, commit, undo, rollback, sharedStrings/style migration, relationship repair, or public API. |
| Source dependency addenda P8.548 | P8.548 reuses the shared materialization-failure hygiene helper for lazy malformed sharedStrings XML where `Shared` contains `t="s"` cells and `Data` remains the valid recovery target, proving both `try_worksheet("Shared")` and `worksheet("Shared")` expose the root-missing-`sst` diagnostic, keep dirty materialized/replacement diagnostics empty, preserve source/planned catalogs and `last_edit_error()`, avoid target replacement leakage, and still allow later `replace_sheet_data("Data", ...)` save-as. | This is diagnostic/test-helper hygiene only; it does not add parser behavior expansion, XML repair, schema validation, attribute whitelisting, relationship repair, sharedStrings rebuild/writeback/pruning/index migration, source reload, source mutation, commit, undo, rollback, or public API. |
| Source dependency addenda P8.549 | P8.549 reuses the shared materialization-failure hygiene helper for lazy missing sharedStrings target metadata where the workbook relationship points at missing `missingSharedStrings.xml`, proving non-`t="s"` `Data` materialization and dirty inline save-as remain valid, while both `try_worksheet("Shared")` and `worksheet("Shared")` expose the missing-target diagnostic, keep dirty materialized/replacement diagnostics empty, preserve catalogs and `last_edit_error()`, avoid target replacement leakage, and still allow later `replace_sheet_data("Data", ...)` save-as. | This is diagnostic/test-helper hygiene only; it does not add relationship repair, target repair, sharedStrings synthesis/rebuild/writeback/pruning/index migration, source reload, source mutation, commit, undo, rollback, or public API. |
| Source dependency addenda P8.550 | P8.550 reuses the shared materialization-failure hygiene helper for lazy duplicate sharedStrings relationship metadata, proving non-`t="s"` `Data` materialization and dirty inline save-as preserve duplicate relationship bytes, while both `try_worksheet("Shared")` and `worksheet("Shared")` expose the multiple-relationships diagnostic, keep dirty materialized/replacement diagnostics empty, preserve catalogs and `last_edit_error()`, avoid target replacement leakage, and still allow later `replace_sheet_data("Data", ...)` save-as. | This is diagnostic/test-helper hygiene only; it does not add duplicate relationship repair/pruning, target repair, sharedStrings synthesis/rebuild/writeback/pruning/index migration, source reload, source mutation, commit, undo, rollback, or public API. |
| Source dependency addenda P8.551 | P8.551 reuses the shared materialization-failure hygiene helper for lazy wrong sharedStrings content type metadata, proving non-`t="s"` `Data` materialization and dirty inline save-as preserve the wrong content type metadata, while both `try_worksheet("Shared")` and `worksheet("Shared")` expose the wrong-content-type diagnostic, keep dirty materialized/replacement diagnostics empty, preserve catalogs and `last_edit_error()`, avoid target replacement leakage, preserve source package bytes, and still allow later `replace_sheet_data("Data", ...)` save-as. | This is diagnostic/test-helper hygiene only; it does not add content type repair, relationship repair/pruning, target repair, sharedStrings synthesis/rebuild/writeback/pruning/index migration, source reload, source mutation, commit, undo, rollback, or public API. |
| Source dependency addenda P8.561 | P8.561 materializes source shared formula definitions and source-order followers through `WorksheetEditor` and package-backed `CellStore`: definitions keep formula text, followers import translated plain formula text, read-only materialization stays clean, and dirty save writes ordinary `<f>...</f>` without stale cached follower values. Internal/public/QA coverage also pins multiple followers per `si`, interleaved shared formula indexes, latest source-order definition behavior for later followers, `$` anchors, range endpoint translation, whole-row/whole-column range translation, function/name-like token boundaries, structured-reference non-rewrites, skipped quoted/bracketed tokens, invalid `si` forms, and `#REF!` output for out-of-bounds relative references. The narrow scanner/translator is factored into internal `detail/formula` and covered by the dedicated `fastxlsx.formula` CTest target. | This is lossy source formula materialization and scanner reuse only; it does not preserve shared formula metadata, evaluate formulas, build a dependency graph, rebuild calcChain, migrate sharedStrings/styles, repair relationships, or implement a complete Excel formula parser. |
| Source dependency QA addenda P8.562 | P8.562 adds an opt-in workbook-editor formula fixture scanner to `tools/run_workbook_editor_qa.py`: it maps workbook sheet names to worksheet XML parts, records formula/shared-formula counts, and runs materialized edit smoke on formula-bearing sheets selected from caller-provided fixture roots. The current local xlnt shared-only run covers `18_formulae.xlsx:Sheet1` with 15 formula elements, 3 shared formula elements, 1 definition, and 2 metadata-only followers; dirty output records 15 formula elements and 0 shared formula metadata elements on the renamed target sheet. | This is local QA coverage only; it does not vendor xlnt/OpenXLSX fixtures, add runtime dependencies, broaden CTest/CI defaults, preserve shared formula metadata, evaluate formulas, repair relationships, migrate sharedStrings/styles, or implement a complete Excel formula parser. |
| Source dependency QA addenda P8.563 | P8.563 adds `generated_shared_formula_boundary_materialization` to the opt-in workbook-editor QA runner. The generated source drives public `WorksheetEditor::try_cell()` and dirty save through shared formula definitions/followers containing sheet-qualified A1 refs, quoted sheet names, quoted strings, bracketed external tokens, structured-reference-like text, whole-row/whole-column refs, function names, name-like tokens, and R1C1-like text. The ZIP/XML and `openpyxl` checks prove the dirty output has ordinary formula elements and 0 shared formula metadata elements while preserving the documented narrow translation boundaries, including whole-row/whole-column range translation. | This is generated QA evidence only; it does not broaden formula parsing beyond the documented materializer, evaluate formulas, preserve shared formula metadata, validate sheet/external references, build a formula dependency graph, rebuild calcChain, repair relationships, migrate sharedStrings/styles, or add default CI fixture data. |
| Source dependency QA addenda P8.564 | P8.564 adds `generated_shared_formula_office_like_materialization` to the opt-in workbook-editor QA runner. The generated source patches a writer workbook into a closer Office/LibreOffice-style shared formula shape with 2D `ref` ranges, multiple `si` groups on the same worksheet, ordinary formulas and scalar values interleaved with shared formula followers, and stale cached formula results. The C++ QA tool verifies public `WorksheetEditor::try_cell()` materialization stays clean before mutation, and the ZIP/XML plus `openpyxl` checks prove dirty output has 11 ordinary formula elements, 0 shared formula metadata elements, and no stale cached `<v>` values. | This is generated QA evidence only; it does not preserve shared formula metadata, evaluate formulas, validate the `ref` range, build a formula dependency graph, rebuild calcChain, migrate sharedStrings/styles, repair relationships, vendor office fixtures, or add default CI fixture data. |
| Source dependency addenda P8.565 | P8.565 promotes the Office-like shared formula shape into the default public `WorkbookEditor` CTest path. `fastxlsx.workbook_editor.source-success` now covers 2D shared formula ranges, multiple `si` groups, ordinary formula interleaving, clean read-only `WorksheetEditor` materialization, dirty projection to ordinary formula XML, stale cached value removal, and untouched sheet preservation. | This is default regression coverage for the current lossy materialization contract; it still does not preserve shared formula metadata, evaluate formulas, validate `ref` membership, rebuild calcChain, migrate sharedStrings/styles, repair relationships, or claim full Excel formula parsing. |
| Source dependency QA addenda P8.587 | P8.587 strengthens the generated shared-formula QA reports without changing runtime semantics. `tools/run_workbook_editor_qa.py` now records exact checked formula cells, output formula cells, `openpyxl` formula readback, shared metadata removal, cached formula value removal, stale cached-value cleanup, and the Excel UI smoke classification for the basic, Office-like, and synthetic boundary generated shared-formula scenarios. | This is report-level evidence hardening only; it does not evaluate formulas, generate cached results, rebuild calcChain, preserve shared formula metadata, repair relationships, migrate sharedStrings/styles, or implement a complete Excel formula parser. |
| Source dependency QA addenda P8.588 | P8.588 adds `generated_formula_rename_rewrite` to the opt-in workbook-editor QA runner. The generated source exercises public `rename_sheet("Data", "RenamedData", RewriteDefinedNamesAndMaterializedWorksheetFormulas)` after materializing only the `Formula` sheet, then ZIP/XML, `openpyxl`, and Excel COM verify that direct local definedNames and already-materialized worksheet formulas are rewritten while external-workbook references, 3D sheet-range references, string literals, non-materialized worksheet formulas, and calcChain absence stay on the documented boundary. | This is generated QA evidence for the explicit policy only; it does not change default `rename_sheet()`, evaluate formulas, generate cached results, rebuild calcChain, scan/rewrite non-materialized worksheet formulas, repair relationships, migrate sharedStrings/styles, or implement a complete Excel formula parser. |
| Source dependency QA addenda P8.589 | P8.589 adds `generated_formula_rename_default_audit` to the opt-in workbook-editor QA runner. The generated source uses the same formula/definedName workbook shape but calls default `rename_sheet("Data", "RenamedData")` after materializing only the `Formula` sheet; ZIP/XML, `openpyxl`, and Excel COM verify the workbook catalog rename while direct local definedNames, already-materialized worksheet formulas, external-workbook references, 3D sheet-range references, string literals, non-materialized worksheet formulas, and calcChain absence remain unchanged, and the tool report still exposes stale source-name rename-risk audits. | This is generated QA evidence protecting the documented catalog-only default; it does not add silent default formula repair, evaluate formulas, generate cached results, rebuild calcChain, scan/rewrite non-materialized worksheet formulas, repair relationships, migrate sharedStrings/styles, or implement a complete Excel formula parser. |
| Source dependency QA addenda P8.590 | P8.590 adds `generated_formula_rename_defined_names_only` to the opt-in workbook-editor QA runner. The generated source uses the same formula/definedName workbook shape but calls `rename_sheet("Data", "RenamedData", RewriteDefinedNames)` after materializing only the `Formula` sheet; ZIP/XML, `openpyxl`, and Excel COM verify the workbook catalog and direct local definedName rewrites while already-materialized worksheet formulas, external-workbook references, 3D sheet-range references, string literals, non-materialized worksheet formulas, and calcChain absence remain unchanged, and the tool report still exposes stale worksheet-formula rename-risk audits. | This is generated QA evidence for the middle explicit policy only; it does not rewrite worksheet formulas under `RewriteDefinedNames`, evaluate formulas, generate cached results, rebuild calcChain, scan/rewrite non-materialized worksheet formulas, repair relationships, migrate sharedStrings/styles, or implement a complete Excel formula parser. |
| Source dependency QA addenda P8.591 | P8.591 adds `generated_formula_rename_escaped_sheet_name` to the opt-in workbook-editor QA runner. The generated source exercises public `rename_sheet("Data", "Renamed & O'Brien", RewriteDefinedNamesAndMaterializedWorksheetFormulas)` after materializing only the `Formula` sheet; ZIP/XML, `openpyxl`, and Excel COM verify workbook catalog XML escaping, definedName XML text escaping, quoted formula qualifiers with doubled apostrophes, preserved external-workbook / 3D / string-literal / non-materialized references, and calcChain absence. | This is generated QA evidence for escaped sheet-name handling in the narrow explicit policy only; it does not broaden default `rename_sheet()`, evaluate formulas, generate cached results, rebuild calcChain, scan/rewrite non-materialized worksheet formulas, repair relationships, migrate sharedStrings/styles, or implement a complete Excel formula parser. |
| Source dependency QA addenda P8.592 | P8.592 adds `generated_formula_rename_chain_rewrite` to the opt-in workbook-editor QA runner. The generated source queues `rename_sheet("Data", "TemporaryData")` with the default catalog-only behavior, then queues `rename_sheet("TemporaryData", "FinalData", RewriteDefinedNamesAndMaterializedWorksheetFormulas)` after materializing only the `Formula` sheet; ZIP/XML, `openpyxl`, and Excel COM verify that original source-name references and current planned-name references are rewritten in direct definedNames and already-materialized worksheet formulas while external-workbook references, 3D sheet-range references, string literals, non-materialized worksheet formulas, and calcChain absence stay on the documented boundary. | This is generated QA evidence for chained rename handling in the narrow explicit policy only; it does not add a semantic formula engine, broaden default `rename_sheet()`, evaluate formulas, generate cached results, rebuild calcChain, scan/rewrite non-materialized worksheet formulas, repair relationships, migrate sharedStrings/styles, or implement a complete Excel formula parser. |
| Source dependency addenda P8.566 | P8.566 pins array/dataTable formula metadata on the default public `WorkbookEditor` path. Source `<f t="array">` and `<f t="dataTable">` cells with formula text materialize as plain `CellValue::formula(...)`, metadata-only array/dataTable cells can materialize from supported cached scalar `<v>` values, read-only materialization stays clean, and dirty projection writes plain formula/scalar cells without preserving array/dataTable metadata or stale cached formula values. | This is lossy source materialization only; it does not implement dynamic array spill semantics, data table recalculation, shared/array formula metadata preservation, formula evaluation, dependency graphing, calcChain rebuild, sharedStrings/styles migration, or relationship repair. |
| Source dependency addenda P8.567 | P8.567 treats formula text as authoritative for default/numeric, `t="str"`, `t="b"`, and `t="e"` cached-result formula cells in the public `WorkbookEditor` source path. These cells materialize as `CellValue::formula(...)`, read-only materialization stays clean, and dirty projection writes plain `<f>...</f>` cells without preserving stale cached `<v>` values. | This is source formula import compatibility only; it does not add date cell import, error-token validation, formula evaluation, cached result generation or preservation, calcChain rebuild, shared/array formula metadata preservation, sharedStrings/styles migration, relationship repair, or a complete Excel formula parser. Inline-string and shared-string formula cell shapes remain unsupported. |
| Source dependency addenda P8.568 | P8.568 materializes source scalar error cells (`t="e"`) as `CellValueKind::Error` with the `<v>` text kept as an opaque token, keeps read-only materialization clean, and projects dirty saves back as `<c t="e"><v>...</v></c>`. Missing or empty error `<v>` payloads fail before returning a materialized store. | This is opaque error-token import only; it does not validate the Excel error token set, evaluate formulas, generate cached results, rebuild calcChain, add date/custom cell support, migrate sharedStrings/styles, or repair relationships. |
| Source dependency addenda P8.569 | P8.569 accepts known formula metadata attributes `aca`, `ca`, `bx`, `dt2D`, `dtr`, `del1`, `del2`, `r1`, and `r2` in addition to `t`, `ref`, and `si` on source `<f>` elements. Shared formula definitions/followers, array formulas, dataTable formulas, package-backed prefixed formula elements, and dirty projection coverage prove these attributes are tolerated as lossy metadata and then omitted from plain `<f>` output. | Unknown formula attributes still fail fast. This is compatibility import only; it does not preserve formula metadata, evaluate formulas, validate dataTable semantics, generate cached results, rebuild calcChain, migrate sharedStrings/styles, or repair relationships. |
| Source dependency addenda P8.570 | P8.570 extends the internal `detail/formula` scanner so each reported A1-style reference can carry an optional raw sheet qualifier span. `fastxlsx.formula` now covers unqualified refs, unquoted `Sheet1!`, quoted `'Other Sheet'!`, escaped quoted `'O''Brien'!`, external-workbook-like `[Book.xlsx]Sheet1!`, and raw 3D-like `Sheet1:Sheet2!` qualifier spans while keeping the reference token itself separate for translation. | This is dependency-audit metadata only; it does not validate sheet names, external workbook targets, 3D reference semantics, perform sheet rename rewrites, build a formula dependency graph, evaluate formulas, rebuild calcChain, or implement a complete Excel formula parser. |
| Source dependency addenda P8.571 | P8.571 exposes the first public materialized-formula reference diagnostic through `WorkbookEditor::formula_reference_audits()` and `WorkbookEditorFormulaReferenceAudit`. `fastxlsx.workbook_editor.facade` covers non-materialized worksheets returning no audit entries, quoted / escaped sheet qualifiers decoding to workbook sheet names, exact reference token / qualified reference text reporting, `rename_sheet()` mapping a source-name formula reference to the new planned sheet name, and save-as preserving formula text without silent rewrite. | This is read-only audit for already-materialized `WorksheetEditor` sessions only; it does not scan the whole package, evaluate formulas, rewrite renamed-sheet references, repair defined names, rebuild calcChain, or implement a complete formula dependency graph. |
| Source dependency addenda P8.572 | P8.572 classifies external-workbook and 3D sheet-range qualifiers in `WorkbookEditorFormulaReferenceAudit`. `fastxlsx.workbook_editor.facade` covers `[Book.xlsx]Data!A1` and `Data:Formula!A1`, reports their exact qualified reference text, and keeps them out of single-sheet source/planned catalog matching. | This is audit classification only; it does not validate external workbook targets, interpret 3D sheet ranges, dereference linked workbooks, rewrite formulas, or build a formula dependency graph. |
| Source dependency addenda P8.573 | P8.573 exposes current workbook definedName formula reference diagnostics through `WorkbookEditor::defined_name_formula_reference_audits()` and `WorkbookEditorDefinedNameFormulaReferenceAudit`. `fastxlsx.workbook_editor.facade` covers direct workbook `definedNames`, workbook-scope and `localSheetId` scoped names, exact reference token / qualified reference text reporting, source-to-planned catalog matching after `rename_sheet()`, queued planned-workbook definedName rewrites, and external-workbook / 3D sheet-range classification without local matching. | This is read-only workbook metadata audit only; it does not evaluate formulas, silently rewrite definedName formulas outside explicit rename policy, validate external workbook targets, interpret 3D sheet ranges, repair workbook metadata, rebuild calcChain, or implement a formula dependency graph. |
| Source dependency addenda P8.574 | P8.574 keeps the public `WorkbookEditor` API unchanged while moving formula dependency audit behind the internal semantic operations `detail::audit_formula_references()` and `detail::audit_workbook_defined_name_formula_references()` in `include/fastxlsx/detail/formula_reference_audit.hpp` / `src/formula_reference_audit.cpp`. The detail module owns formula scanning, source/planned sheet qualifier matching, and workbook definedName formula-reference extraction; `WorkbookEditor` only supplies editor-owned state and maps detail output to public structs. | This is semantic implementation-boundary cleanup only; it is not a file-only move and does not add formula evaluation, rewrite, dependency graphing, or workbook metadata repair. |
| Source dependency addenda P8.575 | P8.575 keeps `WorkbookEditor::replace_image()` public behavior unchanged while moving existing-workbook image target resolution and PNG/JPEG media validation into `src/workbook_editor_image_edit.*`. The image-edit module owns `xl/media/*` target checks, content-type / extension matching, and replacement-format compatibility; `WorkbookEditor` only reads caller replacement data and queues the validated part rewrite. | This is semantic implementation-boundary cleanup only; it does not add broad drawing editing, image conversion, relationship repair, or existing-workbook media graph mutation. |
| Source dependency addenda P8.576 | P8.576 keeps public catalog APIs unchanged while moving source/planned worksheet catalog state into `detail::WorkbookEditorSheetCatalogPlan` in `src/workbook_editor_sheet_catalog.*`. The plan owns source names, current planned names, chained rename mapping, source/current lookup, source-to-planned catalog entries, and revert-to-source behavior; `WorkbookEditor` consumes those views instead of owning a raw rename map. | This is semantic state-boundary cleanup only; it does not add sheet add/delete, workbook relationship repair, definedName/table/formula rewrite, or broader catalog mutation semantics. |
| Source dependency addenda P8.577 | P8.577 keeps public pending replacement diagnostics unchanged while moving queued whole-`<sheetData>` replacement state into `detail::WorkbookEditorPendingSheetDataPayloads` in `src/workbook_editor_pending_edits.*`. The state object owns same-sheet diagnostic replacement, aggregate cell/memory totals, current-catalog ordered pending names, orphan diagnostic fallback ordering, and rename migration. | This is semantic state-boundary cleanup only; `WorkbookEditor` still orchestrates materialized sessions and public summary assembly, and this does not add transaction history, undo, source repair, sheet add/delete, or relationship repair. |
| Source dependency addenda P8.578 | P8.578 keeps public `WorksheetEditor` behavior unchanged while moving strict A1 parsing, row/column and range validation, and internal-to-public snapshot mapping into `src/workbook_editor_worksheet_access.*`. The helper owns single-cell reference syntax and Excel coordinate guardrails; `WorksheetEditor` only drives session lookup and mutation/read orchestration. | This is semantic implementation-boundary cleanup only; it does not add lowercase/R1C1 support, coordinate inference, range repair, dense reads, style migration, or worksheet lifecycle semantics. |
| Source dependency addenda P8.579 | P8.579 keeps public `WorkbookEditor::save_as()` behavior unchanged while moving output path preflight into `src/workbook_editor_save_as_policy.*`. The helper owns empty output, existing directory, missing-parent, and source-overwrite rejection; the facade only calls it before flushing dirty materialized sessions and saving the package. | This is semantic implementation-boundary cleanup only; it does not add in-place save, atomic replace, rollback, transaction snapshots, or broader output path recovery semantics. |
| Source dependency addenda P8.580 | P8.580 keeps public formula diagnostic APIs unchanged while moving the public-adapter layer into `src/workbook_editor_formula_diagnostics.*`. The helper owns source/planned catalog conversion for formula audit, materialized formula-cell scanning, public audit field mapping, and bounded current/planned `xl/workbook.xml` reads for definedName formula diagnostics; `WorkbookEditor` only supplies editor-owned state. | This is semantic implementation-boundary cleanup only; it does not add formula evaluation, dependency graphing, calcChain rebuild, definedName repair, or workbook metadata mutation. Formula rewrite remains limited to explicit rename policies. |
| Source dependency addenda P8.581 | P8.581 keeps public materialized worksheet edit behavior unchanged while moving dirty materialized worksheet diagnostics and save-as flush handoff into `src/workbook_editor_materialized_edits.*`. The helper owns current-catalog target preflight, dirty session projection handoff to `PackageEditor`, and flushed-session cleanup; `WorkbookEditor` still owns lifecycle, public edit-count aggregation, and save-as orchestration. | This is semantic implementation-boundary cleanup only; it does not add transaction history, rollback, relationship repair, formula/metadata synchronization, or large-file random editing. |
| Source dependency addenda P8.582 | P8.582 keeps public whole-`<sheetData>` replacement behavior unchanged while moving row-input diagnostics, rows-to-`CellStore` projection, target/materialized-session preflight, by-name `<sheetData>` chunk handoff, and pending replacement diagnostic recording into `src/workbook_editor_sheet_data_replacement.*`. `WorkbookEditor::replace_sheet_data()` now only provides public API error wrapping, edit-count aggregation, and last-error lifecycle. | This is semantic implementation-boundary cleanup only; it does not add sharedStrings/style migration, worksheet metadata synchronization, formula evaluation, relationship repair, transaction history, rollback, or large-file random editing. |
| Source dependency addenda P8.583 | P8.583 keeps the default public sheet rename behavior unchanged while moving materialized-session rename preflight, package sheet-catalog rename handoff, source/planned catalog state update, and pending whole-`<sheetData>` diagnostic migration into `src/workbook_editor_sheet_rename.*`. `WorkbookEditor::rename_sheet()` provides public API error wrapping, edit-count aggregation, last-error lifecycle, and now maps explicit rename formula policy options. | This is semantic implementation-boundary cleanup plus opt-in direct definedName formula rewrite and opt-in already-materialized WorksheetEditor formula rewrite hooks; it still does not add non-materialized worksheet formula scanning, table/drawing/chart synchronization, sheet add/delete, relationship repair, transaction history, or rollback. |
| In-memory shift addenda P8.830 | Current public `WorksheetEditor` row/column shift helpers (`insert_rows()`, `delete_rows()`, `insert_columns()`, `delete_columns()`) operate only on represented sparse cells in an already materialized small-file session. They move/delete sparse records, preserve source-backed `StyleId` handles on moved cells, use the narrow A1-style translator for moved formula cells, emit `#REF!` for references invalidated by deleted/out-of-bounds rows or columns, refresh sparse dimensions on dirty `save_as()`, and retain clean saved sessions across matching reacquire, failed save retry, option mismatch, missing-sheet queries, invalid reads/mutations, and snapshot reads under the current planned sheet name. | This is not complete Excel structural editing. The helpers do not materialize large worksheets on demand, update formulas outside moved materialized cells, evaluate formulas, repair calcChain, migrate sharedStrings/styles, resize tables/filters/validations/conditional-formatting ranges, synchronize defined names/drawings/charts/hyperlinks, prune relationships, or provide low-memory random editing. |
| In-memory shift addenda P8.831 | Public-state coverage now verifies `WorkbookEditor::formula_reference_audits()` observes the current materialized store after renamed row/column shifts. A moved formula such as `Data!A1+Data!B1` is audited at its shifted formula-cell coordinate with shifted sheet-qualified tokens (`Data!A3` / `Data!B3` after row insertion and `Data!B1` / `Data!C1` after column insertion), while the audit call preserves pending materialized diagnostics. | This is read-only audit evidence only. It does not rewrite formulas by default after `rename_sheet()`, scan non-materialized worksheets, update formulas outside moved materialized cells, repair workbook metadata, rebuild calcChain, migrate sharedStrings/styles, or implement a formula dependency graph. |
| In-memory shift addenda P8.832 | Public-state coverage now also verifies delete-side shifted formula audits. When a renamed materialized worksheet deletes row 1 or column 1, a moved formula such as `Data!A1+Data!B2` can become `Data!#REF!+Data!B1` or `Data!#REF!+Data!A2`; `formula_reference_audits()` reports the surviving shifted sheet-qualified A1 token and skips `Data!#REF!` as a non-reference. | This is still lexical read-only diagnostic behavior for already-materialized formula cells only. It does not evaluate formulas, repair invalid references, rewrite renamed-sheet qualifiers by default, inspect non-materialized worksheet XML, rebuild calcChain, or synchronize workbook/worksheet metadata. |
| In-memory shift addenda P8.833 | Public-state coverage now verifies the same delete-side shifted formula audit after `save_as()` and matching `worksheet("RenamedData")` reacquire. The clean saved materialized session still reports only the surviving shifted sheet-qualified token and keeps pending materialized diagnostics empty. | This is saved-session state hygiene for the existing in-memory materialized store. It does not add transaction history, clean-session commits, broad formula repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.834 | Public-state coverage now verifies a fresh `WorkbookEditor::open(output)` over that saved workbook rematerializes the styled `Data!#REF!+Data!B1` / `Data!#REF!+Data!A2` formulas, skips `Data!#REF!`, and reports only the surviving shifted sheet-qualified token. Because the reopened workbook catalog contains only `RenamedData`, the stale `Data!` qualifier is reported as unmatched rather than reconstructing the old source/planned rename risk. | This is fresh-open diagnostic boundary evidence only. It does not persist edit history, reconstruct prior rename context, rewrite stale qualifiers, repair invalid references, evaluate formulas, synchronize metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.835 | Public-state coverage now verifies fresh output reopen for the insert-side renamed formula audit path too. `WorkbookEditor::open(output)` over saved row/column insertion outputs rematerializes the styled `Data!A3+Data!B3` / `Data!B1+Data!C1` formulas and reports both shifted sheet-qualified tokens as unmatched because the saved workbook catalog contains only `RenamedData`. | This is the same lexical fresh-open diagnostic boundary for already-saved workbook bytes. It does not reconstruct prior rename history, rewrite stale qualifiers, scan non-materialized source XML, evaluate formulas, repair metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.836 | Public-state coverage now verifies `source_formula_reference_audits()` remains a source XML scan while a renamed materialized worksheet has dirty row/column shifts. The materialized audit observes shifted formula tokens, but the source audit still reports the original source `D2` formula `Data!A1+Data!B1` and original `Data!A1` / `Data!B1` tokens while preserving dirty materialized diagnostics. | This is read-only diagnostic isolation only. It does not mutate source XML, merge source and materialized formula views, repair stale qualifiers, evaluate formulas, synchronize metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.837 | Public-state coverage now verifies fresh output source formula audits before materialization. After `WorkbookEditor::open(output)`, `source_formula_reference_audits()` scans the saved shifted worksheet XML, reports both shifted insert-side tokens or the single surviving delete-side token, skips `Data!#REF!`, and keeps the reopened editor clean with no materialized diagnostics. | This is read-only source-scan evidence over saved workbook bytes. It does not materialize worksheets, reconstruct prior rename history, repair stale qualifiers, evaluate formulas, synchronize metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.838 | Public-state coverage now verifies the source/materialized audit isolation after a rejected source-overwrite save. A dirty renamed row/column shifted formula session rejects `save_as(source)`, then `source_formula_reference_audits()` still reports the original source `D2` `Data!A1+Data!B1` tokens, preserves dirty materialized diagnostics, and the later safe `save_as(output)` still writes the shifted formula. | This is failed-save diagnostic hygiene only. It does not add in-place save, transaction history, source XML mutation, merged source/materialized formula views, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.839 | Public-state coverage now verifies the same source/materialized audit isolation after rejected mismatched `WorksheetEditorOptions` access. Dirty renamed row/column shifted formula sessions reject mismatched `try_worksheet()` / `worksheet()`, then `source_formula_reference_audits()` still reports original source formula tokens and preserves dirty materialized diagnostics before a later safe save. | This is option-preflight diagnostic hygiene only. It does not add option migration, session cloning, source XML mutation, merged source/materialized formula views, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.840 | Public-state coverage now verifies the same source/materialized audit isolation after missing-sheet and old-source-name lookups. Dirty renamed row/column shifted formula sessions keep optional missing lookups empty, reject throwing lookups, then `source_formula_reference_audits()` still reports original source formula tokens and preserves dirty materialized diagnostics before a later safe save. | This is lookup-failure diagnostic hygiene only. It does not add source-name fallback, sheet aliasing, source XML mutation, merged source/materialized formula views, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.841 | Public-state coverage now verifies the same source/materialized audit isolation after invalid read preflight failures. Dirty renamed row/column shifted formula sessions reject row-zero `try_cell()`, column-overflow A1 `get_cell()`, and reversed-range `sparse_cells()`, then `source_formula_reference_audits()` still reports original source formula tokens and preserves dirty materialized diagnostics before a later safe save. | This is read-preflight diagnostic hygiene only. It does not add coordinate repair/clamping, relaxed A1 parsing, source XML mutation, merged source/materialized formula views, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.842 | Public-state coverage now verifies the same source/materialized audit isolation after invalid mutation preflight failures. Dirty renamed row/column shifted formula sessions reject row-zero and column-overflow formula `set_cell()` calls plus range-form `erase_cell()`, may update `last_edit_error()`, then `source_formula_reference_audits()` still reports original source formula tokens and preserves dirty materialized diagnostics before a later safe save. | This is mutation-preflight diagnostic hygiene only. It does not add coordinate repair/clamping, relaxed A1 parsing, range erase semantics for single-cell APIs, source XML mutation, merged source/materialized formula views, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.843 | Public-state coverage now verifies the same source/materialized audit isolation after invalid row/column shift preflight failures. Dirty renamed row/column shifted formula sessions reject invalid start coordinates and count ranges for `insert_rows()` / `delete_rows()` or `insert_columns()` / `delete_columns()`, then `source_formula_reference_audits()` still reports original source formula tokens and preserves dirty materialized diagnostics before a later safe save. | This is shift-preflight diagnostic hygiene only. It does not add coordinate repair/clamping, relaxed row/column range validation, partial structural edits, source XML mutation, merged source/materialized formula views, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.844 | Public-state coverage now verifies the same source/materialized audit isolation after unrelated source-backed worksheet materialization guardrail failures. Dirty renamed row/column shifted formula sessions reject materializing the untouched worksheet under a too-small `WorksheetEditorOptions::max_cells`, preserve `last_edit_error()`, avoid registering partial materialized diagnostics, and still let `source_formula_reference_audits()` report original source formula tokens before a later safe save. | This is materialization-failure diagnostic hygiene only. It does not add workbook-level guardrails, option migration, partial materialization recovery, source XML mutation, merged source/materialized formula views, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.845 | Public-state coverage now verifies recovery after that unrelated materialization guardrail failure. A later default-options materialization of the untouched worksheet succeeds as a clean read-only session, reads the preserved source cells, does not add dirty materialized diagnostics beyond the already shifted renamed worksheet, and leaves `source_formula_reference_audits()` on the original source formula tokens before a later safe save. | This is materialization-recovery diagnostic hygiene only. It does not add option downgrading, automatic retry, dirtying clean read-only materializations, source XML mutation, merged source/materialized formula views, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.846 | Public-state coverage now verifies the same source/materialized audit isolation after save-as output path preflight failures. Dirty renamed row/column shifted formula sessions reject path-equivalent source overwrite, empty output path, missing parent, non-directory parent, and existing-directory output targets; each failure leaves `source_formula_reference_audits()` on the original source formula tokens and preserves dirty materialized diagnostics before a later safe save. | This is save-as preflight diagnostic hygiene only. It does not add in-place save, atomic replacement, rollback, output-directory creation, source XML mutation, merged source/materialized formula views, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.847 | Public-state coverage now verifies the safe-save retry after save-as output path preflight failures. After rejected path-equivalent source overwrite, empty output path, missing parent, non-directory parent, and existing-directory targets, the later safe `save_as(output)` still writes the shifted qualified formula, leaves the source package and rejected path artifacts unchanged, and `WorkbookEditor::open(output).source_formula_reference_audits()` reports the saved shifted formula tokens with clean diagnostics. | This is save-as retry/output hygiene only. It does not add in-place save, atomic replacement, rollback, output-directory creation, source XML mutation, merged source/materialized formula views, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.848 | Public-state coverage now applies that save-as retry/output hygiene to delete-side `#REF!` formula audit paths. Dirty renamed delete-row/delete-column formula sessions reject source overwrite, path-equivalent source overwrite, empty output path, missing parent, non-directory parent, and existing-directory targets; each failure keeps `source_formula_reference_audits()` on the original `Data!A1+Data!B2` source tokens. The later safe `save_as(output)` writes `Data!#REF!+Data!B1` / `Data!#REF!+Data!A2`, leaves the source package and rejected path artifacts unchanged, and fresh reopen source audits still skip `Data!#REF!` while reporting only the surviving shifted reference. | This is delete-side save-as retry/source-audit hygiene only. It does not add in-place save, atomic replacement, rollback, output-directory creation, source XML mutation, merged source/materialized formula views, formula repair/evaluation, `#REF!` repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.849 | Public-state coverage now verifies same-editor post-save reacquire preserves source-scan isolation. After safe `save_as(output)` and `worksheet("RenamedData")` reacquire, insert-side shifted formula sessions are clean while `source_formula_reference_audits()` still reports the original source `Data!A1+Data!B1` tokens; delete-side `#REF!` sessions likewise keep source scans on the original `Data!A1+Data!B2` tokens and leave materialized diagnostics empty. | This is same-editor saved-session diagnostic hygiene only. It does not mutate source XML, merge source/materialized formula views, repair stale qualifiers or `#REF!`, evaluate formulas, synchronize metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.850 | Public-state coverage now verifies insert-side `formula_reference_audits()` after same-editor post-save reacquire. A safe saved renamed row/column insert-shift session can be reacquired as clean, keeps pending materialized diagnostics empty, and still reports both shifted qualified tokens (`Data!A3` / `Data!B3` or `Data!B1` / `Data!C1`) from the materialized store. | This is read-only clean-session audit hygiene only. It does not rewrite formulas, reconstruct rename history, scan non-materialized worksheets, mutate source XML, evaluate formulas, synchronize metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.851 | Public-state coverage now verifies source-scan formula audits also preserve the aggregate materialized memory diagnostic. The shared insert/delete source-audit isolation helpers snapshot `estimated_pending_materialized_memory_usage()` alongside pending names and cell counts, covering both dirty shifted sessions and clean same-editor post-save reacquire sessions. | This is read-only diagnostic hygiene only. It does not change memory accounting, materialize worksheets, merge source/materialized formula views, mutate source XML, evaluate formulas, synchronize metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.852 | Public-state coverage now verifies source-scan formula audits preserve the full pending worksheet edit summaries. The shared insert/delete source-audit isolation helpers compare `pending_worksheet_edits()` by content, so source scans cannot silently mutate rename, replacement, dirty materialized, cell-count, or memory summary fields. | This is read-only summary diagnostic hygiene only. It does not change summary generation, materialize worksheets, merge source/materialized formula views, mutate source XML, evaluate formulas, synchronize metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.853 | Public-state coverage now applies the same memory and summary diagnostic hygiene to materialized `formula_reference_audits()` on renamed insert-side row/column shifts. The audit call snapshots aggregate materialized memory plus full pending worksheet edit summaries while preserving pending-change, replacement, materialized-name, cell-count, and last-error state. | This is read-only materialized-audit hygiene only. It does not change formula scanning semantics, rewrite formulas, inspect non-materialized source XML, evaluate formulas, synchronize metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.854 | Public-state coverage now applies the materialized `formula_reference_audits()` diagnostic hygiene to delete-side `#REF!` row/column shifts. The delete-row/delete-column audits reuse a shared public-state snapshot helper across dirty sessions and same-editor post-save reacquire, preserving pending-change, replacement, materialized-name, cell-count, aggregate memory, full edit-summary, and last-error diagnostics while still skipping `Data!#REF!`. | This is read-only delete-side audit hygiene only. It does not repair `#REF!`, rewrite formulas, inspect non-materialized source XML, evaluate formulas, synchronize metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.855 | Public-state coverage now strengthens fresh-output reopened formula audit hygiene. The shared reopened insert/delete formula audit helpers require both source scans and post-materialization `formula_reference_audits()` to leave the reopened editor clean, including zero aggregate materialized memory, empty pending edit summaries, and no `last_edit_error`. | This is saved-output diagnostic hygiene only. It does not reconstruct prior rename history, repair stale qualifiers or `#REF!`, rewrite formulas, evaluate formulas, synchronize metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.856 | Public-state coverage now applies the shared materialized formula-audit diagnostic snapshot to insert-side same-editor post-save reacquire. After saving renamed row/column insert shifts and reacquiring `RenamedData`, `formula_reference_audits()` preserves pending-change, replacement, materialized-name, cell-count, aggregate memory, full edit-summary, and last-error diagnostics while still reporting both shifted qualified tokens from the clean saved session. | This is read-only post-save audit hygiene only. It does not rewrite formulas, reconstruct rename history, inspect non-materialized source XML, mutate source packages, evaluate formulas, synchronize metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.857 | Public-state coverage now strengthens the shared fresh-output clean readback helper. Reopened saved outputs must keep pending edit counts, materialized/replacement cell counts, aggregate materialized/replacement memory estimates, pending edit summaries, dirty worksheet-name diagnostics, and `last_edit_error` empty both immediately after materialization and after valid readback inspections. | This is fresh-output diagnostic hygiene only. It does not add clean-session commits, transaction history, source package mutation, metadata repair, formula repair/evaluation, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.858 | Public-state coverage now applies the same widened reopened clean-diagnostics contract to guardrail and diagnostic-recovery readbacks. Max-cells / memory-budget budget-release saves, missing-erase no-op saves, rejected blank-insertion overwrite saves, last-error replacement recovery, and mixed public-edit recovery all assert empty materialized/replacement memory diagnostics, pending edit summaries, dirty worksheet-name diagnostics, and `last_edit_error` before and after valid readback inspections. | This is guardrail-output readback hygiene only. It does not add broader guardrail policies, rollback, source package mutation, metadata repair, formula repair/evaluation, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.859 | Public-state coverage now strengthens the shared row/column shift reopened-output helper. Insert/delete row and column shift outputs, shift reacquire/retry paths, invalid-to-valid recovery, rich formula shifts, cross-handle shift preservation, and related shift guardrail saves now assert empty materialized/replacement memory diagnostics, pending edit summaries, dirty worksheet-name diagnostics, and `last_edit_error` both before and after saved-output readback. | This is shifted-output diagnostic hygiene only. It does not add broad worksheet structural metadata synchronization, formula repair/evaluation, clean-session commits, source package mutation, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.860 | Public-state coverage now strengthens formula-audit diagnostic snapshots. Source and materialized row/column shift formula audit helpers preserve replacement cell counts and replacement memory estimates, and fresh reopened formula-audit outputs assert empty replacement dirty names alongside materialized diagnostics before and after audit/readback. | This is read-only formula-audit diagnostic hygiene only. It does not add formula repair/evaluation, source/materialized audit merging, source package mutation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.861 | Public-state coverage now verifies formula-audit helpers also preserve source worksheet names, planned worksheet names, and workbook sheet catalog entries while scanning dirty and clean row/column shifted sessions. | This is catalog-view diagnostic hygiene only. It does not add sheet aliasing, rename-history reconstruction, source package mutation, metadata synchronization, formula repair/evaluation, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.862 | Public-state coverage now applies the same catalog-view snapshot to fresh reopened formula-audit outputs. Saved shifted formula workbooks preserve source/planned worksheet names and catalog entries around both `source_formula_reference_audits()` and post-materialization `formula_reference_audits()`. | This is reopened-output catalog diagnostic hygiene only. It does not reconstruct rename history, mutate source packages, merge source/materialized audit views, synchronize metadata, repair/evaluate formulas, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.863 | Public-state failure-diagnostic coverage now applies the catalog-view snapshot to the shared read-only inspection helper. Invalid cell references, guardrail failures, failed replacements, failed renames, and failed materialized mutations preserve source worksheet names, planned worksheet names, and workbook sheet catalog entries across every public inspection, including source/materialized/defined-name formula audits, while keeping `last_edit_error()` unchanged. | This is read-only failure-state diagnostic hygiene only. It does not add rollback machinery, formula or defined-name repair/evaluation, source package mutation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.864 | Formula/definedName rewrite regression coverage now applies the same catalog-view snapshot to its shared read-only inspection helper. The formula-audit shard verifies source worksheet names, planned worksheet names, and full worksheet catalog entries stay stable around formula, source-formula, and definedName audit inspections while preserving `last_edit_error()`. | This is formula-diagnostic test hygiene only. It does not broaden default rename behavior, rewrite non-materialized worksheet formulas, repair definedNames, evaluate formulas, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.865 | The shared facade, public-retry, and source-success regression helpers now mirror the same catalog-view inspection contract. Their read-only public inspection checks preserve source worksheet names, planned worksheet names, and worksheet catalog entries across catalog, pending-state, replacement, materialized, formula, source-formula, and definedName audit APIs while keeping `last_edit_error()` unchanged. | This is cross-shard diagnostic hygiene only. It does not add new public state, transaction rollback, formula/definedName repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.866 | The remaining standalone WorkbookEditor public regression shards now share the catalog-view inspection contract too. Core, public, public-edge, public-guards, materialized-session, and source-failure helpers snapshot source/planned worksheet names and worksheet catalog entries around every read-only inspection while preserving `last_edit_error()`. | This completes test-helper coverage for inspection-state hygiene only. It does not change production semantics, add rollback, repair formulas or definedNames, synchronize workbook metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.867 | Public-state handle-level read coverage now snapshots source worksheet names, planned worksheet names, and worksheet catalog entries around `WorksheetEditor::used_range()`, `contains_cell()`, `row_cells()`, `column_cells()`, and invalid `sparse_cells()` range inspections after prior mutation diagnostics. | This is read-only handle inspection hygiene only. It does not change production semantics, add rollback, infer coordinates, repair formulas or definedNames, synchronize workbook metadata, rebuild calcChain, migrate sharedStrings/styles, or provide low-memory random editing. |
| In-memory shift addenda P8.868 | Public-state base cell-read coverage now applies the catalog-view snapshot to invalid A1 and row/column `WorksheetEditor::try_cell()` / `get_cell()` reads, including the prior-diagnostic read-failure helper. | This is read-only cell-inspection hygiene only. It does not add coordinate inference or clamping, missing-cell synthesis, source reload, rollback, formula/definedName repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.869 | Public-state shift-reacquire coverage now applies the same catalog-view snapshot to non-renamed saved-session option mismatch, missing-query, invalid-read, and invalid-mutation paths after row/column shifts. | This is shifted-session state hygiene only. It does not add dynamic catalog repair, rename-history reconstruction, rollback, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.870 | Public-state row/column shift guard coverage now snapshots source/planned worksheet names and worksheet catalog entries around zero-count shifts, nonzero represented-range no-ops, validation failures, and row/column overflow failures. | This is shift guard state hygiene only. It does not add row/column metadata editing, semantic range synchronization, coordinate clamping, rollback, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory shift addenda P8.871 | Public-state non-renamed delete-row/delete-column coverage now pins source-backed styled formula cells through sparse coordinate shifts: deleted row/column references translate to `#REF!`, surviving references shift, the moved formula keeps its source `StyleId`, and dirty `save_as()` / fresh reopen preserve the projected sparse records. | This is focused in-memory sparse-record evidence only. It does not add caller-supplied style writes, style migration/merge, broad formula repair/evaluation, worksheet metadata synchronization, calcChain rebuild, sharedStrings migration, relationship repair/pruning, or low-memory random editing. |
| In-memory operation-mixing addenda P8.872 | Public-guards coverage now verifies same-sheet targeted `replace_cells()` is rejected after dirty, read-only, and saved-clean materialized `WorksheetEditor` sessions. The rejected targeted-cell path replaces `last_edit_error()` with the public `WorkbookEditor::replace_cells()` wrapper diagnostic, leaves targeted patch diagnostics empty, preserves borrowed handles and catalog state, and does not leak rejected payloads into later `save_as()` output. | This is same-worksheet mode-mixing hygiene only. It does not make targeted Patch cells and materialized sparse sessions composable, does not add conflict resolution or transaction history, and does not change sharedStrings/styles migration, metadata synchronization, relationship repair, calcChain rebuild, or low-memory random editing boundaries. |
| In-memory operation-mixing addenda P8.873 | Public Patch targeted-cell coverage now verifies the reverse guard: after `replace_cells()` queues a same-sheet targeted edit, both `worksheet()` and `try_worksheet()` reject materialization without updating `last_edit_error()`, and targeted cell count, worksheet-name, XML-byte, and pending public edit diagnostics remain intact. | This is guardrail evidence for mutually exclusive worksheet edit modes only. It does not allow a caller to materialize over queued targeted patches, merge Patch and in-memory stores, resolve conflicts, or add broader transaction/rollback behavior. |
| In-memory operation-mixing addenda P8.874 | Public coverage now strengthens the queued whole-`<sheetData>` replacement reverse guard. After `replace_sheet_data()` queues a same-sheet replacement, both `try_worksheet()` and `worksheet()` reject materialization without updating `last_edit_error()`, preserve replacement count/name/memory diagnostics and pending public edit count, avoid materialized diagnostics, and still allow `save_as()` to write the queued replacement. | This is whole-sheet Patch versus in-memory mode hygiene only. It does not compose queued replacements with materialized sparse sessions, add conflict resolution, or change sharedStrings/styles migration, metadata synchronization, relationship repair, calcChain rebuild, or low-memory random editing boundaries. |
| In-memory operation-mixing addenda P8.875 | Public coverage now exercises the materialized-to-Patch catalog guard: once a dirty `WorksheetEditor` session exists, same-sheet `rename_sheet()` and `replace_sheet_data()` failures preserve the source/planned catalog, leave replacement diagnostics empty, keep the dirty materialized summary, and still allow a later safe `save_as()` to flush only the materialized cells. | This is catalog/replacement guard hygiene only. It does not make catalog renames or whole-`<sheetData>` replacements composable with an already materialized sparse session, and it does not add conflict resolution, transaction history, metadata synchronization, formula repair/evaluation, sharedStrings/styles migration, relationship repair, calcChain rebuild, or low-memory random editing. |
| In-memory operation-mixing addenda P8.876 | Public coverage now extends the same catalog guard to clean materialized sessions. Read-only materialization remains a copy-original no-op after rejected same-sheet `rename_sheet()` / `replace_sheet_data()`, and a post-save clean materialized session keeps its prior projection while later safe `save_as()` adds no extra handoff and leaks neither rejected catalog names nor replacement payloads. | This is clean-session mode-mixing hygiene only. It does not add clean-session commit semantics, allow late catalog/replacement composition over materialized state, or change transaction, metadata synchronization, formula repair/evaluation, sharedStrings/styles migration, relationship repair, calcChain rebuild, or low-memory random editing boundaries. |
| In-memory operation-mixing addenda P8.877 | Public coverage now pins `request_full_calculation()` alongside a dirty materialized `WorksheetEditor` session. The helper queues only the workbook `fullCalcOnLoad` metadata edit, leaves dirty materialized names/cell counts/memory intact until `save_as()`, and the saved package carries both the workbook calc request and the materialized sparse projection without inventing `xl/calcChain.xml`. | This is ordering and diagnostic hygiene for an existing metadata helper. It does not evaluate formulas, rebuild calcChain, repair relationships, synchronize metadata, migrate sharedStrings/styles, make materialized state a commit log, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.878 | Public coverage now applies the same `request_full_calculation()` boundary to clean materialized sessions. A read-only materialized `WorksheetEditor` remains clean, contributes no dirty materialized diagnostics or worksheet summaries, and `save_as()` preserves the source worksheet bytes while writing workbook `fullCalcOnLoad="1"` without inventing `xl/calcChain.xml`. | This is clean-session metadata-request hygiene only. It does not add clean-session commit semantics, worksheet metadata synchronization, formula evaluation, calcChain rebuild, relationship repair, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.879 | Public coverage now pins `request_full_calculation()` after a materialized projection has already been flushed by `save_as()`. The saved-clean `WorksheetEditor` remains clean, dirty materialized diagnostics stay empty, the next save reuses the prior sparse projection, and pending public edit count does not gain another materialized handoff. | This is post-save ordering hygiene for an existing projection and metadata helper. It does not add commit/close semantics, history compaction, formula evaluation, calcChain rebuild, relationship repair, metadata synchronization, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.880 | Public coverage now pins the reverse ordering where `request_full_calculation()` is queued before `worksheet()`. The workbook metadata request does not block later clean materialization or later dirty sparse edits, and `save_as()` persists both `fullCalcOnLoad="1"` and the subsequent materialized projection while clearing dirty materialized diagnostics. | This is workbook-metadata versus in-memory ordering hygiene only. It does not make arbitrary Patch replacements composable with materialized state, evaluate formulas, rebuild calcChain, repair metadata/relationships, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.881 | Public coverage now pins the clean reverse ordering too. If `request_full_calculation()` is queued before a read-only `worksheet()` materialization, the clean session contributes no dirty materialized diagnostics, no worksheet summaries, and no materialized handoff; `save_as()` rewrites workbook calc metadata while preserving source worksheet bytes. | This is read-only materialization and metadata-request ordering hygiene only. It does not add clean-session commit semantics, formula evaluation, calcChain rebuild, worksheet metadata synchronization, relationship repair, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.882 | Public coverage now verifies `request_full_calculation()` can be queued before a same-sheet whole-`<sheetData>` `replace_sheet_data()` Patch. Later `try_worksheet()` / `worksheet()` still reject materialization without updating `last_edit_error()`, preserve replacement diagnostics and public edit count, avoid dirty materialized diagnostics, and `save_as()` writes both `fullCalcOnLoad="1"` and the queued replacement payload. | This is workbook metadata plus whole-sheet Patch guard ordering hygiene only. It does not compose queued replacements with materialized sparse sessions, add conflict resolution, repair metadata/relationships, evaluate formulas, rebuild calcChain, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.883 | Public targeted-cell coverage now verifies the matching `request_full_calculation()` before same-sheet `replace_cells()` ordering. Later `worksheet()` / `try_worksheet()` reject materialization without updating `last_edit_error()`, targeted count/name/XML-byte diagnostics and public edit count remain intact, and `save_as()` writes both the targeted patch and workbook `fullCalcOnLoad="1"`. | This is workbook metadata plus targeted Patch guard ordering hygiene only. It does not merge targeted Patch payloads into a materialized store, add arbitrary indexed random editing, repair worksheet metadata, rebuild calcChain, migrate sharedStrings/styles, or change relationship preservation boundaries. |
| In-memory operation-mixing addenda P8.884 | Public coverage now verifies `request_full_calculation()` can be queued before `rename_sheet()` and a later dirty `WorksheetEditor` materialization. The materialized session is acquired by the planned sheet name, dirty diagnostics and summaries use that planned name while preserving the source name, and `save_as()` writes workbook `fullCalcOnLoad="1"`, the renamed catalog entry, and the sparse projection without inventing `xl/calcChain.xml`. | This is workbook metadata, catalog rename, and in-memory ordering hygiene only. It does not rewrite formulas or defined names, evaluate formulas, rebuild calcChain, repair metadata/relationships, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.885 | Public coverage now pins the clean branch for the same `request_full_calculation()` -> `rename_sheet()` -> `worksheet()` ordering. Read-only materialization by the planned name contributes no dirty materialized diagnostics and no materialized handoff; `save_as()` rewrites workbook calc metadata and the catalog name while preserving the worksheet part bytes. | This is clean materialization plus metadata/catalog ordering hygiene only. It does not add clean-session commit semantics, worksheet metadata synchronization, formula evaluation, calcChain rebuild, relationship repair, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.886 | Public coverage now verifies `request_full_calculation()` does not weaken materialized same-sheet catalog guards. With either dirty or clean materialized sessions, later same-sheet `rename_sheet()` and `replace_sheet_data()` failures preserve the catalog, replacement diagnostics, materialized diagnostics, and queued metadata edit; safe save writes `fullCalcOnLoad="1"` while omitting rejected names and payloads. | This is metadata-request plus catalog-guard hygiene only. It does not allow late catalog/replacement composition over materialized state, add conflict resolution, evaluate formulas, rebuild calcChain, repair metadata/relationships, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.887 | Public coverage now pins the opposite rename/full-calc ordering for dirty materialized state. After `rename_sheet()` succeeds, a later `request_full_calculation()` preserves the planned sheet name, lets `worksheet()` materialize by that planned name, and `save_as()` writes the renamed catalog, workbook `fullCalcOnLoad="1"`, and the sparse projection without inventing `xl/calcChain.xml`. | This is catalog metadata and in-memory ordering hygiene only. It does not rewrite formulas, defined names, tables, drawings, relationships, or worksheet metadata, and it does not evaluate formulas, rebuild calcChain, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.888 | Public coverage now pins the clean branch for `rename_sheet()` -> `request_full_calculation()` -> `worksheet()`. Read-only materialization by the planned name keeps dirty materialized diagnostics empty, contributes no materialized handoff, and `save_as()` writes workbook calc metadata plus the renamed catalog while preserving the worksheet part bytes. | This is clean-session ordering evidence only. It does not add clean-session commit semantics, formula/defined-name rewrite, worksheet metadata synchronization, relationship repair, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.889 | Public coverage now verifies same-sheet catalog guards after `rename_sheet()` and `request_full_calculation()` have both been queued before a dirty materialized session. Later same-sheet `rename_sheet()` / `replace_sheet_data()` failures preserve the current planned name, dirty materialized diagnostics, rename plus metadata public edit count, and safe save writes only the planned rename, `fullCalcOnLoad="1"`, and sparse projection. | This is guard/diagnostic hygiene for a renamed materialized session. It does not compose late Patch replacements with materialized state, rewrite formulas or defined names, repair worksheet metadata/relationships, rebuild calcChain, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.890 | Public coverage now applies the same renamed/full-calc same-sheet guard to clean materialized sessions. The failed catalog/Patch attempts do not dirty the borrowed sheet, do not create materialized diagnostics or replacement diagnostics, preserve the rename plus metadata edit count, and safe save keeps the worksheet part bytes source-identical. | This is clean-session catalog guard hygiene only. It does not add clean-session commit semantics, conflict resolution, worksheet metadata synchronization, relationship repair, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.891 | Public targeted-cell coverage now verifies `replace_cells()` is rejected after `rename_sheet()` plus `request_full_calculation()` and a dirty materialized `WorksheetEditor` session. The failure records the public targeted-cell wrapper diagnostic, keeps targeted replacement diagnostics empty, preserves the planned dirty materialized name and rename plus metadata edit count, and safe save omits the rejected targeted payload. | This is targeted Patch guard hygiene only. It does not merge targeted Patch updates into materialized sparse state, add conflict resolution, rewrite formulas/defined names, repair metadata/relationships, rebuild calcChain, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.892 | Public targeted-cell coverage now applies the same renamed/full-calc `replace_cells()` guard to clean materialized sessions. The rejected targeted patch does not dirty the borrowed sheet, leaves materialized and targeted diagnostics empty, preserves the rename plus metadata edit count, and safe save keeps source worksheet bytes intact. | This is clean-session targeted Patch rejection evidence only. It does not add clean-session commit semantics, compose Patch and in-memory modes, repair worksheet metadata/relationships, rebuild calcChain, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.893 | Public-state coverage now verifies `request_full_calculation()` can be queued after a dirty `WorksheetEditor::insert_rows()` sparse shift. The queued workbook metadata edit preserves dirty materialized names, sparse counts, and memory diagnostics until `save_as()`, and the saved package carries both `fullCalcOnLoad="1"` and the shifted styled formula projection without inventing `xl/calcChain.xml`. | This is workbook calc metadata plus already-supported sparse row-shift projection evidence only. It does not evaluate formulas, rebuild calcChain, update formulas outside moved materialized cells, synchronize tables/ranges/drawings/defined names, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.894 | Public-state coverage now verifies the reverse ordering for a column shift: `request_full_calculation()` can be queued before materializing `Data`, then a later dirty `WorksheetEditor::insert_columns()` sparse shift keeps the metadata edit pending until `save_as()`. The saved package writes `fullCalcOnLoad="1"`, the shifted numeric cell, the translated formula cell, and the shifted dirty cell while omitting old coordinates and not inventing `xl/calcChain.xml`. | This is workbook calc metadata plus already-supported sparse column-shift projection evidence only. It does not evaluate formulas, rebuild calcChain, synchronize worksheet metadata ranges, migrate sharedStrings/styles, compose Patch and materialized modes, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.895 | Public-state coverage now verifies `request_full_calculation()` after a dirty `WorksheetEditor::delete_rows()` sparse shift preserves dirty materialized diagnostics until `save_as()`. The saved package writes `fullCalcOnLoad="1"`, the shifted styled formula cell with deleted references serialized as `#REF!`, and the shifted source cells while omitting old coordinates and not inventing `xl/calcChain.xml`. | This is workbook calc metadata plus already-supported sparse delete-row projection evidence only. It does not repair or evaluate formulas, rebuild calcChain, synchronize worksheet metadata ranges, migrate sharedStrings/styles, compose Patch and materialized modes, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.896 | Public-state coverage now verifies the reverse ordering for delete-side column shifts: `request_full_calculation()` can be queued before materializing `Data`, then a dirty `WorksheetEditor::delete_columns()` sparse shift preserves the workbook metadata edit and materialized diagnostics until `save_as()`. The saved package writes `fullCalcOnLoad="1"`, shifted source cells, and the shifted styled formula cell with the deleted-column reference serialized as `#REF!` while omitting old coordinates and not inventing `xl/calcChain.xml`. | This is workbook calc metadata plus already-supported sparse delete-column projection evidence only. It does not repair or evaluate formulas, rebuild calcChain, synchronize worksheet metadata ranges, migrate sharedStrings/styles, compose Patch and materialized modes, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.897 | Public-state coverage now verifies `formula_reference_audits()` remains read-only when a workbook-level `request_full_calculation()` is queued beside a dirty shifted formula session. After `WorksheetEditor::insert_rows()` translates `Data!A1+Data!B1` to `Data!A2+Data!B2`, the audit reports shifted qualified tokens while preserving pending edit count, materialized names/counts/memory, summaries, catalog, and diagnostics; `save_as()` still writes both the shifted formula and `fullCalcOnLoad="1"` without inventing `xl/calcChain.xml`. | This is diagnostic-state hygiene for already-materialized formula cells only. It does not evaluate formulas, rewrite sheet qualifiers, build a complete formula dependency graph, rebuild calcChain, synchronize worksheet metadata ranges, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.898 | Public-state coverage now verifies `source_formula_reference_audits()` keeps scanning source worksheet XML when `request_full_calculation()` is queued beside a dirty shifted formula session. The materialized cell moves to `D3` as `Data!A2+Data!B2`, but the source audit still reports original source `D2` tokens `Data!A1` / `Data!B1` while preserving pending edit count, materialized diagnostics, summaries, catalog, and `last_edit_error`; `save_as()` still writes the shifted formula plus `fullCalcOnLoad="1"` without inventing `xl/calcChain.xml`. | This is source-vs-materialized diagnostic isolation only. It does not merge audit views, mutate source XML, evaluate formulas, rebuild calcChain, synchronize worksheet metadata ranges, migrate sharedStrings/styles, repair stale references, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.899 | Formula-rewrite coverage now verifies `defined_name_formula_reference_audits()` remains read-only when `request_full_calculation()` is queued beside a dirty materialized `WorksheetEditor` shift. The audit still reports source workbook direct definedName text such as `Data!$A$1:$B$2`, preserves dirty materialized names/counts/memory, summaries, catalog, replacement diagnostics, and `last_edit_error`, and `save_as()` writes the shifted worksheet cell plus `fullCalcOnLoad="1"` while leaving workbook definedNames unchanged and not creating `xl/calcChain.xml`. | This is definedName diagnostic-state hygiene only. It does not shift or repair workbook definedNames, evaluate formulas, rebuild calcChain, synchronize worksheet/workbook metadata ranges, migrate sharedStrings/styles, merge audit views, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.900 | Formula-rewrite coverage now verifies the renamed variant of that definedName audit boundary. After default `rename_sheet("Data", "RenamedData")`, `request_full_calculation()`, and a dirty materialized shift under the planned sheet name, `defined_name_formula_reference_audits()` still reports direct workbook definedName text `Data!$A$1:$B$2`, maps source `Data` to planned `RenamedData`, flags the stale source-name reference, and preserves materialized/replacement diagnostics and `last_edit_error`; `save_as()` writes the renamed catalog plus `fullCalcOnLoad="1"` while keeping the definedName text unchanged. | This is rename-risk diagnostic evidence only. It does not make default rename semantic, rewrite definedNames outside explicit policies, evaluate formulas, rebuild calcChain, synchronize worksheet/workbook metadata ranges, migrate sharedStrings/styles, repair references, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.901 | Public-state coverage now verifies the renamed variant of the source formula audit boundary. After default `rename_sheet("Data", "RenamedData")`, `request_full_calculation()`, and a dirty materialized shift under the planned sheet name, `source_formula_reference_audits()` still scans original source worksheet XML, reports source `D2` tokens `Data!A1` / `Data!B1`, maps source `Data` to planned `RenamedData` as stale source-name diagnostics, and preserves materialized diagnostics; `save_as()` writes the renamed catalog, shifted `D3` formula, and `fullCalcOnLoad="1"` without inventing `xl/calcChain.xml`. | This is source-scan and rename-risk diagnostic evidence only. It does not merge source/materialized audit views, mutate source XML, make default rename semantic, evaluate or repair formulas, rebuild calcChain, synchronize worksheet/workbook metadata ranges, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.902 | Public-state coverage now verifies the renamed materialized formula audit variant of the same full-calculation boundary. After default `rename_sheet("Data", "RenamedData")`, `request_full_calculation()`, and a dirty materialized shift under the planned sheet name, `formula_reference_audits()` reports shifted `D3` formula tokens `Data!A2` / `Data!B2`, maps source `Data` to planned `RenamedData` as stale source-name diagnostics, and preserves pending/materialized diagnostics; `save_as()` writes the renamed catalog, shifted formula, and `fullCalcOnLoad="1"` without inventing `xl/calcChain.xml`. | This is materialized formula diagnostic-state evidence only. It does not evaluate formulas, rewrite stale qualifiers under default rename, merge audit views, rebuild calcChain, synchronize worksheet/workbook metadata ranges, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.903 | Public-state coverage now verifies rejected source-overwrite `save_as()` does not corrupt the renamed full-calculation formula audit state. With rename, `request_full_calculation()`, and a dirty shifted formula pending, an exact source-path save failure preserves `formula_reference_audits()` shifted `D3` tokens, `source_formula_reference_audits()` original source `D2` tokens, materialized diagnostics, source package bytes, and `last_edit_error`; the later safe retry writes the renamed catalog, shifted formula, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml`. | This is failed-save retry diagnostic hygiene only. It does not add in-place save, rollback/transaction history, formula evaluation or repair, default rename formula rewrite, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.904 | Public-state coverage now verifies mismatched `WorksheetEditorOptions` access does not corrupt the renamed full-calculation formula audit state. With rename, `request_full_calculation()`, and a dirty shifted formula pending, rejected `try_worksheet("RenamedData", options)` / `worksheet("RenamedData", options)` calls preserve shifted materialized formula audits, original source formula audits, materialized diagnostics, catalog state, and `last_edit_error`; `save_as()` still writes the renamed catalog, shifted formula, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml`. | This is option-preflight diagnostic hygiene only. It does not add option migration, session cloning, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.905 | Public-state coverage now verifies missing-sheet and old-source-name worksheet lookups do not corrupt the renamed full-calculation formula audit state. With rename, `request_full_calculation()`, and a dirty shifted formula pending, empty optional lookups plus throwing `worksheet("Missing")` / `worksheet("Data")` failures preserve shifted materialized formula audits, original source formula audits, materialized diagnostics, catalog state, and `last_edit_error`; `save_as()` still writes the renamed catalog, shifted formula, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml`. | This is lookup-preflight diagnostic hygiene only. It does not add source-name fallback, aliasing, session cloning, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.906 | Public-state coverage now verifies invalid `WorksheetEditor` read preflights do not corrupt the renamed full-calculation formula audit state. With rename, `request_full_calculation()`, and a dirty shifted formula pending, rejected row-zero, column-zero, lowercase-A1, overflow, reversed-range, invalid-batch, and missing-cell reads preserve shifted materialized formula audits, original source formula audits, materialized diagnostics, catalog state, and `last_edit_error`; `save_as()` still writes the renamed catalog, shifted formula, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml`. | This is read-preflight diagnostic hygiene only. It does not add coordinate repair or clamping, relaxed A1 parsing, source-name fallback, session cloning, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.907 | Public-state coverage now verifies invalid `WorksheetEditor` mutation preflights do not corrupt the renamed full-calculation formula audit state. With rename, `request_full_calculation()`, and a dirty shifted formula pending, rejected invalid `set_cell()` / `erase_cell()` / `erase_cells()` calls populate and preserve the expected invalid-reference `last_edit_error()` while leaving shifted materialized formula audits, original source formula audits, materialized diagnostics, catalog state, and `save_as()` output intact; the output still writes the renamed catalog, shifted formula, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml` or leaking rejected formula payloads. | This is mutation-preflight diagnostic hygiene only. It does not add coordinate repair or clamping, relaxed A1 parsing, rollback history, source-name fallback, session cloning, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.908 | Public-state coverage now verifies invalid row/column shift preflights do not corrupt the renamed full-calculation formula audit state. With rename, `request_full_calculation()`, and a dirty shifted formula pending, rejected `insert_rows()` / `delete_rows()` / `insert_columns()` / `delete_columns()` bounds failures populate and preserve the expected shift diagnostic while leaving shifted materialized formula audits, original source formula audits, materialized diagnostics, catalog state, and `save_as()` output intact; the output still writes the renamed catalog, shifted formula, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml`. | This is shift-preflight diagnostic hygiene only. It does not add range repair or clamping, partial shift retry, rollback history, source-name fallback, session cloning, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.909 | Public-state coverage now verifies a valid materialized mutation can recover the renamed full-calculation formula audit state after an invalid mutation diagnostic. With rename, `request_full_calculation()`, and a dirty shifted formula pending, a rejected formula payload at an invalid coordinate leaves the diagnostic and dirty sparse state intact; a later valid `set_cell()` clears `last_edit_error()`, increases the dirty materialized count, preserves shifted materialized and original source formula audits, and `save_as()` writes the renamed catalog, shifted formula, recovered text cell, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml` or leaking the rejected formula payload. | This is diagnostic recovery hygiene only. It does not add undo/rollback history, rejected-payload staging, coordinate repair or clamping, source-name fallback, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.910 | Public-state coverage now verifies saved-session reacquire keeps the renamed full-calculation formula audit state coherent. After rename, `request_full_calculation()`, and a dirty shifted qualified formula are saved once, matching `try_worksheet("RenamedData")` reacquire stays clean, preserves shifted materialized formula audits and original source formula audits without adding handoffs, keeps the old source-name lookup unavailable, and a later valid `set_cell()` re-dirties the shared planned-name session; the second `save_as()` writes the renamed catalog, shifted formula, new text cell, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml`. | This is saved-session/reacquire hygiene only. It does not add session cloning, source-name fallback, undo/rollback history, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.911 | Public-state coverage now verifies rejected save-as preflights do not corrupt a saved/reacquired renamed full-calculation formula audit session after it is dirtied again. After the first save and matching planned-name reacquire, a later `set_cell()` dirty state survives an exact source-overwrite `save_as()` rejection with dirty materialized diagnostics, shifted materialized formula audits, original source formula audits, source package bytes, and `last_edit_error()` intact; the later safe retry records the next handoff and writes the renamed catalog, shifted formula, dirty text cell, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml`. | This is saved-session failed-save retry hygiene only. It does not add in-place save, transaction rollback, source-name fallback, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.912 | Public-state coverage now verifies invalid mutations against a clean saved/reacquired renamed full-calculation formula audit session do not dirty the materialized state, and a later valid mutation recovers it. Rejected `set_cell()` / `erase_cell()` calls populate and preserve the invalid-reference diagnostic while keeping both handles clean, materialized diagnostics empty, shifted materialized formula audits and original source formula audits readable, and rejected payloads absent; the later valid `set_cell()` clears `last_edit_error()`, re-dirties the shared planned-name session, and save/reopen writes the renamed catalog, shifted formula, recovered text cell, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml`. | This is saved-session invalid-mutation recovery hygiene only. It does not add coordinate repair or clamping, rejected-payload staging, source-name fallback, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.913 | Public-state coverage now verifies invalid reads against a clean saved/reacquired renamed full-calculation formula audit session remain read-only. Rejected row/column/A1/range/batch reads and missing-cell `get_cell()` calls keep both handles clean, leave `last_edit_error()` clear, keep materialized diagnostics empty, preserve shifted materialized and original source formula audits, and allow a later valid `set_cell()` to re-dirty the shared planned-name session; save/reopen writes the renamed catalog, shifted formula, recovered text cell, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml`. | This is saved-session invalid-read recovery hygiene only. It does not add relaxed parsing, coordinate repair or clamping, source-name fallback, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.914 | Public-state coverage now verifies invalid row/column shifts against a clean saved/reacquired renamed full-calculation formula audit session do not dirty the materialized state. Rejected `insert_rows()` / `delete_rows()` / `insert_columns()` / `delete_columns()` bounds failures preserve the shift diagnostic, keep both handles clean, keep materialized diagnostics empty, preserve shifted materialized and original source formula audits, and allow a later valid `set_cell()` to clear `last_edit_error()` and re-dirty the shared planned-name session; save/reopen writes the renamed catalog, shifted formula, recovered text cell, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml`. | This is saved-session invalid-shift recovery hygiene only. It does not add range repair or clamping, partial shift retry, rollback history, source-name fallback, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.915 | Public-state coverage now verifies missing-sheet and old-source-name worksheet lookups against a clean saved/reacquired renamed full-calculation formula audit session remain query-only. Empty optional lookups plus throwing `worksheet("Missing")` / `worksheet("Data")` failures keep both handles clean, leave `last_edit_error()` clear, keep materialized diagnostics empty, preserve shifted materialized and original source formula audits, and allow a later valid `set_cell()` to re-dirty the shared planned-name session; save/reopen writes the renamed catalog, shifted formula, recovered text cell, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml`. | This is saved-session lookup-preflight recovery hygiene only. It does not add source-name fallback, aliasing, session cloning, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.916 | Public-state coverage now verifies mismatched `WorksheetEditorOptions` access against a clean saved/reacquired renamed full-calculation formula audit session remains preflight-only. Rejected `try_worksheet("RenamedData", options)` / `worksheet("RenamedData", options)` calls keep both handles clean, leave `last_edit_error()` clear, keep materialized diagnostics empty, preserve shifted materialized and original source formula audits, and allow a later valid `set_cell()` to re-dirty the shared planned-name session; save/reopen writes the renamed catalog, shifted formula, recovered text cell, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml`. | This is saved-session option-preflight recovery hygiene only. It does not add option migration, session cloning, source-name fallback, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.917 | Public-state coverage now verifies same-sheet Patch guard failures against a clean saved/reacquired renamed full-calculation formula audit session remain guard-only. Rejected `rename_sheet("RenamedData", "BlockedData")` and `replace_sheet_data("RenamedData", ...)` calls keep both handles clean, replace `last_edit_error()` with the expected guard diagnostic, avoid replacement/materialized dirty diagnostics, preserve shifted materialized and original source formula audits, and allow a later valid `set_cell()` to clear the diagnostic and re-dirty the shared planned-name session; save/reopen writes the renamed catalog, shifted formula, recovered text cell, and `fullCalcOnLoad="1"` without creating `xl/calcChain.xml` or leaking rejected sheet names/payloads. | This is saved-session same-sheet operation-mixing guard recovery hygiene only. It does not add conflict resolution, same-sheet Patch composition with materialized sparse sessions, source-name fallback, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.918 | Public-state coverage now verifies the guard-only no-op save path for that clean saved/reacquired renamed full-calculation formula audit session. After the rejected same-sheet `rename_sheet()` / `replace_sheet_data()` sequence, a second `save_as()` with no new materialized mutation keeps both handles clean, preserves `last_edit_error()` as the guard diagnostic, keeps replacement/materialized diagnostics empty, preserves source/materialized formula audits and saved edit summaries, and writes the same renamed/fullCalc shifted worksheet bytes as the pre-guard save without leaking rejected sheet names, replacement payloads, recovery cells, or `xl/calcChain.xml`. | This is saved-session same-sheet guard no-op-save hygiene only. It does not make Patch and materialized sparse sessions composable, add a commit/rollback model, clear save-independent diagnostics, repair formulas or metadata, rebuild calcChain, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.919 | Public-state coverage now verifies invalid mutation diagnostics against the same clean saved/reacquired renamed full-calculation formula audit session also remain no-op-save safe. Rejected row-zero / column-overflow `set_cell()` calls and range-form `erase_cell()` keep both handles clean, preserve the invalid-reference `last_edit_error()`, keep materialized diagnostics empty, preserve saved edit summaries and source/materialized formula audits, and a second `save_as()` with no recovery mutation writes the same renamed/fullCalc shifted worksheet bytes as the pre-error save without leaking rejected formula payloads, recovery cells, old formula coordinates, or `xl/calcChain.xml`. | This is saved-session invalid-mutation no-op-save hygiene only. It does not add coordinate repair or clamping, rejected-payload staging, rollback, source-name fallback, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.920 | Public-state coverage now verifies invalid read preflights against that clean saved/reacquired renamed full-calculation formula audit session stay read-only through a no-op save. Rejected row/column/A1/range/batch/row_cells/column_cells reads and missing-cell `get_cell()` keep `last_edit_error()` clear, keep both handles clean, preserve saved edit summaries, catalog state, materialized diagnostics, and source/materialized formula audits, and a second `save_as()` with no recovery mutation writes the same renamed/fullCalc shifted worksheet bytes as the pre-error save without adding recovery cells, old formula coordinates, or `xl/calcChain.xml`. | This is saved-session invalid-read no-op-save hygiene only. It does not relax coordinate parsing, synthesize missing cells, add read-side diagnostics, re-materialize sessions, repair formulas or metadata, rebuild calcChain, migrate sharedStrings/styles, or add low-memory random editing. |
| In-memory operation-mixing addenda P8.921 | Public-state coverage now verifies invalid row/column shift diagnostics against that clean saved/reacquired renamed full-calculation formula audit session remain no-op-save safe. Rejected `insert_rows()` / `delete_rows()` / `insert_columns()` / `delete_columns()` bounds failures preserve the shift `last_edit_error()`, keep both handles clean, preserve saved edit summaries, catalog state, materialized diagnostics, and source/materialized formula audits, and a second `save_as()` with no recovery mutation writes the same renamed/fullCalc shifted worksheet bytes as the pre-error save without adding recovery cells, old formula coordinates, or `xl/calcChain.xml`. | This is saved-session invalid-shift no-op-save hygiene only. It does not add range repair or clamping, partial shift retry, rollback, source-name fallback, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.922 | Public-state coverage now verifies missing-sheet and old-source-name worksheet lookups against that clean saved/reacquired renamed full-calculation formula audit session remain no-op-save safe. Empty optional lookups plus throwing `worksheet("Missing")` / `worksheet("Data")` failures leave `last_edit_error()` clear, keep both handles clean, preserve saved edit summaries, catalog state, materialized diagnostics, and source/materialized formula audits, and a second `save_as()` with no recovery mutation writes the same renamed/fullCalc shifted worksheet bytes as the pre-query save without adding rejected sheet names, recovery cells, old formula coordinates, or `xl/calcChain.xml`. | This is saved-session lookup-preflight no-op-save hygiene only. It does not add source-name fallback, aliasing, missing-sheet creation, session cloning, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory operation-mixing addenda P8.923 | Public-state coverage now verifies mismatched `WorksheetEditorOptions` access against that clean saved/reacquired renamed full-calculation formula audit session remains no-op-save safe. Rejected `try_worksheet("RenamedData", options)` / `worksheet("RenamedData", options)` calls plus old-source optional lookup leave `last_edit_error()` clear, keep both handles clean, preserve saved edit summaries, catalog state, materialized diagnostics, and source/materialized formula audits, and a second `save_as()` with no recovery mutation writes the same renamed/fullCalc shifted worksheet bytes as the pre-option save without adding recovery cells, old formula coordinates, or `xl/calcChain.xml`. | This is saved-session option-preflight no-op-save hygiene only. It does not add option migration, session cloning, source-name fallback, default formula rewrite, formula evaluation or repair, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, or low-memory random editing. |
| In-memory guardrail addenda P8.924 | Public-state coverage now verifies `WorksheetEditor::set_row()` and `set_column()` memory-budget failures stay staged. With `memory_budget_bytes` set to the exact loaded sparse-store estimate, oversized row/column replacement payloads fail before erasing existing target-row or target-column records, preserve sparse counts and memory estimates, keep the editor/session clean, and leave the original source-backed cells readable. | This is sparse-store guardrail hygiene only. It does not add memory-budget auto-sizing, process-RSS accounting, style migration, sharedStrings migration, row/column metadata synchronization, formula repair, calcChain rebuild, transaction rollback, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.925 | Public-state coverage now verifies `WorksheetEditor::set_row_values()` and `set_column_values()` memory-budget failures stay staged. With `memory_budget_bytes` set to the exact loaded sparse-store estimate, oversized value-prefix payloads fail before replacing the first row/column target, preserve sparse counts and memory estimates, keep the editor/session clean, keep the diagnostic visible through `last_edit_error()`, and leave the source-backed prefix/tail cells readable. | This is sparse value-prefix guardrail hygiene only. It does not add memory-budget auto-sizing, process-RSS accounting, style migration, sharedStrings migration, row/column metadata synchronization, formula repair, calcChain rebuild, transaction rollback, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.926 | Public-state coverage now verifies `WorksheetEditor::set_row_values()` and `set_column_values()` recover from exact memory-budget failures in the same session. After oversized value-prefix payloads seed the memory-budget diagnostic without dirtying state, smaller in-budget prefix writes clear `last_edit_error()`, dirty the editor/session, stay within the sparse-store estimate, save through `save_as()`, and reopen with recovered prefix values plus preserved row/column tails while rejected payloads remain absent. | This is value-prefix guardrail recovery and save/readback hygiene only. It does not add memory-budget auto-sizing, process-RSS accounting, style migration, sharedStrings migration, row/column metadata synchronization, formula repair, calcChain rebuild, transaction rollback, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.927 | Public-state coverage now verifies `WorksheetEditor::set_row()` and `set_column()` recover from exact memory-budget failures in the same session. After oversized row/column replacement payloads seed the memory-budget diagnostic without dirtying state, smaller in-budget replacements clear `last_edit_error()`, dirty the editor/session, stay within the sparse-store estimate, save through `save_as()`, and reopen with recovered replacement values while replaced row/column tails and rejected payloads remain absent. | This is row/column replacement guardrail recovery and save/readback hygiene only. It does not add memory-budget auto-sizing, process-RSS accounting, style migration, sharedStrings migration, row/column metadata synchronization, formula repair, calcChain rebuild, transaction rollback, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.928 | Public-state coverage now verifies `WorksheetEditor::erase_row()` and `erase_column()` release exact `memory_budget_bytes` capacity for later small sparse insertions. A rejected oversized insertion first seeds the memory-budget diagnostic without dirtying state; the row/column erase clears the diagnostic, lowers the sparse memory estimate, and a later small `set_cell()` stays within budget, saves, and reopens with erased source cells absent plus preserved non-target cells. | This is erase-driven sparse-store budget-release hygiene only. It does not add memory-budget auto-sizing, process-RSS accounting, row/column metadata synchronization, tombstones, transaction rollback, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.929 | Public-state coverage now verifies `WorksheetEditor::erase_rows()` and `erase_columns()` release exact `memory_budget_bytes` capacity across inclusive row/column ranges. A rejected oversized insertion first seeds the memory-budget diagnostic without dirtying state; the range erase clears the diagnostic, lowers the sparse memory estimate, and a later small `set_cell()` stays within budget, saves, and reopens as the only represented recovery cell while erased source cells and rejected payloads remain absent. | This is range-erase sparse-store budget-release hygiene only. It does not add memory-budget auto-sizing, process-RSS accounting, row/column metadata synchronization, tombstones, transaction rollback, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.930 | Public-state coverage now verifies `WorksheetEditor::erase_cells(CellRange)` and coordinate-batch `erase_cells(...)` release exact `memory_budget_bytes` capacity for later sparse insertions. A rejected oversized insertion first seeds the memory-budget diagnostic without dirtying state; range/batch sparse erases clear the diagnostic, lower the sparse memory estimate, and a later small `set_cell()` stays within budget, saves, and reopens as the only represented recovery cell while erased source cells and rejected payloads remain absent. | This is sparse range/batch erase budget-release hygiene only. It does not add memory-budget auto-sizing, process-RSS accounting, dense range deletion, tombstones, transaction rollback, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.931 | Public-state coverage now verifies no-argument `WorksheetEditor::erase_cells()` releases exact `memory_budget_bytes` capacity for later sparse insertions. A rejected oversized insertion first seeds the memory-budget diagnostic without dirtying state; whole-store sparse erase clears the diagnostic, lowers the sparse memory estimate, and a later small `set_cell()` stays within budget, saves, and reopens as the only represented recovery cell while erased source cells and rejected payloads remain absent. | This is whole-store sparse erase budget-release hygiene only. It does not add memory-budget auto-sizing, process-RSS accounting, dense worksheet deletion, tombstones, transaction rollback, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.932 | Public-state coverage now verifies `WorksheetEditor::clear_row()` and `clear_column()` release value-payload memory within exact `memory_budget_bytes` sessions while keeping represented sparse records as explicit blanks. A rejected oversized insertion first seeds the memory-budget diagnostic without dirtying state; row/column clear clears the diagnostic, lowers the sparse memory estimate by dropping source text payloads, and a later small `set_cell()` stays within budget, saves, and reopens with blank target records plus preserved non-target cells. | This is value-clear estimate-release hygiene only. It does not release sparse record slots like erase, add memory-budget auto-sizing, process-RSS accounting, row/column metadata synchronization, tombstones, transaction rollback, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.933 | Public-state coverage now verifies `WorksheetEditor::clear_rows()` and `clear_columns()` release value-payload memory across inclusive row/column ranges while keeping all represented sparse records as explicit blanks. A rejected oversized insertion first seeds the memory-budget diagnostic without dirtying state; row/column range clear clears the diagnostic, lowers the sparse memory estimate, preserves non-target row/column cells, and a later small `set_cell()` stays within budget, saves, and reopens with blank target records plus preserved non-target cells. | This is range value-clear estimate-release hygiene only. It does not release sparse record slots like erase, add memory-budget auto-sizing, process-RSS accounting, range metadata synchronization, tombstones, transaction rollback, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.934 | Public-state coverage now verifies `WorksheetEditor::clear_cell_values(CellRange)` and coordinate-batch `clear_cell_values(...)` release value-payload memory within exact `memory_budget_bytes` sessions while preserving represented coordinates as blanks. A rejected oversized insertion first seeds the memory-budget diagnostic without dirtying state; sparse range/batch clear clears the diagnostic, lowers the sparse memory estimate, preserves missing-cell no-synthesis and non-target cells, and a later small `set_cell()` stays within budget, saves, and reopens with blank target records plus preserved non-target cells. | This is sparse range/batch value-clear estimate-release hygiene only. It does not add dense range editing, release sparse record slots like erase, add memory-budget auto-sizing, process-RSS accounting, range metadata synchronization, tombstones, transaction rollback, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.935 | Public-state coverage now verifies no-argument `WorksheetEditor::clear_cell_values()` releases all represented value payloads within exact `memory_budget_bytes` sessions while keeping every sparse record as an explicit blank. A rejected oversized insertion first seeds the memory-budget diagnostic without dirtying state; whole-store clear clears the diagnostic, lowers the sparse memory estimate, and a later small `set_cell()` stays within budget, saves, and reopens with all prior coordinates blank plus the recovery cell. | This is whole-store value-clear estimate-release hygiene only. It does not release sparse record slots like erase, add memory-budget auto-sizing, process-RSS accounting, dense worksheet deletion, tombstones, transaction rollback, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.936 | Public-state coverage now verifies that saved no-argument `WorksheetEditor::clear_cell_values()` exact-budget sessions clear dirty materialized diagnostics and can be reacquired with the same `WorksheetEditorOptions` as a clean saved session. The same editor handle and matching-option reacquire both see all prior coordinates as blanks plus the recovery cell, while pending materialized names, counts, memory, and summaries stay empty after `save_as()`. | This is post-save state hygiene for the existing whole-store value-clear exact-budget path only. It does not add option migration, session cloning, clean-session commit semantics, memory-budget auto-sizing, process-RSS accounting, transaction history, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.937 | Public-state coverage now verifies a matching-option reacquired no-argument `clear_cell_values()` exact-budget session remains no-op-save stable. After the first `save_as()` and clean reacquire, a second `save_as()` without mutation keeps both handles clean, does not add another materialized handoff, leaves materialized diagnostics empty, and writes the same decompressed package entries as the first output. | This is post-reacquire no-op-save stability for the existing whole-store value-clear exact-budget path only. It does not add clean-session commit semantics, package timestamp preservation guarantees, transaction history, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.938 | Public-state coverage now verifies mismatched `WorksheetEditorOptions` access against that clean saved/reacquired no-argument `clear_cell_values()` exact-budget session remains no-op-save safe. Rejected `try_worksheet("Data", mismatched_options)` / `worksheet("Data", mismatched_options)` keep `last_edit_error()` clear, keep both handles clean, preserve saved blank/recovery cells and catalog state, leave materialized diagnostics empty, and a later `save_as()` writes the same decompressed package entries as the first output. | This is saved-session option-preflight hygiene for the existing whole-store value-clear exact-budget path only. It does not add option migration, session cloning, relaxed guardrails, clean-session commit semantics, transaction history, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.939 | Public-state coverage now verifies missing-sheet lookups against that clean saved/reacquired no-argument `clear_cell_values()` exact-budget session remain no-op-save safe. Empty `try_worksheet("Missing", options)` plus throwing `worksheet("Missing", options)` keep `last_edit_error()` clear, keep both handles clean, preserve saved blank/recovery cells and catalog state, leave materialized diagnostics empty, and a later `save_as()` writes the same decompressed package entries as the first output. | This is saved-session missing-query hygiene for the existing whole-store value-clear exact-budget path only. It does not add missing-sheet creation, source-name fallback, aliasing, session cloning, clean-session commit semantics, transaction history, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.940 | Public-state coverage now verifies invalid read preflights against that clean saved/reacquired no-argument `clear_cell_values()` exact-budget session remain no-op-save safe. Rejected invalid row/column/A1/range/batch reads, invalid row/column snapshot reads, and valid-but-missing `get_cell()` keep `last_edit_error()` clear, keep both handles clean, preserve saved blank/recovery cells and catalog state, leave materialized diagnostics empty, and a later `save_as()` writes the same decompressed package entries as the first output. | This is saved-session invalid-read hygiene for the existing whole-store value-clear exact-budget path only. It does not add relaxed coordinate parsing, coordinate repair or clamping, missing-cell synthesis, read-side diagnostics, source-name fallback, session cloning, transaction history, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.941 | Public-state coverage now verifies invalid mutation preflights against that clean saved/reacquired no-argument `clear_cell_values()` exact-budget session remain no-op-save safe. Rejected invalid `set_cell()` and `erase_cell()` calls populate and preserve the invalid-reference `last_edit_error()` while keeping both handles clean, preserving saved blank/recovery cells and catalog state, leaving materialized diagnostics empty, and allowing a later `save_as()` to write the same decompressed package entries as the first output without leaking rejected payloads. | This is saved-session invalid-mutation hygiene for the existing whole-store value-clear exact-budget path only. It does not add coordinate repair or clamping, rejected-payload staging, rollback, source-name fallback, session cloning, transaction history, formula repair, calcChain rebuild, sharedStrings/styles migration, metadata repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.942 | Public-state coverage now verifies a later valid mutation recovers the same saved/reacquired no-argument `clear_cell_values()` exact-budget session after invalid mutation diagnostics and a no-op save. A valid `set_cell()` clears `last_edit_error()`, dirties the shared handles once, expands bounds, preserves saved blank/recovery cells, and the next `save_as()` records one additional materialized handoff whose output reopens clean with the new recovery cell. | This is diagnostic recovery hygiene for the existing whole-store value-clear exact-budget path only. It does not add undo/rollback history, rejected-payload staging, coordinate repair or clamping, transaction history, formula repair, calcChain rebuild, sharedStrings/styles migration, metadata repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.943 | Public-state coverage now verifies invalid row/column shift preflights against that recovered saved/reacquired no-argument `clear_cell_values()` exact-budget session remain no-op-save safe. Rejected `insert_rows()` / `delete_rows()` / `insert_columns()` / `delete_columns()` bounds failures preserve the shift diagnostic, keep both handles clean, preserve saved blank/recovery cells and catalog state, leave materialized diagnostics empty, and a later `save_as()` writes the same decompressed package entries as the recovery output. | This is saved-session invalid-shift hygiene for the existing whole-store value-clear exact-budget path only. It does not add range repair or clamping, partial shift retry, rollback, source-name fallback, formula repair, calcChain rebuild, sharedStrings/styles migration, metadata repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.944 | Public-state coverage now verifies a later valid row shift recovers the same no-argument `clear_cell_values()` exact-budget session after invalid shift diagnostics and a no-op save. A valid `insert_rows()` clears `last_edit_error()`, dirties the shared handles once, preserves sparse count, moves the recovered cell to its new coordinate, and the next `save_as()` records one additional materialized handoff whose output reopens clean with the shifted recovery cell. | This is shift diagnostic recovery hygiene for the existing whole-store value-clear exact-budget path only. It does not add broad structural editing semantics, metadata range repair, formula repair, calcChain rebuild, sharedStrings/styles migration, rollback, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.945 | Public-state coverage now verifies same-sheet Patch operation-mixing guards against that recovered no-argument `clear_cell_values()` exact-budget session remain no-op-save safe. Rejected same-sheet `rename_sheet()` and `replace_sheet_data()` calls preserve the materialized-session guard diagnostic, keep both handles clean, preserve saved cells and catalog state, leave materialized diagnostics empty, and a later `save_as()` writes the same decompressed package entries as the recovery output without leaking rejected sheet names or replacement payloads. | This is same-sheet Patch/materialized guard hygiene for the existing whole-store value-clear exact-budget path only. It does not make Patch and materialized sparse sessions composable, add conflict resolution, transaction history, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.946 | Public-state coverage now verifies a later valid materialized mutation recovers the same no-argument `clear_cell_values()` exact-budget session after same-sheet Patch guard diagnostics and a no-op save. A valid `set_cell()` clears the guard diagnostic, dirties the shared handles once, expands bounds, preserves prior recovery cells, and the next `save_as()` records one additional materialized handoff whose output reopens clean without leaking rejected sheet names or replacement payloads. | This is guard diagnostic recovery hygiene for the existing whole-store value-clear exact-budget path only. It does not make Patch and materialized sparse sessions composable, add conflict resolution, transaction history, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.947 | Public-state coverage now verifies `pending_worksheet_edits()` follows the saved/recovered no-argument `clear_cell_values()` exact-budget lifecycle. Each later valid materialized mutation reports one dirty-only `Data` summary with source/planned names intact, no rename/replacement flags, matching sparse count, and matching materialized memory; successful `save_as()`, matching-option reacquire, guard no-op saves, and invalid preflights leave summaries empty until the next real mutation. | This is public summary diagnostic hygiene for the existing whole-store value-clear exact-budget path only. It does not add persisted edit history, expose prior materialized handoffs as current summaries, compose Patch and materialized sparse sessions, add rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.948 | Public-state coverage now verifies the same no-argument `clear_cell_values()` exact-budget path under a queued public sheet rename. A planned-name materialized session reports one combined `pending_worksheet_edits()` summary with source name `Data`, planned name, `renamed=true`, matching dirty materialized count/memory, and no replacement flags; successful `save_as()` clears only the materialized fields while keeping the rename summary visible, clean matching-option reacquire keeps that rename-only summary, and a later valid mutation re-adds the materialized fields before the next handoff. | This is rename-context public summary diagnostic hygiene for the existing whole-store value-clear exact-budget path only. It does not add rename-aware formula repair, metadata repair, sheet catalog transaction history, Patch/materialized sparse-session composition, rollback, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.949 | Public-state coverage now verifies a rejected `save_as(source)` preflight does not flush or corrupt that renamed no-argument `clear_cell_values()` exact-budget dirty session. The source-overwrite rejection preserves the combined rename/materialized summary, planned dirty materialized name, sparse count, dirty handle contents, and pending-change count, leaves the source workbook bytes unchanged, and the later safe `save_as(output)` still records the materialized handoff. | This is rejected-save state hygiene for the existing whole-store value-clear exact-budget path only. It does not add in-place save, transaction rollback, source mutation, rename-aware formula repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.950 | Public-state coverage now verifies mismatched `WorksheetEditorOptions` access does not disturb that renamed no-argument `clear_cell_values()` exact-budget dirty session. Rejected planned-name `try_worksheet()` / `worksheet()` calls keep diagnostics clear, preserve the combined rename/materialized summary, planned dirty materialized name, sparse count, dirty handle contents, and pending-change count, and the later safe `save_as(output)` still flushes the existing materialized handoff. | This is option-preflight state hygiene for the existing whole-store value-clear exact-budget path only. It does not add option migration, relaxed guardrails, session cloning, transaction rollback, rename-aware formula repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.951 | Public-state coverage now verifies missing-sheet and old source-name lookups do not disturb that renamed no-argument `clear_cell_values()` exact-budget dirty session. Empty `try_worksheet("Missing", options)` / `try_worksheet("Data", options)` lookups and throwing `worksheet("Missing", options)` / `worksheet("Data", options)` failures keep diagnostics clear, preserve the combined rename/materialized summary, planned dirty materialized name, sparse count, dirty handle contents, and pending-change count, and the later safe `save_as(output)` still flushes the existing materialized handoff. | This is lookup-preflight state hygiene for the existing whole-store value-clear exact-budget path only. It does not add source-name fallback, sheet aliasing, missing-sheet creation, session cloning, transaction rollback, rename-aware formula repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.952 | Public-state coverage now verifies read-only catalog and pending-diagnostic queries do not disturb that renamed no-argument `clear_cell_values()` exact-budget dirty session. `source_worksheet_names()`, `worksheet_names()`, `worksheet_catalog()`, `has_source_worksheet()`, `has_worksheet()`, `pending_worksheet_edits()`, dirty materialized names/count, and dirty memory estimates preserve the combined rename/materialized state, dirty handle contents, clear diagnostics, and pending-change count, and the later safe `save_as(output)` still flushes the existing materialized handoff. | This is read-only public-state query hygiene for the existing whole-store value-clear exact-budget path only. It does not add catalog mutation, alias fallback, transaction snapshots, session cloning, rename-aware formula repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.953 | Public-state coverage now pins the lower-level materialized diagnostics for that same renamed no-argument `clear_cell_values()` exact-budget lifecycle. Dirty planned-name aggregates report the current planned sheet name, match the active borrowed handle's sparse count and memory estimate across rejected save, option mismatch, missing/source-name lookup, read-only query, and later clean-reacquire mutation windows, while successful `save_as()` and clean reacquire keep names/count/memory empty or zero until the next real mutation. | This is lower-level dirty materialized diagnostic consistency for the existing renamed whole-store value-clear exact-budget path only. It does not change memory accounting, persist historical dirty snapshots, add catalog mutation, alias fallback, session cloning, rename-aware formula repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.954 | Public-state coverage now verifies a clean matching-option reacquire after that renamed no-argument `clear_cell_values()` exact-budget save remains no-op-save stable. A second `save_as()` keeps both borrowed handles clean, does not add another materialized handoff, leaves planned-name materialized diagnostics empty, preserves the rename-only summary, and writes decompressed package entries matching the first saved output. | This is saved renamed-session no-op-save stability for the existing whole-store value-clear exact-budget path only. It does not add clean-session commit semantics, persisted edit history, package timestamp preservation guarantees, catalog mutation, alias fallback, rename-aware formula repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.955 | Public-state coverage now verifies mismatched `WorksheetEditorOptions` access against that clean saved/reacquired renamed no-argument `clear_cell_values()` exact-budget session remains no-op-save safe. Rejected planned-name `try_worksheet()` / `worksheet()` calls keep diagnostics clear, keep both borrowed handles clean, preserve the rename-only summary and empty materialized diagnostics, and a later no-op `save_as()` writes decompressed package entries matching the prior no-op output. | This is saved renamed-session option-preflight hygiene for the existing whole-store value-clear exact-budget path only. It does not add option migration, relaxed guardrails, session cloning, clean-session commit semantics, persisted edit history, catalog mutation, alias fallback, rename-aware formula repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.956 | Public-state coverage now verifies missing-sheet and old source-name lookups against that clean saved/reacquired renamed no-argument `clear_cell_values()` exact-budget session remain no-op-save safe. Empty `try_worksheet("Missing", options)` / `try_worksheet("Data", options)` lookups and throwing `worksheet()` failures keep diagnostics clear, keep both borrowed handles clean, preserve the rename-only summary and empty materialized diagnostics, and a later no-op `save_as()` writes decompressed package entries matching the prior no-op output. | This is saved renamed-session missing/source-name lookup hygiene for the existing whole-store value-clear exact-budget path only. It does not add source-name fallback, sheet aliasing, missing-sheet creation, session cloning, clean-session commit semantics, persisted edit history, catalog mutation, rename-aware formula repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.957 | Public-state coverage now verifies read-only catalog and pending-diagnostic queries against that clean saved/reacquired renamed no-argument `clear_cell_values()` exact-budget session remain no-op-save safe. Source/planned name lists, catalog entries, source/planned existence checks, pending summaries, materialized names/count, and materialized memory aggregates preserve the rename-only clean state, and a later no-op `save_as()` writes decompressed package entries matching the prior no-op output. | This is saved renamed-session read-only query hygiene for the existing whole-store value-clear exact-budget path only. It does not add catalog mutation, alias fallback, transaction snapshots, session cloning, clean-session commit semantics, persisted edit history, rename-aware formula repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.958 | Public-state coverage now verifies invalid read preflights against that clean saved/reacquired renamed no-argument `clear_cell_values()` exact-budget session remain no-op-save safe. Rejected row/column/A1/range/batch/row_cells/column_cells reads and valid-but-missing `get_cell()` keep diagnostics clear, keep both borrowed handles clean, preserve the rename-only summary and empty materialized diagnostics, and a later no-op `save_as()` writes decompressed package entries matching the prior no-op output. | This is saved renamed-session invalid-read hygiene for the existing whole-store value-clear exact-budget path only. It does not add relaxed coordinate parsing, coordinate repair or clamping, missing-cell synthesis, read-side diagnostics, source-name fallback, session cloning, clean-session commit semantics, persisted edit history, rename-aware formula repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.959 | Public `WorkbookEditor` coverage now verifies invalid mutations against a clean saved/reacquired renamed materialized diagnostics session remain no-op-save safe without adding more `public-state` shard load. Rejected `set_cell()` / `erase_cell()` calls record the invalid-reference diagnostic, keep both borrowed handles clean, preserve the rename-only summary and empty materialized diagnostics, a no-op `save_as()` writes decompressed package entries matching the prior save, and a later valid mutation clears the diagnostic and re-dirties the planned-name session. | This is saved renamed-session invalid-mutation/no-op-save hygiene for the existing materialized diagnostics path only. It does not add coordinate repair or clamping, rejected-payload staging, rollback, catalog mutation, source-name fallback, session cloning, clean-session commit semantics, persisted edit history, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.960 | Public `WorkbookEditor` coverage now verifies the basic rename-back materialized diagnostics path remains no-op-save stable after a clean matching-option reacquire. After `Data -> TransientData -> Data`, the saved materialized session is reacquired under the restored source/planned name, keeps handles clean, leaves dirty materialized diagnostics and summaries empty, and a second `save_as()` writes decompressed package entries matching the first restored-name output. | This is saved rename-back session no-op-save hygiene for the existing materialized diagnostics path only. It does not add undo/rollback semantics, clean-session commits, source mutation, transient-name aliasing, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.961 | Public `WorkbookEditor` coverage now verifies a valid mutation after that rename-back no-op save re-dirties and saves through the restored source/planned name. The post-no-op `set_cell()` leaves diagnostics clear, reports dirty materialized aggregates under `Data`, saves as one additional materialized handoff, preserves the first saved value, writes the later value, and still does not revive `TransientData`. | This is post-no-op diagnostic recovery hygiene for the existing rename-back materialized path only. It does not add commit history, undo/rollback, source mutation, transient-name aliasing, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.962 | Public `WorkbookEditor` coverage now verifies invalid reads against the clean saved/reacquired rename-back materialized session remain no-op-save safe. Rejected row/column/A1/range reads keep `last_edit_error()` clear, keep both handles clean, leave materialized diagnostics and summaries empty, and a later `save_as()` writes decompressed package entries matching the first restored-name output before the next valid mutation. | This is read-side preflight hygiene for the existing rename-back materialized path only. It does not add coordinate repair or clamping, read diagnostics, missing-cell synthesis, source reload, transient-name aliasing, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.963 | Public `WorkbookEditor` coverage now verifies invalid mutations against the same clean saved/reacquired rename-back materialized session remain no-op-save safe. Rejected `set_cell()` / `erase_cell()` calls record the invalid-reference diagnostic, keep both handles clean, leave materialized diagnostics and summaries empty, a no-op `save_as()` writes package entries matching the first restored-name output, and the later valid mutation clears the diagnostic before saving. | This is mutation-side preflight hygiene for the existing rename-back materialized path only. It does not add coordinate repair or clamping, rejected-payload staging, undo/rollback, source mutation, transient-name aliasing, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.964 | Public `WorkbookEditor` coverage now verifies lookup and option preflights against the clean saved/reacquired rename-back materialized session remain no-op-save safe. Rejected mismatched `WorksheetEditorOptions` access, missing-sheet lookups, and lookups by the transient planned name keep `last_edit_error()` clear, keep both handles clean, leave materialized diagnostics and summaries empty, preserve the restored catalog, and a later `save_as()` writes package entries matching the first restored-name output. | This is lookup/option preflight hygiene for the existing rename-back materialized path only. It does not add option migration, session cloning, missing-sheet creation, source-name fallback, transient-name aliasing, undo/rollback, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.965 | Public `WorkbookEditor` coverage now verifies read-only catalog, pending-diagnostic, and formula-audit queries against the clean saved/reacquired rename-back materialized session remain no-op-save safe. Source/planned name lists, existence checks, catalog entries, replacement/materialized diagnostics, edit summaries, and formula audit accessors keep `last_edit_error()` clear, keep both handles clean, preserve the restored `Data` catalog, leave materialized diagnostics and summaries empty, and a later `save_as()` writes package entries matching the first restored-name output. | This is read-only query hygiene for the existing rename-back materialized path only. It does not add catalog snapshots, transaction history, source-name fallback, transient-name aliasing, formula repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.966 | Public `WorkbookEditor` coverage now verifies valid missing-cell erase no-ops against the clean saved/reacquired rename-back materialized session remain no-op-save safe after an invalid mutation diagnostic. Rejected invalid mutation first records `last_edit_error()`, then valid row/column and A1 `erase_cell()` calls targeting absent cells clear the diagnostic without dirtying either handle, preserve sparse count/memory and the restored catalog, leave materialized diagnostics and summaries empty, keep missing targets absent, and a later `save_as()` writes package entries matching the first restored-name output. | This is missing-erase diagnostic cleanup for the existing rename-back materialized path only. It does not add erase tombstones, missing-cell synthesis, coordinate repair, source-name fallback, transient-name aliasing, undo/rollback, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.967 | Public-guards coverage now verifies valid `WorksheetEditor` snapshot reads after a same-sheet Patch guard failure preserve the guard diagnostic and no-op-save state. Full sparse snapshots, bounded range snapshots, strict A1 range snapshots, row/column snapshots, and coordinate-batch snapshots remain read-only, preserve sparse count/memory, keep duplicate requested coordinates in batch output, leave materialized diagnostics and pending summaries empty, and a later `save_as()` writes package entries matching the source. | This is same-sheet guard snapshot-read hygiene only. It does not add dense reads, snapshot transactions, clean-session commits, guard bypass, coordinate repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.968 | Public-guards coverage now verifies `WorksheetEditor` scalar reads after a same-sheet Patch guard failure preserve the guard diagnostic and no-op-save state. Existing-cell `try_cell()` / `get_cell()`, missing-cell `try_cell()`, missing-cell `get_cell()` failure behavior, `cell_count()`, and `estimated_memory_usage()` remain read-only, preserve sparse count/memory, leave materialized diagnostics and pending summaries empty, and a later `save_as()` writes package entries matching the source. | This is same-sheet guard scalar-read hygiene only. It does not add dense reads, missing-cell synthesis, read-side diagnostics, clean-session commits, guard bypass, coordinate repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.969 | Public-guards coverage now verifies invalid `WorksheetEditor` reads after a same-sheet Patch guard failure preserve the prior guard diagnostic and no-op-save state. Invalid row/column scalar reads, lowercase/overflow A1 reads, invalid/reversed sparse range reads, and invalid row/column snapshot reads still fail, but do not replace `last_edit_error()`, dirty the borrowed handle, expose materialized diagnostics, or change a later copy-original `save_as()`. | This is same-sheet guard invalid-read hygiene only. It does not add tolerant coordinate parsing, lowercase A1 support, missing-cell synthesis, read-side diagnostics, clean-session commits, guard bypass, coordinate repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.970 | Public-guards coverage now verifies invalid `WorksheetEditor` mutations after a same-sheet Patch guard failure replace the prior guard diagnostic while preserving no-op-save state. Rejected row-zero `set_cell()` and column-overflow `erase_cell()` update `last_edit_error()` to the invalid-coordinate diagnostic, keep the borrowed handle clean, leave materialized diagnostics and pending summaries empty, and a later copy-original `save_as()` does not leak either the rejected replacement payload or rejected mutation payload. | This is same-sheet guard invalid-mutation diagnostic hygiene only. It does not add coordinate repair or clamping, rejected-payload staging, rollback, clean-session commits, guard bypass, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.971 | Public-guards coverage now verifies invalid `WorksheetEditor` reads after a same-sheet guard failure and an invalid mutation preserve the invalid-mutation diagnostic and no-op-save state. Rejected row/column/A1/range/row snapshot/column snapshot reads still fail without replacing `last_edit_error()`, dirtying the borrowed handle, exposing materialized diagnostics, or changing a later copy-original `save_as()`; rejected replacement and mutation payloads remain absent. | This is read-after-invalid-mutation diagnostic hygiene only. It does not add read-side diagnostics, tolerant coordinate parsing, coordinate repair or clamping, rejected-payload staging, rollback, clean-session commits, guard bypass, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.972 | Public-guards coverage now verifies empty-batch `WorksheetEditor` mutation no-ops after a same-sheet guard failure clear the guard diagnostic without dirtying materialized state. Empty `set_cells()`, `append_row()`, `set_cell_values()`, `set_row_values()`, `set_column_values()`, coordinate-batch `clear_cell_values()`, and coordinate-batch `erase_cells()` leave sparse count/memory unchanged, keep pending materialized diagnostics and summaries empty, do not synthesize missing cells, and a later copy-original `save_as()` excludes the rejected replacement payload. | This is empty-input no-op diagnostic cleanup only. It does not add full-sheet clear/erase semantics, dense range writes, batch transactions, rollback, coordinate repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.973 | Public-guards coverage now verifies non-empty missing-only `WorksheetEditor` erase no-ops after a same-sheet guard failure clear the guard diagnostic without dirtying materialized state. `erase_cells(CellRange)`, strict A1 range `erase_cells()`, and coordinate-batch `erase_cells()` over absent targets preserve sparse count/memory, keep pending materialized diagnostics and summaries empty, avoid synthesizing tombstones or missing cells, and a later copy-original `save_as()` excludes the rejected replacement payload. | This is missing-only erase no-op diagnostic cleanup only. It does not add dense range deletion, full-sheet erase semantics, tombstones, batch transactions, rollback, coordinate repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.974 | Public-guards coverage now verifies non-empty missing-only `WorksheetEditor` value-clear no-ops after a same-sheet guard failure clear the guard diagnostic without dirtying materialized state. `clear_cell_values(CellRange)`, strict A1 range `clear_cell_values()`, and coordinate-batch `clear_cell_values()` over absent targets preserve sparse count/memory, keep pending materialized diagnostics and summaries empty, avoid synthesizing explicit blanks or missing cells, and a later copy-original `save_as()` excludes the rejected replacement payload. | This is missing-only value-clear no-op diagnostic cleanup only. It does not add dense range writes, explicit blank synthesis for missing cells, tombstones, batch transactions, rollback, coordinate repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.975 | Public-guards coverage now verifies single missing-cell `WorksheetEditor::clear_cell_value()` no-ops after a same-sheet guard failure clear the guard diagnostic without dirtying read-only or saved-clean materialized state. Row/column and strict A1 `clear_cell_value()` calls over absent targets preserve sparse count/memory, keep pending materialized diagnostics empty, avoid synthesizing explicit blanks or missing cells, and a later copy-original/no-additional-handoff `save_as()` excludes rejected Patch payloads. | This is single missing-cell value-clear diagnostic cleanup only. It does not add dense clear semantics, explicit blank synthesis for missing cells, tombstones, rollback, coordinate repair, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1015 | Public-state coverage now also verifies the exact `WorksheetEditorOptions::memory_budget_bytes` recovery path after a failed sparse mutation: a later valid overwrite clears diagnostics, saves successfully, and a follow-up no-op `save_as()` keeps the same clean materialized session and byte-stable package output. | This is no-op-save hygiene for the existing mutation-side memory-budget recovery path only. It does not add broader guardrail policy, session cloning, clean-session commit semantics, memory-budget auto-sizing, process-RSS accounting, transaction history, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1016 | Public-state coverage now also verifies the exact `WorksheetEditorOptions::memory_budget_bytes` source-load recovery path: a failing `try_worksheet()` materialization can later be recovered by default-options materialization and a valid overwrite, and a follow-up no-op `save_as()` keeps the same clean materialized session and byte-stable package output. | This is no-op-save hygiene for the existing source-load memory-budget recovery path only. It does not add broader guardrail policy, session cloning, clean-session commit semantics, memory-budget auto-sizing, process-RSS accounting, transaction history, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1017 | Public-state coverage now also verifies the exact `WorksheetEditorOptions::max_cells` source-load failure recovery path: a failing source materialization leaves the editor clean, a later `replace_sheet_data("Data", ...)` save succeeds, and a follow-up no-op `save_as()` keeps the same public replacement state and byte-stable package output. | This is no-op-save hygiene for the existing source-load max-cells failure recovery path only. It does not add broader guardrail policy, session cloning, clean-session commit semantics, max-cells auto-sizing, transaction history, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1018 | Public-state coverage now also verifies the exact `WorksheetEditorOptions::max_cells` recovery path after a failed sparse mutation: a later valid overwrite clears diagnostics, saves successfully, and a follow-up no-op `save_as()` keeps the same clean materialized session and byte-stable package output. | This is no-op-save hygiene for the existing mutation-side max-cells recovery path only. It does not add broader guardrail policy, session cloning, clean-session commit semantics, max-cells auto-sizing, transaction history, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1019 | Public-state coverage now also verifies single-cell erase-driven guardrail budget release remains no-op-save stable after the recovery save. Both exact `max_cells` and exact `memory_budget_bytes` paths reject an oversized insertion, erase an existing source-backed cell, insert a replacement within budget, save successfully, then write a follow-up no-op `save_as()` with clean materialized diagnostics and byte-stable package output. | This is no-op-save hygiene for the existing single-cell erase budget-release path only. It does not add broader guardrail policy, auto-sizing, process-RSS accounting, dense deletion, tombstones, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1020 | Public-state coverage now also verifies missing-cell erase after exact `max_cells` / `memory_budget_bytes` insertion failures remains no-op-save stable. The rejected insertion seeds diagnostics without dirtying state, the missing erase clears diagnostics while staying clean, the clean save excludes rejected payloads, and a second no-op `save_as()` keeps no pending handoffs, clean materialized diagnostics, and byte-stable package output. | This is no-op-save hygiene for the existing missing-erase guardrail cleanup path only. It does not add tombstones, missing-cell synthesis, dense deletion, guard bypass, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1021 | Public-state coverage now also verifies explicit blank insertion failures followed by existing-cell blank overwrite recovery remain no-op-save stable. Both exact `max_cells` and exact `memory_budget_bytes` paths reject a missing-cell blank insertion, overwrite source-backed `A1` with an explicit blank, save successfully, then write a follow-up no-op `save_as()` with clean materialized diagnostics and byte-stable package output. | This is no-op-save hygiene for the existing explicit-blank overwrite recovery path only. It does not add tombstones, dense blank synthesis, broader blank-cell policy, guard bypass, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1022 | Public-state coverage now also verifies mutation-side `last_edit_error` replacement recovery remains no-op-save stable. Invalid reference, memory-budget, and invalid-coordinate diagnostics replace one another without dirtying state; a later in-budget overwrite clears diagnostics, saves successfully, and a second no-op `save_as()` keeps clean materialized diagnostics and byte-stable package output. | This is no-op-save hygiene for the existing materialized mutation diagnostic-replacement path only. It does not add diagnostic history, transaction rollback, broader validation recovery, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1023 | Public-state coverage now also verifies mixed public-edit diagnostic replacement recovery remains no-op-save stable. A failed replacement, failed rename, and failed `WorksheetEditor` mutation replace diagnostics without dirtying state; a later `replace_sheet_data("Untouched", ...)` save succeeds, and a second no-op `save_as()` preserves the public replacement save state, pending summaries, clean materialized diagnostics, and byte-stable package output. | This is no-op-save hygiene for the existing public replacement diagnostic-recovery path only. It does not add Patch/materialized composition, guard bypass, diagnostic history, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1024 | Public-state coverage now also verifies a failed shift mutation that exceeds the exact memory budget remains no-op-save stable. The rejected `insert_rows()` formula-translation path leaves the sheet/editor clean while preserving the diagnostic; clean `save_as()` copies source entries, and a second no-op `save_as()` keeps no pending handoffs, preserves the save-state diagnostic, and keeps byte-stable package output. | This is no-op-save hygiene for the existing failed shift memory-guard path only. It does not add formula expansion policy, automatic memory-budget sizing, rollback, metadata repair, formula repair beyond existing translation attempts, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1025 | Public-state coverage now also verifies invalid-to-valid row and column shift recovery remains no-op-save stable. Invalid row/column shift attempts leave the borrowed handle clean and seed diagnostics; later valid `insert_rows()` / `insert_columns()` clears diagnostics, saves shifted sparse output, and a second no-op `save_as()` keeps clean materialized diagnostics, preserves public save state, and keeps byte-stable package output. | This is no-op-save hygiene for the existing invalid-to-valid shift recovery path only. It does not add broader shift validation policy, dense row/column operations, metadata repair, formula repair beyond existing moved-cell handling, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1026 | Public-state coverage now also verifies dirty invalid-to-valid row and column shift recovery remains no-op-save stable. A preexisting dirty sparse cell survives rejected invalid shift diagnostics, then moves with a later valid `insert_rows()` / `insert_columns()` save; a second no-op `save_as()` keeps clean materialized diagnostics, preserves public save state, and keeps byte-stable package output. | This is no-op-save hygiene for the existing dirty invalid-to-valid shift recovery path only. It does not add broader shift validation policy, dense row/column operations, cross-session transactions, metadata repair, formula repair beyond existing moved-cell handling, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1027 | Public-state coverage now also verifies a row-shift save across two dirty materialized worksheet handles remains no-op-save stable. The `Data` handle shifts source-backed and dirty sparse rows, the `Untouched` handle keeps its own dirty coordinates unchanged, the save flushes both handles, and a second no-op `save_as()` preserves clean materialized diagnostics, public save state, catalog state, and byte-stable package output. | This is no-op-save hygiene for the existing cross-handle row-shift path only. It does not add cross-session transactions, dense row operations, metadata repair, formula repair beyond existing moved-cell handling, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1028 | Public-state coverage now also verifies a column-shift save across two dirty materialized worksheet handles remains no-op-save stable. The `Data` handle shifts source-backed and dirty sparse columns, the `Untouched` handle keeps its own dirty coordinates unchanged, the save flushes both handles, and a second no-op `save_as()` preserves clean materialized diagnostics, public save state, catalog state, and byte-stable package output. | This is no-op-save hygiene for the existing cross-handle column-shift path only. It does not add cross-session transactions, dense column operations, metadata repair, formula repair beyond existing moved-cell handling, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1029 | Public-state coverage now also verifies a row-delete save across two dirty materialized worksheet handles remains no-op-save stable. The `Data` handle deletes and shifts its own source-backed and dirty sparse rows, the `Untouched` handle keeps its dirty coordinates unchanged, the save flushes both handles, and a second no-op `save_as()` preserves clean materialized diagnostics, public save state, catalog state, and byte-stable package output. | This is no-op-save hygiene for the existing cross-handle row-delete path only. It does not add cross-session transactions, dense row operations, metadata repair, formula repair beyond existing moved-cell handling, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1030 | Public-state coverage now also verifies a column-delete save across two dirty materialized worksheet handles remains no-op-save stable. The `Data` handle deletes and shifts its own source-backed and dirty sparse columns, the `Untouched` handle keeps its dirty coordinates unchanged, the save flushes both handles, and a second no-op `save_as()` preserves clean materialized diagnostics, public save state, catalog state, and byte-stable package output. | This is no-op-save hygiene for the existing cross-handle column-delete path only. It does not add cross-session transactions, dense column operations, metadata repair, formula repair beyond existing moved-cell handling, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1031 | Public-state coverage now also verifies row-shifting a dirty rich formula remains no-op-save stable. A formula cell containing mixed relative, absolute, sheet-qualified, external, range, string-literal, structured-reference-like, function-name and R1C1-like tokens moves with `insert_rows()`, persists the translated formula XML, and a second no-op `save_as()` preserves clean materialized diagnostics, public save state, catalog state, and byte-stable package output. | This is no-op-save hygiene for the existing row formula-translation path only. It does not add formula evaluation, broad parser semantics, unsupported-reference repair, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1032 | Public-state coverage now also verifies column-shifting a dirty rich formula remains no-op-save stable. A formula cell containing mixed relative, absolute, sheet-qualified, external, range, string-literal, structured-reference-like, function-name and R1C1-like tokens moves with `insert_columns()`, persists the translated formula XML, and a second no-op `save_as()` preserves clean materialized diagnostics, public save state, catalog state, and byte-stable package output. | This is no-op-save hygiene for the existing column formula-translation path only. It does not add formula evaluation, broad parser semantics, unsupported-reference repair, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1033 | Public-state coverage now also verifies row deletion that translates moved formula references to `#REF!` remains no-op-save stable. A dirty formula cell moved by `delete_rows()` persists the row-out-of-bounds `#REF!` formula XML, and a second no-op `save_as()` preserves clean materialized diagnostics, public save state, catalog state, and byte-stable package output. | This is no-op-save hygiene for the existing row formula-translation boundary only. It does not add formula evaluation, broad parser semantics, unsupported-reference repair beyond the existing translation path, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1034 | Public-state coverage now also verifies column deletion that translates moved formula references to `#REF!` remains no-op-save stable. A dirty formula cell moved by `delete_columns()` persists the column-out-of-bounds `#REF!` formula XML, and a second no-op `save_as()` preserves clean materialized diagnostics, public save state, catalog state, and byte-stable package output. | This is no-op-save hygiene for the existing column formula-translation boundary only. It does not add formula evaluation, broad parser semantics, unsupported-reference repair beyond the existing translation path, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1035 | Public-state coverage now also verifies nonzero row/column shift requests that do not touch existing sparse cells remain no-op-save stable. After prior diagnostics are cleared by clean out-of-range `insert_rows()` / `insert_columns()` / `delete_rows()` / `delete_columns()` no-ops, `save_as()` copies source entries and a second no-op `save_as()` preserves empty pending-edit state, public save state, catalog state, and byte-stable package output. | This is clean no-op shift/save hygiene only. It does not add dense row/column operations, shift planning, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1036 | Public-state coverage now also verifies invalid row/column shift validation failures remain no-op-save stable. Rejected row/column shift coordinates keep the materialized session clean, retain the last validation diagnostic, `save_as()` copies source entries, and a second no-op `save_as()` preserves empty pending-edit state, public save state, catalog state, and byte-stable package output. | This is validation-failure no-op-save hygiene for existing shift guards only. It does not add broader validation policy, coordinate repair or clamping, dense row/column operations, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1037 | Public-state coverage now also verifies a row-shift overflow failure with a preexisting dirty edge cell remains no-op-save stable. A rejected `insert_rows()` preserves the dirty `A1048576` cell and overflow diagnostic, `save_as()` persists the unshifted edge-cell worksheet, and a second no-op `save_as()` preserves clean materialized diagnostics, public save state, catalog state, and byte-stable package output. | This is no-op-save hygiene for the existing row overflow guard only. It does not add row-limit expansion, coordinate clamping, rollback beyond current dirty-state preservation, dense row operations, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1038 | Public-state coverage now also verifies a column-shift overflow failure with a preexisting dirty edge cell remains no-op-save stable. A rejected `insert_columns()` preserves the dirty `XFD1` cell and overflow diagnostic, `save_as()` persists the unshifted edge-cell worksheet, and a second no-op `save_as()` preserves clean materialized diagnostics, public save state, catalog state, and byte-stable package output. | This is no-op-save hygiene for the existing column overflow guard only. It does not add column-limit expansion, coordinate clamping, rollback beyond current dirty-state preservation, dense column operations, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1039 | Public-state coverage now also verifies the max-cells erase-and-reinsert budget-release path preserves public save state across its existing no-op `save_as()`. After erasing a source-backed cell releases one sparse record and a replacement insertion restores the count, the second save keeps clean materialized diagnostics, the prior materialized handoff count, catalog state, and byte-stable package output. | This is public save-state coverage for the existing max-cells budget-release no-op save only. It does not add new guardrail accounting, budget recovery policy, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1040 | Public-state coverage now also verifies the memory-budget erase-and-reinsert budget-release path preserves public save state across its existing no-op `save_as()`. After erasing a source-backed cell lowers the sparse memory estimate and a smaller replacement insertion stays within budget, the second save keeps clean materialized diagnostics, the prior materialized handoff count, catalog state, and byte-stable package output. | This is public save-state coverage for the existing memory-budget budget-release no-op save only. It does not add new guardrail accounting, memory compaction policy, budget recovery policy, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1041 | Public-state coverage now also verifies the max-cells and memory-budget missing-erase recovery paths preserve public save state across their existing no-op `save_as()`. A rejected insertion followed by erasing the still-missing target stays clean, clears diagnostics, keeps sparse counts/memory stable, and the second save preserves clean materialized diagnostics, catalog state, and byte-stable source-copy output. | This is public save-state coverage for existing missing-erase no-op saves only. It does not add new guardrail accounting, rollback, mutation recovery policy, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1042 | Public-state coverage now also verifies the max-cells and memory-budget explicit-blank overwrite recovery paths preserve public save state across their existing no-op `save_as()`. A rejected missing-cell blank insertion leaves the session clean, an existing-cell blank overwrite clears the diagnostic and materializes only the accepted blank cell, and the second save preserves clean materialized diagnostics, the single prior handoff, catalog state, and byte-stable package output. | This is public save-state coverage for existing explicit-blank overwrite no-op saves only. It does not add new blank-cell semantics, guardrail accounting, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1043 | Public-state coverage now also verifies the last-edit-error replacement recovery path preserves public save state across its existing no-op `save_as()`. Invalid reference, memory-budget, and coordinate failures replace prior diagnostics without dirtying the session; a later in-budget mutation clears diagnostics and materializes one accepted worksheet handoff; the second save preserves clean materialized diagnostics, catalog state, and byte-stable output. | This is public save-state coverage for the existing diagnostic-replacement recovery no-op save only. It does not add new diagnostic categories, guardrail accounting, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1044 | Public-state coverage now also verifies the memory-budget source-load failure recovery path preserves public save state across its existing no-op `save_as()`. A too-small `WorksheetEditorOptions::memory_budget_bytes` materialization fails without dirtying the editor, a later default-options materialization and overwrite saves one accepted worksheet handoff, and the second save preserves clean materialized diagnostics, catalog state, and byte-stable output. | This is public save-state coverage for the existing memory-budget source-load recovery no-op save only. It does not add new guardrail policy, budget auto-sizing, session cloning, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1045 | Public-state coverage now also verifies the memory-budget mutation failure recovery path preserves public save state across its existing no-op `save_as()`. A rejected oversized cell insertion preserves sparse state and diagnostics, a later in-budget overwrite clears diagnostics and saves one accepted worksheet handoff, and the second save preserves clean materialized diagnostics, catalog state, and byte-stable output. | This is public save-state coverage for the existing memory-budget mutation recovery no-op save only. It does not add new guardrail accounting, memory compaction, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1046 | Public-state coverage now also verifies the max-cells mutation failure recovery path preserves public save state across its existing no-op `save_as()`. A rejected missing-cell insertion over the exact source cell count preserves sparse state and diagnostics, a later existing-cell overwrite clears diagnostics and saves one accepted worksheet handoff, and the second save preserves clean materialized diagnostics, catalog state, and byte-stable output. | This is public save-state coverage for the existing max-cells mutation recovery no-op save only. It does not add new guardrail accounting, sparse-cell compaction, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1047 | Public-state coverage now also verifies the `clear_cell_values()` memory-budget release matching-option reacquire path preserves public save state across its existing no-op `save_as()`. After clearing source-backed cells releases enough estimated memory for a later insertion and a matching-options reacquire returns the saved clean session, the no-op save preserves clean materialized diagnostics, the existing materialized handoff count, and byte-stable output. | This is public save-state coverage for the existing matching-option reacquire no-op save only. It does not add new range-clear semantics, guardrail accounting, memory compaction, session cloning policy, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1048 | Public-state coverage now also verifies the basic row-shift reacquire path preserves public save state across its existing no-op `save_as()`. After `insert_rows()` shifts the sparse source cells, the first save records one materialized handoff and a same-options reacquire returns the saved clean session; the second save preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing row-shift reacquire no-op save only. It does not add new shift semantics, dense row operations, session cloning policy, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1049 | Public-state coverage now also verifies the delete-columns reacquire path preserves public save state across its existing no-op `save_as()`. After `delete_columns()` removes the source A column, shifts source-backed cells, translates the dirty formula, and shifts a dirty tail cell, the first save records one materialized handoff and a same-options reacquire returns the saved clean session; the second save preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing delete-columns reacquire no-op save only. It does not add new column-delete semantics, dense column operations, formula dependency repair beyond existing text translation, rollback, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1050 | Public-state coverage now also verifies the delete-rows reacquire path preserves public save state across its existing no-op `save_as()`. After `delete_rows()` shifts source-backed rows, translates the dirty formula, and shifts a dirty tail cell, the first save records one materialized handoff and a same-options reacquire returns the saved clean session; the second save preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing delete-rows reacquire no-op save only. It does not add new row-delete semantics, dense row operations, formula dependency repair beyond existing text translation, rollback, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1051 | Public-state coverage now also verifies the insert-columns reacquire path preserves public save state across its existing no-op `save_as()`. After `insert_columns()` shifts source-backed cells, translates the dirty formula, and shifts a dirty tail cell, the first save records one materialized handoff and a same-options reacquire returns the saved clean session; the second save preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing insert-columns reacquire no-op save only. It does not add new column-insert semantics, dense column operations, formula dependency repair beyond existing text translation, rollback, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1052 | Public-state coverage now also verifies the try-worksheet row-shift reacquire path preserves public save state across its existing no-op `save_as()`. After `insert_rows()` shifts sparse source cells, the first save records one materialized handoff and `try_worksheet()` returns the saved clean session; the second save preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing try-reacquire row-shift no-op save only. It does not add new shift semantics, dense row operations, session cloning policy, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1053 | Public-state coverage now also verifies the row-shift reacquire option-mismatch path preserves public save state across its existing no-op `save_as()`. After `insert_rows()` saves one materialized handoff, mismatched `try_worksheet()` / `worksheet()` options fail without changing diagnostics or catalog state; the second save preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing option-mismatch reacquire no-op save only. It does not add new option negotiation semantics, shift semantics, session cloning policy, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1054 | Public-state coverage now also verifies the row-shift reacquire missing-query path preserves public save state across its existing no-op `save_as()`. After `insert_rows()` saves one materialized handoff, missing-sheet `try_worksheet()` / `worksheet()` queries fail without changing diagnostics or catalog state; the second save preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing missing-query reacquire no-op save only. It does not add new sheet lookup semantics, sheet creation, shift semantics, session cloning policy, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1055 | Public-state coverage now also verifies the row-shift reacquire invalid-read path preserves public save state across its existing no-op `save_as()`. After `insert_rows()` saves one materialized handoff and a matching reacquire returns a saved clean session, invalid coordinate, A1, range, batch, row, column, and missing-cell reads fail without changing diagnostics or catalog state; the second save preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing invalid-read reacquire no-op save only. It does not add new read semantics, tolerant A1 parsing, range repair, sheet creation, shift semantics, session cloning policy, rollback, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1056 | Public-state coverage now also verifies the row-shift reacquire invalid-mutation path preserves public save state across its existing no-op `save_as()`. After `insert_rows()` saves one materialized handoff and a matching reacquire returns a saved clean session, invalid `set_cell()` / `erase_cell()` calls fail without changing catalog state or materialized counts; the second save preserves clean materialized diagnostics, the retained invalid-reference diagnostic, rejected payload absence, and byte-stable output. | This is public save-state coverage for the existing invalid-mutation reacquire no-op save only. It does not add new mutation semantics, tolerant A1 parsing, rollback, payload staging, shift semantics, session cloning policy, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1057 | Public-state coverage now also verifies the row-shift reacquire invalid-shift path preserves public save state across its existing no-op `save_as()`. After `insert_rows()` saves one materialized handoff and a matching reacquire returns a saved clean session, invalid row/column insert/delete shift calls fail without changing catalog state or materialized counts; the second save preserves clean materialized diagnostics, the retained invalid-shift diagnostic, and byte-stable output. | This is public save-state coverage for the existing invalid-shift reacquire no-op save only. It does not add new shift validation semantics, rollback, payload staging, session cloning policy, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1058 | Public-state coverage now also verifies the row-shift reacquire failed-save retry path preserves public save state across its existing no-op `save_as()`. After a saved row shift, a matching reacquire is dirtied by a column shift, source-overwrite save fails without flushing the dirty session, a safe retry records the second materialized handoff, and a final matching reacquire returns the clean combined shift state; the no-op save then preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing failed-save retry reacquire no-op save only. It does not add rollback, transaction replay, source overwrite support, new shift semantics, session cloning policy, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1059 | Public-state coverage now also verifies the row-shift reacquire path-equivalent failed-save retry path preserves public save state across its existing no-op `save_as()`. After a saved row shift, a matching reacquire is dirtied by a column shift, path-equivalent source overwrite fails without modifying the source workbook or flushing dirty state, a safe retry records the second materialized handoff, and a final matching reacquire returns the clean combined shift state; the no-op save preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing path-equivalent failed-save retry no-op save only. It does not add rollback, transaction replay, source overwrite support, path canonicalization changes, new shift semantics, session cloning policy, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1060 | Public-state coverage now also verifies the row-shift reacquire empty-output failed-save retry path preserves public save state across its existing no-op `save_as()`. After a saved row shift, a matching reacquire is dirtied by a column shift, empty output path save fails without modifying the source workbook or flushing dirty state, a safe retry records the second materialized handoff, and a final matching reacquire returns the clean combined shift state; the no-op save preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing empty-output failed-save retry no-op save only. It does not add rollback, transaction replay, empty-path recovery behavior beyond rejection, new shift semantics, session cloning policy, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1061 | Public-state coverage now also verifies the row-shift reacquire missing-parent failed-save retry path preserves public save state across its existing no-op `save_as()`. After a saved row shift, a matching reacquire is dirtied by a column shift, missing output parent save fails without creating the rejected output or flushing dirty state, a safe retry records the second materialized handoff, and a final matching reacquire returns the clean combined shift state; the no-op save preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing missing-parent failed-save retry no-op save only. It does not add rollback, transaction replay, directory creation, new shift semantics, session cloning policy, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1062 | Public-state coverage now also verifies the row-shift reacquire non-directory-parent failed-save retry path preserves public save state across its existing no-op `save_as()`. After a saved row shift, a matching reacquire is dirtied by a column shift, output path under a file parent fails while preserving the parent file and dirty state, a safe retry records the second materialized handoff, and a final matching reacquire returns the clean combined shift state; the no-op save preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing non-directory-parent failed-save retry no-op save only. It does not add rollback, transaction replay, parent-file replacement, directory creation, new shift semantics, session cloning policy, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1063 | Public-state coverage now also verifies the row-shift reacquire existing-directory failed-save retry path preserves public save state across its existing no-op `save_as()`. After a saved row shift, a matching reacquire is dirtied by a column shift, saving to an existing directory fails without replacing that directory or flushing dirty state, a safe retry records the second materialized handoff, and a final matching reacquire returns the clean combined shift state; the no-op save preserves clean materialized diagnostics, clear last-edit diagnostics, and byte-stable output. | This is public save-state coverage for the existing existing-directory failed-save retry no-op save only. It does not add rollback, transaction replay, directory replacement, directory-as-workbook output support, new shift semantics, session cloning policy, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1064 | Public-state coverage now also verifies older clean copy-original no-op saves after invalid A1 range mutations, invalid scalar cell reads, invalid row/column snapshot reads, and invalid sparse range reads preserve the public save-state snapshot. The tests capture pending counts, replacement diagnostics, and `last_edit_error()` before `save_as(output)`, then verify the no-op save preserves those values while still writing source-equivalent package entries. | This is public save-state coverage for existing invalid-preflight clean no-op saves only. It does not add tolerant coordinate parsing, coordinate repair or clamping, missing-cell synthesis, read-side mutation, new dirty-session semantics, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1065 | Public-state coverage now also verifies the saved `clear_cell_values()` memory-budget release session preserves public save state across its follow-on no-op saves after option mismatch, missing-sheet lookup, invalid reads, invalid mutations, invalid shifts, and same-sheet Patch guard attempts. Each branch captures pending counts, replacement diagnostics, and `last_edit_error()` before the no-op `save_as()`, then verifies the save preserves that public state while keeping materialized diagnostics clean and output entries stable. | This is public save-state coverage for existing saved-session preflight/guard no-op saves only. It does not add option migration, missing-sheet creation, coordinate repair or clamping, relaxed shift validation, Patch/materialized composition, new clear semantics, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1066 | Public-state coverage now also verifies the renamed saved `clear_cell_values()` memory-budget release summary path preserves public save state across plain no-op save, option-mismatch no-op save, missing-name no-op save, read-only-query no-op save, and invalid-read no-op save. Each branch captures pending counts, replacement diagnostics, and `last_edit_error()` before the no-op `save_as()`, then verifies the save preserves that public state while keeping renamed materialized diagnostics and summaries clean. | This is public save-state coverage for existing renamed saved-session no-op saves only. It does not add rename transaction history, source-name fallback, option migration, missing-sheet aliasing, tolerant reads, metadata repair, formula repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1067 | Public-state coverage now also verifies full-calculation renamed formula-audit saved-reacquire no-op saves preserve public save state after invalid mutation, invalid read, invalid shift, missing-sheet lookup, option mismatch, and same-sheet guard preflights. Each branch captures pending counts, replacement diagnostics, and `last_edit_error()` after formula audit inspection and before the second `save_as(second_output)`, then verifies the no-op save preserves that public state while keeping materialized diagnostics clean, saved edit summaries stable, and output entries byte-stable against the first save. | This is public save-state coverage for existing formula-audit saved-session no-op saves only. It does not add formula repair/evaluation, dependency rewrite, option migration, missing-sheet aliasing, same-sheet Patch/materialized composition, rename transaction history, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1068 | Public-state coverage now also verifies renamed styled formula shift reacquire sessions preserve public save state across a clean no-op save after their second successful materialized flush. The insert-row/later-insert-column, delete-column/later-insert-row, and delete-row/later-insert-column paths now capture catalog and save-state snapshots after the second save, then verify a third `save_as(noop_output)` preserves pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and byte-stable output entries. | This is public save-state coverage for existing renamed styled-formula reacquire no-op saves only. It does not add formula evaluation, broader formula rewrite, rename transaction history, new row/column shift semantics, option migration, same-sheet Patch/materialized composition, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1069 | Public-state coverage now also verifies the renamed styled formula insert-row recovery branches preserve public save state across a clean no-op save after the recovery flush. The option-mismatch, missing-sheet lookup, invalid-mutation, and invalid-read paths now capture catalog and save-state snapshots after their second successful save, then verify `save_as(noop_output)` keeps pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for existing renamed styled-formula insert-row recovery no-op saves only. It does not add option migration, missing-sheet aliasing, tolerant reads, relaxed mutation validation, formula evaluation, broader formula rewrite, new shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1070 | Public-state coverage now also verifies the renamed styled formula delete-column recovery branches preserve public save state across a clean no-op save after the recovery flush. The option-mismatch, missing-sheet lookup, invalid-mutation, and invalid-read paths now snapshot catalog/save-state after their second successful save, then verify `save_as(noop_output)` keeps pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for existing renamed styled-formula delete-column recovery no-op saves only. It does not add option migration, missing-sheet aliasing, tolerant reads, relaxed mutation validation, formula evaluation, broader formula rewrite, new shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1071 | Public-state coverage now also verifies the renamed styled formula delete-row recovery branches preserve public save state across a clean no-op save after the recovery flush. The option-mismatch, missing-sheet lookup, invalid-mutation, and invalid-read paths now snapshot catalog/save-state after their second successful save, then verify `save_as(noop_output)` keeps pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for existing renamed styled-formula delete-row recovery no-op saves only. It does not add option migration, missing-sheet aliasing, tolerant reads, relaxed mutation validation, formula evaluation, broader formula rewrite, new shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1072 | Public-state coverage now also verifies the renamed styled formula snapshot-read branches preserve public save state across a clean no-op save after the follow-on flush. The insert-row, delete-column, and delete-row snapshot-read paths now snapshot catalog/save-state after their second successful save, then verify `save_as(noop_output)` keeps pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for existing renamed styled-formula snapshot-read no-op saves only. It does not add snapshot lifetime changes, borrowed iterator exposure, dense reads, formula evaluation, broader formula rewrite, new shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1073 | Public-state coverage now also verifies the renamed styled formula failed-save safe-retry branches preserve public save state across a clean no-op save after the safe retry. The insert-row, delete-column, and delete-row source-overwrite rejection paths now snapshot catalog/save-state after the safe retry save, then verify `save_as(noop_output)` keeps pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for existing renamed styled-formula failed-save safe-retry no-op saves only. It does not add in-place save, rollback, transaction replay, source overwrite support, formula evaluation, broader formula rewrite, new shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1074 | Public-state coverage now also verifies the plain renamed planned-session failed-save safe-retry path preserves public save state across a clean no-op save before the next mutation. After exact/path-equivalent source-overwrite, empty-output, missing-parent, non-directory-parent, and existing-directory save rejections preserve the dirty renamed session, the safe retry output is snapshotted and `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stay stable. | This is public save-state coverage for the existing plain renamed planned-session failed-save safe-retry no-op save only. It does not add in-place save, rollback, transaction replay, source overwrite support, path repair, directory creation/replacement, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1075 | Public-state coverage now also verifies the plain renamed planned-session preflight branches preserve public save state across a clean no-op save after their second successful materialized flush. The option-mismatch, missing-sheet lookup, invalid-read, and invalid-mutation paths now snapshot catalog/save-state after the second save, then verify `save_as(noop_output)` keeps pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for existing plain renamed planned-session preflight no-op saves only. It does not add option migration, missing-sheet aliasing, tolerant reads, relaxed mutation validation, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1076 | Public-state coverage now also verifies the plain renamed planned-session reacquire path preserves public save state across a clean no-op save after its second successful materialized flush. The saved planned-name `try_worksheet()` reacquire path now snapshots catalog/save-state after the follow-on column shift save, then verifies `save_as(noop_output)` keeps pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing plain renamed planned-session reacquire no-op save only. It does not add session cloning policy changes, source-name fallback, rename transaction history, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1077 | Public-state coverage now also verifies the non-renamed shift handle-reuse path preserves public save state across a clean no-op save after its second successful materialized flush. The same borrowed handle performs insert-row, save, insert-column, save, then snapshots save-state and verifies `save_as(noop_output)` keeps pending counts, replacement diagnostics, `last_edit_error()`, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing same-handle shifted-session no-op save only. It does not add session cloning policy changes, source reload, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1078 | Public-state coverage now also verifies the non-renamed saved-session reacquire path preserves public save state across a clean no-op save after its second successful materialized flush. The original handle is saved after an insert-row shift, a matching reacquire performs an insert-column shift, and the second save now snapshots save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing saved-session reacquire second-flush no-op save only. It does not add session cloning policy changes, source reload, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1079 | Public-state coverage now also verifies the non-renamed optional saved-session reacquire path preserves public save state across a clean no-op save after its second successful materialized flush. The original handle is saved after an insert-row shift, a matching `try_worksheet()` reacquire performs an insert-column shift, and the second save now snapshots save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing optional saved-session reacquire second-flush no-op save only. It does not add optional-session cloning policy changes, source reload, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1080 | Public-state coverage now also verifies the non-renamed saved-session option-mismatch branch preserves public save state across a clean no-op save after its second successful materialized flush. After mismatched `WorksheetEditorOptions` fail without dirtying the saved session, a matching reacquire performs an insert-column shift and the second save now snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing saved-session option-mismatch second-flush no-op save only. It does not add option migration, tolerant option matching, source reload, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1081 | Public-state coverage now also verifies the non-renamed saved-session missing-query branch preserves public save state across a clean no-op save after its second successful materialized flush. After missing `try_worksheet()` / throwing `worksheet()` lookups fail without dirtying the saved session, a matching reacquire performs an insert-column shift and the second save now snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing saved-session missing-query second-flush no-op save only. It does not add missing-sheet creation, source-name fallback, source reload, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1082 | Public-state coverage now also verifies the non-renamed saved-session invalid-read branch preserves public save state across a clean no-op save after its second successful materialized flush. After invalid row/column/A1/range/batch read attempts fail without dirtying either shared handle, a matching reacquire performs an insert-column shift and the second save now snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing saved-session invalid-read second-flush no-op save only. It does not add tolerant invalid reads, relaxed coordinate parsing, source reload, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1083 | Public-state coverage now also verifies the non-renamed saved-session invalid-mutation branch preserves public save state across a clean no-op save after its second successful materialized flush. After invalid `set_cell()` / `erase_cell()` attempts fail without dirtying either shared handle, a later valid insert-column shift clears diagnostics and the second save now snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, rejected payload absence, and output entries stable. | This is public save-state coverage for the existing saved-session invalid-mutation second-flush no-op save only. It does not add relaxed mutation validation, rejected payload staging, source reload, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1084 | Public-state coverage now also verifies the non-renamed saved-session failed-save recovery branch preserves public save state across a clean no-op save after the safe retry flush. A rejected save over the source workbook keeps the shared shifted session dirty, a later safe `save_as(second_output)` flushes it, and the test now snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, source workbook preservation, and output entries stable. | This is public save-state coverage for the existing saved-session failed-save safe-retry no-op save only. It does not add overwrite-in-place support, rollback transactions, source reload, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1085 | Public-state coverage now also verifies the non-renamed saved-session after-failed-save retry branch preserves public save state across a clean no-op save after a later reacquire/delete third flush. The rejected source-overwrite save is safely retried, a fresh handle reuses the saved shifted session, a later row delete is flushed to `third_output`, and the test now snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, deleted-row absence, and output entries stable. | This is public save-state coverage for the existing after-failed-save retry plus later mutation third-flush no-op save only. It does not add rollback transactions, overwrite-in-place support, source reload, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1086 | Public-state coverage now also verifies the basic dirty-state and same-handle saved-session paths preserve public save state across clean no-op saves after their second successful materialized flush. The tests now snapshot catalog/save-state after a second post-save dirty mutation or same-handle edit is flushed, then `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for existing basic materialized second-flush no-op saves only. It does not add new mutation semantics, source reload, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1087 | Public-state coverage now also verifies the range-erase saved-session reacquire path preserves public save state across a clean no-op save after its second successful materialized flush. After an all represented-cell range erase is saved, a matching reacquire reuses the empty sparse state, a later C3 mutation is flushed, and the test now snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, erased-cell absence, and output entries stable. | This is public save-state coverage for the existing range-erase reacquire second-flush no-op save only. It does not add dense range semantics, source reload, formula evaluation, broader shift semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1088 | Public-state coverage now also verifies the renamed `clear_cell_values()` memory-budget release path preserves public save state across a clean no-op save after its second successful materialized flush. After the renamed sheet clears source-backed cells, saves, is reacquired cleanly, mutates E5, and saves again, the test snapshots catalog/save-state before `save_as(second_no_op_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, reopened sparse cells, and output entries stable. | This is public save-state coverage for the existing renamed clear-all memory-budget release second-flush no-op save only. It does not add rename transaction semantics, source reload, formula evaluation, broader clear-all semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1089 | Public-state coverage now also verifies the renamed full-calculation formula-audit saved-session reacquire path preserves public save state across a clean no-op save after its second successful materialized flush. After the renamed sheet requests full calculation, shifts a styled formula, saves, is reacquired cleanly, mutates C5, and saves again, the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, formula/source audits, reopened sparse cells, and output entries stable. | This is public save-state coverage for the existing renamed full-calculation formula-audit second-flush no-op save only. It does not add formula evaluation or repair, formula dependency rewriting beyond the existing shift behavior, calcChain rebuild, broader full-calculation semantics, metadata repair, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1090 | Public-state coverage now also verifies the renamed full-calculation formula-audit failed-save safe-retry path preserves public save state across a clean no-op save after the retry materialized flush. After a rejected source-overwrite `save_as(source)` preserves the dirty renamed session, the safe retry writes C5, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, source package bytes, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing renamed formula-audit failed-save safe-retry no-op save only. It does not add overwrite-in-place support, rollback transactions, formula evaluation or repair, formula dependency rewriting beyond the existing shift behavior, calcChain rebuild, metadata repair, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1091 | Public-state coverage now also verifies the renamed full-calculation formula-audit invalid-mutation recovery path preserves public save state across a clean no-op save after the recovery materialized flush. Rejected invalid `set_cell()` / `erase_cell()` calls leave the saved/reacquired session clean and preserve the diagnostic; a later valid C5 mutation clears it, saves, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing renamed formula-audit invalid-mutation recovery no-op save only. It does not add coordinate repair or clamping, rejected-payload staging, rollback transactions, formula evaluation or repair, calcChain rebuild, metadata repair, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1092 | Public-state coverage now also verifies the renamed full-calculation formula-audit invalid-read recovery path preserves public save state across a clean no-op save after the recovery materialized flush. Rejected invalid row/column/A1/range/batch/snapshot reads leave the saved/reacquired session clean and keep diagnostics clear; a later valid C5 mutation saves, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing renamed formula-audit invalid-read recovery no-op save only. It does not add tolerant coordinate parsing, coordinate repair or clamping, missing-cell synthesis, formula evaluation or repair, calcChain rebuild, metadata repair, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1093 | Public-state coverage now also verifies the renamed full-calculation formula-audit invalid-shift recovery path preserves public save state across a clean no-op save after the recovery materialized flush. Rejected invalid row/column insert/delete shifts leave the saved/reacquired session clean and preserve the diagnostic; a later valid C5 mutation clears it, saves, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing renamed formula-audit invalid-shift recovery no-op save only. It does not add tolerant shift bounds, coordinate repair or clamping, rejected-shift staging, formula evaluation or repair, calcChain rebuild, metadata repair, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1094 | Public-state coverage now also verifies the renamed full-calculation formula-audit missing-query recovery path preserves public save state across a clean no-op save after the recovery materialized flush. Missing sheet lookups and old-source-name queries leave the saved/reacquired session clean and keep diagnostics clear; a later valid C5 mutation saves, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing renamed formula-audit missing-query recovery no-op save only. It does not add missing-sheet creation, source-name fallback, query repair, formula evaluation or repair, calcChain rebuild, metadata repair, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1095 | Public-state coverage now also verifies the renamed full-calculation formula-audit option-mismatch recovery path preserves public save state across a clean no-op save after the recovery materialized flush. Rejected mismatched `WorksheetEditorOptions` reacquire attempts leave the saved/reacquired session clean and keep diagnostics clear; a later valid C5 mutation saves, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing renamed formula-audit option-mismatch recovery no-op save only. It does not add option migration, tolerant option matching, session cloning policy changes, formula evaluation or repair, calcChain rebuild, metadata repair, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1096 | Public-state coverage now also verifies the renamed full-calculation formula-audit same-sheet guard recovery path preserves public save state across a clean no-op save after the recovery materialized flush. A rejected same-sheet rename-plus-replacement guard leaves the saved/reacquired session clean and preserves the diagnostic; a later valid C5 mutation clears it, saves, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, rejected replacement absence, and output entries stable. | This is public save-state coverage for the existing renamed formula-audit same-sheet guard recovery no-op save only. It does not add Patch/materialized composition, guard bypass, conflict resolution, rejected replacement staging, formula evaluation or repair, calcChain rebuild, metadata repair, sharedStrings/styles migration, relationship repair, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1097 | Public-state coverage now also verifies the renamed full-calculation formula-audit invalid-diagnostic recovery path preserves public save state across a clean no-op save after the first recovery materialized flush. A rejected invalid formula mutation preserves the dirty materialized session and diagnostic; a later valid C5 mutation clears it, saves, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, rejected payload absence, and output entries stable. | This is public save-state coverage for the existing renamed formula-audit invalid-diagnostic recovery no-op save only. It does not add coordinate repair or clamping, rejected-payload staging, rollback transactions, formula evaluation or repair, calcChain rebuild, metadata repair, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1098 | Public-state coverage now also verifies the renamed full-calculation formula-audit first materialized flush preserves public save state across a clean no-op save. After the renamed sheet requests full calculation, shifts a styled formula, saves once, and the reopened output verifies formula-audit metadata, the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing renamed formula-audit first-flush no-op save only. It does not add formula evaluation or repair, formula dependency rewriting beyond the existing shift behavior, calcChain rebuild, broader full-calculation semantics, metadata repair, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1099 | Public-state coverage now also verifies the `get_cell()` missing-cell / explicit-blank path preserves public save state across a clean no-op save after the first materialized flush. A missing `get_cell(4, 4)` read throws without dirtying diagnostics, explicit blank D4 is saved, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, explicit blank bounds, and output entries stable. | This is public save-state coverage for the existing explicit-blank first-flush no-op save only. It does not add missing-cell synthesis, dense blank semantics, tombstone policy changes, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1100 | Public-state coverage now also verifies strict A1 overload read/mutate/save keeps public save state stable across a clean no-op save after the first materialized flush. The existing path reads source-backed A1/B1, writes D4 through A1 coordinates, erases A2, saves once, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, erased-cell absence, and output entries stable. | This is public save-state coverage for the existing strict A1 overload first-flush no-op save only. It does not add new coordinate parsing policy, lowercase A1 support, dense range semantics, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1101 | Public-state coverage now also verifies strict A1 range clear/erase mutations keep public save state stable across a clean no-op save after the first materialized flush. The existing path blanks represented B1/C3, erases represented A1/A2, treats missing-only B2:C2 erase as a successful no-op, saves once, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, blanked/erased sparse semantics, and output entries stable. | This is public save-state coverage for the existing strict A1 range mutation first-flush no-op save only. It does not add dense range writes, missing-cell synthesis, tombstones, new coordinate parsing policy, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1102 | Public-state coverage now also verifies row/column coordinate recovery keeps public save state stable across a clean no-op save after the first materialized flush. The existing path rejects row zero, column overflow, row overflow, and column zero mutations without dirtying state, then a valid row/column `set_cell(1, 1, ...)` clears diagnostics, saves once, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, rejected payload absence, and output entries stable. | This is public save-state coverage for the existing row/column coordinate recovery first-flush no-op save only. It does not add coordinate clamping or repair, broader coordinate parsing policy, rollback transactions, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1103 | Public-state coverage now also verifies the `sparse_cells()` owning snapshot path keeps public save state stable across a clean no-op save after the first materialized flush. The existing path snapshots source-backed cells, an explicit blank, and an edited cell, proves the snapshot is owning by mutating A1 afterwards, saves once, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, erased-cell absence, and output entries stable. | This is public save-state coverage for the existing `sparse_cells()` snapshot first-flush no-op save only. It does not add borrowed snapshot views, dense row/column reads, streaming sparse iterators, source reload, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1104 | Public-state coverage now also verifies the `sparse_cells(CellRange)` owning snapshot path keeps public save state stable across a clean no-op save after the first materialized flush. The existing path snapshots only active records inside a bounded range, proves the range snapshot is owning by mutating B1 afterwards, rejects invalid `CellRange` reads without dirtying diagnostics, saves once, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, in-range/out-of-range sparse semantics, and output entries stable. | This is public save-state coverage for the existing `sparse_cells(CellRange)` first-flush no-op save only. It does not add dense range reads, missing-cell synthesis, borrowed snapshot views, streaming sparse iterators, range repair, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1105 | Public-state coverage now also verifies the strict A1 `sparse_cells("B1:C3")` snapshot path keeps public save state stable across a clean no-op save after the first materialized flush. The existing path snapshots active records inside an A1 range and single-cell A1 range, proves the snapshot is owning by mutating B1 afterwards, rejects invalid A1 range strings without dirtying diagnostics, saves once, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, strict-A1 range sparse semantics, and output entries stable. | This is public save-state coverage for the existing strict A1 range snapshot first-flush no-op save only. It does not add lowercase/absolute/sheet-qualified range support, dense range reads, missing-cell synthesis, borrowed snapshot views, streaming sparse iterators, range repair, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1106 | Public-state coverage now also verifies the `sparse_cells(span<WorksheetCellReference>)` / initializer-list snapshot path keeps public save state stable across a clean no-op save after the first materialized flush. The existing path preserves caller coordinate order and duplicate coordinates, skips missing records, proves batch snapshots own values by mutating A1 afterwards, rejects invalid coordinates without dirtying diagnostics, saves once, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, `last_edit_error()`, catalog views, clean materialized diagnostics, batch sparse semantics, and output entries stable. | This is public save-state coverage for the existing coordinate-batch sparse snapshot first-flush no-op save only. It does not add dense batch reads, missing-cell synthesis, borrowed snapshot views, streaming sparse iterators, coordinate repair, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1107 | Public-state coverage now also verifies the `used_range()` dirty empty-projection path keeps public save state stable across a clean no-op save after the first materialized flush. The existing path expands/shrinks sparse bounds, preserves prior diagnostics during `used_range()` inspection after failed mutations, clears all represented cells to an empty sparse store, saves once while preserving that prior diagnostic, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, preserved `last_edit_error()`, catalog views, clean materialized diagnostics, empty bounds, and output entries stable. | This is public save-state coverage for the existing `used_range()` empty-projection first-flush no-op save only. It does not add dense range tracking, missing-cell synthesis, source reload, diagnostic clearing policy changes, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1108 | Public-state coverage now also verifies the `contains_cell()` dirty projection path keeps public save state stable across a clean no-op save after the first materialized flush. The existing path observes source-backed cells, explicit blanks, erased records, legal misses, failed-mutation diagnostic preservation, invalid read rejection without sparse-store mutation, saves once, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, preserved `last_edit_error()`, catalog views, clean materialized diagnostics, represented-cell semantics, and output entries stable. | This is public save-state coverage for the existing `contains_cell()` first-flush no-op save only. It does not add dense membership indexes, missing-cell synthesis, relaxed A1 parsing, diagnostic clearing policy changes, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1109 | Public-state coverage now also verifies the `row_cells()` / `column_cells()` dirty projection path keeps public save state stable across a clean no-op save after the first materialized flush. The existing path snapshots source-backed, explicit blank, edited same-row/same-column, other-row/other-column, missing row/column, and post-snapshot mutation semantics, saves once, and the test snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, row/column sparse ordering semantics, and output entries stable. | This is public save-state coverage for the existing row/column sparse snapshot first-flush no-op save only. It does not add dense row/column reads, missing-cell synthesis, borrowed snapshot views, streaming sparse iterators, row/column index acceleration, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1110 | Public-state coverage now also verifies the `row_cells()` / `column_cells()` invalid-read diagnostic-preservation path keeps public save state stable across a second clean no-op save. The existing path seeds an invalid mutation diagnostic without dirtying state, rejects invalid row/column snapshot reads while preserving that diagnostic, accepts valid missing row/column reads as empty snapshots, performs one clean no-op save that copies source entries, then snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, preserved `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing clean row/column invalid-read no-op-save path only. It does not add read-side diagnostics, relaxed coordinate validation, missing-cell synthesis, dense row/column reads, source reload, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1111 | Public-state coverage now also verifies the `sparse_cells(CellRange)` / strict A1 range invalid-read diagnostic-preservation path keeps public save state stable across a second clean no-op save. The existing path seeds an invalid mutation diagnostic without dirtying state, rejects invalid structural and strict-A1 sparse range reads while preserving that diagnostic, confirms a valid sparse snapshot still owns the source cells, performs one clean no-op save that copies source entries, then snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, preserved `last_edit_error()`, catalog views, clean materialized diagnostics, and output entries stable. | This is public save-state coverage for the existing clean sparse range invalid-read no-op-save path only. It does not add relaxed range parsing, range repair, read-side diagnostics, missing-cell synthesis, dense range reads, source reload, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1112 | Public-state coverage now also verifies the single-cell `erase_cell()` dirty projection path keeps public save state stable across a clean no-op save after the first materialized flush. The existing path removes one represented sparse record, shrinks the projected dimension, saves once, reopens the output to confirm the erased source cell remains absent, then snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, shrunk sparse bounds, and output entries stable. | This is public save-state coverage for the existing single-cell erase first-flush no-op save only. It does not add tombstones, dense erase semantics, metadata/range repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, undo/redo, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1113 | Public-state coverage now also verifies the range `erase_cells(CellRange)` all-represented-cell dirty projection path keeps public save state stable across a clean no-op save after the first materialized flush. The existing path erases all represented cells in the range, saves once to an empty sparse projection, reopens the output to confirm erased source cells remain absent, then snapshots catalog/save-state before `save_as(first_noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, empty sparse bounds, and output entries stable before the existing saved-session reacquire branch runs. | This is public save-state coverage for the existing range erase first-flush no-op save only. It does not add dense range semantics, tombstones, metadata/range repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, undo/redo, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1114 | Public-state coverage now also verifies the initializer-list batch overload dirty projection path keeps public save state stable across a clean no-op save after the first materialized flush. The existing path uses initializer-list `set_cells()`, `set_cell_values()`, `clear_cell_values()`, and `erase_cells()` to combine full-cell replacement, value-only insertion, explicit blank clear, duplicate-coordinate later-wins erase, and missing-coordinate no-synthesis before saving once; the added no-op branch snapshots catalog/save-state, verifies pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, byte-stable output entries, and reopened sparse values/absences remain unchanged. | This is public save-state coverage for existing initializer-list batch overload projection only. It does not add new batch transaction semantics, dense range writes/deletes, tombstones, metadata/range repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, undo/redo, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1115 | Public-state coverage now also verifies no-argument `erase_cells()` whole-store dirty projection keeps public save state stable across a clean no-op save after the first materialized flush. The existing path inserts a dirty extra cell, erases every represented sparse record, saves once to an empty projection, reopens the output to confirm source-backed and dirty cells remain absent, then snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, empty sparse bounds, byte-stable output entries, and the later invalid-mutation/reacquire branch still runs from a clean empty saved session. | This is public save-state coverage for the existing whole-store sparse erase first-flush no-op save only. It does not add dense worksheet deletion, tombstones, metadata/range repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, undo/redo, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1116 | Public-state coverage now also verifies no-argument `clear_cell_values()` whole-store dirty projection keeps public save state stable across a clean no-op save after the first materialized flush. The existing styled-source path clears every represented cell to explicit blanks, preserves the non-default source style id on the styled blank, saves once, reopens the output to confirm blank records and bounds, then snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, byte-stable output entries, and reopened styled/unstyled blank records remain unchanged before the saved-session reacquire branch mutates them. | This is public save-state coverage for the existing whole-store value-clear first-flush no-op save only. It does not add new style migration, dense worksheet clearing, tombstones, metadata/range repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, undo/redo, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1117 | Public-state coverage now also verifies invalid strict A1 range mutation diagnostics and missing-only recovery remain stable across a second clean no-op save. The existing path rejects malformed/lowercase/overflow/reversed/absolute/sheet-qualified range strings for `clear_cell_values()` and `erase_cells()` without dirtying state, clears diagnostics through valid missing-only range clear/erase calls, performs one clean copy-original `save_as(output)`, then snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, default source entries, and reopened source-backed cells remain unchanged. | This is public save-state coverage for the existing invalid strict A1 range clean no-op-save path only. It does not add relaxed range parsing, coordinate repair or clamping, missing-cell synthesis, dense range writes/deletes, tombstones, metadata/range repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1118 | Public-state coverage now also verifies invalid scalar cell read preflights preserve their prior mutation diagnostic across a second clean no-op save. The existing path seeds an invalid-coordinate mutation diagnostic, rejects invalid row/column and strict A1 scalar reads plus valid-but-missing `get_cell()` without dirtying state, performs one clean copy-original `save_as(output)` that preserves the diagnostic, then snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, preserved `last_edit_error()`, catalog views, clean materialized diagnostics, default source entries, and reopened source-backed cells remain unchanged. | This is public save-state coverage for the existing invalid scalar cell read clean no-op-save path only. It does not add relaxed coordinate parsing, read-side diagnostics, missing-cell synthesis, coordinate repair or clamping, source reload, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1119 | Public-state coverage now also verifies empty-input `append_row({})` diagnostic cleanup remains stable across repeated clean no-op saves. The existing path seeds an invalid mutation diagnostic, calls `append_row({})` on a clean materialized source worksheet to clear that diagnostic without dirtying state, performs one clean copy-original `save_as(output)`, then snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, default source entries, and reopened source-backed cells remain unchanged. | This is public save-state coverage for the existing empty `append_row({})` clean no-op-save path only. It does not add row synthesis for empty appends, append transaction semantics, sparse row metadata creation, source reload, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1120 | Public-state coverage now also verifies empty-input `set_row()` on a missing row keeps diagnostic cleanup stable across repeated clean no-op saves. The existing path seeds an invalid mutation diagnostic, calls `set_row(3, empty_row)` on a clean materialized source worksheet to clear that diagnostic without dirtying state or creating sparse row metadata, performs one clean copy-original `save_as(output)`, then snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, default source entries, and reopened source-backed cells remain unchanged. | This is public save-state coverage for the existing missing-row empty `set_row()` clean no-op-save path only. It does not change represented-row clear semantics, synthesize missing rows, create row metadata, add dense row writes, source reload, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1121 | Public-state coverage now also verifies empty-input `set_column()` on a missing column keeps diagnostic cleanup stable across repeated clean no-op saves. The existing path seeds an invalid mutation diagnostic, calls `set_column(3, empty_column)` on a clean materialized source worksheet to clear that diagnostic without dirtying state or creating sparse column metadata, performs one clean copy-original `save_as(output)`, then snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, default source entries, and reopened source-backed cells remain unchanged. | This is public save-state coverage for the existing missing-column empty `set_column()` clean no-op-save path only. It does not change represented-column clear semantics, synthesize missing columns, create column metadata, add dense column writes, source reload, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1122 | Public-state coverage now also verifies empty-input `set_row_values()` keeps diagnostic cleanup stable across repeated clean no-op saves before later invalid-row failures. The existing path seeds an invalid mutation diagnostic, calls `set_row_values(3, empty_values)` on a clean materialized source worksheet to clear that diagnostic without dirtying state or creating sparse row metadata, performs one clean copy-original `save_as(output)`, then snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, default source entries, and reopened source-backed cells remain unchanged. | This is public save-state coverage for the existing empty `set_row_values()` clean no-op-save path only. It does not add value-prefix synthesis for empty batches, dense row writes, missing-row creation, source reload, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1123 | Public-state coverage now also verifies empty-input `set_column_values()` keeps diagnostic cleanup stable across repeated clean no-op saves before later invalid-column failures. The existing path seeds an invalid mutation diagnostic, calls `set_column_values(3, empty_values)` on a clean materialized source worksheet to clear that diagnostic without dirtying state or creating sparse column metadata, performs one clean copy-original `save_as(output)`, then snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, default source entries, and reopened source-backed cells remain unchanged. | This is public save-state coverage for the existing empty `set_column_values()` clean no-op-save path only. It does not add value-prefix synthesis for empty batches, dense column writes, missing-column creation, source reload, metadata repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1124 | Public-state coverage now also verifies explicit blank insertion via `set_cell()` remains stable across a second clean no-op save after the first materialized flush and first no-op save. The existing `get_cell()` blank-semantics path inserts `D4` as an explicit blank, verifies missing-cell reads do not mutate state or diagnostics, flushes the dirty materialized worksheet once, then snapshots catalog/save-state before a first and second no-op `save_as()` verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, explicit blank output entries, and reopened source-backed cells remain unchanged. | This is public save-state coverage for the existing explicit blank insertion dirty-flush no-op-save path only. It does not add missing-cell synthesis, blank tombstones, dense worksheet clearing, metadata/range repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1125 | Public-state coverage now also verifies strict uppercase A1 read/mutate/erase overloads remain stable across a second clean no-op save after the first materialized flush and first no-op save. The existing path reads A1/B1 through A1 overloads, writes `D4`, erases `A2`, flushes once, then snapshots catalog/save-state before a first and second no-op `save_as()` verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, inserted D4 text, and erased A2 absence remain unchanged. | This is public save-state coverage for the existing strict A1 overload dirty-flush no-op-save path only. It does not add relaxed/lowercase A1 parsing, coordinate repair or clamping, missing-cell synthesis, dense range writes/deletes, metadata/range repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1126 | Public-state coverage now also verifies strict A1 range clear/erase sparse mutations remain stable across a second clean no-op save after the first materialized flush and first no-op save. The existing path inserts `C3`/`D4`, clears `B1:C3` to explicit sparse blanks, erases `A1:A2`, performs a missing-only range erase no-op, flushes once, then snapshots catalog/save-state before a first and second no-op `save_as()` verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, blanked B1/C3, preserved D4, and erased A1/A2 absence remain unchanged. | This is public save-state coverage for the existing strict A1 range sparse mutation dirty-flush no-op-save path only. It does not add relaxed/lowercase range parsing, dense range writes/deletes, missing-cell synthesis, tombstones, metadata/range repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1127 | Public-state coverage now also verifies row/column coordinate validation recovery remains stable across a second clean no-op save after the first materialized flush and first no-op save. The existing path rejects invalid row/column reads and mutations, recovers with a valid A1 overwrite, flushes once, then snapshots catalog/save-state before a first and second no-op `save_as()` verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, recovered A1 text, source-backed cells, and rejected invalid payload absence remain unchanged. | This is public save-state coverage for the existing row/column coordinate validation recovery dirty-flush no-op-save path only. It does not add coordinate repair or clamping, relaxed references, missing-cell synthesis, dense row/column writes, metadata/range repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1128 | Public-state coverage now also verifies `sparse_cells()` snapshot reads remain stable across a second clean no-op save after the first materialized flush and first no-op save. The existing path captures owning sparse snapshots, mutates A1 afterward, flushes inserted/blank/erased sparse records once, then snapshots catalog/save-state before a first and second no-op `save_as()` verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, post-snapshot A1 text, explicit blank, inserted D4, and erased A2 absence remain unchanged. | This is public save-state coverage for the existing `sparse_cells()` snapshot dirty-flush no-op-save path only. It does not add borrowed snapshot views, dense worksheet snapshots, source reload, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1129 | Public-state coverage now also verifies `sparse_cells(CellRange)` snapshot reads remain stable across a second clean no-op save after the first materialized flush and first no-op save. The existing path captures owning in-range sparse snapshots, rejects invalid ranges without diagnostics, mutates B1 afterward, flushes in-range and outside-range sparse records once, then snapshots catalog/save-state before a first and second no-op `save_as()` verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, post-snapshot B1 number, explicit blank, in-range C3, outside D4, and erased A2 absence remain unchanged. | This is public save-state coverage for the existing `sparse_cells(CellRange)` dirty-flush no-op-save path only. It does not add dense range snapshots, missing-cell synthesis, range repair or clamping, source reload, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1130 | Public-state coverage now also verifies strict A1 range `sparse_cells()` snapshot reads remain stable across a second clean no-op save after the first materialized flush and first no-op save. The existing path captures owning in-range sparse snapshots from `B1:C3`, accepts a single-cell range, rejects invalid A1 ranges without diagnostics, mutates B1 afterward, flushes in-range and outside-range sparse records once, then snapshots catalog/save-state before a first and second no-op `save_as()` verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, post-snapshot B1 number, explicit blank, in-range C3, outside D4, and erased A2 absence remain unchanged. | This is public save-state coverage for the existing strict A1 range `sparse_cells()` dirty-flush no-op-save path only. It does not add relaxed/lowercase range parsing, dense range snapshots, missing-cell synthesis, range repair or clamping, source reload, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1131 | Public-state coverage now also verifies coordinate-batch `sparse_cells()` snapshot reads remain stable across a second clean no-op save after the first materialized flush and first no-op save. The existing path captures owning snapshots through `span<WorksheetCellReference>` and initializer-list overloads, preserves input order and duplicates, rejects invalid coordinates without diagnostics, mutates A1 afterward, flushes inserted/blank/erased sparse records once, then snapshots catalog/save-state before a first and second no-op `save_as()` verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, post-snapshot A1 text, explicit blank, inserted D4, and erased A2 absence remain unchanged. | This is public save-state coverage for the existing coordinate-batch `sparse_cells()` dirty-flush no-op-save path only. It does not add duplicate coalescing, dense batch snapshots, missing-cell synthesis, coordinate repair or clamping, source reload, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1132 | Public-state coverage now also verifies `used_range()` inspection remains stable across a second clean no-op save after the first materialized flush and first no-op save. The existing path checks source-backed bounds, edited/erased sparse bounds, failed-mutation diagnostic preservation, empty-store `nullopt`, and an empty projected save, then snapshots catalog/save-state before a first and second no-op `save_as()` verify pending counts, replacement diagnostics, preserved prior diagnostic, catalog views, clean materialized diagnostics, output-entry stability, and erased A1/B1/A2 absence remain unchanged. | This is public save-state coverage for the existing `used_range()` dirty-flush no-op-save path only. It does not add worksheet metadata dimension repair, dense range tracking, source reload, range/reference repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1133 | Public-state coverage now also verifies `contains_cell()` represented-state inspection remains stable across a second clean no-op save after the first materialized flush and first no-op save. The existing path checks source-backed, missing, inserted, explicit-blank, erased, invalid row/column, and strict A1 lookup behavior while preserving a prior diagnostic, flushes the dirty projection once, then snapshots catalog/save-state before a first and second no-op `save_as()` verify pending counts, replacement diagnostics, preserved prior diagnostic, catalog views, clean materialized diagnostics, output-entry stability, inserted D4, explicit B3 blank, source-backed A1/B1, and erased A2 absence remain unchanged. | This is public save-state coverage for the existing `contains_cell()` dirty-flush no-op-save path only. It does not add dense membership indexes, missing-cell synthesis, coordinate repair or clamping, source reload, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1134 | Public-state coverage now also verifies `row_cells()` / `column_cells()` snapshot reads remain stable across a second clean no-op save after the first materialized flush and first no-op save. The existing path checks row-major and column-major snapshot ordering, explicit blanks, edited row/column records, missing row/column emptiness, owning snapshot behavior, dirty projection save, and reopened row/column readback, then snapshots catalog/save-state before a first and second no-op `save_as()` verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, edited A1/A3, explicit C1 blank, source-backed A2/B1, and outside D4 remain unchanged. | This is public save-state coverage for the existing row/column snapshot dirty-flush no-op-save path only. It does not add dense row/column snapshots, row/column metadata, missing-cell synthesis, source reload, range/reference repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1135 | Public-state coverage now also verifies single-cell `erase_cell()` materialized flush remains stable across a second clean no-op save after the first materialized flush and first no-op save. The existing path erases source-backed A2, saves the shrunk sparse projection once, reopens the output, then snapshots catalog/save-state before a first and second no-op `save_as()` verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, preserved A1/B1, shrunk bounds, and erased A2 absence remain unchanged. | This is public save-state coverage for the existing single-cell erase dirty-flush no-op-save path only. It does not add tombstones, missing-cell synthesis, dense deletion, worksheet metadata repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1136 | Public-state coverage now also verifies range `erase_cells(CellRange)` first-flush state remains stable across a second clean no-op save before the saved-session reacquire branch. The existing path erases all represented cells, saves the empty sparse projection once, performs the existing first no-op save/reopen checks, then snapshots catalog/save-state before another no-op `save_as()` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, empty sparse bounds, and erased A1/B1/A2 absence remain unchanged. | This is public save-state coverage for the existing range erase first-flush clean state only. It does not add dense range semantics, tombstones, missing-cell synthesis, metadata/range repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, undo/redo, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1137 | Public-state coverage now also verifies initializer-list batch overload dirty projection remains stable across a second clean no-op save after the first materialized flush and first no-op save. The existing path combines initializer-list `set_cells()`, `set_cell_values()`, `clear_cell_values()`, and `erase_cells()` with duplicate-coordinate later-wins behavior, explicit blank output, inserted boolean output, and missing-only clear/erase behavior, then snapshots catalog/save-state before a first and second no-op `save_as()` verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, erased A1/C3, explicit B1 blank, source-backed A2, inserted D4 boolean, and missing F6/H8 absence remain unchanged. | This is public save-state coverage for the existing initializer-list batch dirty-flush no-op-save path only. It does not add transaction batching, duplicate coalescing beyond existing later-wins behavior, dense batch writes, missing-cell synthesis, source reload, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1138 | Public-state coverage now also verifies non-empty `append_row()` dirty projection remains stable across first and second clean no-op saves after the materialized flush. The existing path appends text, number, formula, and explicit blank cells after the current sparse max row, saves the expanded sparse projection once, then snapshots catalog/save-state before two no-op `save_as()` calls verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, source-backed A1/B1/A2, appended A3/B3/C3/D3 values, and `A1:D3` bounds remain unchanged. | This is public save-state coverage for the existing non-empty append dirty-flush no-op-save path only. It does not add row metadata creation, dense append semantics, append transactions, source reload, formula evaluation, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1139 | Public-state coverage now also verifies non-empty `set_row()` dirty projection remains stable across first and second clean no-op saves after the materialized flush. The existing path replaces represented row 1 with text, number, formula, and explicit blank cells while preserving non-target row 2, saves the sparse projection once, then snapshots catalog/save-state before two no-op `save_as()` calls verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, target-row A1/B1/C1/D1 values, source-backed A2, missing B2 absence, and `A1:D2` bounds remain unchanged. | This is public save-state coverage for the existing represented-row replacement dirty-flush no-op-save path only. It does not add row metadata creation, dense row writes, row transactions, source reload, formula evaluation, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1140 | Public-state coverage now also verifies non-empty `set_column()` dirty projection remains stable across first and second clean no-op saves after the materialized flush. The existing path replaces represented column 1 with text, number, formula, and explicit blank cells while preserving non-target column B, saves the sparse projection once, then snapshots catalog/save-state before two no-op `save_as()` calls verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, target-column A1/A2/A3/A4 values, source-backed B1, missing B2 absence, and `A1:B4` bounds remain unchanged. | This is public save-state coverage for the existing represented-column replacement dirty-flush no-op-save path only. It does not add column metadata creation, dense column writes, column transactions, source reload, formula evaluation, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1141 | Public-state coverage now also verifies non-empty `set_row_values()` dirty projection remains stable across first and second clean no-op saves after the materialized flush. The existing path updates row 1 value-prefix cells with text, explicit blank, and formula values while preserving the row tail boundary and non-target row 2, saves the sparse projection once, then snapshots catalog/save-state before two no-op `save_as()` calls verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, prefix A1/B1/C1 values, source-backed A2, and `A1:C2` bounds remain unchanged. | This is public save-state coverage for the existing row value-prefix dirty-flush no-op-save path only. It does not add dense row writes, missing-row synthesis, value-prefix transactions, styled-source no-op coverage, source reload, formula evaluation, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1142 | Public-state coverage now also verifies non-empty `set_column_values()` dirty projection remains stable across first and second clean no-op saves after the materialized flush. The existing path updates column 1 value-prefix cells with text, explicit blank, and numeric values while preserving the column tail boundary and non-target column B, saves the sparse projection once, then snapshots catalog/save-state before two no-op `save_as()` calls verify pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, prefix A1/A2/A3 values, source-backed B1, and `A1:B3` bounds remain unchanged. | This is public save-state coverage for the existing column value-prefix dirty-flush no-op-save path only. It does not add dense column writes, missing-column synthesis, value-prefix transactions, styled-source no-op coverage, source reload, formula evaluation, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1143 | Public-state coverage now also verifies whole-store `clear_cell_values()` dirty projection remains stable across a second clean no-op save after the first materialized flush and first no-op save. The existing styled-source path clears every represented cell to explicit blanks, preserves the styled A1 style id and unstyled B1 blank, saves the blank sparse projection once, performs the existing first no-op save/reopen checks, then snapshots catalog/save-state before another no-op `save_as()` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, styled/unstyled blank records, and `A1:B1` bounds remain unchanged before the saved-session reacquire branch. | This is public save-state coverage for the existing whole-store value-clear dirty-flush no-op-save path only. It does not add tombstones, erase semantics, dense worksheet clears, styled-source migration beyond current style-id preservation, metadata/range repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1144 | Public-state coverage now also verifies whole-store `erase_cells()` dirty projection remains stable across a second clean no-op save after the first materialized flush and first no-op save. The existing path inserts an extra sparse cell, erases every represented record, saves the empty sparse projection once, performs the existing first no-op save/reopen checks, then snapshots catalog/save-state before another no-op `save_as()` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, catalog views, clean materialized diagnostics, output-entry stability, empty reopened sparse state, erased source cells, erased dirty D4, and no used range remain unchanged before the diagnostic-cleanup and saved-session reacquire branches. | This is public save-state coverage for the existing whole-store sparse erase dirty-flush no-op-save path only. It does not add tombstones, dense worksheet deletion, source package mutation, transaction history, metadata/range repair, source reload, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1145 | Public-state coverage now also verifies the saved-session reacquire branch after whole-store `clear_cell_values()` remains stable across a clean no-op save after its second materialized flush. The existing path clears the styled source to explicit blanks, saves and no-ops the blank projection, reacquires the saved session, writes a styled numeric A1 and text B1, saves that second dirty projection, then snapshots catalog/save-state before a no-op `save_as()` verifies both handles stay clean, pending counts and materialized diagnostics remain stable, replacement diagnostics stay empty, output entries match the reacquired save, and reopened output still has styled A1, text B1, and `A1:B1` bounds. | This is public save-state coverage for the existing whole-store value-clear saved-session reacquire path only. It does not add session cloning, source reload, style migration beyond current style-id preservation, dense worksheet clears, transaction history, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1146 | Public-state coverage now also verifies the saved-session reacquire branch after whole-store `erase_cells()` remains stable across a clean no-op save after its second materialized flush. The existing path inserts and erases an extra sparse cell, saves and no-ops the empty projection, clears diagnostics with empty no-op clears/erases, reacquires the saved empty session, appends A1/B1 values, saves that second dirty projection, then snapshots catalog/save-state before a no-op `save_as()` verifies both handles stay clean, pending counts and materialized diagnostics remain stable, replacement diagnostics stay empty, output entries match the reacquired save, and reopened output still has appended A1/B1 values with erased D4 absent. | This is public save-state coverage for the existing whole-store sparse erase saved-session reacquire path only. It does not add session cloning, source reload, tombstones, dense worksheet deletion, source package mutation, transaction history, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1147 | Public-state coverage now also verifies the whole-store `erase_cells()` exact `memory_budget_bytes` release path remains stable across a clean no-op save after its recovery materialized flush. The existing path rejects an oversized insertion under the exact source memory estimate, erases all represented records to release sparse memory, inserts A3 within budget, saves once, then snapshots catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, output-entry stability, erased source cells, rejected payload absence, and the recovery A3 value remain unchanged. | This is public save-state coverage for the existing whole-store sparse erase memory-budget recovery no-op save only. It does not add memory-budget auto-sizing, process-RSS accounting, session cloning, source reload, tombstones, dense worksheet deletion, transaction history, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1148 | Public-state coverage now also verifies the whole-store `clear_cell_values()` exact `memory_budget_bytes` release path remains stable across a clean first no-op save before its matching-option reacquire branch. The existing path rejects an oversized insertion under the exact source memory estimate, clears all represented values to explicit blanks to release payload memory, inserts D4 within budget, saves once, then snapshots catalog/save-state before `save_as(first_noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, output-entry stability, blank A1/C3 records, rejected payload absence, missing-cell absence, and the recovery D4 value remain unchanged. | This is public save-state coverage for the existing whole-store value-clear memory-budget first no-op save only. It does not add memory-budget auto-sizing, process-RSS accounting, session cloning, source reload, tombstones, dense worksheet clearing, transaction history, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1149 | Public-state coverage now also verifies single-row `clear_row()` and single-column `clear_column()` exact `memory_budget_bytes` release paths remain stable across clean no-op saves after their recovery materialized flushes. Both existing paths reject an oversized insertion under the exact source memory estimate, clear represented source values to explicit blanks to release payload memory, insert one recovery cell within budget, save once, then snapshot catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, output-entry stability, blank target row/column records, preserved non-target records, rejected payload absence, and recovery cells remain unchanged. | This is public save-state coverage for the existing row/column value-clear memory-budget no-op saves only. It does not add memory-budget auto-sizing, process-RSS accounting, row/column metadata synchronization, session cloning, source reload, tombstones, dense row/column clearing, transaction history, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1150 | Public-state coverage now also verifies row-range `clear_rows()` and column-range `clear_columns()` exact `memory_budget_bytes` release paths remain stable across clean no-op saves after their recovery materialized flushes. Both existing paths reject an oversized insertion under the exact source memory estimate, clear represented source values in inclusive row/column ranges to explicit blanks to release payload memory, insert one recovery cell within budget, save once, then snapshot catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, output-entry stability, blank target range records, preserved non-target records, rejected payload absence, and recovery cells remain unchanged. | This is public save-state coverage for the existing row/column range value-clear memory-budget no-op saves only. It does not add memory-budget auto-sizing, process-RSS accounting, range metadata synchronization, session cloning, source reload, tombstones, dense range clearing, transaction history, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1151 | Public-state coverage now also verifies sparse `clear_cell_values(CellRange)` and coordinate-batch `clear_cell_values(...)` exact `memory_budget_bytes` release paths remain stable across clean no-op saves after their recovery materialized flushes. Both existing paths reject an oversized insertion under the exact source memory estimate, clear only represented target coordinates to explicit blanks without synthesizing missing cells, insert one recovery cell within budget, save once, then snapshot catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, output-entry stability, blank target records, preserved non-target records, rejected payload absence, and recovery cells remain unchanged. | This is public save-state coverage for the existing sparse range/batch value-clear memory-budget no-op saves only. It does not add memory-budget auto-sizing, process-RSS accounting, dense range editing, missing-cell synthesis, session cloning, source reload, tombstones, transaction history, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1152 | Public-state coverage now also verifies sparse `erase_cells(CellRange)` and coordinate-batch `erase_cells(...)` exact `memory_budget_bytes` release paths remain stable across clean no-op saves after their recovery materialized flushes. Both existing paths reject an oversized insertion under the exact source memory estimate, erase represented target records without synthesizing missing cells, insert one recovery cell within budget, save once, then snapshot catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, output-entry stability, erased source cells, rejected payload absence, and recovery cells remain unchanged. | This is public save-state coverage for the existing sparse range/batch erase memory-budget no-op saves only. It does not add memory-budget auto-sizing, process-RSS accounting, dense range deletion, missing-cell synthesis, session cloning, source reload, tombstones, transaction history, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1153 | Public-state coverage now also verifies `erase_row()` and row-range `erase_rows()` exact `memory_budget_bytes` release paths remain stable across clean no-op saves after their recovery materialized flushes. Both existing paths reject an oversized insertion under the exact source memory estimate, erase represented row records to release sparse memory, insert one recovery cell within budget, save once, then snapshot catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, output-entry stability, erased row records, preserved non-target row records where applicable, rejected payload absence, and recovery cells remain unchanged. | This is public save-state coverage for the existing row erase memory-budget no-op saves only. It does not add memory-budget auto-sizing, process-RSS accounting, row metadata synchronization, dense row deletion, session cloning, source reload, tombstones, transaction history, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1154 | Public-state coverage now also verifies `erase_column()` and column-range `erase_columns()` exact `memory_budget_bytes` release paths remain stable across clean no-op saves after their recovery materialized flushes. Both existing paths reject an oversized insertion under the exact source memory estimate, erase represented column records to release sparse memory, insert one recovery cell within budget, save once, then snapshot catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, output-entry stability, erased column records, preserved non-target column records where applicable, rejected payload absence, and recovery cells remain unchanged. | This is public save-state coverage for the existing column erase memory-budget no-op saves only. It does not add memory-budget auto-sizing, process-RSS accounting, column metadata synchronization, dense column deletion, session cloning, source reload, tombstones, transaction history, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1155 | Public-state coverage now also verifies `erase_row()` and `erase_column()` exact `max_cells` budget-release paths remain stable across clean no-op saves after their recovery materialized flushes. Both existing paths start from a source-sized `max_cells` budget, erase one row or column to release sparse records, insert one recovery cell within budget, save once, then snapshot catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, output-entry stability, erased row/column records, preserved non-target records, and recovery cells remain unchanged. | This is public save-state coverage for existing row/column max-cells erase no-op saves only. It does not add max-cells auto-sizing, row/column metadata synchronization, dense row/column deletion, session cloning, source reload, tombstones, transaction history, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1156 | Public-state coverage now also verifies `append_row()`, `set_row()`, and `set_column()` exact `max_cells` recovery paths remain stable across clean no-op saves after their recovery materialized flushes. Each existing path first rejects an over-budget mutation, then uses the already supported in-budget recovery shape to save once, snapshot catalog/save-state, and call `save_as(noop_output)` while checking pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, output-entry stability, rejected payload absence, preserved non-target cells, and recovery cells remain unchanged. | This is public save-state coverage for three existing max-cells recovery no-op saves only. It does not add max-cells auto-sizing, append/row/column metadata synchronization, dense row/column editing, session cloning, source reload, tombstones, transaction history, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.1157 | Public-state coverage now also verifies `set_row()` and `set_column()` exact `memory_budget_bytes` recovery paths remain stable across clean no-op saves after their recovery materialized flushes. Both existing paths start from an exact source memory estimate, reject an oversized replacement payload, recover with a smaller in-budget replacement, save once, then snapshot catalog/save-state before `save_as(noop_output)` verifies pending counts, replacement diagnostics, clear `last_edit_error()`, clean materialized diagnostics, output-entry stability, rejected payload absence, preserved non-target cells, and recovery cells remain unchanged. | This is public save-state coverage for two existing row/column setter memory-budget no-op saves only. It does not add memory-budget auto-sizing, process-RSS accounting, row/column metadata synchronization, dense row/column editing, session cloning, source reload, tombstones, transaction history, metadata/range repair, calcChain rebuild, sharedStrings/styles migration, relationship repair, Patch/materialized composition, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.976 | Public-guards coverage now verifies single missing-cell `clear_cell_value()` no-ops clear stale diagnostics without bypassing the same-sheet Patch/materialized guard. After row/column or strict A1 missing-cell clears, later same-sheet `rename_sheet()` / `replace_sheet_data()` attempts still fail, preserve read-only or saved-clean sparse state, keep materialized diagnostics empty, and no-op `save_as()` preserves the latest guard diagnostic while excluding rejected Patch payloads. | This is guard-preservation hygiene after value-clear no-ops only. It does not add Patch/materialized composition, guard bypass, conflict resolution, rollback, metadata repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.977 | Public-guards coverage now verifies single missing-cell `clear_cell_value()` no-op recovery on one clean materialized handle does not pollute another clean handle's same-sheet Patch guard. A `Data` guard diagnostic cleared by missing-cell value-clear leaves `Data` and `Untouched` clean; later same-sheet `replace_sheet_data("Untouched", ...)` still fails with `Untouched` context, preserves sparse counts/memory, and no-op `save_as()` excludes both rejected sheet payloads. | This is cross-handle guard isolation after value-clear no-ops only. It does not add cross-sheet Patch/materialized composition, shared session transactions, conflict resolution, rollback, metadata repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.978 | Public-guards coverage now verifies single missing-cell `clear_cell_value()` no-op recovery on `Data` still permits a later scoped mutation on clean `Untouched`. The value-clear no-op clears the prior `Data` diagnostic and leaves `Data` clean; `Untouched.set_cell()` then becomes the only dirty materialized session, and `save_as()` flushes only that current dirty handle while preserving rejected `Data` Patch payload absence. | This is scoped other-handle mutation hygiene after value-clear no-ops only. It does not add cross-handle transactions, dense clear/write semantics, Patch/materialized composition, metadata repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.979 | Public-guards coverage now verifies single missing-cell `clear_cell_value()` no-op recovery before the two-clean failed-save dirty-handle path. The value-clear no-op clears the prior `Data` diagnostic without dirtying either clean handle; later valid `Data` and `Untouched` mutations dirty both handles, source-overwrite `save_as(source)` fails before flushing, and a safe `save_as()` writes both materialized handoffs without leaking the rejected `Data` Patch payload or rename. | This is failed-save retry hygiene after value-clear no-ops only. It does not add cross-handle transactions, dense clear/write semantics, Patch/materialized composition, metadata repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.980 | Public-guards coverage now verifies the two-clean failed-save retry reacquire path after a missing-cell `clear_cell_value()` no-op. Read-only and saved-clean branches clear the prior `Data` guard diagnostic without dirtying either handle, then dirty both handles, survive a failed source-overwrite `save_as(source)`, flush safely, reacquire matching-option sessions, and allow a follow-up mutation/save without leaking the rejected `Data` Patch payload or rename. | This is retry reacquire hygiene after value-clear no-ops only. It does not add cross-handle transactions, dense clear/write semantics, Patch/materialized composition, metadata repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.981 | Public-guards coverage now verifies the two-clean failed-save retry query-failure path after a missing-cell `clear_cell_value()` no-op. Read-only and saved-clean branches clear the initial `Data` diagnostic without dirtying either handle, dirty/save both handles, then prove catalog/query/option-mismatch failures after reacquire keep sessions clean before a follow-up mutation/save. | This is retry query-failure hygiene after value-clear no-ops only. It does not add cross-handle transactions, dense clear/write semantics, Patch/materialized composition, metadata repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.982 | Public-guards coverage now verifies the two-clean failed-save retry invalid-read path after a missing-cell `clear_cell_value()` no-op. Read-only and saved-clean branches clear the initial `Data` diagnostic without dirtying either handle, dirty/save both handles, then prove invalid scalar/snapshot reads after reacquire keep sessions clean before a follow-up mutation/save. | This is retry invalid-read hygiene after value-clear no-ops only. It does not add cross-handle transactions, dense clear/write semantics, Patch/materialized composition, metadata repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.983 | Public-guards coverage now verifies the two-clean failed-save retry invalid-mutation path after a missing-cell `clear_cell_value()` no-op. Read-only and saved-clean branches clear the initial `Data` diagnostic without dirtying either handle, dirty/save both handles, then prove rejected coordinate/A1 mutations after reacquire keep sessions clean, preserve diagnostics across failed source-overwrite retry, and allow a later valid mutation/save. | This is retry invalid-mutation hygiene after value-clear no-ops only. It does not add cross-handle transactions, coordinate repair or clamping, rejected-payload staging, dense clear/write semantics, Patch/materialized composition, metadata repair, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.984 | Public `WorkbookEditor` coverage now verifies valid missing-cell `clear_cell_value()` no-ops against the clean saved/reacquired rename-back materialized session remain no-op-save safe after an invalid mutation diagnostic. Rejected invalid mutation first records `last_edit_error()`, then valid row/column and A1 `clear_cell_value()` calls targeting absent cells clear the diagnostic without dirtying either handle, preserve sparse count/memory and the restored catalog, leave materialized diagnostics and summaries empty, keep missing targets absent, avoid synthesizing explicit blank cells, and a later `save_as()` writes package entries matching the first restored-name output. | This is missing value-clear diagnostic cleanup for the existing rename-back materialized path only. It does not add dense clear semantics, explicit blank synthesis for missing cells, coordinate repair, source-name fallback, transient-name aliasing, undo/rollback, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.985 | Public `WorkbookEditor` coverage now verifies the rename-back materialized session recovers after a missing-cell `clear_cell_value()` no-op and no-op save. After `Data -> TransientData -> Data`, an invalid mutation diagnostic is cleared by missing row/column and A1 value-clears, the no-op output matches the first restored-name save, then a later valid `set_cell()` re-dirties the restored `Data` session, reports matching materialized aggregates/summaries, saves as one additional handoff, preserves the first value, writes the later value, keeps missing clear targets absent, and still does not leak rejected payloads or `TransientData`. | This is post-value-clear recovery hygiene for the existing rename-back materialized path only. It does not add dense clear semantics, explicit blank synthesis for missing cells, commit history, undo/rollback, source mutation, transient-name aliasing, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.986 | Public `WorkbookEditor` coverage now verifies the rename-back missing-clear recovery state survives a failed source-overwrite `save_as(source)`. After the missing-cell `clear_cell_value()` no-op and matching no-op save, a valid `set_cell()` re-dirties the restored `Data` session; source-overwrite save fails before flushing, leaves `last_edit_error()` clear, keeps both handles dirty, preserves pending handoff count/materialized aggregates/summaries and missing targets, leaves the source package unchanged, and a later safe save writes the saved and recovered values without leaking rejected payloads or `TransientData`. | This is failed-save preflight hygiene for the existing rename-back materialized path only. It does not add source-package mutation, overwrite mode, transactional commit/rollback, dense clear semantics, explicit blank synthesis for missing cells, transient-name aliasing, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.987 | Public `WorkbookEditor` coverage now verifies the rename-back missing-clear failed-save retry remains reacquire-safe. After the failed source-overwrite save and safe retry from P8.986, a fresh `worksheet("Data")` with matching options reads the saved and recovered values from the clean restored session, keeps prior handles clean, reports no dirty materialized diagnostics or summaries, and a later follow-up mutation/save writes only the new value while preserving saved/recovered values, missing clear target absence, rejected-payload absence, and `TransientData` absence. | This is post-safe-retry reacquire/follow-up hygiene for the existing rename-back materialized path only. It does not add session cloning, source-package mutation, transactional undo/redo, dense clear semantics, explicit blank synthesis for missing cells, transient-name aliasing, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.988 | Public `WorkbookEditor` coverage now verifies mismatched `WorksheetEditorOptions` after the rename-back missing-clear failed-save safe retry are rejected without polluting the clean restored session. The mismatch leaves `last_edit_error()` clear, keeps existing handles clean, preserves pending handoff count, materialized diagnostics/summaries, catalog, saved/recovered values, and missing clear target absence; a later matching reacquire and follow-up save still write the follow-up value without leaking rejected payloads or `TransientData`. | This is post-safe-retry option-mismatch hygiene for the existing rename-back materialized path only. It does not add option migration, session cloning, source-package mutation, transactional undo/redo, dense clear semantics, explicit blank synthesis for missing cells, transient-name aliasing, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.989 | Public `WorkbookEditor` coverage now verifies missing-sheet and transient-name lookups after the rename-back missing-clear failed-save safe retry do not pollute the clean restored session. Missing and `TransientData` optional lookups return empty, throwing lookups fail, diagnostics remain clear, existing handles stay clean, pending handoff/materialized diagnostics/summaries/catalog/saved values/missing target absence are preserved, and a later matching reacquire plus follow-up save still excludes rejected payloads and `TransientData`. | This is post-safe-retry lookup hygiene for the existing rename-back materialized path only. It does not add source-name fallback, aliasing, missing-sheet creation, session cloning, source-package mutation, transactional undo/redo, dense clear semantics, explicit blank synthesis for missing cells, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.990 | Public `WorkbookEditor` coverage now verifies invalid read preflights after the rename-back missing-clear failed-save safe retry do not pollute the clean restored session. Invalid row/column scalar reads, malformed or overflowing A1 reads, invalid/reversed range snapshots, and invalid row/column snapshots leave diagnostics clear, keep handles clean, preserve pending handoff/materialized diagnostics/summaries/catalog/saved values/sparse diagnostics/missing target absence, and a later matching reacquire plus follow-up save still excludes rejected payloads and `TransientData`. | This is post-safe-retry invalid-read hygiene for the existing rename-back materialized path only. It does not add coordinate repair, range clamping, session cloning, source-package mutation, transactional undo/redo, dense clear semantics, explicit blank synthesis for missing cells, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.991 | Public `WorkbookEditor` coverage now verifies invalid mutation preflights after the rename-back missing-clear failed-save safe retry preserve the clean restored session. Rejected row/column/A1/clear operations record diagnostics without dirtying handles, changing pending handoff/materialized diagnostics/summaries/catalog/saved values/sparse diagnostics/missing target absence, or changing no-op save output; a later matching reacquire and valid mutation clear diagnostics and save without leaking rejected payloads or `TransientData`. | This is post-safe-retry invalid-mutation hygiene for the existing rename-back materialized path only. It does not add coordinate repair, range clamping, tombstones, dense clear semantics, session cloning, source-package mutation, transactional undo/redo, explicit blank synthesis for missing cells, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.992 | Public `WorkbookEditor` coverage now verifies successful missing-cell `clear_cell_value()` no-ops after the rename-back missing-clear failed-save safe retry and retry-side invalid mutation diagnostics. The valid missing clears remove the diagnostic without dirtying handles, changing pending handoff/materialized diagnostics/summaries/catalog/saved values/sparse diagnostics/missing target absence, or changing no-op save output; later matching reacquire and valid mutation/save still exclude rejected payloads and `TransientData`. | This is post-safe-retry diagnostic-cleanup hygiene for the existing rename-back materialized path only. It does not add tombstones, dense clear semantics, blank synthesis for absent cells, coordinate repair, range clamping, session cloning, source-package mutation, transactional undo/redo, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.993 | Public `WorkbookEditor` coverage now verifies missing-sheet and transient-name lookups immediately after the P8.992 retry-side missing-cell clear cleanup. Optional `try_worksheet()` calls return empty and throwing `worksheet()` calls fail while keeping diagnostics clear, handles clean, pending handoff/materialized diagnostics/summaries/catalog/saved values/sparse diagnostics/missing target absence, and the later no-op save output unchanged. | This is post-cleanup lookup-failure hygiene for the existing rename-back materialized path only. It does not add source-name fallback, transient-name aliasing, missing-sheet creation, session cloning, source-package mutation, transactional undo/redo, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.994 | Public `WorkbookEditor` coverage now verifies invalid read preflights immediately after the P8.992 retry-side missing-cell clear cleanup and P8.993 lookup failures. Invalid row/column/A1/range/row snapshot/column snapshot reads fail while keeping diagnostics clear, handles clean, pending handoff/materialized diagnostics/summaries/catalog/saved values/sparse diagnostics/missing target absence, and the later no-op save output unchanged. | This is post-cleanup invalid-read hygiene for the existing rename-back materialized path only. It does not add tolerant coordinate parsing, coordinate repair or clamping, read-side diagnostics, missing-cell synthesis, source-name fallback, session cloning, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.995 | Public `WorkbookEditor` coverage now verifies invalid mutation preflights after the P8.992 retry-side missing-cell clear cleanup, P8.993 lookup failures, and P8.994 invalid reads. Rejected row/column/A1/clear operations record diagnostics while keeping handles clean, pending handoff/materialized diagnostics/summaries/catalog/saved values/sparse diagnostics/missing target absence stable, and a no-op save still writes bytes equivalent to the retry output. | This is post-cleanup invalid-mutation hygiene for the existing rename-back materialized path only. It does not add coordinate repair or clamping, tombstones, dense clear semantics, missing-cell synthesis, source-name fallback, session cloning, source-package mutation, transactional undo/redo, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.996 | Public `WorkbookEditor` coverage now verifies missing-cell value-clear no-ops can clear diagnostics after the P8.995 post-cleanup invalid mutations. The clear no-ops remove `last_edit_error()` without dirtying handles, changing pending handoff/materialized diagnostics/summaries/catalog/saved values/sparse diagnostics/missing target absence, or changing no-op save output. | This is post-cleanup diagnostic-clear hygiene for the existing rename-back materialized path only. It does not add tombstones, dense clear semantics, blank synthesis for absent cells, coordinate repair or clamping, missing-cell creation, source-name fallback, session cloning, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.997 | Public `WorkbookEditor` coverage now verifies a matching-option `worksheet("Data")` reacquire after the P8.996 diagnostic cleanup remains a read-only clean session. Reacquire preserves clear diagnostics, clean existing handles, pending handoff/materialized diagnostics/summaries/catalog/saved values/sparse diagnostics/missing target absence, and a no-op save still writes bytes equivalent to the retry output before the next valid mutation. | This is post-cleanup matching-reacquire hygiene for the existing rename-back materialized path only. It does not add session cloning, source-name fallback, transient-name aliasing, source-package mutation, transactional undo/redo, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.998 | Public `WorkbookEditor` coverage now verifies the first valid follow-up mutation after the P8.997 matching reacquire re-exposes correct materialized dirty diagnostics. The mutation dirties all borrowed handles, reports the restored `Data` name, exposes aggregate dirty cell/memory totals, and emits one materialized-only worksheet summary with restored source/planned names before save clears diagnostics again. | This is post-cleanup dirty-diagnostic recovery for the existing rename-back materialized path only. It does not add transactional history, session cloning, metadata repair, Patch/materialized sparse-session composition, formula/range/table sync, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory guardrail addenda P8.999 | Public `WorkbookEditor` coverage now verifies a clean matching-option `worksheet("Data")` reacquire after the P8.998 dirty follow-up remains no-op-save stable. The reacquired handle stays clean, the first saved shift output is reused byte-for-byte, pending materialized diagnostics remain empty, and a later no-op `save_as()` does not add another materialized handoff before the next valid mutation. | This is no-op-save stability for the existing rename-back materialized path only. It does not add session cloning, source-name fallback, transient-name aliasing, source-package mutation, transactional undo/redo, metadata repair, Patch/materialized sparse-session composition, calcChain rebuild, sharedStrings/styles migration, or low-memory large-file random editing. |
| In-memory shift addenda P8.1000 | Public-state coverage now verifies the same clean reacquire no-op-save stability for non-renamed delete-side column shifts. After `WorksheetEditor::delete_columns()` saves the shifted source-backed number, translated formula, and shifted dirty tail, a matching `worksheet("Data")` reacquire stays clean, reuses the saved sparse state, keeps pending materialized diagnostics empty, and a later no-op `save_as()` reuses the first saved delete-column output byte-for-byte without adding another handoff. | This is delete-column saved-session no-op-save stability only. It does not add session cloning, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1001 | Public-state coverage now verifies the same clean reacquire no-op-save stability for non-renamed delete-side row shifts. After `WorksheetEditor::delete_rows()` saves the shifted source-backed row, translated formula, and shifted dirty tail, a matching `worksheet("Data")` reacquire stays clean, reuses the saved sparse state, keeps pending materialized diagnostics empty, and a later no-op `save_as()` reuses the first saved delete-row output byte-for-byte without adding another handoff. | This is delete-row saved-session no-op-save stability only. It does not add session cloning, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1002 | Public-state coverage now verifies direct clean reacquire no-op-save stability for non-renamed insert-side column shifts. After `WorksheetEditor::insert_columns()` saves shifted source-backed cells, a translated formula, and a shifted dirty tail, a matching `worksheet("Data")` reacquire stays clean, reuses the saved sparse state, keeps pending materialized diagnostics empty, and a later no-op `save_as()` reuses the first saved insert-column output byte-for-byte without adding another handoff. | This is insert-column saved-session no-op-save stability only. It does not add session cloning, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1003 | Public-state coverage now verifies optional `try_worksheet("Data")` clean reacquire no-op-save stability for the saved insert-row shift path. The optional reacquired handle stays clean, reuses the saved shifted sparse state, keeps pending materialized diagnostics empty, and a later no-op `save_as()` reuses the first saved output byte-for-byte without adding another handoff. | This is optional-reacquire saved-session no-op-save stability only. It does not add session cloning, source reload, missing-sheet creation, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1004 | Public-state coverage now verifies mismatched `WorksheetEditorOptions` failures after a saved shifted session do not poison the next no-op save. Rejected `try_worksheet("Data", options)` / `worksheet("Data", options)` calls leave `last_edit_error()` clear, keep the saved handle clean, keep pending materialized diagnostics empty, preserve catalog views, and a later no-op `save_as()` reuses the first saved shifted output byte-for-byte. | This is option-mismatch/no-op-save hygiene for saved materialized sessions only. It does not add option migration, session cloning, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1005 | Public-state coverage now verifies missing-sheet lookup failures after a saved shifted session do not poison the next no-op save. Missing `try_worksheet("Missing")` and throwing `worksheet("Missing")` calls leave `last_edit_error()` clear, keep the saved handle clean, keep pending materialized diagnostics empty, preserve catalog views, and a later no-op `save_as()` reuses the first saved shifted output byte-for-byte. | This is missing-query/no-op-save hygiene for saved materialized sessions only. It does not add missing-sheet creation, session cloning, source-name fallback, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1006 | Public-state coverage now verifies invalid read failures after a saved shifted session do not poison the next no-op save. Rejected row/column, A1, range, row/column snapshot, coordinate-batch, and valid-missing `get_cell()` reads leave both saved/reacquired handles clean, keep `last_edit_error()` clear, preserve catalog views and materialized diagnostics, and a later no-op `save_as()` reuses the first saved shifted output byte-for-byte. | This is invalid-read/no-op-save hygiene for saved materialized sessions only. It does not add tolerant coordinate parsing, range clamping, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1007 | Public-state coverage now verifies invalid mutation failures after a saved shifted session do not poison the next no-op save. Rejected row/column/A1 `set_cell()` and `erase_cell()` calls record the invalid-reference diagnostic, keep saved/reacquired handles clean, preserve catalog views and materialized diagnostics, and a later no-op `save_as()` preserves that diagnostic while reusing the first saved shifted output byte-for-byte without leaking rejected payloads. | This is invalid-mutation/no-op-save hygiene for saved materialized sessions only. It does not add coordinate repair or clamping, rejected-payload staging, rollback, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1008 | Public-state coverage now verifies invalid row/column shift failures after a saved shifted session do not poison the next no-op save. Rejected `insert_rows()` / `delete_rows()` / `insert_columns()` / `delete_columns()` bounds failures record the shift diagnostic, keep saved/reacquired handles clean, preserve catalog views and materialized diagnostics, and a later no-op `save_as()` preserves that diagnostic while reusing the first saved shifted output byte-for-byte. | This is invalid-shift/no-op-save hygiene for saved materialized sessions only. It does not add range clamping, partial structural edits, rollback, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1009 | Public-state coverage now verifies a clean matching reacquire after a failed-save safe retry does not poison the next no-op save. After a saved insert-row shift is dirtied again by an insert-column shift, rejected source-overwrite `save_as()` preserves dirty state, the safe retry writes the combined shifted sparse state, matching `worksheet("Data")` reacquire stays clean, and a later no-op `save_as()` reuses the retry output byte-for-byte without adding another handoff. | This is post-safe-retry no-op-save stability for saved materialized shift sessions only. It does not add overwrite mode, rollback, session cloning, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1010 | Public-state coverage now verifies the same post-safe-retry no-op-save stability for path-equivalent source-overwrite rejection. After a saved insert-row shift is dirtied again by an insert-column shift, rejected `save_as(source.parent_path() / "." / source.filename())` preserves dirty state, safe retry writes the combined shifted sparse state, matching `worksheet("Data")` reacquire stays clean, and a later no-op `save_as()` reuses the retry output byte-for-byte without adding another handoff. | This is path-equivalent failed-save retry/no-op-save stability for saved materialized shift sessions only. It does not add overwrite mode, path repair, rollback, session cloning, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1011 | Public-state coverage now verifies the same post-safe-retry no-op-save stability for empty output path rejection. After a saved insert-row shift is dirtied again by an insert-column shift, rejected `save_as(std::filesystem::path())` preserves dirty state, safe retry writes the combined shifted sparse state, matching `worksheet("Data")` reacquire stays clean, and a later no-op `save_as()` reuses the retry output byte-for-byte without adding another handoff. | This is empty-output failed-save retry/no-op-save stability for saved materialized shift sessions only. It does not add default output paths, output path repair, rollback, session cloning, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1012 | Public-state coverage now verifies the same post-safe-retry no-op-save stability for missing-parent output rejection. After a saved insert-row shift is dirtied again by an insert-column shift, rejected `save_as()` into a missing parent preserves dirty state, safe retry writes the combined shifted sparse state, matching `worksheet("Data")` reacquire stays clean, and a later no-op `save_as()` reuses the retry output byte-for-byte without adding another handoff. | This is missing-parent failed-save retry/no-op-save stability for saved materialized shift sessions only. It does not add parent-path creation, output path repair, rollback, session cloning, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1013 | Public-state coverage now verifies the same post-safe-retry no-op-save stability for non-directory-parent output rejection. After a saved insert-row shift is dirtied again by an insert-column shift, rejected `save_as()` into a non-directory parent file preserves dirty state, safe retry writes the combined shifted sparse state, matching `worksheet("Data")` reacquire stays clean, and a later no-op `save_as()` reuses the retry output byte-for-byte without adding another handoff. | This is non-directory-parent failed-save retry/no-op-save stability for saved materialized shift sessions only. It does not add parent-file repair, output path repair, rollback, session cloning, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| In-memory shift addenda P8.1014 | Public-state coverage now verifies the same post-safe-retry no-op-save stability for existing-directory output rejection. After a saved insert-row shift is dirtied again by an insert-column shift, rejected `save_as()` into an existing directory preserves dirty state, safe retry writes the combined shifted sparse state, matching `worksheet("Data")` reacquire stays clean, and a later no-op `save_as()` reuses the retry output byte-for-byte without adding another handoff. | This is existing-directory failed-save retry/no-op-save stability for saved materialized shift sessions only. It does not add directory replacement, output path repair, rollback, session cloning, source reload, formula repair/evaluation, metadata synchronization, calcChain rebuild, sharedStrings/styles migration, Patch/materialized sparse-session composition, or low-memory random editing. |
| Save-as | Dirty materialized edits save through `WorkbookEditor::save_as(output_path)`; clean read-only materialized sessions, missing `try_worksheet()` lookups, and failed materialization attempts with no queued edits stay no-op copy-original. | Public tests prove modified source-loaded cells roundtrip through save-as, P8.409 proves clean read-only materialization does not flush a standalone projection, P8.410 proves failed materialization does not poison no-op copy-original save, P8.411 proves missing optional lookup does not disturb no-op save, P8.529 strengthens missing-lookup no-op save diagnostics after a prior public edit failure, P8.530 adds the same evidence for throwing missing `worksheet()` lookup, P8.531 strengthens post-recovery catalog-query clean-state diagnostics, P8.532 strengthens post-recovery pending-diagnostic clean-state diagnostics, P8.533 strengthens post-recovery handle-read clean-state diagnostics, P8.534 strengthens post-recovery invalid-read clean-state diagnostics, P8.535 strengthens post-recovery invalid-mutation clean-state diagnostics, P8.536 strengthens post-recovery missing-erase no-op clean-state diagnostics, P8.537 strengthens post-recovery blank/erase dirty-state diagnostics, P8.538 strengthens post-recovery scalar/formula dirty-state diagnostics, P8.539 strengthens post-recovery text-escape dirty-state diagnostics, P8.540 strengthens post-recovery max-coordinate dirty-state diagnostics, P8.541 strengthens post-recovery max-coordinate erase dirty-state diagnostics, P8.542 strengthens post-recovery max-coordinate A1 mutation dirty-state diagnostics, P8.543 strengthens post-recovery max-coordinate blank dirty-state diagnostics, P8.544 strengthens post-recovery max-coordinate formula dirty-state diagnostics, P8.545 strengthens post-recovery max-coordinate scalar dirty-state diagnostics, P8.546 strengthens post-recovery max-coordinate scalar erase-shrink dirty-state diagnostics, P8.547 strengthens post-recovery max-coordinate formula erase-shrink dirty-state diagnostics, P8.548 strengthens lazy malformed sharedStrings XML failure recovery diagnostics, P8.549 strengthens lazy missing sharedStrings target failure recovery diagnostics, P8.550 strengthens lazy duplicate sharedStrings relationship failure recovery diagnostics, P8.551 strengthens lazy wrong sharedStrings content type failure recovery diagnostics, and P8.561 proves shared formula follower materialization writes ordinary formula text without stale cached values. |
| Diagnostics | Errors must identify load vs mutation vs save-as preflight context and preserve recovery guidance. | Materialization failures throw `FastXlsxError` at `try_worksheet()` / `worksheet()` time and do not update public `last_edit_error()`; missing `try_worksheet()` returns empty and preserves prior diagnostics; save-as and queued edit diagnostics remain separate. |

### Source dependency materialization summary

This summary is the maintained API-design index for the `Source dependencies`
gate row above. Future source-loading slices should update this list before
adding more prose to the matrix row.
README and public Doxygen wording should point back to this summary instead of
forking a separate source-loading contract.

- **Supported source values:** blank, number, boolean, `t="str"` scalar text,
  `t="e"` opaque error tokens, formula text without stale cached values
  (including default/numeric, `t="str"`, `t="b"`, and `t="e"` cached-result
  formula cells), plain inline strings, simple inline rich text flattened to
  plain text, and workbook-backed `t="s"`
  shared-string indexes resolved through the existing `xl/sharedStrings.xml`.
- **SharedStrings import/projection:** valid shared string text, `xml:space`,
  prefixed `sst` / `si` / `t` / `r` local-names, simple rich runs,
  non-driving `count` / `uniqueCount`, and otherwise well-formed unknown
  attributes are materialized as text. Dirty projection reuses and append-updates
  an existing safe ordinary sharedStrings table when available; otherwise it
  writes inline strings, preserves source sharedStrings bytes, and never creates
  a new sharedStrings part.
- **Ignored metadata:** source sharedStrings and source inline rich text ignore
  text under `rPh` / `phoneticPr` / `extLst`, including opaque nested metadata;
  self-closing ignored metadata is treated as empty metadata. Nested `<si>`
  decoys, markup inside ignored metadata text wrappers, comments / processing
  instructions / CDATA inside sharedStrings text wrappers, orphan closing tags,
  and unclosed ignored metadata remain fail-fast malformed input. CDATA /
  DOCTYPE-like markup declarations are not a supported sharedStrings text
  import path, and a true XML declaration after the sharedStrings root has
  started is rejected rather than treated as ordinary processing-instruction
  trivia. Duplicate XML declarations, declarations missing a supported
  `version` attribute, unsupported declaration versions, duplicate/unknown
  declaration attributes, empty or invalid declaration encoding names,
  declaration `standalone` values other than `yes` or `no`, declaration
  `encoding` after `standalone`, and XML declarations after leading whitespace
  text or prolog comment / ordinary PI trivia, are rejected as malformed source
  XML. Legal version-only declarations, single-quoted
  attributes, supported `version="1.1"`, `standalone="no"`, and encoding names
  using ASCII letters, digits, `.`, `_`, or `-` remain accepted without implying
  charset transcoding. Case-varied XML-like processing-instruction targets such
  as `<?XML ...?>` or `<?Xml ...?>` are rejected as reserved targets instead of
  being skipped as ordinary PI trivia. `<?xml-stylesheet ...?>` remains
  ordinary PI trivia and is not imported or interpreted. Malformed ordinary PI
  tokens missing the `?>` terminator, lacking a non-empty target, starting with
  an obviously invalid ASCII name-start character, containing an obviously
  invalid ASCII name continuation character, or lacking whitespace / immediate
  `?>` after the target are rejected instead of being guessed or skipped. Legal
  ordinary PI targets using ASCII name-start characters such as `_` and `:`,
  or continuation characters such as `.`, `-`, digits, and `:`, remain ignored
  trivia. Empty-data ordinary PIs such as `<?fastxlsx?>` remain ignored trivia
  when the target is followed immediately by `?>`.
- **Local-name policy:** supported worksheet, inline-string, formula, value,
  sharedStrings, and rich-run element names may be prefixed and are matched by
  local-name; namespace URIs are not validated. Unsupported local-names still
  fail fast even when namespace URI is ignored.
- **Lazy sharedStrings dependency:** stale duplicate relationship metadata,
  malformed payloads, and wrong content types are resolved only if the selected
  worksheet actually contains `t="s"` cells; non-shared-string selected sheets
  can still materialize and save without repair or pruning.
- **Strict failure groups:** non-default or malformed source styles,
  unsupported cell types such as date-like `t="d"` or custom/unknown `t="z"`
  tokens, missing or empty source error-cell `<v>` payloads, unsupported metadata,
  malformed XML/entity/attribute syntax,
  invalid references, row/cell ordering failures, invalid numeric payloads,
  unsupported value-wrapper shapes, non-whitespace direct worksheet text outside
  wrapper metadata or `sheetData`, non-whitespace direct sheetData text outside
  rows, non-whitespace direct row text outside cells, non-whitespace direct cell
  text outside `<v>` / `<t>` / `<f>` wrappers, cell-internal comments /
  processing
  instructions / XML declaration tokens / CDATA, DOCTYPE-like, or other
  unsupported markup, malformed sharedStrings structures,
  invalid shared string indexes, and worksheet root / `sheetData` boundary
  failures all fail before a public editor handle is returned.
- **Non-goals:** no sharedStrings rebuild/writeback/migration, style migration,
  rich-text preservation, namespace validation/repair, XML repair,
  relationship repair/pruning, semantic metadata sync, or large-file low-memory
  random editing.

Open decisions before implementation:
- Resolved by P8.312: use `WorksheetEditorOptions`, passed per
  `worksheet(name, options)` / `try_worksheet(name, options)` materialization
  call, not stored in `WorkbookEditor::open()`.
- Handle ownership: reference-returning API, move-only handle, or optional-like
  lookup result. The first implementation must make invalidation impossible or
  explicitly reject conflicting operations.
- Resolved by P8.379: `get_cell()` belongs in the public slice and throws on a
  missing sparse record.
- Whether save-as can initially use whole-`sheetData` local rewrite only, or
  must wait for a broader worksheet rewrite contract. In either case, the API
  must not claim range metadata recalculation, relationship repair, or
  sharedStrings/styles migration.

P8.310 implementation gate status:
- No public header should be added until the implementation task records an
  explicit answer for each open decision above. The answer must be in the task
  description, Doxygen wording, and public tests before the symbol is exposed.
- The first public slice must keep `WorkbookEditorOptions` scoped to the current
  Patch replacement-payload guardrails. Future worksheet materialization limits
  need a separate options type and separate tests; they must not silently reuse
  `max_replacement_cells` or `replacement_memory_budget_bytes`.
- The first handle design must make conflicting operations impossible or
  fail-before-state-change. In particular, sheet rename, whole-`<sheetData>`
  replacement, move construction / assignment, and repeated materialization of
  the same sheet need explicit lifetime and invalidation tests before a
  `WorksheetEditor` reference or handle becomes public.
- The first read API should continue to prefer `try_cell(row, column)` over
  `get_cell()`, because missing cells and explicit blank cells are distinct
  public states. If `get_cell()` is added, it must have a documented missing-cell
  failure mode and separate tests.
- The first mutation API must keep the current internal semantics:
  `set_cell()` overwrites the sparse record, `erase_cell()` removes the sparse
  record, and `CellValue::blank()` writes an explicit blank replacement cell.
  Tombstones and metadata edits remain outside the first slice; later represented
  sparse row/column insert/delete helpers still do not provide complete Excel
  structural edits. Style-preserving clear is now covered by `clear_cell_value()`,
  `clear_row()` / `clear_rows()`, `clear_column()` / `clear_columns()`, and
  `clear_cell_values(CellRange)`, and still writes explicit blanks rather than
  tombstones.
- Save-as remains the only persistence path. Any first public implementation
  must prove modified materialized cells flow through `WorkbookEditor::save_as()`
  without claiming sharedStrings migration, style migration, relationship repair,
  range metadata recalculation, formula evaluation, calcChain rebuild, or
  in-place save.
- Public diagnostics must keep load/materialization failures distinct from
  queued edit failures. P8.311 resolves the first-slice rule below.

P8.311 materialization diagnostic decision:
- `last_edit_error()` remains scoped to failed public edit operations that try
  to queue workbook changes, currently `replace_sheet_data()`, `replace_cells()`,
  `replace_image()`, `rename_sheet()`, and `WorksheetEditor` mutation failures.
  It must not be reused for worksheet materialization failures.
- The first public `worksheet(name, options)` design should report missing,
  unsupported, malformed, or over-limit materialization failures by throwing
  `FastXlsxError` with sheet name, phase, and source package context where
  available. The failure must occur before a caller-visible `WorksheetEditor`
  handle is returned and before queued edit state changes.
- The first public `try_worksheet(name, options)` design, if included, may
  return empty only for a missing worksheet name. Unsupported source content,
  malformed XML, guardrail overflow, ZIP read failures, and dependency-policy
  failures should still throw `FastXlsxError` rather than being collapsed into
  an empty result.
- Do not add `last_materialization_error()` in the first slice. If later user
  feedback proves persistent materialization diagnostics are needed, add a
  separate task and tests rather than widening `last_edit_error()`.

P8.312 options naming and passing decision:
- The future first public slice should use `WorksheetEditorOptions` as the
  public type name. It is intentionally an editor-handle materialization and
  mutation guardrail type, not just a load-only `WorksheetLoadOptions`, because
  the same `max_cells` and `memory_budget_bytes` limits must be enforced while
  loading the source worksheet and while later mutations grow the materialized
  sparse store.
- `WorksheetEditorOptions` should be passed per
  `WorkbookEditor::worksheet(name, options)` /
  `WorkbookEditor::try_worksheet(name, options)` call. Do not place these
  limits in `WorkbookEditor::open()` for the first slice: opening a workbook
  must remain cheap, must not implicitly load cells, and different sheets may
  need different materialization budgets.
- The type remains separate from current `WorkbookEditorOptions`. Current
  `WorkbookEditorOptions::max_replacement_cells` and
  `replacement_memory_budget_bytes` only guard `replace_sheet_data()`
  replacement payloads; they must not be reused or silently interpreted as
  source worksheet load / random-edit limits.
- The first fields are `std::optional<std::size_t> max_cells` and
  `std::optional<std::size_t> memory_budget_bytes`. The memory budget remains an
  estimate for the materialized worksheet working set, not process RSS and not
  save-time package assembly peak.
- This decision does not add a public header. It only removes the naming /
  passing-location blocker from the P8.310 implementation gate.

P8.313 handle lifetime and operation-mixing decision:
- The first public slice should use `WorkbookEditor`-owned materialized
  worksheet state and return a borrowed `WorksheetEditor&` from
  `worksheet(name, options)`. `try_worksheet(name, options)` should return an
  optional reference wrapper for the same owned state. This keeps save-as
  ownership centralized in `WorkbookEditor` and avoids a detached worksheet
  handle that could outlive the package edit session.
- A returned `WorksheetEditor&` remains valid only while the owning
  `WorkbookEditor` object remains alive and unmoved. Moving or move-assigning
  the owning `WorkbookEditor` invalidates previously obtained references; callers
  must reacquire handles from the moved-to / assigned-to editor. The moved-to
  editor may carry materialized state as part of the editor session, but old
  references must not be used. P8.384 implements this with an owner generation
  guard so overwritten target handles also fail instead of attaching to an
  assigned same-name session.
- Repeated `worksheet(name, options)` for the same current-planned worksheet may
  return the existing materialized editor only when the requested options match
  the first materialization options. Different options must fail before state
  changes rather than reloading, shrinking, expanding, or clearing the existing
  sparse store.
- First-slice operation mixing is reject-first. Once a sheet is materialized,
  `rename_sheet()` for that sheet and whole-sheet `replace_sheet_data()` for
  that sheet must fail before changing queued edits or materialized cells.
  Conversely, if a sheet already has a queued rename or whole-`<sheetData>`
  replacement, `worksheet()` materialization for that sheet must fail until a
  later task specifies migration semantics.
- Rename / replacement operations for unrelated sheets may remain allowed only
  if tests prove they do not invalidate existing `WorksheetEditor` references,
  current planned catalog lookup, pending diagnostics, or save-as output for the
  materialized sheet. Otherwise they should also be rejected in the first public
  implementation.
- This decision still does not add a public header. It removes the lifetime and
  first operation-mixing blocker from the implementation gate; save-as handoff
  evidence remains required before exposure.

P8.314 save-as handoff decision:
- Materializing a worksheet without mutation must not create pending public edit
  state. A no-op `save_as()` after materialization should remain a reader-backed
  roundtrip copy of the opened package.
- Successful `set_cell()` / `erase_cell()` on a materialized worksheet marks that
  worksheet dirty inside the owning `WorkbookEditor` session. Save-as remains
  the only persistence path; there is no in-place save, transaction history, or
  detached worksheet commit.
- The first public save-as route may project the dirty sparse store through the
  existing Patch preservation baseline, but public tests must prove set /
  explicit blank / erase projection, untouched source cell preservation,
  package-level copy-original preservation, and failure-before-state-change on
  path guards or projection failures.
- Before any public header is exposed, edited worksheet output must refresh the
  top-level worksheet `<dimension>` to match emitted cell extents. Stale
  dimension after `set_cell()` / `erase_cell()` is not acceptable as a first
  public-slice behavior. Other range-bearing metadata, including tables,
  defined names, drawings, hyperlinks, validations, and conditional formatting,
  remains audit / fail / preserve only and is not recalculated.
- `CellValue::blank()` remains an explicit blank output cell, while
  `erase_cell()` omits the sparse record from emitted sheetData. Formula cells
  still request recalculation through `fullCalcOnLoad` / calcChain cleanup
  policy; FastXLSX does not evaluate formulas or rebuild calcChain.
- This decision does not add a public header. It removes the save-as behavior
  design blocker, but implementation evidence and public facade tests remain
  required before exposure.

P8.315 CellStore dimension projection evidence:
- Internal `cell_store_dimension_reference(store)` now returns the worksheet
  `<dimension>` reference implied by active sparse records. Empty stores return
  `A1`; non-empty stores return the min/max rectangular extent of emitted
  records, including explicit blank records.
- This helper is the first implementation evidence for the P8.314 dimension
  requirement. It lets a future in-memory save-as route compute the top-level
  worksheet dimension from the materialized sparse store without reading table,
  drawing, hyperlink, validation, conditional formatting, or other range-bearing
  metadata.
- Tests cover empty stores, sparse records, edge-record erasure shrinking the
  extent, explicit blank records extending the extent, and source-loaded
  `CellStore` mutation before projection.
- This is not a public `WorksheetEditor`, not a save-as handoff by itself, and
  not a change to current `WorkbookEditor::replace_sheet_data()` semantics. The
  current Patch facade can still preserve wrapper `<dimension>` metadata for
  whole-`<sheetData>` replacement until a dedicated in-memory save-as path is
  implemented and tested.

P8.316 internal worksheet projection handoff evidence:
- Internal `cell_store_worksheet_chunk_source(store)` now wraps the sparse
  `CellStore` projection into a minimal full worksheet XML chunk source:
  XML declaration, `<worksheet>` root, refreshed top-level `<dimension>`, and
  the existing chunked `<sheetData>` payload.
- The helper references the store while the callback is consumed and is only a
  dedicated in-memory save-as building block. It does not change current
  `WorkbookEditor::replace_sheet_data()` / Patch facade wrapper preservation
  semantics.
- Focused unit coverage checks empty worksheet projection, refreshed sparse
  dimensions after set / erase / blank mutations, style id attribute
  preservation in emitted cells, absence of sharedStrings migration, and a
  loader roundtrip for a style-free projection.
- Internal package-editor smoke coverage feeds this full worksheet chunk source
  through by-name worksheet replacement, verifies staged `StreamRewrite`,
  refreshed `<dimension>`, calcChain cleanup / fullCalcOnLoad, and unknown-byte
  preservation.
- This is still not public `WorksheetEditor` readiness. It does not preserve or
  recalculate worksheet metadata, migrate sharedStrings, merge styles, repair
  relationships, evaluate formulas, rebuild calcChain, or provide random cell
  editing facade diagnostics.

P8.317-P8.319 current `WorkbookEditor` public-facade diagnostics evidence:
- Replacement + rename to a temporary planned name + rename back to the source
  name migrates pending replacement diagnostics back to the restored planned
  source name, clears the public `renamed` flag in `worksheet_catalog()` /
  `pending_worksheet_edits()`, and does not leak the transient name into saved
  output.
- A rename-only chain that returns to the source name is treated as a current
  planned-state no-op for `pending_worksheet_edits()`: it remains visible in
  the coarse `pending_change_count()` call counter, but produces no final
  worksheet summary and no replacement diagnostics.
- After that rename-only chain returns to the source name, a later failed
  duplicate rename must preserve the restored catalog / empty summary state.
  A follow-up successful `replace_sheet_data("Data", ...)` resolves through
  that restored planned name, clears the prior public edit diagnostic, and
  records replacement diagnostics under `Data` without reintroducing the
  public `renamed` flag.
- These are state-hygiene guardrails for the existing public Patch facade only.
  They do not add `WorksheetEditor`, random cell editing, semantic sheet rename,
  relationship repair, sharedStrings/style migration, formula evaluation, or
  calcChain rebuild.

P8.320 public wording gate:
- Current public-facing wording is aligned around the implemented Patch facade:
  `WorkbookEditor` exposes whole-`<sheetData>` replacement, narrow sheet-catalog
  rename, source/planned catalog inspection, coarse pending diagnostics, and
  `save_as()` only.
- The future `WorksheetEditor` materialization / random-edit draft remains a
  design section in this document, not a public header contract. The source-level
  public-header gate must remain clean for `WorksheetEditor`,
  `WorksheetEditorOptions`, `WorkbookEditor::worksheet()`, `try_worksheet()`,
  `get_cell()`, `set_cell()`, and `erase_cell()` until the implementation gate is
  explicitly reopened.
- The next implementation slice should not repeat wording-only work by default.
  It should either add focused internal operation-mixing guardrail evidence for
  future materialized worksheets, or deliberately open the public header task
  with tests that prove missing/materialization failures, handle lifetime,
  option matching, rename / whole-sheet replacement mixing rejection, dimension
  refresh, and save-as persistence.
- Do not collapse current `WorkbookEditorOptions` replacement-payload guardrails
  into future `WorksheetEditorOptions`; they protect different inputs and memory
  models.

P8.321 internal planned-catalog / CellStore handoff evidence:
- `fastxlsx.package_editor.cellstore` now covers a source-loaded internal
  `CellStore` handed to the existing by-name `<sheetData>` Patch helper after a
  queued sheet-catalog rename.
- The old source sheet name is rejected against the current planned catalog and
  the failed handoff preserves the queued workbook rename, worksheet copy-original
  state, calc policy, and notes. A fresh handoff using the planned new name then
  rewrites the worksheet, removes stale `calcChain`, requests recalculation, and
  keeps unknown bytes.
- This is useful operation-mixing evidence for future `WorksheetEditor` design,
  but it is still internal `PackageEditor` / `CellStore` coverage. It does not
  add a public borrowed worksheet handle, does not define handle invalidation, and
  does not make materialized worksheet + rename mixing a public supported flow.

P8.322 internal planned-catalog / full worksheet projection evidence:
- `fastxlsx.package_editor.cellstore` also covers a source-loaded `CellStore`
  projected as a full worksheet chunk source after a queued sheet-catalog rename.
- The old source sheet name is rejected before state changes; the planned new
  name succeeds, uses staged `StreamRewrite`, refreshes top-level worksheet
  `<dimension>` from emitted sparse records, removes stale `calcChain`, requests
  recalculation, and preserves unknown bytes.
- This complements P8.321's whole-`<sheetData>` handoff with the future
  in-memory save-as projection boundary. It still does not expose
  `WorksheetEditor` or define public materialized-handle rename semantics.

P8.323 internal old-name preflight chunk-source hygiene:
- The P8.321 and P8.322 regressions now also prove old-source-name failures are
  resolved before consuming the prepared `CellStore` chunk source. The same
  counted source can be reused for the follow-up planned-name success.
- This matters for future public materialization design because lookup /
  operation-mixing preflight failures should not partially drain caller or
  editor-owned projection sources.
- This is still internal evidence only; it does not define a public retryable
  `WorksheetEditor` source object or expose chunk-source lifetimes.

P8.324 internal queued-worksheet / CellStore follow-up handoff evidence:
- `fastxlsx.package_editor.cellstore` now covers a source-loaded `CellStore`
  projected into `<sheetData>` after an earlier queued whole-worksheet
  replacement on the same sheet.
- The follow-up handoff replaces `<sheetData>` inside the current planned
  worksheet wrapper, preserves queued `sheetViews`, `autoFilter`, and `extLst`
  metadata, keeps stale `calcChain` cleanup / `fullCalcOnLoad`, and does not
  resurrect erased source formula payload or earlier queued rows.
- This is operation-mixing evidence for future materialized worksheet save-as
  design only. It does not define public mixing semantics, relationship repair,
  range metadata recalculation, or random cell editing.

P8.325 internal queued-worksheet / full worksheet CellStore projection evidence:
- `fastxlsx.package_editor.cellstore` now also covers a source-loaded
  `CellStore` projected as a full worksheet chunk source after an earlier queued
  whole-worksheet replacement on the same sheet.
- The later full worksheet projection supersedes the prior queued worksheet
  wrapper, stages a `StreamRewrite`, refreshes top-level worksheet
  `<dimension>`, keeps calc cleanup / `fullCalcOnLoad`, and preserves unknown
  bytes.
- This complements P8.324's follow-up `<sheetData>` behavior: sheetData handoff
  preserves the planned wrapper, while full worksheet handoff replaces it. Both
  remain internal evidence and do not define public operation-mixing semantics.

P8.326 internal combined rename / queued-worksheet / CellStore handoff evidence:
- `fastxlsx.package_editor.cellstore` now covers the combined operation-mixing
  case where a sheet is first renamed, then receives a queued whole-worksheet
  replacement, and then a source-loaded `CellStore` is handed off as
  `<sheetData>`.
- The old source sheet name fails against the planned catalog before consuming
  any prepared `CellStore` chunks and without changing the queued rename,
  worksheet replacement, calc policy, or notes. The planned name then succeeds,
  patches the queued worksheet wrapper, and preserves the renamed catalog in the
  output.
- This remains internal `PackageEditor` / `CellStore` evidence. It does not
  define public `WorksheetEditor` handle invalidation, public retry semantics,
  or random cell editing.

P8.327 internal combined rename / queued-worksheet / full worksheet projection evidence:
- `fastxlsx.package_editor.cellstore` now also covers the paired full worksheet
  projection case after the same queued rename plus queued whole-worksheet
  replacement setup.
- The old source sheet name fails against the planned catalog before consuming
  any prepared full-worksheet `CellStore` chunks and without changing the
  queued rename, worksheet replacement, calc policy, or notes. The planned name
  then succeeds as a staged `StreamRewrite`, replaces the prior queued wrapper,
  refreshes top-level worksheet `<dimension>`, preserves the renamed catalog,
  keeps calc cleanup / `fullCalcOnLoad`, and preserves unknown bytes.
- This remains internal evidence for future materialized worksheet save-as
  design. It does not define public operation-mixing semantics, relationship
  repair, range metadata recalculation, or random cell editing.

P8.328 internal combined staged-state save-as failure hygiene evidence:
- The P8.327 regression now also attempts `save_as()` over the source package
  after the planned-name full worksheet projection has staged replacement
  chunks.
- The source-overwrite guard fails before mutating the queued rename, worksheet
  staged rewrite, package-entry audits, notes, calc policy, or staged chunks.
  A follow-up safe output path still saves successfully.
- This is internal save-as persistence evidence for a future materialized
  worksheet handoff. It does not add in-place save, transaction semantics,
  public `WorksheetEditor`, or public `PackageEditor`.

P8.329 internal path-equivalent source-overwrite save-as hygiene evidence:
- The same combined staged-state regression now also attempts a path-equivalent
  source overwrite using a non-identical path string that resolves to the source
  package.
- The path-equivalent output-path guard preserves the queued rename, worksheet
  staged rewrite, notes, calc policy, and staged chunks, and the later safe
  output path still saves successfully.
- This tightens save-as guard evidence only. It does not add in-place save,
  transaction semantics, public `WorksheetEditor`, or public `PackageEditor`.

P8.330 internal empty-output-path save-as hygiene evidence:
- The same combined staged-state regression now also attempts `save_as()` with
  an empty output path after staged full worksheet projection chunks exist.
- The empty-output guard preserves the queued rename, worksheet staged rewrite,
  notes, calc policy, and staged chunks, and the later safe output path still
  saves successfully.
- This is still internal save-as guard evidence only. It does not add in-place
  save, transaction semantics, public `WorksheetEditor`, or public
  `PackageEditor`.

P8.331 internal missing-parent save-as hygiene evidence:
- The same combined staged-state regression now also attempts `save_as()` under
  a missing output parent path after staged full worksheet projection chunks
  exist.
- The missing-parent guard preserves the queued rename, worksheet staged
  rewrite, notes, calc policy, and staged chunks, and the later safe output path
  still saves successfully.
- This is still internal save-as guard evidence only. It does not add in-place
  save, transaction semantics, public `WorksheetEditor`, or public
  `PackageEditor`.

P8.332 internal non-directory-parent save-as hygiene evidence:
- The same combined staged-state regression now also attempts `save_as()` under
  an output parent path that exists as a file rather than a directory.
- The non-directory-parent guard preserves the queued rename, worksheet staged
  rewrite, notes, calc policy, and staged chunks, and the later safe output path
  still saves successfully.
- This is still internal save-as guard evidence only. It does not add in-place
  save, transaction semantics, public `WorksheetEditor`, or public
  `PackageEditor`.

P8.333 internal existing-directory save-as hygiene evidence:
- The same combined staged-state regression now also attempts `save_as()` to an
  output path that is an existing directory.
- The existing-directory guard preserves the queued rename, worksheet staged
  rewrite, notes, calc policy, and staged chunks, and the later safe output path
  still saves successfully.
- This is still internal save-as guard evidence only. It does not add in-place
  save, transaction semantics, public `WorksheetEditor`, or public
  `PackageEditor`.

P8.334 internal writer-failure save-as hygiene evidence:
- The same combined staged-state regression now also attempts `save_as()` with
  an invalid writer backend after staged full worksheet projection chunks exist.
- The writer failure preserves the queued rename, worksheet staged rewrite,
  notes, calc policy, staged chunks, existing output bytes, and temporary-file
  cleanup; the later safe output path still saves successfully.
- This is internal writer-failure hygiene evidence only. It does not add
  in-place save, transaction semantics, public `WorksheetEditor`, or public
  `PackageEditor`.

P8.335 internal successful-save persistence evidence:
- The same combined staged-state regression now saves to a second safe output
  path after the first successful `save_as()`.
- The first successful save preserves the staged full worksheet chunks for reuse;
  the second output preserves the same planned workbook catalog, worksheet XML,
  calcChain omission, and unknown bytes.
- This is internal save-as persistence evidence only. It does not add commit /
  close semantics, in-place save, transaction history, public `WorksheetEditor`,
  or public `PackageEditor`.

P8.336 internal source-copy temp-failure save-as hygiene evidence:
- The same combined staged-state regression now mutates save-time source-copy
  temporary files before package writing.
- The source-copy temp size failure preserves the queued rename, worksheet staged
  rewrite, notes, calc policy, staged chunks, existing output bytes, and
  temporary-file cleanup; a later safe output path still saves successfully.
- This is internal save-time failure hygiene evidence only. It does not expose
  temp paths or hooks, add in-place save, transaction semantics, public
  `WorksheetEditor`, or public `PackageEditor`.

P8.337 internal missing source-copy temp save-as hygiene evidence:
- The same combined staged-state regression now deletes save-time source-copy
  temporary files before package writing.
- The missing source-copy temp-file failure preserves the queued rename,
  worksheet staged rewrite, notes, calc policy, staged chunks, existing output
  bytes, and temporary-file cleanup; a later safe output path still saves
  successfully.
- This is internal save-time failure hygiene evidence only. It does not expose
  temp paths or hooks, add in-place save, transaction semantics, public
  `WorksheetEditor`, or public `PackageEditor`.

P8.338 internal source-copy temp CRC save-as hygiene evidence:
- The same combined staged-state regression now rewrites save-time source-copy
  temporary files with same-size different payloads before package writing.
- The source-copy temp CRC failure preserves the queued rename, worksheet staged
  rewrite, notes, calc policy, staged chunks, existing output bytes, and
  temporary-file cleanup; a later safe output path still saves successfully.
- This is internal save-time failure hygiene evidence only. It does not expose
  temp paths or hooks, add in-place save, transaction semantics, public
  `WorksheetEditor`, or public `PackageEditor`.

P8.339 internal workbook-removal operation-mixing evidence:
- A source-loaded `CellStore` full worksheet chunk source now fails by-name
  handoff after planned `/xl/workbook.xml` removal at catalog preflight, before
  the chunk source is consumed.
- The failure preserves workbook removal, workbook owner `.rels` omission,
  edit-plan state, calc policy, worksheet copy-original mode, and output-plan
  absence of staged worksheet chunks.
- This is internal operation-mixing evidence only. It does not expose public
  materialized worksheet handles, public random cell editing, sheet delete
  semantics, relationship repair, or public `PackageEditor`.

P8.340 internal invalid-planned-catalog operation-mixing evidence:
- A source-loaded `CellStore` full worksheet chunk source now fails by-name
  handoff when the planned workbook catalog references a missing sheet
  relationship id, before the chunk source is consumed.
- The failure preserves the queued workbook replacement, edit-plan state, calc
  policy, worksheet copy-original mode, and output-plan absence of staged
  worksheet chunks.
- This is internal catalog-preflight evidence only. It does not expose public
  materialized worksheet handles, public random cell editing, relationship
  repair, or public `PackageEditor`.

P8.341 internal wrong-namespace planned-catalog-id evidence:
- A source-loaded `CellStore` full worksheet chunk source now fails by-name
  handoff when the planned workbook catalog uses a sheet id attribute in the
  wrong XML namespace, before the chunk source is consumed.
- The failure preserves the queued workbook replacement, edit-plan state, calc
  policy, worksheet copy-original mode, and output-plan absence of staged
  worksheet chunks.
- This is internal namespace / catalog-preflight evidence only. It does not
  expose public materialized worksheet handles, public random cell editing,
  namespace repair, relationship repair, or public `PackageEditor`.

P8.342 internal unqualified planned-catalog-id evidence:
- A source-loaded `CellStore` full worksheet chunk source now fails by-name
  handoff when the planned workbook catalog exposes the sheet relationship only
  through a plain unqualified `id` attribute, before the chunk source is
  consumed.
- The failure preserves the queued workbook replacement, edit-plan state, calc
  policy, worksheet copy-original mode, and output-plan absence of staged
  worksheet chunks.
- This is internal namespace / catalog-preflight evidence only. It does not
  expose public materialized worksheet handles, public random cell editing,
  namespace repair, relationship repair, or public `PackageEditor`.

P8.343 internal unregistered planned-catalog-target evidence:
- A source-loaded `CellStore` full worksheet chunk source now fails by-name
  handoff when the planned workbook catalog relationship resolves through
  workbook `.rels` to an unregistered worksheet part, before the chunk source is
  consumed.
- The failure preserves the queued workbook replacement, edit-plan state, calc
  policy, worksheet copy-original mode, and output-plan absence of staged
  worksheet chunks.
- This is internal catalog-preflight evidence only. It does not expose public
  materialized worksheet handles, public random cell editing, relationship
  repair, orphan cleanup, or public `PackageEditor`.

P8.344 F2 public-header implementation gate refresh:
- The post-P8.321 operation-mixing evidence now covers planned rename, queued
  whole-worksheet replacement, combined rename + queued worksheet replacement,
  save-as guard hygiene, writer/backend failure hygiene, source-copy temp-file
  hygiene, planned workbook removal, malformed planned catalog XML, wrong /
  unqualified planned sheet id attributes, and unregistered planned worksheet
  targets for source-loaded `CellStore` chunk handoff.
- That evidence is enough to stop adding more same-family internal
  planned-catalog negative tests by default. Additional internal tests should
  be added only for a newly identified behavior gap, not just another catalog
  spelling variant.
- The next useful task is a public-header implementation task plan for the
  first `WorksheetEditor` slice: materialization failure tests, handle lifetime
  and option matching tests, operation-mixing rejection tests, dirty
  set/blank/erase projection, dimension refresh, save-as persistence, and
  public diagnostics. This refresh still does not add `WorksheetEditor`,
  `WorksheetEditorOptions`, `worksheet()`, `try_worksheet()`, `get_cell()`,
  `set_cell()`, or `erase_cell()` to public headers.

P8.345 first public `WorksheetEditor` implementation task split:
- Do not expose the public header first. Start with a private
  `WorkbookEditor`-owned materialized worksheet session: current planned sheet
  name, source worksheet part, `CellStore`, options snapshot, dirty flag, and
  projected dependency/calc diagnostics.
- Stage the implementation in this order:
  1. Internal materialization helper: resolve the current planned catalog,
     load exactly one source worksheet through existing `CellStore` loader
     guardrails, and fail before state changes on missing / malformed /
     unsupported / over-limit input.
  2. Internal mutation helper: apply set / explicit blank / erase to the
     materialized store with the same `max_cells` and `memory_budget_bytes`
     limits, and preserve missing-vs-explicit-blank semantics.
  3. Internal save-as handoff: project dirty materialized stores through the
     existing chunked worksheet replacement path, refresh top-level
     `<dimension>`, preserve unknown / unmodified entries, and keep formula /
     calcChain policy explicit without formula evaluation.
  4. Operation-mixing guards: reject ambiguous `rename_sheet()` /
     `replace_sheet_data()` combinations involving materialized sheets before
     state changes, while proving unrelated-sheet operations are either safe or
     rejected.
  5. Public header exposure: add `WorksheetEditorOptions`, borrowed
     `WorksheetEditor` handles, `worksheet()`, and optional `try_worksheet()`
     only after the internal behavior above has targeted tests.
- First public tests must be added with the header, not after it: missing vs
  unsupported materialization, option matching on repeated materialization,
  move invalidation/reacquire behavior, operation-mixing rejection, set / blank
  / erase save-as projection, dimension refresh, output path guard hygiene, and
  diagnostics stage separation.
- Keep deferred scope explicit: no `get_cell()` in the first slice unless its
  missing-cell failure semantics are documented and tested; no row/column
  insertion, sheet add/delete, style migration, sharedStrings migration,
  relationship repair, range metadata recalculation, formula evaluation,
  calcChain rebuild, in-place save, or public `PackageEditor`.

P8.346 internal materialized worksheet session foundation:
- Added an internal-only `detail::MaterializedWorksheetSession` holder for the
  future private `WorkbookEditor` materialized state. It owns one planned sheet
  name, one `CellStore`, the store's materialization options, and a dirty flag.
- The session proves the first private mutation boundary: successful `set_cell`
  marks dirty, erasing an existing record marks dirty, erasing a missing record
  remains a clean no-op, and failed `set_cell` / `erase_cell` guardrails preserve
  both dirty state and sparse records.
- It also provides option matching evidence for repeated materialization
  preflight. This is still detail-only infrastructure: no public
  `WorksheetEditor`, no public `WorksheetEditorOptions`, no public
  `worksheet()` / `try_worksheet()`, and no random-edit save-as route.

P8.347 internal materialized session projection bridge:
- `detail::MaterializedWorksheetSession` now exposes an internal
  `worksheet_chunk_source()` bridge that projects the current materialized
  `CellStore` through the existing full worksheet chunk source.
- Unit coverage verifies the bridge refreshes top-level `<dimension>` from the
  dirty store, includes successful `set_cell()` payloads, and omits erased
  sparse records.
- This is only a private save-as handoff building block. It does not queue
  workbook edits, does not wire `WorkbookEditor::save_as()`, does not preserve
  existing worksheet metadata, and does not expose public random editing.

P8.348 internal materialized session registry foundation:
- `detail::MaterializedWorksheetSessionRegistry` now owns multiple internal
  materialized worksheet sessions keyed by planned sheet name.
- The registry provides mutation-free materialization preflight, matching-option
  repeated materialization reuse, mismatched-option rejection before registry
  state changes, lookup by planned name, and dirty-session bookkeeping.
- Unit coverage verifies repeated matching materialization preserves dirty
  session state instead of replacing it, mismatched materialization leaves the
  existing registry and dirty session untouched, and dirty planned names are
  exposed for a future save-as projection step.
- This remains internal-only. It does not load worksheets from a package, define
  public handle lifetime, wire `WorkbookEditor::save_as()`, expose
  `WorksheetEditor`, or add relationship/style/sharedStrings migration.

P8.349 internal dirty materialized projection enumeration:
- `detail::MaterializedWorksheetSessionRegistry` now exposes
  `dirty_worksheet_chunk_sources()` for dirty sessions only.
- Each projection carries the planned sheet name plus a full worksheet chunk
  source delegated from the corresponding internal session. Names and callbacks
  reference registry-owned state, so callers must keep the registry alive and
  avoid mutating those sessions while consuming the callbacks.
- Unit coverage verifies clean sessions are skipped, dirty sessions are
  enumerated in registry planned-name order, and each projection emits the
  expected refreshed dimension and sparse payload.
- This is still only an internal save-as handoff building block. It does not
  queue package edits, persist random cell edits, expose `WorksheetEditor`, or
  define public retry / handle lifetime semantics.

P8.350 internal operation-mixing preflight foundation:
- `detail::MaterializedWorksheetSessionRegistry` now exposes
  `preflight_no_materialized_session(planned_name, operation_name)`.
- The helper is mutation-free: it allows operations for sheets with no
  materialized session and rejects operations for already materialized planned
  sheets before registry state changes.
- Unit coverage verifies the rejection preserves registry count, session
  identity, and dirty state.
- This does not wire current `WorkbookEditor::rename_sheet()` or
  `replace_sheet_data()` yet and does not define public operation-mixing
  semantics. It is only private evidence for the future `WorksheetEditor`
  implementation gate.

P8.351 internal package-backed one-sheet materialization handoff:
- `detail::MaterializedWorksheetSessionRegistry` now exposes
  `materialize_from_workbook_sheet(reader, planned_name, source_sheet_name,
  options, reader_options)`.
- The helper first runs registry materialization preflight. A matching existing
  planned session is returned without re-reading the package, preserving dirty
  state. Mismatched options fail before package lookup and before registry
  mutation.
- New sessions load exactly one source worksheet through the existing internal
  `load_cell_store_from_workbook_sheet()` path, then insert a clean
  materialized session under the caller-supplied planned name.
- `fastxlsx.package_reader` coverage verifies successful source materialization,
  matching repeated materialization reuse, mismatched repeated options
  failure-before-state-change, missing source-sheet failure hygiene, and source
  load guardrail failure hygiene.
- This P8.351 slice remained internal-only. At that point it did not wire
  current `WorkbookEditor`, expose public `WorksheetEditor`, persist random
  edits, migrate sharedStrings/styles, or repair relationships.

P8.352 private `WorkbookEditor` materialized-state wiring:
- `WorkbookEditor::Impl` now owns a private
  `detail::MaterializedWorksheetSessionRegistry`.
- The current public facade still has no `WorksheetEditor`,
  `WorksheetEditorOptions`, `worksheet()`, `try_worksheet()`, public
  `get_cell()`, public `set_cell()`, or public `erase_cell()`.
- `replace_sheet_data()` now uses the private operation-mixing preflight for
  the current planned sheet, so a future internally materialized sheet blocks
  same-sheet whole-`<sheetData>` replacement before replacement payload
  construction or public pending-edit mutation.
- Test-hook-only `fastxlsx.workbook_editor` coverage verifies source-backed
  private materialization, dirty state, move construction / move assignment
  transfer with `Impl`, and same-sheet operation-mixing rejection while a
  different sheet remains editable.
- Dirty materialized sessions are still not projected through public
  `save_as()`. This is private owner-state and guardrail evidence only, not
  public random-edit persistence, sharedStrings/style migration, relationship
  repair, formula evaluation, calcChain rebuild, or large-file random editing.

P8.353 internal materialized-session flush handoff smoke:
- A `FASTXLSX_ENABLE_TEST_HOOKS`-only
  `testing_workbook_editor_flush_materialized_sessions_to_patch_plan()` helper
  can explicitly project dirty private materialized sessions through the existing
  by-name full worksheet chunk-source Patch helper.
- The helper is internal test evidence only. It is not automatic public
  `WorkbookEditor::save_as()` behavior and does not expose public random cell
  editing.
- `fastxlsx.workbook_editor` verifies clean materialization flush remains a
  source roundtrip, dirty materialized state flushes into the Patch plan,
  successful flush clears private dirty state, and later `save_as()` persists
  the projected worksheet with refreshed sparse-store `<dimension>` while
  preserving an untouched worksheet byte-for-byte.
- This still does not expose `WorksheetEditor`, `WorksheetEditorOptions`,
  `worksheet()`, `try_worksheet()`, public `get_cell()`, public `set_cell()`,
  or public `erase_cell()`, and it does not migrate sharedStrings/styles, repair
  relationships, update range metadata semantically, evaluate formulas, rebuild
  calcChain, or provide large-file random editing.

P8.354 private rename operation-mixing guard:
- `WorkbookEditor::rename_sheet()` now runs the same private
  materialized-session operation-mixing preflight for the current planned old
  name.
- If a sheet has been internally materialized, same-sheet catalog rename fails
  before catalog mutation, pending public edit mutation, or dirty-session
  changes.
- Test-hook-only coverage verifies the rejected rename preserves private dirty
  state and public catalog diagnostics, while renaming a different sheet remains
  allowed.
- This is still internal policy evidence for a future public `WorksheetEditor`
  gate; it is not public materialization, random editing, relationship repair,
  semantic sheet rename synchronization, or automatic dirty materialized
  persistence.

P8.355 repeated internal flush hygiene:
- `fastxlsx.workbook_editor` now verifies a dirty private materialized session
  can be flushed into the Patch plan, become clean, be modified again, and be
  flushed again.
- The later flush replaces the prior staged worksheet projection: final output
  contains the second payload, omits the stale first payload, and keeps the
  refreshed sparse-store dimension.
- This is still test-hook-only evidence for internal save-as handoff hygiene,
  not public automatic persistence or public random editing.

P8.356 materialized flush failure hygiene:
- The test-hook-only materialized-session flush path now preflights all dirty
  projection planned names against the current `WorkbookEditor` catalog before
  staging any worksheet rewrite.
- `fastxlsx.workbook_editor` verifies that a dirty valid session plus a dirty
  orphan planned-name session fails before queuing coarse public edit
  diagnostics, before clearing dirty state, and before staging the earlier valid
  worksheet projection.
- This remains internal-only failure hygiene. It does not make materialized
  sessions persist through public `save_as()`, does not expose public
  `WorksheetEditor` handles, and does not add relationship repair,
  sharedStrings/style migration, formula evaluation, calcChain rebuild, or range
  metadata recalculation.

P8.357 materialized flush after planned rename:
- `fastxlsx.workbook_editor` now verifies the positive planned-catalog path for
  future materialized worksheet handoff: after `WorkbookEditor::rename_sheet()`
  changes `Data` to `RenamedData`, a test-hook-only materialized session keyed
  by the planned name can flush through the existing by-name worksheet
  chunk-source Patch helper.
- The saved workbook keeps the renamed catalog entry, rewrites the original
  worksheet part with the materialized projection, and keeps refreshed
  sparse-store dimension.
- This is still internal evidence only. It does not allow materializing by
  public API, does not make dirty materialized sessions automatically persist
  through public `save_as()`, and does not synchronize defined names, formulas,
  tables, drawings, hyperlinks, relationships, sharedStrings, styles, or
  calcChain.

P8.358 materialized blank-vs-erase projection:
- A new `FASTXLSX_ENABLE_TEST_HOOKS`-only
  `testing_workbook_editor_erase_materialized_cell()` hook exercises the
  existing internal `MaterializedWorksheetSession::erase_cell()` path through
  `WorkbookEditor` ownership.
- `fastxlsx.workbook_editor` verifies that erasing a missing materialized cell
  keeps a clean session clean, while `set_cell(..., CellValue::blank())` remains
  an explicit empty cell and erasing an existing source cell removes it from the
  flushed worksheet projection.
- The saved output keeps the preserved source `B1` number, removes erased row-2
  source text, emits explicit blank `A1`, and refreshes dimension to the
  remaining extents.
- This is still internal sparse-store projection evidence only. It does not
  expose public `erase_cell()` / `set_cell()`, does not define public
  `WorksheetEditor` handle lifetime, and does not add tombstones,
  style-preserving clear, row deletion semantics, relationship repair, or range
  metadata recalculation.

P8.359 repeated materialization preserves dirty state:
- `fastxlsx.workbook_editor` now verifies that calling the test-hook-only source
  materialization twice for the same planned sheet reuses the existing private
  session rather than reloading source worksheet cells.
- A dirty edit made before the second materialization remains dirty, flushes to
  output, and does not revert to the original source `A1` payload.
- This is future handle-reacquire evidence only. It does not expose public
  `WorkbookEditor::worksheet()` / `try_worksheet()`, does not define public
  borrowed-handle lifetime, and does not add automatic public save-as
  persistence.

P8.360 materialized source-load guard failure hygiene:
- `fastxlsx.workbook_editor` now covers a test-hook-only materialized source
  load rejected by the current `CellStoreOptions` guard derived from
  `WorkbookEditorOptions::max_replacement_cells`.
- The failure occurs before registering a private materialized session, before
  creating dirty materialized state, before queuing public pending-change
  diagnostics, and without updating public `last_edit_error()`.
- The same editor remains usable after the failed test-hook load: a later valid
  public `replace_sheet_data()` and `save_as()` succeeds.
- This is internal state-hygiene evidence only. Current `WorkbookEditorOptions`
  remain replacement-payload guardrails for the public facade and must not be
  documented as future public `WorksheetEditorOptions`.

P8.361 materialized memory-budget load failure hygiene:
- `fastxlsx.workbook_editor` now covers the same test-hook-only materialized
  source-load failure hygiene for the `CellStoreOptions::memory_budget_bytes`
  guard currently derived from
  `WorkbookEditorOptions::replacement_memory_budget_bytes`.
- The rejected load leaves the private registry empty, dirty state clean,
  public pending diagnostics unchanged, and public `last_edit_error()` unset.
- The same editor can still queue a catalog-only `rename_sheet()` and save the
  renamed workbook, proving the guarded internal load did not poison later
  public facade operations.
- This is still internal-only guardrail evidence; it is not public
  `WorksheetEditorOptions`, not a process-RSS memory guarantee, and not public
  materialized editing.

P8.362 materialized missing-source load failure hygiene:
- `fastxlsx.workbook_editor` now covers the test-hook-only source
  materialization failure path when the requested source sheet name is absent
  from the package workbook catalog.
- The rejected load leaves no private session for the requested planned name,
  leaves dirty materialized state clean, does not queue public pending
  diagnostics, and does not update public `last_edit_error()`.
- The same editor remains usable for a later public `replace_sheet_data()` /
  `save_as()` flow. This is internal source-load hygiene only; it does not
  validate arbitrary public planned names, expose a public `WorksheetEditor`, or
  make random-cell editing persistent by default.

P8.363 materialize-after-replacement mixing guard:
- The test-hook-only source materialization path now rejects materializing a
  planned sheet that already has public `replace_sheet_data()` payload queued in
  the current `WorkbookEditor`.
- The regression proves the failure registers no private materialized session,
  preserves the queued public replacement diagnostics, leaves
  `last_edit_error()` unset, and still permits materializing a different sheet.
- This is a conservative internal operation-mixing guard. It does not make
  source materialization read from staged replacement payloads, does not imply
  public automatic flush behavior, and does not expose public random editing.

P8.364 renamed replacement materialization guard hygiene:
- `fastxlsx.workbook_editor` now covers the same conservative guard after
  `replace_sheet_data()` is followed by `rename_sheet()` for the same source
  sheet. The pending replacement diagnostics migrate to the renamed planned
  sheet name, and test-hook materialization of that renamed planned name is
  rejected before private state mutation.
- The failure preserves the queued replacement/rename public diagnostics, does
  not update `last_edit_error()`, and does not block materializing a different
  clean sheet.
- This remains internal operation-mixing evidence. It does not make
  materialization consume staged replacement bytes, does not define public
  `WorksheetEditor` handle semantics, and does not add public random-cell
  persistence.

P8.365 replacement-on-renamed-sheet materialization guard hygiene:
- `fastxlsx.workbook_editor` now covers the opposite public operation order:
  `rename_sheet()` first, then `replace_sheet_data()` against the renamed
  planned sheet.
- Test-hook materialization of that renamed planned sheet is rejected before
  private session mutation, preserving public pending replacement diagnostics
  and leaving `last_edit_error()` unchanged. A different sheet can still be
  materialized cleanly.
- This remains a reject-first internal mixing guard and does not make staged
  replacement payloads a materialization source.

P8.366 rejected-public-operation materialized flush hygiene:
- `fastxlsx.workbook_editor` now covers a dirty test-hook-only materialized
  session that rejects same-sheet `replace_sheet_data()` and `rename_sheet()`
  before public state mutation.
- The regression proves the rejected operations preserve the dirty private
  materialized session, leave public replacement diagnostics empty, and still
  allow the explicit internal materialized flush handoff to queue one worksheet
  projection.
- Final output keeps the original sheet name, saves the materialized payload,
  and does not leak the rejected replacement payload or rejected rename. This is
  still not automatic public dirty-session persistence, public `WorksheetEditor`
  support, or public retry/transaction semantics.

P8.367 post-flush rejected-public-operation hygiene:
- `fastxlsx.workbook_editor` now covers a materialized session that has already
  been explicitly flushed into a staged worksheet projection before same-sheet
  public operations are attempted again.
- Later `replace_sheet_data()` and `rename_sheet()` calls remain reject-first
  because the private materialized session still exists; the staged projection
  count, clean private state, and empty replacement diagnostics are preserved.
- Final output keeps the flushed materialized worksheet payload and original
  sheet name, while omitting the rejected replacement payload and rejected
  rename. This still does not define public handle invalidation, automatic
  dirty-session persistence, or transaction/retry semantics.

P8.368 cross-sheet public edit after rejected materialized operations:
- `fastxlsx.workbook_editor` now covers rejected same-sheet
  `replace_sheet_data()` / `rename_sheet()` on a dirty materialized sheet,
  followed by a successful `replace_sheet_data()` on a different sheet.
- The successful cross-sheet public edit clears the prior public
  `last_edit_error()`, records only the other sheet in replacement diagnostics,
  and does not clear dirty private materialized state.
- A later explicit internal materialized flush saves the dirty materialized
  payload while preserving the cross-sheet public replacement. Rejected
  same-sheet payload/name data still does not leak into output.

P8.369 cross-sheet public rename after rejected materialized operations:
- `fastxlsx.workbook_editor` now covers the catalog-only sibling of P8.368:
  rejected same-sheet public operations on dirty materialized `Data`, followed
  by successful `rename_sheet("Untouched", ...)`.
- The successful cross-sheet rename clears the prior public error, updates only
  the other planned sheet name, leaves sheetData replacement diagnostics empty,
  and preserves dirty private materialized state.
- Explicit internal materialized flush then saves the materialized payload while
  preserving the other-sheet catalog rename and omitting rejected same-sheet
  payload/name data.

P8.370 review-only API status matrix:
- The current top-level API design section now separates current public API,
  internal-only foundations, and future API targets in one matrix.
- The public `WorksheetEditor` gate now explicitly requires a clean
  public-header grep, non-goal Doxygen wording, same-sheet operation-mixing
  rejection, cross-sheet recovery evidence, diagnostics separation, and public
  implementation tests before any public symbol is added.
- This is a documentation gate only: no `WorksheetEditor`, no
  `WorksheetEditorOptions`, no `WorkbookEditor::worksheet()` /
  `try_worksheet()`, no public `set_cell()` / `erase_cell()`, and no public
  `PackageEditor`.

Draft Doxygen wording for the future first slice:

```cpp
/// Options for explicitly materializing one worksheet into the future
/// WorksheetEditor small-file random-edit path.
///
/// Mode: In-memory / existing-workbook editing. These limits do not apply to
/// WorkbookWriter streaming output and are separate from the current
/// WorkbookEditor replacement-payload guardrails.
///
/// The memory budget is an estimated in-memory working-set guardrail, not an
/// exact process RSS limit and not a save-time package assembly peak.
struct WorksheetEditorOptions {
    std::optional<std::size_t> max_cells;
    std::optional<std::size_t> memory_budget_bytes;
};

/// Explicitly materialize one worksheet for small-file random cell editing.
///
/// This future API resolves the current planned worksheet catalog and loads
/// only the requested sheet. It must not load every sheet during open().
/// Loading may fail before returning a WorksheetEditor if the worksheet exceeds
/// limits or uses source constructs that the current materializer cannot
/// import, such as source style ids, unsupported cell types, unsupported cell
/// metadata, malformed references, XML parser/entity errors, or invalid
/// sharedStrings relationship targets / XML structures / indexes. Valid
/// workbook-backed source shared string cells are materialized as plain text
/// rather than migrated; declared `count` / `uniqueCount` values are not used
/// to drive materialization.
///
/// The returned editor remains tied to this WorkbookEditor and is saved through
/// save_as(). It is not a Streaming writer, not a low-memory large worksheet
/// random-access API, and not relationship/style/sharedStrings repair.
WorksheetEditor& worksheet(std::string_view name,
                           WorksheetEditorOptions options = {});

/// Try to materialize one worksheet. Missing worksheet names return empty;
/// malformed or unsupported source worksheets still fail with FastXlsxError.
std::optional<std::reference_wrapper<WorksheetEditor>>
try_worksheet(std::string_view name, WorksheetEditorOptions options = {});

/// Future small-file worksheet editor.
///
/// This editor owns or references a compact sparse CellStore. Missing cells and
/// explicit blank cells are distinct: try_cell() returning empty means no
/// sparse record exists; returning CellValue::blank() means an explicit blank
/// record exists.
class WorksheetEditor {
public:
    std::string_view name() const noexcept;
    std::optional<CellValue> try_cell(std::uint32_t row,
                                      std::uint32_t column) const;
    void set_cell(std::uint32_t row, std::uint32_t column, CellValue value);
    void erase_cell(std::uint32_t row, std::uint32_t column);
    std::size_t cell_count() const noexcept;
    std::size_t estimated_memory_usage() const noexcept;
};
```

Draft README wording for the future first slice:

```cpp
// Future API sketch only; not available in the current public headers.
fastxlsx::WorkbookEditor editor = fastxlsx::WorkbookEditor::open("template.xlsx");

fastxlsx::WorksheetEditorOptions options;
options.max_cells = 10000;
options.memory_budget_bytes = 8 * 1024 * 1024;

auto& sheet = editor.worksheet("Data", options);
sheet.set_cell(2, 1, fastxlsx::CellValue::text("updated"));
sheet.erase_cell(3, 1);

if (auto value = sheet.try_cell(2, 1)) {
    // Missing cells return empty; explicit blank cells return CellValue::blank().
}

editor.save_as("output.xlsx");
```

README caveats for that future example:
- This is for small existing workbooks. Large exports should use
  `WorkbookWriter`; template-style whole-`sheetData` replacement should use the
  current `WorkbookEditor::replace_sheet_data()` facade.
- The first slice does not migrate shared string indexes, merge styles, repair
  relationships, recalculate formulas, rebuild calcChain, or update table /
  drawing / defined-name ranges. Valid workbook-backed source shared strings
  are imported as plain text only.
- If source cells use unsupported style/cell metadata shapes, or if
  sharedStrings relationship targets, XML structures, or indexes are invalid,
  loading the worksheet fails before returning a partially materialized editor.
  Declared `count` / `uniqueCount` mismatches alone do not drive
  materialization or repair.

Draft save-as acceptance design for future `WorksheetEditor`:
- Materialization without mutation must not create a pending edit by itself.
  A no-op `save_as()` after materializing a supported worksheet should remain a
  reader-backed roundtrip copy, unless a later implementation deliberately
  exposes materialization as a pending state and documents it.
- Dirty tracking starts at successful `set_cell()` / `erase_cell()`. Failed
  materialization, failed coordinates, failed limit checks, unsupported
  non-default style policy, and rejected operation mixing must not create or
  clear pending edits.
- The first public tests must cover a source-loaded worksheet where:
  - `set_cell()` creates a new number / boolean / text / formula cell.
  - `set_cell(..., CellValue::blank())` overwrites an existing source cell as an
    explicit blank output cell.
  - `erase_cell()` removes an existing source cell from emitted `sheetData`.
  - Existing untouched source cells remain in source order after sparse-store
    projection.
- Save-as output must verify package-level preservation: unknown entries,
  unrelated workbook parts, workbook relationships, content types, and
  unmodified worksheets remain copy-original under the current Patch baseline.
- Calc policy must be explicit. If edited cells include formulas or any
  worksheet rewrite uses the existing calc cleanup helper, tests must verify the
  chosen `fullCalcOnLoad` / calcChain cleanup behavior.
- Dimension policy must be explicit before public release. The first slice must
  refresh worksheet `<dimension>` to match emitted cell extents; stale dimension
  after `set_cell()` / `erase_cell()` is not acceptable as first public-slice
  behavior. This does not imply table ranges, defined names, drawings, charts,
  hyperlinks, validations, conditional formatting, or other range metadata are
  recalculated.
- Relationship and range metadata policy must remain audit/fail/preserve only:
  the first save-as route must not claim table range updates, defined-name
  updates, drawing/chart updates, hyperlink repair, relationship pruning, or
  orphan cleanup.
- Output path guardrails must match current `WorkbookEditor::save_as()`:
  exact/path-equivalent source overwrite, empty output path, missing parent,
  non-directory parent, and directory output fail before consuming pending
  worksheet edits.
- Operation mixing tests must prove the selected policy. If the first slice
  rejects `rename_sheet()` or whole-sheet `replace_sheet_data()` after a
  worksheet is materialized, those failures must preserve both materialized
  cells and existing queued edits.
- Diagnostics must distinguish the failing stage: source materialization,
  mutation preflight, operation-mixing preflight, output-path guard, or writer
  failure.

Implementation preflight checklist before adding a public `WorksheetEditor`
header:

1. Finalize names and ownership:
   - Use `WorksheetEditorOptions` as the per-call materialization options type.
   - Use `WorkbookEditor`-owned materialized state and return borrowed
     `WorksheetEditor&` / optional reference handles.
   - Previously returned handles are invalid after the owning `WorkbookEditor`
     is moved or move-assigned; callers must reacquire from the moved-to editor.
2. Freeze first-slice scope:
   - Include only `name()`, `try_cell(row, column)`, `set_cell(row, column,
     CellValue)`, `erase_cell(row, column)`, `cell_count()`, and
     `estimated_memory_usage()`. Resolved follow-up slices added `get_cell()` and
     strict uppercase A1 overloads.
   - Complete Excel row/column structural edits remain deferred. A later
     follow-up only added represented sparse row/column insert/delete shift
     helpers; style registry integration, metadata semantic editing, and range
     updates remain outside this slice.
3. Freeze style and dependency policy:
   - Recommended first slice rejects non-default `StyleId` in
     `WorksheetEditor::set_cell()`.
   - Caller-supplied non-default `StyleId` values still fail in
     `WorksheetEditor::set_cell()`, but canonical non-zero unsigned decimal
      source style ids now validate against the source styles.xml `cellXfs`
      table, materialize as numeric passthrough handles, and are written back
      when the source styles part is preserved. Explicit
     unqualified source `s` values normalize to no style handle only when the
     value is exactly `0` (`s="0"`, `s='0'`, or `s = "0"`); empty, valueless,
     unquoted, unterminated, padded, signed, leading-zero, entity-encoded,
     duplicate, or qualified source style attributes remain load failures. Valid
     workbook-backed source shared string indexes materialize as text; declared
     `count` /
     `uniqueCount` values do not drive materialization; invalid sharedStrings
     relationship targets, XML structures, or indexes fail before returning a
     handle.
4. Freeze dimension and calc behavior:
   - Edited output refreshes top-level worksheet `<dimension>` to match emitted
     cell extents.
   - Specify fullCalcOnLoad / calcChain cleanup behavior for formula edits.
5. Freeze diagnostics:
   - Materialization failures throw `FastXlsxError` and do not update current
     edit-only `last_edit_error()`.
   - Error text must include sheet name and stage-specific context without
     exposing internal `EditPlan`.
6. Prepare tests before implementation:
   - Public facade tests for load success, load limit failure, unsupported source
     failure, try/set/erase semantics, blank-vs-erase projection, style
     rejection, operation mixing rejection, save-as path guards, package
     preservation, calc policy, and dimension policy.
   - Internal tests should only be added for uncovered behavior, not duplicate
     loader-negative matrices.
7. Check test budget:
   - Current F2 source-loaded `CellStore` coverage is split into the
     `fastxlsx.package_editor.cellstore-*` shard family; sheetData catalog /
     guardrail / linked-object coverage is split into
     `fastxlsx.package_editor.sheetdata-catalog`,
     `fastxlsx.package_editor.sheetdata-guards`, and
     `fastxlsx.package_editor.sheetdata-linked`, leaving
     `fastxlsx.package_editor.sheetdata` for base sheetData / by-name coverage.
   - Preservation-heavy package-editor coverage is split into
     `fastxlsx.package_editor.preservation-core`,
     `fastxlsx.package_editor.preservation-removal`,
     `fastxlsx.package_editor.preservation-resources`,
     `fastxlsx.package_editor.preservation-comments`, and
     `fastxlsx.package_editor.preservation-linked` so one broad preservation
     shard does not consume the 60s default CTest budget.
   - If new public save-as tests would push any package-editor shard near the
     60s timeout,
     add a focused `fastxlsx.workbook_editor` / future
     `fastxlsx.worksheet_editor` target before adding more heavy package
     roundtrips.
8. Validation gate:
   - Header / implementation work requires targeted CTest plus full default
     `ctest --preset windows-nmake-release`.
   - Documentation-only preflight changes require `git diff --check` on touched
     docs.

该 facade 的边界：

- `WorksheetEditor` / random-edit 首片属于 In-memory editor 路径，不是
  `WorkbookWriter` 的随机写补丁，也不是当前 `Workbook` 小文件新建 API 的无界扩展。
- 它不承诺百万行 worksheet 的低内存随机访问，也不能写成完整 In-memory editor ready。
- 它不计算公式、不重建 `calcChain.xml`、不自动修复 relationships / content types、
  不迁移 shared string indexes 或 style ids。
- 已有文件保存仍使用 `save_as(...)` 语义，避免暗示当前支持安全原地覆盖。

后续拆分：

- P7.2 定义 `CellValue` public value 的语义和与现有 `Cell` / `CellView` 的转换边界。
- P7.3 定义内部 `CellStore` / `CellRecord`，避免把 owning `Cell` 用作长期 cell store。
- P7.4 定义 `max_cells`、`memory_budget_bytes`、`cell_count()` 和
  `estimated_memory_usage()` 等 guardrails。
- P7.5 定义 In-memory save-as 与 internal Patch handoff，尤其是 unknown part
  preservation、sharedStrings / styles / calc metadata 和 document properties 的边界。

### P7.2 CellValue Public Value Boundary

P7.2 冻结 `CellValue` 的 public value 语义；当前首个 implementation slice 已新增
`include/fastxlsx/cell_value.hpp`、`src/cell_value.cpp` 和 focused unit tests。这只表示
owning semantic value 已经落地，不表示 `WorkbookEditor` / `WorksheetEditor`、random
cell editing 或 save-as handoff 已经实现。P7.3a 已有 internal `CellStore` 首个切片，
但它只是后续 editor / guardrail / handoff 的内部输入。`CellValue` 是未来 editor
facade 的 API boundary value，而不是内部长期 cell storage layout。

`CellValueKind` 当前首片：

- `Blank`：显式 blank / clear 候选值。
- `Number`：有限 `double` numeric payload。
- `Text`：owned string payload。
- `Boolean`：boolean payload。
- `Formula`：owned write-only formula text。
- `Error`：owned opaque Excel error-token text。

当前 factory：

- `CellValue::blank()`
- `CellValue::number(double)`
- `CellValue::text(std::string)`
- `CellValue::boolean(bool)`
- `CellValue::formula(std::string)`
- `CellValue::error(std::string)`
- `CellValue::with_style(StyleId)` / `CellValue::without_style()`

所有权和现有类型关系：

- `CellValue` owns text / formula / error-token payload，可复制或移动跨过 editor API 调用边界。
- `CellView` 继续是 Streaming-only non-owning view；它的 `string_view` payload 只需在
  `WorksheetWriter::append_row()` 调用期间有效，不能存入 editor / in-memory 长期状态。
- `Cell` 继续是当前 `Workbook` 小文件新建路径的 owning convenience value；当前已提供
  `CellValue::from_cell(const Cell&)` / `CellValue::to_cell()` 这组 helper，用于在小文件
  便利值和 owning semantic value 之间转换，其中 blank 和 error token 没有 `Cell`
  表示。
- `CellValue` 可携带 optional `StyleId`。该 id 是 workbook-local handle；非默认 id
  必须来自同一 workbook/editor style registry，foreign / invalid id 的拒绝时机由后续
  实现定义。P7.2 不定义 style registry merge、existing-file style preservation 或
  style id migration。

读取和 blank 语义：

- `try_cell(ref)` 返回空表示 cell 不存在。
- `CellValue::blank()` 表示 caller 明确传入 blank / clear 候选值。
- `erase_cell(ref)`、blank 是否保留 style / metadata，以及 save-as 时如何表达 clear
  semantics，由 P7.3 cell store 和 P7.5 Patch handoff 决定。

数值和公式边界：

- `number(double)` 延续 `Cell` / `CellView` 的 finite-only 规则；不把 `NaN`、`+Inf`
  或 `-Inf` 转成字符串、空 cell 或 OpenXML 数字文本。
- date/time 仍由 caller 以 Excel serial number 写入 numeric value；date-specific cell
  type 和 date style generation 不属于 P7.2。
- `formula(std::string)` 只保存公式文本。FastXLSX 不解析公式、不求值、不写 cached
  result、不重建 `calcChain.xml`；save-as / calc metadata 策略由后续 handoff 任务定义。
- `error(std::string)` 只保存 source error token 文本。FastXLSX 不校验 Excel error
  token 集合、不把 error token 映射为 enum、不求值公式，也不生成 cached result。

非目标：

- 不把 public `CellValue` 写成 editor、random cell editing 或 save-as handoff 已实现。
- 不定义 rich text、array formulas、formula evaluator、cached formula values 或
  Excel error-token validation。
- 不把 `CellValue` 当作 `CellStore` / `CellRecord` 的 compact internal storage。
- 不承诺百万行 worksheet 随机访问、sharedStrings 索引迁移、styles 合并或 relationship
  repair。

### P7.3 CellStore / CellRecord Internal Model

P7.3 冻结 future In-memory editor 的内部 storage model；P7.3a 已新增首个
internal implementation slice：`include/fastxlsx/detail/cell_store.hpp` 和
`src/cell_store.cpp`。`CellStore` / `CellRecord` 仍是 implementation detail；普通用户仍
应通过当前 `WorkbookEditor` Patch subset 或 future `WorksheetEditor` / random-edit
扩展与 `CellValue` 边界交互。
当前另有 internal `cell_store_sheet_data_chunk_source()` 首片，用于把 sparse records
投影为 standalone `<sheetData>` row/cell chunk source；旧的完整
`cell_store_to_sheet_data_xml()` string helper 已删除，避免 future Patch handoff 重新
退回完整 `<sheetData>` payload 物化。它不是完整 worksheet writer，也不是
random-edit / in-memory save-as handoff。
当前 source-backed materialization 也已有 internal 首片：
`load_cell_store_from_worksheet_chunks()` / `load_cell_store_from_worksheet_xml()`
可从 worksheet XML 事件流建立 `CellStore`，
`load_cell_store_from_workbook_sheet()` 可经 `PackageReader` workbook catalog 按
sheet name 定位 worksheet part 后复用同一路径。generic worksheet XML/chunk
loaders 只接受 number、boolean、inline string、formula text、opaque error
token 和 explicit blank；
standalone sharedStrings、non-default style ids 和 unsupported cell shapes 仍 fail；
explicit source `s="0"` normalizes to no style handle。workbook-backed
loader 还可通过 existing `xl/sharedStrings.xml` 只读 materialize source `t="s"`
cells as text；`count` / `uniqueCount` 和 legal unknown attributes 不驱动
materialization，malformed structures/targets 或 invalid indexes fail fast。不迁移索引、不合并
styles、不修复 relationships，也不暴露 low-level public `PackageReader` /
`PackageEditor`。当前 package-level 回归已固定 source worksheet 含 non-default
`s="..."` style id、unsupported cell type 或 invalid boolean payload 时的失败策略，
同时覆盖 explicit default `s="0"` 归一化为 no style handle，并验证这些 source
dependency/shape 失败不会暴露 partial `CellStore` 或污染 `PackageReader` 后续
entry 读取能力。

核心原则：

- public `CellValue` 是 API boundary value，可以 own text / formula payload。
- internal `CellRecord` 是紧凑存储记录，不应长期保存 public `Cell` 或 `CellValue`
  对象。
- `CellStore` 是 worksheet-local sparse storage，只记录存在或被显式编辑过的 cells；
  当前首片使用 row-major `std::map<CellPosition, CellRecord>`，不分配完整
  row-by-column matrix。
- 该模型服务小文件随机编辑，不是 Streaming writer 的 row hot path，也不是百万行
  worksheet 的低内存随机访问方案。

`CellRecord` 字段草案：

- row / column key：由 `CellStore` sparse index 持有，使用 1-based worksheet 坐标或
  等价 normalized key；必须复用现有 A1 / row-column 边界校验。
- value kind：blank、number、text、boolean、formula、error。
- style marker：default style 或 workbook-local `StyleId` / internal style handle。
- payload：number / boolean 内联保存；当前首片把 text / formula / error-token 存为
  record-owned `std::string`，后续 compact-storage 演进可替换为 pool id。
- edit marker：可选 dirty / tombstone / cleared-state marker，用于后续 save-as /
  Patch handoff 判断写入、清除或保留行为。

`CellStore` 结构草案：

- 当前实现使用 row-major sparse map，保证可确定的 worksheet XML emission order；
  后续可以演进为 row buckets 或等价 sparse index。
- text / formula pool 是后续 compact-storage 工作；当前 P7.3a 为了边界最小化，
  仍在 `CellRecord` 中持有 owned string payload。
- 未来 pool id 是 internal handle，不等同于 `xl/sharedStrings.xml` index；是否迁移
  或复用 shared string indexes 由 P7.5 handoff 和后续 sharedStrings 策略定义。
- style handle 是 workbook-local；P7.3 不合并 styles、不修复 foreign style ids、
  不做 existing-file style preservation。
- row metadata、column metadata、merged ranges、hyperlinks、tables、drawings 和
  worksheet relationship-bearing metadata 不进入 `CellRecord`；这些对象需要独立模型或
  Patch-side preservation / audit。

missing / blank / erase 边界：

- missing cell 表示 sparse index 中没有 record。
- blank record 表示 caller 显式设置 blank / clear 候选值，可能需要在 save-as 时写出
  empty styled cell 或删除 prior value。
- 当前 P7/F2 internal `CellStore::try_cell(row, column)` 返回 nullptr 表示 missing；
  explicit blank 会返回非空 `CellRecord`，且 `kind == CellValueKind::Blank`。
  `find_cell()` 只是保留给现有内部调用的兼容 alias。
- 当前 P7.3a `erase_cell(row, column)` 移除 sparse record；是否写 tombstone、
  保留 style，或触发 Patch handoff 删除语义，由 P7.5 定义。
- 在 P7.5 前，不把 blank / erase 写成现有文件清除语义已完成。

P7.4 guardrail 输入：

- `cell_count()` 可按 active records 计数，是否计入 tombstone / blank record 由 P7.4
  固化。
- `estimated_memory_usage()` 应至少拆分 record bytes、sparse index overhead、string /
  formula pool bytes、style handle overhead 和 save-time XML/package assembly memory。
- `max_cells` 和 `memory_budget_bytes` enforcement 属于 P7.4；P7.3 只提供可计量维度。

非目标：

- 不实现 public editor APIs、string pool、formula pool、workbook-level guardrails 或
  save-as / Patch handoff。P7.4a 只有 internal `CellStore` guardrail first slice。
- 不承诺公式计算、cached formula values、calcChain rebuild、sharedStrings migration、
  styles merge、relationship repair 或 content type repair。
- 不把 in-memory cell model 用作 Streaming / Patch 的默认内部表示。

### P7.4 In-memory Guardrails

P7.4 冻结 future In-memory editor 的 size / memory guardrail 语义。P7.4a 已新增
internal `CellStoreOptions` first slice，覆盖 worksheet-local sparse store 的
`max_cells` / `memory_budget_bytes` enforcement。当前另有 public
`WorkbookEditorOptions` 首片，但它只限制 `WorkbookEditor::replace_sheet_data()`
replacement payload 的 `max_replacement_cells` /
`replacement_memory_budget_bytes`，并通过
`pending_replacement_cell_count()` / `pending_replacement_worksheet_names()` /
`has_pending_replacement()` /
`estimated_pending_replacement_memory_usage()` 暴露最终 queued payload 诊断；它不是
future In-memory `WorkbookEditor` / `WorksheetEditor` 的 workbook-level options、
load materialization limits、string/formula pool budget 或 random-edit memory model。
guardrails 是 small-file editor ready 的前置条件之一；没有 workbook-level public
options、load materialization limits、string/formula pool budget 和 P7.5 save-as /
Patch handoff 前，不能宣称 In-memory editor ready。

候选 options：

- `max_cells`：限制 materialized / edited cell records 的数量。具体默认值留给实现阶段，
  文档不得把任意数字写成稳定承诺。当前 internal `CellStoreOptions::max_cells`
  只限制单个 `CellStore` 的 sparse records。
- `memory_budget_bytes`：限制 editor 估算的 in-memory 工作集。该值是预算 guardrail，
  不是精确进程 RSS 控制。当前 internal `CellStoreOptions::memory_budget_bytes`
  只使用 `CellStore::estimated_memory_usage()` 的预算估算。
- diagnostic / strict mode 扩展点：允许 future implementation 决定是尽早拒绝、
  输出诊断，还是在某些 load 场景下先审计再拒绝；P7.4 只保留扩展点。

候选 diagnostic APIs：

- `WorkbookEditor::cell_count()`：workbook-level active record count。
- `WorksheetEditor::cell_count()`：worksheet-level active record count。
- `WorkbookEditor::estimated_memory_usage()`：workbook-level estimate。
- `WorksheetEditor::estimated_memory_usage()`：worksheet-level estimate。

计数口径：

- active value records 计入 `cell_count()`。
- blank / tombstone / cleared records 是否计入 `cell_count()` 必须由实现阶段固定；
  若计入，文档要说明这是为了反映 pending edit / save-as 成本。
- row / column metadata、merged ranges、hyperlinks、tables、drawings 和 relationships
  metadata 不应混入 cell count；它们可进入 memory estimate 的非 cell 部分。
- `estimated_memory_usage()` 至少拆分或内部计入 record bytes、sparse index overhead、
  string / formula pool bytes、style handle overhead、metadata model overhead，以及
  save-time XML/package assembly memory。
- 估算值是 capacity planning 和 limit enforcement 依据，不是精确 allocator profiler。

enforcement 时机：

- `open(...)` / load materialization：源 workbook 超出 limits 时应拒绝或进入明确的
  audit / diagnostic path，不能静默降级为半加载 workbook。
- `set_cell(...)`、`erase_cell(...)`、append/insert/delete row、sheet-level mutations：
  如果会超过 `max_cells` 或 `memory_budget_bytes`，应在污染 editor state 前拒绝。
  当前 P7.4a 已覆盖 internal `CellStore::set_cell()` 的 no-state-pollution 插入/覆盖拒绝，
  以及 `set_cell()` / `try_cell()` / `erase_cell()` 坐标校验失败不污染已有 sparse
  records 的回归。
- string / formula pool growth：新增 text / formula payload 的 pool bytes 必须纳入预算。
- `save_as(...)` 前：需要复核 save-time XML/package assembly memory 估算，避免把
  runtime store 低估当作保存阶段内存保证。

错误语义：

- 超限使用 `FastXlsxError` 或 future 明确错误码。
- 错误消息应包含触发的 limit kind、当前估算值和建议路径：大规模顺序导出使用
  Streaming，已有文件局部替换或模板填充使用 Patch。
- 失败 mutation 不能部分应用；如果实现阶段无法保证原子性，必须在 API 注释中明确
  state-after-failure 语义并提供 recovery guidance。

非目标：

- 不实现 public guardrail APIs。
- 不把 internal `CellStoreOptions` 写成 current public `WorkbookEditorOptions`；两者
  作用域不同，后者只覆盖 `replace_sheet_data()` replacement payload。
- 不承诺默认 limit 数字、精确 RSS 统计或 allocator-level tracking。
- 不把 guardrails 解释为百万行 worksheet 随机编辑可低内存运行。
- 不替代 P7.5 save-as / Patch handoff 中的 sharedStrings、styles、calc metadata 和
  unknown part preservation 策略。

### P7.5 Future In-memory Save-as / Patch Handoff Draft

P7.5 只冻结 future In-memory editor 的 `save_as(...)` 与 internal Patch / package
rewrite 底座之间的交接契约，不新增 public editor code，也不把 internal
`PackageEditor` 暴露成 public API。该 contract 用于说明 small-file random edits 如何
写回新 package path，以及哪些 existing-file 语义必须 preserve、audit 或 fail。

`save_as(output_path)` contract：

- 不承诺原地 atomic overwrite。
- source-backed editor 必须拒绝 exact / path-equivalent source overwrite。
- output path guard 应拒绝 empty output path、missing parent、non-directory parent 和
  directory output。
- guard failure 不应清空 pending edits、EditPlan、planned output 或 diagnostic state；
  caller 应能换一个安全路径再次 `save_as(...)`。
- writer failure 不能写成已提交 package mutation；如果实现阶段无法保证 atomic temp-file
  replacement，API 注释必须说明残留 output artifact 和 recovery guidance。

handoff source modes：

- new workbook materialization：从 editor workbook model 生成完整新 package，不存在
  source unknown part preservation 问题。
- existing package-backed materialization：以 source package 为 preservation baseline，
  未修改 entries 和 unknown entries 默认 copy-original。
- source-backed mode 需要保留 package relationship、content type、workbook relationships
  和 unmodified part bytes，除非对应 edit 明确进入 rewrite / omission / removal plan。

EditPlan 交接：

- `CellStore` mutations 应投影成 worksheet part rewrite 或 bounded local rewrite plan；
  plan 必须说明 worksheet、workbook metadata、content types、relationships 和 package
  entries 的 side effects。
- 当前 internal 回归验证 `CellStore` 生成的 standalone `<sheetData>` payload 可以
  交给 by-name `PackageEditor` `sheetData` Patch helper，并在 output plan 中暴露
  bounded local rewrite、calc policy 和 unknown entry preservation；当前 public
  `WorkbookEditor::replace_sheet_data()` 已复用这条窄 handoff，并通过
  `CellStore` row/cell chunk source 避免先构造完整 standalone `<sheetData>`
  字符串；internal sheetData helper 也会在 rewritten worksheet output pass 中直接
  消费这些 replacement chunks，而不是先单独 staging/replay，但这仍不是 random cell
  editing / in-memory materialization save-as。
- 当前 internal F2.3 smoke 还覆盖了 source-backed 路径：从 `PackageReader`
  sheet-name helper 加载 source worksheet 到 `CellStore`，执行小范围
  `set_cell` / `erase_cell` / explicit blank mutation，再把 `CellStore` row/cell
  chunk source 交给 by-name `sheetData` Patch helper。该回归只证明窄 handoff、
  stale calcChain cleanup 和 unknown bytes preservation；后续 focused smoke 还固定
  当前 projection 行为：source cell 被 `CellValue::blank()` 覆盖时输出 empty
  `<c>`，source cell 被 `erase_cell()` 删除时从 replacement `<sheetData>` 中省略。
  F2.4 dependency/shape guardrail smoke 现在固定 malformed source style
  attributes、unsupported cell types 和 invalid boolean payload 的 fail policy，
   并固定 explicit source `s="0"` 归一化为 no style handle；canonical non-zero
   source style ids 先验证 source styles.xml `cellXfs` 再做 numeric passthrough，
   不做 style migration / merge。workbook-backed valid source sharedStrings indexes now
  materialize as text, non-critical count/attribute metadata does not drive
  materialization, and malformed sharedStrings structures/targets or invalid
  indexes fail fast。这些失败路径不暴露 partial `CellStore`。这不代表 tombstone policy、
  style preservation、sharedStrings/style migration、relationship repair 或完整
  in-memory save-as。
- `planned_output()` / equivalent diagnostic snapshot 应能展示 active rewrites、
  copy-original entries、omitted entries、removed-part inbound audit 和 calc policy。
- 显式 removal / omission audit 不等于 relationship pruning；P7.5 不自动删除 inbound
  relationships 或 orphan linked parts。

worksheet value handoff：

- `CellRecord` 只负责 value kind、payload 和 style reference 输出。
- row/column metadata、merged ranges、autoFilter、data validations、conditional
  formatting、hyperlinks、tables、drawings、images、comments、OLE/control parts 和其他
  relationship-bearing worksheet metadata 需要独立 model、Patch preservation 或 audit。
- `erase_cell(ref)`、blank records 和 tombstones 的 save-as 行为必须明确：删除 `<c>`、
  写 blank styled cell、保留 style / metadata，或记录 unsupported/fail。P7.5 不实现
  existing-file cell clearing。

sharedStrings / styles / calc policy：

- internal string / formula pools 不等同 source `xl/sharedStrings.xml` indexes。
- existing shared string index migration、string table rebuild 和 worksheet `t="s"`
  reference repair 不是 P7.5 已实现能力；contract 只能写 preserve / audit / fail /
  future strategy。当前 source-backed `CellStore` loader 对 `t="s"` 走 fail policy。
- `StyleId` / internal style handles 是 workbook-local；foreign style ids、existing-file
  styles merge 和 style id migration 不是 P7.5 已实现能力。当前 source-backed
  `CellStore` loader 对 source cell `s="..."` 走 fail policy，包括已读到前置普通
  cell 后出现的 explicit default `s="0"`，且不暴露 partial store。
- Unsupported source cell types and invalid scalar payloads are current
  fail-before-return guardrails, not repair or coercion behavior. Source
  dependency/shape failures after earlier loadable cells do not expose a partial
  `CellStore`.
- formula/value edits 可以请求 full recalculation 或 apply current calcChain
  remove/preserve policy；不求值、不写 cached formula results、不实现 calcChain rebuild。

非目标：

- 不实现超出当前 `WorkbookEditor` Patch subset 的 random-edit / in-memory
  materialization `save_as()`。
- 不实现 random cell editing 或 source workbook cell materialization。
- 不修复 relationships / content types / defined names / table ranges / chart ranges。
- 不宣称 broad existing-file preservation；只能要求 unknown/unmodified entries default
  copy-original，并把不理解的 linked semantics 暴露为 audit / fail。

### P8.1 Future Controlled Large Worksheet Editing Boundary Draft

P8.1 只冻结 large worksheet controlled editing 的 future boundary，不新增
`WorksheetReader`、`WorksheetRewriter`、`TemplateEditor` 或 public Patch API。它服务
sheet replacement、bounded range patch、template fill 和 row/cell streaming
transformation，不是 P7 In-memory random editing，也不是当前 bounded `sheetData`
local rewrite helper 的低内存承诺。

处理管线草案：

```text
source worksheet event reader
→ row/cell transformer
→ streaming worksheet writer
→ package EditPlan / output-plan diagnostics
```

API 模式：

- 属于 Patch / controlled large worksheet editing。
- 输入应按 row / event / token 顺序处理，不暴露完整 worksheet DOM。
- 输出应是 worksheet part stream rewrite 或 full sheet replacement plan。
- `EditPlan` 必须显示 worksheet rewrite、workbook calc metadata、content type /
  relationships package-entry side effects，以及 copy-original unknown/unmodified entries。

能力边界：

- sheet replacement：caller 提供完整 replacement worksheet payload 或 streaming source；
  仍需 worksheet root validation 和 dependency audit。
- bounded range patch：只允许能用 event transformer 定位和替换的明确范围；不承诺
  任意 O(1) random cell edits。
- template fill：按 event stream 替换占位符或受控区域，不把整张 worksheet materialize
  成 cell matrix。
- row/cell transformation：可作为 future transformer callback 形态，但必须说明
  ordering、lookahead 和 memory budget。

metadata / dependency 边界：

- worksheet metadata，例如 sheetPr、dimension、sheetViews、cols、mergeCells、autoFilter、
  dataValidations、conditionalFormatting、hyperlinks、tableParts、drawings、comments、
  OLE/control 和 printerSettings，默认 preserve / audit / fail；不静默 repair。
- sharedStrings indexes、style ids、definedNames、table ranges、drawing anchors、chart
  ranges 和 formulas 不能被 P8.1 写成自动迁移或重写。
- formula/value edits 最多请求 full recalculation 或按 calcChain remove/preserve 策略处理；
  不求值、不写 cached values、不实现 calcChain rebuild。

非目标：

- 不实现 XML event reader、transformer 或 stream rewrite code。
- 不声明当前 internal `replace_worksheet_sheet_data()` 是低内存大文件 transformer。
- 不提供 random cell editor、relationship repair、table resize、sharedStrings migration
  或 style merge。

### P8.2 Future Worksheet Event Reader Token Model Draft

P8.2 只冻结 future internal worksheet event reader 的 token model，不新增
`WorksheetReader`、`EventReader`、`WorksheetRewriter` 或 public API。Token model 是 P8.3
transformer 和 P8.4 stream rewrite 的内部输入契约，不能要求 reader 持有完整 worksheet
DOM、row map 或 cell matrix。

token categories 草案：

- `DocumentStart` / `DocumentEnd`：XML declaration、prolog comment / processing
  instruction 的 pass-through 边界。
- `WorksheetStart` / `WorksheetEnd`：worksheet root local-name、namespace declarations
  和 root attributes 的 preservation 边界。
- `MetadataRaw`：sheetData 外或未由 P8 transformer 理解的 metadata subtree，作为 bounded
  raw XML event pass-through / audit 输入。
- `SheetDataStart` / `SheetDataEnd`：worksheet row/cell streaming 区间。
- `RowStart` / `RowEnd`：row number、raw row attributes、height/custom-height metadata
  和 source-order information。
- `CellValue`：known scalar cell payload，包含 cell reference、row/column index、
  OpenXML cell type token、style id raw token、formula text 和 scalar value。
- `CellRaw`：rich text、array formula、unknown child、oversized payload 或 unsupported
  cell shape 的 raw-preserve / fail 候选。
- `Unsupported` / `Malformed`：可结构化携带 reason、element local-name、cell/row ref
  和 source context。

row token 边界：

- row number 必须按 Excel row limit 校验。
- raw row attributes 需要能 preserve 未理解属性；reader 不负责规范化全部 row metadata。
- row token 不能要求缓存完整 worksheet；如 transformer 需要 lookahead，必须在 P8.3
  明确 bounded lookahead。

cell token 边界：

- cell reference 应解码为 row / column index，并验证 Excel column / row limit。
- numeric、boolean、shared-string index、inline string、formula text 可作为 known scalar
  payload；非有限数字、非法 shared string index 或 unsupported formula shape 应进入
  error / raw / fail policy。
- style id 作为 raw integer token 或 workbook-local handle candidate 暴露，不做 style
  migration 或 foreign style repair。
- formula token 只保留 formula text；不求值、不写 cached values、不维护 calcChain。
- rich text inline string、array formula、data table formula、extLst 或未知 cell children
  默认 `CellRaw` preserve / audit / fail，不静默重写。

memory / pass-through 边界：

- token lifetime 限定在当前 event、当前 row 或声明的 bounded lookahead。
- 大型 inline string、rich text、unknown metadata subtree 或 extLst 不能导致整张
  worksheet materialization；实现阶段必须选择 stream-through、bounded raw buffer 或 fail。
- reader 不修复 namespace prefixes、relationship ids、table ranges、drawing anchors、
  shared string indexes、style ids、definedNames 或 formulas。

非目标：

- 不实现 XML parser。
- 不定义 public callback API。
- 不承诺完整 worksheet schema validation。
- 不把 current bounded `sheetData` local rewrite 升级描述为 event reader。

### P8.3 Future Row/Cell Transformer Contract Draft

P8.3 只冻结 future internal row/cell transformer contract，不新增 public callback API
或 implementation。Transformer 消费 P8.2 worksheet event tokens，产生 P8.4 stream writer
可消费的 ordered actions 和 diagnostics；它不是 P7 In-memory random editor。

input contract：

- 按 source worksheet order 消费 document、metadata、sheetData、row 和 cell tokens。
- token payload 只在当前 callback、当前 row 或声明的 bounded lookahead 内有效。
- transformer configuration 必须在 rewrite 开始前声明 selector：目标 range、row set、
  placeholder pattern、template binding 或 predicate。
- selector 可以持有小型索引或目标表，但不能要求 full worksheet pre-scan 后回写已输出 rows。

output actions 草案：

- `PassThrough`：原样转发 source token / raw XML。
- `ReplaceCell`：替换当前 cell 的 scalar value / formula / style reference candidate。
- `ReplaceRow`：替换当前 row 的完整 row event sequence。
- `InsertRowBefore` / `InsertRowAfter`：在当前 row 边界附近插入 bounded row sequence。
- `DeleteCell` / `DeleteRow` candidate：只作为 future action；具体 dimension、metadata
  和 dependency side effects 由 P8.4 / P8.5 固化。
- `EmitRaw`：输出 bounded raw worksheet XML，必须保留 well-formedness 责任边界。
- `RequestRecalculation`：报告 value/formula 变更需要 workbook calc metadata update。
- `FailUnsupported`：结构化失败，携带 selector、token context 和 unsupported reason。

ordering / memory 边界：

- 输出必须保持 deterministic row-major order。
- transformer 不得在 row 已交给 stream writer 后再修改该 row。
- 允许 bounded row buffer、bounded lookahead 和 selector index；禁止 full cell matrix、
  unbounded row cache 或 worksheet DOM。
- 插入 / 删除 row 这类会影响后续 references 的 action 必须显式声明是否需要 range /
  relationship / formula audit；默认不能静默重写外部 references。

dependency / audit 边界：

- transformer 可报告 sharedStrings indexes、style ids、definedNames、formula refs、
  table ranges、merged ranges、drawing anchors、hyperlinks 和 worksheet relationships 的
  review notes。
- transformer 不迁移 shared string indexes、不合并 styles、不修复 relationships、
  不 resize tables、不重写 chart ranges、不计算公式。
- formula/value edits 最多请求 full recalculation 或 calcChain remove/preserve policy。

failure contract：

- preflight 应拒绝明显 unsupported selector，例如需要 unbounded lookbehind 的 edit。
- streaming 阶段遇到 unsupported token 或 malformed source 时，应 abort rewrite，并把
  state-after-failure / output artifact 语义留给 P8.4。
- failure diagnostics 必须足够定位 selector、row/cell ref 和 token kind。

非目标：

- 不实现 transformer callbacks。
- 不定义 stable public action enum。
- 不把 transformer contract 写成 arbitrary range editor 已可用。
- 不替代 P8.4 `EditPlan` / stream rewrite output contract。

### P8.4 Future Stream Rewrite / EditPlan Output Contract Draft

P8.4 只冻结 future internal stream rewrite output 和 `EditPlan` handoff，不新增
`WorksheetRewriter` implementation、callback API 或 public Patch API。它定义 P8.3 ordered
actions 如何变成 staged worksheet part source、diagnostics 和 package-level plan。

rewrite pipeline contract：

- 输入只来自 P8.2 tokens 与 P8.3 transformer actions；writer 不重新扫描完整 worksheet。
- pass-through raw events 必须保持 source order；replace / insert / delete candidate
  actions 必须按 deterministic row-major order 消费。
- worksheet output 先作为 staged part source 存在；只有 rewrite 成功完成、最小 worksheet
  root/order 检查和 dependency policy 决策通过后，才能进入 active `EditPlan`。
- stream writer 只维护 bounded output buffer、current row state 和 incremental dimension
  state；禁止 worksheet DOM、full cell matrix、unbounded raw XML cache 或回写历史 rows。

action consumption 草案：

- `PassThrough` / `EmitRaw`：转发 known token 或 bounded raw XML，并保留 well-formedness
  和 ordering 责任边界。
- `ReplaceCell`：复用 writer cell serialization 规则，更新 dimension candidate，并把
  shared-string index、style id 和 formula/calc dependency 作为 audit 输入。
- `ReplaceRow` / `InsertRowBefore` / `InsertRowAfter`：写 bounded row event sequence；
  row number、dimension 和 downstream range/reference side effects 必须进入 diagnostics。
- `DeleteCell` / `DeleteRow` candidate：除非 selector 和 dependency policy 能证明安全，
  否则应降级为 unsupported/fail 或 audit-required；P8.4 不静默重写 metadata ranges。
- `RequestRecalculation`：只请求 workbook `fullCalcOnLoad` / calcChain remove-or-preserve
  policy；不求值、不写 cached values、不实现 `CalcChainAction::Rebuild`。
- `FailUnsupported`：abort rewrite，并保留 selector、token context、row/cell ref 和 reason
  diagnostics；不提交 package mutation。

`EditPlan` handoff：

- 成功 rewrite 应记录 worksheet part `StreamRewrite`、rewrite reason、target worksheet part、
  selector context 和 source/staged-output boundary。
- `EditPlan` / planned output diagnostics 应暴露 copy-original linked parts、content types /
  package relationships / owner `.rels` side effects、removed calcChain audit、calc policy、
  `WorksheetPayloadDependencyAudit` 和 `RelationshipTargetAudit`。
- sharedStrings、styles、definedNames、tables、drawings、hyperlinks、OLE/control、
  printerSettings、comments、charts 和 unknown relationship targets 只按 preserve / audit /
  fail / explicit rewrite policy 处理；stream rewrite 不做迁移或 repair。
- planned output snapshot 只是 internal diagnostic，不是 stable public output planner。

failure contract：

- preflight fail 不应改变 active `EditPlan`、manifest、package-entry audit、calc policy 或
  planned output。
- streaming fail 不应产生可保存 package mutation；temporary part files / buffers 只能是
  未提交实现细节，cleanup 是实现责任而不是 public artifact contract。
- failure diagnostics 必须足够定位 selector、worksheet part、row/cell ref、action kind 和
  dependency policy reason。

非目标：

- P8.4 contract 本身不交付完整 stream rewrite code；后续 P8.16/P8.17 已有 internal
  output-side file-backed handoff 和 source-entry extraction first slice，但仍不是完整
  low-memory transformer。
- 不声明 current bounded `replace_worksheet_sheet_data()` 是低内存 streaming rewrite。
- 不提供 relationship repair、table resize、formula rewrite、sharedStrings/style migration
  或 calcChain rebuild。
- 不把 P8.4 写成 public `PackageEditor` / `WorkbookEditor` API 已可用。

### P8.5 First Controlled Template-Fill Fixture

P8.5 固定第一个 controlled edit fixture，而不是新增 public template API。当前 fixture
使用 internal `PackageEditor::replace_worksheet_sheet_data_by_name()` 对 FastXLSX writer
生成的 source workbook 执行 template-fill 风格的 by-name `<sheetData>` replacement。

fixture contract：

- source workbook 由 `WorkbookWriter` 生成，包含 placeholder-like shared strings、
  styles 和 untouched worksheet。
- edit 使用 caller-supplied replacement `<sheetData>`，不解析 placeholder、不做 range
  patch engine，也不暴露 callback API。
- replacement 可使用 inline string 规避 shared string index migration；若 replacement
  引用 style id，只记录 styles dependency audit。
- output 必须保留 untouched worksheet、content types、package relationships、workbook
  relationships、`xl/sharedStrings.xml` 和 `xl/styles.xml` bytes。
- workbook calc metadata 可请求 `fullCalcOnLoad`；source 没有 calcChain 时不得凭空创建
  `xl/calcChain.xml`。

边界：

- 当前 helper 仍是 bounded local worksheet XML rewrite，会物化 planned worksheet XML；
  它只是 P8 future stream rewrite 的 baseline fixture。
- 保留旧 placeholder sharedStrings 是 preserve 证据，不是 pruning、sharedStrings migration
  或 garbage collection。
- `planned_output()` 只能写成 internal diagnostic snapshot，暴露 target worksheet
  `LocalDomRewrite`、copy-original entries、calc policy 和 dependency audits。
- 这不代表 `TemplateEditor`、large-file low-memory transformer、public Patch API、table
  resize、relationship repair、style merge 或 formula rewrite。

P8.1-P8.5 到此只是 controlled large worksheet editing baseline：边界、token / action /
handoff contract 和首个 bounded local fixture 已固定。后续 P8.16/P8.17 已经补上
internal event reader / row-cell transformer、output-side file-backed chunk handoff 和
source-entry file-backed extraction first slice；这仍不表示 complete low-memory stream
rewrite implementation、public editor API 或任意大 worksheet 随机编辑能力已经存在。

任何新增 public API 任务都必须先回答两个问题：

1. 它属于 `WorkbookWriter`、`Workbook`、当前 `WorkbookEditor` subset，还是 future
   editor extension？
2. 它复用哪些已有 public 值类型，是否不必要地暴露了 internal OPC / package 类型？

## API 分层原则

FastXLSX 可以提供多层 API，但每层都要标明成本。

### Streaming API

用于新建 XLSX、大数据导出、多 sheet 批量写入。

要求：

- 接受 row iterator、range、callback 或 chunk writer。
- 写入顺序应清晰，不承诺随机回写已输出历史行。
- 内存占用与当前行 buffer、XML output buffer、file-backed/chunked worksheet entry
  buffer、字符串策略和 package / ZIP writer 状态相关。
- 不持有完整 worksheet cell matrix；如果 finalization 避免完整 worksheet XML
  内存副本，也必须说明该保证只覆盖对应 worksheet entry，不覆盖所有 package parts。
- worksheet metadata API，例如 `WorksheetWriter::add_data_validation()`，应只保存
  轻量规则状态；内存成本按规则数量、multi-area `sqref` 的区域数量/文本长度、
  公式文本长度和 prompt/error 文本长度增长，不能为了校验规则而读取或持有完整
  worksheet cell matrix。

### Patch API

用于编辑已有 XLSX。

要求：

- 以 part-level rewrite 为基本模型。
- 未修改 part 原样复制。
- 当前内部 `PackageReader` 读取 stored/no-compression entries；在
  `FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建下还能读取 DEFLATE entries，默认构建仍拒绝
  compressed input。它使用 central directory 中的 size/CRC 作为 entry 索引权威，
  读取时校验解压后 payload CRC；data descriptor entries 现在可按该 central
  directory 元数据读取。它仍拒绝 local header method/name mismatch、无 data
  descriptor 时的 local header CRC/size mismatch、encrypted flags、
  Zip64、非法 ZIP entry name（绝对路径、尾部斜杠、
  反斜杠、query/fragment components、空段、dot 段或 parent 段）、冲突 content type
  default / override、同一 `.rels` owner 内重复 relationship id，以及
  namespaced metadata attributes（namespace declarations 除外），
  duplicate unqualified metadata attributes，
  non-whitespace metadata text，
  start/end tag QName mismatches，
  `[Content_Types].xml` / `.rels` 第一个真实 XML 元素不是 `Types` / `Relationships`
  的 decoy-root metadata；reader 只 ingest root 的 direct-child
  `Default` / `Override` / `Relationship` 元素，metadata declaration 嵌套在
  unsupported child 下也会失败；这些拒绝
  只是 reader validation，不是 content-type 或 relationship repair。不要把
  这写成完整 ZIP reader 或 public editing API。
- 大型 worksheet 流式重写。
- 小型 XML part 才允许局部 DOM。
- 每个操作都应能映射到 EditPlan：哪些 part copy-original，哪些 part stream-rewrite，
  哪些 part local-DOM-rewrite，哪些已注册 part 显式进入 removed-part audit。
- 对 `[Content_Types].xml`、package `_rels/.rels`、owner `.rels` 这类非 part
  package entries，当前内部 `EditPlan` 可以记录 rewrite / omission / preserved
  copy-original package-entry 审计项；这只是 relationship/content-type side effect
  可见性，不是 public metadata editor。
- 涉及单 sheet 编辑时，必须说明是否影响 sharedStrings、styles、worksheet `.rels`、
  tables、drawings、defined names、calcChain 和 workbook calc metadata。
- 默认策略应保守：未知 part 和未修改 part 原样保留；不懂的图表、透视表、宏和扩展
  不应被静默重写。

### In-memory API

只用于小文件和复杂编辑。

要求：

- 必须显式标注不承诺大文件低内存。
- 不得成为大数据写入的默认路径。
- 不得让用户误以为随机访问 API 适合百万行级导出。
- 可以提供完整随机编辑体验，例如 `get_cell()`、`set_cell()`、`erase_cell()`、
  局部样式和对象修改，但必须限制为小文件路径。
- `Cell` / `CellValue` 这类 public 类型应优先作为 API 输入、返回值或临时值；
  不能直接作为百万级单元格的内部长期存储模型。
- In-memory 内部存储应独立设计 `CellStore` / `CellRecord`，不要长期保存 public
  `Cell` / `CellValue` 对象。当前首片已经有 sparse record；后续 compact-storage
  工作仍需要把字符串 id、公式 id、style id 等进一步分离，并让字符串和公式走
  池化/去重，避免每个 cell 都携带 owning `std::string`。
- 必须设计 size / memory guardrails，例如 `max_cells`、`memory_budget_bytes`、
  `cell_count()`、`estimated_memory_usage()` 或类似诊断入口；超限时应明确提示调用方
  改用 Streaming 或 Patch。

## 文件职责边界

API 设计不只要控制运行时成本，也要控制实现和测试的变化范围。

- public API 可以继续集中在现有 public headers，保持用户入口稳定。
- 核心 writer 文件应保留流程编排、生命周期管理和热路径调用，不应无限承载所有
  feature 的 XML 序列化、校验和状态转换。
- 当一个 feature 已经有独立 public API、独立 XML 结构、独立状态、独立 QA helper
  或大量边界测试时，任务设计应考虑 feature-specific 实现文件、detail helper 和
  独立测试文件。
- 测试文件应镜像功能边界：主 streaming 测试保留主流程、边界和跨功能集成；feature
  的细粒度结构测试、负例和 QA 样例应逐步拆到独立测试文件。
- QA helper 应保持单一功能职责；跨功能 helper 只用于验证真正跨对象的 package
  side effects、relationship id 或兼容性行为。
- 新增 `.cpp` 或测试文件时，任务计划必须包含 CMake 列表同步。
- 不要过度拆分：很小的协调代码、一次性修正或尚未稳定的实验切片，可以先留在现有
  文件中，等边界和增长趋势明确后再拆。

## 文档注释要求

公共头文件中的 public API 应编写文档注释。推荐使用 Doxygen 风格：

```cpp
/// Writes rows to the worksheet streaming path.
///
/// This API does not keep a full worksheet cell matrix in memory. Rows are
/// consumed in order and previously written rows cannot be randomly modified.
///
/// @param rows Row range or iterator source.
/// @throws FastXlsxError when XML encoding or package writing fails.
```

文档注释至少说明：

- API 所属模式：Streaming、Patch 或 In-memory。
- 是否保留完整 worksheet 状态。
- 输入顺序要求。
- 是否允许随机访问或回写历史行。
- 对 Patch API：是否生成 EditPlan、会改写哪些 part、哪些 part 会原样保留、是否设置
  fullCalcOnLoad 或处理 `calcChain.xml`。
- 字符串策略相关行为。
- OpenXML part 副作用，例如是否新增或修改 `xl/styles.xml`、relationships、
  content types、drawing、media、tables 或 docProps。
- 错误处理方式。
- 性能/内存注意事项。

写入 OpenXML 数值或数值型 worksheet metadata 的 `double` API 还要写清
finite-only 边界。`Cell::number(double)`、`CellView::number(double)`、
`RowOptions::height` 和 `WorksheetWriter::set_column_width()` 不接受 `NaN`、
`+Inf` 或 `-Inf`。当前 streaming 路径由 `WorksheetWriter::append_row()` 拒绝
非有限 number / row height，`WorksheetWriter::set_column_width()` 立即拒绝非有限
width；小型 in-memory `Workbook` 路径在 `Workbook::save()` 序列化 worksheet XML
时报 `FastXlsxError`。不要把 `NaN/Inf` 写成字符串、空单元格或 OpenXML 数字文本。

当前 `Cell` / `CellView` 都没有专用 date cell 类型；日期/时间单元格只能由调用方按
Excel serial number 写入 numeric cell。P9 已有 streaming-only 自定义 number format
styles，但 number format 只控制显示格式，不编码 date cell type、不计算日期序列值，也不验证
日期语义。不要把 `DataValidationType::Date` 误写成 date cell encoding 已实现。

对 styles 相关 public API，注释还要写清当前只支持 Streaming / new-workbook-only 的
custom number format、窄 wrap-text + limited horizontal/vertical alignment、
窄 bold/italic/direct ARGB font color 和窄 solid foreground fill slices。`StyleId` 是 workbook-local handle，默认 id `0`
表示 workbook default style；非默认 id 必须来自同一个 `WorkbookWriter::add_style()`
返回值，不能跨 workbook 复用。`CellStyle::number_format` 可为空，表示不改变 number
format；`CellAlignment::wrap_text=true`、`HorizontalAlignment::{Left,Center,Right}`
和 `VerticalAlignment::{Top,Center,Bottom}` 是当前 alignment 子能力，false flags
和空 optional 不贡献 style 属性。`CellFont::bold`、`CellFont::italic` 和
`CellFont::color` 是当前 font 子能力，`CellFont::color` 只支持 direct ARGB；false
flags 或空 optional 不贡献 style 属性；`CellFill::foreground` 是当前唯一 fill 子能力，
使用 `ArgbColor` 写 solid foreground fill，空 optional 不贡献 style 属性。`WorkbookWriter::add_style()` 应拒绝完全空的
style；重复完整 style 复用同一个 `StyleId`，相同 number format 在不同 style 组合中
复用同一个 custom `numFmtId`，相同 bold/italic/direct-color font 组合复用同一个 `fontId`，
相同 foreground ARGB fill 组合复用同一个 `fillId`，
不做 Excel 语义规范化。`CellView::with_style()` 只在 cell view 中携带
小句柄；`WorksheetWriter::append_row()` 必须在推进 row count、dimension、
sharedStrings 或 formula recalculation metadata 前拒绝无效或 foreign style id。
启用非默认样式会在 `close()` 时生成 `xl/styles.xml`、styles content type override 和
workbook styles relationship，并在 worksheet cell 上写 `s="N"`；默认 style 不写
`s="0"`，也不新增 worksheet `.rels`。alignment 只写 `cellXfs` 中的
`applyAlignment="1"` / `<alignment .../>` attributes：`wrapText="1"`、
`horizontal="left|center|right"` 和 `vertical="top|center|bottom"`；不计算
row height，也不代表完整 alignment。bold/italic/direct-color font 只写 `<fonts>` 中的
`<b/>` / `<i/>`、可选 `<color rgb="AARRGGBB"/>`、`fontId` 和
`applyFont="1"`，不代表完整 font control。solid fill 只写 `<fills>` 中的 solid
`<patternFill>`、`fgColor rgb`、`bgColor indexed="64"`、`fillId` 和 `applyFill="1"`，
不代表完整 fill/pattern/theme/tint/indexed palette control。当前不支持 theme/tint/indexed
font color、font size/name/family、underline、border/full alignment、rich text、dxf-backed
conditional formatting、existing-file style preservation 或完整
Excel formatting parity。当前 two-/three-color color scale、basic data bar 和 basic
3Arrows icon set API 是 worksheet metadata，不代表 styles registry 或 `dxfs` 已支持。

对 conditional formatting 这类 worksheet metadata API，注释还要写清是否只支持
Streaming / new-workbook 路径、是否复制规则 endpoint 和 range 列表、内存是否按规则数
和 range 数增长、`ArgbColor` 是否序列化为 8 位大写 ARGB、`ColorScaleValueType` /
`DataBarValueType` / `IconSetValueType` 的 endpoint/threshold token 和 finite-only
数值边界、priority 是否按 worksheet 内调用顺序分配、
multi-range `sqref` 是否排序/合并/去重/检查重叠，以及是否新增 relationships、
content types、styles 或 calc metadata。当前
`WorksheetWriter::add_conditional_color_scale()` 只写 worksheet-local two-/three-color
`<conditionalFormatting><cfRule type="colorScale">` XML；
`WorksheetWriter::add_conditional_data_bar()` 只写 worksheet-local basic
`<cfRule type="dataBar">` XML，`DataBarRule::show_value=false` 只会写
`<dataBar showValue="0">`；`WorksheetWriter::add_conditional_icon_set()` 只写
worksheet-local basic built-in `3Arrows` `<cfRule type="iconSet">` XML，要求三枚有限且
严格递增的阈值。它们不写 `styles.xml`、`dxfs`、worksheet `.rels`、content type、
cell text 或 `<calcPr>`，不支持 formula/cellIs、advanced/custom icon sets、top/bottom、
duplicate/unique、advanced data bar negative color/axis/border/gradient/`extLst`、
dxf-backed styles、existing-file editing 或完整 Excel UI。

对 data validations 这类 worksheet metadata API，注释还要说明是否只写
worksheet XML、是否新增 relationships/content types/styles、是否复制 range 列表、
公式文本和 prompt/error 文本、multi-area `sqref` 是否排序/合并/去重/检查重叠、
是否按属性写出 `showInputMessage`、`showErrorMessage`、`showDropDown`、
`errorStyle`、`promptTitle`、`prompt`、`errorTitle`、`error`，是否省略空字符串和
false flag，是否解析公式或校验单元格值，以及是否支持 existing-file editing 或完整
Excel UI。当前 `hide_dropdown_arrow` 只对 list validation 有效，写出 OpenXML
反向命名的 `showDropDown="1"` 来隐藏 in-cell dropdown arrow；false 时省略。

对 hyperlinks 这类 worksheet metadata API，注释还要区分 external relationship-backed
链接和 internal location-only 链接。external API 要说明是否只支持 Streaming /
new-workbook 路径、是否复制 URL 文本、是否写 worksheet `<hyperlinks>` 和 worksheet
`.rels`、relationship id 是否只在 worksheet owner 内有效、是否新增 workbook
relationships 或 content type overrides、是否会写单元格文本或样式，以及是否校验 URL
可达性或支持 existing-file editing。internal API 要说明是否复制 location 文本、是否写
worksheet `<hyperlink location="...">`、是否不创建 `.rels` 或 `r:id`、是否校验目标
sheet/range/named range 是否存在，以及是否支持 existing-file editing。`HyperlinkOptions`
要说明非空 display/tooltip 会复制进 writer state 并写成 worksheet `<hyperlink>`
attributes，空字符串省略，且不会写单元格文本、创建 hyperlink 样式、生成 `styles.xml`
或改变 relationships/content types。

对 tables 这类跨 part worksheet metadata API，注释还要说明是否只支持
Streaming / new-workbook 路径、是否复制 table name / column names / style name /
`show_totals_row` / `column_totals_functions` / `column_totals_labels`、
是否生成 `xl/tables/tableN.xml`、worksheet `<tableParts>`、worksheet `.rels` 和
content type override、relationship id 是否只在 worksheet owner 内有效、是否读取
已写 header 行或推断列名、是否生成 `styles.xml`，以及是否仅支持 caller-supplied
totals row、列级 `totalsRowFunction` metadata 和列级 `totalsRowLabel` metadata。
注释还必须写清它只拒绝同一 worksheet 内 table-vs-table range overlap，不检查与
data validations、images、merged ranges 或 autoFilter 的冲突；它不计算 totals、
不生成公式文本、totals row 单元格文本、calculated columns、table resize、
existing-file editing 或完整 Excel table UI。

对 document properties 这类小型 workbook metadata API，注释还要说明它是否只支持
new-workbook 输出、是否只写 `docProps/core.xml` 和 `docProps/app.xml`、是否复制
字符串值、是否新增或保持 package relationships / content type overrides、是否创建
`docProps/custom.xml`，以及是否支持 created/modified timestamp、custom document
properties、existing-file editing 或未知 docProps part 保留。当前
`DocumentProperties`、`Workbook::set_document_properties()` 和
`WorkbookWriterOptions::document_properties` 只覆盖 core/app 小型 XML part；它们不进入
worksheet row/cell 热路径，也不代表完整 document properties API。内部
`PackageEditor` 另有 existing-package core/app docProps generated-small-XML
窄切片，可新增/替换 `docProps/core.xml` 和 `docProps/app.xml` 并同步 package
relationships / content types；它会保留 source content type defaults/overrides
形态，避免把默认类型媒体 part 无故提升为 override；docProps generated parts 会把
write-mode / dirty / generated / preserve-original 状态同步到内部 manifest，供 Patch
审计；当前内部回归还验证 core/app docProps helper 重写 package relationships /
content types 时会保留已有 `docProps/custom.xml`、custom-properties package
relationship、custom properties content type override 和 unknown bytes，但不编辑
custom properties；worksheet replacement 也会把 workbook metadata rewrite 同步为 `LocalDomRewrite`
供 Patch 审计；当前内部 `EditPlan` 还会记录 `[Content_Types].xml`、
package `_rels/.rels`、workbook `.rels` rewrite、removed calcChain owner `.rels`
omission，以及 preserved source-owned `.rels` 存在时的 copy-original package-entry 审计项
（包括 ordinary owner-part replacement 的根级 `_rels/foo.xml.rels`、
worksheet/drawing/sharedStrings 关系、preserved calcChain 关系，以及 workbook metadata
rewrite 时被原样保留的 workbook `.rels`）；
当前普通 `replace_part()` 会拒绝 `[Content_Types].xml`、package `_rels/.rels` 和
source-owned `.rels` metadata entry 作为 ordinary part replacement target，避免绕过
metadata-aware helper 和 package-entry audit；这不是完整 relationship/content-type mutator。
重复 ordinary part replacement 回归只证明同一 part 再次替换时最终 bytes、write mode、
edit-plan reason、manifest state 和 preserved source-owned `.rels` audit 以上一次替换为准。
docProps generated-small-XML 被后续 ordinary replacement 覆盖的回归只证明最终
part bytes、EditPlan 和 manifest 采用后续 ordinary replacement，content types /
package relationships 仍由 metadata helper 路径维护和审计。反向顺序回归只证明
后调用的 docProps metadata helper 会接管此前 ordinary replacement 或 explicit
removal 的 core/app part，并清理 stale removal / omitted payload 状态；它只恢复
helper 负责的 core/app payload、content types 和 package relationships，不恢复此前
removal 省略的 docProps owner `.rels`；当前结构回归验证输出包继续省略该 owner
`.rels`，并保留 removed package-entry audit。这不是事务式 undo。
worksheet replacement 删除 calcChain 时会压过此前 ordinary calcChain replacement；
worksheet replacement 也会接管此前 ordinary workbook replacement 以写入 helper-generated
fullCalcOnLoad metadata；若 worksheet rewrite 已请求 fullCalcOnLoad / calcChain
removal，后续 ordinary `replace_part("/xl/workbook.xml", ...)` 仍会保留该 calc policy，
把 workbook XML 中的 `fullCalcOnLoad` 规范为 `1`，并避免把已重写的 workbook
`.rels` package-entry audit 降级为 copy-original；
这仍只是 internal PackageEditor state consistency。

对 Streaming ZIP compression option，注释和文档必须说明它只属于
new-workbook package output：当前 public 入口是
`WorkbookWriterOptions::zip_compression_level`，`-1` 表示 backend default，`0`
表示 no-compression/stored output，`1..9` 表示 minizip/zlib-compatible DEFLATE
level。默认 stored-bootstrap 构建不能产生 DEFLATE 输出，因此 positive level 会在
rows 写入前拒绝；opt-in minizip 构建会在 package close 阶段按 level 调整 CPU 成本和
输出体积。`-1` 必须保持 backend default 语义，不能隐式改成 throughput-first
level 1；需要更快 close-time 的调用方应显式选择 level 1。该选项不改变 worksheet
row/cell streaming，不引入 DOM，不启用 Zip64，不保留或编辑 existing-file
compression method / timestamps / extra fields，也不是 public package writer/editor
API。benchmark 数据应记录 `compression_level` 和实际 backend；不要把某一个
level 写成所有数据形态的无条件默认最优。

当前组合回归覆盖同一内部 edit 里 docProps 生成与 worksheet
replacement 的 relationship/content-type 合并、calcChain removal、stale calcChain
owner `.rels` omission、workbook metadata rewrite、unknown entry preservation、
exact/path-equivalent source-overwrite rejection 和 empty-output / missing-parent / non-directory-parent / existing-directory output rejection；core/app package relationship target 冲突失败只覆盖
不污染 edit plan entries / notes、manifest / package-entry audit / copied output。该路径
还覆盖缺失 `xl/calcChain.xml` payload 时的 stale calcChain metadata cleanup：
只移除残留 content type override 或 workbook calcChain relationship，不创建 payload，
不伪造 removed-part audit，也不能写成完整 metadata repair。
完整 worksheet replacement payload 现在还会对 shared string indexes、style id
references、公式 cell、sheetPr、dimension、sheetViews、sheetFormatPr、cols、
sheetProtection、protectedRanges、autoFilter、mergeCells、dataValidations、
conditionalFormatting、ignoredErrors、printOptions、pageMargins、pageSetup、extLst 等 range/reference
metadata，以及 hyperlinks、drawing、legacyDrawing、tableParts 等 relationship-bearing
metadata 写 audit-only notes；这些 notes 会进入内部 `EditPlan` / planned output，
只提示 caller 复核 sharedStrings、styles、calc metadata、range/reference metadata、
worksheet `.rels` 和 linked parts，不能写成 sharedStrings/styles 迁移、range 修复、
dimension 重算、sheetViews 修复、relationship repair、calcChain rebuild 或 public worksheet editor。
当前还覆盖 internal `PackageEditor::replace_worksheet_sheet_data()` helper：它只替换
已有 worksheet XML 的 `<sheetData>` 元素或 `<sheetData/>`，保留同一 worksheet
part 的外围 XML metadata，并复用 worksheet replacement 的 calcChain /
fullCalcOnLoad 与 preservation 副作用；成功后会用内部 `EditPlan` notes 审计保留的
worksheet-local metadata ranges/references，当前覆盖 sheetPr、dimension、sheetViews、
sheetFormatPr、cols、sheetProtection、protectedRanges、autoFilter、mergeCells、
dataValidations、conditionalFormatting、hyperlinks、ignoredErrors、printOptions、
pageMargins、pageSetup、drawing、legacyDrawing、tableParts 和 extLst。文档只能把它写成 Patch MVP /
template-fill 小切片；当前结构测试还验证输出包中保留的 worksheet `.rels`
legacyDrawing `rId7` 到 `../drawings/vmlDrawing1.vml#shape1` 可由
`PackageReader` / `RelationshipGraph` 重读，这仍不是 VML/drawing 编辑。
worksheet-owned background picture / header-footer VML same-path output-plan
coverage 只能写成内部 `planned_output()` 暴露 background picture
remove-then-replace 的 active picture `LocalDomRewrite`、content types metadata
copy-original、sibling header/footer VML preservation、无 stale removals、
无 relationship target audits、无 fullCalcOnLoad、`CalcChainAction::Preserve`
和 no invented picture owner `.rels`；以及 header/footer VML replace-then-remove
的 omitted VML part、removed-part inbound audit、content types metadata rewrite、
sibling background-picture preservation、无 relationship target audits、无
fullCalcOnLoad、`CalcChainAction::Preserve` 和 no invented VML owner `.rels`。
不能写成 public output planner、图片/VML/header-footer public API、语义合并或删除、
relationship repair/pruning、orphan cleanup、content type repair 或完整 object
lifecycle 支持。
当前组合回归还验证先排队 worksheet replacement 后再执行 sheetData patch 时，
helper 基于当前 planned worksheet bytes 替换，覆盖 queued worksheet 中普通
`<sheetData>` 和 self-closing `<sheetData/>` 两种形态，保留 queued wrapper
metadata，不会把 source-only worksheet metadata 复活。
当前还覆盖源 worksheet 使用 self-closing `<sheetData/>` 的成功替换回归：
输出改为普通 `<sheetData>...</sheetData>`，保留 dimension / autoFilter，沿默认
calcChain remove / fullCalcOnLoad 路径清理 stale 计算 metadata，并保留 unknown bytes。
当前还覆盖 replacement payload 自身为 self-closing `<sheetData/>` 的成功替换回归：
可清空旧 row/cell，输出保留 `<sheetData/>` 和外围 dimension / autoFilter，
并继续沿默认 calcChain remove / fullCalcOnLoad 与 unknown bytes preservation 路径。
当前还覆盖 source worksheet 和 replacement payload 使用 `<x:worksheet>` /
`<x:sheetData>` 前缀形式时的成功替换回归：按 local-name 匹配，输出保留原
wrapper / replacement 字面前缀，仍沿默认 calcChain cleanup 与 unknown bytes
preservation 路径；不能写成通用 namespace repair。
replacement `<sheetData>` 自身若使用 shared string indexes、
style id references 或公式 cell，也只追加 audit notes，提示 caller 复核
`xl/sharedStrings.xml`、`xl/styles.xml`、workbook calc metadata 和 calcChain policy；
它不迁移 sharedStrings 索引、不合并 styles、不计算公式，也不重建 calcChain。不能写成 public API、随机 cell 编辑、dataValidations/conditionalFormatting/
hyperlinks/table/drawing 语义同步、sharedStrings/styles 迁移、range 修复或大文件低内存 transformer。
当前 by-name worksheet replacement 和 by-name `sheetData` helper 会通过当前 planned
workbook sheet catalog 定位已有 worksheet part；如果同一 edit 中已有 ordinary
`/xl/workbook.xml` replacement，或该 replacement 已被 `request_full_calculation()` /
worksheet rewrite 的 fullCalcOnLoad helper 接管，旧 source sheet name 会在状态变更前
失败，新 planned sheet name 可通过源 workbook `.rels` 定位既有 worksheet part。这仍只是
窄 planned workbook catalog resolver；它要求 planned sheet id attribute 绑定到
officeDocument relationships namespace，接受非 `r` 前缀，错误 namespace 的 `x:id`
和普通 `id` 会被当作缺失且不污染状态；不是 sheet rename/delete、relationship repair
或 public API。当前内部 `PackageEditor::rename_sheet_catalog_entry()` 只重写当前 planned
`/xl/workbook.xml` 的直接 `<sheets><sheet name="...">` attribute，保留 worksheet
parts、workbook `.rels`、content types、calcChain 和 unknown entries，并记录
definedNames、公式、tables、drawings、charts、hyperlinks、relationship targets、
sharedStrings、styles 和 calcChain 未同步的 audit note；默认不能写成完整 sheet
rename/add/delete。当前 public `WorkbookEditor::rename_sheet(old, new,
WorkbookEditorRenameOptions)` 增加了显式
`WorkbookEditorRenameFormulaPolicy::RewriteDefinedNames` opt-in，只改 direct workbook
definedName formula text 的本地 sheet qualifier；更窄的
`WorkbookEditorRenameFormulaPolicy::RewriteDefinedNamesAndMaterializedWorksheetFormulas`
还会同步已经 materialized 的 WorksheetEditor formula cells。两者仍不是公式引擎、
非 materialized worksheet formula rewrite、relationship repair、calcChain rebuild 或完整
semantic sheet rename。
默认 `WorkbookEditor::rename_sheet()` 仍是 catalog-only；case-varied local formula
qualifiers 只通过 `formula_reference_audits()`、`source_formula_reference_audits()` 和
`defined_name_formula_reference_audits()` 暴露 stale source-name risks，diagnostics
保留原始 `data!` / `DATA!` 拼写。这三个 audit API 都是 read-only diagnostic：
不 increment `pending_change_count()`，不 queue replacement，不 dirty/create
materialized sessions，不改 pending edit summaries，也不更新 `last_edit_error()`。
`source_formula_reference_audits()` 是 read-only source scan，不 materialize 或 rewrite
非 materialized worksheet XML；显式 rewrite policy 继续保持窄 local
sheet-qualified reference 边界。
planned workbook XML 路径的内部 `planned_output()`
只能写成暴露最终 workbook `LocalDomRewrite`、preserved content types /
workbook `.rels` / worksheet / calcChain / unknown entry，以及 structured sheet
catalog / definedNames audit；不能写成完整 sheet rename API 或完整 definedNames
语义同步。它当前会拒绝精确或 ASCII case-insensitive
new-name duplicates，但这仍不是完整 Excel sheet-name 国际化规范。
invalid/malformed replacement XML、source worksheet 缺失 `sheetData`，以及 source worksheet
`<sheetData>` 起始标签存在但闭合标签损坏/缺失时，只能写成失败不污染
EditPlan、manifest、package-entry audit、calc policy 或 copied output bytes；不能写成
XML repair。

P4.1 冻结的第一个 Patch MVP 是 internal by-name worksheet `<sheetData>` patch：
当前内部 by-name chunk-source helper 接受 caller 提供的
`<sheetData>` / `<sheetData/>` chunk source，做 bounded local rewrite，并沿用现有
calcChain remove / `fullCalcOnLoad`、relationship/content-type audit 和
unknown/unmodified part preservation 路径。这个 helper 已有首个 public facade：
`WorkbookEditor`（`include/fastxlsx/workbook_editor.hpp` / `src/workbook_editor.cpp` /
`tests/test_workbook_editor.cpp`，CTest family `fastxlsx.workbook_editor.*`）。public facade 把
caller 的 `CellValue` 行投影为 standalone `<sheetData>` 后委托上述 by-name helper，
只暴露 `open()` / `worksheet_names()` / `has_worksheet()` /
`source_worksheet_names()` / `has_source_worksheet()` / `replace_sheet_data()` /
`replace_cells()` / `CellPatchMissingCellPolicy` / `rename_sheet()` / `save_as()`。
`replace_cells()` 复用 internal worksheet transformer 的
targeted-cell replacement/upsert 路径：默认只替换 source/planned worksheet stream 中已经
存在的 `<c>` elements；显式 `CellPatchMissingCellPolicy::Insert` 可插入缺失 cells 或合成
minimal rows，并把 rewritten worksheet staged 为 file-backed chunks。
内部 `PackageEditor` current-worksheet-input diagnostics 会继续保留 concrete source
entry / planned staged-chunk context；这只是 facade 失败诊断透传和状态卫生，不新增
public diagnostics API。
这只兑现 whole-`<sheetData>` 替换、targeted cell replacement/upsert 和窄 sheet
catalog 改名；不要把它写成 public `PackageEditor`、row/column insertion、任意
random cell editing、sharedStrings/style id migration、style merge、relationship
repair/pruning、table/drawing semantic sync、range 修复或完整大文件 worksheet 编辑器。
no-op `PackageEditor::save_as()` roundtrip coverage 只能描述为 linked-object fixture
中全部源 entries 的 entry order、stored entry method / CRC / uncompressed size 和
bytes copy baseline，以及初始 copy-original plan 没有 metadata package-entry side
effect；不能写成 broad unknown part preservation 或 public editing API readiness。
ordinary single-part replacement coverage 只能描述为目标 entry 原位重写、其它源
entries 保持 entry order、stored entry method / CRC / uncompressed size 和 bytes；
不能写成 complete package rewrite 或 broad safe editing。
linked-object fixture 上的 ordinary workbook replacement coverage 只能描述为只重写
`xl/workbook.xml`、workbook `.rels` copy-original audit、以及其它 linked/unknown
source entries 保持 copy-original baseline；不能写成 workbook metadata sync、
defined-name policy 或 object editing。
linked-object fixture 上的 ordinary drawing replacement coverage 只能描述为只重写
`xl/drawings/drawing1.xml`、drawing `.rels` copy-original audit、以及 chart/media/
unknown source entries 保持 copy-original baseline；不能写成 drawing mutation、
image editing、chart editing 或 full object preservation。
linked-object fixture 上的 ordinary unknown extension replacement coverage 只能描述为
只重写 `custom/opaque-extension.bin`、其 owner `.rels` copy-original audit 和原样
保留、以及 workbook/worksheet/drawing/chart/media source entries 保持 copy-original
baseline；不能写成 unknown extension 语义编辑、custom relationship repair、
metadata editor 或 public editing API。
linked-object fixture 上同一 unknown extension 的 repeated ordinary replacement
coverage 只能描述为最终 bytes、manifest write-mode、edit-plan reason 和 owner
`.rels` audit upsert 到最后一次替换状态，owner `.rels` 继续 copy-original，且没有
removed-part / removed package-entry audit；不能写成 transactional editing、
unknown extension semantic merging 或 metadata repair。
linked-object fixture 上的 unknown extension remove-then-ordinary-replace coverage
只能描述为后续 replacement 恢复 active unknown extension part、清理 stale
removed-part audit 和 stale removed owner `.rels` audit、恢复 owner `.rels`
copy-original package-entry audit、保留 worksheet `.rels` 中的 inbound unknown
relationship、保留其它 linked/source entries，且不重写 `[Content_Types].xml`；
不能写成 unknown extension semantic merge、custom relationship repair、metadata
repair、transactional undo 或 public editing API。
linked-object fixture 上的 unknown extension ordinary-replace-then-remove coverage
只能描述为后续 removal 清理 active replacement、记录 removed-part 和 removed owner
`.rels` audit、输出省略 unknown extension part 及其 owner `.rels`、保留
worksheet `.rels` 中指向缺失 part 的 inbound unknown relationship、保留其它
linked/source entries 和默认 `bin` content type，且不重写 `[Content_Types].xml`；
不能写成 unknown extension deletion semantics、custom relationship repair、metadata
repair、relationship pruning/repair、content type repair、orphan cleanup、
transactional undo 或 public editing API。
unknown extension ordinary-replace-then-remove output-plan coverage 只能描述为内部
`planned_output()` 暴露 omitted `custom/opaque-extension.bin` part、omitted source-owned
`custom/_rels/opaque-extension.bin.rels` metadata、worksheet-owned inbound relationship
metadata、preserved `[Content_Types].xml` 和 package `_rels/.rels` copy-original
快照；不能写成 public output planner、unknown extension deletion semantics、
custom relationship repair、metadata repair、relationship pruning/repair、content type
repair、orphan cleanup、transactional undo 或 public editing API。
内部 `PackageEditor::remove_part()` coverage 只能描述为显式 registered-part removal
窄切片：只接受源 package 中已有的普通 part，输出省略目标 part 和存在时的
source-owned owner `.rels`，记录 removed-part / removed package-entry audit，并在目标
存在 content type override 时重写 `[Content_Types].xml`；不能写成 inbound relationship
pruning、object deletion、通用删除 API、transactional editing 或 public editing API。
removed-part audit 现在会同时保留结构化 inbound relationship metadata（owner
entry、owner part、id、type、raw target、normalized target part）和可读 reason，
记录仍指向被删除 part 的 inbound package/source relationship 上下文，只能写成
Patch traceability，不是修复。
若 removed-part inbound 扫描遇到 malformed percent relationship target，只能写成
EditPlan audit note 和 byte-preserved `.rels` 证据；不能写成 target repair、
relationship validation 或自动校正。
workbook removal coverage 只能描述为显式移除 `xl/workbook.xml`：输出省略
workbook part 和 source-owned workbook `.rels`、移除 workbook content type
override、保留 package `_rels/.rels` inbound officeDocument relationship、且不修剪
worksheet/drawing/table/sharedStrings/styles/VBA/calcChain 或 unknown extension 等
downstream/source parts；不能写成 workbook deletion、sheet catalog sync、
relationship repair、full workbook editing 或 public workbook editing API。
PackageReader workbook sheet catalog resolver 只能描述为从 package root 解析
package `_rels/.rels` 的 internal `officeDocument` target，当前窄实现只接受
解析到 `/xl/workbook.xml` 的相对或绝对 package target，且不会把 package root
建模成真实 `PartName`；不能写成任意 workbook part-location 支持、sheet catalog
mutation 或 public workbook model。
workbook replace-then-remove ordering coverage 只能描述为后续 explicit removal 清理
active workbook replacement、记录 removed-part 和 workbook owner `.rels` omission audit、
输出省略 workbook part 及 owner `.rels`、移除 workbook content type override、保留
package `_rels/.rels` 中指向缺失 workbook 的 officeDocument relationship 以及
worksheet/drawing/table/sharedStrings/styles/VBA/calcChain/unknown downstream parts；不能写成
workbook deletion semantics、sheet catalog sync、relationship/content type repair、
orphan cleanup、transactional undo 或 public API。
worksheet removal coverage 只能描述为显式移除 `xl/worksheets/sheet1.xml`：
输出省略 worksheet part 和 source-owned worksheet `.rels`、移除 worksheet content
type override、保留 workbook `.rels` inbound worksheet relationship、且不修剪
drawing/table/sharedStrings/styles/VBA/calcChain 或 unknown extension 等
downstream/source parts；不能写成 sheet delete、workbook sheet catalog sync、
relationship repair、full worksheet editing 或 public sheet editing API。
worksheet replace-then-remove ordering coverage 只能写成后续 explicit removal 清理
active worksheet replacement、记录 removed-part 和 worksheet owner `.rels`
omission audit、输出省略 worksheet part 及其 owner `.rels`、移除 worksheet
content type override、保留 workbook `.rels` 中指向缺失 worksheet 的 inbound
worksheet relationship，以及 drawing/chart/media/table/sharedStrings/styles/VBA/
calcChain/unknown downstream/source parts；不能写成 sheet delete、workbook sheet
catalog sync、relationship/content type repair、orphan cleanup、transactional undo
或 public API。
drawing removal coverage 只能描述为显式移除 `xl/drawings/drawing1.xml`：
输出省略 drawing part 和 source-owned drawing `.rels`、移除 drawing content type
override、保留 worksheet `.rels` direct / URI-qualified inbound drawing
relationships、且不修剪 chart/media 或其它 downstream parts；不能写成 drawing
mutation、object deletion、relationship repair、full drawing support 或 public
drawing editing API。
drawing replace-then-remove ordering coverage 只能写成后续 explicit removal 清理
active drawing replacement、记录 removed-part 和 drawing owner `.rels` omission
audit、输出省略 drawing part 及其 owner `.rels`、移除 drawing content type
override、保留 worksheet `.rels` 中 direct / URI-qualified inbound drawing
relationships，以及 chart/media/table/VML/percent-decoded drawing/sharedStrings/
styles/VBA/calcChain/unknown downstream/source parts；不能写成 drawing mutation、
object deletion、relationship/content type repair、orphan cleanup、transactional
undo 或 public API。
drawing replace-then-remove output-plan coverage 只能写成内部 `planned_output()`
暴露 omitted drawing part、omitted drawing owner `.rels`、content types rewrite 和
preserved inbound worksheet relationship audit；不能写成 public output planner、
relationship/content type mutator、relationship pruning/repair 或 drawing editing API。
VML drawing replace-then-remove output-plan coverage 只能写成内部 `planned_output()`
暴露 omitted `xl/drawings/vmlDrawing1.vml` part、removed_parts
target/reason/inbound audit、URI-qualified worksheet inbound relationship
metadata、content types rewrite、empty removed_package_entries，以及没有凭空创建
`xl/drawings/_rels/vmlDrawing1.vml.rels`；不能写成 public output planner、
VML shape editing、legacy drawing mutation、relationship repair、orphan cleanup
或 complete VML/drawing support。
VML drawing remove-then-replace output-plan coverage 只能写成内部 `planned_output()`
暴露 active `xl/drawings/vmlDrawing1.vml` `LocalDomRewrite` entry、content types
copy-original audit、preserved package/workbook/worksheet/drawing relationships、
preserved linked/unknown entries、empty removed_parts / removed_package_entries，
以及没有凭空创建
`xl/drawings/_rels/vmlDrawing1.vml.rels`；不能写成 public output planner、
VML shape editing、legacy drawing mutation、transactional undo、relationship repair、
content type repair、full VML/drawing support 或 public drawing editing API。
percent-decoded drawing replace-then-remove output-plan coverage 只能写成内部
`planned_output()` 暴露 omitted `xl/drawings/drawing space.xml` part、removed_parts
target/reason/inbound audit、encoded worksheet inbound relationship metadata、
content types rewrite、empty removed_package_entries，以及没有凭空创建
`xl/drawings/_rels/drawing space.xml.rels`；不能写成 public output planner、
percent-encoded target rewrite、relationship repair、orphan cleanup 或 drawing
editing API。
percent-decoded drawing remove-then-replace output-plan coverage 只能写成内部
`planned_output()` 暴露 active `xl/drawings/drawing space.xml` `LocalDomRewrite`
entry、content types copy-original audit、preserved package/workbook/worksheet/
drawing relationships、preserved linked/unknown entries，以及没有凭空创建
`xl/drawings/_rels/drawing space.xml.rels`；不能写成 public output planner、
percent-encoded target repair、relationship rewrite、relationship repair、content
type repair、transactional undo、full drawing support 或 public drawing editing API。
remove coverage 还只能描述为后调用的 `remove_part()` 压过此前 ordinary replacement、
清理 stale replacement state，并以 removed-part audit / content type cleanup 为最终状态；
invalid removal failure 只能描述为 edit-plan entries/notes、package-entry audit、
removed audit、calc policy、manifest write modes 和 copied output bytes 不污染。
反向顺序 coverage 只能描述为对源 package 中已有的普通 part，后调用的 ordinary
`replace_part()` 可恢复此前 `remove_part()` 的目标为 active replacement，
清理 stale removed-part / removed owner `.rels` audit 与 omitted entry 状态，
并把存在的 source-owned `.rels` 重新记录为 copy-original audit；对带 content type
override 的 part，只能写成恢复后 `[Content_Types].xml` 回到 source bytes /
copy-original audit。
`unknown extension` ordinary-replace-then-remove coverage 只能写成后续 removal
清理 active replacement、记录 removed-part 和 removed owner `.rels` audit、
输出省略 unknown extension part 及其 owner `.rels`、保留 worksheet `.rels`
中指向缺失 part 的 inbound relationship、保留其它 linked/source entries 和默认
`bin` content type，且不重写 `[Content_Types].xml`；不能写成 unknown extension
deletion semantics、custom relationship repair、metadata repair、relationship
pruning/repair、content type repair、orphan cleanup、transactional undo 或 public
editing API。workbook-specific 反向顺序 coverage 只能写成显式移除
`xl/workbook.xml` 后再 ordinary `replace_part()` 恢复 workbook active replacement、
source-owned workbook `.rels` copy-original audit、package `_rels/.rels` inbound
officeDocument relationship 保留，以及 `[Content_Types].xml` 回到 source bytes /
copy-original audit；worksheet-specific 反向顺序 coverage 只能写成显式移除
`xl/worksheets/sheet1.xml` 后再 ordinary `replace_part()` 恢复 worksheet active
replacement、source-owned worksheet `.rels` copy-original audit、workbook `.rels`
inbound worksheet relationship 保留，以及 `[Content_Types].xml` 回到 source bytes /
copy-original audit；drawing-specific 反向顺序 coverage 只能写成显式移除
`xl/drawings/drawing1.xml` 后再 ordinary `replace_part()` 恢复 drawing active
replacement、source-owned drawing `.rels` copy-original audit、worksheet `.rels`
direct / URI-qualified inbound drawing relationships 保留，以及 `[Content_Types].xml`
回到 source bytes / copy-original audit。不能写成 drawing mutation、object deletion、
transactional undo、relationship repair、
content type repair、semantic merge 或 public editing API。sharedStrings-specific
反向顺序 coverage 只能写成显式移除 `xl/sharedStrings.xml` 后再 ordinary
`replace_part()` 恢复 sharedStrings active replacement、source-owned
sharedStrings `.rels` copy-original audit、workbook `.rels` inbound sharedStrings
relationship 保留，以及 `[Content_Types].xml` 回到 source bytes / copy-original
audit；不能写成 sharedStrings index migration、string-table rebuild、worksheet
cell-reference sync、transactional undo、relationship repair、content type repair、
semantic merge 或 public editing API。styles-specific 反向顺序 coverage 只能写成
显式移除 `xl/styles.xml` 后再 ordinary `replace_part()` 恢复 styles active
replacement、workbook `.rels` inbound styles relationship 保留、不凭空创建
styles owner `.rels`，以及 `[Content_Types].xml` 回到 source bytes /
copy-original audit；不能写成 style id migration、style merge、cell `s`
reference sync、transactional undo、relationship repair、content type repair、
semantic merge、existing-file style preservation 或 public editing API。
linked-object fixture 上的 ordinary media replacement coverage 只能描述为只重写
`xl/media/image1.png`、drawing `.rels` 原样保留、PNG default content type 不提升为
override、以及 workbook/worksheet/drawing/chart/unknown source entries 保持
copy-original baseline；不能写成 image decoding、drawing mutation、
existing-workbook image editing 或 full image preservation。
linked-object fixture 上的 explicit media removal coverage 只能描述为显式移除
default-typed `xl/media/image1.png`：输出省略 media entry、保留 PNG default
content type 和 drawing `.rels` inbound image relationship、不凭空创建 media
owner `.rels` omission；不能写成 relationship repair、object deletion、
existing-workbook image editing 或 full image preservation。
linked-object fixture 上的 media remove-then-ordinary-replace coverage 只能描述为
后续 replacement 恢复 active media part、清理 stale removed-part audit、保留 PNG
default content type 且不把 `xl/media/image1.png` 提升成 override、保留 inbound
drawing `.rels`、不凭空创建 media owner `.rels`；不能写成 transactional undo、
image semantic merge、relationship repair、content type repair 或 full image
preservation。
linked-object fixture 上的 media ordinary-replace-then-remove coverage 只能描述为
后续 removal 清理 active media replacement、记录 removed-part audit 和 inbound
drawing relationship metadata、输出省略 `xl/media/image1.png`、保留 PNG default
content type 且不把 media 提升成 override、保留 inbound drawing `.rels`、不凭空创建
media owner `.rels`；不能写成 transactional undo、image semantic merge、
relationship pruning/repair、content type repair、existing-workbook image editing 或
full image preservation。
media ordinary-replace-then-remove output-plan coverage 只能描述为内部
`planned_output()` 暴露 omitted default-typed media part、drawing-owned inbound
relationship audit、removed_parts target/reason/inbound audit、content types /
drawing `.rels` copy-original、empty removed_package_entries，以及没有 media
owner `.rels` 条目；不能写成 public output planner、relationship/content type
mutator、relationship pruning/repair 或 image editing API。
linked-object fixture 上的 explicit table removal coverage 只能描述为显式移除
`xl/tables/table1.xml`：输出省略 table entry、移除 table content type override、
保留 worksheet `.rels` inbound table relationship、不凭空创建 table owner
`.rels` omission；不能写成 relationship repair、object deletion、table resize、
existing-workbook table editing 或 full table support。
linked-object fixture 上的 explicit sharedStrings removal coverage 只能描述为显式移除
`xl/sharedStrings.xml`：输出省略 sharedStrings part 和 sharedStrings owner `.rels`、
移除 sharedStrings content type override、保留 workbook `.rels` inbound
sharedStrings relationship；不能写成 sharedStrings index migration、string-table
rebuild、worksheet cell-reference sync、relationship repair、existing-workbook
sharedStrings semantic editing 或 public sharedStrings editing API。
linked-object fixture 上的 explicit styles removal coverage 只能描述为显式移除
`xl/styles.xml`：输出省略 styles part、移除 styles content type override、保留
workbook `.rels` inbound styles relationship、不凭空创建 styles owner `.rels`
omission；不能写成 style id migration、style merge、cell `s` reference sync、
relationship repair、existing-file style preservation、full style editing 或 public
styles editing API。
linked-object fixture 上的 explicit VBA project removal coverage 只能描述为显式移除
`xl/vbaProject.bin`：输出省略 VBA project part、移除 VBA content type override、
保留 workbook `.rels` inbound VBA relationship、不凭空创建 VBA owner `.rels`
omission；不能写成 macro generation、VBA semantic editing、signature preservation、
relationship repair、complete macro support 或 public VBA editing API。
linked-object fixture 上的 explicit VML drawing removal coverage 只能描述为显式移除
`xl/drawings/vmlDrawing1.vml`：输出省略 VML drawing part、移除 VML content type
override、保留 worksheet `.rels` URI-qualified inbound `vmlDrawing` relationship、
不凭空创建 VML owner `.rels` omission；不能写成 VML shape editing、legacy
drawing mutation、relationship repair、full VML/drawing support 或 public drawing
editing API。
linked-object fixture 上的 explicit percent-decoded drawing removal coverage 只能描述为
显式移除 `xl/drawings/drawing space.xml`：输出省略目标 drawing part、移除 drawing
content type override、保留 worksheet `.rels` 原始
`../drawings/drawing%20space.xml` inbound relationship、不凭空创建
`xl/drawings/_rels/drawing space.xml.rels`；不能写成 percent-encoded target repair、
relationship rewrite、drawing mutation、full drawing support 或 public drawing
editing API。
linked-object fixture 上的 ordinary table replacement coverage 只能描述为只重写
`xl/tables/table1.xml`、worksheet `.rels` 原样保留、table content type override
仍可读、以及 workbook/worksheet/drawing/chart/media/unknown source entries 保持
copy-original baseline；不能写成 table resize、calculated columns、totals generation、
existing-workbook table editing 或 full table support。
linked-object fixture 上的 table remove-then-ordinary-replace coverage 只能描述为
后续 replacement 恢复 active table part、清理 stale removed-part audit、让
`[Content_Types].xml` 回到 source/copy-original audit、保留 worksheet `.rels`
inbound table relationship、不凭空创建 table owner `.rels`；不能写成 table resize、
calculated columns、totals generation、transactional undo、relationship repair、
content type repair、existing-workbook table editing 或 full table support。
linked-object fixture 上的 table ordinary-replace-then-remove coverage 只能描述为
后续 explicit removal 清理 active table replacement、记录 removed-part audit 和
inbound worksheet relationship metadata、输出省略 table part、移除 table content
type override、保留 worksheet `.rels` inbound table relationship、不凭空创建 table
owner `.rels`；不能写成 table delete semantics、table resize、calculated columns、
totals generation、transactional undo、relationship pruning/repair、content type
repair、existing-workbook table editing 或 full table support。
table ordinary-replace-then-remove output-plan coverage 只能描述为内部
`planned_output()` 暴露 omitted table part、worksheet-owned inbound relationship
audit、content types local-DOM rewrite，以及没有 table owner `.rels` 条目；不能写成
public output planner、relationship/content type mutator、table delete semantics、
relationship pruning/repair 或 table editing API。
linked-object fixture 上的 ordinary sharedStrings replacement coverage 只能描述为只重写
`xl/sharedStrings.xml`、workbook `.rels` 原样保留、sharedStrings owner `.rels`
原样保留、sharedStrings content type override 仍可读、以及 styles/table/media/VBA/
unknown source entries 保持 copy-original baseline；不能写成 sharedStrings index
migration、string-table rebuild、worksheet cell-reference sync、existing-workbook
sharedStrings semantic editing 或 public sharedStrings editing API。
linked-object fixture 上的 sharedStrings ordinary-replace-then-remove coverage 只能描述为
后续 removal 清理 active sharedStrings replacement、记录 removed-part audit、输出省略
`xl/sharedStrings.xml` 及其 source-owned owner `.rels`、移除 sharedStrings content
type override、保留 workbook `.rels` inbound sharedStrings relationship；不能写成
sharedStrings index migration、string-table rebuild、worksheet cell-reference sync、
transactional undo、relationship pruning/repair、content type repair、existing-workbook
sharedStrings semantic editing 或 public sharedStrings editing API。
sharedStrings ordinary-replace-then-remove output-plan coverage 只能描述为内部
`planned_output()` 暴露 final omitted `xl/sharedStrings.xml` part、source-owned
owner `.rels` omission、removed_parts target/reason/inbound audit、
removed_package_entries owner-omission audit、workbook inbound relationship
metadata 和 content types rewrite；不能写成 metadata repair、relationship pruning、transactional undo 或
public sharedStrings editing API。
sharedStrings remove-then-ordinary-replace output-plan coverage 只能描述为内部
`planned_output()` 暴露 active `xl/sharedStrings.xml` stream-rewrite、source-owned
owner `.rels` copy-original audit、content types copy-original audit、preserved
workbook relationships、empty removed_parts / removed_package_entries 和 untouched
linked entries；不能写成 sharedStrings index
migration、string-table rebuild、worksheet cell-reference sync、relationship repair、
transactional undo 或 public sharedStrings editing API。
linked-object fixture 上的 ordinary styles replacement coverage 只能描述为只重写
`xl/styles.xml`、workbook `.rels` 原样保留、styles content type override 仍可读、
不凭空创建 `xl/_rels/styles.xml.rels`、以及 sharedStrings/table/media/VBA/unknown
source entries 保持 copy-original baseline；不能写成 style id migration、style
merge、cell `s` reference sync、existing-file style preservation、full style
editing 或 public style editing API。
linked-object fixture 上的 styles ordinary-replace-then-remove coverage 只能描述为
后续 removal 清理 active styles replacement、记录 removed-part audit、输出省略
`xl/styles.xml`、移除 styles content type override、保留 workbook `.rels` inbound
styles relationship，且不凭空创建 `xl/_rels/styles.xml.rels`；不能写成 style id
migration、style merge、cell `s` reference sync、existing-file style preservation、
transactional undo、relationship pruning/repair、content type repair、full style
editing 或 public style editing API。
styles replace-then-remove output-plan coverage 只能描述为内部 `planned_output()`
暴露 omitted `xl/styles.xml` part、removed_parts target/reason/inbound audit、
workbook-owned inbound relationship metadata、content types rewrite、empty
removed_package_entries，以及没有凭空创建 `xl/_rels/styles.xml.rels`；不能写成
public output planner、style id migration、style merge、cell `s` reference sync、
relationship repair、existing-file style preservation 或 complete style editing
support。
styles remove-then-ordinary-replace output-plan coverage 只能描述为内部
`planned_output()` 暴露 active `xl/styles.xml` local-DOM rewrite、content types
copy-original audit、preserved workbook relationships、empty removed_parts /
removed_package_entries、untouched linked entries，
且不凭空创建 `xl/_rels/styles.xml.rels`；不能写成 public output planner、style id
migration、style merge、cell `s` reference sync、relationship repair、
existing-file style preservation 或 complete style editing support。
linked-object fixture 上的 chart remove-then-ordinary-replace coverage 只能描述为
后续 replacement 恢复 active chart part、清理 stale removed-part audit、让
`[Content_Types].xml` 回到 source/copy-original audit、保留 drawing `.rels` 里的
direct / URI-qualified inbound chart relationships、保留其它 linked/unknown source
entries，且不凭空创建 chart owner `.rels`；不能写成 chart semantic merge、
chart reference repair、relationship repair、content type repair、transactional undo、
existing-workbook chart editing 或 public chart editing API。
chart remove-then-replace output-plan coverage 只能描述为内部 `planned_output()`
暴露 active `xl/charts/chart1.xml` stream-rewrite、content types copy-original
audit、preserved drawing relationships、preserved linked/unknown entries、empty
removed_parts / removed_package_entries，以及没有凭空创建
`xl/charts/_rels/chart1.xml.rels`；不能写成 public output planner、
chart semantic merge、chart reference repair、relationship repair、transactional undo
或 complete chart editing support。
linked-object fixture 上的 ordinary chart replacement coverage 只能描述为只重写
`xl/charts/chart1.xml`、drawing `.rels` 里的 chart 和 URI-qualified chart
relationships 原样保留、chart content type override 仍可读、不凭空创建 chart
owner `.rels`、以及 media/table/sharedStrings/styles/VBA/unknown source entries 保持
copy-original baseline；不能写成 chart reference migration、series/cache update、
drawing mutation、existing-workbook chart editing、full chart support 或 public chart
editing API。
linked-object fixture 上的 chart 先 ordinary replacement 再显式移除 coverage 只能描述为
后续 removal 清理 active chart replacement、记录 removed-part audit 和 direct /
URI-qualified inbound drawing relationship metadata、输出省略 `xl/charts/chart1.xml`、
移除 chart content type override、保留 inbound drawing `.rels` 和其它 linked/unknown
source entries，且不凭空创建 chart owner `.rels`；不能写成 chart delete semantics、
chart reference repair、relationship pruning/repair、content type repair、transactional
undo、semantic merge、existing-workbook chart editing 或 public chart editing API。
chart replace-then-remove output-plan coverage 只能描述为内部 `planned_output()`
暴露 omitted `xl/charts/chart1.xml` part、removed_parts target/reason/inbound audit、
drawing-owned direct / URI-qualified inbound relationship metadata、content types
rewrite、empty removed_package_entries，以及没有凭空创建
`xl/charts/_rels/chart1.xml.rels`；不能写成 public output planner、chart delete
semantics、chart reference repair、relationship repair、orphan cleanup 或 complete
chart editing support。
linked-object fixture 上的 ordinary VBA project replacement coverage 只能描述为只重写
`xl/vbaProject.bin`、workbook `.rels` 原样保留、VBA content type override 仍可读、
不凭空创建 `xl/_rels/vbaProject.bin.rels`、以及 worksheet/drawing/chart/media/table/
sharedStrings/styles/calcChain/unknown source entries 保持 copy-original baseline；
不能写成 macro generation、VBA semantic editing、signature preservation、
workbook relationship repair、full macro support 或 public macro editing API。
linked-object fixture 上的 VBA project remove-then-ordinary-replace coverage 只能描述为
后续 replacement 恢复 active VBA project part、清理 stale removed-part audit、让
`[Content_Types].xml` 回到 source/copy-original audit、保留 workbook `.rels` inbound
VBA relationship、且不凭空创建 `xl/_rels/vbaProject.bin.rels`；不能写成 macro
generation、VBA semantic editing、signature preservation、transactional undo、
workbook relationship repair、content type repair、full macro support 或 public macro
editing API。
VBA project remove-then-replace output-plan coverage 只能描述为内部 `planned_output()`
暴露 active `xl/vbaProject.bin` stream-rewrite、content types copy-original audit、
preserved package/workbook relationships、preserved linked/unknown entries、empty
removed_parts / removed_package_entries，以及没有凭空创建
`xl/_rels/vbaProject.bin.rels`；不能写成 public output planner、
macro generation、VBA semantic editing、signature preservation、transactional undo、
workbook relationship repair、content type repair、full macro support 或 public macro
editing API。
linked-object fixture 上的 VBA project ordinary-replace-then-remove coverage 只能描述为
后续 removal 清理 active VBA replacement、记录 removed-part audit、输出省略
VBA project part、移除 VBA content type override、保留 workbook `.rels` inbound
VBA relationship、且不凭空创建 `xl/_rels/vbaProject.bin.rels`；不能写成 macro
generation、VBA semantic editing、signature preservation、transactional undo、
workbook relationship repair、content type repair、full macro support 或 public macro
editing API。
VBA project replace-then-remove output-plan coverage 只能描述为内部 `planned_output()`
暴露 omitted `xl/vbaProject.bin` part、removed_parts target/reason/inbound audit、
workbook-owned inbound VBA relationship metadata、content types rewrite、empty
removed_package_entries，以及没有凭空创建 `xl/_rels/vbaProject.bin.rels`；
不能写成 public output planner、macro generation、VBA semantic editing、signature
preservation、workbook relationship repair、content type repair、orphan cleanup、
transactional undo、full macro support 或 public macro editing API。
linked-object fixture 上的 ordinary VML drawing replacement coverage 只能描述为只重写
`xl/drawings/vmlDrawing1.vml`、worksheet `.rels` 里的 URI-qualified `vmlDrawing`
relationship 原样保留、VML content type override 仍可读、不凭空创建
`xl/drawings/_rels/vmlDrawing1.vml.rels`、以及 workbook/worksheet/drawing/chart/
media/table/sharedStrings/styles/VBA/calcChain/unknown source entries 保持
copy-original baseline；不能写成 VML shape editing、legacy drawing mutation、
relationship repair、full VML/drawing support 或 public drawing editing API。
linked-object fixture 上的 VML drawing remove-then-ordinary-replace coverage 只能描述为
后续 replacement 恢复 active VML drawing part、清理 stale removed-part audit、让
`[Content_Types].xml` 回到 source/copy-original audit、保留 worksheet `.rels`
URI-qualified inbound `vmlDrawing` relationship、不凭空创建 VML owner `.rels`；
不能写成 VML shape editing、legacy drawing mutation、transactional undo、
relationship repair、content type repair、full VML/drawing support 或 public drawing
editing API。
该 restore 状态的 output-plan coverage 也只能描述为内部 `planned_output()` 快照暴露
active VML drawing `LocalDomRewrite` entry、content types copy-original audit、
preserved package/workbook/worksheet/drawing relationships、preserved linked/unknown
entries、empty removed_parts / removed_package_entries，以及 no invented owner
`.rels`；不能写成 public output planner、relationship
repair、content type repair、transactional undo 或 public drawing editing API。
同一路径还覆盖 VML drawing 先 ordinary replacement 再显式移除的顺序：后续
removal 会清理 active VML drawing replacement、记录 removed-part audit、输出省略
VML drawing part、移除 VML content type override、保留 worksheet `.rels` 中的
URI-qualified inbound `vmlDrawing` relationship，且不凭空创建 VML owner `.rels`；
这不是 VML shape editing、legacy drawing mutation、事务式 undo、relationship
pruning/repair、content type repair 或完整 VML/drawing 支持。
该 final-removal 状态的 output-plan coverage 也只能描述为内部 `planned_output()`
快照暴露 omitted VML drawing part、removed_parts target/reason/inbound audit、
URI-qualified worksheet inbound relationship metadata、content types rewrite、empty
removed_package_entries，以及 no invented VML owner `.rels`；不能写成 public output
planner、drawing editing API、relationship repair、content type repair、transactional
undo 或 public drawing editing API。
linked-object fixture 上的 ordinary percent-decoded drawing replacement coverage 只能描述为只重写
`xl/drawings/drawing space.xml`、worksheet `.rels` 里的原始
`../drawings/drawing%20space.xml` relationship 原样保留、drawing content type override
仍可读、不凭空创建 `xl/drawings/_rels/drawing space.xml.rels`、以及 workbook/
worksheet/drawing/chart/media/table/VML/sharedStrings/styles/VBA/calcChain/unknown
source entries 保持 copy-original baseline；不能写成 percent-encoded target repair、
relationship rewrite、drawing mutation、full drawing support 或 public drawing editing API。
linked-object fixture 上的 percent-decoded drawing remove-then-ordinary-replace
coverage 只能描述为后续 replacement 恢复 active decoded drawing part、清理 stale
removed-part audit、让 `[Content_Types].xml` 回到 source/copy-original audit、保留
worksheet `.rels` 中原始 encoded inbound `../drawings/drawing%20space.xml`
relationship、且不凭空创建 `xl/drawings/_rels/drawing space.xml.rels`；不能写成
percent-encoded target repair、relationship rewrite、drawing mutation、
transactional undo、relationship repair、content type repair、full drawing support
或 public drawing editing API。
该 restore 状态的 output-plan coverage 也只能描述为内部 `planned_output()` 快照暴露
active decoded drawing `LocalDomRewrite` entry、content types copy-original audit、
preserved package/workbook/worksheet/drawing relationships、preserved linked/unknown
entries，以及 no invented owner `.rels`；不能写成 public output planner、
percent-encoded target repair、relationship rewrite、relationship repair、content type
repair、transactional undo 或 public drawing editing API。
同一路径还覆盖 percent-decoded drawing 先 ordinary replacement 再显式移除的
顺序：后续 removal 会清理 active decoded drawing replacement、记录 removed-part
audit、输出省略 decoded drawing part、移除 drawing content type override、保留
worksheet `.rels` 中原始 encoded inbound `../drawings/drawing%20space.xml`
relationship，且不凭空创建 `xl/drawings/_rels/drawing space.xml.rels`；内部
`planned_output()` 也覆盖该 final-removal 状态，暴露 omitted decoded drawing part、
removed_parts target/reason/inbound audit、encoded inbound worksheet relationship
metadata、content types rewrite、empty removed_package_entries 和 no invented owner
`.rels`；这不是
percent-encoded target repair、relationship rewrite、drawing mutation、事务式 undo、
relationship pruning/repair、content type repair 或完整 drawing 支持。
registered comments-part fixture coverage 只能描述为 worksheet rewrite 会把
`xl/comments/comment1.xml` 和 source worksheet `.rels` 作为 copy-original
preservation 处理、保留 comments content type override，并可由 `PackageReader` /
`RelationshipGraph` 重读；不能写成 comments editing、threaded comments、notes UI、
relationship repair、orphan cleanup 或 public API。
threaded comments / persons fixture coverage 只能描述为 worksheet rewrite 会把
`xl/threadedComments/threadedComment1.xml`、`xl/persons/person.xml`、source worksheet
`.rels` 和 workbook `.rels` 作为 copy-original preservation 处理，并可由
`PackageReader` / `RelationshipGraph` 重读；不能写成 comments / threaded comments
editing、notes UI、relationship repair、orphan cleanup 或 public API。
ordinary threaded comments replacement/removal coverage 只能描述为
`replace_part("/xl/threadedComments/threadedComment1.xml", ...)` 只重写 threaded
comments XML，并保留 legacy comments、persons、worksheet `.rels` 中的
legacy/threaded inbound relationships、workbook `.rels` 中的 persons relationship、
content type overrides 和 unknown entry；显式 removal 会省略 threaded comments part
并移除其 content type override，但保留 worksheet `.rels` 中指向缺失 part 的 inbound
relationship、persons part / workbook relationship、legacy comments 和 unknown entry。
不能写成 threaded comments model mutation、persons/schema repair、relationship
pruning/repair、orphan cleanup、notes UI 或 public API。
ordinary threaded comments replacement output-plan coverage 只能描述为内部
`planned_output()` 暴露 active threaded comments part `LocalDomRewrite`、preserved
content types / package relationships / workbook / workbook `.rels` / worksheet /
worksheet `.rels` / legacy comments / persons part / unknown entry，且不凭空创建
threaded comments owner `.rels`；不能写成 threaded comments model mutation、
persons/schema repair、notes UI、relationship repair、orphan cleanup 或 public API。
ordinary persons replacement/removal coverage 只能描述为
`replace_part("/xl/persons/person.xml", ...)` 只重写 persons XML，并保留 workbook
inbound persons relationship、threaded comments、legacy comments、worksheet `.rels`、
content type overrides 和 unknown entry；显式 removal 会省略 persons part 并移除
persons content type override，但保留 workbook `.rels` 中指向缺失 part 的 inbound
relationship、threaded comments、legacy comments、worksheet 和 unknown entry。不能写成
persons/schema repair、threaded comments model mutation、relationship pruning/repair、
orphan cleanup、notes UI 或 public API。
threaded comments / persons same-path ordering coverage 只能描述为两条路径都覆盖
remove-then-replace 和 replace-then-remove：后续 replacement 会清理 stale
removed-part audit，恢复 active threaded comments/persons part，让 source content
types audit 回到 copy-original，并且不创建对应 owner `.rels`；threaded comments
remove-then-replace output-plan coverage 只能描述为内部 `planned_output()` 暴露
active threaded comments part local-DOM rewrite、content types copy-original audit、
preserved package/workbook/worksheet `.rels`、legacy comments、persons part 和
unknown entry，并清空 output-plan removed_parts / removed_package_entries，且不凭空创建 threaded comments owner `.rels`；不能写成 threaded
comments undo、semantic merge、relationship repair、orphan cleanup 或 public API。
threaded comments
后续 removal 会记录 removed-part 和 worksheet inbound relationship audit，输出省略
threaded comments part，移除 threaded comments content type override，并保留
worksheet `.rels` 中指向缺失 part 的 inbound relationship、persons part / workbook
relationship、legacy comments 和 unknown entry；persons 后续 removal 会记录
removed-part 和 workbook inbound relationship audit，输出省略 persons part，移除
persons content type override，并保留 workbook `.rels` 中指向缺失 part 的 inbound
relationship、threaded comments、legacy comments、worksheet 和 unknown entry。不能写成
transactional undo、threaded comments/persons semantic merge、persons/schema repair、
relationship pruning/repair、content type repair、orphan cleanup、notes UI 或 public API。
threaded comments replace-then-remove output-plan coverage 只能描述为内部
`planned_output()` 暴露单个 omitted threaded comments part、removed_parts 中目标为
threaded comments part 且 reason / inbound audit 保留的 removed-part audit、
worksheet-owned inbound threadedComment relationship metadata、content types rewrite、
preserved worksheet / workbook `.rels` 和 persons part copy-original audit，且
removed_package_entries 为空、不凭空创建 threaded comments owner `.rels`。
persons replace-then-remove output-plan coverage 只能描述为内部 `planned_output()`
暴露单个 omitted persons part、removed_parts 中目标为 persons part 且 reason /
inbound audit 保留的 removed-part audit、workbook-owned inbound persons relationship
metadata、content types rewrite、preserved workbook/worksheet `.rels` 和 threaded
comments part copy-original audit，且 removed_package_entries 为空、不凭空创建 persons owner `.rels`。
persons remove-then-replace output-plan coverage 只能描述为内部 `planned_output()`
暴露 active persons part local-DOM rewrite、content types copy-original audit、
preserved package/workbook/worksheet `.rels`、threaded comments、legacy comments 和
unknown entry，并清空 output-plan removed_parts / removed_package_entries，且不凭空创建 persons owner `.rels`；不能写成 persons/schema undo、
semantic merge、relationship repair、orphan cleanup 或 public API。
pivot table / pivot cache fixture coverage 只能描述为 worksheet rewrite 会把
`xl/pivotTables/pivotTable1.xml`、`xl/pivotCache/pivotCacheDefinition1.xml`、
`xl/pivotCache/pivotCacheRecords1.xml`、source worksheet `.rels`、pivot table owner
`.rels`、pivot cache definition owner `.rels` 和 workbook `.rels` 作为 copy-original
preservation 处理，并可由 `PackageReader` / `RelationshipGraph` 重读；不能写成
pivot table editing、pivot cache rebuild、relationship repair、orphan cleanup 或 public API。
该 worksheet rewrite 路径的 internal `planned_output()` coverage 只能描述为暴露
fullCalcOnLoad / `CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook
`LocalDomRewrite`、package/workbook/worksheet `.rels` copy-original、pivot table /
pivot cache definition / pivot cache records relationship context、content types 和
unknown entry copy-original，且确认不凭空创建 records owner `.rels`；不能写成
pivot cache rebuild、records refresh、relationship repair/pruning、orphan cleanup 或
public API。
ordinary pivot table replacement/removal coverage 只能描述为
`replace_part("/xl/pivotTables/pivotTable1.xml", ...)` 只重写 pivot table XML，并保留
worksheet `.rels` 中的 inbound pivotTable relationship、pivot table owner `.rels` /
pivotCacheDefinition relationship、pivot cache definition / records parts、pivot cache
definition owner `.rels`、workbook `<pivotCaches>`、workbook `.rels`
pivotCacheDefinition relationship、content type overrides 和 unknown entry；显式 removal
会省略 pivot table part 及其 owner `.rels`，移除 pivot table content type override，
但保留 worksheet `.rels` 中指向缺失 part 的 inbound relationship、workbook pivot cache
metadata、pivot cache definition / records 链和 unknown entry。不能写成 pivot table
semantic editing、pivot cache rebuild、cache-record refresh、relationship pruning/repair、
orphan cleanup、owner `.rels` repair 或 public API。
pivot table same-path ordering coverage 只能描述为 remove-then-replace 清理
stale removed-part / removed owner `.rels` audit，恢复 active pivot table、owner
`.rels` copy-original audit 和 source content types audit；replace-then-remove 清理
active replacement，记录 removed-part / removed owner `.rels` audit，输出省略 pivot
table part 和 owner `.rels`，移除 pivot table content type override，并保留 worksheet
`.rels` 中指向缺失 part 的 inbound relationship、workbook pivot cache metadata、
pivot cache definition / records 链和 unknown entry。不能写成 transactional undo、
pivot table semantic merge、pivot cache rebuild、relationship pruning/repair、
content type repair、orphan cleanup 或 public API。
pivot table remove-then-replace output-plan coverage 只能写成内部 `planned_output()`
暴露 active pivot table `LocalDomRewrite` entry、owner `.rels` copy-original
`SourceRelationships` audit、content types copy-original audit、preserved
package/worksheet/workbook relationships、pivot cache definition / records 链和 unknown
entry；不能写成 pivot table semantic editing、pivot cache rebuild、
relationship pruning/repair、orphan cleanup 或 public API。
pivot table replace-then-remove output-plan coverage 只能写成内部 `planned_output()`
暴露 omitted pivot table part、omitted owner `.rels`、worksheet inbound pivotTable
relationship audit、content types rewrite、preserved worksheet/workbook relationships、
pivot cache definition / records 链和 unknown entry；不能写成 pivot table semantic
editing、pivot cache rebuild、relationship pruning/repair、orphan cleanup 或 public API。
ordinary pivot cache definition replacement/removal coverage 只能描述为
`replace_part("/xl/pivotCache/pivotCacheDefinition1.xml", ...)` 只重写 pivot cache
definition XML，并保留 workbook/pivot-table inbound relationships、pivot cache records、
pivot cache definition owner `.rels`、content type overrides 和 unknown entry；显式
removal 会省略 pivot cache definition part 及其 owner `.rels`，移除 cache definition
content type override，但保留 workbook/pivot table inbound relationships、pivot table、
pivot cache records、worksheet 和 unknown entry。不能写成 pivot cache rebuild、
cache-record refresh、relationship pruning/repair、orphan cleanup、owner `.rels`
repair 或 public API。
pivot cache definition same-path ordering coverage 只能描述为 remove-then-replace
清理 stale removed-part / removed owner `.rels` audit，恢复 active pivot cache
definition、owner `.rels` copy-original audit 和 source content types audit；
replace-then-remove 清理 active replacement，记录 removed-part / removed owner `.rels`
audit，输出省略 cache definition part 和 owner `.rels`，并保留 workbook / pivot table
inbound relationships、pivot table、cache records、worksheet 和 unknown entry。不能写成
transactional undo、pivot cache semantic merge、relationship pruning/repair、content
type repair、orphan cleanup 或 public API。
pivot cache definition remove-then-replace output-plan coverage 只能描述为内部
`planned_output()` 暴露 active pivot cache definition `LocalDomRewrite` entry、owner
`.rels` copy-original `SourceRelationships` audit、content types copy-original
audit、preserved package/worksheet/workbook relationships、pivot table/cache records
和 unknown entry；pivot cache definition replace-then-remove output-plan coverage
只能描述为内部 `planned_output()` 暴露 omitted cache definition part、omitted owner
`.rels`、workbook / pivot table inbound pivotCacheDefinition relationship audit、
content types rewrite、preserved workbook/worksheet/pivot table/cache records/unknown
entries；不能写成 pivot cache rebuild、cache-record refresh、
relationship pruning/repair、content type repair、orphan cleanup 或 public API。
ordinary pivot cache records replacement/removal coverage 只能描述为
`replace_part("/xl/pivotCache/pivotCacheRecords1.xml", ...)` 只重写 pivot cache records
XML，并保留 cache definition owner `.rels` 中的 inbound relationship、pivot cache
definition、pivot table、workbook / worksheet relationships、content type overrides
和 unknown entry；显式 removal 会省略 pivot cache records part，移除 records
content type override，但保留 cache definition owner `.rels` 中指向缺失 records part
的 inbound relationship、pivot cache definition、pivot table、workbook、worksheet 和
unknown entry。不能写成 pivot cache records refresh、pivot cache rebuild、
relationship pruning/repair、orphan cleanup 或 public API。
pivot cache records same-path ordering coverage 只能描述为 remove-then-replace
清理 stale removed-part audit，恢复 active pivot cache records，让 source content types
audit 回到 copy-original，并且不创建 records owner `.rels`；replace-then-remove 清理
active replacement，记录 removed-part 和 cache-definition inbound relationship audit，
输出省略 records part，移除 records content type override，并保留 cache definition
owner `.rels` 中指向缺失 records part 的 inbound relationship、pivot cache definition、
pivot table、workbook、worksheet 和 unknown entry。不能写成 transactional undo、
pivot cache records semantic merge、relationship pruning/repair、content type repair、
orphan cleanup 或 public API。
pivot cache records remove-then-replace output-plan coverage 只能描述为内部
`planned_output()` 暴露 active pivot cache records `StreamRewrite` entry、content
types copy-original audit、preserved package/worksheet/workbook relationships、pivot
table/cache definition 链、unknown entry，且 no invented records owner `.rels`；
pivot cache records replace-then-remove output-plan coverage 只能描述为内部
`planned_output()` 暴露 omitted records part、cache-definition inbound
pivotCacheRecords relationship audit、content types rewrite、preserved cache definition
owner `.rels`、no invented records owner `.rels` 和 preserved workbook/worksheet/pivot
table/cache definition/unknown entries；不能写成 pivot cache records refresh、pivot
cache rebuild、relationship pruning/repair、content type repair、orphan cleanup 或
public API。
workbook external links fixture coverage 只能描述为 worksheet rewrite 在重写
`xl/workbook.xml` calc metadata 时，会保留 workbook `<externalReferences>`、workbook
`.rels` 中的 externalLink relationship、`xl/externalLinks/externalLink1.xml`、
externalLink owner `.rels`、external `externalLinkPath` target、content type override
和 unknown entry，并可由 `PackageReader` / `RelationshipGraph` 重读；不能写成
external links editing、external data refresh、path validation、relationship repair、
orphan cleanup 或 public API。
该 worksheet rewrite 路径的 internal `planned_output()` coverage 只能写成暴露
fullCalcOnLoad、`CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook
`LocalDomRewrite`、workbook `.rels` copy-original、externalLink part 与 owner `.rels`
copy-original、content types copy-original 和 unknown entry preservation，且不新增
relationship target audit；不能写成 external links editing 或 relationship repair。
ordinary external links replacement/removal coverage 只能描述为
`replace_part("/xl/externalLinks/externalLink1.xml", ...)` 只重写 externalLink XML，
并保留 workbook `.rels` 中的 inbound externalLink relationship、externalLink owner
`.rels` 中的 external `externalLinkPath` target、content type override、worksheet 和
unknown entry；显式 removal 会省略 externalLink part 及其 owner `.rels`，移除
externalLink content type override，但保留 workbook `<externalReferences>`、workbook
`.rels` 中指向缺失 part 的 inbound relationship、worksheet 和 unknown entry。不能写成
external links semantic editing、external data refresh、path validation、relationship
pruning/repair、orphan cleanup、owner `.rels` repair 或 public API。
externalLink same-path ordering coverage 只能描述为 remove-then-replace 清理 stale
removed-part / removed owner `.rels` audit，恢复 active externalLink part、owner `.rels`
copy-original audit 和 source content types audit；replace-then-remove 清理 active
replacement，记录 removed-part / removed owner `.rels` audit，输出省略 externalLink
part 和 owner `.rels`，并保留 workbook inbound relationship、worksheet 和 unknown entry。
不能写成 transactional undo、external links semantic merge、relationship pruning/repair、
content type repair、orphan cleanup 或 public API。
externalLink remove-then-replace output-plan coverage 只能描述为内部
`planned_output()` 暴露 active externalLink `LocalDomRewrite` entry、owner `.rels`
copy-original `SourceRelationships` audit、content types copy-original audit，以及
preserved package/workbook relationships、workbook、worksheet 和 unknown entry；
externalLink replace-then-remove output-plan coverage 只能描述为内部
`planned_output()` 暴露 omitted externalLink part、omitted owner `.rels`、workbook
inbound relationship audit、content types rewrite、preserved package/workbook
relationships、workbook、worksheet 和 unknown entry；不能写成 external links semantic
editing、external data refresh、relationship pruning/repair、orphan cleanup 或 public API。
custom XML fixture coverage 只能描述为 worksheet rewrite 会保留 package `_rels/.rels`
中的 customXml relationship、`customXml/item1.xml`、custom XML item owner `.rels`、
`customXml/itemProps1.xml`、custom XML properties content type override 和 unknown
entry，并可由 `PackageReader` / `RelationshipGraph` 重读；不能写成 custom XML editing、
schema/data binding、relationship repair、orphan cleanup 或 public API。
该 worksheet rewrite 路径的 internal `planned_output()` coverage 只能写成暴露
fullCalcOnLoad、`CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook
`LocalDomRewrite`、package relationships copy-original、custom XML item / item owner
`.rels` / properties part copy-original、content types copy-original 和 unknown entry
preservation，且不新增 relationship target audit、不凭空创建 properties owner `.rels`；
不能写成 custom XML editing、schema/data binding 或 relationship repair。
ordinary custom XML replacement coverage 只能描述为
`replace_part("/customXml/item1.xml", ...)` 只重写 custom XML item，保留 package
`_rels/.rels` customXml inbound relationship、custom XML item owner `.rels` /
customXmlProps relationship、`customXml/itemProps1.xml`、custom XML properties content
type override、默认 XML content type 和 unknown entry copy-original 基线，并可由
`PackageReader` / `RelationshipGraph` 重读；不能写成 custom XML semantic editing、
schema/data binding、relationship repair、content type repair、orphan cleanup 或
public editing API。
explicit custom XML removal coverage 只能描述为显式移除 `customXml/item1.xml`：
输出省略 custom XML item 及其 source-owned owner `.rels`、保留 package
`_rels/.rels` customXml inbound relationship、保留 `customXml/itemProps1.xml`、
custom XML properties content type override、默认 XML content type 和 unknown entry，
且不重写 `[Content_Types].xml`；不能写成 custom XML deletion semantics、
schema/data binding、relationship pruning/repair、content type repair、orphan cleanup
或 public editing API。
custom XML set/remove ordering coverage 只能描述为同一路径先显式移除再 ordinary
replace 会恢复 active custom XML item、清理 stale removed-part / removed owner
`.rels` audit、恢复 owner `.rels` copy-original audit，且不重写
`[Content_Types].xml`；先 ordinary replace 再显式移除会清理 active replacement、
记录 removed-part / removed owner `.rels` audit、输出省略 custom XML item 和 owner
`.rels`，并保留 package inbound relationship、properties part、默认 XML content
type 和 unknown entry。Internal `planned_output()` coverage for this restore state
exposes the active custom XML item `LocalDomRewrite` entry, owner `.rels`
copy-original `SourceRelationships` audit, and preserved package relationships,
content types, workbook, worksheet, properties part, and unknown entry。
Internal `planned_output()` coverage for this final-removal state exposes
the omitted custom XML item, omitted source-owned owner `.rels`,
package inbound customXml relationship audit, and preserved package relationships,
content types, workbook, worksheet, properties part, and unknown entry。不能写成 transactional undo、custom XML semantic merge、
relationship pruning/repair、content type repair、orphan cleanup 或 public editing API。
custom XML properties part replacement/removal coverage 只能描述为
`customXml/itemProps1.xml` replacement 只重写 properties part，保留 custom XML
item、item owner `.rels` / customXmlProps inbound relationship、package customXml
relationship、properties content type override 和 unknown entry；explicit removal
会输出省略 properties part、移除 properties content type override，但保留 custom XML
item、item owner `.rels` 中指向缺失 properties part 的 inbound customXmlProps
relationship、package customXml relationship、默认 XML content type 和 unknown
entry。不能写成 custom XML properties editing、schema/data binding、
relationship pruning/repair、content type repair、orphan cleanup 或 public editing API。
内部 `planned_output()` 对 ordinary properties replacement 状态的覆盖只能描述为暴露
active properties part `LocalDomRewrite`、preserved content types / package
relationships、preserved custom XML item / item owner `.rels` / workbook /
worksheet / unknown entry，且不凭空创建 properties owner `.rels`。不能写成 custom XML
properties semantic editing、schema/data binding、transactional undo、
relationship pruning/repair、content type repair、orphan cleanup 或 public editing API。
custom XML properties part ordering coverage 只能描述为同一路径 properties part
先显式移除再 ordinary replace 会清理 stale removed-part audit、恢复 active
properties part、恢复 properties content type override/content-types copy-original
audit，并继续保留 item owner `.rels`；先 ordinary replace 再显式移除会清理
active replacement、记录 removed-part audit、输出省略 properties part、移除
properties content type override，并继续保留 item owner `.rels` 中的 inbound
customXmlProps relationship。不能写成 transactional undo、custom XML properties
semantic merge、relationship pruning/repair、content type repair、orphan cleanup
或 public editing API。
内部 `planned_output()` 对该 properties final-removal 状态的覆盖只能描述为暴露 omitted
properties part、item-owned inbound customXmlProps relationship audit、content types
rewrite、preserved custom XML item / item owner `.rels` / package relationships /
workbook / worksheet / unknown entry，且不凭空创建 properties owner `.rels`。不能写成
custom XML properties deletion semantics、relationship
pruning/repair、content type repair、orphan cleanup 或 public editing API。
内部 `planned_output()` 对该 properties restore 状态的覆盖只能描述为暴露 active
properties part `LocalDomRewrite`、restored content types copy-original audit、
preserved custom XML item / item owner `.rels` / package relationships / workbook /
worksheet / unknown entry，且不凭空创建 properties owner `.rels`。不能写成 custom XML
properties semantic merge、transactional undo、relationship pruning/repair、
content type repair、orphan cleanup 或 public editing API。
custom XML item removal plus properties replacement coverage 只能描述为先显式移除
custom XML item，再 ordinary replace `customXml/itemProps1.xml` properties part：
后续 properties replacement 只重写 properties payload，保留 removed custom XML item /
removed owner `.rels` audit，输出继续省略 custom XML item 和 item owner `.rels`，
并保留 package customXml inbound relationship、properties content type override、
默认 XML content type 和 unknown entry。不能写成 custom XML dependency repair、
relationship pruning/repair、content type repair、orphan cleanup、transactional undo
或 public editing API。
内部 `planned_output()` 对该跨路径状态的覆盖只能描述为暴露 omitted custom XML item、
omitted source-owned owner `.rels`、package inbound customXml relationship audit、
active properties part local-DOM rewrite、preserved package relationships / content
types / workbook / worksheet / unknown entry，且不凭空创建 properties owner `.rels`。
不能写成 custom XML dependency repair、relationship pruning/repair、content type repair、
orphan cleanup、transactional undo 或 public editing API。
custom XML properties removal plus item replacement coverage 只能描述为先显式移除
`customXml/itemProps1.xml` properties part，再 ordinary replace custom XML item：
后续 item replacement 只重写 item payload，保留 removed properties part audit /
content-types rewrite，输出继续省略 properties part 和 properties content type override，
并保留 item owner `.rels` 中指向缺失 properties part 的 customXmlProps relationship、
package customXml inbound relationship、默认 XML content type 和 unknown entry。不能写成
custom XML dependency repair、relationship pruning/repair、content type repair、
orphan cleanup、transactional undo 或 public editing API。
内部 `planned_output()` 对该反向跨路径状态的覆盖只能描述为暴露 omitted
properties part、item-owned inbound customXmlProps relationship audit、content types
rewrite、active custom XML item local-DOM rewrite、preserved item owner `.rels` /
package relationships / workbook / worksheet / unknown entry，且不凭空创建 properties
owner `.rels`。不能写成 custom XML dependency repair、relationship pruning/repair、
content type repair、orphan cleanup、transactional undo 或 public editing API。
ordinary comments-part replacement coverage 只能描述为
`replace_part("/xl/comments/comment1.xml", ...)` 只重写 comments XML、保留
worksheet `.rels` inbound comments relationship、comments content type override、
workbook XML / workbook `.rels`、worksheet 和 unknown entry copy-original 基线，且不凭空
创建 comments owner `.rels`；不能写成 comments model mutation、threaded comments、
notes UI、relationship repair、orphan cleanup 或 public API。
ordinary comments replacement output-plan coverage 只能描述为内部
`planned_output()` 暴露 active comments part local-DOM rewrite、preserved content
types / package relationships / workbook / workbook `.rels` / worksheet /
worksheet `.rels` / unknown entry，且不凭空创建 comments owner `.rels`；不能写成
comments model mutation、notes UI、relationship repair、orphan cleanup 或 public API。
explicit comments-part removal coverage 只能描述为输出省略 `xl/comments/comment1.xml`、
移除 comments content type override、保留 worksheet `.rels` inbound comments
relationship，且不凭空创建 comments owner `.rels` omission；不能写成 comments
deletion semantics、threaded comments、notes UI、relationship pruning/repair、
orphan cleanup 或 public API。
remove-then-ordinary-replace comments coverage 只能描述为后续
`replace_part("/xl/comments/comment1.xml", ...)` 恢复 active comments replacement、
清理 stale removed-part audit、让 comments content type override 和
`[Content_Types].xml` 回到 source/copy-original 状态、保留 inbound worksheet `.rels`，
且仍不凭空创建 comments owner `.rels`；不能写成 comments undo、semantic merge、
relationship repair、orphan cleanup 或 public API。
remove-then-replace comments output-plan coverage 只能描述为内部 `planned_output()`
暴露 active comments part local-DOM rewrite、content types copy-original audit、
preserved package/workbook/worksheet `.rels` 和 unknown entry，并清空 output-plan
removed_parts / removed_package_entries，且不凭空创建 comments owner `.rels`；不能写成 comments undo、semantic merge、relationship
repair、orphan cleanup 或 public API。
replace-then-remove comments coverage 只能描述为后续 explicit removal 清理 active
comments replacement、记录 removed-part audit、输出省略 comments part、移除
comments content type override、保留 inbound worksheet `.rels`，且仍不凭空创建
comments owner `.rels`；不能写成 comments deletion semantics、transactional undo、
relationship pruning/repair、orphan cleanup 或 public API。
replace-then-remove comments output-plan coverage 只能描述为内部
`planned_output()` 暴露单个 omitted comments part、removed_parts 中目标为
comments part 且 reason / inbound audit 保留的 removed-part audit、worksheet-owned
inbound comments relationship metadata、content types rewrite、preserved
package/workbook/worksheet `.rels` copy-original audit，且 removed_package_entries
为空、不凭空创建 comments owner `.rels`。
exact/path-equivalent source-overwrite rejection 和 empty-output / missing-parent / non-directory-parent / existing-directory output
rejection 只能描述为 reader-backed copy 输出安全护栏，不能写成 atomic in-place
editing、filesystem repair 或 public editing API。
当前只可补充说明该护栏会在 materialize output entries 前失败，且失败后 queued
part replacement 状态不污染，后续
安全 `save_as()` 仍可输出 queued rewrite；现在还可说明 queued worksheet
replacement 的 `fullCalcOnLoad` / calcChain removal / package-entry audit /
planned output 状态同样不污染，后续安全输出仍按计划省略 calcChain；不能写成
transaction 或 atomic save。
它不是 public document properties editing API。
invalid replacement failure no-state-pollution 只能描述为 edit plan entries / notes、manifest
write-mode 和 copied output bytes 的窄回归，不能写成完整 validator 或 public editor。
metadata-entry replacement failure no-state-pollution 只能描述为 edit plan entries /
notes、package-entry audit、calc policy、manifest write-mode 和 copied output bytes 的窄回归。

图片对象 hyperlink 必须和 worksheet cell hyperlink 分开描述。当前
`ImageOptions::external_hyperlink_url` 属于 drawing object metadata：它写 drawing XML
`a:hlinkClick` 和 drawing-owned external hyperlink relationship，不写 worksheet
`<hyperlinks>`、不写 cell text、不创建 hyperlink style、不校验 URL 可达性，也不编辑
已有 XLSX。

对 images 这类跨 part 对象 API，注释还要说明是否只支持 Streaming /
new-workbook 路径，还是属于 Patch / existing-workbook 路径。图片读取 helper 当前使用
`stb` 读取 PNG/JPEG 尺寸、通道数和像素；但 `stb` 不负责 OpenXML media part、
drawing XML、drawing relationships、worksheet relationships、content types 或 anchors。
当前 `ImageInfo` / `read_image_info()` 只做 PNG/JPEG metadata probing；`ImagePixels` /
`read_image_pixels()` 会解码 PNG/JPEG 并分配完整 caller-owned decoded pixel buffer，
`pixels` 内存成本随 `width * height * channels` 增长。`read_image_pixels()` 是图片
解码 helper，不生成 media/drawing parts，也不应进入 streaming worksheet row/cell 热路径。
当前 `WorksheetWriter::add_image(path, anchor)` 和
`WorksheetWriter::add_image(bytes, anchor)` 是 Streaming / new-workbook PNG/JPEG
基础切片：它们用 `read_image_info()` 验证元数据，把原始图片字节复制到临时
file-backed media entry，并在 `close()` 时生成 media part、drawing XML、drawing
`.rels`、worksheet `.rels`、worksheet `<drawing>` 和 content type entries。
memory-source overload 接受 `std::span<const std::byte>`，caller-owned span 只需在调用
期间有效；FastXLSX 不保留该 span，不保留 decoded pixel buffer，也不把图片 bytes 放入
worksheet row/cell 热路径。
当前 `ImageOptions` 是同一 streaming image API 的窄 metadata options：
`from_offset` / `to_offset` EMU 值、`edit_as` 枚举和非空 `name` / `description`
字符串会复制进 writer state，并分别写到 drawing XML two-cell marker 的
`xdr:colOff` / `xdr:rowOff`、`xdr:twoCellAnchor editAs` 和 `xdr:cNvPr` 的
`name` / `descr` attributes；空 `name` 保留生成的 `Picture N`，空 `description`
省略。非空 `external_hyperlink_url` 会写 `a:hlinkClick` 并创建 drawing-local
external hyperlink relationship，`external_hyperlink_tooltip` 只写该 `a:hlinkClick`
的 `tooltip` attribute，且 tooltip without URL 会被拒绝。它只改变 drawing marker
metadata、non-visual picture properties 和可选 drawing-local hyperlink metadata，
不修改图片二进制、EXIF、media filename、anchor cell range、content types 或 cell
text，也不支持 `oneCellAnchor` / `absoluteAnchor` 元素或 row/column resize 几何计算。
`read_image_info()` 注释应限制为 PNG/JPEG metadata helper：读取格式、尺寸和
通道数；`read_image_pixels()` 注释必须写清完整 decoded pixel buffer 分配和 caller-owned
生命周期。不要暗示这两个 helper 会生成 media part、drawing XML、relationships、
content types、anchors，或验证 Excel package 兼容性。
图片插入 API 还要说明是否复制原始图片字节、是否会解码像素、decoded pixel buffer
生命周期、内存成本是否与图片字节数或 `width * height * channels` 相关、是否生成
`xl/media/*`、`xl/drawings/drawing*.xml`、drawing `.rels`、worksheet `.rels`、
worksheet `<drawing>` 和 content type entries，以及是否支持裁剪、旋转、压缩、
格式转换、existing drawing mutation、existing-file editing 和图片保真复制。
memory-source API 的注释还必须说明 span 生命周期、同步复制语义、空 buffer 和
unsupported header 的错误边界，以及它不是任意 stream/URL/base64 图片源。
如果只是嵌入已有 PNG/JPEG，应优先说明原始字节保留路径；不要为了便利 API 把图片
处理放进 worksheet row/cell 热路径或让大型 worksheet 进入 DOM。
如果暴露图片对象 hyperlink metadata，应说明 relationship owner 是 drawing part，
不是 worksheet part；`a:blip r:embed` 的 image relationship id 必须与 `a:hlinkClick`
的 hyperlink relationship id 分离，不能复用 cell hyperlink 路径。

## 性能注释要求

涉及热路径或大数据行为的 API，注释必须包含性能边界。

需要说明：

- 是否 O(rows)、O(cells) 或与 unique strings 数量相关。
- 是否分配跨行缓存。
- 是否可能触发 shared strings 去重状态增长。
- 是否影响 ZIP 压缩等级或输出文件大小。
- 是否会触发 DOM。
- Finalization API 必须说明当前是否 assemble package entries、哪些 entries 可
  file-backed/chunked、哪些 parts 仍可能 in-memory、是否 true package streaming、
  是否有 Zip64 或 existing-file preservation 保证。

禁止写模糊承诺，例如“高性能”“低内存”，却不说明内存由哪些状态组成。

## API 设计禁忌

- 不要为了让 API 像普通 workbook 编辑器一样方便，而默认保存完整 worksheet。
- 不要让 `Workbook` 级 API 隐式把大数据路径转成 in-memory 模式。
- 不要把当前 owning `Cell` 当作通用内部存储单元去堆百万级 cell；它适合便利 API，
  不适合大规模 cell store。
- 不要把 streaming API 做成普通 DOM API 的附属补丁。
- 不要把 PackageEditor 做成 streaming writer 的事后补丁；已有文件编辑必须有独立
  Patch API、EditPlan 和 preservation 语义。
- 不要隐藏压缩等级、字符串策略、DOM 模式等会影响性能的关键选择。
- 不要让高级功能污染 cell XML 写入热路径。
- 不要在无法更新或保留联动 part 时静默修改 sheet 结构；应提供保守策略、明确错误或
  用户可选择的 ReferencePolicy。

## 任务计划要求

规划 API 或实现任务时，任务说明必须写清：

- 对应的 `docs/TASK_BREAKDOWN.md` 子任务编号；没有拆分编号的较大任务应先拆分再执行。
- 属于哪个阶段：Phase 1、Phase 2、Phase 3、Phase 4 或 Phase 5。
- 使用哪种 API 模式：Streaming、Patch 或 In-memory。
- 是否触碰性能热路径。
- 是否需要文档注释。
- 需要哪些结构测试、Excel 可视化验证、拆包 XML 对比或 benchmark。
- 是否会引入依赖或改变 CMake target。
- 如果是 Patch / editing 任务，必须列出 EditPlan 影响范围、unknown part preservation、
  relationship/content type side effects、sharedStrings/styles/calcChain 策略。

如果任务要求“API 更易用”，必须同时说明为什么不会破坏流式性能主线。

## 验证清单

- public API 有文档注释。
- 文档注释写明模式、内存行为、性能边界、随机访问限制和 OpenXML part 副作用。
- 大数据路径仍能 row/chunk 化。
- 大型 worksheet finalization 不会重新物化完整 worksheet XML；如果仍会发生，
  必须在 API 注释中明确限制。
- 便利 API 不会隐式 DOM 化大型 worksheet。
- 测试覆盖 API 行为和 OpenXML 结构。
- 需要时完成本机 Excel 可视化验证。
- styles API 还需验证文档注释写清 workbook-local `StyleId`、foreign id 拒绝、
  `xl/styles.xml` / workbook relationship / content type side effects、默认 `s="0"`
  省略、number format 不是 date cell type、wrap-text + limited horizontal/vertical
  alignment 不是完整 alignment、bold/italic/direct ARGB font color 不是完整 font control、
  solid fill 不是完整 fill control，
  以及当前不支持完整 formatting。
- 图片 API 还需验证文档注释没有把 `stb` 解码能力写成 OpenXML 图片支持，也没有省略
  decoded pixel buffer、caller-owned memory span、copy-to-temp-file-backed media entry
  或 media/drawing part state 的内存边界。
- 图片对象 hyperlink API 还需验证注释明确 `a:hlinkClick`、drawing-owned `.rels`、
  no worksheet `<hyperlinks>` / no cell style / no URL reachability validation，以及
  `a:blip r:embed` 与 hyperlink `r:id` 分离。
- document properties API 还需验证结构测试覆盖 `docProps/core.xml`、
  `docProps/app.xml`、relationships、content types、XML escape，并明确不生成
  `docProps/custom.xml`；若走内部 Patch 小切片，还需验证未修改 part 和 unknown
  entry preservation。
- Patch / PackageEditor 相关任务若验证了 worksheet `.rels`、linked object parts、
  untouched `xl/sharedStrings.xml`、`xl/styles.xml` 或 workbook `definedNames`
  保留，只能写成窄 preservation regression；即使 replacement XML 省略旧
  `<drawing>` / `<tableParts>` 引用，也不能写成 relationship pruning、orphan
  cleanup、sharedStrings/styles/defined-name 语义同步、索引迁移、重建或 public editing API。
- Patch / PackageEditor 相关任务若验证 DEFLATE source 输入，只能写成未修改 part
  的解压后 payload 语义保留；当前 minizip-enabled PackageEditor 回归覆盖 ordinary
  workbook replacement、unknown extension target replacement、workbook calc metadata
  helper，以及 worksheet replacement 下的 calcChain cleanup、linked
  payload preservation 和 unknown extension owner `.rels` 可由输出 `PackageReader` /
  `RelationshipGraph` 重读，不能写成保留源 ZIP compression method、timestamps、
  extra fields 或压缩字节。
- Patch / PackageEditor 相关任务若组合了 docProps generated-small-XML 和 worksheet
  replacement，只能写成 relationship/content-type 状态合并、calcChain removal、
  stale calcChain owner `.rels` omission、缺失 calcChain payload 时的 stale metadata
  cleanup、workbook metadata rewrite 和 unknown-entry
  preservation 的窄回归；metadata-only cleanup 只能写成 content type / workbook
  relationship 残留清理，不能写成通用 metadata repair；如果提到 removed-part audit，只能写成
  `PartRewritePlanner::plan_worksheet_stream_rewrite()` 在 `CalcChainAction::Remove`
  下产出的内部审计元数据，不能写成完整 relationship pruning、完整
  document properties editing 或完整 existing-file editing。
- Patch / PackageEditor 相关任务若验证 malformed workbook XML 导致 workbook metadata
  rewrite 预检失败，只能写成失败不污染 edit plan entries / notes、manifest / copied output 的窄回归；
  不能写成完整 workbook metadata editor、repair engine 或 robust XML parser。
- Patch / PackageEditor 相关任务若验证 `ReferencePolicyAction::Fail` 且此前已有
  ordinary replacement 排队，只能写成后续 linked worksheet rewrite 失败不会丢失既有
  replacement、manifest write-mode、source-owned `.rels` audit 或输出 bytes；不能写成
  通用事务、atomic rollback 或 public editor。
- Patch / PackageEditor 相关任务若验证 core/app docProps package relationship target
  冲突，只能写成 generated-small-XML 路径失败不污染 edit plan entries / notes、manifest /
  package-entry audit / copied output 的窄回归；不能写成完整关系修复器或完整
  document properties editing。
- Patch / PackageEditor 相关任务若验证 invalid replacement failure，只能写成
  edit plan entries / notes、manifest write-mode / copied output bytes 不污染的窄回归；不能写成
  完整 package validator、relationship repair 或 public editor。
- Patch / PackageEditor 相关任务若验证了 `EditPlan` package-entry 审计，只能写成
  `[Content_Types].xml` / package `.rels` / owner `.rels` rewrite、omission 或
  preserved copy-original side effects 可见；若提到结构，只能写成内部
  `PackageEntryAuditKind` 分类加可选 `owner_part`，且只有 source-owned `.rels`
  携带 owner；kind 与 entry path 必须一一匹配，`ContentTypes` 只能指向
  `[Content_Types].xml`，`PackageRelationships` 只能指向 package `_rels/.rels`，
  `SourceRelationships` 必须匹配 owner part 推导出的 source-owned `.rels` entry；
  不能写成完整 relationship/content-type mutator、relationship pruning 或 public editing API。
- Patch planner / DependencyAnalyzer 相关任务若验证 external relationship target，
  只能写成 external target 不作为 package part、但会留下携带 owner part、relationship
  id 和原始 target 的审计 note；不能写成 existing-file hyperlink editing、target
  validation 或完整 hyperlink support。
- Patch planner / DependencyAnalyzer 相关任务若验证 drawing-owned `.rels` 中的
  external、URI-qualified、invalid 或 unresolved relationship target，只能写成从 worksheet
  rewrite 递归发现后的审计 traceability；不能写成 drawing/image/chart editing、
  relationship repair、orphan cleanup 或完整 package validation。
- Patch planner / DependencyAnalyzer 相关任务若验证 unresolved internal relationship
  target，只能写成不虚构 package part、留下携带 owner part、relationship id、
  relationship type、原始 target 和 normalized unresolved target 的 package structure review 审计 note；不能写成
  relationship repair、target creation 或完整 package validation。
- Patch planner / DependencyAnalyzer 相关任务若验证带 query/fragment 的 internal
  relationship target，只能写成 URI-qualified target 本身不作为 package part、留下
  package structure review 审计 note，且 base target 在已注册时可作为保守
  copy-original 依赖；审计 note 可携带 owner part、relationship id、relationship type、原始 target 和
  normalized base target；不能写成 relationship repair、target validation 或完整
  package validation。
- Patch planner / DependencyAnalyzer 相关任务若验证以 `/` 开头的 absolute internal
  relationship target，只能写成按 package part path 做 normalization，已注册 target
  可作为保守 copy-original 依赖；不能写成完整 URI validator 或 relationship repair。
- Patch planner / DependencyAnalyzer 相关任务若验证 percent-encoded internal
  relationship target，只能写成先解码 `%XX` 再做 part-name normalization，
  registered decoded target 可作为保守 copy-original 依赖；malformed percent
  escape 或解码后非法 target 只能写成 invalid-target 审计路径，不能写成
  relationship repair、target validation 或完整 URI validator。
- Patch / PackageEditor 相关任务若验证了写 exact/path-equivalent source package
  路径、empty output path、missing or non-directory output parent path 或 existing directory output path 被拒绝，只能写成 reader-backed copy
  安全护栏、exact/path-equivalent source-overwrite rejection 或 empty-output /
  missing-parent / non-directory-parent / existing-directory output rejection；
  不能写成 atomic in-place
  editing、atomic save 或 public in-place editor。
- Patch planner / PackageEditor 相关任务若只把 workbook calcPr / definedNames
  review 写入 dependency reason 或 rewrite reason，只能写成审计上下文；不能写成
  defined-name 引用更新、sheet rename/delete/move 支持或完整 workbook metadata editing。
- Patch planner / DependencyAnalyzer 相关任务若验证 copy-original dependency reason
  里的 relationship id、relationship type 和 normalized target part path，只能写成 Patch audit
  可追溯性增强；不能写成 relationship repair、target validation 或 object editing。
- Patch planner / DependencyAnalyzer 相关任务若验证 `RelationshipTargetAudit`、
  relationship-derived `PartDependency`，或 relationship-derived copy-original
  `EditPlanEntry` 的 owner/id/type/target 结构化字段，只能写成 internal Patch audit metadata；
  不能写成 public relationship editor、relationship repair 或完整 object editing。
- 性能敏感 API 有 benchmark 或明确的后续 benchmark 任务。
