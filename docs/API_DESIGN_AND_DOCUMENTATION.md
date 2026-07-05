# API 设计与文档注释

## 目标

FastXLSX 的 API 可以追求易用，但不能为了易用性牺牲大文件性能主线，也不能把
编辑能力降级为 streaming writer 的附属补丁。

API 设计必须和项目定位对齐：FastXLSX 是可编辑的高性能 XLSX 读写与编辑库，而不是
单纯 high-performance writer，也不是 Excel 公式/渲染/对象语义的完整复刻。

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

// In-memory random editing (已落地 small-file public 切片): 小文件随机读写单元格。
auto editable_sheet = editor.worksheet("Data");
editable_sheet.set_cell("A1", CellValue::text("A"));
```

统一命名规则：

- 创建 sheet 使用 `add_worksheet(name)`。
- 查找已有 sheet 使用 `worksheet(name)` 或 `try_worksheet(name)`，不要混用
  `get_sheet()`、`sheet_by_name()`、`select_sheet()` 等同义词。
- 追加行使用 `append_row(...)`；随机写单元格使用 `set_cell(...)`，只能出现在
  In-memory editor 语义里。
- 保存新文件使用 `save(path)` 或 writer 的 `close()`；已有文件编辑输出使用
  `save_as(path)`，避免暗示当前支持原地 atomic 覆盖。
- 能用 workbook / worksheet / cell / range 语言表达的 public API，不应要求用户先理解
  part、relationship owner、content type override 或 package entry。

统一值类型规则：

- `CellRange` 是跨 Streaming metadata、图片 anchor、Patch / In-memory range
  操作都应复用的 public range 值。
- `StyleId`、`CellStyle`、`DocumentProperties`、`HyperlinkOptions`、
  `ImageOptions`、`ImageAnchorOffset` 等应保持 workbook / worksheet 语义，不要泄漏底层 part 名。
- `CellValue` 是当前已落地的 owning 单元格语义值：number、text、boolean、formula、
  blank 以及可选 style reference。它可以被 `Cell`、当前
  `WorkbookEditor::replace_sheet_data()` rows 输入、`WorkbookEditor::replace_cells()`
  targeted-cell 输入和 `WorksheetEditor` small-file In-memory editor 共同使用。
  当前已有 internal `CellStore` 稀疏存储、guardrail、standalone `<sheetData>`
  emission 和 public `WorksheetEditor` handoff；这仍不表示 large-file low-memory
  random editing、sharedStrings/styles migration、relationship repair 或完整语义同步已经实现。
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
概念            Streaming              Small new workbook       Current Patch / In-memory facade
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
`tests/test_workbook_editor_*.cpp` shards，CTest family `fastxlsx.workbook_editor.*`）：已覆盖
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

`WorksheetEditor::set_cells()` is the full-cell sparse batch counterpart to
`set_cell()`: it preflights the whole batch, accepts duplicate coordinates with
later-wins ordering, rejects caller-supplied non-default `StyleId` handles, drops
prior source style handles on overwritten cells, and rejects guardrail failures
before mutating the active sparse store.

`WorksheetEditor::set_row()` and `WorksheetEditor::set_column()` are the
represented-row / represented-column full-cell replacements. They remove the
currently represented sparse records in that row or column, write the input
prefix from A / row 1, reject caller-supplied non-default `StyleId` handles, drop
prior source style handles on overwritten target records, and leave non-target
sparse cells and style handles unchanged.

`WorksheetEditor::append_row()` inserts a new represented sparse row after the
current maximum represented row. Appended cells are new full cells, reject
caller-supplied non-default `StyleId` handles, and do not inherit source
`StyleId` handles from existing rows; existing source cells and the preserved
source styles part remain unchanged.

`WorksheetEditor` value-only APIs (`set_cell_value()`、`set_cell_values()`、
`set_row_values()`、`set_column_values()`) keep the target cell's currently
materialized source `StyleId` when overwriting an existing cell, reject
caller-supplied non-default `StyleId` handles, and insert missing cells without a
style. `set_cell_values()` preflights the whole batch, accepts duplicate
coordinates, and applies later-wins ordering after validation.

`WorksheetEditor` row/column shift helpers (`insert_rows()`、`delete_rows()`、
`insert_columns()`、`delete_columns()`) are represented sparse-store transforms.
They keep shifted `CellValue` payloads and materialized source `StyleId` handles,
translate supported references in moved formula cells, and apply the same narrow
structural rewrite to stationary formula cells already in the materialized store.
They are not Excel semantic row/column operations: they do not synchronize
tables, filters, validations, conditional formatting, drawings, defined names,
relationships, sharedStrings/styles metadata, or calcChain.

### 当前能力事实源

当前 public / internal / planned / non-goal 状态矩阵统一维护在
[CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md)。本文件只保留 API 设计原则、Doxygen 要求、
模式边界和功能设计门；不要继续在这里追加历史 coverage addendum 作为“当前能力矩阵”。

新增或修改 API 时，先对照当前事实源确认它属于以下哪一类：

- `Public API`：已经进入 public header、实现、测试和文档注释的能力。
- `Internal Foundation`：可以作为实现证据或审计说明，但不能写成 stable public API。
- `Planned / Not Yet Public`：只能作为设计目标或后续 gate，不能暗示用户现在可用。
- `Explicit Non-goals`：文档、README、Doxygen 和任务说明都不能反向承诺。

尤其要保持这条边界：`WorkbookEditor` / `WorksheetEditor` 是当前 public existing-workbook facade；
internal `PackageEditor`、`EditPlan`、`DependencyAnalyzer` 和 `RelationshipGraph` 只用于解释 Patch 底座，
不得包装成 public package-editing API。

### 当前 API / 功能设计门

下一轮功能推进先过设计门，再进入代码。每个 public API 或功能切片必须先回答：

- 所属 facade：`WorkbookWriter`、`Workbook`、当前 `WorkbookEditor`，还是当前
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

当前 `Cell` / `CellView` 都没有专用 date cell 类型；日期/时间单元格仍写成 numeric cell。
Streaming public helper 可以按 Excel 1900 date system 计算 date/time serial，并明确保留
serial 60 的 1900 leap-year compatibility gap；它不支持 1904 date system，不做 timezone
推断，也不创建样式或 workbook metadata。当前已有 streaming-only 自定义 number format
styles 和常用 date/time number-format presets，但 number format 只控制显示格式，不编码
date cell type。不要把 `DataValidationType::Date` 误写成 date cell encoding 已实现。

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
还会同步已经 materialized 的 WorksheetEditor formula cells。两者仍不是公式求值器、
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

当前 Patch facade 的第一个 existing-file 核心路径是 internal by-name worksheet `<sheetData>` patch：
当前内部 by-name chunk-source helper 接受 caller 提供的
`<sheetData>` / `<sheetData/>` chunk source，做 bounded local rewrite，并沿用现有
calcChain remove / `fullCalcOnLoad`、relationship/content-type audit 和
unknown/unmodified part preservation 路径。这个 helper 已有首个 public facade：
`WorkbookEditor`（`include/fastxlsx/workbook_editor.hpp` / `src/workbook_editor.cpp` /
`tests/test_workbook_editor_*.cpp` shards，CTest family `fastxlsx.workbook_editor.*`）。public facade 把
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
- 对应 `docs/TASK_BREAKDOWN.md` active queue 任务编号，并标明能力分类：
  public API、internal foundation、planned / not yet public 或 explicit non-goal。
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
  不能写成完整 workbook metadata editor、relationship repair 子系统或 robust XML parser。
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
