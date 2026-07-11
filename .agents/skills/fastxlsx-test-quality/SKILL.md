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

普通 CTest timeout 为 60 秒；legacy public-state 剩余 6 个 shard 在拆分超大 translation unit 期间使用专用 120 秒上限，已拆出的 standalone tests 与其他测试不得借此放宽。

Benchmark 不进默认 CTest。Release 只引用 `benchmarks/evidence/` 中通过 `validate_benchmark_evidence.py` 的 bundle；0 bundle 是合法“无证据”状态。
