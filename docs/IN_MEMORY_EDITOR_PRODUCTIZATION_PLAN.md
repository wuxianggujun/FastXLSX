# In-memory Worksheet Editor 产品化完善计划

## 文档定位

本文是 `WorksheetEditor` small-file In-memory editing path 的产品化计划。它用于完善
`WorkbookEditor::worksheet()` / `try_worksheet()` 返回的 `WorksheetEditor` 小文件随机编辑体验，
包括 sparse cell reads/writes、row/column convenience API、guardrails、dirty-session `save_as()`
handoff、failure recovery 和 source style/formula 边界。

本文不是当前能力事实源。当前状态只以
[CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md) 为准；本文描述后续如何完善和验收。

所属路径：

- 模式：In-memory。
- Public facade：`WorkbookEditor` + `WorksheetEditor`。
- 任务入口：`TASK_BREAKDOWN.md` 中的 F1、C4，以及和 C1/C2 交叉的 `save_as()` handoff。
- 核心约束：这是 small-file random cell editing，不是 large-file low-memory random editing。

## 当前基线

当前 `WorksheetEditor` 已经提供较完整的小文件 sparse editing 能力：

- materialize source worksheet 到 sparse store。
- `try_cell()`、`get_cell()`、`contains_cell()`、`used_range()`。
- `sparse_cells()`、`row_cells()`、`column_cells()` owning snapshots。
- `set_cell()` / `set_cells()` full sparse replacement。
- `set_cell_value()` / batch value-only writes，保留已有 materialized source `StyleId` handles。
- `append_row()`、`set_row()`、`set_column()`。
- clear / erase 系列 API。
- represented sparse row/column insert/delete。
- strict uppercase A1 convenience overloads。
- `cell_count()`、`estimated_memory_usage()`。
- dirty-session `save_as()` auto-flush。

当前边界同样明确：

- `max_cells` 和 `memory_budget_bytes` 是 sparse-store estimate guardrails，不是进程 RSS 保证。
- 非默认 caller-supplied `StyleId` 仍被拒绝，除非有 existing-workbook style policy。
- row/column shift 只移动 represented sparse records，不同步 tables、filters、validations、
  conditional formatting、drawings、defined names、relationships、sharedStrings/styles metadata 或 calcChain。
- 公式 rewrite 是窄范围 A1-style structural rewrite，不是完整 parser/evaluator。

## 非目标

以下能力不属于本计划：

- 不支持 large-file low-memory random editing。
- 不提供 dense worksheet matrix access。
- 不把 `WorksheetEditor` 扩展成完整 Excel object editor。
- 不做 broad sharedStrings / styles migration、merge、repair。
- 不做 relationship repair、orphan cleanup、linked-part regeneration。
- 不同步 tables、filters、validations、conditional formatting、drawings、defined names 或 comments。
- 不计算公式，不写 cached values，不 rebuild calcChain。
- 不做 transaction history、undo/redo 或 multi-session merge。

## 模块边界

Public API 层：

- `WorksheetEditor` 是 borrowed handle，生命周期受 owning `WorkbookEditor` 管理。
- `WorksheetEditorOptions` 控制 materialization guardrails。
- public snapshots 必须是 owning data，不能暴露内部 sparse-store references。

Internal foundation：

- `CellStore` / `CellRecord` 负责 sparse representation。
- materialized worksheet session 管理 dirty state、borrowed handles 和 save-as auto-flush。
- sheetData / full worksheet projection helpers 负责把 sparse store 投影回 Patch plan。

与 Patch 的关系：

- `WorksheetEditor` dirty state 最终通过 `WorkbookEditor::save_as()` 进入 Patch write-out。
- 同一 sheet 上 materialized session 和 `replace_sheet_data()` / `replace_cells()` 的混用必须受 public
  operation-mixing rules 约束。
- In-memory 路径可以复用 internal Patch foundation，但不能公开 internal EditPlan。

## 数据流

推荐数据流：

```text
WorkbookEditor::worksheet(sheet, options)
-> source worksheet materialization with max_cells / memory budget
-> CellStore sparse records
-> public random reads / writes / row-column operations
-> dirty session waits for save_as()
-> projection to sheetData / worksheet XML
-> Patch save_as writes output package
```

关键边界：

- materialization 是一次性读取 source worksheet，不是 streaming low-memory random access。
- sparse store 只记录 represented cells，不代表完整 worksheet grid。
- dirty projection 可以生成 sheetData，但不负责修复所有外围 metadata。
- memory estimate 只覆盖 sparse store 估算，不覆盖 source package bytes、generated XML chunks、
  `PackageEditor` staging files、ZIP writer buffers 或 save-time peak。

## 状态流转

推荐状态：

```text
editor opened
-> worksheet materialized clean
-> public mutations preflight
-> sparse store dirty
-> save_as auto-flush
-> projected Patch plan written
-> session clean but handle remains borrowed
```

失败策略：

- batch input 必须 preflight，失败时整个 batch 不变更 sparse store。
- guardrail failure 不能部分写入。
- failed `save_as()` 不能丢失 dirty session。
- successful `save_as()` 后，再次 acquire 同一 sheet 应复用或重新加载一致状态，行为需文档化。

## Workstreams

### IME-1 Guardrail and diagnostics polish

目标：让 `max_cells` / `memory_budget_bytes` 更可解释。

验收：

- source materialization 和后续 mutations 都受 guardrail 约束。
- diagnostics 区分 represented sparse cells、estimated memory、dirty state 和 save-time unknown cost。
- 文档明确不是 RSS 或 peak memory。

### IME-2 Batch operation consistency

目标：统一 `set_cells()`、value-only writes、row/column writes、clear/erase 和 shift 的 preflight 行为。

验收：

- duplicate coordinates 的 later-wins 语义清楚。
- batch 失败不污染 sparse store。
- row/column 上限、Excel coordinate 上限和 invalid style handle 有 focused tests。

### IME-3 Dirty-session save_as handoff

目标：保证 dirty In-memory edits 与 Patch write-out 的顺序和失败恢复稳定。

验收：

- dirty session auto-flush 与 queued Patch edits 的混用规则清楚。
- failed save 不丢 session，不清 dirty flag。
- successful save 后 diagnostics 和 reacquire 行为一致。

### IME-4 Formula narrow rewrite boundary

目标：把 row/column shift 对 materialized formulas 的支持做成窄而可靠的能力。

验收：

- supported A1 references 有测试。
- unsupported references 保守保留或 audit，不误改。
- 不宣称 formula evaluation、cached values 或 full workbook formula rewrite。

### IME-5 Style and sharedStrings boundary

目标：稳定 source style handle preservation 与 text projection。

验收：

- value-only writes 保留目标 source style handle。
- full sparse replacement 按当前 public 语义丢弃目标 source style handle。
- 非默认 caller-supplied `StyleId` 在没有 policy 前继续拒绝。
- sharedStrings 投影策略写清，不写成 broad migration。

### IME-6 Snapshot and traversal ergonomics

目标：让小文件随机编辑更容易使用，同时不暴露内部可变引用。

验收：

- `sparse_cells()`、`row_cells()`、`column_cells()` 的 ordering、ownership、成本明确。
- A1 overloads 的 uppercase / strict 规则清楚。
- 大范围 traversal 不被写成大文件性能 API。

## 阶段顺序

### Phase 0：边界收敛

- 在所有入口文案中强调 small-file random editing。
- 去除任何 large-file low-memory random editing 暗示。

### Phase 1：guardrails 和 failure recovery

- materialization guard、mutation guard、batch preflight。
- save_as dirty-session recovery。

### Phase 2：row/column convenience 稳定

- insert/delete/clear/erase/set 的组合行为。
- formula narrow rewrite 和 style handle preservation。

### Phase 3：API 易用性小补强

- snapshot traversal、diagnostics、examples。
- 只补不破坏现有 sparse-store 模型的便利 API。

## Definition of Done

In-memory editor 产品化完成的最低标准：

- 文档和 Doxygen 明确 small-file / materialized / sparse-store 边界。
- guardrails 对 source materialization 和 mutations 都有测试。
- batch failure-before-state-change 可验证。
- dirty-session `save_as()` 成功和失败状态明确。
- row/column operations 的公式窄 rewrite、style handle movement 和 metadata non-sync 都写清。
- public examples 不把该路径用于大文件低内存编辑。
- `CURRENT_CAPABILITIES.md` 与 public header、API docs 不漂移。

## 任务模板

```text
任务编号：
目标：
模式：In-memory
Public facade：WorkbookEditor / WorksheetEditor
输入：
输出：
触碰文件：
不触碰文件：
sparse-store 影响：
guardrail 策略：
save_as handoff：
formula / style / sharedStrings 边界：
失败策略：
验收标准：
禁止项：
验证命令：
```

## 首批建议任务

### IME-1 Guardrail wording and focused tests

补齐 materialization、batch mutation、row/column mutation 的 guardrail 文档和验证。

### IME-2 Dirty-session save_as recovery table

把 no-op save、failed save、successful save、reacquire、rename interaction 形成可执行测试矩阵。

### IME-3 Formula shift audit

整理 supported / unsupported formula references 的测试和文档，不扩大到公式引擎。

### IME-4 Style handle preservation QA

验证 value-only、full replacement、shift、clear、erase 对 source style handle 的不同语义。

## 文档-only 验证

```powershell
git diff --check -- docs/IN_MEMORY_EDITOR_PRODUCTIZATION_PLAN.md docs/TASK_BREAKDOWN.md
rg -n "IN_MEMORY_EDITOR_PRODUCTIZATION_PLAN|IME-|WorksheetEditor|large-file low-memory" docs/IN_MEMORY_EDITOR_PRODUCTIZATION_PLAN.md docs/TASK_BREAKDOWN.md
```

不需要运行 C++ build / CTest，除非同时修改 public header、source、CMake 或测试。
