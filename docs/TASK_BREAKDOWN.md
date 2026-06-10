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

状态：进行中；P4.0 文档基线已完成，本阶段先做 public facade / value /
storage / guardrail / handoff 设计，再决定是否进入代码实现。

目标：提供小文件随机编辑体验，但不成为大文件默认路径。

子任务：
- P7.1 `WorkbookEditor` / `WorksheetEditor` public facade draft：基础完成。
- P7.2 `CellValue` public value draft：基础完成。
- P7.3 internal `CellStore` / `CellRecord` memory model：基础完成。
- P7.4 guardrails：`max_cells`、`memory_budget_bytes`、`cell_count()`、
  `estimated_memory_usage()`：基础完成。
- P7.5 save-as and Patch handoff contract：当前最小可执行任务。

验收：
- API 注释明确 In-memory mode、随机访问语义和内存增长。
- 超限时给出明确错误并建议 Streaming 或 Patch。
- 不承诺百万行 worksheet 低内存随机读写。

### P7.1 `WorkbookEditor` / `WorksheetEditor` public facade draft

状态：基础完成。

类型：public API 文档设计；不新增 header / implementation。

目标：在进入 `CellValue`、cell store 和 guardrails 之前，先冻结 future editor facade
的命名、职责、入口和非目标，确保 In-memory 小文件随机编辑不会污染 Streaming 热路径，
也不会把 internal `PackageEditor` 直接暴露为 public API。

输入：
- P4.0 facade matrix 和 cell value boundary 文档基线。
- 当前 `docs/API_DESIGN_AND_DOCUMENTATION.md`、`docs/ARCHITECTURE.md` 和
  `docs/EDITING_MODEL.md` 中的 future `WorkbookEditor` / `WorksheetEditor` /
  `CellValue` 表述。
- 当前 public API：`WorkbookWriter` / `WorksheetWriter` / `CellView`、
  `Workbook` / `Worksheet` / `Cell`。
- 当前 internal Patch 底座：`PackageReader`、`PackageEditor`、`EditPlan` 和
  `planned_output()`。

输出：
- future `WorkbookEditor` facade draft：`open(...)`、`worksheet(...)`、
  `try_worksheet(...)`、sheet listing、`save_as(...)` 和 no in-place overwrite 边界。
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
- 文档明确 P7.1 只是 future facade draft，不是已实现 API。
- 文档明确该 facade 不承诺百万行 worksheet 随机访问、原地覆盖、公式求值、
  relationship repair 或 public package editing。

禁止项：
- 不新增 public `WorkbookEditor` / `WorksheetEditor` / `CellValue` 代码。
- 不把 internal `PackageEditor` 或 OPC part concepts 暴露给普通 public API。
- 不在 P7.4 guardrails 前宣称 In-memory editor ready。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P7.2 `CellValue` public value draft

状态：基础完成。

类型：public API 文档设计；不新增 header / implementation。

目标：冻结 future `CellValue` 的语义值边界、所有权、value kind、style reference
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
- future `CellValueKind` 草案：blank、number、text、boolean、formula。
- optional style reference 草案：future `CellValue` 可携带 workbook-local
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
- 文档明确 `CellValue` 是 future editor / in-memory API boundary，不是已实现符号。
- 文档明确 style id 是 workbook-local handle，P7.2 不做 style registry merge。
- 文档明确 blank 与 missing cell 不等价，公式不求值，数字必须 finite。

禁止项：
- 不新增 public `CellValue`、`CellValueKind` 或 editor 代码。
- 不把 `CellValue` 写成内部 `CellStore` / `CellRecord` 的长期存储布局。
- 不新增 date / rich text / error cell 承诺。
- 不在 P7.4 guardrails 前宣称 In-memory editor ready。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P7.3 internal `CellStore` / `CellRecord` memory model

状态：基础完成。

类型：internal architecture / API 文档设计；不新增 header / implementation。

目标：定义 future In-memory editor 的内部 cell 存储草案，确保随机编辑使用
紧凑 `CellStore` / `CellRecord`，而不是长期保存 public `Cell` 或 `CellValue` 对象；
同时为 P7.4 size / memory guardrails 和 P7.5 save-as / Patch handoff 留出明确输入。

输入：
- P7.1 future `WorkbookEditor` / `WorksheetEditor` facade draft。
- P7.2 future `CellValue` public value draft。
- 当前 `docs/API_DESIGN_AND_DOCUMENTATION.md`、`docs/ARCHITECTURE.md` 和
  `docs/EDITING_MODEL.md` 的 In-memory boundary。
- 当前 public `Cell` / `CellView` / `StyleId` / styles / sharedStrings 文档边界。

输出：
- `CellStore` 草案：worksheet-local sparse storage，按 row / column key 索引已存在或
  显式编辑过的 cells；不分配完整 worksheet matrix。
- `CellRecord` 草案：保存 value kind、optional/default style id 和紧凑 payload。
  number / boolean 内联保存，text / formula 保存 pool id，blank / clear 语义用显式
  record 或 tombstone 候选表达，最终 erase/save-as 规则留给 P7.5。
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
- 文档明确 `CellStore` / `CellRecord` 是 internal future design，不是 public API。
- 文档明确 public `CellValue` 是边界值，内部长期存储使用 compact record / pool。
- 文档明确 sparse storage、pooling、style handle、missing / blank / erase 边界。
- 文档给 P7.4 提供可计量的 memory / cell-count 维度。

禁止项：
- 不新增 public 或 internal C++ 符号。
- 不把 future `CellStore` 写成已实现能力。
- 不承诺百万行 worksheet 低内存随机访问。
- 不在 P7.5 前宣称 sharedStrings、styles、calcChain 或 relationship handoff 已完成。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P7.4 guardrails：`max_cells` / `memory_budget_bytes`

状态：基础完成。

类型：public API / internal architecture 文档设计；不新增 header / implementation。

目标：冻结 future In-memory editor 的 size / memory guardrail 草案，明确
`max_cells`、`memory_budget_bytes`、`cell_count()`、`estimated_memory_usage()` 的
候选语义、计量口径、拒绝时机和错误提示，确保小文件随机编辑不会被误写成大文件
低内存路径。

输入：
- P7.1 future editor facade draft。
- P7.2 future `CellValue` public value draft。
- P7.3 internal `CellStore` / `CellRecord` memory model。
- 当前 In-memory API 文档要求：超限时提示 caller 改用 Streaming 或 Patch。

输出：
- future `WorkbookEditorOptions` / equivalent guardrail options 草案：
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
- 文档明确 guardrails 是 future design，不是已实现 public API。
- 文档明确 limit options、diagnostic APIs、计量维度和 enforcement 时机。
- 文档明确超限错误需要建议 Streaming 或 Patch。
- 文档明确没有 guardrails 和 P7.5 handoff 前不能宣称 In-memory ready。

禁止项：
- 不新增 `WorkbookEditorOptions`、`cell_count()`、`estimated_memory_usage()` 等代码。
- 不定义默认 limit 数值为稳定承诺。
- 不把 `estimated_memory_usage()` 写成精确内存 profiler。
- 不承诺百万行 worksheet 低内存随机访问。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

### P7.5 save-as and Patch handoff contract

状态：当前最小可执行任务。

类型：public API / internal architecture 文档设计；不新增 header / implementation。

目标：冻结 future In-memory editor 的 `save_as(...)` 与 internal Patch / package
rewrite 底座的交接契约，明确 source-backed existing workbook、小文件 materialized
cell edits、unknown/unmodified part preservation、sharedStrings / styles / calc metadata、
blank / erase / tombstone 和输出路径 guard 的边界。

输入：
- P7.1 future editor facade draft。
- P7.2 future `CellValue` public value draft。
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
- 不新增 `WorkbookEditor::save_as()` 或 public editor code。
- 不把 internal `PackageEditor` 直接暴露为 public API。
- 不宣称 random cell editing、sharedStrings migration、style id migration、relationship
  repair、calcChain rebuild 或 broad existing-file preservation 已完成。

验证命令：
```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

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
