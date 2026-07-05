# API 稳定性与 Public Wording Gate

## 文档定位

本文是 FastXLSX public API 设计、Doxygen、README、release wording 和能力事实源的门禁文档。
它用于防止把 planned 能力写成 current，把 internal foundation 写成 public API，或用模糊宣传词掩盖
性能和兼容性边界。

当前能力事实只以 [CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md) 为准。任何 public 文案、
README 示例、API docs、任务计划和 release notes 都必须能回链到该事实源或明确标记为 planned。

所属路径：

- 模式：Streaming / Patch / In-memory / Packaging 全部适用。
- Public facade：`WorkbookWriter`、`WorksheetWriter`、`Workbook`、`WorkbookEditor`、`WorksheetEditor`
  以及 value/helper types。
- 任务入口：`TASK_BREAKDOWN.md` 中的 C0、C3、C7。

## 基本规则

任何 public API 变更必须先写清：

- 所属模式：Streaming、Patch、In-memory，或组合。
- 所属 facade：`WorkbookWriter`、`WorksheetWriter`、`Workbook`、`WorkbookEditor`、`WorksheetEditor`。
- 输入生命周期和所有权。
- 内存行为。
- OpenXML part side effects。
- 失败是否发生在状态变更前。
- sharedStrings、styles、formulas、relationships、calcChain、unknown parts 策略。
- 测试和 QA 证据。
- 是否改变 `CURRENT_CAPABILITIES.md`。

如果这些问题答不清，不能进入 public header。

## Public / Internal 边界

可以写成 public API 的内容：

- `include/fastxlsx/` 中明确暴露的稳定入口。
- README / examples 中可由用户直接 include 和调用的类型。
- Doxygen 中作为 supported behavior 描述的 public facade 行为。

只能写成 internal foundation 的内容：

- `PackageReader`。
- `PackageEditor`。
- `EditPlan`。
- `DependencyAnalyzer`。
- `RelationshipGraph`。
- `PartIndex`。
- `PartRewritePlanner`。
- package-entry chunk helpers。
- test-only hooks。
- benchmark-only instrumentation。

可以解释 internal foundation 如何支撑 public facade，但不能写成用户可依赖接口。

## 模式门禁

### Streaming

必须确认：

- 是否只用于 new-workbook。
- 是否保持 row-order append。
- 是否会缓存历史 rows 或完整 worksheet matrix。
- metadata 是否 bounded by rule/object count。
- string strategy 是否显式。
- image/media bytes 是否避开 row hot path。

禁止表述：

- “支持随机修改流式 worksheet 历史行”。
- “Streaming writer 可编辑已有 XLSX”。
- “完整 Excel 样式 / 图表 / pivot 支持”。

### Patch

必须确认：

- 是否编辑 existing workbook。
- 目标 parts 是哪些。
- 未修改 parts 如何 preserve。
- relationships / content types side effects。
- sharedStrings / styles / formulas / calcChain policy。
- failure-before-state-change。

禁止表述：

- “public PackageEditor”。
- “完整 relationship repair”。
- “replace_image 支持插入图片或编辑 anchors”。
- “rename_sheet 会完整更新全 workbook 公式和对象引用”。

### In-memory

必须确认：

- 是否 small-file。
- 是否 materializes source worksheet。
- guardrails 是估算还是硬内存预算。
- sparse store 的 represented cells 语义。
- dirty-session `save_as()` handoff。

禁止表述：

- “大文件低内存随机编辑”。
- “完整 worksheet DOM 但仍低内存”。
- “自动同步 tables/drawings/defined names”。

## Doxygen 要求

public header 新增或修改 API 时，Doxygen 至少写清：

- 模式。
- 输入所有权和生命周期。
- 调用顺序要求。
- 内存随什么增长。
- OpenXML side effects。
- failure boundary。
- interaction with `save_as()` / `close()`。
- sharedStrings / styles / formula / relationships / calc metadata 策略。
- non-goals。

示例要求：

- 不使用 internal 类型。
- 不依赖本地绝对路径。
- 不展示未实现能力。
- 涉及中文文案时，源文件和文档保持 UTF-8。

## README / Release wording 门禁

推荐表述：

- “Streaming new-workbook writer supports ordered row/cell export with ...”
- “Existing workbook editing uses a Patch facade and preserves unknown parts where current policy allows.”
- “`WorksheetEditor` is for small-file materialized sparse editing with guardrails.”
- “Benchmark results are available for dataset X, backend Y, string strategy Z.”

禁止或需改写的表述：

- “完整支持 Excel”。
- “生产级大文件编辑”。
- “低内存编辑任意 XLSX”。
- “支持图表 / VBA / pivot 编辑”。
- “自动修复 relationships”。
- “公式计算支持”。
- “无损编辑所有复杂对象”。

改写方式：

- 把绝对词改成具体模式、对象、边界和证据。
- 把 planned 能力放入 roadmap / plan 文档。
- 把 non-goal 保留为否定边界。

## 数据流

public wording 的审核流：

```text
proposed API / wording
-> identify mode and facade
-> verify CURRENT_CAPABILITIES
-> verify public header symbols
-> verify implementation and tests
-> verify Doxygen and examples
-> verify performance / QA evidence if claimed
-> approve wording or downgrade to planned / non-goal
```

## 状态流转

能力状态只能按下面顺序推进：

```text
idea
-> plan
-> internal foundation
-> tested internal slice
-> public design gate
-> public header
-> implementation
-> tests / QA
-> docs / examples
-> current capability
```

不能从 internal foundation 直接跳到 current public capability。

## Workstreams

### APIG-1 Current capability drift scan

目标：检查 README、TASK_BREAKDOWN、API docs、plans 是否与 `CURRENT_CAPABILITIES.md` 漂移。

验收：

- current / planned / non-goal 表述一致。
- internal 名称未 public 化。

### APIG-2 Doxygen gap audit

目标：检查 public header 注释是否写清模式、内存、side effects 和 non-goals。

验收：

- 新增 public API 没有裸露无边界注释。
- 复杂 API 有 failure boundary。

### APIG-3 Release wording review

目标：发布前逐句审查 README / release notes。

验收：

- 每个性能词都有 benchmark evidence。
- 每个兼容性词都有结构测试或 QA。
- 未实现能力仍是 planned / non-goal。

### APIG-4 Public API proposal template

目标：为未来 public API 扩展提供统一模板。

验收：

- 所有 C3 gate 任务都能套用。
- 不能回答模板的问题不得进入实现。

## Review Checklist

提交 public API 或 public wording 前检查：

- 是否标明 Streaming / Patch / In-memory？
- 是否标明 public facade？
- 是否链接 `CURRENT_CAPABILITIES.md`？
- 是否误用了 internal 类型？
- 是否宣称了没有 benchmark / QA 的性能？
- 是否把 preservation 写成 semantic editing？
- 是否把 audit 写成 repair？
- 是否把 formula text / recalc request 写成 formula evaluation？
- 是否把 `save_as()` 写成 atomic in-place save？
- 是否把 small-file In-memory 写成 large-file low-memory？
- 是否同步 public header、implementation、tests、docs、examples？

## 阶段顺序

### Phase 0：建立 gate

- 新文档和任务入口链接本 gate。
- 后续 public API 任务必须填写模板。

### Phase 1：漂移扫描

- 检查 README、docs、AGENTS、skills 相关文案。
- 优先修正文档，不急于改代码。

### Phase 2：Doxygen 补齐

- 针对已存在 public API 补模式、内存、side effects、failure boundary。

### Phase 3：release wording

- 发布前逐句审查。

## Definition of Done

API 稳定性门禁完成的最低标准：

- 每个 public API 都能归到 Streaming / Patch / In-memory。
- public facade 与 internal foundation 不混淆。
- Doxygen 写清内存、side effects、失败和 non-goals。
- README / release notes 没有无证据的“完整、高性能、低内存”表述。
- `CURRENT_CAPABILITIES.md` 是唯一 current fact source。

## Public API Proposal 模板

```text
API 名称：
所属模式：
Public facade：
用户问题：
非目标：
输入所有权 / 生命周期：
内存行为：
OpenXML side effects：
sharedStrings 策略：
styles 策略：
formulas / calcChain 策略：
relationships / content types 策略：
unknown parts 策略：
失败前置校验：
测试计划：
QA / benchmark 计划：
Doxygen 要点：
README / example 影响：
CURRENT_CAPABILITIES 更新：
```

## 文档-only 验证

```powershell
git diff --check -- docs/API_STABILITY_AND_PUBLIC_WORDING_GATE.md docs/TASK_BREAKDOWN.md
rg -n "API_STABILITY_AND_PUBLIC_WORDING_GATE|APIG-|public PackageEditor|完整支持|低内存" docs/API_STABILITY_AND_PUBLIC_WORDING_GATE.md docs/TASK_BREAKDOWN.md
```

不需要运行 C++ build / CTest，除非同时修改 public header、source、CMake 或测试。
