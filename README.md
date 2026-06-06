# FastXLSX

FastXLSX 是一个面向高性能 XLSX 处理的现代 C++ 库。

项目目标不是简单延续 `FastExcel` 的旧实现，而是重新设计一个
**流式优先、DOM 可选、面向 OpenXML 的 XLSX 引擎**。

## 定位

- 大数据写入路径：禁止依赖 DOM，必须流式生成 XML。
- 读取路径：使用 SAX / event parser，避免整表加载。
- 编辑路径：允许局部 DOM 或轻量对象模型，用于样式、工作簿元数据、
  关系文件、小型 XML part 和复杂局部修改。
- 未修改的 XLSX part：尽量原样透传，避免破坏图表、图片、宏和未知扩展。

## 设计原则

1. 写入性能优先
   - worksheet.xml 使用流式写入。
   - 单元格 XML 热路径直接写入字节流。
   - 压缩等级可配置。

2. 编辑能力保留
   - 小型 XML part 可以使用局部 DOM。
   - 大型 worksheet 通过事件流重写。
   - 修改已有文件时采用 part-level rewrite。

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
一个 C++20 / MSVC 2026 优先的高性能 XLSX 引擎，
面向 C++ OpenXLSX 高频功能，采用流式优先架构，
同时保留局部 DOM 编辑能力。
```

更详细的定位说明见：

- [项目定位](docs/PROJECT_POSITIONING.md)
- [开发环境](docs/DEVELOPMENT_ENVIRONMENT.md)
- [依赖策略](docs/DEPENDENCIES.md)
- [技术对比](docs/TECHNICAL_COMPARISON.md)
- [测试流程](docs/TESTING_WORKFLOW.md)
- [API 设计与文档注释](docs/API_DESIGN_AND_DOCUMENTATION.md)
- [后续推进清单](docs/NEXT_STEPS.md)

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
CTest preset 和测试属性保持 60s 边界。大型 benchmark 后续应作为显式 opt-in
目标，不进入默认 CTest。

生成的 `.xlsx` 若出现结构异常，按 [测试流程](docs/TESTING_WORKFLOW.md)：
用 Excel、`openpyxl` 或 `XlsxWriter` 生成语义等价参考文件，拆包后对比
`[Content_Types].xml`、relationships、workbook、worksheet、shared strings 和
styles 等 XML。

## 目录

```text
FastXLSX
├── docs
│   ├── API_DESIGN_AND_DOCUMENTATION.md
│   ├── ARCHITECTURE.md
│   ├── DEVELOPMENT_ENVIRONMENT.md
│   ├── DEPENDENCIES.md
│   ├── EDITING_MODEL.md
│   ├── PERFORMANCE_TARGETS.md
│   ├── PROJECT_POSITIONING.md
│   ├── ROADMAP.md
│   ├── TASK_PLAN.md
│   ├── TESTING_WORKFLOW.md
│   └── TECHNICAL_COMPARISON.md
├── include
│   └── fastxlsx
│       ├── detail
│       │   ├── opc.hpp
│       │   └── xml.hpp
│       ├── fastxlsx.hpp
│       ├── streaming_writer.hpp
│       └── workbook.hpp
├── src
│   ├── opc.cpp
│   ├── package_writer.cpp
│   ├── streaming_writer.cpp
│   ├── workbook.cpp
│   ├── xml.cpp
│   └── zip_store_writer.cpp
├── tests
│   ├── CMakeLists.txt
│   ├── test_minimal_xlsx.cpp
│   ├── test_opc.cpp
│   └── test_streaming_writer.cpp
└── CMakeLists.txt
```

## 推荐技术路线

- XML 大文件读写：SAX / event streaming。
- XML 小文件编辑：可选局部 DOM。
- ZIP/OPC：part 级别索引、复制、替换。
- 字符串：支持 inlineStr 与 sharedStrings 双策略。
- 样式：独立 registry，统一去重。

## 规划依赖

这些依赖记录在 `vcpkg.json` 的 planned features 中，当前默认构建和 CI
尚未接入或链接它们：

- ZIP/OPC 底层：`minizip-ng`。
- 压缩：`zlib-ng` 优先，保留 `zlib` fallback。
- 大型 XML 流式读取：`Expat`。
- 小型 XML DOM 编辑：`pugixml`。
- Phase 5 图片读取/插入解码：`stb`，当前只记录在 `planned-image`
  feature，尚未接入 CMake。
- 测试：`Catch2`。
- 性能基准：`Google Benchmark`。

`OpenXLSX` 和 `xlnt` 只作为参考库和 benchmark 对象，不作为 FastXLSX 的底层依赖。

## 当前状态

项目处于 Phase 1 最小可写 XLSX 的早期实现阶段。

当前已具备：

- compiled `fastxlsx` CMake target 和 `FastXLSX::fastxlsx` alias。
- 保守 `vcpkg.json`、`CMakePresets.json` 和 Windows VS2026/NMake CI workflow 基础。
- 最小 public API：`fastxlsx::Workbook`、`fastxlsx::Worksheet`、`fastxlsx::Cell`
  和 `fastxlsx::FastXlsxError`。
- 流式 writer 写入骨架：`WorkbookWriter`、`WorksheetWriter`、`CellView`。
- 最小 OpenXML package 输出：
  `[Content_Types].xml`、`_rels/.rels`、`xl/workbook.xml`、
  `xl/_rels/workbook.xml.rels`、`xl/worksheets/sheet1.xml`、
  `docProps/core.xml` 和 `docProps/app.xml`。
- 数字、inline string、布尔和公式单元格写入。
- `StringStrategy::SharedString` 最小写出路径、`xl/sharedStrings.xml` package wiring
  和结构测试；仍需 Excel 可视化、参考 XML 对比和内存/大小数据后再视为生产特性。
- 默认写出基础 `docProps/core.xml` 和 `docProps/app.xml` 小型 XML part；
  这只是静态文档属性元数据，不代表完整 document properties public API。
- 行高、列宽、冻结窗格、自动筛选和合并单元格的写入骨架。
- 内部 OPC manifest / relationships / `PartIndex` / `RelationshipGraph`
  基础；Phase 5 已有文件编辑仍是规划。
- 内部 package writer boundary：新建 workbook 输出通过 `src/package_writer.*`
  进入 ZIP backend。默认构建使用 stored/no-compression bootstrap；
  `FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建使用 minizip-ng DEFLATE backend。
- CTest 测试 `fastxlsx.unit`，覆盖 XML escape、cell reference、OpenXML 结构和
  基础单元格编码。
- CTest 测试 `fastxlsx.streaming`，覆盖当前流式 writer 写入骨架。
- CTest 测试 `fastxlsx.opc`，覆盖内部 OPC manifest、content types、
  relationships 和 XML serializer 基础。
- 本机已做 Excel 可视化验证并核对生成样例的关键单元格。

当前仍未完成：

- 将 minizip-ng backend 设为默认前的 CI/cache/release packaging 验证。
- Zip64、公开压缩等级配置和真正 package streaming。
- Catch2 和 Google Benchmark 接入。
- CI workflow 和 example 入口已有基础文件/分支，但仍需 GitHub 侧验证、完善和发布面确认。
- 完整 Phase 3 写入特性、完整 Phase 5 OPC 编辑能力和性能 benchmark。
- 图片、VBA、table 等复杂对象的完整读写/编辑支持。

`src/package_writer.*` 是当前内部 package writer 边界。默认无依赖构建仍调用
`src/zip_store_writer.*` Phase 1 bootstrap；opt-in minizip 构建会写出
DEFLATE-compressed ZIP entries。它仍不是 public package editing API，不要据此
宣称 Zip64、真正 package streaming、已有文件编辑或大文件性能。

## 许可证

FastXLSX 使用 MIT License，见 [LICENSE](LICENSE)。
