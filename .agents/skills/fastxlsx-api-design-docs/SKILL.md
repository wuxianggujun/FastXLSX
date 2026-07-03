---
name: fastxlsx-api-design-docs
description: "设计或审查 FastXLSX public API、Doxygen 文档、任务计划和性能边界。用于 Workbook/WorkbookWriter/WorkbookEditor/WorksheetEditor 边界、Streaming/Patch/In-memory 模式说明、API 易用性与性能取舍，以及防止把 internal PackageEditor/EditPlan 写成 public API。"
---

# FastXLSX API Design Docs

## 必读入口

执行 FastXLSX API 设计、API 文档、任务拆分或性能边界审查前，先读这些文件：

- `docs/CURRENT_CAPABILITIES.md`：当前能力唯一事实源，维护 public API、internal foundation、planned / not yet public 和 explicit non-goals。
- `docs/API_DESIGN_AND_DOCUMENTATION.md`：API 设计原则、Doxygen 注释要求、Streaming/Patch/In-memory 模式边界。
- `docs/TASK_BREAKDOWN.md`：当前执行入口，只从 active queue 选择任务。
- `docs/ARCHITECTURE.md`：模块分层、OpenXML/OPC 底座、DOM 边界。
- `docs/EDITING_MODEL.md`：Streaming、Patch、In-memory 三路径和编辑语义。
- `docs/PERFORMANCE_TARGETS.md`：性能目标、热路径约束、benchmark 口径。
- `docs/TESTING_WORKFLOW.md`：本地验证、CTest、Excel / OpenXML QA 入口。
- `README.md`：用户视角的项目定位和快速入口。

再检查 `include/`、`src/`、`tests/`，确认符号、行为和测试是否真实存在。文档中的设计名、历史任务名或 future wording 不能直接当成已实现 API。

## 当前事实规则

- 当前能力以 `docs/CURRENT_CAPABILITIES.md` 为准；本 skill 不维护 API 矩阵或测试覆盖流水。
- `README.md`、`AGENTS.md`、`docs/API_DESIGN_AND_DOCUMENTATION.md` 和 `docs/TASK_BREAKDOWN.md` 只保留入口摘要；发现状态冲突时，优先修正事实源或让入口链接事实源。
- `docs/TASK_BREAKDOWN.md` 顶部 active queue 是任务入口；旧阶段设计已经从当前入口文档移除，不再作为默认任务来源。需要历史上下文时查 git history，而不是把历史编号重新写回入口。
- `PackageEditor`、`PackageReader`、`EditPlan`、`DependencyAnalyzer`、`PartRewritePlanner` 等只能按 internal foundation 描述，除非 public header 已明确暴露 facade。
- public `WorkbookEditor` / `WorksheetEditor` 的真实边界必须同时核对 public header、实现和 `docs/CURRENT_CAPABILITIES.md`；不要把 internal helper 名称包装成 public API。
- planned / not yet public 能力必须写成计划或设计边界，不能写成 roadmap 承诺或当前支持。
- explicit non-goals 必须保留否定边界，特别是 relationship repair、sharedStrings/styles 迁移、完整 object semantic editing、公式求值、calcChain rebuild 和低内存大文件随机编辑。

## API 模式

设计或审查任何 API 前，先标记它属于哪条路径：

- `Streaming`：新建 XLSX、大数据导出、多 sheet 批量写入。不能隐式持有完整 worksheet matrix 或 DOM。
- `Patch`：已有 XLSX 编辑、part-level rewrite、模板替换、unknown part preservation、audit / side-effect 可见性。不能把 internal package editor 直接宣称成 public API。
- `In-memory`：小文件复杂编辑和随机访问。必须写清 cell store、内存预算、guardrail 和 save-as / Patch handoff 边界；不得承诺大文件低内存。

如果 API 同时触碰多个模式，文档必须拆清入口、数据流和性能后果。

## Doxygen 要求

public header 中的 API 应有 Doxygen 风格注释。注释至少写清：

- API 所属模式。
- 输入生命周期和所有权，例如 `std::span` 是否只需调用期间有效。
- 是否保留完整 worksheet / workbook 状态。
- 输入顺序要求，是否允许回写历史行或随机访问。
- 内存随什么增长：行数、规则数、range 数、字符串长度、图片字节、decoded pixels、cell count 或 part size。
- OpenXML part 副作用，例如 worksheet `.rels`、drawing `.rels`、content types、workbook relationships、styles、sharedStrings、docProps、calc metadata。
- Patch API 会生成或暴露哪些 audit / side-effect，哪些 part copy-original、rewrite、remove 或 preserve。
- 是否设置 `fullCalcOnLoad`，是否移除、保留或拒绝 `calcChain.xml` rebuild。
- 错误边界、异常类型、失败是否在状态变更前发生。
- 性能边界和不支持项。

数值 API 必须写清 finite-only 边界，不要把 `NaN` / `Inf` 转成字符串、空单元格或 OpenXML 数字文本。

## 设计边界

- 易用 API 不能牺牲 streaming 热路径。
- 大型 worksheet、大型 sharedStrings、大型模板填充不能隐式 DOM 化。
- `Cell` / `CellValue` 这类 owning value 只能作为 API 边界值、临时值或小文件便利值；不要把它们当作百万级 worksheet 的内部长期存储模型。
- In-memory editor 必须有紧凑 cell store、估算入口、cell 计数和内存预算 guardrail。
- Patch API 必须明确 part-level rewrite、unknown part preservation、relationship/content type side effects、sharedStrings/styles/calc metadata 策略。
- `PackageEditor` / `EditPlan` 的审计能力只能说明 internal traceability，不能写成完整 metadata editor、relationship repair、object lifecycle editor 或 public surface。
- 图片、表格、批注、VBA、pivot、external links、custom XML 等 existing-file preservation 证据，只能写成 preservation / audit evidence，不能升级成语义编辑能力。
- 第三方库只能承担底层通用能力；XLSX 语义层由 FastXLSX 自己维护。

## 任务计划要求

规划 API 任务时，任务说明必须包含：

- `docs/TASK_BREAKDOWN.md` active queue 中的具体任务编号；没有编号的大任务先拆分。
- API 模式：Streaming、Patch、In-memory 或组合。
- public / internal / planned / non-goal 分类。
- 是否触碰性能热路径，是否需要 benchmark 或大文件内存验证。
- 是否需要 public Doxygen 注释。
- 需要哪些测试：单元测试、OpenXML 结构测试、Excel 可视化验证、fixture preservation、benchmark。
- 是否改变 CMake target、vcpkg manifest 或第三方依赖。
- Patch / editing 任务必须列出 EditPlan 或 equivalent audit 影响范围、unknown part preservation、relationship/content type side effects、sharedStrings/styles/calcChain 策略和 ReferencePolicy。

如果需求只说“更易用”，必须同时说明为什么不会导致大文件路径 DOM 化、跨行缓存或完整 cell matrix。

## 禁止事项

- 不要把旧阶段编号重新作为当前任务入口。
- 不要把 internal helper 名称写成 public API。
- 不要把 planned 能力写成已实现。
- 不要把 preservation 回归写成语义编辑支持。
- 不要让 `Workbook` 级便利 API 默认持有完整 worksheet。
- 不要把 Patch 做成 streaming writer 的事后补丁。
- 不要隐藏压缩等级、字符串策略、DOM 模式、图片解码内存、sharedStrings/styles 迁移等性能关键选择。
- 不要用“高性能”“低内存”“完整支持”这类模糊描述替代明确边界。

## 验证

- 用 `rg` 确认 public API 名称确实存在于 `include/fastxlsx/`。
- 用 `rg` 检查 internal 名称没有被写成 public surface。
- 文档入口必须能跳到 `docs/CURRENT_CAPABILITIES.md`。
- API 注释写清模式、内存行为、OpenXML side effects、失败边界和 non-goals。
- 文档修改只需要 `rg` / `git diff --check` 级别验证；除非改源码、CMake 或测试，否则不跑 C++ build / CTest。
