---
name: fastxlsx-worksheet-metadata-features
description: "规划或实现 FastXLSX worksheet metadata 功能。用于 data validations、hyperlinks、conditional formatting、table references、worksheet .rels、当前 worksheet metadata 基础切片，以及判断这些功能是否会破坏 streaming worksheet 热路径或需要 RelationshipGraph。"
---

# FastXLSX Worksheet Metadata Features

## 必读文件

- `docs/CURRENT_CAPABILITIES.md`
- `docs/ARCHITECTURE.md`
- `include/fastxlsx/streaming_writer.hpp`
- metadata-specific source/tests/QA

## 功能边界

Data validations、hyperlinks、conditional formatting、tables、freeze panes、auto filter 等都是独立窄切片。必须逐项确认 public API、worksheet XML 顺序、`.rels`、content types 和 package side effect。

## Streaming 约束

Metadata 可保留与规则数/range 数相关的有界状态，但不能持有 row/cell matrix 或阻塞 row-order hot path。Large formula/object semantic sync 不在 Streaming new-workbook metadata 范围。

## Existing-file 边界

Preserve/audit/fail/edit 必须逐对象定义；不能因为 Streaming 能创建某对象，就宣称 Patch 能编辑已有对象。

## 验证

Feature-specific unit/XML/negative tests，跨 feature 只保留 suffix ordering、relationship id 和 package side effect。必要时运行 openpyxl/XlsxWriter/Excel QA。