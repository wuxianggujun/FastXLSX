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
- streaming tests/benchmarks

## 热路径

Row-order input -> cell validation/encoding -> 256 KiB bounded body batching -> file-backed/chunked package entry。禁止完整 worksheet DOM、dense matrix 和跨 row 无界 state；成功 close 必须释放临时文件与构建期缓存，失败保留 retry 状态。

## 关键能力

Cell references、dimension tracking、XML escape、finite numbers、inline/shared strings、styles 和 row/worksheet metadata。Feature side effect 必须保持 worksheet XML 顺序、relationships 和 content types 正确。

## Bounded Read

`WorkbookReader` 是 read-only Streaming facade：每次 traversal 新建 package-entry chunk source，按 row start/cell/row end 回调，borrowed strings 只活到当前 callback。保持 XML window 与 active-cell decoded text 双 guardrail；sharedStrings/style 首切片输出 opaque index，formula 与 cached scalar 分离。Callback failure 原样传播并释放 entry，后续可从头读取。禁止完整 sharedStrings/styles、worksheet DOM、dense matrix、`CellStore` 或 Patch/In-memory 隐式 handoff。

## Large Rewrite

Existing-file large worksheet rewrite 属于 C5，不应通过 `WorksheetEditor` materialization 实现。需要 event/stream reader、coordinate/formula policy、metadata audit 和大文件内存证据。

## 验证

Writer 运行 focused streaming tests、ZIP/XML、Office/openpyxl；性能相关时使用 schema-v6 executable/schema-v3 matrix，检查 generation/package-close/total wall 与 process CPU、CPU 总账、body buffer peak/flush count、process peak working set 和 close 后 active temporary file count。Reader 另测 stored/DEFLATE、typed projection、source order、borrowed copy、callback failure retry、双 guardrail、unsupported metadata 和 malformed diagnostics。
