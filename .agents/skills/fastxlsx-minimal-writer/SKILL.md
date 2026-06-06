---
name: fastxlsx-minimal-writer
description: "实现或审查 FastXLSX Phase 1 最小可写 XLSX。用于 workbook、worksheet、content types、relationships、基础 sheetData、数字/字符串/布尔单元格、ZIP package 输出、minizip-ng/zlib-ng 接入、小型 XML part 生成、本机 Excel 可视化验证和参考文件拆包 XML 对比。"
---

# FastXLSX Minimal Writer

## 必读文件

- `docs/ROADMAP.md`
- `docs/ARCHITECTURE.md`
- `docs/DEPENDENCIES.md`
- `docs/EDITING_MODEL.md`
- `docs/TESTING_WORKFLOW.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `README.md`
- `CMakeLists.txt`
- `tests/CMakeLists.txt`

先确认当前源码状态。当前已有 Phase 1 最小写入实现：
`include/fastxlsx/workbook.hpp`、`src/workbook.cpp`、`src/xml.cpp`、
`src/zip_store_writer.cpp` 和 `tests/test_minimal_xlsx.cpp`。

## Phase 1 边界

Phase 1 的目标是生成 Excel / WPS / LibreOffice 可打开的最小 `.xlsx`。
功能范围来自 `docs/ROADMAP.md`：

- workbook。
- worksheet。
- content types。
- relationships。
- 基础 `sheetData`。
- 数字、字符串、布尔值。
- ZIP package 输出。
- `minizip-ng` / `zlib-ng` 接入。
- `pugixml` 接入小型 XML part。

不要在 Phase 1 顺手实现 Phase 3 的样式、合并单元格、冻结窗格、自动筛选等高级功能。

## 推荐实现顺序

1. 先建立最小 OPC package 结构：
   - `[Content_Types].xml`
   - `_rels/.rels`
   - `xl/workbook.xml`
   - `xl/_rels/workbook.xml.rels`
   - `xl/worksheets/sheet1.xml`
2. worksheet 写入走流式 XML，不引入 worksheet DOM。
3. 数字、字符串、布尔值分别建立最小编码路径。
4. 当前 ZIP backend 是内部 stored ZIP bootstrap；如果修改 ZIP 层，必须确认它仍
   只是临时例外，长期路线是 `minizip-ng` / `zlib-ng`。
5. 最小输出通过结构检查后，再补办公软件打开验证。
6. 新增 `.cpp` 时同步更新 `CMakeLists.txt` 的 `fastxlsx` 源文件列表。
7. public API 同步补文档注释，说明 streaming 模式、顺序写入限制和内存行为。

## 复用项目约定

- 大数据写入路径禁止 DOM。
- 小型 XML part 可以直接生成或局部 DOM。
- `OpenXLSX`、`xlnt`、`libxlsxwriter` 只能参考或 benchmark，不能作为实现底座。
- 通用 ZIP/压缩能力用成熟库；OpenXML/XLSX 语义层由 FastXLSX 自己实现。
- `src/zip_store_writer.*` 只是 Phase 1 bootstrap：stored/no compression、无 Zip64、
  无 package streaming，不要暴露为 public API 或声称具备压缩性能。

## 常用命令

```powershell
cmd /d /c "call ""D:\Program Files\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat"" -arch=x64 && cmake -S . -B build-nmake -G ""NMake Makefiles"" -DCMAKE_BUILD_TYPE=Release -DFASTXLSX_BUILD_TESTS=ON -DFASTXLSX_BUILD_EXAMPLES=OFF && cmake --build build-nmake && ctest --test-dir build-nmake --output-on-failure --timeout 60"
```

如果本机生成器名称不同，先用：

```powershell
cmake --help
```

## 禁止事项

- 不要为了最小可写而使用大型 worksheet DOM。
- 不要直接依赖 `OpenXLSX`、`xlnt` 或 `libxlsxwriter` 生成文件。
- 不要把性能目标写成已达成结论。
- 不要假设 Catch2 或 Google Benchmark 已经接入；当前测试是轻量 CTest 可执行文件。

## 验证

- 构建通过。
- `ctest` 能发现并运行新增测试；普通测试 60s 内完成。
- ZIP entry 和 OpenXML 基本 part 存在。
- 输出 `.xlsx` 能被 Excel / WPS / LibreOffice 打开；如果本机无这些工具，说明未验证。
- 本机有 Excel 时，用 Excel 打开关键样例做可视化验证。
- 当前 smoke 样例通常是 `build-nmake/tests/fastxlsx-phase1-minimal.xlsx`。
- 结构异常时，创建 Excel 或 Python XLSX 库参考文件，拆包对比 XML。
