---
name: fastxlsx-streaming-worksheet
description: "开发或审查 FastXLSX 流式 worksheet 路径。用于 row/cell writer、worksheet reader、worksheet rewriter、模板 sheet 替换、dimension tracking、XML escape、cell reference 编码、inlineStr/sharedStrings 策略、大文件导出，以及 worksheet 处理中的内存/性能问题。"
---

# FastXLSX Streaming Worksheet

## 必读文件

- `docs/CURRENT_CAPABILITIES.md`
- `docs/ARCHITECTURE.md`
- `docs/PERFORMANCE_TARGETS.md`
- `include/fastxlsx/streaming_writer.hpp`
- `src/streaming_writer.cpp`
- `include/fastxlsx/worksheet_reader.hpp`
- `src/worksheet_reader.cpp`
- `src/worksheet_data_validation_reader.cpp`
- `src/shared_strings_reader.cpp`
- `tests/test_worksheet_reader.cpp`
- `tests/test_worksheet_data_validation_reader.cpp`
- `tests/test_shared_strings_reader.cpp`
- `tests/test_shared_string_runs_reader.cpp`
- streaming tests/benchmarks

## 热路径

Row-order input -> cell validation/encoding -> 256 KiB bounded body batching -> file-backed/chunked package entry。禁止完整 worksheet DOM、dense matrix 和跨 row 无界 state；成功 close 必须释放临时文件与构建期缓存，失败保留 retry 状态。

## 关键能力

Cell references、dimension tracking、XML escape、finite numbers、inline/shared strings、styles 和 row/worksheet metadata。Feature side effect 必须保持 worksheet XML 顺序、relationships 和 content types 正确。

## Bounded Read

`WorkbookReader` 是 read-only Streaming facade：每次 traversal 新建 package-entry chunk source，按 row start/cell/row end 回调，borrowed strings 只活到当前 callback。保持 XML window 与 active-cell decoded text 双 guardrail；sharedStrings/style 首切片输出 opaque index，formula 与 cached scalar 分离。Callback failure 原样传播并释放 entry，后续可从头读取。禁止完整 sharedStrings/styles、worksheet DOM、dense matrix、`CellStore` 或 Patch/In-memory 隐式 handoff。

`read_worksheet_metadata()` 是独立 bounded companion：按 source order 投影 owning primary frozen split、worksheet-root auto-filter range 与 zero-based merged ranges，其他 workbook view 只做结构/view-id 审计，table-local filter 不读取。保持 XML window、nesting、reference bytes、sheetView count 与 retained merge count guardrail，并显式拒绝 QName/schema/count/overlap/unsupported pane 歧义。失败前允许 partial callbacks，成功返回才是完整收集信号；禁止 worksheet model、OPC mutation 或 Patch/In-memory handoff。

`read_worksheet_data_validations()` 是另一条独立 bounded worksheet-root companion：按 source order 投影 zero-based owning ranges/rule，复用 writer/Patch 的 `DataValidationRule` shape，但不共享 worksheet model。保持 XML window、nesting、validation/range count、decoded `sqref`、formula 与 prompt/error text guardrail；审计 container count/direct child、QName/schema、boolean/enum/entity 和 formula1/formula2 shape。Target 内 foreign/extension 与 non-default prompt-window metadata 明确 fail，target 外 foreign extension local-name 不得误识别。失败前允许 partial callbacks；禁止求值、cell-value validation、overlap repair、OPC mutation 或 Patch/In-memory handoff。

`read_shared_strings()` 是显式分离的 bounded companion：审计唯一 internal relationship、normalized target part 与标准 content type，按 index/source order 投影 simple `<si><t>`。Borrowed text 只活到 callback；XML window/item text 双 guardrail，rich/phonetic/extension/extra metadata 明确 fail。禁止构建完整 table、自动解析 worksheet index 或隐式接入 Patch/In-memory。

`read_shared_string_runs()` 是独立的 bounded rich companion：成功时按 item start/run/item end 顺序保留 rich `<r>` boundary，并把 simple `<t>` 映射成一个默认 run。Run text 只活到 `on_run`；index/kind/format owning。限制 XML window、item/run bytes、runs per item 与 nesting；只投影 bold/italic/direct ARGB 并接受固定 default font metadata。失败前可能已有 partial callbacks；需原子结果时只在成功返回后发布。Mixed shape、phonetic/extension、非默认 font/theme/tint 和其他 run property 明确 fail；禁止完整 table、format inheritance、worksheet index 自动关联或 Patch/In-memory handoff。

`read_cell_formats()` 是显式分离的 bounded styles companion：审计唯一 internal relationship、normalized target part 与标准 content type，按 source order 分别投影 custom `numFmtId + formatCode` 和 zero-based `cellXfs`。Format code 只活到 callback；cell format ids/apply/alignment 是 owning values，number-format/font/fill ids 保持 opaque。XML window、active format-code、nesting 与 custom-id count 提供 guardrail；enabled border/base-style/protection/quote/pivot、extension 和其他未投影 metadata 明确 fail。禁止完整 styles registry、自动解析 worksheet style index 或隐式接入 Patch/In-memory。

`read_style_components()` 是另一条显式 bounded styles traversal：按 source order 投影 zero-based owning font/fill values，font 仅含 bold/italic/optional direct ARGB 并接受 writer 固定 default metadata，fill 仅含 none/gray125/solid direct ARGB。XML window、nesting、font/fill count 与 container count 必须有门禁；theme/tint inheritance、其他 font/color/pattern/gradient 明确 fail。它不保留 component table，也不自动关联 cellXfs 或 worksheet style index。

## Large Rewrite

Existing-file large worksheet rewrite 属于 C5，不应通过 `WorksheetEditor` materialization 实现。需要 event/stream reader、coordinate/formula policy、metadata audit 和大文件内存证据。

## 验证

Writer 运行 focused streaming tests、ZIP/XML、Office/openpyxl；性能相关时使用 schema-v6 executable/schema-v3 matrix，检查 generation/package-close/total wall 与 process CPU、CPU 总账、body buffer peak/flush count、process peak working set 和 close 后 active temporary file count。Worksheet reader 另测 stored/DEFLATE、typed projection、source order、borrowed copy、callback failure retry、双 guardrail、unsupported metadata 和 malformed diagnostics。Worksheet metadata companion 另测 primary/other view、frozen/root-filter/merged source order、owning values、partial callback retry、五类 guardrail、QName/schema/count/overlap/unsupported pane rejection与 package no-side-effect。Data-validation companion 另测 owning multi-range/rule、entity decode、absent/empty container、partial callback retry、七类 guardrail、count/direct-child/QName/schema/formula-shape rejection、foreign extension disambiguation 与 package no-side-effect。Strict sharedStrings companion 另测 simple/empty/entity decode、zero-based order、超过 package input chunk 的 token、relationship target/content type、rich/phonetic/extension rejection、callback retry 与双 guardrail。Rich-run companion 另测 item/run 顺序、simple compatibility、owning format、三类 callback retry、五类 guardrail、chunk boundary、OPC audit、mixed/phonetic/extension/unsupported format 与 malformed diagnostics。Cell-formats companion 另测 custom formats/cellXfs source order、format-code decode/borrowed copy、两类 callback retry、跨 package chunk token、四类 guardrail、container count/duplicate id、relationship/content type 与 unsupported xf/alignment metadata。
