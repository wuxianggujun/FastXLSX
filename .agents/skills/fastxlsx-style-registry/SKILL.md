---
name: fastxlsx-style-registry
description: "实现或审查 FastXLSX 样式注册表、StyleId、CellAlignment、HorizontalAlignment、VerticalAlignment、CellFont、CellFill、CellStyle、WorkbookWriter::add_style()、CellView::with_style()、xl/styles.xml、number formats、wrap-text + limited horizontal/vertical alignment、bold/italic/direct ARGB font color、solid fill、worksheet s 属性、sharedStrings + styles 共存和样式 QA。用于当前 streaming styles 基础、未来 full-font/full-fill/border/full-alignment、conditional formatting 与 styles 交互，或排查 Excel 修复样式输出。"
---

# FastXLSX Style Registry

## 当前边界

以 public header/tests 为准：写侧包括 `StyleId`、number formats、wrap text、有限 horizontal/vertical alignment、bold/italic/direct ARGB font color、solid fill 与 worksheet cell `s` 属性；读侧包括独立 bounded custom number-format/cellXfs traversal。

## 设计规则

- Registry 位于 workbook scope，style id 稳定且可复用。
- Cell hot path 只写已注册 id，不构造通用 DOM style object。
- 校验 number format、ARGB、alignment 和 unsupported combinations。
- sharedStrings 与 styles 并存时保持 relationship/content-type/index 正确。

## Existing-file 边界

Source style preservation 与 value-only edits 不等于完整 styles migration。Caller-supplied non-default source style handle 的支持以 public contract 为准，不猜测映射。

`WorkbookReader::read_cell_formats()` 只投影 custom format code、zero-based cellXfs、opaque number-format/font/fill ids、apply flags 与窄 alignment；它不构建完整 registry、不解析 font/fill table、不自动关联 worksheet `style_index`。Relationship/content type、container count、duplicate custom id 与 guardrail 必须审计；未支持的 enabled xf metadata 明确 fail，standard explicit false/no-op flags 可接受。

## 非目标

完整 font/fill/border/alignment parity、theme/tint inheritance、existing styles semantic rewrite 和 conditional-formatting dxf registry。

## 验证

Feature-specific tests、`styles.xml` counts/index、cell `s`、invalid input/no-state-pollution、sharedStrings coexistence、bounded read callbacks/guardrails/retry/relationship audit 和 Office/openpyxl QA。
