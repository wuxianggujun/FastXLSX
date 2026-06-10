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
  `Worksheet`、`Cell`、`CellValue`。
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
workbook-level guardrails 和完整 save-as / Patch handoff 仍未 ready。

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
- 文档明确 guardrails 的 public editor API 仍是 future design；当前只实现 internal
  `CellStore` first slice。
- 文档明确 limit options、diagnostic APIs、计量维度和 enforcement 时机。
- 文档明确超限错误需要建议 Streaming 或 Patch。
- 文档明确没有 guardrails 和 P7.5 handoff 前不能宣称 In-memory ready。

禁止项：
- 不新增 public `WorkbookEditorOptions` 或 public editor diagnostic APIs。
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
- 不新增 `WorkbookEditor::save_as()` 或 public editor code。
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
- 这仍是 internal handoff regression，不是 `WorkbookEditor::save_as()`。
- 不新增 public `WorkbookEditor` / `WorksheetEditor` / `PackageEditor` API。
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
P8.7 已落地首个 internal transformer action model 切片；后续真正 stream rewrite
implementation 必须继续按任务模板拆分，不能把当前 reader、action scanner 或 bounded
local fixture 写成完整低内存大文件路径。

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

## 并行拆分建议

可以并行：
- P4.0.1 facade matrix 和 P4.0.2 cell boundary 的只读调研。
- P5 preservation fixture 设计中的不同对象类型。
- P6 dependency policy 的不同 feature 分析。

必须串行：
- P4.0 合并前，不实现 public `WorkbookEditor`；`CellValue` value type 已在 P7.2a
  作为后续独立切片落地。
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
