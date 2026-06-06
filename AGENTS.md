# FastXLSX Agent Guide

## 项目快照

FastXLSX 是一个 C++20 / MSVC 2026 优先的 XLSX 引擎。项目文档固定的
核心方向是：流式优先、局部 DOM 可选、面向 OpenXML。

当前仓库处于 Phase 1 最小可写 XLSX 的早期实现阶段：

- `CMakeLists.txt` 定义了 compiled `fastxlsx` library。
- 目标别名是 `FastXLSX::fastxlsx`。
- `include/fastxlsx/fastxlsx.hpp`、`include/fastxlsx/workbook.hpp`、
  `include/fastxlsx/streaming_writer.hpp`、`include/fastxlsx/document_properties.hpp`
  和 `include/fastxlsx/image.hpp` 提供当前 public API：
  `Workbook`、`Worksheet`、`Cell`、`DocumentProperties`、`WorkbookWriter`、
  `WorksheetWriter`、`CellView`、`DataValidationRule`、`DataValidationType`、
  `DataValidationOperator`、`ImageFormat`、`ImageInfo`、`read_image_info()`、
  `Workbook::set_document_properties()`、`WorkbookWriterOptions::document_properties`、
  `WorksheetWriter::add_image()` 和 `FastXlsxError`。
- `src/image.cpp`、`src/opc.cpp`、`src/package_writer.cpp`、`src/workbook.cpp`、
  `src/streaming_writer.cpp`、`src/xml.cpp`、`src/zip_store_writer.cpp`
  是当前已接入 CMake 的实现入口。
- `include/fastxlsx/detail/opc.hpp` 和 `src/opc.cpp` 是内部 OPC
  manifest / relationships / XML serializer 基础，不代表完整已有 XLSX 编辑能力。
- `tests/test_minimal_xlsx.cpp` 通过 CTest 注册为 `fastxlsx.unit`，覆盖 XML escape、
  cell reference、最小 OpenXML package 结构和基础单元格编码。
- `tests/test_streaming_writer.cpp` 通过 CTest 注册为 `fastxlsx.streaming`，覆盖
  流式 writer 骨架、公式、行高、列宽、冻结窗格、自动筛选、合并单元格和
  data validations / external hyperlinks / tables / opt-in image drawing XML 输出。
- `tests/test_opc.cpp` 通过 CTest 注册为 `fastxlsx.opc`，覆盖内部 OPC part name、
  content types、relationships、manifest 和 serializer 基础。
- `tests/test_image.cpp` 通过 CTest 注册为 `fastxlsx.image`，覆盖默认无 `stb`
  构建下的明确错误，以及 opt-in `FASTXLSX_ENABLE_STB=ON` 构建下 PNG
  文件/内存尺寸和通道读取。
- 当前可见文件集中已有 `vcpkg.json`、`CMakePresets.json` 和
  `.github/workflows/ci.yml` 作为基础工程入口。默认构建仍不拉第三方依赖；
  `FASTXLSX_ENABLE_MINIZIP_NG=ON` 会通过 vcpkg `planned-runtime` 接入
  `minizip-ng[core,zlib]` 并链接 `MINIZIP::minizip-ng`。
- `CMakeLists.txt` 已有 `FASTXLSX_BUILD_EXAMPLES` 分支，当前工作树中可见
  `examples/` 目录和示例源码；除非本轮任务验证该路径，否则不要把 example
  目标当作已完成发布面。

当前 P17a 图片元数据切片已存在：`FASTXLSX_ENABLE_STB=ON` 通过
`planned-image` 使用 `find_package(Stb REQUIRED)` 和 `${Stb_INCLUDE_DIR}`，
并定义 `FASTXLSX_HAS_STB`。`read_image_info()` 只读取 PNG/JPEG 的尺寸、格式
和通道数；它不生成 media part、drawing XML、relationships、content types、
anchor，也不代表图片插入、图片编辑或 existing-workbook 图片保真。

当前 P17b 图片插入基础切片也已存在：`WorksheetWriter::add_image(path, anchor)`
是 streaming-only new-workbook API，只在 `FASTXLSX_ENABLE_STB=ON` 时接受
PNG/JPEG 文件。它用 `read_image_info()` 验证格式/尺寸/通道，把原始图片字节复制到
临时 file-backed media entry，并在 `close()` 写出 `xl/media/image*.png|jpg`、
`xl/drawings/drawing*.xml`、drawing `.rels`、worksheet `.rels`、worksheet
`<drawing>` 和 content type entries。它不裁剪、不旋转、不压缩、不格式转换、不编辑
已有 drawing，也不代表 existing-workbook 图片保真或完整 drawing 支持。

不要把文档里的设计名当成已实现符号。`WorkbookWriter` / `WorksheetWriter` /
`CellView` 流式写入骨架已存在；公式、行高、列宽、冻结窗格、自动筛选、
合并单元格、data validations、external hyperlinks 和 streaming-only tables
是当前写入骨架能力，不等同完整 Phase 3 或完整 Phase 5。当前可见
`StringStrategy::SharedString`、内部 `SharedStringTable` 和
`xl/sharedStrings.xml` 结构测试，状态只能写为 sharedStrings 进行中或基础，
不能写成生产策略完成。当前有基础可配置 `docProps/core.xml` /
`docProps/app.xml` public metadata API：`DocumentProperties` 可设置 creator、
lastModifiedBy、title、subject、description、keywords、category、Application 和
AppVersion，并被 `Workbook::save()` / `WorkbookWriter::close()` 写入新建
workbook package；它不生成 `docProps/custom.xml`，不代表完整 document properties
API，也不是 existing-file editing。`PackageReader`、`CellEncoder` 等名称仍主要
来自架构文档或路线图；只有在 `include/` 或 `src/` 中找到对应实现后，才能当作
真实 API。OPC 当前有 `PartWriteMode` 和 package part edit-state 基础，
当前新建 workbook 写出路径有内部 `PackageEntry` / `write_package` 边界。
worksheet part finalization 已支持 file-backed/chunked package entry，避免在
`WorkbookWriter::close()` 阶段重新物化完整 worksheet XML 字符串。默认无依赖构建仍由
stored ZIP bootstrap 支撑；启用 `FASTXLSX_ENABLE_MINIZIP_NG` 时会使用
minizip-ng DEFLATE backend。它仍不是已有文件编辑用 public `PackageWriter`，
也不代表 true package streaming、Zip64、完整低内存或已验证大文件性能。
`PackageReader`、已有文件编辑和 unknown part preservation 仍是计划。当前已有内部
`PartIndex`、`RelationshipGraph` 和 `ContentTypeRegistry` 基础，但这只代表
owner-aware relationship / content type groundwork；不要宣称 package editing、
hyperlink、图片、VBA 或 table 支持。

当前数值 XML 写入是 finite-only 边界：`Workbook::save()` 会在序列化时拒绝
in-memory numeric cell 和 row height 中的 `NaN` / `+Inf` / `-Inf`；
`WorksheetWriter::append_row()` 会在写入前拒绝 streaming numeric cell 和 row height
中的非有限值；`WorksheetWriter::set_column_width()` 要求 width 为正且有限。不要让
OpenXML worksheet XML 写出 `nan`、`inf` 或 `-inf` 数字文本。

## 本轮推进计划同步

- sharedStrings：进行中。当前可见 API 选项、内部表、package wiring 和结构测试；
  仍需默认 CTest、Excel 可视化验证、Excel / `openpyxl` / `XlsxWriter`
  参考文件拆包 XML 对比，以及大小/内存数据后，才能扩大支持表述。
- vcpkg / CMakePresets / CI：基础。默认 preset 仍是无 vcpkg 的 VS2026/NMake
  路径；`windows-nmake-release-minizip` 是 opt-in vcpkg 验证路径，会启用
  `FASTXLSX_ENABLE_MINIZIP_NG` 和 `planned-runtime`。
- package writer boundary：基础。`src/package_writer.*` 已把新建 workbook
  输出从 workbook writer 代码中隔离出来，并支持 memory/file chunk entry source。
  `WorkbookWriter` 的 worksheet part 以 header + file-backed body + footer 写入内部
  package writer；sharedStrings XML 也可通过临时 file-backed entry 写入，但 shared string
  table 本身仍保留唯一字符串状态。默认 backend 是 `src/zip_store_writer.*` 的
  stored/no-compression bootstrap，opt-in backend 是 `minizip-ng[core,zlib]` DEFLATE。
  不要据此宣称 Zip64、true package streaming、existing-file editing、完整低内存或
  大文件性能。
- document properties：基础。`DocumentProperties`、`Workbook::set_document_properties()`
  和 `WorkbookWriterOptions::document_properties` 已支持新建 workbook 的基础
  `docProps/core.xml` / `docProps/app.xml` 配置；当前只覆盖 core/app 小型 XML
  part，不创建 `docProps/custom.xml`，不编辑已有文件，也不代表完整文档属性 API。
- OPC edit plan：基础。当前是 internal manifest、relationships、content types、
  part write-mode、`PartIndex`、`RelationshipGraph` 和 `ContentTypeRegistry`
  基础；已有 XLSX 编辑、未知 part 保真复制和 package rewrite 仍需 reader/writer
  和 preservation 测试闭环。
- Phase 5 worksheet metadata：基础。`WorksheetWriter::add_data_validation()`
  已有 streaming-only 新建文件切片，会在 worksheet XML 写出
  `<dataValidations>`；它不解析公式、不校验单元格值、不检查重叠、不编辑已有
  XLSX，也不新增 relationships 或 content types。`WorksheetWriter::add_external_hyperlink()`
  已有 external-only 新建文件切片，会写 worksheet `<hyperlinks>` 和对应的
  `xl/worksheets/_rels/sheetN.xml.rels`；它不写单元格文本、不创建 hyperlink
  样式、不支持 internal links、已有文件编辑或完整 hyperlink 属性。
  `WorksheetWriter::add_table()` 已有 streaming-only 新建文件切片，会写
  worksheet `<tableParts>`、`xl/tables/tableN.xml`、worksheet `.rels` 和 table
  content type override；它不读取已写 header 行、不推断列名、不生成 `styles.xml`、
  不支持 totals row、table resize、已有文件编辑或完整 Excel table UI。
  `WorksheetWriter::add_image()` 已有 opt-in `stb` streaming-only 新建文件切片，
  会写 media/drawing parts、drawing `.rels`、worksheet `.rels`、worksheet
  `<drawing>` 和 content type entries；它不进入 row/cell 热路径，不持有完整
  worksheet cell matrix，不支持 existing-file editing、drawing mutation、裁剪、旋转、
  压缩或格式转换。

## 先读哪些文件

- 项目定位：`README.md`、`docs/PROJECT_POSITIONING.md`
- 架构与数据流：`docs/ARCHITECTURE.md`、`docs/EDITING_MODEL.md`
- 依赖与环境：`docs/DEPENDENCIES.md`、`docs/DEVELOPMENT_ENVIRONMENT.md`
- 性能目标与路线图：`docs/PERFORMANCE_TARGETS.md`、`docs/ROADMAP.md`
- 测试流程：`docs/TESTING_WORKFLOW.md`
- API 设计和文档注释：`docs/API_DESIGN_AND_DOCUMENTATION.md`
- 与参考项目的边界：`docs/TECHNICAL_COMPARISON.md`
- 构建和测试骨架：`CMakeLists.txt`、`tests/CMakeLists.txt`

## 核心架构约束

- 新建 XLSX 和大数据写入路径必须使用 XML streaming。
- 大型 `worksheet.xml`、大型 `sharedStrings.xml`、批量导出和大型模板填充路径禁止 DOM。
- 小型 XML part 可以使用局部 DOM，例如 `workbook.xml`、关系文件、
  `[Content_Types].xml`、`docProps/*.xml`、较小的 `styles.xml`，
  以及规划中的小型 drawing/comments/table part；这只是 DOM 边界，不代表
  当前完整支持图片、VBA 或 table 编辑。
- 编辑已有 XLSX 时优先 part-level rewrite：未修改 part 原样复制，
  修改 part 才重新生成。
- 未知 part 默认保留，避免破坏图表、图片、宏和未知扩展。
- 大数据 API 必须面向 row iterator 或 chunk writer，不要为了 API 方便
  持有完整 worksheet cell matrix。
- `FastXmlWriter`、`CellEncoder`、`RowStreamWriter` 是文档中的性能热路径；
  不要在 cell XML 热路径上直接依赖通用 XML serializer。
- public API 必须向性能主线靠齐。不能为了 API 易用性让大型 worksheet
  进入 DOM、完整 cell matrix 或 cell map。
- public API 应写文档注释，说明模式、内存行为、随机访问限制和性能注意事项。

## 依赖策略

通用底层能力使用成熟库：

- `minizip-ng`：ZIP package 处理。
- `zlib-ng / zlib`：DEFLATE 压缩。
- `Expat`：大型 XML event parser。
- `pugixml`：小型 XML 局部 DOM 编辑。
- `stb`：Phase 5 图片读取/插入中的图片解码、尺寸和像素读取；当前只通过
  `FASTXLSX_ENABLE_STB=ON` 的 opt-in `planned-image` 路径接入，用于
  `read_image_info()` 图片元数据 helper 和 `WorksheetWriter::add_image()` 基础切片，
  不属于默认构建。
- `Catch2`：单元测试。
- `Google Benchmark`：性能基准。

FastXLSX 自己实现 XLSX 语义层：OPC part 索引、relationships、row/cell 编码、
`sharedStrings` / `inlineStr` 策略、styles registry、part-level rewrite。

真正接入依赖时，使用 `vcpkg` manifest mode。CMake 侧优先 `find_package`。
默认不要用 `FetchContent` 拉取核心依赖，也不要把第三方源码复制进 `src`
或 `include`。

当前已接入的第三方依赖是 opt-in 的 `minizip-ng[core,zlib]` package writer
backend：`find_package(minizip-ng CONFIG REQUIRED)`、`MINIZIP::minizip-ng`、
license 为 Zlib。它需要 `planned-runtime` feature；其中 `zlib-ng`、`expat`
和 `pugixml` 已能随 feature clean install，但当前源码尚未使用它们。

`OpenXLSX`、`xlnt`、`libxlsxwriter`、`QXlsx` 只能作为参考库、经验来源或
benchmark 对象，不作为 FastXLSX 的运行时底座。

项目使用 MIT License，见 `LICENSE`。

## 构建和测试命令

本项目以 Visual Studio 2026 / MSVC 2026 为主开发环境。推荐在 VS2026
Developer Command Prompt 中使用 preset：

```powershell
cmake --list-presets
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

验证 opt-in minizip-ng package writer backend 时，确保 `VCPKG_ROOT` 指向目标
vcpkg 根目录后运行：

```powershell
cmake --preset windows-nmake-release-minizip
cmake --build --preset windows-nmake-release-minizip
ctest --preset windows-nmake-release-minizip
```

验证 opt-in stb 图片元数据 helper 和 streaming 图片插入基础切片时，确保 `VCPKG_ROOT`
指向目标 vcpkg 根目录后运行：

```powershell
cmake --preset windows-nmake-release-image
cmake --build --preset windows-nmake-release-image
ctest --preset windows-nmake-release-image
```

手工 benchmark 必须显式 opt-in，不进入默认 CTest：

```powershell
cmake --preset windows-nmake-release-benchmark
cmake --build --preset windows-nmake-release-benchmark --target fastxlsx_bench_streaming_writer
```

需要验证 minizip backend 的 benchmark 时，使用
`windows-nmake-release-benchmark-minizip`。benchmark 结果必须记录数据规模、
字符串策略、ZIP backend/压缩设置、总耗时、峰值内存、输出大小和办公软件打开结果；
不要把 benchmark preset 当成默认质量门禁。
不传 `--output` / `--result` 时，当前 benchmark 工具默认写到 benchmark target
的 binary dir；手工工具限制 `--sheets <= 1024`，这只是 benchmark 输入护栏。
当前 benchmark JSON schema version 为 `2`，会记录 `string_pattern`、
`package_entry_source_mode="worksheet-file-backed-chunked"`、
`temporary_worksheet_part_footprint="not_measured"` 和
`temporary_worksheet_part_footprint_bytes=null`。这只说明当前工具还没有测量临时
worksheet part footprint，不能据此宣称完整低内存或大文件性能。

如果其他机器上的 Visual Studio 2026 对应新的 CMake 生成器名称，用下面命令确认：

```powershell
cmake --help
```

不要把旧 `build/CMakeCache.txt` 当成推荐构建配置。它曾显示为
`NMake Makefiles`，而开发文档已说明：未显式指定生成器时可能误选 NMake，
并在没有 `nmake` 的普通终端环境中失败。

后台运行普通单元测试时，核心测试超时时间设为 60s。当前 60s 边界来自
`CMakePresets.json` 的 CTest preset 和 `tests/CMakeLists.txt` 的测试属性。
如果手写 `-B build-nmake` 目录排障，必须显式给 `ctest --test-dir ...`
加 `--timeout 60`。大型 benchmark 不要混入默认单元测试。

## 常见开发路径

- 架构定位、模块边界、当前实现状态：使用 `.agents/skills/fastxlsx-project-navigation`。
- Phase 1 最小可写 XLSX：使用 `.agents/skills/fastxlsx-minimal-writer`。
- CMake、本地构建、测试入口、target 形态：使用 `.agents/skills/fastxlsx-cmake-build`。
- 第三方依赖、vcpkg、license、参考库边界：使用 `.agents/skills/fastxlsx-dependency-policy`。
- worksheet writer/reader/rewriter、大数据路径：使用 `.agents/skills/fastxlsx-streaming-worksheet`。
- 已有 XLSX 编辑、OPC part rewrite、未知 part 保留：使用 `.agents/skills/fastxlsx-opc-editing`。
- 测试、benchmark、兼容性验证、质量排障：使用 `.agents/skills/fastxlsx-test-quality`。
- data validations、hyperlinks 等 worksheet metadata / Phase 5 早期切片：
  使用 `.agents/skills/fastxlsx-worksheet-metadata-features`。
- 图片读取/插入、media part、drawing rels 和 `stb` 解码边界：
  使用 `.agents/skills/fastxlsx-image-media-features`。

## 质量和兼容性检查

- Phase 1 输出不能只看编译通过；生成的 `.xlsx` 应校验 OpenXML 基本结构，
  并在可用时验证 Excel / WPS / LibreOffice 可打开。
- 本机有 Excel 时，关键 `.xlsx` 样例必须用 Excel 打开做可视化验证。
- 当前 `fastxlsx.unit` 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-phase1-minimal.xlsx`；本机已用
  Excel 可视化验证并核对 `Sheet1`、`A1`、`B1`、`C1`、`A2`、`B2`。
  本地旧 `build-nmake/tests/*.xlsx` 可能存在，但除非确认由当前源码重新生成，
  否则应视为过期 artifact。
- 当前 `fastxlsx.streaming` data validations 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-data-validations.xlsx`；
  本机已用 Excel 打开验证 `Validation` sheet、`A2:G10` validation 类型、
  operator 和公式返回值。Excel COM 会把 list 公式外层引号去掉，并给函数公式
  返回值加 `=` 前缀；结构测试仍以拆包后的 worksheet XML 语义为准。
- 当前 `fastxlsx.streaming` external hyperlinks 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-external-hyperlinks.xlsx`；
  本机已用 Excel 打开验证 `Links`、`MoreLinks` 和 `Plain` sheet 的
  `Hyperlinks` 集合、Address 和 TextToDisplay，确认链接不替代单元格文本。
- 当前 `fastxlsx.streaming` tables 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-tables.xlsx`。本机已用
  Excel 打开验证 `Inventory`、`Totals` 和 `Plain` sheet 的 `ListObjects` 数量、
  表名、范围、header 文本和内建 table style flags；不要把这扩展成完整 table UI
  或已有文件编辑保证。
- 当前 `fastxlsx.streaming` Phase 3 metadata 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-phase3-metadata.xlsx`。本机已用
  Excel COM 打开验证 `Metadata` sheet、`B2` / `C2` 公式、row 2 高度、A/C 列宽、
  `B2:D4` auto filter、`A3:B3` / `C4:D4` merge areas，以及
  `SplitRow=2` / `SplitColumn=3` frozen panes；不要把这扩展成公式计算、
  cached values、calcChain、styles 或完整 Phase 3。
- 当前 `fastxlsx.streaming` images 推荐 preset 输出样例为
  `build/windows-nmake-release-image/tests/fastxlsx-streaming-images.xlsx`。本机已用
  Excel COM 打开验证 workbook 可读、`Images` 和 `SecondImage` sheet 各 1 个
  shape、`Plain` sheet 为 0 个 shape，Excel 报告首图锚点为 `C1:F5`、第二图为
  `A1:B2`；不要把这扩展成 existing-workbook 图片保真、完整 drawing 编辑或图片 UI 保证。
- `.xlsx` 结构异常时，按 `docs/TESTING_WORKFLOW.md` 使用 Excel、`openpyxl`
  或 `XlsxWriter` 生成语义等价参考文件，拆包后对比 XML，重点检查
  content types、relationships、workbook、worksheet、shared strings 和 styles。
- 编辑已有 XLSX 时，不只验证目标单元格，还要验证未修改 part 被保留。
- 性能结论必须记录总耗时、峰值内存、输出文件大小和打开兼容性。
- benchmark 对比对象是文档中的 `OpenXLSX`、`xlnt` streaming writer 和旧 `FastExcel`。

## 高风险误区

- 把路线图里的类名当成已实现 API。
- 为了快速实现，把大型 worksheet 路径改成 DOM。
- 把 `OpenXLSX` / `xlnt` 包一层当 FastXLSX 引擎。
- 从已知 part 全量重建 package，导致未知 part 丢失。
- 在代码未使用前，把所有规划依赖提前接入 CMake。
- 把 `src/zip_store_writer.*` 或 `src/package_writer.*` 当成已有文件编辑 API。
  当前 package writer 只是内部边界：默认 backend 是 Phase 1 stored bootstrap，
  opt-in backend 是 minizip-ng/DEFLATE；仍无 Zip64、true package streaming、
  `PackageReader` 或 existing-file preservation。
  即使 worksheet package entry 使用 file-backed/chunked source，也只是内部
  new-workbook output 的 entry-source 优化。
- 把 `stb` 当成完整图片 OpenXML 支持。`stb` 只负责图片解码/尺寸/像素读取；
  当前 `WorksheetWriter::add_image()` 只是 new-workbook PNG/JPEG 插入基础切片，
  不代表 existing-workbook 图片保真、drawing 编辑、裁剪、旋转、压缩或格式转换。
- 把第三方源码复制进 `src` 或 `include`。
- 修改 `tests/CMakeLists.txt` 后让 `ctest` 回到 0 测试，或让默认测试超过 60s。
- 为了 API 易用性牺牲 streaming 性能主线。
- 允许 `NaN`、`+Inf` 或 `-Inf` 写进 numeric cell、row height 或 column width XML。
- public API 没有文档注释，或注释不说明内存/性能限制。

## 项目 Skills

项目专属 skills 位于 `.agents/skills/`：

- `fastxlsx-project-navigation`：架构导航、当前实现状态、模块边界。
- `fastxlsx-minimal-writer`：Phase 1 最小可写 XLSX。
- `fastxlsx-cmake-build`：本地配置、构建、测试入口和 CMake target。
- `fastxlsx-dependency-policy`：依赖、vcpkg、license 和参考库边界。
- `fastxlsx-streaming-worksheet`：大型 worksheet 流式路径和热路径约束。
- `fastxlsx-opc-editing`：part-level rewrite 和已有文件保真编辑。
- `fastxlsx-test-quality`：测试、benchmark、兼容性验证和质量排障。
- `fastxlsx-api-design-docs`：API 设计、文档注释和性能边界。
- `fastxlsx-worksheet-metadata-features`：worksheet metadata 和 Phase 5 早期切片。
- `fastxlsx-image-media-features`：图片读取/插入、stb 解码、media/drawing part 和关系同步。
