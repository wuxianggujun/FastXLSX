# 公式支持边界

## 当前支持

- `Cell`、`CellView` 和 `CellValue` 可保存公式文本。
- Streaming 和 small workbook 路径把公式写入 worksheet XML。
- Existing-file editor 提供公式引用审计、worksheet rename 相关策略和窄 row/column shift 重写。
- `WorkbookEditor::request_full_calculation()` 可请求办公软件打开时重新计算。
- Existing formula text、defined names 和 calc metadata 在未修改时按 Patch preservation 策略处理。

## 计算模型

FastXLSX 不包含公式计算引擎：

- 不求值公式。
- 不生成或承诺更新 cached value。
- 不完整重建 `calcChain.xml`。
- 不保证所有 Excel 公式语法、structured references、external links 或 dynamic arrays 都能被重写。

调用方需要最新计算结果时，应请求 full calculation，并由 Excel、LibreOffice 或其他计算引擎完成求值。

## 引用重写边界

公式重写必须：

- 区分相对、绝对和 mixed references。
- 避免修改字符串字面量和无法识别的 token。
- 对超出支持范围的语法 audit 或 fail，不静默猜测。
- 在状态变更前完成输入与 guardrail 检查。
- 与 worksheet rename、row/column insert/delete 的实际 materialized/Patch 范围一致。

结构编辑当前不代表 tables、charts、validations、conditional formatting 或其他 metadata 中公式的完整同步。

## 验证

- unit test：公式文本、引用解析、rename/shift、无效输入和边界坐标。
- OpenXML：`<f>` 文本、calc properties、calcChain preserve/remove 策略。
- Existing-file：defined names、跨 sheet 引用、失败恢复和 reopened output。
- Office smoke：请求重算后的打开行为；不能把 Office 重算结果写成 FastXLSX 自身求值。

## 非目标

- Excel-compatible calculation engine。
- cached value/materialized result API。
- 完整 formula grammar 和 dependency graph。
- 完整 calcChain rebuild。