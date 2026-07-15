# 性能目标与证据

FastXLSX 的性能方向是 Streaming 大数据写入与 Patch part-level rewrite；任何性能声明都必须可复现、可追溯并注明边界。

## 设计目标

- Streaming worksheet row/cell 热路径不进入 DOM 或 dense matrix。
- Patch 未修改 part copy-original，避免整包语义重建。
- In-memory 只服务 small-file random editing，并受 guardrail 限制。
- ZIP backend、compression、string strategy、rewrite strategy 必须作为结果维度记录。

## 声明所需字段

- dataset：sheets、rows、columns、cell/value/string 分布。
- build：git revision、compiler、preset、backend、compression。
- strategy：inline/shared strings、rewrite path、临时文件/内存口径。
- result：elapsed、output size、memory/estimate 的准确含义。
- compatibility：ZIP/XML 检查与 Office/openpyxl/LibreOffice 状态。

Worksheet body instrumentation 不是 peak RSS，也不包含 package assembly、sharedStrings、media、ZIP buffers 或 OS cache。

当前 Streaming benchmark executable 使用 schema v5，分列 generation、package close、million-cells/s、body buffer peak/flush count 与 close 后 active temporary file count。Patch `WorkbookEditor` benchmark executable 使用 schema v13：保留 v6 的 direct-range/single-pass transform、parser/source-callback/coalesced/action traffic、output batching、relationship scanner input calls/bytes/boundary carry/slow-path tags、relationship/temporary IO、package writer/target entry 与 raw compressed-copy telemetry，保留 v7 staged CRC、v8 aggregate simple inline-string fast path、v9 compression CPU、v10 complete-cell、v11 pass-through batch 与 v12 canonical inline-string telemetry，并新增 canonical complete-cell count/bytes/formula/inline-string counters。Patch matrix v7 继续支持在同一 source 上重复传入 output level，并验证各 source profile 的 complete-cell type traffic。历史 schema-v4 至 v12 artifact 仍可汇总或验证，但不能提供其 schema 尚未记录的新 telemetry；release 当前 Patch telemetry 以最新 validated v13 bundle 为准。

## Tracked Evidence

Release note 或性能文档只能引用 `benchmarks/evidence/<bundle>/manifest.json` 声明的 artifact。Schema 位于 `benchmarks/evidence/benchmark-evidence-manifest.schema.json`，validator 会检查字段、路径逃逸、SHA-256 和 claim 引用。

```powershell
py -3 tools/validate_benchmark_evidence.py --root benchmarks/evidence
```

Streaming 重复矩阵使用 `tools/run_benchmark_matrix.py`，Patch 重复矩阵使用 `tools/run_patch_benchmark_matrix.py`。默认每个 case 先 warm-up 1 次，再保留 3 次 measured JSON；矩阵报告使用 total elapsed median 对应的真实 run 作为代表结果，并同时保留每次 raw result 与 min/median/max。Openpyxl 验证在 benchmark 进程计时结束后执行，不得混入 benchmark timing。

当前没有 bundle 时，不得从本地 build 目录或 Markdown 摘抄形成 release claim。

当前 tracked bundle：

- [`2026-07-12-windows-msvc-streaming-mixed-inline`](../benchmarks/evidence/2026-07-12-windows-msvc-streaming-mixed-inline/manifest.json)：production Streaming、1,000,000 cells、20% mixed repeated strings、inline strings、minizip-ng DEFLATE level 6；仅允许引用 manifest 中的单机单次观测和 ZIP/XML/openpyxl 验证，Office 为 `not_run`。
- [`2026-07-12-windows-msvc-streaming-strategy-matrix`](../benchmarks/evidence/2026-07-12-windows-msvc-streaming-strategy-matrix/manifest.json)：同一 Windows/MSVC production 环境下的 7-case Streaming 策略矩阵；每个 case 为 1 次 warm-up + 3 次 measured run，保留 min/median/max、全部 measured 指标和代表 workbook 的 openpyxl 验证，Office 为 `not_run`。
- [`2026-07-12-windows-msvc-patch-matrix`](../benchmarks/evidence/2026-07-12-windows-msvc-patch-matrix/manifest.json)：同机 production `WorkbookEditor` Patch 矩阵；1,000,000 numeric cells、source/output DEFLATE level 6，覆盖 no-op、core/app metadata 与 1,000-cell targeted replace/upsert，分列 copied logical、copied source/output compressed 与 rewritten bytes，代表 workbook 通过 openpyxl，Office 为 `not_run`。
- [`2026-07-12-windows-msvc-patch-bounded-direct-range`](../benchmarks/evidence/2026-07-12-windows-msvc-patch-bounded-direct-range/manifest.json)：同机同数据集复跑优化后的 public Patch 矩阵；DEFLATE strict targeted replace 使用 one-inflate target-only direct-range staging，并保留 no-op、document-properties、upsert 对照、min/median/max、byte accounting 和 openpyxl 代表 workbook 验证。
- [`2026-07-13-windows-msvc-streaming-schema-v5`](../benchmarks/evidence/2026-07-13-windows-msvc-streaming-schema-v5/manifest.json)：256 KiB body batching 后的 7-case schema-v5 Streaming 矩阵；分列 generation/package-close、body buffer high-water/flush、成功 close 后 active temporary files、peak working set 与代表输出 openpyxl 验证。
- [`2026-07-13-windows-msvc-patch-single-pass-schema-v5`](../benchmarks/evidence/2026-07-13-windows-msvc-patch-single-pass-schema-v5/manifest.json)：同机 schema-v5 public Patch 矩阵；记录 direct-range 与 single-pass upsert 的 scan/match/insert/staged bytes、transform/commit、peak working set、byte accounting 与代表输出验证。
- [`2026-07-13-windows-msvc-openxlsx-reference`](../benchmarks/evidence/2026-07-13-windows-msvc-openxlsx-reference/manifest.json)：OpenXLSX 0.4.1 public workbook API reference；numeric/mixed 主场景采用 1 次 warm-up + 3 次 measured run，记录 API/save 设置差异、elapsed、output size、peak working set 与 openpyxl 检查；repeated/unique 只作为明确标记的 exploratory 结果。
- [`2026-07-13-windows-msvc-streaming-compression-profile`](../benchmarks/evidence/2026-07-13-windows-msvc-streaming-compression-profile/manifest.json)：同机 warmed 2+5 repeated protocol，覆盖 numeric/mixed InlineString 的 DEFLATE level 1/3/6；记录 elapsed/generation/package-close、输出大小、peak working set、buffer/resource lifecycle，并用独立同 revision pass 做 openpyxl 验证。
- [`2026-07-13-windows-msvc-patch-raw-copy-profile`](../benchmarks/evidence/2026-07-13-windows-msvc-patch-raw-copy-profile/manifest.json)：同机 warmed 2+5 Patch compression profile；记录 exact raw compressed-payload validation、entry/byte telemetry、no-op/document-properties 与完整 worksheet rewrite 的 level 1/3/6 save/size/memory 取舍，12 个代表输出通过 openpyxl。
- [`2026-07-13-windows-msvc-patch-rewrite-batching-profile`](../benchmarks/evidence/2026-07-13-windows-msvc-patch-rewrite-batching-profile/manifest.json)：同机 warmed 2+5 targeted-upsert profile；覆盖 1,000,000-cell level 1/6 与 5,000,000-cell level 1，分列 256 KiB batching、source scan/action residual、relationship/temporary IO、target worksheet writer timing、peak working set 与 openpyxl 代表输出。
- [`2026-07-13-windows-msvc-patch-event-coalescing-profile`](../benchmarks/evidence/2026-07-13-windows-msvc-patch-event-coalescing-profile/manifest.json)：同机 warmed 2+5 event/action profile；覆盖 5,000,000-cell numeric 对照，以及 1,000,000-cell numeric、mixed inline、sharedStrings、formula + external hyperlink 分布，分列 parser/source callback/coalesced/action traffic、bounded output、relationship/package timing、peak working set 与 openpyxl/relationship 验证。
- [`2026-07-14-windows-msvc-patch-relationship-selective-scan-profile`](../benchmarks/evidence/2026-07-14-windows-msvc-patch-relationship-selective-scan-profile/manifest.json)：同机 warmed 2+5 schema-v6 relationship scanner profile；5,000,000-cell numeric transform 只输入 1 次 / 167 bytes metadata，1,000,000-cell formula + external hyperlink transform 输入 1 次 / 303 bytes 并访问 2 个 slow-path tags，median scanner 分别为 3/8 us，代表输出通过 openpyxl 与 hyperlink target 验证。该轮总耗时受系统负载影响，不用于 overall throughput 提升声明。
- [`2026-07-14-windows-msvc-patch-staged-crc-reuse-profile`](../benchmarks/evidence/2026-07-14-windows-msvc-patch-staged-crc-reuse-profile/manifest.json)：同机 schema-v7 level-6 staged CRC profile；1,000,000-cell numeric upsert 的 isolated target-entry residual median 从 131,892 us 降至 603 us，median staged CRC validation 为 152 us，numeric/formula median process peak working set 均约 8.34 MB；formula representative 通过 openpyxl 与 external hyperlink target 验证。整体 elapsed 只作观测，不泛化为 arbitrary-workbook 吞吐提升。
- [`2026-07-15-windows-msvc-patch-inline-string-fast-path-profile`](../benchmarks/evidence/2026-07-15-windows-msvc-patch-inline-string-fast-path-profile/manifest.json)：同机 warmed 2+5 schema-v8 mixed-inline profile；simple wrapper fast path 将 parser/source-callback/action traffic 分别降低 22.73%/17.24%/31.23%，transform/residual median 分别降低 21.10%/21.48%，median process peak working set 从 9.31641 MB 变为 9.32031 MB。Formula + external hyperlink guard 的 fast-path count 为 0，代表输出通过 openpyxl 与 target 验证。
- [`2026-07-15-windows-msvc-patch-compression-cpu-profile`](../benchmarks/evidence/2026-07-15-windows-msvc-patch-compression-cpu-profile/manifest.json)：同机 schema-v9/v3 targeted-upsert compression profile；numeric/mixed-inline 使用 2+5、formula + external hyperlink 使用 1+3，覆盖 level 1/3/6/9 或 guard level 1/6，分列 requested level、package/target-entry process CPU 与 DEFLATE writer CPU envelope。Level 1 对记录 workload 是吞吐配置，public 默认未改变；全部代表输出通过 openpyxl，hyperlink target 保留。
- [`2026-07-15-windows-msvc-patch-complete-cell-coalescing-profile`](../benchmarks/evidence/2026-07-15-windows-msvc-patch-complete-cell-coalescing-profile/manifest.json)：同机 schema-v10/v4 level-1 targeted-upsert profile；numeric/mixed-inline 使用 2+5、formula + external hyperlink 使用 1+3。每类 profile 至少合并 999,982 个完整 source cell、最多 18 个 fallback；total editor median 较紧邻同协议 schema-v9 baseline 降低 51.18%/33.13%/25.85%，median process peak working set 变化小于 0.03 MB。代表输出通过 openpyxl 3.1.5，hyperlink target 保留。
- [`2026-07-15-windows-msvc-patch-pass-through-batching-profile`](../benchmarks/evidence/2026-07-15-windows-msvc-patch-pass-through-batching-profile/manifest.json)：同机 schema-v11/v5 level-1 targeted-upsert profile；pass-through batching 将 transform action/output append 降低 99.82%，numeric/formula-hyperlink total editor median 较 schema-v10 baseline 降低 21.21%/15.69%，mixed-inline 从 1205 ms 变为 1222 ms，不声明吞吐提升。Median process peak working set 降低 0.21–0.22 MB，pending output buffer 低于 29 KiB；代表输出通过 openpyxl 3.1.2，hyperlink target 保留。
- [`2026-07-15-windows-msvc-patch-canonical-inline-string-profile`](../benchmarks/evidence/2026-07-15-windows-msvc-patch-canonical-inline-string-profile/manifest.json)：同机 schema-v11/v5 → v12/v6 back-to-back mixed-inline A/B；total/transform/source scan median 分别降低 14.36%/24.71%/22.62%，333,327 个 payload 全部命中 canonical literal path，6 个跨窗口 candidate 保留原 parser，median process peak working set 只增加约 0.004 MB。Numeric/formula-hyperlink matrix 只作零命中与兼容性 guard，其 elapsed 不用于性能声明。
- [`2026-07-15-windows-msvc-patch-canonical-complete-cell-profile`](../benchmarks/evidence/2026-07-15-windows-msvc-patch-canonical-complete-cell-profile/manifest.json)：同机 schema-v12/v6 ↔ v13/v7 双顺序 A/B；三类 1,000,000-cell workload 的 parsed events 降低 73.53%–76.92%，source scan/action median 在两种顺序中均降低 27.99%–54.85%，paired peak working set 未增加。Package writer/temporary IO 随顺序波动，因此 bundle 不声明通用 total-editor 提升。
- 十九个 bundle 都不是跨机器、跨数据规模或泛化“高性能/低内存”证据；当前构成为 4 个 production Streaming、14 个 Patch 和 1 个 OpenXLSX reference bundle。每个 bundle 只支持其 manifest 中记录的同机同数据集结论；历史 schema-v4 至 v12 继续作为回归基线，release 当前 Patch telemetry 以 schema-v13 bundle 为准。

## Streaming 字符串策略

- `InlineString` 是默认值。它不保存 workbook 级唯一字符串表，字符串基数未知、接近全唯一或调用方要求稳定低内存时应优先使用。
- `SharedString` 只在调用方已知字符串高度重复且可接受内存随唯一值增长时使用。最新 schema-v5 的 20% mixed workload 中，inline/shared elapsed median 为 1248/1359 ms、peak working set median 为 6.88672/6.86719 MB；该数据集不支持 shared 更快的结论。
- 对 1,000,000 个全部唯一字符串，本机 inline/shared elapsed median 为 1498/3356 ms，process peak working set median 为 6.92578/123.039 MB，output 为 3593977/5810478 bytes；该 workload 应使用 inline。
- 不增加自动策略：单遍 Streaming writer 在看到未来输入前无法可靠判断最终字符串基数；自动切换需要缓存历史数据、重写已输出 cell encoding 或接受不可预测的内存增长。调用方应依据业务数据分布选择，并用矩阵工具验证自己的 workload。

## 当前结论与缺口

- 在 schema-v5 level 6 策略矩阵中，Streaming 创建路径已具备稳定的 file-backed worksheet footprint；除 unique sharedStrings 外，各 case 的 process peak working set median 为 6.87–6.93 MB，body buffer peak 小于 256 KiB，成功 close 后 active worksheet temporary files 为零。
- 最新 warmed compression profile 中，numeric/mixed-inline 的 level 1 median elapsed 为 322/406 ms，level 6 为 955/981 ms；level 1 输出增大 9.62%/21.63%，全部 measured peak working set 为 6.28125–6.32812 MB。Package close 在六个 case 中仍是主要阶段；吞吐优先 workload 应先评估 level 1，而不是继续微调 cell XML encoding。
- 旧 Patch baseline bundle 的同机 1,000,000 numeric cells、DEFLATE level 6、1,000-cell targeted replace total/mutation median 为 5325/4027 ms，peak working set median 为 7.96484 MB。优化 bundle 在相同协议下为 1529/489 ms 与 7.80859 MB，total 与 mutation 分别降低 71.29%/87.86%；该比较只支持本机该 workload。
- Event/action coalescing 后的 5,000,000-cell numeric level 1 upsert 将 26,000,006 个 parser event 降为 16,000,068 个 source callback 和 11,001,084 个 transform action；total/mutation/transform/residual median 为 8004/6210/5180.113/4059.595 ms，相对紧邻的同机同协议 rewrite batching profile 分别降低 5.77%/10.30%/11.53%/14.21%。Process peak working set median 为 8.79297 MB，owned batching buffer 为 262,144 bytes；该比较仍受单机调度影响，只支持记录 workload。
- 1,000,000-cell distribution profile 已覆盖 numeric、mixed inline strings、sharedStrings、formula metadata 与 external hyperlink relationship；各代表输出通过 openpyxl，formula fixture 额外以流式 worksheet 标签 + 小型 `.rels` 检查确认 A1 hyperlink binding/target。Mixed/formula elapsed 仅作分布覆盖，不用于广义性能结论。
- 同机 1,000,000-cell numeric/mixed public writer workload 中，FastXLSX Streaming median 为 1583/1248 ms，OpenXLSX 0.4.1 workbook API 为 3180/3292 ms，对应 2.01×/2.64×吞吐比；peak working set 为 6.87109/6.88672 MB 对 395.258/403.957 MB。该结论只覆盖这两个数据分布、API adapter 与机器。
- Production minizip-ng 的 method-matching unchanged entry 已使用 exact raw compressed-payload copy。最新 no-op profile raw-copy 7 个 entry / 3,128,791 bytes，level 1/3/6 median save 为 19/21/25 ms；document-properties profile raw-copy 5 个 entry / 3,128,295 bytes 并重写 1,039 logical bytes，median save 为 15/24/41 ms。该能力不保留 local header、central directory、extra fields 或 package layout。
- Schema-v6 bundle 中，5,000,000-cell numeric transform 的 relationship scanner 只接收 167 metadata bytes，1,000,000-cell formula + external hyperlink transform 接收 303 bytes 并保留 relationship target；median scanner 为 3/8 us，owned output buffer 保持 256 KiB，median process peak working set 为 8.25781/8.28516 MB。该轮 total elapsed 受系统负载影响，不用于提升声明。
- Schema-v8 bundle 中，1,000,000-cell mixed-inline level-1 upsert 有 333,327 个完整 simple wrapper、7,895,685 bytes 进入 fast path，6 个跨窗口 candidate 回退原 parser；parser/source-callback/action traffic 分别降低 22.73%/17.24%/31.23%，transform/residual median 分别降低 21.10%/21.48%，median process peak working set 只增加约 0.004 MB。Formula + external hyperlink guard 不进入 fast path，并保留 target。
- Schema-v9 compression CPU profile 中，numeric/mixed-inline/formula-hyperlink 的 level 1 对 level 6 median save 比分别为 1:2.12、1:3.05、1:3.49，DEFLATE writer CPU envelope 比为 1:4.38、1:4.36、1:6.78；level 1 输出分别增大 9.73%、14.51%、18.38%。Numeric level 9 比 level 6 只缩小 0.11%，median save 却为 9296 ms 对 1033 ms。该证据支持吞吐 workload 先显式选择 level 1，不支持静默修改 public 默认值或直接切换 backend。
- Schema-v12/v6 back-to-back mixed-inline A/B 中，total editor、transform 与 source scan/action median 分别降低 14.36%/24.71%/22.62%，为后续 generic scanning profile 提供紧邻 baseline。
- 最新 schema-v13/v7 双顺序 A/B 中，canonical complete-cell path 将 numeric/mixed-inline/formula-hyperlink parsed events 降低 76.92%/73.53%/76.92%，coalescer input 降低 80.00%/76.92%/80.00%，source callback 与 transform action 不变。两种顺序的 transform median 分别降低 27.26%–52.72%、27.44%–46.12%、34.95%–35.35%，source scan/action 降低 30.01%–54.85%、27.99%–47.39%、36.34%–37.18%；paired peak working set 未增加。Package writer/temporary IO 随顺序波动，numeric forward-order total 增加 5.51%，所以不声明通用 total-editor 提升。下一轮应在隔离负载下 profile staged temporary output → package target-entry handoff。
- Large-file random editing 不是当前目标；大 worksheet 顺序 Patch 采用 file-backed/chunked transformation，并要求单独的正确性、临时文件和 process peak working set 证据。

## 当前 Profiling 门禁

- Streaming matrix 必须证明 body buffer peak 不超过 256 KiB、flush count 非零、成功 close 后 active temporary file count 为零，并同时报告 generation 与 package close 时间，避免把压缩成本误归因于 XML encoding。
- Patch upsert/fallback matrix 必须证明 single-pass mode、生效的 scanned/matched/inserted counts、非零 staged output bytes、精确 dimension、relationship policy 与保存后可重开；同时分列 parsed/source-callback/coalesced/action traffic、aggregate/canonical inline-string count/bytes/fallback、complete-cell count/bytes/fallback、canonical complete-cell count/bytes/formula/inline-string counters、pass-through batch count/cells/bytes/peak cells、append/flush/peak owned buffer、relationship scanner input calls/bytes/boundary carry/slow-path tags、relationship/temporary IO、package writer target-entry timing、staged CRC reuse/validation 与 process CPU，避免把 XML scan、CRC、IO 与 DEFLATE writer 混为一个 elapsed。涉及 relationship-bearing fixture 时必须同时验证 worksheet binding 与 `.rels` target。
- Patch compression matrix 必须分列 source/output method、requested level、package/target-entry process CPU、DEFLATE writer CPU envelope、raw-copy entry names/count/bytes、copied source/output compressed bytes 与 rewritten bytes；每个 planned raw entry 必须比较 exact compressed payload 且 DEFLATE writer CPU 为 0，不能仅比较 CRC/size，也不能将结果表述为完整 ZIP record passthrough。Writer CPU 包含 backend bookkeeping，不得命名为纯 encode CPU。
- 重复 Patch rewrite 测试必须证明 superseded temporary file 在新事务提交后立即删除；失败注入则证明旧文件和旧 state 保留、新 staged file 被 RAII 清理。
- OpenXLSX 对比只接受同机、同 cell/value/string 分布与相同 warm-up/measured protocol；compression/save 设置应尽量对齐。若 reference public API 不暴露相同压缩级别，必须记录双方实际设置与 output size，并明确它不是 identical-backend microbenchmark。只在实际 workload 上领先时，才可写“该 workload 超过 OpenXLSX”。

## 禁止措辞

- 未给数据集和 backend 的“高性能”。
- 将 estimate/instrumentation 写成 peak memory。
- 将 stored bootstrap 结果泛化到 DEFLATE production。
- 将 `office_open=not_run` 写成 Office 验证通过。
- 用 preservation 或单次 smoke 代替语义正确性/性能证据。
