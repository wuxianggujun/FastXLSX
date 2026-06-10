# 编辑模型

## 目标

FastXLSX 需要同时满足三个场景：

1. 高性能创建新 XLSX。
2. 编辑已有 XLSX，同时尽量保留原文件结构。
3. 小文件复杂随机编辑，提供接近普通 workbook 对象模型的体验。

因此编辑模型不能是纯内存 workbook，也不能是完全无状态流。正确模型是共享
OpenXML / OPC 底座上的三条路径：Streaming、Patch、In-memory。

执行层任务必须按 [任务拆分设计](TASK_BREAKDOWN.md) 选择最小子任务。当前顺序是
先完成 `P4.0 API surface unification`，再推进窄 Patch MVP；不要把完整 existing-file
editing、public `WorkbookEditor`、public `PackageEditor` 或 `CellValue` 当作已经实现。

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
worksheet XML rewrite，而不是大文件低内存 streaming rewrite。当前结构测试还验证输出包中保留的 worksheet `.rels` legacyDrawing
`rId7` 到 `../drawings/vmlDrawing1.vml#shape1` 可由 `PackageReader` /
`RelationshipGraph` 重读；当前还覆盖 worksheet-owned background picture part
与 header/footer VML drawing part preservation：`sheetData` 局部替换保留
`<picture>` / `<legacyDrawingHF>` 引用、worksheet `.rels` 中的 `image` /
`vmlDrawing` relationships、`xl/media/background.png` bytes、
`xl/drawings/vmlDrawingHF1.vml` bytes、PNG content type default 和 VML content
type override，并在 `EditPlan` / planned output 中把这些 part 暴露为
relationship-derived copy-original entries；这只是 Patch preservation / audit
可见性，不是图片/VML/header-footer 语义编辑、relationship repair/pruning、
orphan cleanup、content type repair、public API 或完整 object preservation；
当前还覆盖 worksheet-owned printerSettings opaque part preservation：`sheetData`
局部替换保留 `<pageSetup r:id>` 引用、worksheet `.rels` 中的
`printerSettings` relationship、`xl/printerSettings/printerSettings1.bin` bytes 和
content type override，并在 `EditPlan` / planned output 中把该 part 暴露为
relationship-derived copy-original entry；这仍只是 Patch preservation / audit
可见性，不是打印设置语义编辑、relationship repair/pruning、orphan cleanup、
content type repair、public API 或完整 object lifecycle 支持；
当前还覆盖同一 fixture 的显式 removal audit：移除 `xl/media/background.png`
会输出省略目标 media part、保留 PNG default 且不提升为 override，并保留
worksheet `.rels` 中指向缺失 background picture 的 inbound relationship；移除
`xl/drawings/vmlDrawingHF1.vml` 会输出省略目标 VML part、移除 VML content type
override，并保留 worksheet `.rels` 中指向缺失 header/footer VML 的 inbound
relationship；两条路径都会在 `EditPlan` / planned output 暴露结构化
removed-part inbound relationship audit。这仍只是 Patch audit / no-pruning
可见性，不是图片/VML/header-footer 删除语义、relationship repair/pruning、
orphan cleanup、content type repair、public API 或完整 object lifecycle 支持；
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
当前还覆盖 worksheet-owned registered OLE opaque
part 与 control-property part preservation：`sheetData` 局部替换保留
`<oleObjects>` / `<controls>` 引用、worksheet `.rels` 中的 `oleObject` /
`control` relationships、`xl/embeddings/oleObject1.bin` bytes、
`xl/ctrlProps/control1.xml` bytes 和对应 content type overrides，并在
`EditPlan` / planned output 中把这些 part 暴露为 relationship-derived
copy-original entries；这只是 Patch preservation / audit 可见性，不是 OLE /
ActiveX / control 语义编辑、relationship repair/pruning、orphan cleanup、
content type repair、public API 或完整 object preservation；
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
另有 registered comments part 小 fixture 证明 worksheet rewrite 会把
`xl/comments/comment1.xml` 和 source worksheet `.rels` 作为 copy-original
preservation 处理，保留 comments content type override，并可由 `PackageReader` /
`RelationshipGraph` 重读。
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
同一 custom XML 小 fixture 还覆盖显式移除 `customXml/item1.xml`：输出省略
custom XML item 及其 source-owned owner `.rels`、保留 package `_rels/.rels`
customXml inbound relationship、保留 `customXml/itemProps1.xml`、custom XML
properties content type override、默认 XML content type 和 unknown entry，且不重写
`[Content_Types].xml`；这不是 custom XML 删除语义、schema/data binding、
relationship pruning/repair、content type repair、orphan cleanup 或 public API。
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
另有 no-op `save_as()` roundtrip 覆盖 linked-object fixture 的全部源 entry
顺序、stored entry method / CRC / uncompressed size 和 bytes 保持一致，并确认初始
`EditPlan` 只包含 copy-original part entries、没有 metadata package-entry side
effect；这只是未修改包 copy baseline。
内部 `PackageEditor::planned_output()` 现在暴露与 `save_as()` 共用的聚合
output-plan snapshot，包括 entry-level 决策顺序、全局
`full_calculation_on_load` / `calc_chain_action`、audit notes、结构化
`removed_parts` / `removed_package_entries`，以及结构化
`relationship_target_audits` / `worksheet_relationship_reference_audits`；
兼容入口 `planned_output_entries()` 仍返回同一份
entry list。entry 决策包括 `source_entry` / `generated` flags、`package_part` /
`part_name` classification、write mode、copied-from-source / omitted flags、
package-entry audit kind、owner part、relationship-derived owner/id/type/target、
omitted removed-part inbound relationship audit 和 reason；当前回归覆盖 no-op
copy-original、docProps generated-small-XML 新增/重写、worksheet calcChain omission
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
- public `Cell` 可以作为输入/返回值，但内部应使用紧凑 cell store、字符串/公式池和
  style id 引用，不能把每个单元格都长期保存为 owning `Cell` 对象。
- 必须有 `max_cells`、内存预算、cell 计数或估算内存这类 guardrail；超过边界时应
  引导用户使用 Streaming 或 Patch。

## 建议 API 形态

```cpp
auto wb = fastxlsx::Workbook::create("out.xlsx");
auto ws = wb.addSheet("Data");

ws.writeRows(rows);        // 流式主路径
wb.save();
```

下面是规划中的 Patch public API 形态伪代码，不是当前已暴露的
`include/fastxlsx` API；当前 `PackageEditor` 仍是 internal test-only 基础。

```cpp
auto editor = fastxlsx::PackageEditor::open("template.xlsx");
editor.replaceSheet("Data", rows);       // 流式替换
editor.setDocumentProperty("Author", "FastXLSX");
editor.saveAs("output.xlsx");
```

```cpp
auto book = fastxlsx::Workbook::open("small.xlsx");
auto& sheet = book.sheet("Data");
sheet.setCell(10, 3, fastxlsx::Cell::text("hello")); // 小文件 in-memory 随机编辑
book.saveAs("output.xlsx");
```

## 关键原则

不要为了编辑方便，让所有路径都变成 DOM。

也不要为了纯流式洁癖，把小型 XML 修改写得过度复杂。

FastXLSX 的边界是：

```text
大 part 流式，小 part 可 DOM，未知 part 原样保留，编辑前先分析联动 part。
```

编辑能力不是流式 writer 的附属补丁。`PackageEditor`、`WorksheetRewriter`、
小文件 `Workbook::open()` 和 EditPlan / dependency analysis 都应作为核心架构能力推进。
