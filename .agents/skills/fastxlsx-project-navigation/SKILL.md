---
name: fastxlsx-project-navigation
description: "导航 FastXLSX 架构、public/internal 边界和当前能力。"
---
# FastXLSX Project Navigation

## 必读
`docs/CURRENT_CAPABILITIES.md`、public headers、source、tests、CMake、`docs/TASK_BREAKDOWN.md`。

## 三路径
- Streaming：`WorkbookWriter`，大型有序新建。
- Patch：`WorkbookEditor`，已有文件 part-level rewrite。
- In-memory：`WorksheetEditor`，small-file sparse editing。

## 当前关键事实
- Production 默认 minizip stored+DEFLATE；stored-only 是显式 profile。
- `has_pending_changes()` 与 save watermark 分离。
- Dirty In-memory save 采用 stage → package write → state commit；失败不提交 dirty handoff。
- In-memory 默认 `RejectKnownLosses`，通过 `WorksheetMaterializationError` 暴露稳定 loss category/context；lossy 必须显式 opt-in。
- Images 可关闭；关闭时 public stubs 抛错。
- Internal package/edit-plan 类型不进入 public surface。
- Streaming worksheet body 使用 256 KiB bounded batching，成功 close 后回收临时资源；Patch strict replace 使用 direct-range，其他 cell fallback 使用 256 KiB output-batched single-pass source-order transform。Internal opt-in reader 可暴露 bounded window 内完整 numeric/simple-inline/formula cell；canonical `<is><t>` payload 走 literal fast path，其他变体保留结构 parser。Transformer 可把连续 untouched span 合并为 callback-lifetime pass-through batch，同时保留 metadata/replacement/window boundary、dimension 与 formula/sharedStrings/style audit；telemetry 拆分 parser/source-callback/coalesced/action、aggregate/canonical inline-string、complete-cell、pass-through batching、relationship/temporary IO 与 package writer。

判断顺序：public headers → source → tests → capability docs。历史计划只查 Git。
