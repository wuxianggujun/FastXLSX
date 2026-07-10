---
name: fastxlsx-api-design-docs
description: "设计或审查 FastXLSX public API、Doxygen、状态与性能边界。"
---
# FastXLSX API Design Docs

先标记 Streaming/Patch/In-memory 与 public facade，再定义输入、所有权、状态、失败、保存、内存和 OpenXML side effects。

Public Doxygen 必须写清：
- 顺序/随机访问与生命周期。
- `has_pending_changes()` 和 `has_unsaved_changes()` 的区别。
- In-memory strict/lossy projection，默认 strict，lossy 显式 opt-in。
- preserve/audit/fail/edit 和 non-goals。

禁止把 internal 类型、preservation evidence、公式文本或窄图片能力扩大为 public/full support。