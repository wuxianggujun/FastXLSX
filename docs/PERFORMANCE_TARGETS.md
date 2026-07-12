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

## Tracked Evidence

Release note 或性能文档只能引用 `benchmarks/evidence/<bundle>/manifest.json` 声明的 artifact。Schema 位于 `benchmarks/evidence/benchmark-evidence-manifest.schema.json`，validator 会检查字段、路径逃逸、SHA-256 和 claim 引用。

```powershell
py -3 tools/validate_benchmark_evidence.py --root benchmarks/evidence
```

重复矩阵使用 `tools/run_benchmark_matrix.py`。默认每个 case 先 warm-up 1 次，再保留 3 次 measured schema-v4 JSON；矩阵报告使用 `elapsed_ms` median 对应的真实 run 作为兼容 consumer 的代表结果，并同时保留每次 raw result 与 min/median/max。Openpyxl 验证在 benchmark 进程计时结束后执行，不得混入 `elapsed_ms`。

当前没有 bundle 时，不得从本地 build 目录或 Markdown 摘抄形成 release claim。

当前 tracked bundle：

- [`2026-07-12-windows-msvc-streaming-mixed-inline`](../benchmarks/evidence/2026-07-12-windows-msvc-streaming-mixed-inline/manifest.json)：production Streaming、1,000,000 cells、20% mixed repeated strings、inline strings、minizip-ng DEFLATE level 6；仅允许引用 manifest 中的单机单次观测和 ZIP/XML/openpyxl 验证，Office 为 `not_run`。
- 该 bundle 不是跨机器比较、重复性统计、inline/shared 策略比较或泛化“高性能/低内存”证据。

## 禁止措辞

- 未给数据集和 backend 的“高性能”。
- 将 estimate/instrumentation 写成 peak memory。
- 将 stored bootstrap 结果泛化到 DEFLATE production。
- 将 `office_open=not_run` 写成 Office 验证通过。
- 用 preservation 或单次 smoke 代替语义正确性/性能证据。
