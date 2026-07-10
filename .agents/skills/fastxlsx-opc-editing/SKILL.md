---
name: fastxlsx-opc-editing
description: "处理 FastXLSX existing-file OPC、part-level rewrite 与 preservation。"
---
# FastXLSX OPC Editing

Public facade 是 `WorkbookEditor` / `WorksheetEditor`；package reader/editor、EditPlan、dependency/relationship graph 是 internal。

- Unchanged/unknown part copy-original。
- Changed part stream/small-part rewrite 或 remove。
- 功能必须声明 preserve/audit/fail/edit 与 relationship/content-type/calc side effects。
- `save_as()` 不是 in-place；pending staged state 与 unsaved watermark 分离。
- In-memory 默认拒绝已知有损 materialization；显式 lossy 才拍平。
- Preservation 不等于 tables/drawings/comments/VBA/pivot/custom XML semantic editing。