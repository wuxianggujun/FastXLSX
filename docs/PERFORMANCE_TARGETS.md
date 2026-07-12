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

当前 benchmark executable 使用 schema v5：Streaming 分列 generation、package close、million-cells/s、body buffer peak/flush count 与 close 后 active temporary file count；Patch 分列 direct-range 与 single-pass transform 的 source scan、match/insert、staged bytes、transform/commit 时间。历史 schema-v4 artifact 仍可汇总，但不能提供新增资源生命周期证据。

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
- 四个 bundle 都不是跨机器、跨数据规模或泛化“高性能/低内存”证据；三个矩阵只支持各自 manifest 中记录的同机同数据集结论。它们记录的是 schema-v4/旧热路径结果；256 KiB Streaming batching 与 Patch single-pass fallback 必须产生新的 validated bundle 后才能替换这些数值。

## Streaming 字符串策略

- `InlineString` 是默认值。它不保存 workbook 级唯一字符串表，字符串基数未知、接近全唯一或调用方要求稳定低内存时应优先使用。
- `SharedString` 只在调用方已知字符串高度重复、并接受内存随唯一值数量增长时评估。20% mixed repeated workload 的本机 median 为 1488 ms，相比 inline 的 2385 ms 低 37.61%。
- 对 1,000,000 个全部唯一字符串，本机 shared/inline 的 elapsed median 为 3798/2328 ms，process peak working set median 为 122.195/6.02344 MB，output 为 5810478/3593977 bytes；该 workload 应使用 inline。
- 不增加自动策略：单遍 Streaming writer 在看到未来输入前无法可靠判断最终字符串基数；自动切换需要缓存历史数据、重写已输出 cell encoding 或接受不可预测的内存增长。调用方应依据业务数据分布选择，并用矩阵工具验证自己的 workload。

## 当前结论与缺口

- 在上述单机、1,000,000-cell、DEFLATE level 6 范围内，Streaming 创建路径已具备稳定的 file-backed worksheet footprint；除 unique sharedStrings 外，各 case 的 process peak working set median 约为 6 MB。
- 旧 Patch baseline bundle 的同机 1,000,000 numeric cells、DEFLATE level 6、1,000-cell targeted replace total/mutation median 为 5325/4027 ms，peak working set median 为 7.96484 MB。优化 bundle 在相同协议下为 1529/489 ms 与 7.80859 MB，total 与 mutation 分别降低 71.29%/87.86%；该比较只支持本机该 workload。
- 优化 bundle 的 targeted upsert total/mutation median 为 4353/2968 ms、peak 为 8.01562 MB；这是旧 analysis/output 双扫描实现的历史基线。当前实现已改为 single-pass source-order transform，但在 schema-v5 矩阵完成前不得用未验证数值替换历史结果。Targeted replace/upsert 仍各重写完整 logical worksheet XML。
- No-op copy-original 当前只验证 CRC/logical size，并在 output 重新封装/可能重新压缩 entry；不声称 raw compressed-byte passthrough。后续优先依据 profiling 评估 raw-copy backend、compression 与 allocation 成本，而不是扩大 In-memory guardrail。
- Large-file random editing 不是当前目标；大 worksheet 顺序 Patch 采用 file-backed/chunked transformation，并要求单独的正确性、临时文件和 process peak working set 证据。

## 当前 Profiling 门禁

- Streaming matrix 必须证明 body buffer peak 不超过 256 KiB、flush count 非零、成功 close 后 active temporary file count 为零，并同时报告 generation 与 package close 时间，避免把压缩成本误归因于 XML encoding。
- Patch upsert/fallback matrix 必须证明 single-pass mode、生效的 scanned/matched/inserted counts、非零 staged output bytes、精确 dimension、relationship policy 与保存后可重开。
- 重复 Patch rewrite 测试必须证明 superseded temporary file 在新事务提交后立即删除；失败注入则证明旧文件和旧 state 保留、新 staged file 被 RAII 清理。
- OpenXLSX 对比只接受同机、同 cell/value/string 分布、同 compression 与相同 warm-up/measured protocol；必须同时报告 elapsed、output size 与 process peak working set。只在实际 workload 上领先时，才可写“该 workload 超过 OpenXLSX”。

## 禁止措辞

- 未给数据集和 backend 的“高性能”。
- 将 estimate/instrumentation 写成 peak memory。
- 将 stored bootstrap 结果泛化到 DEFLATE production。
- 将 `office_open=not_run` 写成 Office 验证通过。
- 用 preservation 或单次 smoke 代替语义正确性/性能证据。
