# 路线图

## Phase 0：项目初始化

- 建立项目名称、定位和文档。
- 固定流式主线与 DOM 编辑边界。
- 明确性能目标。
- 固定 `OpenXLSX`、`xlnt`、`FastExcel` 的参考边界。
- 固定第一阶段依赖边界。

## Phase 1：最小可写 XLSX

目标：生成 Excel 可打开的 `.xlsx`。

功能：

- workbook。
- worksheet。
- content types。
- relationships。
- 基础 sheetData。
- 数字、字符串、布尔值。
- ZIP package 输出。

当前 package writer 边界：

- 默认无依赖构建使用内部 stored ZIP writer。
- `FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建使用 `minizip-ng[core,zlib]` DEFLATE backend。
- 无 Zip64。
- 无真实 package streaming。
- `zlib-ng` / `pugixml` 尚未被当前源码使用。

Phase 1 后续依赖工作：

- minizip-ng backend 默认化前的 CI/cache/release packaging 验证。
- 压缩等级配置、Zip64 策略和真正 package streaming。
- `pugixml` 用于小型 XML part 编辑能力。

验收：

- Excel / WPS / LibreOffice 可打开。
- 生成文件结构符合 OpenXML 基本要求。
- 本机有 Excel 时，必须用 Excel 打开关键样例做可视化验证。
- 结构异常时，必须拆包对比 FastXLSX 输出与 Excel 或 Python XLSX 库生成的参考文件。
- public API 需要有文档注释，并写明该 API 是否属于 streaming 路径。

## Phase 2：高性能流式写入

目标：建立比 `OpenXLSX` DOM 主路径更强的大数据写入能力。

功能：

- Row iterator API。
- 流式 worksheet.xml。
- `Expat` 接入大型 XML 流式读取。
- inlineStr / sharedStrings 策略。
- 数字和日期快速编码。
- 压缩等级配置。
- 性能基准。
- 对比 `OpenXLSX`、`xlnt` streaming writer 和旧 `FastExcel`。

验收：

- 1,000 万 cells 写入稳定。
- 内存占用可控。
- API 设计必须与性能目标对齐，不能为了易用性迫使大数据路径持有完整 worksheet。
- 性能测试必须记录数据规模、压缩等级、字符串策略、耗时、峰值内存和输出文件大小。

## Phase 3：OpenXLSX 高频功能

功能：

- 样式。
- 列宽、行高。
- 合并单元格。
- 冻结窗格。
- 自动筛选。
- 公式写入。
- 文档属性。
- 命名区域。

约束：

- API 体验可以参考 `OpenXLSX`，但实现不能继承 `OpenXLSX` 的完整 worksheet DOM 主路径。
- 便利 API 必须写明适用范围；只适合小文件的 API 需要标记为 in-memory 路径。
- public API 必须补文档注释，说明模式、内存行为、随机访问限制和性能注意事项。

## Phase 4：编辑已有 XLSX

当前状态提示：本 roadmap 描述目标能力，不等同当前实现。截至当前任务计划，
`PartIndex` / `RelationshipGraph` 属于 internal groundwork；`PackageReader`、
existing-file editing、unknown part preservation 仍是计划。

功能：

- PackageReader。
- PartIndex。
- 未修改 part 原样复制。
- 小型 XML part 局部 DOM 编辑。
- 大型 worksheet 流式替换。
- 模板填充。

## Phase 5：复杂对象

当前状态提示：`stb` 已通过 opt-in `FASTXLSX_ENABLE_STB=ON` /
`planned-image` 接入 PNG/JPEG `read_image_info()` 元数据 helper。当前还存在
`WorksheetWriter::add_image(path, anchor)` 的 streaming-only new-workbook
PNG/JPEG 图片插入基础切片，会写 media/drawing parts、drawing rels、
worksheet rels、worksheet `<drawing>` 和 content types。它不代表图片编辑、
existing-workbook 图片保真、drawing mutation、裁剪、旋转、压缩或格式转换。
图片、超链接、table、chart/VBA passthrough 不能仅凭本 roadmap 条目宣称支持；
以 `TASK_PLAN.md`、`NEXT_STEPS.md`、`AGENTS.md` 的 current verified state 为准。

功能：

- 图片读取和插入；图片解码/尺寸读取使用 `stb`，OpenXML media/drawing
  package 逻辑由 FastXLSX 自己实现。
- 超链接。
- 数据验证。
- 条件格式。
- table。
- 图表透传。
- VBA 透传。

## 长期目标

FastXLSX 应该成为：

```text
一个流式优先、编辑能力可靠、性能明显优于 OpenXLSX DOM 主路径，
并在核心流式路径上对标 xlnt streaming API 的 C++ XLSX 引擎。
```

## 持续性要求

- 测试流程见 [测试流程](TESTING_WORKFLOW.md)。
- API 设计和文档注释要求见 [API 设计与文档注释](API_DESIGN_AND_DOCUMENTATION.md)。
- 任何任务计划都必须说明是否触碰性能热路径，以及需要的结构测试、Excel 可视化验证、
  拆包 XML 对比或 benchmark。
