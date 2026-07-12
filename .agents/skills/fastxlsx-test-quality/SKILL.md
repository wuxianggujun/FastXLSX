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

Benchmark 不进默认 CTest。Release 只引用 `benchmarks/evidence/` 中通过 `validate_benchmark_evidence.py` 的 bundle；当前 2 个 production Streaming bundle 与 2 个 Patch bundle 只支持 manifest 限定的单机 workload 结论。Patch matrix 必须分列 copied source/output compressed bytes，不能把 logical copy-original 写成 raw ZIP copy；DEFLATE strict targeted replace 的 one-inflate direct-range 结果不得泛化到 missing-cell upsert 或 relationship-bearing worksheet。
