# FastXLSX Agent Guide

## 项目快照

FastXLSX 是一个 C++20 / MSVC 2026 优先的 XLSX 引擎。项目文档固定的
核心方向是：流式优先、局部 DOM 可选、面向 OpenXML。

当前仓库处于 Phase 1 最小可写 XLSX 的早期实现阶段：

- `CMakeLists.txt` 定义了 compiled `fastxlsx` library。
- 目标别名是 `FastXLSX::fastxlsx`。
- `include/fastxlsx/fastxlsx.hpp`、`include/fastxlsx/workbook.hpp` 和
  `include/fastxlsx/streaming_writer.hpp` 提供当前 public API：
  `Workbook`、`Worksheet`、`Cell`、`WorkbookWriter`、`WorksheetWriter`、
  `CellView` 和 `FastXlsxError`。
- `src/opc.cpp`、`src/workbook.cpp`、`src/streaming_writer.cpp`、`src/xml.cpp`、
  `src/zip_store_writer.cpp` 是当前已接入 CMake 的实现入口。
- `include/fastxlsx/detail/opc.hpp` 和 `src/opc.cpp` 是内部 OPC
  manifest / relationships / XML serializer 基础，不代表完整已有 XLSX 编辑能力。
- `tests/test_minimal_xlsx.cpp` 通过 CTest 注册为 `fastxlsx.unit`，覆盖 XML escape、
  cell reference、最小 OpenXML package 结构和基础单元格编码。
- `tests/test_streaming_writer.cpp` 通过 CTest 注册为 `fastxlsx.streaming`，覆盖
  流式 writer 骨架、公式、行高、列宽、冻结窗格、自动筛选和合并单元格 XML 输出。
- `tests/test_opc.cpp` 通过 CTest 注册为 `fastxlsx.opc`，覆盖内部 OPC part name、
  content types、relationships、manifest 和 serializer 基础。
- 当前可见文件集中已有 `vcpkg.json`、`CMakePresets.json` 和
  `.github/workflows/ci.yml` 作为基础工程入口；依赖仍放在 planned feature，
  CMake 尚未接入第三方 `find_package` / link。
- `CMakeLists.txt` 已有 `FASTXLSX_BUILD_EXAMPLES` 分支，当前工作树中可见
  `examples/` 目录和示例源码；除非本轮任务验证该路径，否则不要把 example
  目标当作已完成发布面。

不要把文档里的设计名当成已实现符号。`WorkbookWriter` / `WorksheetWriter` /
`CellView` 流式写入骨架已存在；公式、行高、列宽、冻结窗格、自动筛选和
合并单元格是当前写入骨架能力，不等同完整 Phase 3。当前可见
`StringStrategy::SharedString`、内部 `SharedStringTable` 和
`xl/sharedStrings.xml` 结构测试，状态只能写为 sharedStrings 进行中或基础，
不能写成生产策略完成。`PackageReader`、`CellEncoder` 等名称仍主要来自
架构文档或路线图；只有在 `include/` 或 `src/` 中找到对应实现后，才能当作
真实 API。OPC 当前有 `PartWriteMode` 和 package part edit-state 基础，
但 `PackageReader`、`PackageWriter`、`PartIndex`、`RelationshipGraph`
以及已有文件编辑仍是计划；不要宣称完整图片、VBA 或 table 支持。

## 本轮推进计划同步

- sharedStrings：进行中。当前可见 API 选项、内部表、package wiring 和结构测试；
  仍需默认 CTest、Excel 可视化验证、Excel / `openpyxl` / `XlsxWriter`
  参考文件拆包 XML 对比，以及大小/内存数据后，才能扩大支持表述。
- vcpkg / CMakePresets / CI：基础。当前可见文件是工程入口基础，不代表
  第三方依赖已经接入 CMake；新增依赖前必须确认 vcpkg port、feature、
  imported target 和许可证。
- OPC edit plan：基础。当前是 internal manifest、relationships、content types
  和 part write-mode 规划；已有 XLSX 编辑、未知 part 保真复制和 package
  rewrite 仍需 reader/writer、part index、relationship graph 和测试闭环。

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
- `Catch2`：单元测试。
- `Google Benchmark`：性能基准。

FastXLSX 自己实现 XLSX 语义层：OPC part 索引、relationships、row/cell 编码、
`sharedStrings` / `inlineStr` 策略、styles registry、part-level rewrite。

真正接入依赖时，使用 `vcpkg` manifest mode。CMake 侧优先 `find_package`。
默认不要用 `FetchContent` 拉取核心依赖，也不要把第三方源码复制进 `src`
或 `include`。

`OpenXLSX`、`xlnt`、`libxlsxwriter`、`QXlsx` 只能作为参考库、经验来源或
benchmark 对象，不作为 FastXLSX 的运行时底座。

项目使用 MIT License，见 `LICENSE`。

## 构建和测试命令

本项目以 Visual Studio 2026 / MSVC 2026 为主开发环境。本机已验证的
Windows/MSVC 配置方式：

```powershell
cmd /d /c "call ""D:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S . -B build-nmake -G ""NMake Makefiles"" -DCMAKE_BUILD_TYPE=Release && cmake --build build-nmake && ctest --test-dir build-nmake --output-on-failure --timeout 60"
```

如果其他机器上的 Visual Studio 2026 对应新的 CMake 生成器名称，用下面命令确认：

```powershell
cmake --help
```

不要把旧 `build/CMakeCache.txt` 当成推荐构建配置。它曾显示为
`NMake Makefiles`，而开发文档已说明：未显式指定生成器时可能误选 NMake，
并在没有 `nmake` 的普通终端环境中失败。

后台运行普通单元测试时，核心测试超时时间设为 60s。大型 benchmark 不要混入
默认单元测试。

## 常见开发路径

- 架构定位、模块边界、当前实现状态：使用 `.agents/skills/fastxlsx-project-navigation`。
- Phase 1 最小可写 XLSX：使用 `.agents/skills/fastxlsx-minimal-writer`。
- CMake、本地构建、测试入口、target 形态：使用 `.agents/skills/fastxlsx-cmake-build`。
- 第三方依赖、vcpkg、license、参考库边界：使用 `.agents/skills/fastxlsx-dependency-policy`。
- worksheet writer/reader/rewriter、大数据路径：使用 `.agents/skills/fastxlsx-streaming-worksheet`。
- 已有 XLSX 编辑、OPC part rewrite、未知 part 保留：使用 `.agents/skills/fastxlsx-opc-editing`。
- 测试、benchmark、兼容性验证、质量排障：使用 `.agents/skills/fastxlsx-test-quality`。

## 质量和兼容性检查

- Phase 1 输出不能只看编译通过；生成的 `.xlsx` 应校验 OpenXML 基本结构，
  并在可用时验证 Excel / WPS / LibreOffice 可打开。
- 本机有 Excel 时，关键 `.xlsx` 样例必须用 Excel 打开做可视化验证。
- 当前 `fastxlsx.unit` 会生成 `build-nmake/tests/fastxlsx-phase1-minimal.xlsx`；
  本机已用 Excel 可视化验证并核对 `Sheet1`、`A1`、`B1`、`C1`、`A2`、`B2`。
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
- 把 `src/zip_store_writer.*` 当成长期 ZIP 后端。它只是 Phase 1 bootstrap：
  stored/no compression、无 Zip64、无 package streaming，不进入 public API。
- 把第三方源码复制进 `src` 或 `include`。
- 修改 `tests/CMakeLists.txt` 后让 `ctest` 回到 0 测试，或让默认测试超过 60s。
- 为了 API 易用性牺牲 streaming 性能主线。
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
