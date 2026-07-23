---
name: fastxlsx-worksheet-metadata-features
description: "规划、实现或审查 FastXLSX worksheet metadata。用于 Streaming 或 existing-workbook 的 data validations、hyperlinks、conditional formatting、tables、freeze panes、auto filter、merged cells、worksheet relationships、content types，以及判断 metadata mutation 是否破坏 row-order、Patch preservation 或事务状态。"
---

# FastXLSX Worksheet Metadata Features

## 必读文件

- `docs/CURRENT_CAPABILITIES.md`、`docs/ARCHITECTURE.md`、`docs/EDITING_MODEL.md`
- `include/fastxlsx/streaming_writer.hpp`、`include/fastxlsx/worksheet_reader.hpp`、`include/fastxlsx/workbook_editor.hpp`
- `src/streaming_writer.cpp`、`src/worksheet_metadata_reader.cpp`、`src/worksheet_data_validation_reader.cpp`、`src/workbook_editor*.cpp`、`src/package_editor.cpp`
- 对应 metadata、relationship、preservation 和 failure-recovery tests

## 功能边界

将 data validations、internal/external hyperlinks、conditional formatting、tables、freeze panes、auto filter 和 merged cells 作为独立窄切片。逐项定义 public API、输入校验、worksheet XML schema 顺序、`.rels`、content types、linked part、状态增长和 non-goal。

## Streaming

- 只保留与规则数、range 数或 relationship 数相关的有界状态；不得持有 row/cell matrix 或阻塞 row-order hot path。
- 将 serializer/helper/test 与 `streaming_writer.cpp` 协调层分离，并验证跨 feature suffix ordering、relationship id 和 package side effects。
- Large formula/object semantic sync 不属于 new-workbook metadata。

## Streaming Read

- Public `WorkbookReader::read_worksheet_metadata()` 只投影 primary frozen pane、worksheet-root auto-filter 与 worksheet-root merged ranges；callback 值全部 owning，其他 view 只审计，table-local filter 不读取。
- 复用 bounded worksheet event source，但必须独立维护 XML nesting、reference bytes、sheetView count 与 retained merge count guardrail；merge overlap audit 可保留受限 ranges，禁止 worksheet DOM/dense matrix/CellStore。
- Parser/package failure 前允许已有 source-order callbacks，成功返回才是原子收集的 completion signal；callback exception 原样传播并允许 stored/DEFLATE entry 从头 retry。该路径不修改 relationships、content types、manifest、Patch state 或 In-memory state。
- Public `WorkbookReader::read_worksheet_data_validations()` 与通用 metadata traversal 分离，按 source order 投影 zero-based owning multi-range `sqref + DataValidationRule`。复用 shared rule validator，但独立审计 container/direct child/QName/schema、boolean/enum/entity、formula1/formula2 shape 与 XML/nesting/count/range/text guardrail；target 外 foreign extension local-name 不得误识别，target 内 unsupported metadata 明确 fail。它不求值、不校验 cell values、不做 overlap repair，也不修改 OPC/Patch/In-memory state。

## Existing Workbook

- 先为对象定义 preserve/audit/fail/edit；Streaming 可以创建不代表 Patch 可以编辑。
- Public Patch 已能事务式追加 generated empty worksheet，并同步 workbook catalog、workbook relationships、content types、manifest 和 worksheet part；这不克隆或初始化 styles、tables、drawings、validations、formulas 等 linked metadata。
- Public Patch 已能事务式删除 relationship-closed worksheet；last-visible、active/selected、definedNames、formula、materialized、queued payload、owned relationship 和 extra-inbound linked semantics 默认 fail，不做静默 orphan 或修复。
- Public Patch 已公开 `WorkbookEditor::add_internal_hyperlink()` 窄切片：用两次 bounded worksheet event scan 规划/写出 worksheet-local `<hyperlink>`，支持 existing/self-closing 容器、display/tooltip escaping、A1 range overlap rejection、planned rename 和同会话新增 worksheet。它不创建 worksheet `.rels` 或 content type，不修改 cell value/style，也不同步 formula、definedName、table、drawing 或 external hyperlink。
- Public Patch 已公开 `WorkbookEditor::add_external_hyperlink()` 窄切片：用 bounded worksheet rewrite 追加带 `r:id` 的 external `<hyperlink>`，并在同一事务中更新 worksheet `.rels` 的 `TargetMode="External"` relationship；支持既有/缺失 `.rels`、关系 id 分配、`xmlns:r` 注入、display/tooltip escaping、A1 range overlap rejection、planned rename 和同会话新增 worksheet。它不创建 content type，不做 target reachability、relationship repair/pruning 或 cell/formula/definedName/table/drawing 同步。
- Public Patch 已公开 `WorkbookEditor::add_data_validation()` 窄切片：复用 Streaming/Patch shared `DataValidationRule` validator/serializer，用 bounded worksheet rewrite 追加 single-/multi-range `sqref`、formula1/formula2 与 prompt/error metadata；支持 existing/self-closing container、缺失 count 补齐、count mismatch fail、planned rename 和同会话新增 worksheet。它不创建 `.rels`/content type、不求值/请求重算，也不与 structural mutation 或 linked objects 同步。
- Public Patch 已公开 `WorkbookEditor::set_auto_filter()` / `clear_auto_filter()` 窄切片：用两次 bounded worksheet event scan 整体 replace/insert/remove worksheet-root `<autoFilter>`，校验 existing ref、duplicate/nesting 与 suffix ordering，支持 planned rename 和同会话新增 worksheet。Set 丢弃旧 criteria/sort metadata；clear absent clean no-op。Table-local filter part、`.rels`、content type、calc metadata 与 unknown entries 保留，range 不随 structural mutation 同步。
- Public Patch 已公开 `WorkbookEditor::merge_cells()` / `unmerge_cells()`：用两次 bounded worksheet event scan 编辑 worksheet-root `<mergeCells>`，支持 missing/ordinary/self-closing container。Merge 拒绝 duplicate/overlap；unmerge 只删除 exact range，partial overlap fail，absent disjoint clean no-op。实现严格审计 count、direct child、multi-cell ref、schema order 和 existing duplicate/overlap；不删除非左上角 cell payload，不改 value/style/formula、relationships、content types、table、`calcPr` 或 `calcChain`，range 不随 structural mutation 同步。
- Public Patch 已公开 `WorkbookEditor::set_freeze_panes()` / `clear_freeze_panes()`：用两次 bounded worksheet event scan 编辑 primary `workbookViewId="0"` 的 direct frozen `<pane>`，支持 missing/self-closing sheetViews/primary sheetView、single-/dual-axis set、zero/explicit clear、planned rename 和同会话新增 worksheet。实现审计 QName/view id/frozen state/child schema/selection pane；普通 split/frozenSplit、pivotSelection 和失效 pane selection fail。它保留其他 workbook views、合法 selection、cells、relationships、content types、tables、`calcPr`、`calcChain` 与 unknown entries。
- 跨 worksheet XML、worksheet `.rels`、content types、manifest、public diagnostics 和 pending/watermark 的 mutation，必须先在副本中完整 staging，再以 noexcept commit 发布。
- External hyperlink 的 worksheet XML 与 `.rels` relationship mutation 必须一起 staging；遇到未知、重复、external、非法 target 或 unsupported linked metadata 时默认 fail 或 preserve，不能静默 repair。
- Row/column insert/delete、copy/move 当前不自动同步 validations、hyperlinks、auto filters、tables、conditional formatting、merged cells 或 drawings；新增同步能力前保持该边界。

## 验证

- 覆盖 feature XML、schema ordering、relationship/content-type side effects、invalid input 和 feature-specific duplicate/range/count conflicts。
- Patch 覆盖 failure-before-state-change、pending/unsaved/diagnostic 不污染、save failure retry、reopen 和 unknown-part preservation。
- Auto-filter 另测 complete nested-criteria replacement、clear absent no-op、table-local filter preservation、rename/added worksheet 与 optional-range diagnostic。
- Merged-cell 另测 missing/append/self-closing/remove-container、exact removal/absent no-op、count/ref/child/schema/overlap audit、cell payload/table preservation，以及 `calcPr`、`calcChain` part、workbook relationship 和 content type 的 exact preservation。
- Freeze-pane 另测 missing/self-closing view metadata、other workbook views、single-/dual-axis active pane、zero clear/absent no-op、QName、selection preserve/fail、unsupported pane state/pivot/schema audit、final split diagnostic，以及 cells/table/relationships/content types/`calcPr`/`calcChain` exact preservation。
- Metadata read 另测 primary/other view、frozen/root-filter/merged source order、owning values、callback retry、stored/DEFLATE、XML/nesting/reference/view/merge guardrail、QName/schema/count/overlap/unsupported pane rejection和 package no-side-effect。
- Data-validation read 另测 owning multi-range/rule、entity decode、formula shape、absent/empty container、callback retry、stored/DEFLATE、XML/nesting/validation/range/sqref/formula/text guardrail、count/direct-child/QName/schema/unsupported metadata rejection、foreign extension disambiguation和 package no-side-effect。
- 使用 OpenPyXL/XlsxWriter 做结构对照；Office 未运行时明确记录 `not_run`。
