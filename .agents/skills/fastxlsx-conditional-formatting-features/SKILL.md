---
name: fastxlsx-conditional-formatting-features
description: "实现或审查 FastXLSX streaming conditional formatting。用于 two-/three-color color scales、basic data bars、basic 3Arrows icon sets、priority 共享、multi-range sqref、本地 Excel/openpyxl/XlsxWriter 验证，以及判断 advanced/custom icon sets、advanced data bars、dxf/styles、formula/cellIs 是否越界。"
---

# FastXLSX Conditional Formatting Features

## 当前窄切片

以 public header 和 tests 为准，当前重点是 Streaming two/three-color scales、basic data bars、basic `3Arrows` icon sets、priority 和 multi-range `sqref`。

## 非目标

- Advanced/custom icon sets。
- Advanced data bar options。
- 通用 `dxf`/styles 对象模型。
- `formula` / `cellIs` 等完整规则族。
- Existing-workbook semantic editing。

## 实现规则

- 校验 range、阈值、颜色、priority 和 feature-specific enum。
- 保持 worksheet XML schema 顺序。
- 状态随规则/range 数增长，不读取或缓存完整 cell 数据。
- 与 styles/sharedStrings/metadata 共存时验证 suffix ordering。

## 验证

Feature-specific CTest、XML structure、invalid input/no-state-pollution，以及现有 openpyxl/XlsxWriter/Excel QA scripts。验证通过只能支持当前窄切片 wording。