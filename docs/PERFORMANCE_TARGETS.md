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
如果只需要检查 runner 自身的 case 解析、期望字符串分布和 matrix report shape，可运行
`py tools\run_benchmark_matrix.py --self-test`；该自检不调用 benchmark exe、不写
`.xlsx` / JSON，也不代表 Office/openpyxl 兼容性或性能数据。

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

### P6 Benchmark Summary Helper

`tools/summarize_benchmark_results.py` 用于把已有 schema-v4 benchmark JSON 或
`benchmark-matrix-report.json` 汇总成 Markdown / JSON 报告。它不调用 benchmark exe、
不生成 workbook、不修改原始 case JSON，也不自动完成 Office/openpyxl 验证；用途是把
手工性能数据沉淀为可审阅的吞吐量、峰值内存、输出体积和风险提示。
当输入是目录时，helper 会优先汇总同目录下的 `benchmark-matrix-report.json`，
避免把矩阵报告和其对应的 raw case JSON 重复统计；单独传入 schema-v4 case JSON 时，
仍按文件逐个汇总。

```powershell
py tools\summarize_benchmark_results.py --self-test
py tools\summarize_benchmark_results.py build\qa\benchmark-matrix `
  --output-markdown build\qa\benchmark-matrix\summary.md `
  --output-json build\qa\benchmark-matrix\summary.json
```

默认风险提示会标出 `sharedStrings + unique strings`、超过内存阈值的 case、超过
1GiB 的输出，以及 `office_open=not_run` 的兼容性缺口。报告中的
`worksheet_body_mib` 仍来自 `worksheet-body-file-bytes`，只代表 worksheet body row
XML 字节数，不是完整 RSS、完整 package 临时文件或 allocator-level footprint。
`--self-test` 只验证 summary helper 对已有 result / matrix report / directory input 的
collection、既有 summary JSON skip、non-benchmark JSON warning、Markdown rendering 和
Markdown / JSON output writes；它不运行 benchmark、不读取实际 benchmark artifact、不生成
workbook，也不替代 Office/openpyxl 检查。

### P6 Python Writer Comparison Helper

`tools/run_python_writer_benchmarks.py` 用于对 Python XLSX writer 做 opt-in
本地对比。当前支持：

- `xlsxwriter-constant-memory`
- `openpyxl-write-only`

该 helper 会在独立 worker process 中生成 workbook，记录 worker 内部写入耗时、
输出文件大小和可用时的进程工作集 / RSS 峰值，并可选用 `openpyxl` 只读打开输出
验证 `Sheet1` 行列数和首尾单元格。它不会接入默认 CTest / CI，不会安装 Python
包，不会修改 FastXLSX CMake/vcpkg 依赖，也不代表 OpenXLSX/xlnt 已完成对比。
OpenXLSX 和 xlnt 应通过后续独立 C++ adapter benchmark 接入，不能作为 FastXLSX
runtime dependency。

```powershell
py tools\run_python_writer_benchmarks.py --self-test
py tools\run_python_writer_benchmarks.py `
  --rows 100000 --cols 10 --sheets 1 `
  --verify-openpyxl `
  --output-dir build\qa\python-writer-benchmarks
```

结果会写入：

- `python-writer-benchmark-report.json`
- `python-writer-benchmark-summary.md`
- 每个 Python writer / case 对应的 `.xlsx` 和 worker JSON

注意：FastXLSX 当前默认 benchmark 使用 `stored-bootstrap` / `store`，Python
writer 通常生成压缩 ZIP。因此吞吐量和峰值内存可以用于判断写入路径强弱，输出文件
体积不能直接当作 apples-to-apples 结论；需要 minizip/DEFLATE FastXLSX benchmark
后才能做更公平的体积/压缩成本对比。

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

2026-06-10 本机 VS2026 / NMake release benchmark preset 下，使用
`tools/run_benchmark_matrix.py` 和 schema-v4 `fastxlsx_bench_streaming_writer`
对 `strings` 场景做了 4 组小规模矩阵。输入规模均为 `rows=10000`、`cols=10`、
`sheets=1`、`cells=100000`、`string_ratio=1`；ZIP backend 为
`stored-bootstrap` / `store`，package entry source mode 为
`worksheet-file-backed-chunked`，临时 worksheet footprint 指标为
`worksheet-body-file-bytes`。runner 还用 `openpyxl` 只读打开并验证了每个输出
workbook 的 `Sheet1` 首尾值；benchmark JSON 中的 `office_open` 字段仍保持
工具写出的 `not_run`。

| string_pattern | string_strategy | elapsed_ms | peak_memory_mb | worksheet_body_bytes | output_bytes |
| --- | --- | ---: | ---: | ---: | ---: |
| repeated | inline | 84 | 5.04297 | 5487834 | 5491711 |
| repeated | shared | 61 | 5.0625 | 3287834 | 3292289 |
| unique | inline | 111 | 5.03125 | 5986774 | 5990651 |
| unique | shared | 325 | 18.2383 | 3676724 | 6380102 |

2026-06-15 本机 Codex session 使用已有
`build/windows-nmake-release-benchmark/benchmarks/fastxlsx_bench_streaming_writer.exe`
做了一组手工性能探针。该 benchmark executable 是现有 release benchmark 产物，不代表
本轮源码改动已重新编译；测试机器为 Intel i5-11320H 4C/8T、约 16GB RAM。输入均为
`sheets=1`、ZIP backend 为 `stored-bootstrap` / `store`、package entry source mode 为
`worksheet-file-backed-chunked`，临时 worksheet footprint 指标为
`worksheet-body-file-bytes`。

| case | cells | string_pattern | string_strategy | elapsed_ms | peak_memory_mb | worksheet_body_bytes | output_bytes |
| --- | ---: | --- | --- | ---: | ---: | ---: | ---: |
| numeric-1m | 1000000 | mixed | inline | 724 | 4.99609 | 34897865 | 34901743 |
| numeric-10m | 10000000 | mixed | inline | 7526 | 5.02344 | 369707886 | 369711765 |
| numeric-50m | 50000000 | mixed | inline | 41315 | 5.01172 | 1784983846 | 1784987726 |
| strings-repeated-1m-inline | 1000000 | repeated | inline | 1363 | 5 | 55977845 | 55981723 |
| strings-repeated-1m-shared | 1000000 | repeated | shared | 778 | 5.01172 | 33977845 | 33982302 |
| strings-unique-1m-inline | 1000000 | unique | inline | 1230 | 5.00391 | 61966795 | 61970673 |
| strings-unique-1m-shared | 1000000 | unique | shared | 2095 | 112.988 | 38866735 | 66860126 |

`openpyxl 3.1.2` 只读打开了上述输出并验证 worksheet dimension：50M numeric 样例为
`1000000 x 50`，10M numeric 样例为 `1000000 x 10`，1M string 样例均为
`100000 x 10`。benchmark JSON 中的 `office_open` 字段仍保持工具写出的
`not_run`；这次没有运行 Excel COM/WPS/LibreOffice 全量验证。

这些记录只能用于当前 streaming writer / stored-bootstrap 写出趋势观察：
数字密集写入可低内存生成到 50,000,000 cells 级别；高重复字符串下 sharedStrings
输出更小且更快；高唯一字符串下 sharedStrings 的峰值内存和输出体积明显上升。
不要据此宣称 sharedStrings 生产就绪、sharedStrings 是默认最佳策略、完整 RSS /
allocator-level 低内存已验证、DEFLATE/minizip 压缩性能已验证、Zip64 / true package
streaming / existing-file editing 已验证，或办公套件兼容性已全面验证。

2026-06-20 本机使用现有
`build/windows-nmake-release-benchmark/benchmarks/fastxlsx_bench_streaming_writer.exe`
和新增 `tools/run_python_writer_benchmarks.py` 做了同规模 Python writer 对比探针。
输入均为 `rows=100000`、`cols=10`、`sheets=1`、`cells=1000000`。FastXLSX 结果
来自 `stored-bootstrap` / `store`，Python writer 结果来自各库默认 ZIP 写出行为；
所有输出均用 `openpyxl 3.1.2` 只读打开并验证 `Sheet1` 首尾值。本轮没有运行
Excel COM/WPS/LibreOffice 验证，FastXLSX benchmark JSON 的 `office_open` 仍为
`not_run`。本轮 benchmark executable 是已有本机构建产物。

| writer | mode / strategy | case | elapsed_ms | M cells/s | peak_memory_mb | output_MiB |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| FastXLSX | inline / store | numeric | 555 | 1.80 | 4.49 | 33.28 |
| FastXLSX | inline / store | mixed-mixed | 864 | 1.16 | 4.47 | 39.45 |
| FastXLSX | shared / store | mixed-mixed | 613 | 1.63 | 27.09 | 39.30 |
| FastXLSX | inline / store | strings-repeated | 820 | 1.22 | 4.49 | 53.39 |
| FastXLSX | shared / store | strings-repeated | 392 | 2.55 | 4.50 | 32.41 |
| FastXLSX | inline / store | strings-unique | 709 | 1.41 | 4.49 | 59.10 |
| FastXLSX | shared / store | strings-unique | 1433 | 0.70 | 112.39 | 63.76 |
| XlsxWriter 3.2.0 | constant_memory | numeric | 5242 | 0.19 | 26.36 | 2.99 |
| XlsxWriter 3.2.0 | constant_memory | mixed-mixed | 6644 | 0.15 | 26.30 | 3.95 |
| XlsxWriter 3.2.0 | constant_memory | strings-repeated | 6944 | 0.14 | 26.39 | 2.23 |
| XlsxWriter 3.2.0 | constant_memory | strings-unique | 7362 | 0.14 | 26.52 | 3.43 |
| openpyxl 3.1.2 | write_only | numeric | 6587 | 0.15 | 45.86 | 2.99 |
| openpyxl 3.1.2 | write_only | mixed-mixed | 8780 | 0.11 | 45.86 | 3.91 |
| openpyxl 3.1.2 | write_only | strings-repeated | 10045 | 0.10 | 45.86 | 2.23 |
| openpyxl 3.1.2 | write_only | strings-unique | 13037 | 0.08 | 45.93 | 3.43 |

直接结论：在 stored/no-compression 当前路径下，FastXLSX 的 1M cells 写入吞吐
明显高于 Python 优化写入路径，低内存 inline 场景约 4.5 MB 峰值；Python writer
输出文件显著更小主要来自 ZIP 压缩，不能据此否定 FastXLSX 写入热路径。下一步若要
做更公平体积对比，应跑 `windows-nmake-release-benchmark-minizip` 的 FastXLSX
DEFLATE 结果，并补 OpenXLSX / xlnt 独立 C++ adapter。
