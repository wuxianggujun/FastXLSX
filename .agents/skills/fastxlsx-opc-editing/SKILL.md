---
name: fastxlsx-opc-editing
description: "处理 FastXLSX existing-file OPC、part-level rewrite 与 preservation。"
---
# FastXLSX OPC Editing

Public facade 是 `WorkbookEditor` / `WorksheetEditor`；package reader/editor、EditPlan、dependency/relationship graph 是 internal。

- Unchanged/unknown part copy-original。
- Changed part stream/small-part rewrite 或 remove。
- 功能必须声明 preserve/audit/fail/edit 与 relationship/content-type/calc side effects。
- 跨 edit plan、replacements、omitted entries 和 manifest 的 metadata mutation 必须先 staging，再以 noexcept commit 发布；测试需注入提交前失败并验证原状态与 retry。
- `save_as()` 不是 in-place；pending staged state 与 unsaved watermark 分离。无 options overload 保留 stored 输出；显式 `WorkbookEditorSaveOptions` 可选择 active backend/DEFLATE，配置失败必须早于 dirty-session staging。
- Copy-original 始终要求 logical payload/CRC preservation；production minizip-ng 在 source/output method 匹配时还会复制 exact compressed payload。性能证据必须分列 source/output compressed bytes 与 planned raw names/count/bytes，并验证 payload equality；不得描述成 local header、central directory、extra fields 或整包 byte passthrough。
- DEFLATE strict existing-cell replace 在无 worksheet relationships、目标全部存在且具有 top-level dimension 时使用 one-inflate + owned temporary-file ranges；missing-cell upsert、relationship-bearing worksheet 与其他 fallback 使用 single-pass source-order transform，在同一扫描中完成精确 dimension、relationship audit 与 telemetry。两条路径必须分别报告性能，重复 rewrite 还需验证 superseded temporary file 立即回收。
- Dirty In-memory projection 可在写包前进入 internal Patch stage，但只有 package write 成功后才能清 dirty、增加 handoff count 和推进 watermark；失败 retry 必须用当前 CellStore 覆盖 stale stage。
- In-memory 默认拒绝已知有损 materialization，并以 public typed diagnostic 报告稳定语义分类和 source cell context；显式 lossy 才拍平。
- Preservation 不等于 tables/drawings/comments/VBA/pivot/custom XML semantic editing。
