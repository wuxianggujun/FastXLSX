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
- Production minizip-ng 对具备完整 expected size/CRC32 的 staged file chunks 在正常路径合并 CRC 并校验 completed-entry CRC；combined mismatch 才重读定位具体 chunk，不完整 metadata 保留 per-chunk fallback。任何优化都必须保留 failure-before-state-change、output protection 与 retry。
- Windows production minizip-ng 对至少 4 MiB staged file chunk 使用两个固定 512 KiB buffer 做 bounded overlapped read-ahead；先发起下一块读取，再同步写当前块。小 chunk、raw compressed-copy、stored-only 与非 Windows 路径保持同步读取；本地 RAII 必须取消并等待 pending IO，不得留下后台 worker。性能报告需分列 activation、prefetched chunks/bytes、1 MiB peak buffer、foreground read/wait、file IO buffer、writer input peak/call count/maximum call wall time，并且 warmed run 不能冒充物理 OS cache-hit 证据。
- DEFLATE strict existing-cell replace 在无 worksheet relationships、目标全部存在且具有 top-level dimension 时使用 one-inflate + owned temporary-file ranges；missing-cell upsert、relationship-bearing worksheet 与其他 fallback 使用 256 KiB output-batched single-pass source-order transform，在同一扫描中完成精确 dimension、relationship audit 与 telemetry。Internal opt-in Patch reader 会在 generic parser 前识别 bounded window 内 exact writer-compatible value、formula、cached-formula 与 simple inline-string complete cell，并暴露一个 callback-lifetime exact-byte span；attribute 变体、rich/unsupported metadata、malformed markup 与跨窗口 candidate 保留结构 parser 和 diagnostics。Canonical `<is><t>`/`xml:space="preserve"` payload 仍使用不复制的 literal terminator/suffix matcher。Transformer 可继续合并连续 row/raw/untouched-cell span；metadata、replacement、非连续 offset 与窗口边界必须 flush。Batch summary 必须保留 coordinate extrema 与 formula/sharedStrings/style/inline-string audit。有序 upsert 使用单游标，乱序 source 保留 set fallback。性能报告需拆 parser/source-callback/coalesced/action traffic、aggregate/canonical inline-string、complete-cell/canonical complete-cell count/bytes/type counters、pass-through batch count/cells/bytes/peak cells、append/flush/peak buffer、relationship/temporary IO 和 package writer target entry；重复 rewrite 还需验证 superseded temporary file 立即回收。
- Dirty In-memory projection 可在写包前进入 internal Patch stage，但只有 package write 成功后才能清 dirty、增加 handoff count 和推进 watermark；失败 retry 必须用当前 CellStore 覆盖 stale stage。
- In-memory 默认拒绝已知有损 materialization，并以 public typed diagnostic 报告稳定语义分类和 source cell context；显式 lossy 才拍平。
- Preservation 不等于 tables/drawings/comments/VBA/pivot/custom XML semantic editing。
