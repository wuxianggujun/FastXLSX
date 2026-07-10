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
- In-memory 默认 `RejectKnownLosses`，lossy 必须显式 opt-in。
- Images 可关闭；关闭时 public stubs 抛错。
- Internal package/edit-plan 类型不进入 public surface。

判断顺序：public headers → source → tests → capability docs。历史计划只查 Git。