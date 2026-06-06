---
name: fastxlsx-worksheet-metadata-features
description: "规划或实现 FastXLSX worksheet metadata 功能。用于 data validations、hyperlinks、conditional formatting、table references、worksheet .rels、Phase 5 早期切片，以及判断这些功能是否会破坏 streaming worksheet 热路径或需要 RelationshipGraph。"
---

# FastXLSX Worksheet Metadata Features

## 必读文件

- `include/fastxlsx/streaming_writer.hpp`
- `src/streaming_writer.cpp`
- `tests/test_streaming_writer.cpp`
- `include/fastxlsx/detail/opc.hpp`
- `src/opc.cpp`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`
- `docs/TASK_PLAN.md`
- `docs/TESTING_WORKFLOW.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`

先用 `rg` 确认符号是否真实存在。当前没有完整 Phase 5，也没有
`PackageReader`、生产 `PackageWriter` 或已有文件编辑管线。内部
`PartIndex` / `RelationshipGraph` 已存在，但它们只是 detail groundwork。

## 当前事实

- `WorkbookWriter` / `WorksheetWriter` 是当前 streaming 新建 workbook 路径。
- worksheet 已有 metadata 写出骨架：列宽、冻结窗格、自动筛选、合并单元格。
- 大型 worksheet 路径禁止 DOM，metadata 必须以小向量/轻量结构记录，再在
  worksheet XML 正确位置输出。
- `RelationshipSet` 可表达 external target，但这不是 hyperlink feature。
  真正 hyperlink 还需要 worksheet `<hyperlinks>`、worksheet `.rels` 和关系 id 一致性。
- 图片不是 worksheet-only metadata。图片需要 media part、drawing part、drawing
  relationships、worksheet relationships、content types 和 anchors；`WorksheetWriter`
  最多保存轻量 anchor/reference metadata，package wiring 归 OPC graph/package work。
- `docProps/core.xml` 和 `docProps/app.xml` 是当前基础小型 XML part 输出，
  不代表完整 document-properties API。

## 推荐流程

1. 先判定功能是否只需要 worksheet XML metadata。
2. 若只改 worksheet XML，例如最小 data validations，可以先留在
   streaming new-workbook 路径，不引入 package relationship。
3. 若功能需要 worksheet `.rels`、新 part、content type override 或跨 part id，
   先基于内部 relationship graph / part index 做窄范围 package wiring，不要直接
   宣称完整 public feature。
4. 在 public header 增加 API 时，同步 Doxygen 注释，写清 Streaming 模式、
   顺序写入限制、内存行为、是否新增 relationships/content types。
5. 测试先覆盖 XML 结构，再做本机 Excel 可视化验证；结构异常时创建 Excel、
   `openpyxl` 或 `XlsxWriter` 参考文件，拆包对比 XML。

## 可切入切片

- Data validations：优先做 streaming-only、新建 workbook、worksheet metadata 版本。
  先覆盖简单 whole/decimal/list/date/textLength 结构，禁止已有文件编辑和 DOM。
- Hyperlinks：保持计划，可基于内部 relationship graph 设计 worksheet `.rels`
  wiring，但必须同时写 worksheet XML 和 `.rels` 后才能宣称功能。
  可先做 metadata/API 设计，不要宣称功能已实现。
- Conditional formatting：保持计划；如果只写 worksheet metadata，也必须确认样式、
  公式和 range 依赖不会进入大型 DOM。
- Tables：保持计划；需要 table part、content type override、worksheet rels 和
  worksheet table reference 一起更新。
- Images/Pictures：保持计划；不要把它当成 data validations 那样的纯 worksheet XML
  切片。需要 `fastxlsx-image-media-features` 和 OPC graph/package 边界。

## 禁止事项

- 不要把 Phase 5 写成已实现。
- 不要因为 `RelationshipSet` 能序列化 external target 就宣称 hyperlinks 已支持。
- 不要让 data validation / conditional formatting API 持有完整 worksheet cell matrix。
- 不要在没有 PackageReader/PackageWriter 前宣称 existing XLSX editing 或 unknown part passthrough。
- 不要把 Excel、`openpyxl` 或 `XlsxWriter` 加为运行时依赖；它们只能用于测试和排障参考。

## 验证

- `ctest` 默认测试 60s 内完成。
- worksheet XML 中 metadata 位置符合 OpenXML 结构要求。
- 如果功能新增 relationships，检查 worksheet XML 引用、`.rels` id、content types 同步。
- 本机有 Excel 时打开关键 `.xlsx`，确认无修复弹窗并检查可视化结果。
- 结构失败时拆包对比 FastXLSX 输出、Excel 修复文件和参考文件的 XML 语义。
