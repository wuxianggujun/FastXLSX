---
name: fastxlsx-minimal-writer
description: "实现或审查 FastXLSX 最小可写 XLSX。用于 workbook、worksheet、content types、relationships、基础 sheetData、数字/字符串/布尔单元格、ZIP package 输出、production 默认 minizip-ng/zlib DEFLATE backend 与显式 stored-only profile、小型 XML part 生成、本机 Excel 可视化验证和参考文件拆包 XML 对比。"
---

# FastXLSX Minimal Writer

## 必读文件

- `docs/CURRENT_CAPABILITIES.md`
- `docs/ARCHITECTURE.md`
- `include/fastxlsx/workbook.hpp`
- `src/workbook.cpp`
- `src/package_writer.cpp`
- `tests/test_minimal_xlsx.cpp`

## 路径定位

`Workbook` / `Worksheet` / `Cell` 是 small new-workbook convenience path。它在 `save()` 前缓冲 rows/cells；large export 使用 `WorkbookWriter`。

## 最小 Package

关注 `[Content_Types].xml`、root/workbook relationships、`workbook.xml`、worksheet、styles/sharedStrings（需要时）、docProps 和 ZIP central directory。

## 实现边界

- 数字必须 finite。
- XML escape、sheet name、cell reference 和 relationship target 必须合法。
- production minizip backend 与显式 stored-only profile 保持同一 OpenXML 语义。
- 不把 small `Workbook` 写成 existing-file editor 或低内存大文件路径。

## 验证

Focused unit test、ZIP/XML 拆包、默认/minizip（相关时）、Excel/openpyxl smoke。中文内容按 UTF-8 XML 输出验证。