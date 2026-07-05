# FastXLSX 任务拆分设计

## 目标

这份文档是执行层任务拆分入口，用来避免把历史大阶段直接交给一次实现。

`TASK_PLAN.md` 负责长期路线图，`NEXT_STEPS.md` 负责下一轮推进顺序；本文件负责把
当前优先级拆成可讨论、可验收、可并行的小任务。后续执行时，应先在本文件选定一个
最小任务，再进入设计或实现。

## 执行入口治理

新任务默认只从下方 active queue 选择最小可验收切片：`C0 -> C7` 和当前功能 lane 是执行入口。
旧条目不再保留在正文中；需要历史索引、设计证据和能力检索资料时查 git 历史。
不要继续把执行入口当作流水日志追加。

如果一个任务需要引用历史 P 编号，应同时写清它映射到哪个 active queue / lane、是否改变 public API、
是否触碰 Streaming / Patch / In-memory 边界，以及当前能力事实是否已同步到
[CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md)。

## 当前阶段任务重设计

当前不再保留旧阶段线性设计正文。后续执行应从下面的事实驱动队列选择最小任务；
需要历史上下文时先查 git 历史或 release notes，不把旧设计重新追加到本文件。

```text
C0 状态校准与验证基线
→ C1 当前 public WorkbookEditor Patch facade 硬化
→ C2 Patch preservation / dependency audit 缺口补齐
→ C3 public editor 扩展决策门
→ C4 In-memory random editor 内部到 public 的窄切片
→ C5 大 worksheet 低内存 rewrite 真正流式化
→ C6 ZIP/backend、sharedStrings、benchmark、streaming hot path 支撑线
→ C7 release / packaging / public wording gate
```

### 当前功能 / API 优先推进口径

执行入口只保留当前 active queue。历史 P 编号和旧设计正文已删除；当前能力事实看
[CURRENT_CAPABILITIES.md](CURRENT_CAPABILITIES.md)，长期方向看 `TASK_PLAN.md` / `NEXT_STEPS.md`。

功能 lane：
- F0 `WorkbookEditor` Patch facade：只收口 existing-file Patch 的 source/planned catalog、diagnostics、guardrails 和 `save_as()` 行为。
- F1 `WorksheetEditor` In-memory editor：只处理 small-file random cell editing，不扩展成 large worksheet 低内存随机访问。
- F2 Existing-file preservation policy：先定义 preserve / audit / fail / edit，再决定是否开放语义编辑。
- F3 Streaming writer polish：只做不破坏 row-order hot path 的 new-workbook API、示例和文档改进。
  细化产品化计划见 [STREAMING_WRITER_PRODUCTIZATION_PLAN.md](STREAMING_WRITER_PRODUCTIZATION_PLAN.md)。
- F4 Release evidence：只用 benchmark / Excel / openpyxl / CTest 证据支撑 release wording，不把实现细节写成性能承诺。

产品化计划索引：
- F0 / C1：[`EXISTING_WORKBOOK_EDITING_PRODUCTIZATION_PLAN.md`](EXISTING_WORKBOOK_EDITING_PRODUCTIZATION_PLAN.md)。
- F1 / C4：[`IN_MEMORY_EDITOR_PRODUCTIZATION_PLAN.md`](IN_MEMORY_EDITOR_PRODUCTIZATION_PLAN.md)。
- C5：[`LARGE_WORKSHEET_REWRITE_PLAN.md`](LARGE_WORKSHEET_REWRITE_PLAN.md)。
- C1 / C2 / C5 / C6：[`OPC_PACKAGE_FOUNDATION_PLAN.md`](OPC_PACKAGE_FOUNDATION_PLAN.md)。
- F2 / C2 / C3：[`COMPLEX_OBJECTS_SUPPORT_PLAN.md`](COMPLEX_OBJECTS_SUPPORT_PLAN.md)。
- F3 / C6：[`STREAMING_WRITER_PRODUCTIZATION_PLAN.md`](STREAMING_WRITER_PRODUCTIZATION_PLAN.md)。
- F4 / C6 / C7：[`PERFORMANCE_AND_RELEASE_EVIDENCE_PLAN.md`](PERFORMANCE_AND_RELEASE_EVIDENCE_PLAN.md)。
- C0 / C3 / C7：[`API_STABILITY_AND_PUBLIC_WORDING_GATE.md`](API_STABILITY_AND_PUBLIC_WORDING_GATE.md)。

### C0 状态校准与验证基线

目标：进入实现前核对 public header、当前事实源、README、API docs、CMake/CTest 和脏工作树。

验收：
- 当前 public/internal/planned/non-goal 状态已同步到 `docs/CURRENT_CAPABILITIES.md`。
- 入口文档只链接事实源，不复制长状态矩阵。
- 未把 internal `PackageEditor` / `EditPlan` / `DependencyAnalyzer` / `RelationshipGraph` 写成 public API。

### C1 当前 public `WorkbookEditor` Patch facade 硬化

目标：稳定 existing-file Patch facade 的可解释行为。

范围：
- source catalog / current planned catalog inspection。
- pending diagnostics、replacement guardrails、same-sheet operation mixing。
- `replace_sheet_data()`、`replace_cells()`、`replace_image()`、`rename_sheet()`、`request_full_calculation()` 和 `save_as()` 的边界说明。

禁止：
- 不公开 package-level mutation API。
- 不把 `replace_image()` 写成 drawing/anchor/relationship 编辑。
- 不承诺 sharedStrings/styles migration、formula evaluation 或 relationship repair。

### C2 Patch preservation / dependency audit 缺口补齐

目标：补 existing-file editing 的 preservation / audit / fail 证据，而不是扩大 marketing wording。

范围：
- unknown parts、linked parts、relationships、content types、calc metadata。
- sharedStrings、styles、tables、drawings、images、comments、VBA、custom XML 的 policy gap。

验收：
- 每个对象族都能说明 preserve / audit / fail / edit 策略。
- failure-before-state-change 和 no-state-pollution 有 focused 验证。

### C3 public editor 扩展决策门

目标：任何 public API 扩展先过设计门，再进实现。

必须回答：
- 属于 `WorkbookWriter`、`Workbook`、`WorkbookEditor` 还是 `WorksheetEditor`。
- 属于 Streaming、Patch 还是 In-memory。
- 是否会物化大 worksheet，是否触碰 row/cell 热路径。
- 对 sharedStrings、styles、formulas、relationships、range metadata 和 unknown parts 的策略。

### C4 In-memory random editor 小文件路径

目标：继续硬化 `WorksheetEditor` small-file In-memory editor。

范围：
- `WorksheetEditorOptions::max_cells` / `memory_budget_bytes` guardrails。
- sparse cell reads/writes、row/column convenience APIs、dirty-session `save_as()` handoff。
- style/sharedStrings/formula materialization 边界、failure recovery 和 diagnostics。

禁止：
- 不承诺 large-file low-memory random editing。
- 不做 broad style/sharedStrings migration、relationship repair 或 full metadata sync。

### C5 大 worksheet 低内存 rewrite 路径

目标：把大 worksheet 编辑维持在独立 low-memory rewrite 路径，不和 small-file In-memory 混用。

范围：
- worksheet event reader / transformer / chunk-source handoff。
- file-backed staged chunks、bounded payload validation、dependency audit 和 relationship-id audit。
- public low-memory transformer 是否开放必须另过 C3 gate。

禁止：
- 不把 internal transformer / chunk helper 写成 public API。
- 不把 file-backed implementation detail 写成已完成大文件性能承诺。

### C6 支撑线：ZIP/backend、sharedStrings、benchmark、streaming hot path

目标：支撑性能和 release 证据，不替代 API / editing 主线。

范围：
- package writer / reader guardrails、opt-in minizip-ng。
- sharedStrings evidence、styles/metadata streaming polish。
- benchmark runner、Excel/openpyxl smoke 和 hot-path regressions。

### C7 release / packaging / public wording gate

目标：发布前统一 API 稳定性、examples、README、Doxygen、CI 和兼容性证据。

验收：
- public surface 有 header、实现、测试、README 示例和 Doxygen。
- 当前事实源、README、AGENTS、API docs、TASK_BREAKDOWN 不互相漂移。
- 未实现能力保持 non-goals，不写成承诺。

## 执行规则

- 单个子任务必须有明确输入、输出、触碰文件和验收标准。
- 单个子任务不应同时修改 public API、内部 Patch 语义、测试 fixture 和文档总线。
- 文档设计任务不宣称运行时能力。
- 代码任务必须带对应结构测试；默认 CTest 仍需遵守 60s 边界。
- 涉及 public API 的任务，必须同步 Doxygen 注释、`API_DESIGN_AND_DOCUMENTATION.md`
  和本拆分文档。
- 涉及 existing-file editing 的任务，必须说明是否影响 relationships、content types、
  sharedStrings、styles、worksheet `.rels`、linked parts、calc metadata 和 unknown parts。

## 并行拆分建议

可以并行：
- C0 文档事实校准、C6 benchmark/supporting evidence 和 C7 release wording gate 的只读检查。
- C1 `WorkbookEditor` facade diagnostics、C2 preservation/audit gap 和 C3 public editor decision gate 的调研。
- 不同 feature 的 preserve / audit / fail / edit policy 分析，只要不同时改同一个 public facade。

必须串行：
- public API 扩展必须先经过 C3 决策门，再进入 C1/C4/C5 的具体实现切片。
- `WorkbookEditor` Patch facade、`WorksheetEditor` In-memory editor 和 large worksheet rewrite
  不能在同一子任务里同时扩大行为面。
- preservation / dependency audit 未覆盖前，不宣称 broad existing-file editing。
- In-memory guardrails 和 save-as handoff 未验证前，不扩大 small-file random editing 表述。
- 性能支撑线没有 benchmark / QA 证据前，不扩大 release wording。

## 每轮任务模板

后续每个执行任务应按这个模板写清楚：

```text
任务编号：
目标：
输入：
输出：
触碰文件：
不触碰文件：
可并行性：
验收标准：
禁止项：
验证命令：
```

如果任务不能填完这个模板，说明它还不是一个可以开始执行的工程任务。
