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

Streaming new-workbook 路径现在通过
`WorkbookWriterOptions::zip_compression_level` 支持压缩等级配置；benchmark CLI
也支持 `--compression-level -1|0..9`。该配置只影响 ZIP close-time 压缩成本和
输出体积，不改变 worksheet row/cell streaming，不启用 Zip64，也不是 existing-file
editing。

默认策略保持生态常见行为：`-1` 委托给当前 backend default（minizip-ng 下即
minizip/zlib 默认策略），不会因为 P11.8 的吞吐数据而自动切到 level 1。需要更快
close-time 的调用方应显式传 `zip_compression_level = 1`；需要更小文件的调用方可保留
`-1` 或显式传 `6`，再按业务数据形态复测。

```text
level -1   backend default，用于保留 zlib/minizip 默认策略，不映射为 FastXLSX 快速档
level 0    no-compression/stored，最快，文件最大
level 1    显式 opt-in 的 throughput-first 推荐值
level 3    repeated strings 场景的速度/体积折中候选
level 6    zlib 常见默认级别，文件通常更小但 CPU 成本明显更高
level 9    当前数据下不推荐：显著更慢，体积收益很小甚至可能回退
```

默认 stored-bootstrap 构建不能产生 DEFLATE 输出，因此 positive level `1..9` 会在
Streaming writer / benchmark CLI 中被拒绝；需要使用 opt-in
`FASTXLSX_ENABLE_MINIZIP_NG=ON` / `windows-nmake-release-benchmark-minizip`。

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
当前 public streaming writer 和 benchmark CLI 都会把实际传入的 `-1`、`0` 或
`1..9` 记录到 `compression_level`，不能只写“minizip enabled”；`0` 应记录为
minizip no-compression/stored output，而不是 DEFLATE 压缩结果。stored-bootstrap
构建只接受 `-1` 或 `0`。
当前 internal package writer 会拒绝需要 Zip64 的 entry count 或单 entry
未压缩大小，因此 file-backed/chunked worksheet entry 仍不能被写成大文件或
Zip64 benchmark 证据。

### Workbook Editor Benchmark Workflow

已有文件编辑性能使用独立 opt-in 手工工具，不进入默认 CTest/CI：

```powershell
cmake --preset windows-nmake-release-benchmark
cmake --build --preset windows-nmake-release-benchmark --target fastxlsx_bench_workbook_editor
.\build\windows-nmake-release-benchmark\benchmarks\fastxlsx_bench_workbook_editor.exe `
  --scenario batch-set --rows 10000 --cols 10 --edits 10000 `
  --source .\build\windows-nmake-release-benchmark\benchmarks\editor-source.xlsx `
  --output .\build\windows-nmake-release-benchmark\benchmarks\editor-edited.xlsx `
  --result .\build\windows-nmake-release-benchmark\benchmarks\editor-batch-set.json
```

`fastxlsx_bench_workbook_editor` 会先生成一个 stored source workbook，再通过
public `WorkbookEditor` 打开、materialize `Data` worksheet、执行 small-file
In-memory sparse mutation，并 `save_as()` 到新 workbook。它支持：

- `point-set`：逐个调用 `set_cell_value(row, column, value)`。
- `batch-set`：一次性调用 `set_cell_values(span<WorksheetCellUpdate>)`。
- `a1-range-clear`：调用 `clear_cell_values(std::string_view)`，只清 represented
  sparse records。
- `a1-range-erase`：调用 `erase_cells(std::string_view)`，只删除 represented
  sparse records。

A1 range 场景会使用从 `A1` 开始的矩形范围；当 `--edits` 不是 `--cols` 的整数倍时，
实际触达坐标数以 JSON 中的 `touched_coordinates` 为准。

生成 JSON 使用 `workbook_editor_benchmark_schema_version = "1"`，记录：

- `source_write_ms`：生成基准 source workbook 的耗时，不属于编辑路径。
- `open_ms`：`WorkbookEditor::open()` metadata/package 读取耗时。
- `materialize_ms`：`WorksheetEditor` source worksheet materialization 耗时。
- `mutation_ms`：实际 sparse edit API 耗时。
- `save_ms`：`WorkbookEditor::save_as()` 输出耗时。
- `total_editor_ms`：`open + materialize + mutation + save`，用于比较编辑路径。
- `materialized_cells_before/after`、`estimated_memory_before/after_bytes`：
  sparse store 诊断值，不是进程 RSS。
- `peak_memory_mb`：当前进程 PeakWorkingSetSize；它包含 source 生成、打开、编辑、
  save 和运行时开销，不是单独的 sparse store 内存。
- `source_bytes` / `output_bytes`：输入/输出 package 文件大小。

该 benchmark 只覆盖当前 public small-file In-memory sparse 编辑路径，不代表
large-file low-memory random editing、relationship repair、metadata recalculation、
sharedStrings/styles broad migration、Zip64 支持或 Office 打开兼容性。`office_open`
初始仍为 `not_run`，只有实际用 Excel / WPS / LibreOffice 打开后，才能把兼容性写成
已验证事实。

2026-06-25 本地 MSVC release 手工快照，stored source workbook，`office_open=not_run`：

| scenario | source cells / edits | total_editor_ms | materialize_ms | mutation_ms | save_ms | peak_memory_mb |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `batch-set` | 100000 / 10000 | 409 | 158 | 7 | 241 | 20.68 |
| `point-set` | 100000 / 10000 | 438 | 165 | 4 | 268 | 18.55 |
| `batch-set` | 500000 / 50000 | 2746 | 777 | 39 | 1929 | 79.86 |
| `point-set` | 500000 / 50000 | 3295 | 1278 | 50 | 1958 | 69.12 |

该快照用于定位阶段性瓶颈，不是跨机器基准排名。`batch-set` 已改为
CellStore batch preflight + direct commit，避免为失败原子性复制整张 sparse map；
因此相对上一轮本机快照，500000 / 50000 场景的 batch mutation 从约 118 ms 降到
约 39 ms，峰值工作集从约 133.76 MB 降到约 79.86 MB。端到端时间仍主要由
source worksheet materialization 和 `save_as()` package 输出支配，后续大文件编辑优化
必须走 worksheet streaming patch / transformer，而不是扩大 In-memory sparse materialization。

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
  "compression_level": 1,
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
`--compression-level` 可以重复传入，runner 会对每个 case 乘上每个 level，并给
输出文件和 report case name 增加 compression 后缀，避免不同 level 互相覆盖。
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
  --compression-level 1 `
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
包，也不会修改 FastXLSX CMake/vcpkg 依赖。OpenXLSX 和 xlnt 通过后续独立
C++ adapter benchmark 接入，但仍不能作为 FastXLSX runtime dependency。

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

### P11 C++ Reference Writer Comparison Helper

`tools/run_cpp_reference_writer_benchmarks.py` 用于对 opt-in C++ XLSX writer
adapter 做本地对比。当前支持通过 `--adapter name=path` 传入：

- `openxlsx=...fastxlsx_bench_reference_openxlsx.exe`
- `xlnt=...fastxlsx_bench_reference_xlnt.exe`

这些 adapter 只链接第三方库自己的 public workbook API，用于 benchmark/reference；
不链接到 FastXLSX runtime library，不接入默认 CTest/CI，也不改变 FastXLSX public API。
大规模第三方 workbook API case 可能非常慢，runner 支持 `--timeout-seconds` 作为
每个 adapter/case 的超时护栏；超时子进程会被杀掉并记录为 `timeout`，避免留下长跑
benchmark 进程。

```powershell
py tools\run_cpp_reference_writer_benchmarks.py --self-test
py tools\run_cpp_reference_writer_benchmarks.py `
  --adapter openxlsx=build\windows-nmake-release-reference-benchmark\benchmarks\fastxlsx_bench_reference_openxlsx.exe `
  --adapter xlnt=build\windows-nmake-release-reference-benchmark\benchmarks\fastxlsx_bench_reference_xlnt.exe `
  --rows 10000 --cols 10 --sheets 1 `
  --verify-openpyxl --strict-missing `
  --output-dir build\qa\cpp-reference-writer-benchmarks
```

CMake adapter 入口是 `FASTXLSX_BUILD_REFERENCE_BENCHMARKS=ON` 和
`windows-nmake-release-reference-benchmark` preset；vcpkg feature 为
`reference-benchmarks`，仅拉取 `openxlsx` / `xlnt` 作为独立 reference benchmark
依赖。

### Benchmark Scale Interpretation

当前 `1,000,000` cells 规模是筛选级 / 对比级 benchmark，不是 release 级大文件
证明。它足以暴露写入热路径、字符串策略、压缩成本和参考库明显瓶颈，但不能替代
更大规模的线性扩展、长时间运行和 Zip64 / Office 兼容性验证。

| scale | 推荐用途 | 能说明什么 | 不能说明什么 |
| --- | --- | --- | --- |
| `100k` cells | adapter smoke / API 对齐 | 第三方 adapter 是否能跑通、输出是否可读、明显 API/内存差异 | 大文件能力、线性扩展、默认策略 |
| `1M` cells | 策略筛选 / 同机对比 | inline/sharedStrings 方向性、DEFLATE 体积收益和 CPU 成本、参考库早期瓶颈、`openpyxl` 基本可读性 | 10M/50M 线性扩展、close-time 峰值、Zip64、长时间稳定性、完整 Office/WPS/LibreOffice 兼容性 |
| `10M` cells | FastXLSX 自身线性扩展门 | row/cell 热路径是否近似线性、压缩路径是否出现明显拐点、临时 worksheet body footprint 趋势 | release 级超大文件承诺、Zip64 边界、多 sheet 长跑稳定性 |
| `50M` cells | 大文件压力 / 资源边界 | worksheet temp footprint、close-time package assembly 是否退化、长时间写出资源回收 | 100M+ / multi-sheet 生产门禁、全办公套件体验 |
| `100M+` cells 或 multi-sheet | release gate / 超大文件验收 | Zip64 边界、长时间稳定性、多 sheet 资源管理、代表性 Office/WPS/LibreOffice 打开体验 | 不能由低一级 benchmark 外推替代 |

因此，当前 1M 记录的正确结论是：FastXLSX 热路径可用，DEFLATE 体积已公平，
唯一字符串 sharedStrings 有明确内存风险，OpenXLSX / xlnt 在小中规模已暴露
部分瓶颈。它的错误用法是把 1M 当成“真正大文件能力已证明”或把
`worksheet-body-file-bytes` 当成完整 RSS / package temp footprint。

后续如果继续 C6 benchmark 线，优先跑 FastXLSX 自身 `10M -> 50M` 阶梯；
第三方库按 `100k -> 1M with timeout` 分层记录，不强行等待完整大矩阵。

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
输出文件显著更小主要来自 ZIP 压缩，不能据此否定 FastXLSX 写入热路径。

同日又使用 `windows-nmake-release-benchmark-minizip` 对 FastXLSX 跑了同规模
`100000 x 10 x 1 = 1000000` cells DEFLATE 矩阵，输出均通过 `openpyxl 3.1.2`
只读首尾值验证；`office_open` 仍为 benchmark 工具原始 `not_run`。

| case | store ms / MiB | deflate ms / MiB | deflate M cells/s | deflate peak MB | size delta | time delta |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| numeric-inline | 555 / 33.28 | 1884 / 2.98 | 0.53 | 5.48 | -91.0% | +239.5% |
| mixed-mixed-inline | 864 / 39.45 | 1291 / 3.94 | 0.77 | 5.13 | -90.0% | +49.4% |
| mixed-mixed-shared | 613 / 39.30 | 1470 / 4.26 | 0.68 | 29.65 | -89.1% | +139.8% |
| strings-repeated-inline | 820 / 53.39 | 1329 / 2.23 | 0.75 | 5.11 | -95.8% | +62.1% |
| strings-repeated-shared | 392 / 32.41 | 1258 / 1.99 | 0.79 | 5.11 | -93.9% | +220.9% |
| strings-unique-inline | 709 / 59.10 | 2547 / 3.43 | 0.39 | 5.11 | -94.2% | +259.2% |
| strings-unique-shared | 1433 / 63.76 | 3945 / 5.54 | 0.25 | 121.36 | -91.3% | +175.3% |

P11.4 结论：FastXLSX DEFLATE 输出体积已与 Python writer 同量级，部分场景更小；
压缩 CPU 成本明显，但 1M cells 下仍明显快于 Python writer。`strings-unique-shared`
仍是内存风险，不适合作默认策略。

P11.5 同日新增 OpenXLSX / xlnt 独立 C++ adapter benchmark，并在
`10000 x 10 x 1 = 100000` cells 上跑了可完成的同机对比。FastXLSX 选取
minizip/DEFLATE 下最合理策略：numeric/mixed/unique 使用 inline，repeated strings
使用 sharedStrings。所有 100k 输出均用 `openpyxl` 只读验证；未运行 Excel
COM/WPS/LibreOffice。

| case | FastXLSX minizip selected | OpenXLSX workbook API | xlnt workbook API |
| --- | --- | --- | --- |
| numeric | 268 ms / 0.37 M/s / 5.11 MB / 0.30 MiB | 304 ms / 0.33 M/s / 44.63 MB / 0.30 MiB | 809 ms / 0.12 M/s / 92.67 MB / 0.30 MiB |
| mixed-mixed | 387 ms / 0.26 M/s / 5.09 MB / 0.39 MiB | 1061 ms / 0.09 M/s / 51.65 MB / 0.42 MiB | 762 ms / 0.13 M/s / 107.78 MB / 0.44 MiB |
| strings-repeated | 191 ms / 0.52 M/s / 5.11 MB / 0.20 MiB | 165 ms / 0.61 M/s / 49.12 MB / 0.25 MiB | 929 ms / 0.11 M/s / 92.66 MB / 0.26 MiB |
| strings-unique | 385 ms / 0.26 M/s / 5.11 MB / 0.34 MiB | 34933 ms / 0.003 M/s / 78.94 MB / 0.56 MiB | 1749 ms / 0.06 M/s / 177.28 MB / 0.54 MiB |

P11.5 结论：FastXLSX 在 numeric、mixed 和 unique strings 上明显更低内存且更快；
OpenXLSX repeated strings 在 100k workbook-API case 上略快，但峰值内存约 49 MB，
约为 FastXLSX 的 9.6 倍。OpenXLSX unique strings 在 100k 已耗时 34.9s；
尝试跑 1M C++ reference 全矩阵时，OpenXLSX unique strings 未在本轮等待窗口完成，
进程已停止并不作为完整 1M C++ reference 矩阵结论。

P11.7 同日按 scale ladder 使用现有本地
`fastxlsx_bench_streaming_writer.exe` 对 FastXLSX 自身做了 `10M -> 50M` 阶梯探针。
本轮未运行 `openpyxl` / Excel COM / WPS / LibreOffice 打开验证，所有 case 的
`office_open` 仍为 `not_run`，`openpyxl_status` 为 `not_requested`；结果只用于
热路径、压缩成本和资源趋势判断，不是完整 Office 兼容性或 release gate。

| case | cells | store ms / MiB | store M cells/s | store peak MB | deflate ms / MiB | deflate M cells/s | deflate peak MB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| numeric-inline | 10000000 | 5927 / 352.58 | 1.69 | 4.46 | 13223 / 30.36 | 0.76 | 5.12 |
| mixed-mixed-inline | 10000000 | 7380 / 413.85 | 1.36 | 4.49 | 14024 / 40.02 | 0.71 | 5.11 |
| strings-repeated-shared | 10000000 | 6164 / 334.53 | 1.62 | 4.50 | 12051 / 20.60 | 0.83 | 5.13 |
| strings-unique-inline | 10000000 | 11822 / 610.99 | 0.85 | 4.49 | 15269 / 34.31 | 0.65 | 5.11 |
| numeric-inline | 50000000 | 28704 / 1702.30 | 1.74 | 4.50 | 63365 / 140.06 | 0.79 | 5.11 |

P11.7 结论：FastXLSX numeric-inline 在 10M 到 50M 的 stored 路径上吞吐基本稳定
（约 1.69 -> 1.74 M cells/s），峰值内存仍约 4.5 MB；minizip/DEFLATE 路径在
50M numeric 下仍约 5.1 MB 峰值，输出从 1702.30 MiB 降到 140.06 MiB（约 -91.8%），
耗时从 28.7s 增到 63.4s（约 +120.8%）。10M strings-repeated-shared 仍保持低内存，
10M strings-unique-inline 也保持低内存；本轮故意不跑 10M `strings-unique-shared`，
因为 1M 证据已显示该策略是内存风险，不适合作为默认策略或常规大规模矩阵项。

P11.8 同日新增 Streaming public compression-level 配置、
benchmark `--compression-level` CLI 和 runner 多 level 矩阵支持。使用
`windows-nmake-release-benchmark-minizip` 在 `10,000,000` cells 下对
`numeric-inline`、`strings-repeated-shared` 和 `strings-unique-inline` 跑了
`-1/0/1/3/6/9` 压缩等级曲线；另补 `50,000,000` cells numeric-inline level 1
单点。本轮未运行 `openpyxl` / Office 打开验证，`office_open` 仍为 `not_run`。

| case | level | elapsed ms | M cells/s | peak MB | output MiB |
| --- | ---: | ---: | ---: | ---: | ---: |
| numeric-inline 10M | -1 | 16056 | 0.62 | 5.11 | 30.36 |
| numeric-inline 10M | 0 | 4940 | 2.02 | 4.80 | 352.58 |
| numeric-inline 10M | 1 | 7768 | 1.29 | 5.13 | 33.29 |
| numeric-inline 10M | 3 | 8556 | 1.17 | 5.12 | 31.64 |
| numeric-inline 10M | 6 | 14826 | 0.67 | 5.13 | 30.36 |
| numeric-inline 10M | 9 | 87274 | 0.11 | 5.11 | 30.37 |
| strings-repeated-shared 10M | -1 | 13814 | 0.72 | 5.13 | 20.60 |
| strings-repeated-shared 10M | 0 | 5526 | 1.81 | 4.80 | 334.53 |
| strings-repeated-shared 10M | 1 | 6953 | 1.44 | 5.09 | 28.87 |
| strings-repeated-shared 10M | 3 | 6639 | 1.51 | 5.12 | 26.81 |
| strings-repeated-shared 10M | 6 | 12678 | 0.79 | 5.12 | 20.60 |
| strings-repeated-shared 10M | 9 | 54575 | 0.18 | 5.12 | 25.10 |
| strings-unique-inline 10M | -1 | 16076 | 0.62 | 5.11 | 34.31 |
| strings-unique-inline 10M | 0 | 10265 | 0.97 | 4.80 | 610.99 |
| strings-unique-inline 10M | 1 | 9663 | 1.03 | 5.11 | 34.92 |
| strings-unique-inline 10M | 3 | 15520 | 0.64 | 5.11 | 34.91 |
| strings-unique-inline 10M | 6 | 17763 | 0.56 | 5.12 | 34.31 |
| strings-unique-inline 10M | 9 | 62576 | 0.16 | 5.11 | 34.22 |
| numeric-inline 50M | 1 | 29921 | 1.67 | 5.13 | 208.15 |

P11.8 结论：level 9 当前没有实际价值，耗时显著恶化且体积收益很小；level 6 /
backend default 更偏最小体积，适合强体积约束；level 1 是 throughput-first 推荐值，
50M numeric 下相比上一轮 default/level6 的 63.4s / 140.06 MiB，level 1 为
29.9s / 208.15 MiB，即约 2.1 倍更快但文件约 48.6% 更大。repeated shared strings
场景可考虑 level 3：10M 下比 level 1 稍快且更小，但仍比 level 6 大约 30%。
因此当前建议是保留显式可配置策略：吞吐优先用 level 1，重复字符串且体积敏感可试
level 3/6，避免 level 9；不要把某一个等级写成所有数据形态的无条件默认最优，也不要把
FastXLSX 默认从 backend default 改成 level 1。
