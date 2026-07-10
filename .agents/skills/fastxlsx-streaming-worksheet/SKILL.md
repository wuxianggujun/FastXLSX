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
- streaming tests/benchmarks

## 热路径

Row-order input -> cell validation/encoding -> incremental worksheet XML -> file-backed/chunked package entry。禁止完整 worksheet DOM、dense matrix 和跨 row 无界 state。

## 关键能力

Cell references、dimension tracking、XML escape、finite numbers、inline/shared strings、styles 和 row/worksheet metadata。Feature side effect 必须保持 worksheet XML 顺序、relationships 和 content types 正确。

## Large Rewrite

Existing-file large worksheet rewrite 属于 C5，不应通过 `WorksheetEditor` materialization 实现。需要 event/stream reader、coordinate/formula policy、metadata audit 和大文件内存证据。

## 验证

Focused streaming tests、ZIP/XML、Office/openpyxl，性能相关时使用 schema-v4 benchmark 并明确 footprint 口径。