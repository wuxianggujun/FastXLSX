---
name: fastxlsx-conditional-formatting-features
description: "实现或审查 FastXLSX Streaming/Patch conditional formatting write 与 bounded read。用于 two-/three-color color scales、basic data bars、basic 3Arrows icon sets、priority、multi-range sqref、existing-workbook writer-compatible add、事务/retry、本地 Excel/openpyxl/XlsxWriter 验证，以及判断 advanced/custom icon sets、advanced data bars、dxf/styles、formula/cellIs 是否越界。"
---

# FastXLSX Conditional Formatting Features

## 当前窄切片

以 public header 和 tests 为准，当前覆盖 Streaming write、bounded read 与 existing-workbook Patch add 的 two-/three-color scales、basic data bars、basic `3Arrows` icon sets、priority 和 multi-range `sqref`。Patch public surface 为 `add_conditional_color_scale()`、`add_conditional_data_bar()` 与 `add_conditional_icon_set()`；三类 API 均支持 single range、`span` 和 initializer-list multi-range overload。

## 非目标

- Advanced/custom icon sets。
- Advanced data bar options。
- 通用 `dxf`/styles 对象模型。
- `formula` / `cellIs` 等完整规则族。
- Existing rule update/remove、任意 source takeover 或完整 existing-workbook conditional-formatting object model。

## 实现规则

- 校验 range、阈值、颜色、priority 和 feature-specific enum。
- 保持 worksheet XML schema 顺序。
- 状态随规则/range 数增长，不读取或缓存完整 cell 数据。
- 与 styles/sharedStrings/metadata 共存时验证 suffix ordering。
- Public `WorkbookReader::read_worksheet_conditional_formats()` 只投影 writer-compatible owning ranges、priority 和 rule payload；复用 writer rule 类型不代表完整 conditional-formatting object model。
- Reader 必须保持 XML/nesting/format/range/`sqref` guardrail、callback exception retry 和 package no-side-effect；advanced/custom/dxf/formula/cellIs/multiple-rule container 默认 fail。
- Patch add 使用独立 bounded structural/priority source audit，不复用或放宽严格 reader projection。Advanced/custom/dxf/formula/cellIs/multiple-rule payload 原样保留且不投影/接管；root/QName/nesting/schema/direct-child 或 priority 缺失/非法/重复/耗尽必须在状态发布前 fail，不能 repair 或静默覆盖。
- Patch priority 从 source 与 same-session additions 的 effective maximum + 1 分配；planned rename 和 same-session added worksheet 必须走 planned catalog，`pending_worksheet_edits()` 以 `conditional_format_count` 报告最终追加数。
- Patch 只追加 worksheet-local `<conditionalFormatting>`；cells、worksheet `.rels`、content types、styles/dxf、calc metadata、`calcChain` 与 linked objects 保留。Worksheet replacement、priority/count、pending/unsaved 与 public diagnostics 必须先在副本中 staging，再统一 commit；失败不污染状态并可 retry。

## 验证

按 T1 只构建 library 与直接 feature targets，运行 Patch conditional-format add、Streaming conditional-formatting 与 bounded reader focused tests；默认不跑全量 CTest。覆盖 XML structure/schema order、三类 rule、multi-range/priority、planned rename/added worksheet、invalid source no-state-pollution、advanced/custom/multiple-rule payload preservation、failure/save retry、cells/relationships/content types/styles/calc/unknown-part preservation，以及 reader stored/DEFLATE/callback retry/foreign extension disambiguation。OpenPyXL/XlsxWriter/Excel 仅在实际运行时记录；验证通过只能支持当前窄切片 wording。
