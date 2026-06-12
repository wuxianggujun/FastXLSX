---
name: fastxlsx-api-design-docs
description: "设计或审查 FastXLSX public API、API 文档注释、任务计划和性能边界。用于 Workbook/WorksheetWriter/PackageEditor/In-memory editor 类接口、Doxygen 注释、Streaming/Patch/In-memory 模式说明、EditPlan/DependencyAnalyzer 联动说明、API 易用性与性能取舍，以及防止便利 API 破坏流式主线或把编辑能力降级为补丁。"
---

# FastXLSX API Design Docs

## 必读文件

- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`
- `docs/TESTING_WORKFLOW.md`
- `docs/PERFORMANCE_TARGETS.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `docs/TASK_BREAKDOWN.md`
- `README.md`

执行 API 设计或任务计划时，必须先从 `docs/TASK_BREAKDOWN.md` 选择最小子任务编号。
不要再把“任务 4”或整个 Phase 4 当作一个可直接执行的大任务；当前默认顺序是
`C0 -> C7`，`P*` 只保留为历史索引和能力切片，API 设计任务在需要时仍可引用
`P4.0` / `P4` / `P7` 等历史编号做定位。

再检查 `include/` 和 `src/`，确认 API 是否已经实现。当前已实现的 public API
包括 `Workbook`、`Worksheet`、`Cell`、`DocumentProperties`、`WorkbookWriter`、
`WorksheetWriter`、`CellView`、`StyleId`、`CellAlignment`、`HorizontalAlignment`、
`VerticalAlignment`、`CellFont`、`CellFill`、`CellStyle`、`DataValidationRule`、`DataValidationType`、
`DataValidationOperator`、`DataValidationErrorStyle`、`ArgbColor`、`ColorScaleValueType`、
`ColorScalePoint`、`TwoColorScaleRule`、`ThreeColorScaleRule`、`DataBarValueType`、
`DataBarEndpoint`、`DataBarRule`、`IconSetStyle`、`IconSetValueType`、`IconSetRule`、
`ImageFormat`、`ImageInfo`、`ImagePixels`、`ImageEditAs`、`ImageAnchorOffset`、
`ImageOptions`、`WorkbookWriter::add_style()`、`CellView::with_style()`、
`read_image_info()`、`read_image_pixels()`、
`WorksheetWriter::add_conditional_color_scale()`、`WorksheetWriter::add_conditional_data_bar()`、
`WorksheetWriter::add_conditional_icon_set()`、`WorksheetWriter::add_image()` 和
`FastXlsxError`。
`Workbook::set_document_properties()`
和 `WorkbookWriterOptions::document_properties` 是当前 new-workbook core/app
docProps metadata API；它们只写 `docProps/core.xml` 和 `docProps/app.xml`。
`HyperlinkOptions` 是当前 streaming hyperlink display/tooltip metadata API。
`WorksheetWriter::add_external_hyperlink()` 是当前 external URL
worksheet hyperlink API；`WorksheetWriter::add_internal_hyperlink()` 是当前 internal
workbook location worksheet hyperlink API；`WorksheetWriter::add_table()` 和 `TableOptions` 是当前
streaming-only table API。`WorkbookWriter` / `WorksheetWriter` / `CellView`
是流式写入骨架；data validation、external/internal hyperlink、two-/three-color conditional
color scale、basic data bar、basic 3Arrows icon set 和 table API 是 worksheet metadata 基础切片，不等同完整 Phase 3 或完整
Phase 5。
当前 `ImageFormat`、`ImageInfo` 和 `read_image_info()` 是默认 `stb` PNG/JPEG
图片元数据 API，只读取格式、尺寸和通道。当前 `ImagePixels` 和 `read_image_pixels()`
是默认 `stb` PNG/JPEG 像素读取 API，会解码并分配完整 caller-owned decoded pixel
buffer，不创建 package media part。当前 `WorksheetWriter::add_image()`
是默认 `stb` streaming-only new-workbook PNG/JPEG path 和 memory-source 图片插入基础
API，会写 OpenXML media/drawing parts，但不代表 existing-workbook 图片保真或完整
drawing 编辑。memory-source overload 接受 `std::span<const std::byte>`，span 只需在
调用期间有效，并会把 caller bytes 同步复制到临时 file-backed media entry。
当前 `ImageOptions` 只给该插入 API 增加 drawing XML anchor marker /
non-visual metadata：`from_offset` / `to_offset` 写 two-cell marker `xdr:colOff` /
`xdr:rowOff`，`edit_as` 写 `xdr:twoCellAnchor editAs`，非空 `name` / `description`
写 `xdr:cNvPr name` / `descr`，空值走默认名或省略；非空
`external_hyperlink_url` 会在 `xdr:cNvPr` 下写 `a:hlinkClick`，并在 drawing `.rels`
创建 `TargetMode="External"` 的 hyperlink relationship，`external_hyperlink_tooltip`
只写该 hyperlink metadata 的 tooltip attribute。

## 核心原则

API 可以易用，但不能为了易用性牺牲性能主线，也不能把已有文件编辑做成
streaming writer 的事后补丁。FastXLSX 的 public API 应围绕共享 OpenXML/OPC
底座上的 Streaming、Patch、In-memory 三条路径设计。

- 大数据写入 API 优先 row iterator / chunk writer。
- 大型 worksheet 不能被 public API 隐式转入 DOM 或完整 cell matrix。
- 便利 API 必须写清适用范围。
- 新增或修改便利 API 必须向 streaming 性能主线靠齐，不能为了易用性引入完整
  worksheet cell matrix、DOM 或 cell map。
- 只适合小文件的 API 应明确标记为 in-memory 路径。
- `Cell` / `CellValue` 这类 public owning value 只能作为 API 边界值、临时值或小文件
  便利值；不要把它当作百万级 worksheet 的内部长期存储模型。
- In-memory editor 需要独立紧凑 cell store、字符串/公式池、style id 引用、cell 计数
  和内存预算/估算入口；没有这些 guardrail 前，不要宣称小文件随机编辑 ready。
- 性能热路径不能因为高层包装落到通用 XML serializer。
- Patch API 必须写清 part-level rewrite、unknown part preservation、EditPlan 影响范围、
  relationships/content types/sharedStrings/styles/calc metadata side effects。
- 当前 `Worksheet::append_row()` 是 append-only、streaming-oriented public API，
  但 Phase 1 实现会临时 buffer rows；不要把这个 buffer 当成长期大文件架构。
- 当前 `WorksheetWriter` 骨架覆盖公式、行高、列宽、冻结窗格、自动筛选、
  合并单元格、data validations、two-/three-color conditional color scales、basic data bars
  和 basic 3Arrows icon sets 的写入 XML；
  这些不代表完整 Phase 3 或
  Phase 5 功能集。
- `WorksheetWriter::add_conditional_color_scale()` 是 Streaming metadata API：range 列表
  和两个 color-scale endpoints 被复制进 writer state，内存按规则数和 range 数增长。
  它写 worksheet-local `<conditionalFormatting><cfRule type="colorScale">`、两个
  `<cfvo>` 和两个 inline ARGB `<color>`，priority 按 worksheet 内调用顺序分配；
  不生成 `styles.xml`、`dxfs`、worksheet `.rels`、content types、workbook
  relationships、cell text 或 `<calcPr>`，不解析公式、不计算单元格值、不排序/合并/
  去重 ranges、不检查重叠，也不支持 existing-file editing 或完整 Excel UI。
- 当前 `Cell::formula()` / `CellView::formula()` 写入 worksheet `<f>`，并让包含公式
  cell 的新建 workbook 在 `xl/workbook.xml` 写 `<calcPr calcId="124519"
  fullCalcOnLoad="1"/>` 请求打开后重算；这不是公式求值、cached value 或
  `calcChain.xml` 支持。
- `WorksheetWriter::add_data_validation()` 是 Streaming metadata API：规则、range 列表、
  公式文本和 prompt/error 文本被复制进 writer state，内存按规则数量、multi-area
  `sqref` 区域数量/文本长度、公式文本长度和 prompt/error 文本长度增长；
  `showInputMessage`、`showErrorMessage`、`showDropDown`、`errorStyle`、
  `promptTitle`、`prompt`、`errorTitle` 和 `error` 只写为 worksheet `<dataValidation>`
  attributes，空字符串和 false flags 省略。`hide_dropdown_arrow` 只对 list validation
  有效，写出 OpenXML 反向命名的 `showDropDown="1"` 来隐藏 in-cell dropdown arrow。
  它不解析公式、不校验单元格值、不检查重叠、
  不排序/合并/去重区域，不新增 relationships/content types/styles，也不支持
  existing-file editing 或完整 Excel UI。
- `WorksheetWriter::add_external_hyperlink()` 是 Streaming metadata API：cell ref 和
  target URL 被复制进 writer state，内存按链接数量、URL 文本长度以及可选 display/tooltip
  文本长度增长；它会新增
  worksheet `<hyperlinks>` 和 worksheet `.rels`，但不新增 workbook relationships 或
  content type overrides，不写单元格文本、不创建 hyperlink 样式、不校验 URL 可达性，
  也不支持 existing-file editing。
- `WorksheetWriter::add_internal_hyperlink()` 是 Streaming metadata API：cell ref 和
  workbook location 文本被复制进 writer state，内存按链接数量、location 文本长度以及
  可选 display/tooltip 文本长度增长；
  它只新增 worksheet `<hyperlinks>` 中的 `location` 属性，不新增 worksheet `.rels`、
  workbook relationships 或 content type overrides，不消耗 worksheet-local `rId`，不写
  单元格文本、不创建 hyperlink 样式、不校验 target sheet/range/named range 是否存在，也
  不支持 existing-file editing。
- `HyperlinkOptions` 是 Streaming metadata options：非空 `display` / `tooltip` 只写成
  worksheet `<hyperlink>` attributes，空字符串省略；它不写 cell text、不生成 `styles.xml`、
  不改变 relationships/content types，也不代表完整 Excel hyperlink UI。
- `WorksheetWriter::add_table()` 是 Streaming metadata API：range、table name、
  column names、style flags、`show_totals_row`、`column_totals_functions` 和
  `column_totals_labels` 被复制进
  writer state，内存按表数量和文本长度增长；
  它会新增 `xl/tables/tableN.xml`、worksheet `<tableParts>`、worksheet `.rels` 和
  table content type override，但不读取已写 header 行、不推断列名、不生成
  `styles.xml`。`show_totals_row` 只写 totals-row visibility metadata；
  `column_totals_functions` 只写 caller-supplied `totalsRowFunction` attributes；
  `column_totals_labels` 只写 caller-supplied `totalsRowLabel` attributes；它不计算
  totals、不生成公式文本、totals row 单元格文本、样式、table resize 或 existing-file
  editing。当前只拒绝同一 worksheet 内 table-vs-table range overlap，不检查与 data
  validations、images、merged ranges 或 autoFilter 的冲突。
- `DocumentProperties` 是 new-workbook 小型 metadata API：字符串值被复制进
  `Workbook` 或 `WorkbookWriter` state，并写入 `docProps/core.xml` /
  `docProps/app.xml`。它不创建 `docProps/custom.xml`，不支持任意 timestamps、
  custom document properties 或 existing-file editing，也不进入 worksheet row/cell
  热路径。内部 core/app docProps Patch helper 的现有回归只证明会保留已有
  `docProps/custom.xml`、custom-properties package relationship、content type override
  和 unknown bytes，不代表 custom properties 创建/编辑或 public existing-file API。
- `read_image_info()` 是图片元数据 helper：当前默认 vcpkg manifest 依赖 `stb`，
  并用 `stb_image` 的 header probing 读取 PNG/JPEG 格式、尺寸和通道。它不创建
  media part、drawing XML、relationships、
  content types 或 anchors，也不代表图片插入或 existing-file 图片保真。
- `read_image_pixels()` 是图片像素 helper：当前默认 vcpkg manifest 依赖 `stb`，
  会解码 PNG/JPEG 并在 `ImagePixels::pixels` 中分配完整 caller-owned decoded pixel
  buffer；内存随像素宽高和通道数增长。它不创建 media part、drawing XML、relationships、
  content types 或 anchors，也不同于 `WorksheetWriter::add_image()` 的 raw-byte/file-backed
  media insertion 热路径。
- `WorksheetWriter::add_image()` 是 Streaming metadata/object API：当前通过默认
  `stb` 依赖验证 PNG/JPEG 元数据，复制原始图片字节到临时 file-backed media entry；
  memory-source overload 不保留 caller span 或 decoded pixel buffer，
  并在 `close()`
  写 `xl/media/*`、`xl/drawings/drawing*.xml`、drawing `.rels`、worksheet `.rels`、
  worksheet `<drawing>` 和 content type entries。它不解码完整像素、不进入 row/cell
  热路径、不持有完整 worksheet matrix，也不支持裁剪、旋转、压缩、格式转换、
  existing drawing mutation 或 existing-file editing。
- `ImageOptions` 是 Streaming image metadata options：`from_offset` / `to_offset`
  EMU 值、`edit_as` 枚举和非空 `name` / `description` 被复制进 writer state，并作为
  drawing XML two-cell marker `xdr:colOff` / `xdr:rowOff`、`xdr:twoCellAnchor editAs`
  和 `xdr:cNvPr` 的 `name` / `descr` attributes 输出；空 `name` 使用生成的
  `Picture N`，空 `description` 省略。非空 `external_hyperlink_url` 被复制进
  writer state，并作为 drawing object external click metadata 输出为
  `xdr:cNvPr/a:hlinkClick`；relationship 属于 drawing `.rels` owner，写
  `TargetMode="External"`，可选 `external_hyperlink_tooltip` 只写 tooltip attribute。
  除该 drawing hyperlink relationship 外，`ImageOptions` 不新增 media parts、worksheet
  `<hyperlinks>`、worksheet hyperlink relationships、content types、styles、cell text 或
  workbook relationships，不改变 anchor cell range，不写 EXIF/PNG/JPEG metadata，也不代表
  完整 alt text/accessibility UI。
- 当前 `Workbook::save()` 使用 internal package writer boundary；默认 ZIP backend 走
  stored ZIP bootstrap，`FASTXLSX_ENABLE_MINIZIP_NG=ON` 走 minizip-ng DEFLATE
  backend。两者都不是已有文件编辑 API，也不承诺 Zip64 或 true package streaming。
- OPC/Phase 4 当前已有内部 manifest / relationships、`EditPlan`、
  `DependencyAnalyzer`、`ReferencePolicy` 和 `PartRewritePlanner` 计划基础；
  它们只表达 part-level copy/rewrite 决策、worksheet rewrite dependency notes、
  fullCalcOnLoad 和 calcChain 计划语义；`DependencyAnalyzer` 可沿 worksheet `.rels`
  中已知 internal relationship target 做保守遍历，例如 worksheet → table 和
  worksheet → drawing → image/chart，把这些 part 纳入 dependency summary；当前回归还覆盖
  递归到 drawing-owned `.rels` 后的 external、URI-qualified、invalid 和 unresolved
  relationship target 审计 note 和结构化 `RelationshipTargetAudit`。它会跳过 external hyperlink target，不会把它当成
  package part，但会记录 external target 审计 note；
  对带 query/fragment 的 internal relationship target，会记录 URI-qualified 审计 note，
  并在 base target 解析到已注册 package part 时保守纳入 dependency summary；
  以 `/` 开头的 absolute internal target 会按 package part path 做 normalization；
  percent-encoded internal relationship target 会先解码 `%XX` 后再做 part-name
  normalization，已注册的 decoded target 会纳入 dependency summary；malformed percent
  escape 或解码后非法的 target 仍走 invalid-target 审计路径；逃出
  package root 或解析到内部路径但当前 manifest 未注册的 internal relationship target，
  不虚构 part，只记录 package structure review 审计 note；这些 relationship target
  审计 note 会携带 owner part、relationship id、relationship type、原始 target，以及可用时的 normalized
  base target part，方便 Patch 审计但不代表 target validation 或 repair；
  未知 relationship type 只要 target normalize 后命中已注册 internal part，也会被保守纳入
  dependency summary / copy-original reason；这只是保守依赖审计，不代表理解或编辑该
  custom relationship 语义；
  relationship-driven dependency reason 会携带 relationship id、relationship type 和 normalized target
  part path，workbook part 的 dependency reason 会显式携带 calcPr /
  definedNames review 上下文；`PartRewritePlanner` 会把 dependency reason 写回 copy-original
  `EditPlan` entries，并可把已注册目标 part 的显式删除表达为 removed-part audit；当前
  `plan_worksheet_stream_rewrite()` 在 `CalcChainAction::Remove` 下会直接产出 stale
  calcChain removed-part audit，worksheet replacement 只消费该 planner 输出。内部 `DependencyAnalysis`、
  worksheet `EditPlan` 和 existing-file `PackageEditor::edit_plan()` 还保留结构化
  `RelationshipTargetAudit` 列表，以及 relationship-derived `PartDependency` 的 owner part、
  relationship id/type 和原始 target 字段；relationship-derived copy-original
  `EditPlanEntry` 也会保留这些字段作为内部 Patch audit metadata，静态依赖仍只带
  reason 文本。当前另有内部 `PackageReader` ZIP
  entry reader + OPC metadata ingestion 基础，可索引和读取
  stored/no-compression package entries；`FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建还能通过
  minizip-ng 读取 DEFLATE entries，默认构建仍拒绝 compressed input。读取 entry
  时会校验解压后 payload CRC，拒绝
  local header CRC/method/name/size mismatch、encrypted flags、data descriptor entries、Zip64、
  非法 ZIP entry name（绝对路径、尾部斜杠、反斜杠、query/fragment components、空段、dot 段或 parent 段）
  和损坏 metadata 或 payload bytes，也会拒绝 owner part 缺失的 source-owned `.rels`，包括根级
  `_rels/foo.xml.rels` owner relationship entry，并建立内部 `PartIndex` /
  `RelationshipGraph` 视图；冲突 content type default / override 和同一 `.rels`
  owner 内重复的 relationship id、以及 `[Content_Types].xml` / `.rels` 第一个真实
  XML 元素不是 `Types` / `Relationships` 的 decoy-root metadata、metadata declaration
  嵌套在 unsupported child 下的 decoy 会在 ingestion 阶段被拒绝；reader 只 ingest root 的
  direct-child `Default` / `Override` / `Relationship` 元素；metadata attributes 必须未命名空间
  （namespace declarations 除外），namespaced metadata attribute decoy 会在 `PackageReader::open()`
  阶段失败，未命名空间 metadata attributes 不得重复，非 whitespace metadata text 和 start/end tag
  QName mismatch 会失败。这只是
  reader 校验，不是 content-type 或 relationship repair；
  当前 reader-only 回归还覆盖 unknown extension owner
  `.rels` metadata ingestion 和 `RelationshipGraph` 挂回；当前内部 `PackageEditor` copy/replace 基础只能替换
  一个已存在 part 并复制未替换 entries；当前还支持内部 core/app docProps
  generated-small-XML 窄切片，可生成/替换 `docProps/core.xml` 和
  `docProps/app.xml`，包括两者都缺失的输入，并同步 package rels / content types；当前还支持内部 worksheet replacement
  窄切片，可同步 calcChain remove、workbook fullCalcOnLoad、workbook rels 和 content
  types，并保留 source content type defaults/overrides 形态，避免把默认类型媒体 part
  无故提升为 override；普通 part replacement、docProps generated parts 和 worksheet
  replacement 会把 write-mode / dirty / generated / preserve-original 状态同步到内部
  manifest；worksheet replacement 还覆盖缺失 `xl/calcChain.xml` payload 但残留
  calcChain content type override 或 workbook calcChain relationship 的 metadata-only
  cleanup，不记录缺失 payload 的 removed-part audit、不创建 calcChain payload，也不是
  通用 relationship/content-type repair；当前还支持内部
  `PackageEditor::replace_worksheet_part_by_name()` 目标定位 helper：通过
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
  relationship repair、pruning 或 public API。它还
  要求 sheet relationship id 使用 officeDocument relationships XML namespace（可用非
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
  保持 copy-original。该 helper 使用 planned workbook catalog；坏 planned workbook catalog
  （planned sheet relationship id 在 workbook `.rels` 中缺失，或 planned worksheet
  relationship 指向未注册 worksheet part）会在 rename 状态变更前失败，并保留 queued
  workbook replacement、EditPlan / audit、manifest、calc policy、package-entry audit
  和输出 bytes；缺失旧名、精确或 ASCII
  大小写不敏感重复新名、非法新名、`ReferencePolicyAction::Fail` 遇到直接 `definedNames`、以及 workbook planned
  removal 都会失败且不污染 EditPlan、manifest、package-entry audit、calc policy 或输出
  bytes。它不更新 definedNames、公式、tables、drawings、charts、hyperlinks、
  relationship targets、sharedStrings、styles 或 calcChain，不能写成完整 sheet
  rename/add/delete、relationship repair 或 public API。planned workbook XML 路径的内部
  `planned_output()` 只能写成暴露最终 workbook `LocalDomRewrite`、preserved content
  types / workbook `.rels` / worksheet / calcChain / unknown entry，以及 structured
  sheet catalog / definedNames audit；不能写成 sheet rename API、definedNames 语义同步
  或 public API。
  完整 worksheet replacement payload 现在会在 Patch 状态变更前做最小根元素校验：
  replacement XML 必须是单个 `<worksheet>` 根元素（按 local-name 接受前缀形式，
  且允许 XML declaration、注释和处理指令位于根元素前），
  否则失败不污染 EditPlan、manifest、package-entry audit、calc policy 或 copied output
  bytes；任意非 prolog 元素或文本位于根元素前仍会被拒绝；不能写成 XML schema
  validation、namespace repair 或 XML repair。
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
  当前还支持内部 `PackageEditor::replace_worksheet_sheet_data()` helper，只替换既有 worksheet XML 的
  `<sheetData>` / `<sheetData/>`，保留同一 worksheet part 的外围 XML metadata，
  并复用 worksheet replacement 的 calcChain/fullCalcOnLoad 与 preservation 副作用。
  成功后会在内部 `EditPlan` notes 中记录保留的 worksheet-local metadata
range/reference 需要 caller review，当前覆盖 sheetPr、dimension、sheetViews、
sheetFormatPr、cols、sheetProtection、protectedRanges、autoFilter、mergeCells、
scenarios、dataConsolidate、customProperties、cellWatches、smartTags、webPublishItems、
dataValidations、conditionalFormatting、hyperlinks、ignoredErrors、printOptions、
pageMargins、pageSetup、drawing、legacyDrawing、picture、legacyDrawingHF、
oleObjects、controls、tableParts 和 extLst。
当前 helper 仍会物化当前 planned worksheet XML，因此受内部
`package_editor_sheet_data_local_rewrite_byte_limit` 约束；source/queued worksheet
XML、replacement `<sheetData>` payload 或 rewritten worksheet XML 超过该 bounded local rewrite 限制时，
direct/by-name 路径都会在状态变更前失败，不污染 EditPlan、manifest、
package-entry audit、calc policy、planned output 或 copied output bytes，并保留
unknown extension bytes。成功路径也会在 EditPlan/output-plan note 和 worksheet
part reason 中暴露 bounded local rewrite 边界，不能写成 public API 或大文件低内存
streaming worksheet transformer。
当前结构测试还验证 sheetData patch 输出后，worksheet `.rels` 中保留的
legacyDrawing `rId7` target `../drawings/vmlDrawing1.vml#shape1` 可由
`PackageReader` / `RelationshipGraph` 重读；这仍是 preservation 证据，不是
VML/drawing 编辑。
当前还覆盖 worksheet-owned background picture part 与 header/footer VML drawing
part preservation：`sheetData` 局部替换保留 `<picture>` / `<legacyDrawingHF>`
引用、worksheet `.rels` 中的 `image` / `vmlDrawing` relationships、
`xl/media/background.png` bytes、`xl/drawings/vmlDrawingHF1.vml` bytes、PNG
content type default 和 VML content type override，并在 `EditPlan` / planned
output 中把这些 part 暴露为 relationship-derived copy-original entries；API 文档只能把它写成
internal Patch preservation / audit 可见性，不能写成图片/VML/header-footer
public API、语义编辑、relationship repair/pruning、orphan cleanup、content type
repair 或完整 object preservation。
当前还覆盖 worksheet-owned printerSettings opaque part preservation：`sheetData`
局部替换保留 `<pageSetup r:id>` 引用、worksheet `.rels` 中的
`printerSettings` relationship、`xl/printerSettings/printerSettings1.bin` bytes 和
content type override，并在 `EditPlan` / planned output 中把该 part 暴露为
relationship-derived copy-original entry；API 文档只能把它写成 internal Patch
preservation / audit 可见性，不能写成打印设置 public API、语义编辑、
relationship repair/pruning、orphan cleanup、content type repair 或完整 object
lifecycle 支持。
当前还覆盖同一 fixture 的显式 removal audit：移除 `xl/media/background.png`
会输出省略目标 media part、保留 PNG default 且不提升为 override，并保留
worksheet `.rels` 中指向缺失 background picture 的 inbound relationship；移除
`xl/drawings/vmlDrawingHF1.vml` 会输出省略目标 VML part、移除 VML content type
override，并保留 worksheet `.rels` 中指向缺失 header/footer VML 的 inbound
relationship；两条路径都会在 `EditPlan` / planned output 暴露结构化
removed-part inbound relationship audit。API 文档仍只能把它写成 internal Patch
audit / no-pruning 可见性，不能写成图片/VML/header-footer 删除语义、public API、
relationship repair/pruning、orphan cleanup、content type repair 或完整 object
lifecycle 支持。
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
`CalcChainAction::Preserve`，且不发明 VML owner `.rels`。API 文档仍只能把它写成
internal Patch same-path state hygiene / audit
可见性，不能写成事务式 undo、图片/VML/header-footer public API、语义合并或删除、
relationship repair/pruning、orphan cleanup、content type repair 或完整 object
lifecycle 支持。
当前还覆盖 worksheet-owned registered OLE opaque part 与 control-property part
preservation：`sheetData` 局部替换保留 `<oleObjects>` / `<controls>` 引用、
worksheet `.rels` 中的 `oleObject` / `control` relationships、
`xl/embeddings/oleObject1.bin` bytes、`xl/ctrlProps/control1.xml` bytes 和
对应 content type overrides，并在 `EditPlan` / planned output 中把这些 part
暴露为 relationship-derived copy-original entries；API 文档只能把它写成 internal
Patch preservation / audit 可见性，不能写成 OLE / ActiveX / control public API、
语义编辑、relationship repair/pruning、orphan cleanup、content type repair 或完整
object preservation。
当前还覆盖同一 fixture 的显式 removal audit：移除 `xl/embeddings/oleObject1.bin`
会输出省略目标 OLE part、移除 OLE content type override，并保留 worksheet
`.rels` 中指向缺失 OLE object 的 inbound relationship；移除
`xl/ctrlProps/control1.xml` 会输出省略目标 control-property part、移除 control
properties content type override，并保留 worksheet `.rels` 中指向缺失 control
properties 的 inbound relationship；两条路径都会在 `EditPlan` / planned output
暴露结构化 removed-part inbound relationship audit。API 文档仍只能把它写成
internal Patch audit / no-pruning 可见性，不能写成 OLE / ActiveX / control
删除语义、public API、relationship repair/pruning、orphan cleanup、content type
repair 或完整 object lifecycle 支持。
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
`.rels`。API 文档仍只能把它写成
internal Patch same-path state hygiene / audit 可见性，不能写成事务式 undo、OLE /
ActiveX / control public API、语义合并或删除、relationship repair/pruning、
orphan cleanup、content type repair 或完整 object lifecycle 支持。
当前组合回归还验证先排队 worksheet replacement 后再执行 sheetData patch 时，helper
基于当前 planned worksheet bytes 替换，覆盖 queued worksheet 中普通 `<sheetData>` 和
self-closing `<sheetData/>` 两种形态，保留 queued wrapper metadata，不把
source-only worksheet metadata 复活；不要把它写成事务式编辑、metadata repair 或 public API。
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
文档只能把它写成 internal Patch MVP / template-fill 证明，不能写成 public
`PackageEditor`、随机 cell 编辑、sharedStrings 索引迁移、style id 迁移、styles
合并、table/drawing 语义同步或大文件 streaming worksheet transformer。
当前还覆盖源 worksheet 使用 self-closing `<sheetData/>` 的成功替换回归：
输出改为普通 `<sheetData>...</sheetData>`，保留 dimension / autoFilter，沿默认
calcChain remove / fullCalcOnLoad 路径清理 stale 计算 metadata，并保留 unknown bytes。
当前还覆盖 replacement payload 自身为 self-closing `<sheetData/>` 的成功替换回归：
可清空旧 row/cell，输出保留 `<sheetData/>` 和外围 dimension / autoFilter，
并继续沿默认 calcChain remove / fullCalcOnLoad 与 unknown bytes preservation 路径。
当前还覆盖 source worksheet 和 replacement payload 使用 `<x:worksheet>` /
`<x:sheetData>` 前缀形式时的成功替换回归：按 local-name 匹配，输出保留原
wrapper / replacement 字面前缀，仍沿默认 calcChain cleanup 与 unknown bytes
preservation 路径；不能写成通用 namespace repair。
replacement `<sheetData>` 自身若使用 shared string indexes、style id references
或公式 cell，也只追加 audit notes 和结构化 `WorksheetPayloadDependencyAudit`，
提示 caller 复核 `xl/sharedStrings.xml`、
  `xl/styles.xml`、workbook calc metadata 和 calcChain policy；它不迁移 sharedStrings
  索引、不合并 styles、不计算公式，也不重建 calcChain。
  invalid/malformed replacement XML、source worksheet 缺失 `sheetData`，以及 source worksheet
  `<sheetData>` 起始标签存在但闭合标签损坏/缺失时，只能写成失败不污染
  EditPlan、manifest、package-entry audit、calc policy 或 copied output bytes；不能写成
  XML repair。
  文档只能把它写成 internal Patch MVP / template-fill 小切片，不需要 public Doxygen，
  也不能写成 public API、随机 cell 编辑、range 修复、dataValidations /
  conditionalFormatting / hyperlinks / table / drawing 语义同步、sharedStrings/styles
  迁移或大文件低内存 transformer；当前还支持内部
  `PackageEditor::request_full_calculation()` workbook-only calc metadata helper，只局部
  重写 `/xl/workbook.xml` 来设置 `fullCalcOnLoad="1"`，并可按
  `CalcChainAction::Remove/Preserve` 清理或保留 calcChain payload、content type、
  workbook relationship 和 calcChain owner `.rels` 审计；Remove 路径还覆盖没有
  `xl/calcChain.xml` payload 但残留 calcChain content type override 或 workbook
  relationship 的 metadata-only cleanup，不创建 payload 或 removed-part audit；
  `CalcChainAction::Rebuild`
  未实现且失败不污染 edit plan、manifest、package-entry audit 或输出包。文档只能把它
  写成 internal small-part Patch helper，不能写成 public API、公式求值、
  calcChain rebuild、worksheet rewrite 或通用 relationship/content-type repair；
  DEFLATE 源包回归中，与 workbook calc helper 无直接因果关系的 unknown owner
  `.rels` 只能写成通过 `planned_output()` copy-original 可见性和输出重读验证保留，
  不能写成 `EditPlan` package-entry side-effect audit。
  文档还要说明该 helper 只更新 workbook 根元素的直接子级 `calcPr`，保留 `extLst`
  或 custom extension 内的嵌套同名 decoy；没有直接子级时才在真实 workbook closing
  tag 前插入按根前缀命名的 `calcPr`，这不是 XML schema validation、namespace
  repair 或 workbook metadata DOM；
  当前还有 prior ordinary workbook / calcChain replacement 后调用 workbook-only
  `request_full_calculation()` 的组合回归：helper 会使用已排队 workbook XML，
  保留其中非 calc workbook metadata，规范 `fullCalcOnLoad="1"` 并清理
  calcChain payload/content type/workbook relationship；此前排队的 calcChain
  replacement 不会在输出包复活，也不回退到 source workbook bytes。
  若同一路径指定 `CalcChainAction::Preserve`，此前排队的 calcChain replacement
  会保持为 active `LocalDomRewrite` 并作为最终 `xl/calcChain.xml` payload 写出，
  同时只保留 calcChain owner `.rels` copy-original audit；文档仍必须说明这不是
  calcChain rebuild、公式求值或 public editing API。
  worksheet replacement 会把 workbook metadata rewrite 同步为
  `LocalDomRewrite`；内部 `EditPlan` 还会记录 `[Content_Types].xml`、
  package `_rels/.rels`、workbook `.rels` rewrite、removed calcChain owner `.rels`
  omission，以及 preserved source-owned `.rels` 存在时的 copy-original package-entry 审计项，
  供 Patch 审计；该审计也覆盖 ordinary owner-part replacement 的根级
  `_rels/foo.xml.rels`，并有 roundtrip 回归验证其可由 `PackageReader` /
  `RelationshipGraph` 重新挂回 owner part；另有 reader-only 回归验证 unknown
  extension owner `.rels` 在 editor roundtrip 前即可挂回 owner；`CalcChainAction::Preserve` 下保留的 calcChain
  owner `.rels`，以及 workbook metadata rewrite 时被原样保留的 workbook `.rels`；
  这些 package-entry audit 是内部结构化 side-effect metadata，分类为 content
  types、package relationships 和 source-owned relationships；只有 source-owned
  relationships 携带 owner part；kind 与 entry path 必须一一匹配：
  `ContentTypes` 指向 `[Content_Types].xml`，`PackageRelationships` 指向
  package `_rels/.rels`，`SourceRelationships` 指向 owner-derived `.rels` entry。
  审查文档时不得写成完整 metadata editor。
  普通 `replace_part()` 会拒绝 `[Content_Types].xml`、package `_rels/.rels` 和
  source-owned `.rels` metadata entry 作为 ordinary part replacement target，以避免
  Patch API 绕过窄 metadata helper 和 package-entry audit；这不能写成完整
  relationship/content-type mutator。metadata-entry replacement 拒绝回归也覆盖
  edit plan entries / notes、package-entry audit、calc policy、manifest write-mode
  、aggregate `planned_output()` / legacy output-entry preview 和 copied output bytes
  不污染。重复 ordinary part replacement 回归还覆盖最终
  bytes、write mode、edit-plan reason、manifest state 和 preserved source-owned `.rels`
  audit upsert；内部 `EditPlan` 还覆盖 part-level set/remove 互斥，restored active
  part 会清理 stale removed-part audit，已有 relationship-derived entry 改成
  rewrite/generate entry 时也会清理 stale relationship metadata；package-entry set/remove
  也有互斥回归，避免同一 metadata entry 同时存在 active 和 removed 审计。docProps generated-small-XML 被后续 ordinary replacement 覆盖时，
  最终 part bytes、EditPlan 和 manifest 采用 ordinary replacement，content types /
  package relationships 仍由 helper 路径维护和审计。反向顺序回归也覆盖后调用的
  docProps metadata helper 接管此前 ordinary replacement 或 explicit removal 的
  core/app part，并清理 stale removal / omitted payload 状态；这只恢复 helper 负责的
  core/app payload、content types 和 package relationships，不恢复此前 removal 省略的
  docProps owner `.rels`；当前结构回归验证输出包继续省略该 owner `.rels`，并保留
  removed package-entry audit，也不是事务式 undo。还有回归覆盖
  worksheet replacement 删除 calcChain 时压过此前 ordinary calcChain replacement，并接管此前
  ordinary workbook replacement 以写入 helper-generated fullCalcOnLoad metadata；若后续
  ordinary `replace_part("/xl/workbook.xml", ...)` 发生在 worksheet rewrite 请求
  fullCalcOnLoad / calcChain removal 之后，仍会保留该 calc policy、把
  `fullCalcOnLoad` 规范为 `1`，并避免把已重写的 workbook `.rels`
  package-entry audit 降级为 copy-original。
  组合回归还覆盖 docProps generated-small-XML
  与 worksheet replacement 共存时的 relationship/content-type 状态合并、
  calcChain removal、stale calcChain owner `.rels` omission、workbook metadata
  rewrite、unknown entry preservation、exact/path-equivalent source-overwrite
  rejection 和 empty-output / missing-parent / non-directory-parent / existing-directory output rejection；core/app docProps
  package relationship target 冲突失败也只覆盖不污染 edit plan entries / notes、manifest /
  package-entry audit / copied output；malformed workbook metadata / workbook calc
  metadata preflight failure 和 invalid replacement failure 只覆盖
  edit plan entries / notes、aggregate `planned_output()` / legacy output-entry preview、
  manifest write-mode / copied output bytes 不污染；这些拒绝只能写成
  reader-backed copy 输出安全护栏，不能写成 atomic in-place editing、filesystem
  repair 或 public editing API；现在还可说明 guard 会在 materialize output
  entries 前失败，且失败后 queued part replacement 保留在 `EditPlan` / manifest / planned
  output 中，后续安全 `save_as()` 仍输出 queued rewrite 并保留 untouched
  worksheet / unknown bytes；同一 guard 也覆盖 queued worksheet replacement 的
  `fullCalcOnLoad` / calcChain removal / package-entry audit / planned output 状态，
  后续安全输出仍按计划省略 calcChain，但不能写成 transaction 或 atomic save；
  no-op `PackageEditor::save_as()` roundtrip 只能写成 linked-object fixture 全部源
  entries 的 entry order、stored entry method / CRC / uncompressed size 和 bytes copy
  baseline，以及初始 copy-original plan 没有 metadata package-entry side effect；不能写成
  broad unknown part preservation 或 public editing API readiness；
  `PackageEditor::planned_output()` 只能写成内部 Patch audit visibility：
  它复用 `save_as()` 的聚合 output-plan snapshot，并记录 entry-level 决策顺序、
  全局 `full_calculation_on_load` / `calc_chain_action`、audit notes、结构化
  `removed_parts` / `removed_package_entries`，以及结构化
  `relationship_target_audits` / `worksheet_relationship_reference_audits`；
  兼容入口 `planned_output_entries()` 仍返回同一份
  entry list。entry 决策记录 `source_entry` / `generated` flags、`package_part` /
  `part_name` classification、write mode、copied-from-source / omitted flags、
  package-entry audit kind、owner part、relationship-derived owner/id/type/target、
  omitted removed-part inbound relationship audit 和 reason；
  当前回归覆盖 no-op copy-original、docProps generated-small-XML 新增/重写、worksheet
  calcChain omission 与 workbook metadata rewrite、sheetData Patch MVP 的 output-plan
  snapshot（worksheet stream-rewrite、workbook metadata rewrite、calcChain omission、
  metadata-entry audit、preserved source-owned `.rels` 和 relationship-derived
  linked parts）、ordinary unknown extension replacement 的 output-plan snapshot
  （target part stream-rewrite、owner `.rels` copy-original audit 和 untouched
  linked parts）、VBA project remove-then-replace output-plan 的 active VBA project
  stream-rewrite / content types copy-original / preserved package/workbook relationships /
  preserved linked/unknown entries / empty removed_parts 与 removed_package_entries /
  no invented owner `.rels` 快照、VBA project
  replace-then-remove final-removal 的 omitted VBA
  project part / removed_parts target-reason-inbound audit / workbook inbound
  relationship audit / content types rewrite / empty removed_package_entries /
  no invented owner `.rels` 快照、VML drawing remove-then-replace output-plan 的
  active VML drawing local-DOM rewrite / content types copy-original /
  preserved package/workbook/worksheet/drawing relationships / preserved
  linked/unknown entries / no invented owner `.rels` 快照、percent-decoded drawing
  remove-then-replace output-plan 的 active decoded drawing local-DOM rewrite /
  content types copy-original / preserved package/workbook/worksheet/drawing
  relationships / preserved linked/unknown entries / no invented owner `.rels`
  快照、chart remove-then-replace 的 active chart
  stream-rewrite / content types copy-original / preserved drawing relationships /
  empty removed_parts 与 removed_package_entries / no invented owner `.rels`
  快照、request-recalculation preserve-policy output snapshot，以及显式
  workbook removal 的 owner `.rels` omission、linked worksheet rewrite 的
  relationship-derived output audit 和显式 chart removal 的 removed-part inbound
  audit；metadata entries 如 `[Content_Types].xml`、package `_rels/.rels`
  和 source-owned `.rels` 不会暴露为 package part；不能写成 public editor API、complete output planner 或 general
  relationship/content-type mutator；
  DEFLATE source input 只能写成未修改 part 的解压后 payload 语义保留；当前
  minizip-enabled PackageEditor 回归覆盖 DEFLATE source 上 ordinary workbook
  replacement、unknown extension target replacement、workbook calc metadata helper，以及
  worksheet replacement 下的 calcChain cleanup、linked payload
  preservation 和 unknown extension owner `.rels` 可由输出 `PackageReader` /
  `RelationshipGraph` 重读；不能写成保留源 ZIP compression method、timestamps、
  extra fields 或压缩字节；
  ordinary single-part replacement coverage 只能写成目标 entry 原位重写、其它源
  entries 保持 entry order、stored entry method / CRC / uncompressed size 和 bytes；
  不能写成 complete package rewrite 或 broad safe editing；
  linked-object fixture 上的 ordinary workbook replacement coverage 只能写成只重写
  `xl/workbook.xml`、workbook `.rels` copy-original audit、其它 linked/unknown
  source entries 保持 copy-original baseline；不能写成 workbook metadata sync、
  defined-name policy 或 object editing；
  linked-object fixture 上的 ordinary drawing replacement coverage 只能写成只重写
  `xl/drawings/drawing1.xml`、drawing `.rels` copy-original audit、chart/media/unknown
  source entries 保持 copy-original baseline；不能写成 drawing mutation、image editing、
  chart editing 或 full object preservation；
  linked-object fixture 上的 ordinary unknown extension replacement coverage 只能写成只重写
  `custom/opaque-extension.bin`、其 owner `.rels` copy-original audit 和原样保留、
  workbook/worksheet/drawing/chart/media source entries 保持 copy-original baseline；
  不能写成 unknown extension 语义编辑、custom relationship repair、metadata editor
  或 public editing API；
  linked-object fixture 上同一 unknown extension 的 repeated ordinary replacement
  coverage 只能写成最终 bytes、manifest write-mode、edit-plan reason 和 owner
  `.rels` audit upsert 到最后一次替换状态，owner `.rels` 继续 copy-original，且没有
  removed-part / removed package-entry audit；不能写成 transactional editing、
  unknown extension semantic merging 或 metadata repair；
  linked-object fixture 上的 unknown extension remove-then-ordinary-replace coverage
  只能写成后续 replacement 恢复 active unknown extension part、清理 stale
  removed-part audit 和 stale removed owner `.rels` audit、恢复 owner `.rels`
  copy-original package-entry audit、保留 worksheet `.rels` 中的 inbound unknown
  relationship、保留其它 linked/source entries，且不重写 `[Content_Types].xml`；
  不能写成 unknown extension semantic merge、custom relationship repair、metadata
  repair、transactional undo 或 public editing API；
  linked-object fixture 上的 unknown extension ordinary-replace-then-remove coverage
  只能写成后续 removal 清理 active replacement、记录 removed-part 和 removed owner
  `.rels` audit、输出省略 unknown extension part 及其 owner `.rels`、保留
  worksheet `.rels` 中指向缺失 part 的 inbound unknown relationship、保留其它
  linked/source entries 和默认 `bin` content type，且不重写 `[Content_Types].xml`；
  不能写成 unknown extension deletion semantics、custom relationship repair、metadata
  repair、relationship pruning/repair、content type repair、orphan cleanup、
  transactional undo 或 public editing API；
  unknown extension ordinary-replace-then-remove output-plan coverage 只能写成内部
  `planned_output()` 暴露 omitted `custom/opaque-extension.bin` part、omitted source-owned
  `custom/_rels/opaque-extension.bin.rels` metadata、worksheet-owned inbound relationship
  metadata、preserved `[Content_Types].xml` 和 package `_rels/.rels` copy-original
  快照；不能写成 public output planner、unknown extension deletion semantics、
  custom relationship repair、metadata repair、relationship pruning/repair、content type
  repair、orphan cleanup、transactional undo 或 public editing API；
  内部 `PackageEditor::remove_part()` coverage 只能写成显式 registered-part removal
  窄切片：只接受源 package 中已有的普通 part，输出省略目标 part 和存在时的
  source-owned owner `.rels`，记录 removed-part / removed package-entry audit，并在目标
  存在 content type override 时重写 `[Content_Types].xml`；不能写成 inbound relationship
  pruning、object deletion、通用删除 API、transactional editing 或 public editing API；
  removed-part audit 现在会同时保留结构化 inbound relationship metadata（owner
  entry、owner part、id、type、raw target、normalized target part）和可读 reason，
  记录仍指向被删除 part 的 inbound package/source relationship 上下文，只能写成
  Patch traceability，不是修复；
  malformed percent relationship target 只能写成 removed-part inbound 扫描阶段的
  EditPlan / planned output audit note、byte-preserved `.rels`、removed target
  omission 和 copy-original metadata entry 证据，不能写成 target repair、
  relationship validation、结构化 relationship target audit 或自动校正；
  workbook removal coverage 只能写成 `xl/workbook.xml` 的显式移除：输出省略
  workbook part 和 source-owned workbook `.rels`、移除 workbook content type
  override、保留 package `_rels/.rels` inbound officeDocument relationship、且不修剪
  worksheet/drawing/table/sharedStrings/styles/VBA/calcChain 或 unknown extension 等
  downstream/source parts；不能写成 workbook deletion、sheet catalog sync、
  relationship repair、full workbook editing 或 public workbook editing API；
  workbook replace-then-remove ordering coverage 只能写成后续 explicit removal 清理
  active workbook replacement、记录 removed-part 和 workbook owner `.rels` omission audit、
  输出省略 workbook part 及 owner `.rels`、移除 workbook content type override、保留
  package `_rels/.rels` 中指向缺失 workbook 的 officeDocument relationship 以及
  worksheet/drawing/table/sharedStrings/styles/VBA/calcChain/unknown downstream parts；
  不能写成 workbook deletion semantics、sheet catalog sync、relationship/content type
  repair、orphan cleanup、transactional undo 或 public API；
  worksheet removal coverage 只能写成 `xl/worksheets/sheet1.xml` 的显式移除：
  输出省略 worksheet part 和 source-owned worksheet `.rels`、移除 worksheet content
  type override、保留 workbook `.rels` inbound worksheet relationship、且不修剪
  drawing/table/sharedStrings/styles/VBA/calcChain 或 unknown extension 等
  downstream/source parts；不能写成 sheet delete、workbook sheet catalog sync、
  relationship repair、full worksheet editing 或 public sheet editing API；
  worksheet replace-then-remove ordering coverage 只能写成后续 explicit removal 清理
  active worksheet replacement、记录 removed-part 和 worksheet owner `.rels`
  omission audit、输出省略 worksheet part 及其 owner `.rels`、移除 worksheet
  content type override、保留 workbook `.rels` 中指向缺失 worksheet 的 inbound
  worksheet relationship，以及 drawing/chart/media/table/sharedStrings/styles/VBA/
  calcChain/unknown downstream/source parts；不能写成 sheet delete、workbook sheet
  catalog sync、relationship/content type repair、orphan cleanup、transactional undo
  或 public API；
  drawing removal coverage 只能写成 `xl/drawings/drawing1.xml` 的显式移除：
  输出省略 drawing part 和 source-owned drawing `.rels`、移除 drawing content type
  override、保留 worksheet `.rels` direct / URI-qualified inbound drawing
  relationships、且不修剪 chart/media 或其它 downstream parts；不能写成 drawing
  mutation、object deletion、relationship repair、full drawing support 或 public
  drawing editing API；
  drawing replace-then-remove ordering coverage 只能写成后续 explicit removal 清理
  active drawing replacement、记录 removed-part 和 drawing owner `.rels` omission
  audit、输出省略 drawing part 及其 owner `.rels`、移除 drawing content type
  override、保留 worksheet `.rels` 中 direct / URI-qualified inbound drawing
  relationships，以及 chart/media/table/VML/percent-decoded drawing/sharedStrings/
  styles/VBA/calcChain/unknown downstream/source parts；不能写成 drawing mutation、
  object deletion、relationship/content type repair、orphan cleanup、transactional
  undo 或 public API；
  drawing replace-then-remove output-plan coverage 只能写成内部 `planned_output()`
  暴露 omitted drawing part、omitted drawing owner `.rels`、content types rewrite 和
  preserved inbound worksheet relationship audit；不能写成 public output planner、
  relationship/content type mutator、relationship pruning/repair 或 drawing editing API；
  VML drawing replace-then-remove output-plan coverage 只能写成内部 `planned_output()`
  暴露 omitted `xl/drawings/vmlDrawing1.vml` part、removed_parts
  target/reason/inbound audit、URI-qualified worksheet inbound relationship
  metadata、content types rewrite、empty removed_package_entries，以及没有凭空创建
  `xl/drawings/_rels/vmlDrawing1.vml.rels`；不能写成 public output planner、
  VML shape editing、legacy drawing mutation、relationship repair、orphan cleanup
  或 complete VML/drawing support；
  VML drawing remove-then-replace output-plan coverage 只能写成内部 `planned_output()`
  暴露 active `xl/drawings/vmlDrawing1.vml` `LocalDomRewrite` entry、content types
  copy-original audit、preserved package/workbook/worksheet/drawing relationships、
  preserved linked/unknown entries、empty removed_parts / removed_package_entries，
  以及没有凭空创建
  `xl/drawings/_rels/vmlDrawing1.vml.rels`；不能写成 public output planner、
  VML shape editing、legacy drawing mutation、transactional undo、relationship repair、
  content type repair、full VML/drawing support 或 public drawing editing API；
  percent-decoded drawing replace-then-remove output-plan coverage 只能写成内部
  `planned_output()` 暴露 omitted `xl/drawings/drawing space.xml` part、removed_parts
  target/reason/inbound audit、encoded worksheet inbound relationship metadata、
  content types rewrite、empty removed_package_entries，以及没有凭空创建
  `xl/drawings/_rels/drawing space.xml.rels`；不能写成 public output planner、
  percent-encoded target rewrite、relationship repair、orphan cleanup 或 drawing
  editing API；
  percent-decoded drawing remove-then-replace output-plan coverage 只能写成内部
  `planned_output()` 暴露 active `xl/drawings/drawing space.xml` `LocalDomRewrite`
  entry、content types copy-original audit、preserved package/workbook/worksheet/
  drawing relationships、preserved linked/unknown entries，以及没有凭空创建
  `xl/drawings/_rels/drawing space.xml.rels`；不能写成 public output planner、
  percent-encoded target repair、relationship rewrite、relationship repair、content
  type repair、transactional undo、full drawing support 或 public drawing editing API；
  media removal coverage 只能写成 default-typed `xl/media/image1.png` 的显式移除：
  输出省略 media entry、保留 PNG default content type 和 drawing `.rels` inbound
  image relationship、不凭空创建 media owner `.rels` omission；不能写成
  existing-workbook image editing、relationship repair 或完整图片保真；
  table removal coverage 只能写成 `xl/tables/table1.xml` 的显式移除：
  输出省略 table entry、移除 table content type override、保留 worksheet `.rels`
  inbound table relationship、不凭空创建 table owner `.rels` omission；不能写成
  relationship repair、object deletion、table resize、existing-workbook table editing
  或 full table support；
  sharedStrings removal coverage 只能写成 `xl/sharedStrings.xml` 的显式移除：
  输出省略 sharedStrings part 和 sharedStrings owner `.rels`、移除 sharedStrings
  content type override、保留 workbook `.rels` inbound sharedStrings relationship；
  不能写成 sharedStrings index migration、string-table rebuild、worksheet cell-reference
  sync、relationship repair、existing-workbook sharedStrings semantic editing 或 public
  sharedStrings editing API；
  styles removal coverage 只能写成 `xl/styles.xml` 的显式移除：输出省略 styles
  part、移除 styles content type override、保留 workbook `.rels` inbound styles
  relationship、不凭空创建 styles owner `.rels` omission；不能写成 style id
  migration、style merge、cell `s` reference sync、relationship repair、
  existing-file style preservation、full style editing 或 public styles editing API；
  VBA project removal coverage 只能写成 `xl/vbaProject.bin` 的显式移除：输出省略
  VBA project part、移除 VBA content type override、保留 workbook `.rels` inbound
  VBA relationship、不凭空创建 VBA owner `.rels` omission；不能写成 macro generation、
  VBA semantic editing、signature preservation、relationship repair、complete macro
  support 或 public VBA editing API；
  VML drawing removal coverage 只能写成 `xl/drawings/vmlDrawing1.vml` 的显式移除：
  输出省略 VML drawing part、移除 VML content type override、保留 worksheet `.rels`
  URI-qualified inbound `vmlDrawing` relationship、不凭空创建 VML owner `.rels`
  omission；不能写成 VML shape editing、legacy drawing mutation、relationship
  repair、full VML/drawing support 或 public drawing editing API；
  percent-decoded drawing removal coverage 只能写成 `xl/drawings/drawing space.xml`
  的显式移除：输出省略目标 drawing part、移除 drawing content type override、保留
  worksheet `.rels` 原始 `../drawings/drawing%20space.xml` inbound relationship、
  不凭空创建 `xl/drawings/_rels/drawing space.xml.rels`；不能写成
  percent-encoded target repair、relationship rewrite、drawing mutation、full drawing
  support 或 public drawing editing API；
  remove coverage 还只能写成后调用的 `remove_part()` 压过此前 ordinary replacement、
  清理 stale replacement state，并以 removed-part audit / content type cleanup 为最终状态；
  invalid removal failure 只能写成 edit-plan entries/notes、package-entry audit、removed
  audit、calc policy、manifest write-mode、aggregate `planned_output()` / legacy
  output-entry preview 和 copied output bytes 不污染；
  反向顺序 coverage 只能写成对源 package 中已有的普通 part，后调用的 ordinary
  `replace_part()` 可恢复此前 `remove_part()` 的目标为 active replacement，
  清理 stale removed-part / removed owner `.rels` audit 与 omitted entry 状态，
  并把存在的 source-owned `.rels` 重新记录为 copy-original audit；对带 content type
  override 的 part，只能写成恢复后 `[Content_Types].xml` 回到 source bytes /
  copy-original audit；workbook-specific 反向顺序 coverage 只能写成显式移除
  `xl/workbook.xml` 后再 ordinary `replace_part()` 恢复 workbook active replacement、
  source-owned workbook `.rels` copy-original audit、package `_rels/.rels` inbound
  officeDocument relationship 保留，以及 `[Content_Types].xml` 回到 source bytes /
  copy-original audit；worksheet-specific 反向顺序 coverage 只能写成显式移除
  `xl/worksheets/sheet1.xml` 后再 ordinary `replace_part()` 恢复 worksheet active
  replacement、source-owned worksheet `.rels` copy-original audit、workbook `.rels`
  inbound worksheet relationship 保留，以及 `[Content_Types].xml` 回到 source bytes /
  copy-original audit；drawing-specific 反向顺序 coverage 只能写成显式移除
  `xl/drawings/drawing1.xml` 后再 ordinary `replace_part()` 恢复 drawing active
  replacement、source-owned drawing `.rels` copy-original audit、worksheet `.rels`
  direct / URI-qualified inbound drawing relationships 保留，以及 `[Content_Types].xml`
  回到 source bytes / copy-original audit；不能写成 drawing mutation、object deletion、
  transactional undo、relationship repair、
  content type repair、semantic merge 或 public editing API；sharedStrings-specific
  反向顺序 coverage 只能写成显式移除 `xl/sharedStrings.xml` 后再 ordinary
  `replace_part()` 恢复 sharedStrings active replacement、source-owned
  sharedStrings `.rels` copy-original audit、workbook `.rels` inbound sharedStrings
  relationship 保留，以及 `[Content_Types].xml` 回到 source bytes / copy-original
  audit；不能写成 sharedStrings index migration、string-table rebuild、worksheet
  cell-reference sync、transactional undo、relationship repair、content type repair、
  semantic merge 或 public editing API；styles-specific 反向顺序 coverage 只能写成
  显式移除 `xl/styles.xml` 后再 ordinary `replace_part()` 恢复 styles active
  replacement、workbook `.rels` inbound styles relationship 保留、不凭空创建
  styles owner `.rels`，以及 `[Content_Types].xml` 回到 source bytes /
  copy-original audit；不能写成 style id migration、style merge、cell `s`
  reference sync、transactional undo、relationship repair、content type repair、
  semantic merge、existing-file style preservation 或 public editing API；
  linked-object fixture 上的 ordinary media replacement coverage 只能写成只重写
  `xl/media/image1.png`、drawing `.rels` 原样保留、PNG default content type 不提升为
  override、workbook/worksheet/drawing/chart/unknown source entries 保持 copy-original
  baseline；不能写成 image decoding、drawing mutation、existing-workbook image editing
  或 full image preservation；
  linked-object fixture 上的 media remove-then-ordinary-replace coverage 只能写成
  后续 replacement 恢复 active media part、清理 stale removed-part audit、保留 PNG
  default content type 且不把 `xl/media/image1.png` 提升成 override、保留 inbound
  drawing `.rels`、不凭空创建 media owner `.rels`；不能写成 transactional undo、
  image semantic merge、relationship repair、content type repair 或 full image
  preservation；
  linked-object fixture 上的 media ordinary-replace-then-remove coverage 只能写成
  后续 removal 清理 active media replacement、记录 removed-part audit 和 inbound
  drawing relationship metadata、输出省略 `xl/media/image1.png`、保留 PNG default
  content type 且不把 media 提升成 override、保留 inbound drawing `.rels`、不凭空创建
  media owner `.rels`；不能写成 transactional undo、image semantic merge、
  relationship pruning/repair、content type repair、existing-workbook image editing 或
  full image preservation；
  media ordinary-replace-then-remove output-plan coverage 只能写成内部
  `planned_output()` 暴露 omitted default-typed media part、drawing-owned inbound
  relationship audit、removed_parts target/reason/inbound audit、content types /
  drawing `.rels` copy-original、empty removed_package_entries，以及没有 media
  owner `.rels` 条目；不能写成 public output planner、relationship/content type
  mutator、relationship pruning/repair 或 image editing API；
  linked-object fixture 上的 ordinary table replacement coverage 只能写成只重写
  `xl/tables/table1.xml`、worksheet `.rels` 原样保留、table content type override
  仍可读、workbook/worksheet/drawing/chart/media/unknown source entries 保持
  copy-original baseline；不能写成 table resize、calculated columns、totals generation、
  existing-workbook table editing 或 full table support；
  linked-object fixture 上的 table remove-then-ordinary-replace coverage 只能写成
  后续 replacement 恢复 active table part、清理 stale removed-part audit、让
  `[Content_Types].xml` 回到 source/copy-original audit、保留 worksheet `.rels`
  inbound table relationship、不凭空创建 table owner `.rels`；不能写成 table resize、
  calculated columns、totals generation、transactional undo、relationship repair、
  content type repair、existing-workbook table editing 或 full table support；
  linked-object fixture 上的 table ordinary-replace-then-remove coverage 只能写成
  后续 explicit removal 清理 active table replacement、记录 removed-part audit 和
  inbound worksheet relationship metadata、输出省略 table part、移除 table content
  type override、保留 worksheet `.rels` inbound table relationship、不凭空创建 table
  owner `.rels`；不能写成 table delete semantics、table resize、calculated columns、
  totals generation、transactional undo、relationship pruning/repair、content type
  repair、existing-workbook table editing 或 full table support；
  table ordinary-replace-then-remove output-plan coverage 只能写成内部
  `planned_output()` 暴露 omitted table part、worksheet-owned inbound relationship
  audit、content types local-DOM rewrite，以及没有 table owner `.rels` 条目；不能写成
  public output planner、relationship/content type mutator、table delete semantics、
  relationship pruning/repair 或 table editing API；
  linked-object fixture 上的 ordinary sharedStrings replacement coverage 只能写成只重写
  `xl/sharedStrings.xml`、workbook `.rels` 原样保留、sharedStrings owner `.rels`
  原样保留、sharedStrings content type override 仍可读、styles/table/media/VBA/
  unknown source entries 保持 copy-original baseline；不能写成 sharedStrings index
  migration、string-table rebuild、worksheet cell-reference sync、existing-workbook
  sharedStrings semantic editing 或 public sharedStrings editing API；
  linked-object fixture 上的 sharedStrings ordinary-replace-then-remove coverage 只能写成
  后续 removal 清理 active sharedStrings replacement、记录 removed-part audit、输出省略
  `xl/sharedStrings.xml` 及其 source-owned owner `.rels`、移除 sharedStrings content
  type override、保留 workbook `.rels` inbound sharedStrings relationship；不能写成
  sharedStrings index migration、string-table rebuild、worksheet cell-reference sync、
  transactional undo、relationship pruning/repair、content type repair、existing-workbook
  sharedStrings semantic editing 或 public sharedStrings editing API；
  sharedStrings ordinary-replace-then-remove output-plan coverage 只能写成内部
  `planned_output()` 暴露 final omitted `xl/sharedStrings.xml` part、source-owned
  owner `.rels` omission、removed_parts target/reason/inbound audit、
  removed_package_entries owner-omission audit、workbook inbound relationship
  metadata 和 content types rewrite；不能写成 metadata repair、relationship pruning、transactional undo 或
  public sharedStrings editing API；
  sharedStrings remove-then-ordinary-replace output-plan coverage 只能写成内部
  `planned_output()` 暴露 active `xl/sharedStrings.xml` stream-rewrite、source-owned
  owner `.rels` copy-original audit、content types copy-original audit、preserved
  workbook relationships、empty removed_parts / removed_package_entries 和 untouched
  linked entries；不能写成 sharedStrings index
  migration、string-table rebuild、worksheet cell-reference sync、relationship repair、
  transactional undo 或 public sharedStrings editing API；
  linked-object fixture 上的 ordinary styles replacement coverage 只能写成只重写
  `xl/styles.xml`、workbook `.rels` 原样保留、styles content type override 仍可读、
  不凭空创建 `xl/_rels/styles.xml.rels`、sharedStrings/table/media/VBA/unknown
  source entries 保持 copy-original baseline；不能写成 style id migration、style
  merge、cell `s` reference sync、existing-file style preservation、full style
  editing 或 public style editing API；
  linked-object fixture 上的 styles ordinary-replace-then-remove coverage 只能写成
  后续 removal 清理 active styles replacement、记录 removed-part audit、输出省略
  `xl/styles.xml`、移除 styles content type override、保留 workbook `.rels` inbound
  styles relationship，且不凭空创建 `xl/_rels/styles.xml.rels`；不能写成 style id
  migration、style merge、cell `s` reference sync、existing-file style preservation、
  transactional undo、relationship pruning/repair、content type repair、full style
  editing 或 public style editing API；
  styles replace-then-remove output-plan coverage 只能写成内部 `planned_output()`
  暴露 omitted `xl/styles.xml` part、removed_parts target/reason/inbound audit、
  workbook-owned inbound relationship metadata、content types rewrite、empty
  removed_package_entries，以及没有凭空创建 `xl/_rels/styles.xml.rels`；不能写成
  public output planner、style id migration、style merge、cell `s` reference sync、
  relationship repair、existing-file style preservation 或 complete style editing
  support；
  styles remove-then-ordinary-replace output-plan coverage 只能写成内部
  `planned_output()` 暴露 active `xl/styles.xml` local-DOM rewrite、content types
  copy-original audit、preserved workbook relationships、empty removed_parts /
  removed_package_entries、untouched linked entries，
  且不凭空创建 `xl/_rels/styles.xml.rels`；不能写成 public output planner、style id
  migration、style merge、cell `s` reference sync、relationship repair、
  existing-file style preservation 或 complete style editing support；
  linked-object fixture 上的 chart remove-then-ordinary-replace coverage 只能写成
  后续 replacement 恢复 active chart part、清理 stale removed-part audit、让
  `[Content_Types].xml` 回到 source/copy-original audit、保留 drawing `.rels` 里的
  direct / URI-qualified inbound chart relationships、保留其它 linked/unknown source
  entries，且不凭空创建 chart owner `.rels`；不能写成 chart semantic merge、
  chart reference repair、relationship repair、content type repair、transactional undo、
  existing-workbook chart editing 或 public chart editing API；
  chart remove-then-replace output-plan coverage 只能写成内部 `planned_output()`
  暴露 active `xl/charts/chart1.xml` stream-rewrite、content types copy-original
  audit、preserved drawing relationships、preserved linked/unknown entries、empty
  removed_parts / removed_package_entries，以及没有凭空创建
  `xl/charts/_rels/chart1.xml.rels`；不能写成 public output planner、
  chart semantic merge、chart reference repair、relationship repair、transactional undo
  或 complete chart editing support；
  linked-object fixture 上的 ordinary chart replacement coverage 只能写成只重写
  `xl/charts/chart1.xml`、drawing `.rels` 里的 chart 和 URI-qualified chart
  relationships 原样保留、chart content type override 仍可读、不凭空创建 chart
  owner `.rels`、media/table/sharedStrings/styles/VBA/unknown source entries 保持
  copy-original baseline；不能写成 chart reference migration、series/cache update、
  drawing mutation、existing-workbook chart editing、full chart support 或 public chart
  editing API；
  linked-object fixture 上的 chart 先 ordinary replacement 再显式移除 coverage 只能写成
  后续 removal 清理 active chart replacement、记录 removed-part audit 和 direct /
  URI-qualified inbound drawing relationship metadata、输出省略 `xl/charts/chart1.xml`、
  移除 chart content type override、保留 inbound drawing `.rels` 和其它 linked/unknown
  source entries，且不凭空创建 chart owner `.rels`；不能写成 chart delete semantics、
  chart reference repair、relationship pruning/repair、content type repair、transactional
  undo、semantic merge、existing-workbook chart editing 或 public chart editing API；
  chart replace-then-remove output-plan coverage 只能写成内部 `planned_output()`
  暴露 omitted `xl/charts/chart1.xml` part、removed_parts target/reason/inbound audit、
  drawing-owned direct / URI-qualified inbound relationship metadata、content types
  rewrite、empty removed_package_entries，以及没有凭空创建
  `xl/charts/_rels/chart1.xml.rels`；不能写成 public output planner、chart delete
  semantics、chart reference repair、relationship repair、orphan cleanup 或 complete
  chart editing support；
  linked-object fixture 上的 ordinary VBA project replacement coverage 只能写成只重写
  `xl/vbaProject.bin`、workbook `.rels` 原样保留、VBA content type override 仍可读、
  不凭空创建 `xl/_rels/vbaProject.bin.rels`、worksheet/drawing/chart/media/table/
  sharedStrings/styles/calcChain/unknown source entries 保持 copy-original baseline；
  不能写成 macro generation、VBA semantic editing、signature preservation、
  workbook relationship repair、full macro support 或 public macro editing API；
  linked-object fixture 上的 VBA project remove-then-ordinary-replace coverage 只能写成
  后续 replacement 恢复 active VBA project part、清理 stale removed-part audit、让
  `[Content_Types].xml` 回到 source/copy-original audit、保留 workbook `.rels` inbound
  VBA relationship、且不凭空创建 `xl/_rels/vbaProject.bin.rels`；不能写成 macro
  generation、VBA semantic editing、signature preservation、transactional undo、
  workbook relationship repair、content type repair、full macro support 或 public macro
  editing API；
  VBA project remove-then-replace output-plan coverage 只能写成内部 `planned_output()`
  暴露 active `xl/vbaProject.bin` `StreamRewrite` entry、content types
  copy-original audit、preserved package/workbook relationships、preserved
  worksheet/drawing/chart/media/table/sharedStrings/styles/calcChain/unknown entries、
  empty removed_parts / removed_package_entries，且不凭空创建
  `xl/_rels/vbaProject.bin.rels`；不能写成 public output planner、
  macro generation、VBA semantic editing、signature preservation、workbook
  relationship repair、content type repair、orphan cleanup、transactional undo、full
  macro support 或 public macro editing API；
  linked-object fixture 上的 VBA project ordinary-replace-then-remove coverage 只能写成
  后续 removal 清理 active VBA replacement、记录 removed-part audit、输出省略
  VBA project part、移除 VBA content type override、保留 workbook `.rels` inbound
  VBA relationship、且不凭空创建 `xl/_rels/vbaProject.bin.rels`；不能写成 macro
  generation、VBA semantic editing、signature preservation、transactional undo、
  workbook relationship repair、content type repair、full macro support 或 public macro
  editing API；
  VBA project replace-then-remove output-plan coverage 只能写成内部 `planned_output()`
  暴露 omitted `xl/vbaProject.bin` part、removed_parts target/reason/inbound audit、
  workbook-owned inbound VBA relationship metadata、content types rewrite、empty
  removed_package_entries，以及没有凭空创建 `xl/_rels/vbaProject.bin.rels`；
  不能写成 public output planner、macro generation、VBA semantic editing、signature
  preservation、workbook relationship repair、content type repair、orphan cleanup、
  transactional undo、full macro support 或 public macro editing API；
  linked-object fixture 上的 ordinary VML drawing replacement coverage 只能写成只重写
  `xl/drawings/vmlDrawing1.vml`、worksheet `.rels` 里的 URI-qualified `vmlDrawing`
  relationship 原样保留、VML content type override 仍可读、不凭空创建
  `xl/drawings/_rels/vmlDrawing1.vml.rels`、workbook/worksheet/drawing/chart/media/
  table/sharedStrings/styles/VBA/calcChain/unknown source entries 保持 copy-original
  baseline；不能写成 VML shape editing、legacy drawing mutation、relationship repair、
  full VML/drawing support 或 public drawing editing API；
  linked-object fixture 上的 VML drawing remove-then-ordinary-replace coverage 只能写成
  后续 replacement 恢复 active VML drawing part、清理 stale removed-part audit、让
  `[Content_Types].xml` 回到 source/copy-original audit、保留 worksheet `.rels`
  URI-qualified inbound `vmlDrawing` relationship、不凭空创建 VML owner `.rels`；
  不能写成 VML shape editing、legacy drawing mutation、transactional undo、
  relationship repair、content type repair、full VML/drawing support 或 public drawing
  editing API；
  该 restore 状态的 output-plan coverage 只能写成内部 `planned_output()` 快照暴露
  active VML drawing `LocalDomRewrite` entry、content types copy-original audit、
  preserved package/workbook/worksheet/drawing relationships、preserved linked/unknown
  entries、empty removed_parts / removed_package_entries，以及 no invented owner
  `.rels`；不能写成 public output planner、relationship
  repair、content type repair、transactional undo 或 public drawing editing API；
  同一路径还覆盖 VML drawing 先 ordinary replacement 再显式移除的顺序：后续
  removal 会清理 active VML drawing replacement、记录 removed-part audit、输出省略
  VML drawing part、移除 VML content type override、保留 worksheet `.rels` 中的
  URI-qualified inbound `vmlDrawing` relationship，且不凭空创建 VML owner `.rels`；
  这不是 VML shape editing、legacy drawing mutation、事务式 undo、relationship
  pruning/repair、content type repair 或完整 VML/drawing 支持；
  该 final-removal 状态的 output-plan coverage 只能写成内部 `planned_output()` 快照
  暴露 omitted VML drawing part、removed_parts target/reason/inbound audit、
  URI-qualified worksheet inbound relationship metadata、content types rewrite、empty
  removed_package_entries，以及 no invented VML owner `.rels`；不能写成 public output
  planner、drawing editing API、relationship repair、content type repair、transactional
  undo 或 public drawing editing API；
  linked-object fixture 上的 ordinary percent-decoded drawing replacement coverage 只能写成只重写
  `xl/drawings/drawing space.xml`、worksheet `.rels` 里的原始
  `../drawings/drawing%20space.xml` relationship 原样保留、drawing content type override
  仍可读、不凭空创建 `xl/drawings/_rels/drawing space.xml.rels`、workbook/
  worksheet/drawing/chart/media/table/VML/sharedStrings/styles/VBA/calcChain/unknown
  source entries 保持 copy-original baseline；不能写成 percent-encoded target repair、
  relationship rewrite、drawing mutation、full drawing support 或 public drawing editing API；
  linked-object fixture 上的 percent-decoded drawing remove-then-ordinary-replace
  coverage 只能写成后续 replacement 恢复 active decoded drawing part、清理 stale
  removed-part audit、让 `[Content_Types].xml` 回到 source/copy-original audit、保留
  worksheet `.rels` 中原始 encoded inbound `../drawings/drawing%20space.xml`
  relationship、且不凭空创建 `xl/drawings/_rels/drawing space.xml.rels`；不能写成
  percent-encoded target repair、relationship rewrite、drawing mutation、
  transactional undo、relationship repair、content type repair、full drawing support
  或 public drawing editing API；
  该 restore 状态的 output-plan coverage 只能写成内部 `planned_output()` 快照暴露
  active decoded drawing `LocalDomRewrite` entry、content types copy-original audit、
  preserved package/workbook/worksheet/drawing relationships、preserved linked/unknown
  entries，以及 no invented owner `.rels`；不能写成 public output planner、
  percent-encoded target repair、relationship rewrite、relationship repair、content type
  repair、transactional undo 或 public drawing editing API；
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
  有结构测试证明该窄路径会 byte-preserve worksheet `.rels`、drawing、
  drawing `.rels`、media、chart、table、untouched `xl/sharedStrings.xml`、
  sharedStrings owner `.rels`、untouched `xl/styles.xml`、VBA 和可达 unknown extension
  part 及其 owner `.rels`，包括替换后的
  worksheet XML 省略源 `<drawing>` / `<tableParts>` 引用时仍原样保留；并验证 workbook
  `definedNames` 在 workbook metadata rewrite 路径下保留；registered comments-part
  fixture coverage 只能写成 worksheet rewrite 会把 `xl/comments/comment1.xml` 和 source
  worksheet `.rels` 作为 copy-original preservation 处理、保留 comments content type
  override，并可由 `PackageReader` / `RelationshipGraph` 重读；不能写成 comments
  editing、threaded comments、notes UI、relationship repair、orphan cleanup 或 public API；
  threaded comments / persons fixture coverage 只能写成 worksheet rewrite 会把
  `xl/threadedComments/threadedComment1.xml`、`xl/persons/person.xml`、source worksheet
  `.rels` 和 workbook `.rels` 作为 copy-original preservation 处理，并可由
  `PackageReader` / `RelationshipGraph` 重读；不能写成 comments / threaded comments
  editing、notes UI、relationship repair、orphan cleanup 或 public API；
  ordinary threaded comments replacement/removal coverage 只能写成
  `replace_part("/xl/threadedComments/threadedComment1.xml", ...)` 只重写 threaded
  comments XML，并保留 legacy comments、persons、worksheet `.rels` 中的
  legacy/threaded inbound relationships、workbook `.rels` 中的 persons relationship、
  content type overrides 和 unknown entry；显式 removal 会省略 threaded comments part
  并移除其 content type override，但保留 worksheet `.rels` 中指向缺失 part 的 inbound
  relationship、persons part / workbook relationship、legacy comments 和 unknown entry。
  不能写成 threaded comments model mutation、persons/schema repair、relationship
  pruning/repair、orphan cleanup、notes UI 或 public API；
  ordinary threaded comments replacement output-plan coverage 只能写成内部
  `planned_output()` 暴露 active threaded comments part `LocalDomRewrite`、preserved
  content types / package relationships / workbook / workbook `.rels` / worksheet /
  worksheet `.rels` / legacy comments / persons part / unknown entry，且不凭空创建
  threaded comments owner `.rels`；不能写成 threaded comments model mutation、
  persons/schema repair、notes UI、relationship repair、orphan cleanup 或 public API；
  ordinary persons replacement/removal coverage 只能写成
  `replace_part("/xl/persons/person.xml", ...)` 只重写 persons XML，并保留 workbook
  inbound persons relationship、threaded comments、legacy comments、worksheet `.rels`、
  content type overrides 和 unknown entry；显式 removal 会省略 persons part 并移除
  persons content type override，但保留 workbook `.rels` 中指向缺失 part 的 inbound
  relationship、threaded comments、legacy comments、worksheet 和 unknown entry。不能写成
  persons/schema repair、threaded comments model mutation、relationship pruning/repair、
  orphan cleanup、notes UI 或 public API；
  threaded comments / persons same-path ordering coverage 只能写成两条路径都覆盖
  remove-then-replace 和 replace-then-remove：后续 replacement 会清理 stale
  removed-part audit，恢复 active threaded comments/persons part，让 source content
  types audit 回到 copy-original，并且不创建对应 owner `.rels`；threaded comments
  remove-then-replace output-plan coverage 只能写成内部 `planned_output()` 暴露
  active threaded comments part local-DOM rewrite、content types copy-original audit、
  preserved package/workbook/worksheet `.rels`、legacy comments、persons part 和
  unknown entry，并清空 output-plan removed_parts / removed_package_entries，且不凭空创建 threaded comments owner `.rels`；不能写成 threaded
  comments undo、semantic merge、relationship repair、orphan cleanup 或 public API；
  threaded comments
  后续 removal 会记录 removed-part 和 worksheet inbound relationship audit，输出省略
  threaded comments part，移除 threaded comments content type override，并保留
  worksheet `.rels` 中指向缺失 part 的 inbound relationship、persons part / workbook
  relationship、legacy comments 和 unknown entry；persons 后续 removal 会记录
  removed-part 和 workbook inbound relationship audit，输出省略 persons part，移除
  persons content type override，并保留 workbook `.rels` 中指向缺失 part 的 inbound
  relationship、threaded comments、legacy comments、worksheet 和 unknown entry。不能写成
  transactional undo、threaded comments/persons semantic merge、persons/schema repair、
  relationship pruning/repair、content type repair、orphan cleanup、notes UI 或 public API；
  threaded comments replace-then-remove output-plan coverage 只能写成内部
  `planned_output()` 暴露单个 omitted threaded comments part、removed_parts 中目标为
  threaded comments part 且 reason / inbound audit 保留的 removed-part audit、
  worksheet-owned inbound threadedComment relationship metadata、content types rewrite、
  preserved worksheet / workbook `.rels` 和 persons part copy-original audit，且
  removed_package_entries 为空、不凭空创建 threaded comments owner `.rels`；
  persons replace-then-remove output-plan coverage 只能写成内部 `planned_output()`
  暴露单个 omitted persons part、removed_parts 中目标为 persons part 且 reason /
  inbound audit 保留的 removed-part audit、workbook-owned inbound persons relationship
  metadata、content types rewrite、preserved workbook/worksheet `.rels` 和 threaded
  comments part copy-original audit，且 removed_package_entries 为空、不凭空创建 persons owner `.rels`；
  persons remove-then-replace output-plan coverage 只能写成内部 `planned_output()`
  暴露 active persons part local-DOM rewrite、content types copy-original audit、
  preserved package/workbook/worksheet `.rels`、threaded comments、legacy comments 和
  unknown entry，并清空 output-plan removed_parts / removed_package_entries，且不凭空创建 persons owner `.rels`；不能写成 persons/schema undo、
  semantic merge、relationship repair、orphan cleanup 或 public API；
  pivot table / pivot cache fixture coverage 只能写成 worksheet rewrite 会把
  `xl/pivotTables/pivotTable1.xml`、`xl/pivotCache/pivotCacheDefinition1.xml`、
  `xl/pivotCache/pivotCacheRecords1.xml`、source worksheet `.rels`、pivot table owner
  `.rels`、pivot cache definition owner `.rels` 和 workbook `.rels` 作为 copy-original
  preservation 处理，并可由 `PackageReader` / `RelationshipGraph` 重读；不能写成
  pivot table editing、pivot cache rebuild、relationship repair、orphan cleanup 或 public API；
  该 worksheet rewrite 路径的 internal `planned_output()` coverage 只能写成暴露
  fullCalcOnLoad / `CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook
  `LocalDomRewrite`、package/workbook/worksheet `.rels` copy-original、pivot table /
  pivot cache definition / pivot cache records relationship context、content types 和
  unknown entry copy-original，且确认不凭空创建 records owner `.rels`；不能写成 pivot
  cache rebuild、records refresh、relationship repair/pruning、orphan cleanup 或 public API；
  ordinary pivot table replacement/removal coverage 只能写成
  `replace_part("/xl/pivotTables/pivotTable1.xml", ...)` 只重写 pivot table XML，并保留
  worksheet `.rels` 中的 inbound pivotTable relationship、pivot table owner `.rels` /
  pivotCacheDefinition relationship、pivot cache definition / records parts、pivot cache
  definition owner `.rels`、workbook `<pivotCaches>`、workbook `.rels`
  pivotCacheDefinition relationship、content type overrides 和 unknown entry；显式 removal
  会省略 pivot table part 及其 owner `.rels`，移除 pivot table content type override，
  但保留 worksheet `.rels` 中指向缺失 part 的 inbound relationship、workbook pivot cache
  metadata、pivot cache definition / records 链和 unknown entry。不能写成 pivot table
  semantic editing、pivot cache rebuild、cache-record refresh、relationship pruning/repair、
  orphan cleanup、owner `.rels` repair 或 public API；
  pivot table same-path ordering coverage 只能写成 remove-then-replace 清理
  stale removed-part / removed owner `.rels` audit，恢复 active pivot table、owner
  `.rels` copy-original audit 和 source content types audit；replace-then-remove 清理
  active replacement，记录 removed-part / removed owner `.rels` audit，输出省略 pivot
  table part 和 owner `.rels`，移除 pivot table content type override，并保留 worksheet
  `.rels` 中指向缺失 part 的 inbound relationship、workbook pivot cache metadata、
  pivot cache definition / records 链和 unknown entry。不能写成 transactional undo、
  pivot table semantic merge、pivot cache rebuild、relationship pruning/repair、
  content type repair、orphan cleanup 或 public API；
  pivot table remove-then-replace output-plan coverage 只能写成内部 `planned_output()`
  暴露 active pivot table `LocalDomRewrite` entry、owner `.rels` copy-original
  `SourceRelationships` audit、content types copy-original audit、preserved
  package/worksheet/workbook relationships、pivot cache definition / records 链和 unknown
  entry；不能写成 pivot table semantic editing、pivot cache rebuild、
  relationship pruning/repair、orphan cleanup 或 public API；
  pivot table replace-then-remove output-plan coverage 只能写成内部 `planned_output()`
  暴露 omitted pivot table part、omitted owner `.rels`、worksheet inbound pivotTable
  relationship audit、content types rewrite、preserved worksheet/workbook relationships、
  pivot cache definition / records 链和 unknown entry；不能写成 pivot table semantic
  editing、pivot cache rebuild、relationship pruning/repair、orphan cleanup 或 public API；
  ordinary pivot cache definition replacement/removal coverage 只能写成
  `replace_part("/xl/pivotCache/pivotCacheDefinition1.xml", ...)` 只重写 pivot cache
  definition XML，并保留 workbook/pivot-table inbound relationships、pivot cache records、
  pivot cache definition owner `.rels`、content type overrides 和 unknown entry；显式
  removal 会省略 pivot cache definition part 及其 owner `.rels`，移除 cache definition
  content type override，但保留 workbook/pivot table inbound relationships、pivot table、
  pivot cache records、worksheet 和 unknown entry。不能写成 pivot cache rebuild、
  cache-record refresh、relationship pruning/repair、orphan cleanup、owner `.rels`
  repair 或 public API；
  pivot cache definition same-path ordering coverage 只能写成 remove-then-replace 清理
  stale removed-part / removed owner `.rels` audit，恢复 active pivot cache definition、
  owner `.rels` copy-original audit 和 source content types audit；replace-then-remove
  清理 active replacement，记录 removed-part / removed owner `.rels` audit，输出省略
  cache definition part 和 owner `.rels`，并保留 workbook / pivot table inbound
  relationships、pivot table、cache records、worksheet 和 unknown entry。不能写成
  transactional undo、pivot cache semantic merge、relationship pruning/repair、content
  type repair、orphan cleanup 或 public API；
  pivot cache definition remove-then-replace output-plan coverage 只能写成内部
  `planned_output()` 暴露 active pivot cache definition `LocalDomRewrite` entry、owner
  `.rels` copy-original `SourceRelationships` audit、content types copy-original
  audit、preserved package/worksheet/workbook relationships、pivot table/cache records
  和 unknown entry；pivot cache definition replace-then-remove output-plan coverage
  只能写成内部 `planned_output()` 暴露 omitted cache definition part、omitted owner
  `.rels`、workbook / pivot table inbound pivotCacheDefinition relationship audit、
  content types rewrite、preserved workbook/worksheet/pivot table/cache records/unknown
  entries；不能写成 pivot cache rebuild、cache-record refresh、
  relationship pruning/repair、content type repair、orphan cleanup 或 public API；
  ordinary pivot cache records replacement/removal coverage 只能写成
  `replace_part("/xl/pivotCache/pivotCacheRecords1.xml", ...)` 只重写 pivot cache records
  XML，并保留 cache definition owner `.rels` 中的 inbound relationship、pivot cache
  definition、pivot table、workbook / worksheet relationships、content type overrides
  和 unknown entry；显式 removal 会省略 pivot cache records part，移除 records
  content type override，但保留 cache definition owner `.rels` 中指向缺失 records part
  的 inbound relationship、pivot cache definition、pivot table、workbook、worksheet 和
  unknown entry。不能写成 pivot cache records refresh、pivot cache rebuild、
  relationship pruning/repair、orphan cleanup 或 public API；
  pivot cache records same-path ordering coverage 只能写成 remove-then-replace 清理
  stale removed-part audit，恢复 active pivot cache records，让 source content types
  audit 回到 copy-original，并且不创建 records owner `.rels`；replace-then-remove
  清理 active replacement，记录 removed-part 和 cache-definition inbound relationship
  audit，输出省略 records part，移除 records content type override，并保留 cache
  definition owner `.rels` 中指向缺失 records part 的 inbound relationship、pivot
  cache definition、pivot table、workbook、worksheet 和 unknown entry。不能写成
  transactional undo、pivot cache records semantic merge、relationship pruning/repair、
  content type repair、orphan cleanup 或 public API；
  pivot cache records remove-then-replace output-plan coverage 只能写成内部
  `planned_output()` 暴露 active pivot cache records `StreamRewrite` entry、content
  types copy-original audit、preserved package/worksheet/workbook relationships、pivot
  table/cache definition 链、unknown entry，且 no invented records owner `.rels`；
  pivot cache records replace-then-remove output-plan coverage 只能写成内部
  `planned_output()` 暴露 omitted records part、cache-definition inbound
  pivotCacheRecords relationship audit、content types rewrite、preserved cache definition
  owner `.rels`、no invented records owner `.rels` 和 preserved workbook/worksheet/pivot
  table/cache definition/unknown entries；不能写成 pivot cache records refresh、pivot
  cache rebuild、relationship pruning/repair、content type repair、orphan cleanup 或
  public API；
  external links fixture coverage 只能写成 worksheet rewrite 在重写 `xl/workbook.xml`
  calc metadata 时，仍会保留 workbook `<externalReferences>`、workbook `.rels` 中的
  externalLink relationship、`xl/externalLinks/externalLink1.xml`、externalLink owner
  `.rels`、external `externalLinkPath` target、content type override 和 unknown entry，
  并可由 `PackageReader` / `RelationshipGraph` 重读；不能写成 external links editing、
  external data refresh、path validation、relationship repair、orphan cleanup 或 public API；
  该 worksheet rewrite 路径的 internal `planned_output()` coverage 只能写成暴露
  fullCalcOnLoad、`CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook
  `LocalDomRewrite`、workbook `.rels` copy-original、externalLink part 与 owner `.rels`
  copy-original、content types copy-original 和 unknown entry preservation，且不新增
  relationship target audit；不能写成 external links editing 或 relationship repair；
  ordinary external links replacement/removal coverage 只能写成
  `replace_part("/xl/externalLinks/externalLink1.xml", ...)` 只重写 externalLink XML，
  并保留 workbook `.rels` 中的 inbound externalLink relationship、externalLink owner
  `.rels` 中的 external `externalLinkPath` target、content type override、worksheet 和
  unknown entry；显式 removal 会省略 externalLink part 及其 owner `.rels`，移除
  externalLink content type override，但保留 workbook `<externalReferences>`、workbook
  `.rels` 中指向缺失 part 的 inbound relationship、worksheet 和 unknown entry。不能写成
  external links semantic editing、external data refresh、path validation、relationship
  pruning/repair、orphan cleanup、owner `.rels` repair 或 public API；
  externalLink same-path ordering coverage 只能写成 remove-then-replace 清理 stale
  removed-part / removed owner `.rels` audit，恢复 active externalLink part、owner `.rels`
  copy-original audit 和 source content types audit；replace-then-remove 清理 active
  replacement，记录 removed-part / removed owner `.rels` audit，输出省略 externalLink
  part 和 owner `.rels`，并保留 workbook inbound relationship、worksheet 和 unknown entry。
  不能写成 transactional undo、external links semantic merge、relationship pruning/repair、
  content type repair、orphan cleanup 或 public API；
  externalLink remove-then-replace output-plan coverage 只能写成内部
  `planned_output()` 暴露 active externalLink `LocalDomRewrite` entry、owner `.rels`
  copy-original `SourceRelationships` audit、content types copy-original audit，以及
  preserved package/workbook relationships、workbook、worksheet 和 unknown entry；
  externalLink replace-then-remove output-plan coverage 只能写成内部
  `planned_output()` 暴露 omitted externalLink part、omitted owner `.rels`、workbook
  inbound relationship audit、content types rewrite、preserved package/workbook
  relationships、workbook、worksheet 和 unknown entry；不能写成 external links semantic
  editing、external data refresh、relationship pruning/repair、orphan cleanup 或 public API；
  custom XML fixture coverage 只能写成 worksheet rewrite 会保留 package `_rels/.rels`
  中的 customXml relationship、`customXml/item1.xml`、custom XML item owner `.rels`、
  `customXml/itemProps1.xml`、custom XML properties content type override 和 unknown
  entry，并可由 `PackageReader` / `RelationshipGraph` 重读；不能写成 custom XML editing、
  schema/data binding、relationship repair、orphan cleanup 或 public API；
  该 worksheet rewrite 路径的 internal `planned_output()` coverage 只能写成暴露
  fullCalcOnLoad、`CalcChainAction::Remove`、worksheet `StreamRewrite`、workbook
  `LocalDomRewrite`、package relationships copy-original、custom XML item / item owner
  `.rels` / properties part copy-original、content types copy-original 和 unknown entry
  preservation，且不新增 relationship target audit、不凭空创建 properties owner `.rels`；
  不能写成 custom XML editing、schema/data binding 或 relationship repair；
  ordinary custom XML replacement coverage 只能写成
  `replace_part("/customXml/item1.xml", ...)` 只重写 custom XML item，保留 package
  `_rels/.rels` customXml inbound relationship、custom XML item owner `.rels` /
  customXmlProps relationship、`customXml/itemProps1.xml`、custom XML properties content
  type override、默认 XML content type 和 unknown entry copy-original 基线，并可由
  `PackageReader` / `RelationshipGraph` 重读；不能写成 custom XML semantic editing、
  schema/data binding、relationship repair、content type repair、orphan cleanup 或
  public editing API；
  explicit custom XML removal coverage 只能写成显式移除 `customXml/item1.xml`：
  输出省略 custom XML item 及其 source-owned owner `.rels`、保留 package
  `_rels/.rels` customXml inbound relationship、保留 `customXml/itemProps1.xml`、
  custom XML properties content type override、默认 XML content type 和 unknown entry，
  且不重写 `[Content_Types].xml`；不能写成 custom XML deletion semantics、
  schema/data binding、relationship pruning/repair、content type repair、orphan cleanup
  或 public editing API；
  custom XML set/remove ordering coverage 只能写成同一路径先显式移除再 ordinary
  replace 会恢复 active custom XML item、清理 stale removed-part / removed owner
  `.rels` audit、恢复 owner `.rels` copy-original audit，且不重写
  `[Content_Types].xml`；先 ordinary replace 再显式移除会清理 active replacement、
  记录 removed-part / removed owner `.rels` audit、输出省略 custom XML item 和 owner
  `.rels`，并保留 package inbound relationship、properties part、默认 XML content
  type 和 unknown entry。Internal `planned_output()` coverage for this restore state
  exposes the active custom XML item `LocalDomRewrite` entry, owner `.rels`
  copy-original `SourceRelationships` audit, and preserved package relationships,
  content types, workbook, worksheet, properties part, and unknown entry。
  Internal `planned_output()` coverage for this final-removal state exposes
  the omitted custom XML item, omitted source-owned owner `.rels`,
  package inbound customXml relationship audit, and preserved package relationships,
  content types, workbook, worksheet, properties part, and unknown entry。不能写成 transactional undo、custom XML semantic merge、
  relationship pruning/repair、content type repair、orphan cleanup 或 public editing API；
  custom XML properties part replacement/removal coverage 只能写成
  `customXml/itemProps1.xml` replacement 只重写 properties part，保留 custom XML
  item、item owner `.rels` / customXmlProps inbound relationship、package customXml
  relationship、properties content type override 和 unknown entry；explicit removal
  会输出省略 properties part、移除 properties content type override，但保留 custom XML
  item、item owner `.rels` 中指向缺失 properties part 的 inbound customXmlProps
  relationship、package customXml relationship、默认 XML content type 和 unknown
  entry。不能写成 custom XML properties editing、schema/data binding、
  relationship pruning/repair、content type repair、orphan cleanup 或 public editing API；
  内部 `planned_output()` 对 ordinary properties replacement 状态的覆盖只能写成暴露
  active properties part `LocalDomRewrite`、preserved content types / package
  relationships、preserved custom XML item / item owner `.rels` / workbook /
  worksheet / unknown entry，且不凭空创建 properties owner `.rels`。不能写成 custom XML
  properties semantic editing、schema/data binding、transactional undo、
  relationship pruning/repair、content type repair、orphan cleanup 或 public editing API；
  custom XML properties part ordering coverage 只能写成同一路径 properties part
  先显式移除再 ordinary replace 会清理 stale removed-part audit、恢复 active
  properties part、恢复 properties content type override/content-types copy-original
  audit，并继续保留 item owner `.rels`；先 ordinary replace 再显式移除会清理 active
  replacement、记录 removed-part audit、输出省略 properties part、移除 properties
  content type override，并继续保留 item owner `.rels` 中的 inbound customXmlProps
  relationship。不能写成 transactional undo、custom XML properties semantic merge、
  relationship pruning/repair、content type repair、orphan cleanup 或 public editing API；
  内部 `planned_output()` 对该 properties final-removal 状态的覆盖只能写成暴露 omitted
  properties part、item-owned inbound customXmlProps relationship audit、content types
  rewrite、preserved custom XML item / item owner `.rels` / package relationships /
  workbook / worksheet / unknown entry，且不凭空创建 properties owner `.rels`。不能写成
  custom XML properties deletion semantics、relationship
  pruning/repair、content type repair、orphan cleanup 或 public editing API；
  内部 `planned_output()` 对该 properties restore 状态的覆盖只能写成暴露 active
  properties part `LocalDomRewrite`、restored content types copy-original audit、
  preserved custom XML item / item owner `.rels` / package relationships / workbook /
  worksheet / unknown entry，且不凭空创建 properties owner `.rels`。不能写成 custom XML
  properties semantic merge、transactional undo、relationship pruning/repair、
  content type repair、orphan cleanup 或 public editing API；
  custom XML item removal plus properties replacement coverage 只能写成先显式移除
  custom XML item，再 ordinary replace `customXml/itemProps1.xml` properties part：
  后续 properties replacement 只重写 properties payload，保留 removed custom XML item /
  removed owner `.rels` audit，输出继续省略 custom XML item 和 item owner `.rels`，
  并保留 package customXml inbound relationship、properties content type override、
  默认 XML content type 和 unknown entry。不能写成 custom XML dependency repair、
  relationship pruning/repair、content type repair、orphan cleanup、transactional undo
  或 public editing API；
  内部 `planned_output()` 对该跨路径状态的覆盖只能写成暴露 omitted custom XML item、
  omitted source-owned owner `.rels`、package inbound customXml relationship audit、
  active properties part local-DOM rewrite、preserved package relationships / content
  types / workbook / worksheet / unknown entry，且不凭空创建 properties owner `.rels`。
  不能写成 custom XML dependency repair、relationship pruning/repair、content type repair、
  orphan cleanup、transactional undo 或 public editing API；
  custom XML properties removal plus item replacement coverage 只能写成先显式移除
  `customXml/itemProps1.xml` properties part，再 ordinary replace custom XML item：
  后续 item replacement 只重写 item payload，保留 removed properties part audit /
  content-types rewrite，输出继续省略 properties part 和 properties content type override，
  并保留 item owner `.rels` 中指向缺失 properties part 的 customXmlProps relationship、
  package customXml inbound relationship、默认 XML content type 和 unknown entry。不能写成
  custom XML dependency repair、relationship pruning/repair、content type repair、
  orphan cleanup、transactional undo 或 public editing API；
  内部 `planned_output()` 对该反向跨路径状态的覆盖只能写成暴露 omitted
  properties part、item-owned inbound customXmlProps relationship audit、content types
  rewrite、active custom XML item local-DOM rewrite、preserved item owner `.rels` /
  package relationships / workbook / worksheet / unknown entry，且不凭空创建 properties
  owner `.rels`。不能写成 custom XML dependency repair、relationship pruning/repair、
  content type repair、orphan cleanup、transactional undo 或 public editing API；
  ordinary comments-part replacement coverage 只能写成
  `replace_part("/xl/comments/comment1.xml", ...)` 只重写 comments XML、保留
  worksheet `.rels` inbound comments relationship、comments content type override、
  workbook XML / workbook `.rels`、worksheet 和 unknown entry copy-original 基线，且不凭空
  创建 comments owner `.rels`；不能写成 comments model mutation、threaded comments、
  notes UI、relationship repair、orphan cleanup 或 public API；
  ordinary comments replacement output-plan coverage 只能写成内部
  `planned_output()` 暴露 active comments part local-DOM rewrite、preserved content
  types / package relationships / workbook / workbook `.rels` / worksheet /
  worksheet `.rels` / unknown entry，且不凭空创建 comments owner `.rels`；不能写成
  comments model mutation、notes UI、relationship repair、orphan cleanup 或 public API；
  explicit comments-part removal coverage 只能写成输出省略 `xl/comments/comment1.xml`、
  移除 comments content type override、保留 worksheet `.rels` inbound comments
  relationship，且不凭空创建 comments owner `.rels` omission；不能写成 comments
  deletion semantics、threaded comments、notes UI、relationship pruning/repair、
  orphan cleanup 或 public API；
  remove-then-ordinary-replace comments coverage 只能写成后续
  `replace_part("/xl/comments/comment1.xml", ...)` 恢复 active comments replacement、
  清理 stale removed-part audit、让 comments content type override 和
  `[Content_Types].xml` 回到 source/copy-original 状态、保留 inbound worksheet `.rels`，
  且仍不凭空创建 comments owner `.rels`；不能写成 comments undo、semantic merge、
  relationship repair、orphan cleanup 或 public API；
  remove-then-replace comments output-plan coverage 只能写成内部 `planned_output()`
  暴露 active comments part local-DOM rewrite、content types copy-original audit、
  preserved package/workbook/worksheet `.rels` 和 unknown entry，并清空 output-plan
  removed_parts / removed_package_entries，且不凭空创建 comments owner `.rels`；不能写成 comments undo、semantic merge、relationship
  repair、orphan cleanup 或 public API；
  replace-then-remove comments coverage 只能写成后续 explicit removal 清理 active
  comments replacement、记录 removed-part audit、输出省略 comments part、移除
  comments content type override、保留 inbound worksheet `.rels`，且仍不凭空创建
  comments owner `.rels`；不能写成 comments deletion semantics、transactional undo、
  relationship pruning/repair、orphan cleanup 或 public API；
  replace-then-remove comments output-plan coverage 只能写成内部
  `planned_output()` 暴露单个 omitted comments part、removed_parts 中目标为
  comments part 且 reason / inbound audit 保留的 removed-part audit、worksheet-owned
  inbound comments relationship metadata、content types rewrite、preserved
  package/workbook/worksheet `.rels` copy-original audit，且 removed_package_entries
  为空、不凭空创建 comments owner `.rels`；
  且覆盖
  `ReferencePolicy` 的 linked-object fail、calcChain preserve / rebuild 拒绝和
  malformed workbook metadata / workbook calc metadata rewrite 预检失败不污染
  edit-plan entries/notes、aggregate `planned_output()` 和 manifest write-mode、
  worksheet rewrite 缺少 `xl/workbook.xml` 前置 metadata 时失败不污染状态、
  已有 ordinary workbook replacement 排队后 linked-object fail 仍保留既有 replacement /
  manifest / source-owned `.rels` audit / 输出 bytes、
  queued core/app docProps helper 后 linked-object fail 仍保留既有 metadata edit /
  package-entry audit / 输出 bytes、
  core/app docProps relationship target 冲突失败不污染状态、request-recalculation
  fullCalcOnLoad 输出窄边界；linked-object fixture 也验证 worksheet-owned 和
  drawing-owned external、URI-qualified、invalid 和 unresolved relationship target 审计 note
  及结构化 `RelationshipTargetAudit` 会传播到 existing-file
  `PackageEditor` edit plan。它们不代表 public
  `PackageEditor`、complete existing-file package rewrite、atomic in-place editing、relationship pruning/orphan cleanup、
  sharedStrings/styles/tables/drawings/defined-name dependency sync、
  drawing/image/chart/table editing、broad unknown part preservation、完整
  document properties editing、图片、VBA 或 table 已有完整能力。

## 文件职责边界

- public API 可以继续集中在现有 public headers，保持用户入口稳定。
- 核心 writer 文件应保留流程编排、生命周期管理、热路径调用和必要的跨功能协调；
  不应无限承载所有 feature 的 XML 序列化、校验和状态转换。
- 当一个 feature 已有独立 public API、独立 XML 结构、独立状态、独立 QA helper
  或大量边界测试时，任务计划应考虑 feature-specific 实现文件、detail helper 和
  独立测试文件。
- 测试组织应镜像功能边界：主 streaming 测试保留主流程和跨功能集成；feature 的
  细粒度结构测试和负例应逐步拆分。
- 新增 `.cpp` 或测试文件时，任务计划必须包含 CMake 列表同步。
- 不要过度拆分：少量协调代码、一次性修正或尚未稳定的实验切片可以先留在现有文件，
  等边界和增长趋势明确后再拆。

## Current Data Bar API Notes

- `DataBarValueType`, `DataBarEndpoint`, `DataBarRule`, and
  `WorksheetWriter::add_conditional_data_bar()` are public API symbols.
- API mode is Streaming / new-workbook-only worksheet metadata.
- The API copies range lists, two endpoints, bar color, and the `show_value`
  flag into writer state; memory grows with rule count and copied range count,
  not worksheet cell count.
- `DataBarValueType::Minimum` / `Maximum` write no `val`; `Number` / `Percent`
  / `Percentile` require finite numeric values. Lower endpoints cannot use
  `Maximum`; upper endpoints cannot use `Minimum`.
- Data bars share worksheet-local priority order with color scales and write one
  space-separated `sqref` for multi-range calls.
- `DataBarRule::show_value=false` writes only the basic data bar XML attribute
  `showValue="0"`; the default is omitted.
- The API does not create `styles.xml`, `dxfs`, worksheet `.rels`, content type
  entries, workbook relationships, cell text, or `<calcPr>`.
- Do not document it as formula/cellIs, advanced/custom icon sets, advanced data bars
  (negative colors, axis, border, gradient, `extLst`), dxf-backed styles,
  existing-file editing, or full conditional formatting.
- Basic 3Arrows icon sets are a separate current `WorksheetWriter::add_conditional_icon_set()`
  API surface, not part of the data bar API.
- Current icon-set QA covers the existing Percent, Number, and Percentile
  threshold serialization paths; this does not imply advanced/custom icon sets.

## API 模式

设计或审查 API 时，先标记模式：

- `Streaming`：新建 XLSX、大数据导出、多 sheet 批量写入。
- `Patch`：已有 XLSX 编辑、part-level rewrite、模板替换、EditPlan 驱动的联动 part 更新。
- `In-memory`：小文件复杂编辑和真正随机访问，不承诺大文件低内存。

任何 public API 都要说明它属于哪种模式，以及它是否允许随机访问或回写历史行。

## 文档注释要求

public header 中的 API 应有 Doxygen 风格注释。注释必须写清 Streaming/Patch/In-memory
模式、内存行为、随机访问限制、OpenXML part 副作用和性能边界，至少说明：

- API 所属模式。
- 是否保留完整 worksheet 状态。
- 输入顺序要求。
- 是否允许随机访问。
- Patch API 是否生成 EditPlan、会改写哪些 part、哪些 part 原样保留、是否设置
  fullCalcOnLoad 或处理 `calcChain.xml`。
- Patch API 若涉及 `[Content_Types].xml`、package `_rels/.rels` 或 owner `.rels`，
  只能把当前内部 package-entry rewrite / omission / preserved copy-original 记录写成
  side-effect audit，不能写成完整 metadata editor。
- 字符串策略。
- 样式、relationships 或 content types 副作用。
- 错误处理方式。
- 性能/内存注意事项。

审查 public `double` API 时，必须核对 finite-only 边界、拒绝时机和异常类型是否写清。
`Cell::number(double)` 和 `CellView::number(double)` 的数值 payload 不接受
`NaN`、`+Inf` 或 `-Inf`；`CellView::number()` 构造本身是 `noexcept`，当前由
`WorksheetWriter::append_row()` 抛 `FastXlsxError`。小型 in-memory `Cell::number()`
路径当前在 `Workbook::save()` 序列化 worksheet XML 时报错。in-memory 和 streaming
`RowOptions::height`、以及 `WorksheetWriter::set_column_width()` 都要写清正数/有限值要求。
当前 `Cell` / `CellView` 都没有专用 date cell 类型；日期/时间单元格只能由调用方按
Excel serial number 写入 numeric cell。当前已有 streaming-only 自定义 number format
styles，但 number format 只负责显示格式，不编码 date cell type、不计算日期序列值。
不要把 `DataValidationType::Date` 误写成 date cell encoding 已实现。
不要设计或描述把 `NaN/Inf` 转成字符串、空单元格或 OpenXML 数字文本的行为。

styles API 还要写清：Streaming / new-workbook-only、`StyleId` 是 workbook-local
handle、默认 id `0` 表示 default style、非默认 id 必须来自同一个
`WorkbookWriter::add_style()`、foreign id 应在 `append_row()` 推进 row state 前拒绝。
`CellStyle::number_format` 可为空，表示不改变 number format；`CellAlignment::wrap_text`、
`HorizontalAlignment::{Left,Center,Right}` 和
`VerticalAlignment::{Top,Center,Bottom}` 是当前 alignment 子能力，false flags 或空 optional
不贡献 style 属性；
`CellFont::bold` / `CellFont::italic` 和 direct ARGB `CellFont::color` 是当前 font
子能力，false flags 且空 `color` 或空 optional 不贡献 style 属性；`CellFill::foreground` 是当前唯一 fill 子能力，使用 `ArgbColor`
写 solid foreground fill，空 optional 不贡献 style 属性。完全空 style 会被拒绝。
重复完整 style 复用同一个 `StyleId`，相同 number format 在不同 style 组合中复用同一个
custom `numFmtId`，相同 bold/italic/color font 组合在不同 style 组合中复用同一个 `fontId`，
相同 foreground ARGB fill 组合在不同 style 组合中复用同一个 `fillId`，
format 只按字符串精确匹配去重。样式会生成
`xl/styles.xml` / workbook relationship / content type override、cell 写 `s="N"`、
默认 `s="0"` 省略、不创建 worksheet `.rels`。alignment 只写
`applyAlignment="1"` / `<alignment .../>` attributes：`wrapText="1"`、
`horizontal="left|center|right"` 和 `vertical="top|center|bottom"`；
不计算行高，不代表完整 alignment；
bold/italic/direct ARGB font color 只写 `<fonts>` 中的 `<b/>` / `<i/>`、可选
`<font><color rgb="..."/></font>`、`fontId` 和 `applyFont="1"`，不代表完整 font control。
solid fill 只写 `<fills>` 中的 solid `<patternFill>`、
`fgColor rgb`、`bgColor indexed="64"`、`fillId` 和 `applyFill="1"`，不代表完整
fill/pattern/theme/tint/indexed palette control。当前不支持 font name、font size、underline、
theme/tint/indexed font color、
border/full alignment、rich text、dxf-backed conditional formatting、
existing-file style preservation 或完整
Excel formatting parity。当前 two-/three-color color scale、basic data bar 和 basic
3Arrows icon set 是 worksheet
metadata，不代表 styles registry 或 `dxfs` 已支持。

conditional formatting 这类 worksheet metadata API 还要写清：Streaming-only、
new-workbook-only、规则/range 拷贝成本、`ArgbColor` 8 位大写 ARGB 序列化、
`ColorScaleValueType` / `DataBarValueType` / `IconSetValueType` token、finite endpoint/threshold
边界、priority 规则、multi-range `sqref`
行为、是否新增 relationships/content types/styles/calc metadata、无公式求值、无冲突
检测、无 existing-file editing 和无完整 Excel UI。当前
`WorksheetWriter::add_conditional_color_scale()` 只覆盖 two-/three-color color scale；
`WorksheetWriter::add_conditional_data_bar()` 只覆盖 basic data bar，`DataBarRule::show_value=false`
只写 `showValue="0"`；
`WorksheetWriter::add_conditional_icon_set()` 只覆盖 basic built-in `3Arrows` icon set；
不要写成 formula/cellIs、advanced data bars、advanced/custom icon sets、dxf-backed styles
或完整 conditional formatting。

data validations 这类 worksheet metadata API 还要写清：Streaming-only、
new-workbook-only、规则数量/multi-area `sqref` 区域数量/公式文本/prompt-error 文本
内存成本、range 列表/公式文本/prompt-error 文本拷贝、attribute escape、空字符串和
false flag 省略、无公式求值、无单元格值校验、无区域排序/合并/去重、无重叠检查、
无完整 Excel UI 保证，以及是否新增 relationships/content types/styles。
hyperlinks 这类 worksheet metadata API 还要写清：Streaming-only、
new-workbook-only、URL/location 文本拷贝、external 链接的 worksheet `<hyperlinks>` 与
worksheet `.rels` 副作用、worksheet-owner-local `rId`、internal 链接是否只写 `location`
且不创建 `.rels` / `r:id`、`HyperlinkOptions` 是否只写 display/tooltip attributes 并省略
空字符串、不写单元格文本、不创建 hyperlink 样式、无 URL 可达性校验、无 internal target
存在性校验、无 existing-file editing，以及不代表完整 hyperlink 支持。
tables 这类跨 part worksheet metadata API 还要写清：Streaming-only、new-workbook-only、
table name / column names / style name / `show_totals_row` / `column_totals_functions` /
`column_totals_labels`
拷贝、`xl/tables/tableN.xml`、worksheet `<tableParts>`、worksheet `.rels`、
content type override、worksheet-owner-local `rId`、不读取已写 header 行、不推断列名、
不生成 `styles.xml`、只支持 totals-row visibility metadata、caller-supplied
`totalsRowFunction` attributes 和 caller-supplied `totalsRowLabel` attributes、只拒绝
同一 worksheet 内 table-vs-table range overlap、无公式生成、无 totals row 单元格文本、
无 table resize、无 existing-file editing，以及不代表完整 table 支持。
document properties 这类 workbook metadata API 还要写清：new-workbook-only、
字符串值拷贝、只写 `docProps/core.xml` 和 `docProps/app.xml`、package relationships
与 content type side effects、不创建 `docProps/custom.xml`、无 arbitrary timestamp
API、无 custom document properties、无 existing-file editing，以及不代表完整
document properties 支持。
images 相关 API 还要写清：是图片元数据读取、new-workbook 插入还是 existing-workbook
patch；`stb` 是否只做 header probing、是否会解码完整像素、原始图片字节和 decoded
pixel buffer 的生命周期与内存成本；是否写 `xl/media/*`、drawing XML、drawing `.rels`、
worksheet `.rels`、worksheet `<drawing>` 和 content types；以及是否不支持裁剪、旋转、
格式转换、existing drawing mutation 或 existing-file preservation。
当前 `ImagePixels` / `read_image_pixels()` 注释应写清：会分配完整 decoded pixel buffer，
`ImagePixels::pixels` 由 caller 持有，内存随像素宽高和通道数增长；它只读像素，不写
media/drawing parts、relationships、content types 或 anchors，也不是图片插入热路径。
当前 `WorksheetWriter::add_image()` 注释应保持说明：Streaming / new-workbook-only、
原始图片字节 file-backed、memory-source span lifetime、two-cell anchor、package side
effects、无完整像素解码、无 existing-file editing、无 drawing mutation，且不牺牲
worksheet streaming 热路径；它与 `read_image_pixels()` 不同，走 raw-byte/file-backed
media insertion，不为插入保留 decoded pixel buffer。
memory-source overload 注释还要写清同步 copy-to-temp-file-backed media entry、空 buffer
和 unsupported header 错误边界，并说明它不是任意 stream/URL/base64 图片源。
涉及 `ImageOptions` 时还要写清：from/to marker EMU offset、`edit_as` 枚举、
name/description 字符串和 external hyperlink URL/tooltip 的复制成本、
OpenXML token / XML attribute escape、空值行为、只写 two-cell marker
`xdr:colOff` / `xdr:rowOff`、`xdr:twoCellAnchor editAs`、`xdr:cNvPr name` /
`descr` 和 `xdr:cNvPr/a:hlinkClick`；external picture link 只创建 drawing `.rels`
里的 `TargetMode="External"` relationship，不写 worksheet `<hyperlinks>`、cell text、
hyperlink style、workbook relationship 或 content type override。不修改图片二进制 /
EXIF / media filename / anchor cell range，不校验 URL，不支持 internal picture link、
`oneCellAnchor` / `absoluteAnchor` 元素、row/column resize 几何计算、existing drawing
mutation 或 existing-file editing，也不承诺完整 Excel UI 或跨办公软件 accessibility 行为。

涉及热路径的 API，还要说明是否会触发 DOM、跨行缓存、shared strings 状态增长、
压缩等级影响或输出文件大小变化。

## 任务计划要求

规划 API 任务时，任务说明必须写清：

- 对应的 `docs/TASK_BREAKDOWN.md` 子任务编号；没有拆分编号的大任务必须先拆分。
- 所属 Phase。
- API 模式。
- 是否触碰性能热路径。
- 是否需要文档注释。
- 需要哪些单元测试、OpenXML 结构测试、Excel 可视化验证、拆包 XML 对比或 benchmark。
- 是否改变 CMake target 或引入依赖。
- 如果是 Patch / editing API，必须列出 EditPlan 影响范围、unknown part preservation、
  relationship/content type side effects、sharedStrings/styles/calcChain 策略和 ReferencePolicy。
- 阶段排序必须体现当前方向：默认执行顺序是 `C0 -> C7`，`P*` 只保留为历史索引和
  能力切片。API 设计任务在需要时仍可引用 `P4.0` / `P4` / `P7` 等历史编号，但不要把
  它们写成当前默认入口；`WorkbookWriter` / `Workbook` / current narrow `WorkbookEditor`
  / future `WorksheetEditor` 的边界、`CellView` / `Cell` / `CellValue` 的边界和
  internal/public 分界仍然要先写清，再进入窄 Patch MVP、preservation fixture、sheet
  dependency policy 和后续 In-memory 小文件随机编辑。writer/backend/sharedStrings/
  benchmark 工作是性能支撑线，不能把编辑能力长期排到所有性能任务之后，也不能绕过当前
  queue gate 直接扩大 public Patch API。
- 如果借鉴其他语言或生态的 XLSX 库，必须写清借鉴对象、借鉴点和不借鉴的架构缺点；
  例如 API 体验归 In-memory，低内存写入归 Streaming，已有文件保真归 Patch / OPC。

如果任务要求“更易用”，必须同时说明为什么不会破坏 streaming 性能主线。

## 禁止事项

- 不要让 `Workbook` 级便利 API 默认持有完整 worksheet。
- 不要为了便利 API 引入完整 worksheet cell matrix、DOM 或 cell map。
- 不要把当前 owning `Cell` 复用成通用内部 cell store；大文件输入走 `CellView` /
  row iterator，小文件随机编辑走独立紧凑存储。
- 不要让 large worksheet 因 API 简化进入 DOM。
- 不要把 streaming API 做成 DOM API 的附属补丁。
- 不要把 PackageEditor / Patch API 做成 streaming writer 的附属补丁；已有文件编辑要有
  独立模式、EditPlan 和 preservation 语义。
- 不要隐藏压缩等级、字符串策略或 DOM 模式这类性能关键选择。
- 不要用“高性能”“低内存”等模糊描述替代明确边界。

## 验证

- public API 有文档注释。
- 注释写明模式、内存行为、限制和性能注意事项。
- 大数据路径仍然 row/chunk 化。
- 便利 API 不会隐式 DOM 化大型 worksheet。
- 测试计划包含结构验证和必要的 Excel 可视化验证。
- document properties 测试覆盖 core/app docProps 字段、XML escape、relationships、
  content types，并确认不生成 `docProps/custom.xml`。当前本地 QA helper 是
  `tools/verify_document_properties.py` 和 `tools/verify_document_properties_excel.ps1`；
  只把它们当作 XML/openpyxl/Excel COM 验证入口，不要把它们写成运行时依赖、
  默认 CI 要求或完整 document properties 支持。
- 内部 existing-package docProps Patch 测试可验证已有 `docProps/custom.xml`
  preservation，但只能写成 preservation-only 回归，不能写成 custom properties
  editor、relationship repair 或 public existing-file document-properties API。
- existing-package docProps Patch 测试若覆盖 package relationship target 冲突，
  只能写成 core/app generated-small-XML helper 的失败不污染状态回归，不能写成
  完整 relationship 修复或 public document-properties editor。
- 数值相关 API 注释写清 `NaN` / `+Inf` / `-Inf` 拒绝边界，且测试覆盖拒绝路径。
- 性能敏感 API 有 benchmark 或明确后续 benchmark 任务。
