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
- 当前测试生成基础和 streaming smoke `.xlsx`，并检查 stored ZIP entries、
  content types、relationships、worksheet XML、XML escape、cell reference、
  streaming writer metadata、sharedStrings 结构、基础 docProps 输出和内部 OPC
  manifest 基础。
- 当前没有 Catch2 集成。
- `Catch2` 和 `Google Benchmark` 是 planned-dev 依赖，尚未接入 CMake。

## 单元测试优先级

优先补小而确定的测试：

- XML escape。
- 单元格引用生成。
- 数字/日期/布尔/字符串编码。
- `inlineStr` 和 `sharedStrings` 策略。
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
- workbook part。
- worksheet part。
- 基础 `sheetData`。
- 本机有 Excel 时，必须用 Excel 打开关键样例做可视化验证。
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
5. 不要求 byte-level 完全一致，重点比较 OpenXML 语义、关系、content type、
   cell reference、cell type 和 value。

Python XLSX 库只作为测试/排障参考，不是 FastXLSX 运行时依赖。

## 图片和复杂对象验收

图片功能实现后，结构测试至少检查：

- `xl/media/image*.png|jpg|jpeg` 存在。
- `xl/drawings/drawing*.xml` 存在。
- `xl/drawings/_rels/drawing*.xml.rels` 指向 media part。
- `xl/worksheets/_rels/sheet*.xml.rels` 指向 drawing part。
- worksheet XML 中 drawing `r:id` 与 worksheet rels 一致。
- `[Content_Types].xml` 有图片格式 default 或 override。

Anchor 测试要覆盖起始/结束单元格、offset、零尺寸、负尺寸和越界 anchor；不要为了
anchor 计算引入完整 worksheet DOM。兼容性测试要用 Excel 打开确认无修复弹窗并检查
图片显示位置/尺寸；结构异常时用 Excel、`openpyxl` 或 `XlsxWriter` 参考文件拆包
对比 XML。已有文件编辑场景还要证明未修改 drawings、media、charts、macros 和
unknown parts 没有丢失，relationships 仍指向有效 target。

## Benchmark 优先级

Benchmark 应记录：

- 总耗时。
- 峰值内存。
- 输出文件大小。
- 办公软件打开兼容性。

文档中的对比对象是 `OpenXLSX`、`xlnt` streaming writer 和旧 `FastExcel`。
不要把这些 benchmark 对象变成 FastXLSX 运行时依赖。

重点覆盖：

- 百万行级导出。
- 多 sheet 批量导出。
- 数字/日期/布尔密集写入。
- 字符串密集下的 `inlineStr` 与 `sharedStrings`。
- 模板 sheet 替换。
- ZIP 压缩等级。

## 排障路径

- `ctest` 跑出 0 个测试：先查 `tests/CMakeLists.txt` 是否仍注册
  `fastxlsx.unit`、`fastxlsx.streaming` 和 `fastxlsx.opc`。
- 生成 XLSX 打不开：先查 package entries、content types、relationships、
  workbook、worksheet、`sheetData`。
- Excel 提示修复：保存修复后的文件，与原输出和参考文件拆包对比 XML。
- 内存异常：优先查 DOM、完整 worksheet matrix、cell map、跨行缓存。
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
