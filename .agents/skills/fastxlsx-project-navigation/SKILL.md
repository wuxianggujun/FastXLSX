---
name: fastxlsx-project-navigation
description: "导航 FastXLSX 项目架构和当前实现状态。用于理解修改位置、判断 API/模块是否已实现、审查 Streaming/Patch/In-memory 三路径、区分 public surface 与 internal foundation，并避免把旧设计或路线图内容误当作当前代码。"
---

# FastXLSX Project Navigation

## 必读入口

架构级、模块级、文档治理或实现状态判断前，先读：

- `docs/CURRENT_CAPABILITIES.md`：当前能力唯一事实源。
- `README.md`：用户入口和快速示例。
- `AGENTS.md`：agent 快速上下文、禁忌和执行口径。
- `docs/TASK_BREAKDOWN.md`：当前 active queue。
- `docs/API_DESIGN_AND_DOCUMENTATION.md`：API 设计和 Doxygen 规则。
- `docs/ARCHITECTURE.md`：模块分层、OpenXML/OPC 底座和 DOM 边界。
- `docs/EDITING_MODEL.md`：Streaming、Patch、In-memory 三路径。
- `docs/PERFORMANCE_TARGETS.md`：性能目标和热路径限制。
- `docs/TESTING_WORKFLOW.md`：本地验证方式。
- `CMakeLists.txt`、`CMakePresets.json`：构建入口。

然后用 `rg` 检查 `include/`、`src/`、`tests/`。不要从旧设计文档、历史任务名或示例名字推断当前实现。

## 当前事实读取顺序

1. 先看 `docs/CURRENT_CAPABILITIES.md`，区分 `Public API`、`Internal Foundation`、`Planned / Not Yet Public` 和 `Explicit Non-goals`。
2. 再看 public headers，确认用户可见符号。
3. 再看 `src/`，确认实现入口、internal helper 和 CMake 接入。
4. 再看 `tests/`，确认回归覆盖和行为边界。
5. 最后看设计文档，理解方向和约束。

如果事实源、源码和测试不一致，不能直接猜测结论；要指出冲突，并给出需要更新的文件或验证命令。

## 项目定位

- FastXLSX 是 C++20 / MSVC 2026 优先的可编辑高性能 XLSX 读写与编辑库。
- 核心方向是共享 OpenXML/OPC 底座，提供 `Streaming`、`Patch`、`In-memory` 三条 API 路径。
- Streaming 是大文件性能主线；Patch / editing 也是核心主线，不能被降级为 writer 的附属补丁。
- 当前任务从 `docs/TASK_BREAKDOWN.md` active queue 选择，默认按 `C0 -> C7` 推进。
- 旧阶段设计已经不再作为当前任务入口；需要历史上下文时查 git history，不要把旧编号重新写回入口文档。

## 模块导航

- `include/fastxlsx/`：public API surface。只有这里明确暴露的符号才能写成 public API。
- `include/fastxlsx/detail/`：internal detail。可以说明 internal foundation，不要写成用户承诺。
- `src/workbook.*`、`src/streaming_writer.*`：new-workbook / streaming writer 主流程。
- `src/package_writer.*`、`src/zip_store_writer.*`：package writer / ZIP backend 边界。
- `src/package_reader.*`：internal ZIP reader 和 OPC metadata ingestion。
- `src/package_editor.*`：internal existing-package copy / replace / Patch foundation。
- `src/opc.*`：internal OPC manifest、relationships、content types、part model。
- `src/image.*`：图片 metadata / pixel helper。
- `tests/`：行为事实和边界证据；测试覆盖不能自动升级成 public API。
- `examples/`：示例入口。除非本轮验证 examples，否则不要把示例目标当作发布承诺。

## 三路径边界

- `Streaming`：新建 workbook、row/chunk 写入、大文件导出。禁止隐式 DOM 化大型 worksheet、sharedStrings 或批量数据。
- `Patch`：已有 workbook 编辑、part-level rewrite、unknown part preservation、audit visibility。internal `PackageEditor` / `EditPlan` 不能被包装成 public API。
- `In-memory`：小文件随机编辑。必须受 cell store、内存预算、guardrail 和 save-as / handoff 边界约束。

跨路径功能必须明确数据流。例如 public editor facade 可以复用 internal Patch foundation，但文档要写清 public facade、internal helper、planned 能力和 non-goals。

## 架构边界

- 大型 XML part 走 streaming 或 chunked rewrite；小型 metadata part 才允许局部 DOM。
- 编辑已有 XLSX 时，未修改 part 默认 copy-original，修改 part 才 rewrite，unknown part 默认保留。
- relationship/content type 变化必须写成窄 helper side effect 或 audit，不能泛化成完整 repair。
- sharedStrings/styles 索引迁移、公式求值、calcChain rebuild、object semantic editing、sheet catalog mutation、relationship pruning/orphan cleanup 都必须按事实源区分当前状态和 non-goal。
- 第三方库只负责底层通用能力；XLSX 语义层不外包。

## 搜索与文件读取

- 优先使用 `rg` / `rg --files`。
- 工作目录保持在仓库根目录或更小子目录。
- 默认排除 `.git/`、`build/`、`out/`、`dist/`、`node_modules/`、`.cache/`、`.tmp/`、`coverage/`、`artifacts/` 等生成物和缓存目录。
- 如果仓库存在 `.ignore`，搜索必须遵守 `.ignore`。
- 只有明确排查构建产物、日志、缓存或资源文件时才进入这些目录，并且只读明确目标。

## 修改位置判断

- public API 变化：先看 `include/fastxlsx/*.hpp`，再看对应 `src/*.cpp` 和 tests。
- internal foundation 变化：优先放在 `include/fastxlsx/detail/`、`src/` 中合适模块，避免泄露到 public header。
- writer feature 增长明显时，考虑 feature-specific helper / `.cpp` / test，而不是继续堆进 `src/streaming_writer.cpp` 和 `tests/test_streaming_writer.cpp`。
- 新增 `.cpp` 或测试文件时，同步更新 `CMakeLists.txt` 或 `tests/CMakeLists.txt`。
- 文档治理只改文档；除非用户明确要求，不顺手改源码或测试。

## 推荐流程

1. 判断任务是实现、文档、测试、构建还是审查。
2. 读取 `docs/CURRENT_CAPABILITIES.md` 和 active queue，确定当前事实和任务入口。
3. 用 `rg` 确认符号、文件和测试是否存在。
4. 按 Streaming / Patch / In-memory 标记模式和性能边界。
5. 最小化修改范围，保持现有风格。
6. 做匹配风险的验证：文档用 `rg` / `git diff --check`；源码按 `docs/TESTING_WORKFLOW.md` 选择 CMake / CTest。

## 禁止事项

- 不要把旧设计或历史任务编号当作当前执行入口。
- 不要把文档示例当作真实 C++ 符号。
- 不要把 `PackageEditor`、`EditPlan`、`PackageReader` 等 internal 类型写成 public API。
- 不要把 preservation / audit 测试写成完整语义编辑能力。
- 不要把 planned / future 能力写成已实现。
- 不要让完整 workbook/worksheet DOM 成为大数据默认路径。
- 不要在编辑流程里无故丢弃 unknown part。
- 不要把 OpenXLSX、xlnt、libxlsxwriter、QXlsx 等参考库包装成 FastXLSX 底座。

## 验证

- 用 `rg --files` 确认真实文件结构。
- 用 `rg` 检查关键符号在 public header、src 和 tests 中的位置。
- 文档修改后确认入口链接到 `docs/CURRENT_CAPABILITIES.md`，并明确区分当前事实、internal foundation、planned 能力和 non-goals。
- CMake 或源码修改后按 `CMakePresets.json` 和 `docs/TESTING_WORKFLOW.md` 运行匹配验证。
- 文档-only 任务不运行 C++ build / CTest，除非用户要求或文档变更涉及构建指令真实性。
