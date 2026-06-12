# FastXLSX 任务拆分设计

## 目标

这份文档是执行层任务拆分入口，用来避免把 `P4`、`P7` 这类大任务直接交给一次实现。

`TASK_PLAN.md` 负责长期路线图，`NEXT_STEPS.md` 负责下一轮推进顺序；本文件负责把
当前优先级拆成可讨论、可验收、可并行的小任务。后续执行时，应先在本文件选定一个
最小任务，再进入设计或实现。

## 当前阶段任务重设计

截至 2026-06-12，旧的 `P3 → P4.0 → P4 → P5 → P6 → P7 → P8`
线性顺序已经不能再作为下一步执行入口：这些阶段中的多个基础切片已经落地。
后续执行应从下面的事实驱动队列选择最小任务，旧 P 编号只作为历史索引和能力归档。

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

### C0 状态校准与验证基线

目标：每次进入实现前，先确认文档、public header、CMake、CTest 和当前脏工作树的事实一致。

当前事实：
- `WorkbookEditor` 已是 public 窄 Patch facade，不再是纯 future 名称。
- `PackageReader` / `PackageEditor` / `EditPlan` / dependency audits 仍是 internal。
- `CellValue`、internal `CellStore`、`CellStoreOptions`、standalone `<sheetData>`
  emission 和 `CellStore` 到 by-name `PackageEditor` handoff 已有基础。
- P8 event reader / transformer / bounded cell replacement / file-backed output handoff
  已有 internal foundation，但仍不是 public low-memory large-file editor。

验收：
- `rg` 搜索无旧口径：`Workbook::open`、`addSheet`、`writeRows`、`saveAs`、
  `no public WorkbookEditor` 等不应出现在当前 API 说明里。
- `git diff --check` 通过。
- 若触碰代码，至少运行对应 CTest；若只改文档，说明未跑 CTest。

### C1 当前 public `WorkbookEditor` Patch facade 硬化

状态：已完成。

目标：把已落地的 public facade 固定稳定，而不是立刻扩展成完整 editor。

范围：
- 围绕 `WorkbookEditor::open()`、`worksheet_names()`、`has_worksheet()`、
  `replace_sheet_data()`、`rename_sheet()`、`save_as()` 维护 Doxygen、README 示例、
  public structure tests 和错误路径测试。
- 明确 `rename_sheet()` 只是 workbook sheet catalog name rewrite，不是语义 rename。
- 明确 `replace_sheet_data()` 只生成 whole-`<sheetData>`，不迁移 sharedStrings、
  不合并 styles、不修复 relationships、不重算 ranges。

禁止：
- 不新增 `WorksheetEditor`。
- 不新增 `get_cell()` / `set_cell()` random editing。
- 不把 internal `PackageEditor` 暴露为 public API。

### C2 Patch preservation / dependency audit 缺口补齐

状态：当前收口批次已完成；后续只按新增对象或失败状态卫生缺口重开。

目标：继续补 existing-file editing 的保真与审计证据，先保证“不破坏未知对象”，再谈语义编辑。

范围：
- 以 `docs/PATCH_PRESERVATION_COVERAGE.md` 为 fixture 明细权威来源。
- 按对象补缺口：comments / threaded comments / persons、printerSettings、
  OLE / controls、VML、charts、VBA、custom XML、unknown extensions、tables、
  drawings / media、sharedStrings / styles。
- 对每类对象只声明 preserve / audit / fail，不声明 repair / pruning / semantic editing。
- 新增测试时优先落在 `fastxlsx.package_editor`，并保持 failure-before-state-change。

推荐执行顺序：
1. comments / threaded comments / persons 的 ordinary replace / remove / ordering：
   当前已有 `test_package_editor_replaces_comments_and_preserves_worksheet_links()`、
   `test_package_editor_removes_comments_and_preserves_worksheet_links()`、
   `test_package_editor_comments_replacement_restores_prior_removal()`、
   `test_package_editor_comments_removal_overrides_prior_replacement()`、
   comments / threaded comments / persons repeated replacement 状态卫生，以及
   threaded comments / persons replace-remove 和 same-path ordering 回归；后续只补真实缺口。
2. worksheet-owned VML / printerSettings / OLE / controls 的保留与显式 removal 审计：
   当前已有 background/header-footer VML、printerSettings、OLE/control preservation、
   explicit removal、same-path ordering 和 repeated replacement 状态卫生回归；后续只补
   未覆盖对象或失败状态卫生。
3. charts / tables / drawings / media 的 linked-part 保留与 relationship audit：
   当前批次已完成覆盖核对。`test_package_editor_replaces_drawing_and_preserves_linked_media_entries()`、
   drawing remove / replacement ordering、media replace / remove / ordering、table
   replace / remove / ordering、chart replace / remove / ordering，以及 percent-decoded
   drawing replace / remove / ordering 已覆盖 planned-output 快照、remove/replace
   state hygiene、URI-qualified / percent-decoded target、content type side effects、
   no invented owner `.rels` 和 output `PackageReader` 重读证据。
4. custom XML、unknown extensions、external links、pivot table / pivot cache、sharedStrings / styles、VBA 的 preserve / fail / ordering：
   已覆盖 custom XML、unknown extensions、externalLink、pivot table / pivot cache、sharedStrings / styles 和 VBA 的
   remove/replacement 反向顺序、owner `.rels` omission/audit 以及 repeated
   replacement 状态卫生；worksheet-owned object 这一批也已补齐 OLE / control /
   background picture 的 repeated replacement 状态卫生。当前收口批次未发现已知对象级
   replacement/order 缺口；后续若新增对象或 `ReferencePolicyAction::Fail` 状态卫生缺口，
   再按小切片重开。

当前阶段的判断标准是“对象级保真和审计覆盖是否完整”，不是“是否新增了完整对象生命周期”。

本轮验收：
- `fastxlsx.package_editor` 覆盖 linked-object preservation、replacement/remove/order、
  repeated replacement state hygiene、relationship/content-type audit 和
  `ReferencePolicyAction::Fail` no-state-pollution。
- `docs/PATCH_PRESERVATION_COVERAGE.md` 是对象级明细权威来源；本文件只记录任务队列状态。
- 默认 `windows-nmake-release` 构建与 CTest 已通过，作为 C2 当前收口批次的回归基线。

禁止：
- 不把 preservation 测试写成完整 object lifecycle 支持。
- 不自动 prune inbound relationships 或 orphan parts，除非有单独设计和测试证明安全。

### C3 public editor 扩展决策门

状态：首个扩展决策已完成；后续仍作为 `WorkbookEditor` / `WorksheetEditor` 分叉门。

目标：在扩大 public API 前，先决定扩展方向和 guardrail，而不是顺手加方法。

当前已选方向：
- 扩展当前 `WorkbookEditor`，先落地 coarse public diagnostics。已实现
  `has_pending_changes()` / `pending_change_count()`，它们只表示 public facade 是否已有
  成功编辑，以及粗粒度 pending-save 数量，不暴露 internal `EditPlan`。
- replacement payload 级 guardrails 已交给 C4 窄切片落地；不把它扩展成 future
  `WorksheetEditor` random-edit guardrail，也不把 internal `EditPlan` 暴露给 public API。
- future `WorksheetEditor` 仍保留为后续小文件 random cell editing 方向，但前置
  workbook-level guardrails、load/materialization contract 和 save-as handoff 仍未进入本轮。

前置条件：
- C1 public facade 当前行为稳定。
- C2 对 sharedStrings、styles、relationships、calc metadata、range metadata 的
  preserve/audit/fail 策略清晰。
- 文档能说明内存增长、失败语义和不支持大文件低内存随机访问。

完成条件：
- 下一步扩展方向已经写入 `TASK_BREAKDOWN.md` 和 `API_DESIGN_AND_DOCUMENTATION.md`。
- `WorkbookEditor` 扩展的确切新增方法已经列出：`has_pending_changes()` /
  `pending_change_count()`，并明确它们只是 coarse diagnostics；replacement payload
  级 options / diagnostics 由 C4 承接。
- `WorksheetEditor` 仍未实现，且文档先写清 load/materialization contract、大小限制和
  save-as handoff。
- C3 现在保留为后续 `WorkbookEditor` / `WorksheetEditor` 分叉门，而不是当前阻塞项。

### C4 In-memory random editor 内部到 public 的窄切片

状态：public Patch facade guardrail / diagnostics 首片完成并通过默认 CTest；真正
In-memory random editor 仍未实现。

目标：把已有 internal `CellStore` foundation 逐步推进到真正小文件 random editor，
但不把它误用为大文件路径。

当前已完成：
- `WorkbookEditorOptions` public 首片已落地，但只作用于
  `WorkbookEditor::replace_sheet_data()` 的 replacement payload 构建：
  `max_replacement_cells` 和 `replacement_memory_budget_bytes`。
- `WorkbookEditor::open(path, options)` 已落地；这些 options 不加载 source worksheet
  cells、不提供 random editing，也不是 workbook-level / worksheet-level In-memory 限制。
- `pending_replacement_cell_count()` 和
  `estimated_pending_replacement_memory_usage()` 已落地；它们只统计最终 queued
  replacement payload per sheet，不统计 source workbook cells、renamed sheets、
  relationships、sharedStrings/styles 或 save-time package assembly。

推荐切片顺序：
1. public replacement-payload options / diagnostics contract：已完成当前
   `WorkbookEditorOptions` 窄语义；future In-memory `max_cells` /
   `memory_budget_bytes`、`cell_count()`、`estimated_memory_usage()` 仍需另行冻结。
2. internal source-backed worksheet load prototype：从一个 worksheet 建立 `CellStore`，
   只覆盖有限 cell value，不处理完整 worksheet 语义。
3. random mutation model：`set_cell` / `erase_cell` / blank / tombstone 的 save-as 语义。
4. public `WorksheetEditor` 首片：只在上述 guardrail 和 save-as contract 有测试后进入。

禁止：
- 不把 current `WorkbookEditor::replace_sheet_data()` 写成 random editing。
- 不承诺 sharedStrings/style migration、formula evaluation、calcChain rebuild 或
  relationship repair。

### C5 大 worksheet 低内存 rewrite 真正流式化

状态：第一片可验证实现已完成；source-entry file-backed extraction 和 output-side
file-backed chunk handoff 已有测试，完整低内存输入仍未实现。

目标：把 P8 internal reader/transformer/output chunks 从 bounded foundation 推向真正大文件 rewrite。

当前已完成：
- `PackageEditor::replace_worksheet_cells()` 输出侧已有 temporary file-backed chunk，
  当前 source package worksheet entry 会先经 `PackageReader::extract_entry_to_file()`
  抽取到 file-backed source。stored entry 路径做 incremental CRC；DEFLATE 构建暂复用
  `read_entry()` 后写文件。
- by-name cell replacement linked-object fixture 已覆盖 dimension refresh、old target
  audit skip、relationship-bearing metadata audit、calcChain cleanup、unknown bytes
  preservation、output `PackageReader` 重读和 temporary file cleanup。

当前缺口：
- 现有 event reader 仍会从 file-backed source 物化 validation input；planned replacement /
  chunk input 也仍会物化 current planned worksheet XML。
- 还没有 event reader chunk/window 输入；真正 rewrite validation 仍不是低内存。
- 还没有 public low-memory worksheet transformer。

推荐切片顺序：
1. 设计 PackageReader worksheet entry streaming source，明确 CRC、错误上下文和
   compressed input 限制。当前第一片已完成：stored entry 可 file-backed extraction
   并做 incremental CRC；DEFLATE 构建暂复用 `read_entry()` 后写文件。
2. 让 event reader 消费 streaming chunks 或 bounded windows，而不是完整 worksheet string。
3. 把 transformer action stream 接到 package-entry staged writer。
4. 在 linked-object fixture 上验证大 worksheet path 的 audit / preservation 不倒退。

验收：
- `fastxlsx.package_reader` 覆盖 `extract_entry_to_file()` stored extraction 和读回一致性。
- `fastxlsx.package_editor` 覆盖 source-entry file-backed extraction note、output-side
  file-backed cell replacement handoff、linked-object preservation 和失败不污染状态。
- 本轮默认 `windows-nmake-release` 构建与 9 个 CTest 条目全部通过。

禁止：
- 不把当前 P8 foundation 宣称为 complete low-memory large-file editing。
- 不修复 tables / drawings / formulas / ranges，除非进入对应 C2/C6 专项。

### C6 支撑线：ZIP/backend、sharedStrings、benchmark、streaming hot path

目标：继续硬化性能和 package writer，但不让它们重新取代编辑主线。

范围：
- P9 package writer guardrails / opt-in minizip。
- P10 sharedStrings 证据。
- P11 benchmark tooling。
- P12 streaming hot path。
- P13 streaming metadata/styles hardening。

边界：
- 支撑线可以并行，但不能作为扩大 public editor 的替代条件。
- 小规模 benchmark 不能写成生产性能结论。

### C7 release / packaging / public wording gate

目标：准备发布前统一 API 稳定性、CI、examples、README、Doxygen 和兼容性证据。

前置条件：
- public surface 有 header、实现、测试、README 示例和文档注释。
- 默认 CTest、minizip opt-in CTest、必要的 Excel/openpyxl QA 有记录。
- 未实现能力在文档中保持 non-goals，而不是 marketing wording。

## 执行规则

- 单个子任务必须有明确输入、输出、触碰文件和验收标准。
- 单个子任务不应同时修改 public API、内部 Patch 语义、测试 fixture 和文档总线。
- 文档设计任务不宣称运行时能力。
- 代码任务必须带对应结构测试；默认 CTest 仍需遵守 60s 边界。
- 涉及 public API 的任务，必须同步 Doxygen 注释、`API_DESIGN_AND_DOCUMENTATION.md`
  和本拆分文档。
- 涉及 existing-file editing 的任务，必须说明是否影响 relationships、content types、
  sharedStrings、styles、worksheet `.rels`、linked parts、calc metadata 和 unknown parts。

## P4.0 - API Surface Unification Design

状态：基础完成，后续 API 任务继续引用。P4.0 设计的 future existing-file editing
facade 现已落地首个 public 切片 `WorkbookEditor`（`include/fastxlsx/workbook_editor.hpp`、
`src/workbook_editor.cpp`、`tests/test_workbook_editor.cpp`，CTest `fastxlsx.workbook_editor`）：
`open()` / `worksheet_names()` / `has_worksheet()` / `replace_sheet_data(name, rows)` /
`rename_sheet(old_name, new_name)` / `save_as()`，输入复用 `CellValue` 行，底层委托
internal `PackageEditor::replace_worksheet_sheet_data_by_name()` 的 bounded local
rewrite 与 `rename_sheet_catalog_entry()` 的窄 catalog 名改写。这是 Patch-mode
whole-sheet 数据替换 + sheet-catalog 改名门面，不是 `WorksheetEditor` / random cell
editing / `get_cell` / `set_cell` / 增删 sheet / 语义 rename，那些仍是 future。

目标：在继续扩大 Patch MVP 或 In-memory API 前，统一 public facade、命名、值类型和
internal/public 边界。

### P4.0.1 Public Facade Matrix

状态：已完成文档基线。

类型：文档设计。

输入：
- 当前 public API：`WorkbookWriter`、`WorksheetWriter`、`CellView`、`Workbook`、
  `Worksheet`、`Cell`、`CellValue`。
- 当前 internal Patch 底座：`PackageReader`、`PackageEditor`、`EditPlan`、
  `PartIndex`、`RelationshipGraph`。

输出：
- 一张稳定 public facade 矩阵：
  `WorkbookWriter` 用于 Streaming；
  `Workbook` 用于 small new workbook；
  当前窄 `WorkbookEditor` subset 用于 Patch-mode existing-file editing；
  future `WorksheetEditor` / random-edit 扩展用于后续小文件编辑。
- 明确 `PackageReader` / `PackageEditor` / `EditPlan` 暂不作为稳定 public API。

可并行性：
- 可与 P4.0.2 并行讨论。
- 不应与修改同一文档段落的任务并行写入。

主要文件：
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`

验收：
- 四个文档对 facade 名称和 internal/public 边界表述一致。
- 文档明确当前 `WorkbookEditor` subset 已实现，future `WorksheetEditor` /
  random-edit 名称不代表已实现。

禁止：
- 本历史设计任务不再新增超出当前 subset 的 `WorkbookEditor` 代码。
- 不把 internal `PackageEditor` 写成 public API。

### P4.0.2 Cell Value Boundary

状态：已完成文档基线。

类型：文档设计，后续可进入 public header 设计。

输入：
- 当前 owning `Cell`。
- 当前 non-owning `CellView`。
- 未来 editor / In-memory 所需随机读写单元格值。

输出：
- `CellView` / `Cell` / `CellValue` / future `CellStore` 边界说明。
- `CellValue` 的候选语义范围：blank、number、text、boolean、formula、style reference。
- 明确 `Cell` 不作为百万级 worksheet 的长期内部存储模型。

可并行性：
- 可与 P4.0.1 并行讨论。
- 代码实现必须等 P4.0.1 和 P4.0.2 文档结论合并后开始。

主要文件：
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/TASK_PLAN.md`

验收：
- 文档能回答：什么时候用 `CellView`，什么时候用 `Cell`，什么时候需要 `CellValue`。
- In-memory 任务能继承该边界，不再重新发明 cell 类型。

禁止：
- 不在没有 guardrails 的情况下把 `Cell` 扩展成通用内部 cell object。

### P4.0.3 Public Naming Rules

状态：已完成文档基线。

类型：文档设计。

输出：
- 统一方法命名规则：
  `add_worksheet`、`worksheet` / `try_worksheet`、`append_row`、`set_cell`、
  `save`、`save_as`。
- 明确哪些名字不能混用，例如 `get_sheet()`、`sheet_by_name()`、`select_sheet()`。

主要文件：
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`

验收：
- 后续 API task 都能引用这套命名规则。

禁止：
- 不为同一行为增加多个 public 同义 API。

### P4.0.4 API Examples and Documentation Contract

状态：已完成文档基线。

类型：文档设计。

输出：
- 三条路径的示例：
  Streaming new workbook、small new workbook、future existing-file editor。
- 每个示例都标注已实现或 future design target。
- Doxygen 注释检查清单更新：mode、memory、ordering、random access、OpenXML side
  effects、error behavior。

主要文件：
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `README.md`
- `docs/TASK_PLAN.md`

验收：
- README 不把 future editor 示例写成当前可编译示例。
- 文档注释要求能覆盖 Patch / In-memory future APIs。

## P4 - Patch MVP

状态：进行中，首个 internal MVP 路径已有基础覆盖。

目标：证明一个窄 existing-file edit 可以打开已有 workbook、定位目标、改写一个目标
part 或 `<sheetData>`、保留 unknown/unmodified parts，并给出 calc policy。

### P4.1 MVP Use Case Freeze

状态：已完成。

类型：设计。

输出：
- 冻结首个 MVP 用例为 **internal by-name worksheet `<sheetData>` patch**。
- 用户故事已通过当前窄 `WorkbookEditor` subset 对外暴露：打开已有 `.xlsx`，
  按 sheet name 定位已有 worksheet，只替换该 worksheet 的 `<sheetData>` payload，
  再 `save_as()` 输出新 package。
- 底层实施入口仍是 internal `PackageEditor::replace_worksheet_sheet_data_by_name()`；
  不新增 public `PackageEditor`、`WorksheetEditor` 或随机 cell API。
- MVP 输入是 caller 已生成的完整 `<sheetData>` / `<sheetData/>` XML payload。
  helper 只做 bounded local rewrite，保留同一 worksheet 的外围 XML metadata，
  并复用现有 calcChain remove / `fullCalcOnLoad`、relationship/content-type audit
  和 unknown/unmodified part preservation 路径。
- 明确不支持随机 cell editing、sharedStrings index migration、style id migration、
  style merge、relationship repair/pruning、table/drawing semantic sync、range 修复、
  dimension 重算或大文件 streaming worksheet transformer。

验收：
- `TASK_PLAN.md` / `NEXT_STEPS.md` / 本文档对 MVP 范围一致。
- 文档只把该能力写成 internal Patch MVP / template-fill 小切片，不能写成 public
  existing-file editor 已完成。

### P4.2 Package Boundary Hardening

状态：基础完成，可继续按失败路径补小切片。

类型：代码 + 测试。

输入：
- 当前 `PackageReader` stored/no-compression + opt-in DEFLATE reader。
- 当前 internal `PackageEditor` copy/replace、worksheet replacement、sheetData helper。
- 当前 linked-object fixture 已覆盖 ordinary workbook replacement 的 active
  `planned_output()` audit 可见性和 source-owned workbook `.rels` copy-original 决策。

输出：
- 更清晰的 package read/copy/write failure behavior。
- 继续证明失败不污染 `EditPlan`、manifest、package-entry audit、calc policy 和输出 bytes。
- 当前已覆盖：
  - package reader central directory bounds hardening。
  - `PackageEditor::save_as()` copy-original source entry 读取失败时带 entry 上下文，
    且不污染状态、不覆盖既有输出。
  - writer backend failure 时带输出路径上下文，且不污染状态、不覆盖既有输出。
  - internal `PackageWriterOptions::compression_level` 接受 `-1` 默认、`0`
    minizip no-compression/stored output 和 `1..9` minizip DEFLATE 等级；非法等级
    在写输出前失败，显式 minizip 等级回归覆盖 level 0 / level 9 可读输出、
    central directory method 和重复 payload 下的压缩体积差异。
  - internal package writer 写前 ZIP32 guardrail：超过 `65535` 个 entries、
    entry name 超过 ZIP 16-bit 字段、单 entry 未压缩大小超过 `UINT32_MAX`
    都会在打开输出路径前失败；file-backed >4GiB chunk 用 sparse file 测试覆盖。

主要文件：
- `src/package_reader.*`
- `src/package_editor.*`
- `tests/test_package_reader.cpp`
- `tests/test_package_editor.cpp`

验收：
- 默认 preset CTest 通过。
- minizip preset 在触碰 DEFLATE 路径时通过。

### P4.3 Relationship and Content-Type Side Effects

状态：基础完成，后续缺口转入 P5 / P6 按对象和依赖类型拆分。

类型：代码 + 测试。

输出：
- 针对 MVP edit 的 relationships/content types 同步策略。
- 明确哪些 side effect 是 helper 管理，哪些只是 audit note。
- 当前已覆盖：
  - 有 `calcChain` 的 `sheetData` patch 会按默认策略重写 workbook calc metadata、
    移除 stale calcChain content type / workbook relationship，并保留 worksheet
    `.rels`、linked parts、sharedStrings、styles、VBA 和 unknown parts。
  - 无 `calcChain` 的 by-name `sheetData` patch 只改 worksheet 与 workbook calc
    metadata，保留 `[Content_Types].xml`、package `_rels/.rels` 和 workbook `.rels`
    为 copy-original，不发明 worksheet `.rels` 或 calcChain。
  - audit-only notes 只提示 sharedStrings、styles、worksheet-local metadata 和
    linked parts 需要 caller review，不做 repair/pruning。

验收：
- 测试覆盖 content type override、package rels、workbook rels、worksheet rels 的保留或重写。
- 不把 audit-only note 写成 repair。

### P4.4 Calc Policy Slice

状态：基础完成。

类型：代码 + 测试。

输出：
- `fullCalcOnLoad`、`calcChain.xml` remove/preserve/rebuild 的窄策略。
- Rebuild 若未实现，必须显式失败或保持 planned。
- 当前已覆盖：
  - 默认 remove：数据/公式相关 worksheet 或 `sheetData` rewrite 会请求
    `fullCalcOnLoad` 并省略 stale `xl/calcChain.xml`。
  - preserve：显式策略会保留现有 calcChain payload / owner `.rels`，同时更新
    workbook recalculation metadata。
  - rebuild：当前未实现，必须显式失败且不污染 `EditPlan`、manifest、package-entry
    audit、calc policy 或输出 bytes。

验收：
- 修改数据或公式相关 payload 时，输出 calc metadata 行为可预测。
- 不凭空生成未实现的 calcChain rebuild。

### P4.5 MVP End-to-End Fixture

状态：基础完成；固定本地 Excel QA helper 已补入并通过当前验证。

类型：测试 + 文档。

输出：
- 一个真实 writer-source 或 fixture workbook 的 end-to-end Patch 回归。
- 验证目标 part 改写，未修改 worksheet、docProps、content types、relationships、
  sharedStrings/styles 和 unknown parts 保留。
- 当前已覆盖：
  - `WorkbookWriter` 生成 package 后，经 `PackageReader` 解析 workbook sheet catalog，
    再由 internal `PackageEditor::replace_worksheet_sheet_data_by_name()` 替换目标
    sheet `<sheetData>` 并 `save_as()`。
  - 输出保留 untouched worksheet、docProps、content types、package relationships、
    workbook relationships、sharedStrings、styles 和相关 bytes，并保持 internal Patch
    MVP 口径。
  - 额外聚合回归会在 writer-source package 中注入 unknown entry，并验证 by-name
    `sheetData` Patch 后该 unknown bytes、默认 content type 和其它 source-owned
    metadata 仍按 copy-original 保留。
  - 固定本地 QA 入口 `tools/verify_patch_mvp_excel.ps1` 会用 Excel COM 只读打开
    writer-roundtrip 和 template-fill Patch MVP 输出，核对 target sheet replacement
    与 untouched sheet preservation 的关键可见单元格。

验收：
- 拆包 XML 检查通过。
- Excel 可视化验证若本机 Excel 可用则通过固定 helper 记录。
- 文档仍标注为 internal Patch MVP，不是 public editor。

## P5 - Preservation Fixtures

状态：基础完成，后续只按对象缺口补小切片。

目标：在扩大 existing-file editing 前，建立含复杂对象的保真 fixture。

子任务：
- P5.1 images/drawings fixture：基础完成。现有 linked-object fixture 覆盖 worksheet
  `.rels`、drawing XML、drawing `.rels`、PNG media bytes、VML drawing、percent-encoded
  drawing target、ordinary replace/remove ordering，以及 active drawing replacement
  等 `planned_output()` audit 可见性；仍不能写成 existing-workbook image/drawing
  语义编辑。
- P5.2 charts fixture：基础完成。现有 fixture 覆盖 chart part、drawing-owned direct
  / URI-qualified chart relationships、ordinary replace/remove ordering 和 content type
  audit；仍不能写成 chart reference migration、series/cache update 或 chart editing。
- P5.3 VBA / macro-enabled fixture：基础完成。现有 fixture 覆盖 `xl/vbaProject.bin`
  bytes、workbook inbound relationship、content type override、ordinary replacement、
  repeated replacement、replace/remove ordering 和 no invented VBA owner `.rels`；
  仍不能写成 macro generation、VBA editing 或 signature preservation。
- P5.4 sharedStrings/styles fixture：基础完成。现有 fixture 覆盖 sharedStrings、
  sharedStrings owner `.rels`、styles、workbook relationships、ordinary replacement、
  sharedStrings repeated replacement、styles repeated replacement、replace/remove
  ordering 和 no invented styles owner `.rels`；仍不能写成 sharedStrings index
  migration、style id migration、style merge 或 cell reference sync。
- P5.5 unknown extension fixture：基础完成。现有 fixture 覆盖 reachable unknown
  extension part、source-owned owner `.rels`、metadata ingestion、ordinary replacement、
  repeated replacement、remove/replace ordering、output-plan omitted/active audit、
  ordinary replacement 下 content types / package relationships / workbook /
  worksheet / drawing / chart / media / table / VML / percent-decoded drawing /
  sharedStrings / styles / VBA / calcChain copy-original 决策和 relationship target
  audit；仍不能写成 custom extension semantic editing、relationship repair 或
  broad unknown-part preservation guarantee。
- P5.6 custom XML fixture：基础完成。现有 fixture 覆盖 custom XML item /
  properties part、item owner `.rels`、package customXml inbound relationship、
  ordinary replacement、explicit removal、跨路径 ordering 和 `planned_output()`
  audit 可见性，包括 item 基础 replacement/removal 的 active/omitted output-plan
  快照；仍不能写成 custom XML 语义编辑、schema/data binding、relationship repair
  或 content type repair。

验收：
- unrelated edit 后，未修改 part 仍存在且 bytes 尽量保留。
- relationships 仍可由 `PackageReader` / `RelationshipGraph` 重读。
- 不宣称语义编辑能力，只宣称 preservation / audit 可见性。

可并行性：
- 后续若发现对象级缺口，各 fixture 可继续并行设计。
- 当前测试集中在 `tests/test_package_editor.cpp`，同一测试文件写入需要串行合并，
  避免冲突。
- 不应把 P5 基础覆盖解释为 P6 dependency policy 已完成；依赖策略仍需按对象类型拆分。

## P6 - Sheet Dependency Policies

状态：基础完成；后续缺口按具体 feature / 测试任务继续推进。

目标：给 sheet-local edits 建立保守依赖策略。

子任务：
- P6.1 sharedStrings / styles dependency policy：基础完成。
- P6.2 tables / hyperlinks / validations / conditionalFormatting policy：基础完成。
- P6.3 drawings / images / charts linked-part policy：基础完成。
- P6.4 definedNames / formulas / calc metadata policy：基础完成。
- P6.5 unsupported edits preserve / request recalc / fail matrix：基础完成。

验收：
- 每类 dependency 都有 preserve、rewrite、audit-only 或 fail 的明确策略。
- 不自动 prune relationships，除非测试证明该行为安全。

### P6.1 sharedStrings / styles dependency policy

状态：基础完成。

类型：文档设计 + 现有测试映射；后续可按缺口补代码测试。

目标：把 sheet-local edit 遇到 shared string indexes 和 style id references 时的保守
策略写清楚，避免把现有 audit-only 行为误写成 sharedStrings/styles 迁移或修复。

输入：
- 当前完整 worksheet replacement / `sheetData` patch 对 shared string indexes、
  style id references 和公式 cell 的 audit-only 扫描。
- 当前 linked-object fixture 对 `xl/sharedStrings.xml`、sharedStrings owner `.rels`、
  `xl/styles.xml`、workbook relationships 和 content type overrides 的 copy-original /
  replace/remove ordering 覆盖。
- 当前 `ReferencePolicy`、`WorksheetPayloadDependencyAudit`、`EditPlan` 和
  `planned_output()` 结构化审计。

输出：
- sharedStrings policy：保留现有 `xl/sharedStrings.xml` 和 owner `.rels`，对 worksheet
  payload 中的 shared string indexes 只做 audit-only caller review；不迁移索引、不重建
  string table、不改写 worksheet `t="s"` cell。
- styles policy：保留现有 `xl/styles.xml`，对 worksheet payload 中的 style id references
  只做 audit-only caller review；不迁移 style id、不合并 styles、不改写 cell `s`
  references。
- `ReferencePolicyAction::Fail` 下的失败边界：遇到 caller 不接受的 sharedStrings /
  styles 依赖时，必须在状态变更前失败，且不污染 `EditPlan`、manifest、
  package-entry audit、calc policy、relationship audits 或输出 bytes。
- 文档明确该任务不新增 public API、不暴露 public `PackageEditor` / `WorkbookEditor`。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/EDITING_MODEL.md`
- 必要时同步 `docs/API_DESIGN_AND_DOCUMENTATION.md`、`docs/TASK_PLAN.md`、
  `docs/NEXT_STEPS.md`
- 若发现测试缺口，再触碰 `tests/test_package_editor.cpp`

不触碰文件：
- `include/fastxlsx/*` public headers
- streaming writer API / tests
- package reader ZIP 边界
- CMake 配置

可并行性：
- 可与 P6.2 / P6.3 / P6.4 的只读调研并行。
- 若需要新增 `tests/test_package_editor.cpp` 用例，写入该文件的变更必须串行合并。

验收标准：
- 文档能回答 sharedStrings/styles 是 preserve、rewrite、audit-only 还是 fail。
- 文档明确现有行为不做索引迁移、style merge、worksheet reference rewrite、
  relationship pruning/repair 或 public editing API。
- 若新增测试，默认 preset 下 `fastxlsx.package_editor` 通过，并保持 60s CTest 边界。

禁止项：
- 不新增 public `WorkbookEditor`、`WorksheetEditor` 或 public `PackageEditor`；
  不把 `CellValue` 扩展成 editor / store。
- 不把 audit-only note 写成 repair。
- 不自动 prune workbook relationships、owner `.rels` 或 content type overrides。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_editor
```

### P6.2 tables / hyperlinks / validations / conditionalFormatting policy

状态：基础完成。

类型：文档设计 + 现有测试映射；后续可按缺口补代码测试。

目标：把 sheet-local edit 遇到 worksheet-local metadata 和 table-linked metadata
时的保守策略写清楚，避免把 preservation / audit-only 行为误写成 table resize、
hyperlink repair、validation recalculation 或 conditional formatting semantic sync。

输入：
- 当前完整 worksheet replacement / `sheetData` patch 对 dataValidations、
  conditionalFormatting、hyperlinks 和 tableParts 的 payload audit。
- 当前 linked-object fixture 对 `xl/tables/table1.xml`、worksheet `.rels` table
  relationship、worksheet `<hyperlinks>` / relationship ids 和 source worksheet metadata
  preservation 的 copy-original / replace/remove ordering 覆盖。
- 当前 `WorksheetRelationshipReferenceAudit`、`WorksheetPayloadDependencyAudit`、
  `RelationshipTargetAudit`、`ReferencePolicy`、`EditPlan` 和 `planned_output()`。

输出：
- tables policy：保留 source table part、worksheet `.rels` inbound relationship 和
  table content type override；worksheet payload 中的 `<tableParts>` 只做 caller
  review / relationship audit，不 resize table、不迁移 table range、不重写 table XML。
- hyperlinks policy：保留 worksheet `<hyperlinks>` 和 worksheet `.rels`；只审计
  missing/stale/type-mismatch relationship ids，不修复 target、不创建 hyperlink style、
  不校验 external URL 或 internal location 可达性。
- data validations policy：保留或审计 worksheet-local `<dataValidations>` metadata；
  不重算 ranges、不校验公式或单元格值、不与 table / merged range / autoFilter 做语义同步。
- conditionalFormatting policy：保留或审计 worksheet-local
  `<conditionalFormatting>` metadata；不重排 priority、不计算公式、不生成 dxfs、不做
  style / range / table 联动修复。
- `ReferencePolicyAction::Fail` 下的失败边界：caller 不接受这些 payload /
  relationship dependencies 时，必须在状态变更前失败，且不污染 `EditPlan`、manifest、
  package-entry audit、relationship audits、calc policy、planned output 或输出 bytes。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/EDITING_MODEL.md`
- 必要时同步 `docs/API_DESIGN_AND_DOCUMENTATION.md`、`docs/TASK_PLAN.md`、
  `docs/NEXT_STEPS.md`
- 若发现测试缺口，再触碰 `tests/test_package_editor.cpp`

不触碰文件：
- `include/fastxlsx/*` public headers
- streaming writer API / tests
- package reader ZIP 边界
- CMake 配置

可并行性：
- 可与 P6.3 / P6.4 的只读调研并行。
- 若需要新增 `tests/test_package_editor.cpp` 用例，写入该文件的变更必须串行合并。

验收标准：
- 文档能回答 tables、hyperlinks、data validations、conditionalFormatting 分别是
  preserve、rewrite、audit-only 还是 fail。
- 文档明确现有行为不做 table resize、hyperlink repair、validation formula/value
  validation、conditional formatting priority/dxf/style/range sync、relationship
  pruning/repair 或 public editing API。
- 若新增测试，默认 preset 下 `fastxlsx.package_editor` 通过，并保持 60s CTest 边界。

禁止项：
- 不新增 public editing API。
- 不把 audit-only note 写成 repair。
- 不自动 prune worksheet relationships、table parts、hyperlink relationships、
  content type overrides 或 worksheet-local metadata。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_editor
```

### P6.3 drawings / images / charts linked-part policy

状态：基础完成。

类型：文档设计 + 现有测试映射；后续可按缺口补代码测试。

目标：把 sheet-local edit 遇到 drawing、image/media、chart 和 legacy drawing linked
parts 时的保守策略写清楚，避免把 linked-part preservation / audit-only 行为误写成
drawing mutation、image editing、chart reference repair 或 relationship pruning。

输入：
- 当前 linked-object fixture 对 worksheet `<drawing>` / `<legacyDrawing>` /
  `<picture>` / `<legacyDrawingHF>`、worksheet `.rels`、drawing XML、drawing `.rels`、
  PNG media、chart XML、VML drawing、percent-decoded drawing target、URI-qualified
  relationship target、external / invalid / unresolved target audit 的覆盖。
- 当前 ordinary replace/remove 和 remove-then-replace / replace-then-remove ordering
  对 drawing、media、chart、VML drawing、percent-decoded drawing 的 output-plan audit。
- 当前 `RelationshipTargetAudit`、removed-part inbound audit、`ReferencePolicy`、
  `EditPlan` 和 `planned_output()`。

输出：
- drawing policy：保留 source drawing part、worksheet inbound relationship、drawing
  owner `.rels` 和 drawing content type override；worksheet payload 中的 drawing
  references 只做 relationship audit，不重写 drawing XML、不合成 owner `.rels`。
- image/media policy：保留 drawing-owned image relationships 和 media bytes；default
  typed media 保持 default content type，不提升为 override；不解码、不裁剪、不压缩、
  不转换图片格式，也不编辑 existing drawing anchor。
- chart policy：保留 drawing-owned chart relationships、URI-qualified base target
  audit 和 chart part bytes；不修复 chart series/cache、drawing frame、chart reference
  或 content type metadata。
- malformed / external / URI-qualified / unresolved linked targets 只进入结构化 audit；
  不虚构 package part，不做 target repair，也不 prune source relationships。
- `ReferencePolicyAction::Fail` 下的失败边界：caller 不接受 linked drawing/image/chart
  dependencies 时，必须在状态变更前失败，且不污染 `EditPlan`、manifest、
  package-entry audit、relationship audits、calc policy、planned output 或输出 bytes。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/EDITING_MODEL.md`
- 必要时同步 `docs/API_DESIGN_AND_DOCUMENTATION.md`、`docs/TASK_PLAN.md`、
  `docs/NEXT_STEPS.md`
- 若发现测试缺口，再触碰 `tests/test_package_editor.cpp`

不触碰文件：
- `include/fastxlsx/*` public headers
- streaming writer API / tests
- package reader ZIP 边界
- CMake 配置

可并行性：
- 可与 P6.4 的只读调研并行。
- 若需要新增 `tests/test_package_editor.cpp` 用例，写入该文件的变更必须串行合并。

验收标准：
- 文档能回答 drawings、images/media、charts 是 preserve、rewrite、audit-only 还是 fail。
- 文档明确现有行为不做 drawing mutation、image editing、chart reference repair、
  relationship target repair、relationship pruning/orphan cleanup 或 public editing API。
- 若新增测试，默认 preset 下 `fastxlsx.package_editor` 通过，并保持 60s CTest 边界。

禁止项：
- 不新增 public drawing/image/chart editing API。
- 不把 linked-part audit 写成 repair。
- 不自动 prune worksheet/drawing relationships、media/chart parts、drawing owner `.rels`
  或 content type overrides。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_editor
```

### P6.4 definedNames / formulas / calc metadata policy

状态：基础完成。

类型：文档设计 + 现有测试映射；后续可按缺口补代码测试。

目标：把 sheet-local edit 遇到 workbook `definedNames`、worksheet formulas、
workbook calc metadata 和 `calcChain.xml` 时的保守策略写清楚，避免把当前
audit-only / request-recalc 行为误写成 named range repair、公式求值、dependency
graph 或 calcChain rebuild。

输入：
- 当前完整 worksheet replacement / `sheetData` patch 对 formula cells、
  workbook calc metadata、calcChain policy 和 range/reference metadata 的 audit。
- 当前 `rename_sheet_catalog_entry()` 对 direct workbook `definedNames`、
  `ReferencePolicyAction::Fail` 和 planned workbook catalog 的状态不污染回归。
- 当前 `request_full_calculation()`、`CalcChainAction::Remove` /
  `CalcChainAction::Preserve` / `CalcChainAction::Rebuild` rejection、direct-child
  `calcPr` rewrite、stale calcChain metadata cleanup 和 planned output audit。
- 当前 `WorksheetPayloadDependencyAudit`、`ReferencePolicy`、`EditPlan`、
  package-entry audit 和 `planned_output()`。

输出：
- definedNames policy：默认保留 workbook `definedNames`；sheet rename / catalog rename
  只记录 audit，不同步 named ranges、print areas、formula references 或 sheet-scoped
  names。
- formulas policy：worksheet formula cells 只进入 audit-only caller review；不求值公式、
  不写 cached result、不改写 references、不构建 dependency graph。
- calc metadata policy：数据或公式相关 worksheet rewrite 默认 request full calculation，
  可按策略 remove stale calcChain 或 preserve existing calcChain；`CalcChainAction::Rebuild`
  明确未实现并在状态变更前失败。
- workbook `calcPr` helper 边界：只改 workbook 根的 direct-child `calcPr`，不做 schema
  validation、namespace repair、relationship/content-type 通用修复或 public editing API。
- `ReferencePolicyAction::Fail` 下的失败边界：caller 不接受 definedNames / formulas /
  calc metadata dependency 时，必须在状态变更前失败，且不污染 `EditPlan`、manifest、
  package-entry audit、relationship audits、calc policy、planned output 或输出 bytes。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/EDITING_MODEL.md`
- 必要时同步 `docs/API_DESIGN_AND_DOCUMENTATION.md`、`docs/TASK_PLAN.md`、
  `docs/NEXT_STEPS.md`
- 若发现测试缺口，再触碰 `tests/test_package_editor.cpp`

不触碰文件：
- `include/fastxlsx/*` public headers
- streaming writer API / tests
- package reader ZIP 边界
- CMake 配置

可并行性：
- 可与 P6.5 的只读调研并行。
- 若需要新增 `tests/test_package_editor.cpp` 用例，写入该文件的变更必须串行合并。

验收标准：
- 文档能回答 definedNames、formulas、workbook calc metadata 和 calcChain 分别是
  preserve、rewrite、audit-only 还是 fail。
- 文档明确现有行为不做 named range repair、公式求值、cached result 写入、
  formula reference rewrite、calcChain rebuild、dependency graph 或 public editing API。
- 若新增测试，默认 preset 下 `fastxlsx.package_editor` 通过，并保持 60s CTest 边界。

禁止项：
- 不新增 public formula / named range / calc engine API。
- 不把 request full calculation 写成公式计算。
- 不自动 rewrite definedNames、formula references、calcChain payload、workbook
  relationships 或 content type metadata，除非已有 helper 明确管理该窄路径。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_editor
```

### P6.5 unsupported edits preserve / request recalc / fail matrix

状态：基础完成。

类型：文档设计 + 现有测试映射；后续可按缺口补代码测试。

目标：把 P6 sheet dependency policy 收束成统一决策矩阵，说明 unsupported edits
何时 preserve/audit、何时 request recalculation、何时 strict fail，避免把当前内部
Patch 审计和状态卫生写成完整语义编辑、relationship repair 或 public API。

输入：
- P6.1-P6.4 对 sharedStrings、styles、tables、hyperlinks、validations、
  conditionalFormatting、drawings/images/charts、definedNames、formulas 和 calc
  metadata 的策略。
- 当前 `ReferencePolicyAction::Fail`、`ReferencePolicyAction::RequestRecalculation`、
  `CalcChainAction::Remove` / `Preserve` / `Rebuild` rejection。
- 当前 invalid replacement、metadata-entry replacement、invalid removal、malformed /
  missing workbook metadata、`save_as()` guard、copy-original read failure 和 writer
  failure 的 no-state-pollution 回归。
- 当前 `EditPlan`、manifest、package-entry audit、relationship / worksheet reference /
  payload dependency audits、calc policy、`planned_output()` 和输出 bytes。

输出：
- preserve / audit lane：未知或未修改 parts、source relationships、unsupported linked
  dependencies 和静态 workbook/worksheet dependencies 默认 copy-original 或 audit-only；
  不做 semantic sync、relationship pruning、orphan cleanup 或 target repair。
- request recalculation lane：数据/公式相关 worksheet rewrite 或 caller 明确选择
  `RequestRecalculation` 时，只请求 workbook full calculation，并按 calcChain 策略
  remove 或 preserve；不计算公式、不重建 dependency graph、不修复 linked parts。
- fail lane：caller 选择 strict fail、请求未实现的 calcChain rebuild、替换/移除非法
  target、普通替换 metadata package entry、workbook metadata 不可安全重写，或输出
  guard/copy/write 失败时，必须保持既有 plan / manifest / audits / calc policy /
  planned output / source bytes 或 queued-safe state 不污染。
- ordinary rewrite / removal lane：显式 ordinary replacement / removal 只表达目标 part
  rewrite 或 omission audit；除已有 helper 管理的 docProps / calcChain / workbook calc
  metadata 窄路径外，不自动重写 inbound relationships、content types 或 sibling parts。
- 文档明确该矩阵不新增 public `PackageEditor`、不声明 broad safe editing、不声明完整
  preservation pipeline。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/EDITING_MODEL.md`
- 必要时同步 `docs/API_DESIGN_AND_DOCUMENTATION.md`、`docs/TASK_PLAN.md`、
  `docs/NEXT_STEPS.md`
- 若发现测试缺口，再触碰 `tests/test_package_editor.cpp`

不触碰文件：
- `include/fastxlsx/*` public headers
- streaming writer API / tests
- package reader ZIP 边界
- CMake 配置

可并行性：
- 可与后续 P7 的只读设计调研并行。
- 若需要新增 `tests/test_package_editor.cpp` 用例，写入该文件的变更必须串行合并。

验收标准：
- 文档能把 unsupported edits 明确归入 preserve/audit、request recalculation、fail 或
  explicit part rewrite/removal。
- 文档明确 request recalculation 不是公式求值，preserve/audit 不是 repair，fail 必须
  no-state-pollution。
- 若新增测试，默认 preset 下 `fastxlsx.package_editor` 通过，并保持 60s CTest 边界。

禁止项：
- 不新增 public existing-file editing API。
- 不把 unsupported edit 自动修复、relationship pruning、orphan cleanup 或 target repair
  写成现有行为。
- 不把 failed `save_as()`、copy-original read failure 或 writer failure 写成已提交的
  package mutation。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_editor
```

## P7 - In-memory Small-File Editor

状态：设计基线基础完成；`CellValue` 首个 public value 切片和 internal
`CellStore` / `CellRecord` 首个稀疏存储切片和 internal `CellStoreOptions` guardrail
enforcement 首片已实现；internal `CellStore` 到 standalone `<sheetData>` payload
emission 首片也已实现，并已有首个 internal `CellStore` 到 by-name
`PackageEditor` `sheetData` Patch handoff 回归。public editor、random cell editing、
workbook-level guardrails 和 random-edit / in-memory materialization save-as handoff
仍未 ready。

目标：提供小文件随机编辑体验，但不成为大文件默认路径。

子任务：
- P7.1 `WorkbookEditor` / `WorksheetEditor` public facade draft：基础完成。
- P7.2 `CellValue` public value draft：基础完成。
- P7.2a `CellValue` public value implementation：基础完成。
- P7.3 internal `CellStore` / `CellRecord` memory model：基础完成。
- P7.3a internal `CellStore` / `CellRecord` implementation：基础完成。
- P7.4 guardrails：`max_cells`、`memory_budget_bytes`、`cell_count()`、
  `estimated_memory_usage()`：基础完成。
- P7.4a internal `CellStoreOptions` guardrail implementation：基础完成。
- P7.5 save-as and Patch handoff contract：基础完成。
- P7.5a internal `CellStore` sheetData emission implementation：基础完成。
- P7.5b internal `CellStore` to PackageEditor sheetData handoff regression：基础完成。

验收：
- API 注释明确 In-memory mode、随机访问语义和内存增长。
- 超限时给出明确错误并建议 Streaming 或 Patch。
- 不承诺百万行 worksheet 低内存随机读写。

### P7.1 `WorkbookEditor` / `WorksheetEditor` public facade draft

状态：基础完成。此 draft 的 Patch 子集已落地为首个 public `WorkbookEditor` 切片
（`open` / `worksheet_names` / `has_worksheet` / `replace_sheet_data` /
`rename_sheet` / `save_as`，见 P4.0 落地说明）；`WorksheetEditor`、`get_cell` /
`set_cell` / `erase_cell`、随机 cell 编辑和 append/insert/delete row 仍是 future
facade draft，未实现。

类型：public API 文档设计；首个 `WorkbookEditor` Patch 切片已附带 header /
implementation / 测试，其余仍为文档设计。

目标：记录当前 `WorkbookEditor` Patch subset 的职责，并冻结 future editor extension
的命名、职责、入口和非目标，确保 In-memory 小文件随机编辑不会污染 Streaming 热路径，
也不会把 internal `PackageEditor` 直接暴露为 public API。

输入：
- P4.0 facade matrix 和 cell value boundary 文档基线。
- 当前 `docs/API_DESIGN_AND_DOCUMENTATION.md`、`docs/ARCHITECTURE.md` 和
  `docs/EDITING_MODEL.md` 中的 current `WorkbookEditor` subset、future
  `WorksheetEditor` / random-edit 和 `CellValue` 表述。
- 当前 public API：`WorkbookWriter` / `WorksheetWriter` / `CellView`、
  `Workbook` / `Worksheet` / `Cell`。
- 当前 internal Patch 底座：`PackageReader`、`PackageEditor`、`EditPlan` 和
  `planned_output()`。

输出：
- current `WorkbookEditor` Patch subset：`open(...)`、`worksheet_names()`、
  `has_worksheet()`、`replace_sheet_data(...)`、`rename_sheet(...)`、`save_as(...)`
  和 no in-place overwrite 边界。
- future `WorkbookEditor` extension draft：options / diagnostics / broader handoff
  扩展点，不能覆盖当前 subset 的窄 Patch 边界。
- future `WorksheetEditor` facade draft：`name()`、`get_cell()` / `try_cell()`、
  `set_cell()`、`erase_cell()`、append/insert/delete row 的阶段边界。
- 模式说明：这是 In-memory / future editor 小文件随机编辑路径，不是大文件低内存路径，
  也不是 public `PackageEditor`。
- 后续依赖：P7.2 负责 `CellValue` 语义，P7.3 负责 `CellStore` / `CellRecord`，
  P7.4 负责 size / memory guardrails，P7.5 负责 save-as / Patch handoff。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- 必要时同步 `docs/ARCHITECTURE.md`、`docs/TASK_PLAN.md`、`docs/NEXT_STEPS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/*`
- `tests/*`
- CMake 配置

可并行性：
- 可与 P7.2 / P7.3 的只读调研并行。
- 若同一文档段落也在修改，写入必须串行合并。

验收标准：
- 文档能回答 `WorkbookEditor`、`WorksheetEditor`、`Workbook`、`WorkbookWriter` 的职责差异。
- 文档明确 P7.1 中 `WorkbookEditor` Patch subset 已实现；`WorksheetEditor` /
  random-edit 只是 future facade draft，不是已实现 API。
- 文档明确该 facade 不承诺百万行 worksheet 随机访问、原地覆盖、公式求值、
  relationship repair 或 public package editing。

禁止项：
- 不新增超出当前 subset 的 public `WorkbookEditor` / `WorksheetEditor` /
  `CellValue` 代码。
- 不把 internal `PackageEditor` 或 OPC part concepts 暴露给普通 public API。
- 不因 internal guardrail first slice 就宣称 In-memory editor ready；仍需 P7.5 handoff。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P7.2 `CellValue` public value draft

状态：基础完成；首个 public value implementation slice 已在 P7.2a 落地。

类型：public API 文档设计；不新增 header / implementation。

目标：冻结 `CellValue` 的语义值边界、所有权、value kind、style reference
和与现有 `Cell` / `CellView` 的转换关系，避免把 public value 类型误用为
内部长期 cell store，也避免让小文件随机编辑污染 Streaming 热路径。

输入：
- P4.0 facade matrix 和 cell value boundary 文档基线。
- P7.1 future `WorkbookEditor` / `WorksheetEditor` facade draft。
- 当前 public API：`Cell`、`CellView`、`StyleId`、`CellStyle` 和
  `WorkbookWriter::add_style()`。
- 当前 `Cell` / `CellView` 的 finite numeric、write-only formula、date-as-serial
  和 string ownership / view lifetime 边界。

输出：
- `CellValueKind`：blank、number、text、boolean、formula。
- optional style reference：`CellValue` 可携带 workbook-local
  `StyleId`；非默认 id 必须来自同一 workbook/editor style registry，foreign /
  invalid id 的拒绝时机由后续实现定义。
- ownership 边界：`CellValue` owns text / formula payload，可跨 editor API 调用
  copy / move；不同于 streaming-only non-owning `CellView`。
- 与 `Cell` / `CellView` 的关系：`Cell` 继续是小文件新建路径 owning convenience
  value，未来可转换到 `CellValue`；`CellView` 继续只用于 `WorksheetWriter`
  append-row 热路径，不进入 editor / in-memory 长期状态。
- missing cell vs blank 边界：`try_cell()` 返回空表示 cell 不存在；
  `CellValue::blank()` 表示显式 blank / clear 候选，最终 erase semantics 留给
  P7.3 / P7.5 定义。
- formula 边界：只保存 formula text，不解析、不求值、不写 cached value、
  不重建 `calcChain.xml`。
- numeric 边界：延续 `Cell` / `CellView` 的 finite-only 要求，不把 `NaN` /
  `Inf` 转为字符串、空 cell 或 OpenXML 数字文本。
- 非目标：P7.2 不定义 date cell、rich text、error cell、array formula、
  formula evaluator、sharedStrings/style merge 策略或百万行 cell storage model。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/*`
- `tests/*`
- CMake 配置

可并行性：
- 可与 P7.3 internal storage 的只读调研并行。
- 若同一文档段落也在修改，写入必须串行合并。

验收标准：
- 文档能回答 `CellValue`、`Cell` 和 `CellView` 的所有权和模式差异。
- 文档明确 `CellValue` 是已实现 public value type，但它本身不是 internal store，
  也不代表 public editor ready。
- 文档明确 style id 是 workbook-local handle，P7.2 不做 style registry merge。
- 文档明确 blank 与 missing cell 不等价，公式不求值，数字必须 finite。

禁止项：
- 不把 `CellValue` value type 写成 public editor、worksheet storage 或 save-as 实现。
- 不把 `CellValue` 写成内部 `CellStore` / `CellRecord` 的长期存储布局。
- 不新增 date / rich text / error cell 承诺。
- 不因 internal guardrail first slice 就宣称 In-memory editor ready；仍需 P7.5 handoff。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P7.2a `CellValue` public value implementation

状态：基础完成。

类型：public API + 实现 + 测试；不新增 editor / store。

目标：落地 P7.2 的最小 owning semantic value，让后续 `WorkbookEditor` /
`WorksheetEditor` 和 In-memory store 可以复用统一 cell 边界，而不是继续只停留在
future 文档名。

输入：
- P7.2 `CellValue` public value draft。
- 当前 `Cell` / `CellView` 的 finite numeric、owning / non-owning 字符串和 formula
  边界。
- 当前 `StyleId` workbook-local handle 边界。

输出：
- `include/fastxlsx/cell_value.hpp` 暴露 `CellValueKind` 和 `CellValue`。
- `CellValue` 支持 blank、finite number、owned text、boolean、owned formula。
- `CellValue::from_cell(const Cell&)` / `to_cell()` 提供当前 small-workbook
  `Cell` 与 owning semantic value 之间的轻量转换 helper。
- `CellValue::with_style(StyleId)` / `without_style()` 只携带或清除 opaque
  workbook-local style handle；不校验 foreign / invalid non-default style id。
- `fastxlsx.unit` 覆盖 kind、payload ownership、style handle 和 non-finite number
  rejection。

边界：
- 不新增 `WorkbookEditor` / `WorksheetEditor`。
- 不新增 `CellStore` / `CellRecord` 实现。
- 不实现 random cell editing、save-as handoff、sharedStrings migration、style merge、
  relationship repair 或 calcChain rebuild。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.unit
```

### P7.3 internal `CellStore` / `CellRecord` memory model

状态：基础完成。

类型：internal architecture / API 文档设计；不新增 header / implementation。

目标：定义 future In-memory editor 的内部 cell 存储草案，确保随机编辑使用
紧凑 `CellStore` / `CellRecord`，而不是长期保存 public `Cell` 或 `CellValue` 对象；
同时为 P7.4 size / memory guardrails 和 P7.5 save-as / Patch handoff 留出明确输入。

输入：
- P7.1 future `WorkbookEditor` / `WorksheetEditor` facade draft。
- P7.2 `CellValue` public value boundary。
- 当前 `docs/API_DESIGN_AND_DOCUMENTATION.md`、`docs/ARCHITECTURE.md` 和
  `docs/EDITING_MODEL.md` 的 In-memory boundary。
- 当前 public `Cell` / `CellView` / `StyleId` / styles / sharedStrings 文档边界。

输出：
- `CellStore` 草案：worksheet-local sparse storage，按 row / column key 索引已存在或
  显式编辑过的 cells；不分配完整 worksheet matrix。
- `CellRecord` 草案：保存 value kind、optional/default style id 和紧凑 payload。
  number / boolean 内联保存；当前首片把 text / formula 存为 owned string，后续
  compact-storage 演进再替换为 pool id。blank / clear 语义用显式 record 或 tombstone
  候选表达，最终 erase/save-as 规则留给 P7.5。
- pool 草案：worksheet 或 workbook 作用域的 string pool / formula pool，避免每个
  cell record 持有 owning `std::string`；是否与 `xl/sharedStrings.xml` 索引复用或迁移
  由后续 Patch / save-as 任务定义。
- style 边界：`CellRecord` 只保存 workbook-local style handle / default marker；
  不在 P7.3 合并 styles、不迁移 foreign style ids、不做 existing-file style preservation。
- sparse index 边界：可用 row-major ordered map、row buckets 或 equivalent sparse
  index；必须支持 deterministic worksheet XML emission order，但不承诺百万行随机访问
  低内存。
- 计量输入：P7.3 只定义 `cell_count`、record overhead、pool bytes、sparse index
  overhead 和 save-time assembly memory 的估算维度；实际 `max_cells` /
  `memory_budget_bytes` enforcement 由 P7.4 定义。
- side-effect 边界：`CellStore` 不计算公式、不重建 calcChain、不修复 relationships /
  content types，也不负责 sharedStrings / styles migration。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/*`
- `tests/*`
- CMake 配置

可并行性：
- 可与 P7.4 guardrail API 的只读调研并行。
- 与 P7.5 save-as handoff 的写入需串行，因为 blank / erase / tombstone 语义会互相影响。

验收标准：
- 文档明确 `CellStore` / `CellRecord` 是 internal design / implementation detail，
  不是 public API。
- 文档明确 public `CellValue` 是边界值，内部长期存储使用 compact record / pool。
- 文档明确 sparse storage、pooling、style handle、missing / blank / erase 边界。
- 文档给 P7.4 提供可计量的 memory / cell-count 维度。

禁止项：
- 不新增 public 或 internal C++ 符号。
- 不把 P7.3 文档草案本身写成 public editor ready；实现状态以 P7.3a
  internal first slice 为准。
- 不承诺百万行 worksheet 低内存随机访问。
- 不在 P7.5 前宣称 sharedStrings、styles、calcChain 或 relationship handoff 已完成。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P7.3a internal `CellStore` / `CellRecord` implementation

状态：基础完成。

类型：internal detail header + implementation + focused unit tests；不新增 public API。

目标：落地 P7.3 的最小 worksheet-local sparse cell store，让后续 P7.4 guardrails
和 P7.5 save-as / Patch handoff 有真实可测的内部输入，而不是继续只停留在文档草案。

输入：
- P7.2a `CellValue` public value implementation。
- P7.3 internal storage model draft。
- 当前 `detail::cell_reference(row, column)` Excel 坐标边界校验。

输出：
- `include/fastxlsx/detail/cell_store.hpp` 暴露 internal `CellPosition`、`CellRecord`
  和 `CellStore`。
- `src/cell_store.cpp` 实现 row-major `std::map<CellPosition, CellRecord>` sparse
  index、`set_cell()`、`find_cell()`、`erase_cell()`、`cell_count()`、
  `estimated_memory_usage()` 和只读 `records()` 视图。
- `CellRecord::from_value()` / `to_value()` 在 internal record 与 public
  `CellValue` 边界之间转换，并保留 optional `StyleId` handle。
- 当前首片把 text / formula payload 存在 record-owned `std::string` 中；string /
  formula pool 仍是后续 compact-storage 演进，不属于 P7.3a。
- 显式 `CellValue::blank()` 当前可作为 active blank record 存入 store；
  `erase_cell()` 当前移除 sparse record。existing-file clear / tombstone /
  style-preservation semantics 仍由 P7.5 定义。
- `fastxlsx.unit` 覆盖 sparse ordering、insert / overwrite / erase、explicit blank
  record、style handle round-trip、坐标拒绝和 memory estimate growth。

触碰文件：
- `include/fastxlsx/detail/cell_store.hpp`
- `src/cell_store.cpp`
- `CMakeLists.txt`
- `tests/test_minimal_xlsx.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`
- 必要时同步 `docs/TASK_PLAN.md`、`docs/NEXT_STEPS.md`、`README.md`、`AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/package_editor.*`
- `src/streaming_writer.*`

可并行性：
- 可与 P7.4 guardrail 文档细化并行。
- 与 P7.5 save-as / Patch handoff 写入必须串行，因为 blank / erase /
  tombstone 和 style-preservation semantics 会影响输出 contract。

验收标准：
- 默认构建把 `src/cell_store.cpp` 纳入 `fastxlsx` target。
- `fastxlsx.unit` 验证 internal store 的当前合同。
- 文档明确该切片是 internal foundation，不是 public `WorkbookEditor`、
  `WorksheetEditor`、random cell editing、existing-file editing 或 save-as handoff。
- 文档明确当前尚未实现 string / formula pool、public/workbook-level guardrails、
  load/save-as preflight、sharedStrings migration、styles merge、calcChain rebuild 或
  relationship repair。

禁止项：
- 不新增 public `WorkbookEditor` / `WorksheetEditor` / `PackageEditor` API。
- 不把 `CellStore` 写成 Streaming / Patch 默认内部表示。
- 不承诺百万行 worksheet 低内存随机访问。
- 不在 P7.5 前宣称 blank / erase 的 existing-file clear semantics 已完成。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.unit
```

### P7.4 guardrails：`max_cells` / `memory_budget_bytes`

状态：基础完成；P7.4a 已落地 internal `CellStore` guardrail first slice。

类型：public API / internal architecture 文档设计；本节本身不新增 public header /
implementation。

目标：冻结 future In-memory editor 的 size / memory guardrail 草案，明确
`max_cells`、`memory_budget_bytes`、`cell_count()`、`estimated_memory_usage()` 的
候选语义、计量口径、拒绝时机和错误提示，确保小文件随机编辑不会被误写成大文件
低内存路径。

输入：
- P7.1 future editor facade draft。
- P7.2 `CellValue` public value boundary。
- P7.3 internal `CellStore` / `CellRecord` memory model。
- 当前 In-memory API 文档要求：超限时提示 caller 改用 Streaming 或 Patch。

输出：
- future In-memory editor options / equivalent guardrail options 草案：
  `max_cells`、`memory_budget_bytes`、可选 diagnostic / strict mode 扩展点。
- future diagnostic APIs 草案：`cell_count()`、`estimated_memory_usage()`，作用域需明确
  是 workbook-level、worksheet-level 还是两者都提供。
- 计数口径：active value records、blank / tombstone records、row/column metadata、
  string/formula pool bytes、sparse index overhead、style handles 和 save-time assembly
  memory 的估算边界。
- enforcement 时机：open/load materialization、`set_cell()` / `erase_cell()`、row/column
  edits、pool growth、before save-as snapshot；拒绝必须发生在状态污染前或记录为明确
  failed mutation。
- 错误边界：超限走 `FastXlsxError` 或 future 明确错误码；错误消息应说明触发的 limit
  和建议路径（Streaming 用于大导出，Patch 用于已有文件局部替换）。
- 非 ready 边界：P7.4 只定义 guardrail 设计；没有 P7.5 save-as / Patch handoff 前，
  不宣称 In-memory editor ready。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/*`
- `tests/*`
- CMake 配置

可并行性：
- 可与 P7.5 save-as handoff 的只读调研并行。
- 若 P7.5 要写 blank / erase / tombstone save semantics，同一段落需串行合并。

验收标准：
- 文档明确 future In-memory guardrails 的 public editor API 仍是 future design；
  当前只实现 internal `CellStore` first slice，以及 current public
  `WorkbookEditorOptions` 的 replacement payload guardrail 首片。
- 文档明确 limit options、diagnostic APIs、计量维度和 enforcement 时机。
- 文档明确超限错误需要建议 Streaming 或 Patch。
- 文档明确没有 guardrails 和 P7.5 handoff 前不能宣称 In-memory ready。

禁止项：
- 不把当前 public `WorkbookEditorOptions` 写成 future In-memory workbook /
  worksheet random-edit options；它只限制 `replace_sheet_data()` replacement payload。
- 不定义默认 limit 数值为稳定承诺。
- 不把 `estimated_memory_usage()` 写成精确内存 profiler。
- 不承诺百万行 worksheet 低内存随机访问。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P7.4a internal `CellStoreOptions` guardrail implementation

状态：基础完成。

类型：internal detail header + implementation + focused unit tests；不新增 public API。

目标：给 P7.3a internal `CellStore` 加上最小 `max_cells` 和
`memory_budget_bytes` enforcement，验证超限 mutation 在状态变更前失败，为后续
public editor guardrails 提供可测基础。

输入：
- P7.3a internal `CellStore` / `CellRecord` implementation。
- P7.4 guardrail design。
- 当前 `CellStore::cell_count()` 和 `CellStore::estimated_memory_usage()`。

输出：
- `CellStoreOptions` internal struct，包含 optional `max_cells` 和
  `memory_budget_bytes`。
- `CellStore(CellStoreOptions)` 构造入口和 `options()` 只读视图。
- `CellStore::set_cell()` 在插入或覆盖前检查 cell count 和 estimated memory；
  超限抛出 `FastXlsxError`，并保持既有 sparse records 不变。
- `erase_cell()` 仍只做坐标校验和 record removal，不做 tombstone / save-as semantics。
- `fastxlsx.unit` 覆盖 max_cells 插入拒绝、overwrite 允许、memory budget 插入/覆盖
  拒绝，以及失败不污染 store。

触碰文件：
- `include/fastxlsx/detail/cell_store.hpp`
- `src/cell_store.cpp`
- `tests/test_minimal_xlsx.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`
- 必要时同步 `docs/TASK_PLAN.md`、`docs/NEXT_STEPS.md`、`README.md`、`AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/package_editor.*`
- `src/streaming_writer.*`

可并行性：
- 可与 P7.5 save-as handoff 的只读设计并行。
- 与任何 public `WorkbookEditorOptions` 或 save-as preflight 实现必须串行。

验收标准：
- 默认 `fastxlsx.unit` 覆盖 internal guardrails 并通过。
- 文档明确这只是 internal `CellStore` budget enforcement，不是 public
  `WorkbookEditorOptions`、workbook-level guardrails、save-as preflight 或 precise RSS
  profiler。
- 超限失败不污染 `CellStore` 当前记录。

禁止项：
- 不新增 public editor API。
- 不定义默认 limit 数字。
- 不实现 open/load materialization limits、string/formula pool budget 或 save-as
  assembly preflight。
- 不宣称 In-memory editor ready。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.unit
```

### P7.5 save-as and Patch handoff contract

状态：基础完成。

类型：public API / internal architecture 文档设计；不新增 header / implementation。

目标：冻结 future In-memory editor 的 `save_as(...)` 与 internal Patch / package
rewrite 底座的交接契约，明确 source-backed existing workbook、小文件 materialized
cell edits、unknown/unmodified part preservation、sharedStrings / styles / calc metadata、
blank / erase / tombstone 和输出路径 guard 的边界。

输入：
- P7.1 future editor facade draft。
- P7.2 `CellValue` public value boundary。
- P7.3 internal `CellStore` / `CellRecord` memory model。
- P7.4 guardrails draft。
- P6 dependency policies：sharedStrings/styles、worksheet metadata、linked parts、
  calc metadata 和 unsupported edit failure lanes。
- 当前 internal `PackageReader` / `PackageEditor` / `EditPlan` / `planned_output()`
  只代表内部 Patch 底座，不是 public editor API。

输出：
- `save_as(output_path)` contract：不承诺原地覆盖；输出路径 guard 应延续 current
  PackageEditor save-as 边界，拒绝 exact / path-equivalent source overwrite、空路径、
  缺失父目录、非目录父路径和 directory output。
- handoff source modes：new workbook materialization 与 existing package-backed
  materialization 分开说明；existing package 默认 copy-original 未修改和 unknown entries。
- EditPlan 交接：In-memory worksheet edits 应产生或更新 part-level plan，明确哪些
  worksheet/workbook parts rewrite、哪些 package entries copy-original、哪些显式 omission /
  removal 只是 audit，不做 relationship pruning。
- worksheet cell edits：`CellStore` 到 worksheet XML emission 只负责 cell value /
  style reference 输出；row/column metadata、merged ranges、hyperlinks、tables、drawings
  等关系型或 range metadata 需要独立模型、audit 或 Patch preservation，不能由 cell store
  静默修复。
- blank / erase / tombstone：需要定义 save-as 时是删除 `<c>`、写 blank styled cell、
  还是保留/清除 prior value 的候选规则；P7.5 只冻结 contract，不实现 existing-file
  cell clearing。
- sharedStrings / styles：internal string/formula pools 不等同 source sharedStrings indexes；
  style handles 不等同 foreign style ids。P7.5 应声明 preserve / audit / fail 策略，
  不把索引迁移、style merge 或 styles preservation 写成已实现。
- calc policy：公式或值变更最多请求 full recalculation / calcChain remove-or-preserve
  策略；不求值、不写 cached values、不实现 calcChain rebuild。
- output failure semantics：preflight 或 writer failure 不应写成已提交 package mutation；
  如果无法承诺 atomic output，必须明确 state-after-failure 和 caller recovery。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/*`
- `tests/*`
- CMake 配置

可并行性：
- 可与后续 implementation planning 的只读调研并行。
- 与任何新增 public editor API 或 internal store implementation 写入必须串行，避免
  文档 contract 和代码行为分叉。

验收标准：
- 文档明确 P7.5 是 future handoff contract，不是已实现 editor。
- 文档能回答 source-backed save-as 如何保留 unknown/unmodified parts。
- 文档明确 sharedStrings、styles、calcChain、worksheet relationships 和 range metadata
  是 preserve / audit / fail / future strategy，而不是自动修复。
- 文档明确 output path guard、failure semantics 和 no in-place overwrite。

禁止项：
- 不新增超出当前 `WorkbookEditor` Patch subset 的 public editor code。
- 不把 internal `PackageEditor` 直接暴露为 public API。
- 不宣称 random cell editing、sharedStrings migration、style id migration、relationship
  repair、calcChain rebuild 或 broad existing-file preservation 已完成。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P7.5a internal `CellStore` sheetData emission implementation

状态：基础完成。

类型：internal detail helper + focused unit tests；不新增 public editor API。

目标：给 P7.5 handoff contract 落地首个可测内部输出：把 worksheet-local sparse
`CellStore` 序列化成 standalone `<sheetData>` payload，供未来 internal Patch /
save-as handoff 复用，而不是继续只停留在合同文档。

输入：
- P7.3a internal `CellStore` / `CellRecord` implementation。
- P7.4a internal `CellStoreOptions` guardrail implementation。
- P7.5 save-as and Patch handoff contract。
- 当前 `detail::cell_reference()` / `detail::append_cell_reference()`、
  `detail::append_escaped_xml_text()` / `detail::append_escaped_xml_attribute()`、
  XML escape string helpers 和 finite number output 规则。

输出：
- `detail::cell_store_to_sheet_data_xml(const CellStore&)` 生成完整
  `<sheetData>...</sheetData>` 片段；空 store 输出空 `sheetData`。
- 输出按 `CellStore::records()` 的 row-major 顺序分组 `<row r="...">`。
- number / boolean / formula / text / blank records 分别写成 OpenXML cell XML；
  text 使用 `inlineStr`，公式只写 `<f>` text，blank 写空 `<c/>`。
- optional `StyleId` 只输出非默认 `s="N"`；默认 style id 仍省略。
- `detail::format_number(double)` 和 `detail::append_number(std::string&, double)`
  作为内部公共数字输出 helpers，保持 finite-only OpenXML number text 边界；append
  helper 供 in-memory、CellStore 和 streaming XML buffer 直接追加数字文本。
- `detail::cell_reference(row, column)` 和
  `detail::append_cell_reference(std::string&, row, column)` 作为内部公共坐标输出
  helpers，保持 Excel row/column 上限校验；append helper 供 in-memory、CellStore
  和 streaming XML buffer 直接追加 cell reference。
- `detail::append_escaped_xml_text(std::string&, value)` 和
  `detail::append_escaped_xml_attribute(std::string&, value)` 作为内部公共 XML
  escape append helpers，供 in-memory、CellStore、streaming 和小型 OPC XML
  serializer 直接追加转义内容；`escape_xml_text()` / `escape_xml_attribute()`
  仍保留给需要 owned string 的 replacement 路径。
- `fastxlsx.unit` 覆盖空 store、稀疏行分组、XML escape、空白保留、公式转义、
  explicit blank record 和默认 style omission。

边界：
- 这不是完整 worksheet XML writer，也不生成 `<worksheet>` root、dimension、
  relationships、content types、sharedStrings、styles 或 calc metadata。
- 不迁移 sharedStrings index、不合并 styles、不验证 foreign style id、不修复
  relationships、不实现 existing-file clear semantics。
- 不新增 public `WorkbookEditor` / `WorksheetEditor` / `PackageEditor` API。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.unit
```

### P7.5b internal `CellStore` to PackageEditor sheetData handoff regression

状态：基础完成。

类型：internal integration regression + 文档同步；不新增 public editor API。

目标：把 P7.5a 的 `CellStore` standalone `<sheetData>` emitter 接到当前 internal
by-name `PackageEditor` `sheetData` Patch helper 的回归里，证明该 payload 可以作为
未来 In-memory / Patch handoff 的内部输入，而不是只停留在 unit-level XML 片段。

输入：
- P7.5a internal `cell_store_to_sheet_data_xml(const CellStore&)`。
- 当前 internal `PackageEditor::replace_worksheet_sheet_data_by_name()`。
- 当前 `PackageEditor` calcChain cleanup、workbook `fullCalcOnLoad` 和 output-plan audit。

输出：
- `fastxlsx.package_editor` 构造 worksheet-local `CellStore`，写入 number、inline text、
  formula、boolean 和 explicit blank records。
- 测试使用 `cell_store_to_sheet_data_xml()` 生成完整 `<sheetData>` payload，并传给
  `replace_worksheet_sheet_data_by_name("Sheet1", ...)`。
- 回归验证 worksheet `LocalDomRewrite`、bounded local rewrite note、formula payload
  dependency audit、workbook full calculation request、默认 stale calcChain removal、
  unknown entry bytes preservation 和输出 package 可由 `PackageReader` 重读。

边界：
- 这仍是 internal handoff regression；当前 `WorkbookEditor::replace_sheet_data()` /
  `save_as()` 只复用其 whole-`<sheetData>` facade，不代表 random cell editing。
- 不新增超出当前 subset 的 public `WorkbookEditor` / `WorksheetEditor` /
  `PackageEditor` API。
- 不迁移 sharedStrings index、不合并 styles、不验证 foreign style id、不修复
  relationships、definedNames、table ranges 或 drawing metadata。
- 不把当前 bounded local `sheetData` helper 写成大文件低内存 streaming transformer。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_editor
```

## P8 - Large Worksheet Controlled Editing

状态：基础完成；P8.1-P8.5 已冻结大 worksheet 受控编辑边界、event reader token、
transformer contract、stream rewrite / `EditPlan` handoff，并补入首个 bounded local
template-fill fixture。P8.6 已落地首个 internal worksheet event reader 实现切片；
P8.7 已落地首个 internal transformer action model 切片；P8.8 已落地首个 internal
worksheet replacement output chunk emitter 切片；P8.9 已落地首个 internal bounded
`PackageEditor` cell-replacement handoff 切片；P8.10 已落地 bounded cell replacement
handoff 的 top-level worksheet dimension refresh 切片；P8.11 已落地 replacement cell
payload preflight 切片；P8.12 已落地 internal package-entry chunked replacement source
foundation；P8.13 已落地 internal worksheet replacement chunk handoff；P8.14 已落地
internal cell replacement staged chunk handoff；P8.15 已落地 invalid replacement
cell payload no-state-pollution 回归；P8.16 已落地 cell replacement 输出侧
file-backed stream handoff。后续 PackageReader 输入侧 streaming、range/row 级
transformer、dependency repair 或完整大文件低内存编辑仍必须继续按任务模板拆分，
不能把当前 reader、action scanner、chunk emitter、PackageEditor handoff、dimension
refresh、payload preflight、chunked replacement source foundation、worksheet chunk
handoff、cell replacement file-backed output handoff、invalid payload state hygiene 或
bounded local fixture 写成完整低内存大文件路径。

目标：支持 sheet replacement、range patch、template fill 等受控大 worksheet 编辑，
同时避免把大型 `worksheet.xml` 载入 DOM 或完整 cell matrix。

子任务：
- P8.1 capability boundary and pipeline draft：基础完成。
- P8.2 worksheet event reader token model：基础完成。
- P8.3 row/cell transformer contract：基础完成。
- P8.4 stream rewrite output and `EditPlan` integration：基础完成。
- P8.5 first controlled edit fixture：template fill or bounded range patch：基础完成。
- P8.6 internal worksheet event reader first implementation slice：基础完成。
- P8.7 internal row/cell transformer action model first implementation slice：基础完成。
- P8.8 internal worksheet replacement output chunk emitter first implementation slice：基础完成。
- P8.9 internal bounded PackageEditor cell-replacement handoff first implementation slice：基础完成。
- P8.10 internal bounded worksheet dimension refresh for cell replacement handoff：基础完成。
- P8.11 internal replacement cell payload preflight：基础完成。
- P8.12 internal package-entry chunked replacement source foundation：基础完成。
- P8.13 internal worksheet replacement chunk handoff：基础完成。
- P8.14 internal cell replacement staged chunk handoff：基础完成。
- P8.15 internal cell replacement invalid payload no-state-pollution：基础完成。
- P8.16 internal cell replacement file-backed output stream handoff：基础完成。

验收：
- 文档明确大 worksheet 编辑走 event reader → transformer → stream writer。
- 不承诺任意随机 cell editing 或百万行 worksheet DOM。
- 说明 sharedStrings、styles、relationships、tables、drawings、definedNames、
  calc metadata 和 unknown parts 的 preserve / audit / fail 边界。

### P8.1 capability boundary and pipeline draft

状态：基础完成。

类型：architecture / API 文档设计；不新增 header / implementation。

目标：冻结 P8 受控大 worksheet 编辑的能力边界、非目标和处理管线，明确它不同于
P7 In-memory 随机编辑，也不同于当前 bounded `sheetData` local rewrite helper。

输入：
- `docs/PERFORMANCE_TARGETS.md` 对大文件受控编辑和禁止完整 cell matrix 的要求。
- `docs/EDITING_MODEL.md` 中“大型 worksheet 编辑”的 event reader → transformer →
  stream writer 形态。
- 当前 internal Patch `EditPlan`、`DependencyAnalyzer`、worksheet rewrite 和
  `replace_worksheet_sheet_data()` 的边界。
- P6 dependency policies 和 P7 save-as / Patch handoff contract。

输出：
- P8 能力边界：sheet replacement、bounded range patch、template fill 和 row/cell
  streaming transformation；不支持任意 O(1) random cell editing。
- pipeline 草案：source worksheet event reader → row/cell transformer →
  streaming worksheet writer → package `EditPlan` / output plan。
- metadata 边界：保留或审计 sheetPr、dimension、sheetViews、cols、mergeCells、
  autoFilter、dataValidations、conditionalFormatting、hyperlinks、tableParts、drawings、
  comments、OLE/control、printerSettings 等 worksheet metadata；不静默修复或迁移。
- dependency 边界：sharedStrings indexes、style ids、definedNames、tables、drawings、
  calcChain 和 workbook calc metadata 只按 preserve / audit / fail / request-recalc
  策略处理。
- implementation sequencing：P8.2 先定义 event reader token model，P8.3 定义 transformer，
  P8.4 接入 stream rewrite / `EditPlan`，P8.5 才选择首个 fixture。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/*`
- `tests/*`
- CMake 配置

可并行性：
- P8.2 event reader token 调研可与 P8.3 transformer API 调研并行。
- 写入同一 public boundary 文档或实现 streaming rewrite 时必须串行。

验收标准：
- 文档能回答 P8 与 P7 In-memory、P4/P6 Patch helpers 的差异。
- 文档明确大 worksheet 编辑不使用 DOM 或 full cell matrix。
- 文档明确哪些 metadata 和 linked parts 只 preserve / audit / fail。
- 文档给 P8.2-P8.5 提供清晰顺序。

禁止项：
- 不新增 `WorksheetReader`、`WorksheetRewriter`、`TemplateEditor` 或 public Patch API。
- 不宣称当前已有大文件 streaming worksheet transformer。
- 不把 bounded local `sheetData` rewrite 写成低内存大文件路径。
- 不承诺 sharedStrings/style migration、relationship repair、table resize 或 formula rewrite。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P8.2 worksheet event reader token model

状态：基础完成。

类型：architecture / internal API 文档设计；不新增 header / implementation。

目标：冻结 future worksheet event reader 的 token vocabulary、payload 边界、坐标校验和
pass-through 语义，为 P8.3 transformer 和 P8.4 stream rewrite 接口提供稳定输入，同时
避免把大型 worksheet 解析成 DOM 或 full cell matrix。

输入：
- P8.1 controlled large worksheet editing boundary。
- 当前 streaming writer 的 row/cell value、formula、style id、sharedStrings 和 dimension
  边界。
- 当前 internal `PartRewritePlanner` / `EditPlan` 的 worksheet stream rewrite 审计语义。
- `docs/EDITING_MODEL.md` 中 old sheet.xml event reader → row/cell transformer →
  new sheet.xml stream writer 形态。

输出：
- token categories 草案：document/prolog、worksheet root start/end、metadata pass-through、
  `sheetData` start/end、row start/end、cell value、cell raw fallback、relationship-bearing
  metadata reference、error / unsupported token。
- row token 草案：row number、raw row attributes、height/custom-height metadata、
  source byte/span hint 或 raw attribute preservation 语义；不需要持有整张 worksheet。
- cell token 草案：cell reference、row/column index、OpenXML cell type token、style id
  raw token、formula text、numeric/string/boolean/error/raw payload、raw attribute slice 和
  unsupported cell preservation/fail flag。
- pass-through 边界：reader 可以把未理解的 metadata XML 作为 bounded raw event 交给
  writer 原样输出或交给 `DependencyAnalyzer` audit；不能静默 repair namespace、relationship
  ids、table ranges、drawing anchors 或 formulas。
- payload/memory 边界：token 生命周期限定在当前 event / row / bounded lookahead；大型
  inline string、rich text、extLst 或未知 child 超过 payload limit 时必须 preserve raw、
  stream-through 或 fail，不得累计完整 worksheet。
- validation 边界：可校验 row/cell reference、Excel row/column limit、start/end element
  nesting 和 required local names；不做完整 worksheet schema validation。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/*`
- `tests/*`
- CMake 配置

可并行性：
- P8.3 transformer contract 可并行只读调研，但写入同一 token 字段时需串行。
- P8.4 stream rewrite integration 必须等 token model 稳定后再写。

验收标准：
- 文档明确 event reader token model 是 future internal design，不是已实现 API。
- 文档明确 token 不持有完整 worksheet、完整 row matrix 或跨 sheet state。
- 文档明确 raw pass-through 与 unsupported/fail 边界。
- 文档给 P8.3 transformer 和 P8.4 stream writer 提供字段级输入。

禁止项：
- 不新增 `WorksheetReader`、`EventReader`、`WorksheetRewriter` 代码。
- 不把 token model 写成 public API。
- 不承诺完整 worksheet schema validation、namespace repair 或 relationship repair。
- 不为读取单元格方便而引入 full row/cell cache。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P8.3 row/cell transformer contract

状态：基础完成。

类型：architecture / internal API 文档设计；不新增 header / implementation。

目标：冻结 future row/cell transformer 如何消费 P8.2 tokens、声明选择器、产生输出动作、
控制 lookahead / memory，并把 unsupported edits、dependency notes 和 calc policy 交给
P8.4 stream rewrite / `EditPlan`。

输入：
- P8.1 controlled large worksheet editing boundary。
- P8.2 worksheet event reader token model。
- P6 dependency policies：sharedStrings/styles、worksheet metadata、linked parts、
  definedNames/formulas/calc metadata 和 unsupported edits matrix。
- 当前 `WorksheetWriter` row/cell value serialization、style id、formula fullCalcOnLoad
  和 dimension tracking 边界。

输出：
- transformer input contract：按 source order 消费 document / metadata / row / cell tokens；
  token 只在当前 callback / bounded lookahead 内有效。
- selector contract：range patch、template fill 和 row/cell transformation 必须在 rewrite
  开始前声明目标范围、placeholder pattern 或 predicate；不得要求完整 worksheet scan 后
  回头修改已输出 rows。
- output action 草案：pass-through、replace cell、replace row、insert row before/after、
  delete row/cell candidate、emit raw metadata、request recalculation、fail unsupported。
- ordering 边界：输出保持 row-major deterministic order；transformer 不能在 row 已交给
  stream writer 后再修改该 row。
- memory 边界：允许 bounded row buffer / bounded lookahead / selector index；禁止 full
  cell matrix、unbounded row cache 或 DOM。
- dependency/audit 边界：transformer 只能报告 sharedStrings/style/calc/relationship/range
  metadata 依赖；不迁移 shared string indexes、不合并 styles、不修复 relationships。
- failure 边界：preflight 阶段应拒绝明显 unsupported selectors；streaming 阶段遇到
  unsupported token 时 abort rewrite，并由 P8.4 定义 output artifact / state-after-failure。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/*`
- `tests/*`
- CMake 配置

可并行性：
- P8.4 stream rewrite / `EditPlan` integration 可并行只读调研。
- 修改 transformer output actions 与 P8.4 writer contract 时必须串行。

验收标准：
- 文档明确 transformer 是 future internal contract，不是 public API。
- 文档明确 input token lifetime、output actions、ordering 和 memory 边界。
- 文档明确 unsupported edit、dependency audit 和 calc policy 不由 transformer 静默修复。
- 文档给 P8.4 stream rewrite 提供可消费的 action / diagnostic 形态。

禁止项：
- 不新增 transformer 代码或 callback public API。
- 不承诺 arbitrary random cell edit、full-row lookback 或 full worksheet scan。
- 不实现 sharedStrings/style migration、relationship repair、table resize 或 formula rewrite。
- 不把 failed streaming rewrite 写成已提交 package mutation。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P8.4 stream rewrite output and `EditPlan` integration

状态：基础完成。

类型：architecture / internal API 文档设计；不新增 header / implementation。

目标：冻结 future stream rewrite 如何消费 P8.3 transformer actions、生成 worksheet part
输出、接入 `EditPlan` / planned output diagnostics，并定义成功提交、失败回滚、calc policy
和 dependency audit 边界。

输入：
- P8.1 controlled large worksheet editing boundary。
- P8.2 worksheet event reader token model。
- P8.3 row/cell transformer contract。
- 当前 internal `PartRewritePlanner`、`EditPlan`、`PackageEditorOutputPlan`、
  `DependencyAnalyzer` 和 calcChain / fullCalcOnLoad helper 语义。
- P6 dependency policies：sharedStrings/styles、worksheet metadata、linked parts、
  definedNames/formulas/calc metadata 和 unsupported edits matrix。
- 当前 `WorksheetWriter` row/cell serialization、dimension tracking、formula
  recalculation metadata 和 package-entry audit 边界。

输出：
- stream rewrite input contract：只接受 P8.3 ordered actions 和 pass-through raw events；
  不重新扫描完整 worksheet，不要求 transformer 回写历史 row。
- worksheet output contract：输出为 staged worksheet part source；只有 rewrite 完成、
  最小 worksheet root/order 检查和 dependency policy 决策完成后，才能进入 active
  `EditPlan`。
- action consumption 边界：pass-through / replace cell / replace row / bounded insert /
  delete candidate / emit raw / request recalculation / fail unsupported 各自如何影响
  worksheet bytes、dimension tracking、calc policy 和 diagnostics。
- `EditPlan` 集成边界：记录 worksheet part `StreamRewrite`、copy-original linked parts、
  content types / relationships package-entry side effects、removed calcChain audit、
  `WorksheetPayloadDependencyAudit` 和 `RelationshipTargetAudit`。
- planned output diagnostics：暴露 active stream rewrite entry、rewrite reason、selector
  / target worksheet context、copy-original / omitted entries、calcChain action 和
  failure diagnostics；不把 diagnostic snapshot 写成 public output planner。
- failure 边界：preflight fail 和 streaming fail 都不得污染 active `EditPlan`、manifest、
  package-entry audit、calc policy 或输出 package；临时 artifact 只能作为未提交实现细节。
- memory 边界：stream writer 只维护 bounded row/output buffers 和 incremental dimension
  state；禁止 worksheet DOM、full cell matrix、unbounded raw XML cache。

触碰文件：
- `docs/TASK_BREAKDOWN.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/*`
- `tests/*`
- CMake 配置

可并行性：
- P8.5 first fixture 可以并行只读调研。
- 修改 stream rewrite action semantics、`EditPlan` output diagnostics 或 failure state
  时必须串行。

验收标准：
- 文档明确 P8.4 是 future internal stream rewrite / `EditPlan` contract，不是实现。
- 文档明确 staged worksheet output 何时进入 active `EditPlan`，以及失败不污染状态。
- 文档明确 action consumption、dimension/calc policy、dependency audit 和 planned output
  diagnostics。
- 文档明确不做 sharedStrings/style migration、relationship repair、table resize、formula
  rewrite、calcChain rebuild 或 public output planner。

禁止项：
- 不新增 `WorksheetRewriter`、stream writer、callback 或 public Patch API。
- 不把 current bounded `sheetData` local rewrite 写成低内存 streaming rewrite。
- 不承诺 failed rewrite 会产生可保存 package mutation。
- 不实现 `CalcChainAction::Rebuild`、sharedStrings/style migration、relationship repair、
  table resize 或 formula rewrite。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P8.5 first controlled edit fixture：template fill or bounded range patch

状态：基础完成。

类型：internal fixture / test + docs；不新增 public header / implementation。

目标：用当前 internal by-name `sheetData` Patch helper 固定首个受控编辑 fixture：
template-fill 风格地替换目标 worksheet 的 `<sheetData>`，同时证明该路径仍是 bounded local
rewrite，不是 P8 future low-memory stream transformer。

输入：
- P8.1-P8.4 的 controlled large worksheet editing / token / transformer / stream rewrite
  合同。
- 当前 internal `PackageEditor::replace_worksheet_sheet_data_by_name()`。
- 当前 `WorkbookWriter` 可生成含 sharedStrings / styles 的 source workbook。
- P6 dependency policies：sharedStrings/styles preserve / audit、不迁移、不修复。

输出：
- 新增 `fastxlsx.package_editor` fixture：
  `test_package_editor_controlled_template_fill_fixture_uses_bounded_sheet_data_patch()`。
- fixture 使用 FastXLSX writer 生成带占位符的 source workbook，通过 by-name
  `sheetData` patch 写入 caller-supplied filled row。
- 测试验证 target worksheet `LocalDomRewrite`、workbook calc metadata rewrite、
  fullCalcOnLoad、无 calcChain 创建、untouched worksheet / content types / relationships /
  sharedStrings / styles byte-preserved。
- 测试验证 replacement 使用 inline string 时不声称 sharedStrings index migration，
  且旧 placeholder sharedStrings 被保留而不是 pruning / repair。
- 文档明确这是 current bounded local fixture，只给 P8 future streaming rewrite 提供
  baseline，不代表 low-memory transformer、placeholder parser 或 public API。

触碰文件：
- `tests/test_package_editor.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/*`
- CMake 配置

可并行性：
- 后续真正 event-reader / stream-rewrite implementation 需要单独串行拆分。
- 与 P9/new-workbook writer feature 写入同一测试文件时需串行。

验收标准：
- 默认 `fastxlsx.package_editor` 覆盖 template-fill fixture 并通过。
- fixture 清楚暴露当前 helper 的 bounded local rewrite 限制。
- fixture 证明 sharedStrings/styles preserve/audit，不做迁移、合并、pruning 或 repair。
- 文档不把该 fixture 写成 P8 streaming transformer 已实现。

禁止项：
- 不新增 public `TemplateEditor` / `WorksheetRewriter` / Patch API。
- 不实现 placeholder parser、range patch engine 或 event reader。
- 不把 current `replace_worksheet_sheet_data_by_name()` 写成低内存大文件路径。
- 不迁移 shared string indexes、不合并 styles、不修复 relationships、不重写 formulas。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P8.6 internal worksheet event reader first implementation slice

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public header / Patch API。

目标：落地 P8.2 token model 的首个内部事件读取器，让 worksheet XML 可按源顺序发出
prolog / worksheet root / metadata / `sheetData` / row / cell / value token，为后续
transformer 提供真实输入形态，同时继续避免 worksheet DOM、full cell matrix 和
PackageEditor 语义接入。

输入：
- P8.2 worksheet event reader token model。
- P8.3 transformer contract 对 source-order token、bounded lifetime 和 raw pass-through 的要求。
- 当前 XML helper 和 `FastXlsxError` 失败路径。

输出：
- 新增 internal `include/fastxlsx/detail/worksheet_event_reader.hpp` 和
  `src/worksheet_event_reader.cpp`。
- `scan_worksheet_events()` 以 callback 方式发出非 owning token view；string_view
  生命周期绑定到 source worksheet XML buffer。
- 覆盖 XML declaration / processing instruction / comment、qualified worksheet /
  row / cell local-name、raw metadata pass-through、inline string / value text 和
  malformed boundary 拒绝。
- 新增 `fastxlsx.worksheet_event_reader` CTest 目标。

触碰文件：
- `include/fastxlsx/detail/worksheet_event_reader.hpp`
- `src/worksheet_event_reader.cpp`
- `tests/test_worksheet_event_reader.cpp`
- `CMakeLists.txt`
- `tests/CMakeLists.txt`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/package_editor.*`
- `tests/test_package_editor.cpp`

可并行性：
- 后续 transformer contract implementation 可只读调研并行。
- 修改 token vocabulary、callback lifetime 或 stream rewrite handoff 时必须串行。

验收标准：
- `fastxlsx.worksheet_event_reader` 覆盖核心 token 顺序、prefix/local-name、raw value
  text 和失败边界。
- 默认完整 CTest 通过。
- 文档明确这只是 internal first reader slice，不是 public API、full XML parser、
  schema validation、relationship repair、transformer 或 low-memory package rewrite。

禁止项：
- 不新增 public `WorksheetReader` / `WorksheetRewriter` / `TemplateEditor`。
- 不接入 `PackageEditor` active `EditPlan`。
- 不实现 row/cell transformer、placeholder parser、range patch engine 或 stream writer。
- 不解码 XML entity、不验证完整 worksheet schema、不修复 namespaces / relationships。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P8.7 internal row/cell transformer action model first implementation slice

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public header / Patch API。

目标：落地 P8.3 transformer contract 的首个内部 action scanner，让 bounded cell
replacement selectors 能映射到 P8.6 event reader 的 source-order token，并发出
`PassThrough` / `ReplaceCell` actions，为后续 stream writer handoff 提供真实 action
形态，同时不生成 worksheet XML、不接入 `PackageEditor` active `EditPlan`。

输入：
- P8.6 internal worksheet event reader。
- P8.3 transformer contract 对 selector preflight、source-order output actions 和
  missing target diagnostics 的要求。
- P8.4 stream rewrite handoff 对 action stream 和 failure-before-commit 的边界。

输出：
- 新增 internal `include/fastxlsx/detail/worksheet_transformer.hpp` 和
  `src/worksheet_transformer.cpp`。
- `scan_cell_replacement_actions()` 建立 bounded selector index，拒绝空 selector、
  空 replacement payload 和重复 selector。
- scanner 按 source worksheet 顺序发出 `ReplaceCell` action，跳过目标 cell 原始 payload，
  对未命中的 selector 返回 `missing_cell_references` diagnostics。
- 新增 `fastxlsx.worksheet_transformer` CTest 目标。

触碰文件：
- `include/fastxlsx/detail/worksheet_transformer.hpp`
- `src/worksheet_transformer.cpp`
- `tests/test_worksheet_transformer.cpp`
- `CMakeLists.txt`
- `tests/CMakeLists.txt`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/package_editor.*`
- `tests/test_package_editor.cpp`

可并行性：
- 后续 stream writer/output staging 只能在 action model 稳定后串行推进。
- 额外 selector 类型或 diagnostics 可单独补小切片，但修改 action lifetime 需串行。

验收标准：
- `fastxlsx.worksheet_transformer` 覆盖 source-order replacement actions、目标 cell
  原始 payload consumption、missing selector diagnostics 和 duplicate/empty preflight。
- 默认完整 CTest 通过。
- 文档明确这只是 internal transformer action model，不是 public API、worksheet writer、
  dimension recalculation、dependency repair、stream rewrite output 或 PackageEditor commit。

禁止项：
- 不新增 public `WorksheetTransformer` / `WorksheetRewriter` / `TemplateEditor`。
- 不生成 rewritten worksheet XML 或临时 worksheet artifact。
- 不接入 `PackageEditor` active `EditPlan`。
- 不实现 placeholder parser、range patch engine、dimension update、sharedStrings/style
  migration、relationship repair 或 calcChain rebuild。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P8.8 internal worksheet replacement output chunk emitter first implementation slice

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public header / Patch API。

目标：在 P8.7 action model 上落地首个 internal worksheet replacement output chunk
emitter，让 bounded cell replacement 能通过 callback 输出 pass-through source XML chunks
和 caller-supplied replacement cell XML，为后续 staged stream writer / package-entry
handoff 提供真实输出形态，同时不接入 `PackageEditor` active `EditPlan`。

输入：
- P8.6 internal worksheet event reader。
- P8.7 internal row/cell transformer action model。
- P8.4 stream rewrite handoff 对 pass-through raw events、failure-before-commit 和
  package-entry staged output 的边界。

输出：
- `scan_worksheet_events()` 新增 `RawText` 和 `CellValueMarkup` token，保留 source
  worksheet 中的 raw text separators 以及 `<v>` / `<t>` / `<f>` value wrapper markup，
  供 pass-through reconstruction 使用。
- 新增 internal `emit_cell_replacement_worksheet()`，复用
  `scan_cell_replacement_actions()`，把 `PassThrough` action 的 raw XML 和 `ReplaceCell`
  action 的 caller replacement payload 逐 chunk 发给 callback。
- `fastxlsx.worksheet_event_reader` 覆盖 raw text separator token。
- `fastxlsx.worksheet_transformer` 覆盖 replacement 输出后 raw text 和非目标 cell value
  markup 的 pass-through preservation。

触碰文件：
- `include/fastxlsx/detail/worksheet_event_reader.hpp`
- `src/worksheet_event_reader.cpp`
- `include/fastxlsx/detail/worksheet_transformer.hpp`
- `src/worksheet_transformer.cpp`
- `tests/test_worksheet_event_reader.cpp`
- `tests/test_worksheet_transformer.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/package_editor.*`
- `tests/test_package_editor.cpp`
- CMake 配置

可并行性：
- 后续 staged worksheet output source、dimension tracking 和 `EditPlan` handoff 必须在
  chunk emitter 稳定后串行推进。
- 额外 selector 类型、dependency diagnostics 或 replacement payload validation 可单独补
  小切片，但修改 token/action lifetime 需串行。

验收标准：
- `fastxlsx.worksheet_event_reader` 覆盖 raw text 和 value text token 边界。
- `fastxlsx.worksheet_transformer` 覆盖 callback chunk output、replacement payload
  substitution、missing selector diagnostics 和 raw text / value markup pass-through。
- 默认完整 CTest 通过。
- 文档明确这只是 internal output chunk emitter，不是 public API、full stream writer、
  package-entry staged output、dimension recalculation、dependency repair 或
  `PackageEditor` / `EditPlan` commit。

禁止项：
- 不新增 public `WorksheetTransformer` / `WorksheetRewriter` / `TemplateEditor`。
- 不创建 temporary worksheet artifact 或 package entry source。
- 不接入 `PackageEditor` active `EditPlan`。
- 不实现 dimension update、sharedStrings/style migration、relationship repair、
  table resize、formula rewrite、calcChain rebuild、placeholder parser 或 range patch engine。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P8.9 internal bounded PackageEditor cell-replacement handoff first implementation slice

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public header / Patch API。

目标：把 P8.8 output chunk emitter 接到 internal `PackageEditor` 的受控 Patch 路径：
caller 提供 bounded cell replacement selectors，helper 读取当前 planned worksheet XML，
通过 `emit_cell_replacement_worksheet()` 生成 rewritten worksheet XML，再委托现有
worksheet replacement 路径处理 calcChain/fullCalcOnLoad、payload audit、relationship
audit 和 package output plan。当前 helper 仍物化完整 worksheet XML，只是 handoff
fixture，不是最终 package-entry staged stream writer。

输入：
- P8.6 internal worksheet event reader。
- P8.7 internal row/cell transformer action model。
- P8.8 internal output chunk emitter。
- 当前 internal `PackageEditor::replace_worksheet_part()` / by-name resolver /
  calcChain cleanup / planned output audit。

输出：
- 新增 internal `PackageEditor::replace_worksheet_cells()` 和
  `replace_worksheet_cells_by_name()`。
- helper 使用 source/planned workbook sheet catalog resolver 定位 worksheet part。
- helper 拒绝空 replacement list、missing cell selector，以及超过 bounded local rewrite
  限制的 source / output worksheet XML；失败不污染 active `EditPlan`、manifest、
  package-entry audit、calc policy 或输出 bytes。
- 成功路径把 rewritten worksheet 接入现有 worksheet replacement 路径，保留
  fullCalcOnLoad / calcChain removal / dependency audit / relationship audit 语义，并把
  target worksheet 记录为 bounded local rewrite reason。
- 新增 `fastxlsx.package_editor` 回归：by-name cell replacement handoff 写出 transformed
  worksheet XML、移除 stale calcChain、保留 unknown bytes，并验证 missing selector
  failure-before-state-change。

触碰文件：
- `src/package_editor.hpp`
- `src/package_editor.cpp`
- `tests/test_package_editor.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/streaming_writer.cpp`
- CMake 配置

可并行性：
- 后续 package-entry staged output source、true file-backed stream writer、dimension
  recalculation 和 dependency policy 扩展必须单独串行推进。
- 额外 selector 类型、range patch、placeholder parser 或 output artifact source 可单独补
  小切片，但修改 `PackageEditor` state transition 或 failure-before-state-change 需串行。

验收标准：
- `fastxlsx.package_editor` 覆盖 by-name bounded cell replacement handoff 成功路径和
  missing target 失败路径。
- 默认完整 CTest 通过。
- 文档明确这只是 internal bounded handoff，不是 public API、低内存 package-entry
  staged stream writer、dimension recalculation、dependency repair、sharedStrings/style
  migration、relationship repair、table resize、formula rewrite 或 calcChain rebuild。

禁止项：
- 不新增 public `WorkbookEditor` / `WorksheetEditor` / `PackageEditor` API。
- 不创建真正 package-entry staged stream output source。
- 不实现 dimension update、sharedStrings/style migration、relationship repair、
  table resize、formula rewrite、calcChain rebuild、placeholder parser 或 range patch engine。
- 不把 materialized worksheet XML handoff 写成低内存大文件路径。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_editor --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P8.10 internal bounded worksheet dimension refresh for cell replacement handoff

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public header / Patch API。

目标：补齐 P8.9 bounded cell-replacement handoff 的最小 dimension tracking：在
replacement chunks 生成 rewritten worksheet XML 后，用 event reader 扫描 output cell refs，
计算 top-level worksheet `<dimension ref="..."/>`，并替换已有 stale dimension 或在
worksheet root 后插入新的 dimension。该切片只刷新 top-level dimension，不修复其他
range-bearing metadata。

输入：
- P8.6 internal worksheet event reader。
- P8.8 internal output chunk emitter。
- P8.9 internal bounded `PackageEditor` cell-replacement handoff。
- 当前 `detail::range_reference()` / Excel row-column limit 语义。

输出：
- `PackageEditor::replace_worksheet_cells()` 在状态变更前刷新 rewritten worksheet
  的 top-level `<dimension>`。
- dimension refresh 解析 emitted cell refs，支持 existing self-closing dimension、
  non-self-closing dimension 的整体替换，以及无 dimension 时在 worksheet root 后插入。
- 失败路径仍在 `replace_worksheet_part_impl()` 之前抛出，不污染 active `EditPlan`、
  manifest、package-entry audit、calc policy 或输出 bytes。
- `fastxlsx.package_editor` 回归覆盖 no-dimension input 插入 `<dimension ref="A1"/>`，
  以及 stale `<dimension ref="A1"/>` 刷新为 `A1:C3`。

触碰文件：
- `src/package_editor.cpp`
- `tests/test_package_editor.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/streaming_writer.cpp`
- CMake 配置

可并行性：
- 后续 package-entry staged output source 和 true file-backed stream writer 必须串行。
- table range、autoFilter、definedNames、formula reference、drawing anchor、mergeCells、
  validations 和 conditional formatting 等 range metadata 需要单独按 dependency policy
  拆分，不能混入本切片。

验收标准：
- `fastxlsx.package_editor` 覆盖 inserted/refreshed dimension 的 XML 输出。
- 默认完整 CTest 通过。
- 文档明确这只是 bounded top-level dimension refresh，不是 full dimension/range metadata
  recalculation、public API、低内存 package-entry staged stream writer、dependency repair、
  sharedStrings/style migration、relationship repair、table resize、formula rewrite 或
  calcChain rebuild。

禁止项：
- 不新增 public `WorkbookEditor` / `WorksheetEditor` / `PackageEditor` API。
- 不创建真正 package-entry staged stream output source。
- 不更新 autoFilter、tables、definedNames、formulas、drawings、mergeCells、validations、
  conditional formatting 或其他 range-bearing metadata。
- 不把 materialized worksheet XML dimension refresh 写成低内存大文件路径。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_editor --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P8.11 internal replacement cell payload preflight

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public header / Patch API。

目标：收紧 P8.7/P8.8/P8.9 bounded cell replacement 的 replacement payload 边界：
在 transformer 建立 replacement index 前预检 caller-supplied cell XML，要求首个元素是
`<c>` 或带前缀的 `*:c` cell element，且存在未命名空间 `r` attribute，并且该 `r`
必须与 `WorksheetCellReplacement::cell_reference` selector 完全一致。

输入：
- P8.6 internal worksheet event reader。
- P8.7 internal transformer action model。
- P8.8 internal worksheet replacement output chunk emitter。
- P8.9/P8.10 internal bounded `PackageEditor` cell-replacement handoff。

输出：
- `scan_cell_replacement_actions()` / `emit_cell_replacement_worksheet()` 在 action emission
  前拒绝非 cell root、缺失 unqualified `r`、qualified-only `x:r` 或 selector / payload
  `r` 不一致的 replacement payload。
- root local-name 检查接受 prefixed cell element，例如 `<x:c r="A1">`，但不做 namespace
  declaration validation。
- `fastxlsx.worksheet_transformer` 回归覆盖 invalid root、missing `r`、qualified-only
  `x:r`、mismatched `r` 和 prefixed `<x:c>` 正向 payload。
- internal header 注释说明 replacement XML 已有窄 root / reference preflight，但仍不做
  full cell schema validation。

触碰文件：
- `include/fastxlsx/detail/worksheet_transformer.hpp`
- `src/worksheet_transformer.cpp`
- `tests/test_worksheet_transformer.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/package_editor.cpp`
- `src/streaming_writer.cpp`
- CMake 配置

可并行性：
- 后续 dependency audit / sharedStrings-style policy 可并行只读调研。
- replacement payload schema、formula/dependency audit、relationship repair、package-entry
  staged stream writer 与 public API 必须拆成独立任务，不能混入本切片。

验收标准：
- `fastxlsx.worksheet_transformer` 覆盖 payload preflight 成功和失败路径。
- `fastxlsx.package_editor` 继续通过，证明 handoff 复用 transformer preflight 不破坏现有路径。
- 默认完整 CTest 通过。
- 文档明确这只是 internal bounded replacement payload preflight，不是 full XML parser、
  full cell schema validation、namespace repair、public API、dependency repair、
  relationship repair、package-entry staged stream writer 或低内存大文件编辑完成。

禁止项：
- 不新增 public `WorkbookEditor` / `WorksheetEditor` / `PackageEditor` API。
- 不解析或验证完整 cell schema。
- 不迁移 sharedStrings index、不合并 styles、不重写 formula dependencies。
- 不修复 worksheet relationships、table/drawing metadata、range-bearing metadata 或 calcChain。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R "fastxlsx.worksheet_transformer|fastxlsx.package_editor" --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P8.12 internal package-entry chunked replacement source foundation

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public header / Patch API。

目标：为后续 package-entry staged worksheet stream rewrite 铺设内部 payload 形态：
`PackageEditor` 的 ordinary part replacement state 可以持有 `PackageEntryChunk`
memory/file chunks，并在 `save_as()` materialize output package 时直接交给
`PackageWriter`，避免 staged payload 被强制折叠成单个 `std::string`。

输入：
- P8.4 staged worksheet output contract。
- P8.8 output chunk emitter 对 pass-through chunks 的后续需求。
- 当前 `PackageWriter` / `PackageEntry` 已支持 memory/file chunks。
- 当前 `PackageEditor` ordinary part replacement / planned output / save_as pipeline。

输出：
- `PackagePartReplacement` 支持可选 chunk payload。
- 新增 internal `PackageEditor::replace_part_chunks()`，记录 existing part 的
  `StreamRewrite` staged chunk replacement。
- `materialize_planned_output_entry()` 对 chunk payload 生成 chunked `PackageEntry`；
  普通 `replace_part()` 会清理同一路径 stale chunks。
- `current_planned_part_data()` / `current_planned_part_data_size()` 遇到 staged chunk
  part 时显式失败，避免现有 bounded local DOM helpers 误把 chunked payload 当作
  可物化 XML。
- `fastxlsx.package_editor` 回归覆盖 memory/file/memory chunks 写出 worksheet part、
  unknown entry preservation、planned output `StreamRewrite` 状态，以及同一路径后续
  string replacement 清理 stale chunks。

触碰文件：
- `src/package_editor.hpp`
- `src/package_editor.cpp`
- `tests/test_package_editor.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/streaming_writer.cpp`
- `src/package_writer.cpp`
- CMake 配置

可并行性：
- 后续 cell replacement staged output handoff、true file-backed worksheet stream writer
  和 dependency audit policy 必须串行推进。
- `PackageWriter` backend hardening、benchmark 和 public API 设计可并行只读调研。

验收标准：
- `fastxlsx.package_editor` 覆盖 chunked replacement output 和 same-path string override。
- 默认完整 CTest 通过。
- 文档明确这只是 internal package-entry chunked payload foundation，不是 public API、
  cell replacement low-memory handoff、full worksheet stream writer、dependency repair、
  relationship repair、range metadata recalculation 或完整 large-file editing。

禁止项：
- 不新增 public `WorkbookEditor` / `WorksheetEditor` / `PackageEditor` API。
- 不把 `replace_worksheet_cells()` 改写成低内存 package-entry staged stream writer。
- 不在 chunked payload 上运行 XML schema validation、dependency repair 或 calc metadata
  mutation。
- 不修改 `PackageWriter` backend 行为或 CMake 依赖。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_editor --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P8.13 internal worksheet replacement chunk handoff

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public header / Patch API。

目标：把 P8.12 chunked package-entry replacement source 接到现有 worksheet replacement
helper 的内部路径上：caller 提供 materialized worksheet XML 供当前 validation /
dependency audit / relationship audit / calc metadata 使用，同时提供最终 output chunks，
`PackageEditor` 在现有 worksheet replacement 成功后把 target worksheet payload 切换为
`PackageEntryChunk` memory/file chunks。

输入：
- P8.12 internal package-entry chunked replacement source foundation。
- 当前 `PackageEditor::replace_worksheet_part_impl()` worksheet validation、payload audit、
  relationship audit 和 calcChain/fullCalcOnLoad 语义。
- 当前 `PackageWriter` chunked `PackageEntry` 输出能力。

输出：
- 新增 internal `PackageEditor::replace_worksheet_part_chunks()`。
- helper 在状态变更前拒绝 empty chunks。
- 成功路径保留 `replace_worksheet_part_impl()` 的 worksheet root validation、payload /
  relationship audit、workbook fullCalcOnLoad 和 calcChain policy，然后把 target worksheet
  replacement payload 切换为 chunks。
- `fastxlsx.package_editor` 回归覆盖 staged worksheet chunks 输出、workbook calc metadata
  rewrite、unknown entry preservation，以及 empty chunks failure-before-state-change。

触碰文件：
- `src/package_editor.hpp`
- `src/package_editor.cpp`
- `tests/test_package_editor.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/streaming_writer.cpp`
- `src/package_writer.cpp`
- CMake 配置

可并行性：
- 后续 cell replacement staged output handoff 已进入 P8.14；true file-backed worksheet
  transformer 必须串行推进。
- dependency policy 扩展、relationship repair 设计和 benchmark 调研可并行只读调研。

验收标准：
- `fastxlsx.package_editor` 覆盖 worksheet chunk handoff 成功和 empty chunks 失败路径。
- 默认完整 CTest 通过。
- 文档明确这只是 internal worksheet replacement chunk handoff，不是 public API、
  low-memory validation/audit、cell replacement low-memory stream writer、full worksheet stream writer、
  dependency repair、relationship repair 或 range metadata recalculation。

禁止项：
- 不新增 public `WorkbookEditor` / `WorksheetEditor` / `PackageEditor` API。
- 不把 materialized audit XML 写成已经完成低内存校验。
- 不把 `replace_worksheet_cells()` 改写成 low-memory staged stream writer。
- 不修复 sharedStrings/styles、relationships、tables、drawings、definedNames、formulas、
  calcChain rebuild 或其他 range-bearing metadata。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_editor --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P8.14 internal cell replacement staged chunk handoff

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public header / Patch API。

目标：把 P8.9/P8.10/P8.11 的 bounded cell replacement helper 接到 P8.13
worksheet chunk handoff：`replace_worksheet_cells()` 仍先物化 current planned worksheet
XML、运行 chunk emitter、刷新 top-level worksheet `<dimension>`，再把最终
dimension-refreshed worksheet output 作为 `PackageEntryChunk` 交给
`replace_worksheet_part_chunks()`，让目标 worksheet 在 `EditPlan` / planned output
中暴露为 `StreamRewrite`。

输入：
- P8.9 internal bounded `PackageEditor` cell-replacement handoff。
- P8.10 top-level worksheet dimension refresh。
- P8.11 replacement cell payload preflight。
- P8.13 internal worksheet replacement chunk handoff。

输出：
- `PackageEditor::replace_worksheet_cells()` 在 dimension refresh 后构造 staged memory
  chunk，并调用 `replace_worksheet_part_chunks()`。
- target worksheet part、manifest 和 planned output 均记录为 `StreamRewrite`。
- EditPlan reason / notes 明确这是 cell replacement staged package-entry chunk handoff，
  但该切片当时仍物化 planned 和 rewritten worksheet XML；P8.16 已把 rewritten
  output 改为 PackageEditor-owned temporary file-backed chunk。
- `fastxlsx.package_editor` 回归覆盖 by-name cell replacement 的 `StreamRewrite`
  状态、workbook calc metadata rewrite、calcChain cleanup、unknown bytes preservation
  和 dimension refresh note。

触碰文件：
- `src/package_editor.hpp`
- `src/package_editor.cpp`
- `tests/test_package_editor.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/streaming_writer.cpp`
- `src/package_writer.cpp`
- CMake 配置

可并行性：
- true file-backed worksheet transformer、low-memory validation/audit 和 dependency
  repair 必须后续串行推进。
- relationship repair 设计、benchmark 和 public API 设计可并行只读调研。

验收标准：
- `fastxlsx.package_editor` 覆盖 cell replacement handoff 的 `StreamRewrite` 状态和
  staged chunk note。
- 默认完整 CTest 通过。
- 文档明确这只是 internal staged output handoff，不是 public API、低内存
  validation/audit、full worksheet stream writer、dependency repair、relationship
  repair 或 range metadata recalculation。

禁止项：
- 不新增 public `WorkbookEditor` / `WorksheetEditor` / `PackageEditor` API。
- 不把 `replace_worksheet_cells()` 写成 true low-memory staged stream writer。
- 不修复 sharedStrings/styles、relationships、tables、drawings、definedNames、formulas、
  calcChain rebuild 或其他 range-bearing metadata。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_editor --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P8.15 internal cell replacement invalid payload no-state-pollution

状态：基础完成。

类型：internal integration regression + docs；不新增 public header / Patch API。

目标：把 P8.11 replacement cell payload preflight 在 `PackageEditor` handoff
层的失败状态卫生补齐，证明非法 replacement cell XML 会在 Patch 状态变更前失败，
不会污染 `EditPlan`、manifest、package-entry audit、payload audits、calc policy
或 planned output。

输入：
- P8.11 `emit_cell_replacement_worksheet()` replacement payload preflight。
- P8.14 `replace_worksheet_cells()` staged chunk handoff。
- 当前 no-state-pollution 检查模式和 `planned_output()` 聚合快照。

输出：
- `fastxlsx.package_editor` 新增 by-name cell replacement 回归，覆盖非 cell root、
  缺失 unqualified `r`、qualified-only `x:r` 和 `r` attribute 与 selector 不匹配。
- 每个失败路径验证不新增 notes、relationship audits、worksheet/workbook payload
  audits、package-entry audit、removed parts、calcChain policy 或 manifest write-mode
  变化。
- 失败后 `save_as()` 仍输出 source copy-original package，保留 worksheet、
  `xl/calcChain.xml` 和 unknown bytes。

触碰文件：
- `tests/test_package_editor.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/package_editor.cpp`
- `src/worksheet_transformer.cpp`
- CMake 配置

可并行性：
- true file-backed worksheet transformer、low-memory validation/audit 和 dependency
  repair 必须后续串行推进。
- P9/P10/P11 的只读调研可并行，但同一测试文件写入需串行合并。

验收标准：
- `fastxlsx.package_editor` 覆盖 invalid cell replacement payload 的状态卫生。
- 默认完整 CTest 通过。
- 文档明确这只是 internal preflight failure hygiene，不是 public API、XML schema
  validation、low-memory worksheet stream transformer、relationship repair、
  sharedStrings/style migration 或 range metadata recalculation。

禁止项：
- 不新增 public `WorkbookEditor` / `WorksheetEditor` / `PackageEditor` API。
- 不把 replacement cell payload preflight 写成完整 cell schema validation。
- 不修复 sharedStrings/styles、relationships、tables、drawings、definedNames、
  formulas、calcChain rebuild 或其他 range-bearing metadata。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_editor --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P8.16 internal cell replacement file-backed output stream handoff

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public header / Patch API。

目标：把 P8.14 的 cell replacement rewritten worksheet 输出从完整内存字符串改成
输出侧 file-backed stream handoff：仍物化 current planned worksheet XML 作为当前
PackageReader 边界输入，但不再物化完整 rewritten worksheet XML。

输入：
- P8.8 internal output chunk emitter。
- P8.10 top-level worksheet dimension refresh。
- P8.11 replacement cell payload preflight。
- P8.12 / P8.13 chunked package-entry 与 worksheet chunk handoff。
- P6 payload / relationship audit policy。

输出：
- `PackageEditor::replace_worksheet_cells()` 先扫描 planned worksheet action stream
  与 replacement payload，计算 top-level `<dimension>`，并分拆 audit 为 source
  metadata 与 replacement cell payload。
- 被 replacement selector 命中的旧 source cell payload 不参与 sharedStrings /
  styles / formula payload audit，也不参与 worksheet relationship-id audit。
- worksheet relationship-id audit 基于 rewritten action stream：pass-through chunk
  与 replacement cell payload 会被扫描，被替换旧 cell 中的 stale relationship
  reference 不污染 audit。
- second pass 直接把 dimension-refreshed worksheet XML 写入
  `PackageEditor` 持有的 temporary file-backed `PackageEntryChunk`，再通过
  prevalidated worksheet chunk path 提交 `StreamRewrite`。
- `PackageEditor` 管理临时文件 RAII 生命周期，析构和 move-assignment cleanup 会移除
  已接管的 temporary XML 文件。
- `fastxlsx.package_editor` 回归覆盖 file-backed handoff note、`StreamRewrite`
  planned output、`save_as()` 后 `PackageReader` 可重读、dimension refresh、
  old target cell audit skip、replacement payload audit / ReferencePolicy fail
  no-state-pollution，以及 PackageEditor 析构后临时文件不残留。
- `fastxlsx.package_editor` linked-object fixture 回归覆盖 by-name cell replacement
  file-backed handoff 下的 internal Patch preservation / audit 可见性：worksheet
  `.rels`、drawing/media/chart/table/VML/percent-decoded drawing、sharedStrings /
  owner `.rels`、styles、VBA、reachable unknown extension / owner `.rels`、
  workbook definedNames、PNG default content type、calcChain cleanup 和
  `PackageReader` 重读均保持可验证。

触碰文件：
- `src/package_editor.hpp`
- `src/package_editor.cpp`
- `include/fastxlsx/detail/worksheet_transformer.hpp`
- `src/worksheet_transformer.cpp`
- `tests/test_package_editor.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/package_reader.cpp`
- `src/package_writer.cpp`
- `src/streaming_writer.cpp`
- CMake 配置

可并行性：
- 文档同步可与只读实现复核并行。
- 修改 `PackageEditor` state transition、temporary file ownership 或 audit policy
  必须串行，避免同文件/同状态机冲突。

验收标准：
- `fastxlsx.package_editor` 覆盖 file-backed cell replacement handoff、dimension
  refresh、old target cell audit skip、replacement payload policy failure、linked-object
  preservation fixture 和临时文件清理。
- 默认完整 CTest 通过。
- 文档明确这是 output-side streaming/file-backed handoff；当前仍物化 planned
  worksheet XML，不是完整 PackageReader input streaming、完整 low-memory large-file
  editing、relationship repair/pruning、object semantic editing、sharedStrings/style
  migration 或 range metadata repair。

禁止项：
- 不新增 public `WorkbookEditor` / `WorksheetEditor` / `PackageEditor` API。
- 不把 current planned worksheet XML 物化边界写成已解决。
- 不修复 sharedStrings/styles、relationships、tables、drawings、definedNames、
  formulas、calcChain rebuild 或其他 range-bearing metadata。
- 不把 replacement payload preflight 写成完整 XML schema validation。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_editor --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P8.17 internal PackageReader file-backed source extraction for cell replacement

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public header / Patch API。

目标：把 C5 输入侧从直接 `PackageReader::read_entry()` 物化 source worksheet entry
推进到第一片 file-backed source handoff。该切片只改变 source package entry 的读取
边界，不改变当前 event reader 仍需要完整 worksheet XML validation input 的事实。

输入：
- P8.16 cell replacement output-side file-backed chunk handoff。
- 当前 internal `PackageReader` stored/no-compression reader 和 opt-in DEFLATE reader。
- 当前 `PackageEditor::replace_worksheet_cells()` by-name / direct cell replacement path。

输出：
- `PackageReader::extract_entry_to_file(entry_name, output_path)` internal helper：
  stored/no-compression entry 用 64 KiB buffer 复制到 caller-provided file，并做
  incremental CRC 校验；缺失 entry、读写失败或 CRC mismatch 失败。
- 在 `FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建中，DEFLATE entry extraction 暂复用
  `read_entry()` 后写文件，因此仍不是低内存 compressed input extraction。
- `PackageEditor::replace_worksheet_cells()` 当目标 worksheet 仍是 source package
  entry 时，先通过 `PackageReader` 抽取到 scoped temporary file-backed source，再读取
  该文件进入当前 event reader validation；planned replacement / staged chunk input
  仍走 current planned worksheet XML materialization。
- `EditPlan` / `planned_output()` note 区分 source-entry extraction 和 planned-input
  materialization，避免误报 source extraction。
- `fastxlsx.package_reader` 覆盖 stored entry extraction 和 corrupt payload CRC
  extraction failure。
- `fastxlsx.package_editor` 覆盖 source-entry cell replacement note、planned worksheet
  input 不误报 source extraction、`StreamRewrite` output、calcChain cleanup 和输出
  `PackageReader` 可重读。

触碰文件：
- `src/package_reader.hpp`
- `src/package_reader.cpp`
- `src/package_editor.cpp`
- `tests/test_package_reader.cpp`
- `tests/test_package_editor.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/EDITING_MODEL.md`
- `docs/PATCH_PRESERVATION_COVERAGE.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/worksheet_transformer.cpp`
- `src/package_writer.cpp`
- `src/streaming_writer.cpp`
- CMake 配置

可并行性：
- 后续 event reader chunk/window input 设计可并行只读调研。
- 修改 `PackageReader` input streaming、CRC policy、temporary source file ownership 或
  `replace_worksheet_cells()` state transition 必须串行。

验收标准：
- `fastxlsx.package_reader` 和 `fastxlsx.package_editor` 通过。
- 文档明确这是 source-entry file-backed extraction first slice；当前 event reader 仍会
  materialize validation input，不是完整 PackageReader input streaming、low-memory
  worksheet transformer、relationship repair、sharedStrings/style migration 或 public API。

禁止项：
- 不把 `extract_entry_to_file()` 写成 public API。
- 不把 DEFLATE fallback 写成低内存 compressed input 支持。
- 不把 planned replacement / staged chunk worksheet input 写成 source package extraction。
- 不修复 sharedStrings/styles、relationships、tables、drawings、definedNames、
  formulas、calcChain rebuild 或 range-bearing metadata。

验证命令：
```powershell
cmake --build --preset windows-nmake-release --target fastxlsx_package_reader_tests fastxlsx_package_editor_tests
ctest --preset windows-nmake-release -R "fastxlsx.package_reader|fastxlsx.package_editor" --output-on-failure --timeout 60
```

## P9 - Production ZIP/backend and package writer hardening

状态：推进中；P9.1 / P9.2 / P9.3 / P9.4 / P9.5 / P9.6 已落地。

目标：继续加固内部 `src/package_writer.*` boundary，保持新建 workbook
输出、chunked package entries、stored bootstrap 和 opt-in minizip backend
的行为可验证；不要把内部 writer boundary 写成 public package editing、
true package streaming 或 Zip64 支持。

子任务：
- P9.1 internal package writer duplicate entry-name preflight：基础完成。
- P9.2 internal package writer invalid entry-name preflight：基础完成。
- P9.3 internal package writer missing file-backed chunk preflight：基础完成。
- P9.4 internal package writer empty-package preflight：基础完成。
- P9.5 internal package writer mixed legacy-data/chunks preflight：基础完成。
- P9.6 internal package writer invalid chunk-source preflight：基础完成。

### P9.1 internal package writer duplicate entry-name preflight

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public API / CMake dependency。

目标：在 `write_package()` 打开输出路径前拒绝重复 ZIP entry name，避免 writer
生成 reader 会拒绝的 ambiguous package，并保持失败路径不覆盖已有输出文件。

输入：
- 当前 `src/package_writer.cpp` 已有 compression-level、entry-count、entry-name
  length 和 single-entry uncompressed-size 的 writer preflight。
- 当前 `tests/test_package_reader.cpp` 已覆盖 writer guardrail 的 sentinel output
  preservation 语义。
- 当前 `PackageReader` 已拒绝重复 ZIP entry name。

输出：
- `validate_package_entries_zip32()` 记录已见 entry name，并在重复时抛出
  `duplicate ZIP entry name` 错误。
- `fastxlsx.package_reader` 新增 writer 回归：重复 `xl/workbook.xml` entry
  在打开输出前失败，已有 sentinel output bytes 保持不变。
- 文档同步 P9 writer guardrail 当前事实。

触碰文件：
- `src/package_writer.cpp`
- `tests/test_package_reader.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/streaming_writer.cpp`
- `src/package_editor.cpp`
- CMake 配置

可并行性：
- 可与 P10 sharedStrings hardening、P11 benchmark groundwork 的只读调研并行。
- 与其他 package writer guardrail / backend 行为修改串行合并，避免同一
  validation 函数和同一测试文件冲突。

验收标准：
- `fastxlsx.package_reader` 通过。
- 默认完整 CTest 通过。
- 文档明确当前只是 internal package writer preflight，不是 Zip64、
  package streaming、public compression controls 或 public existing-file editing。

禁止项：
- 不新增 public `PackageWriter` / `PackageEditor` API。
- 不改变 ZIP backend 选择、compression-level 语义或 minizip data-descriptor 策略。
- 不声明 reader/writer 已支持 Zip64、duplicate-entry repair 或 package streaming。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_reader --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P9.2 internal package writer invalid entry-name preflight

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public API / CMake dependency。

目标：让 `write_package()` 在打开输出路径前拒绝 reader 已经拒绝的非法 ZIP
entry name，避免 writer 生成 package boundary 自己无法重新读取的 entry shape。

输入：
- 当前 `PackageReader` 已拒绝 absolute path、trailing slash、backslash、
  query/fragment component、empty segment、dot segment、parent segment 和 null byte
  entry names。
- P9.1 已让 writer 在打开输出路径前拒绝重复 ZIP entry name。
- 当前 writer guardrail 测试已使用 sentinel output 验证失败不覆盖已有输出。

输出：
- 新增 `src/zip_entry_name.hpp` 内部共享 `validate_zip_entry_name()`，reader 和 writer
  使用同一 ZIP entry-name 规则。
- `validate_package_entries_zip32()` 在 entry length、duplicate 和 Zip64 size
  guardrail 之外，也预检非法 entry name。
- `fastxlsx.package_reader` 新增 writer 回归，覆盖 empty、absolute、trailing slash、
  empty/dot/parent segment、backslash、query、fragment 和 null-byte entry name。

触碰文件：
- `src/zip_entry_name.hpp`
- `src/package_reader.cpp`
- `src/package_writer.cpp`
- `tests/test_package_reader.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/streaming_writer.cpp`
- `src/package_editor.cpp`
- CMake 配置

可并行性：
- 可与 P10 sharedStrings hardening、P11 benchmark groundwork 的只读调研并行。
- 与其他 ZIP entry-name / package writer validation 行为修改串行合并，避免
  shared helper 和同一测试文件冲突。

验收标准：
- `fastxlsx.package_reader` 通过。
- 默认完整 CTest 通过。
- 文档明确当前只是 internal package writer preflight，不是 ZIP repair、
  path normalization、Zip64、package streaming 或 public editing API。

禁止项：
- 不新增 public API。
- 不自动 normalize caller 提供的 entry name。
- 不声明 writer 支持修复非法 ZIP entry、Zip64 或 package streaming。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_reader --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P9.3 internal package writer missing file-backed chunk preflight

状态：基础完成。

类型：internal tests + docs；不新增 public API / CMake dependency。

目标：让 `write_package()` 的 file-backed chunk size preflight 在打开输出路径前
拒绝缺失或不可 stat 的 chunk path，避免 writer 进入部分写出状态，并保持已有
output bytes 不被覆盖。

输入：
- 当前 `PackageEntryChunk::file()` 用于 worksheet body、sharedStrings 和 image media
  等 close-time package assembly 路径。
- 当前 `entry_uncompressed_size()` 已在 `validate_package_entries_zip32()` 中对
  file-backed chunk 执行 `std::filesystem::file_size()` 预检。
- P9.1 / P9.2 已覆盖 duplicate / invalid entry name 的 sentinel output
  preservation 语义。

输出：
- `fastxlsx.package_reader` 新增 writer 回归：缺失 file-backed chunk path 会以
  `file-backed ZIP entry chunk` 错误在打开输出前失败。
- 失败后已有 sentinel output bytes 保持不变。
- 文档同步 P9 writer guardrail 当前事实。

触碰文件：
- `tests/test_package_reader.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/streaming_writer.cpp`
- `src/package_editor.cpp`
- CMake 配置

可并行性：
- 可与 P10 sharedStrings hardening、P11 benchmark groundwork 的只读调研并行。
- 与其他 package writer guardrail / backend 行为修改串行合并，避免同一
  validation 路径和同一测试文件冲突。

验收标准：
- `fastxlsx.package_reader` 通过。
- 默认完整 CTest 通过。
- 文档明确当前只是 internal package writer preflight，不是 missing file repair、
  Zip64、package streaming、public compression controls 或 public editing API。

禁止项：
- 不新增 public API。
- 不自动创建缺失 chunk、回退空 payload 或忽略 caller 提供的 file-backed source。
- 不声明 writer 支持 Zip64、true package streaming 或 atomic output。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_reader --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P9.4 internal package writer empty-package preflight

状态：基础完成。

类型：internal tests + docs；不新增 public API / CMake dependency。

目标：固定 `write_package()` 对空 entry 列表的 writer boundary 语义，证明该失败
路径会在打开输出前拒绝空 ZIP package，并保持已有 output bytes 不被覆盖。

输入：
- 当前 `validate_package_entries_zip32()` 已在 backend 选择和实际 writer open 前拒绝
  empty entry list。
- P9.1-P9.3 已覆盖 duplicate / invalid entry name 和 missing file-backed chunk 的
  sentinel output preservation 语义。

输出：
- `fastxlsx.package_reader` 新增 writer 回归：空 `std::vector<PackageEntry>` 会以
  `empty ZIP package` 错误失败。
- 失败后已有 sentinel output bytes 保持不变。
- 文档同步 P9 writer guardrail 当前事实。

触碰文件：
- `tests/test_package_reader.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/package_writer.cpp`
- `src/package_editor.cpp`
- CMake 配置

可并行性：
- 可与 P10 sharedStrings hardening、P11 benchmark groundwork 的只读调研并行。
- 与其他 package writer guardrail / backend 行为修改串行合并，避免同一测试文件冲突。

验收标准：
- `fastxlsx.package_reader` 通过。
- 默认完整 CTest 通过。
- 文档明确当前只是 internal package writer preflight，不是 empty-package repair、
  public package writer API、Zip64、package streaming 或 public existing-file editing。

禁止项：
- 不新增 public API。
- 不自动创建占位 entry、回退空 payload 或输出 technically-empty ZIP。
- 不声明 writer 支持 public package creation controls、Zip64、true package streaming
  或 atomic output。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_reader --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P9.5 internal package writer mixed legacy-data/chunks preflight

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public API / CMake dependency。

目标：让 `write_package()` 在打开输出路径前拒绝同一 `PackageEntry` 同时携带
legacy `data` payload 和 chunked payload 的内部非法状态，避免 writer 静默忽略
其中一种 payload source，并保持已有 output bytes 不被覆盖。

输入：
- 当前 `PackageEntry` 仍保留 legacy `data` 字段和 chunked `PackageEntryChunk`
  字段，以兼容 internal memory entry 和 file-backed/chunked entry 两条装配路径。
- 当前 writer 写入时若 `chunks` 非空会走 chunked payload，legacy `data` 不参与输出。
- P9.1-P9.4 已覆盖 duplicate / invalid entry name、missing file-backed chunk 和
  empty package 的 sentinel output preservation 语义。

输出：
- `validate_package_entries_zip32()` 在 entry-name / duplicate / ZIP32 size
  guardrail 旁，拒绝 `data` 非空且 `chunks` 非空的 `PackageEntry`。
- `fastxlsx.package_reader` 新增 writer 回归：mixed legacy-data/chunks entry 会以
  `chunked payload` 错误在打开输出前失败。
- 失败后已有 sentinel output bytes 保持不变。
- 文档同步 P9 writer guardrail 当前事实。

触碰文件：
- `src/package_writer.cpp`
- `tests/test_package_reader.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/streaming_writer.cpp`
- `src/package_editor.cpp`
- CMake 配置

可并行性：
- 可与 P10 sharedStrings hardening、P11 benchmark groundwork 和 P12 hot-path
  只读调研并行。
- 与其他 package writer guardrail / backend 行为修改串行合并，避免同一
  validation 函数和同一测试文件冲突。

验收标准：
- `fastxlsx.package_reader` 通过。
- 默认完整 CTest 通过。
- 文档明确当前只是 internal package writer preflight，不是 public package writer
  API、payload merge/repair、Zip64、package streaming 或 public existing-file editing。

禁止项：
- 不新增 public API。
- 不自动合并 legacy `data` 和 chunked payload。
- 不静默选择其中一种 payload source。
- 不声明 writer 支持 payload repair、Zip64、true package streaming 或 atomic output。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_reader --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P9.6 internal package writer invalid chunk-source preflight

状态：基础完成。

类型：internal implementation + tests + docs；不新增 public API / CMake dependency。

目标：让 `write_package()` 在打开输出路径前拒绝 `PackageEntryChunk` 自身的非法
source state，避免 memory chunk 静默忽略 file path、file chunk 静默忽略 memory
data，或未知 chunk kind 被当作空 payload。

输入：
- 当前 `PackageEntryChunk::memory()` 和 `PackageEntryChunk::file()` 是 internal
  helper，但 struct 字段仍可被测试或内部调用方直接修改。
- 当前 writer 根据 `PackageEntryChunk::kind` 选择 memory 或 file source，另一组字段
  不参与输出。
- P9.5 已拒绝同一 `PackageEntry` 混用 legacy `data` payload 和 chunked payload。

输出：
- `validate_package_entries_zip32()` 增加 chunk-source preflight，拒绝 memory chunk
  携带非空 file path、file chunk 携带非空 memory data，以及未知 chunk kind。
- `fastxlsx.package_reader` 新增 writer 回归：三类非法 chunk state 都会在打开输出
  前失败，且已有 sentinel output bytes 保持不变。
- 文档同步 P9 writer guardrail 当前事实。

触碰文件：
- `src/package_writer.cpp`
- `tests/test_package_reader.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/streaming_writer.cpp`
- `src/package_editor.cpp`
- CMake 配置

可并行性：
- 可与 P10 sharedStrings hardening、P11 benchmark groundwork 和 P12 hot-path
  只读调研并行。
- 与其他 package writer guardrail / backend 行为修改串行合并，避免同一
  validation 函数和同一测试文件冲突。

验收标准：
- `fastxlsx.package_reader` 通过。
- 默认完整 CTest 通过。
- 文档明确当前只是 internal package writer preflight，不是 public package writer
  API、chunk source repair、Zip64、package streaming 或 public existing-file editing。

禁止项：
- 不新增 public API。
- 不自动合并 memory/file chunk sources。
- 不静默选择其中一种 chunk source。
- 不声明 writer 支持 chunk repair、Zip64、true package streaming 或 atomic output。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.package_reader --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

## P10 - SharedStrings hardening and memory/size evidence

状态：推进中；P10.1 已落地。

目标：继续把 `StringStrategy::SharedString` 保持为显式 size/performance
tradeoff 选项，补充结构测试和本地 benchmark 证据；不要把 sharedStrings 写成
默认最佳策略、完整低内存路径或生产级大文件性能结论。

子任务：
- P10.1 schema-v4 sharedStrings benchmark matrix evidence：基础完成。

### P10.1 schema-v4 sharedStrings benchmark matrix evidence

状态：基础完成。

类型：manual benchmark + docs；不新增 public API / CMake dependency。

目标：用当前 schema-v4 benchmark runner 记录一组小规模 repeated / unique
字符串场景，补齐历史 schema-v3 快照之外的当前工具链证据。

输入：
- 当前 `fastxlsx_bench_streaming_writer` JSON schema version 为 `4`。
- 当前 `tools/run_benchmark_matrix.py` 可聚合一个已构建 benchmark exe 的 per-case
  schema-v4 JSON，并可选用 `openpyxl` 做本地只读 workbook 检查。
- 历史 2026-06-07 记录是 `500000` cells、schema-v3 小规模快照。

输出：
- 2026-06-10 本机 VS2026 / NMake benchmark preset 下构建
  `fastxlsx_bench_streaming_writer`。
- 运行 `strings` 场景的 repeated/unique × inline/shared 四组矩阵，
  `10000 x 10 x 1 = 100000` cells per case，stored-bootstrap / store backend。
- `openpyxl` 只读打开并验证每个输出 workbook 的 `Sheet1` 首尾值。
- `docs/PERFORMANCE_TARGETS.md` 记录 schema-v4 结果，并继续保留
  `office_open="not_run"` 与小规模趋势边界。

触碰文件：
- `docs/PERFORMANCE_TARGETS.md`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/*`
- `tests/*`
- CMake 配置

可并行性：
- 可与 P11 benchmark runner 设计调研和 P12 streaming hot-path 只读分析并行。
- 与 benchmark result 文档更新串行合并，避免同一性能表和状态摘要冲突。

验收标准：
- benchmark preset target 构建成功。
- `tools/run_benchmark_matrix.py` 四组 sharedStrings 相关 case 运行成功。
- `docs/PERFORMANCE_TARGETS.md` 明确数据规模、ZIP backend、string pattern、
  string strategy、time、peak memory、output size、worksheet-body footprint 和
  openpyxl 检查范围。
- 文档明确这不是 10,000,000-cell、大文件低内存、Google Benchmark、
  Zip64、true package streaming 或生产级 sharedStrings 结论。

禁止项：
- 不把 benchmark 接入默认 CTest / CI。
- 不提交 `build/` 下生成的 `.xlsx`、JSON 或 report。
- 不根据小规模数据改变默认 string strategy 或 public API wording。

验证命令：
```powershell
cmake --preset windows-nmake-release-benchmark
cmake --build --preset windows-nmake-release-benchmark --target fastxlsx_bench_streaming_writer
py tools\run_benchmark_matrix.py --bench-exe build\windows-nmake-release-benchmark\benchmarks\fastxlsx_bench_streaming_writer.exe --output-dir build\qa\benchmark-matrix-2026-06-10-sharedstrings --rows 10000 --cols 10 --sheets 1 --case strings:inline:repeated --case strings:shared:repeated --case strings:inline:unique --case strings:shared:unique --verify-openpyxl
```

## P11 - Benchmark groundwork

状态：推进中；P11.1 已落地。

目标：继续把 benchmark 能力保持为显式 opt-in 的本地工具链，补强 runner
自身的可验证性和文档边界；不要把 benchmark helper 写成默认 CTest / CI、
Google Benchmark 集成、Office 兼容性自动证明或大文件低内存结论。

子任务：
- P11.1 benchmark matrix runner self-test：基础完成。

### P11.1 benchmark matrix runner self-test

状态：基础完成。

类型：tooling + docs；不新增 public API / CMake dependency；不调用 benchmark exe。

目标：为 `tools/run_benchmark_matrix.py` 增加轻量 `--self-test` 入口，验证
benchmark matrix runner 的内部 case 解析、期望字符串分布、首尾单元格期望值和
聚合 report schema，方便后续 P11/P12 修改 runner 时先做快速守护。

输入：
- 当前 `tools/run_benchmark_matrix.py` 只包装一个已构建的
  `fastxlsx_bench_streaming_writer` exe，正常矩阵运行会写 `.xlsx`、schema-v4
  per-case JSON 和 `benchmark-matrix-report.json`。
- P10.1 已用该 runner 记录 schema-v4 sharedStrings 小矩阵。
- 当前默认 CTest 仍应保持轻量，benchmark 运行必须显式 opt-in。

输出：
- `tools/run_benchmark_matrix.py --self-test` 在检查 benchmark exe 存在性之前执行，
  不调用 benchmark exe、不创建输出目录、不写 `.xlsx` / JSON。
- 自检覆盖合法 / 非法 case 解析、repeated / unique / numeric 的字符串分布、
  numeric / repeated / unique / mixed 首尾期望值，以及 matrix report schema
  和 `cells_per_case` 计算。
- 文档同步该自检只是 runner 内部假设检查，不代表 benchmark 已运行、Office
  已打开 workbook、Google Benchmark 已接入或大文件性能已验证。

触碰文件：
- `tools/run_benchmark_matrix.py`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `docs/PERFORMANCE_TARGETS.md`
- `docs/TESTING_WORKFLOW.md`
- `AGENTS.md`

不触碰文件：
- `include/fastxlsx/*` public headers
- `src/*`
- `tests/*`
- CMake 配置

可并行性：
- 可与 P12 streaming hot-path 只读分析并行。
- 与其他 benchmark runner 行为修改串行合并，避免同一 Python helper 的 CLI
  和 report schema 冲突。

验收标准：
- `py tools\run_benchmark_matrix.py --self-test` 通过。
- 默认 build 和完整 CTest 通过。
- 文档明确 `--self-test` 不运行 benchmark exe、不写 build artifact、不证明
  Office/openpyxl 兼容性、不改变 `office_open="not_run"` 语义。

禁止项：
- 不把 benchmark 或 runner self-test 接入默认 CTest / CI。
- 不新增 Google Benchmark、openpyxl、XlsxWriter 或 Excel 运行时依赖。
- 不改变 benchmark JSON schema-v4、matrix report schema-v1 或默认 case 矩阵语义。
- 不根据 self-test 改变 sharedStrings / inlineStr 默认策略或 public API wording。

验证命令：
```powershell
py tools\run_benchmark_matrix.py --self-test
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

## P12 - Streaming writer hot-path work

状态：推进中；P12.1 / P12.2 / P12.3 / P12.4 已落地。

目标：继续硬化 row/cell XML 追加路径，保持 row-order streaming、bounded
worksheet body buffering 和默认 CTest 轻量；不要把本阶段写成完整性能优化、
date encoding 完成、benchmark 结论或生产级大文件承诺。

子任务：
- P12.1 unsigned decimal append helper：基础完成。
- P12.2 shared string index append path：基础完成。
- P12.3 shared string duplicate lookup without temporary key：基础完成。
- P12.4 shared string index stores string_view keys：基础完成。

### P12.1 unsigned decimal append helper

状态：基础完成。

类型：internal helper + tests + docs；不新增 public API / CMake dependency。

目标：为追加型 XML 路径提供 `detail::append_unsigned_decimal()`，用
`std::to_chars` 直接把 unsigned decimal 写入既有 buffer，减少 row/cell 热路径
中为了无符号十进制整数产生的临时 `std::string`。

输入事实：
- `detail::append_number()` 已覆盖 finite-only double append fast path。
- `detail::append_cell_reference()` 已让 row/cell 路径避免构造完整 cell
  reference 临时字符串，但内部 row suffix 仍通过 `std::to_string()` 追加。
- `WorksheetWriter::append_row()` 的 `<row r="...">` 和 in-memory /
  streaming style `s="N"` attribute 都属于已有 XML append buffer 路径。

范围：
- 新增内部 `detail::append_unsigned_decimal(std::string&, std::uint64_t)`。
- `detail::append_cell_reference()` 的 row suffix 改走该 helper。
- `WorksheetWriter::append_row()` 的 row 编号改走该 helper。
- `CellStore` 和 `WorksheetWriter` 的 style id XML attribute 改走该 helper。
- `test_xml_helpers()` 覆盖 prefix 保留、`0`、`uint32_t` 最大值和
  `uint64_t` 最大值。
- 文档记录该 helper 的边界和非性能结论。

触碰文件：
- `include/fastxlsx/detail/xml.hpp`
- `src/xml.cpp`
- `src/cell_store.cpp`
- `src/streaming_writer.cpp`
- `tests/test_minimal_xlsx.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

验收条件：
- `fastxlsx.unit` 覆盖 `append_unsigned_decimal()` 基础输出。
- `fastxlsx.streaming` 保持既有 row/cell XML 结构行为。
- 全量默认 CTest 通过。
- 文档明确它不是 benchmark、date encoding、完整 hot-path 优化或大文件性能证明。

非目标：
- 不替换 metadata/package 路径中的所有 `std::to_string()`。
- 不改变 sharedStrings 索引策略、ZIP backend、worksheet body file-backed
  chunking 或 benchmark schema。
- 不新增 public API，不增加外部依赖。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.unit --output-on-failure --timeout 60
ctest --preset windows-nmake-release -R fastxlsx.streaming --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P12.2 shared string index append path

状态：基础完成。

类型：internal hot-path helper usage + tests + docs；不新增 public API / CMake dependency。

目标：让 `StringStrategy::SharedString` 下字符串 cell 的 worksheet `<v>` shared
string index 直接写入 row XML buffer，避免每个 shared string cell 先通过
`std::to_string()` 构造临时字符串。

输入事实：
- `shared_string_index()` 处在 `WorksheetWriter::append_row()` 的 string cell 写入路径。
- 现有 sharedStrings 结构测试已锁定 worksheet `t="s"` indexes、重复值去重、
  跨 worksheet workbook-scope indexes、`count` / `uniqueCount` 和无字符串 cell 的空表边界。
- P12.1 的 unsigned append helper 已覆盖 row number、cell reference row suffix 和
  style id 局部路径。

范围：
- `detail::append_unsigned_decimal()` 保持单一 `uint64_t` 入口，覆盖 `std::size_t`
  shared string index 输出。
- `write_cell()` 中 sharedStrings `<v>` index 改走该 helper。
- `test_xml_helpers()` 增加 `uint64_t` 最大值输出覆盖。
- 文档记录这只是 shared string cell XML append 路径优化。

触碰文件：
- `include/fastxlsx/detail/xml.hpp`
- `src/xml.cpp`
- `src/streaming_writer.cpp`
- `tests/test_minimal_xlsx.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

验收条件：
- `fastxlsx.unit` 覆盖 `append_unsigned_decimal()` 的 `uint64_t` 输出。
- `fastxlsx.streaming` 继续覆盖 sharedStrings worksheet indexes、去重和 package wiring。
- 全量默认 CTest 通过。
- 文档明确不改变 sharedStrings 策略、索引语义、benchmark schema 或生产性能结论。

非目标：
- 不重建 shared string table，不迁移 existing-file sharedStrings indexes。
- 不替换 `sharedStrings.xml` close-time `count` / `uniqueCount` metadata 的
  `std::to_string()`。
- 不改变 `StringStrategy::InlineString` 默认策略、ZIP backend、benchmark runner 或
  Office compatibility evidence。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.unit --output-on-failure --timeout 60
ctest --preset windows-nmake-release -R fastxlsx.streaming --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P12.3 shared string duplicate lookup without temporary key

状态：基础完成。

类型：internal sharedStrings hot-path helper usage + existing structure tests + docs；
不新增 public API / CMake dependency。

目标：让 `shared_string_index()` 在查找重复 shared string 时使用
`std::string_view` 透明 lookup，避免每个 repeated shared string cell 都先创建
owning `std::string` key；只有首次遇到新字符串时才分配并存入 shared string table。

输入事实：
- `StringStrategy::SharedString` 维护 workbook-scope `SharedStringTable`。
- P12.2 已让 shared string cell `<v>` index 直接 append 到 row XML buffer。
- 当前 `shared_string_index()` 仍在查重前构造 `std::string key(value)`，对 repeated
  字符串密集输入不必要。
- 现有 `fastxlsx.streaming` sharedStrings 结构测试覆盖重复字符串、顺序、跨 worksheet
  去重、`count` / `uniqueCount` 和空表边界。

范围：
- 为 `SharedStringTable::index_by_value` 增加 transparent hash / equality。
- `shared_string_index()` 先用 caller `std::string_view` 查重，miss 时再构造 owning key。
- 文档记录该优化只减少 repeated shared string lookup 前的临时 key。

触碰文件：
- `src/streaming_writer.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

验收条件：
- `fastxlsx.streaming` 继续通过，证明 sharedStrings index / de-dup / package wiring 语义不变。
- 全量默认 CTest 通过。
- 文档明确这不是 sharedStrings 生产就绪、峰值内存证明、benchmark 结果或
  existing-file sharedStrings index migration。

非目标：
- 不改变 `SharedStringTable` 仍持有 unique string state 的事实。
- 不重建 shared string table，不迁移 existing-file sharedStrings indexes。
- 不改变 `StringStrategy::InlineString` 默认策略、benchmark schema 或 Office QA 记录。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.streaming --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P12.4 shared string index stores string_view keys

状态：基础完成。

类型：internal sharedStrings memory/hot-path storage + existing structure tests + docs；
不新增 public API / CMake dependency。

目标：让 shared string index map 不再持有第二份 owning string key。unique strings
由稳定 owned storage 持有，index map 只保存指向该 storage 的 `std::string_view`
和 index，减少 unique shared string 的重复 key storage。

输入事实：
- P12.3 已让 duplicate lookup 在分配前使用 caller `std::string_view` 查找。
- 旧结构同时在 `values` 和 `index_by_value` 的 owning `std::string` key 中保存
  unique string。
- shared string XML 输出仍需要按 index 顺序遍历 unique string values。

范围：
- `SharedStringTable::values` 改为 stable storage，保证 map key view 指向的字符串
  在追加 unique strings 后仍有效。
- `SharedStringTable::index_by_value` 改为 `std::string_view` key。
- `shared_string_index()` miss 时先 emplace owned value，再把 view/index 写入 map。
- 文档记录该优化只减少 shared string index map 的重复 owning key。

触碰文件：
- `src/streaming_writer.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

验收条件：
- `fastxlsx.streaming` 继续通过，证明 worksheet indexes、unique order、
  `count` / `uniqueCount` 和 package wiring 语义不变。
- 全量默认 CTest 通过。
- 文档明确这不是 sharedStrings 生产就绪、完整低内存证明、benchmark 结果或
  existing-file sharedStrings migration。

非目标：
- 不改变 shared string table 仍保留全部 unique strings 的事实。
- 不改变 `StringStrategy::InlineString` 默认策略。
- 不改变 benchmark schema、Office QA 记录、ZIP backend 或 sharedStrings XML 语义。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.streaming --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

## P13 - Phase 3 metadata/styles hardening

状态：推进中；P13.1、P13.2、P13.3、P13.4、P13.5 已落地。

目标：继续硬化已存在的 Phase 3 metadata 和 streaming-only styles 表面，补齐
public 注释已经承诺的结构回归；不要把本阶段写成完整 styles、公式计算、
dxfs、hyperlink styles、existing-file style preservation 或 full Phase 3。

已落地子任务：
- P13.1 default `StyleId{}` clears per-cell style：基础完成。
- P13.2 styles coexist with relationship-backed worksheet metadata：基础完成。
- P13.3 invalid style registration preserves registry state：基础完成。
- P13.4 all-default optional style metadata is ignored：基础完成。
- P13.5 styles with formula recalculation metadata：基础完成。

### P13.1 default `StyleId{}` clears per-cell style

状态：基础完成。

类型：streaming styles structure test + docs；不新增 public API / CMake dependency。

目标：锁定 `CellView::with_style(StyleId{})` 的 public contract：默认 style id `0`
会把 cell view 恢复到 workbook default style，输出时不写 `s="0"`，也不会把
cleared cell 当作 foreign style。

输入事实：
- `StyleId{}` 是 style `0`，header 注释说明它会清除 cell style。
- 已有 styles 测试覆盖非默认 `s="N"`、默认 `s="0"` 省略、foreign style 拒绝
  和 style registration guardrails。
- 仍缺少一个直接覆盖“先应用非默认 style，再用 `StyleId{}` 清除”的结构回归。

范围：
- 在 `fastxlsx.streaming` 中新增一个 workbook-level style 回归。
- 验证 registered style 仍生成 `xl/styles.xml`，styled cell 写 `s="1"`。
- 验证 cleared number/text cells 回到默认样式，不写 `s` / `s="0"`。
- 文档记录该测试只是锁住现有 narrow style contract。

触碰文件：
- `tests/test_streaming_writer.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

验收条件：
- `fastxlsx.streaming` 通过。
- 全量默认 CTest 通过。
- 文档明确这不是 full styles、style migration、existing-file style preservation、
  hyperlink styles、dxfs 或完整 Phase 3。

非目标：
- 不新增 style property、public editor API、`styles.xml` 合并或 existing-file
  style id migration。
- 不改变 `StyleId` owner-token foreign style 拒绝语义。
- 不改变 workbook-local style registry、number format / font / fill / alignment
  现有 XML 语义。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.streaming --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P13.2 styles coexist with relationship-backed worksheet metadata

状态：基础完成。

类型：streaming styles / worksheet metadata integration structure test + docs；
不新增 public API / CMake dependency。

目标：锁定 streaming-only styles 的 workbook-local 作用域：registered style 会生成
`xl/styles.xml` 和 workbook styles relationship，但不会占用 worksheet-local
relationship id；同一 worksheet 中 external hyperlink 和 table 仍按 owner-local
`rId1` / `rId2` 写入 worksheet XML 和 worksheet `.rels`。

输入事实：
- styles registry 是 workbook scope；`xl/styles.xml` 是 workbook-level 小型 XML part。
- external hyperlink 和 table 是 worksheet-local relationship-backed metadata。
- 已有测试分别覆盖 styles 不创建 worksheet `.rels`、table/hyperlink 共存时的
  worksheet-local relationship id，但缺少“样式 + relationship-backed metadata”
  同 worksheet 的结构回归。

范围：
- 在 `fastxlsx.streaming` 中新增一个 workbook：一个 styled number cell、一个
  external hyperlink 和一个 table 共存。
- 验证 styles relationship 留在 `xl/_rels/workbook.xml.rels`。
- 验证 worksheet `.rels` 只包含 hyperlink 和 table，且 table 仍使用下一枚
  worksheet-local `rId`。
- 验证 styled cell 仍写 `s="1"`，默认 style 仍不写 `s="0"`。

触碰文件：
- `tests/test_streaming_writer.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

验收条件：
- `fastxlsx.streaming` 通过。
- 全量默认 CTest 通过。
- 文档明确该测试只是 styles workbook scope 与 worksheet relationship scope 的
  结构隔离回归。

非目标：
- 不新增 hyperlink styles、table styles、`dxfs`、style migration、relationship repair
  或 existing-file editing。
- 不改变 workbook relationship id 顺序、worksheet relationship id 分配策略或
  现有 table / hyperlink XML 语义。
- 不把 streaming-only style registry 扩展成完整 Excel formatting parity。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.streaming --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P13.3 invalid style registration preserves registry state

状态：基础完成。

类型：streaming styles guardrail structure test + docs；不新增 public API /
CMake dependency。

目标：锁定 `WorkbookWriter::add_style()` 的失败前置校验语义：空样式、
无有效属性的 alignment / font metadata、未知 horizontal / vertical alignment enum
都必须在写入 workbook style registry 前失败；随后注册的第一个合法 style 仍应获得
`StyleId` 1、custom `numFmtId` 164，且 generated `styles.xml` 不包含失败尝试产生的
alignment、font、fill 或 extra `cellXfs` 记录。

输入事实：
- `add_style()` 已有运行时 guardrail，现有测试只确认这些非法输入会抛错。
- P13.1 / P13.2 已覆盖 `StyleId{}` 清除和 styles 与 worksheet relationships
  的 scope 隔离。
- 仍需要一个直接覆盖 style registration failure no-state-pollution 的结构回归，
  防止后续扩展 full-font / full-fill / border / alignment slices 时把失败对象半注册。

范围：
- 扩展 `fastxlsx.streaming` 中的 invalid style registration 回归。
- 在多次非法 `add_style()` 后注册一个合法 number-format style。
- 验证合法 style 仍是 `StyleId` 1、`styles.xml` 只包含一个 custom number format
  和一个 custom `xf`。
- 验证失败 alignment / font metadata 不产生 `<alignment>`、`applyFont="1"`、
  custom font、custom fill 或额外 `cellXfs`。
- 验证 worksheet 中合法 styled cell 写 `s="1"`，普通 cell 仍不写 `s="0"`。

触碰文件：
- `tests/test_streaming_writer.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

验收条件：
- `fastxlsx.streaming` 通过。
- 全量默认 CTest 通过。
- 文档明确这只是 style registration guardrail 的状态卫生回归。

非目标：
- 不新增 style property、public editor API、style migration、relationship repair
  或 existing-file editing。
- 不改变现有 number format / font / fill / alignment XML 语义。
- 不把 streaming-only style registry 扩展成完整 Excel formatting parity。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.streaming --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P13.4 all-default optional style metadata is ignored

状态：基础完成。

类型：streaming styles structure test + docs；不新增 public API / CMake
dependency。

目标：锁定 `CellStyle` public 注释中的 all-default optional metadata 语义：
当 style 已经有合法有效属性时，附带全默认 `CellAlignment{}` 或 `CellFont{}`
不应贡献新的 style property，不应生成额外 style id、`<alignment>`、custom font、
`applyAlignment` 或额外 `cellXfs`。

输入事实：
- `CellStyle::alignment` 和 `CellStyle::font` 注释都说明 empty optional 或
  all-default metadata 不贡献 style property。
- P13.3 已覆盖非法空 style / all-default-only alignment / all-default-only font
  会被拒绝，且失败不污染 registry。
- 仍需要覆盖“all-default optional metadata 与其他有效 style 属性组合”时应被忽略，
  防止后续扩展 full-font / full-alignment 时把默认占位对象当作真实格式差异。

范围：
- 在 `fastxlsx.streaming` 中新增一个 workbook-level style 回归。
- 注册 number-format style，然后注册 number-format + all-default alignment/font，
  验证复用同一个 `StyleId`。
- 注册 bold font style，然后注册 bold font + all-default alignment，验证复用同一个
  `StyleId`。
- 验证 `styles.xml` 只包含一个 custom number format、一个 custom bold font、
  两个 custom `xf`，且无 `<alignment>` / `applyAlignment`。
- 验证 worksheet cell 使用复用后的 `s="1"` / `s="2"`，默认 style 仍不写
  `s="0"`。

触碰文件：
- `tests/test_streaming_writer.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

验收条件：
- `fastxlsx.streaming` 通过。
- 全量默认 CTest 通过。
- 文档明确这只是 all-default optional style metadata 的 narrow contract 回归。

非目标：
- 不新增 style property、full font control、full alignment、border、rich text、
  `dxfs`、public editor API 或 existing-file style preservation。
- 不改变 all-default-only style 仍被 `add_style()` 拒绝的 guardrail。
- 不改变 number format / font / fill / alignment 现有 XML 语义。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.streaming --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

### P13.5 styles with formula recalculation metadata

状态：基础完成。

类型：streaming styles / formula metadata integration structure test + docs；不新增
public API / CMake dependency。

目标：锁定带样式公式 cell 的组合语义：`CellView::formula(...).with_style(style)`
既要在 worksheet cell XML 中保留 `s="N"`，也要触发 workbook
`<calcPr fullCalcOnLoad="1"/>` 重算请求，并且仍不生成 `xl/calcChain.xml`。

输入事实：
- P13.4 前已有 number-format style 测试覆盖 styled formula cell 输出
  `<c ... s="N"><f>...</f></c>`。
- Phase 3 metadata 测试已覆盖普通公式会写 workbook `calcPr` 并且不创建
  `calcChain.xml`。
- 仍需要一个 styles + formula 组合回归，防止后续 style registry 或 formula
  metadata 改动只保留其中一侧行为。

范围：
- 扩展 `fastxlsx.streaming` 既有 number-format style 结构测试。
- 验证 styled formula cell 继续写 `s="1"` 和 `<f>A2*2</f>`。
- 验证同一个 workbook 写出 `<calcPr calcId="124519" fullCalcOnLoad="1"/>`。
- 验证生成包仍不包含 `xl/calcChain.xml`，且 styles workbook relationship /
  content type 语义不变。

触碰文件：
- `tests/test_streaming_writer.cpp`
- `docs/TASK_BREAKDOWN.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `AGENTS.md`

验收条件：
- `fastxlsx.streaming` 通过。
- 全量默认 CTest 通过。
- 文档明确这只是 styled formula + style registry + calc metadata 的组合回归。

非目标：
- 不计算公式、不写 cached values、不创建或重建 `calcChain.xml`。
- 不新增 formula parser、dependency graph、full Phase 3 或 existing-file style
  preservation。
- 不改变现有 number format / styles relationship / content type 输出语义。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release -R fastxlsx.streaming --output-on-failure --timeout 60
ctest --preset windows-nmake-release --output-on-failure --timeout 60
```

## 并行拆分建议

可以并行：
- P4.0.1 facade matrix 和 P4.0.2 cell boundary 的只读调研。
- P5 preservation fixture 设计中的不同对象类型。
- P6 dependency policy 的不同 feature 分析。

必须串行：
- P4.0 已合并并已有 public `WorkbookEditor` 窄 subset；后续扩展必须先经过
  C3 决策门，不直接追加 random-edit / `WorksheetEditor`。
- `CellValue` value type 已在 P7.2a 作为后续独立切片落地。
- P4.1 MVP 用例冻结前，不扩大 P4.2 / P4.3 的实现范围。
- P5 preservation 未覆盖前，不宣称 broad existing-file editing。
- P7 guardrails 未设计前，不把 In-memory 作为默认 workbook 编辑路径。

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
