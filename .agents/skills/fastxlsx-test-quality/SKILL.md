---
name: fastxlsx-test-quality
description: "添加或排查 FastXLSX CTest、OpenXML、profile 和 benchmark evidence。"
---
# FastXLSX Test Quality

先 focused，再 production CTest；profile/package 变更追加 stored/no-images/install smoke。

关键门禁：
- Patch preservation、side effects、failure-before-state-change、retry/reopen。
- Save transaction/watermark：stage → package write → state commit、post-stage failure 保持 dirty/counts/error、retry 写最新值、成功清 unsaved、move 转移状态。
- In-memory typed strict category/context、explicit lossy、generic policy mismatch、malformed-source precedence、guardrail、no pollution、`last_edit_error()` preservation。
- Streaming row order、无 DOM/dense matrix。
- No-images disabled-feature runtime smoke。

普通 CTest timeout 为 60 秒；public-state 测试已全部拆为 standalone targets，不再保留专用 120 秒 legacy shard。

Benchmark 不进默认 CTest。Release 只引用 `benchmarks/evidence/` 中通过 `validate_benchmark_evidence.py` 的 bundle；当前 4 个 production Streaming bundle、7 个 Patch bundle 与 1 个 OpenXLSX reference bundle 只支持 manifest 限定的单机 workload 结论。Streaming executable 使用 schema v5，分列 generation/package-close/body-buffer/resource-lifecycle；Patch `WorkbookEditor` executable 使用 schema v6，保留 direct-range、single-pass parser/source-callback/coalesced/action traffic、batching、relationship/temporary IO、package writer target-entry 与 raw compressed-copy telemetry，并增加 relationship scanner input calls/bytes、boundary carry 与 slow-path tags。Patch matrix 必须分列 copied source/output compressed bytes；planned raw-copy entry 还必须比较 exact compressed payload 并核对 names/count/bytes，但不得写成 local-header/central-directory/整包 byte copy。Relationship-bearing fixture 还必须验证 worksheet binding 与 `.rels` target。Reference 必须记录 API、save/compression 差异、output size 与 peak working set，任何结果不得泛化到未覆盖 workload。
