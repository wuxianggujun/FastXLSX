---
name: fastxlsx-minimal-writer
description: "实现或审查 FastXLSX 最小可写 XLSX。用于 workbook、worksheet、content types、relationships、基础 sheetData、数字/字符串/布尔单元格、ZIP package 输出、默认 stored bootstrap 与 opt-in minizip-ng/zlib DEFLATE backend、小型 XML part 生成、本机 Excel 可视化验证和参考文件拆包 XML 对比。"
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

先确认当前源码状态。当前已有最小写入实现：
`include/fastxlsx/workbook.hpp`、`src/workbook.cpp`、`src/xml.cpp`、
`src/opc.cpp`、`src/package_writer.cpp`、`src/zip_store_writer.cpp` 和
`tests/test_minimal_xlsx.cpp`。
当前写出基础可配置 `docProps/core.xml` 和 `docProps/app.xml` 小型 XML part；
`DocumentProperties`、`Workbook::set_document_properties()` 和
`WorkbookWriterOptions::document_properties` 只覆盖 new-workbook core/app metadata，
不是完整 document properties public API。

## 最小写入边界

最小写入目标是生成 Excel / WPS / LibreOffice 可打开的 `.xlsx`。
功能范围来自 `docs/ROADMAP.md`：

- workbook。
- worksheet。
- content types。
- relationships。
- 基础 `sheetData`。
- 数字、字符串、布尔值。
- ZIP package 输出。

当前 package writer 边界：

- default internal stored ZIP writer。
- opt-in minizip-ng DEFLATE backend via `FASTXLSX_ENABLE_MINIZIP_NG=ON`。
- internal package writer boundary around both backends。
- no Zip64。
- no package streaming。
- no `zlib-ng` / `pugixml` runtime use in current source。

后续依赖工作：

- minizip backend 默认化前的 CI/cache/release packaging 验证。
- 压缩等级配置、Zip64 策略和真正 package streaming。
- `pugixml` for small XML part editing。

不要在最小 writer 任务中顺手实现样式、合并单元格、冻结窗格、自动筛选等更高层功能。

## 推荐实现顺序

1. 先建立最小 OPC package 结构：
   - `[Content_Types].xml`
   - `_rels/.rels`
   - `xl/workbook.xml`
   - `xl/_rels/workbook.xml.rels`
   - `xl/worksheets/sheet1.xml`
   - `docProps/core.xml`
   - `docProps/app.xml`
2. worksheet 写入走流式 XML，不引入 worksheet DOM。
3. 数字、字符串、布尔值分别建立最小编码路径。
4. ZIP 层必须继续经过 `src/package_writer.*`；默认 backend 是 stored bootstrap，
   opt-in backend 是 `minizip-ng[core,zlib]`。不要让 workbook/worksheet writer
   直接依赖 ZIP 细节。
5. 最小输出通过结构检查后，再补办公软件打开验证。
6. 新增 `.cpp` 时同步更新 `CMakeLists.txt` 的 `fastxlsx` 源文件列表。
7. public API 同步补文档注释，说明 streaming 模式、顺序写入限制和内存行为。

## 复用项目约定

- 大数据写入路径禁止 DOM。
- 小型 XML part 可以直接生成或局部 DOM。
- `OpenXLSX`、`xlnt`、`libxlsxwriter` 只能参考或 benchmark，不能作为实现底座。
- 通用 ZIP/压缩能力用成熟库；OpenXML/XLSX 语义层由 FastXLSX 自己实现。
- `src/package_writer.*` 只是内部 package writer 边界。默认 backend 是
  `src/zip_store_writer.*` stored ZIP bootstrap；opt-in backend 是 minizip-ng/DEFLATE。
  不要暴露为 public API 或声称具备 Zip64、true package streaming 或大文件性能。

## 常用命令

```powershell
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

如果本机生成器名称不同，先用：

```powershell
cmake --help
```

## 禁止事项

- 不要为了最小可写而使用大型 worksheet DOM。
- 不要直接依赖 `OpenXLSX`、`xlnt` 或 `libxlsxwriter` 生成文件。
- 不要把性能目标写成已达成结论。
- 不要把基础 docProps 输出写成完整文档属性 API；当前只覆盖 core/app 小型 XML
  part，不生成 `docProps/custom.xml`，也不编辑已有文件。
- 不要假设 Catch2 或 Google Benchmark 已经接入；当前测试是轻量 CTest 可执行文件。

## 验证

- 构建通过。
- `ctest` 能发现并运行新增测试；普通测试 60s 内完成。
- ZIP entry 和 OpenXML 基本 part 存在。
- `fastxlsx.unit` 结构测试覆盖基础 cell 编码、公式 XML escape、row height metadata、
  空 worksheet / 单空行 dimension、`XFD1` 最大列和 16385 列拒绝路径。
- minizip backend 变更时，测试必须读取解压后的 entries，不要假设 ZIP method 0。
- 输出 `.xlsx` 能被 Excel / WPS / LibreOffice 打开；如果本机无这些工具，说明未验证。
- 本机有 Excel 时，用 Excel 打开关键样例做可视化验证。
- 当前推荐 preset smoke 样例通常是
  `build/windows-nmake-release/tests/fastxlsx-phase1-minimal.xlsx`；旧
  `build-nmake/tests/*.xlsx` 可能是过期 artifact，人工验证前必须确认重新生成。
- 结构异常时，创建 Excel 或 Python XLSX 库参考文件，拆包对比 XML。
