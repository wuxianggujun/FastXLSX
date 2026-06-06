---
name: fastxlsx-api-design-docs
description: "设计或审查 FastXLSX public API、API 文档注释、任务计划和性能边界。用于 Workbook/WorksheetWriter/PackageEditor 类接口、Doxygen 注释、Streaming/Patch/In-memory 模式说明、API 易用性与性能取舍，以及防止便利 API 破坏流式主线。"
---

# FastXLSX API Design Docs

## 必读文件

- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`
- `docs/PERFORMANCE_TARGETS.md`
- `docs/ROADMAP.md`
- `README.md`

再检查 `include/` 和 `src/`，确认 API 是否已经实现。当前已实现的 public API
包括 `Workbook`、`Worksheet`、`Cell`、`WorkbookWriter`、`WorksheetWriter`、
`CellView`、`DataValidationRule`、`DataValidationType`、`DataValidationOperator`
和 `FastXlsxError`。`WorkbookWriter` / `WorksheetWriter` / `CellView`
是流式写入骨架；data validation API 是 worksheet XML metadata 基础切片，
不等同完整 Phase 3 或完整 Phase 5。

## 核心原则

API 可以易用，但不能为了易用性牺牲性能主线。

- 大数据写入 API 优先 row iterator / chunk writer。
- 大型 worksheet 不能被 public API 隐式转入 DOM 或完整 cell matrix。
- 便利 API 必须写清适用范围。
- 只适合小文件的 API 应明确标记为 in-memory 路径。
- 性能热路径不能因为高层包装落到通用 XML serializer。
- 当前 `Worksheet::append_row()` 是 append-only、streaming-oriented public API，
  但 Phase 1 实现会临时 buffer rows；不要把这个 buffer 当成长期大文件架构。
- 当前 `WorksheetWriter` 骨架覆盖公式、行高、列宽、冻结窗格、自动筛选、
  合并单元格和 data validations 的写入 XML；这些不代表完整 Phase 3 或
  Phase 5 功能集。
- `WorksheetWriter::add_data_validation()` 是 Streaming metadata API：规则和公式文本
  被复制进 writer state，内存按规则数量和公式文本长度增长；它不解析公式、不校验
  单元格值、不检查重叠、不新增 relationships/content types，也不支持 existing-file editing。
- 当前 `Workbook::save()` 使用 internal package writer boundary；默认无依赖构建走
  stored ZIP bootstrap，`FASTXLSX_ENABLE_MINIZIP_NG=ON` 走 minizip-ng DEFLATE
  backend。两者都不是已有文件编辑 API，也不承诺 Zip64 或 true package streaming。
- OPC/Phase 5 仍是内部 manifest / relationships 基础和规划，不要把
  `PackageEditor`、图片、VBA 或 table 支持写成当前完整能力。

## API 模式

设计或审查 API 时，先标记模式：

- `Streaming`：新建 XLSX、大数据导出、多 sheet 批量写入。
- `Patch`：已有 XLSX 编辑、part-level rewrite、模板替换。
- `In-memory`：小文件复杂编辑，不承诺大文件低内存。

任何 public API 都要说明它属于哪种模式，以及它是否允许随机访问或回写历史行。

## 文档注释要求

public header 中的 API 应有 Doxygen 风格注释，至少说明：

- API 所属模式。
- 是否保留完整 worksheet 状态。
- 输入顺序要求。
- 是否允许随机访问。
- 字符串策略。
- 样式、relationships 或 content types 副作用。
- 错误处理方式。
- 性能/内存注意事项。

data validations 这类 worksheet metadata API 还要写清：Streaming-only、
new-workbook-only、规则数量内存成本、公式文本拷贝、无公式求值、无单元格值校验、
无重叠检查、无完整 Excel UI 保证，以及是否新增 relationships/content types。

涉及热路径的 API，还要说明是否会触发 DOM、跨行缓存、shared strings 状态增长、
压缩等级影响或输出文件大小变化。

## 任务计划要求

规划 API 任务时，任务说明必须写清：

- 所属 Phase。
- API 模式。
- 是否触碰性能热路径。
- 是否需要文档注释。
- 需要哪些单元测试、OpenXML 结构测试、Excel 可视化验证、拆包 XML 对比或 benchmark。
- 是否改变 CMake target 或引入依赖。

如果任务要求“更易用”，必须同时说明为什么不会破坏 streaming 性能主线。

## 禁止事项

- 不要让 `Workbook` 级便利 API 默认持有完整 worksheet。
- 不要让 large worksheet 因 API 简化进入 DOM。
- 不要把 streaming API 做成 DOM API 的附属补丁。
- 不要隐藏压缩等级、字符串策略或 DOM 模式这类性能关键选择。
- 不要用“高性能”“低内存”等模糊描述替代明确边界。

## 验证

- public API 有文档注释。
- 注释写明模式、内存行为、限制和性能注意事项。
- 大数据路径仍然 row/chunk 化。
- 便利 API 不会隐式 DOM 化大型 worksheet。
- 测试计划包含结构验证和必要的 Excel 可视化验证。
- 性能敏感 API 有 benchmark 或明确后续 benchmark 任务。
