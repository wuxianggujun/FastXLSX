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

当前 worksheet rewrite 与 part-level Patch 还有大量窄回归证据，覆盖 worksheet-owned
object（background picture / header-footer VML / printerSettings / OLE / control）的
`sheetData` 局部替换保留、显式 removal audit 与 same-path ordering；self-closing
`<sheetData/>` 与前缀形式的成功替换；by-name worksheet / `sheetData` patch 入口；
真实 `WorkbookWriter` 产物的 Patch roundtrip；`rename_sheet_catalog_entry()` sheet
catalog name rewrite；完整 worksheet replacement 的根元素校验、payload dependency
audit 和 relationship reference audit；comments / threaded comments / persons / pivot
table / pivot cache / external links / custom XML / drawing / VML / percent-decoded
drawing / media / table / sharedStrings / styles / VBA / chart 等 linked part 的
ordinary replace、显式 removal 与 remove/replace 双向 ordering；`planned_output()`
聚合快照对上述状态的逐 fixture 覆盖；以及 DEFLATE source、no-op `save_as()` roundtrip
和各类失败路径的 no-state-pollution。这些逐 fixture 回归明细见
[Patch 保留能力回归明细](PATCH_PRESERVATION_COVERAGE.md)。上述能力都是内部 Patch
preservation / audit 可见性，不是 public API、relationship/content-type repair、
orphan cleanup、语义编辑或完整 object lifecycle 支持。

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
