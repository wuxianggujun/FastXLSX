# FastXLSX

[![爱发电](https://img.shields.io/badge/%E7%88%B1%E5%8F%91%E7%94%B5-%E6%94%AF%E6%8C%81%E5%BC%80%E6%BA%90-946ce6?style=flat-square)](https://ifdian.net/a/wuxianggujun)

FastXLSX 是一个面向高性能 XLSX 处理和可控编辑的现代 C++ 库。

项目目标不是简单延续 `FastExcel` 的旧实现，而是重新设计一个
**可编辑的高性能 XLSX/OpenXML 引擎**：大文件走流式路径，已有文件走
part-level patch，小文件保留 in-memory 随机编辑体验。

## 定位

- 大数据写入路径：禁止依赖 DOM，必须流式生成 XML。
- 读取路径：使用 SAX / event parser，避免整表加载。
- 编辑路径：作为核心主线设计，分为 Patch 编辑、In-memory 随机编辑和
  大型 worksheet 受控流式重写。
- 小型 XML part：允许局部 DOM 或轻量对象模型，用于样式、工作簿元数据、
  关系文件、小型 XML part 和复杂局部修改。
- 未修改的 XLSX part：尽量原样透传，避免破坏图表、图片、宏和未知扩展。

## 设计原则

1. 新建大文件写入性能优先
   - worksheet.xml 使用流式写入。
   - 单元格 XML 热路径直接写入字节流。
   - 压缩等级可配置。

2. 编辑能力是一等主线
   - Patch API 负责已有 XLSX 的 part-level rewrite 和未知 part 保留。
   - In-memory API 负责小文件随机编辑体验。
   - 大型 worksheet 编辑通过事件流扫描、转换和重写完成。

3. OpenXML 结构清晰
   - OPC package、relationships、content types、workbook、worksheet、styles、
     sharedStrings 分层实现。

4. 功能对标 C++ `OpenXLSX`
   - 优先覆盖 `OpenXLSX` 高频功能。
   - 大数据写入性能和内存占用必须明显优于 `OpenXLSX` 的 DOM 主路径。

5. 参考但不照搬 `xlnt`
   - 吸收 event parser / serializer 和 producer / consumer 分层。
   - 避免让完整 workbook 内存模型统治大文件路径。

## 项目定位

FastXLSX 的当前定位是：

```text
一个 C++20 / MSVC 2026 优先的可编辑高性能 XLSX/OpenXML 引擎，
共享 OpenXML/OPC 底座，
同时提供 Streaming、Patch 和 In-memory 三条 API 路径。
```

更详细的说明已经并入少数主文档，避免重复维护：

- [架构与编辑边界](docs/ARCHITECTURE.md)
- [编辑模型](docs/EDITING_MODEL.md)
- [API 设计与文档注释](docs/API_DESIGN_AND_DOCUMENTATION.md)
- [任务计划](docs/TASK_PLAN.md)
- [任务拆分设计](docs/TASK_BREAKDOWN.md)
- [下一步推进](docs/NEXT_STEPS.md)
- [性能目标](docs/PERFORMANCE_TARGETS.md)
- [测试流程](docs/TESTING_WORKFLOW.md)
- [Patch 保留能力回归明细](docs/PATCH_PRESERVATION_COVERAGE.md)

## 构建与测试

项目主开发环境是 Visual Studio 2026 / MSVC 2026。推荐从 VS2026 Developer
Command Prompt 使用 preset：

```powershell
cmake --list-presets
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

`windows-nmake-release` preset 使用 `NMake Makefiles`，普通单元测试通过
CTest preset 和测试属性保持 60s 边界。当前手工 benchmark 通过
`FASTXLSX_BUILD_BENCHMARKS=ON` 和 `fastxlsx_bench_streaming_writer` 显式 opt-in，
不进入默认 CTest。

生成的 `.xlsx` 若出现结构异常，按 [测试流程](docs/TESTING_WORKFLOW.md)：
用 Excel、`openpyxl` 或 `XlsxWriter` 生成语义等价参考文件，拆包后对比
`[Content_Types].xml`、relationships、workbook、worksheet、shared strings 和
styles 等 XML。

## 示例

当前 `examples/` 是 opt-in 构建入口，不进入默认 CTest：

```powershell
cmake --preset windows-nmake-release -DFASTXLSX_BUILD_EXAMPLES=ON
cmake --build --preset windows-nmake-release --target fastxlsx_minimal_writer_example
cmake --build --preset windows-nmake-release --target fastxlsx_streaming_writer_example
```

- `examples/minimal_writer.cpp` 使用 `Workbook` / `Worksheet` / `Cell`，
  面向小型 new-workbook buffered creation。它演示 ASCII case-insensitive sheet lookup、
  `rename_worksheet()`、`remove_worksheet()`、`worksheet_count()`、
  `cell_count()` 和 `estimated_memory_usage()`；这些 diagnostics 是近似观测值，
  不是进程 RSS、硬内存预算或大数据导出进度。
- `examples/streaming_writer.cpp` 使用 `WorkbookWriter` / `WorksheetWriter` /
  `CellView`，面向大数据顺序导出和 worksheet metadata 写入，不保留完整
  worksheet cell matrix。

已有 XLSX 文件的 Patch / small-file In-memory 编辑请使用 `WorkbookEditor` /
`WorksheetEditor`，不要把 `Workbook` 示例当作 existing-file sheet rename/delete
或 relationship repair。

## 目录

```text
FastXLSX
├── docs
│   ├── API_DESIGN_AND_DOCUMENTATION.md
│   ├── ARCHITECTURE.md
│   ├── EDITING_MODEL.md
│   ├── NEXT_STEPS.md
│   ├── PATCH_PRESERVATION_COVERAGE.md
│   ├── PERFORMANCE_TARGETS.md
│   ├── TASK_BREAKDOWN.md
│   ├── TASK_PLAN.md
│   └── TESTING_WORKFLOW.md
├── include
│   └── fastxlsx
│       ├── detail
│       │   ├── cell_store.hpp
│       │   ├── opc.hpp
│       │   ├── worksheet_event_reader.hpp
│       │   ├── worksheet_transformer.hpp
│       │   └── xml.hpp
│       ├── cell_value.hpp
│       ├── document_properties.hpp
│       ├── fastxlsx.hpp
│       ├── image.hpp
│       ├── streaming_writer.hpp
│       ├── workbook.hpp
│       └── workbook_editor.hpp
├── src
│   ├── cell_store.cpp
│   ├── cell_value.cpp
│   ├── image.cpp
│   ├── opc.cpp
│   ├── package_editor.cpp
│   ├── package_reader.cpp
│   ├── package_writer.cpp
│   ├── streaming_writer.cpp
│   ├── workbook.cpp
│   ├── workbook_editor.cpp
│   ├── worksheet_event_reader.cpp
│   ├── worksheet_transformer.cpp
│   ├── xml.cpp
│   └── zip_store_writer.cpp
├── tests
│   ├── CMakeLists.txt
│   ├── test_image.cpp
│   ├── test_minimal_xlsx.cpp
│   ├── test_opc.cpp
│   ├── test_package_editor.cpp
│   ├── test_package_reader.cpp
│   ├── test_streaming_writer.cpp
│   ├── test_workbook_editor.cpp
│   ├── test_worksheet_event_reader.cpp
│   └── test_worksheet_transformer.cpp
└── CMakeLists.txt
```

## 推荐技术路线

- XML 大文件读写：SAX / event streaming。
- XML 小文件编辑：可选局部 DOM。
- ZIP/OPC：part 级别索引、复制、替换。
- 字符串：`inlineStr` 是默认低内存路径；`sharedStrings` 是显式体积/性能策略，
  当前仍在 hardening。
- 样式：独立 registry，统一去重。

## 规划依赖

这些依赖记录在 `vcpkg.json` 和 CMake feature 中，当前接入状态不同：

- ZIP/OPC 底层：默认仍使用内部 stored ZIP writer；`FASTXLSX_ENABLE_MINIZIP_NG=ON`
  时通过 vcpkg `planned-runtime` 接入 `minizip-ng[core,zlib]` 读写 DEFLATE entries。
- 压缩：当前 opt-in minizip-ng 路径使用 zlib backend；`zlib-ng` 仍是后续性能依赖方向。
- 大型 XML 流式读取：`Expat` 仍是后续 planned dependency。
- 小型 XML DOM 编辑：`pugixml` 仍是后续 planned dependency；当前已有小型 XML
  局部重写 helper，但没有默认接入 pugixml。
- Phase 5 图片读取/插入解码：`stb` 默认通过 vcpkg manifest 接入，用于
  PNG/JPEG `read_image_info()` 元数据 helper 和
  `WorksheetWriter::add_image()` 基础插入切片；仍不代表 existing-workbook
  图片保真、drawing 编辑或完整图片功能。
- 测试：`Catch2`。
- 性能基准：`Google Benchmark`。

`OpenXLSX` 和 `xlnt` 只作为参考库和 benchmark 对象，不作为 FastXLSX 的底层依赖。

## 当前状态

项目已越过单纯 Phase 1 bootstrap：最小可写 XLSX、新建 workbook 流式写入骨架、
多项 worksheet metadata、图片插入基础切片、内部 OPC/Patch 底座，以及首个
public `WorkbookEditor` Patch facade 都已经存在。当前仍不是完整 XLSX 引擎；
文档和 API 继续按 Streaming、Patch、In-memory 三条路径区分能力边界。

当前已具备：

- compiled `fastxlsx` CMake target 和 `FastXLSX::fastxlsx` alias。
- 保守 `vcpkg.json`、`CMakePresets.json` 和 Windows VS2026/NMake CI workflow 基础。
- 新建小工作簿 public API：`Workbook`、`Worksheet`、`Cell`、`CellRange`、
  `RowOptions`、`DocumentProperties`、`Workbook::add_worksheet()`、
  `worksheet_count()`、`worksheet_names()`、`has_worksheet()`、`worksheet()`、
  `try_worksheet()`、`rename_worksheet()`、`remove_worksheet()`、
  `Workbook::cell_count()`、`Workbook::estimated_memory_usage()`、
  `Worksheet::cell_count()`、`Worksheet::estimated_memory_usage()` 和
  `FastXlsxError`。这些 size diagnostics 是小型 buffered creation path 的近似观测值，
  不是进程 RSS、硬内存预算或 large-export progress API；sheet lookup、
  rename old-name lookup 和 remove lookup 与 duplicate-name 规则一致，都是
  ASCII case-insensitive。
- 流式 writer public API：`WorkbookWriter`、`WorksheetWriter`、`CellView`、
  `WorkbookWriterOptions`、`StringStrategy`、`StyleId`、`CellStyle` 及当前窄
  styles / worksheet metadata / image insertion 值类型。
- Patch public API 首片：`WorkbookEditorOptions`、`WorkbookEditor::open()`、
  `WorkbookEditor::open(path, options)`、`worksheet_names()`、`has_worksheet()`、
  `source_worksheet_names()`、`has_source_worksheet()`、`has_pending_changes()`、
  `pending_change_count()`、
  `pending_replacement_cell_count()`、`pending_replacement_worksheet_names()`、
  `pending_materialized_worksheet_names()`、
  `pending_materialized_cell_count()`、
  `estimated_pending_materialized_memory_usage()`、
  `has_pending_replacement()`、`estimated_pending_replacement_memory_usage()`、
  `last_edit_error()`、
  `WorkbookEditorWorksheetCatalogEntry`、`worksheet_catalog()`、
  `WorkbookEditorWorksheetEditSummary`、`pending_worksheet_edits()`、
  `replace_sheet_data()`、`rename_sheet()` 和 `save_as()`。
  Patch path 只做已有 workbook 的 whole-`<sheetData>` 替换和窄 sheet catalog 改名；
  不是语义 rename 或 public `PackageEditor`。
- In-memory existing-workbook public API 首片：`WorksheetEditorOptions`、
  `WorkbookEditor::worksheet()`、`WorkbookEditor::try_worksheet()`、
  `WorksheetEditor`、`WorksheetEditor::name()`、`try_cell()`、`get_cell()`、
  `set_cell()`、`erase_cell()`、row/column coordinate guardrails、这些 cell API 的
  strict uppercase A1 string overload、`WorksheetCellReference`、`WorksheetCellSnapshot`、
  `has_pending_changes()`、`sparse_cells()`、`sparse_cells(CellRange)`、
  `cell_count()` 和 `estimated_memory_usage()`。它是小文件随机 cell 编辑路径，dirty session 由
  `WorkbookEditor::save_as()` 自动 flush；caller-supplied default `StyleId{0}`
  会归一化为 no style handle，workbook-backed source `t="s"` shared string cells
  可只读 materialize 为 plain text，且 prefixed source sharedStrings `sst` / `si` /
  `t` / `r` element names 按 local-name 匹配；prefixed source worksheet /
  `sheetData` / row / cell / inlineStr wrapper element names 也按 local-name
  匹配，namespace URI 不参与判断；`WorksheetEditorOptions::max_cells` 和
  `memory_budget_bytes` 会约束 source materialization 与后续 sparse-store
  mutations，且 source-load / mutation guardrail failure paths 均保持
  no-state-pollution hygiene；`erase_cell()` 会移除 active sparse record，并为后续
  insertions 释放这些 sparse-store guardrail budgets；missing-cell `erase_cell()`
  保持 clean no-op，并会清除先前 public mutation diagnostic；explicit blank
  insertion 作为 active sparse record 也受这些 guardrail 约束；不支持 namespace validation/repair、non-default `StyleId`、
  sharedStrings writeback/rebuild/migration、style migration、semantic metadata
  sync、relationship repair 或 large-file low-memory random editing。
- 公共值和 helper：`CellValue` / `CellValueKind`、PNG/JPEG `ImageInfo` /
  `ImagePixels`、`read_image_info()` 和 `read_image_pixels()`。
- 最小 OpenXML package 输出：
  `[Content_Types].xml`、`_rels/.rels`、`xl/workbook.xml`、
  `xl/_rels/workbook.xml.rels`、`xl/worksheets/sheet1.xml`、
  `docProps/core.xml` 和 `docProps/app.xml`。
- 数字、inline string、布尔和公式单元格写入。
- `StringStrategy::SharedString` 基础写出路径、`xl/sharedStrings.xml` package wiring
  和结构测试；它是显式策略，内存随唯一字符串表增长。
- 基础 `docProps/core.xml` 和 `docProps/app.xml` 配置；不生成
  `docProps/custom.xml`，也不是完整 document properties editor。
- 流式 worksheet 写入能力：行高、列宽、冻结窗格、自动筛选、合并单元格、公式
  full recalculation request、data validations、external/internal hyperlinks、
  streaming-only tables、two-/three-color color scales、basic data bars、basic
  `3Arrows` icon sets、基础 styles 和 PNG/JPEG 图片插入。
- 内部 OPC manifest / relationships / `PartIndex` / `RelationshipGraph`、ZIP
  `PackageReader`、`PackageEditor`、`EditPlan`、`DependencyAnalyzer`、
  `ReferencePolicy` 和 worksheet event/transformer 基础；这些仍主要是 internal
  Patch 底座，不是稳定 public package API。
- 内部 `CellStore` / `CellRecord` sparse store、worksheet-local guardrail 首片、
  standalone `<sheetData>` / full worksheet emission 和 by-name `PackageEditor`
  handoff 回归；其首个 public 入口是 `WorkbookEditor::worksheet()` / `WorksheetEditor`。
- 内部 package writer boundary：新建 workbook 输出通过 `src/package_writer.*`
  进入 ZIP backend。默认构建使用 stored/no-compression bootstrap；
  `FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建使用 minizip-ng DEFLATE backend。
  internal `PackageWriterOptions::compression_level` 可选择 `-1` backend default、
  `0` no-compression/stored output 或 `1..9` minizip DEFLATE level；
  stored bootstrap 仍不压缩。内部 writer 会在写输出前拒绝需要 Zip64 的
  entry count 或单 entry 未压缩大小，以及超过 ZIP 16-bit 字段的 entry name。
- CTest 测试 `fastxlsx.unit`，覆盖 XML escape、cell reference、OpenXML 结构和
  基础单元格编码。
- CTest 测试 `fastxlsx.streaming`，覆盖当前流式 writer 写入骨架。
- CTest 测试 `fastxlsx.opc`，覆盖内部 OPC manifest、content types、
  relationships 和 XML serializer 基础。
- CTest 测试 `fastxlsx.package_reader`、`fastxlsx.workbook_editor`、
  `fastxlsx.image`、`fastxlsx.worksheet_event_reader` 和
  `fastxlsx.worksheet_transformer`，以及按对象/预算拆分的
  `fastxlsx.package_editor.*` shards，覆盖当前内部 Patch / public Patch facade /
  image helper / worksheet scan-transform 基础切片。
- 本机已做 Excel 可视化验证并核对生成样例的关键单元格。

## WorkbookEditor Patch facade 示例

当前 `WorkbookEditor` 的默认 worksheet inspection 面向 current planned catalog：
成功 `rename_sheet()` 后，`worksheet_names()` / `has_worksheet()` 会看到将要写入
`save_as()` 输出的名称；`source_worksheet_names()` / `has_source_worksheet()` 保留
打开时的 source workbook 视图。`replace_sheet_data()` 后再 `rename_sheet()` 会把
pending replacement diagnostics 迁移到 planned 新名称；如果之后再改回 source 名称，
diagnostics 也会迁回 source 名称，public summaries 不再标记 renamed。`save_as()`
被输出路径 guard 拒绝时，不会清空 queued replacement / rename，也不会覆盖
`last_edit_error()`；调用方
可以换一个安全输出路径后重试。成功 `save_as()` 也不是 commit / close 操作；
pending diagnostics 仍可见，同一个 planned state 可以再次另存或继续编辑。
`pending_replacement_worksheet_names()` 按 current planned catalog order 返回 planned
names；`pending_worksheet_edits()` / `worksheet_catalog()` 保持 source workbook
sheet-catalog order。`pending_worksheet_edits()` 是 current planned-state
summary：rename-only 链如果最终回到 source name，不会留下 summary；粗粒度
`pending_change_count()` 仍会统计这些成功 public edit calls。public inspection 和 pending diagnostic methods 不会清空、替换或
创建 `last_edit_error()`；后续失败 public edit 会替换旧诊断，后续成功 public
edit 会清空它；这个规则跨 `replace_sheet_data()`、`rename_sheet()` 和
`WorksheetEditor` mutation 共用同一个 latest-error 槽。没有 queued public edits 时，no-op `save_as()` 仍是 reader-backed
roundtrip copy；如果之前失败的是 `replace_sheet_data()` 或 `rename_sheet()`，
该失败诊断也会保留，直到后续失败或成功 public edit 分别替换或清空它。
`WorkbookEditor` move construction / move assignment 只是 ownership transfer：
move assignment 会用 source editor state 替换 target state，不合并 queued edits；
如果 source 已经 moved-from，target 也会变为 moved-from / not open。

```cpp
#include <fastxlsx/workbook_editor.hpp>

auto editor = fastxlsx::WorkbookEditor::open("template.xlsx");

const auto source_names = editor.source_worksheet_names();
const bool source_has_data = editor.has_source_worksheet("Data");

editor.replace_sheet_data("Data", {
    {fastxlsx::CellValue::text("Name"), fastxlsx::CellValue::text("Score")},
    {fastxlsx::CellValue::text("Alice"), fastxlsx::CellValue::number(98.0)},
});

editor.rename_sheet("Data", "Report");

const bool output_has_report = editor.has_worksheet("Report");
const auto source_to_planned_catalog = editor.worksheet_catalog();
const auto pending_replacements = editor.pending_replacement_worksheet_names();
const auto pending_edit_summaries = editor.pending_worksheet_edits();
const auto pending_cells = editor.pending_replacement_cell_count();
const auto last_edit_error = editor.last_edit_error();

editor.replace_sheet_data("Report", {
    {fastxlsx::CellValue::text("Name"), fastxlsx::CellValue::text("Score"),
        fastxlsx::CellValue::text("Comment")},
    {}, // Advances row mapping; it does not emit an explicit empty row.
    {fastxlsx::CellValue::text("Bob"), fastxlsx::CellValue::number(87.0),
        fastxlsx::CellValue::blank()}, // Emits an explicit empty cell.
});

editor.save_as("patched.xlsx");
```

这条路径只替换已有 worksheet 的 whole-`<sheetData>` 并保存到新文件；它不是
small-file random cell editing、sheet rename/remove、sharedStrings/style migration 或 public
`PackageEditor`。小文件随机 cell 编辑请显式使用 `WorkbookEditor::worksheet()` /
`WorksheetEditor`。`CellValue::blank()` 是显式 replacement cell，会写 empty cell；
空 row vector 只推进输入行号，不表示 tombstone / erase。
如果没有 queued public edits，`WorkbookEditor::save_as()` 只是写一个 reader-backed
roundtrip copy，用于另存为已有 workbook；它仍不是原地 atomic save、transaction
commit 或 undo/redo history。

## WorksheetEditor small-file In-memory 示例

`WorksheetEditor` 是 `WorkbookEditor` 下的 borrowed handle，用于显式 materialize
一个已有 worksheet 的小文件随机 cell edits。dirty edits 通过
`WorkbookEditor::save_as()` 自动 flush 到 Patch plan。这个路径不会迁移
sharedStrings/style ids，不修复 relationships，也不是大 worksheet 低内存 random access。
当前 source materialization 边界与
`docs/API_DESIGN_AND_DOCUMENTATION.md` 的 "Source dependency materialization
summary" 保持一致：

- 支持 source blank、number、boolean、`t="str"` scalar text、formula text
  （包含 `t="str"` formula cells 且丢弃 stale cached values）、
  plain inline string、simple inline rich text flatten，以及 workbook-backed
  `t="s"` shared string indexes。
- sharedStrings 只做 read-only import；dirty projection 继续写 inline strings，
  并保留 source `xl/sharedStrings.xml`，不 rebuild / writeback / migrate。
- source sharedStrings 和 inline rich text 中 `rPh` / `phoneticPr` / `extLst`
  下的 ignored metadata text 不进入 materialized text；self-closing ignored
  metadata 是合法空 metadata；nested `<si>` decoy、ignored text wrapper 里的
  nested markup（包括 comment / processing instruction / CDATA）、
  orphan closing tag 和未闭合 ignored metadata 都 fail fast。
- 支持的 worksheet / inlineStr / formula / value-wrapper / sharedStrings /
  rich-run element names 按 local-name 匹配；namespace URI 不验证。unsupported
  local-name 仍 fail fast。
- unsupported source cell type tokens 仍 fail fast，例如 error `t="e"`、
  date-like `t="d"` 和 custom/unknown `t="z"`；这些不会被导入成普通值。
- cell-internal comments、processing instructions、XML declaration tokens、
  CDATA / DOCTYPE-like unsupported markup、worksheet 根下 metadata / `sheetData`
  外的非空 raw text、`sheetData` 内 row 外或 row 内 cell 外的非空 raw
  text，以及 `<v>` / `<t>` / `<f>` wrapper 外的非空 cell raw text 仍 fail
  fast；cell-external comment / PI 可以在只读 materialization 时被忽略，
  dirty projection 不保留这类 XML trivia。
- 非目标保持不变：不做 rich-text preservation、style migration、
  relationship repair/pruning、XML repair、namespace repair、semantic metadata
  sync 或 large-file low-memory random editing。

workbook-backed source `t="s"` cells 会通过现有 `xl/sharedStrings.xml` 只读解析成
plain `CellValue::text(...)`；dirty `save_as()` 仍把 materialized text 写成 inline
strings，并保留 source sharedStrings part，而不是重建或回写 string table。
prefixed source sharedStrings element names (`sst` / `si` / `t` / `r`) 会按
local-name 参与该只读 materialization；这不是 namespace URI validation、namespace
repair 或 schema validation。unsupported sharedStrings item/rich-run local-name
仍会 fail fast，即使对应元素绑定到非 spreadsheetml URI。sharedStrings 中
direct `<t>` 与 rich `<r>` 混用、`rPr` 位于 rich run 外、`rPr` 内出现 text wrapper
也会 fail fast。`rPh` / `phoneticPr` / `extLst` 内的 opaque nested markup 文本仍被忽略，
self-closing ignored metadata 也按空 metadata 处理；但 nested `<si>` decoy、
`<t>` 内再嵌 markup（包括 comment / processing instruction / CDATA）、
orphan closing tag 或未闭合 ignored metadata 仍按 malformed source fail fast。
sharedStrings 中的 CDATA / DOCTYPE-like markup declaration 不作为 text import
能力处理；当前窄 loader 会 fail fast，避免静默丢文本。`<?xml ...?>`
只允许作为 source sharedStrings payload 开头的 XML declaration；出现在
sharedStrings root 开始后不是普通 processing instruction，会 fail fast；重复
XML declaration 也会 fail fast。XML declaration 必须声明 `version="1.0"` 或
`version="1.1"`，并只接受 `version`、可选 `encoding`、可选 `standalone`
这一窄 metadata 顺序；version-only declaration、单引号属性和合法
`encoding` -> `standalone` metadata 形式保持可读，但不会触发字符集转码；
缺失或不支持的 version、空/非法 encoding token、空/非法 standalone token、
重复/未知属性、`encoding` 出现在 `standalone` 之后都会 fail fast。XML
declaration 不能出现在前导空白文本、comment / ordinary processing instruction
这类 prolog trivia 之后；但 XML declaration 后的 ordinary processing
instruction 仍作为 trivia 忽略，不 materialize。大小写变体的 XML-like
processing instruction target（例如 `<?XML ...?>` / `<?Xml ...?>`）按 reserved
target 失败，不作为 ordinary PI trivia 跳过；`<?xml-stylesheet ...?>` 仍按普通
PI trivia 忽略，不导入或解析 stylesheet；普通 processing instruction 仍必须以
`?>` 结束且带非空 target，target 不能以明显非法的 ASCII name 起始字符
（例如 `-` 或 digit）开头，合法 ASCII name-start（letter/`_`/`:`）例如
`<?_fastxlsx legal-start?>` / `<?:fastxlsx legal-colon-start?>` 仍是 ignored
trivia；target 的 ASCII 后续字符只接受明显合法的 XML name 字符
（letter/digit/`_`/`:`/`-`/`.`），例如 `<?fastxlsx.data-1:probe legal-target?>`
仍是 ignored trivia；target 后必须是空白或立即 `?>`，`<?fastxlsx?>` 这种
空 data PI 仍是 ignored trivia；缺失终止符、空 target、非法起始/后续字符或缺失
target/data 分隔符的 malformed PI 会 fail fast。
prefixed source worksheet / `sheetData` / row / cell / inlineStr wrapper
element names 也会按 local-name 读取；dirty projection 仍使用 standalone worksheet
XML，不保留 source prefixes，也不做 namespace repair。该 local-name 路径不检查
元素 namespace URI；即使 source 元素绑定到非 spreadsheetml URI，只要 local-name
匹配当前窄支持形状，也按同一边界 materialize。
但 unsupported local-name 仍会 fail fast；namespace URI 被忽略不代表 XML repair、
metadata import 或 malformed-package tolerance。
source inline rich text runs 也会只读 flatten 成普通 text；rich formatting 不保留，
inline phonetic / extension metadata text 会被忽略；这些 ignored metadata 里的
opaque nested markup 文本也不会进入 materialized text 或 dirty projection，self-closing
ignored metadata 也按空 metadata 处理；但 nested `<si>` decoy、`<t>` 内嵌 markup、
orphan closing tag 或未闭合 ignored metadata 仍会 fail fast。malformed inline rich metadata
（例如 direct/rich text 混用、`rPr` 位于 run 外、`rPr` 内出现 value wrapper，
或 rich/ignored metadata 未闭合）仍会 fail fast，不做 XML repair。
source wrapper metadata 不是 materialized sparse store 的一部分：普通 wrapper
metadata、range/reference wrapper metadata（例如 mergeCells / dataValidations /
conditionalFormatting / ignoredErrors / pageMargins / pageSetup）以及
relationship-bearing wrapper metadata（例如 worksheet hyperlinks / tableParts）不会
阻止 supported cells 读取；dirty projection 会丢弃这些 worksheet XML wrapper 引用，
但保留 source worksheet `.rels` / linked table parts 这类 package artifacts，不做
range recalculation、relationship pruning、repair 或语义同步。
`WorksheetEditor::set_cell()` 接受 caller-supplied `StyleId{0}`，但会把它归一化为
no style handle；dirty output 不写 `s="0"`。非默认 style ids 仍会被拒绝，
row/column 和 A1 overload 的失败都不会 mutate sparse store、dirty materialized
session 或 queue pending edit。
读取 source worksheet 时，显式默认 `s` 属性值精确为 `0`（例如 `s="0"`、
`s='0'` 或 `s = "0"`）也会归一化为 no style handle；非默认 source style ids
仍不导入、不迁移、不合并。这个 source 归一化不做数值宽松解析：空值、缺失值、
未加引号、未闭合引号、带符号、前导零、前后空白、entity 编码或重复的
default-like style attribute 仍会失败；`x:s` 这类 qualified style-like
attribute 也会作为 unsupported cell metadata 失败。
移动或 move-assign owning `WorkbookEditor` 后，之前取得的 `WorksheetEditor`
handle 不会自动跟随新 owner；除 `name()` 这个本地 planned-name label 外，
继续读写/检查 session 会抛 `FastXlsxError`，调用方必须从 moved-to / assigned-to
editor 重新 `worksheet()` 或 `try_worksheet()`。`save_as()` 不属于这个
invalidation 边界：成功或失败的 `save_as()` 都不会删除或失效同一个 owner 下已有的
`WorksheetEditor` handle，调用方可以继续用同一 handle 做后续小文件编辑并在下一次
`save_as()` reflush；旧输出文件不会被后续编辑反向修改。

```cpp
#include <fastxlsx/workbook_editor.hpp>

auto editor = fastxlsx::WorkbookEditor::open("template.xlsx");

auto maybe_sheet = editor.try_worksheet("Data", fastxlsx::WorksheetEditorOptions{
    .max_cells = 10000,
    .memory_budget_bytes = 8 * 1024 * 1024,
});
if (!maybe_sheet) {
    return;
}

auto& sheet = *maybe_sheet;
auto maybe_a1 = sheet.try_cell(1, 1);
auto required_a1 = sheet.get_cell(1, 1); // Throws if the sparse cell is missing.
sheet.set_cell(1, 1, fastxlsx::CellValue::text("updated"));
sheet.set_cell(1, 2, fastxlsx::CellValue::text("default style")
    .with_style(fastxlsx::StyleId{})); // Normalized to no style handle.
sheet.set_cell("D4", fastxlsx::CellValue::text("strict A1 ref"));
const auto cells = sheet.sparse_cells(); // Owning row-major sparse snapshot.
const auto visible_cells = sheet.sparse_cells(fastxlsx::CellRange{1, 1, 10, 5});
sheet.erase_cell(2, 1);
const bool sheet_dirty = sheet.has_pending_changes();
const auto dirty_materialized_sheets = editor.pending_materialized_worksheet_names();
const auto dirty_materialized_cells = editor.pending_materialized_cell_count();
const auto dirty_materialized_memory =
    editor.estimated_pending_materialized_memory_usage();
const auto pending_summaries = editor.pending_worksheet_edits();

editor.save_as("edited.xlsx");
```

`WorksheetEditor` 的 A1 overload 只接受单个 uppercase cell reference，例如
`A1` 或 `XFD1048576`；`a1`、`A1:B2`、`A0`、`A01` 和超出 Excel
行列上限的引用会被拒绝。row/column overload 同样要求 1-based Excel 坐标：
invalid read throws but does not update `last_edit_error()`，invalid
`set_cell()` / `erase_cell()` throws、updates `last_edit_error()`，并且不会 dirty 或
mutate sparse store；连续失败 mutation 只保留最新 `last_edit_error()`，后续成功
mutation 会清空它；最后一个合法坐标 `(1048576, 16384)` 仍是有效输入。
`sparse_cells()` 返回当前 materialized sparse store 的 owning row-major snapshot，
包含 explicit blank records；`sparse_cells(CellRange)` 返回 1-based inclusive
range 内已经存在的 active sparse records，不补齐 missing cells。两者都不暴露内部
iterator/lifetime，不是 dense range read 或 streaming sparse iterator，也不会同步
worksheet metadata。
`WorksheetEditor::has_pending_changes()` 只检查该 borrowed handle 对应的
materialized session 是否 dirty；它不触发 flush、不增加
`WorkbookEditor::pending_change_count()`，也不更新 `last_edit_error()`。
`WorkbookEditor::pending_materialized_worksheet_names()` 返回当前 dirty
materialized sessions 的 planned sheet names，按 planned catalog order 排列；
它同样不触发 flush、不增加 pending change count，也不更新
`last_edit_error()`。`WorkbookEditor::pending_materialized_cell_count()` 和
`estimated_pending_materialized_memory_usage()` 是同一 dirty materialized session
集合的 workbook-level 聚合诊断：只统计 dirty sessions，不统计 clean
materialized sessions 或 queued whole-`<sheetData>` replacement payloads，不触发
flush、不增加 `pending_change_count()`、不暴露 internal `EditPlan`，也不更新
`last_edit_error()`。`WorkbookEditor::pending_worksheet_edits()` 也会把 dirty
materialized sessions 合并进同一组 source-order summary，设置
`materialized_dirty`、`materialized_cell_count` 和
`estimated_materialized_memory_usage`；clean materialized sessions 不返回，
successful `save_as()` 自动 flush 后 dirty materialized summary 会消失，除非同一
worksheet 仍有 rename / whole-`<sheetData>` replacement summary。

当前仍未完成：

- 将 minizip-ng backend 设为默认前的 CI/cache/release packaging 验证。
- Zip64、公开压缩等级配置和真正 package streaming。
- Catch2 和 Google Benchmark 接入。
- CI workflow 和 example 入口已有基础文件/分支，但仍需 GitHub 侧验证、完善和发布面确认。
- 完整 Phase 3 写入特性、完整 Phase 5 对象编辑能力和系统化性能 benchmark。
- workbook-level in-memory guardrails、non-default style migration、sharedStrings
  migration、完整 semantic metadata sync、relationship repair 和 large-file
  low-memory random editing。
  当前 public wording gate 也保持这个边界：`WorkbookEditor` 已承载 Patch facade
  和小文件 In-memory `WorksheetEditor` 首批接口，但 existing-file styles、
  sharedStrings、relationships、tables、drawings 和 calcChain 仍只做 preserve /
  audit / fail 的窄策略，不做语义迁移或修复。当前 internal evidence 已补到
  source-loaded `CellStore` 在 queued sheet rename 后必须按 planned
  catalog name handoff；whole-`<sheetData>` handoff 和 full worksheet chunk projection
  都已有对应回归，后者还验证 refreshed `<dimension>`；old source name 失败也已覆盖
  不消费 prepared chunk source，planned name 可重试；queued whole worksheet replacement
  之后再用 source-loaded `CellStore` patch `<sheetData>` 也已覆盖，证明 follow-up
  handoff 使用 planned worksheet wrapper 而不复活 source-only payload；full worksheet
  `CellStore` projection 在 queued worksheet replacement 之后也已覆盖，证明后续完整
  worksheet handoff 会替换 prior planned wrapper 并刷新 `<dimension>`；queued rename
  与 queued worksheet replacement 组合后，source-loaded `CellStore` `<sheetData>`
  handoff 也必须按 planned sheet name 执行，old source name 失败不消费 chunk source。
  同一组合下的 full worksheet `CellStore` projection 也已覆盖，证明 planned-name
  路径会替换 prior queued wrapper、保留 renamed catalog 并刷新 `<dimension>`。
  该 staged state 还覆盖 exact 和 path-equivalent source-overwrite `save_as()`
  失败卫生，并覆盖 empty output path / missing parent / non-directory parent /
  existing-directory output guards：拒绝这些非法输出路径后仍保留
  staged chunks / pending edits；source-copy temp failure 和 writer/backend failure
  也不会覆盖既有输出或丢失 staged state，随后安全输出路径仍可成功保存；成功
  `save_as()` 后 staged chunks 仍可复用到第二个安全输出路径。
  这些仍是 internal `PackageEditor` / `CellStore` 回归，不是 public worksheet handle
  或 random cell editing。
- 大文件低内存 worksheet event reader / transformer / stream rewrite 的完整 public
  编辑路径；当前 bounded `sheetData` patch 已通过 source entry / planned staged chunks
  的 chunk-source 读取 worksheet input，并把 rewritten output 写成 file-backed staged
  chunks，replacement `<sheetData>` caller chunks 会在 rewritten output pass 中直接
  消费而不是先单独 staging/replay，但仍受 bounded local rewrite 限制，不是完整大文件
  transformer。
- sharedStrings 索引迁移、style id 迁移或 styles merge、relationship repair/pruning、
  table/drawing/chart/defined-name 语义同步、calcChain rebuild 和公式求值。
- existing-workbook 图片、VBA、table、chart、pivot、comments 等复杂对象的语义编辑。

`src/package_writer.*` 是当前内部 package writer 边界。默认构建通过 vcpkg 拉取
`stb` 图片依赖，但 ZIP 后端仍调用 `src/zip_store_writer.*` Phase 1 bootstrap；
opt-in minizip 构建会写出 DEFLATE-compressed ZIP entries，并且内部 writer option
可选择 backend default、`0` no-compression/stored output 或 `1..9` DEFLATE
压缩等级。内部 writer 现在有 no-Zip64 写前 guardrail，但它仍不是 public package
editing API，不要据此宣称 Zip64、真正 package streaming、已有文件编辑或大文件性能。

## 许可证

FastXLSX 使用 MIT License，见 [LICENSE](LICENSE)。

## 支持项目

FastXLSX 长期免费开源，但持续开发高性能 XLSX 引擎需要大量测试、
兼容性验证、性能对比和文档维护。

如果这个项目帮你节省了时间，欢迎通过爱发电或赞赏码支持我继续维护：

[支持无相孤君继续开源](https://ifdian.net/a/wuxianggujun)

| 微信赞赏码 | 支付宝收款码 |
| :---: | :---: |
| <img src="docs/assets/donation/weixin.png" alt="微信赞赏码" width="220"> | <img src="docs/assets/donation/zhifubao.jpg" alt="支付宝收款码" width="220"> |

你的支持会优先用于：

- 完善 XLSX 写入、读取和编辑能力。
- 补充 Excel、OpenXML、openpyxl、XlsxWriter 等兼容性验证。
- 建立更完整的 benchmark 和真实大文件测试集。
- 维护中文文档、示例和发布版本。
