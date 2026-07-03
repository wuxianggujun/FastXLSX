# FastXLSX Agent Guide

## 项目快照

FastXLSX 是一个 C++20 / MSVC 2026 优先的可编辑高性能 XLSX 读写与编辑库。项目方向固定为共享
OpenXML/OPC 底座，并清晰区分 Streaming、Patch、In-memory 三条 API 路径：Streaming 是大文件新建
workbook 的性能主线，Patch 和 In-memory 是已有文件编辑主线。

当前 public / internal / planned / non-goal 能力的唯一事实源是
[docs/CURRENT_CAPABILITIES.md](docs/CURRENT_CAPABILITIES.md)。修改 README、API 文档、任务计划或 agent
上下文前，先更新或核对该文件；不要在 AGENTS 中继续维护超长 coverage 流水账。

当前执行口径：

- CMake target 是 compiled `fastxlsx` library，目标别名是 `FastXLSX::fastxlsx`。
- Public new-workbook 路径包括 `Workbook` / `Worksheet` / `Cell` 小文件创建和
  `WorkbookWriter` / `WorksheetWriter` / `CellView` streaming writer。
- Public existing-workbook 路径包括 `WorkbookEditor` Patch facade 和 small-file
  `WorksheetEditor` In-memory editor，输出使用 `save_as()`，不支持 atomic in-place save。
- `PackageReader`、`PackageEditor`、`EditPlan`、`DependencyAnalyzer`、`RelationshipGraph`、
  worksheet transformer 和 `CellStore` 是 internal foundation，不要写成 public API。
- `WorkbookEditor::replace_image()` 只替换已有 PNG/JPEG media part bytes；
  `WorksheetWriter::add_image()` 才是 streaming new-workbook 插入路径；两者都不是完整 drawing 编辑。
- 公式能力是公式文本、审计和重算请求边界；不要宣称公式求值、cached values 或完整 calcChain rebuild。
- 性能声明必须有 benchmark / QA 证据，写清 dataset、backend、string strategy、rewrite strategy、耗时、内存和打开结果。

推荐先读：

- [docs/CURRENT_CAPABILITIES.md](docs/CURRENT_CAPABILITIES.md)：当前能力事实源。
- [docs/API_DESIGN_AND_DOCUMENTATION.md](docs/API_DESIGN_AND_DOCUMENTATION.md)：API 设计门和 Doxygen 边界。
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)：模块分层和 OpenXML/OPC 架构边界。
- [docs/EDITING_MODEL.md](docs/EDITING_MODEL.md)：Streaming / Patch / In-memory 编辑模型。
- [docs/TASK_BREAKDOWN.md](docs/TASK_BREAKDOWN.md)：执行级 active queue；历史任务需要时查 git 历史。

## 本轮推进计划同步

- 当前执行线是 `C0 -> C7`，具体 active queue 和功能 lane 以
  [docs/TASK_BREAKDOWN.md](docs/TASK_BREAKDOWN.md) 顶部为准；旧阶段内容不再保留在入口正文中，需要时查 git 历史。
- 当前能力事实以 [docs/CURRENT_CAPABILITIES.md](docs/CURRENT_CAPABILITIES.md) 为准；README、AGENTS、API docs
  不再维护独立长矩阵或逐项 coverage addendum。
- 新任务必须先说明所属路径：Streaming、Patch、In-memory；涉及 public API 时还要说明所属 facade：
  `WorkbookWriter`、`Workbook`、`WorkbookEditor` 或 `WorksheetEditor`。
- Patch / existing-file 任务必须写清 preserve / audit / fail / edit 策略，尤其是 sharedStrings、styles、formulas、
  relationships、tables、drawings、images、comments、VBA、custom XML 和 unknown extensions。
- In-memory 任务默认是 small-file random editing，不能扩展成 large-file low-memory random editing；大 worksheet
  低内存 rewrite 属于独立 C5 路径。
- benchmark、minizip、sharedStrings、file-backed/chunked worksheet entry 和 streaming hot path 是 C6 支撑线；
  只有触碰性能边界或 release 宣称时才作为验证输入，不能阻塞编辑能力事实收敛。
- 不要把 internal `PackageEditor` / `EditPlan` / `DependencyAnalyzer` / `RelationshipGraph` 写成 public API。

## 先读哪些文件

- 项目定位：`README.md`、`docs/ARCHITECTURE.md`
- 架构与数据流：`docs/ARCHITECTURE.md`、`docs/EDITING_MODEL.md`
- Patch 保留能力回归明细：`docs/PATCH_PRESERVATION_COVERAGE.md`
- 依赖与环境：`vcpkg.json`、`CMakePresets.json`、`.github/workflows/ci.yml`
- 性能目标与阶段计划：`docs/PERFORMANCE_TARGETS.md`、`docs/TASK_PLAN.md`、
  `docs/NEXT_STEPS.md`
- 测试流程：`docs/TESTING_WORKFLOW.md`
- API 设计和文档注释：`docs/API_DESIGN_AND_DOCUMENTATION.md`
- 与参考项目的边界：`docs/ARCHITECTURE.md`、`docs/PERFORMANCE_TARGETS.md`
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
- Patch / existing-file editing 规划应先生成或更新 EditPlan，明确哪些 part
  copy-original、stream-rewrite、local-DOM-rewrite 或 removed，并说明 sharedStrings、styles、
  worksheet `.rels`、tables、drawings、defined names、calcChain 和 workbook calc metadata
  的联动策略。
- In-memory API 是小文件随机编辑路径，可以规划 `get_cell()` / `set_cell()` 等便利
  能力，但不得成为大数据默认路径。
- 大数据 API 必须面向 row iterator 或 chunk writer，不要为了 API 方便
  持有完整 worksheet cell matrix。
- `FastXmlWriter`、`CellEncoder`、`RowStreamWriter` 是文档中的性能热路径；
  不要在 cell XML 热路径上直接依赖通用 XML serializer。
- public API 必须向性能主线靠齐。不能为了 API 易用性让大型 worksheet
  进入 DOM、完整 cell matrix 或 cell map。
- 编辑能力不能被降级为 streaming writer 的补丁；PackageEditor、In-memory editor 和
  EditPlan 应作为核心架构模块推进。
- public API 应写文档注释，说明模式、内存行为、随机访问限制和性能注意事项。

## 文件职责边界与模块化约束

- 新增功能、测试或 QA helper 时，优先保持文件职责清晰，避免把所有实现继续堆进
  少数几个“大文件”。
- `src/streaming_writer.cpp` 应保持为 streaming worksheet 写入流程协调层和热路径入口；
  不应无限承载 conditional formatting、data validations、hyperlinks、tables、
  images/drawings、styles、sharedStrings 等所有细节实现。
- `tests/test_streaming_writer.cpp` 应保留 streaming writer 主流程、关键边界和跨功能
  集成测试；独立 feature 的大量结构测试和负例应优先拆到 feature-specific 测试文件。
- 当一个功能已经有独立 public API、独立 XML 序列化、独立状态结构、独立 QA helper
  或大量边界用例时，应考虑拆到独立 `.cpp`、detail helper 或独立测试文件。
- public API 可以继续集中在现有 public headers，保持用户入口稳定；拆分主要针对内部
  实现、XML 生成、校验、状态转换和测试组织。
- 集成测试只保留真正跨模块的行为，例如 suffix 顺序、relationship id、content type
  side effects、package side effects 和多对象共存。
- QA helper 应保持 feature-specific；不要把无关功能混成一个巨型脚本。
- 新增 `.cpp` 或测试文件时，必须同步更新 `CMakeLists.txt` / `tests/CMakeLists.txt`。
- 拆分不能引入完整 worksheet cell matrix、DOM 热路径或无关重构。
- 不为很小的临时代码过度拆分；只有功能边界明确、代码继续增长或测试开始挤占主测试
  文件时才拆。

## 依赖策略

通用底层能力使用成熟库：

- `minizip-ng`：ZIP package 处理。
- `zlib-ng / zlib`：DEFLATE 压缩。
- `Expat`：大型 XML event parser。
- `pugixml`：小型 XML 局部 DOM 编辑。
- `stb`：图片读取/插入中的图片解码、尺寸和像素读取；当前作为默认
  vcpkg manifest 依赖接入，用于 `read_image_info()` 图片元数据 helper 和
  `WorksheetWriter::add_image()` 基础切片。
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
当前 benchmark JSON schema version 为 `4`，会记录 `string_pattern`、
`string_cells`、`unique_string_values`、`duplicate_string_cells`、
`string_dedup_ratio`、`package_entry_source_mode="worksheet-file-backed-chunked"`、
`temporary_worksheet_part_footprint="worksheet-body-file-bytes"` 和数值型
`temporary_worksheet_part_footprint_bytes`。字符串分布字段描述 benchmark 输入，
适用于 inline/shared 两种策略；footprint 值来自 benchmark-only instrumentation，
只累计 worksheet body row XML 写入字节数，不包含 worksheet header/footer、
sharedStrings 临时文件、小型 XML parts、media 文件、ZIP/backend 缓冲、
package assembly 峰值内存或 OS 文件系统开销；不能据此宣称完整低内存或大文件性能。
当前 `tools/run_benchmark_matrix.py` 是 opt-in 本地矩阵 runner，只包装一个已构建的
`fastxlsx_bench_streaming_writer` exe 并聚合 schema-v4 JSON；stored/minizip 要分别传入
各自 preset 的 exe 和输出目录。`--self-test` 只检查 runner 内部 parser / distribution /
report 假设，不调用 benchmark exe、不生成 `.xlsx` 或 JSON，不得写成 benchmark 或 Office
验证。`tools/verify_benchmark_matrix_excel.ps1` 可本机只读打开
report 中的部分 workbook，但不会改写 benchmark JSON 的 `office_open="not_run"`。

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
当前 GitHub Actions workflow 使用 VS2026 runner。默认 job 跑
`windows-nmake-release`；独立 vcpkg matrix job 跑 `windows-nmake-release-minizip`。
Excel COM、openpyxl / XlsxWriter 参考 QA 和
benchmark 仍是本机/手工验证，不作为 CI 强依赖。

## 常见开发路径

- 架构定位、模块边界、当前实现状态：使用 `.agents/skills/fastxlsx-project-navigation`。
- 最小可写 XLSX：使用 `.agents/skills/fastxlsx-minimal-writer`。
- CMake、本地构建、测试入口、target 形态：使用 `.agents/skills/fastxlsx-cmake-build`。
- 第三方依赖、vcpkg、license、参考库边界：使用 `.agents/skills/fastxlsx-dependency-policy`。
- worksheet writer/reader/rewriter、大数据路径：使用 `.agents/skills/fastxlsx-streaming-worksheet`。
- 已有 XLSX 编辑、OPC part rewrite、未知 part 保留：使用 `.agents/skills/fastxlsx-opc-editing`。
- 测试、benchmark、兼容性验证、质量排障：使用 `.agents/skills/fastxlsx-test-quality`。
- data validations、hyperlinks 等 worksheet metadata 基础切片：
  使用 `.agents/skills/fastxlsx-worksheet-metadata-features`。
- conditional formatting、color scale、basic data bar、basic 3Arrows icon set、priority 和本地 QA：
  使用 `.agents/skills/fastxlsx-conditional-formatting-features`。
- 图片读取/插入、media part、drawing rels 和 `stb` 解码边界：
  使用 `.agents/skills/fastxlsx-image-media-features`。

## 质量和兼容性检查

- 文档-only 变更：运行 focused `rg` / `git diff --check`，确认链接存在、当前事实没有漂移、没有把 internal 类型写成 public API；不需要跑 C++ build / CTest。
- 代码变更：按 [docs/TESTING_WORKFLOW.md](docs/TESTING_WORKFLOW.md)、`CMakePresets.json` 和相关 skill 要求选择最小可验证 preset / CTest shard。
- OpenXML 输出变更：除单元测试外，优先做 ZIP 拆包结构检查；关键 workbook 在可用时用 Excel / openpyxl / LibreOffice 做 smoke。
- Patch / existing-file 变更：必须验证 unknown part preservation、relationship/content type side effects、calcChain/fullCalcOnLoad 策略、failure-before-state-change 和 no-state-pollution。
- Streaming 变更：必须确认 row-order hot path 没有引入 DOM、dense worksheet matrix 或跨行无界状态。
- In-memory 变更：必须验证 `max_cells` / `memory_budget_bytes` guardrails、dirty-session `save_as()` handoff、failure recovery 和 style/sharedStrings/formula 边界。
- 性能声明：必须引用 benchmark 或 QA 证据，写清 dataset、backend、compression、string strategy、rewrite strategy、耗时、内存/estimate、输出大小和打开结果。

## 高风险误区

- 把路线图、历史任务或 internal test hook 写成当前 public API。
- 把 `PackageReader`、`PackageEditor`、`EditPlan`、`DependencyAnalyzer`、`RelationshipGraph` 或 package-entry chunk helper 写成 public package editing API。
- 为了便利 API 让大型 worksheet 进入 DOM、dense cell matrix、完整 cell map 或无界 shared state。
- 把 `Workbook` 小文件 creation path 写成 existing-file editor，或把 `save_as()` 写成 atomic in-place save。
- 把 `WorksheetEditor` small-file In-memory 能力写成 large-file low-memory random editing。
- 把 `replace_image()` 写成 drawing/anchor/relationship 编辑，或把 `WorksheetWriter::add_image()` 写成 existing-workbook 图片保真。
- 把公式文本、formula audit 或 `request_full_calculation()` 写成公式求值、cached values 或完整 calcChain rebuild。
- 把 `stb`、minizip-ng、sharedStrings、styles、conditional formatting、tables、data validations 或 hyperlinks 的窄切片写成完整 Excel parity。
- 把 benchmark 小样本、stored bootstrap 数据或 file-backed/chunked implementation detail 写成泛化性能结论。
- 修改 CMake / tests 后让 `ctest` 回到 0 测试，或让默认测试明显超时。
- 在日志、文档或示例中泄漏 token、密钥、用户隐私或本地绝对敏感路径。

## 项目 Skills

项目专属 skills 位于 `.agents/skills/`：

- `fastxlsx-project-navigation`：架构导航、当前实现状态、模块边界。
- `fastxlsx-minimal-writer`：最小可写 XLSX。
- `fastxlsx-cmake-build`：本地配置、构建、测试入口和 CMake target。
- `fastxlsx-dependency-policy`：依赖、vcpkg、license 和参考库边界。
- `fastxlsx-streaming-worksheet`：大型 worksheet 流式路径和热路径约束。
- `fastxlsx-opc-editing`：part-level rewrite 和已有文件保真编辑。
- `fastxlsx-test-quality`：测试、benchmark、兼容性验证和质量排障。
- `fastxlsx-api-design-docs`：API 设计、文档注释和性能边界。
- `fastxlsx-worksheet-metadata-features`：worksheet metadata 基础切片。
- `fastxlsx-conditional-formatting-features`：streaming conditional formatting、color scale、
  basic data bar、basic 3Arrows icon set、priority、multi-range `sqref` 和 Excel/openpyxl/XlsxWriter QA。
- `fastxlsx-image-media-features`：图片读取/插入、stb 解码、media/drawing part 和关系同步。
- `fastxlsx-style-registry`：number format / wrap-text + limited horizontal/vertical alignment /
  bold-italic font / solid fill styles、`StyleId`、`CellAlignment`、
  `HorizontalAlignment`、`VerticalAlignment`、`CellFont`、`CellFill`、`CellStyle`、`xl/styles.xml`
  和样式 QA 验证。
