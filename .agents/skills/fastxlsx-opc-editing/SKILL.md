---
name: fastxlsx-opc-editing
description: "处理或规划 FastXLSX 已有文件编辑、OPC package、relationships、part index、part-level rewrite、EditPlan/DependencyAnalyzer、sheet 联动、小型 XML part 局部 DOM、模板填充和未知 part 保留边界。用于审查当前内部 OPC manifest/relationships、PartIndex、RelationshipGraph 基础，以及 PackageReader、PackageEditor、生产 PackageWriter、TemplateEditor 等计划行为；不要据此宣称完整图片/VBA/table 支持。"
---

# FastXLSX OPC Editing

## 必读文件

- `docs/EDITING_MODEL.md`
- `docs/ARCHITECTURE.md`
- `docs/TECHNICAL_COMPARISON.md`
- `docs/ROADMAP.md`
- `README.md`

引用实现类前，先检查 `include/` 和 `src/`。当前已有内部 `PartName`、
`RelationshipSet`、`ContentTypesManifest`、`PackageManifest`、`PartWriteMode`、
`ContentTypeRegistry`、`PartIndex`、`RelationshipGraph`、`PackagePart`
edit-state metadata、最小 workbook manifest 构建和 content types /
relationships XML serializer 基础。当前最小新建 workbook 包含基础可配置
`docProps/core.xml` 和 `docProps/app.xml` 小型 XML builder；`DocumentProperties`
只覆盖 public new-workbook core/app metadata；内部 `PackageEditor` 可复用这些
builder 做 core/app docProps generated-small-XML Patch 小切片，但这不代表
`docProps/custom.xml` 创建/编辑、完整 document properties API 或 public 已有文件编辑。
当前内部回归只证明该 Patch 小切片在重写 package relationships / content types 时
会保留已有 `docProps/custom.xml`、custom-properties package relationship、custom
properties content type override 和 unknown bytes。
完整 worksheet object / metadata editing 仍是 planned / not yet public，以
`docs/CURRENT_CAPABILITIES.md` 为准。

当前新建 workbook streaming 路径已有 external-only hyperlink worksheet relationships
输出：`WorksheetWriter::add_external_hyperlink()` 会为有链接的 sheet 生成
`xl/worksheets/_rels/sheetN.xml.rels`，并与 worksheet `<hyperlinks>` 中的 `r:id`
保持一致。这只是 new-workbook feature wiring，不是 existing-file relationship
editing，也不证明 package preservation。

当前新建 workbook streaming 路径也已有 streaming-only table relationship 输出：
`WorksheetWriter::add_table()` 会生成 `xl/tables/tableN.xml`、worksheet
`<tableParts>`、worksheet `.rels` 和 table content type override。这同样只是
new-workbook feature wiring，不是 existing-file table editing 或完整 object
support；当前 existing-file table 只在内部 `PackageEditor` worksheet replacement
fixture 中作为 copy-original table part preservation 被验证。

当前新建 workbook streaming 路径已有 number-format styles 输出：
`WorkbookWriter::add_style()` 注册非默认 style 后，minimal workbook manifest 会包含
generated small XML part `/xl/styles.xml`、styles content type override，以及从
`/xl/workbook.xml` 指向 `styles.xml` 的 workbook relationship。styles relationship
不属于 worksheet owner，不创建 worksheet `.rels`。这只是 new-workbook number-format
style wiring，不是 existing-file style reading、style id migration、unknown style
extension preservation 或完整 formatting support。

本轮 OPC edit plan 只能写为基础或计划：当前有 copy-original、
generate-small-XML、stream-rewrite、local-DOM-rewrite 的 write-mode metadata，
以及显式 registered-part removal 的 removed-part audit，
内部 `EditPlan`、`DependencyAnalyzer`、`ReferencePolicy` 和 `PartRewritePlanner`
计划基础，并且有新建 workbook 输出使用的内部 `src/package_writer.*` boundary；
当前另有内部 `src/package_reader.*` ZIP entry reader 基础，可索引和读取
stored/no-compression entries；`FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建还能通过
minizip-ng 读取 DEFLATE entries，默认构建仍拒绝 compressed input。读取 entry
时会校验解压后 payload CRC，拒绝 local header
CRC/method/name/size mismatch、encrypted flags、data descriptor entries、Zip64、
非法 ZIP entry name（绝对路径、尾部斜杠、反斜杠、query/fragment components、空段、dot 段或 parent 段）
和损坏 metadata 或 payload bytes，也会拒绝 owner part 缺失的 source-owned
`.rels`，包括根级
`_rels/foo.xml.rels` owner relationship entry，并从 `[Content_Types].xml` / `.rels` 建立内部
`PartIndex` / `RelationshipGraph` 视图；冲突 content type default / override 和同一 `.rels`
owner 内重复的 relationship id、以及 `[Content_Types].xml` / `.rels` 第一个真实
XML 元素不是 `Types` / `Relationships` 的 decoy-root metadata、metadata declaration
嵌套在 unsupported child 下的 decoy 会在 ingestion 阶段被拒绝；reader 只 ingest root 的
direct-child `Default` / `Override` / `Relationship` 元素；metadata attributes 必须未命名空间
（namespace declarations 除外），namespaced metadata attribute decoy 会在 `PackageReader::open()`
阶段失败，未命名空间 metadata attributes 不得重复，非 whitespace metadata text 和 start/end tag
QName mismatch 会失败。这只是 reader
校验，不是 content-type 或 relationship repair；当前 reader-only 回归还覆盖 unknown extension
owner `.rels` metadata ingestion 和 `RelationshipGraph` 挂回；内部 `src/package_editor.*` copy/replace
基础可替换一个已存在 part，并把未替换 entries（包括 unknown entries）按原始 bytes
写到新 stored package；当前还支持 core/app docProps generated-small-XML 窄切片，
可生成/替换 `docProps/core.xml` 和 `docProps/app.xml`，包括两者都缺失的输入，并同步 package rels /
content types；当前还支持 worksheet replacement 窄切片，可在替换既有
worksheet part 时省略 stale `xl/calcChain.xml`、更新 `[Content_Types].xml`、
workbook `.rels` 和 workbook `fullCalcOnLoad` metadata，并保留 source content type
defaults/overrides 形态，避免把默认类型媒体 part 无故提升为 override；当前还覆盖
内部 `PackageEditor::replace_worksheet_part_by_name()` 目标定位 helper：通过
`PackageReader` workbook sheet catalog resolver 找到已有 worksheet part；该 resolver
会先验证 package `_rels/.rels` 中存在且仅存在一个 internal `officeDocument`
relationship，当前窄实现只接受解析到 `/xl/workbook.xml` 的 target，缺失、重复、
external、带 query/fragment 或非固定 target 会在 lookup 阶段失败；相对、绝对
和 dot-segment package target（例如 `xl/./workbook.xml`）都从 package root
解析，且不会把 package root 建模成真实
`PartName`；当前 reader/source-catalog by-name Patch 回归还覆盖 workbook-owned 绝对 worksheet
target（例如 `/xl/worksheets/sheetN.xml`）和 dot-segment 相对 target
（例如 `./worksheets/../worksheets/sheetN.xml`）解析到已有 worksheet part，并在
完整 worksheet by-name replacement 与 by-name `sheetData` patch 两条
source-catalog 路径输出后保留 worksheet relationship 的 target 字面值和 unknown
extension bytes；当 calcChain cleanup 需要重写 workbook `.rels` 时，这不是
整份 workbook relationships 字节保真，也不是任意 workbook part-location、
relationship repair、pruning 或 public API。它还要求
sheet relationship id 使用 officeDocument relationships XML namespace（可用非
`r` 前缀，普通 `id` 或错误 namespace 的 `id` 会被拒绝），并且只读取 workbook
`<sheets>` 目录的直接 `<sheet>` 子元素，要求 `name` / `sheetId` 是未命名空间的
workbook sheet 属性，忽略目录外或嵌套在非 sheet 目录子元素下的
同名标签，再委托
同一 `replace_worksheet_part()` 路径处理 calcChain/fullCalcOnLoad 与 preservation
副作用；缺失或重复 sheet name 失败不污染 EditPlan、manifest、package-entry audit、
calc policy 或输出 bytes；invalid package `officeDocument` entrypoint 也会在
by-name Patch 前失败且不污染状态；source workbook catalog 查找还会在 by-name
Patch 状态变更前拒绝 workbook `.rels` 中缺失的 sheet relationship id，以及指向
未注册 worksheet part 的 worksheet relationship，这只是目标定位校验，不是
relationship repair、pruning 或 orphan cleanup。这不是 sheet rename/delete、sheet catalog
mutation、任意 workbook part-location 支持、随机 cell 编辑或 public API。
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
当前还支持内部 `PackageEditor::rename_sheet_catalog_entry()` workbook sheet catalog
name helper：它只重写当前 planned `/xl/workbook.xml` 中直接 `<sheets><sheet
name="...">` 的 `name` attribute，成功后 workbook part 为 `LocalDomRewrite`，
worksheet parts、workbook `.rels`、content types、calcChain 和 unknown entries
保持 copy-original。该 helper 使用 planned workbook catalog；planned workbook XML 路径的内部
`planned_output()` 快照也暴露最终 workbook `LocalDomRewrite`、preserved content types /
workbook `.rels` / worksheet / calcChain / unknown entry，以及 structured sheet catalog /
definedNames audit；坏 planned workbook catalog（planned sheet relationship id 在 workbook
`.rels` 中缺失，或 planned worksheet relationship 指向未注册 worksheet part）会在 rename
状态变更前失败，并保留 queued workbook replacement、EditPlan / audit、manifest、calc policy、
package-entry audit 和输出 bytes；缺失旧名、精确或 ASCII
大小写不敏感重复新名、非法新名、`ReferencePolicyAction::Fail` 遇到直接 `definedNames`、以及 workbook planned
removal 都会失败且不污染 EditPlan、manifest、package-entry audit、calc policy 或输出
bytes。它不更新 definedNames、公式、tables、drawings、charts、hyperlinks、
relationship targets、sharedStrings、styles 或 calcChain，不能写成完整 sheet
rename/add/delete、relationship repair 或 public API。
完整 worksheet replacement payload 现在会在 Patch 状态变更前做最小根元素校验：
replacement XML 必须是单个 `<worksheet>` 根元素（按 local-name 接受前缀形式，
且允许 XML declaration、注释和处理指令位于根元素前），
否则失败不污染 EditPlan、manifest、package-entry audit、calc policy 或 copied output
bytes；任意非 prolog 元素或文本位于根元素前仍会被拒绝；不能写成 XML schema
validation、namespace repair 或 XML repair；
成功的完整 worksheet replacement 还会对 replacement payload 做 audit-only 扫描：
若新 worksheet XML 使用 shared string indexes、style id references、公式 cell，或包含
sheetPr、dimension、sheetViews、sheetFormatPr、cols、sheetProtection、protectedRanges、
autoFilter、mergeCells、scenarios、dataConsolidate、customProperties、cellWatches、smartTags、
webPublishItems、dataValidations、conditionalFormatting、ignoredErrors、
printOptions、pageMargins、pageSetup、extLst 等 range/reference worksheet metadata，
以及 hyperlinks、drawing、legacyDrawing、picture、legacyDrawingHF、`pageSetup r:id`
printerSettings 引用、oleObjects、controls、tableParts 等 relationship-bearing worksheet
metadata，会在 `EditPlan` / planned output notes 和结构化
`WorksheetPayloadDependencyAudit` 中提示 caller 复核 `xl/sharedStrings.xml`、
`xl/styles.xml`、workbook calc metadata、calcChain policy、range/reference metadata、
worksheet `.rels` 和 linked parts；这不迁移 sharedStrings 索引、不合并 styles、
不计算公式、不重建 calcChain，不重算 dimension、不修复 sheetViews / ranges，
也不修复 relationships 或 linked parts；
完整 worksheet replacement 还会把缺失 worksheet `.rels`、缺失 relationship id、
保留但未引用的 relationship id，以及已知 worksheet 元素/type mismatch 记录为
结构化 `WorksheetRelationshipReferenceAudit`，并传播到内部 `EditPlan` /
`PackageEditorOutputPlan`；这仍只是 Patch audit traceability，不做 namespace
validation、relationship pruning、repair 或 linked-part regeneration；
当前 relationship-id audit 只把绑定到 officeDocument relationships namespace 的
`*:id` 当作 OpenXML relationship 引用；接受非 `r` 前缀，忽略普通 `id` 和错误
namespace 的 `x:id`；这仍不是 namespace validation、XML repair、relationship
repair/pruning 或 public API；
当前 relationship-id audit 还把 `<pageSetup r:id="...">` 作为 printerSettings
relationship 引用处理，记录 missing id / type mismatch notes 与结构化审计，但不会合成
worksheet relationship 或 printerSettings part；
当前回归还覆盖源包没有 worksheet `.rels` 时，replacement XML 的 `<drawing r:id="...">`
只生成 `MissingRelationships` 结构化审计，输出包不会凭空创建 worksheet `.rels`，
unknown bytes 仍原样保留；
当前 `ReferencePolicyAction::Fail` 回归还会用包含 shared string indexes、
style id references、公式 cell、sheetPr / dimension / sheetViews / sheetFormatPr /
cols / scenarios / dataConsolidate / customProperties / cellWatches / smartTags /
webPublishItems / printOptions / pageMargins / pageSetup 等 range/reference metadata、
hyperlinks、drawing 和 tableParts 的完整 worksheet replacement payload 触发失败，验证这些 audit-only payload notes /
`WorksheetPayloadDependencyAudit` 不会污染 `EditPlan`、relationship target audit、manifest、calc policy 或输出 bytes；
当前还覆盖内部 `PackageEditor::replace_worksheet_sheet_data()` helper：只替换既有 worksheet XML
中的 `<sheetData>` / `<sheetData/>`，保留同一 worksheet part 外围 XML metadata，
并复用 worksheet replacement 的 calcChain/fullCalcOnLoad 与 preservation 副作用；
成功替换后会在内部 `EditPlan` notes 中审计保留的 worksheet-local metadata
range/reference 需要 caller review，当前覆盖 sheetPr、dimension、sheetViews、
sheetFormatPr、cols、sheetProtection、protectedRanges、autoFilter、mergeCells、
scenarios、dataConsolidate、customProperties、cellWatches、smartTags、webPublishItems、
dataValidations、conditionalFormatting、hyperlinks、ignoredErrors、printOptions、
pageMargins、pageSetup、drawing、legacyDrawing、picture、legacyDrawingHF、
oleObjects、controls、tableParts 和 extLst；
当前 helper 仍会物化当前 planned worksheet XML，因此受内部
`package_editor_sheet_data_local_rewrite_byte_limit` 约束；source/queued worksheet
XML、replacement `<sheetData>` payload 或 rewritten worksheet XML 超过该 bounded local rewrite 限制时，
direct/by-name 路径都会在状态变更前失败，不污染 EditPlan、manifest、
package-entry audit、calc policy、planned output 或 copied output bytes，并保留
unknown extension bytes；成功路径也会在 EditPlan/output-plan note 和 worksheet
part reason 中暴露 bounded local rewrite 边界，不能写成大文件低内存 streaming
worksheet transformer；
当前 targeted-cell public Patch 只通过
`WorkbookEditor::replace_cells(..., CellPatchMissingCellPolicy::Insert)` 暴露 point
upsert；内部 `PackageEditor::replace_or_insert_worksheet_cells*` 只是 transformer /
staged-chunk handoff 细节，不是 public API。当前 transformer 在 target
完成后会跳过 tail replacement / pending-target lookup；strict replace 只在 source
stream 越过最后 target 坐标后进入该 tail fast path，仍线性扫描 source XML。这只能
写成 tail pass-through 热路径优化，不能写成默认索引算法、range metadata repair 或
完整大文件随机编辑。当前已有 internal `WorksheetCellIndex` source-offset index
foundation 和 indexed rewrite planning foundation，可用 compact `{row, column,
range}` 主索引把 source cells 映射到 `<c>` byte ranges，并校验 / 排序有界
target set；旧 `cells()` map 只作为 lazy diagnostic snapshot 存在，不再是
benchmark / rewrite 热路径。transformer actions 也会暴露 source XML offset。
当前还有 internal materialized indexed slicer，可在 worksheet XML
bytes 与 index 完全匹配时按 source ranges 拼接 strict existing-cell replacements。
当前还有 internal `PackageEntryChunk` byte-range emitter，可按 offset/size 切出
memory/file staged chunks，并覆盖跨 chunk 边界 range；当前还已有 internal
PackageEditor chunk-backed indexed strict-replace slicer prototype，可用 staged
chunks + prebuilt index 或 target-only preplanned ranges 拼接 existing-cell
replacement payload；当前
`fastxlsx_bench_package_editor_cell_replacement` 还提供 opt-in
`--rewrite-strategy indexed-staged` schema-v2 benchmark/prototype 路径，专门验证
benchmark 生成的 staged chunks + indexed/preplanned ranges。该 benchmark 现在使用
target-only range planner 流式扫描 source worksheet，只保存请求 target cells 的
`<c>` byte ranges；它们不是 public API、不是当前 public Patch 默认路径、不是
PackageEditor source-entry ZIP seek，也不是完整 indexed worksheet rewrite 能力。
当前结构测试还验证 sheetData patch 输出后，worksheet `.rels` 中保留的
legacyDrawing `rId7` target `../drawings/vmlDrawing1.vml#shape1` 可由
`PackageReader` / `RelationshipGraph` 重读；这仍是 preservation 证据，不是
VML/drawing 编辑；
当前还覆盖 worksheet-owned background picture part 与 header/footer VML drawing
part preservation：`sheetData` 局部替换保留 `<picture>` / `<legacyDrawingHF>`
引用、worksheet `.rels` 中的 `image` / `vmlDrawing` relationships、
`xl/media/background.png` bytes、`xl/drawings/vmlDrawingHF1.vml` bytes、PNG
content type default 和 VML content type override，并在 `EditPlan` / planned
output 中把这些 part 暴露为 relationship-derived copy-original entries；这只是
Patch preservation / audit 可见性，不是图片/VML/header-footer 语义编辑、
relationship repair/pruning、orphan cleanup、content type repair、public API 或
完整 object preservation；
当前还覆盖 worksheet-owned printerSettings opaque part preservation：`sheetData`
局部替换保留 `<pageSetup r:id>` 引用、worksheet `.rels` 中的
`printerSettings` relationship、`xl/printerSettings/printerSettings1.bin` bytes 和
content type override，并在 `EditPlan` / planned output 中把该 part 暴露为
relationship-derived copy-original entry；这只是 Patch preservation / audit
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
当前还覆盖 worksheet-owned registered OLE opaque part 与 control-property part
preservation：`sheetData` 局部替换保留 `<oleObjects>` / `<controls>` 引用、
worksheet `.rels` 中的 `oleObject` / `control` relationships、
`xl/embeddings/oleObject1.bin` bytes、`xl/ctrlProps/control1.xml` bytes 和
对应 content type overrides，并在 `EditPlan` / planned output 中把这些 part
暴露为 relationship-derived copy-original entries；这只是 Patch preservation /
audit 可见性，不是 OLE / ActiveX / control 语义编辑、relationship
repair/pruning、orphan cleanup、content type repair、public API 或完整 object
preservation；
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
当前还覆盖源 worksheet 使用 self-closing `<sheetData/>` 的成功替换回归：
输出改为普通 `<sheetData>...</sheetData>`，保留 dimension / autoFilter，沿默认
calcChain remove / fullCalcOnLoad 路径清理 stale 计算 metadata，并保留 unknown bytes；
当前还覆盖 replacement payload 自身为 self-closing `<sheetData/>` 的成功替换：
可清空旧 row/cell，输出保留 `<sheetData/>` 和外围 dimension / autoFilter，
并继续沿默认 calcChain remove / fullCalcOnLoad 与 unknown bytes preservation 路径；
当前还覆盖 source worksheet 和 replacement payload 使用 `<x:worksheet>` /
`<x:sheetData>` 前缀形式时的成功替换：按 local-name 匹配，输出保留原 wrapper /
replacement 字面前缀，仍沿默认 calcChain cleanup 与 unknown bytes preservation 路径；
不能写成通用 namespace repair；
当前组合回归还验证先排队 worksheet replacement 后再执行 sheetData patch 时，helper
基于当前 planned worksheet bytes 替换，覆盖 queued worksheet 中普通 `<sheetData>` 和
self-closing `<sheetData/>` 两种形态，保留 queued wrapper metadata，不把
source-only worksheet metadata 复活；这仍不是事务式编辑、metadata repair 或 public API；
当前还覆盖一个真实 `WorkbookWriter` source package 的 internal Patch roundtrip：
`PackageReader` 解析 writer workbook sheet catalog，`PackageEditor::replace_worksheet_sheet_data_by_name()`
只替换目标 `<sheetData>`，输出再由 `PackageReader` 重读；回归验证 untouched
worksheet、content types、package relationships、workbook relationships 和 docProps
bytes 保留，并验证 writer 生成的 worksheet XML declaration/prolog 会在 patch 后
按原前缀保留，且输出 `<worksheet>` 根紧随该 prolog 通过最终 worksheet 根校验；
source 使用 writer sharedStrings 策略和简单 style 时，
还验证 `xl/sharedStrings.xml`、`xl/styles.xml`、对应 content type override 和 workbook
relationships 原样保留，并把 replacement `<sheetData>` 里的 shared string index /
style id references 记录为结构化 `WorksheetPayloadDependencyAudit`。workbook XML 设置
`fullCalcOnLoad="1"`，且不会在源包没有 `xl/calcChain.xml` 时凭空创建 calcChain。
这只是 internal Patch MVP / template-fill 证明，不是 public `PackageEditor`、
随机 cell 编辑、sharedStrings 索引迁移、style id 迁移、styles 合并、table/drawing
语义同步或大文件 streaming worksheet transformer；
replacement `<sheetData>` 自身包含 shared string indexes、style id references
或公式 cell 时，也只追加 audit notes 和结构化 `WorksheetPayloadDependencyAudit`，
提示 caller 复核 `xl/sharedStrings.xml`、
`xl/styles.xml` 和 workbook calc metadata / calcChain policy；它不迁移 sharedStrings
索引、不合并 styles、不计算公式、不重建 calcChain；
invalid/malformed replacement XML、source worksheet 缺失 `sheetData`，以及 source worksheet
`<sheetData>` 起始标签存在但闭合标签损坏/缺失时，当前只覆盖失败不污染
EditPlan、manifest、package-entry audit、calc policy 或 copied output bytes；不能写成
XML repair；
这不是 public API、随机 cell 编辑、range 修复、dataValidations/conditionalFormatting/
hyperlinks/table/drawing 语义同步、sharedStrings/styles 迁移或大文件低内存
transformer；当前还覆盖
没有 `xl/calcChain.xml` payload 但残留 calcChain content type override 或 workbook
calcChain relationship 的 metadata-only cleanup，不记录缺失 payload 的 removed-part
audit、不创建 calcChain payload，也不是通用 relationship/content-type repair；
当前还支持内部 `PackageEditor::request_full_calculation()` workbook-only calc metadata
helper：只局部重写 `/xl/workbook.xml` 来设置 `fullCalcOnLoad="1"`，并可按
`CalcChainAction::Remove/Preserve` 清理或保留 calcChain payload、content type、
workbook relationship 和 calcChain owner `.rels` 审计；Remove 路径还覆盖没有
`xl/calcChain.xml` payload 但残留 calcChain content type override 或 workbook
relationship 的 metadata-only cleanup，不创建 payload 或 removed-part audit；
`CalcChainAction::Rebuild`
未实现且失败不污染 edit plan、manifest、package-entry audit 或输出包。它不改
worksheet、linked objects、sharedStrings/styles/tables/drawings/VBA/unknown extension
语义，不是公式求值、calcChain rebuild、public API 或通用 relationship/content-type
repair；DEFLATE 源包回归中，与 workbook calc helper 无直接因果关系的 unknown owner
`.rels` 只通过 `planned_output()` copy-original 可见性和输出重读验证保留，不写成
edit-plan package-entry side-effect audit；该 helper 只更新 workbook 根元素的直接子级 `calcPr`，保留 `extLst`
或 custom extension 内的嵌套同名 decoy；缺少直接子级时才在真实 workbook closing
tag 前插入按根前缀命名的 `calcPr`，这不是 XML schema validation、namespace
repair 或 workbook metadata DOM；如果 ordinary workbook 或 calcChain replacement 已排队，随后调用
workbook-only `request_full_calculation()` 会使用该已排队 workbook XML、保留其中
非 calc metadata、规范 `fullCalcOnLoad="1"`，并清理 calcChain payload/content
type/workbook relationship；此前排队的 calcChain replacement 不会在输出包复活，
也不回退到 source workbook bytes；若同一路径指定 `CalcChainAction::Preserve`，
此前排队的 calcChain replacement 会保持为 active `LocalDomRewrite` 并作为最终
`xl/calcChain.xml` payload 写出，同时只保留 calcChain owner `.rels`
copy-original audit；这仍不是 calcChain rebuild 或公式求值；当前结构测试还证明
普通 part replacement、docProps generated parts 和 worksheet replacement 会把
write-mode / dirty / generated / preserve-original 状态同步到内部 manifest，供 Patch
审计；worksheet replacement 还会把 workbook metadata rewrite 同步为
`LocalDomRewrite`；内部 `EditPlan` 还会记录 `[Content_Types].xml`、package
`_rels/.rels`、workbook `.rels` rewrite、removed calcChain owner `.rels` omission，
以及 preserved source-owned `.rels` 存在时的 copy-original package-entry 审计项；该审计也覆盖
ordinary owner-part replacement 的根级 `_rels/foo.xml.rels`，且
root-level roundtrip 回归验证其可由 `PackageReader` / `RelationshipGraph`
重新挂回 owner part；另有 reader-only 回归验证 unknown extension owner `.rels`
在 editor roundtrip 前即可挂回 owner；`CalcChainAction::Preserve` 下保留的 calcChain owner `.rels`，以及 workbook metadata
rewrite 时被原样保留的 workbook `.rels`；普通 `replace_part()` 会拒绝
`[Content_Types].xml`、package `_rels/.rels` 和 source-owned `.rels` metadata entry
作为 ordinary part replacement target；这些 entry 仍只能通过窄 metadata-aware helper
和 package-entry audit 路径变化，不能据此宣称完整 relationship/content-type mutator；
package-entry audit 现在是内部结构化状态：`ContentTypes` / `PackageRelationships`
不携带 owner part，`SourceRelationships` 必须携带 owner part；kind 与 entry path
必须一一匹配：`ContentTypes` 只能指向 `[Content_Types].xml`，
`PackageRelationships` 只能指向 package `_rels/.rels`，`SourceRelationships`
必须指向由 owner part 推导出的 source-owned `.rels` entry；
metadata-entry replacement 拒绝回归也覆盖 edit plan entries / notes、package-entry
audit、calc policy、manifest write-mode、aggregate `planned_output()` / legacy
output-entry preview 和 copied output bytes 不污染；重复 ordinary
part replacement 回归还覆盖最终 bytes、write mode、edit-plan reason、manifest state
和 preserved source-owned `.rels` audit upsert；内部 `EditPlan` 还覆盖 part-level
set/remove 互斥，restored active part 会清理 stale removed-part audit，已有
relationship-derived entry 改成 rewrite/generate entry 时也会清理 stale relationship metadata；package-entry
set/remove 也有互斥回归，避免同一 metadata entry 同时存在 active 和 removed 审计；docProps generated-small-XML 被后续
ordinary replacement 覆盖时，最终 part bytes、EditPlan 和 manifest 采用 ordinary
replacement，content types / package relationships 仍由 helper 路径维护和审计。反向
顺序回归也覆盖后调用的 docProps metadata helper 接管此前 ordinary replacement 的
core/app part 或 explicit removal，并清理 stale removal / omitted payload 状态；这只恢复
helper 负责的 core/app payload、content types 和 package relationships，不恢复此前
removal 省略的 docProps owner `.rels`；当前结构回归验证输出包继续省略该 owner
`.rels`，并保留 removed package-entry audit，也不是事务式 undo。还有回归覆盖
worksheet replacement 删除 calcChain 时压过此前 ordinary
calcChain replacement，并接管此前 ordinary workbook replacement 以写入 helper-generated
fullCalcOnLoad metadata；若后续 ordinary `replace_part("/xl/workbook.xml", ...)`
发生在 worksheet rewrite 请求 fullCalcOnLoad / calcChain removal 之后，仍会保留该
calc policy、把 `fullCalcOnLoad` 规范为 `1`，并避免把已重写的 workbook `.rels`
package-entry audit 降级为 copy-original。
组合回归还覆盖 docProps generated-small-XML 与 worksheet
replacement 共存时的 relationship/content-type 状态合并、calcChain removal、
stale calcChain owner `.rels` omission、workbook metadata rewrite 和 unknown entry
preservation、exact/path-equivalent source-overwrite rejection 和 empty-output /
missing-parent / non-directory-parent / existing-directory output rejection；core/app docProps package relationship
target 冲突失败也覆盖不污染 edit plan entries / notes、manifest / package-entry audit / copied output；这些拒绝只是 reader-backed copy 输出安全护栏，
现在还覆盖 guard 在 materialize output entries 前失败，且失败后 queued part replacement、structured audit snapshots、calc policy 和 removal audits 仍保留在 `EditPlan` / manifest /
planned output 中，后续安全 `save_as()` 仍输出 queued rewrite 并保留 untouched
worksheet / unknown bytes；同一 guard 也覆盖 queued worksheet replacement 的
`fullCalcOnLoad` / calcChain removal / package-entry audit / planned output 状态，
后续安全输出仍按计划省略 calcChain 并保留 unknown bytes；不是 atomic in-place editor；malformed workbook metadata / workbook calc metadata
preflight failure 和 invalid replacement failure 只覆盖 edit plan entries / notes、
structured payload/removal/calc-policy snapshots、aggregate
`planned_output()` / legacy output-entry preview、manifest write-mode
和 copied output bytes 不污染；no-op `PackageEditor::save_as()` roundtrip 还验证
linked-object fixture 全部源 entries 的顺序、stored entry method / CRC / uncompressed
size 和 bytes 保持一致、初始 `EditPlan` 只有 copy-original part entries 且没有
metadata package-entry side effect；这只是未修改包 copy baseline；ordinary
`PackageEditor::planned_output()` 现在暴露与 `save_as()` 共用的聚合
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
request-recalculation preserve-policy output snapshot、显式 workbook removal 的
owner `.rels` omission、drawing replace-then-remove final-removal 的 omitted
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
inbound worksheet relationship audit / content types rewrite 快照、pivot worksheet
rewrite 的 fullCalcOnLoad / `CalcChainAction::Remove` / worksheet `StreamRewrite` /
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
remove-then-replace 的 active VBA project stream-rewrite / content types copy-original /
preserved package/workbook relationships / preserved linked/unknown entries /
empty removed_parts 与 removed_package_entries / no invented owner `.rels` 快照、VBA project
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
的 removed-part inbound audit；
metadata entries 如 `[Content_Types].xml`、package `_rels/.rels` 和 source-owned `.rels`
不会暴露为 package part；这只是内部 Patch 审计可见性，不是 public editor API
或通用 metadata mutator；malformed unrelated relationship target 的 explicit removal
回归现在验证 EditPlan / planned output notes-only audit、omitted target、
copy-original metadata entries、无结构化 relationship target / worksheet reference
audit、无 package-entry rewrite/omission，以及 calc policy 保持不变；这不是
relationship repair；ordinary
single-part replacement 回归还验证目标 entry 原位重写时，其它源 entries
保持 entry 顺序、stored entry method / CRC / uncompressed size 和 bytes；这只是窄
part-level rewrite copy-original 证据；linked-object fixture 上的 ordinary workbook
replacement 回归还验证只重写 `xl/workbook.xml` 时，workbook `.rels` 被记录为
copy-original package-entry audit，worksheet、drawing、media、sharedStrings、styles、
VBA、calcChain 和 unknown extension entries 仍保持 copy-original 基线；如果源包是
DEFLATE input，当前只承诺未修改 part 的解压后 payload 语义保留；minizip-enabled
PackageEditor 回归还覆盖 DEFLATE source 上 ordinary workbook replacement、
unknown extension target replacement、workbook calc metadata helper，以及 worksheet
replacement 下的 calcChain cleanup、linked payload preservation 和
unknown extension owner `.rels` 由输出 `PackageReader` / `RelationshipGraph` 重读；
不承诺保留源 ZIP compression method、timestamps、extra fields 或压缩字节；
linked-object fixture 上的 ordinary drawing replacement 回归还验证只重写
`xl/drawings/drawing1.xml` 时，drawing `.rels` 被记录为 copy-original package-entry
audit，chart、media 和 unknown extension entries 仍保持同一 copy-original 基线；
这不是 drawing mutation、图片编辑或图表编辑；linked-object fixture 上的 ordinary
unknown extension replacement 回归还验证只重写 `custom/opaque-extension.bin` 时，
其 owner `.rels` 被记录为 copy-original package-entry audit 并原样保留，workbook、
worksheet、drawing、chart 和 media entries 仍保持同一 copy-original 基线；这不是
unknown extension 语义编辑、custom relationship repair 或 public API；linked-object fixture
上同一 unknown extension 的 repeated ordinary replacement 回归还验证最终 bytes、
manifest write-mode、edit-plan reason 和 owner `.rels` audit 会 upsert 到最后一次
替换状态，owner `.rels` 仍保持 copy-original，且不会产生 removed-part 或 removed
package-entry audit；这不是事务式编辑或 unknown extension 语义合并；linked-object fixture
上同一路径还覆盖 unknown extension 先显式移除再 ordinary replacement 的反向顺序：
后续 replacement 会恢复 active unknown extension part、清理 stale removed-part audit
和 stale removed owner `.rels` audit、恢复 owner `.rels` copy-original package-entry
audit、保留 worksheet `.rels` 中的 inbound unknown relationship、保留其它
linked/source entries，且不重写 `[Content_Types].xml`；这不是 unknown extension
语义合并、custom relationship repair、metadata repair、事务式 undo 或 public API；linked-object fixture
上同一路径现在还覆盖 unknown extension 先 ordinary replacement 再显式移除：
后续 removal 会清理 active replacement、记录 removed-part 和 removed owner `.rels`
audit、输出省略 unknown extension part 及其 owner `.rels`、保留 worksheet `.rels`
中指向缺失 part 的 inbound unknown relationship、保留其它 linked/source entries 和默认
`bin` content type，且不重写 `[Content_Types].xml`；这不是 unknown extension
删除语义、custom relationship repair、metadata repair、relationship pruning/repair、
content type repair、orphan cleanup、事务式 undo 或 public API；linked-object fixture
上当前内部 `PackageEditor::remove_part()` 还覆盖显式 registered-part removal 窄切片：
只接受源 package 中已有的普通 part，输出时省略目标 part 和存在时的 source-owned
owner `.rels`，记录 removed-part / removed package-entry audit，并在目标存在 content
type override 时重写 `[Content_Types].xml`；它不会修剪其它 part 指向该目标的 inbound
relationships，不是 object deletion、relationship pruning 或事务式编辑；removed-part
audit 现在会同时保留结构化 inbound relationship metadata（owner entry、owner
part、id、type、raw target、normalized target part）和可读 reason，记录仍指向
被删除 part 的 inbound package/source relationship 上下文，方便 Patch traceability，
但不代表修复；若 removed-part inbound 扫描遇到 malformed percent relationship
target，会记录 EditPlan / planned output audit note 并继续保留该 `.rels`
bytes，不让无关坏 target 阻塞显式 part removal；planned output 也暴露
removed target omission 与 copy-original metadata entries，且不新增结构化
relationship target audit；这不是 target repair 或 relationship validation；linked-object fixture
上的显式 workbook removal 回归还覆盖 `xl/workbook.xml`：输出省略 workbook part
及其 source-owned workbook `.rels`，移除 workbook content type override，保留
package `_rels/.rels` 中的 inbound officeDocument relationship，且不修剪 worksheet、
drawing、table、sharedStrings、styles、VBA、calcChain 或 unknown extension 等
downstream/source parts；这只是 no-pruning / preservation 审计证据，不是 workbook
deletion、sheet catalog sync、relationship repair 或完整 workbook editing；同一路径还覆盖
ordinary workbook replace-then-remove：后续 removal 清理 active workbook replacement、
记录 removed-part 和 workbook owner `.rels` omission audit、输出省略 workbook part
及 owner `.rels`、移除 workbook content type override，但保留 package
`_rels/.rels` 中指向缺失 workbook 的 officeDocument relationship 和下游/source
parts；这不是 workbook deletion semantics、sheet catalog sync、relationship/content
type repair、orphan cleanup、事务式 undo 或 public API；linked-object fixture
上的显式 worksheet removal 回归还覆盖 `xl/worksheets/sheet1.xml`：输出省略 worksheet
part 及其 source-owned worksheet `.rels`，移除 worksheet content type override，保留
workbook `.rels` 中的 inbound worksheet relationship，且不修剪 drawing、table、
sharedStrings、styles、VBA、calcChain 或 unknown extension 等 downstream/source
parts；这只是 no-pruning / preservation 审计证据，不是 sheet delete、workbook
sheet catalog sync、relationship repair 或完整 worksheet editing；linked-object fixture
上的 worksheet ordinary replace-then-remove 回归还覆盖后续 removal 清理 active
worksheet replacement、记录 removed-part 和 worksheet owner `.rels` omission audit、
输出省略 worksheet part 及其 owner `.rels`、移除 worksheet content type override，
但保留 workbook `.rels` 中指向缺失 worksheet 的 inbound worksheet relationship
以及 drawing/chart/media/table/sharedStrings/styles/VBA/calcChain/unknown
downstream/source parts；这不是 sheet delete、workbook sheet catalog sync、
relationship/content type repair、orphan cleanup、事务式 undo 或 public API；linked-object fixture
上的显式 drawing removal 回归还覆盖 `xl/drawings/drawing1.xml`：输出省略 drawing
part 及其 source-owned drawing `.rels`，移除 drawing content type override，保留
worksheet `.rels` 中 direct / URI-qualified inbound drawing relationships，且不修剪
chart、media 或其它 downstream parts；这只是 no-pruning / preservation 审计证据，
不是 drawing mutation、object deletion、relationship repair 或完整 drawing 支持；linked-object fixture
上的 drawing ordinary replace-then-remove 回归还覆盖后续 removal 清理 active
drawing replacement、记录 removed-part 和 drawing owner `.rels` omission audit、
输出省略 drawing part 及其 owner `.rels`、移除 drawing content type override，
但保留 worksheet `.rels` 中 direct / URI-qualified inbound drawing relationships
以及 chart/media/table/VML/percent-decoded drawing/sharedStrings/styles/VBA/
calcChain/unknown downstream/source parts；这不是 drawing mutation、object deletion、
relationship/content type repair、orphan cleanup、事务式 undo 或 public API；linked-object fixture
上的显式 media removal 回归还覆盖 default-typed `xl/media/image1.png`：输出省略 media
entry，保留 PNG default content type 和 drawing `.rels` 中的 inbound image
relationship，不凭空创建 media owner `.rels` omission；这只是 no-pruning /
preservation 审计证据，不是 existing-workbook 图片编辑或关系修复；linked-object fixture
上的显式 table removal 回归还覆盖 `xl/tables/table1.xml`：输出省略 table entry，
移除 table content type override，保留 worksheet `.rels` 中的 inbound table
relationship，不凭空创建 table owner `.rels` omission；这只是 no-pruning /
preservation 审计证据，不是 table resize、relationship repair 或完整 table editing；linked-object fixture
上的显式 sharedStrings removal 回归还覆盖 `xl/sharedStrings.xml`：输出省略
sharedStrings part 及其 owner `.rels`，移除 sharedStrings content type override，
保留 workbook `.rels` 中的 inbound sharedStrings relationship；这只是 no-pruning /
preservation 审计证据，不是 sharedStrings 索引迁移、字符串表重建、worksheet cell
引用同步、relationship repair 或 existing-file sharedStrings 语义编辑；linked-object fixture
上的显式 styles removal 回归还覆盖 `xl/styles.xml`：输出省略 styles part，
移除 styles content type override，保留 workbook `.rels` 中的 inbound styles
relationship，不凭空创建 styles owner `.rels` omission；这只是 no-pruning /
preservation 审计证据，不是 style id 迁移、样式合并、cell `s` 引用同步、
relationship repair、existing-file style preservation 或完整样式编辑；linked-object fixture
上的显式 VBA project removal 回归还覆盖 `xl/vbaProject.bin`：输出省略 VBA project
part，移除 VBA content type override，保留 workbook `.rels` 中的 inbound VBA
relationship，不凭空创建 VBA owner `.rels` omission；这只是 no-pruning /
preservation 审计证据，不是 macro generation、VBA 语义编辑、签名保真、
relationship repair 或完整宏支持；linked-object fixture
上的显式 VML drawing removal 回归还覆盖 `xl/drawings/vmlDrawing1.vml`：输出省略
VML drawing part，移除 VML content type override，保留 worksheet `.rels` 中的
URI-qualified inbound `vmlDrawing` relationship，不凭空创建 VML owner `.rels`
omission；这只是 no-pruning / preservation 审计证据，不是 VML shape editing、
legacy drawing mutation、relationship repair 或完整 VML/drawing 支持；linked-object fixture
上的显式 percent-decoded drawing removal 回归还覆盖 `xl/drawings/drawing space.xml`：
输出省略 percent-decoded drawing part，移除 drawing content type override，保留 worksheet
`.rels` 中原始 `../drawings/drawing%20space.xml` inbound relationship，不凭空创建
`xl/drawings/_rels/drawing space.xml.rels`；这只是 no-pruning / preservation
审计证据，不是 percent-encoded target repair、relationship rewrite、drawing
mutation 或完整 drawing 支持；linked-object fixture
上的 removal 回归还覆盖后调用的 `remove_part()` 压过此前 ordinary replacement，清理 stale
replacement state 并以 removed-part audit / content type cleanup 为最终状态；反向顺序回归还
覆盖对源 package 中已有的普通 part，后调用的 ordinary `replace_part()` 可把此前
`remove_part()` 的目标恢复为 active replacement，清理 stale removed-part / removed owner
`.rels` audit 与 omitted entry 状态，并把存在的 source-owned `.rels` 重新记录为
copy-original audit；对带 content type override 的 part，还覆盖恢复后
`[Content_Types].xml` 回到 source bytes / copy-original audit；linked-object fixture
还覆盖 workbook-specific 反向顺序：显式移除 `xl/workbook.xml` 后再 ordinary
`replace_part()` 会恢复 workbook active replacement、恢复 source-owned workbook `.rels`
copy-original audit、保留 package `_rels/.rels` inbound officeDocument relationship，
并让 `[Content_Types].xml` 回到 source bytes / copy-original audit；同时覆盖
worksheet-specific 反向顺序：显式移除 `xl/worksheets/sheet1.xml` 后再 ordinary
`replace_part()` 会恢复 worksheet active replacement、恢复 source-owned worksheet `.rels`
copy-original audit、保留 workbook `.rels` inbound worksheet relationship，并让
`[Content_Types].xml` 回到 source bytes / copy-original audit；同时覆盖
drawing-specific 反向顺序：显式移除 `xl/drawings/drawing1.xml` 后再 ordinary
`replace_part()` 会恢复 drawing active replacement、恢复 source-owned drawing `.rels`
copy-original audit、保留 worksheet `.rels` 中 direct / URI-qualified inbound drawing
relationships，并让 `[Content_Types].xml` 回到 source bytes / copy-original audit；
这不是 drawing mutation、object deletion、事务式 undo、
relationship repair、content type repair、语义合并或 public API；同时覆盖
sharedStrings-specific 反向顺序：显式移除 `xl/sharedStrings.xml` 后再 ordinary
`replace_part()` 会恢复 sharedStrings active replacement、恢复 source-owned
sharedStrings `.rels` copy-original audit、保留 workbook `.rels` inbound sharedStrings
relationship，并让 `[Content_Types].xml` 回到 source bytes / copy-original audit。
这不是 sharedStrings 索引迁移、字符串表重建、worksheet cell 引用同步、relationship
repair、content type repair、事务式 undo、语义合并或 public API；同时覆盖
styles-specific 反向顺序：显式移除 `xl/styles.xml` 后再 ordinary
`replace_part()` 会恢复 styles active replacement、保留 workbook `.rels` inbound
styles relationship，不凭空创建 styles owner `.rels`，并让 `[Content_Types].xml`
回到 source bytes / copy-original audit。这不是 style id 迁移、样式合并、cell `s`
引用同步、relationship repair、content type repair、事务式 undo、语义合并、
existing-file style preservation 或 public API；
invalid removal 失败覆盖 edit-plan entries/notes、package-entry audit、removed audit、calc policy、
manifest write-mode、aggregate `planned_output()` / legacy output-entry preview
和 copied output bytes 不污染；linked-object fixture
上的 ordinary media replacement 回归还验证只重写 `xl/media/image1.png` 时，drawing
`.rels`、PNG default content type、workbook、worksheet、drawing、chart 和 unknown
extension entries 仍保持同一 copy-original 基线；这不是图片解码、drawing mutation
或 existing-workbook image editing；同一路径还覆盖 default-typed media 先显式移除再
ordinary replacement 的反向顺序：后续 replacement 会恢复 active media part、清理
stale removed-part audit、保留 PNG default content type 且不把
`xl/media/image1.png` 提升成 override、保留 inbound drawing `.rels`，并且不凭空创建
media owner `.rels`；这不是事务式 undo、图片语义合并、relationship repair、
content type repair 或完整 image preservation；同一路径还覆盖 media 先 ordinary
replacement 再显式移除的顺序：后续 removal 会清理 active media replacement、记录
removed-part audit 和 inbound drawing relationship metadata，输出省略
`xl/media/image1.png`，保留 PNG default content type 且不把 media 提升成 override、
保留 inbound drawing `.rels`，并且不凭空创建 media owner `.rels`；内部 output-plan
快照还暴露 omitted media part、drawing inbound audit、content types / drawing `.rels`
copy-original 和无 media owner `.rels` 条目；这同样不是事务式 undo、图片语义合并、
relationship pruning/repair、content type repair、existing-workbook image editing 或完整
image preservation；linked-object fixture 上的 ordinary table
replacement 回归还验证只重写 `xl/tables/table1.xml` 时，worksheet `.rels`、table
content type override、workbook、worksheet、drawing、chart、media 和 unknown
extension entries 仍保持同一 copy-original 基线；这不是 table resize、
calculated columns、totals generation 或 existing-workbook table editing；同一路径还覆盖
table 先显式移除再 ordinary replacement 的反向顺序：后续 replacement 会恢复 active
table part、清理 stale removed-part audit、让 `[Content_Types].xml` 回到
source/copy-original audit、保留 worksheet `.rels` inbound table relationship，且不凭空
创建 table owner `.rels`；这不是 table resize、calculated columns、totals generation、
事务式 undo、relationship repair、content type repair 或 existing-workbook table editing；
同一路径还覆盖 table 先 ordinary replacement 再显式移除的顺序：后续 removal
会清理 active table replacement、记录 removed-part audit 和 inbound worksheet
relationship metadata、输出省略 table part、移除 table content type override、保留
worksheet `.rels` inbound table relationship，且不凭空创建 table owner `.rels`；
内部 output-plan 快照还暴露 omitted table part、worksheet inbound audit、content
types local-DOM rewrite 和无 table owner `.rels` 条目；这不是 table delete
semantics、table resize、calculated columns、totals generation、事务式 undo、
relationship pruning/repair、content type repair 或 existing-workbook table editing；
linked-object fixture 上的 ordinary
sharedStrings replacement 回归还验证只重写 `xl/sharedStrings.xml` 时，workbook
`.rels`、sharedStrings owner `.rels`、sharedStrings content type override、styles、table、
media、VBA 和 unknown extension entries 仍保持同一 copy-original 基线；这不是
sharedStrings 索引迁移、字符串表重建、worksheet cell 引用同步或 existing-file
sharedStrings 语义编辑；同一路径还覆盖 sharedStrings 先 ordinary replacement 再显式移除的
顺序：后续 removal 会清理 active sharedStrings replacement、记录 removed-part audit、
输出省略 `xl/sharedStrings.xml` 及其 source-owned owner `.rels`、移除 sharedStrings
content type override、保留 workbook `.rels` 中的 inbound sharedStrings relationship；
它不会修剪 worksheet `t="s"` 引用或重建字符串表。这不是 sharedStrings 索引迁移、
字符串表重建、worksheet cell 引用同步、事务式 undo、relationship pruning/repair、
content type repair、existing-file sharedStrings 语义编辑或 public API；linked-object fixture 上的 ordinary styles replacement
回归还验证只重写 `xl/styles.xml` 时，workbook `.rels`、styles content type override、
sharedStrings、sharedStrings owner `.rels`、table、media、VBA 和 unknown extension
entries 仍保持同一 copy-original 基线，且不会凭空创建 `xl/_rels/styles.xml.rels`；
这不是 style id 迁移、样式合并、cell `s` 引用同步、existing-file style preservation
或完整样式编辑；同一路径还覆盖 styles 先 ordinary replacement 再显式移除的顺序：
后续 removal 会清理 active styles replacement、记录 removed-part audit、输出省略
`xl/styles.xml`、移除 styles content type override、保留 workbook `.rels` 中的 inbound
styles relationship，且不凭空创建 `xl/_rels/styles.xml.rels`；它不会迁移 style id
或重写 cell `s` 引用。这不是 style id 迁移、样式合并、existing-file style
preservation、事务式 undo、relationship pruning/repair、content type repair、
完整样式编辑或 public API；linked-object fixture 上的 ordinary chart replacement 回归还验证只重写
`xl/charts/chart1.xml` 时，drawing `.rels` 中的 chart / URI-qualified chart
relationships、chart content type override、media、table、sharedStrings、styles、VBA
和 unknown extension entries 仍保持同一 copy-original 基线，且不会凭空创建 chart
owner `.rels`；这不是 chart reference migration、series/cache update、drawing
mutation、existing-workbook chart editing 或完整图表支持；同一路径还覆盖 chart 先
显式移除再 ordinary replacement 的反向顺序：后续 replacement 会恢复 active chart
part、清理 stale removed-part audit、让 `[Content_Types].xml` 回到 source/copy-original
audit、保留 drawing `.rels` 中的 direct / URI-qualified inbound chart relationships、
保留其它 linked/unknown source entries，且不会凭空创建 chart owner `.rels`；
这不是 chart semantic merge、chart reference repair、relationship repair、
content type repair、事务式 undo、existing-workbook chart editing 或 public API；
内部 `planned_output()` 快照还覆盖该 restore 状态：暴露 active chart
`StreamRewrite` entry、content types copy-original audit、preserved inbound drawing
`.rels`、preserved linked/unknown entries、empty removed_parts 与
removed_package_entries，且不凭空创建 chart owner `.rels`；这仍只是 Patch audit，
不是 public output planner 或 chart editing API；
同一路径还覆盖 chart 先
ordinary replacement 再显式移除的顺序：后续 removal 会清理 active chart
replacement、记录 removed-part audit 和 direct / URI-qualified inbound drawing
relationship metadata、输出省略 `xl/charts/chart1.xml`、移除 chart content type
override、保留 inbound drawing `.rels` 和其它 linked/unknown source entries，
且不会凭空创建 chart owner `.rels`；这不是 chart delete semantics、chart reference
repair、relationship pruning/repair、content type repair、事务式 undo、语义合并、
existing-workbook chart editing 或 public API；
内部 `planned_output()` 快照还覆盖该 final-removal 状态：暴露 omitted chart
part、removed_parts target/reason/inbound audit、drawing-owned direct /
URI-qualified inbound relationship metadata、content types rewrite、empty
removed_package_entries，且不凭空创建 chart owner `.rels`；这仍只是 Patch audit，
不是 public output planner、chart editing API、relationship repair 或 transactional
undo；linked-object fixture 上的
ordinary VBA project replacement 回归还验证只重写 `xl/vbaProject.bin` 时，
workbook `.rels`、VBA content type override、worksheet、drawing、chart、media、table、
sharedStrings、styles、calcChain 和 unknown extension entries 仍保持同一 copy-original
基线，且不会凭空创建 `xl/_rels/vbaProject.bin.rels`；这不是 macro generation、
VBA 语义编辑、signature preservation、workbook relationship repair 或完整宏支持；
同一路径还覆盖 VBA project 先显式移除再 ordinary replacement 的反向顺序：
后续 replacement 会恢复 active VBA project part、清理 stale removed-part audit、
让 `[Content_Types].xml` 回到 source/copy-original audit、保留 workbook `.rels`
中的 inbound VBA relationship，且不凭空创建 `xl/_rels/vbaProject.bin.rels`；
这不是 macro generation、VBA 语义编辑、签名保真、事务式 undo、workbook
relationship repair、content type repair 或完整宏支持；
内部 `planned_output()` 快照还覆盖该 restore 状态：暴露 active VBA project
`StreamRewrite` entry、content types copy-original audit、preserved package/workbook
relationships、preserved worksheet/drawing/chart/media/table/sharedStrings/styles/
calcChain/unknown entries、empty removed_parts 与 removed_package_entries，
且不凭空创建 `xl/_rels/vbaProject.bin.rels`；这只是 Patch audit，不是 public
output planner、macro editing API、relationship repair、content type repair 或
transactional undo；
同一路径还覆盖 VBA project 先 ordinary replacement 再显式移除的顺序：
后续 removal 会清理 active VBA replacement、记录 removed-part audit、输出省略
VBA project part、移除 VBA content type override、保留 workbook `.rels` 中的
inbound VBA relationship，且不凭空创建 `xl/_rels/vbaProject.bin.rels`；
这不是 macro generation、VBA 语义编辑、签名保真、事务式 undo、workbook
relationship repair、content type repair 或完整宏支持；
内部 `planned_output()` 快照还覆盖该 final-removal 状态：暴露 omitted VBA project
part、removed_parts target/reason/inbound audit、workbook inbound VBA relationship
metadata、content types rewrite、empty removed_package_entries，且不凭空创建
`xl/_rels/vbaProject.bin.rels`；这只是 Patch audit，不是 public output planner、
macro editing API、relationship repair、content type repair 或 transactional undo；
linked-object fixture 上的 ordinary VML drawing replacement 回归还验证只重写
`xl/drawings/vmlDrawing1.vml` 时，worksheet `.rels` 中的 URI-qualified
`vmlDrawing` relationship、VML content type override、workbook、worksheet、drawing、
chart、media、table、sharedStrings、styles、VBA、calcChain 和 unknown extension
entries 仍保持同一 copy-original 基线，且不会凭空创建
`xl/drawings/_rels/vmlDrawing1.vml.rels`；这不是 VML shape editing、legacy drawing
mutation、relationship repair 或完整 VML/drawing 支持；
同一路径还覆盖 VML drawing 先显式移除再 ordinary replacement 的反向顺序：后续
replacement 会恢复 active VML drawing part、清理 stale removed-part audit、让
`[Content_Types].xml` 回到 source/copy-original audit、保留 worksheet `.rels`
中的 URI-qualified inbound `vmlDrawing` relationship，且不凭空创建 VML owner
`.rels`；内部 `planned_output()` 快照也覆盖该 restore 状态：暴露 active
VML drawing `LocalDomRewrite` entry、content types copy-original audit、
preserved package/workbook/worksheet/drawing relationships、preserved
linked/unknown entries、empty removed_parts 与 removed_package_entries，
且不凭空创建 VML owner `.rels`；这不是 public output
planner、VML shape editing、legacy drawing mutation、事务式 undo、relationship
repair、content type repair 或完整 VML/drawing 支持；
同一路径还覆盖 VML drawing 先 ordinary replacement 再显式移除的顺序：后续
removal 会清理 active VML drawing replacement、记录 removed-part audit、输出省略
VML drawing part、移除 VML content type override、保留 worksheet `.rels` 中的
URI-qualified inbound `vmlDrawing` relationship，且不凭空创建 VML owner `.rels`；
这不是 VML shape editing、legacy drawing mutation、事务式 undo、relationship
pruning/repair、content type repair 或完整 VML/drawing 支持；
内部 `planned_output()` 快照还覆盖该 final-removal 状态：暴露 omitted VML
drawing part、removed_parts target/reason/inbound audit、URI-qualified worksheet
inbound relationship metadata、content types rewrite、empty removed_package_entries，
且不凭空创建 VML owner `.rels`；这仍只是 Patch audit，不是 public output
planner、drawing editing API、relationship repair 或 transactional undo；
linked-object fixture 上的 ordinary percent-decoded drawing replacement 回归还验证只重写
`xl/drawings/drawing space.xml` 时，worksheet `.rels` 中原始
`../drawings/drawing%20space.xml` relationship、drawing content type override、workbook、
worksheet、drawing、chart、media、table、VML、sharedStrings、styles、VBA、calcChain 和
unknown extension entries 仍保持同一 copy-original 基线，且不会凭空创建
`xl/drawings/_rels/drawing space.xml.rels`；这不是 percent-encoded target repair、
relationship rewrite、drawing mutation 或完整 drawing 支持；
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
事务式 undo、content type repair 或完整 drawing 支持；
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
relationship pruning/repair、content type repair 或完整 drawing 支持；
worksheet replacement 窄路径会 byte-preserve worksheet `.rels`、drawing、drawing `.rels`、media、
chart、table、untouched `xl/sharedStrings.xml`、sharedStrings owner `.rels`、untouched `xl/styles.xml`、VBA 和
可达 unknown extension part 及其 owner `.rels`，包括替换后的 worksheet XML 省略源 `<drawing>` /
`<tableParts>` 引用时仍原样保留；并验证 workbook `definedNames` 在 workbook metadata rewrite
路径下保留；另有 registered comments part 小 fixture 证明 worksheet rewrite 会把
`xl/comments/comment1.xml` 和 source worksheet `.rels` 作为 copy-original preservation
处理，保留 comments content type override，并可由 `PackageReader` / `RelationshipGraph`
重读；这不是 comments 编辑、threaded comments、notes UI、relationship repair、
orphan cleanup 或 public API；另有 threaded comments / persons 小 fixture 证明
worksheet rewrite 会把 `xl/threadedComments/threadedComment1.xml`、
`xl/persons/person.xml`、source worksheet `.rels` 和 workbook `.rels` 作为
copy-original preservation 处理，并可由 `PackageReader` / `RelationshipGraph`
重读；这不是 comments / threaded comments 编辑、notes UI、relationship repair、
orphan cleanup 或 public API；同一 threaded comments / persons 小 fixture 还覆盖
ordinary `replace_part("/xl/threadedComments/threadedComment1.xml", ...)` 和显式
removal：replacement 只重写 threaded comments XML，保留 legacy comments、persons、
worksheet `.rels` 中的 legacy/threaded inbound relationships、workbook `.rels` 中的
persons relationship、content type overrides 和 unknown entry；removal 输出省略
threaded comments part 并移除其 content type override，但保留 worksheet `.rels` 中
指向缺失 threaded comments part 的 inbound relationship、persons part / workbook
relationship、legacy comments 和 unknown entry。这不是 threaded comments model
mutation、persons/schema repair、relationship pruning/repair、orphan cleanup、notes UI
或 public API；同一 threaded comments / persons 小 fixture 还覆盖 ordinary
`replace_part("/xl/persons/person.xml", ...)` 和显式移除：replacement 只重写 persons
XML，保留 workbook inbound persons relationship、threaded comments、legacy comments、
worksheet `.rels`、content type overrides 和 unknown entry；removal 输出省略 persons
part 并移除 persons content type override，但保留 workbook `.rels` 中指向缺失 persons
part 的 inbound relationship、threaded comments、legacy comments、worksheet 和 unknown
entry。这不是 persons/schema repair、threaded comments model mutation、relationship
pruning/repair、orphan cleanup、notes UI 或 public API；
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
relationship repair、orphan cleanup 或 public API；threaded comments 后续 removal 会清理 active
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
relationship pruning/repair、content type repair、orphan cleanup、notes UI 或 public API；
内部 output-plan 快照还暴露单个 omitted threaded comments part / removed-part audit
（target 为 threaded comments part、reason 保留 after replacement、inbound relationship 一条）、
worksheet inbound threadedComment relationship audit、content types rewrite、preserved
worksheet / workbook `.rels` 与 persons part、且 removed_package_entries 为空、不凭空创建
threaded comments owner `.rels`；
内部 output-plan 快照还暴露单个 omitted persons part / removed-part audit（target 为
persons part、reason 保留 after replacement、inbound relationship 一条）、workbook inbound
persons relationship audit、content types rewrite、preserved workbook/worksheet `.rels`
与 threaded comments part、且 removed_package_entries 为空、不凭空创建 persons owner `.rels`；
另有 pivot table / pivot cache 小 fixture 证明 worksheet rewrite 会把
`xl/pivotTables/pivotTable1.xml`、
`xl/pivotCache/pivotCacheDefinition1.xml`、`xl/pivotCache/pivotCacheRecords1.xml`、
source worksheet `.rels`、pivot table owner `.rels`、pivot cache definition owner
`.rels` 和 workbook `.rels` 作为 copy-original preservation 处理，并可由
`PackageReader` / `RelationshipGraph` 重读；这不是 pivot table 编辑、pivot cache
rebuild、relationship repair、orphan cleanup 或 public API；该 worksheet rewrite
路径的内部 `planned_output()` 现在也暴露 fullCalcOnLoad / `CalcChainAction::Remove`、
worksheet `StreamRewrite`、workbook `LocalDomRewrite`、package/workbook/worksheet
`.rels` copy-original、pivot table / pivot cache definition / pivot cache records
relationship context、content types 和 unknown entry copy-original，且确认不凭空创建
records owner `.rels`；这仍只是 Patch audit 快照，不是 pivot cache rebuild、
records refresh、relationship repair/pruning、orphan cleanup 或 public API；同一 pivot table / pivot
cache 小 fixture 还覆盖 ordinary `replace_part("/xl/pivotTables/pivotTable1.xml", ...)`
和显式 removal：replacement 只重写 pivot table XML，保留 worksheet `.rels` 中的
inbound pivotTable relationship、pivot table owner `.rels` / pivotCacheDefinition
relationship、pivot cache definition / records parts、pivot cache definition owner
`.rels`、workbook `<pivotCaches>`、workbook `.rels` pivotCacheDefinition relationship、
content type overrides 和 unknown entry；removal 输出省略 pivot table part 及其 owner
`.rels`，移除 pivot table content type override，但保留 worksheet `.rels` 中指向缺失
part 的 inbound relationship、workbook pivot cache metadata、pivot cache definition /
records 链和 unknown entry。这不是 pivot table 语义编辑、pivot cache rebuild、
cache records 刷新、relationship pruning/repair、orphan cleanup、owner `.rels` repair
或 public API；
同一路径现在还覆盖 pivot table remove-then-ordinary-replace 和
ordinary-replace-then-remove ordering：后续 replacement 会恢复 active pivot table
part、清理 stale removed-part / removed owner `.rels` audit、恢复 owner `.rels`
copy-original audit，并让 `[Content_Types].xml` 回到 source/copy-original audit；后续
removal 会清理 active replacement、记录 removed-part / removed owner `.rels`
audit、输出省略 pivot table part 和 owner `.rels`、移除 pivot table content type
override，并保留 worksheet `.rels` 中指向缺失 part 的 inbound relationship、workbook
pivot cache metadata、pivot cache definition / records 链和 unknown entry。这不是
事务式 undo、pivot table 语义合并、pivot cache rebuild、relationship pruning/repair、
content type repair、orphan cleanup 或 public API；
内部 `planned_output()` 快照还覆盖该 remove-then-replace restore 状态：
暴露 active pivot table `LocalDomRewrite` entry、pivot table owner `.rels`
copy-original `SourceRelationships` audit、content types copy-original audit，以及
preserved package/worksheet/workbook relationships、pivot cache definition / records 链
和 unknown entry；同一快照族也覆盖 replace-then-remove final-removal 状态：暴露
omitted pivot table part、omitted pivot table owner `.rels`、worksheet inbound
pivotTable relationship audit、content types rewrite、preserved worksheet/workbook
relationships、pivot cache definition / records 链和 unknown entry；这仍只是 Patch
audit，不是 pivot table 语义编辑、pivot cache rebuild、relationship pruning/repair、
orphan cleanup 或 public API；
同一 fixture 还覆盖 ordinary
`replace_part("/xl/pivotCache/pivotCacheDefinition1.xml", ...)` 和显式移除：
replacement 只重写 pivot cache definition XML，保留 workbook/pivot-table inbound
relationships、pivot cache records、pivot cache definition owner `.rels`、content type
overrides 和 unknown entry；removal 输出省略 pivot cache definition part 及其 owner
`.rels`，移除 cache definition content type override，但保留 workbook/pivot table
inbound relationships、pivot table、pivot cache records、worksheet 和 unknown entry。
这不是 pivot cache rebuild、cache-record refresh、relationship pruning/repair、
orphan cleanup、owner `.rels` repair 或 public API；
`replace_part("/xl/pivotCache/pivotCacheDefinition1.xml", ...)` 同路径现在还覆盖
remove-then-ordinary-replace 和 ordinary-replace-then-remove ordering：后续 replacement
会恢复 active pivot cache definition、清理 stale removed-part / removed owner `.rels`
audit、恢复 owner `.rels` copy-original audit，并让 `[Content_Types].xml` 回到
source/copy-original audit；后续 removal 会清理 active replacement、记录 removed-part /
removed owner `.rels` audit、输出省略 cache definition part 和 owner `.rels`，并保留
workbook / pivot table inbound relationships、pivot table、cache records、worksheet 和
unknown entry。这不是事务式 undo、pivot cache 语义合并、relationship pruning/repair、
content type repair、orphan cleanup 或 public API；内部 `planned_output()` 快照还覆盖
该 remove-then-replace restore 状态：暴露 active pivot cache definition
`LocalDomRewrite` entry、owner `.rels` copy-original `SourceRelationships` audit、
content types copy-original audit，以及 preserved package/worksheet/workbook
relationships、pivot table/cache records/unknown entries；该快照也覆盖
replace-then-remove final-removal 状态：暴露 omitted cache definition part、omitted
owner `.rels`、workbook / pivot table inbound pivotCacheDefinition relationship audit、
content types rewrite、preserved workbook/worksheet/pivot table/cache records/unknown
entries；这仍只是 Patch audit，不是 pivot cache rebuild、cache-record refresh、
relationship pruning/repair、content type repair、orphan cleanup 或 public API；
同一 fixture 还覆盖 ordinary
`replace_part("/xl/pivotCache/pivotCacheRecords1.xml", ...)` 和显式移除：
replacement 只重写 pivot cache records XML，保留 cache definition owner `.rels`
inbound relationship、pivot cache definition、pivot table、workbook / worksheet
relationships、content type overrides 和 unknown entry；removal 输出省略 pivot cache
records part，移除 records content type override，但保留 cache definition owner `.rels`
中指向缺失 records part 的 inbound relationship、pivot cache definition、pivot table、
workbook、worksheet 和 unknown entry。这不是 pivot cache records refresh、
pivot cache rebuild、relationship pruning/repair、orphan cleanup 或 public API；
同一路径现在还覆盖 remove-then-ordinary-replace 和 ordinary-replace-then-remove
ordering：后续 replacement 会恢复 active pivot cache records、清理 stale
removed-part audit、让 `[Content_Types].xml` 回到 source/copy-original audit，并且
不凭空创建 records owner `.rels`；后续 removal 会清理 active replacement、记录
removed-part 和 cache-definition inbound relationship audit、输出省略 records part、
移除 records content type override，并继续保留 cache definition owner `.rels`
中指向缺失 records part 的 inbound relationship、pivot cache definition、pivot table、
workbook、worksheet 和 unknown entry。这不是事务式 undo、pivot cache records 语义合并、
relationship pruning/repair、content type repair、orphan cleanup 或 public API；
内部 `planned_output()` 快照还覆盖该 remove-then-replace restore 状态：暴露
active pivot cache records `StreamRewrite` entry、content types copy-original audit、
preserved package/worksheet/workbook relationships、pivot table/cache definition 链和
unknown entry，且 no invented records owner `.rels`；该快照也覆盖
replace-then-remove final-removal 状态：暴露 omitted records part、cache-definition
inbound pivotCacheRecords relationship audit、content types rewrite、preserved
cache definition owner `.rels`、no invented records owner `.rels` 和 preserved
workbook/worksheet/pivot table/cache definition/unknown entries；这仍只是 Patch audit，
不是 pivot cache records refresh、pivot cache rebuild、relationship pruning/repair、
content type repair、orphan cleanup 或 public API；另有 workbook external
links 小 fixture 证明 worksheet rewrite 在重写 `xl/workbook.xml` calc metadata 时，
仍会保留 workbook `<externalReferences>`、workbook `.rels` 中的 externalLink
relationship、`xl/externalLinks/externalLink1.xml`、externalLink owner `.rels`、
external `externalLinkPath` target、content type override 和 unknown entry，并可由
`PackageReader` / `RelationshipGraph` 重读；这不是 external links 编辑、外部数据
刷新、路径校验、relationship repair、orphan cleanup 或 public API；该 worksheet
rewrite 路径的内部 `planned_output()` 现在也暴露 fullCalcOnLoad /
`CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook `LocalDomRewrite`、
workbook `.rels` copy-original、externalLink part 与 owner `.rels` copy-original、
content types copy-original 和 unknown entry preservation，且不新增 relationship
target audit；这仍只是 Patch audit 快照，不是 external links 编辑或 relationship
repair；同一 workbook
external links 小 fixture 还覆盖 ordinary
`replace_part("/xl/externalLinks/externalLink1.xml", ...)` 和显式 removal：
replacement 只重写 externalLink XML，保留 workbook `.rels` 中的 inbound
externalLink relationship、externalLink owner `.rels` 中的 external
`externalLinkPath` target、content type override、worksheet 和 unknown entry；removal
输出省略 externalLink part 及其 owner `.rels`，移除 externalLink content type
override，但保留 workbook `<externalReferences>`、workbook `.rels` 中指向缺失 part
的 inbound relationship、worksheet 和 unknown entry。这不是 external links 语义编辑、
外部数据刷新、路径校验、relationship pruning/repair、orphan cleanup、owner `.rels`
repair 或 public API；同一路径还覆盖先显式移除 externalLink 再 ordinary replace，
以及先 ordinary replace 再显式移除：前者清理 stale removed-part / removed owner
`.rels` audit，恢复 active externalLink part、owner `.rels` copy-original audit 和
source content types audit；后者清理 active replacement，记录 removed-part / removed
owner `.rels` audit，输出省略 externalLink part 和 owner `.rels`，并保留 workbook
inbound relationship、worksheet 和 unknown entry。这不是事务式 undo、external links
语义合并、relationship pruning/repair、content type repair、orphan cleanup 或 public
API；内部 `planned_output()` 快照还覆盖该 remove-then-replace restore 状态：
暴露 active externalLink `LocalDomRewrite` entry、externalLink owner `.rels`
copy-original `SourceRelationships` audit、content types copy-original audit，以及
preserved package/workbook relationships、workbook、worksheet 和 unknown entry；同一
快照族也覆盖 replace-then-remove final-removal 状态：暴露 omitted externalLink part、
omitted externalLink owner `.rels`、workbook inbound externalLink relationship audit、
content types rewrite、preserved package/workbook relationships、workbook、worksheet
和 unknown entry；这仍只是 Patch audit，不是 external links 语义编辑、外部数据刷新、
relationship pruning/repair、orphan cleanup
或 public API；另有 custom XML
小 fixture 证明 worksheet rewrite 会保留 package `_rels/.rels` 中的 customXml
relationship、`customXml/item1.xml`、custom XML item owner `.rels`、
`customXml/itemProps1.xml`、custom XML properties content type override 和 unknown
entry，并可由 `PackageReader` / `RelationshipGraph` 重读；这不是 custom XML 编辑、
schema/data binding、relationship repair、orphan cleanup 或 public API；该 worksheet
rewrite 路径的内部 `planned_output()` 现在也暴露 fullCalcOnLoad /
`CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook `LocalDomRewrite`、
package relationships copy-original、custom XML item / item owner `.rels` /
properties part copy-original、content types copy-original 和 unknown entry preservation，
且不新增 relationship target audit、不凭空创建 properties owner `.rels`；这仍只是
Patch audit 快照，不是 custom XML 编辑、schema/data binding 或 relationship
repair；同一 custom XML
小 fixture 还覆盖 ordinary `replace_part("/customXml/item1.xml", ...)`：只重写
custom XML item，保留 package `_rels/.rels` customXml inbound relationship、
custom XML item owner `.rels` / customXmlProps relationship、`customXml/itemProps1.xml`、
custom XML properties content type override、默认 XML content type 和 unknown entry
copy-original 基线，并可由 `PackageReader` / `RelationshipGraph` 重读；这不是
custom XML 语义编辑、schema/data binding、relationship repair、content type repair、
orphan cleanup 或 public API；同一 custom XML 小 fixture 还覆盖显式移除
`customXml/item1.xml`：输出省略 custom XML item 及其 source-owned owner `.rels`、
保留 package `_rels/.rels` customXml inbound relationship、保留
`customXml/itemProps1.xml`、custom XML properties content type override、默认 XML
content type 和 unknown entry，且不重写 `[Content_Types].xml`；这不是 custom XML
删除语义、schema/data binding、relationship pruning/repair、content type repair、
orphan cleanup 或 public API；同一小 fixture 还覆盖同一路径先显式移除再
ordinary replace，以及先 ordinary replace 再显式移除：前者恢复 active custom XML
item、清理 stale removed-part / removed owner `.rels` audit、恢复 owner `.rels`
copy-original audit，且不重写 `[Content_Types].xml`；后者清理 active replacement、
记录 removed-part / removed owner `.rels` audit、输出省略 custom XML item 和 owner
`.rels`，并保留 package inbound relationship、properties part、默认 XML content
type 和 unknown entry。内部 `planned_output()` 快照现在还覆盖该 restore 状态：
暴露 active custom XML item `LocalDomRewrite` entry、owner `.rels` copy-original
`SourceRelationships` audit、preserved package relationships、content types、
workbook、worksheet、properties part 和 unknown entry。内部 `planned_output()` 快照还覆盖该 final-removal 状态：
暴露 omitted custom XML item、omitted source-owned owner `.rels`、package inbound
customXml relationship audit，以及 preserved package relationships、content types、
workbook、worksheet、properties part 和 unknown entry。这不是事务式 undo、custom XML 语义合并、relationship
pruning/repair、content type repair、orphan cleanup 或 public API；同一小 fixture
还覆盖 `customXml/itemProps1.xml` properties part 的 ordinary replacement 和显式
removal：replacement 只重写 properties part，保留 custom XML item、item owner
`.rels` / customXmlProps inbound relationship、package customXml relationship、
properties content type override 和 unknown entry；removal 输出省略 properties part、
移除 properties content type override，但保留 custom XML item、item owner `.rels`
中指向缺失 properties part 的 inbound customXmlProps relationship、package customXml
relationship、默认 XML content type 和 unknown entry。这不是 custom XML properties
编辑、schema/data binding、relationship pruning/repair、content type repair、
orphan cleanup 或 public API；同一小 fixture
的内部 `planned_output()` 快照还覆盖 ordinary properties replacement 状态：暴露
active properties part `LocalDomRewrite`、preserved content types / package
relationships、preserved custom XML item / item owner `.rels` / workbook / worksheet /
unknown entry，且不凭空创建 properties owner `.rels`。这仍只是 Patch audit，不是
custom XML properties 语义编辑、schema/data binding、事务式 undo、relationship
pruning/repair、content type repair、orphan cleanup 或 public API；同一小 fixture
还覆盖 properties part 先显式移除再 ordinary replace，以及先 ordinary replace
再显式移除：前者清理 stale removed-part audit、恢复 active properties part、恢复
properties content type override/content-types copy-original audit，并继续保留 item
owner `.rels`；后者清理 active replacement、记录 removed-part audit、输出省略
properties part、移除 properties content type override，并继续保留 item owner `.rels`
中的 inbound customXmlProps relationship。这不是事务式 undo、custom XML properties
语义合并、relationship pruning/repair、content type repair、orphan cleanup 或 public
API；同一小 fixture
的内部 `planned_output()` 快照还覆盖该 properties final-removal 状态：暴露 omitted
properties part、item-owned inbound customXmlProps relationship audit、content types
rewrite、preserved custom XML item / item owner `.rels` / package relationships /
workbook / worksheet / unknown entry，且不凭空创建 properties owner `.rels`。这仍只是
Patch audit，不是 custom XML properties 删除语义、relationship pruning/repair、
content type repair、orphan cleanup 或 public API；同一小 fixture
的内部 `planned_output()` 快照还覆盖该 properties restore 状态：暴露 active
properties part `LocalDomRewrite`、restored content types copy-original audit、
preserved custom XML item / item owner `.rels` / package relationships / workbook /
worksheet / unknown entry，且不凭空创建 properties owner `.rels`。这仍只是 Patch
audit，不是 custom XML properties 语义合并、事务式 undo、relationship
pruning/repair、content type repair、orphan cleanup 或 public API；同一小 fixture
还覆盖跨路径顺序：先显式移除 custom XML item，再 ordinary replace
`customXml/itemProps1.xml` properties part。后续 properties replacement 只重写
properties payload，保留 removed custom XML item / removed owner `.rels` audit，输出
继续省略 custom XML item 和 item owner `.rels`，并保留 package customXml inbound
relationship、properties content type override、默认 XML content type 和 unknown entry。
这不是 custom XML dependency repair、relationship pruning/repair、content type repair、
orphan cleanup、事务式 undo 或 public API；同一小 fixture
的内部 `planned_output()` 快照还覆盖该跨路径状态：暴露 omitted custom XML item、
omitted source-owned owner `.rels`、package inbound customXml relationship audit、
active properties part local-DOM rewrite、preserved package relationships / content
types / workbook / worksheet / unknown entry，且不凭空创建 properties owner `.rels`。
这仍只是 Patch audit，不是 custom XML dependency repair、relationship pruning/repair、
content type repair、orphan cleanup、事务式 undo 或 public API；同一小 fixture
还覆盖反向跨路径顺序：先显式移除 properties part，再 ordinary replace custom XML
item。后续 item replacement 只重写 item payload，保留 removed properties part audit /
content-types rewrite，输出继续省略 properties part 和 properties content type override，
并保留 item owner `.rels` 中指向缺失 properties part 的 customXmlProps relationship、
package customXml inbound relationship、默认 XML content type 和 unknown entry。这不是
custom XML dependency repair、relationship pruning/repair、content type repair、
orphan cleanup、事务式 undo 或 public API；同一小 fixture
的内部 `planned_output()` 快照还覆盖该反向跨路径状态：暴露 omitted properties
part、item-owned inbound customXmlProps relationship audit、content types rewrite、
active custom XML item local-DOM rewrite、preserved item owner `.rels` /
package relationships / workbook / worksheet / unknown entry，且不凭空创建 properties
owner `.rels`。这仍只是 Patch audit，不是 custom XML dependency repair、
relationship pruning/repair、content type repair、orphan cleanup、事务式 undo 或
public API；同一小 fixture
还覆盖 ordinary
`replace_part("/xl/comments/comment1.xml", ...)`：只重写 comments XML，保留 worksheet
`.rels` inbound comments relationship、comments content type override、workbook XML / workbook
`.rels`、worksheet 和 unknown entry copy-original 基线，且不凭空创建 comments owner
`.rels`；这不是 comments model mutation、threaded comments、notes UI、relationship
repair、orphan cleanup 或 public API；内部 `planned_output()` 快照还覆盖该 ordinary
replacement 状态：暴露 active comments part local-DOM rewrite、preserved content
types / package relationships / workbook / workbook `.rels` / worksheet /
worksheet `.rels` / unknown entry，且不凭空创建 comments owner `.rels`。这仍只是
Patch audit，不是 comments model mutation、notes UI、relationship repair、orphan
cleanup 或 public API；同一小 fixture 还覆盖显式移除
`xl/comments/comment1.xml`：输出省略 comments part、移除 comments content type override、
保留 worksheet `.rels` inbound comments relationship，且不凭空创建 comments owner
`.rels` omission；这不是 comments 删除语义、threaded comments、notes UI、
relationship pruning/repair、orphan cleanup 或 public API；同一小 fixture 还覆盖先显式移除再
ordinary `replace_part()` 的反向顺序：后续 replacement 会恢复 active comments part、
清理 stale removed-part audit、让 `[Content_Types].xml` 回到 source/copy-original audit、
保留 inbound worksheet `.rels`，且仍不凭空创建 comments owner `.rels`；这不是事务式
undo、comments 语义合并、relationship repair、orphan cleanup 或 public API；内部
`planned_output()` 快照还覆盖该 remove-then-replace 状态：暴露 active comments part
local-DOM rewrite、content types copy-original audit、preserved package/workbook/worksheet
`.rels` 和 unknown entry，清空 output-plan removed_parts / removed_package_entries
中的 stale removal 状态，且不凭空创建 comments owner `.rels`。这仍只是 Patch audit，
不是 comments undo、semantic merge、relationship repair、orphan cleanup 或 public API；
同一小 fixture
还覆盖先 ordinary `replace_part()` 再显式移除的顺序：后续 removal 会清理 active
comments replacement、记录 removed-part audit、输出省略 comments part、移除 comments
content type override、保留 inbound worksheet `.rels`，且仍不凭空创建 comments
owner `.rels`；这不是 comments 删除语义、事务式 undo、relationship pruning/repair、
orphan cleanup 或 public API；内部 `planned_output()` 快照还覆盖该
replace-then-remove final-removal 状态：暴露单个 omitted comments part /
removed-part audit（target 为 comments part、reason 保留 after replacement、
inbound relationship 一条）、content types rewrite、preserved package/workbook/worksheet
`.rels`，且 removed_package_entries 为空、不凭空创建 comments owner `.rels`；这仍只是
Patch audit，不是 comments 删除语义、事务式 undo、relationship pruning/repair、
orphan cleanup 或 public API；同时覆盖 `ReferencePolicy` 的 linked-object
fail、inbound-linked drawing removal fail、calcChain preserve / rebuild 拒绝、malformed workbook metadata / workbook calc
metadata rewrite 预检失败不污染 edit-plan entries/notes、aggregate `planned_output()` 和 manifest write-mode、
worksheet rewrite 缺少 `xl/workbook.xml` 前置 metadata 时失败不污染状态、
基础 linked-object fail 也会把 aggregate `planned_output()` 保持为 source copy-original 快照，
已有 ordinary workbook replacement 排队后 linked-object fail 或 inbound-linked
drawing removal fail 仍保留既有 replacement / manifest / source-owned `.rels` audit /
aggregate `planned_output()` 快照 / 输出 bytes、
queued core/app docProps helper 后 linked-object fail 仍保留既有 metadata edit /
package-entry audit / aggregate `planned_output()` 快照 / 输出 bytes、
inbound-linked drawing removal fail 也保持 aggregate `planned_output()`、worksheet/workbook payload audit、
removed audit、calc policy 和输出 bytes 不污染，
core/app docProps relationship target 冲突失败不污染状态、request-recalculation
fullCalcOnLoad 输出窄边界；linked-object fixture 也验证 worksheet-owned 和
drawing-owned external、URI-qualified、invalid 和 unresolved relationship target 审计 note
及结构化 `RelationshipTargetAudit` 会传播到 existing-file
`PackageEditor` edit plan。但当前没有 end-to-end public
`PackageWriter` / `PackageEditor` 编辑管线。该 writer boundary 默认走 stored bootstrap，
`FASTXLSX_ENABLE_MINIZIP_NG=ON` 可走 minizip-ng/DEFLATE。

## 核心编辑模型

编辑基本单位是 OpenXML part，不是完整 workbook 对象图。已有文件编辑不是
streaming writer 的附属补丁；它应通过 PackageReader / PackageEditor / EditPlan /
PackageWriter 形成独立 Patch 管线。

```text
读取 package
-> 建立 part index
-> 生成 EditPlan / dependency analysis
-> 标记修改过的 part
-> 未修改 part 原样复制
-> 修改 part 重新生成
-> 写出 package
```

这套模型的目标是保留 FastXLSX 尚不了解的 Excel 结构，包括图表、图片、宏和
未知扩展；当前不能宣称完整图片、VBA 或 table 读写/编辑支持。

单个 sheet 编辑也必须先判断联动 part。常见影响范围包括：

- `xl/worksheets/sheetN.xml`
- `xl/worksheets/_rels/sheetN.xml.rels`
- `xl/sharedStrings.xml`
- `xl/styles.xml`
- `xl/tables/tableN.xml`
- `xl/workbook.xml` / defined names / calcPr
- `xl/calcChain.xml`
- drawings、charts、pivot caches、VBA 和未知扩展 part

默认策略应保守：未知或未修改 part 原样复制；改数据后可设置 fullCalcOnLoad，并对
`calcChain.xml` 采用删除、重建或显式保留策略。涉及 sheet rename/delete/move、table
resize、chart reference、defined names 或跨 sheet 公式引用时，必须有明确
ReferencePolicy，不能静默破坏联动。

图片关系链至少包括：

```text
worksheet.xml drawing reference
-> xl/worksheets/_rels/sheetN.xml.rels
-> xl/drawings/drawingN.xml
-> xl/drawings/_rels/drawingN.xml.rels
-> xl/media/imageN.{png,jpeg}
-> [Content_Types].xml default/override
```

`stb` 只能证明图片 bytes 可解码或可读取尺寸；当前
`WorksheetWriter::add_image()` 另有 new-workbook drawing/package 基础切片，但不证明
existing-file image passthrough、drawing 编辑或 package preservation。
修改 relationships 时必须同步 content types 和 target part。

## DOM 边界

允许局部 DOM 的小型 XML part：

- `workbook.xml`
- workbook relationships
- `[Content_Types].xml`
- `docProps/*.xml`
- 较小的 `styles.xml`
- 规划中的小型 table、drawing、comments part

禁止 DOM：

- 大型 `worksheet.xml`
- 大型 `sharedStrings.xml`
- 批量数据写入路径
- 大型模板填充路径

## 设计锚点

当前内部基础，不是 public API：

- `PartName`
- `RelationshipSet`
- `ContentTypesManifest`
- `PackageManifest`
- `PartWriteMode`
- `PackagePart`
- `ContentTypeRegistry`
- `PartIndex`
- `RelationshipGraph`
- `EditPlan`
- `DependencyAnalyzer`
- `ReferencePolicy`
- `PartRewritePlanner`
- `PackageManifest::set_part_write_mode`
- `PackageManifest::mark_part_dirty`
- `PackageManifest::mark_part_generated`
- `make_minimal_workbook_manifest`
- `serialize_content_types`
- `serialize_relationships`

这些是文档设计名或尚未完整落地的 public/production 模块，使用前先确认源码和任务阶段：

- public / production `PackageEditor`
- `PackageWriter`
- `WorksheetRewriter`
- `OptionalDomDocument`
- `TemplateEditor`

## 推荐流程

1. 确认本次编辑影响哪些 XLSX parts，并生成 EditPlan。
2. 对每个 part 分类：原样复制、流式重写、局部 DOM 重写。
3. 同步维护 relationships、content types、sharedStrings、styles 和 calc metadata。
4. 大型 worksheet 走 event reader -> transformer -> stream writer。
5. 未知和未修改 part 尽量 byte-preserved。
6. 对 sheet 联动采用保守 ReferencePolicy：能更新才更新，不能更新则保留、触发重算或显式失败。
7. 输出后做 package 结构和打开兼容性验证。

## 本轮计划边界

- OPC edit plan：基础。内部 manifest、PartIndex、RelationshipGraph、content type
  registry 和 write-mode metadata 可作为规划入口。
- 当前默认推进线：只从 `docs/TASK_BREAKDOWN.md` active queue 选择任务，先校准
  public facade、命名、`CellView` / `Cell` / `CellValue` 边界和 internal/public
  分界；再推进窄 Patch MVP、preservation fixture、dependency policy 和后续
  In-memory 子任务。writer/backend/sharedStrings/benchmark 可继续并行硬化，但不应阻塞
  `PackageReader`、`PackageEditor`、`EditPlan`、`DependencyAnalyzer`、`ReferencePolicy`
  和 preservation fixture 的设计，也不能绕过 active queue 直接扩大 public Patch API。
- EditPlan / DependencyAnalyzer / ReferencePolicy / PartRewritePlanner：基础。
  它们是内部 Patch 计划元数据，只能表达 copy-original、目标 part rewrite、
  registered-part removal audit、worksheet dependency notes、沿已知 internal worksheet relationship target 的保守遍历
  （例如 worksheet → table 和 worksheet → drawing → image/chart）、fullCalcOnLoad 和 calcChain 计划语义；
  当前回归还覆盖递归到 drawing-owned `.rels` 后的 external、URI-qualified、invalid
  和 unresolved relationship target 审计 note；
  也能记录非 part package-entry rewrite/omission/preserved copy-original 审计项，例如
  `[Content_Types].xml`、package `_rels/.rels`、workbook `.rels`、removed calcChain owner
  `.rels`、present preserved root-level `_rels/foo.xml.rels`、present preserved
  worksheet/drawing `.rels` 和 present preserved calcChain owner `.rels`；
  external hyperlink target 不会被当成 package part，但会记录 external target 审计 note；
  带 query/fragment 的 internal relationship target 会留下 URI-qualified 审计 note，并在
  base target 解析到已注册 package part 时保守纳入 dependency summary；
  以 `/` 开头的 absolute internal target 会按 package part path 做 normalization；
  percent-encoded internal relationship target 会先解码 `%XX` 后再做 part-name
  normalization，已注册的 decoded target 会纳入 dependency summary；malformed percent
  escape 或解码后非法的 target 仍走 invalid-target 审计路径；relationship
  target 逃出 package root 或解析到内部路径但当前 manifest 未注册时，会留下 package
  structure review note，不虚构 package part；这些 relationship target 审计 note 和结构化
  `RelationshipTargetAudit` 会携带 owner part、relationship id、relationship type、原始 target，
  以及可用时的 normalized base target part，并会传播到 worksheet `EditPlan` 和
  existing-file `PackageEditor::edit_plan()`，方便 Patch 审计但不代表 target validation 或 repair；
  `EditPlan` 会对完全相同的 audit note 去重，并按 owner/id/type/raw target/normalized
  target upsert 重复 `RelationshipTargetAudit`，避免重复 linked worksheet rewrite 堆积同一
  结构化 relationship target audit；这只是内部 Patch audit 状态卫生，不是
  relationship validation、repair 或 pruning；
  未知 relationship type 只要 target normalize 后命中已注册 internal part，也会被保守纳入
  dependency summary / copy-original reason；这只是保守依赖审计，不代表理解或编辑该
  custom relationship 语义；
  relationship-driven dependency reason 会携带 relationship id、relationship type 和 normalized target
  part path；workbook part 的 dependency reason 会显式携带
  calcPr / definedNames review 上下文；copy-original entries 可带
  dependency reason 以便审计；显式 part removal 也只是不带 relationship pruning 的
  removed-part audit；当前 `plan_worksheet_stream_rewrite()` 在 `CalcChainAction::Remove`
  下会直接产出 stale calcChain removed-part audit，worksheet replacement 只消费该 planner 输出。
  当前不能当作已实现 public API、drawing/image/chart/table 编辑
  或 preservation 管线。
  内部 `DependencyAnalysis`、worksheet `EditPlan` 和 existing-file
  `PackageEditor::edit_plan()` 还保留结构化 `RelationshipTargetAudit` 列表，以及
  relationship-derived `PartDependency` 的 owner part、relationship id/type 和原始 target
  字段；relationship-derived copy-original `EditPlanEntry` 也会保留这些字段作为内部
  Patch 审计 metadata，workbook/sharedStrings/styles 等静态依赖仍只带 reason 文本；
  这些只是 Patch 审计元数据。
- 基础 docProps 输出/Patch：基础。public `DocumentProperties` 仍只是新建 package
  的 core/app 小型 XML part 配置；内部 `PackageEditor` 另有 core/app docProps
  generated-small-XML 窄切片，可新增/替换既有 package 的 `docProps/core.xml` 和
  `docProps/app.xml`，包括两者都缺失的输入，并同步 package rels / content types。它不是
  `docProps/custom.xml` 创建/编辑、完整 document properties API 或 public existing-file editing；
  当前只新增 preservation-only 回归，证明已有 custom properties part 及其 package
  relationship / content type override 在 core/app docProps rewrite 下保留。
- Package read/copy/write：基础。当前已有内部 `PackageReader`
  ZIP entry reader、stored-entry 与 opt-in DEFLATE payload CRC validation、owner-missing source-owned `.rels`
  rejection（包括根级 `_rels/foo.xml.rels` owner relationships）、
  conflicting content type default/override rejection、
  duplicate relationship id rejection within one `.rels` owner、
  content-types/relationships ingestion、unknown extension owner `.rels`
  reader-only `RelationshipGraph` 挂回覆盖、内部 `PackageEditor`
  copy/replace 基础、core/app docProps 小切片、worksheet replacement
  calcChain-remove/fullCalcOnLoad 小切片、
  EditPlan package-entry audit、docProps relationship-conflict no-state-pollution
  regression、exact/path-equivalent source-overwrite rejection guard、
  empty-output / missing-parent / non-directory-parent / existing-directory output rejection guard、
  malformed workbook metadata / workbook calc metadata、invalid replacement、
  metadata-entry replacement 和 invalid removal no-state-pollution regression
  （含 structured payload/removal/calc-policy snapshots）、
  `ReferencePolicy` narrow regression coverage 和
  opt-in minizip package output backend，但仍需要 sharedStrings/styles/tables/drawings/
  defined-name dependency sync、existing-file writer hardening 和 broader preservation 测试。
- 保真验证：基础。当前已有 unknown entry bytes copy/replace 结构测试，以及 worksheet
  replacement 下 worksheet `.rels`、drawing、drawing `.rels`、media、chart、table、
  untouched `xl/sharedStrings.xml`、sharedStrings owner `.rels`、untouched `xl/styles.xml`、VBA 和可达 unknown extension
  part 及其 owner `.rels` byte-preservation fixture，包括替换后的 worksheet XML 省略源 `<drawing>` /
  `<tableParts>` 引用时仍保留源关系和 linked parts；以及 workbook `definedNames`
  保留回归；`ReferencePolicyAction::Fail`
  还覆盖 linked worksheet rewrite 拒绝后不污染 edit plan entries / notes 与 manifest write-mode，
  以及已有 ordinary workbook replacement（含后续 inbound-linked drawing removal fail）
  和 queued core/app docProps helper 排队后的失败保留（均含 aggregate
  `planned_output()` 快照）；
  inbound-linked drawing removal fail 还覆盖 aggregate `planned_output()`、payload audit、
  removal audit 与 calc policy 快照保持不污染；malformed workbook
  metadata rewrite 预检失败和缺少 `xl/workbook.xml` 的 worksheet rewrite 前置失败也保持
  edit plan entries / notes、manifest / copied output 不污染；这些失败路径现在也覆盖
  `relationship_target_audits()` 和 `worksheet_relationship_reference_audits()` 不追加
  stale 结构化 relationship target / worksheet reference audit。仍不能据此宣称完整图片/图表/VBA
  passthrough 或 broad safe editing。
- 不能因为有 write-mode metadata 就宣称已有 XLSX 编辑、图片、VBA、table 或 chart
  支持。
- 不能因为 `stb` 可用或 `WorksheetWriter::add_image()` 存在就宣称已有 XLSX image
  passthrough、existing-file 图片插入或 drawing 编辑。

## 高风险区域

- 从已知 part 全量重建 package 会破坏未知内容。
- 修改 relationship 但不更新对应 part/content type 会生成无效 package。
- 大型 worksheet 或 sharedStrings 使用局部 DOM 会违反架构。
- 模板替换不能退化为完整 worksheet 随机访问模型。

## 验证

- 最小新建 XLSX：检查 package entries、`[Content_Types].xml`、relationships、
  workbook、worksheet、基础 `sheetData`。
- 编辑已有 XLSX：比较前后 package，确认未修改 part 被保留。
- 可用时验证 Excel / WPS / LibreOffice 能打开输出。
- 结构异常时，用 Excel / `openpyxl` / `XlsxWriter` 生成语义参考文件，拆包后比较
  content types、relationships、workbook、worksheet、shared strings、styles
  和相关 object part 的 XML 语义。
- part index 和 relationship graph 已有 `fastxlsx.opc` 回归测试；新增 reader/writer
  或对象功能时继续扩展这些测试。
