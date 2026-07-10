---
name: fastxlsx-test-quality
description: "添加或排查 FastXLSX CTest、OpenXML、profile 和 benchmark evidence。"
---
# FastXLSX Test Quality

先 focused，再 production CTest；profile/package 变更追加 stored/no-images/install smoke。

关键门禁：
- Patch preservation、side effects、failure-before-state-change、retry/reopen。
- Save watermark 成功/失败/move。
- In-memory strict rejection、explicit lossy、policy mismatch、guardrail、no pollution。
- Streaming row order、无 DOM/dense matrix。
- No-images disabled-feature runtime smoke。

Benchmark 不进默认 CTest。Release 只引用 `benchmarks/evidence/` 中通过 `validate_benchmark_evidence.py` 的 bundle；0 bundle 是合法“无证据”状态。