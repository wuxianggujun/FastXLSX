---
name: fastxlsx-project-navigation
description: "导航 FastXLSX 项目架构和当前实现状态。用于理解修改位置、判断文档中的 API/模块是否已实现、调整公共架构、添加核心模块、审查流式优先 OpenXML 设计边界，或避免把路线图内容误当作现有代码。"
---

# FastXLSX Project Navigation

## 必读文件

在架构级或模块级修改前，先读：

- `README.md`
- `docs/PROJECT_POSITIONING.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`
- `docs/TESTING_WORKFLOW.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ROADMAP.md`
- `CMakeLists.txt`

再检查 `include/`、`src/`、`tests/`，确认 API 或模块是否真实存在。
当前已有 Phase 1 最小写入实现；不要再假设 `include/`、`src/`、`tests/`
仍是空骨架。

## 项目事实

- FastXLSX 是 C++20 / MSVC 2026 优先的 XLSX 引擎。
- 项目方向是流式优先、局部 DOM 可选、面向 OpenXML。
- 当前是 Phase 1 最小可写 XLSX 的早期实现，不是完整 XLSX 引擎。
- CMake 目标 `fastxlsx` 是 compiled library，别名是 `FastXLSX::fastxlsx`。
- 已实现 public API：`Workbook`、`Worksheet`、`Cell`、`WorkbookWriter`、
  `WorksheetWriter`、`CellView`、`StyleId`、`CellStyle`、`DocumentProperties`、`DataValidationRule`、
  `DataValidationErrorStyle`、`ArgbColor`、`ColorScaleValueType`、`ColorScalePoint`、
  `TwoColorScaleRule`、`ThreeColorScaleRule`、`DataBarValueType`、`DataBarEndpoint`、
  `DataBarRule`、`TableOptions`、`TableTotalsFunction`、
  `ImageFormat`、`ImageInfo`、`ImageEditAs`、`ImageOptions`、`read_image_info()`、
  `WorkbookWriter::add_style()`、`CellView::with_style()`、
  `WorksheetWriter::add_conditional_color_scale()`、`WorksheetWriter::add_conditional_data_bar()`、
  `WorksheetWriter::add_image()`
  和 `FastXlsxError`；`Workbook::set_document_properties()` 和
  `WorkbookWriterOptions::document_properties` 是当前基础 docProps 配置入口。
- `WorkbookWriter` / `WorksheetWriter` / `CellView` 是当前流式 writer 写入骨架；
  公式、行高、列宽、冻结窗格、自动筛选和合并单元格属于当前写入骨架，
  不等同完整 Phase 3。
- 当前写出基础可配置 `docProps/core.xml` 和 `docProps/app.xml` 小型 XML part；
  这不是 `docProps/custom.xml`、existing-file editing 或完整 document properties
  public API。
- 当前已有 P9a streaming-only number-format styles 基础切片：`StyleId` 是
  workbook-local handle，`CellStyle` 当前只支持 `number_format`，`WorkbookWriter::add_style()`
  会注册样式并让 `close()` 写 `xl/styles.xml`、workbook styles relationship 和 content
  type override，`CellView::with_style()` 让 cell 写 `s="N"`。这不是完整 styles、
  font/fill/border/alignment、date cell type、dxf-backed conditional formatting、
  rich text 或 existing-file style preservation。当前 two-/three-color color scale 和
  basic data bar 是 worksheet metadata，不是 styles registry 或 `dxfs` 支持。
- `PackageReader`、`CellEncoder` 等名称仍主要是文档设计目标；
  除非源码中存在，否则不是已实现 API。
- OPC 当前已有内部 manifest / relationships / `PartIndex` / `RelationshipGraph`
  / content type registry 基础；Phase 5 已有 data validations（含 worksheet-only
  prompt/error attributes）、external/internal hyperlinks、two-/three-color conditional color
  scales、basic conditional data bars、streaming-only tables 和
  `WorksheetWriter::add_image()`（含 drawing-only `ImageOptions` edit_as/name/description metadata）
  等基础切片，但不能宣称完整 data validation UI、
  conditional formatting、图片、VBA、table、drawing 编辑或 existing-workbook 保真支持。
- `src/package_writer.*` 是内部 package writer boundary；默认 backend 是
  `src/zip_store_writer.*` bootstrap ZIP writer，opt-in backend 是
  `minizip-ng[core,zlib]` DEFLATE。它仍不是 public existing-file editing API。

## 架构边界

- 新建 XLSX 和大数据写入必须使用 XML streaming。
- 大型 `worksheet.xml`、大型 `sharedStrings.xml`、批量导出、大型模板填充禁止 DOM。
- 小型 XML part 可以使用局部 DOM：`workbook.xml`、关系文件、
  `[Content_Types].xml`、`docProps/*.xml`、较小的 `styles.xml`，
  以及规划中的小型 drawing/comments/table part；这只是边界描述，不代表当前
  完整支持图片、VBA 或 table 编辑。
- 编辑已有 XLSX 时使用 part-level rewrite：未修改 part 原样复制，
  修改 part 才重新生成，未知 part 默认保留。
- 第三方库只负责通用底层能力；XLSX 语义层由 FastXLSX 自己实现。

## 推荐流程

1. 判断任务是在改已实现代码，还是只在补架构/规划文档。
2. 如果要新增实现，把模块归入文档分层：`api`、`opc`、`xml`、
   `workbook`、`worksheet`、`style`、`strings`。
3. 保持大数据路径以 iterator/chunk 为主，不引入完整 worksheet cell matrix。
4. 如果新增 `.cpp` 文件，必须同步更新 `CMakeLists.txt` 的 `fastxlsx` 源文件列表。
5. 如果新增测试，接入已有 `FASTXLSX_BUILD_TESTS` 路径。

## 复用入口

- `docs/ARCHITECTURE.md`：分层、数据流、DOM 边界。
- `docs/EDITING_MODEL.md`：Streaming/Patch/In-memory 三种模式。
- `docs/ROADMAP.md`：阶段边界，避免把后期功能提前当成当前目标。
- `README.md`：项目定位和推荐技术路线。

## 禁止事项

- 不要把文档 API 示例当作当前 C++ 符号。
- 不要把 `OpenXLSX`、`xlnt`、`libxlsxwriter`、`QXlsx` 包装成 FastXLSX 底座。
- 不要让完整 workbook/worksheet DOM 成为大数据默认路径。
- 不要在编辑流程里无故丢弃未知 XLSX part。

## 验证

- 用 `rg --files -g '!build/**'` 确认真实文件结构。
- CMake 修改后按 `docs/DEVELOPMENT_ENVIRONMENT.md` 的生成器建议配置和构建。
- 当前本机可用验证路径是 VS2026 Developer Command Prompt + `NMake Makefiles`。
- 文档修改后确认引用的文件真实存在，并明确区分“当前事实”和“规划/目标”。
