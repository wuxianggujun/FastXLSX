# Existing Workbook Patch 产品化完善计划

## 文档定位

本文是 `WorkbookEditor` existing-file Patch facade 的产品化计划，用来把当前已有的
`replace_sheet_data()`、`replace_cells()`、`replace_image()`、`rename_sheet()`、
formula / defined-name audit、`request_full_calculation()` 和 `save_as()` 收敛为稳定、
可解释、可验证的已有文件编辑主线。

本文不是当前能力事实源。当前 public / internal / planned / non-goal 边界只以
[CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md) 为准。本文只描述后续如何完善、
如何拆任务、如何验收，以及哪些表述必须保持保守。

所属路径：

- 模式：Patch。
- Public facade：`WorkbookEditor`。
- 任务入口：`TASK_BREAKDOWN.md` 中的 F0、F2、C1、C2、C3。
- 核心约束：已有文件编辑必须走 part-level rewrite 和 preservation/audit 策略；
  internal `PackageEditor`、`EditPlan`、`DependencyAnalyzer`、`RelationshipGraph`
  只能作为实现和审计基础，不能写成 public API。

## 当前基线

当前 `WorkbookEditor` 已经提供 existing-file Patch facade：

- 打开已有 workbook 并暴露 source / planned catalog inspection。
- 查询 pending diagnostics、last edit error 和 worksheet edit summaries。
- 替换已有 worksheet 的 whole `<sheetData>`。
- 对已有 cells 做定点替换；显式 `CellPatchMissingCellPolicy::Insert` 只做 point upsert。
- 替换已有 PNG/JPEG `xl/media/*` part bytes。
- 改写 workbook sheet catalog 中的 sheet name。
- 暴露 formula / defined-name audit，支持请求 full recalculation。
- 使用 `save_as()` 输出新路径，不做 atomic in-place save。

这些能力说明 Patch facade 已经从 internal foundation 走到 public 入口，但还没有达到
“广义 Excel 语义编辑器”的完成度。差距主要在策略闭环：

- 复杂对象仍以 preserve / audit / fail 为主，不能直接宣称语义级编辑。
- sharedStrings、styles、formulas、range metadata、relationships 和 linked parts 的策略需要
  更清晰地暴露给调用方。
- 失败路径必须持续证明 failure-before-state-change 和 no-state-pollution。
- public diagnostics 需要足够解释行为，但不能泄漏 internal edit plan 结构作为稳定接口。
- `save_as()` 的输出语义、路径 guard、失败恢复和 dirty-session handoff 需要持续硬化。

## 非目标

本计划不包括以下能力，除非先经过新的 public API design gate：

- 不公开 package-level mutation API。
- 不公开 `PackageEditor`、`EditPlan`、`DependencyAnalyzer`、`RelationshipGraph`
  或 package-entry reason。
- 不做 atomic in-place save。
- 不把 `replace_image()` 写成 drawing / anchor / relationship 编辑或 existing-file image insertion。
- 不做 row/column shifting、range resize、table resize、filter resize、drawing sync 或 defined-name sync
  的 broad semantic editor。
- 不做 sharedStrings / styles broad migration、merge、repair 或 schema count repair。
- 不计算公式，不写 cached values，不 rebuild 完整 `calcChain.xml`。
- 不做 broad relationship repair / pruning、orphan cleanup 或 linked-part regeneration。
- 不把 preservation fixture 写成完整 object support。

## 模块边界

Public API 层：

- `include/fastxlsx/workbook_editor.hpp` 是唯一 public facade 入口。
- `WorkbookEditor` 只暴露 workbook-level Patch 操作、catalog view、diagnostics 和 `save_as()`。
- `WorksheetEditor` 是 In-memory 小文件路径，不应和 Patch facade 的 whole-sheet / targeted-cell
  操作在同一 sheet 上无约束混用。

Internal foundation：

- `PackageReader` 负责读取 package entries、content types、relationships 和 internal indexes。
- `PackageEditor` 负责 copy-original、part replacement、small XML rewrite、stream/chunk rewrite
  和 package write-out。
- `EditPlan` / `DependencyAnalyzer` / `ReferencePolicy` 负责 internal audit 和 conservative policy。
- `RelationshipGraph` / `PartIndex` 只用于 internal dependency 视图。

文档可以解释这些内部模块如何支撑 Patch，但 public 文案只能承诺 `WorkbookEditor` facade
可观察到的行为。

## 数据流

推荐 Patch 数据流保持如下：

```text
WorkbookEditor::open(source.xlsx)
-> PackageReader ingests package structure
-> public facade validates requested edit
-> internal Patch plan records changed parts and audits dependencies
-> unchanged parts copy-original
-> changed parts use generate-small-XML / local-DOM-rewrite / stream-rewrite
-> request calc metadata policy when needed
-> save_as(output.xlsx)
```

关键边界：

- 未修改 part 默认 copy-original。
- 修改 worksheet data 不等于理解或更新所有 worksheet-linked objects。
- 修改 workbook metadata 必须说明是否触发 content types、relationships、calc metadata side effects。
- 任何 relationship-bearing metadata 都必须先进入 preserve / audit / fail 决策，不能静默破坏。

## 状态流转

推荐 public facade 状态：

```text
opened clean
-> successful edit queued
-> planned catalog / diagnostics updated
-> save_as preflight
-> dirty WorksheetEditor sessions auto-flush if applicable
-> internal output plan materialized
-> output package written
-> editor remains reusable with observable clean/dirty diagnostics
```

失败策略：

- 参数、路径、catalog、relationship、payload 或 policy 失败应尽量发生在状态变更前。
- 失败不能污染 planned catalog、pending diagnostics、edit summaries、calc policy 或 staged output bytes。
- `save_as()` 失败后，下一次合法 `save_as()` 应能继续使用同一 editor 状态。

## 对象族策略

### sharedStrings

默认策略：preserve / audit / fail 优先。

- `replace_sheet_data()` 和 `replace_cells()` 不能假设 caller-provided text 已完成全局 sharedStrings 迁移。
- 使用 shared string indexes 的 replacement payload 必须提示调用方复核 `xl/sharedStrings.xml`。
- In-memory dirty save 的文本投影策略必须和 `WorksheetEditor` 文档保持一致，不写成 broad migration。

### styles

默认策略：preserve source handles，拒绝没有策略支撑的 broad style mutation。

- targeted-cell Patch 可以按当前 public 边界写入 caller 提供的 style id 或保留 materialized source handle。
- 不合并、不修复、不重排 existing styles。
- style id 是否有效只能在已有 guardrail 能验证的范围内描述。

### formulas and calcChain

默认策略：formula text / audit / recalc request。

- `request_full_calculation()` 只请求 Excel 打开后重算，并处理 stale calcChain metadata。
- `rename_sheet()` 的 formula policy 只覆盖当前已实现的窄边界。
- 不计算公式，不生成 cached values，不 rebuild 完整 calcChain。

### relationships and linked parts

默认策略：copy-original + dependency audit。

- worksheet `.rels`、drawing `.rels`、table parts、comments、charts、VBA、custom XML、pivots、
  external links 等必须以 owner / target / relationship type 为单位审计。
- 能保留的先保留；无法安全更新的进入 audit 或 fail。
- 不做 relationship pruning、orphan cleanup 或 target repair。

### images

当前 public `replace_image()` 只替换已有 PNG/JPEG media part bytes。

- 不创建新的 image part。
- 不编辑 drawing XML、anchor、picture metadata、relationships 或 content types。
- 不裁剪、旋转、压缩、转码或解码为 pixel buffer。

### tables, drawings, charts, comments, VBA, custom XML

默认策略：preserve / audit / fail，不能写成 semantic editing。

- 对象 part 原样保留是 preservation evidence，不等于支持对象模型编辑。
- 删除或替换关联 part 时，必须说明 inbound / outbound relationship 的处理策略。
- public semantic API 必须另过 C3 design gate。

## Workstreams

### WEP-1 Public diagnostics hardening

目标：让调用方能理解 queued edits、planned catalog、dirty state 和失败原因。

验收：

- public diagnostics 不暴露 internal `EditPlan` 结构。
- 同一 sheet 上 replace / materialize / rename 的混用规则清楚。
- failed edit 不更新 last successful diagnostics。

### WEP-2 Preservation policy matrix

目标：为每个对象族定义 preserve / audit / fail / edit 策略。

验收：

- sharedStrings、styles、formulas、tables、drawings、images、comments、VBA、custom XML、
  pivots、external links 都有明确策略。
- 每个策略都有 focused fixture 或明确的待补测试。

### WEP-3 `save_as()` hardening

目标：稳定输出路径、临时输出、失败恢复和 no-state-pollution。

验收：

- 拒绝空路径、源路径、目录路径和危险覆盖场景。
- 成功和失败后的 editor 状态可解释。
- dirty-session auto-flush 与 queued Patch edit 的顺序明确。

### WEP-4 Formula and name audit polish

目标：把 formula / defined-name audit 做成可靠的风险提示，而不是公式引擎。

验收：

- direct definedNames、materialized formula cells、unsupported references 分层报告。
- external workbook refs、3D refs、table structured refs 等不能误改。
- 文档明确“不求值、不写 cached values、不 rebuild calcChain”。

### WEP-5 Existing-file image replacement polish

目标：使 `replace_image()` 成为稳定的 media-byte replacement API。

验收：

- PNG/JPEG header、part name、path/span 生命周期、重复 replacement 最终生效规则清楚。
- drawing / anchor / relationship 不被误写。
- 原始未知 drawing metadata 保留。

## 阶段顺序

### Phase 0：事实收敛

- 核对 `CURRENT_CAPABILITIES.md`、public header 和测试证据。
- 清理任何把 internal package API 写成 public 的文案。

### Phase 1：public facade 行为稳定

- diagnostics、operation mixing、save_as guard 和 failure recovery。
- `replace_sheet_data()` / `replace_cells()` / `rename_sheet()` 的边界说明。

### Phase 2：preservation / audit 证据补齐

- 按对象族补 fixture 和 no-state-pollution。
- 明确 conservative fail policy。

### Phase 3：扩展决策门

- 任何 semantic object editing 或 relationship mutation API 先过 C3 gate。
- 才能进入 header / implementation / tests / docs。

## Definition of Done

existing-file Patch 产品化完成的最低标准：

- public `WorkbookEditor` 文档能解释每个 edit 的 part side effects、内存行为和失败边界。
- 每个对象族都有 preserve / audit / fail / edit 策略。
- unknown parts 和未修改 linked parts 默认保留。
- 失败路径有 no-state-pollution 验证。
- `save_as()` 路径 guard、失败恢复和 dirty-session handoff 有测试。
- formula / calc 文案只承诺 audit 和 fullCalcOnLoad，不承诺求值。
- `replace_image()` 只被描述为 media-byte replacement。
- README / API docs / `CURRENT_CAPABILITIES.md` 不漂移。

## 任务模板

```text
任务编号：
目标：
模式：Patch
Public facade：WorkbookEditor
输入：
输出：
触碰文件：
不触碰文件：
影响 parts：
preserve / audit / fail / edit 策略：
sharedStrings / styles / formulas / relationships 策略：
失败前置校验：
验收标准：
禁止项：
验证命令：
```

## 首批建议任务

### WEP-1 Catalog and diagnostics audit

梳理 source catalog、planned catalog、pending summaries、last edit error 和 dirty sessions 的公开语义，
补齐 public docs 和 focused tests。

### WEP-2 Object-family policy document/test map

把 tables、drawings、charts、comments、VBA、custom XML、pivots、external links 的现有 preservation
fixture 与缺口映射到 C2。

### WEP-3 Save-as failure recovery matrix

补齐 rejected path、write failure、dirty auto-flush failure、planned edit failure 后的状态验证。

### WEP-4 Replace-image boundary tests

强化 media bytes replacement、duplicate queued replacement、invalid format、missing part 和 drawing
preservation 的结构检查。

## 文档-only 验证

修改本文或任务入口但不改源码时，验证级别是文档检查：

```powershell
git diff --check -- docs/EXISTING_WORKBOOK_EDITING_PRODUCTIZATION_PLAN.md docs/TASK_BREAKDOWN.md
rg -n "EXISTING_WORKBOOK_EDITING_PRODUCTIZATION_PLAN|WEP-|WorkbookEditor|PackageEditor|EditPlan" docs/EXISTING_WORKBOOK_EDITING_PRODUCTIZATION_PLAN.md docs/TASK_BREAKDOWN.md
```

不需要运行 C++ build / CTest，除非同时修改 public header、source、CMake 或测试。
