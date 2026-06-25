---
name: fastxlsx-streaming-worksheet
description: "开发或审查 FastXLSX 流式 worksheet 路径。用于 row/cell writer、worksheet reader、worksheet rewriter、模板 sheet 替换、dimension tracking、XML escape、cell reference 编码、inlineStr/sharedStrings 策略、大文件导出，以及 worksheet 处理中的内存/性能问题。"
---

# FastXLSX Streaming Worksheet

## 必读文件

- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`
- `docs/PERFORMANCE_TARGETS.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/TECHNICAL_COMPARISON.md`
- `README.md`

然后检查 `include/`、`src/` 和测试，确认哪些已实现。当前已有
`WorkbookWriter`、`WorksheetWriter`、`CellView` 流式写入骨架；文档中的其他
模块名仍可能只是设计目标。
当前可见 `StringStrategy::SharedString`、内部 `SharedStringTable`、
`xl/sharedStrings.xml` 生成路径和 focused 结构测试。把这条线写为
sharedStrings 进行中或基础，不要写成生产级字符串策略完成。
当前可见 file-backed/chunked worksheet package entry 基础：worksheet part
finalization 以 header + temporary body file + footer 写入内部 package writer，
避免 close 阶段重新物化完整 worksheet XML。sharedStrings XML 也可通过临时
file-backed entry 写入，但 shared string table 仍保留唯一字符串状态。只能写成
worksheet/package-entry 缓冲优化，不要写成 true package streaming、Zip64、完整低内存
package writer 或生产级大文件性能完成。
当前还可见 internal `PackageEntryChunk` byte-range emitter，可从 memory/file staged
chunks 中按 offset/size 输出片段，作为 PackageEditor indexed rewrite 的底层前置能力；
当前还有 internal PackageEditor chunk-backed indexed strict-replace slicer prototype，
可用 staged chunks + prebuilt index 拼接 existing-cell replacement payload。它不是
source ZIP entry seek、不是默认 Patch 算法切换，也不是完整低内存随机编辑。

## 不可破坏的边界

- 大型 `worksheet.xml` 禁止 DOM。
- 大型 `sharedStrings.xml` 禁止 DOM。
- 批量写入路径禁止持有完整 worksheet cell matrix。
- 大型模板填充必须 event/stream 化。
- 大数据 API 必须接受 row iterator 或 chunk writer。
- 单元格 XML 热路径应直接写字节流，而不是通用 XML serializer。
- API 易用性不能迫使 large worksheet 进入 DOM、完整 cell matrix 或 cell map。
- Streaming worksheet 是大文件写入/重写主线，不是完整编辑架构本身；已有文件编辑应走
  Patch / EditPlan / part-level rewrite，小文件随机编辑应走 In-memory 路径。
- 大型 worksheet package entry finalization 禁止重新物化完整 worksheet XML。
- file-backed/chunked entry 只覆盖 package entry source，不改变 row-order
  streaming、字符串策略状态或 ZIP backend 限制。
- 不要让 `src/streaming_writer.cpp` 无限膨胀成所有 worksheet feature 的实现堆场；
  它应优先保留流程编排、生命周期管理、热路径调用和跨功能协调。
- 不要让 `tests/test_streaming_writer.cpp` 承载所有 streaming feature 的细粒度结构测试；
  功能边界明确且测试规模继续增长时，应拆成 feature-specific 测试文件。

## 设计锚点

当前已存在的写入骨架：

- `WorkbookWriter`
- `WorksheetWriter`
- `CellView`

当前骨架已覆盖公式、行高、列宽、冻结窗格、自动筛选和合并单元格的 XML 输出；
`fastxlsx.streaming` 已有 focused Phase 3 metadata 结构测试，覆盖公式 XML escape、
row height、多个 column width records、last-call-wins frozen panes、
last-call-wins auto filters、多个 merged ranges、suffix ordering 和无额外
relationship/content type side effects；公式 cell 会在 workbook XML 写
`<calcPr calcId="124519" fullCalcOnLoad="1"/>` 请求打开后重算，但仍不生成
`xl/calcChain.xml`。本机 Excel COM 已验证
`build/windows-nmake-release/tests/fastxlsx-streaming-phase3-metadata.xlsx` 可打开并显示
公式、行高、列宽、自动筛选、合并区域和冻结窗格；当前固定本地 QA 入口是
`tools/verify_phase3_metadata.py` 和 `tools/verify_phase3_metadata_excel.ps1`。
这些仍是写入骨架能力，不等同
公式计算、cached values、calcChain、styles 或完整 Phase 3。

当前 two-/three-color conditional color scale、basic data bar 和 basic 3Arrows icon set 基础：
- `WorksheetWriter::add_conditional_color_scale()` 是 streaming-only new-workbook
  worksheet metadata API。
- `WorksheetWriter::add_conditional_data_bar()` 是 streaming-only new-workbook
  worksheet metadata API。
- `WorksheetWriter::add_conditional_icon_set()` 是 streaming-only new-workbook
  worksheet metadata API，当前只写 built-in `3Arrows`。
- `ArgbColor` 写成 8 位大写 ARGB；`ColorScaleValueType` 支持 min/max/num/percent/
  percentile，num/percent/percentile endpoint 要求有限值。
- 规则写成 worksheet-local `<conditionalFormatting><cfRule type="colorScale">`，
  priority 按同一 worksheet 内调用顺序分配，multi-range 写成一个空格分隔 `sqref`。
- data bar 写成 worksheet-local `<conditionalFormatting><cfRule type="dataBar">`，
  写两个 `<cfvo>`、一个 inline ARGB `<color>`，以及可选 `showValue="0"`；priority 与
  color scale 共享同一 worksheet-local 调用顺序。
- icon set 写成 worksheet-local `<conditionalFormatting><cfRule type="iconSet">`，
  写三个 finite 严格递增的 `<cfvo>` threshold；priority 与 color scale / data bar
  共享同一 worksheet-local 调用顺序。
- 它不生成 `styles.xml`、`dxfs`、worksheet `.rels`、content type、workbook
  relationships、cell text 或 `<calcPr>`，也不读取历史 row/cell 或持有完整 worksheet
  matrix。
- 这不是 formula/cellIs、advanced data bars、advanced/custom icon sets、dxf-backed styles、existing-file
  editing 或完整 conditional formatting。

当前 sharedStrings 基础：

- `StringStrategy::InlineString` 仍是低内存默认路径。
- `StringStrategy::SharedString` 是显式性能/体积策略入口。
- 当前可见内部共享字符串表、`xl/sharedStrings.xml` part、content type、
  workbook relationship、worksheet `t="s"` 引用和 XML escape 结构测试。
- 当前还覆盖 `SharedString` 模式下没有字符串 cell 的空表边界：不要生成空
  `xl/sharedStrings.xml`、sharedStrings content type、workbook relationship、
  `t="s"` 或 `inlineStr`；公式 cell 仍要请求 workbook recalculation metadata。
- 当前空表边界也有本地 QA helper：`tools/verify_shared_strings_absence.py`
  负责 ZIP / `openpyxl` / 可选 `XlsxWriter` 检查，
  `tools/verify_shared_strings_absence_excel.ps1` 负责本机 Excel COM 只读打开。
- 当前已有默认 CTest 结构覆盖、`tools/verify_shared_strings_excel.ps1` 本机 Excel
  COM 只读检查，以及 `tools/verify_shared_strings_reference.py` 的 `openpyxl`
  参考语义检查；本机 `py` 当前也能创建 `XlsxWriter` 参考，缺少模块的 Python 环境会记录为
  可选跳过。
- 继续写成 进行中，直到更大规模大小/内存数据和更多参考兼容性数据补齐。

当前 P9a number-format styles 基础：
- `StyleId` 是 workbook-local handle，默认 id `0` 表示 default style。
- `CellStyle` 当前只支持 `number_format`，`WorkbookWriter::add_style()` 注册 workbook
  级样式并按 format 字符串精确去重。
- `CellView::with_style()` 只携带小 style handle；row/cell streaming 仍保持 append-only，
  不持有完整 worksheet cell matrix。
- `WorksheetWriter::append_row()` 必须在推进 row count、dimension、sharedStrings
  state 或 formula recalculation metadata 前拒绝无效或 foreign `StyleId`。
- 非默认 style cell 写 `s="N"`，默认 style 不写 `s="0"`；`close()` 只在注册非默认
  styles 时写 `xl/styles.xml`、workbook relationship 和 content type override，不创建
  worksheet `.rels`。
- 这不是 font/fill/border/alignment、date cell type、dxf-backed conditional formatting、
  rich text 或 existing-file style preservation；当前 two-/three-color color scale 和 basic
  data bar 是 worksheet metadata，不是 styles registry 或 `dxfs` 支持。

这些仍主要是文档中的设计名，使用前先确认源码是否存在：

- `RowStreamWriter`
- `RowStreamReader`
- `WorksheetRewriter`
- `CellEncoder`
- `DimensionTracker`
- `FastXmlWriter`
- `InlineStringPolicy`
- `StringEscaper`
- `StyleRegistry`

## 设计流程

1. 判断任务属于新建 XLSX 流式写入、大 worksheet 读取，还是已有 sheet 重写。
2. 保持 row-oriented 处理，避免大文件随机访问 cell map。
3. 用专门的 writer 处理 XML 热路径。
4. 明确字符串策略：
   - `inlineStr` 优先低内存。
   - `sharedStrings` 在去重收益值得维护状态时优先文件体积。
5. 增量维护 dimensions，不要回头扫描全部 cells。
6. 样式 ID 和 registry 与原始 row/cell streaming 分离。
7. 当 conditional formatting、data validations、hyperlinks、tables、images、
   styles 或 sharedStrings 已有独立 XML 序列化/状态/QA helper 时，优先拆到独立
   helper 或 `.cpp`；少量协调代码和边界尚不稳定的小切片不要强拆。
8. 新增 `.cpp` 或测试文件时，同步更新 `CMakeLists.txt` / `tests/CMakeLists.txt`。

当前 dimension 行为是 append-order based：未 append 行或只 append 空行时
`worksheet_dimension()` 写 `A1`；空行仍写 `<row r="N"></row>`；存在前导空行、数据行和
尾部空行时，dimension 使用最后 append 行号和最大列号，例如 `A1:C3`。本机 Excel 可能把
`UsedRange` 收缩到实际非空单元格；后续结构判断以拆包后的 worksheet XML 为准，不要为了
匹配 `UsedRange` 引入完整 worksheet cell matrix 或回扫。
当前 streaming 边界结构测试还锁定合法最大列 `XFD1`、test-only hook 下合法最大行
`1048576` 的稀疏输出，以及失败 `append_row()` 不推进 row number / dimension、
不写入被拒绝 cells、不创建无用 sharedStrings part、不污染 workbook `<calcPr>`。
这些是结构和状态卫生测试，不是宽表、百万行或低内存性能 benchmark。

当前行上限结构测试通过 `FASTXLSX_ENABLE_TEST_HOOKS` 和
`fastxlsx::detail::testing_set_worksheet_row_count()` 注入内部 `row_count` 到 Excel
最大行数，再验证下一次 `append_row()` 拒绝。这个 hook 只属于测试构建；不要把它用于
功能代码、public API 设计或性能结论。

## 已有 XLSX 的 sheet 重写

大型 sheet 修改使用文档中的流程：

```text
old sheet.xml event reader
-> row/cell transformer
-> new sheet.xml stream writer
```

适用场景：定向单元格修改、追加行、删除/替换行、模板占位符替换、
dimension 更新。

## 性能关注点

优先围绕这些点写测试和 benchmark：

- 单元格 XML 编码。
- XML 字符转义。
- 数字转字符串。
- 单元格引用生成。
- ZIP 压缩等级。
- `sharedStrings` 去重。
- 行级 buffer 复用。
- worksheet package entry source：in-memory、file-backed 或 chunked。
- close-time package assembly 的峰值内存。
- 临时 worksheet part 文件大小、生命周期和清理行为。

文档目标包括 1,000 万 cells 内存 `< 256 MB`、5,000 万 cells 内存 `< 1 GB`。
这些是目标，不是当前已验证事实。
当前手工 benchmark JSON schema v3 会记录 `string_pattern`、
`package_entry_source_mode="worksheet-file-backed-chunked"`、
`temporary_worksheet_part_footprint="worksheet-body-file-bytes"` 和数值型
`temporary_worksheet_part_footprint_bytes`。该值只累计 worksheet body row XML 写入字节；
不包含 worksheet header/footer、sharedStrings 临时文件、小型 XML parts、media 文件、
ZIP/backend 缓冲、package assembly 峰值内存或 OS 文件系统开销，不能据此宣称完整低内存。

## 禁止事项

- 不要在大型 worksheet 路径使用 `pugixml`。
- 不要复制 `OpenXLSX` 的 worksheet DOM 模型。
- 不要复制 `xlnt` 常规 API 的完整 workbook/cell-map 路径作为大数据主线。
- 不要硬编码压缩等级。
- 触及字符串模型时，不要只实现一种策略而忽略文档中的双策略。
- 不要为了让 API 更像普通 workbook 编辑器而牺牲流式性能主线。

## 验证

- 为 XML escape、cell reference、值编码、dimensions、字符串策略补单元测试。
- 触及公式、行高、列宽、冻结窗格、自动筛选或合并单元格时，保持 Phase 3
  metadata 结构测试和 Excel 可视化样例同步；公式 cell 可以请求 fullCalcOnLoad
  重算，但不要把 write-only 公式扩展成公式计算、cached values 或 calcChain 保证。
- sharedStrings 进行中时，验证 `xl/sharedStrings.xml`、content type override、
  workbook relationship、worksheet shared-string index、`count` / `uniqueCount`、
  XML escape、`xml:space="preserve"`、exact occurrence counts、跨 worksheet 去重、
  newline / carriage-return preserve，以及 ZIP duplicate entry 检测。
- 还要覆盖没有字符串 cell 的 `SharedString` 模式，确认不会写死的 sharedStrings
  part、content type、relationship、`t="s"` 或 `inlineStr`。
- 对 streaming hot-path 边界，覆盖合法最大列 `XFD1`、合法最大行 `1048576` 的
  test-only 稀疏输出、过宽行拒绝、行上限拒绝，以及失败 `append_row()` 不污染
  后续行号、dimension、sharedStrings 或 formula recalculation metadata。
- 对当前 sharedStrings smoke 样例，运行 `tools/verify_shared_strings_reference.py`
  做 `openpyxl` 参考语义检查，运行 `tools/verify_shared_strings_excel.ps1` 做本机
  Excel COM 只读可视化检查。不要把这些本地 QA 工具接入默认 CTest 或运行时依赖。
- 对当前 sharedStrings 空表样例，运行
  `tools/verify_shared_strings_absence.py` 和
  `tools/verify_shared_strings_absence_excel.ps1`。这些同样是本地 QA 工具。
- 对当前 number-format styles 样例，运行 `tools/verify_styles_number_formats.py`
  做拆包 XML / `openpyxl` / 可选 `XlsxWriter` 检查，并运行
  `tools/verify_styles_excel.ps1` 做本机 Excel COM 只读 NumberFormat 检查。结构测试还要
  覆盖 `xl/styles.xml`、custom `numFmtId`、worksheet `s="N"`、默认 `s="0"` 省略、
  sharedStrings + styles 共存，以及 foreign `StyleId` 数值碰撞也不污染 row state。
- 对当前 conditional color scale 样例，运行
  `tools/verify_conditional_formatting_color_scales.py` 做拆包 XML / `openpyxl` /
  可选 `XlsxWriter` 检查，并运行
  `tools/verify_conditional_formatting_color_scales_excel.ps1` 做本机 Excel COM 只读
  可视化检查。结构测试还要覆盖 `conditionalFormatting` XML、priority、multi-range
  `sqref`、suffix 顺序、无 styles/dxfs/rels/content type/calcPr 副作用，以及失败
  调用不污染 state。
- 对当前 conditional data bar 样例，运行
  `tools/verify_conditional_formatting_data_bars.py` 做拆包 XML / `openpyxl` /
  可选 `XlsxWriter` 检查，并运行
  `tools/verify_conditional_formatting_data_bars_excel.ps1` 做本机 Excel COM 只读
  可视化检查。结构测试还要覆盖 `<cfRule type="dataBar">`、两个 `<cfvo>`、
  一个 `<color rgb>`、`showValue="0"`、shared priority、multi-range `sqref`、suffix 顺序、无
  styles/dxfs/rels/content type/calcPr 副作用，以及失败调用不污染 state。
- 对当前 conditional icon set 样例，运行
  `tools/verify_conditional_formatting_icon_sets.py` 做拆包 XML / `openpyxl` /
  可选 `XlsxWriter` 检查，并运行
  `tools/verify_conditional_formatting_icon_sets_excel.ps1` 做本机 Excel COM 只读
  可视化检查。结构测试还要覆盖 `<cfRule type="iconSet">`、`3Arrows`、三枚
  `<cfvo>`、Percent / Number / Percentile threshold serialization、
  showValue/reverse metadata、shared priority、multi-range `sqref`、suffix 顺序、
  无 styles/dxfs/rels/content type/calcPr 副作用，以及失败调用不污染 state。
- Benchmark 必须显式 opt-in，不得注册进默认 CTest；普通单元测试继续遵守
  60s 核心测试边界。
- 性能/内存结论必须来自 benchmark。
- file-backed/chunked worksheet entry 实现后，测试应验证解压后的
  `xl/worksheets/sheet*.xml` 语义、duplicate entry 检测、两种 ZIP backend
  可用时的输出一致性，并避免依赖 ZIP method、entry order、compressed size、
  archive size 或 chunk 边界。
- 生成 `.xlsx` 后，在可用时验证 OpenXML 结构和办公软件打开兼容性；结构异常时
  用 Excel / `openpyxl` / `XlsxWriter` 生成参考文件并拆包对比 XML 语义。
- public API 需要文档注释，说明 streaming 模式、输入顺序、内存行为和限制。
