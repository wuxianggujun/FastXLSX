# 编辑模型

## 目标

FastXLSX 需要同时满足三个场景：

1. 高性能创建新 XLSX。
2. 编辑已有 XLSX，同时尽量保留原文件结构。
3. 小文件复杂随机编辑，提供接近普通 workbook 对象模型的体验。

因此编辑模型不能是纯内存 workbook，也不能是完全无状态流。正确模型是共享
OpenXML / OPC 底座上的三条路径：Streaming、Patch、In-memory。

执行层任务必须按 [任务拆分设计](TASK_BREAKDOWN.md) 选择最小子任务。`P4.0 API surface
unification` 文档基线已完成，其设计的 existing-file editing facade 现在落地了首个 public
Patch 切片：`include/fastxlsx/workbook_editor.hpp` / `src/workbook_editor.cpp` 暴露
`WorkbookEditor::open()` / `worksheet_names()` / `has_worksheet()` /
`replace_sheet_data(sheet_name, rows)` / `rename_sheet(old_name, new_name)` /
`save_as()`，由 `tests/test_workbook_editor.cpp`
（CTest `fastxlsx.workbook_editor`）覆盖。它在内部复用
`PackageEditor::replace_worksheet_sheet_data_by_name()`，只替换整张 `<sheetData>`，
做 bounded local rewrite；`rename_sheet()` 复用
`PackageEditor::rename_sheet_catalog_entry()`，只改写 `/xl/workbook.xml` 里
`<sheets><sheet name="...">` 这个 catalog 名，保留 worksheet parts / relationships /
content types / unknown entries，不动 defined names / formulas / tables / drawings /
rel targets（窄 catalog-name 改写，不是语义 rename）。这仍**不**表示完整
existing-file editing、public `PackageEditor`、随机 cell 读写（`get_cell` /
`set_cell`）或 `WorksheetEditor` 已实现。
`CellValue` 作为 public value type 已实现，internal `CellStore` 首个稀疏存储、guardrail
和 standalone `<sheetData>` emission 切片也已实现，但 random editing 仍未 ready。

## 编辑策略

### Part-level rewrite

XLSX 是 ZIP + OpenXML parts。编辑已有文件时，基本单位应该是 part。

```text
读取 package
→ 建立 part 索引
→ 标记修改过的 part
→ 未修改 part 原样复制
→ 修改 part 重新生成
→ 输出新 package
```

这个策略能最大程度保留 Excel 文件里的未知结构。
当前内部 `PackageReader` 覆盖 stored/no-compression ZIP entry reader 基础；
在 `FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建下还可通过 minizip-ng 读取 DEFLATE
entries，默认构建仍拒绝 compressed input。该 reader 依赖 header 中已有
size/CRC，读取时校验解压后 payload CRC，并拒绝 local header
CRC/method/name/size mismatch、encrypted flags、data descriptor entries、Zip64，
以及非法 ZIP entry name（绝对路径、尾部斜杠、反斜杠、query/fragment
components、空段、dot 段或 parent 段）；它还会在
OPC metadata ingestion 阶段拒绝冲突 content type default / override 和同一
`.rels` owner 内重复的 relationship id；`[Content_Types].xml` / `.rels`
metadata attributes 必须未命名空间（namespace declarations 除外），namespaced
metadata attribute decoy 会在 `PackageReader::open()` 阶段失败，未命名空间
metadata attributes 不得重复，非 whitespace metadata text 和 start/end tag
QName mismatch 会失败；同时要求
`[Content_Types].xml` / `.rels` 的第一个真实 XML 元素分别是
`Types` / `Relationships`，嵌套 decoy root 会失败；
当前只 ingest root 的 direct-child `Default` / `Override` / `Relationship` 元素，
metadata declaration 嵌套在非 root direct-child 元素下也会失败；
这不是 content-type 或 relationship repair。
它还可内部解析 workbook sheet catalog：只读取 workbook `<sheets>` 目录的直接
`<sheet>` 子元素前，会先验证 package `_rels/.rels` 中存在且仅存在一个 internal
`officeDocument` relationship；当前窄实现只接受解析到 `/xl/workbook.xml` 的 target，
相对、绝对和 dot-segment package target（例如 `xl/./workbook.xml`）都从
package root 解析到固定 workbook part，且不会把 package root 建模成
真实 `PartName`；缺失、重复、external、带 query/fragment 或非固定 target 会在 lookup 阶段失败。
随后它忽略该目录外或嵌套在非 sheet 目录子元素下的 `<sheet>` 标签，沿 workbook-owned
worksheet relationships 定位 worksheet part、解码 XML attribute 和 percent-encoded
relationship target，并要求 `name` / `sheetId` 是未命名空间的 workbook sheet
属性，且 sheet relationship id 位于 officeDocument relationships
XML namespace（可用非 `r` 前缀，普通 `id` 或错误 namespace 的 `id` 会被拒绝），
并能按 sheet name 找到已有 worksheet part；当前回归还覆盖 workbook-owned
绝对 worksheet target（例如 `/xl/worksheets/sheetN.xml`）和 dot-segment
相对 target（例如 `./worksheets/../worksheets/sheetN.xml`）可解析到已注册
worksheet part，并被 source-catalog 的完整 worksheet by-name replacement 与
by-name `sheetData` helper 复用；两条路径输出都会保留 worksheet relationship
的 target 字面值，但 calcChain cleanup 仍可能
重写 workbook `.rels`。这仍只是在固定 `/xl/workbook.xml` 入口下的目标定位，
不是任意 workbook part-location 支持。
缺失 namespace-valid `r:id`、workbook `.rels` 中缺失对应 relationship id、
worksheet relationship 指向未注册 worksheet part、external sheet target 或非 worksheet target
会在 by-name Patch 状态变更前的 lookup 阶段失败，重复 sheet name
lookup 会按 ambiguous 失败。它只是 Patch
目标定位基础，不是 sheet add/delete/rename、workbook DOM 或 public editing API。
它不是完整 ZIP reader 或 public editing API。

### EditPlan / dependency analysis

编辑已有 XLSX 前，应先把本次操作转换成 EditPlan。EditPlan 不是完整 workbook DOM，
而是一个 part 级别的影响范围说明。

```text
用户操作
→ 识别目标 sheet / range / workbook metadata
→ 分析受影响 part
→ 决定 copy-original / stream-rewrite / local-DOM-rewrite
→ 写出新 package
```

单个 sheet 修改可能联动：

- `xl/worksheets/sheetN.xml`
- `xl/worksheets/_rels/sheetN.xml.rels`
- `xl/sharedStrings.xml`
- `xl/styles.xml`
- `xl/tables/tableN.xml`
- `xl/workbook.xml`
- `xl/calcChain.xml`
- drawings、charts、pivot cache、VBA 和未知扩展 part

默认策略应保守：未知或未修改 part 原样复制；修改数据后可设置 fullCalcOnLoad，并对
`calcChain.xml` 采用删除、重建或显式保留策略，不能静默留下错误联动。
当前内部 `PackageEditor` 只有窄 preservation regression 覆盖部分未修改
worksheet `.rels`、drawing/media/chart/table、`xl/sharedStrings.xml` /
`xl/styles.xml`、VBA、可达 unknown extension part 及其 owner `.rels` 和 workbook
`definedNames` 保留；
其中包括替换后的 worksheet XML 省略源 `<drawing>` / `<tableParts>` 引用时仍
保留源关系和 linked parts。完整 relationship pruning、orphan cleanup、
sharedStrings/styles/defined-name 联动管理仍是 Patch 目标。

### P6.1 sharedStrings / styles dependency policy

当前 sheet-local Patch 对 sharedStrings / styles 采用保守策略：

- `xl/sharedStrings.xml` 和 source-owned `xl/_rels/sharedStrings.xml.rels` 默认
  copy-original；worksheet payload 中出现 shared string indexes 时，只记录
  audit-only caller review，不迁移索引、不重建 string table、不改写 worksheet
  `t="s"` cell。
- `xl/styles.xml` 默认 copy-original；worksheet payload 中出现 style id references
  时，只记录 audit-only caller review，不迁移 style id、不合并 styles、不改写
  cell `s` references，也不凭空创建 `xl/_rels/styles.xml.rels`。
- `ReferencePolicyAction::Fail` 可把这些 payload-only dependency 转为失败边界；
  失败必须发生在状态变更前，并保持 `EditPlan`、manifest、package-entry audit、
  relationship audits、calc policy、planned output 和输出 bytes 不污染。
- Ordinary `replace_part()` / explicit `remove_part()` 对 sharedStrings 或 styles
  仍只是 part-level rewrite / omission audit：保留 inbound workbook relationship，
  同步当前 content type audit，不做 worksheet cell reference sync、relationship
  pruning、metadata repair 或 public editing API。

### P6.2 tables / hyperlinks / validations / conditionalFormatting policy

当前 sheet-local Patch 对 tables、hyperlinks、data validations 和 conditional
formatting 也采用 preservation-first 策略：

- Tables：`xl/tables/tableN.xml`、worksheet `.rels` 中的 table relationship 和 table
  content type override 默认 copy-original。worksheet payload 中的 `<tableParts>` 只
  触发 caller review / relationship audit；即使 replacement worksheet 省略
  `<tableParts>`，当前也不自动删除 table part 或 prune worksheet relationship。
  Ordinary table replacement / removal 只是 part-level rewrite / omission audit，
  不 resize table、不迁移 range、不更新 table columns / totals / formulas。
- Hyperlinks：worksheet `<hyperlinks>` 与 worksheet `.rels` 默认保留。Patch audit
  只检查已知 hyperlink relationship id 的 missing / stale / type-mismatch 情况；
  不修复 target、不重写 display / tooltip、不创建 hyperlink style、不校验 external
  URL 或 internal workbook location。
- Data validations：worksheet-local `<dataValidations>` 在 `sheetData` patch 中随外围
  worksheet XML 保留；完整 worksheet replacement 只把新 payload 中的 validation
  metadata 记录为 audit-only caller review。不重算 ranges、不解析公式、不校验
  单元格值，也不与 table、merged ranges 或 autoFilter 做语义同步。
- Conditional formatting：worksheet-local `<conditionalFormatting>` 同样保留或审计。
  不重排 priority、不计算公式、不生成 `dxfs` / styles、不调整 ranges，也不把当前
  streaming-only color scale / data bar / icon set 能力提升为 existing-file editing。
- `ReferencePolicyAction::Fail` 可把这些 payload-only 或 relationship-bearing
  dependency 转为失败边界；失败必须发生在状态变更前，并保持 `EditPlan`、manifest、
  package-entry audit、relationship audits、calc policy、planned output 和输出 bytes 不污染。

### P6.3 drawings / images / charts linked-part policy

当前 sheet-local Patch 对 drawing / media / chart linked parts 继续采用
preservation-first 策略：

- Drawings：worksheet `<drawing>`、`<legacyDrawing>`、`<picture>` 和
  `<legacyDrawingHF>` 引用会触发 relationship / linked-part audit。source drawing
  part、drawing owner `.rels`、worksheet inbound relationship 和 drawing content type
  override 默认 copy-original；replacement worksheet 省略 drawing 引用时，当前也不自动
  删除 drawing part 或 prune worksheet relationship。Ordinary drawing / VML drawing /
  percent-decoded drawing replacement 或 removal 只表达 part-level rewrite / omission
  audit，不 mutate drawing XML、不合成 owner `.rels`、不修复 percent-encoded target。
- Images / media：drawing-owned image relationships 和 media bytes 默认保留。
  default-typed PNG/JPEG media 继续依赖 content type default，不为了 removal / restore
  无故提升为 part override。当前 Patch 不解码、不重采样、不压缩、不裁剪、不旋转、
  不转换图片格式，也不编辑 existing drawing anchors 或 image metadata。
- Charts：drawing-owned chart relationships 和 chart part bytes 默认保留；对
  URI-qualified chart targets 只审计 normalized base target。当前 Patch 不更新 chart
  series/cache、不修复 chart references、不改 drawing frame，也不创建 chart owner `.rels`。
- External、URI-qualified、percent-encoded、invalid 或 unresolved linked targets 只进入
  `RelationshipTargetAudit` / removed-part inbound audit；命中已注册 package part 的
  normalized target 可作为保守 dependency 纳入 copy-original 决策，但不会虚构缺失 part、
  repair target 或 prune source relationship。
- `ReferencePolicyAction::Fail` 可把 caller 不接受的 drawing/image/chart linked
  dependencies 转为失败边界；失败必须发生在状态变更前，并保持 `EditPlan`、manifest、
  package-entry audit、relationship audits、calc policy、planned output 和输出 bytes 不污染。

### P6.4 definedNames / formulas / calc metadata policy

当前 sheet-local Patch 对 workbook `definedNames`、worksheet formulas 和 calc metadata
继续采用 conservative rewrite + audit-first 策略：

- Defined names：workbook `definedNames` 默认随 `xl/workbook.xml` 保留。sheet catalog
  rename 只改直接 `<sheets><sheet name="...">` attribute，并把 direct workbook
  `definedNames` 作为 structured audit / caller-review 风险；当前不更新 named ranges、
  print areas、sheet-scoped names、formula references、table references 或 external
  names，也不做 name collision repair。
- Formulas：worksheet formula cells 会触发 payload dependency audit 和 caller review。
  当前 Patch 不解析公式 AST、不求值公式、不写 cached result、不改写 cell / range /
  sheet references、不构建 dependency graph，也不把 streaming writer 的 formula 输出能力
  提升为 existing-file formula editor。
- Calc metadata：数据或公式相关 worksheet rewrite 默认请求 workbook recalculation：
  `xl/workbook.xml` 会设置 `fullCalcOnLoad="1"`，并按 `CalcChainAction::Remove`
  清理 stale `xl/calcChain.xml` payload、content type override、workbook relationship
  和 calcChain owner `.rels` audit；源包只有 stale calcChain metadata 而没有 payload
  时，只清理 metadata，不创建 payload 或 removed-part audit。
- CalcChain preserve / rebuild：显式 `CalcChainAction::Preserve` 可保留 source 或
  queued calcChain payload / metadata，并把 calcChain owner `.rels` 作为 copy-original
  package-entry audit；`CalcChainAction::Rebuild` 仍未实现，必须在状态变更前失败，
  不污染 `EditPlan`、manifest、package-entry audit、calc policy、planned output 或输出
  bytes。这不代表 calcChain rebuild、公式求值或 relationship repair。
- Workbook calc helper：`request_full_calculation()` 只重写 workbook 根元素的 direct-child
  `calcPr`，保留 `extLst` 或 custom extension 中的 nested decoy；缺少 direct-child
  `calcPr` 时只在真实 workbook closing tag 前插入对应前缀的 `calcPr`。这不是 XML
  schema validation、namespace repair、workbook DOM、public editing API 或通用
  relationship/content-type repair。
- `ReferencePolicyAction::Fail` 可把 caller 不接受的 definedNames、formula cells 或
  calc metadata dependency 转为失败边界；失败必须发生在状态变更前，并保持
  `EditPlan`、manifest、package-entry audit、relationship audits、calc policy、
  planned output 和输出 bytes 不污染。

### P6.5 unsupported edits preserve / request recalc / fail matrix

P6 sheet dependency policy 的当前收束矩阵如下：

- Preserve / audit lane：未知或未修改 package entries、source-owned `.rels`、
  relationship-derived linked parts、sharedStrings、styles、tables、drawings/images/charts、
  VBA、custom XML、comments、pivot、external-link 和 workbook/static dependencies 默认
  copy-original 或 audit-only。审计可记录 caller review、relationship target /
  worksheet reference / payload dependency risk、removed-part inbound relationship 和
  package-entry preservation context，但不做 semantic sync、relationship pruning、
  orphan cleanup、target repair、range repair、style merge 或 shared string migration。
- Request recalculation lane：数据或公式相关 worksheet rewrite，或 caller 对 unsupported
  linked dependencies 显式选择 `ReferencePolicyAction::RequestRecalculation` 时，只把风险
  收敛为 workbook recalculation request。输出会设置 `fullCalcOnLoad="1"`，并按
  `CalcChainAction::Remove` 清理 stale calcChain payload/metadata，或按
  `CalcChainAction::Preserve` 保留 calcChain payload/metadata；linked parts 仍按 preserve /
  audit 处理。这不是公式求值、cached result 更新、dependency graph、calcChain rebuild
  或 linked-part repair。
- Fail lane：caller 选择 `ReferencePolicyAction::Fail`、请求未实现的
  `CalcChainAction::Rebuild`、replacement payload / workbook metadata / sheet catalog
  无法安全解析、ordinary replacement 指向 metadata package entry、removal target 非法，
  或 `save_as()` output guard / copy-original read / writer backend 失败时，失败必须保持
  state hygiene。没有 queued edit 时，`EditPlan`、manifest、package-entry audit、
  relationship audits、worksheet reference audits、payload dependency audits、removed audits、
  calc policy、planned output 和 source/output bytes 都不得新增污染；已有 queued edit 时，
  失败保留既有 queued-safe state，并允许后续安全输出继续消费该 plan。
- Explicit rewrite / removal lane：ordinary `replace_part()` / `remove_part()` 只表达目标
  part 的 `LocalDomRewrite` / omission audit，并保留 inbound relationships，除非已有窄
  helper 明确管理对应 metadata 副作用。当前已知 helper 只覆盖 core/app docProps metadata、
  worksheet rewrite 触发的 calcChain cleanup / workbook `fullCalcOnLoad`、以及 workbook-only
  `request_full_calculation()`；其它 unsupported edits 不自动改写 sibling parts、owner
  `.rels`、content types 或 workbook/worksheet references。

这个矩阵只是内部 Patch MVP 的风险分流和状态卫生边界，不是 public `PackageEditor`、
full preservation pipeline、relationship/content-type repair engine、formula engine 或 broad
existing-file semantic editor。

当前另有 internal `PackageEditor::replace_worksheet_sheet_data()` helper，只替换
已有 worksheet XML 的 `<sheetData>` 元素或 `<sheetData/>`，保留同一 worksheet
part 中的外围 XML metadata，并复用 worksheet replacement 的 calcChain /
fullCalcOnLoad 与 preservation 行为；成功后还会把保留的 worksheet-local
metadata range/reference 风险写入内部 `EditPlan` notes，当前覆盖 sheetPr、
dimension、sheetViews、sheetFormatPr、cols、sheetProtection、protectedRanges、
autoFilter、mergeCells、scenarios、dataConsolidate、customProperties、cellWatches、
smartTags、webPublishItems、dataValidations、conditionalFormatting、hyperlinks、
ignoredErrors、printOptions、pageMargins、pageSetup、drawing、legacyDrawing、
tableParts 和 extLst。这是模板填充/Patch MVP 的局部 rewrite
切片；当前实现不是 true streaming transformer，会物化当前 planned worksheet XML，
因此现在受内部 `package_editor_sheet_data_local_rewrite_byte_limit` 约束：
source/queued worksheet XML、replacement `<sheetData>` payload 或 rewritten worksheet
XML 超过该 bounded local rewrite 限制时，会在状态变更前失败，不污染 `EditPlan`、manifest、
package-entry audit、calc policy、planned output 或输出 bytes；成功路径也会在
EditPlan/output-plan note 和 worksheet part reason 中标明这是 bounded local
worksheet XML rewrite，而不是大文件低内存 streaming rewrite。

当前 `sheetData` 局部替换还覆盖 worksheet-owned background picture / header-footer
VML / legacyDrawing、printerSettings opaque part、registered OLE / control-property
part 的保留、显式 removal audit 和 same-path ordering（含 `planned_output()` 聚合快照）。
逐 fixture 明细见 [Patch 保留能力回归明细](PATCH_PRESERVATION_COVERAGE.md)。这些仍只是
Patch preservation / audit 可见性，不是对应对象的语义编辑、relationship repair/pruning、
orphan cleanup、content type repair、public API 或完整 object lifecycle 支持。

当前还覆盖先排队 worksheet replacement 再执行
sheetData patch 的组合回归，确认 helper 基于当前 planned worksheet bytes 替换，
覆盖 queued worksheet 中普通 `<sheetData>` 和 self-closing `<sheetData/>` 两种形态，
保留 queued wrapper metadata，且不会把 source-only worksheet metadata 复活；
当前还覆盖源 worksheet 使用 self-closing `<sheetData/>` 的成功替换：输出改为
普通 `<sheetData>...</sheetData>`，保留 dimension / autoFilter，沿默认
calcChain remove / fullCalcOnLoad 路径清理 stale 计算 metadata，并保留 unknown bytes；
当前还覆盖 replacement payload 自身为 self-closing `<sheetData/>` 的成功替换：
可清空旧 row/cell，输出保留 `<sheetData/>` 和外围 dimension / autoFilter，
并继续沿默认 calcChain remove / fullCalcOnLoad 与 unknown bytes preservation 路径；
当前还覆盖 source worksheet 和 replacement payload 使用 `<x:worksheet>` /
`<x:sheetData>` 前缀形式时的成功替换：按 local-name 匹配，输出保留原 wrapper /
replacement 字面前缀，仍沿默认 calcChain cleanup 与 unknown bytes preservation 路径；
这不是通用 namespace repair；
当前还覆盖内部按 sheet name 的 `sheetData` patch 入口：它通过 `PackageReader`
workbook sheet catalog resolver 定位目标 worksheet part，再委托同一 part-level
`sheetData` replacement 路径；缺失或重复 sheet name 失败不污染 EditPlan、manifest、
package-entry audit、calc policy 或输出 bytes；invalid package `officeDocument`
entrypoint 也会在 by-name Patch 前失败且不污染状态。当前还覆盖内部按 sheet name 的完整
worksheet replacement 入口：它使用同一 workbook sheet catalog resolver 定位目标
worksheet part，再委托既有 `replace_worksheet_part()` 路径，复用 calcChain /
fullCalcOnLoad 和 preservation 副作用。这不是 sheet rename/delete、sheet catalog
mutation、任意 workbook part-location 支持、随机 cell 编辑或 public API。
当前还覆盖一个真实 `WorkbookWriter` 产物的 internal Patch roundtrip：writer
生成 source workbook，`PackageReader` 解析 sheet catalog，`PackageEditor::replace_worksheet_sheet_data_by_name()`
替换目标 sheet 的 `<sheetData>`，输出再由 `PackageReader` 重读。该回归只证明
untouched worksheet、`[Content_Types].xml`、package `_rels/.rels`、workbook
`.rels` 和 core/app docProps bytes 在此窄路径下保留；该路径还验证 writer
生成的 worksheet XML declaration/prolog 会在 sheetData patch 后按原前缀保留，
且输出 `<worksheet>` 根紧随该 prolog 并通过最终根校验；当 source 走 writer
sharedStrings 策略并注册简单样式时，
还证明 `xl/sharedStrings.xml`、
`xl/styles.xml`、对应 content type override 和 workbook relationships byte-preserved，
并对 replacement `<sheetData>` 里的 shared string index / style id references
生成结构化 `WorksheetPayloadDependencyAudit`。workbook XML 会设置
`fullCalcOnLoad="1"`，且不会在无源 `xl/calcChain.xml` 时凭空创建 calcChain；
它不是 public existing-file editor、随机 cell API、sharedStrings 索引迁移、
style id 迁移、styles 合并、table/drawing 语义同步或大文件 streaming worksheet
transformer。
如果同一 edit 中已有 planned `/xl/workbook.xml`（ordinary replacement，或随后被
`request_full_calculation()` / worksheet rewrite 的 fullCalcOnLoad helper 接管为
helper-managed workbook metadata rewrite），by-name worksheet replacement 和 by-name
`sheetData` helper 会解析当前 planned workbook sheet catalog：旧 source sheet name
会在状态变更前失败，新 planned sheet name 可通过源 workbook `.rels` 定位到既有
worksheet part；当前 planned catalog 回归在普通 planned workbook 和被 workbook metadata
helper 接管后的 planned workbook 中，都覆盖 by-name worksheet replacement 与 by-name
`sheetData` 两条路径上的源 workbook `.rels` dot-segment worksheet target
（例如 `./worksheets/../worksheets/sheetN.xml`），输出保留该 target 字面值，
但 calcChain cleanup 仍可能重写 workbook `.rels`。planned catalog
中缺失或 namespace 非法的 sheet id attribute 仍失败且不污染状态：绑定到
officeDocument relationships namespace 的非 `r` 前缀可用，错误 namespace 的 `x:id`
和普通 `id` 会被当作缺失；namespace 合法但 workbook `.rels` 中缺失的 planned
sheet relationship id，以及通过 workbook `.rels` 指向未注册 worksheet part 的 planned
sheet relationship，也会在两个 by-name helper 状态变更前失败。同一 reader-only 回归还直接覆盖内部
`PackageReader::workbook_sheets_from_xml()` planned workbook XML 入口：只暴露直接
`<sheets><sheet>`，忽略 catalog 外或嵌套 decoy sheet；绑定到
officeDocument relationships namespace 的替代前缀可解析，错误 namespace 或普通
`id` 会被当作缺失，且 planned sheet id 在 workbook relationships 中不存在或解析到未注册 worksheet part 时也会失败。这只是窄
planned workbook catalog resolver，不是 sheet rename/delete、sheet catalog mutation、
relationship repair 或 public API。
如果同一 edit 中 `/xl/workbook.xml` 已被显式 planned removal 移除，两个 by-name
helper 会在解析 sheet catalog 前失败并保留既有 removal / owner `.rels` omission
状态，不回退 source workbook catalog 或复活 workbook metadata。
当前还覆盖内部 `PackageEditor::rename_sheet_catalog_entry()` helper：它只重写当前
planned `/xl/workbook.xml` 的直接 `<sheets><sheet name="...">` 属性，成功后
workbook part 是 `LocalDomRewrite`，worksheet parts、workbook `.rels`、content
types、calcChain 和 unknown entries 保持 copy-original。它使用 planned workbook
catalog 而不是固定 source catalog；planned workbook XML 路径的内部 `planned_output()`
快照也暴露最终 workbook `LocalDomRewrite`、preserved content types / workbook `.rels` /
worksheet / calcChain / unknown entry，以及 structured sheet catalog / definedNames audit；
坏 planned workbook catalog（planned sheet relationship id 在 workbook `.rels` 中缺失，
或 planned worksheet relationship 指向未注册 worksheet part）会在 rename 状态变更前失败，
并保留 queued workbook replacement、EditPlan / audit、manifest、calc policy、
package-entry audit 和输出 bytes。缺失旧名、精确或 ASCII 大小写不敏感重复新名、非法新名、
direct workbook `definedNames` 配合 `ReferencePolicyAction::Fail`、以及 planned workbook removal
失败路径都不污染 EditPlan、manifest、package-entry audit、calc policy 或输出
bytes。它不同步 definedNames、公式、tables、drawings、charts、hyperlinks、
relationship targets、sharedStrings、styles 或 calcChain，不是完整 sheet rename/add/delete、
relationship repair 或 public API。
完整 worksheet replacement payload 现在会在 Patch 状态变更前做最小根元素校验：
replacement XML 必须是单个 `<worksheet>` 根元素（按 local-name 接受前缀形式，
且允许 XML declaration、注释和处理指令位于根元素前），
否则失败不污染 EditPlan、manifest、package-entry audit、calc policy 或输出 bytes；
任意非 prolog 元素或文本位于根元素前仍会被拒绝；这不是 XML schema validation、
namespace repair 或 XML repair。
成功的完整 worksheet replacement 还会对 replacement payload 做 audit-only 扫描：
若新 worksheet XML 使用 shared string indexes、style id references、公式 cell，或包含
sheetPr、dimension、sheetViews、sheetFormatPr、cols、sheetProtection、protectedRanges、
autoFilter、mergeCells、scenarios、dataConsolidate、customProperties、cellWatches、
smartTags、webPublishItems、dataValidations、conditionalFormatting、ignoredErrors、
printOptions、pageMargins、pageSetup、extLst 等 range/reference worksheet metadata，
以及 hyperlinks、drawing、legacyDrawing、picture、legacyDrawingHF、`pageSetup r:id`
printerSettings 引用、oleObjects、controls、tableParts 等 relationship-bearing worksheet
metadata，会在 `EditPlan` / planned output notes 和结构化
`WorksheetPayloadDependencyAudit` 中提示 caller 复核 `xl/sharedStrings.xml`、
`xl/styles.xml`、workbook calc metadata、calcChain policy、range/reference metadata、
worksheet `.rels` 和 linked parts；这不迁移 sharedStrings 索引、不合并 styles、
不计算公式、不重建 calcChain，不重算 dimension、不修复 sheetViews / ranges，
也不修复 relationships 或 linked parts。
完整 worksheet replacement 还会把缺失 worksheet `.rels`、缺失 relationship id、
保留但未引用的 relationship id，以及已知 worksheet 元素/type mismatch 记录为
结构化 `WorksheetRelationshipReferenceAudit`，并传播到内部 `EditPlan` /
`PackageEditorOutputPlan`；这仍只是 Patch audit traceability，不做 namespace
validation、relationship pruning、repair 或 linked-part regeneration。
当前 relationship-id audit 只把绑定到 officeDocument relationships namespace 的
`*:id` 当作 OpenXML relationship 引用；接受非 `r` 前缀，忽略普通 `id` 和错误
namespace 的 `x:id`。这仍不是 namespace validation、XML repair、relationship
repair/pruning 或 public API。
当前 relationship-id audit 还把 `<pageSetup r:id="...">` 作为 printerSettings
relationship 引用处理，记录 missing id / type mismatch notes 与结构化审计，但不会合成
worksheet relationship 或 printerSettings part。
当前回归还覆盖源包没有 worksheet `.rels` 时，replacement XML 的 `<drawing r:id="...">`
只生成 `MissingRelationships` 结构化审计，输出包不会凭空创建 worksheet `.rels`，
unknown bytes 仍原样保留。
当前 `ReferencePolicyAction::Fail` 回归还会用包含 shared string indexes、
style id references、公式 cell、sheetPr / dimension / sheetViews / sheetFormatPr /
cols / scenarios / dataConsolidate / customProperties / cellWatches / smartTags /
webPublishItems / printOptions / pageMargins / pageSetup 等 range/reference metadata、
hyperlinks、drawing 和 tableParts 的完整 worksheet replacement payload 触发失败，验证这些 audit-only payload notes /
`WorksheetPayloadDependencyAudit` 不会污染 `EditPlan`、relationship target audit、manifest、calc policy 或输出 bytes。
replacement `<sheetData>` 中的 shared string indexes、style id references
和公式 cell 也只会生成 audit notes 和结构化 `WorksheetPayloadDependencyAudit`，
提示 caller 复核 `xl/sharedStrings.xml`、
`xl/styles.xml`、workbook calc metadata 和 calcChain policy，不迁移索引、不合并
styles、不求值公式、不重建 calcChain。它不是 public API、随机 cell 编辑、range 修复、dataValidations/conditionalFormatting/hyperlinks/table/
drawing 语义同步、sharedStrings/styles 迁移或大文件低内存 transformer。
invalid/malformed replacement XML、source worksheet 缺失 `sheetData`，以及 source worksheet
`<sheetData>` 起始标签存在但闭合标签损坏/缺失时，当前回归验证失败不污染
EditPlan、manifest、package-entry audit、calc policy 或输出 bytes；这不是 XML repair。

worksheet rewrite 与 ordinary `replace_part()` / `remove_part()` 还有一组小 fixture
证明 comments、threaded comments / persons、pivot table / pivot cache、workbook
external links、custom XML（item / properties）等 linked part 在替换 / 移除 / 同路径
ordering 下的 copy-original 保留、removed-part inbound audit 和 `planned_output()`
快照，并可由 `PackageReader` / `RelationshipGraph` 重读；另有 no-op `save_as()`
roundtrip 证明未修改包 copy baseline。逐 fixture 明细见
[Patch 保留能力回归明细](PATCH_PRESERVATION_COVERAGE.md)。这些不是对应对象的语义编辑、
relationship repair/pruning、orphan cleanup、content type repair、事务式 undo 或 public API。

内部 `PackageEditor::planned_output()` 现在暴露与 `save_as()` 共用的聚合
output-plan snapshot，包括 entry-level 决策顺序、全局
`full_calculation_on_load` / `calc_chain_action`、audit notes、结构化
`removed_parts` / `removed_package_entries`，以及结构化
`relationship_target_audits` / `worksheet_relationship_reference_audits`；
兼容入口 `planned_output_entries()` 仍返回同一份
entry list。entry 决策包括 `source_entry` / `generated` flags、`package_part` /
`part_name` classification、write mode、copied-from-source / omitted flags、
package-entry audit kind、owner part、relationship-derived owner/id/type/target、
omitted removed-part inbound relationship audit 和 reason；

该聚合快照的逐 fixture 覆盖（no-op copy-original、docProps、worksheet calcChain omission、
unknown extension、drawing / VML / percent-decoded drawing、media、table、pivot、
comments / threaded comments / persons、sharedStrings、styles、VBA、chart、custom XML
等的 replace / remove / ordering 状态）见
[Patch 保留能力回归明细](PATCH_PRESERVATION_COVERAGE.md)。

metadata entries 如 `[Content_Types].xml`、package `_rels/.rels` 和 source-owned `.rels`
不会暴露为 package part；这只是内部 Patch 审计可见性，不是 public editor API
或通用 relationship/content-type mutator。
malformed unrelated relationship target 的 explicit removal 回归现在验证
EditPlan / planned output notes-only audit、omitted target、copy-original metadata
entries、无结构化 relationship target / worksheet reference audit、无 package-entry
rewrite/omission，以及 calc policy 保持不变；这不是 relationship repair。
ordinary single-part replacement 回归还覆盖目标 entry 原位重写时，其它源 entries
保持顺序、stored entry method / CRC / uncompressed size 和 bytes 一致；这只是窄
part-level rewrite copy-original 证据。
如果源包是 DEFLATE 输入，当前 `PackageEditor::save_as()` 保留未修改 part 的解压后
payload 语义；minizip-enabled PackageEditor 回归还覆盖 DEFLATE source 上 ordinary
workbook replacement、unknown extension target replacement、workbook calc metadata
helper，以及 worksheet replacement 下的 calcChain cleanup、linked
payload preservation 和 unknown extension owner `.rels` 可由输出 `PackageReader` /
`RelationshipGraph` 重读。不承诺保留源 ZIP compression method、timestamps、
extra fields 或压缩字节。

linked-object fixture 还覆盖 ordinary workbook / drawing / unknown-extension
replacement、各 part（workbook / worksheet / drawing / media / table / sharedStrings /
styles / VBA / VML / percent-decoded drawing）的显式 registered-part removal、以及
remove 后再 ordinary replace 的反向顺序恢复和 media / table / sharedStrings / styles /
chart / VBA / VML / percent-decoded drawing 的 ordinary replace + ordering，含
`planned_output()` 快照。逐 fixture 明细见
[Patch 保留能力回归明细](PATCH_PRESERVATION_COVERAGE.md)。这些只是 no-pruning /
preservation 审计证据，不是 object deletion、relationship/content type repair、
orphan cleanup、事务式 undo 或 public API。

当前内部 `EditPlan` 已能额外记录非 part package-entry 审计项，例如
`[Content_Types].xml`、package `_rels/.rels`、workbook `.rels` rewrite 和
removed calcChain owner `.rels` omission；窄 worksheet replacement 还会把被保留的
worksheet / drawing 等 source-owned `.rels` 记录为 copy-original package-entry audit。
这些审计项现在有内部结构化分类：content types、package relationships 和
source-owned relationships；只有 source-owned `.rels` 审计会携带 owner part，
并且 entry name 必须匹配该 owner part 推导出的 `.rels` 路径。
root-level ordinary owner replacement 的 source-owned `.rels` roundtrip 当前也会验证
`PackageReader` / `RelationshipGraph` 能重新把它挂回 owner part。
另有 reader-only 回归直接验证 unknown extension part 的 owner `.rels` 会保持
metadata-only，并在 `PackageReader` / `RelationshipGraph` 中挂回该 unknown owner；
这只证明 metadata ingestion，不证明完整 preservation 管线。
worksheet rewrite 可递归审计 worksheet-owned 和 drawing-owned relationship target；target
若是 external，会只传播为 external-target 审计 note；若带 query/fragment，会传播
URI-qualified 审计 note，且 base target 可在已注册时纳入 copy-original 依赖；
以 `/` 开头的 absolute internal target 会按 package part path 做 normalization；
percent-encoded internal target 会先解码 `%XX` 再做 part-name normalization，已注册的
decoded target 可纳入 copy-original 依赖；malformed percent escape 或解码后非法的
target 仍走 invalid-target 审计路径；若逃出
package root 或指向 manifest 未注册 part，当前只传播为
package-structure 审计 note。上述 note 和结构化 `RelationshipTargetAudit` 会携带
owner part、relationship id、relationship type、原始 target，以及可用时的
normalized base target part，并会传播到 worksheet `EditPlan` 和 existing-file
`PackageEditor::edit_plan()`；它不会创建、修复或裁剪 package part。
`EditPlan` 现在会按 owner/id/type/raw target/normalized target 对重复
`RelationshipTargetAudit` 做 upsert，并对完全相同的 audit note 去重；重复执行同一
linked worksheet rewrite 时，Patch audit 不会因为同一源 relationship target
反复堆积重复记录。这只是 audit traceability 的状态卫生，不是 relationship rewrite、
repair 或 pruning。
未知 relationship type 只要 target normalize 后命中已注册 internal part，也会被保守纳入
copy-original 依赖；这只是保守依赖审计，不代表理解或编辑该 custom relationship 语义。
当前内部 `DependencyAnalysis`、worksheet `EditPlan` 和 existing-file
`PackageEditor::edit_plan()` 还保留结构化 `RelationshipTargetAudit` 列表，以及
relationship-derived `PartDependency` 的 owner part、relationship id/type 和原始 target
字段；relationship-derived copy-original `EditPlanEntry` 也会保留这些字段作为内部
Patch 审计 metadata。workbook/sharedStrings/styles 等静态依赖仍只带 reason 文本，
不会伪造 relationship metadata；这些字段只用于 Patch 审计，不是 relationship mutator。
当前内部 `PackageEditor::replace_part()` 也会显式拒绝把 `[Content_Types].xml`、
package `_rels/.rels` 或 source-owned `.rels` 当作 ordinary part replacement target，
这些 metadata entry 必须通过窄 metadata helper 和 package-entry audit 路径处理。
该拒绝路径已有 no-state-pollution 回归，覆盖 edit plan entries / notes、
package-entry audit、calc policy、manifest write-mode 和输出包字节不污染。
ordinary part 重复 replacement 也有窄回归，确认同一 part 再次替换时 edit plan、
manifest、preserved source-owned `.rels` audit 和输出 bytes 采用最终替换状态。
内部 `EditPlan` 还覆盖 part-level active/removed 互斥：已移除 part 后续重新
`set_part()` 时会清理同名 removed entry，避免同一 part 同时处于 active 和
removed 计划状态。
当前内部 `PartRewritePlanner` 也能把已注册 part 的显式删除规划为 removed-part audit；
当前 `plan_worksheet_stream_rewrite()` 在 `CalcChainAction::Remove` 且源包注册
`xl/calcChain.xml` 时，会直接把 stale calcChain 写入 removed-part audit；`PackageEditor`
只消费该 worksheet plan，并执行当前窄 content type / workbook `.rels` 副作用。
当源包没有 `xl/calcChain.xml` payload、但 `[Content_Types].xml` 或 workbook
`.rels` 残留 calcChain metadata 时，worksheet replacement 也会清理这些 stale
metadata，但不会伪造 removed-part audit 或创建 calcChain payload。
这只是 part-level 计划元数据，不代表 relationship pruning、orphan cleanup 或完整删除语义。
当前内部 `PackageEditor::request_full_calculation()` 另有 workbook-only calc
metadata 窄 helper：只把 `/xl/workbook.xml` 作为小型 XML metadata 局部重写，
设置 `fullCalcOnLoad="1"`，并按 `CalcChainAction::Remove` 删除 stale
`xl/calcChain.xml` payload 与对应 content type / workbook relationship /
calcChain owner `.rels` audit；如果源包只有 stale calcChain content type override
或 workbook relationship、没有 `xl/calcChain.xml` payload，该 helper 也会清理这些
metadata，但不会创建 payload 或 removed-part audit；或按 `CalcChainAction::Preserve` 保留这些
calcChain payload/metadata。`CalcChainAction::Rebuild` 仍未实现，失败路径保持
EditPlan、manifest、package-entry audit 和输出包不污染。该 helper 不重写
worksheet part、不遍历 linked objects、不计算公式，也不是 public existing-file
editing API 或通用 relationship/content-type repair。
该 helper 现在只更新 workbook 根元素的直接子级 `calcPr`，保留 `extLst` 或
custom extension 内的嵌套同名 decoy；如果没有直接子级 `calcPr`，会在真实
workbook closing tag 前插入按 workbook 根前缀命名的 `calcPr`。这不是 XML
schema validation、namespace repair 或 workbook metadata DOM。
如果 ordinary workbook 或 calcChain replacement 已先排队，随后调用该 workbook-only
helper 会基于已排队 workbook XML 继续规范 `fullCalcOnLoad="1"`，保留
replacement 中的非 calc workbook metadata，并清理 calcChain payload/content
type/workbook relationship；此前排队的 calcChain replacement 不会在输出包复活，
也不会回退到 source workbook bytes。
如果同一路径显式使用 `CalcChainAction::Preserve`，此前排队的 calcChain replacement
会保持为 active `LocalDomRewrite`，并作为最终 `xl/calcChain.xml` payload 写出；
calcChain owner `.rels` 仅作为 copy-original package-entry audit 保留，这不代表
calcChain rebuild、公式求值或通用 metadata repair。
`ReferencePolicyAction::Fail` 失败路径也覆盖已有 ordinary workbook replacement 排队后，
后续 linked worksheet rewrite 或 inbound-linked drawing removal strict failure
仍保留既有 replacement、manifest write-mode、source-owned `.rels` audit、aggregate
`planned_output()` 快照和输出 bytes；
queued core/app docProps generated-small-XML
后，同类失败也会保留既有 metadata edit、package-entry audit、aggregate
`planned_output()` 快照和输出 bytes。
这些 no-state-pollution 回归现在也把 `relationship_target_audits()` 和
`worksheet_relationship_reference_audits()` 纳入状态快照，覆盖 linked worksheet policy fail、
calcChain rebuild rejection、missing sheet-name worksheet/sheetData lookup、invalid
worksheet XML / sheetData source、malformed workbook metadata、missing workbook metadata、
invalid replacement、metadata-entry replacement 和 invalid removal 等失败路径不会追加
stale 结构化 relationship target / worksheet reference audit。
其中 invalid replacement、metadata-entry replacement、invalid removal 和
inbound-linked removal `ReferencePolicyAction::Fail`（含已有 ordinary workbook replacement
排队后的失败）还额外快照
worksheet/workbook payload dependency audit、removed audit 和 calc policy；malformed
linked worksheet policy failure、malformed
workbook metadata / workbook calc metadata、invalid replacement、metadata-entry
replacement、invalid removal 和 inbound-linked removal policy failure 还验证
aggregate `planned_output()` 和 legacy
`planned_output_entries()` 保持 source copy-original 快照。
这只是窄状态一致性回归，不是通用事务。
exact/path-equivalent source-overwrite rejection 和 empty-output / missing-parent / non-directory-parent / existing-directory
output rejection 还验证 guard 失败后 queued part
replacement、structured audit snapshots、calc policy 和 removal audits 保留在
`EditPlan` / manifest / planned output 中，后续安全
`save_as()` 仍输出 queued rewrite 并保留 untouched worksheet / unknown bytes；
同一 guard 现在也覆盖 queued worksheet replacement 的 `fullCalcOnLoad` /
calcChain removal / package-entry audit / planned output 状态，后续安全输出仍按计划
省略 calcChain 并保留 unknown bytes。这只是 reader-backed copy guard，不是 atomic in-place editor。
当 metadata-aware helper 先生成 docProps part 后，后续 ordinary replacement 也应
成为该 part 的最终输出和计划状态；content types / package relationships 仍由 helper
路径维护和审计。反过来，当 docProps metadata helper 后调用时，也会接管此前
ordinary replacement 或 explicit removal 的 core/app part，清理 stale removal /
omitted payload 状态，最终输出、EditPlan 和 manifest 采用 helper
生成状态；同一路径还验证已有 `docProps/custom.xml`、custom-properties package
relationship、custom properties content type override 和 unknown bytes 在 core/app
docProps helper 重写 package metadata 时保持保留，但不编辑 custom properties；
当 worksheet replacement 删除 stale calcChain 时，此前 ordinary
calcChain replacement 也会被移除/省略，不能在输出包中复活；此前 ordinary
workbook replacement 也会被 worksheet rewrite 的 helper-generated fullCalcOnLoad
metadata 接管。
当 worksheet replacement 显式使用 `CalcChainAction::Preserve` 时，此前 ordinary
calcChain replacement 会保持 active `LocalDomRewrite`，并作为最终 `xl/calcChain.xml`
payload 写出；calcChain owner `.rels` 保持 copy-original package-entry audit。这不代表
calcChain rebuild、公式求值或 relationship repair。
如果 worksheet rewrite 已经建立 `fullCalcOnLoad` / calcChain policy，后续 ordinary
workbook replacement 仍会保留该重算策略，且不会把已重写的 workbook `.rels`
package-entry audit 降级为 copy-original。
`CalcChainAction::Preserve` 保留 stale calcChain 时，也会把源
`xl/_rels/calcChain.xml.rels` 记录为 copy-original package-entry audit。
当 workbook metadata rewrite 只改写 `xl/workbook.xml` 而原样保留
`xl/_rels/workbook.xml.rels` 时，也会记录该 workbook `.rels` copy-original audit。
当 worksheet rewrite 的 dependency summary 包含 `xl/sharedStrings.xml`，且源包存在
`xl/_rels/sharedStrings.xml.rels` 时，也会记录 sharedStrings owner `.rels`
copy-original audit，并在 roundtrip 后由 `PackageReader` / `RelationshipGraph`
重新挂回 owner part。
当 worksheet `.rels` 指向一个已注册 unknown extension part，且该 part 自己有
source-owned `.rels` 时，当前窄 fixture 也会记录该 owner `.rels` 的 copy-original
audit，并在 roundtrip 后重新挂回该 unknown owner；reader-only 测试也覆盖该 owner
`.rels` 的解析和挂回。这仍只是可达 part 的保守保留。
这只是 side-effect 可见性，不是完整 relationship/content-type mutator。

## 大型 worksheet 编辑

大型 worksheet 不使用 DOM。

推荐方式：

```text
旧 sheet.xml event reader
→ row/cell transformer
→ 新 sheet.xml stream writer
```

P8.1 boundary draft：

- 该路径属于 controlled large worksheet editing，不是 In-memory random cell editor。
- source worksheet 必须按 event / row stream 顺序读取，transformer 只能在声明的
  lookahead / buffer 边界内工作。
- 输出必须作为 worksheet stream rewrite 或 sheet replacement 进入 `EditPlan`，并暴露
  workbook calc metadata、content types、relationships 和 unknown part preservation
  side effects。
- 当前 bounded `sheetData` local rewrite 会物化 worksheet XML，不能写成大文件低内存
  transformer。
- 当前 `replace_worksheet_cells()` 对 source package worksheet entry 已先通过
  `PackageReader::extract_entry_to_file()` 抽取到 scoped file-backed source，再由
  event-reader / transformer chunk-source readers 驱动 root validation、
  dependency/dimension analysis、relationship-id audit 和 output pass。P8.19 已新增
  internal `scan_worksheet_events_from_chunks()` chunk/window scanner，P8.20 已新增
  internal chunk transformer adapter，P8.21 已把 PackageEditor output pass 接到 chunk
  transformer adapter，P8.22 已把 dependency/dimension analysis 接到 chunk transformer
  adapter，P8.23 已把 relationship-id audit 接到 chunk transformer adapter，P8.24 已把
  worksheet root validation 接到 event-reader chunk-window validator，P8.25 已把
  source-entry file-backed input 接到 pull-based chunk-source scanner，P8.26 已把
  current planned staged chunk input 接到同一 chunk-source scanner，P8.27 已把 queued
  planned replacement string input 接到 string-view chunk-source scanner。queued string
  仍由 prior planned replacement helper 持有完整 `std::string`，所以这不是 full
  planned-input low-memory pipeline；event-reader `max_window_bytes` 仍约束单个未完成
  token / retained window。

P8.2 token model draft：

- event reader 输出 document、worksheet root、metadata raw、sheetData、row、cell、
  raw/unsupported 和 malformed/error tokens。
- row/cell token 必须暴露 normalized row / column index，并保留 raw attributes 供
  pass-through；未知 worksheet metadata 先作为 bounded raw event 处理。
- token 生命周期只覆盖当前 event / row 或声明的 bounded lookahead，不允许累积完整
  worksheet、完整 row map 或完整 cell matrix。
- reader 不做 sharedStrings/style migration、relationship repair、formula evaluation 或
  完整 schema validation。

P8.3 transformer contract draft：

- transformer 在 rewrite 开始前声明 selector，例如目标 range、row set、placeholder
  pattern 或 predicate。
- transformer 输出 pass-through、replace cell/row、bounded insert/delete candidate、
  emit raw、request recalculation 或 fail actions，并保持 row-major order。
- transformer 不能在 row 已输出后回头修改该 row；需要 lookahead 时必须声明 bounded
  row buffer。
- transformer 只报告 sharedStrings、styles、relationships、definedNames、tables、
  drawings 和 calc metadata 依赖，不执行迁移、repair 或公式求值。

P8.4 stream rewrite / `EditPlan` contract draft：

- stream rewrite 只消费 P8.3 ordered actions，生成 staged worksheet part output；成功完成
  和 dependency policy 决策通过后，才进入 active `EditPlan`。
- rewrite writer 只维护 bounded output buffer、current row state 和 incremental dimension
  state；不持有完整 worksheet DOM、row map 或 cell matrix。
- pass-through、replace、insert、delete candidate、emit raw、request recalculation 和 fail
  action 都必须有明确 worksheet bytes、dimension/calc diagnostics 和 failure semantics。
- preflight fail 或 streaming fail 不得污染 active `EditPlan`、manifest、package-entry
  audit、calc policy 或输出 package；temporary artifacts 只是未提交实现细节。
- planned output diagnostics 只作为 internal audit snapshot，暴露 stream rewrite entry、
  copy-original linked parts、relationship/content-type side effects、calcChain action 和
  dependency audits，不是 public output planner。

P8.5 controlled template-fill fixture：

- 当前首个 fixture 使用 internal by-name `sheetData` patch，把 writer-generated template
  worksheet 的 `<sheetData>` 替换为 caller-supplied filled row。
- fixture 证明 untouched worksheet、content types、relationships、sharedStrings 和 styles
  保留，workbook calc metadata 请求 full recalculation，且没有凭空创建 calcChain。
- replacement 可使用 inline string，旧 placeholder sharedStrings 仍会保留；这证明 preserve
  baseline，不是 sharedStrings pruning / migration。
- 该 fixture 仍是 bounded local rewrite baseline，不是 large-file streaming transformer、
  placeholder parser、range patch engine 或 public editor API。

P8.1-P8.5 完成后，编辑模型已有 controlled large worksheet editing 的 baseline 和首个
bounded local fixture；真正低内存 event reader / transformer / stream rewrite 仍是后续
implementation work，不能从该 fixture 推导出任意大 worksheet 随机编辑能力。
P8.16-P8.27 又补上 cell replacement 输出侧 file-backed chunk handoff、source-entry
file-backed extraction first slice、planned-input materialized guard、internal event-reader
chunk/window 与 chunk-source input first slices、transformer chunk-event / chunk-source
adapters，以及 PackageEditor output-pass / dependency-dimension analysis / relationship-id
audit / root validation 对 source-entry chunk-source 的 handoff，并把 current planned
staged chunk input 和 queued planned replacement string input 接到 chunk-source handoff；
但 queued string 仍是 prior helper 已持有的完整字符串，且 PackageReader 仍没有直接
ZIP entry chunk provider，因此不能写成完整 low-memory large worksheet transformer。
本轮 C5 验收只覆盖这些基础片：`fastxlsx.package_reader` 验证 source-entry
`extract_entry_to_file()`，`fastxlsx.package_editor` 验证 by-name cell replacement
file-backed handoff、linked-object preservation、dimension refresh、audit visibility、
output re-read、large source worksheet 超过 materialized guard 仍成功和 temporary file
cleanup，large queued planned string 超过 prior materialized guard 仍成功，
`fastxlsx.worksheet_event_reader` 验证 chunk/window 与 chunk-source scanner，
`fastxlsx.worksheet_transformer` 验证 chunk-event 与 chunk-source transformer，
`fastxlsx.package_editor` 验证 source-entry output-pass、dependency/dimension analysis、
relationship-id audit 和 root validation chunk-source note；planned staged chunk 和 queued
planned string cell replacement 也验证 chunk-source note、large payload 和 output re-read；
direct PackageReader ZIP-entry source 接入仍是后续任务。

适合：

- 修改某些单元格
- 追加行
- 删除/替换某些行
- 模板占位符替换
- 更新维度信息

不适合：

- 任意随机访问大量单元格
- 频繁反复修改同一个大型 sheet

这类场景需要用户选择内存模式或临时索引模式。

大型 worksheet 允许受控编辑，例如：

- 替换整个 sheet 数据。
- 替换某个连续 range。
- 根据 row/cell 条件做一次性 patch。
- 模板占位符替换。

这些能力通常是 O(rows) 扫描和重写，不是 O(1) 随机写入。

## 小型 XML 编辑

小型 XML part 可以使用 DOM，因为这样更可靠：

- 插入一个 sheet 关系。
- 修改 workbook sheet 列表。
- 修改 docProps。
- 修改 content types。
- 更新少量 style。

这些文件通常很小，DOM 成本可控。

## 三种模式

### Streaming Mode

用于新建和大数据写入。

特点：

- 最快。
- 内存最低。
- 不能随机修改已写出的历史行。

### Patch Mode

用于编辑已有文件。

特点：

- part 级别复制和替换。
- 大 part 流式重写。
- 小 part 可选 DOM。
- 通过 EditPlan 管理 relationships、content types、sharedStrings、styles、tables、
  drawings、defined names 和 calc metadata 的联动。
- 默认保留未知和未修改 part。

### In-memory Mode

用于小文件和复杂编辑。

特点：

- API 最方便。
- 适合小数据量。
- 不承诺大文件低内存。
- 可以提供真正随机编辑，例如 `get_cell()`、`set_cell()`、`erase_cell()`、局部样式修改。
- public `Cell` 可以作为输入/返回值；当前 internal `CellStore` 首片已使用 sparse
  record 保存 value kind、payload 和 style id 引用，但后续仍需要字符串/公式池、
  guardrails 和 save-as handoff，不能把每个单元格都长期保存为 owning `Cell` 对象。
  当前 internal `CellStore` sheetData emitter 只生成 standalone `<sheetData>` payload，
  且已有 internal by-name `PackageEditor` handoff 回归；这不代表完整 worksheet writer、
  sharedStrings/styles 迁移或 public save-as。
- 必须有 `max_cells`、内存预算、cell 计数或估算内存这类 guardrail；超过边界时应
  引导用户使用 Streaming 或 Patch。
- P7.4 guardrail 草案要求 future editor 在 open/load、cell mutation、pool growth 和
  save-as preflight 阶段检查 `max_cells` / `memory_budget_bytes`；失败应尽量不污染
  editor state。当前 internal `CellStoreOptions` 首片只覆盖 worksheet-local
  `CellStore::set_cell()` 的 `max_cells` / `memory_budget_bytes` 失败不污染 records。
- `estimated_memory_usage()` 只能写成预算估算，覆盖 record、sparse index、string /
  formula pool、metadata model 和保存阶段 assembly 成本，不是精确 RSS profiler。
- P7.5 save-as handoff 草案要求 source-backed editor 通过 part-level plan 输出：
  未修改和 unknown entries 默认 copy-original，修改的 worksheet/workbook metadata 才
  rewrite；`save_as()` 不承诺原地覆盖。
- In-memory cell edits 不自动修复 sharedStrings、styles、calcChain、relationships、
  tables、drawings 或 range metadata；这些语义必须 preserve、audit、fail 或交给后续
  专门模型。

## 建议 API 形态

新建小工作簿走 `Workbook` / `Worksheet` / `Cell`，这是 append-only 的便利路径，
实现会在 `save(path)` 前持有较小状态：

```cpp
auto wb = fastxlsx::Workbook::create();
auto& sheet = wb.add_worksheet("Data");

sheet.append_row({fastxlsx::Cell::text("Name"), fastxlsx::Cell::number(1.0)});
wb.save("out.xlsx");
```

大文件新建导出走 `WorkbookWriter` / `WorksheetWriter` / `CellView`，按行顺序消费，
不承诺随机修改已输出历史行：

```cpp
auto writer = fastxlsx::WorkbookWriter::create("out.xlsx");
auto sheet = writer.add_worksheet("Data");

sheet.append_row({fastxlsx::CellView::text("Name"), fastxlsx::CellView::number(1.0)});
writer.close();
```

已落地的 Patch public 入口是 `WorkbookEditor`：打开已有 workbook，按 sheet name
替换整个 `<sheetData>` 或改写 sheet catalog 名，再 `save_as()` 输出新 package。这是
当前唯一可编译的 existing-file editing public API（`include/fastxlsx/workbook_editor.hpp`）。

```cpp
auto editor = fastxlsx::WorkbookEditor::open("template.xlsx");
editor.replace_sheet_data("Data", rows);   // rows: std::vector<std::vector<CellValue>>
editor.rename_sheet("Data", "Report");     // 只改 save_as 输出 catalog 的 sheet@name
editor.save_as("output.xlsx");             // 不原地覆盖
```

`rename_sheet()` 是窄的 catalog-name 改写：只动 `xl/workbook.xml` 里
`<sheets><sheet name="...">` 属性，保留 worksheet parts / relationships /
content types / unknown entries，不同步 defined names / formulas / tables /
drawings / relationship targets，也不增删 sheet。它只影响 `save_as()` 输出的
catalog，`worksheet_names()` / `has_worksheet()` 在打开的 session 内仍报告源
workbook 视图。

下面是规划中的更宽 Patch public API 形态伪代码，不是当前已暴露的
`include/fastxlsx` API；当前 `PackageEditor` 仍是 internal test-only 基础，
`WorkbookEditor` 只暴露 whole-`<sheetData>` 替换、窄 sheet catalog 改名和
`save_as()`，尚未暴露 document properties editing、random cell editing 等更宽能力。

```cpp
auto editor = fastxlsx::WorkbookEditor::open("template.xlsx", options); // future
auto sheet = editor.worksheet("Data");                                  // future
sheet.set_cell("A1", fastxlsx::CellValue::text("hello"));               // future
editor.set_document_properties(properties);                             // future
editor.save_as("output.xlsx");
```

```cpp
auto editor = fastxlsx::WorkbookEditor::open("small.xlsx", options); // future
auto sheet = editor.worksheet("Data");                               // future
sheet.set_cell(10, 3, fastxlsx::CellValue::text("hello"));           // future
editor.save_as("output.xlsx");
```

## 关键原则

不要为了编辑方便，让所有路径都变成 DOM。

也不要为了纯流式洁癖，把小型 XML 修改写得过度复杂。

FastXLSX 的边界是：

```text
大 part 流式，小 part 可 DOM，未知 part 原样保留，编辑前先分析联动 part。
```

编辑能力不是流式 writer 的附属补丁。内部 `PackageEditor`、future
`WorksheetRewriter`、future `WorksheetEditor` / random-edit 扩展，以及 EditPlan /
dependency analysis 都应作为核心架构能力推进。
