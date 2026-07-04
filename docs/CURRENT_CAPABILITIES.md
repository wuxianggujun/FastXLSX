# 当前能力事实源

本文是 FastXLSX 当前能力的唯一事实源。`README.md`、`AGENTS.md` 和 API 设计文档只保留入口摘要，
详细 public / internal / planned / non-goal 边界应链接到这里，避免同一事实在多个长文档里漂移。

## Public API

### Small New Workbook

- 当前入口：`Workbook`、`Worksheet`、`Cell`、`CellValue`、`CellRange`、`RowOptions`、
  `DocumentProperties`、`Workbook::create()`、`add_worksheet()`、`save()`、
  `worksheet_count()`、`worksheet_names()`、`has_worksheet()`、`worksheet()`、
  `try_worksheet()`、`rename_worksheet()`、`remove_worksheet()`、`cell_count()` 和
  `estimated_memory_usage()`。
- 适用边界：小文件 / buffered new-workbook creation。`rename_worksheet()` 和
  `remove_worksheet()` 只修改待生成 workbook 中的 sheet buffer，不编辑已有 XLSX。
- 诊断边界：`cell_count()` 和 `estimated_memory_usage()` 是 buffered creation path 的近似观测值，
  不是进程 RSS、硬内存预算、save-time peak 或 large-export progress API。

### Streaming New Workbook Writer

- 当前入口：`WorkbookWriter`、`WorksheetWriter`、`CellView`、`WorkbookWriterOptions`、
  `StringStrategy`、`StyleId`、`CellStyle`、`CellAlignment`、`CellFont`、`CellFill`、
  `DocumentProperties`、worksheet metadata 值类型和 image 值类型。
- 当前能力：row/cell 顺序写入、数字 / 文本 / 布尔 / 公式单元格、sharedStrings 显式策略、
  document properties、行高、列宽、冻结窗格、自动筛选、合并单元格、data validations、
  external/internal hyperlinks、streaming-only tables、two-/three-color color scales、
  basic data bars、basic `3Arrows` icon sets、基础 number format / alignment / font / fill styles、
  PNG/JPEG 图片插入和 minizip-ng opt-in DEFLATE 输出。
- 适用边界：新建 workbook / 大数据导出主线。`WorksheetWriter` 不支持随机回写历史行，
  convenience API 不能引入完整 worksheet DOM、cell matrix 或跨行热路径状态。

### Existing Workbook Patch Facade

- 当前入口：`WorkbookEditor::open()`、source/planned catalog inspection、
  `WorkbookEditorOptions`、`WorkbookEditorWorksheetCatalogEntry`、
  `WorkbookEditorWorksheetEditSummary`、`CellPatchMissingCellPolicy`、
  `replace_sheet_data()`、`replace_cells()`、`replace_image()`、`rename_sheet()`、
  formula audit / defined-name audit 查询、`request_full_calculation()` 和 `save_as()`。
- `replace_sheet_data()`：替换已有 worksheet 的 whole `<sheetData>`，保留 worksheet 外围 XML，
  并走 Patch 路径处理 calcChain cleanup、fullCalcOnLoad 和 preservation/audit。
- `replace_cells()`：定点替换已有 cells；显式
  `CellPatchMissingCellPolicy::Insert` 只做 point upsert，可插入缺失 cells 或合成 minimal rows。
  它不做 row/column shifting、range/table/filter/drawing/defined-name resize 或 broader metadata sync。
- `replace_image()`：替换已有 PNG/JPEG `xl/media/*` part bytes。它不编辑 drawing XML、anchors、
  relationships、content types、cell text，也不是 existing-file image insertion。
- `rename_sheet()`：当前是已有 workbook catalog 改名 facade；默认不重写公式。可选 formula policy
  只限当前已实现的 direct definedNames 和已 materialized `WorksheetEditor` formula cells 边界，
  不是公式求值器或全 workbook 语义 rename。
- `request_full_calculation()`：请求 workbook 打开后重算，并清理 stale calcChain metadata。
  它不计算公式、不写 cached values、不 rebuild `calcChain.xml`。
- `save_as()`：已有文件编辑输出到新路径。当前不支持 atomic in-place save。
- Public 边界：调用方不需要也不应该依赖 internal `PackageEditor`、`EditPlan`、`DependencyAnalyzer`、
  `RelationshipGraph` 或 package-entry reason。

### Existing Workbook In-memory Worksheet Editor

- 当前入口：`WorksheetEditorOptions`、`WorkbookEditor::worksheet()`、
  `WorkbookEditor::try_worksheet()`、`WorksheetEditor`、`WorksheetCellReference`、
  `WorksheetCellUpdate`、`WorksheetCellSnapshot`。
- 当前能力：`try_cell()` / `get_cell()` / `contains_cell()` / `used_range()`、
  `sparse_cells()` / `row_cells()` / `column_cells()` owning snapshots、
  `set_cell()` / `set_cells()` full sparse replacements、`append_row()`、
  `set_row()` / `set_column()` represented sparse row/column replacement、
  `set_cell_value()` / `set_cell_values()` / `set_row_values()` /
  `set_column_values()` value-only writes that preserve existing materialized StyleId handles,
  `clear_cell_value()` / `clear_cell_values()` / `clear_row()` /
  `clear_rows()` / `clear_column()` / `clear_columns()` value clears、
  `erase_cell()` / `erase_cells()` / `erase_row()` / `erase_rows()` /
  `erase_column()` / `erase_columns()` sparse record removal、
  `insert_rows()` / `delete_rows()` / `insert_columns()` / `delete_columns()`
  represented sparse row/column shifts、
  strict uppercase A1 convenience overloads、
  `cell_count()`、`estimated_memory_usage()` 和 dirty-session `save_as()` auto-flush。
- `set_cells()` and `set_cell_values()` batch inputs are preflighted, allow duplicate
  coordinates, and apply later-wins ordering. `set_cells()` is full sparse replacement and
  drops prior source style handles on overwritten cells; `set_cell_values()` is value-only and
  preserves the target's existing materialized source style handle. Guardrail checks reject the
  whole batch before mutation, and duplicate missing coordinates count as one final sparse target.
- Row/column shift helpers only move or delete represented sparse records. Shifted records keep
  their `CellValue` payloads and materialized source `StyleId` handles; moved formula cells
  translate supported A1-style references, and stationary formula cells already in the
  materialized store use the same narrow structural rewrite for affected references. These helpers
  do not synchronize tables, filters, validations, conditional formatting, drawings, defined names,
  relationships, sharedStrings/styles metadata, or calcChain.
- Guardrail：`WorksheetEditorOptions::max_cells` 和 `memory_budget_bytes` 约束 source materialization
  与后续 sparse-store mutations。它们是 sparse-store estimate guardrails，不是进程 RSS 或 package save peak。
- 适用边界：small-file random cell editing。该路径不支持 non-default caller-supplied style id 写入、
  sharedStrings broad migration、style migration/merge、formula evaluation、full formula rewrite、
  rich-text preservation、namespace repair、semantic metadata sync、relationship repair、
  transaction history、dense matrix access 或 large-file low-memory random editing。

### Image Helpers

- 当前入口：`ImageFormat`、`ImageInfo`、`ImagePixels`、`read_image_info()`、`read_image_pixels()`、
  `ImageOptions`、`ImageAnchorOffset`、`ImageEditAs` 和 `WorksheetWriter::add_image()`。
- `read_image_info()` 只读取 PNG/JPEG 格式、尺寸和通道数。
- `read_image_pixels()` 会解码 PNG/JPEG 并分配 caller-owned decoded pixel buffer；它不创建 media part、
  drawing XML、relationships、content types 或 anchors。
- `WorksheetWriter::add_image()` 是 streaming-only new-workbook 图片插入 API，会复制原始 PNG/JPEG bytes
  到 file-backed media entry，并写出 media/drawing parts、drawing `.rels`、worksheet `.rels`、
  worksheet `<drawing>` 和 content types。它不裁剪、旋转、压缩、格式转换或编辑已有 drawing。

## Internal Foundation

- OPC / ZIP：`PackageWriter`、`PackageReader`、content types、relationships、`PartIndex` 和
  `RelationshipGraph` 是 internal package foundation。
- Patch：`PackageEditor`、`EditPlan`、`DependencyAnalyzer`、`ReferencePolicy`、planned output、
  relationship target audits、payload dependency audits 和 preservation fixtures 是 internal Patch foundation。
- Worksheet rewrite：worksheet event reader、transformer action model、cell replacement planning、
  chunked/file-backed handoff 和 staged payload helpers 是 internal implementation evidence。
- In-memory：`CellStore`、`CellRecord`、worksheet-local sparse store、guardrails 和 sheetData/full worksheet
  projection helpers 是 `WorksheetEditor` 的 internal foundation。
- 这些 internal 类型可以被文档用于解释实现边界和审计证据，但不能写成 stable public API。

## Planned / Not Yet Public

这些能力不得当作当前能力；只有经过新的 public design gate、header、实现、测试和文档注释后才能升级：

- 大 worksheet 低内存 public transformer / rewrite API。
- broader existing-file semantic object editing：tables、drawings、charts、comments、VBA、custom XML、
  pivot/external links、worksheet relationships 的语义级增删改。
- existing-file worksheet add/delete、完整 semantic sheet rename、跨 workbook metadata 同步。
- sharedStrings / styles broad migration、merge、repair 和 schema count repair。
- full relationship repair/pruning、orphan cleanup、linked-part regeneration。
- 完整公式 parser/evaluator、cached values、完整 calcChain rebuild。
- full image/drawing editing、one-cell/absolute anchors、row/column resize geometry calculation。
- broad namespace validation/repair、full XML schema validation 或 malformed XML repair。

## Explicit Non-goals

- 不把大型 worksheet 隐式装入完整 DOM、dense cell matrix 或长期 cell map。
- 不把 `Workbook` 小文件 creation path 当作 existing-file editing API。
- 不公开 public `PackageEditor`、public `EditPlan` 或 public package-entry mutation API。
- 不支持 atomic in-place save；已有文件编辑使用 `save_as()` 输出新路径。
- 不计算 Excel 公式、不生成 cached values、不承诺完整 calcChain rebuild。
- 不承诺 broad relationship repair、relationship pruning、orphan cleanup 或 semantic linked-object sync。
- 不在没有 benchmark 证据时宣称“高性能”“低内存”或“生产级大文件编辑”。

## Performance Boundary

- Streaming writer 是大文件导出主线；任何 convenience API 都不能破坏 row/chunk 热路径。
- Patch / In-memory 能力必须明确说明是否物化 source worksheet、replacement payload、staged chunks 或 sparse store。
- file-backed / chunked internal handoff 只能说明实现边界，不等同于 public low-memory performance claim。
- 性能结论必须来自 opt-in benchmark 或 QA 证据，并写清 dataset、compression/backend、string strategy、
  source mode、rewrite strategy、wall time、peak memory / estimate、输出大小和 Excel/openpyxl 打开结果。
