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

当前已有轻量测试入口：`tests/test_minimal_xlsx.cpp` 通过 `tests/CMakeLists.txt`
注册为 `fastxlsx.unit`。尚未接入 Catch2。

## 当前测试事实

- `FASTXLSX_BUILD_TESTS` 默认 `ON`。
- 顶层 CMake 在启用测试时调用 `enable_testing()` 和 `add_subdirectory(tests)`。
- 当前有 `fastxlsx_tests` 测试可执行文件和 CTest 条目 `fastxlsx.unit`。
- 当前测试生成 `fastxlsx-phase1-minimal.xlsx`，并检查 stored ZIP entries、
  content types、relationships、worksheet XML、XML escape 和 cell reference。
- 当前没有 Catch2 集成。
- README 推荐 `Catch2` 作为测试框架，`Google Benchmark` 作为 benchmark 框架。

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
- workbook part。
- worksheet part。
- 基础 `sheetData`。
- 本机有 Excel 时，必须用 Excel 打开关键样例做可视化验证。
- 在可用时验证 Excel / WPS / LibreOffice 能打开。
- 当前 Phase 1 smoke 样例通常位于测试工作目录：
  `build-nmake/tests/fastxlsx-phase1-minimal.xlsx`。

编辑已有文件时，还要验证未修改 part 被保留，尤其是图表、图片、宏和未知扩展。

## Excel 可视化和 XML 对比

遇到 `.xlsx` 打不开、Excel 修复弹窗、单元格显示不对或结构测试失败时，
按 `docs/TESTING_WORKFLOW.md` 的流程排障：

1. 用本机 Excel 创建语义等价的参考 `.xlsx`，或用 `openpyxl` / `XlsxWriter`
   创建参考文件。
2. 将 FastXLSX 输出和参考文件都复制为 `.zip` 并解压。
3. 对比 `[Content_Types].xml`、`_rels/.rels`、`xl/workbook.xml`、
   `xl/_rels/workbook.xml.rels`、`xl/worksheets/sheet*.xml`。
4. 如果涉及 shared strings 或 styles，再对比 `xl/sharedStrings.xml` 和 `xl/styles.xml`。
5. 不要求 byte-level 完全一致，重点比较 OpenXML 语义、关系、content type、
   cell reference、cell type 和 value。

Python XLSX 库只作为测试/排障参考，不是 FastXLSX 运行时依赖。

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

- `ctest` 跑出 0 个测试：先查 `tests/CMakeLists.txt` 是否仍注册 `fastxlsx.unit`。
- 生成 XLSX 打不开：先查 package entries、content types、relationships、
  workbook、worksheet、`sheetData`。
- Excel 提示修复：保存修复后的文件，与原输出和参考文件拆包对比 XML。
- 内存异常：优先查 DOM、完整 worksheet matrix、cell map、跨行缓存。
- 性能回退：优先查 XML 编码、escape、数字转换、cell reference、
  压缩等级、sharedStrings、row buffer。

## 验证命令

按 `docs/DEVELOPMENT_ENVIRONMENT.md` 的生成器建议：

```powershell
cmd /d /c "call ""D:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S . -B build-nmake -G ""NMake Makefiles"" -DCMAKE_BUILD_TYPE=Release && cmake --build build-nmake && ctest --test-dir build-nmake --output-on-failure --timeout 60"
```

当前本机可用路径是 VS2026 Developer Command Prompt + NMake；其他机器可用
`cmake --help` 检查是否有更合适的 Visual Studio 2026 生成器。

后台跑普通单元测试时使用 60s 超时。大型 benchmark 必须显式 opt-in。
