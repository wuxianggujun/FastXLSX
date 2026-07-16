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

Benchmark 不进默认 CTest。Release 只引用 `benchmarks/evidence/` 中通过 `validate_benchmark_evidence.py` 的 bundle；当前 4 个 production Streaming bundle、16 个 Patch bundle 与 1 个 OpenXLSX reference bundle，共 21 个，只支持 manifest 限定的单机 workload 结论。Streaming executable/matrix 使用 schema v6/v3，分列 generation/package-close/total wall 与 process CPU、body-buffer/resource-lifecycle 与 pre-warmup/warmed observation，并校验 CPU 总账；Patch `WorkbookEditor` benchmark executable/matrix 使用 schema v20/v15，保留 v6 direct-range/single-pass traffic、v7 staged CRC、v8 至 v15 fast-path/batching/prefetch/writer-call telemetry、v16 至 v18 editor CPU/CRC backend/fused CRC gate、v19 多 worksheet aggregate，并增加按 package 顺序输出的 rewritten worksheet entry 明细数组。数组 count/order、逐字段 sum/max 与 sheet1 legacy 字段必须和 aggregate 总账一致；stored backend 保持数组为空。Patch matrix 支持一次记录多个 output level，必须分列 copied source/output compressed bytes；多 worksheet profile 必须核对每表 names/value/dimension、edits/inserted、transform/CRC segments 与 OpenPyXL。Isolated package-writer executable/matrix 为 schema v2、balanced paired runner 为 schema v1，internal Patch CRC/fusion paired runner 为 schema v3。最新 tracked Streaming/Patch/isolated evidence 仍分别为 v5/v9/v1。Warm-up raw result 必须保留；第一轮 observation 只能标记 `cache_control=none`、`cold_cache_claim=false`，release representative 继续使用 warmed measured median。Patch paired runner 必须让两个 profile 在两个 position 等频出现，并逐轮核对 logical/compressed fingerprint、CRC 与 size；fused path 必须验证 completed-entry CRC、same-size mutation output protection 与 retry。Windows production 的至少 4 MiB staged file chunk 还必须报告固定 512 KiB input peak 与 1 MiB prefetch buffer，小/raw-copy/stored/non-Windows guard 保持同步且 prefetch traffic 为零。Planned raw-copy entry 必须比较 exact compressed payload 并核对 names/count/bytes，且 DEFLATE writer CPU 必须为 0。Minizip CPU envelope 包含 backend bookkeeping，不是纯 encoder CPU；direct-zlib engine CPU 只覆盖 `deflateInit2`/`deflate`/`deflateEnd`，CRC/read/raw-output write/entry close 必须独立。Relationship-bearing fixture 还必须验证 worksheet binding 与 `.rels` target。Reference 必须记录 API、save/compression 差异、output size 与 peak working set，任何结果不得泛化到未覆盖 workload。

Worksheet scaling runner schema v2 的 1/2/4-sheet variants 必须使用反转 rotation，让每个 variant 在每个 position 等频出现；`fixed-shape` 与 `fixed-total` 分开报告，fixed-total 明确 row-number/XML-size 并非 byte-identical。保留全部 raw result、position statistics、同轮 baseline ratio、逐 worksheet entry 跨 measured runs 的 min/median/max 与代表 OpenPyXL 状态。Windows process CPU 约 15.625 ms 量化使快速小 entry 可合法为 0，正 DEFLATE CPU gate 只对至少 4 MiB rewritten target 强制。

新的 standalone package-writer bundle 使用 `package-writer` manifest kind；旧的 Patch 合并 bundle 保持 `workbook-editor`，不追溯改写历史 evidence。

Production/stored/no-images 必须保持 `FASTXLSX_ENABLE_DIRECT_ZLIB_PROFILING=OFF`，并验证 unavailable selection 在 package write 前失败且不污染既有输出；one-pass round-trip/guard/CRC mutation 使用 `windows-nmake-release-direct-zlib-profile`。默认 install consumer 不得因 profiling engine 额外要求 FastXLSX 的 `ZLIB::ZLIB` export dependency。

Production/stored/no-images 也必须保持 `FASTXLSX_ENABLE_PORTABLE_CRC_PROFILING=OFF`。CRC backend A/B 只使用 `windows-nmake-release-patch-crc-minizip-profile` 与 `windows-nmake-release-patch-crc-portable-profile`；后者只改变 private PackageEditor CRC implementation，不改变 minizip package backend。
