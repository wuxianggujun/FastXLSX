---
name: fastxlsx-worksheet-metadata-features
description: "规划、实现或审查 FastXLSX worksheet metadata。用于 Streaming 或 existing-workbook 的 data validations、hyperlinks、conditional formatting、tables、freeze panes、auto filter、merged cells、worksheet relationships、content types，以及判断 metadata mutation 是否破坏 row-order、Patch preservation 或事务状态。"
---

# FastXLSX Worksheet Metadata Features

## 必读文件

- `docs/CURRENT_CAPABILITIES.md`、`docs/ARCHITECTURE.md`、`docs/EDITING_MODEL.md`
- `include/fastxlsx/streaming_writer.hpp`、`include/fastxlsx/workbook_editor.hpp`
- `src/streaming_writer.cpp`、`src/workbook_editor*.cpp`、`src/package_editor.cpp`
- 对应 metadata、relationship、preservation 和 failure-recovery tests

## 功能边界

将 data validations、internal/external hyperlinks、conditional formatting、tables、freeze panes、auto filter 和 merged cells 作为独立窄切片。逐项定义 public API、输入校验、worksheet XML schema 顺序、`.rels`、content types、linked part、状态增长和 non-goal。

## Streaming

- 只保留与规则数、range 数或 relationship 数相关的有界状态；不得持有 row/cell matrix 或阻塞 row-order hot path。
- 将 serializer/helper/test 与 `streaming_writer.cpp` 协调层分离，并验证跨 feature suffix ordering、relationship id 和 package side effects。
- Large formula/object semantic sync 不属于 new-workbook metadata。

## Existing Workbook

- 先为对象定义 preserve/audit/fail/edit；Streaming 可以创建不代表 Patch 可以编辑。
- Public Patch 已能事务式追加 generated empty worksheet，并同步 workbook catalog、workbook relationships、content types、manifest 和 worksheet part；这不克隆或初始化 styles、tables、drawings、validations、formulas 等 linked metadata。
- Public Patch 已能事务式删除 relationship-closed worksheet；last-visible、active/selected、definedNames、formula、materialized、queued payload、owned relationship 和 extra-inbound linked semantics 默认 fail，不做静默 orphan 或修复。
- 跨 worksheet XML、worksheet `.rels`、content types、manifest、public diagnostics 和 pending/watermark 的 mutation，必须先在副本中完整 staging，再以 noexcept commit 发布。
- External hyperlink 需要 relationship mutation；internal hyperlink 不应伪造 external relationship。遇到未知、重复、external、非法 target 或 unsupported linked metadata 时默认 fail 或 preserve，不能静默 repair。
- Row/column insert/delete、copy/move 当前不自动同步 validations、hyperlinks、tables、conditional formatting、merged cells 或 drawings；新增同步能力前保持该边界。

## 验证

- 覆盖 feature XML、schema ordering、relationship/content-type side effects、invalid input 和 duplicate/range conflicts。
- Patch 覆盖 failure-before-state-change、pending/unsaved/diagnostic 不污染、save failure retry、reopen 和 unknown-part preservation。
- 使用 OpenPyXL/XlsxWriter 做结构对照；Office 未运行时明确记录 `not_run`。
