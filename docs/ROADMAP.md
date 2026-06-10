# 路线图

本路线图描述能力层级，不表示所有工作必须严格按编号线性完成。当前推进策略是：
在继续硬化新建 workbook 和 streaming writer 的同时，把 Phase 4 的已有文件编辑
能力前置为主线；Phase 5 的复杂对象继续保留已落地的 streaming-only 新建文件切片，
但新的对象广度不应绕过 Patch / preservation 基础。

## Phase 0：项目初始化

- 建立项目名称、定位和文档。
- 固定共享 OpenXML/OPC 底座与 Streaming / Patch / In-memory 三条 API 路径。
- 固定流式主线、Patch 编辑主线与 DOM 编辑边界。
- 明确性能目标。
- 固定 C++ 与跨语言 XLSX 库参考边界：`OpenXLSX`、`xlnt`、`libxlsxwriter`、
  `openpyxl`、`XlsxWriter`、Apache POI、`ClosedXML`、`EPPlus`、`ExcelJS`、
  `Excelize`、`rust_xlsxwriter`、`OpenSpout`、OpenXML SDK 等只作为经验、
  API、兼容性或 benchmark 参考，不作为无边界照搬对象。
- 固定第一阶段依赖边界。

## Phase 1：最小可写 XLSX

目标：生成 Excel 可打开的 `.xlsx`。

功能：

- workbook。
- worksheet。
- content types。
- relationships。
- 基础 sheetData。
- 数字、字符串、布尔值。
- ZIP package 输出。

当前 package writer 边界：

- 默认 ZIP backend 使用内部 stored ZIP writer。
- `FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建使用 `minizip-ng[core,zlib]` DEFLATE backend。
- 无 Zip64。
- 无真实 package streaming。
- `zlib-ng` / `pugixml` 尚未被当前源码使用。

Phase 1 后续依赖工作：

- minizip-ng backend 默认化前的 CI/cache/release packaging 验证。
- 压缩等级配置、Zip64 策略和真正 package streaming。
- `pugixml` 用于小型 XML part 编辑能力。

验收：

- Excel / WPS / LibreOffice 可打开。
- 生成文件结构符合 OpenXML 基本要求。
- 本机有 Excel 时，必须用 Excel 打开关键样例做可视化验证。
- 结构异常时，必须拆包对比 FastXLSX 输出与 Excel 或 Python XLSX 库生成的参考文件。
- public API 需要有文档注释，并写明该 API 是否属于 streaming 路径。

## Phase 2：高性能流式写入

目标：建立比 `OpenXLSX` DOM 主路径更强的大数据写入能力。

功能：

- Row iterator API。
- 流式 worksheet.xml。
- `Expat` 接入大型 XML 流式读取。
- inlineStr / sharedStrings 策略。
- 数字和日期快速编码。
- 压缩等级配置。
- 性能基准。
- 对比 `OpenXLSX`、`xlnt` streaming writer 和旧 `FastExcel`。

验收：

- 1,000 万 cells 写入稳定。
- 内存占用可控。
- API 设计必须与性能目标对齐，不能为了易用性迫使大数据路径持有完整 worksheet。
- 性能测试必须记录数据规模、压缩等级、字符串策略、耗时、峰值内存和输出文件大小。
- Phase 2 的性能工作不能替代 Phase 4 编辑能力；大文件编辑应通过 Patch / streaming
  rewrite 设计，而不是把 writer 性能当成唯一产品定位。

## Phase 3：OpenXLSX 高频功能

功能：

- 样式。
- 列宽、行高。
- 合并单元格。
- 冻结窗格。
- 自动筛选。
- 公式写入。
- 文档属性。
- 命名区域。

约束：

- API 体验可以参考 `OpenXLSX`，但实现不能继承 `OpenXLSX` 的完整 worksheet DOM 主路径。
- 便利 API 必须写明适用范围；只适合小文件的 API 需要标记为 in-memory 路径。
- public API 必须补文档注释，说明模式、内存行为、随机访问限制和性能注意事项。
- 小文件随机编辑能力应作为独立 In-memory 路径规划，不能让它成为大数据默认路径。

## Phase 4：编辑已有 XLSX

当前状态提示：本 roadmap 描述目标能力，不等同当前实现。截至当前任务计划，
`PartIndex` / `RelationshipGraph` / internal `EditPlan` / `DependencyAnalyzer` /
`ReferencePolicy` / `PartRewritePlanner` 属于 internal groundwork；
`PackageReader` 当前有 internal ZIP entry reader 和 content-types /
relationships ingestion 基础，可读 stored/no-compression entries；在
`FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建下还可读 DEFLATE entries。读取 entry
时会校验解压后 payload CRC，拒绝非法 ZIP entry name（绝对路径、尾部斜杠、
反斜杠、query/fragment components、空段、dot 段或 parent 段）、local header
CRC/method/name/size mismatch、encrypted flags、data descriptor entries、Zip64 和损坏 metadata 或 payload bytes，也会拒绝 owner part 缺失的
source-owned `.rels`，包括根级
`_rels/foo.xml.rels` owner relationship entry；冲突 content type default / override
和同一 `.rels` owner 内重复的 relationship id 会在 metadata ingestion 阶段被拒绝，
metadata attributes 必须未命名空间（namespace declarations 除外），namespaced
metadata attribute decoy 会在 `PackageReader::open()` 阶段失败，
未命名空间 metadata attributes 不得重复，
非 whitespace metadata text 会失败，
start/end tag QName mismatch 会失败，
并且 `[Content_Types].xml` / `.rels` 的第一个真实 XML 元素必须分别是
`Types` / `Relationships`，只 ingest root 的 direct-child
`Default` / `Override` / `Relationship` 元素，嵌套 decoy root 和嵌套 declaration
decoy 会被拒绝；这不是 content-type 或 relationship repair；
reader-only 回归还直接覆盖
unknown extension owner `.rels` metadata ingestion 和 `RelationshipGraph` 挂回；
internal `PackageEditor` 当前有 stored-package
copy/replace 基础，可替换一个已存在 part 并复制未改 entries，且 `save_as()`
拒绝写回覆盖 exact 或 path-equivalent 的 source package 路径，也会在 materialize
output entries 前拒绝空路径、缺失父目录、非目录父路径或已存在目录作为输出路径，因为当前
reader-backed copy 路径不是 atomic in-place editing；当前回归还验证这些拒绝
不污染已排队 part replacement，也不污染已排队 worksheet replacement 的
`fullCalcOnLoad` / calcChain removal / package-entry audit / planned output 状态，
后续安全 `save_as()` 仍会输出 queued rewrite、按计划省略 calcChain 并保留
untouched / unknown bytes；当前还可生成/替换
既有 package 的 core/app docProps 小型 XML part，必要时同步 package rels 和
content types；该 docProps helper 的回归还覆盖同步 package rels / content types
时保留已有 `docProps/custom.xml`、custom-properties package relationship、
custom properties content type override 和 unknown bytes，但不编辑 custom
properties；当前还可在替换既有 worksheet part 时同步 calcChain remove、workbook
fullCalcOnLoad、workbook rels 和 content types，并已有 linked-object fixture 证明 worksheet `.rels`、drawing、
drawing `.rels`、media、chart、table、untouched `xl/sharedStrings.xml`、
sharedStrings owner `.rels`、untouched `xl/styles.xml`、VBA 和可达 unknown extension
part 及其 owner `.rels` 在该窄路径下原样保留，
包括替换后的 worksheet XML 省略源 `<drawing>` / `<tableParts>` 引用时仍保留；
当前还新增 internal `replace_worksheet_sheet_data()` helper，可只替换已有
worksheet XML 的 `<sheetData>` / `<sheetData/>` 并保留同一 worksheet part 的外围
metadata，再复用 worksheet replacement 的 calcChain/fullCalcOnLoad 与 preservation
行为；成功替换后会用内部 `EditPlan` notes 标记保留的 worksheet-local
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
package-entry audit、calc policy、planned output 或输出 bytes，并保留 unknown
extension bytes；成功路径也会在 EditPlan/output-plan note 和 worksheet part
reason 中暴露 bounded local rewrite 边界，不能写成大文件低内存 streaming
worksheet transformer；
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
当前结构测试还覆盖先排队 worksheet replacement 再执行 sheetData patch 时，
helper 会基于当前 planned worksheet bytes 替换，覆盖 queued worksheet 中普通
`<sheetData>` 和 self-closing `<sheetData/>` 两种形态，保留 queued wrapper
metadata，不会把 source-only worksheet metadata 复活；
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
当前还覆盖内部按 sheet name 的完整 worksheet replacement 和 `sheetData` patch
入口：两者都通过 `PackageReader` workbook sheet catalog resolver 定位已有
worksheet part；resolver 会先验证 package `_rels/.rels` 中存在且仅存在一个
internal `officeDocument` relationship，当前窄实现只接受解析到 `/xl/workbook.xml`
的 target；相对、绝对和 dot-segment package target（例如 `xl/./workbook.xml`）
都从 package root 解析，且不会把 package root 建模成真实 `PartName`；
缺失、重复、external、带 query/fragment 或非固定 target 会在 lookup
阶段失败；resolver 还要求 sheet relationship id 使用 officeDocument relationships
XML namespace（可用非 `r` 前缀，普通 `id` 或错误 namespace 的 `id` 会被拒绝），
要求 `name` / `sheetId` 是未命名空间的 workbook sheet 属性，
并且只读取 workbook `<sheets>` 目录的直接 `<sheet>` 子元素，忽略目录外或嵌套在
非 sheet 目录子元素下的同名标签，
当前 reader/source-catalog by-name Patch 回归还覆盖 workbook-owned 绝对 worksheet target
（例如 `/xl/worksheets/sheetN.xml`）和 dot-segment 相对 target
（例如 `./worksheets/../worksheets/sheetN.xml`）解析到已注册 worksheet part，并被
完整 worksheet by-name replacement 与 by-name `sheetData` patch 两条 source-catalog
路径复用；输出后保留 worksheet relationship 的 target 字面值和 unknown extension
bytes；当 calcChain cleanup 需要重写 workbook `.rels` 时，
这不是整份 workbook relationships 字节保真，也不是任意 workbook 位置、
relationship repair、relationship pruning 或 sheet catalog mutation。source workbook
catalog 查找还会在状态变更前拒绝 workbook `.rels` 中缺失的 sheet relationship id，
以及指向未注册 worksheet part 的 worksheet relationship；这只是目标定位校验，
不是 relationship repair、pruning 或 orphan cleanup。随后再分别委托既有 worksheet replacement 或 part-level `sheetData`
replacement 路径；缺失或重复 sheet name 失败不污染 EditPlan、manifest、package-entry
audit、calc policy 或输出 bytes；invalid package `officeDocument` entrypoint
也会在 by-name Patch 前失败且不污染状态。这不是 sheet rename/delete、sheet catalog
mutation、任意 workbook part-location 支持、随机 cell 编辑或 public API；
当前还覆盖一个真实 FastXLSX writer 产物的 internal Patch roundtrip：
`WorkbookWriter` 生成两张 sheet 的 source package，`PackageReader` 解析 writer
workbook sheet catalog，`PackageEditor::replace_worksheet_sheet_data_by_name()`
只替换目标 sheet 的 `<sheetData>`，`save_as()` 后再由 `PackageReader` 重读；
回归验证 untouched worksheet、`[Content_Types].xml`、package `_rels/.rels`、
workbook `.rels` 和 core/app docProps bytes 原样保留，并验证 writer 生成的
worksheet XML declaration/prolog 会在 patch 后按原前缀保留，且输出 `<worksheet>`
根紧随该 prolog 通过最终 worksheet 根校验；
source 现在也覆盖 writer 生成的 `xl/sharedStrings.xml` 与 `xl/styles.xml`，验证这两个 part、
对应 content type override 和 workbook relationships 原样保留，并把 replacement
`<sheetData>` 中的 shared string index / style id references 暴露为结构化
`WorksheetPayloadDependencyAudit`。workbook XML 设置 `fullCalcOnLoad="1"`，
且源包没有 `xl/calcChain.xml` 时不会凭空创建 calcChain。这仍是 internal
Patch MVP / template-fill 证明，不是 public `PackageEditor`、随机 cell 编辑、
sharedStrings 索引迁移、style id 迁移、styles 合并、table/drawing 语义同步或
大文件 streaming worksheet transformer；
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
relationship repair 或 public API；
如果同一 edit 中 `/xl/workbook.xml` 已被显式 planned removal 移除，两个 by-name
helper 会在解析 sheet catalog 前失败并保留既有 removal / owner `.rels` omission
状态，不回退 source workbook catalog 或复活 workbook metadata；
当前还落地了内部 `PackageEditor::rename_sheet_catalog_entry()` workbook sheet
catalog name rewrite：它只修改当前 planned `/xl/workbook.xml` 中直接
`<sheets><sheet name="...">` 的 `name` attribute，使用 `LocalDomRewrite`
写回 workbook part，并保持 worksheet parts、workbook `.rels`、content types、
calcChain 和 unknown entries 原样复制。它可以基于 planned workbook catalog
继续改名；planned workbook XML 路径的内部 `planned_output()` 快照也暴露最终
workbook `LocalDomRewrite`、preserved content types / workbook `.rels` / worksheet /
calcChain / unknown entry，以及 structured sheet catalog / definedNames audit；失败路径还覆盖坏 planned workbook catalog：planned sheet relationship id 在 workbook `.rels` 中缺失，
或 planned worksheet relationship 指向未注册 worksheet part 时，会在 rename 状态变更前失败并保留 queued workbook replacement、EditPlan / audit、manifest、calc policy、package-entry audit 和输出 bytes。其他失败路径覆盖缺失旧名、精确或 ASCII 大小写不敏感重复新名、非法新名、
`ReferencePolicyAction::Fail` 遇到直接 `definedNames`、以及 workbook planned removal，不污染 EditPlan、manifest、
package-entry audit、calc policy 或输出 bytes。它不同步 definedNames、公式、
tables、drawings、charts、hyperlinks、relationship targets、sharedStrings、
styles 或 calcChain，不能列为完整 sheet rename/add/delete、relationship repair
或 public API；
完整 worksheet replacement payload 现在会在 Patch 状态变更前做最小根元素校验：
replacement XML 必须是单个 `<worksheet>` 根元素（按 local-name 接受前缀形式，
且允许 XML declaration、注释和处理指令位于根元素前），
否则失败不污染 EditPlan、manifest、package-entry audit、calc policy 或输出 bytes；
任意非 prolog 元素或文本位于根元素前仍会被拒绝；这不是 XML schema validation、
namespace repair 或 XML repair；
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
replacement `<sheetData>` 自身包含 shared string
index、style id reference 或公式 cell 时，也只会追加 audit notes，提示 caller 复核
`xl/sharedStrings.xml`、`xl/styles.xml` 和 calc metadata / calcChain policy；当前结构测试
还验证输出包中保留的 worksheet `.rels` legacyDrawing `rId7`
到 `../drawings/vmlDrawing1.vml#shape1` 可由 `PackageReader` /
`RelationshipGraph` 重读，这仍是 preservation 证据，不是 VML/drawing 编辑；不迁移
sharedStrings 索引、不合并 styles、不计算公式、不重建 calcChain；这不是 public API、随机 cell 编辑、dataValidations/conditionalFormatting/hyperlinks/table/drawing 语义同步、sharedStrings/styles 迁移或
大文件低内存 transformer；
invalid/malformed replacement XML、source worksheet 缺失 `sheetData`，以及 source worksheet
`<sheetData>` 起始标签存在但闭合标签损坏/缺失时，失败不污染 EditPlan、manifest、
package-entry audit、calc policy 或输出 bytes，这不是 XML repair；
另有 registered comments part 小 fixture 证明 worksheet rewrite 会把
`xl/comments/comment1.xml` 和 source worksheet `.rels` 作为 copy-original
preservation 处理，保留 comments content type override，并可由 `PackageReader` /
`RelationshipGraph` 重读；
另有 threaded comments / persons 小 fixture 证明 worksheet rewrite 会把
`xl/threadedComments/threadedComment1.xml`、`xl/persons/person.xml`、source worksheet
`.rels` 和 workbook `.rels` 作为 copy-original preservation 处理，并可由
`PackageReader` / `RelationshipGraph` 重读；这不是 comments / threaded comments
编辑、notes UI、relationship repair、orphan cleanup 或 public API；
同一 threaded comments / persons 小 fixture 还覆盖 ordinary
`replace_part("/xl/threadedComments/threadedComment1.xml", ...)` 和显式移除：
replacement 只重写 threaded comments XML，保留 legacy comments、persons、
worksheet `.rels` 中的 legacy/threaded inbound relationships、workbook `.rels`
中的 persons relationship、content type overrides 和 unknown entry；removal
输出省略 threaded comments part 并移除其 content type override，但保留 worksheet
`.rels` 中指向缺失 threaded comments part 的 inbound relationship、persons part /
workbook relationship、legacy comments 和 unknown entry。这不是 threaded comments
model mutation、persons/schema repair、relationship pruning/repair、orphan cleanup、
notes UI 或 public API；
同一 threaded comments / persons 小 fixture 还覆盖 ordinary
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
`xl/pivotTables/pivotTable1.xml`、`xl/pivotCache/pivotCacheDefinition1.xml`、
`xl/pivotCache/pivotCacheRecords1.xml`、source worksheet `.rels`、
pivot table owner `.rels`、pivot cache definition owner `.rels` 和 workbook `.rels`
作为 copy-original preservation 处理，并可由 `PackageReader` / `RelationshipGraph`
重读；这不是 pivot table 编辑、pivot cache rebuild、relationship repair、
orphan cleanup 或 public API；
该 worksheet rewrite 路径的内部 `planned_output()` 现在也暴露 fullCalcOnLoad /
`CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook `LocalDomRewrite`、
package/workbook/worksheet `.rels` copy-original、pivot table / pivot cache
definition / pivot cache records relationship context、content types 和 unknown entry
copy-original，且确认不凭空创建 records owner `.rels`；这仍只是 Patch audit 快照，
不是 pivot cache rebuild、records refresh、relationship repair/pruning、orphan cleanup
或 public API；
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
刷新、relationship pruning/repair、orphan cleanup、owner `.rels` repair 或 public API；
同一路径现在还覆盖 pivot table remove-then-ordinary-replace 和
ordinary-replace-then-remove ordering：后续 replacement 会恢复 active pivot table
part、清理 stale removed-part / removed owner `.rels` audit、恢复 owner `.rels`
copy-original audit，并让 `[Content_Types].xml` 回到 source/copy-original audit；
后续 removal 会清理 active replacement、记录 removed-part / removed owner `.rels`
audit、输出省略 pivot table part 和 owner `.rels`、移除 pivot table content type
override，并保留 worksheet `.rels` 中指向缺失 part 的 inbound relationship、
workbook pivot cache metadata、pivot cache definition / records 链和 unknown entry。
这不是事务式 undo、pivot table 语义合并、pivot cache rebuild、
relationship pruning/repair、content type repair、orphan cleanup 或 public API；
内部 `planned_output()` 快照还覆盖该 remove-then-replace restore 状态：
暴露 active pivot table `LocalDomRewrite` entry、pivot table owner `.rels`
copy-original `SourceRelationships` audit、content types copy-original audit，以及
preserved package/worksheet/workbook relationships、pivot cache definition / records 链
和 unknown entry；该快照也覆盖 replace-then-remove final-removal 状态：暴露
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
同一路径现在还覆盖 remove-then-ordinary-replace 和 ordinary-replace-then-remove
ordering：后续 replacement 会恢复 active pivot cache definition、清理 stale
removed-part / removed owner `.rels` audit、恢复 owner `.rels` copy-original audit，并让
`[Content_Types].xml` 回到 source/copy-original audit；后续 removal 会清理 active
replacement、记录 removed-part / removed owner `.rels` audit、输出省略 cache definition
part 和 owner `.rels`，并保留 workbook / pivot table inbound relationships、pivot table、
cache records、worksheet 和 unknown entry。这不是事务式 undo、pivot cache 语义合并、
relationship pruning/repair、content type repair、orphan cleanup 或 public API；
内部 `planned_output()` 快照还覆盖该 remove-then-replace restore 状态：暴露
active pivot cache definition `LocalDomRewrite` entry、owner `.rels` copy-original
`SourceRelationships` audit、content types copy-original audit，以及 preserved
package/worksheet/workbook relationships、pivot table/cache records/unknown entries；
该快照也覆盖 replace-then-remove final-removal 状态：暴露 omitted cache definition
part、omitted owner `.rels`、workbook / pivot table inbound pivotCacheDefinition
relationship audit、content types rewrite、preserved workbook/worksheet/pivot
table/cache records/unknown entries；这仍只是 Patch audit，不是 pivot cache rebuild、
cache-record refresh、relationship pruning/repair、content type repair、orphan cleanup
或 public API；
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
content type repair、orphan cleanup 或 public API；
另有 workbook external links 小 fixture 证明 worksheet rewrite 在重写
`xl/workbook.xml` calc metadata 时，仍会保留 workbook `<externalReferences>`、
workbook `.rels` 中的 externalLink relationship、`xl/externalLinks/externalLink1.xml`、
externalLink owner `.rels`、external `externalLinkPath` target、content type override
和 unknown entry，并可由 `PackageReader` / `RelationshipGraph` 重读；这不是
external links 编辑、外部数据刷新、路径校验、relationship repair、orphan cleanup
或 public API；
该 worksheet rewrite 路径的内部 `planned_output()` 现在也暴露 fullCalcOnLoad /
`CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook `LocalDomRewrite`、
workbook `.rels` copy-original、externalLink part 与 owner `.rels` copy-original、
content types copy-original 和 unknown entry preservation，且不新增 relationship
target audit；这仍只是 Patch audit 快照，不是 external links 编辑或 relationship
repair；
同一 workbook external links 小 fixture 还覆盖 ordinary
`replace_part("/xl/externalLinks/externalLink1.xml", ...)` 和显式移除：
replacement 只重写 externalLink XML，保留 workbook `.rels` inbound externalLink
relationship、externalLink owner `.rels` 中的 external `externalLinkPath` target、
content type override、worksheet 和 unknown entry；removal 输出省略 externalLink
part 及其 owner `.rels`，移除 externalLink content type override，但保留 workbook
`<externalReferences>`、workbook `.rels` 中指向缺失 externalLink part 的 inbound
relationship、worksheet 和 unknown entry。这不是 external links 语义编辑、
外部数据刷新、路径校验、relationship pruning/repair、orphan cleanup、owner `.rels`
repair 或 public API；
同一路径还覆盖先显式移除 externalLink 再 ordinary replace，以及先 ordinary replace
再显式移除：前者清理 stale removed-part / removed owner `.rels` audit，恢复 active
externalLink part、owner `.rels` copy-original audit 和 source content types audit；
后者清理 active replacement，记录 removed-part / removed owner `.rels` audit，输出省略
externalLink part 和 owner `.rels`，并保留 workbook inbound relationship、worksheet 和
unknown entry。这不是事务式 undo、external links 语义合并、relationship pruning/repair、
content type repair、orphan cleanup 或 public API；
内部 `planned_output()` 快照还覆盖该 remove-then-replace restore 状态：
暴露 active externalLink `LocalDomRewrite` entry、externalLink owner `.rels`
copy-original `SourceRelationships` audit、content types copy-original audit，以及
preserved package/workbook relationships、workbook、worksheet 和 unknown entry；
该快照也覆盖 replace-then-remove final-removal 状态：暴露 omitted externalLink part、
omitted externalLink owner `.rels`、workbook inbound externalLink relationship audit、
content types rewrite、preserved package/workbook relationships、workbook、worksheet
和 unknown entry；这仍只是 Patch audit，不是 external links 语义编辑、外部数据刷新、
relationship pruning/repair、orphan cleanup 或 public API；
另有 custom XML 小 fixture 证明 worksheet rewrite 会保留 package `_rels/.rels`
中的 customXml relationship、`customXml/item1.xml`、custom XML item owner `.rels`、
`customXml/itemProps1.xml`、custom XML properties content type override 和 unknown
entry，并可由 `PackageReader` / `RelationshipGraph` 重读；这不是 custom XML
编辑、schema/data binding、relationship repair、orphan cleanup 或 public API；
该 worksheet rewrite 路径的内部 `planned_output()` 现在也暴露 fullCalcOnLoad /
`CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook `LocalDomRewrite`、
package relationships copy-original、custom XML item / item owner `.rels` /
properties part copy-original、content types copy-original 和 unknown entry preservation，
且不新增 relationship target audit、不凭空创建 properties owner `.rels`；这仍只是
Patch audit 快照，不是 custom XML 编辑、schema/data binding 或 relationship repair；
同一 custom XML 小 fixture 还覆盖 ordinary `replace_part("/customXml/item1.xml", ...)`：
只重写 custom XML item，保留 package `_rels/.rels` customXml inbound relationship、
custom XML item owner `.rels` / customXmlProps relationship、`customXml/itemProps1.xml`、
custom XML properties content type override、默认 XML content type 和 unknown entry
copy-original 基线，并可由 `PackageReader` / `RelationshipGraph` 重读；这不是
custom XML 语义编辑、schema/data binding、relationship repair、content type repair、
orphan cleanup 或 public API；
同一 custom XML 小 fixture 还覆盖显式移除 `customXml/item1.xml`：输出省略
custom XML item 及其 source-owned owner `.rels`、保留 package `_rels/.rels`
customXml inbound relationship、保留 `customXml/itemProps1.xml`、custom XML
properties content type override、默认 XML content type 和 unknown entry，且不重写
`[Content_Types].xml`；这不是 custom XML 删除语义、schema/data binding、
relationship pruning/repair、content type repair、orphan cleanup 或 public API；
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
pruning/repair、content type repair、orphan cleanup 或 public API；
同一 custom XML 小 fixture 还覆盖 `customXml/itemProps1.xml` properties part
的 ordinary replacement 和显式 removal：replacement 只重写 properties part，
保留 custom XML item、item owner `.rels` / customXmlProps inbound relationship、
package customXml relationship、properties content type override 和 unknown entry；
removal 输出省略 properties part、移除 properties content type override，但保留
custom XML item、item owner `.rels` 中指向缺失 properties part 的 inbound
customXmlProps relationship、package customXml relationship、默认 XML content type
和 unknown entry。这不是 custom XML properties 编辑、schema/data binding、
relationship pruning/repair、content type repair、orphan cleanup 或 public API；
内部 `planned_output()` 快照还覆盖 ordinary properties replacement 状态：暴露 active
properties part `LocalDomRewrite`、preserved content types / package relationships、
preserved custom XML item / item owner `.rels` / workbook / worksheet / unknown
entry，且不凭空创建 properties owner `.rels`。这仍只是 Patch audit，不是 custom XML
properties 语义编辑、schema/data binding、事务式 undo、relationship pruning/repair、
content type repair、orphan cleanup 或 public API；
同一路径顺序回归还覆盖 properties part 先显式移除再 ordinary replace，以及先
ordinary replace 再显式移除：前者清理 stale removed-part audit、恢复 active
properties part、恢复 properties content type override/content-types copy-original
audit，并继续保留 item owner `.rels`；后者清理 active replacement、记录
removed-part audit、输出省略 properties part、移除 properties content type override，
并继续保留 item owner `.rels` 中的 inbound customXmlProps relationship。这不是
事务式 undo、custom XML properties 语义合并、relationship pruning/repair、
content type repair、orphan cleanup 或 public API；
内部 `planned_output()` 快照还覆盖该 properties final-removal 状态：暴露 omitted
properties part、item-owned inbound customXmlProps relationship audit、content types
rewrite、preserved custom XML item / item owner `.rels` / package relationships /
workbook / worksheet / unknown entry，且不凭空创建 properties owner `.rels`。这仍只是
Patch audit，不是 custom XML properties 删除语义、relationship pruning/repair、
content type repair、orphan cleanup 或 public API；
内部 `planned_output()` 快照还覆盖该 properties restore 状态：暴露 active properties
part `LocalDomRewrite`、restored content types copy-original audit、preserved custom
XML item / item owner `.rels` / package relationships / workbook / worksheet /
unknown entry，且不凭空创建 properties owner `.rels`。这仍只是 Patch audit，不是
custom XML properties 语义合并、事务式 undo、relationship pruning/repair、
content type repair、orphan cleanup 或 public API；
同一 custom XML fixture 还覆盖跨路径顺序：先显式移除 custom XML item，再 ordinary
replace `customXml/itemProps1.xml` properties part。后续 properties replacement
只重写 properties payload，保留 removed custom XML item / removed owner `.rels`
audit，输出继续省略 custom XML item 和 item owner `.rels`，并保留 package customXml
inbound relationship、properties content type override、默认 XML content type 和 unknown
entry。这不是 custom XML dependency repair、relationship pruning/repair、
content type repair、orphan cleanup、事务式 undo 或 public API；
内部 `planned_output()` 快照还覆盖该跨路径状态：暴露 omitted custom XML item、
omitted source-owned owner `.rels`、package inbound customXml relationship audit、
active properties part local-DOM rewrite、preserved package relationships / content
types / workbook / worksheet / unknown entry，且不凭空创建 properties owner `.rels`。
这仍只是 Patch audit，不是 custom XML dependency repair、relationship pruning/repair、
content type repair、orphan cleanup、事务式 undo 或 public API；
反向跨路径顺序也有回归覆盖：先显式移除 properties part，再 ordinary replace
custom XML item。后续 item replacement 只重写 item payload，保留 removed properties
part audit / content-types rewrite，输出继续省略 properties part 和 properties content
type override，并保留 item owner `.rels` 中指向缺失 properties part 的
customXmlProps relationship、package customXml inbound relationship、默认 XML content type
和 unknown entry。这不是 custom XML dependency repair、relationship pruning/repair、
content type repair、orphan cleanup、事务式 undo 或 public API；
内部 `planned_output()` 快照还覆盖该反向跨路径状态：暴露 omitted properties part、
item-owned inbound customXmlProps relationship audit、content types rewrite、active
custom XML item local-DOM rewrite、preserved item owner `.rels` / package relationships /
workbook / worksheet / unknown entry，且不凭空创建 properties owner `.rels`。这仍只是
Patch audit，不是 custom XML dependency repair、relationship pruning/repair、
content type repair、orphan cleanup、事务式 undo 或 public API；
同一小 fixture 还覆盖 ordinary `replace_part("/xl/comments/comment1.xml", ...)`：
只重写 comments XML，保留 worksheet `.rels` inbound comments relationship、
comments content type override、workbook XML / workbook `.rels`、worksheet 和 unknown entry
copy-original 基线，且不凭空创建 comments owner `.rels`；这不是 comments model
mutation、threaded comments、notes UI、relationship repair、orphan cleanup 或 public API；
内部 `planned_output()` 快照还覆盖该 ordinary replacement 状态：暴露 active
comments part local-DOM rewrite、preserved content types / package relationships /
workbook / workbook `.rels` / worksheet / worksheet `.rels` / unknown entry，且不凭空创建
comments owner `.rels`。这仍只是 Patch audit，不是 comments model mutation、notes UI、
relationship repair、orphan cleanup 或 public API；
同一小 fixture 还覆盖显式移除 `xl/comments/comment1.xml`：输出省略 comments part、
移除 comments content type override、保留 worksheet `.rels` inbound comments
relationship，且不凭空创建 comments owner `.rels` omission；这不是 comments
删除语义、threaded comments、notes UI、relationship pruning/repair、orphan cleanup
或 public API；
同一小 fixture 还覆盖先显式移除再 ordinary `replace_part()` 的反向顺序：
后续 replacement 会恢复 active comments part、清理 stale removed-part audit、让
`[Content_Types].xml` 回到 source/copy-original audit、保留 inbound worksheet
`.rels`，且仍不凭空创建 comments owner `.rels`；这不是事务式 undo、comments
语义合并、relationship repair、orphan cleanup 或 public API；
内部 `planned_output()` 快照还覆盖该 remove-then-replace 状态：暴露 active
comments part local-DOM rewrite、content types copy-original audit、preserved
package/workbook/worksheet `.rels` 和 unknown entry，清空 output-plan removed_parts /
removed_package_entries 中的 stale removal 状态，且不凭空创建 comments owner
`.rels`。这仍只是 Patch audit，不是 comments undo、semantic merge、
relationship repair、orphan cleanup 或 public API；
同一小 fixture 还覆盖先 ordinary `replace_part()` 再显式移除的顺序：
后续 removal 会清理 active comments replacement、记录 removed-part audit、输出省略
comments part、移除 comments content type override、保留 inbound worksheet `.rels`，
且仍不凭空创建 comments owner `.rels`；这不是 comments 删除语义、事务式 undo、
relationship pruning/repair、orphan cleanup 或 public API；
内部 `planned_output()` 快照还覆盖该 replace-then-remove final-removal 状态：
暴露单个 omitted comments part / removed-part audit（target 为 comments part、
reason 保留 after replacement、inbound relationship 一条）、content types rewrite、
preserved package/workbook/worksheet `.rels`，且 removed_package_entries 为空、不凭空创建
comments owner `.rels`。这仍只是 Patch audit，不是 comments 删除语义、事务式 undo、
relationship pruning/repair、orphan cleanup 或 public API；
并验证 workbook `definedNames` 在 workbook metadata rewrite 路径下保留；
该路径还保留 source `[Content_Types].xml` defaults/overrides 形态，当前测试确认
移除 calcChain override 时不会把 PNG media default 提升成 image override；当前还覆盖
没有 `xl/calcChain.xml` payload 但残留 calcChain content type override 或 workbook
calcChain relationship 的 metadata-only 清理，不记录缺失 payload 的 removed-part
audit、不创建 calcChain payload，也不代表通用 metadata repair；当前还覆盖内部
`PackageEditor::request_full_calculation()` workbook calc metadata helper：只重写
`/xl/workbook.xml` 小型 metadata part 来设置 `fullCalcOnLoad="1"`，并按
`CalcChainAction::Remove/Preserve` 清理或保留 calcChain payload、content type、
workbook relationship 和 calcChain owner `.rels` 审计；Remove 路径还覆盖没有
`xl/calcChain.xml` payload 但残留 calcChain content type override 或 workbook
relationship 的 metadata-only cleanup，不创建 payload 或 removed-part audit；
`CalcChainAction::Rebuild`
仍未实现，失败不污染 edit plan、manifest、package-entry audit 或输出包。这不是
公式求值、calcChain rebuild、worksheet edit、public API 或通用 metadata repair；
该 helper 只更新 workbook 根元素的直接子级 `calcPr`，会保留 `extLst` /
custom extension 内的嵌套同名 decoy；没有直接子级时才在 workbook closing tag
前插入按根前缀命名的 `calcPr`，这不是 XML schema validation 或 namespace repair；
普通 part
replacement 现在显式拒绝 `[Content_Types].xml`、package `_rels/.rels` 和
source-owned `.rels` metadata entry 作为 ordinary part target，且 metadata-entry
replacement 拒绝回归覆盖 edit plan entries / notes、package-entry audit、calc
policy、manifest write-mode 和输出包字节不污染；docProps generated
parts 和 worksheet replacement 也会把 write-mode /
dirty / generated / preserve-original 状态同步到内部 manifest；worksheet replacement
还会把 workbook metadata rewrite 同步为 `LocalDomRewrite`，供 Patch 审计；
重复 ordinary part replacement 回归验证同一 part 再次替换时最终 bytes、write mode、
edit-plan reason、manifest state 和 preserved source-owned `.rels` audit 以上一次替换为准；
docProps generated-small-XML 后续被 ordinary replacement 覆盖的回归也验证输出 bytes、
EditPlan 和 manifest 采用最终 ordinary replacement，同时保留 content types /
package relationships 的 helper-managed audit；反向顺序回归也验证后调用的 docProps
metadata helper 会接管此前 ordinary replacement 或 explicit removal 的 core/app part，
清理 stale removal / omitted payload 状态，worksheet
replacement 删除 calcChain 时会压过此前 ordinary calcChain replacement，并接管此前
ordinary workbook replacement 以写入 helper-generated fullCalcOnLoad metadata；
这不恢复 docProps owner `.rels`，当前结构回归验证输出 omission 和 removed
package-entry audit，也不是事务式 undo；后续 ordinary workbook replacement
反过来也会保留既有 worksheet-rewrite `fullCalcOnLoad` / calcChain policy，且不会把已重写的
workbook `.rels` audit 降级为 copy-original；
当 worksheet replacement 显式使用 `CalcChainAction::Preserve` 时，此前 ordinary
calcChain replacement 会保持 active `LocalDomRewrite` 并作为最终 `xl/calcChain.xml`
payload 写出，同时保留 calcChain owner `.rels` copy-original audit；这不是 calcChain
rebuild、公式求值或 relationship repair；
当前还覆盖 prior ordinary workbook / calcChain replacement 后再调用 workbook-only
`request_full_calculation()` 的顺序组合：helper 会使用已排队 workbook XML，
保留其中非 calc workbook metadata，同时把 `fullCalcOnLoad` 规范为 `1` 并清理
calcChain payload/content type/workbook relationship；此前排队的 calcChain
replacement 不会在输出包复活，也不回退到 source workbook bytes；
若同一路径指定 `CalcChainAction::Preserve`，此前排队的 calcChain replacement 会保持为
active `LocalDomRewrite` 并作为最终 `xl/calcChain.xml` payload 写出，同时只保留
calcChain owner `.rels` 的 copy-original 审计；这不是 calcChain rebuild 或公式求值；
内部 `EditPlan` 现在还记录 `[Content_Types].xml`、package `_rels/.rels`、
workbook `.rels` rewrite、removed calcChain owner `.rels` omission，以及窄 worksheet
replacement 中被保留的 source-owned `.rels` copy-original audit 的 package-entry
审计项；这些审计项已有内部结构化分类，区分 content types、package relationships
和 source-owned relationships，且只有 source-owned `.rels` 携带 owner part；
kind 与 entry path 会在 EditPlan 入口校验，避免把 content types、package
relationships 或 owner-derived `.rels` 审计写到错误 entry；
root-level ordinary owner replacement 回归还验证 source-owned `.rels`
roundtrip 后仍可由 `PackageReader` / `RelationshipGraph` 挂回 owner part；reader-only
回归也验证 unknown extension owner `.rels` 在 editor roundtrip 前即可挂回 owner；
内部 `EditPlan` 还覆盖 part-level set/remove 互斥：已移除 part 后续重新 set 为
active entry 时会清理 stale removed-part audit，已有 relationship-derived entry
改成 rewrite/generate entry 时也会清理 stale relationship metadata；package-entry
set/remove 也有互斥回归，避免同一 metadata entry 同时存在 active 和 removed 审计；
`CalcChainAction::Preserve` 保留 stale calcChain 时也会记录
`xl/_rels/calcChain.xml.rels` copy-original audit；当 workbook metadata rewrite
只改写 `xl/workbook.xml` 而原样保留 `xl/_rels/workbook.xml.rels` 时，也会记录该
workbook `.rels` copy-original audit；
已有组合回归覆盖同一 edit 内 core/app docProps 生成和 worksheet replacement 的
relationship/content-type 状态合并、calcChain 删除、stale calcChain owner `.rels`
omission、workbook metadata rewrite、unknown entry 保留、exact/path-equivalent
source-overwrite rejection 与 empty-output / missing-parent / non-directory-parent / existing-directory output rejection；
malformed workbook metadata / workbook calc metadata preflight failure 和
invalid replacement failure 回归覆盖 edit plan、manifest write-mode、aggregate
`planned_output()` / legacy output-entry preview 和 copied output bytes 不污染；
no-op `PackageEditor::save_as()` roundtrip 回归还验证 linked-object fixture 中全部源
entries 的顺序、stored entry method / CRC / uncompressed size 和 bytes 保持一致，
初始计划保持 copy-original part entries 且没有 metadata package-entry side effect；
这只是未修改包 copy baseline；
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
的 removed-part inbound audit；
metadata entries 如 `[Content_Types].xml`、package `_rels/.rels` 和 source-owned `.rels`
不会暴露为 package part；这只是内部 Patch 审计可见性，不是 public editor API
或通用 metadata mutator；
ordinary single-part replacement 回归还验证目标 entry 原位重写时，其它源 entries
保持顺序、stored entry method / CRC / uncompressed size 和 bytes 一致；
这只是窄 part-level rewrite copy-original 证据；
DEFLATE source input 当前只承诺未修改 part 的解压后 payload 语义保留；minizip-enabled
PackageEditor 回归还覆盖 DEFLATE source 上 ordinary workbook replacement、
unknown extension target replacement、workbook calc metadata helper，以及 worksheet
replacement 下的 calcChain cleanup、linked payload preservation 和
unknown extension owner `.rels` 由输出 `PackageReader` / `RelationshipGraph` 重读；
不承诺保留源 ZIP compression method、timestamps、extra fields 或压缩字节；
linked-object fixture 上的 ordinary workbook replacement 回归还验证只重写
`xl/workbook.xml` 时，workbook `.rels` 作为 copy-original package-entry audit 记录，
worksheet、drawing、media、sharedStrings、styles、VBA、calcChain 和 unknown extension
等其它 entries 保持同一 copy-original 基线；
同一路径还覆盖 ordinary workbook replace-then-remove：后续 removal 清理 active
workbook replacement、记录 removed-part 和 workbook owner `.rels` omission audit，
输出省略 workbook part 及其 owner `.rels`、移除 workbook content type override，
但保留 package `_rels/.rels` 中指向缺失 workbook 的 officeDocument relationship
以及 worksheet/drawing/table/sharedStrings/styles/VBA/calcChain/unknown downstream parts；
这不是 workbook deletion semantics、sheet catalog sync、relationship/content type repair、
orphan cleanup、事务式 undo 或 public API；
linked-object fixture 上的 ordinary drawing replacement 回归还验证只重写
`xl/drawings/drawing1.xml` 时，drawing `.rels` 作为 copy-original package-entry audit
记录，chart、media 和 unknown extension 等其它 entries 保持同一 copy-original
基线；这不是 drawing/image/chart editing；
linked-object fixture 上的 ordinary unknown extension replacement 回归还验证只重写
`custom/opaque-extension.bin` 时，其 owner `.rels` 作为 copy-original package-entry
audit 原样保留，workbook、worksheet、drawing、chart 和 media entries 仍保持同一
copy-original 基线；这不是 unknown extension 语义编辑、custom relationship repair
或 public API；
对同一 unknown extension 的 repeated ordinary replacement 回归还验证最终 bytes、
manifest write-mode、edit-plan reason 和 owner `.rels` audit 会 upsert 到最后一次
替换状态，owner `.rels` 仍保持 copy-original，且不会产生 removed-part 或 removed
package-entry audit；这不是事务式编辑或 unknown extension 语义合并；
同一路径还覆盖 unknown extension 先显式移除再 ordinary replacement 的反向顺序：
后续 replacement 会恢复 active unknown extension part、清理 stale removed-part audit
和 stale removed owner `.rels` audit、恢复 owner `.rels` copy-original package-entry
audit、保留 worksheet `.rels` 中的 inbound unknown relationship、保留其它
linked/source entries，且不重写 `[Content_Types].xml`；这不是 unknown extension
语义合并、custom relationship repair、metadata repair、事务式 undo 或 public API；
同一路径现在还覆盖 unknown extension 先 ordinary replacement 再显式移除：
后续 removal 会清理 active replacement、记录 removed-part 和 removed owner `.rels`
audit、输出省略 unknown extension part 及其 owner `.rels`、保留 worksheet `.rels`
中指向缺失 part 的 inbound relationship、保留其它 linked/source entries 和默认
`bin` content type，且不重写 `[Content_Types].xml`；这不是 unknown extension
删除语义、custom relationship repair、metadata repair、relationship pruning/repair、
content type repair、orphan cleanup、事务式 undo 或 public API；
当前内部 `PackageEditor::remove_part()` 还覆盖显式 registered-part removal 窄切片：
只接受源 package 中已有的普通 part，输出时省略目标 part 和存在时的 source-owned
owner `.rels`，记录 removed-part / removed package-entry audit，并在目标存在 content
type override 时重写 `[Content_Types].xml`；它不会修剪其它 part 指向该目标的 inbound
relationships，不是 object deletion、relationship pruning 或事务式编辑；removed-part
audit 现在会同时保留结构化 inbound relationship metadata（owner entry、owner
part、id、type、raw target、normalized target part）和可读 reason，记录仍指向
被删除 part 的 inbound package/source relationship 上下文，便于 Patch traceability；
若 removed-part inbound 扫描遇到 malformed percent relationship target，会记录
EditPlan / planned output audit note 并继续保留该 `.rels` bytes，不让无关坏
target 阻塞显式 part removal；planned output 也暴露 removed target omission
与 copy-original metadata entries，且不新增结构化 relationship target audit；
这不是 target repair 或 relationship validation；
linked-object fixture 还覆盖显式移除 `xl/workbook.xml`：输出省略 workbook part
及其 source-owned workbook `.rels`，移除 workbook content type override，保留
package `_rels/.rels` 中的 inbound officeDocument relationship，且不修剪 worksheet、
drawing、table、sharedStrings、styles、VBA、calcChain 或 unknown extension 等
downstream/source parts；这只是 no-pruning / preservation 审计证据，不是 workbook
deletion、sheet catalog sync、relationship repair 或完整 workbook editing；
linked-object fixture 还覆盖显式移除 `xl/worksheets/sheet1.xml`：输出省略 worksheet
part 及其 source-owned worksheet `.rels`，移除 worksheet content type override，保留
workbook `.rels` 中的 inbound worksheet relationship，且不修剪 drawing、table、
sharedStrings、styles、VBA、calcChain 或 unknown extension 等 downstream/source parts；
这只是 no-pruning / preservation 审计证据，不是 sheet delete、workbook sheet catalog
sync、relationship repair 或完整 worksheet editing；
同一 worksheet 路径还覆盖 ordinary replace-then-remove：后续 removal 清理 active
worksheet replacement、记录 removed-part 和 worksheet owner `.rels` omission audit，
输出省略 worksheet part 及其 owner `.rels`、移除 worksheet content type override，
但保留 workbook `.rels` 中指向缺失 worksheet 的 inbound worksheet relationship，
以及 drawing/chart/media/table/sharedStrings/styles/VBA/calcChain/unknown
downstream/source parts；这不是 sheet delete、workbook sheet catalog sync、
relationship/content type repair、orphan cleanup、事务式 undo 或 public API；
linked-object fixture 还覆盖显式移除 `xl/drawings/drawing1.xml`：输出省略 drawing
part 及其 source-owned drawing `.rels`，移除 drawing content type override，保留
worksheet `.rels` 中 direct / URI-qualified inbound drawing relationships，且不修剪
chart、media 或其它 downstream parts；这只是 no-pruning / preservation 审计证据，
不是 drawing mutation、object deletion、relationship repair 或完整 drawing 支持；
同一 drawing 路径还覆盖 ordinary replace-then-remove：后续 removal 清理 active
drawing replacement、记录 removed-part 和 drawing owner `.rels` omission audit，
输出省略 drawing part 及其 owner `.rels`、移除 drawing content type override，
但保留 worksheet `.rels` 中 direct / URI-qualified inbound drawing relationships，
以及 chart/media/table/VML/percent-decoded drawing/sharedStrings/styles/VBA/
calcChain/unknown downstream/source parts；这不是 drawing mutation、object deletion、
relationship/content type repair、orphan cleanup、事务式 undo 或 public API；
linked-object fixture 还覆盖显式移除 `xl/media/image1.png`：输出省略 media
entry，保留 PNG default content type 和 drawing `.rels` 中的 inbound image
relationship，不凭空创建 media owner `.rels` omission；这只是 no-pruning /
preservation 审计证据，不是 existing-workbook 图片编辑或关系修复；
linked-object fixture 还覆盖显式移除 `xl/tables/table1.xml`：输出省略 table
entry，移除 table content type override，保留 worksheet `.rels` 中的 inbound
table relationship，不凭空创建 table owner `.rels` omission；这只是 no-pruning /
preservation 审计证据，不是 table resize、relationship repair 或完整 table editing；
linked-object fixture 还覆盖显式移除 `xl/sharedStrings.xml`：输出省略 sharedStrings
part 及其 owner `.rels`，移除 sharedStrings content type override，保留 workbook
`.rels` 中的 inbound sharedStrings relationship；这只是 no-pruning / preservation
审计证据，不是 sharedStrings 索引迁移、字符串表重建、worksheet cell 引用同步、
relationship repair 或 existing-file sharedStrings 语义编辑；
linked-object fixture 还覆盖显式移除 `xl/styles.xml`：输出省略 styles part，
移除 styles content type override，保留 workbook `.rels` 中的 inbound styles
relationship，不凭空创建 styles owner `.rels` omission；这只是 no-pruning /
preservation 审计证据，不是 style id 迁移、样式合并、cell `s` 引用同步、
relationship repair、existing-file style preservation 或完整样式编辑；
linked-object fixture 还覆盖显式移除 `xl/vbaProject.bin`：输出省略 VBA project
part，移除 VBA content type override，保留 workbook `.rels` 中的 inbound VBA
relationship，不凭空创建 VBA owner `.rels` omission；这只是 no-pruning /
preservation 审计证据，不是 macro generation、VBA 语义编辑、签名保真、
relationship repair 或完整宏支持；
linked-object fixture 还覆盖显式移除 `xl/drawings/vmlDrawing1.vml`：输出省略
VML drawing part，移除 VML content type override，保留 worksheet `.rels` 中的
URI-qualified inbound `vmlDrawing` relationship，不凭空创建 VML owner `.rels`
omission；这只是 no-pruning / preservation 审计证据，不是 VML shape editing、
legacy drawing mutation、relationship repair 或完整 VML/drawing 支持；
linked-object fixture 还覆盖显式移除 `xl/drawings/drawing space.xml`：输出省略
percent-decoded drawing part，移除 drawing content type override，保留 worksheet
`.rels` 中原始 `../drawings/drawing%20space.xml` inbound relationship，不凭空创建
`xl/drawings/_rels/drawing space.xml.rels`；这只是 no-pruning / preservation
审计证据，不是 percent-encoded target repair、relationship rewrite、drawing
mutation 或完整 drawing 支持；
removal 回归还覆盖后调用的 `remove_part()` 压过此前 ordinary replacement，清理 stale
replacement state 并以 removed-part audit / content type cleanup 为最终状态；invalid removal
失败覆盖 edit-plan entries/notes、package-entry audit、removed audit、calc policy、
manifest write-mode、aggregate `planned_output()` / legacy output-entry preview
和 copied output bytes 不污染；
反向顺序回归还覆盖对源 package 中已有的普通 part，后调用的 ordinary
`replace_part()` 可把此前 `remove_part()` 的目标恢复为 active replacement，
清理 stale removed-part / removed owner `.rels` audit 与 omitted entry 状态，
并把存在的 source-owned `.rels` 重新记录为 copy-original audit；对带 content type
override 的 part，还覆盖恢复后 `[Content_Types].xml` 回到 source bytes /
copy-original audit；linked-object fixture 还覆盖 workbook-specific 反向顺序：
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
`[Content_Types].xml` 回到 source bytes / copy-original audit；这不是 drawing
mutation、object deletion、事务式 undo、relationship repair、
content type repair、语义合并或 public API；同时覆盖 sharedStrings-specific
反向顺序：显式移除 `xl/sharedStrings.xml` 后再 ordinary `replace_part()` 会恢复
sharedStrings active replacement、恢复 source-owned sharedStrings `.rels`
copy-original audit、保留 workbook `.rels` inbound sharedStrings relationship，
并让 `[Content_Types].xml` 回到 source bytes / copy-original audit。这不是
sharedStrings 索引迁移、字符串表重建、worksheet cell 引用同步、relationship
repair、content type repair、事务式 undo、语义合并或 public API；同时覆盖
styles-specific 反向顺序：显式移除 `xl/styles.xml` 后再 ordinary
`replace_part()` 会恢复 styles active replacement、保留 workbook `.rels` inbound
styles relationship，不凭空创建 styles owner `.rels`，并让 `[Content_Types].xml`
回到 source bytes / copy-original audit。这不是 style id 迁移、样式合并、
cell `s` 引用同步、relationship repair、content type repair、事务式 undo、
语义合并、existing-file style preservation 或 public API；
linked-object fixture 上的 ordinary media replacement 回归还验证只重写
`xl/media/image1.png` 时，drawing `.rels`、PNG default content type、workbook、
worksheet、drawing、chart 和 unknown extension entries 仍保持同一 copy-original
基线；这不是图片解码、drawing mutation 或 existing-workbook image editing；
同一路径还覆盖 default-typed media 先显式移除再 ordinary replacement 的反向顺序：
后续 replacement 会恢复 active media part、清理 stale removed-part audit、保留
PNG default content type 且不把 `xl/media/image1.png` 提升成 override、保留
inbound drawing `.rels`，并且不凭空创建 media owner `.rels`；这不是事务式 undo、
图片语义合并、relationship repair、content type repair 或完整 image preservation；
同一路径还覆盖 media 先 ordinary replacement 再显式移除的顺序：后续 removal 会清理
active media replacement、记录 removed-part audit 和 inbound drawing relationship
metadata，输出省略 `xl/media/image1.png`，保留 PNG default content type 且不把
media 提升成 override、保留 inbound drawing `.rels`，并且不凭空创建 media owner
`.rels`；内部 output-plan 快照还暴露 omitted media part、drawing inbound audit、
content types / drawing `.rels` copy-original 和无 media owner `.rels` 条目；这同样
不是事务式 undo、图片语义合并、relationship pruning/repair、content type repair、
existing-workbook image editing 或完整 image preservation；
linked-object fixture 上的 ordinary table replacement 回归还验证只重写
`xl/tables/table1.xml` 时，worksheet `.rels`、table content type override、
workbook、worksheet、drawing、chart、media 和 unknown extension entries 仍保持同一
copy-original 基线；这不是 table resize、calculated columns、totals generation
或 existing-workbook table editing；
同一路径还覆盖 table 先显式移除再 ordinary replacement 的反向顺序：后续
replacement 会恢复 active table part、清理 stale removed-part audit、让
`[Content_Types].xml` 回到 source/copy-original audit、保留 worksheet `.rels`
inbound table relationship，且不凭空创建 table owner `.rels`；这不是 table resize、
calculated columns、totals generation、事务式 undo、relationship repair、
content type repair 或 existing-workbook table editing；
同一路径还覆盖 table 先 ordinary replacement 再显式移除的顺序：后续 removal
会清理 active table replacement、记录 removed-part audit 和 inbound worksheet
relationship metadata、输出省略 table part、移除 table content type override、保留
worksheet `.rels` inbound table relationship，且不凭空创建 table owner `.rels`；
内部 output-plan 快照还暴露 omitted table part、worksheet inbound audit、content
types local-DOM rewrite 和无 table owner `.rels` 条目；这不是 table delete
semantics、table resize、calculated columns、totals generation、事务式 undo、
relationship pruning/repair、content type repair 或 existing-workbook table editing；
linked-object fixture 上的 ordinary sharedStrings replacement 回归还验证只重写
`xl/sharedStrings.xml` 时，workbook `.rels`、sharedStrings owner `.rels`、
sharedStrings content type override、styles、table、media、VBA 和 unknown
extension entries 仍保持同一 copy-original 基线；这不是 sharedStrings
索引迁移、字符串表重建、worksheet cell 引用同步或 existing-file sharedStrings
语义编辑；
同一路径还覆盖 sharedStrings 先 ordinary replacement 再显式移除的顺序：后续
removal 会清理 active sharedStrings replacement、记录 removed-part audit、输出省略
`xl/sharedStrings.xml` 及其 source-owned owner `.rels`、移除 sharedStrings
content type override、保留 workbook `.rels` 中的 inbound sharedStrings relationship；
它不会修剪 worksheet `t="s"` 引用或重建字符串表。这不是 sharedStrings 索引迁移、
字符串表重建、worksheet cell 引用同步、事务式 undo、relationship pruning/repair、
content type repair、existing-file sharedStrings 语义编辑或 public API；
linked-object fixture 上的 ordinary styles replacement 回归还验证只重写
`xl/styles.xml` 时，workbook `.rels`、styles content type override、sharedStrings、
sharedStrings owner `.rels`、table、media、VBA 和 unknown extension entries 仍保持
同一 copy-original 基线，且不会凭空创建 `xl/_rels/styles.xml.rels`；这不是
style id 迁移、样式合并、cell `s` 引用同步、existing-file style preservation
或完整样式编辑；
同一路径还覆盖 styles 先 ordinary replacement 再显式移除的顺序：后续 removal
会清理 active styles replacement、记录 removed-part audit、输出省略 `xl/styles.xml`、
移除 styles content type override、保留 workbook `.rels` 中的 inbound styles
relationship，且不凭空创建 `xl/_rels/styles.xml.rels`；它不会迁移 style id
或重写 cell `s` 引用。这不是 style id 迁移、样式合并、existing-file style
preservation、事务式 undo、relationship pruning/repair、content type repair、
完整样式编辑或 public API；
同一路径还覆盖 chart 先显式移除再 ordinary replacement 的反向顺序：后续
replacement 会恢复 active chart part、清理 stale removed-part audit、让
`[Content_Types].xml` 回到 source/copy-original audit、保留 drawing `.rels` 中的
direct / URI-qualified inbound chart relationships、保留其它 linked/unknown source
entries，且不会凭空创建 chart owner `.rels`；这不是 chart semantic merge、
chart reference repair、relationship repair、content type repair、事务式 undo、
existing-workbook chart editing 或 public API；
内部 `planned_output()` 快照还覆盖该 restore 状态：暴露 active chart
`StreamRewrite` entry、content types copy-original audit、preserved inbound drawing
`.rels`、preserved linked/unknown entries、empty removed_parts 与
removed_package_entries，且不凭空创建 chart owner `.rels`；这仍只是 Patch audit，
不是 public output planner 或 chart editing API；
linked-object fixture 上的 ordinary chart replacement 回归还验证只重写
`xl/charts/chart1.xml` 时，drawing `.rels` 中的 chart / URI-qualified chart
relationships、chart content type override、media、table、sharedStrings、styles、VBA
和 unknown extension entries 仍保持同一 copy-original 基线，且不会凭空创建 chart
owner `.rels`；这不是 chart reference migration、series/cache update、drawing
mutation、existing-workbook chart editing 或完整图表支持；
同一路径还覆盖 chart 先 ordinary replacement 再显式移除的顺序：后续 removal
会清理 active chart replacement、记录 removed-part audit 和 direct / URI-qualified
inbound drawing relationship metadata、输出省略 `xl/charts/chart1.xml`、移除 chart
content type override、保留 inbound drawing `.rels` 和其它 linked/unknown source
entries，且不会凭空创建 chart owner `.rels`；这不是 chart delete semantics、
chart reference repair、relationship pruning/repair、content type repair、事务式 undo、
语义合并、existing-workbook chart editing 或 public API；
内部 `planned_output()` 快照还覆盖该 final-removal 状态：暴露 omitted chart
part、removed_parts target/reason/inbound audit、drawing-owned direct /
URI-qualified inbound relationship metadata、content types rewrite、empty
removed_package_entries，且不凭空创建 chart owner `.rels`；这仍只是 Patch audit，
不是 public output planner、chart editing API、relationship repair 或 transactional
undo；
linked-object fixture 上的 ordinary VBA project replacement 回归还验证只重写
`xl/vbaProject.bin` 时，workbook `.rels`、VBA content type override、worksheet、
drawing、chart、media、table、sharedStrings、styles、calcChain 和 unknown extension
entries 仍保持同一 copy-original 基线，且不会凭空创建
`xl/_rels/vbaProject.bin.rels`；这不是 macro generation、VBA 语义编辑、
signature preservation、workbook relationship repair 或完整宏支持；
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
`vmlDrawing` relationship、VML content type override、workbook、worksheet、
drawing、chart、media、table、sharedStrings、styles、VBA、calcChain 和 unknown
extension entries 仍保持同一 copy-original 基线，且不会凭空创建
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
另有 `ReferencePolicy` 窄回归覆盖 linked-object fail、calcChain preserve / rebuild
拒绝、malformed workbook metadata / workbook calc metadata rewrite 预检失败不污染状态并保持
aggregate `planned_output()` copy-original、缺少 `xl/workbook.xml`
时 worksheet rewrite 失败不污染状态、已有 ordinary workbook
replacement 排队后 linked-object fail 仍保留既有 replacement / manifest /
source-owned `.rels` audit / 输出 bytes、queued core/app docProps helper 后
linked-object fail 仍保留既有 metadata edit / audit / 输出 bytes，和 request-recalculation
fullCalcOnLoad 输出路径；这些 no-state-pollution 回归还覆盖
`relationship_target_audits()` 和 `worksheet_relationship_reference_audits()` 不追加
stale 结构化 relationship target / worksheet reference audit；
`DependencyAnalyzer` 还能沿 worksheet
`.rels` 中已知 internal relationship target 做保守遍历，例如 worksheet → table 和
worksheet → drawing → image/chart，把这些 part 纳入 dependency summary；当前回归还覆盖
递归到 drawing-owned `.rels` 后的 external、URI-qualified、invalid 和 unresolved
relationship target 审计 note，并跳过 external hyperlink target，不会把它当成 package
part，同时记录 external target 审计 note；
对带 query/fragment 的 internal relationship target，会记录 URI-qualified 审计 note，
并在 base target 解析到已注册 package part 时保守纳入 dependency summary；
以 `/` 开头的 absolute internal target 会按 package part path 做 normalization；
percent-encoded internal relationship target 会先解码 `%XX` 后再做 part-name
normalization，已注册的 decoded target 会纳入 dependency summary；
malformed percent escape 或解码后非法的 target 仍走 invalid-target 审计路径；逃出
package root 或解析到内部路径但当前 manifest 未注册的 internal relationship target，
只记录 package structure review 审计 note，不虚构 package part；这些 relationship
target 审计 note 和结构化 `RelationshipTargetAudit` 会携带 owner part、relationship id、
relationship type、原始 target，以及可用时的 normalized base target part，并会传播到
worksheet `EditPlan` 和 existing-file `PackageEditor::edit_plan()`，供 Patch 审计；
`EditPlan` 会对完全相同的 audit note 去重，并按 owner/id/type/raw target/normalized
target upsert 重复 `RelationshipTargetAudit`，所以重复 linked worksheet rewrite 不会
堆积同一结构化 relationship target audit；这只是内部 Patch audit 状态卫生，不是
relationship validation、repair 或 pruning；
未知 relationship type 只要 target normalize 后命中已注册 internal part，也会被保守纳入
dependency summary / copy-original reason；这只是保守依赖审计，不代表理解或编辑该
custom relationship 语义；
`PartRewritePlanner` 会把 dependency reason 回写到
copy-original EditPlan entries；这些 reason 现在携带 relationship id、relationship type 和
normalized target part path，workbook part 的 reason 还会显式携带 calcPr / definedNames review
上下文；`plan_worksheet_stream_rewrite()` 在 `CalcChainAction::Remove` 下会直接
产出 stale calcChain removed-part audit，供 `PackageEditor` 消费，也供后续 sheet
rename/delete/move 策略继续演进。
内部 `DependencyAnalysis`、worksheet `EditPlan` 和 existing-file
`PackageEditor::edit_plan()` 还保留结构化 `RelationshipTargetAudit` 列表，以及
relationship-derived `PartDependency` 的 owner part、relationship id/type 和原始 target
字段；relationship-derived copy-original `EditPlanEntry` 也会保留这些字段作为内部
Patch 审计 metadata，而 workbook/sharedStrings/styles 等静态依赖仍只带 reason
文本；它们不是 public relationship editor。
完整 existing-file editing、public `PackageEditor`、relationship pruning/orphan cleanup、
sharedStrings/styles/tables/drawings/defined-name 语义联动、drawing/image/chart/table 编辑和 broad unknown part preservation
仍是计划。

当前优先级提示：Phase 4 是下一轮架构主线，但不应再把它作为一个笼统大任务直接执行。
当前执行顺序是先完成 `P4.0 API surface unification`，统一 public facade、命名、
`CellView` / `Cell` / `CellValue` 边界和 internal/public 分界，再进入窄
Patch MVP。具体子任务拆分以 [任务拆分设计](TASK_BREAKDOWN.md) 为准。

功能：

- PackageReader。
- PackageEditor。
- EditPlan / DependencyAnalyzer。
- ReferencePolicy / PartRewritePlanner。
- PartIndex。
- 未修改 part 原样复制。
- 小型 XML part 局部 DOM 编辑。
- 大型 worksheet 流式替换。
- 模板填充。
- `P4.0` public API 统一设计，并明确 `PackageReader` / `PackageEditor` /
  `EditPlan` 仍是 internal Patch 底座，不是稳定 public API。
- 小文件 In-memory 随机编辑入口，并明确它不承担大数据低内存场景。
- Replace sheet data 并保留未修改 part。
- 公式重算策略：设置 fullCalcOnLoad，明确 `calcChain.xml` 删除、重建或保留边界。
- ReferencePolicy：对 sheet rename/delete/move、defined names、table ranges、chart refs
  和跨 sheet 公式引用给出保守策略或显式失败。

约束：

- Phase 4 的核心不是“把 workbook 全量 DOM 化”，而是已有文件的可控编辑和保真保存。
- 大型 worksheet patch 采用 event reader -> transformer -> stream writer。
- 未知或未修改的 chart、image、VBA、pivot cache 和 extension part 默认原样复制。
- 不承诺任意大型 worksheet 的 O(1) 随机 cell 读写。

## Phase 5：复杂对象

当前状态提示：`stb` 已作为默认 vcpkg 依赖接入 PNG/JPEG `read_image_info()`
元数据 helper。当前还存在
`WorksheetWriter::add_image(path, anchor)` 的 streaming-only new-workbook
PNG/JPEG 图片插入基础切片，会写 media/drawing parts、drawing rels、
worksheet rels、worksheet `<drawing>` 和 content types。它不代表图片编辑、
existing-workbook 图片保真、drawing mutation、裁剪、旋转、压缩或格式转换。
当前 `ImageOptions` 只补充 drawing XML two-cell marker `xdr:colOff` /
`xdr:rowOff`、`xdr:twoCellAnchor editAs` 和 `xdr:cNvPr name` / `descr` metadata；
它不代表 `oneCellAnchor` / `absoluteAnchor` 元素支持、row/column resize 几何计算、
完整图片 metadata、EXIF/PNG/JPEG metadata、media filename 语义或完整
alt text/accessibility UI。
图片、超链接、table、chart/VBA passthrough 不能仅凭本 roadmap 条目宣称支持；
以 `TASK_PLAN.md`、`NEXT_STEPS.md`、`TASK_BREAKDOWN.md`、`AGENTS.md` 的 current
verified state 和任务拆分为准。

功能：

- 图片读取和插入；图片解码/尺寸读取使用 `stb`，OpenXML media/drawing
  package 逻辑由 FastXLSX 自己实现。
- 超链接。
- 数据验证。
- 条件格式。
- table。
- 图表透传。
- VBA 透传。

## 长期目标

FastXLSX 应该成为：

```text
一个可编辑的高性能 XLSX/OpenXML 引擎：
大文件写入走 Streaming，
已有文件编辑走 Patch / part-level rewrite，
小文件复杂编辑走 In-memory，
并在大数据路径上明显优于 OpenXLSX DOM 主路径。
```

## 持续性要求

- 测试流程见 [测试流程](TESTING_WORKFLOW.md)。
- API 设计和文档注释要求见 [API 设计与文档注释](API_DESIGN_AND_DOCUMENTATION.md)。
- 任务执行拆分见 [任务拆分设计](TASK_BREAKDOWN.md)。
- 任何任务计划都必须说明是否触碰性能热路径，以及需要的结构测试、Excel 可视化验证、
  拆包 XML 对比或 benchmark。
