# FastXLSX 任务拆分设计

## 目标

这份文档是执行层任务拆分入口，用来避免把 `P4`、`P7` 这类大任务直接交给一次实现。

`TASK_PLAN.md` 负责长期路线图，`NEXT_STEPS.md` 负责下一轮推进顺序；本文件负责把
当前优先级拆成可讨论、可验收、可并行的小任务。后续执行时，应先在本文件选定一个
最小任务，再进入设计或实现。

## 当前总顺序

```text
P3 package read/copy/write foundation
→ P4.0 API surface unification design
→ P4 Patch MVP
→ P5 preservation fixtures
→ P6 sheet dependency policies
→ P7 In-memory small-file editor
→ P8 large worksheet controlled editing
```

当前重点不是继续扩大一个笼统的“任务 4”。`P4.0` 已形成 public API 统一文档基线，
`P4.1` 已冻结首个 Patch MVP 用例；后续推进必须从 `P4.2` 之后选择最小缺口，
且不得绕过该基线直接扩大 public Patch / In-memory API。

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

状态：基础完成，后续 API 任务继续引用。

目标：在继续扩大 Patch MVP 或 In-memory API 前，统一 public facade、命名、值类型和
internal/public 边界。

### P4.0.1 Public Facade Matrix

状态：已完成文档基线。

类型：文档设计。

输入：
- 当前 public API：`WorkbookWriter`、`WorksheetWriter`、`CellView`、`Workbook`、
  `Worksheet`、`Cell`。
- 当前 internal Patch 底座：`PackageReader`、`PackageEditor`、`EditPlan`、
  `PartIndex`、`RelationshipGraph`。

输出：
- 一张稳定 public facade 矩阵：
  `WorkbookWriter` 用于 Streaming；
  `Workbook` 用于 small new workbook；
  future `WorkbookEditor` / `WorksheetEditor` 用于 existing-file editing。
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
- 文档明确 future 名称不代表已实现。

禁止：
- 不新增 `WorkbookEditor` 代码。
- 不把 internal `PackageEditor` 写成 public API。

### P4.0.2 Cell Value Boundary

状态：已完成文档基线。

类型：文档设计，后续可进入 public header 设计。

输入：
- 当前 owning `Cell`。
- 当前 non-owning `CellView`。
- 未来 editor / In-memory 所需随机读写单元格值。

输出：
- `CellView` / `Cell` / future `CellValue` / future `CellStore` 边界说明。
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
- 文档能回答：什么时候用 `CellView`，什么时候用 `Cell`，什么时候需要 future
  `CellValue`。
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
- 用户故事按 future `WorkbookEditor` 语义描述：打开已有 `.xlsx`，按 sheet name
  定位已有 worksheet，只替换该 worksheet 的 `<sheetData>` payload，再 `save_as()`
  输出新 package。
- 当前实施入口仍是 internal `PackageEditor::replace_worksheet_sheet_data_by_name()`；
  不新增 public `WorkbookEditor`、public `PackageEditor` 或随机 cell API。
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

输出：
- 更清晰的 package read/copy/write failure behavior。
- 继续证明失败不污染 `EditPlan`、manifest、package-entry audit、calc policy 和输出 bytes。
- 当前已覆盖：
  - package reader central directory bounds hardening。
  - `PackageEditor::save_as()` copy-original source entry 读取失败时带 entry 上下文，
    且不污染状态、不覆盖既有输出。
  - writer backend failure 时带输出路径上下文，且不污染状态、不覆盖既有输出。

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

状态：基础完成，Excel 可视化记录仍是后续 QA 缺口。

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

验收：
- 拆包 XML 检查通过。
- Excel 可视化验证若本机 Excel 可用则记录。
- 文档仍标注为 internal Patch MVP，不是 public editor。

## P5 - Preservation Fixtures

状态：基础完成，后续只按对象缺口补小切片。

目标：在扩大 existing-file editing 前，建立含复杂对象的保真 fixture。

子任务：
- P5.1 images/drawings fixture：基础完成。现有 linked-object fixture 覆盖 worksheet
  `.rels`、drawing XML、drawing `.rels`、PNG media bytes、VML drawing、percent-encoded
  drawing target、ordinary replace/remove ordering 和 `planned_output()` audit
  可见性；仍不能写成 existing-workbook image/drawing 语义编辑。
- P5.2 charts fixture：基础完成。现有 fixture 覆盖 chart part、drawing-owned direct
  / URI-qualified chart relationships、ordinary replace/remove ordering 和 content type
  audit；仍不能写成 chart reference migration、series/cache update 或 chart editing。
- P5.3 VBA / macro-enabled fixture：基础完成。现有 fixture 覆盖 `xl/vbaProject.bin`
  bytes、workbook inbound relationship、content type override、ordinary replace/remove
  ordering 和 no invented VBA owner `.rels`；仍不能写成 macro generation、VBA editing
  或 signature preservation。
- P5.4 sharedStrings/styles fixture：基础完成。现有 fixture 覆盖 sharedStrings、
  sharedStrings owner `.rels`、styles、workbook relationships、ordinary replace/remove
  ordering 和 no invented styles owner `.rels`；仍不能写成 sharedStrings index
  migration、style id migration、style merge 或 cell reference sync。
- P5.5 unknown extension fixture：基础完成。现有 fixture 覆盖 reachable unknown
  extension part、source-owned owner `.rels`、metadata ingestion、ordinary replacement、
  repeated replacement、remove/replace ordering、output-plan omitted/active audit 和
  relationship target audit；仍不能写成 custom extension semantic editing、
  relationship repair 或 broad unknown-part preservation guarantee。

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

状态：进行中。

目标：给 sheet-local edits 建立保守依赖策略。

子任务：
- P6.1 sharedStrings / styles dependency policy：基础完成。
- P6.2 tables / hyperlinks / validations / conditionalFormatting policy：基础完成。
- P6.3 drawings / images / charts linked-part policy：当前最小可执行任务。
- P6.4 definedNames / formulas / calc metadata policy。
- P6.5 unsupported edits preserve / request recalc / fail matrix。

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
- 不新增 public `CellValue`、`WorkbookEditor`、`WorksheetEditor` 或 public
  `PackageEditor`。
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

状态：当前最小可执行任务。

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

## P7 - In-memory Small-File Editor

状态：计划，必须在 P4.0 后继续设计。

目标：提供小文件随机编辑体验，但不成为大文件默认路径。

子任务：
- P7.1 `WorkbookEditor` / `WorksheetEditor` public facade draft。
- P7.2 `CellValue` public value draft。
- P7.3 internal `CellStore` / `CellRecord` memory model。
- P7.4 guardrails：`max_cells`、`memory_budget_bytes`、`cell_count()`、
  `estimated_memory_usage()`。
- P7.5 save-as and Patch handoff contract。

验收：
- API 注释明确 In-memory mode、随机访问语义和内存增长。
- 超限时给出明确错误并建议 Streaming 或 Patch。
- 不承诺百万行 worksheet 低内存随机读写。

## 并行拆分建议

可以并行：
- P4.0.1 facade matrix 和 P4.0.2 cell boundary 的只读调研。
- P5 preservation fixture 设计中的不同对象类型。
- P6 dependency policy 的不同 feature 分析。

必须串行：
- P4.0 合并前，不实现 public `WorkbookEditor` / `CellValue`。
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
