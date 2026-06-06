---
name: fastxlsx-test-quality
description: "添加或排查 FastXLSX 测试、CTest/Catch2 集成、OpenXML 兼容性检查、本机 Excel 可视化验证、参考 XLSX 拆包 XML 对比、benchmark、内存/性能验证和质量门禁。用于让 ctest 跑起来、补单元测试、设计基准、验证生成 XLSX，或调查结构、性能、内存、兼容性回退。"
---

# FastXLSX Test Quality

## 必读文件

- `tests/CMakeLists.txt`
- `CMakeLists.txt`
- `docs/PERFORMANCE_TARGETS.md`
- `docs/TESTING_WORKFLOW.md`
- `docs/ROADMAP.md`
- `docs/TECHNICAL_COMPARISON.md`
- `README.md`
- `docs/DEPENDENCIES.md`

当前已有轻量 CTest 测试入口。尚未接入 Catch2。

## 当前测试事实

- `FASTXLSX_BUILD_TESTS` 默认 `ON`。
- 顶层 CMake 在启用测试时调用 `enable_testing()` 和 `add_subdirectory(tests)`。
- 当前测试入口包括：
  - `fastxlsx_tests` / `fastxlsx.unit`。
  - `fastxlsx_streaming_writer_tests` / `fastxlsx.streaming`。
  - `fastxlsx_opc_tests` / `fastxlsx.opc`。
  - `fastxlsx_image_tests` / `fastxlsx.image`。
- 当前测试生成基础和 streaming smoke `.xlsx`，通过 backend-neutral ZIP helper
  读取解压后的 entries，并检查 content types、relationships、worksheet XML、XML escape、cell reference、
  streaming writer metadata、data validations、sharedStrings 结构、基础可配置
  docProps 输出和内部 OPC manifest 基础。
- 当前 streaming 测试还覆盖 file-backed/chunked worksheet package entry 的结构语义：
  解压后的 worksheet XML、entry name 唯一性、content type、relationships，以及
  stored/minizip backend 可用时的 backend-neutral 结果。
- 当前 streaming Phase 3 metadata 测试覆盖公式 XML escape、row height、多个
  column width records、last-call-wins frozen panes、last-call-wins auto filters、
  多个 merged ranges、worksheet suffix ordering，以及这些 metadata-only 功能不新增
  worksheet relationships、workbook relationships 或 content type side effects。
- 当前 document properties 测试覆盖 in-memory `Workbook::set_document_properties()`
  和 streaming `WorkbookWriterOptions::document_properties`，检查 `docProps/core.xml`、
  `docProps/app.xml`、XML escape、package relationships、content type overrides，
  并确认不生成 `docProps/custom.xml`。
- 当前 streaming data validations 测试覆盖 `count`、`sqref`、`type`、`operator`、
  `allowBlank`、`formula1`、`formula2`、XML escape、invalid ranges、
  invalid rule shapes、package relationship absence 和 mutation-after-close。
- 当前 streaming external hyperlinks 测试覆盖 worksheet `<hyperlinks>`、worksheet
  `.rels`、target XML escape、owner-local `rId`、plain sheet absence、workbook
  relationship absence、content type override absence、invalid cell、empty target
  和 mutation-after-close。
- 当前 streaming tables 测试覆盖 `xl/tables/tableN.xml`、worksheet `<tableParts>`、
  worksheet `.rels`、table content type override、owner-local `rId`、与 hyperlinks
  共存时的关系 id、XML escape、invalid ranges/options、duplicate names 和
  mutation-after-close。
- 当前 image 测试覆盖默认无 `stb` 构建下 `read_image_info()` 的明确错误，以及
  opt-in `FASTXLSX_ENABLE_STB=ON` 构建下 PNG 文件/内存尺寸和通道读取、
  unsupported image header、missing file。当前 streaming 测试还覆盖默认无 `stb`
  时 `WorksheetWriter::add_image()` 明确失败，以及 opt-in image preset 下
  media/drawing/rels/content types/worksheet `<drawing>` 结构。
- 当前没有 Catch2 集成。
- `Catch2` 和 `Google Benchmark` 是 planned-dev 依赖，尚未接入 CMake。

## 单元测试优先级

优先补小而确定的测试：

- XML escape。
- 单元格引用生成。
- 数字/日期/布尔/字符串编码。
- `inlineStr` 和 `sharedStrings` 策略。
- worksheet metadata：列宽、冻结窗格、自动筛选、合并单元格、data validations、
  external hyperlinks 和 tables。
- dimension tracking。
- row buffer 复用不变量。
- OPC content types 和 relationships。
- part index 与未修改 part 保留。

普通单元测试不要混入大型 benchmark，并遵守 60s 核心测试边界。

## 兼容性 QA

Phase 1 质量不只是“能编译”。生成的 `.xlsx` 应检查：

- ZIP package 结构。
- `[Content_Types].xml`。
- relationships。
- `docProps/core.xml` 和 `docProps/app.xml`。
- configurable document properties 场景还要检查 creator、lastModifiedBy、title、
  subject、description、keywords、category、Application、AppVersion，以及是否没有
  意外的 `docProps/custom.xml`。
- workbook part。
- worksheet part。
- 基础 `sheetData`。
- 本机有 Excel 时，必须用 Excel 打开关键样例做可视化验证。
- 当前 Phase 3 metadata 推荐 preset 样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-phase3-metadata.xlsx`；
  本机 Excel COM 已验证 `Metadata` sheet、`B2` / `C2` 公式、row 2 高度、A/C
  列宽、`B2:D4` auto filter、`A3:B3` / `C4:D4` merge areas，以及
  `SplitRow=2` / `SplitColumn=3` frozen panes。不要据此宣称公式计算、
  cached values、calcChain、styles 或完整 Phase 3。
- 在可用时验证 Excel / WPS / LibreOffice 能打开。
- 当前 Phase 1 smoke 样例通常位于测试工作目录；推荐 preset 路径是
  `build/windows-nmake-release/tests/fastxlsx-phase1-minimal.xlsx`。旧
  `build-nmake/tests/*.xlsx` 可能是过期 artifact，人工验证前必须确认重新生成。

编辑已有文件时，还要验证未修改 part 被保留，尤其是图表、图片、宏和未知扩展。

## Excel 可视化和 XML 对比

遇到 `.xlsx` 打不开、Excel 修复弹窗、单元格显示不对或结构测试失败时，
按 `docs/TESTING_WORKFLOW.md` 的流程排障：

1. 用本机 Excel 创建语义等价的参考 `.xlsx`，或用 `openpyxl` / `XlsxWriter`
   创建参考文件。
2. 将 FastXLSX 输出和参考文件都复制为 `.zip` 并解压。
3. 对比 `[Content_Types].xml`、`_rels/.rels`、`docProps/core.xml`、
   `docProps/app.xml`、`xl/workbook.xml`、`xl/_rels/workbook.xml.rels`、
   `xl/worksheets/sheet*.xml`。
4. 如果涉及 shared strings 或 styles，再对比 `xl/sharedStrings.xml` 和 `xl/styles.xml`。
5. 如果涉及 data validations，重点对比 worksheet XML 中的 `<dataValidations>`、
   `count`、`sqref`、`type`、`operator`、`allowBlank`、`formula1` 和 `formula2`。
6. 如果涉及 external hyperlinks，重点对比 worksheet `<hyperlinks>`、
   `xl/worksheets/_rels/sheet*.xml.rels`、relationship `Type`、`Target`、
   `TargetMode="External"`、worksheet-local `rId`、workbook relationships 是否未污染，
   以及 `[Content_Types].xml` 是否只依赖 `.rels` default。
7. 如果涉及 tables，重点对比 `xl/tables/table*.xml`、worksheet `<tableParts>`、
   `xl/worksheets/_rels/sheet*.xml.rels`、relationship `Type` / `Target`、
   table content type override、table `id` / `name` / `displayName` / `ref`、
   `autoFilter`、`tableColumns` 和 `tableStyleInfo`。
8. 不要求 byte-level 完全一致，重点比较 OpenXML 语义、关系、content type、
   cell reference、cell type 和 value。

Python XLSX 库只作为测试/排障参考，不是 FastXLSX 运行时依赖。

## ZIP backend 验证

OpenXML 结构测试应比较解压后的 package entries 和 XML 语义，不要断言 ZIP
central directory method 必须是 `0`。默认构建的 stored bootstrap 会写 method 0；
`FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建会写 method 8（DEFLATE）。二者 entry 顺序、
压缩大小和 archive size 可以不同。

验证 opt-in minizip backend 时运行：

```powershell
cmake --preset windows-nmake-release-minizip
cmake --build --preset windows-nmake-release-minizip
ctest --preset windows-nmake-release-minizip
```

file-backed/chunked entry 测试也必须走 decompressed package semantics，不要断言
entry source、entry order、compressed size、archive size 或 chunk 边界。测试可以断言
duplicate entry 被拒绝，以及 worksheet XML 内容没有因 chunk 边界被截断或重排。

## 图片和复杂对象验收

当前完整图片 OpenXML 读取/插入仍是 Phase 5；但
`WorksheetWriter::add_image()` 已有 streaming-only new-workbook PNG/JPEG 基础插入
切片。`read_image_info()` 只表示 PNG/JPEG 图片元数据 helper 已有 opt-in `stb`
路径；不要把它写成 existing-workbook 图片保真、drawing 编辑或完整图片支持已完成。
图片结构测试至少检查：

- `xl/media/image*.png|jpg|jpeg` 存在。
- `xl/drawings/drawing*.xml` 存在。
- `xl/drawings/_rels/drawing*.xml.rels` 指向 media part。
- `xl/worksheets/_rels/sheet*.xml.rels` 指向 drawing part。
- worksheet XML 中 drawing `r:id` 与 worksheet rels 一致。
- `[Content_Types].xml` 有图片格式 default 或 override。
当前推荐样例是
`build/windows-nmake-release-image/tests/fastxlsx-streaming-images.xlsx`。

Anchor 测试要覆盖起始/结束单元格、offset、零尺寸、负尺寸和越界 anchor；不要为了
anchor 计算引入完整 worksheet DOM。图片读取应使用 `stb` 处理解码、尺寸、通道和像素，
但结构测试必须验证 FastXLSX 自己生成的 OpenXML media/drawing package 语义。
兼容性测试要用本机 Excel 打开 `.xlsx` 样例，确认无修复弹窗并检查图片显示位置/尺寸；
当前本机 Excel COM 验证结果应记录为 `Images` / `SecondImage` 各 1 个 shape、
`Plain` 为 0 个 shape，锚点 `C1:F5` 和 `A1:B2`。
结构异常时用 Excel、`openpyxl` 或 `XlsxWriter` 参考文件拆包对比 XML。已有文件编辑
场景还要证明未修改 drawings、media、charts、macros 和 unknown parts 没有丢失，
relationships 仍指向有效 target。

涉及 public 图片 API 时，测试计划还要检查文档注释是否写清 Streaming/Patch/In-memory
模式、原始图片字节和 decoded pixel buffer 的内存成本、OpenXML part 副作用、是否触发
DOM，以及为什么不会破坏 worksheet streaming 热路径。

## Benchmark 优先级

Benchmark 应记录：

- 构建类型。
- 数据规模。
- 压缩等级或 ZIP backend。
- 字符串策略。
- 总耗时。
- 峰值内存。
- 输出文件大小。
- 办公软件打开兼容性。

文档中的对比对象是 `OpenXLSX`、`xlnt` streaming writer 和旧 `FastExcel`。
不要把这些 benchmark 对象变成 FastXLSX 运行时依赖。

当前最小 P6 benchmark 入口是 `FASTXLSX_BUILD_BENCHMARKS=ON` 下的手工工具
`fastxlsx_bench_streaming_writer`。它不使用 Google Benchmark，不注册 CTest，
不进入默认 CI；`planned-dev` 中的 `benchmark` 仍不是当前 CMake 事实。
不传 `--output` / `--result` 时，该工具默认写到 benchmark target 的 binary dir；
`--sheets` 超过 1024 会被拒绝，这是 benchmark 工具护栏，不是 public API 限制。
当前 benchmark JSON schema version 为 `2`，会写 `string_pattern`、
`package_entry_source_mode="worksheet-file-backed-chunked"`、
`temporary_worksheet_part_footprint="not_measured"` 和
`temporary_worksheet_part_footprint_bytes=null`。不要把 `not_measured` 写成临时文件
footprint 已验证；只有后续工具真实记录字节数后才能据此做低内存结论。

重点覆盖：

- 百万行级导出。
- 多 sheet 批量导出。
- 数字/日期/布尔密集写入。
- 字符串密集下的 `inlineStr` 与 `sharedStrings`。
- repeated / unique string pattern 下的 sharedStrings 体积、耗时和峰值内存对比。
- 模板 sheet 替换。
- ZIP 压缩等级。
- package entry source mode：in-memory / file-backed / chunked。
- close-time package assembly peak memory。
- temporary worksheet part footprint。

## 排障路径

- `ctest` 跑出 0 个测试：先查 `tests/CMakeLists.txt` 是否仍注册
  `fastxlsx.unit`、`fastxlsx.streaming` 和 `fastxlsx.opc`。
- 生成 XLSX 打不开：先查 package entries、content types、relationships、
  workbook、worksheet、`sheetData`。
- Excel 提示修复：保存修复后的文件，与原输出和参考文件拆包对比 XML。
- 内存异常：优先查 DOM、完整 worksheet matrix、cell map、跨行缓存。
- 内存异常还要查 close-time 是否重新物化完整 worksheet XML、file-backed entry 是否退化为
  in-memory entry、chunk buffer 是否无界增长、临时文件生命周期是否异常。
- 性能回退：优先查 XML 编码、escape、数字转换、cell reference、
  压缩等级、sharedStrings、row buffer。

## 验证命令

按 `docs/DEVELOPMENT_ENVIRONMENT.md` 的生成器建议：

```powershell
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

当前本机可用路径是 VS2026 Developer Command Prompt + NMake；其他机器可用
`cmake --help` 检查是否有更合适的 Visual Studio 2026 生成器。

后台跑普通单元测试时使用 60s 超时；preset 和 `tests/CMakeLists.txt` 已承载该
边界。手写 build dir 时显式加 `ctest --test-dir ... --timeout 60`。大型
benchmark 必须显式 opt-in。
