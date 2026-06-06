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
  `WorksheetWriter`、`CellView`、`FastXlsxError`。
- `WorkbookWriter` / `WorksheetWriter` / `CellView` 是当前流式 writer 写入骨架；
  公式、行高、列宽、冻结窗格、自动筛选和合并单元格属于当前写入骨架，
  不等同完整 Phase 3。
- `PackageReader`、`CellEncoder`、`SharedStringTable` 等名称仍主要是文档设计目标；
  除非源码中存在，否则不是已实现 API。
- OPC/Phase 5 当前仍是内部 manifest / relationships 基础和规划，不能宣称完整
  图片、VBA 或 table 支持。
- `src/zip_store_writer.*` 是内部 bootstrap ZIP writer，只写 stored/no-compression ZIP。

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
