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

## 不可破坏的边界

- 大型 `worksheet.xml` 禁止 DOM。
- 大型 `sharedStrings.xml` 禁止 DOM。
- 批量写入路径禁止持有完整 worksheet cell matrix。
- 大型模板填充必须 event/stream 化。
- 大数据 API 必须接受 row iterator 或 chunk writer。
- 单元格 XML 热路径应直接写字节流，而不是通用 XML serializer。
- API 易用性不能迫使 large worksheet 进入 DOM、完整 cell matrix 或 cell map。
- 大型 worksheet package entry finalization 禁止重新物化完整 worksheet XML。
- file-backed/chunked entry 只覆盖 package entry source，不改变 row-order
  streaming、字符串策略状态或 ZIP backend 限制。

## 设计锚点

当前已存在的写入骨架：

- `WorkbookWriter`
- `WorksheetWriter`
- `CellView`

当前骨架已覆盖公式、行高、列宽、冻结窗格、自动筛选和合并单元格的 XML 输出；
`fastxlsx.streaming` 已有 focused Phase 3 metadata 结构测试，覆盖公式 XML escape、
row height、多个 column width records、last-call-wins frozen panes、
last-call-wins auto filters、多个 merged ranges、suffix ordering 和无额外
relationship/content type side effects。本机 Excel COM 已验证
`build/windows-nmake-release/tests/fastxlsx-streaming-phase3-metadata.xlsx` 可打开并显示
公式、行高、列宽、自动筛选、合并区域和冻结窗格。这些仍是写入骨架能力，不等同
公式计算、cached values、calcChain、styles 或完整 Phase 3。

当前 sharedStrings 基础：

- `StringStrategy::InlineString` 仍是低内存默认路径。
- `StringStrategy::SharedString` 是显式性能/体积策略入口。
- 当前可见内部共享字符串表、`xl/sharedStrings.xml` part、content type、
  workbook relationship、worksheet `t="s"` 引用和 XML escape 结构测试。
- 继续写成 进行中，直到默认 CTest、Excel 可视化打开、参考文件拆包 XML 对比
  和大小/内存数据补齐。

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
当前手工 benchmark JSON 会记录 `string_pattern` 和
`package_entry_source_mode="worksheet-file-backed-chunked"`，并把
`temporary_worksheet_part_footprint` 写成 `not_measured`。这说明当前工具还没有
测量临时 worksheet body 文件或 chunk footprint，不能据此宣称完整低内存。

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
  metadata 结构测试和 Excel 可视化样例同步；不要把 write-only 公式扩展成
  公式计算、cached values 或 calcChain 保证。
- sharedStrings 进行中时，验证 `xl/sharedStrings.xml`、content type override、
  workbook relationship、worksheet shared-string index、`count` / `uniqueCount`、
  XML escape、`xml:space="preserve"`、exact occurrence counts、跨 worksheet 去重、
  newline / carriage-return preserve，以及 ZIP duplicate entry 检测。
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
