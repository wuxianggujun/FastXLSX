# Patch 保留能力回归明细

本文件是 FastXLSX Patch（已有文件编辑 / part-level rewrite）逐个 fixture 回归
明细的唯一权威来源，内容从 [编辑模型](EDITING_MODEL.md)、[路线图](ROADMAP.md)
和 [后续推进清单](NEXT_STEPS.md) 中逐字迁移而来，便于这三份设计文档只保留契约、
边界和能力摘要。

以下描述均为内部 `PackageReader` / `PackageEditor` / `EditPlan` 的窄回归证据，
不是 public API、relationship/content-type repair、orphan cleanup 或完整 object
lifecycle 支持；具体边界和非目标见各段原文。设计契约、依赖策略（P6.1–P6.5）、
helper 行为和 bounded local rewrite 边界仍以 [编辑模型](EDITING_MODEL.md) 为准。

## 目录

- 一、sheetData 局部替换下的 worksheet-owned object 保留与审计
- 二、worksheet rewrite 下的 linked-part 保留与 part-level 编辑
- 三、planned_output() 聚合快照逐 fixture 覆盖
- 四、linked-object fixture 的 ordinary replacement / 显式 removal / 反向顺序

## 一、sheetData 局部替换下的 worksheet-owned object 保留与审计

### 背景图片 / header-footer VML / legacyDrawing 保留

当前结构测试还验证输出包中保留的 worksheet `.rels` legacyDrawing
`rId7` 到 `../drawings/vmlDrawing1.vml#shape1` 可由 `PackageReader` /
`RelationshipGraph` 重读；内部 `planned_output()` 快照现在还覆盖对应
`xl/drawings/vmlDrawing1.vml` copy-original entry、URI-qualified legacyDrawing
relationship metadata 和 preserved legacy drawing caller-review note。当前还覆盖
worksheet-owned background picture part 与 header/footer VML drawing part
preservation：`sheetData` 局部替换保留
`<picture>` / `<legacyDrawingHF>` 引用、worksheet `.rels` 中的 `image` /
`vmlDrawing` relationships、`xl/media/background.png` bytes、
`xl/drawings/vmlDrawingHF1.vml` bytes、PNG content type default 和 VML content
type override，并在 `EditPlan` / planned output 中把这些 part 暴露为
relationship-derived copy-original entries。内部 `planned_output()` 快照现在还覆盖该
状态的边界：fullCalcOnLoad / `CalcChainAction::Remove`、worksheet / workbook
`LocalDomRewrite`、content types / package relationships / workbook relationships /
worksheet relationships copy-original、background picture / header-footer VML
copy-original relationship metadata、preserved picture/VML caller-review notes、无
relationship target audit、无 worksheet relationship-id audit、无 removed parts /
removed package entries，且不凭空创建 `xl/calcChain.xml`；这只是 Patch
preservation / audit 可见性，不是图片/VML/header-footer 语义编辑、calcChain
rebuild、relationship repair/pruning、orphan cleanup、content type repair、public API
或完整 object preservation；

### printerSettings opaque part 保留

当前还覆盖 worksheet-owned printerSettings opaque part preservation：`sheetData`
局部替换保留 `<pageSetup r:id>` 引用、worksheet `.rels` 中的
`printerSettings` relationship、`xl/printerSettings/printerSettings1.bin` bytes 和
content type override，并在 `EditPlan` / planned output 中把该 part 暴露为
relationship-derived copy-original entry。内部 `planned_output()` 快照现在还覆盖该
状态的边界：fullCalcOnLoad / `CalcChainAction::Remove`、worksheet / workbook
`LocalDomRewrite`、content types / package relationships / workbook relationships /
worksheet relationships copy-original、printerSettings part copy-original
relationship metadata、preserved pageSetup caller-review note、无 relationship
target audit、无 removed parts / removed package entries，且不凭空创建
`xl/calcChain.xml`；这仍只是 Patch preservation / audit 可见性，不是打印设置
语义编辑、calcChain rebuild、relationship repair/pruning、orphan cleanup、
content type repair、public API 或完整 object lifecycle 支持；

### 背景图片 / VML 的显式 removal audit

当前还覆盖同一 fixture 的显式 removal audit：移除 `xl/media/background.png`
会输出省略目标 media part、保留 PNG default 且不提升为 override，并保留
worksheet `.rels` 中指向缺失 background picture 的 inbound relationship；移除
`xl/drawings/vmlDrawingHF1.vml` 会输出省略目标 VML part、移除 VML content type
override，并保留 worksheet `.rels` 中指向缺失 header/footer VML 的 inbound
relationship；两条路径都会在 `EditPlan` / planned output 暴露结构化
removed-part inbound relationship audit。这仍只是 Patch audit / no-pruning
可见性，不是图片/VML/header-footer 删除语义、relationship repair/pruning、
orphan cleanup、content type repair、public API 或完整 object lifecycle 支持；

### 背景图片 / VML 的 same-path ordering

当前还覆盖同一 background/VML fixture 的 same-path ordering：先移除
`xl/media/background.png` 再 ordinary `replace_part()` 会清理 stale
removed-part audit、恢复 active background picture replacement，保持 PNG default
和 `[Content_Types].xml` source-copy 审计状态且不把 media 提升为 override，并继续
保留 worksheet `.rels` inbound relationship；先 ordinary replace
`xl/drawings/vmlDrawingHF1.vml` 再显式移除会清理 active VML replacement、记录
removed-part inbound audit、输出省略目标 VML part、移除 VML content type override，
并继续保留 worksheet `.rels` inbound relationship 和 sibling background picture
part。内部 `planned_output()` 聚合快照现在也覆盖这两个最终状态：
background picture remove-then-replace 暴露 active picture `LocalDomRewrite`、
content types metadata copy-original、sibling header/footer VML
copy-original、无 stale removals、无 relationship target audits、
fullCalcOnLoad=false、`CalcChainAction::Preserve`，且不发明 picture owner
`.rels`；header/footer VML replace-then-remove 暴露 omitted VML part、
removed-part inbound audit、content types metadata rewrite、sibling background
picture copy-original、无 relationship target audits、fullCalcOnLoad=false、
`CalcChainAction::Preserve`，且不发明 VML owner `.rels`。这仍只是 Patch
same-path state hygiene / audit 可见性，不是事务式 undo、
图片/VML/header-footer 语义合并或删除、relationship repair/pruning、orphan cleanup、
content type repair、public API 或完整 object lifecycle 支持；

### OLE / control-property part 保留

当前还覆盖 worksheet-owned registered OLE opaque
part 与 control-property part preservation：`sheetData` 局部替换保留
`<oleObjects>` / `<controls>` 引用、worksheet `.rels` 中的 `oleObject` /
`control` relationships、`xl/embeddings/oleObject1.bin` bytes、
`xl/ctrlProps/control1.xml` bytes 和对应 content type overrides，并在
`EditPlan` / planned output 中把这些 part 暴露为 relationship-derived
copy-original entries。内部 `planned_output()` 快照现在还覆盖该状态的边界：
fullCalcOnLoad / `CalcChainAction::Remove`、worksheet / workbook
`LocalDomRewrite`、content types / package relationships / workbook relationships /
worksheet relationships copy-original、OLE / control part copy-original
relationship metadata、preserved OLE/control caller-review notes、无 relationship
target audit、无 worksheet relationship-id audit、无 removed parts / removed package
entries，且不凭空创建 `xl/calcChain.xml`；这只是 Patch preservation / audit
可见性，不是 OLE / ActiveX / control 语义编辑、calcChain rebuild、
relationship repair/pruning、orphan cleanup、content type repair、public API 或完整
object preservation；

### OLE / control 的显式 removal audit

当前还覆盖同一 fixture 的显式 removal audit：移除 `xl/embeddings/oleObject1.bin`
会输出省略目标 OLE part、移除 OLE content type override，并保留 worksheet
`.rels` 中指向缺失 OLE object 的 inbound relationship；移除
`xl/ctrlProps/control1.xml` 会输出省略目标 control-property part、移除 control
properties content type override，并保留 worksheet `.rels` 中指向缺失 control
properties 的 inbound relationship；两条路径都会在 `EditPlan` / planned output
暴露结构化 removed-part inbound relationship audit。这仍只是 Patch audit /
no-pruning 可见性，不是 OLE / ActiveX / control 删除语义、relationship
repair/pruning、orphan cleanup、content type repair、public API 或完整 object
lifecycle 支持；

### OLE / control 的 same-path ordering

当前还覆盖同一 worksheet-owned object fixture 的 same-path ordering：先移除
`xl/embeddings/oleObject1.bin` 再 ordinary `replace_part()` 会清理 stale
removed-part audit、恢复 active OLE replacement、让 OLE content type override 和
`[Content_Types].xml` 回到 source/copy-original 审计状态，并继续保留 worksheet
`.rels` inbound relationship；先 ordinary replace `xl/ctrlProps/control1.xml` 再显式
移除会清理 active control replacement、记录 removed-part inbound audit、输出省略
control-properties part、移除 control-properties content type override，并继续保留
worksheet `.rels` inbound relationship 和 sibling OLE part。内部 `planned_output()`
聚合快照现在也覆盖这两个最终状态：OLE remove-then-replace 暴露 active OLE
`LocalDomRewrite`、content types metadata / `ContentTypes` audit、sibling
control copy-original、无 stale removals、无 relationship target audits、
fullCalcOnLoad=false、`CalcChainAction::Preserve`，且不发明 OLE owner
`.rels`；control replace-then-remove 暴露 omitted control part、removed-part
inbound audit、content types metadata rewrite、sibling OLE copy-original、
fullCalcOnLoad=false、`CalcChainAction::Preserve`，且不发明 control owner
`.rels`。这仍只是 Patch
same-path state hygiene / audit 可见性，不是事务式 undo、OLE / ActiveX / control
语义合并或删除、relationship repair/pruning、orphan cleanup、content type repair、
public API 或完整 object lifecycle 支持；

## 二、worksheet rewrite 下的 linked-part 保留与 part-level 编辑

### cell replacement file-backed handoff 下的 linked-object 保留

linked-object fixture 现在还覆盖 by-name `replace_worksheet_cells()` 路径：
cell replacement 输出侧使用 temporary file-backed `PackageEntryChunk` 记录
worksheet `StreamRewrite`，`save_as()` 后输出可由 `PackageReader` 重读，并验证
dimension refresh、calcChain cleanup、workbook `fullCalcOnLoad`、old target cell
payload 跳过审计和临时 XML 文件析构清理。该回归同时证明 worksheet `.rels`、
drawing / drawing `.rels`、media、chart、table、VML、percent-decoded drawing、
untouched `xl/sharedStrings.xml` / owner `.rels`、untouched `xl/styles.xml`、VBA、
reachable unknown extension bytes / owner `.rels`、workbook definedNames、PNG
default content type 和 workbook sharedStrings/styles/VBA relationships 在 cell
replacement handoff 下保持 internal Patch preservation / audit 可见性。
这不是 relationship repair/pruning、object 语义编辑、public API、sharedStrings /
styles migration、table/drawing sync、PackageReader 输入侧 streaming，或完整
low-memory large-file editing。

### comments part

另有 registered comments part 小 fixture 证明 worksheet rewrite 会把
`xl/comments/comment1.xml` 和 source worksheet `.rels` 作为 copy-original
preservation 处理，保留 comments content type override，并可由 `PackageReader` /
`RelationshipGraph` 重读。

### threaded comments / persons

另有 threaded comments / persons 小 fixture 证明 worksheet rewrite 会把
`xl/threadedComments/threadedComment1.xml`、`xl/persons/person.xml`、source worksheet
`.rels` 和 workbook `.rels` 作为 copy-original preservation 处理，并可由
`PackageReader` / `RelationshipGraph` 重读；这不是 comments / threaded comments
编辑、notes UI、relationship repair、orphan cleanup 或 public API。
同一 threaded comments / persons 小 fixture 还覆盖 ordinary
`replace_part("/xl/threadedComments/threadedComment1.xml", ...)` 和显式移除：
replacement 只重写 threaded comments XML，保留 legacy comments、persons、
worksheet `.rels` 中的 legacy/threaded inbound relationships、workbook `.rels`
中的 persons relationship、content type overrides 和 unknown entry；removal
输出省略 threaded comments part 并移除其 content type override，但保留 worksheet
`.rels` 中指向缺失 threaded comments part 的 inbound relationship、persons part /
workbook relationship、legacy comments 和 unknown entry。这不是 threaded comments
model mutation、persons/schema repair、relationship pruning/repair、orphan cleanup、
notes UI 或 public API。
同一 threaded comments / persons 小 fixture 还覆盖 ordinary
`replace_part("/xl/persons/person.xml", ...)` 和显式移除：replacement 只重写 persons
XML，保留 workbook inbound persons relationship、threaded comments、legacy comments、
worksheet `.rels`、content type overrides 和 unknown entry；removal 输出省略 persons
part 并移除 persons content type override，但保留 workbook `.rels` 中指向缺失 persons
part 的 inbound relationship、threaded comments、legacy comments、worksheet 和 unknown
entry。这不是 persons/schema repair、threaded comments model mutation、relationship
pruning/repair、orphan cleanup、notes UI 或 public API。
同一 threaded comments / persons 小 fixture 现在还覆盖两条同路径 ordering：
threaded comments 和 persons 都覆盖 remove-then-ordinary-replace 与
ordinary-replace-then-remove；后续 replacement 会恢复 active part、清理 stale
removed-part audit、让 `[Content_Types].xml` 回到 source/copy-original audit，并且
不凭空创建对应 owner `.rels`；内部 `planned_output()` 快照还覆盖 ordinary threaded
comments replacement 状态：暴露 active threaded comments part `LocalDomRewrite`、
preserved content types / package relationships / workbook / workbook `.rels` / worksheet /
worksheet `.rels` / legacy comments / persons part / unknown entry，且不凭空创建
threaded comments owner `.rels`。这仍只是 Patch audit，不是 threaded comments model
mutation、persons/schema repair、notes UI、relationship repair、orphan cleanup 或 public API；
内部 `planned_output()` 快照还覆盖 threaded comments remove-then-replace 状态：暴露 active threaded comments part local-DOM rewrite、
content types copy-original audit、preserved package/workbook/worksheet `.rels`、
legacy comments、persons part 和 unknown entry，清空 output-plan removed_parts /
removed_package_entries 中的 stale removal 状态，且不凭空创建 threaded comments owner
`.rels`。这仍只是 Patch audit，不是 threaded comments undo、semantic merge、
relationship repair、orphan cleanup 或 public API。threaded comments 后续 removal 会清理 active
replacement、记录 removed-part 和 worksheet inbound relationship audit、输出省略
threaded comments part、移除 threaded comments content type override，并保留 worksheet
`.rels` 中指向缺失 part 的 inbound relationship、persons part / workbook relationship、
legacy comments 和 unknown entry；persons 后续 removal 会清理 active replacement、
记录 removed-part 和 workbook inbound relationship audit、输出省略 persons part、
移除 persons content type override，并保留 workbook `.rels` 中指向缺失 part 的
inbound relationship、threaded comments、legacy comments、worksheet 和 unknown entry。
内部 `planned_output()` 快照还覆盖 persons remove-then-replace 状态：暴露 active
persons part local-DOM rewrite、content types copy-original audit、preserved
package/workbook/worksheet `.rels`、threaded comments、legacy comments 和 unknown
entry，清空 output-plan removed_parts / removed_package_entries 中的 stale removal
状态，且不凭空创建 persons owner `.rels`。这仍只是 Patch audit，不是
persons/schema undo、semantic merge、relationship repair、orphan cleanup 或 public API。
这不是事务式 undo、threaded comments/persons 语义合并、persons/schema repair、
relationship pruning/repair、content type repair、orphan cleanup、notes UI 或 public API。
内部 output-plan 快照还暴露单个 omitted threaded comments part / removed-part audit
（target 为 threaded comments part、reason 保留 after replacement、inbound relationship 一条）、
worksheet inbound threadedComment relationship audit、content types rewrite、preserved
worksheet / workbook `.rels` 与 persons part、且 removed_package_entries 为空、不凭空创建
threaded comments owner `.rels`。
内部 output-plan 快照还暴露单个 omitted persons part / removed-part audit（target 为
persons part、reason 保留 after replacement、inbound relationship 一条）、workbook inbound
persons relationship audit、content types rewrite、preserved workbook/worksheet `.rels`
与 threaded comments part、且 removed_package_entries 为空、不凭空创建 persons owner `.rels`。

### pivot table / pivot cache

另有 pivot table / pivot cache 小 fixture 证明 worksheet rewrite 会把
`xl/pivotTables/pivotTable1.xml`、`xl/pivotCache/pivotCacheDefinition1.xml`、
`xl/pivotCache/pivotCacheRecords1.xml`、source worksheet `.rels`、
pivot table owner `.rels`、pivot cache definition owner `.rels` 和 workbook `.rels`
作为 copy-original preservation 处理，并可由 `PackageReader` / `RelationshipGraph`
重读；这不是 pivot table 编辑、pivot cache rebuild、relationship repair、
orphan cleanup 或 public API。
该 worksheet rewrite 路径的内部 `planned_output()` 现在也暴露 fullCalcOnLoad /
`CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook `LocalDomRewrite`、
package/workbook/worksheet `.rels` copy-original、pivot table / pivot cache
definition / pivot cache records relationship context、content types 和 unknown entry
copy-original，且确认不凭空创建 records owner `.rels`；这仍只是 Patch audit 快照，
不是 pivot cache rebuild、records refresh、relationship repair/pruning、orphan cleanup
或 public API。
同一 pivot table / pivot cache 小 fixture 还覆盖 ordinary
`replace_part("/xl/pivotTables/pivotTable1.xml", ...)` 和显式移除：replacement
只重写 pivot table XML，保留 worksheet `.rels` inbound pivotTable relationship、
pivot table owner `.rels` / pivotCacheDefinition relationship、pivot cache definition /
records parts、pivot cache definition owner `.rels`、workbook `<pivotCaches>`、
workbook `.rels` pivotCacheDefinition relationship、content type overrides 和 unknown
entry；removal 输出省略 pivot table part 及其 owner `.rels`，移除 pivot table
content type override，但保留 worksheet `.rels` 中指向缺失 pivot table part 的
inbound relationship、workbook pivot cache metadata、pivot cache definition / records
链和 unknown entry。这不是 pivot table 语义编辑、pivot cache rebuild、cache records
刷新、relationship pruning/repair、orphan cleanup、owner `.rels` repair 或 public API。
同一路径现在还覆盖 pivot table remove-then-ordinary-replace 和
ordinary-replace-then-remove ordering：后续 replacement 会恢复 active pivot table
part、清理 stale removed-part / removed owner `.rels` audit、恢复 owner `.rels`
copy-original audit，并让 `[Content_Types].xml` 回到 source/copy-original audit；
后续 removal 会清理 active replacement、记录 removed-part / removed owner `.rels`
audit、输出省略 pivot table part 和 owner `.rels`、移除 pivot table content type
override，并保留 worksheet `.rels` 中指向缺失 part 的 inbound relationship、
workbook pivot cache metadata、pivot cache definition / records 链和 unknown entry。
这不是事务式 undo、pivot table 语义合并、pivot cache rebuild、
relationship pruning/repair、content type repair、orphan cleanup 或 public API。
内部 `planned_output()` 快照还覆盖该 remove-then-replace restore 状态：
暴露 active pivot table `LocalDomRewrite` entry、pivot table owner `.rels`
copy-original `SourceRelationships` audit、content types copy-original audit，以及
preserved package/worksheet/workbook relationships、pivot cache definition / records 链
和 unknown entry；该快照也覆盖 replace-then-remove final-removal 状态：暴露
omitted pivot table part、omitted pivot table owner `.rels`、worksheet inbound
pivotTable relationship audit、content types rewrite、preserved worksheet/workbook
relationships、pivot cache definition / records 链和 unknown entry；这仍只是 Patch
audit，不是 pivot table 语义编辑、pivot cache rebuild、relationship pruning/repair、
orphan cleanup 或 public API。
同一 fixture 还覆盖 ordinary
`replace_part("/xl/pivotCache/pivotCacheDefinition1.xml", ...)` 和显式移除：
replacement 只重写 pivot cache definition XML，保留 workbook/pivot-table inbound
relationships、pivot cache records、pivot cache definition owner `.rels`、content type
overrides 和 unknown entry；removal 输出省略 pivot cache definition part 及其 owner
`.rels`，移除 cache definition content type override，但保留 workbook/pivot table
inbound relationships、pivot table、pivot cache records、worksheet 和 unknown entry。
这不是 pivot cache rebuild、cache-record refresh、relationship pruning/repair、
orphan cleanup、owner `.rels` repair 或 public API。
同一路径现在还覆盖 remove-then-ordinary-replace 和 ordinary-replace-then-remove
ordering：后续 replacement 会恢复 active pivot cache definition、清理 stale
removed-part / removed owner `.rels` audit、恢复 owner `.rels` copy-original audit，并让
`[Content_Types].xml` 回到 source/copy-original audit；后续 removal 会清理 active
replacement、记录 removed-part / removed owner `.rels` audit、输出省略 cache definition
part 和 owner `.rels`，并保留 workbook / pivot table inbound relationships、pivot table、
cache records、worksheet 和 unknown entry。这不是事务式 undo、pivot cache 语义合并、
relationship pruning/repair、content type repair、orphan cleanup 或 public API。
内部 `planned_output()` 快照还覆盖该 remove-then-replace restore 状态：暴露
active pivot cache definition `LocalDomRewrite` entry、owner `.rels` copy-original
`SourceRelationships` audit、content types copy-original audit，以及 preserved
package/worksheet/workbook relationships、pivot table/cache records/unknown entries；
该快照也覆盖 replace-then-remove final-removal 状态：暴露 omitted cache definition
part、omitted owner `.rels`、workbook / pivot table inbound pivotCacheDefinition
relationship audit、content types rewrite、preserved workbook/worksheet/pivot
table/cache records/unknown entries；这仍只是 Patch audit，不是 pivot cache rebuild、
cache-record refresh、relationship pruning/repair、content type repair、orphan cleanup
或 public API。
同一 fixture 还覆盖 ordinary
`replace_part("/xl/pivotCache/pivotCacheRecords1.xml", ...)` 和显式移除：
replacement 只重写 pivot cache records XML，保留 cache definition owner `.rels`
inbound relationship、pivot cache definition、pivot table、workbook / worksheet
relationships、content type overrides 和 unknown entry；removal 输出省略 pivot cache
records part，移除 records content type override，但保留 cache definition owner `.rels`
中指向缺失 records part 的 inbound relationship、pivot cache definition、pivot table、
workbook、worksheet 和 unknown entry。这不是 pivot cache records refresh、
pivot cache rebuild、relationship pruning/repair、orphan cleanup 或 public API。
同一路径现在还覆盖 remove-then-ordinary-replace 和 ordinary-replace-then-remove
ordering：后续 replacement 会恢复 active pivot cache records、清理 stale
removed-part audit、让 `[Content_Types].xml` 回到 source/copy-original audit，并且
不凭空创建 records owner `.rels`；后续 removal 会清理 active replacement、记录
removed-part 和 cache-definition inbound relationship audit、输出省略 records part、
移除 records content type override，并继续保留 cache definition owner `.rels`
中指向缺失 records part 的 inbound relationship、pivot cache definition、pivot table、
workbook、worksheet 和 unknown entry。这不是事务式 undo、pivot cache records 语义合并、
relationship pruning/repair、content type repair、orphan cleanup 或 public API。
内部 `planned_output()` 快照还覆盖该 remove-then-replace restore 状态：暴露
active pivot cache records `StreamRewrite` entry、content types copy-original audit、
preserved package/worksheet/workbook relationships、pivot table/cache definition 链和
unknown entry，且 no invented records owner `.rels`；该快照也覆盖
replace-then-remove final-removal 状态：暴露 omitted records part、cache-definition
inbound pivotCacheRecords relationship audit、content types rewrite、preserved
cache definition owner `.rels`、no invented records owner `.rels` 和 preserved
workbook/worksheet/pivot table/cache definition/unknown entries；这仍只是 Patch audit，
不是 pivot cache records refresh、pivot cache rebuild、relationship pruning/repair、
content type repair、orphan cleanup 或 public API。

### workbook external links

另有 workbook external links 小 fixture 证明 worksheet rewrite 在重写
`xl/workbook.xml` calc metadata 时，仍会保留 workbook `<externalReferences>`、
workbook `.rels` 中的 externalLink relationship、`xl/externalLinks/externalLink1.xml`、
externalLink owner `.rels`、external `externalLinkPath` target、content type override
和 unknown entry，并可由 `PackageReader` / `RelationshipGraph` 重读；这不是
external links 编辑、外部数据刷新、路径校验、relationship repair、orphan cleanup
或 public API。
该 worksheet rewrite 路径的内部 `planned_output()` 现在也暴露 fullCalcOnLoad /
`CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook `LocalDomRewrite`、
workbook `.rels` copy-original、externalLink part 与 owner `.rels` copy-original、
content types copy-original 和 unknown entry preservation，且不新增 relationship
target audit。这仍只是 Patch audit 快照，不是 external links 编辑或 relationship
repair。
同一 workbook external links 小 fixture 还覆盖 ordinary
`replace_part("/xl/externalLinks/externalLink1.xml", ...)` 和显式移除：
replacement 只重写 externalLink XML，保留 workbook `.rels` inbound externalLink
relationship、externalLink owner `.rels` 中的 external `externalLinkPath` target、
content type override、worksheet 和 unknown entry；removal 输出省略 externalLink
part 及其 owner `.rels`，移除 externalLink content type override，但保留 workbook
`<externalReferences>`、workbook `.rels` 中指向缺失 externalLink part 的 inbound
relationship、worksheet 和 unknown entry。这不是 external links 语义编辑、
外部数据刷新、路径校验、relationship pruning/repair、orphan cleanup、owner `.rels`
repair 或 public API。
同一路径还覆盖先显式移除 externalLink 再 ordinary replace，以及先 ordinary replace
再显式移除：前者清理 stale removed-part / removed owner `.rels` audit，恢复 active
externalLink part、owner `.rels` copy-original audit 和 source content types audit；
后者清理 active replacement，记录 removed-part / removed owner `.rels` audit，输出省略
externalLink part 和 owner `.rels`，并保留 workbook inbound relationship、worksheet 和
unknown entry。这不是事务式 undo、external links 语义合并、relationship pruning/repair、
content type repair、orphan cleanup 或 public API。
内部 `planned_output()` 快照还覆盖该 remove-then-replace restore 状态：
暴露 active externalLink `LocalDomRewrite` entry、externalLink owner `.rels`
copy-original `SourceRelationships` audit、content types copy-original audit，以及
preserved package/workbook relationships、workbook、worksheet 和 unknown entry；
该快照也覆盖 replace-then-remove final-removal 状态：暴露 omitted externalLink part、
omitted externalLink owner `.rels`、workbook inbound externalLink relationship audit、
content types rewrite、preserved package/workbook relationships、workbook、worksheet
和 unknown entry；这仍只是 Patch audit，不是 external links 语义编辑、外部数据刷新、
relationship pruning/repair、orphan cleanup 或 public API。

### custom XML

另有 custom XML 小 fixture 证明 worksheet rewrite 会保留 package `_rels/.rels`
中的 customXml relationship、`customXml/item1.xml`、custom XML item owner `.rels`、
`customXml/itemProps1.xml`、custom XML properties content type override 和 unknown
entry，并可由 `PackageReader` / `RelationshipGraph` 重读；这不是 custom XML
编辑、schema/data binding、relationship repair、orphan cleanup 或 public API。
该 worksheet rewrite 路径的内部 `planned_output()` 现在也暴露 fullCalcOnLoad /
`CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook `LocalDomRewrite`、
package relationships copy-original、custom XML item / item owner `.rels` /
properties part copy-original、content types copy-original 和 unknown entry preservation，
且不新增 relationship target audit、不凭空创建 properties owner `.rels`。这仍只是
Patch audit 快照，不是 custom XML 编辑、schema/data binding 或 relationship repair。
同一 custom XML 小 fixture 还覆盖 ordinary `replace_part("/customXml/item1.xml", ...)`：
只重写 custom XML item，保留 package `_rels/.rels` customXml inbound relationship、
custom XML item owner `.rels` / customXmlProps relationship、`customXml/itemProps1.xml`、
custom XML properties content type override、默认 XML content type 和 unknown entry
copy-original 基线，并可由 `PackageReader` / `RelationshipGraph` 重读；这不是
custom XML 语义编辑、schema/data binding、relationship repair、content type repair、
orphan cleanup 或 public API。
内部 `planned_output()` 快照现在也覆盖该 ordinary replacement 状态：暴露 active
custom XML item `LocalDomRewrite` entry、owner `.rels` copy-original
`SourceRelationships` audit、preserved package relationships、content types、
workbook、worksheet、properties part 和 unknown entry，且 output-plan removed_parts /
removed_package_entries 为空、不凭空创建 properties owner `.rels`。这仍只是
Patch audit 可见性，不是 custom XML 语义编辑或 relationship/content-type repair。
同一 custom XML 小 fixture 还覆盖显式移除 `customXml/item1.xml`：输出省略
custom XML item 及其 source-owned owner `.rels`、保留 package `_rels/.rels`
customXml inbound relationship、保留 `customXml/itemProps1.xml`、custom XML
properties content type override、默认 XML content type 和 unknown entry，且不重写
`[Content_Types].xml`；这不是 custom XML 删除语义、schema/data binding、
relationship pruning/repair、content type repair、orphan cleanup 或 public API。
内部 `planned_output()` 快照现在也覆盖该 explicit removal 状态：暴露 omitted
custom XML item、omitted source-owned owner `.rels`、removed-part / removed owner
`.rels` audit、package inbound customXml relationship audit，以及 preserved package
relationships、content types、workbook、worksheet、properties part 和 unknown entry；
仍不凭空创建 properties owner `.rels`。这只是 Patch audit 快照，不是 custom XML
删除语义、relationship pruning/repair、content type repair、orphan cleanup 或
public API。
同一路径顺序回归还覆盖先显式移除再 ordinary replace，以及先 ordinary replace
再显式移除：前者恢复 active custom XML item、清理 stale removed-part /
removed owner `.rels` audit、恢复 owner `.rels` copy-original audit，且不重写
`[Content_Types].xml`；后者清理 active replacement、记录 removed-part /
removed owner `.rels` audit、输出省略 custom XML item 和 owner `.rels`，
并保留 package inbound relationship、properties part、默认 XML content type 和
unknown entry。内部 `planned_output()` 快照现在还覆盖该 restore 状态：
暴露 active custom XML item `LocalDomRewrite` entry、owner `.rels` copy-original
`SourceRelationships` audit、preserved package relationships、content types、
workbook、worksheet、properties part 和 unknown entry。内部 `planned_output()` 快照还覆盖该 final-removal 状态：
暴露 omitted custom XML item、omitted source-owned owner `.rels`、package inbound
customXml relationship audit，以及 preserved package relationships、content types、
workbook、worksheet、properties part 和 unknown entry。这不是事务式 undo、custom XML 语义合并、relationship
pruning/repair、content type repair、orphan cleanup 或 public API。
同一 custom XML 小 fixture 还覆盖 `customXml/itemProps1.xml` properties part
的 ordinary replacement 和显式 removal：replacement 只重写 properties part，
保留 custom XML item、item owner `.rels` / customXmlProps inbound relationship、
package customXml relationship、properties content type override 和 unknown entry；
removal 输出省略 properties part、移除 properties content type override，但保留
custom XML item、item owner `.rels` 中指向缺失 properties part 的 inbound
customXmlProps relationship、package customXml relationship、默认 XML content type
和 unknown entry。这不是 custom XML properties 编辑、schema/data binding、
relationship pruning/repair、content type repair、orphan cleanup 或 public API。
内部 `planned_output()` 快照还覆盖 ordinary properties replacement 状态：暴露 active
properties part `LocalDomRewrite`、preserved content types / package relationships、
preserved custom XML item / item owner `.rels` / workbook / worksheet / unknown
entry，且不凭空创建 properties owner `.rels`。这仍只是 Patch audit，不是 custom XML
properties 语义编辑、schema/data binding、事务式 undo、relationship pruning/repair、
content type repair、orphan cleanup 或 public API。
同一路径顺序回归还覆盖 properties part 先显式移除再 ordinary replace，以及先
ordinary replace 再显式移除：前者清理 stale removed-part audit、恢复 active
properties part、恢复 properties content type override/content-types copy-original
audit，并继续保留 item owner `.rels`；后者清理 active replacement、记录
removed-part audit、输出省略 properties part、移除 properties content type override，
并继续保留 item owner `.rels` 中的 inbound customXmlProps relationship。这不是
事务式 undo、custom XML properties 语义合并、relationship pruning/repair、
content type repair、orphan cleanup 或 public API。
内部 `planned_output()` 快照还覆盖该 properties final-removal 状态：暴露 omitted
properties part、item-owned inbound customXmlProps relationship audit、content types
rewrite、preserved custom XML item / item owner `.rels` / package relationships /
workbook / worksheet / unknown entry，且不凭空创建 properties owner `.rels`。这仍只是
Patch audit，不是 custom XML properties 删除语义、relationship pruning/repair、
content type repair、orphan cleanup 或 public API。
内部 `planned_output()` 快照还覆盖该 properties restore 状态：暴露 active properties
part `LocalDomRewrite`、restored content types copy-original audit、preserved custom
XML item / item owner `.rels` / package relationships / workbook / worksheet /
unknown entry，且不凭空创建 properties owner `.rels`。这仍只是 Patch audit，不是
custom XML properties 语义合并、事务式 undo、relationship pruning/repair、
content type repair、orphan cleanup 或 public API。
同一 custom XML fixture 还覆盖跨路径顺序：先显式移除 custom XML item，再 ordinary
replace `customXml/itemProps1.xml` properties part。后续 properties replacement
只重写 properties payload，保留 removed custom XML item / removed owner `.rels`
audit，输出继续省略 custom XML item 和 item owner `.rels`，并保留 package customXml
inbound relationship、properties content type override、默认 XML content type 和 unknown
entry。这不是 custom XML dependency repair、relationship pruning/repair、
content type repair、orphan cleanup、事务式 undo 或 public API。
内部 `planned_output()` 快照还覆盖该跨路径状态：暴露 omitted custom XML item、
omitted source-owned owner `.rels`、package inbound customXml relationship audit、
active properties part local-DOM rewrite、preserved package relationships / content
types / workbook / worksheet / unknown entry，且不凭空创建 properties owner `.rels`。
这仍只是 Patch audit，不是 custom XML dependency repair、relationship pruning/repair、
content type repair、orphan cleanup、事务式 undo 或 public API。
反向跨路径顺序也有回归覆盖：先显式移除 properties part，再 ordinary replace
custom XML item。后续 item replacement 只重写 item payload，保留 removed properties
part audit / content-types rewrite，输出继续省略 properties part 和 properties content
type override，并保留 item owner `.rels` 中指向缺失 properties part 的
customXmlProps relationship、package customXml inbound relationship、默认 XML content type
和 unknown entry。这不是 custom XML dependency repair、relationship pruning/repair、
content type repair、orphan cleanup、事务式 undo 或 public API。
内部 `planned_output()` 快照还覆盖该反向跨路径状态：暴露 omitted properties part、
item-owned inbound customXmlProps relationship audit、content types rewrite、active
custom XML item local-DOM rewrite、preserved item owner `.rels` / package relationships /
workbook / worksheet / unknown entry，且不凭空创建 properties owner `.rels`。这仍只是
Patch audit，不是 custom XML dependency repair、relationship pruning/repair、
content type repair、orphan cleanup、事务式 undo 或 public API。

### comments part 的 ordinary replace / remove / ordering

同一小 fixture 还覆盖 ordinary `replace_part("/xl/comments/comment1.xml", ...)`：
只重写 comments XML，保留 worksheet `.rels` inbound comments relationship、
comments content type override、workbook XML / workbook `.rels`、worksheet 和 unknown entry
copy-original 基线，且不凭空创建 comments owner `.rels`；这不是 comments model
mutation、threaded comments、notes UI、relationship repair、orphan cleanup 或 public API。
内部 `planned_output()` 快照还覆盖该 ordinary replacement 状态：暴露 active
comments part local-DOM rewrite、preserved content types / package relationships /
workbook / workbook `.rels` / worksheet / worksheet `.rels` / unknown entry，且不凭空创建
comments owner `.rels`。这仍只是 Patch audit，不是 comments model mutation、notes UI、
relationship repair、orphan cleanup 或 public API。
同一小 fixture 还覆盖显式移除 `xl/comments/comment1.xml`：输出省略 comments part、
移除 comments content type override、保留 worksheet `.rels` inbound comments
relationship，且不凭空创建 comments owner `.rels` omission；这不是 comments
删除语义、threaded comments、notes UI、relationship pruning/repair、orphan cleanup
或 public API。
同一小 fixture 还覆盖先显式移除再 ordinary `replace_part()` 的反向顺序：
后续 replacement 会恢复 active comments part、清理 stale removed-part audit、让
`[Content_Types].xml` 回到 source/copy-original audit、保留 inbound worksheet
`.rels`，且仍不凭空创建 comments owner `.rels`。这不是事务式 undo、comments
语义合并、relationship repair、orphan cleanup 或 public API。
内部 `planned_output()` 快照还覆盖该 remove-then-replace 状态：暴露 active
comments part local-DOM rewrite、content types copy-original audit、preserved
package/workbook/worksheet `.rels` 和 unknown entry，清空 output-plan removed_parts /
removed_package_entries 中的 stale removal 状态，且不凭空创建 comments owner
`.rels`。这仍只是 Patch audit，不是 comments undo、semantic merge、
relationship repair、orphan cleanup 或 public API。
同一小 fixture 还覆盖先 ordinary `replace_part()` 再显式移除的顺序：
后续 removal 会清理 active comments replacement、记录 removed-part audit、输出省略
comments part、移除 comments content type override、保留 inbound worksheet `.rels`，
且仍不凭空创建 comments owner `.rels`。这不是 comments 删除语义、事务式 undo、
relationship pruning/repair、orphan cleanup 或 public API。
内部 `planned_output()` 快照还覆盖该 replace-then-remove final-removal 状态：
暴露单个 omitted comments part / removed-part audit（target 为 comments part、
reason 保留 after replacement、inbound relationship 一条）、content types rewrite、
preserved package/workbook/worksheet `.rels`，且 removed_package_entries 为空、不凭空创建
comments owner `.rels`。这仍只是 Patch audit，不是 comments 删除语义、事务式 undo、
relationship pruning/repair、orphan cleanup 或 public API。
### no-op save_as roundtrip baseline

另有 no-op `save_as()` roundtrip 覆盖 linked-object fixture 的全部源 entry
顺序、stored entry method / CRC / uncompressed size 和 bytes 保持一致，并确认初始
`EditPlan` 只包含 copy-original part entries、没有 metadata package-entry side
effect；这只是未修改包 copy baseline。

## 三、planned_output() 聚合快照逐 fixture 覆盖

当前回归覆盖 no-op
copy-original、docProps generated-small-XML 新增/重写（包括 custom properties
preservation 场景下的 core/app generated entries、`docProps/custom.xml` 与
unknown entry copy-original、content types / package relationships metadata
rewrite、无 removed/audit 污染）、worksheet calcChain omission
与 workbook metadata rewrite、sheetData Patch MVP 的 output-plan snapshot
（worksheet stream-rewrite、workbook metadata rewrite、calcChain omission、
metadata-entry audit、preserved source-owned `.rels` 和 relationship-derived
linked parts）、ordinary unknown extension replacement 的 output-plan snapshot
（target part stream-rewrite、owner `.rels` copy-original audit 和 untouched
linked parts）、unknown extension replace-then-remove final-removal 的 omitted
unknown part / source-owned owner `.rels` omission / inbound worksheet
relationship audit / preserved content types 与 package relationships 快照、
linked worksheet rewrite 的 relationship-derived output audit、
request-recalculation preserve-policy output snapshot、显式 workbook removal
的 owner `.rels` omission、drawing replace-then-remove final-removal 的 omitted
drawing part / owner `.rels` 与 inbound worksheet relationship audit、
VML drawing replace-then-remove final-removal 的 omitted VML part /
removed_parts target-reason-inbound audit / URI-qualified inbound worksheet
relationship audit / content types rewrite / empty removed_package_entries /
no invented owner `.rels` 快照、
VML drawing remove-then-replace 的 active VML drawing local-DOM rewrite /
content types copy-original / preserved package/workbook/worksheet/drawing
relationships / preserved linked/unknown entries / empty removed_parts 与
removed_package_entries / no invented owner `.rels` 快照、
percent-decoded drawing replace-then-remove final-removal 的 omitted decoded
drawing part / removed_parts target-reason-inbound audit / encoded inbound
worksheet relationship audit / content types rewrite / empty removed_package_entries /
no invented owner `.rels` 快照、
percent-decoded drawing remove-then-replace 的 active decoded drawing local-DOM
rewrite / content types copy-original / preserved package/workbook/worksheet/drawing
relationships / preserved linked/unknown entries / no invented owner `.rels` 快照、
media replace-then-remove final-removal 的 omitted default-typed media part /
removed_parts target/reason/inbound audit / inbound drawing relationship audit /
preserved content types 与 drawing `.rels` / empty removed_package_entries
快照、table replace-then-remove final-removal 的 omitted table part /
inbound worksheet relationship audit / content types rewrite 快照、pivot worksheet rewrite
的 fullCalcOnLoad / `CalcChainAction::Remove` / worksheet `StreamRewrite` /
workbook `LocalDomRewrite` / package-workbook-worksheet `.rels` copy-original /
pivot table-cache relationship context / content types 与 unknown entry copy-original /
no invented records owner `.rels` 快照、pivot table
remove-then-replace 的 active pivot table local-DOM rewrite / owner `.rels`
copy-original / content types copy-original / preserved package/worksheet/workbook
relationships 与 pivot cache definition / records 链快照、pivot table
replace-then-remove final-removal 的 omitted pivot table part / owner `.rels` /
worksheet inbound pivotTable relationship audit / content types rewrite /
preserved pivot cache definition 与 records 链快照、pivot cache definition
remove-then-replace 的 active cache definition local-DOM rewrite / owner `.rels`
copy-original / content types copy-original / preserved workbook/worksheet/pivot
table/cache records/unknown entries 快照、pivot cache definition replace-then-remove
final-removal 的 omitted cache definition part / owner `.rels` / workbook 与 pivot
table inbound pivotCacheDefinition relationship audit / content types rewrite /
preserved workbook/worksheet/pivot table/cache records/unknown entries 快照、pivot
cache records remove-then-replace 的 active records stream-rewrite / content types
copy-original / preserved pivot table/cache definition 链 / no invented records
owner `.rels` 快照、pivot cache records replace-then-remove final-removal 的
omitted records part / cache-definition inbound pivotCacheRecords relationship audit /
content types rewrite / preserved cache definition owner `.rels` / no invented
records owner `.rels` 快照、comments ordinary replacement 的 active comments part
local-DOM rewrite / preserved content types / package relationships / workbook /
workbook `.rels` / worksheet / worksheet `.rels` / unknown entry / no invented
comments owner `.rels` 快照、comments
replace-then-remove final-removal 的 omitted comments part / removed_parts
target-reason-inbound audit / worksheet inbound relationship audit / content types
rewrite / preserved package/workbook/worksheet `.rels` / empty removed_package_entries /
no invented owner `.rels` 快照、comments remove-then-replace 的 active comments rewrite /
content types copy-original / preserved package/workbook/worksheet `.rels` 与 unknown entry /
empty removed_parts 与 removed_package_entries / no invented owner `.rels` 快照、threaded comments
ordinary replacement 的 active threaded comments local-DOM rewrite / preserved
content types / package relationships / workbook / workbook `.rels` / worksheet /
worksheet `.rels` / legacy comments / persons part 与 unknown entry / no invented
owner `.rels` 快照、threaded comments replace-then-remove final-removal 的 omitted threaded comments part /
removed_parts target-reason-inbound audit / worksheet inbound threadedComment relationship audit /
content types rewrite / preserved worksheet/workbook `.rels` 与 persons part /
empty removed_package_entries / no invented owner `.rels` 快照、threaded comments
remove-then-replace 的 active threaded comments rewrite / content types copy-original /
preserved package/workbook/worksheet `.rels`、legacy comments、persons part 与 unknown
entry / empty removed_parts 与 removed_package_entries / no invented owner `.rels` 快照、persons
replace-then-remove final-removal 的 omitted persons part / removed_parts
target-reason-inbound audit / workbook inbound relationship audit / content types
rewrite / preserved workbook/worksheet `.rels` 与 threaded comments part /
empty removed_package_entries / no invented owner `.rels` 快照、persons remove-then-replace
的 active persons rewrite / content types copy-original / preserved
package/workbook/worksheet `.rels`、threaded comments、legacy comments 与 unknown
entry / empty removed_parts 与 removed_package_entries / no invented owner `.rels` 快照、sharedStrings
remove-then-replace 的 active sharedStrings stream-rewrite / source-owned
owner `.rels` copy-original / content types copy-original / preserved workbook
`.rels` / empty removed_parts 与 removed_package_entries 快照、sharedStrings
replace-then-remove final-removal 的 omitted sharedStrings part / removed_parts
target-reason-inbound audit / source-owned owner `.rels` omission /
removed_package_entries owner-omission audit / workbook inbound relationship
audit / content types rewrite 快照、styles remove-then-replace
的 active styles local-DOM rewrite / content types copy-original / preserved
workbook `.rels` / empty removed_parts 与 removed_package_entries /
no invented owner `.rels` 快照、styles replace-then-remove final-removal
的 omitted styles part / removed_parts target-reason-inbound audit /
workbook inbound relationship audit / content types rewrite /
empty removed_package_entries / no invented owner `.rels` 快照、VBA project
remove-then-replace 的 active VBA project stream-rewrite / content types
copy-original / preserved package/workbook relationships / preserved
linked/unknown entries / empty removed_parts 与 removed_package_entries /
no invented owner `.rels` 快照、VBA project
replace-then-remove final-removal 的 omitted VBA
project part / removed_parts target-reason-inbound audit / workbook inbound
relationship audit / content types rewrite / empty removed_package_entries /
no invented owner `.rels` 快照、chart remove-then-replace 的 active chart
stream-rewrite / content types copy-original / preserved drawing relationships /
empty removed_parts 与 removed_package_entries / no invented owner `.rels`
快照、chart replace-then-remove final-removal 的 omitted chart part /
removed_parts target-reason-inbound audit / drawing-owned direct 与 URI-qualified
inbound relationship audit / content types rewrite / empty removed_package_entries /
no invented owner `.rels` 快照、custom XML item
remove-then-replace 的 active custom XML item local-DOM rewrite / owner `.rels`
copy-original audit / preserved package relationships、content types、workbook、
worksheet、properties part 与 unknown entry 快照、custom XML item replace-then-remove
final-removal 的 omitted item / owner `.rels` / package inbound customXml
relationship audit / preserved package relationships、content types、workbook、
worksheet、properties part 与 unknown entry 快照、custom XML properties
ordinary replacement 的 active properties part local-DOM rewrite / preserved
content types、package relationships、custom XML item、item owner `.rels`、
workbook、worksheet 与 unknown entry / no invented properties owner `.rels` 快照、
custom XML properties
remove-then-replace 的 active properties part local-DOM rewrite / restored content
types copy-original audit / preserved custom XML item、item owner `.rels`、package
relationships、workbook、worksheet 与 unknown entry / no invented properties owner
`.rels` 快照，以及显式 chart removal
的 removed-part inbound audit。

## 四、linked-object fixture 的 ordinary replacement / 显式 removal / 反向顺序

### ordinary workbook / drawing / unknown-extension replacement

linked-object fixture 上的 ordinary workbook replacement 回归还覆盖只重写
`xl/workbook.xml` 时，workbook `.rels` 被记录为 copy-original package-entry audit，
worksheet、drawing、media、sharedStrings、styles、VBA、calcChain 和 unknown extension
entries 仍保持同一 copy-original 基线。
同一路径现在还覆盖 ordinary workbook replace-then-remove：后续 removal 清理 active
workbook replacement、记录 removed-part 和 workbook owner `.rels` omission audit，
输出省略 workbook part 及其 owner `.rels`、移除 workbook content type override，
但保留 package `_rels/.rels` 中指向缺失 workbook 的 officeDocument relationship
以及 worksheet/drawing/table/sharedStrings/styles/VBA/calcChain/unknown downstream parts。
这不是 workbook deletion semantics、sheet catalog sync、relationship/content type
repair、orphan cleanup、事务式 undo 或 public API。
linked-object fixture 上的 ordinary drawing replacement 回归还覆盖只重写
`xl/drawings/drawing1.xml` 时，drawing `.rels` 被记录为 copy-original package-entry
audit，chart、media 和 unknown extension entries 仍保持同一 copy-original 基线；
这不是 drawing mutation、图片编辑或图表编辑。
linked-object fixture 上的 ordinary unknown extension replacement 回归还覆盖只重写
`custom/opaque-extension.bin` 时，其 owner `.rels` 被记录为 copy-original
package-entry audit 并原样保留，workbook、worksheet、drawing、chart 和 media
entries 仍保持同一 copy-original 基线；这不是 unknown extension 语义编辑、
custom relationship repair 或 public API。
对同一 unknown extension 的 repeated ordinary replacement 回归还验证最终 bytes、
manifest write-mode、edit-plan reason 和 owner `.rels` audit 会 upsert 到最后一次
替换状态，owner `.rels` 仍保持 copy-original，且不会产生 removed-part 或 removed
package-entry audit；这不是事务式编辑或 unknown extension 语义合并。
同一路径还覆盖 unknown extension 先显式移除再 ordinary replacement 的反向顺序：
后续 replacement 会恢复 active unknown extension part、清理 stale removed-part audit
和 stale removed owner `.rels` audit、恢复 owner `.rels` copy-original package-entry
audit、保留 worksheet `.rels` 中的 inbound unknown relationship、保留其它
linked/source entries，且不重写 `[Content_Types].xml`；这不是 unknown extension
语义合并、custom relationship repair、metadata repair、事务式 undo 或 public API。
同一路径现在还覆盖 unknown extension 先 ordinary replacement 再显式移除：
后续 removal 会清理 active replacement、记录 removed-part 和 removed owner `.rels`
audit、输出省略 unknown extension part 及其 owner `.rels`、保留 worksheet `.rels`
中指向缺失 part 的 inbound relationship、保留其它 linked/source entries 和默认
`bin` content type，且不重写 `[Content_Types].xml`；这不是 unknown extension
删除语义、custom relationship repair、metadata repair、relationship pruning/repair、
content type repair、orphan cleanup、事务式 undo 或 public API。

### 显式 registered-part removal 窄切片

当前内部 `PackageEditor::remove_part()` 还覆盖显式 registered-part removal 窄切片：
只接受源 package 中已有的普通 part，输出时省略目标 part 和存在时的 source-owned
owner `.rels`，记录 removed-part / removed package-entry audit，并在目标存在 content
type override 时重写 `[Content_Types].xml`；它不会修剪其它 part 指向该目标的 inbound
relationships，不是 object deletion、relationship pruning 或事务式编辑；removed-part
audit 现在会同时保留结构化 inbound relationship metadata（owner entry、owner
part、id、type、raw target、normalized target part）和可读 reason，记录仍指向
被删除 part 的 inbound package/source relationship 上下文，便于 Patch traceability。
若 removed-part inbound 扫描遇到 malformed percent relationship target，会记录
EditPlan / planned output audit note 并继续保留该 `.rels` bytes，不让无关坏
target 阻塞显式 part removal；planned output 也暴露 removed target omission
与 copy-original metadata entries，且不新增结构化 relationship target audit；这不是
target repair 或 relationship validation。

### 各 part 的显式移除（workbook/worksheet/drawing/media/table/sharedStrings/styles/VBA/VML/percent-decoded）

linked-object fixture 还覆盖显式移除 `xl/workbook.xml`：输出省略 workbook part
及其 source-owned workbook `.rels`，移除 workbook content type override，保留
package `_rels/.rels` 中的 inbound officeDocument relationship，且不修剪 worksheet、
drawing、table、sharedStrings、styles、VBA、calcChain 或 unknown extension 等
downstream/source parts；这只是 no-pruning / preservation 审计证据，不是 workbook
deletion、sheet catalog sync、relationship repair 或完整 workbook editing。
linked-object fixture 还覆盖显式移除 `xl/worksheets/sheet1.xml`：输出省略 worksheet
part 及其 source-owned worksheet `.rels`，移除 worksheet content type override，保留
workbook `.rels` 中的 inbound worksheet relationship，且不修剪 drawing、table、
sharedStrings、styles、VBA、calcChain 或 unknown extension 等 downstream/source parts；
这只是 no-pruning / preservation 审计证据，不是 sheet delete、workbook sheet catalog
sync、relationship repair 或完整 worksheet editing。
同一 worksheet 路径还覆盖 ordinary replace-then-remove：后续 removal 清理 active
worksheet replacement、记录 removed-part 和 worksheet owner `.rels` omission audit，
输出省略 worksheet part 及其 owner `.rels`、移除 worksheet content type override，
但保留 workbook `.rels` 中指向缺失 worksheet 的 inbound worksheet relationship，
以及 drawing/chart/media/table/sharedStrings/styles/VBA/calcChain/unknown
downstream/source parts。这不是 sheet delete、workbook sheet catalog sync、
relationship/content type repair、orphan cleanup、事务式 undo 或 public API。
linked-object fixture 还覆盖显式移除 `xl/drawings/drawing1.xml`：输出省略 drawing
part 及其 source-owned drawing `.rels`，移除 drawing content type override，保留
worksheet `.rels` 中 direct / URI-qualified inbound drawing relationships，且不修剪
chart、media 或其它 downstream parts；这只是 no-pruning / preservation 审计证据，
不是 drawing mutation、object deletion、relationship repair 或完整 drawing 支持。
同一 drawing 路径还覆盖 ordinary replace-then-remove：后续 removal 清理 active
drawing replacement、记录 removed-part 和 drawing owner `.rels` omission audit，
输出省略 drawing part 及其 owner `.rels`、移除 drawing content type override，
但保留 worksheet `.rels` 中 direct / URI-qualified inbound drawing relationships，
以及 chart/media/table/VML/percent-decoded drawing/sharedStrings/styles/VBA/
calcChain/unknown downstream/source parts。这不是 drawing mutation、object deletion、
relationship/content type repair、orphan cleanup、事务式 undo 或 public API。
linked-object fixture 还覆盖显式移除 `xl/media/image1.png`：输出省略 media
entry，保留 PNG default content type 和 drawing `.rels` 中的 inbound image
relationship，不凭空创建 media owner `.rels` omission；这只是 no-pruning /
preservation 审计证据，不是 existing-workbook 图片编辑或关系修复。
linked-object fixture 还覆盖显式移除 `xl/tables/table1.xml`：输出省略 table
entry，移除 table content type override，保留 worksheet `.rels` 中的 inbound
table relationship，不凭空创建 table owner `.rels` omission；这只是 no-pruning /
preservation 审计证据，不是 table resize、relationship repair 或完整 table editing。
linked-object fixture 还覆盖显式移除 `xl/sharedStrings.xml`：输出省略 sharedStrings
part 及其 owner `.rels`，移除 sharedStrings content type override，保留 workbook
`.rels` 中的 inbound sharedStrings relationship；这只是 no-pruning / preservation
审计证据，不是 sharedStrings 索引迁移、字符串表重建、worksheet cell 引用同步、
relationship repair 或 existing-file sharedStrings 语义编辑。
linked-object fixture 还覆盖显式移除 `xl/styles.xml`：输出省略 styles part，
移除 styles content type override，保留 workbook `.rels` 中的 inbound styles
relationship，不凭空创建 styles owner `.rels` omission；这只是 no-pruning /
preservation 审计证据，不是 style id 迁移、样式合并、cell `s` 引用同步、
relationship repair、existing-file style preservation 或完整样式编辑。
linked-object fixture 还覆盖显式移除 `xl/vbaProject.bin`：输出省略 VBA project
part，移除 VBA content type override，保留 workbook `.rels` 中的 inbound VBA
relationship，不凭空创建 VBA owner `.rels` omission；这只是 no-pruning /
preservation 审计证据，不是 macro generation、VBA 语义编辑、签名保真、
relationship repair 或完整宏支持。
linked-object fixture 还覆盖显式移除 `xl/drawings/vmlDrawing1.vml`：输出省略
VML drawing part，移除 VML content type override，保留 worksheet `.rels` 中的
URI-qualified inbound `vmlDrawing` relationship，不凭空创建 VML owner `.rels`
omission；这只是 no-pruning / preservation 审计证据，不是 VML shape editing、
legacy drawing mutation、relationship repair 或完整 VML/drawing 支持。
linked-object fixture 还覆盖显式移除 `xl/drawings/drawing space.xml`：输出省略
percent-decoded drawing part，移除 drawing content type override，保留 worksheet
`.rels` 中原始 `../drawings/drawing%20space.xml` inbound relationship，不凭空创建
`xl/drawings/_rels/drawing space.xml.rels`；这只是 no-pruning / preservation
审计证据，不是 percent-encoded target repair、relationship rewrite、drawing
mutation 或完整 drawing 支持。
removal 回归还覆盖后调用的 `remove_part()` 压过此前 ordinary replacement，清理 stale
replacement state 并以 removed-part audit / content type cleanup 为最终状态；invalid removal
失败覆盖 edit-plan entries/notes、package-entry audit、removed audit、calc policy、
manifest write-mode、aggregate `planned_output()` / legacy output-entry preview
和 copied output bytes 不污染。

### 反向顺序：remove 后再 ordinary replace 恢复

当前还覆盖反向顺序：对源 package 中已有的普通 part，后调用的 ordinary
`replace_part()` 可把此前 `remove_part()` 的目标恢复为 active replacement，
清理 stale removed-part / removed owner `.rels` audit 与 omitted entry 状态，
并把存在的 source-owned `.rels` 重新记录为 copy-original audit；对带 content type
override 的 part，还覆盖恢复后 `[Content_Types].xml` 回到 source bytes /
copy-original audit。linked-object fixture 还覆盖 workbook-specific 反向顺序：
显式移除 `xl/workbook.xml` 后再 ordinary `replace_part()` 会恢复 workbook active
replacement、恢复 source-owned workbook `.rels` copy-original audit、保留 package
`_rels/.rels` inbound officeDocument relationship，并让 `[Content_Types].xml` 回到
source bytes / copy-original audit；同时覆盖 worksheet-specific 反向顺序：
显式移除 `xl/worksheets/sheet1.xml` 后再 ordinary `replace_part()` 会恢复 worksheet
active replacement、恢复 source-owned worksheet `.rels` copy-original audit、保留
workbook `.rels` inbound worksheet relationship，并让 `[Content_Types].xml` 回到
source bytes / copy-original audit；同时覆盖 drawing-specific 反向顺序：
显式移除 `xl/drawings/drawing1.xml` 后再 ordinary `replace_part()` 会恢复 drawing
active replacement、恢复 source-owned drawing `.rels` copy-original audit、保留
worksheet `.rels` 中 direct / URI-qualified inbound drawing relationships，并让
`[Content_Types].xml` 回到 source bytes / copy-original audit。这不是 drawing
mutation、object deletion、事务式 undo、relationship repair、
content type repair、语义合并或 public API。同时覆盖 sharedStrings-specific
反向顺序：显式移除 `xl/sharedStrings.xml` 后再 ordinary `replace_part()` 会恢复
sharedStrings active replacement、恢复 source-owned sharedStrings `.rels`
copy-original audit、保留 workbook `.rels` inbound sharedStrings relationship，
并让 `[Content_Types].xml` 回到 source bytes / copy-original audit。这不是
sharedStrings 索引迁移、字符串表重建、worksheet cell 引用同步、relationship
repair、content type repair、事务式 undo、语义合并或 public API。同时覆盖
styles-specific 反向顺序：显式移除 `xl/styles.xml` 后再 ordinary
`replace_part()` 会恢复 styles active replacement、保留 workbook `.rels` inbound
styles relationship，不凭空创建 styles owner `.rels`，并让 `[Content_Types].xml`
回到 source bytes / copy-original audit。这不是 style id 迁移、样式合并、
cell `s` 引用同步、relationship repair、content type repair、事务式 undo、
语义合并、existing-file style preservation 或 public API。

### media / table / sharedStrings / styles / chart / VBA / VML / percent-decoded 的 ordinary replace + ordering

linked-object fixture 上的 ordinary media replacement 回归还覆盖只重写
`xl/media/image1.png` 时，drawing `.rels`、PNG default content type、workbook、
worksheet、drawing、chart 和 unknown extension entries 仍保持同一 copy-original
基线；这不是图片解码、drawing mutation 或 existing-workbook image editing。
同一路径还覆盖 default-typed media 先显式移除再 ordinary replacement 的反向顺序：
后续 replacement 会恢复 active media part、清理 stale removed-part audit、保留
PNG default content type 且不把 `xl/media/image1.png` 提升成 override、保留
inbound drawing `.rels`，并且不凭空创建 media owner `.rels`。这不是事务式 undo、
图片语义合并、relationship repair、content type repair 或完整 image preservation。
同一路径还覆盖 media 先 ordinary replacement 再显式移除的顺序：后续 removal 会清理
active media replacement、记录 removed-part audit 和 inbound drawing relationship
metadata，输出省略 `xl/media/image1.png`，保留 PNG default content type 且不把
media 提升成 override、保留 inbound drawing `.rels`，并且不凭空创建 media owner
`.rels`。内部 output-plan 快照还暴露 omitted media part、drawing inbound audit、
content types / drawing `.rels` copy-original 和无 media owner `.rels` 条目。这同样
不是事务式 undo、图片语义合并、relationship pruning/repair、content type repair、
existing-workbook image editing 或完整 image preservation。
linked-object fixture 上的 ordinary table replacement 回归还覆盖只重写
`xl/tables/table1.xml` 时，worksheet `.rels`、table content type override、workbook、
worksheet、drawing、chart、media 和 unknown extension entries 仍保持同一
copy-original 基线；这不是 table resize、calculated columns、totals generation
或 existing-workbook table editing。
同一路径还覆盖 table 先显式移除再 ordinary replacement 的反向顺序：后续
replacement 会恢复 active table part、清理 stale removed-part audit、让
`[Content_Types].xml` 回到 source/copy-original audit、保留 worksheet `.rels`
inbound table relationship，且不凭空创建 table owner `.rels`。这不是 table resize、
calculated columns、totals generation、事务式 undo、relationship repair、
content type repair 或 existing-workbook table editing。
同一路径还覆盖 table 先 ordinary replacement 再显式移除的顺序：后续 removal
会清理 active table replacement、记录 removed-part audit 和 inbound worksheet
relationship metadata、输出省略 table part、移除 table content type override、保留
worksheet `.rels` inbound table relationship，且不凭空创建 table owner `.rels`。
内部 output-plan 快照还暴露 omitted table part、worksheet inbound audit、content
types local-DOM rewrite 和无 table owner `.rels` 条目。这不是 table delete
semantics、table resize、calculated columns、totals generation、事务式 undo、
relationship pruning/repair、content type repair 或 existing-workbook table editing。
linked-object fixture 上的 ordinary sharedStrings replacement 回归还覆盖只重写
`xl/sharedStrings.xml` 时，workbook `.rels`、sharedStrings owner `.rels`、
sharedStrings content type override、styles、table、media、VBA 和 unknown
extension entries 仍保持同一 copy-original 基线；这不是 sharedStrings
索引迁移、字符串表重建、worksheet cell 引用同步或 existing-file sharedStrings
语义编辑。
同一路径还覆盖 sharedStrings 先 ordinary replacement 再显式移除的顺序：后续
removal 会清理 active sharedStrings replacement、记录 removed-part audit、输出省略
`xl/sharedStrings.xml` 及其 source-owned owner `.rels`、移除 sharedStrings
content type override、保留 workbook `.rels` 中的 inbound sharedStrings relationship。
它不会修剪 worksheet `t="s"` 引用或重建字符串表。这不是 sharedStrings 索引迁移、
字符串表重建、worksheet cell 引用同步、事务式 undo、relationship pruning/repair、
content type repair、existing-file sharedStrings 语义编辑或 public API。
linked-object fixture 上的 ordinary styles replacement 回归还覆盖只重写
`xl/styles.xml` 时，workbook `.rels`、styles content type override、sharedStrings、
sharedStrings owner `.rels`、table、media、VBA 和 unknown extension entries 仍保持
同一 copy-original 基线，且不会凭空创建 `xl/_rels/styles.xml.rels`；这不是
style id 迁移、样式合并、cell `s` 引用同步、existing-file style preservation
或完整样式编辑。
同一路径还覆盖 styles 先 ordinary replacement 再显式移除的顺序：后续 removal
会清理 active styles replacement、记录 removed-part audit、输出省略 `xl/styles.xml`、
移除 styles content type override、保留 workbook `.rels` 中的 inbound styles
relationship，且不凭空创建 `xl/_rels/styles.xml.rels`。它不会迁移 style id
或重写 cell `s` 引用。这不是 style id 迁移、样式合并、existing-file style
preservation、事务式 undo、relationship pruning/repair、content type repair、
完整样式编辑或 public API。
同一路径还覆盖 chart 先显式移除再 ordinary replacement 的反向顺序：后续
replacement 会恢复 active chart part、清理 stale removed-part audit、让
`[Content_Types].xml` 回到 source/copy-original audit、保留 drawing `.rels` 中的
direct / URI-qualified inbound chart relationships、保留其它 linked/unknown source
entries，且不会凭空创建 chart owner `.rels`；这不是 chart semantic merge、
chart reference repair、relationship repair、content type repair、事务式 undo、
existing-workbook chart editing 或 public API。
内部 `planned_output()` 快照还覆盖该 restore 状态：暴露 active chart
`StreamRewrite` entry、content types copy-original audit、preserved inbound drawing
`.rels`、preserved linked/unknown entries、empty removed_parts 与
removed_package_entries，且不凭空创建 chart owner `.rels`；这仍只是 Patch audit，
不是 public output planner 或 chart editing API。
linked-object fixture 上的 ordinary chart replacement 回归还覆盖只重写
`xl/charts/chart1.xml` 时，drawing `.rels` 中的 chart / URI-qualified chart
relationships、chart content type override、media、table、sharedStrings、styles、VBA
和 unknown extension entries 仍保持同一 copy-original 基线，且不会凭空创建 chart
owner `.rels`；这不是 chart reference migration、series/cache update、drawing
mutation、existing-workbook chart editing 或完整图表支持。
同一路径还覆盖 chart 先 ordinary replacement 再显式移除的顺序：后续 removal
会清理 active chart replacement、记录 removed-part audit 和 direct / URI-qualified
inbound drawing relationship metadata、输出省略 `xl/charts/chart1.xml`、移除 chart
content type override、保留 inbound drawing `.rels` 和其它 linked/unknown source
entries，且不会凭空创建 chart owner `.rels`；这不是 chart delete semantics、
chart reference repair、relationship pruning/repair、content type repair、事务式 undo、
语义合并、existing-workbook chart editing 或 public API。
内部 `planned_output()` 快照还覆盖该 final-removal 状态：暴露 omitted chart
part、removed_parts target/reason/inbound audit、drawing-owned direct /
URI-qualified inbound relationship metadata、content types rewrite、empty
removed_package_entries，且不凭空创建 chart owner `.rels`；这仍只是 Patch audit，
不是 public output planner、chart editing API、relationship repair 或 transactional
undo。
linked-object fixture 上的 ordinary VBA project replacement 回归还覆盖只重写
`xl/vbaProject.bin` 时，workbook `.rels`、VBA content type override、worksheet、
drawing、chart、media、table、sharedStrings、styles、calcChain 和 unknown extension
entries 仍保持同一 copy-original 基线，且不会凭空创建
`xl/_rels/vbaProject.bin.rels`；这不是 macro generation、VBA 语义编辑、
signature preservation、workbook relationship repair 或完整宏支持。
同一路径还覆盖 VBA project 先显式移除再 ordinary replacement 的反向顺序：
后续 replacement 会恢复 active VBA project part、清理 stale removed-part audit、
让 `[Content_Types].xml` 回到 source/copy-original audit、保留 workbook `.rels`
中的 inbound VBA relationship，且不凭空创建 `xl/_rels/vbaProject.bin.rels`；
这不是 macro generation、VBA 语义编辑、签名保真、事务式 undo、workbook
relationship repair、content type repair 或完整宏支持。
内部 `planned_output()` 快照还覆盖该 restore 状态：暴露 active VBA project
`StreamRewrite` entry、content types copy-original audit、preserved package/workbook
relationships、preserved worksheet/drawing/chart/media/table/sharedStrings/styles/
calcChain/unknown entries、empty removed_parts 与 removed_package_entries，
且不凭空创建 `xl/_rels/vbaProject.bin.rels`；这只是 Patch audit，不是 public
output planner、macro editing API、relationship repair、content type repair 或
transactional undo。
同一路径还覆盖 VBA project 先 ordinary replacement 再显式移除的顺序：
后续 removal 会清理 active VBA replacement、记录 removed-part audit、输出省略
VBA project part、移除 VBA content type override、保留 workbook `.rels` 中的
inbound VBA relationship，且不凭空创建 `xl/_rels/vbaProject.bin.rels`；
这不是 macro generation、VBA 语义编辑、签名保真、事务式 undo、workbook
relationship repair、content type repair 或完整宏支持。
内部 `planned_output()` 快照还覆盖该 final-removal 状态：暴露 omitted VBA project
part、removed_parts target/reason/inbound audit、workbook inbound VBA relationship
metadata、content types rewrite、empty removed_package_entries，且不凭空创建
`xl/_rels/vbaProject.bin.rels`；这只是 Patch audit，不是 public output planner、
macro editing API、relationship repair、content type repair 或 transactional undo。
linked-object fixture 上的 ordinary VML drawing replacement 回归还覆盖只重写
`xl/drawings/vmlDrawing1.vml` 时，worksheet `.rels` 中的 URI-qualified
`vmlDrawing` relationship、VML content type override、workbook、worksheet、
drawing、chart、media、table、sharedStrings、styles、VBA、calcChain 和 unknown
extension entries 仍保持同一 copy-original 基线，且不会凭空创建
`xl/drawings/_rels/vmlDrawing1.vml.rels`；这不是 VML shape editing、legacy drawing
mutation、relationship repair 或完整 VML/drawing 支持。
同一路径还覆盖 VML drawing 先显式移除再 ordinary replacement 的反向顺序：后续
replacement 会恢复 active VML drawing part、清理 stale removed-part audit、让
`[Content_Types].xml` 回到 source/copy-original audit、保留 worksheet `.rels`
中的 URI-qualified inbound `vmlDrawing` relationship，且不凭空创建 VML owner
`.rels`。内部 `planned_output()` 快照也覆盖该 restore 状态：暴露 active
VML drawing `LocalDomRewrite` entry、content types copy-original audit、
preserved package/workbook/worksheet/drawing relationships、preserved
linked/unknown entries、empty removed_parts 与 removed_package_entries，
且不凭空创建 VML owner `.rels`；这不是 public output
planner、VML shape editing、legacy drawing mutation、事务式 undo、relationship
repair、content type repair 或完整 VML/drawing 支持。
同一路径还覆盖 VML drawing 先 ordinary replacement 再显式移除的顺序：后续
removal 会清理 active VML drawing replacement、记录 removed-part audit、输出省略
VML drawing part、移除 VML content type override、保留 worksheet `.rels` 中的
URI-qualified inbound `vmlDrawing` relationship，且不凭空创建 VML owner `.rels`；
这不是 VML shape editing、legacy drawing mutation、事务式 undo、relationship
pruning/repair、content type repair 或完整 VML/drawing 支持。
内部 `planned_output()` 快照还覆盖该 final-removal 状态：暴露 omitted VML
drawing part、removed_parts target/reason/inbound audit、URI-qualified worksheet
inbound relationship metadata、content types rewrite、empty removed_package_entries，
且不凭空创建 VML owner `.rels`；这仍只是 Patch audit，不是 public output
planner、drawing editing API、relationship repair 或 transactional undo。
linked-object fixture 上的 ordinary percent-decoded drawing replacement 回归还覆盖只重写
`xl/drawings/drawing space.xml` 时，worksheet `.rels` 中原始
`../drawings/drawing%20space.xml` relationship、drawing content type override、workbook、
worksheet、drawing、chart、media、table、VML、sharedStrings、styles、VBA、calcChain 和
unknown extension entries 仍保持同一 copy-original 基线，且不会凭空创建
`xl/drawings/_rels/drawing space.xml.rels`；这不是 percent-encoded target repair、
relationship rewrite、drawing mutation 或完整 drawing 支持。
同一路径还覆盖 percent-decoded drawing 先显式移除再 ordinary replacement 的
反向顺序：后续 replacement 会恢复 active decoded drawing part、清理 stale
removed-part audit、让 `[Content_Types].xml` 回到 source/copy-original audit、
保留 worksheet `.rels` 中原始 encoded inbound
`../drawings/drawing%20space.xml` relationship，且不凭空创建
`xl/drawings/_rels/drawing space.xml.rels`；内部 `planned_output()` 也覆盖该
restore 状态，暴露 active decoded drawing `LocalDomRewrite` entry、content types
copy-original audit、preserved package/workbook/worksheet/drawing relationships、
preserved linked/unknown entries 和 no invented owner `.rels`。这不是 public output
planner、percent-encoded target repair、relationship rewrite/repair、drawing mutation、
事务式 undo、content type repair 或完整 drawing 支持。
同一路径还覆盖 percent-decoded drawing 先 ordinary replacement 再显式移除的
顺序：后续 removal 会清理 active decoded drawing replacement、记录 removed-part
audit、输出省略 decoded drawing part、移除 drawing content type override、保留
worksheet `.rels` 中原始 encoded inbound `../drawings/drawing%20space.xml`
relationship，且不凭空创建 `xl/drawings/_rels/drawing space.xml.rels`；内部
`planned_output()` 也覆盖该 final-removal 状态，暴露 omitted decoded drawing part、
removed_parts target/reason/inbound audit、encoded inbound worksheet relationship
metadata、content types rewrite、empty removed_package_entries 和 no invented owner
`.rels`；这不是
percent-encoded target repair、relationship rewrite、drawing mutation、事务式 undo、
relationship pruning/repair、content type repair 或完整 drawing 支持。

