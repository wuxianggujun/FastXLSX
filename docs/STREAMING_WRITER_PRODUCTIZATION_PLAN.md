# Streaming Writer 产品化完善计划

## 文档定位

本文是 `WorkbookWriter` / `WorksheetWriter` / `CellView` 的 Streaming new-workbook
产品化计划，用于把当前已有的流式写入能力推进到可对外稳定推荐的报表导出主线。

本文不是当前能力事实源。当前 public / internal / planned / non-goal 状态只以
[CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md) 为准；本文只描述后续任务如何拆分、
按什么顺序做、如何验收，以及哪些边界不能破坏。

所属路径：

- 模式：Streaming。
- Public facade：`WorkbookWriter`、`WorksheetWriter`、`CellView`。
- 任务入口：`TASK_BREAKDOWN.md` 中的 F3 Streaming writer polish 和 C6 支撑线。
- 核心约束：不破坏 row-order hot path，不引入大型 worksheet DOM，不持有完整
  worksheet cell matrix，不把内部 package / chunk 实现写成 public API。

## 当前基线

Streaming writer 已经不是“只有最小 XLSX 输出”的状态。当前能力包括有序 row/cell 写入、
数字/文本/布尔/公式单元格、显式 `StringStrategy`、document properties、行高、列宽、
冻结窗格、自动筛选、合并单元格、data validations、external/internal hyperlinks、
streaming-only tables、two-/three-color color scales、basic data bars、basic `3Arrows`
icon sets、基础 number format / alignment / font / fill styles、PNG/JPEG 图片插入，以及
opt-in minizip-ng DEFLATE 输出。

但从“生产可推荐”的角度，差距仍然明显，主要不在底座，而在产品化闭环：

- API 易用性还偏底层，日期、空白、错误值、稀疏行、常用样式和报表布局需要更顺手。
- 样式和 worksheet metadata 仍是窄切片，不能给用户造成完整 Excel parity 的错觉。
- 示例覆盖不足，当前示例不能代表真实业务报表、图片、表格、样式、条件格式和大数据导出组合。
- benchmark / QA 证据还需要矩阵化，不能只靠小样本或单一后端支撑 release wording。
- `src/streaming_writer.cpp` 和主测试文件承载越来越多功能，需要按成熟度逐步拆分实现和测试。
- 文档必须持续区分 current / planned / non-goal，避免把路线图写成已支持能力。

因此，本计划的目标不是重新设计 Streaming writer，而是围绕已有 row-oriented 架构补齐易用性、
验证证据、示例和模块边界。

## 非目标

以下能力不属于本计划，除非先经过新的 public API design gate：

- 不支持随机回写历史行或修改已经 append 的 row XML。
- 不把大型 worksheet 或 sharedStrings 隐式装入 DOM。
- 不引入完整 worksheet cell matrix、dense cell map 或跨行无界状态。
- 不把 `WorkbookWriter` 扩展成 existing-file editor；已有文件编辑仍走 Patch / In-memory 路径。
- 不计算公式，不生成 cached values，不承诺完整 calcChain rebuild。
- 不把 sharedStrings 写成默认最佳策略；它仍是显式体积/去重策略，默认低内存路径是 inline string。
- 不把 file-backed/chunked worksheet entry 写成 true package streaming、Zip64 或完整低内存保证。
- 不做完整 Excel parity，例如 full styles、rich text、pivot、chart、macro、full drawing editing。
- 不公开 `PackageEditor`、`EditPlan`、`DependencyAnalyzer`、`RelationshipGraph` 或 package-entry chunk helper。

## 设计原则

1. Row-oriented first：用户按行追加数据，writer 只维护当前行、worksheet 小型 metadata、
   workbook 小型 metadata 和必要的策略状态。
2. Validate before mutate：所有 public API 在推进 row count、dimension、sharedStrings、styles、
   relationship 或 package state 前完成参数校验。
3. Bounded metadata：metadata API 允许按规则数、range 数、图片数、表格数增长，但不能随 worksheet
   cell count 形成无界缓存。
4. Explicit tradeoff：涉及 string strategy、compression level、date serial、styles、images、
   tables 和 conditional formatting 时，文档必须写清内存、输出 part 和兼容性副作用。
5. Evidence-driven wording：性能和兼容性声明必须来自 benchmark、结构测试、openpyxl、Excel COM
   或等价 QA 证据。
6. Public API stable, internals split：public header 可以保持集中入口；实现、XML 序列化、状态转换、
   QA helper 和 feature-specific 测试按边界拆分。

## 模块边界

Public API 层：

- `include/fastxlsx/streaming_writer.hpp` 保持用户入口稳定，承载 Doxygen 边界说明。
- `WorkbookWriter` 管理 workbook 级状态、style registry、sharedStrings 策略、worksheet 列表和 close。
- `WorksheetWriter` 管理单个 worksheet 的 row append、worksheet metadata、drawing/table/hyperlink 等记录。
- `CellView` 表示 append-time 非 owning cell view，不作为长期存储模型。

内部实现层：

- `src/streaming_writer.cpp` 继续作为 streaming 流程协调层和热路径入口。
- 当某个 feature 拥有独立状态、XML serializer、校验规则、package side effects 和大量测试时，
  可以拆到候选实现文件，例如 `src/streaming_styles.cpp`、`src/streaming_metadata.cpp`、
  `src/streaming_tables.cpp`、`src/streaming_drawings.cpp`、`src/streaming_shared_strings.cpp`。
- 新增 `.cpp` 必须同步更新 `CMakeLists.txt`。

测试与 QA 层：

- 主流程和跨功能 suffix ordering 可留在 `tests/test_streaming_writer.cpp`。
- 功能细节应优先进入 feature-specific 测试，例如 metadata、styles、tables、images、sharedStrings、
  conditional formatting。
- 本地 Office / openpyxl / XlsxWriter 对比脚本保持 opt-in QA，不进入默认 CTest 强依赖。

## 数据流

Streaming writer 的目标数据流保持如下：

```text
caller row iterator / append loop
-> CellView / row metadata validation
-> worksheet row XML byte stream
-> worksheet body temporary entry / chunk source
-> workbook small XML parts, styles, sharedStrings, rels, content types
-> package writer backend
-> .xlsx
```

需要强调的边界：

- row XML 写入后不可回头扫描或重写历史行。
- worksheet dimension 只能增量维护，不能为了匹配 Excel `UsedRange` 做全表回扫。
- sharedStrings 可以复制唯一字符串到 workbook-level table；这是一种显式策略成本。
- image bytes 可以进入 file-backed media entry，但不能进入 worksheet row hot path。
- 表格、超链接、条件格式、data validation 等 metadata 写 suffix XML 或独立 part，不读取历史 cell 值。

## 状态流转

`WorkbookWriter` 推荐状态：

```text
created
-> worksheets/styles/options accepted
-> rows and metadata appended
-> close started
-> package entries assembled
-> closed
```

`WorksheetWriter` 推荐状态：

```text
attached
-> metadata calls accepted while workbook is open
-> append_row validates full row
-> append_row writes row and advances row_count/dimension
-> close serializes suffix metadata and relationships
-> detached/closed mutation rejected
```

失败策略：

- public mutating API 失败时，必须尽量在 state change 之前失败。
- `append_row()` 的失败不能推进 row number / dimension，不能写入被拒绝 cells，不能污染 sharedStrings，
  不能触发 formula recalculation metadata。
- metadata API 的失败不能留下半条 validation、hyperlink、table、image、conditional formatting 或 style。

## Workstreams

### S1 API 易用性

目标：让常见报表导出不需要用户手写大量 OpenXML 细节，同时不牺牲 streaming 热路径。

候选任务：

- `CellView::blank()`：支持显式空白单元格，清楚区分“省略 cell”和“写空白 cell”。需要定义 XML 输出、
  dimension 影响、sharedStrings 无副作用和 style 搭配规则。
- 稀疏行 API：为宽表中少量有值的行提供 column-indexed row view 或 builder，避免用户用大量 blank
  占位，同时保持行内一次性消费和递增 row order。
- date/time helper：提供 Excel serial 转换 helper 和常用 number format presets。底层仍写 number cell，
  不引入 date cell type，也不做时区推断。
- `CellView::error()`：支持 OpenXML `t="e"` 错误 token 的窄切片，采用明确 token allowlist 或严格
  opaque-token 策略，不计算错误语义。
- `CellView::from(CellValue)` 或 owning row builder：复用 `CellValue` 便利性时必须解决 string lifetime。
  直接返回非 owning `CellView` 的 API 只能在调用期有效；如果需要持有文本，应由 row builder 持有 owning storage。
- 常用 row builder：提供 header / values / formula / style 组合便利层，确保 builder 输出仍是一次性
  `std::span<const CellView>` 或等价 view。

验收重点：

- Doxygen 写清 string lifetime、blank/error/date 边界和性能行为。
- 失败调用不污染 row state。
- 宽表稀疏 API 不退化成完整 row matrix。

### S2 报表样式

目标：覆盖高频报表样式，而不是追求完整 styles.xml parity。

当前已有窄切片：custom number formats、wrap-text、有限 horizontal/vertical alignment、bold/italic、
direct ARGB font color、solid fill。

候选任务：

- border：优先支持 thin / medium / thick / dashed 等常用边框、四边/全边配置和 direct ARGB color。
- font 扩展：font size、font name、underline、strike 可分阶段进入；不做 rich text。
- style presets：提供内置 header、currency、percent、date、datetime、integer、warning、success 等
  helper，底层仍走 `WorkbookWriter::add_style()`。
- style dedup：继续保证相同 style 注册复用，增加对新增 style 维度的去重测试。
- style + sharedStrings + formulas 共存：验证 `s` 属性、`t` 属性、formula recalculation metadata
  和 `xl/styles.xml` 的组合稳定。

非目标：

- 不做 existing-file style migration / merge。
- 不做 conditional formatting `dxf` 样式。
- 不做完整 font/fill/border/alignment Excel parity。

### S3 Worksheet Metadata

目标：补齐常见报表布局设置，优先选择不读取历史 row/cell 的 metadata。

候选任务：

- sheet tab color：写 `sheetPr/tabColor`。
- sheet view：zoom scale、show gridlines、active cell / selection、active / selected sheet。
- page setup / margins：paper size、orientation、fit-to-page、print area、page margins。
- print titles：需要 workbook definedNames 联动，必须写清 workbook-level side effects。
- row / column metadata：hidden、outline level、default row height、default column width 等可分阶段实现。
- sheet protection 可作为独立 gate，不能混入基础 metadata polish。

验收重点：

- 明确每个 API 写哪些 worksheet/workbook part。
- 和现有 freeze panes、auto filter、merge cells、tables、hyperlinks 的 suffix ordering 不冲突。
- 不引入 row/cell 回扫。

### S4 数据类型边界

目标：让用户更清楚地表达值，同时保持 OpenXML 输出可控。

候选任务：

- blank：显式空白、缺省 cell、空字符串三者文档和测试分开。
- error：限定错误 token 支持范围，不做错误计算。
- date/time：只提供 serial helper 和 style presets，不做自动 timezone 或 locale 转换。
- text-as-number：提供文档和示例说明如何强制文本，避免 Excel 自动转换造成业务 ID 丢失。
- finite numeric：继续拒绝 NaN / Inf，不能默默转文本或空白。

### S5 性能与 Benchmark 证据

目标：把“高性能”从口号变成可引用证据。

建议 benchmark 矩阵：

- 数据规模：1M、10M、50M cells。
- 数据形态：numeric-only、mixed text/number、repeated strings、high-cardinality unique strings。
- 字符串策略：inlineStr、sharedStrings。
- ZIP backend：stored bootstrap、minizip-ng。
- compression level：stored / default / 1 / 3 / 6，按 backend 可用性选择。
- 输出记录：dataset、backend、compression、string strategy、source mode、wall time、peak memory 或估算、
  output size、temporary worksheet footprint、openpyxl / Excel 打开结果。

验收重点：

- benchmark 是 opt-in，不能进入默认 CTest。
- benchmark JSON schema 变更必须同步文档。
- `temporary_worksheet_part_footprint_bytes` 只能描述 worksheet body row XML 字节数，不能写成完整峰值内存。
- release wording 必须引用具体矩阵结果，不写泛化承诺。

### S6 兼容性 QA

目标：每个对外能力都有结构检查和至少一种办公软件 / 解析器 smoke 证据。

必做检查：

- ZIP entry duplicate 检查。
- `[Content_Types].xml` override / default 检查。
- workbook relationships、worksheet `.rels`、drawing `.rels`、table parts 一致性。
- worksheet XML suffix ordering。
- XML escaping：text、formula、hyperlink display / tooltip、table columns、validation messages、style names。
- openpyxl load smoke。
- Excel COM read-only smoke 作为 Windows 本地可选 QA。

按功能补充：

- sharedStrings：count / uniqueCount、跨 sheet 去重、xml:space、空字符串策略、无字符串 cell 时不生成死 part。
- styles：numFmtId、cellXfs、font/fill/border/alignment 组合、默认 style 省略。
- tables：table id/name、column count、totals row metadata、table style info、worksheet relationship。
- images：media part、drawing part、anchor、content type、PNG/JPEG metadata、不会修改 existing workbook drawing。
- conditional formatting：priority、multi-range `sqref`、规则 XML、无 dxfs/rels/content-type 副作用。

### S7 示例与文档

目标：用户能从示例直接复制出真实报表，而不是只看到 toy workbook。

建议示例：

- `streaming_basic_export`：基础 row append、公式、列宽、冻结窗格。
- `streaming_large_numeric`：大规模数字导出，说明 benchmark 和内存边界。
- `streaming_sales_report`：header styles、number formats、tables、freeze panes、auto filter。
- `streaming_styles`：number format、alignment、font、fill、后续 border。
- `streaming_tables`：table range、style flags、totals row metadata。
- `streaming_validations_hyperlinks`：data validation、external/internal hyperlink、tooltip/display。
- `streaming_conditional_formatting`：color scale、data bar、3Arrows。
- `streaming_images`：PNG/JPEG 插入、anchor、metadata。
- `streaming_shared_strings_strategy`：inline vs sharedStrings 的体积和内存取舍。

文档入口：

- README 只保留用户入口和边界摘要，不复制长矩阵。
- API docs 写清每个 public API 的模式、内存行为、side effects、失败边界和 non-goals。
- `CURRENT_CAPABILITIES.md` 只在能力真实变更后更新。

### S8 内部模块化

目标：避免 `src/streaming_writer.cpp` 和主测试文件持续膨胀，降低后续功能修改风险。

拆分触发条件：

- feature 已有独立 public API。
- feature 有独立状态结构或 XML serializer。
- feature 有独立 package side effects。
- feature-specific 测试明显挤占主流程测试。
- feature 有独立 QA helper 或 Office/openpyxl 验证脚本。

拆分原则：

- public header 不因内部拆分而频繁 churn。
- 热路径的 cell XML 编码保持直接、可测、可 profile。
- 新增 `.cpp` / 测试文件同步更新 `CMakeLists.txt` / `tests/CMakeLists.txt`。
- 不为了很小的临时代码做过早抽象。

## 阶段顺序

### Phase 0：事实与入口收敛

- 保持本文、`TASK_BREAKDOWN.md` 和 `CURRENT_CAPABILITIES.md` 的定位清楚。
- 新任务必须标注 Streaming / Patch / In-memory 路径和 public facade。
- 不把 planned 能力写成 current。

### Phase 1：补最影响用户体验的 API 缺口

- blank / sparse row。
- date/time helper + format presets。
- error cell。
- owning row builder 或 `CellValue` adapter。

### Phase 2：真实业务报表 polish

- border / font 扩展 / style presets。
- sheet view / page setup / print metadata。
- sales report 示例和 Office smoke。

### Phase 3：证据闭环

- benchmark matrix。
- sharedStrings / styles / images / tables / conditional formatting QA。
- release wording gate。

### Phase 4：模块拆分

- 按成熟 feature 拆实现和测试。
- 保持 `streaming_writer.cpp` 为流程协调层。
- 避免 public API churn。

## Definition of Done

Streaming writer 可称为“产品化完成”的最低标准：

- 能用单个示例生成一个真实业务报表：标题、表格、公式、number formats、基础样式、冻结窗格、
  自动筛选、条件格式、validation、hyperlink、图片可组合。
- 10M cells 有可重复 benchmark；50M numeric 有 opt-in evidence 或明确未覆盖说明。
- inlineStr 与 sharedStrings 的使用建议有数据支撑。
- stored bootstrap 与 minizip-ng 的输出和性能取舍有文档。
- 样式、metadata、tables、images、conditional formatting 的结构测试和 QA helper 覆盖核心边界。
- Excel / openpyxl 至少对关键样例做 smoke。
- public Doxygen 写清 string lifetime、内存增长、side effects、失败边界和 non-goals。
- `CURRENT_CAPABILITIES.md` 与 public header、README、API docs 不漂移。
- 没有把 internal OPC / Patch / package helper 写成 public API。

## 任务模板

后续每个 Streaming writer 产品化任务必须按这个模板写清楚：

```text
任务编号：
目标：
模式：Streaming
Public facade：WorkbookWriter / WorksheetWriter / CellView / value helper
输入：
输出：
触碰文件：
不触碰文件：
OpenXML side effects：
内存行为：
失败策略：
验收标准：
禁止项：
验证命令：
```

如果任务无法填写 `OpenXML side effects`、`内存行为` 或 `失败策略`，说明它还不能进入实现。

## 首批建议任务

### SWP-1 CellView blank / sparse row

目标：解决宽表和显式空白表达问题。

建议触碰：

- `include/fastxlsx/streaming_writer.hpp`
- `src/streaming_writer.cpp`
- `tests/test_streaming_writer.cpp` 或新的 streaming row value 测试文件
- API docs / examples

验收：

- blank、missing、empty string 三者 XML 和 dimension 行为清楚。
- 失败不推进 row state。
- 稀疏行 API 不需要构造完整 dense row。

### SWP-2 Date/time helper and format presets

目标：让用户不用手写 Excel serial 和常用日期格式。

建议触碰：

- public helper header 或 `streaming_writer.hpp`
- style preset helper
- focused unit tests
- example / Doxygen

验收：

- 明确 1900 date system 策略。
- 不做 timezone 推断。
- 输出仍是 number cell + style。

### SWP-3 Border and font expansion

目标：补齐常见报表 header / total row 样式。

建议触碰：

- `CellStyle` / style registry
- styles XML serializer
- styles QA helper

验收：

- border/font 新字段参与去重。
- 默认 style 不生成多余 `s="0"`。
- 和 sharedStrings / formulas 共存。

### SWP-4 Sales report example and QA

目标：形成一个用户能直接参考的真实报表示例。

建议触碰：

- `examples/`
- `examples/CMakeLists.txt`
- README 示例索引
- opt-in QA script

验收：

- 示例组合 styles、table、formula、freeze panes、auto filter、conditional formatting、validation、hyperlink。
- openpyxl 和本地 Excel COM 可选 smoke 有记录。

### SWP-5 Benchmark matrix release evidence

目标：形成可引用的 performance evidence。

建议触碰：

- benchmark runner / docs
- `docs/PERFORMANCE_TARGETS.md`
- release wording gate

验收：

- 至少覆盖 1M / 10M cells。
- 记录 string strategy、backend、compression、wall time、memory estimate、output size、open result。
- benchmark 仍是 opt-in。

### SWP-6 Streaming implementation/test split

目标：降低后续 feature 修改的回归风险。

建议触碰：

- `src/streaming_writer.cpp`
- 候选 `src/streaming_*.cpp`
- `tests/test_streaming_writer*.cpp`
- `CMakeLists.txt` / `tests/CMakeLists.txt`

验收：

- public API 不因拆分变化。
- 主测试保留跨功能集成；feature-specific 测试承担细节。
- 默认 CTest 仍在 60s 核心边界内。

## 文档-only 验证

修改本文或任务入口但不改源码时，验证级别是文档检查：

```powershell
git diff --check -- docs/STREAMING_WRITER_PRODUCTIZATION_PLAN.md docs/TASK_BREAKDOWN.md
rg -n "STREAMING_WRITER_PRODUCTIZATION_PLAN|Streaming writer polish|SWP-" docs/STREAMING_WRITER_PRODUCTIZATION_PLAN.md docs/TASK_BREAKDOWN.md
```

不需要运行 C++ build / CTest，除非同时修改 public header、source、CMake 或测试。
