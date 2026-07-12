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

Benchmark 不进默认 CTest。Release 只引用 `benchmarks/evidence/` 中通过 `validate_benchmark_evidence.py` 的 bundle；当前 1 个 production Streaming bundle 仅支持精确单机单次 claim，不支持泛化性能结论。
