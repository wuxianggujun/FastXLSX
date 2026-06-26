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
public `WorkbookEditor` 打开，并按 scenario 选择 small-file In-memory sparse
mutation 或 large-worksheet targeted Patch mutation，最后 `save_as()` 到新
workbook。它支持：

- `point-set`：逐个调用 `set_cell_value(row, column, value)`。
- `batch-set`：一次性调用 `set_cell_values(span<WorksheetCellUpdate>)`。
- `a1-range-clear`：调用 `clear_cell_values(std::string_view)`，只清 represented
  sparse records。
- `a1-range-erase`：调用 `erase_cells(std::string_view)`，只删除 represented
  sparse records。
- `patch-replace`：调用 public `WorkbookEditor::replace_cells()`，只替换已存在
  target cells，不 materialize worksheet。
- `patch-upsert`：调用 public
  `WorkbookEditor::replace_cells(..., CellPatchMissingCellPolicy::Insert)`，一半编辑替换
  已有 cells，另一半在 source 尾部之后合成新 rows，不 materialize worksheet。

A1 range 场景会使用从 `A1` 开始的矩形范围；当 `--edits` 不是 `--cols` 的整数倍时，
实际触达坐标数以 JSON 中的 `touched_coordinates` 为准。

生成 JSON 使用 `workbook_editor_benchmark_schema_version = "2"`，记录：

- `source_write_ms`：生成基准 source workbook 的耗时，不属于编辑路径。
- `open_ms`：`WorkbookEditor::open()` metadata/package 读取耗时。
- `materialize_ms`：`WorksheetEditor` source worksheet materialization 耗时；Patch
  scenarios 固定为 `0`。
- `mutation_ms`：实际 edit API 耗时；Patch scenarios 包含 source/planned worksheet
  transformer 分析和 staged rewrite 排队。
- `save_ms`：`WorkbookEditor::save_as()` 输出耗时。
- `total_editor_ms`：`open + materialize + mutation + save`，用于比较编辑路径。
- `materialized_worksheet` / `editor_mode`：明确本轮是 In-memory sparse 还是 Patch
  targeted cell path。
- `inserted_coordinates`：仅 `patch-upsert` 使用，用于记录本轮合成缺失 cell/row 数。
- `materialized_cells_before/after`、`estimated_memory_before/after_bytes`：
  sparse store 诊断值；Patch scenarios 为 `0`，不是进程 RSS。
- `peak_memory_mb`：当前进程 PeakWorkingSetSize；它包含 source 生成、打开、编辑、
  save 和运行时开销，不是单独的 sparse store 内存。
- `source_bytes` / `output_bytes`：输入/输出 package 文件大小。

该 benchmark 覆盖当前 public small-file In-memory sparse 编辑路径，以及 public
targeted Patch replace/upsert 路径；它仍不代表 arbitrary random editing、row/column
shifting、relationship repair、metadata recalculation、sharedStrings/styles broad
migration、Zip64 支持或 Office 打开兼容性。`office_open` 初始仍为 `not_run`，
只有实际用 Excel / WPS / LibreOffice 打开后，才能把兼容性写成已验证事实。

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

同日补充 public Patch editor benchmark smoke，规模为 `50000 x 10 = 500000`
source cells、`1000` edits，stored source workbook，`office_open=not_run`：

| scenario | editor_mode | materialized | inserted | total_editor_ms | materialize_ms | mutation_ms | save_ms | peak_memory_mb |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `batch-set` | `existing-workbook-in-memory-sparse` | true | 0 | 1926 | 802 | 19 | 1104 | 69.24 |
| `patch-replace` | `existing-workbook-patch-targeted-cell-replace` | false | 0 | 1475 | 0 | 1231 | 243 | 6.26 |
| `patch-upsert` | `existing-workbook-patch-targeted-cell-upsert` | false | 500 | 1639 | 0 | 1367 | 268 | 6.30 |

本轮还修正了 `patch-upsert` transformer 的 target 调度：从每个 source event 反复扫描
全部 targets，改为按 row/column 排序后的单向游标推进。相同 500000 / 1000 upsert
smoke 中，优化前本机 `total_editor_ms` 约 4248 ms，优化后约 1639 ms。结论是：
大文件少量点编辑应优先走 Patch replace/upsert；它避免 worksheet materialization，
峰值工作集在该 smoke 下约 6.3 MB。当前 public Patch 耗时仍与 source worksheet
线性扫描相关；内部已有 `WorksheetCellIndex` source-offset compact index 和 indexed
rewrite planning 基础，transformer action stream 也暴露 source XML offset，且已有
internal materialized indexed slicer 可按 index range 拼接 strict existing-cell replacement。
但这些还没有被切换成默认 Patch 算法，也不是 PackageEditor source-entry seek、
source ZIP entry seek 或完整 O(1) 随机编辑。插入/替换也不会修复 tables、filters、
drawings、defined names、formulas、sharedStrings 或 styles。

同日又收紧 targeted-cell transformer 热路径：strict replace 在全部 target
命中且 source stream 已越过最后一个 target 坐标后，不再对 tail source cells 做
replacement map lookup；Insert/upsert 在保持 source cell reference 校验的前提下，
全部 target 发射后也停止 pending-target / replacement lookup。该优化不改变 public
API、不使用索引，也不跳过 source XML 扫描；它只减少“大 worksheet 前段少量点编辑、
后段大量透传 cells”场景中的无效 target 查找。后续 indexed rewrite 可以复用
event-reader source offsets、transformer action offsets 和 `WorksheetCellIndex`
rewrite plans；materialized indexed slicer 已证明按 range 拼接可行，但 package-level
staged-chunk byte-range slicer 目前已覆盖 internal `PackageEntryChunk` memory/file
range emitter foundation，并已有 internal chunk-backed indexed strict-replace slicer
prototype 可在预建 index 与 staged chunks 匹配时拼接 replacement payload；source-entry
ZIP seek 和默认算法切换仍是单独任务。当前 package-editor benchmark 已有 opt-in
`--rewrite-strategy indexed-staged` 入口，用于验证 benchmark 生成的 staged chunks +
prebuilt index 原型路径；它不是 public 默认算法证明。回归测试覆盖早段 target 完成后尾部 cells 仍原样透传，并固定重复 source
target 在进入 tail fast path 前仍按旧行为替换。

### Public WorkbookEditor Targeted Cell Replacement Benchmark Workflow

大 worksheet 的已有文件定向 cell replacement 使用独立 opt-in 手工工具验证
public `WorkbookEditor::replace_cells()` facade；工具也保留
`--editor-api internal-package-editor` 用于排查 internal `PackageEditor` transformer
边界。该 benchmark 不进入默认 CTest/CI：

```powershell
cmake --preset windows-nmake-release-benchmark
cmake --build --preset windows-nmake-release-benchmark --target fastxlsx_bench_package_editor_cell_replacement
.\build\windows-nmake-release-benchmark\benchmarks\fastxlsx_bench_package_editor_cell_replacement.exe `
  --rows 500000 --cols 10 --edits 5000 `
  --editor-api public-workbook-editor `
  --source build\bench-package-editor\source-500k.xlsx `
  --source-body build\bench-package-editor\source-body-500k.xml `
  --output build\bench-package-editor\out-500k.xlsx `
  --result build\bench-package-editor\result-500k.json
.\build\windows-nmake-release-benchmark\benchmarks\fastxlsx_bench_package_editor_cell_replacement.exe `
  --rows 500000 --cols 10 --edits 5000 `
  --editor-api internal-package-editor `
  --rewrite-strategy indexed-staged `
  --source build\bench-package-editor\source-500k-indexed.xlsx `
  --source-body build\bench-package-editor\source-body-500k-indexed.xml `
  --output build\bench-package-editor\out-500k-indexed.xlsx `
  --result build\bench-package-editor\result-500k-indexed.json
& .\tools\verify_package_editor_cell_replacement_benchmark_excel.ps1 `
  -ResultPath @(
    'build\bench-package-editor\result-100k.json',
    'build\bench-package-editor\result-300k.json',
    'build\bench-package-editor\result-500k.json'
  ) `
  -ReportPath build\bench-package-editor\package-editor-cell-replacement-office-report.json
```

`fastxlsx_bench_package_editor_cell_replacement` 会生成一个 stored source package，
其中 `xl/worksheets/sheet1.xml` 由 worksheet prefix、file-backed row body 和
worksheet suffix 组成；默认通过 public `WorkbookEditor::replace_cells()` 替换已存在
cell，`save_as()` 写出结果，并用 `PackageReader::entry_chunk_source()` 流式检查输出
包含首个替换 cell 和尾部未修改 source cell。生成 JSON 使用
`package_editor_cell_replacement_benchmark_schema_version = "2"`，记录 source body /
package 生成耗时、open、patch plan、save、verify、总编辑耗时、进程峰值工作集、输入 /
输出 package 大小，以及以下关键路径证据：

- `package_entry_source_mode = "source-zip-entry-chunk-source"`。
- `output_entry_mode = "file-backed-stream-rewrite"`。
- `editor_api = "public-workbook-editor"` 默认 public facade 路径。
- `rewrite_strategy = "transformer"` 默认保持现有 source-entry transformer 路径。
- `public_facade_reports_targeted_cells = true`。
- `public_facade_targeted_cell_count = edits`。
- `public_facade_replacement_xml_bytes` 记录 caller single-cell replacement XML payload
  字节，不是 source worksheet XML、PackageEditor 临时文件或进程 RSS。
- 仅在 `--editor-api internal-package-editor` 下，`plan_reports_source_entry_chunk_source`、
  `plan_reports_file_backed_stream_rewrite`、`output_plan_staged_replacement_chunks` 和
  `output_plan_materialized_replacement` 作为 internal output-plan 证据有意义。
- `--rewrite-strategy indexed-staged` 仅允许 internal API，记录
  `index_build_ms`、`indexed_emit_ms`、`indexed_stage_commit_ms`、
  `indexed_source_cell_count`、`indexed_matched_replacement_count`、
  `indexed_staged_output_bytes`，并把 `package_entry_source_mode` 写成
  `benchmark-prefix-body-file-suffix-staged-chunks`、
  `output_entry_mode` 写成
  `indexed-staged-file-backed-worksheet-replacement`。该模式使用 benchmark 已知的
  prefix/body-file/suffix staged chunks，不是任意 source ZIP-entry seek，也不是
  public Patch 默认算法。

2026-06-25 本地 MSVC release 手工快照，stored source package，`office_open=not_run`：

| source cells / edits | source package MiB | output package MiB | patch_plan_ms | save_ms | total_edit_ms | total_ms | peak_memory_mb |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1000000 / 1000 | 36.12 | 36.12 | 4516 | 802 | 5322 | 7207 | 6.21 |
| 3000000 / 3000 | 112.79 | 112.79 | 11196 | 1745 | 12943 | 17977 | 6.61 |
| 5000000 / 5000 | 189.47 | 189.47 | 14609 | 3037 | 17647 | 25219 | 7.31 |

同日追加 target-lookup fast path 后，重新跑 public facade 1M / 3M / 5M 矩阵，并用
internal API 补 1M / 5M output-plan 证据；stored source package，`office_open=not_run`，
工具内置 output verifier 均为 `output_verified=true`：

| editor_api | source cells / edits | source MiB | output MiB | patch_plan_ms | save_ms | total_edit_ms | total_ms | peak_memory_mb |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `public-workbook-editor` | 1000000 / 1000 | 36.11 | 36.12 | 4305 | 605 | 4912 | 6356 | 6.16 |
| `public-workbook-editor` | 3000000 / 3000 | 112.79 | 112.79 | 9224 | 2097 | 11323 | 16102 | 7.46 |
| `public-workbook-editor` | 5000000 / 5000 | 189.47 | 189.47 | 14952 | 3026 | 17979 | 26040 | 8.77 |
| `internal-package-editor` | 1000000 / 1000 | 36.11 | 36.12 | 3533 | 650 | 4188 | 5921 | 6.00 |
| `internal-package-editor` | 5000000 / 5000 | 189.47 | 189.47 | 15225 | 3256 | 18482 | 25119 | 7.81 |

所有 public facade JSON 都记录
`package_entry_source_mode="source-zip-entry-chunk-source"` 和
`output_entry_mode="file-backed-stream-rewrite"`；public facade 的
`public_facade_targeted_cell_count` 等于 edits。internal API 结果还记录
`plan_reports_source_entry_chunk_source=true`、
`plan_reports_file_backed_stream_rewrite=true`、
`output_plan_staged_replacement_chunks=true` 和
`output_plan_materialized_replacement=false`。这说明 public facade 已走同一
file-backed Patch 路线。与上一组 internal 5M `total_edit_ms=17647` 相比，本次
重跑为 `18482`，属于同量级手工 benchmark 波动；不要据此写成稳定加速比例。
该数据仍是本机 opt-in 手工结果，不是默认 CI 性能门禁，也不是随机访问索引、
Zip64、DEFLATE input/output 或 Office 兼容性证明。

2026-06-25 追加 schema-v2 internal 对照，`100000 x 10 = 1000000` source cells、
`1000` edits、stored source package、工具内置 verifier 为 `output_verified=true`：

| rewrite_strategy | patch_plan_ms | index_build_ms | indexed_emit_ms | indexed_stage_commit_ms | save_ms | total_edit_ms | total_ms | peak_memory_mb |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `transformer` | 2370 | 0 | 0 | 0 | 616 | 2989 | 4170 | 5.64 |
| `indexed-staged` | 2755 | 754 | 146 | 1651 | 779 | 3535 | 4736 | 97.73 |

该结果说明 indexed-staged benchmark/prototype 能正确输出，但在“每次编辑都临时
构建完整 `WorksheetCellIndex`”的单轮场景中并不比 transformer 快。旧实现还会因
`std::map` source-cell range index 产生明显内存成本。

同日随后把 `WorksheetCellIndex` 主索引从 per-cell `std::map<std::string, range>`
改为 compact `{row, column, range}` vector，并仅在调用 `cells()` 诊断入口时惰性
生成旧 map 快照。相同 `100000 x 10 = 1000000` source cells、`1000` edits、
stored source package、工具内置 verifier 为 `output_verified=true` 的本机 release
对照如下：

| rewrite_strategy | patch_plan_ms | index_build_ms | indexed_emit_ms | indexed_stage_commit_ms | save_ms | total_edit_ms | total_ms | peak_memory_mb |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `transformer` | 3022 | 0 | 0 | 0 | 588 | 3611 | 4929 | 5.63 |
| `indexed-staged` compact index | 3055 | 533 | 132 | 2387 | 723 | 3780 | 5014 | 37.64 |

紧凑索引把 indexed-staged 1M-cell 峰值工作集从旧快照的 97.73 MB 降到
37.64 MB，说明原来的 per-cell string/map node 是真实瓶颈之一；但它仍显著高于
transformer 的 5.63 MB，因为 indexed-staged 仍为每个 source cell 保存一个随机访问
range entry，并且当前单轮编辑还要支付完整 index build 成本。因此它仍只能作为
internal opt-in prototype：合理推进方向是 index 复用、目标集合更大/多轮编辑摊销、
或进一步压缩/分段化 index representation；不能把它切为 public 默认路径。

同日随后把 indexed-staged benchmark 改成 target-only range planning：不再构建完整
`WorksheetCellIndex`，而是流式扫描 benchmark staged worksheet source，仅为请求的
target cells 保存 `<c>` byte ranges。`index_build_ms` 在该路径下表示 target range
scan/planning 耗时；`indexed_source_cell_count` 仍表示扫描过的 source cell 数，不表示
保存在内存中的 index entry 数。stored source package，工具内置 verifier 均为
`output_verified=true`，`office_open=not_run`：

| rewrite_strategy | source cells / edits | patch_plan_ms | index_build_ms | indexed_emit_ms | indexed_stage_commit_ms | save_ms | total_edit_ms | total_ms | peak_memory_mb |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `transformer` | 1000000 / 1000 | 2291 | 0 | 0 | 0 | 787 | 3079 | 4324 | 5.63 |
| `indexed-staged` target-only | 1000000 / 1000 | 2218 | 450 | 146 | 1620 | 489 | 2712 | 3941 | 5.71 |
| `indexed-staged` target-only | 10000000 / 1000 | 28927 | 4869 | 1987 | 22068 | 7194 | 36123 | 49541 | 5.70 |

这一组历史快照说明 target-only planner 已把 indexed-staged 的峰值内存从 compact
full-index 1M 快照的 37.64 MB 降到与 transformer 同量级的约 5.7 MB，并且 10M
cells / 1000 edits 样本仍保持约 5.70 MB 峰值工作集。在这组 target-only 快照里，
主要耗时不在 range planning，而在 `indexed_stage_commit_ms` 和后续 `save_ms`：
benchmark prototype 当时先把 rewritten worksheet 写成临时 file-backed XML，再通过
`PackageEditor::replace_worksheet_part_chunks_by_name()` 走完整 staged worksheet
validation / audit / package save 路径。因此这组数据证明了大 worksheet 稀疏替换的
内存边界可以做到 O(targets)，但还没有证明最终生产默认算法或任意 source ZIP entry
seek；当时若继续优化耗时，应减少 staged worksheet 二次提交/二次扫描，而不是恢复
全量 source-cell index。

当前代码里的 `indexed-staged` benchmark 已切到
`PackageEditor::replace_worksheet_part_prevalidated_chunks_by_name()` 这一条 by-name
prevalidated commit fast path；因此上表中的 `indexed_stage_commit_ms` 仍是旧
target-only 采集值，不代表新入口的最新耗时。新入口会直接复用已完成验证的 staged
chunks，不再重新读取/审计 staged worksheet。后续如果要比较新旧路径，必须重新跑
benchmark 并替换这张表，不能把旧快照直接当成当前 fast path 基线。

同日随后重编 benchmark 并复跑 prevalidated commit fast path，stored source package、
工具内置 verifier 为 `output_verified=true`、`office_open=not_run`：

| rewrite_strategy | source cells / edits | patch_plan_ms | index_build_ms | indexed_emit_ms | indexed_stage_commit_ms | save_ms | total_edit_ms | total_ms | peak_memory_mb |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `indexed-staged` target-only + prevalidated commit | 1000000 / 1000 | 1408 | 804 | 314 | 287 | 937 | 2349 | 4273 | 5.53 |
| `indexed-staged` target-only + prevalidated commit | 10000000 / 1000 | 10407 | 5663 | 2067 | 2673 | 6254 | 16668 | 34198 | 5.53 |

这一组说明 prevalidated commit 已把 1M cells / 1000 edits 的
`indexed_stage_commit_ms` 从旧 target-only 快照的 1620 ms 降到 287 ms，把
10M cells / 1000 edits 从 22068 ms 降到 2673 ms；峰值工作集仍在约 5.5 MB
量级。端到端仍由线性 target range scan、indexed emit、package save 和输出验证
共同支配，并且仍不是 public Patch 默认算法、source ZIP-entry seek、Zip64 或 Office
兼容性证明。

2026-06-25 复跑当前 binary 的同规模 10M / 1000 edits 对照，再次确认
`indexed-staged` 仍明显快于 `transformer`：`transformer` 这次录得
`total_edit_ms=97444`、`total_ms=119032`、`peak_memory_mb=6.04`，
`indexed-staged` 录得 `total_edit_ms=17370`、`total_ms=34200`、`peak_memory_mb=6.14`，
两者均为 `output_verified=true`。当前剩余热点主要仍在 `index_build_ms`
和 `save_ms`，而不是 public `WorkbookEditor::replace_cells()` facade 本身。

同日继续优化 prevalidated staged commit：benchmark-only by-name fast path 已避免
`require_staged_package_entry_chunks_valid()` 对刚写出的 400 MB staged worksheet 再做
CRC32 全量读取，只保留 chunk 结构和 size guardrail；实际 package writer 仍会在写 ZIP
时读取 staged chunk 并计算 entry CRC。相同 `10000000 / 1000` indexed-staged 复跑录得
`patch_plan_ms=6744`、`index_build_ms=4989`、`indexed_emit_ms=1751`、
`indexed_stage_commit_ms=1`、`save_ms=6273`、`total_edit_ms=13019`、
`total_ms=27904`、`peak_memory_mb=5.83`，且 `output_verified=true`。这把上一轮
`indexed_stage_commit_ms=3174` 的重复验证成本基本清零；当前真实热点已经转移到
线性 target range scan 和 `save_as()` package 输出。

同日继续把 target-only planner 增加 benchmark/prototype opt-in early-stop：当所有请求
targets 已命中后停止 worksheet event scan，后续 worksheet tail 仍由 byte-range emitter
原样复制。相同 `10000000 / 1000` indexed-staged 复跑录得
`patch_plan_ms=2250`、`index_build_ms=2`、`indexed_emit_ms=2244`、
`indexed_stage_commit_ms=0`、`save_ms=7786`、`total_edit_ms=10038`、
`total_ms=29834`、`peak_memory_mb=5.88`，且 `indexed_source_cell_count=1000`、
`output_verified=true`。这说明“前段稀疏 edits + 大尾部透传”的 benchmark 上，
计划阶段已经接近 O(targets)，剩余主耗时是生成 staged worksheet 和 `save_as()` 写出
完整 package；该 early-stop 不是默认 source validation，也不是 public random-access
editing 语义。

同日继续优化 stored ZIP writer 的 staged single-chunk 输出：当单个 staged chunk 已携带
trusted `expected_size` 和 `expected_crc32` 时，local header stats 可直接复用该元数据，
避免 `save_as()` 写 local header 前对 400 MB staged worksheet 做第二次全量读取；真正写
entry data 时仍会读取文件、计算 chunk CRC 并校验 expected CRC。相同
`10000000 / 1000` indexed-staged 复跑录得 `patch_plan_ms=2515`、
`index_build_ms=1`、`indexed_emit_ms=2512`、`indexed_stage_commit_ms=0`、
`save_ms=3553`、`verify_ms=2836`、`total_edit_ms=6069`、`total_ms=24240`、
`peak_memory_mb=5.68`，且 `indexed_source_cell_count=1000`、`output_verified=true`。
这把上一轮 `save_ms=7786` 的写出前预读成本明显压下去；当前剩余主耗时是
staged worksheet emission、完整 package 写出和 benchmark verifier，而不是 worksheet
target planning 或 staged commit。

2026-06-26 继续推进 direct source-range chunks：`PackageEntryChunk` 增加内部
file-range 描述，indexed-staged benchmark 不再先落盘整份 rewritten worksheet，而是把
source prefix / body file ranges / suffix 与 replacement memory chunks 直接交给
`PackageEditor` staged replacement。直接 descriptor 如果不携带 CRC 会让 stored ZIP
writer 为 local header 预读整份 worksheet，实际会拖慢 `save_as()`；因此本轮同时让
direct descriptors 给每个 chunk 记录 expected size / CRC，并让 stored ZIP writer 用
chunk CRC combine 得出 entry CRC，写 entry data 时仍逐 chunk 校验。相同
`10000000 / 1000` indexed-staged 复跑录得 `patch_plan_ms=2090`、
`index_build_ms=1`、`indexed_emit_ms=2082`、`indexed_stage_commit_ms=4`、
`save_ms=3005`、`verify_ms=2264`、`total_edit_ms=5097`、`total_ms=21210`、
`peak_memory_mb=6.21`，且 `indexed_source_cell_count=1000`、
`output_verified=true`。当前 `package_entry_source_mode` 为
`benchmark-prefix-body-file-suffix-direct-range-chunks`，`output_entry_mode` 为
`indexed-staged-source-range-chunk-replacement`。这仍是 internal benchmark/prototype
路径，不是 public Patch 默认算法、source ZIP-entry seek、Zip64、DEFLATE 输出或
Office 兼容性证明。

同日继续把 stored ZIP writer 改为 seekable local-header patch 模式：先写占位
local header，entry data 写出时计算最终 entry CRC / size，再回填 local header，
central directory 继续使用最终 stats。direct-range descriptors 因此不再需要为
source file ranges 预先计算 CRC，只保留 size/range contract；memory replacement
chunks 仍可携带 expected CRC。随后又给同一个 entry 内相同 path 的 file-range chunks
增加输入流和文件大小缓存，避免 1000 个 source ranges 反复 open/stat。相同
`10000000 / 1000` indexed-staged 复跑录得 `patch_plan_ms=8`、
`index_build_ms=1`、`indexed_emit_ms=0`、`indexed_stage_commit_ms=4`、
`save_ms=2454`、`verify_ms=2081`、`total_edit_ms=2463`、`total_ms=13120`、
`peak_memory_mb=6.22`，且 `indexed_source_cell_count=1000`、`output_verified=true`。
这把上一组 direct-range `total_edit_ms=5097` 继续降到约 2.46s；当前剩余主耗时
基本是 400 MB package 顺序写出、entry CRC 计算和 benchmark verifier，而不是
range planning、staged worksheet emission 或 commit。该路径仍是 internal
benchmark/prototype，不是 public 默认算法、通用 source ZIP-entry seek 层、Zip64、
DEFLATE 输出或 Office 兼容性证明。

同日继续把 indexed-staged benchmark 的 source chunks 从 benchmark sidecar
`source_body` 切到真实 stored source package 的 worksheet entry payload range：
通过 `PackageReaderEntry::data_offset` 把 `xl/worksheets/sheet1.xml` 映射为
`PackageEntryChunk::file_range(source.xlsx, data_offset, uncompressed_size)`，后续 indexed
range planning 和 replacement descriptor 都直接引用 source `.xlsx` 内的 worksheet
payload bytes。`source_body` 仍用于构造 benchmark source package，但不再作为编辑输入。
相同 `10000000 / 1000` indexed-staged 复跑录得 `patch_plan_ms=18`、
`index_build_ms=6`、`indexed_emit_ms=1`、`indexed_stage_commit_ms=4`、
`save_ms=2958`、`verify_ms=2077`、`total_edit_ms=2983`、`total_ms=12954`、
`peak_memory_mb=6.19`，且 `package_entry_source_mode` 为
`source-package-worksheet-entry-direct-range-chunks`、`output_verified=true`。这比
body-sidecar direct range 的 `total_edit_ms=2463` 略慢，但边界更接近真实 existing
package 输入：无需先把整份 worksheet entry 抽到外部 rewritten/body sidecar 文件再编辑。
当前仍只覆盖 stored generated source package 的 internal benchmark/prototype 路径；
它不是 public 默认算法、DEFLATE / Zip64 source-entry seek、任意 source package
range mutator 或 Office 兼容性证明。

2026-06-26 继续把 source-package direct-range 获取路径下沉到 internal
`PackageEditor` helper：benchmark 不再自己解析 `PackageReaderEntry::data_offset`，
而是调用 `PackageEditor::source_worksheet_part_stored_entry_chunks_by_name()` 取得
stored source worksheet entry 的 file-range chunks。该 helper 仍是内部能力，只接受
stored/no-compression source entry；DEFLATE / Zip64 仍不属于该 fast path。benchmark
同时新增 `--reuse-source` 和 `source_fixture_mode`，用于跳过 400 MB source/body
fixture 生成，避免把 source 写盘噪声混进 edit-path 对比。相同
`10000000 / 1000`、`--rewrite-strategy indexed-staged`、复用既有 source package
的两次本地 release 复跑分别录得：`save_ms=6769`、`verify_ms=6628`、
`total_edit_ms=6778`、`total_ms=13409`、`peak_memory_mb=5.53`；以及
`save_ms=4960`、`verify_ms=3965`、`total_edit_ms=4977`、`total_ms=8944`、
`peak_memory_mb=5.55`。两次 `index_build_ms` 均约 `1-2 ms`，`indexed_emit_ms=0`，
`indexed_stage_commit_ms` 为个位数毫秒，且 `output_verified=true`。这说明当前
算法段已接近 O(targets) 毫秒级，剩余波动主要来自 400 MB stored ZIP 顺序写出、
entry CRC 计算、benchmark verifier 和本机磁盘状态；不要把单次 `save_ms` 当成稳定
SLA。代码侧还给 `PackageWriter` ZIP32 validation 加了同路径 file-range stat cache，
减少 staged chunks 对同一 source `.xlsx` 的重复 `file_size()`，但本轮数据没有证明它
是主瓶颈。

同日继续把保守 direct-range fast path 接到 `PackageEditor::replace_worksheet_cells()`
内部路径，因此 public `WorkbookEditor::replace_cells()` 在简单 strict replace 场景下也能
自动受益：当前只覆盖 source worksheet 是 stored/no-compression package entry、没有已排队
planned worksheet replacement、没有 worksheet `.rels`、不是 upsert、且
`ReferencePolicyAction::Fail` 未启用，并且 source worksheet 已有顶层 `<dimension>`
metadata 可被安全原样保留的场景；compressed source、planned input、upsert、
policy Fail、缺失顶层 `<dimension>` 或带 worksheet relationships 的 worksheet 仍回退到 transformer。
内部回归会断言该路径输出 staged file-range chunks + replacement memory chunks，且不
materialize 整份 worksheet XML。相同 `10000000 / 1000`、复用既有 source package、
`--editor-api public-workbook-editor` 的本地 release benchmark 初次录得
`patch_plan_ms=7540`、`save_ms=2120`、`verify_ms=1982`、
`total_edit_ms=9663`、`total_ms=11659`、`peak_memory_mb=5.96`；加入 dimension
guard 后复跑同一 source package 录得 `patch_plan_ms=6264`、`save_ms=2013`、
`verify_ms=1875`、`total_edit_ms=8278`、`total_ms=10156`、`peak_memory_mb=6.02`，且
`output_verified=true`。

2026-06-26 继续给 public benchmark 增加只读 internal output-plan telemetry。相同
`10000000 / 1000`、复用既有 source package、
`--editor-api public-workbook-editor` 的本地 release rerun 录得
`patch_plan_ms=6419`、`save_ms=2492`、`verify_ms=1908`、
`total_edit_ms=8912`、`total_ms=10822`、`peak_memory_mb=6.02`，且
`output_verified=true`。JSON 现在明确记录
`output_plan_observed=true`、
`output_plan_indexed_source_entry_fast_path=true`、
`output_plan_transformer_fallback=false`、
`output_plan_staged_replacement_chunks=true`、
`output_plan_materialized_replacement=false`、
`output_plan_staged_replacement_chunk_count=1101`、
`output_plan_staged_replacement_file_range_chunk_count=101`、
`output_plan_staged_replacement_memory_chunk_count=1000`、
`indexed_source_cell_count=10000000`、
`indexed_matched_replacement_count=1000` 和
`package_entry_source_mode="source-package-worksheet-entry-direct-range-chunks"`。这证明
public facade 实际走的是 source-entry direct-range staged chunks，而不是旧
transformer file-backed full-output 路径；`rewrite_strategy="transformer"` 仍只是 CLI
默认策略名，因为 public facade 没有暴露 strategy 选择。当前 public fast path 仍保留完整
source scan 以检测 missing / duplicate source targets，所以不是 benchmark early-stop，不是
O(1) 任意随机编辑，也不修复 sharedStrings/styles、tables、filters、drawings、
defined names、formulas、relationships 或缺失/stale dimension。

同日继续把 public direct-range 输出计划 telemetry 从可读 note 文本解析升级为
结构化 `PackageEditorOutputEntryPlan` 字段：`planned_output()` 现在直接暴露
`indexed_source_entry_direct_range`、scanned source cell count、matched replacement
count 和 staged output bytes，benchmark 不再从 note 文案中解析数字。相同
`10000000 / 1000`、复用既有 source package、`--editor-api public-workbook-editor`
的本地 release rerun（`build/qa/continue-public-structured-noparse-result.json`）
录得 `patch_plan_ms=9230`、`save_ms=2622`、`verify_ms=2012`、
`total_edit_ms=11857`、`total_ms=13871`、`peak_memory_mb=6.00`，且
`output_verified=true`。JSON 仍明确记录
`output_plan_indexed_source_entry_fast_path=true`、
`output_plan_transformer_fallback=false`、
`output_plan_materialized_replacement=false`、
`output_plan_staged_replacement_file_range_chunk_count=11`、
`output_plan_staged_replacement_memory_chunk_count=1000`、
`indexed_source_cell_count=10000000`、
`indexed_matched_replacement_count=1000` 和
`indexed_staged_output_bytes=367269975`。本轮目的不是降低耗时，而是让性能报告对
fast path 选择和计数具有结构化证据；`patch_plan_ms` 仍包含完整 source worksheet
扫描，且本机 400 MB stored package 写盘/读取会产生明显波动。

同日继续收敛 target-only planner 的内部查找结构：public 默认路径不启用
early-stop，因为那会在 source XML 后段存在重复 target cell 时跳过重复检测，削弱当前
strict/no-state-pollution 语义。实现上把 target coordinate 查找整理为排序后的
vector bucket，并补充大小写归一化重复坐标拒绝回归；这减少树节点/哈希桶类状态，但不是
稳定性能声明。相同 `10000000 / 1000` public facade verified rerun
（`build/qa/continue-public-vector-target-verified-result.json`）录得
`patch_plan_ms=10573`、`save_ms=3821`、`verify_ms=3937`、
`total_edit_ms=14399`、`total_ms=18339`、`peak_memory_mb=6.27`，
且 `output_verified=true`。该结果说明当前剩余成本仍由 source scan、stored ZIP
写出/读取和本机磁盘状态共同支配；不能把本轮 cleanup 写成大文件随机访问完成。

同日继续把 public direct-range fast path 的 `patch_plan_ms` 拆成内部阶段计时，
并写入 schema-v2 benchmark JSON。相同 `10000000 / 1000`、复用既有 source
package、`--editor-api public-workbook-editor` 的本地 release rerun
（`build/qa/continue-phase-telemetry-result.json`）录得
`patch_plan_ms=4180`、`save_ms=2198`、`verify_ms=2525`、
`total_edit_ms=6382`、`total_ms=8910`、`peak_memory_mb=7.36`，且
`output_verified=true`。新增字段显示
`output_plan_indexed_source_entry_target_plan_ms=4175`，而
`source_range_chunk_ms`、`payload_audit_ms`、`relationship_audit_ms`、
`descriptor_ms` 和 `commit_ms` 均为 0ms 量级。这证明当前 patch planning
瓶颈几乎全部在 target-only source scan，而不是 replacement payload audit、
relationship audit、chunk descriptor 生成或 commit。public 默认路径仍不启用
`stop_after_all_targets_found`，因为该开关会跳过尾部 duplicate target source
cell 检测；后续优化应围绕可证明的 valid row-major source fast validation、
持久/复用 source index，或 PackageReader/source-entry 层更低成本扫描，而不是牺牲
strict replace 校验换取表面耗时。

同日继续把 PackageEditor 内部 file-backed chunk CRC/copy buffer 从 64KiB 提升到
1MiB，并改用 heap buffer，避免 Windows 默认栈承载 1MiB `std::array`。相同
`10000000 / 1000` public facade verified rerun
（`build/qa/continue-package-editor-fileio-1m-result.json`）录得
`patch_plan_ms=4340`、`output_plan_indexed_source_entry_target_plan_ms=4336`、
`save_ms=2026`、`verify_ms=1953`、`total_edit_ms=6370`、
`total_ms=8326`、`peak_memory_mb=7.39`，且 `output_verified=true`。这属于
PackageEditor file IO 粒度 cleanup，主要降低 save/copy/CRC 固定成本；它不改变
strict source scan、CRC/size 校验、staged chunk 语义、public API 或随机访问边界。

同日继续优化同一 source scan 热路径，但不引入新 XML parser：`WorksheetEventReaderOptions`
增加内部 `copy_context_attributes` 开关，target-only planner 默认关闭它，只在
row/cell start 事件读取属性并自行维护 active cell state，避免扫描 10M source cells
时为后续 nested/end events 复制当前 row/cell context 字符串。默认 event reader 行为保持
copy-on，现有 transformer / diagnostic caller 不受影响。相同 `10000000 / 1000`
public facade verified rerun（`build/qa/continue-public-nocopy-context-result.json`）录得
`patch_plan_ms=8743`、`save_ms=2813`、`verify_ms=2725`、
`total_edit_ms=11557`、`total_ms=14284`、`peak_memory_mb=6.29`，
且 `output_verified=true`。这比上一轮 sorted-bucket verified run 的
`patch_plan_ms=10573` 更低，但本机 IO/缓存波动仍明显；可作为安全 hot-path cleanup
证据，不能写成稳定性能 SLA。

同日进一步把 target-only planner 切到 internal lightweight scanner，并对该 scanner
的 hot-path tag-name / attribute parsing 做直接扫描优化。它只服务当前 bounded
target range planning：识别 worksheet / sheetData / row / cell / value-wrapper
边界，保留 strict missing/duplicate validation 和 top-level dimension detection，
不扩大 public API，也不替代通用 worksheet event reader。相同 `10000000 / 1000`
public facade verified rerun（`build/qa/continue-public-fast-target-scanner-parser-result.json`）
录得 `patch_plan_ms=7852`、`save_ms=2272`、`verify_ms=2183`、
`total_edit_ms=10128`、`total_ms=12324`、`peak_memory_mb=6.26`，且
`output_verified=true`、`indexed_source_cell_count=10000000`、
`indexed_matched_replacement_count=1000`、`indexed_staged_output_bytes=367269975`。
该结果说明 scanner/parser cleanup 能降低当前 public strict replace 的 source-scan
overhead，但它仍是线性 source XML scan；不能写成 arbitrary random access、
metadata repair、sharedStrings/styles migration 或 relationship repair 已完成。

随后继续优化 save path：stored ZIP writer 对同一输入文件的 source file-range chunks
保留 active input stream，并在小的前向 gap 上顺序丢弃而不是反复 seek；stored/minizip
文件复制缓冲从 64 KiB stack buffer 调整为 1 MiB heap buffer，stored writer 也给
output stream 设置同级别 buffer。相同 `10000000 / 1000` public facade reused-source
rerun（`build/qa/continue-save-buffered-rerun-result.json`）录得
`patch_plan_ms=6416`、`save_ms=2152`、`verify_ms=1905`、`total_edit_ms=8572`、
`total_ms=10479`、`peak_memory_mb=8.03`，且 `output_verified=true`。该结果说明
当前 stored output path 已把 367 MB package 写出压到约 2.15s；峰值内存上升约
1.7-2 MB 是显式 IO buffer 成本，不是 worksheet DOM 或完整 cell matrix。

随后继续收窄 `save_as()` copy-original 路径：stored source package entries 不再先
extract 到 `PackageEditor` 临时文件，而是直接作为 source package file-range chunk
交给 `PackageWriter`，并携带 source entry CRC 作为 expected CRC；DEFLATE source
entry 和测试 hook 仍走原先的 temp-file fallback，以保留压缩源 correctness 和故障注入覆盖。
相同 `10000000 / 1000` public facade reused-source rerun
（`build/qa/continue-direct-source-copy-rerun-result.json`）录得
`patch_plan_ms=4774`、`save_ms=1951`、`verify_ms=1905`、`total_edit_ms=6727`、
`total_ms=8639`、`peak_memory_mb=7.99`，且 `output_verified=true`。
输出计划记录 `11` 个 source file-range chunks、`1000` 个 replacement memory chunks、
`package_entry_source_mode="source-package-worksheet-entry-direct-range-chunks"` 和
`output_entry_mode="indexed-source-entry-direct-range-staged-chunks"`。这说明当前 sparse
strict replace 的 public facade 在本机 10M/1000 场景下已能把编辑+保存压到约
6.7s，但仍依赖 stored source、顶层 dimension、无 worksheet relationships 和 strict
existing-cell replacement；它不是 compressed-source fast path、Zip64、arbitrary O(1)
random access、metadata repair 或 relationship repair。

随后继续去掉 source-entry direct-range path 的重复 CRC 预读：source stored-entry
chunks 现在直接携带 `PackageReaderEntry` 已知的 expected size / CRC32，避免
`PackageEntryChunkReader` 构造阶段为了补 descriptor metadata 预先读取整段
worksheet payload。实际 target scan/replay 仍会读取 source entry bytes 并校验 CRC，
所以这是重复 IO 消除，不是关闭校验。相同 `10000000 / 1000` public facade
reused-source rerun（`build/qa/continue-source-crc-skip-result.json`）录得
`patch_plan_ms=3932`、`save_ms=1818`、`verify_ms=1757`、`total_edit_ms=5753`、
`total_ms=7512`、`peak_memory_mb=7.92`，且 `output_verified=true`。输出仍记录
`package_entry_source_mode="source-package-worksheet-entry-direct-range-chunks"`、
`output_entry_mode="indexed-source-entry-direct-range-staged-chunks"`、`11` 个 source
file-range chunks 和 `1000` 个 replacement memory chunks。对比上一轮
`total_edit_ms=6727`，这次主要收益来自少读一次 stored source worksheet payload；
边界仍不变：不支持 compressed-source direct range、Zip64、arbitrary O(1) random
access、metadata repair、relationship repair 或 sharedStrings/styles migration。

再下一步把内部 `PackageEntryChunkReader` 的 file replay chunk 从 64 KiB 放大到
1 MiB，用来减少 source worksheet 线性扫描时的 read callback / scanner window
handoff 次数；既有 `package_editor_cell_replacement_event_window_byte_limit` 仍是
4 MiB，没有放宽 retained-window guard，也不改变 chunk CRC 校验。相同
`10000000 / 1000` public facade reused-source rerun
（`build/qa/continue-reader-1m-result.json`）录得 `patch_plan_ms=3449`、
`save_ms=1827`、`verify_ms=1725`、`total_edit_ms=5279`、`total_ms=7007`、
`peak_memory_mb=7.94`，且 `output_verified=true`。这说明当前计划阶段还在受
full-source scan 支配，但 callback/window 固定开销已经继续下降；它仍不是 O(1)
random access，也不改变 fallback 或 metadata/relationship 边界。

随后调整 `PackageReader` stored/DEFLATE chunk-source IO buffer，从 64 KiB 提升到
1 MiB，用于减少 package entry streaming / benchmark verifier 的 reader callback
次数；ZIP metadata、CRC 校验和 entry size 校验不变。由于本机磁盘状态波动较大，
这里记录同一环境下的 A/B 结果：`build/qa/continue-reader-io-1m-result.json`
录得 `patch_plan_ms=4656`、`save_ms=2567`、`verify_ms=2289`、
`total_edit_ms=7226`、`total_ms=9521`、`peak_memory_mb=7.41`，
`output_verified=true`；临时退回 64 KiB 的
`build/qa/continue-reader-io-64k-result.json` 录得 `total_edit_ms=9099`。
这说明 larger reader IO buffer 在当前验证路径下减少了固定 IO/callback 开销；它不改变
worksheet fast path 选择、strict replace 校验、ZIP 格式支持或 public API。

再下一步继续收窄 target-only planner 的 hot-path 固定开销：当 scanner 没有跨 chunk
残留 XML token 时，直接扫描当前 chunk 的 `string_view`，只把未完成的 tail 复制进
bounded window；同时 source cell reference 解析改用 ASCII 大小写折叠，避免每个列字母
走 `std::toupper` locale 路径。相同 `10000000 / 1000` public facade reused-source
verified rerun（`build/qa/continue-direct-window-ascii-verified-result.json`）录得
`patch_plan_ms=5537`、`save_ms=2681`、`verify_ms=2758`、
`total_edit_ms=8219`、`total_ms=10980`、`peak_memory_mb=7.38`，且
`output_verified=true`；隔离 verifier 的
`build/qa/continue-ascii-ref-noverify-result.json` 录得 `patch_plan_ms=4563`、
`save_ms=2111`、`total_edit_ms=6675`。这应记录为 scanner/copy hygiene，而不是新的
性能 SLA：真实 wall-clock 仍明显受本机磁盘状态、stored ZIP save 和 verifier replay 影响。

再下一步减少 staged output descriptor 的固定开销：相邻 replacement memory chunks
现在会在 direct-range descriptor 层合并，并同步 merged chunk 的 expected size / CRC32
校验元数据；source file-range chunks 不合并，以保持保守边界。相同
`10000000 / 1000` public facade reused-source verified rerun
（`build/qa/continue-memory-chunk-merge-result.json`）录得
`patch_plan_ms=3990`、`save_ms=2037`、`verify_ms=2005`、
`total_edit_ms=6030`、`total_ms=8038`、`peak_memory_mb=7.49`，且
`output_verified=true`。输出计划记录 `21` 个 staged chunks，其中 `10` 个 replacement
memory chunks 和 `11` 个 source file-range chunks；`staged_replacement_memory_bytes=30840`
与 public facade replacement XML bytes 一致。该优化减少 per-chunk replay/validation
固定成本，但不改变 source scan、strict duplicate/missing-target validation、ZIP 校验、
public API、metadata repair 或随机访问边界。

上一组 internal `PackageEditor` 1M / 3M / 5M 快照另有本机 Excel COM sidecar
验证：使用 Excel 16.0 只读打开三个输出，
并核对 `Data` sheet UsedRange 分别为 `100000 x 10`、`300000 x 10`、`500000 x 10`，
`A1=900000000`，尾部未修改 source cell 分别为 `100000000010`、`300000000010`、
`500000000010`。sidecar 报告写入
`build/bench-package-editor/package-editor-cell-replacement-office-report.json`，且不回写
benchmark JSON；原始 `office_open` 字段仍保持工具输出的 `not_run`。

该快照来自 public direct-range fast path 落地前的 internal `PackageEditor` 路径，
说明底层 Patch cell replacement 已能在 5M cells 级别避免整张 source worksheet XML
物化，并把 rewritten worksheet 作为 file-backed staged chunks 输出。当前 public
`WorkbookEditor::replace_cells()` 已在保守 stored/no-rels strict replace 场景复用
source-entry direct-range staged chunks；该复用还要求 source worksheet 已有顶层
`<dimension>`，否则保持 transformer dimension-refresh 路径。更广泛 public 路径 benchmark 应继续使用
`--editor-api public-workbook-editor` 重新跑同规模矩阵。该能力仍不是
任意大文件随机编辑：它只替换已存在 cells，不插入缺失 cells/rows，不覆盖 sharedStrings/styles
迁移、table / range metadata recalculation、relationship repair、Zip64、DEFLATE
input/output 或更广泛的 Office/WPS/LibreOffice 兼容性；本次 Excel 结果只证明这些本地
stored-package 输出可被 Excel 只读打开并读到预期单元格。

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
