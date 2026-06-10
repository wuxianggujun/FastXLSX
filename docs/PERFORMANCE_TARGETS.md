# 性能目标

## 目标基准

FastXLSX 的性能目标是明显优于 C++ `OpenXLSX` 的大数据写入场景。

同时，FastXLSX 也应该和 `xlnt` 的 streaming API 建立基准对比。
目标不是在所有功能上压过 `xlnt`，而是在流式写入、模板替换和 part-level rewrite
这些主路径上保持更低的内存占用和更直接的 API。编辑能力仍是核心目标，但必须区分
小文件 In-memory 随机编辑、已有文件 Patch 编辑和大文件受控流式重写。

本文件中的 1,000 万 / 5,000 万 cells 数字是目标和验收方向，不是当前已记录的
实测结论。任何性能声明必须来自显式 opt-in benchmark，并记录数据规模、压缩设置、
字符串策略、package-entry source mode、总耗时、峰值内存、输出大小和办公软件打开结果。
file-backed/chunked worksheet package entry 可以作为降低 worksheet part finalization
内存副本的实现事实记录，但不能直接写成已验证的大文件性能或完整低内存结论。

重点不是在所有功能上最快，而是在以下场景建立优势：

- 百万行级数据导出。
- 多 sheet 批量导出。
- 数字、日期、布尔值密集写入。
- 字符串较多但可配置 inlineStr / sharedStrings。
- 模板 sheet 替换。

## 初始性能目标

### 1,000 万 cells

```text
数据形态：数字为主
内存目标：< 256 MB
耗时目标：30s - 180s
```

### 5,000 万 cells

```text
数据形态：数字 + 少量字符串
内存目标：< 1 GB
耗时目标：5min - 20min
```

### 字符串密集场景

```text
inlineStr 模式：优先低内存
sharedStrings 模式：优先文件体积
```

## 热点路径

优先优化：

- 单元格 XML 编码。
- XML 字符转义。
- 数字转字符串。
- 单元格引用生成。
- ZIP 压缩等级。
- sharedStrings 去重。
- 行级 buffer 复用。

## 对比对象

第一阶段基准至少覆盖：

- `OpenXLSX`：DOM 主路径写入和保存。
- `xlnt`：streaming writer 和常规 workbook API。
- `FastExcel`：旧项目中已有的高性能写入路径。

基准指标：

- 数据规模。
- 压缩设置。
- 字符串策略。
- 总耗时。
- 峰值内存。
- 输出文件大小。
- Excel / WPS / LibreOffice 打开兼容性。

## 非目标

第一阶段不追求：

- 完整公式计算引擎。
- 任意复杂图表生成。
- 任意大型 worksheet 像二维数组一样 O(1) 随机编辑。
- 透视表创建。
- 宏编辑。

这不排除大文件的受控编辑，例如 sheet 替换、range patch、模板填充或 event-stream
worksheet rewrite。

## 压缩策略

目标上必须支持压缩等级配置；当前 public compression-level 配置尚未集成，
不能据此宣称已经支持压缩等级调优。

```text
store       最快，文件最大
level 1-3   推荐默认，性能和体积平衡
level 6+    文件更小，但 CPU 成本更高
```

## 内存原则

大数据路径内存应该主要由以下部分组成：

- 当前行 buffer。
- XML 输出 buffer。
- file-backed worksheet part 的临时文件/句柄元数据，或 chunk buffer。
- 样式 registry。
- 字符串策略状态。
- package / ZIP writer 状态。

不得持有完整 worksheet cell matrix。

## 手工 Benchmark Workflow

当前最小 benchmark 入口是 opt-in 手工工具，不进入默认 CTest：

```powershell
cmake --preset windows-nmake-release-benchmark
cmake --build --preset windows-nmake-release-benchmark --target fastxlsx_bench_streaming_writer
.\build\windows-nmake-release-benchmark\benchmarks\fastxlsx_bench_streaming_writer.exe `
  --scenario numeric --rows 100000 --cols 10 --sheets 1 `
  --string-pattern mixed `
  --string-strategy inline `
  --output .\build\windows-nmake-release-benchmark\benchmarks\fastxlsx-bench-numeric.xlsx `
  --result .\build\windows-nmake-release-benchmark\benchmarks\fastxlsx-bench-numeric.json
```

如果需要验证 opt-in minizip backend，使用
`windows-nmake-release-benchmark-minizip` preset。生成的 JSON 中
`office_open` 初始为 `not_run`；只有实际用 Excel / WPS / LibreOffice 打开后，
才能把兼容性结果写成已验证事实。
benchmark 结果还应记录 package-entry source mode（`in-memory` / `file-backed` /
`chunked`）、ZIP backend、压缩等级、峰值内存和临时 worksheet part footprint。没有这些
数据时，不要声称完整低内存写出。
当前压缩等级配置只存在于 internal `PackageWriterOptions`，默认新建 workbook
路径仍使用 backend default；benchmark CLI 若未来暴露该选项，结果必须明确记录
实际传入的 `-1`、`0` 或 `1..9`，不能只写“minizip enabled”；`0` 应记录为
minizip no-compression/stored output，而不是 DEFLATE 压缩结果。
当前 internal package writer 会拒绝需要 Zip64 的 entry count 或单 entry
未压缩大小，因此 file-backed/chunked worksheet entry 仍不能被写成大文件或
Zip64 benchmark 证据。

当前 `fastxlsx_bench_streaming_writer` JSON schema version 为 `4`，记录字符串分布和
package 元数据：

```json
{
  "benchmark_schema_version": "4",
  "string_pattern": "mixed",
  "string_cells": 2500,
  "unique_string_values": 2001,
  "duplicate_string_cells": 499,
  "string_dedup_ratio": 0.1996,
  "package_entry_source_mode": "worksheet-file-backed-chunked",
  "temporary_worksheet_part_footprint": "worksheet-body-file-bytes",
  "temporary_worksheet_part_footprint_bytes": 317823
}
```

`string_pattern` 支持 `mixed`、`repeated` 和 `unique`。`mixed` 保持旧的部分重复
字符串生成方式；`repeated` / `unique` 用于对比 sharedStrings 在高重复和高唯一
字符串场景下的文件体积、耗时和峰值内存。该字段只是 benchmark 输入描述，不代表
sharedStrings 已生产就绪。

`string_cells`、`unique_string_values`、`duplicate_string_cells` 和
`string_dedup_ratio` 描述输入字符串分布；`string_dedup_ratio` 是
`duplicate_string_cells / string_cells`，没有字符串时为 `0`。这些字段对
inline/shared 两种策略都有效，用于解释 benchmark 输入，不代表实际
`xl/sharedStrings.xml` 内存占用已经受控，也不替代峰值内存测量。

`package_entry_source_mode` 记录当前 worksheet entry finalization 经过
file-backed/chunked source；`temporary_worksheet_part_footprint_bytes` 由
benchmark-only instrumentation 累计 worksheet body row XML 写入字节数。该字段不包含
worksheet header/footer、小型 XML parts、sharedStrings 临时文件、media 文件、ZIP/backend
内部缓冲或 OS 文件系统开销；它只能作为临时 worksheet body footprint 指标，不能单独用于
完整低内存或大文件性能结论。

如果不传 `--output` / `--result`，当前工具默认把结果写到 benchmark target 的
binary dir，例如 `build/windows-nmake-release-benchmark/benchmarks/`。手工工具会
拒绝超过 1024 个 worksheet 的输入；这只是 benchmark 工具边界，不代表
FastXLSX public API 的 worksheet 数量承诺。

### P6 Benchmark Matrix Helper

`tools/run_benchmark_matrix.py` 是现有 benchmark 可执行文件的本地外层 runner。
它不改 C++ benchmark schema，不进入默认 CTest/CI，也不自动证明 Office 兼容性。
默认小矩阵会覆盖 numeric、mixed、strings，inline/shared，以及 repeated/unique
字符串分布；不同 ZIP backend 应分别传入对应 preset 构建出的 benchmark exe。

默认 stored-bootstrap runner 示例：

```powershell
py tools\run_benchmark_matrix.py `
  --bench-exe build\windows-nmake-release-benchmark\benchmarks\fastxlsx_bench_streaming_writer.exe `
  --output-dir build\qa\benchmark-matrix `
  --rows 1000 --cols 10 --sheets 1 `
  --verify-openpyxl
```

opt-in minizip runner 示例：

```powershell
py tools\run_benchmark_matrix.py `
  --bench-exe build\windows-nmake-release-benchmark-minizip\benchmarks\fastxlsx_bench_streaming_writer.exe `
  --output-dir build\qa\benchmark-matrix-minizip `
  --rows 1000 --cols 10 --sheets 1 `
  --verify-openpyxl
```

runner 会保留每个 case 的 `.xlsx` 和原始 schema-v4 `.json`，并写
`benchmark-matrix-report.json` 聚合命令、输入规模、结果字段和可选 `openpyxl`
读取结果。`office_open` 字段仍保持 benchmark 工具原始值 `not_run`；本机 Excel
验证是独立步骤，可用 `tools/verify_benchmark_matrix_excel.ps1` 只读打开 report 中的
部分 workbook 并核对 `Sheet1` 使用范围和首尾值。不要把该 helper 的小规模输出写成
sharedStrings 生产就绪、完整低内存、大文件性能或 Google Benchmark 结论。

## 当前手工 Benchmark 记录

2026-06-07 本机 VS2026 / NMake release benchmark preset 下，使用
`fastxlsx_bench_streaming_writer` 对 `strings` 场景做了 4 组小规模对比。输入规模均为
`rows=50000`、`cols=10`、`sheets=1`、`cells=500000`、`string_ratio=1`，
ZIP backend 为 `stored-bootstrap` / `store`，package entry source mode 为
`worksheet-file-backed-chunked`，临时 worksheet footprint 指标为
`worksheet-body-file-bytes`。

| string_pattern | string_strategy | elapsed_ms | peak_memory_mb | worksheet_body_bytes | output_bytes |
| --- | --- | ---: | ---: | ---: | ---: |
| repeated | inline | 493 | 4.97266 | 27927834 | 27931711 |
| repeated | shared | 392 | 4.98828 | 16927834 | 16932289 |
| unique | inline | 658 | 4.97266 | 30866774 | 30870651 |
| unique | shared | 1045 | 70.1055 | 19316724 | 33260102 |

本机 Excel COM 只读打开了上述 4 个输出文件，并验证 `Sheet1` 使用范围为
`50000 x 10`。repeated 样例的 `A1` / `J50000` 均为 `repeat`；unique 样例的
`A1` 为 `s1-r1-c1`，`J50000` 为 `s1-r50000-c10`。benchmark JSON 中的
`office_open` 字段仍保持工具写出的 `not_run`；这次 Excel COM 结果是文档记录的
独立本机检查，不是 benchmark 工具自动写回的字段。

这组记录只能用于小规模 sharedStrings 趋势观察：高重复字符串下 sharedStrings 输出更小，
高唯一字符串下 sharedStrings 的峰值内存和输出体积明显上升。不要据此宣称
sharedStrings 生产就绪、sharedStrings 是默认最佳策略、完整低内存写出已验证、
10,000,000-cell 级别性能已记录、Google Benchmark 已接入、Zip64 / true package
streaming / existing-file editing 已验证，或办公套件兼容性已全面验证。
