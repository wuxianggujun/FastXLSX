# FastXLSX Agent Guide

## 项目快照

FastXLSX 是一个 C++20 / MSVC 2026 优先的可编辑高性能 XLSX/OpenXML 引擎。
项目文档固定的核心方向是：共享 OpenXML/OPC 底座，提供 Streaming、Patch、
In-memory 三条 API 路径；流式写入是大文件性能主线，编辑能力也是核心主线。

当前仓库处于 Phase 1 最小可写 XLSX 的早期实现阶段：

- `CMakeLists.txt` 定义了 compiled `fastxlsx` library。
- 目标别名是 `FastXLSX::fastxlsx`。
- `include/fastxlsx/fastxlsx.hpp`、`include/fastxlsx/workbook.hpp`、
  `include/fastxlsx/cell_value.hpp`、
  `include/fastxlsx/streaming_writer.hpp`、`include/fastxlsx/document_properties.hpp`
  和 `include/fastxlsx/image.hpp` 提供当前 public API：
  `Workbook`、`Worksheet`、`Cell`、`CellValue`、`CellValueKind`、`DocumentProperties`、`WorkbookWriter`、
  `WorksheetWriter`、`CellView`、`StyleId`、`CellAlignment`、`HorizontalAlignment`、
  `VerticalAlignment`、`CellFont`、`CellFill`、`CellStyle`、`DataValidationRule`、
  `DataValidationType`、`DataValidationOperator`、`DataValidationErrorStyle`、`HyperlinkOptions`、
  `ArgbColor`、`ColorScaleValueType`、`ColorScalePoint`、`TwoColorScaleRule`、
  `ThreeColorScaleRule`、`DataBarValueType`、`DataBarEndpoint`、`DataBarRule`、
  `IconSetStyle`、`IconSetValueType`、`IconSetRule`、
  `TableOptions`、`TableTotalsFunction`、`TableOptions::show_totals_row`、
  `TableOptions::column_totals_functions`、`TableOptions::column_totals_labels`、
  `ImageFormat`、`ImageInfo`、`ImagePixels`、
  `ImageEditAs`、`ImageAnchorOffset`、`ImageOptions`、
  `ImageOptions::external_hyperlink_url`、
  `ImageOptions::external_hyperlink_tooltip`、`read_image_info()`、`read_image_pixels()`、
  `Workbook::set_document_properties()`、`WorkbookWriterOptions::document_properties`、
  `WorkbookWriter::add_style()`、`CellView::with_style()`、
  `WorksheetWriter::add_conditional_color_scale()`、`WorksheetWriter::add_conditional_data_bar()`、
  `WorksheetWriter::add_conditional_icon_set()`、
  `WorksheetWriter::add_external_hyperlink()`、`WorksheetWriter::add_internal_hyperlink()`、
  `WorksheetWriter::add_image()` 和 `FastXlsxError`。
- `src/cell_store.cpp`、`src/cell_value.cpp`、`src/image.cpp`、`src/opc.cpp`、`src/package_editor.cpp`、`src/package_reader.cpp`、
  `src/package_writer.cpp`、`src/workbook.cpp`、`src/streaming_writer.cpp`、
  `src/xml.cpp`、`src/zip_store_writer.cpp`
  是当前已接入 CMake 的实现入口。
- `include/fastxlsx/detail/cell_store.hpp` 和 `src/cell_store.cpp` 是当前
  P7.3a internal In-memory foundation：`CellPosition`、`CellRecord` 和 worksheet-local
  sparse `CellStore`，并已有 P7.4a internal `CellStoreOptions` guardrail 首片。
  它只提供 internal sparse storage、`CellValue` 转换、`cell_count()`、估算内存输入
  和单个 `CellStore` 的 `max_cells` / `memory_budget_bytes` enforcement；当前还已有
  internal `cell_store_to_sheet_data_xml()` 首片，可把 sparse records 投影成 standalone
  `<sheetData>` payload，并有 internal by-name `PackageEditor` handoff 回归验证该
  payload 可进入当前 bounded `sheetData` Patch helper；不代表 public `WorkbookEditor`、
  `WorksheetEditor`、random cell editing、workbook-level guardrails、完整 save-as /
  Patch handoff、sharedStrings migration、styles merge、calcChain rebuild 或
  relationship repair。
- `include/fastxlsx/detail/opc.hpp` 和 `src/opc.cpp` 是内部 OPC
  manifest / relationships / XML serializer 基础，不代表完整已有 XLSX 编辑能力。
- `tests/test_minimal_xlsx.cpp` 通过 CTest 注册为 `fastxlsx.unit`，覆盖 XML escape、
  cell reference、最小 OpenXML package 结构、基础单元格编码、in-memory 公式 XML
  escape、row height metadata、空 worksheet / 单空行 dimension、`XFD1` 列边界、
  16385 列拒绝路径、`CellValue` public value 边界、internal `CellStore` sparse
  storage 边界和 internal `CellStoreOptions` guardrail 边界。
- `tests/test_streaming_writer.cpp` 通过 CTest 注册为 `fastxlsx.streaming`，覆盖
  流式 writer 骨架、公式、行高、列宽、冻结窗格、自动筛选、合并单元格和
  空行 dimension、合法最大列/最大行结构边界、失败 append 不污染状态、
  Excel 行/列上限拒绝路径、data validations / external and internal hyperlinks /
  tables / two-/three-color conditional color scales / basic conditional data bars /
  basic 3Arrows conditional icon sets /
  streaming number-format、wrap-text + limited horizontal/vertical alignment、
  bold/italic/direct-color font 和 solid fill styles / style 与 worksheet
  relationship-backed metadata 共存的 rId 作用域回归 /
  默认 stb image drawing XML 输出和图片对象 external hyperlink metadata。
- `tests/test_opc.cpp` 通过 CTest 注册为 `fastxlsx.opc`，覆盖内部 OPC part name、
  content types、relationships、manifest 和 serializer 基础。
- `tests/test_worksheet_event_reader.cpp` 通过 CTest 注册为
  `fastxlsx.worksheet_event_reader`，覆盖 internal P8 worksheet event reader 首片：
  `scan_worksheet_events()` 会按 source order 发出 XML declaration / processing
  instruction / comment、worksheet root、raw metadata、`sheetData`、row、cell 和
  raw text separators、cell value wrapper markup、value text token，token 中的
  string_view 只在 source worksheet XML buffer 生命周期内有效；当前测试覆盖
  qualified local-name、inline text raw value、metadata / raw text pass-through 和
  malformed boundary 拒绝。这不是 public `WorksheetReader`、完整 XML parser /
  schema validation、namespace repair、relationship repair、PackageEditor stream
  rewrite handoff 或低内存大文件编辑完成。
- `tests/test_worksheet_transformer.cpp` 通过 CTest 注册为
  `fastxlsx.worksheet_transformer`，覆盖 internal P8 worksheet transformer action
  model 首片：`scan_cell_replacement_actions()` 基于 event reader token，把 bounded
  cell replacement selectors 映射为 source-order `PassThrough` / `ReplaceCell`
  actions，跳过目标 cell 原始 payload，并返回 missing selector diagnostics；当前
  测试覆盖 source-order replacement、目标 payload consumption、missing selector、
  duplicate selector、empty payload preflight、replacement payload `<c>` / `*:c`
  root 与未命名空间 `r` selector match preflight，以及
  `emit_cell_replacement_worksheet()` 输出 chunk 时保留 raw text / value markup
  pass-through。这不是 public transformer、package-entry staged worksheet writer、
  full cell schema validation、dimension recalculation、dependency repair、
  `PackageEditor` / `EditPlan` commit
  或低内存大文件编辑完成。
- `tests/test_package_reader.cpp` 通过 CTest 注册为 `fastxlsx.package_reader`，
  覆盖 stored/no-compression package entry 索引/读取、content-types /
  relationships ingestion、内部 PartIndex / RelationshipGraph 视图、unknown entry
  读取、unknown extension owner `.rels` metadata ingestion 和坏 package/metadata
  拒绝路径，包括冲突 content type default / override 和同一 `.rels` owner
  内重复 relationship id 的拒绝，以及 `[Content_Types].xml` / `.rels` 第一个真实
  XML 元素不是 `Types` / `Relationships` 的 decoy-root 拒绝、metadata declaration
  嵌套在非 root direct-child 元素下的拒绝、namespaced metadata attribute decoy
  拒绝、重复未命名空间 metadata attribute 拒绝、非 whitespace metadata text
  拒绝、start/end tag QName mismatch 拒绝；minizip-enabled 测试还覆盖真实 DEFLATE entry
  读取、DEFLATE unknown part / owner `.rels` ingestion，以及损坏 DEFLATE payload
  在 `read_entry()` 阶段失败。默认构建仍拒绝 compressed input。
- `tests/test_package_editor.cpp` 通过 CTest 注册为 `fastxlsx.package_editor`，
  覆盖内部 stored-package copy/replace、core/app docProps generated-small-XML、
  docProps helper 接管 prior removal 时的 docProps owner `.rels` omission、
  worksheet replacement calcChain-remove/fullCalcOnLoad、docProps + worksheet
  replacement 组合回归、linked-object preservation fixture、external /
  URI-qualified / invalid / unresolved relationship target 审计 note 与结构化 audit 传播、
  ReferencePolicy 窄边界，以及非法替换/冲突拒绝路径；当前还覆盖一个真实
  `WorkbookWriter` 生成 package 的 internal Patch roundtrip：`PackageReader`
  解析 writer workbook sheet catalog，`PackageEditor::replace_worksheet_sheet_data_by_name()`
  替换目标 sheet 的 `<sheetData>`，输出保留 untouched worksheet、content types、
  package relationships、workbook relationships 和 docProps bytes，并字节级保留 writer
  生成的 worksheet XML declaration/prolog，且输出 `<worksheet>` 根紧随该 prolog；当 writer source 使用 sharedStrings 与
  styles 时，还验证 `xl/sharedStrings.xml`、`xl/styles.xml`、
  对应 content type override 和 workbook relationships 原样保留，并把 replacement
  `<sheetData>` 中的 shared string index / style id 记录为结构化
  `WorksheetPayloadDependencyAudit`；输出会设置 workbook `fullCalcOnLoad="1"`，
  且不会凭空创建 `xl/calcChain.xml`；固定本地 QA 入口
  `tools/verify_patch_mvp_excel.ps1` 会用 Excel COM 只读打开 writer-roundtrip 和
  template-fill Patch MVP 输出，并检查目标 sheet 替换与 untouched sheet 保留 smoke
  值；当前还覆盖 internal `CellStore` 生成
  standalone `<sheetData>` payload 后接入 by-name `sheetData` Patch helper 的 handoff
  回归，验证 bounded local rewrite、公式审计、默认 calcChain cleanup 和 unknown bytes
  preservation；当前还覆盖 internal P8 cell-replacement output-side file-backed
  handoff：`PackageEditor::replace_worksheet_cells()` /
  `replace_worksheet_cells_by_name()` 仍会物化当前 planned worksheet XML 作为
  PackageReader 边界输入，但不再物化完整 rewritten worksheet XML；它先扫描 source
  action stream 和 replacement payload，计算 top-level worksheet `<dimension>`，分拆
  audit 为 source metadata 与 replacement cell payload，并跳过会被替换掉的旧 target
  cell payload / relationship-id references；second pass 直接把 dimension-refreshed
  output 写入 `PackageEditor` 持有的 temporary file-backed `PackageEntryChunk`，再以
  `StreamRewrite` staged chunks 进入 Patch 输出；当前还复用 transformer replacement
  payload preflight，验证 missing selector、非 cell root、缺失未命名空间 `r`、
  qualified-only `r`、selector / `r` mismatch，以及 audit-heavy replacement payload
  policy failure 均不污染 `EditPlan`、manifest、payload audits、calc policy 或 planned
  output；测试还覆盖 save_as 后 `PackageReader` 可重读、dimension refresh、old target
  audit skip、linked-object fixture preservation/audit visibility 和 `PackageEditor`
  析构后的临时文件清理；linked-object 回归验证 by-name cell replacement file-backed
  handoff 下 worksheet `.rels`、drawing/media/chart/table/VML/percent-decoded drawing、
  sharedStrings owner `.rels`、styles、VBA、unknown extension owner `.rels`、
  workbook definedNames、PNG default content type 和 calcChain cleanup 可由输出
  `PackageReader` 重读；当前还覆盖
  internal package-entry chunked replacement source foundation：
  `PackageEditor::replace_part_chunks()` 会把 existing package part 记录为
  `PackageEntryChunk` memory/file chunks 支撑的 `StreamRewrite` replacement，
  `save_as()` 会把这些 chunks 交给 `PackageWriter`，普通 `replace_part()` 会清理同路径
  stale chunks；当前还覆盖 internal worksheet replacement chunk handoff：
  `PackageEditor::replace_worksheet_part_chunks()` 会先复用 materialized worksheet XML 的
  validation / dependency audit / relationship audit / calc metadata 路径，成功后把目标
  worksheet payload 切换为 `PackageEntryChunk` chunks。这只是 internal Patch MVP /
  handoff / staged payload foundation / cell replacement output-side file-backed 回归，不是
  public `PackageEditor`、随机 cell 编辑、PackageReader 输入侧流式、
  broad range metadata recalculation、sharedStrings 索引迁移、style id 迁移、styles
  合并、relationship repair/pruning、object semantic editing、table/drawing 语义同步
  或大文件 streaming worksheet transformer。
- `tests/test_image.cpp` 通过 CTest 注册为 `fastxlsx.image`，覆盖默认 `stb`
  构建下 PNG/JPEG 文件/内存尺寸和通道读取、unsupported memory/file header、
  empty memory buffer、empty file、missing file，以及 PNG/JPEG 文件/内存像素解码
  的尺寸、通道数和 owned pixel buffer size。
- 当前可见文件集中已有 `vcpkg.json`、`CMakePresets.json` 和
  `.github/workflows/ci.yml` 作为基础工程入口。默认构建通过 vcpkg 拉取 `stb`；
  `FASTXLSX_ENABLE_MINIZIP_NG=ON` 会通过 vcpkg `planned-runtime` 接入
  `minizip-ng[core,zlib]` 并链接 `MINIZIP::minizip-ng`。
  CI workflow 先跑 vcpkg-backed 的 `windows-nmake-release` job，并另有 opt-in
  vcpkg matrix 跑 `windows-nmake-release-minizip`。
  远端 runner 的 vcpkg 可用性、安装耗时和缓存行为以实际 CI run 为准。
- `CMakeLists.txt` 已有 `FASTXLSX_BUILD_EXAMPLES` 分支，当前工作树中可见
  `examples/` 目录和示例源码；除非本轮任务验证该路径，否则不要把 example
  目标当作已完成发布面。

当前 P17a 图片读取 helper 切片已存在：默认 vcpkg manifest 依赖 `stb`，CMake 使用
vcpkg installed tree 中的 `share/stb/FindStb.cmake`、
`find_package(Stb MODULE REQUIRED)` 和 `${Stb_INCLUDE_DIR}`。`read_image_info()` 只读取 PNG/JPEG 的尺寸、格式
和通道数。`read_image_pixels()` 会用 `stb` 解码 PNG/JPEG 到 caller-owned
`ImagePixels::pixels`，并分配完整 decoded pixel buffer；它不生成 media part、
drawing XML、relationships、content types、anchor，也不代表图片插入、图片编辑、
格式转换或 existing-workbook 图片保真。

当前 P17b 图片插入基础切片也已存在：`WorksheetWriter::add_image(path, anchor)`
和 memory-source overload 都是 streaming-only new-workbook API，默认接受 PNG/JPEG。
path overload 读取文件；memory-source overload 接受 `std::span<const std::byte>`，
span 只需要在调用期间有效。两条路径都会用 `read_image_info()` 验证格式/尺寸/通道，
把原始图片字节复制到临时 file-backed media entry，并在 `close()` 写出
`xl/media/image*.png|jpg`、
`xl/drawings/drawing*.xml`、drawing `.rels`、worksheet `.rels`、worksheet
`<drawing>` 和 content type entries。memory-source overload 不持有 caller span，
不保留 decoded pixel buffer，也不把图片 bytes 放进 worksheet row/cell 热路径。
它不裁剪、不旋转、不压缩、不格式转换、不编辑已有 drawing，也不代表
existing-workbook 图片保真或完整 drawing 支持。
当前还支持窄 `ImageOptions` metadata：`from_offset` / `to_offset` 是
`ImageAnchorOffset` EMU 值，只写到现有 two-cell anchor marker 的 `xdr:colOff` /
`xdr:rowOff`；`edit_as` 只写成 drawing XML `xdr:twoCellAnchor editAs` attribute；
非空 `name` 会写成 drawing XML `<xdr:cNvPr name="...">`，非空 `description`
会写成 `descr="..."`；空 `name` 保留生成的 `Picture N`，空 `description` 省略。
非空 `external_hyperlink_url` 会在 `xdr:cNvPr` 下写 `a:hlinkClick`，
并在 drawing-owned `.rels` 中创建 `TargetMode="External"` 的 hyperlink relationship；
非空 `external_hyperlink_tooltip` 只写该 `a:hlinkClick` 的 `tooltip` attribute，
且没有 URL 时会被拒绝。这些 metadata 不修改图片二进制、EXIF、media filename、
anchor cell range、content types 或 worksheet cell text；图片对象 hyperlink 也不写
worksheet `<hyperlinks>`、不消耗 worksheet hyperlink relationship id、不创建 cell
hyperlink style、不校验 URL 可达性，也不支持 internal picture link、existing-file
editing、完整 hyperlink UI、`oneCellAnchor` / `absoluteAnchor` 元素或 row/column
resize 几何计算。

不要把文档里的设计名当成已实现符号。`WorkbookWriter` / `WorksheetWriter` /
`CellView` 流式写入骨架已存在；公式、行高、列宽、冻结窗格、自动筛选、
  合并单元格、data validations、external/internal hyperlinks、two-/three-color conditional
  color scales、basic data bars、basic 3Arrows icon sets 和 streaming-only tables 是当前写入骨架能力，不等同完整 Phase 3
  或完整 Phase 5。当前可见
`StringStrategy::SharedString`、内部 `SharedStringTable` 和
`xl/sharedStrings.xml` 结构测试，状态只能写为 sharedStrings 进行中或基础，
不能写成生产策略完成。当前有基础可配置 `docProps/core.xml` /
`docProps/app.xml` public metadata API：`DocumentProperties` 可设置 creator、
lastModifiedBy、title、subject、description、keywords、category、Application 和
AppVersion，并被 `Workbook::save()` / `WorkbookWriter::close()` 写入新建
workbook package；它不生成 `docProps/custom.xml`，不代表完整 document properties
API，也不是 existing-file editing。`CellEncoder` 等名称仍主要来自架构文档或路线图；
只有在 `include/` 或 `src/` 中找到对应实现后，才能当作真实 API。
当前已有内部 `src/package_reader.*` ZIP entry reader 基础，可索引和按名读取
stored/no-compression package entries（包括 unknown entries）；在
`FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建下还可通过 minizip-ng 读取 DEFLATE
entries，默认构建仍拒绝 compressed input。读取 entry 时校验解压后 payload CRC，
并拒绝非法 ZIP entry name（绝对路径、尾部斜杠、反斜杠、query/fragment
components、空段、dot 段或 parent 段）、local-header CRC/method/name/size mismatch、encrypted、data descriptor、Zip64、multi-disk、重复 entry、坏 header 和损坏
metadata/payload bytes；它还会解析 `[Content_Types].xml` 和
`.rels` 小型 OPC metadata，要求第一个真实 XML 元素分别是 `Types` / `Relationships`，
只 ingest root 的 direct-child `Default` / `Override` / `Relationship` 元素，
metadata attributes 必须未命名空间且未命名空间 metadata attributes 不得重复，
并拒绝嵌套 decoy root、嵌套 declaration decoy、
namespaced metadata attribute decoy、非 whitespace metadata text、
start/end tag QName mismatch、
冲突的 content type default / override、
同一 `.rels` owner 内重复的 relationship id，并建立内部 `PartIndex` /
`RelationshipGraph` 视图；
当前 reader-only 回归还覆盖可达 unknown extension part 自己的 source-owned
owner `.rels` 会保持 metadata-only，并能重新挂到该 unknown owner 的
`RelationshipGraph` relationships。
当前 reader workbook sheet catalog resolver 会先验证 package `_rels/.rels`
中存在且仅存在一个 internal `officeDocument` relationship；当前窄实现只接受
解析到 `/xl/workbook.xml` 的 target，并显式覆盖 `xl/./workbook.xml` 这类
dot-segment 相对 package entrypoint；缺失、重复、external、带 query/fragment
或非固定 target 会在 lookup 阶段失败。
它不复制原始 part，不重写 package，也不是 public `PackageEditor` 或完整
existing-file editing。当前另有内部 `src/package_editor.*` copy/replace 基础，
可打开 `PackageReader` 支持的既有 package（默认 stored/no-compression；minizip
构建可读 DEFLATE 输入）、替换一个已存在 part，并在输出新 package
时保留未替换 entries 的解压后 payload（含 unknown entries）；minizip-enabled
PackageEditor 回归还覆盖 DEFLATE source 上 ordinary workbook replacement、
unknown extension target replacement、workbook calc metadata helper，以及 worksheet
replacement 下的 calcChain cleanup、linked payload preservation 和
unknown extension owner `.rels` 由输出 `PackageReader` / `RelationshipGraph` 重读；
它不保留源 ZIP compression method、timestamps、extra fields 或压缩字节。当前还支持内部
core/app document-properties generated-small-XML 窄切片：可为既有 package
生成/替换 `docProps/core.xml` 和 `docProps/app.xml`，包括两者都缺失的输入，必要时同步
`[Content_Types].xml` 和 package `_rels/.rels`，并保留未修改 entries；
当前还支持内部
worksheet replacement 窄切片：替换已有 `/xl/worksheets/sheetN.xml` 时，按默认
`ReferencePolicy` 省略 stale `xl/calcChain.xml`、从 `[Content_Types].xml` 移除
calcChain override、从 workbook `.rels` 移除 calcChain relationship，并在
`xl/workbook.xml` 设置 `fullCalcOnLoad="1"`；若源包没有 `xl/calcChain.xml`
payload 但残留 calcChain content type override 或 workbook calcChain relationship，
worksheet replacement 也会清理这些 stale metadata，不记录缺失 payload 的
removed-part audit、不创建 calcChain payload。这只是窄 metadata cleanup，不是
完整 relationship/content-type repair。当前还支持内部
`PackageEditor::replace_worksheet_part_by_name()` 目标定位 helper：它通过
`PackageReader` workbook sheet catalog resolver 找到已有 worksheet part；该 resolver
会先从 package root 解析 `_rels/.rels` 的 internal `officeDocument` target，
相对、绝对和 dot-segment package target（例如 `xl/./workbook.xml`）都会从
package root 规范到 `/xl/workbook.xml`，且不会把 package root
建模成真实 `PartName`；当前 reader / source-catalog by-name Patch 回归还覆盖 workbook-owned
绝对 worksheet target（例如 `/xl/worksheets/sheetN.xml`）和 dot-segment
相对 target（例如 `./worksheets/../worksheets/sheetN.xml`）解析到已有 worksheet
part，并被完整 worksheet by-name replacement 与 by-name `sheetData` patch
两条 source-catalog 路径复用；输出后保留 worksheet relationship 的 target
字面值和 unknown extension bytes；当 calcChain cleanup 需要重写
workbook `.rels` 时，这不是整份 workbook relationships 字节保真，也不是任意
workbook part-location、relationship repair、pruning 或 public API。它还要求 sheet relationship id 使用 officeDocument relationships XML namespace（可用非
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
name helper：它只改当前 planned `/xl/workbook.xml` 中直接 `<sheets><sheet
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
当前还支持内部 `PackageEditor::replace_worksheet_sheet_data()` sheetData 局部替换 helper：
它只替换既有 worksheet XML 中的 `<sheetData>` 元素或 self-closing
`<sheetData/>`，保留同一 worksheet part 中的 sheetPr、dimension、sheetViews、
sheetFormatPr、cols、sheetProtection、protectedRanges、autoFilter、mergeCells、
scenarios、dataConsolidate、customProperties、cellWatches、smartTags、webPublishItems、
dataValidations、conditionalFormatting、hyperlinks、ignoredErrors、printOptions、
pageMargins、pageSetup、drawing、legacyDrawing、picture、legacyDrawingHF、
oleObjects、controls、tableParts 和 extLst 等外围 XML，
然后复用 worksheet replacement 路径处理 calcChain remove、workbook
fullCalcOnLoad、relationships/content types audit 和 unknown part preservation。
当前 helper 仍会物化当前 planned worksheet XML，因此受内部
`package_editor_sheet_data_local_rewrite_byte_limit` 保护；source/queued worksheet
XML、replacement `<sheetData>` payload 或 rewritten worksheet XML 超过该 bounded local rewrite 限制时，
direct/by-name 路径都会在状态变更前失败，不污染 EditPlan、manifest、
package-entry audit、calc policy、planned output 或输出 bytes，并保持 unknown
extension bytes 原样。成功路径也会在 EditPlan/output-plan note 和 worksheet
part reason 中暴露 bounded local rewrite 边界，不能写成大文件低内存 streaming
worksheet transformer。
当前结构测试还验证 sheetData patch 输出后，worksheet `.rels` 中的
legacyDrawing `rId7` / `../drawings/vmlDrawing1.vml#shape1` 仍可由
`PackageReader` / `RelationshipGraph` 重读。
当前还覆盖 worksheet-owned background picture part 与 header/footer VML drawing
part preservation：`sheetData` 局部替换会保留 `<picture>` / `<legacyDrawingHF>`
引用、worksheet `.rels` 中的 `image` / `vmlDrawing` relationships、
`xl/media/background.png` bytes、`xl/drawings/vmlDrawingHF1.vml` bytes、PNG
content type default 和 VML content type override，并把这些 part 作为
relationship-derived copy-original entries 暴露到 `EditPlan` / planned output；这只是
Patch preservation / audit 可见性，不是图片/VML/header-footer 语义编辑、
relationship repair/pruning、orphan cleanup、content type repair、public API 或
完整 object preservation。
当前还覆盖 worksheet-owned printerSettings opaque part preservation：`sheetData`
局部替换会保留 `<pageSetup r:id>` 引用、worksheet `.rels` 中的
`printerSettings` relationship、`xl/printerSettings/printerSettings1.bin` bytes 和
content type override，并把该 part 作为 relationship-derived copy-original entry
暴露到 `EditPlan` / planned output。内部 `planned_output()` 快照现在还覆盖该
状态的边界：fullCalcOnLoad / `CalcChainAction::Remove`、worksheet / workbook
`LocalDomRewrite`、content types / package relationships / workbook relationships /
worksheet relationships copy-original、printerSettings part copy-original
relationship metadata、preserved pageSetup caller-review note、无 relationship
target audit、无 removed parts / removed package entries，且不凭空创建
`xl/calcChain.xml`；这仍只是 Patch preservation / audit 可见性，不是打印设置
语义编辑、calcChain rebuild、relationship repair/pruning、orphan cleanup、
content type repair、public API 或完整 object lifecycle 支持。
当前还覆盖同一 fixture 的显式 removal audit：移除 `xl/media/background.png`
会输出省略目标 media part、保留 PNG default 且不提升为 override，并保留
worksheet `.rels` 中指向缺失 background picture 的 inbound relationship；移除
`xl/drawings/vmlDrawingHF1.vml` 会输出省略目标 VML part、移除 VML content type
override，并保留 worksheet `.rels` 中指向缺失 header/footer VML 的 inbound
relationship；两条路径都会在 `EditPlan` / planned output 暴露结构化
removed-part inbound relationship audit。这仍只是 Patch audit / no-pruning
可见性，不是图片/VML/header-footer 删除语义、relationship repair/pruning、
orphan cleanup、content type repair、public API 或完整 object lifecycle 支持。
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
content type repair、public API 或完整 object lifecycle 支持。
当前还覆盖 worksheet-owned registered OLE opaque part 与 control-property part
preservation：`sheetData` 局部替换会保留 `<oleObjects>` / `<controls>` 引用、
worksheet `.rels` 中的 `oleObject` / `control` relationships、
`xl/embeddings/oleObject1.bin` bytes、`xl/ctrlProps/control1.xml` bytes 和
对应 content type overrides，并把这些 part 作为 relationship-derived
copy-original entries 暴露到 `EditPlan` / planned output；这只是 Patch
preservation / audit 可见性，不是 OLE / ActiveX / control 语义编辑、
relationship repair/pruning、orphan cleanup、content type repair、public API 或
完整 object preservation。
当前还覆盖同一 fixture 的显式 removal audit：移除 `xl/embeddings/oleObject1.bin`
会输出省略目标 OLE part、移除 OLE content type override，并保留 worksheet
`.rels` 中指向缺失 OLE object 的 inbound relationship；移除
`xl/ctrlProps/control1.xml` 会输出省略目标 control-property part、移除 control
properties content type override，并保留 worksheet `.rels` 中指向缺失 control
properties 的 inbound relationship；两条路径都会在 `EditPlan` / planned output
暴露结构化 removed-part inbound relationship audit。这仍只是 Patch audit /
no-pruning 可见性，不是 OLE / ActiveX / control 删除语义、relationship
repair/pruning、orphan cleanup、content type repair、public API 或完整 object
lifecycle 支持。
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
public API 或完整 object lifecycle 支持。
当前还覆盖源 worksheet 使用 self-closing `<sheetData/>` 的成功替换回归：
输出写入普通 `<sheetData>...</sheetData>`，保留 dimension / autoFilter，
沿默认 calcChain remove / fullCalcOnLoad 路径清理 stale 计算 metadata，
并保留 unknown bytes。
当前还覆盖 replacement payload 自身为 self-closing `<sheetData/>` 的成功替换：
可清空旧 row/cell，输出保留 `<sheetData/>` 与外围 dimension / autoFilter，
继续走默认 calcChain remove / fullCalcOnLoad 和 unknown bytes preservation。
当前还覆盖 source worksheet 和 replacement payload 使用前缀形式
`<x:sheetData>` / `<x:worksheet>` 的成功替换：helper 按 local-name 匹配，
输出保留原 wrapper / replacement 字面前缀，仍沿默认 calcChain cleanup
与 unknown bytes preservation 路径；这不是通用 namespace repair。
当前还覆盖先排队 worksheet replacement 再调用 sheetData patch 的组合回归：
patch 基于当前 planned worksheet bytes 替换，覆盖 queued worksheet 中普通
`<sheetData>` 和 self-closing `<sheetData/>` 两种形态，保留 queued wrapper
metadata，不会把 source-only worksheet metadata 复活。
成功替换后还会在内部 `EditPlan` notes 中记录这些 worksheet-local metadata
被保留且 range/reference 可能需要 caller review；这只是审计可见性。
若 replacement `<sheetData>` 自身使用 shared string indexes、style id references
或公式 cell，当前也只会追加内部 `EditPlan` audit notes 和结构化
`WorksheetPayloadDependencyAudit`，提示 caller 复核
`xl/sharedStrings.xml`、`xl/styles.xml` 和 workbook calc metadata / calcChain policy；
它不迁移 sharedStrings 索引、不合并 styles、不计算公式，也不重建 calcChain。
invalid/malformed replacement XML、source worksheet 缺失 `sheetData`，以及 source worksheet
`<sheetData>` 起始标签存在但闭合标签损坏/缺失时，当前回归验证失败不会污染
EditPlan、manifest、package-entry audit、calc policy 或输出 bytes；这不是 XML repair。
这仍不是 public API、随机 cell 编辑、dataValidations/conditionalFormatting/
hyperlinks/table/drawing 语义同步、sharedStrings/styles 迁移、dimension/range
修复或大文件低内存 worksheet transformer。当前还支持内部
`PackageEditor::request_full_calculation()` workbook calc metadata helper：
只局部重写 `/xl/workbook.xml` 小型 metadata part，把 `fullCalcOnLoad`
规范为 `1`，并可按 `CalcChainAction::Remove` 清理 stale `xl/calcChain.xml`
payload、calcChain content type override、workbook calcChain relationship 和
calcChain owner `.rels` audit/omission；同一路径也覆盖只有 stale calcChain
content type override 或 workbook relationship、没有 `xl/calcChain.xml` payload
的 metadata-only cleanup，不伪造 removed-part audit、不创建 calcChain payload；
该 helper 现在只更新 workbook 根元素的直接子级 `calcPr`，会保留 `extLst`
或 custom extension 内的嵌套同名 decoy；若没有直接子级 `calcPr`，只在真实
workbook closing tag 前插入一个按 workbook 根前缀命名的 `calcPr`；
`CalcChainAction::Preserve` 会保留
calcChain payload、content type、workbook relationship 和存在的 calcChain owner
`.rels`；`CalcChainAction::Rebuild` 未实现并失败不污染 edit plan、manifest 或
输出包状态。该 helper 不碰 worksheet XML、linked objects 或 sharedStrings/styles/
tables/drawings/VBA/unknown extension 语义，也不是 public API、公式求值或
relationship repair。DEFLATE 源包回归中，与 workbook calc helper 无直接因果关系的
unknown owner `.rels` 只通过 `planned_output()` copy-original 可见性和输出重读验证保留，
不写成 `EditPlan` package-entry side-effect audit。docProps generated-small-XML
和 worksheet replacement 也已有组合回归，覆盖同一 edit 里
relationships/content types 状态合并、calcChain removal、stale calcChain owner
`.rels` omission、workbook metadata rewrite、unknown entry preservation 和
exact/path-equivalent source-overwrite rejection 和 empty-output / missing-parent / non-directory-parent / existing-directory output rejection；malformed workbook metadata /
workbook calc metadata rewrite 预检失败和 invalid replacement 失败也覆盖
不污染 edit plan entries / notes、aggregate `planned_output()` / legacy output-entry preview、
manifest write-mode 和输出包字节；core/app docProps package relationship target 冲突
失败也覆盖不污染 edit plan entries / notes、manifest / 输出包状态。普通 part replacement、docProps generated parts
和 worksheet replacement 会把 write-mode / dirty / generated /
preserve-original 状态同步到内部 manifest；内部 `EditPlan` 还会记录非 part
package-entry 审计项，当前覆盖 `[Content_Types].xml`、package `_rels/.rels`、
workbook `.rels` rewrite、removed calcChain owner `.rels` omission，以及窄 worksheet
replacement 中被保留的 root-level `_rels/foo.xml.rels`、worksheet / drawing /
preserved calcChain 等 source-owned `.rels` 存在时的 copy-original 审计；当前
这些非 part package-entry 审计还带内部结构化分类：content types、package
relationships、source-owned relationships；只有 source-owned `.rels` 审计携带
owner part，且 EditPlan 入口会校验 kind、entry name 和 owner-derived `.rels`
路径一致性；这供 Patch traceability 使用，不是 public metadata editor；
root-level ordinary owner replacement roundtrip 还验证这些 source-owned `.rels`
可由 `PackageReader` / `RelationshipGraph` 重新挂回 owner part；worksheet
replacement 还会把被重写的 workbook metadata part 标记为 `LocalDomRewrite`，
供 Patch 审计；内部 `EditPlan` 还覆盖 part-level set/remove 互斥，已移除 part
后续恢复为 active entry 时会清理 stale removed-part audit，已有 relationship-derived
entry 被改成 rewrite/generate entry 时也会清理 stale relationship metadata；package-entry
set/remove 也有互斥回归，避免 active package-entry audit 和 removed package-entry audit
同时残留。重复 ordinary `replace_part()` 现在有回归覆盖，验证同一 part
再次替换时最终 bytes、write mode、edit-plan reason、manifest state 和 preserved
source-owned `.rels` audit 都以上一次替换为准；metadata-aware helper 先生成的
docProps part 也可被后续 ordinary `replace_part()` 覆盖，输出 bytes、EditPlan 和
manifest 以最后的 ordinary part replacement 为准，同时保留 content types /
package relationships 的 helper-managed audit；反向顺序也有窄回归，metadata-aware
helper 后调用时会接管此前 ordinary replacement 或 explicit removal 的 core/app docProps part，
清理 stale removal / omitted payload 状态，输出 bytes、EditPlan 和 manifest 以 helper
生成状态为准；它只恢复 core/app payload、content types 和 package relationships，不恢复此前
removal 省略的 docProps owner `.rels`，并已有输出 omission 和 removed package-entry
audit 结构回归；这不是事务式 undo。worksheet replacement 删除
calcChain 时也会压过此前 ordinary calcChain replacement，并接管此前 ordinary
workbook replacement 以写入 helper-generated fullCalcOnLoad metadata；如果后续又普通替换
`/xl/workbook.xml`，仍会保留 worksheet rewrite 请求的 fullCalcOnLoad / calcChain
removal policy，把 workbook XML 中的 `fullCalcOnLoad` 规范为 `1`，且不会把已重写的
workbook `.rels` package-entry audit 降级为 copy-original，避免旧 replacement 在输出包中复活。
当 worksheet replacement 显式使用 `CalcChainAction::Preserve` 时，此前 ordinary
calcChain replacement 会保持 active `LocalDomRewrite` 并作为最终 `xl/calcChain.xml`
payload 写出，同时保留 calcChain owner `.rels` copy-original audit；这仍不是 calcChain
rebuild、公式求值或 relationship repair。
workbook-only `request_full_calculation()` 也覆盖 prior ordinary workbook / calcChain
replacement 顺序组合：helper 会使用已排队 workbook XML，保留其中非 calc workbook
metadata，规范 `fullCalcOnLoad="1"`，并清理 calcChain payload/content
type/workbook relationship；此前排队的 calcChain replacement 不会在输出包复活，
也不会回退到 source workbook bytes。
若调用时选择 `CalcChainAction::Preserve`，此前排队的 calcChain replacement 会保持为
active `LocalDomRewrite` 并作为最终 `xl/calcChain.xml` payload 写出，同时只把
calcChain owner `.rels` 作为 copy-original 审计；这仍不是 calcChain rebuild 或公式求值。
普通 `replace_part()` 现在会显式拒绝 `[Content_Types].xml`、
package `_rels/.rels` 和 source-owned `.rels` 这类 OPC metadata entry 作为 ordinary
part target，以防绕过 metadata-aware helper 和 package-entry audit；这不是完整
relationship/content-type mutator。metadata-entry replacement 拒绝回归也覆盖
edit plan entries / notes、package-entry audit、calc policy、manifest write-mode
、aggregate `planned_output()` / legacy output-entry preview 和输出包字节不污染。它不新增 worksheet，不对
sharedStrings/styles/tables/drawings/defined names 做语义联动同步、索引迁移或重建，
不支持 Zip64、data descriptor、encrypted input 或写回覆盖 source package；`save_as()` 拒绝输出到
exact 或 path-equivalent 的源 package 路径，也会在 materialize output entries 前拒绝
空路径、缺失父目录、非目录父路径或已存在目录作为输出路径，因为当前 reader-backed copy 路径
不是 atomic in-place editor；当前回归还验证这些 guard 拒绝后 queued part replacement、
structured audit snapshots、calc policy 和 removal audits
仍保留在 `EditPlan` / manifest / planned output 中，后续安全 `save_as()`
仍输出 queued rewrite 并保留 untouched worksheet / unknown bytes；同一 guard
现在也覆盖 queued worksheet replacement 的 `fullCalcOnLoad` / calcChain removal /
package-entry audit / planned output 状态，后续安全输出仍按计划省略 calcChain 并保留
unknown bytes，
也不是 public API 或完整 preservation 证明。当前结构测试
还覆盖该窄切片下 worksheet `.rels`、drawing、drawing `.rels`、media、chart、table、
`xl/sharedStrings.xml`、sharedStrings owner `.rels`、`xl/styles.xml`、VBA 和可达
unknown extension part 及其 owner `.rels` 的 byte-for-byte preservation，
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
内部 `planned_output()` 快照现在也覆盖该 ordinary replacement 状态：暴露 active
custom XML item `LocalDomRewrite` entry、owner `.rels` copy-original
`SourceRelationships` audit、preserved package relationships、content types、
workbook、worksheet、properties part 和 unknown entry，且 output-plan removed_parts /
removed_package_entries 为空、不凭空创建 properties owner `.rels`；这仍只是
Patch audit 可见性，不是 custom XML 语义编辑或 relationship/content-type repair；
同一 custom XML 小 fixture 还覆盖显式移除 `customXml/item1.xml`：输出省略
custom XML item 及其 source-owned owner `.rels`、保留 package `_rels/.rels`
customXml inbound relationship、保留 `customXml/itemProps1.xml`、custom XML
properties content type override、默认 XML content type 和 unknown entry，且不重写
`[Content_Types].xml`；这不是 custom XML 删除语义、schema/data binding、
relationship pruning/repair、content type repair、orphan cleanup 或 public API；
内部 `planned_output()` 快照现在也覆盖该 explicit removal 状态：暴露 omitted
custom XML item、omitted source-owned owner `.rels`、removed-part / removed owner
`.rels` audit、package inbound customXml relationship audit，以及 preserved package
relationships、content types、workbook、worksheet、properties part 和 unknown entry；
仍不凭空创建 properties owner `.rels`；这只是 Patch audit 快照，不是 custom XML
删除语义、relationship pruning/repair、content type repair、orphan cleanup 或
public API；
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
以及 workbook `definedNames` 在 calc metadata rewrite 路径下的保留回归、`ReferencePolicyAction::Fail`
拒绝 linked worksheet rewrite 后不污染 edit plan entries / notes 与 manifest write-mode、
并覆盖已有 ordinary workbook replacement 排队后，失败的 linked worksheet rewrite
或 inbound-linked removal strict fail 仍保留既有 replacement、manifest write-mode、
source-owned `.rels` audit、aggregate `planned_output()` 快照和输出 bytes；
还覆盖 queued core/app docProps generated-small-XML 后，同类失败仍保留既有
docProps edit plan、manifest、package-entry audit、aggregate `planned_output()` 快照和输出 bytes；
`CalcChainAction::Preserve`
保留 stale calcChain、`CalcChainAction::Rebuild` 未实现拒绝不污染状态、以及
`ReferencePolicyAction::RequestRecalculation` 的 planner 分支和窄 PackageEditor
fullCalcOnLoad 输出回归路径；还覆盖 malformed workbook XML 导致 workbook metadata
rewrite 预检失败、worksheet rewrite 缺少 `xl/workbook.xml` 前置 metadata 时失败不污染状态、
以及 core/app docProps package relationship target 冲突失败时
不污染 edit plan entries / notes、manifest / 输出包状态；malformed workbook metadata /
workbook calc metadata rewrite 预检失败和 invalid replacement failure 还覆盖
edit plan entries / notes、aggregate `planned_output()` / legacy output-entry preview、
manifest write-mode / copied output bytes 不污染，但这仍不是完整图片/图表/VBA
passthrough、复杂对象编辑支持或完整 validator。OPC 当前有 `PartWriteMode` 和 package part edit-state 基础，
当前 no-state-pollution 回归还把 `relationship_target_audits()` 和
`worksheet_relationship_reference_audits()` 纳入状态快照，覆盖 linked worksheet policy fail、
calcChain rebuild rejection、missing sheet-name worksheet/sheetData lookup、invalid
worksheet XML / sheetData source、malformed workbook metadata、missing workbook metadata、
invalid replacement、metadata-entry replacement 和 invalid removal 等失败路径不会追加
stale 结构化 relationship target / worksheet reference audit；其中 invalid
replacement、metadata-entry replacement、invalid removal 和 inbound-linked removal
`ReferencePolicyAction::Fail`（含已有 ordinary workbook replacement 排队后的失败）
还额外快照
worksheet/workbook payload dependency audit、removed audit 和 calc policy；
linked worksheet policy failure、malformed workbook metadata / workbook calc metadata、
invalid replacement、metadata-entry replacement、invalid removal 和 inbound-linked removal policy failure
还覆盖 aggregate `planned_output()` /
legacy output-entry preview 维持 source copy-original
快照；这只是内部 Patch audit
状态卫生，不代表 relationship validation 或 repair。
当前新建 workbook 写出路径有内部 `PackageEntry` / `write_package` 边界。
worksheet part finalization 已支持 file-backed/chunked package entry，避免在
`WorkbookWriter::close()` 阶段重新物化完整 worksheet XML 字符串。默认 ZIP backend
仍由 stored ZIP bootstrap 支撑；启用 `FASTXLSX_ENABLE_MINIZIP_NG` 时会使用
minizip-ng DEFLATE backend。它仍不是已有文件编辑用 public `PackageWriter`，
也不代表 true package streaming、Zip64、完整低内存或已验证大文件性能。
已有文件编辑和 unknown part preservation 仍是计划。当前已有内部
`PartIndex`、`RelationshipGraph` 和 `ContentTypeRegistry` 基础，但这只代表
owner-aware relationship / content type groundwork；不要宣称 package editing、
完整 hyperlink、完整 conditional formatting、完整图片、VBA 或完整 table 支持。

当前 styles 基础切片已存在：`WorkbookWriter::add_style(CellStyle)`
会在 workbook state 注册 streaming-only new-workbook 样式，当前支持自定义
number format、窄 `CellAlignment::wrap_text`、窄 `HorizontalAlignment` /
`VerticalAlignment`、窄 `CellFont::bold` / `CellFont::italic` / direct ARGB
`CellFont::color` 和窄
`CellFill` solid foreground ARGB fill。重复完整 style 复用同一个 `StyleId`；相同 number format
在不同 style 组合里复用同一个 custom `numFmtId`；相同 bold/italic/color font
组合在不同 style 组合里复用同一个 `fontId`；相同 foreground ARGB fill
组合在不同 style 组合里复用同一个 `fillId`，自定义 `fillId` 从 `2` 开始，因为
`0/1` 是默认 `none` / `gray125` fills。`CellView::with_style()` 只携带 workbook-local `StyleId` 小句柄，
`WorksheetWriter::append_row()` 会在推进 row count、dimension、sharedStrings
或公式重算状态前验证非默认 style id。使用样式时 `close()` 会写
`xl/styles.xml`、styles content type override 和 workbook relationship；worksheet
cell 会写 `s="N"`，默认样式不写 `s="0"`。alignment 当前只写
`cellXfs` 里的 `applyAlignment="1"` 和 `<alignment .../>` attributes：
`wrapText="1"`、`horizontal="left|center|right"`、
`vertical="top|center|bottom"`；不计算行高、不代表完整 alignment。
font slice 只写 `<fonts>` 里的 `<b/>` / `<i/>` 和可选 direct
`<color rgb="..."/>`、`fontId` 和 `applyFont="1"`，不代表完整 font control、
theme/tint/indexed font color、font size/name、underline 或 rich text。solid fill 只写 `<fills>` 中的
solid `<patternFill>`、`fgColor rgb`、`bgColor indexed="64"`、`fillId` 和
`applyFill="1"`，不代表完整 fill/pattern/theme/tint/indexed palette control。
该切片不支持字号、字体名、下划线、边框、完整 alignment、rich text、
dxf-backed conditional formatting、date cell type、existing-file style preservation
或完整 Excel formatting parity。当前 two-/three-color
conditional color scale、basic data bar 和 basic 3Arrows icon set 是 worksheet metadata
切片，不走 styles registry 或 `dxfs`。

当前数值 XML 写入边界是：`Workbook::save()` 会在序列化时拒绝 in-memory
numeric cell 中的 `NaN` / `+Inf` / `-Inf`，并要求 in-memory row height 为正且有限；
in-memory 一行超过 16384 列时也会在序列化 cell reference 时拒绝；
`WorksheetWriter::append_row()` 会在写入前拒绝 streaming numeric cell 中的非有限值，
并要求 streaming row height 为正且有限；`WorksheetWriter::set_column_width()`
要求 width 为正且有限。不要让 OpenXML worksheet XML 写出 `nan`、`inf`、`-inf`
或非正 row height / column width 数字文本。

## 本轮推进计划同步

- 阶段任务重排：当前默认推进线从“Phase 2/性能硬化优先”调整为
  “P4.0 API surface unification 先行，窄 Patch MVP 随后推进，性能硬化并行支撑”。
  下一轮实现或设计任务必须先按 `docs/TASK_BREAKDOWN.md` 选择子任务编号，不再把
  “任务 4”或整个 Phase 4 当作一个可直接执行的大任务。P4.0 先统一
  `WorkbookWriter` / `Workbook` / future `WorkbookEditor` 门面、
  `CellView` / `Cell` / `CellValue` 边界和 internal/public 分界；之后再围绕
  `PackageReader`、`PackageEditor`、`EditPlan`、`DependencyAnalyzer`、
  `ReferencePolicy`、`PartRewritePlanner`、unknown part preservation、
  part-level copy/rewrite 和 sheet 联动分析推进窄 Patch MVP。sharedStrings、minizip、
  benchmark、file-backed/chunked worksheet entry 和 streaming hot path 继续推进，
  但不再作为阻塞编辑能力的唯一主线。In-memory 小文件随机编辑是单独路径，可规划
  `get_cell()` / `set_cell()` / `erase_cell()` 等便利能力，但不能成为大文件低内存路径，
  也不能把已实现的 `CellValue` value type 扩大成 public `WorkbookEditor`、
  public `PackageEditor` 或 editor-ready 口径。
- 跨语言库参考矩阵：`docs/TECHNICAL_COMPARISON.md` 记录 C/C++、Python、Java、.NET、
  Go、Rust、JavaScript、PHP、Ruby、R/Julia 等生态的 XLSX 库参考边界。新增功能时可以
  吸收其他库的 API 体验、写入性能、大文件低内存、OPC 严谨性或兼容性样例，但必须写清
  对应到 FastXLSX 的 In-memory / Streaming / Patch 哪条路径，不能把所有优点堆进一个
  无边界的完整 workbook DOM。
- sharedStrings：进行中。当前可见 API 选项、内部表、package wiring 和结构测试；
  当前已有默认 CTest 结构覆盖、本机 Excel COM 只读验证，以及
  `tools/verify_shared_strings_reference.py` 的 `openpyxl` 参考语义检查；
  系统 `py` 当前也验证了 `XlsxWriter` 参考 workbook；缺少 `xlsxwriter` 的 Python
  环境只记录为可选跳过。仍需更大规模大小/内存数据和更多参考兼容性数据后，才能扩大支持表述。
  当前结构测试还覆盖 `StringStrategy::SharedString` 启用但没有字符串 cell 的场景：
  只写数字、布尔和公式时，不生成空 `xl/sharedStrings.xml`、sharedStrings content type
  或 workbook relationship，worksheet 也不会写 `t="s"` 或 `inlineStr`；公式仍会触发
  workbook `<calcPr fullCalcOnLoad="1"/>`。这只是空表 package hygiene，不代表
  sharedStrings 策略完成。当前本地 QA helper
  `tools/verify_shared_strings_absence.py` 会用 ZIP / `openpyxl` / 可选
  `XlsxWriter` 验证该样例并写本地 report；
  `tools/verify_shared_strings_absence_excel.ps1` 会用本机 Excel COM 只读打开并核对
  `NoStrings!A1:C1`。
  当前已有 2026-06-07 本机小规模手工 benchmark 快照：`strings` 场景
  `50000 x 10 x 1 = 500000` cells。historical schema v3 结果记录了
  `temporary_worksheet_part_footprint="worksheet-body-file-bytes"`：repeated/inline
  为 `493 ms`、`4.97266 MB`、`27927834` worksheet body bytes、`27931711` output
  bytes；repeated/shared 为 `392 ms`、`4.98828 MB`、`16927834` worksheet body
  bytes、`16932289` output bytes；unique/inline 为 `658 ms`、`4.97266 MB`、
  `30866774` worksheet body bytes、`30870651` output bytes；unique/shared 为
  `1045 ms`、`70.1055 MB`、`19316724` worksheet body bytes、`33260102` output
  bytes。四个输出已用本机 Excel COM 只读打开并核对 `Sheet1` 使用范围和首尾值；
  这只是 stored-bootstrap ZIP 下的小规模趋势快照，不是 sharedStrings 生产就绪、
  默认最佳策略、完整低内存或大文件性能结论。
  当前另有 2026-06-10 schema-v4 本机矩阵快照：`strings` 场景
  `10000 x 10 x 1 = 100000` cells per case，stored-bootstrap ZIP，
  repeated/inline 为 `84 ms`、`5.04297 MB`、`5487834` worksheet body bytes、
  `5491711` output bytes；repeated/shared 为 `61 ms`、`5.0625 MB`、
  `3287834` worksheet body bytes、`3292289` output bytes；unique/inline 为
  `111 ms`、`5.03125 MB`、`5986774` worksheet body bytes、`5990651` output
  bytes；unique/shared 为 `325 ms`、`18.2383 MB`、`3676724` worksheet body
  bytes、`6380102` output bytes。`tools/run_benchmark_matrix.py` 还用
  `openpyxl` 只读验证了四个输出 workbook 的 `Sheet1` 首尾值；benchmark JSON 的
  `office_open` 仍为工具原始 `not_run`。当前 `tools/run_benchmark_matrix.py --self-test`
  可在不调用 benchmark exe、不写 workbook artifact 的情况下验证 runner 内部
  case 解析、字符串分布期望值、单元格期望值和 matrix report shape；它不是
  benchmark 运行、Office/openpyxl 兼容性验证或性能证据。
- vcpkg / CMakePresets / CI：基础。默认 preset 是 vcpkg-backed 的 VS2026/NMake
  路径，会安装默认 `stb` 依赖；`windows-nmake-release-minizip` 是 opt-in vcpkg
  验证路径，会启用 `FASTXLSX_ENABLE_MINIZIP_NG` 和 `planned-runtime`。
  CI workflow 已有独立 opt-in minizip matrix，但不要把 CI 覆盖写成生产化依赖。
- package writer boundary：基础。`src/package_writer.*` 已把新建 workbook
  输出从 workbook writer 代码中隔离出来，并支持 memory/file chunk entry source。
  `WorkbookWriter` 的 worksheet part 以 header + file-backed body + footer 写入内部
  package writer；sharedStrings XML 也可通过临时 file-backed entry 写入，但 shared string
  table 本身仍保留唯一字符串状态。默认 backend 是 `src/zip_store_writer.*` 的
  stored/no-compression bootstrap，opt-in backend 是 `minizip-ng[core,zlib]` DEFLATE。
  内部 writer 会在打开输出路径前拒绝非法或重复 ZIP entry name，以及缺失或不可
  stat 的 file-backed chunk，并拒绝同一 entry 同时携带 legacy `data` payload 和
  chunked payload、memory/file chunk-source 混用或未知 chunk kind。
  不要据此宣称 Zip64、true package streaming、existing-file editing、完整低内存或
  大文件性能。
- document properties：基础。`DocumentProperties`、`Workbook::set_document_properties()`
  和 `WorkbookWriterOptions::document_properties` 已支持新建 workbook 的基础
  `docProps/core.xml` / `docProps/app.xml` 配置；当前只覆盖 core/app 小型 XML
  part，不创建 `docProps/custom.xml`，也不代表完整文档属性 API。内部
  `PackageEditor` 另有 existing-package core/app docProps generated-small-XML
  窄切片，可新增/替换这两个 metadata part 并同步 package relationships /
  content types；当前回归还覆盖该 helper 重写 `_rels/.rels` / `[Content_Types].xml`
  时保留既有 `docProps/custom.xml`、custom-properties package relationship、
  custom properties content type override 和 unknown part bytes。它不是 public
  document-properties editing API，也不支持 custom properties 编辑、任意 timestamps
  或复杂 metadata preservation。
  当前本地 QA 入口是 `tools/verify_document_properties.py` 和
  `tools/verify_document_properties_excel.ps1`，覆盖 in-memory / streaming 样例的
  XML、`openpyxl` 和 Excel COM 只读打开 smoke；Excel COM 不稳定暴露的属性以
  拆包 XML / `openpyxl` 结果为准。
- styles number formats + limited alignment + bold/italic/direct-color fonts + solid fills：基础。`StyleId` / `CellAlignment` / `HorizontalAlignment` /
  `VerticalAlignment` / `CellFont` / `CellFill` / `CellStyle` /
  `WorkbookWriter::add_style()` / `CellView::with_style()` 已支持 streaming-only
  new-workbook 自定义 number format、wrap-text + limited horizontal/vertical alignment、
  bold/italic/direct ARGB font color 和 solid foreground fill 样式注册及 cell style 引用；`xl/styles.xml`
  是 workbook-level 小型 XML part，styles relationship 位于 workbook `.rels`，
  不新增 worksheet `.rels`。当前结构测试覆盖 style id 去重、XML attribute escape、
  custom `numFmtId` 复用、`fontId` 复用、`fillId` 复用、`applyFont` / `<b/>` / `<i/>` /
  direct `<color rgb="..."/>`、
  `applyFill` / solid `<patternFill>` / `fgColor rgb`、`applyAlignment` /
  `wrapText` / `horizontal` / `vertical`、默认 `s="0"` 省略、
  显式 `StyleId{}` 把已设置样式的 cell view 清回默认输出、
  sharedStrings + styles 共存、styles workbook relationship 与 worksheet-local
  hyperlink/table relationships 共存且不偏移 worksheet `rId`、以及非法 foreign `StyleId`
  在污染 row state 前被拒绝；非法 `add_style()` 注册失败不会污染 workbook style registry；
  all-default optional alignment/font metadata 与其他有效 style 属性组合时会被忽略；
  带样式公式 cell 会保留 `s="N"`，同时写出 workbook
  `<calcPr fullCalcOnLoad="1"/>`，且不创建 `xl/calcChain.xml`。
  固定本地 QA 入口是
  `tools/verify_styles_number_formats.py` 和 `tools/verify_styles_excel.ps1`，分别做
  拆包 XML / `openpyxl` / `XlsxWriter` 参考检查和本机 Excel COM 只读可视化检查；
  Excel COM helper 会核对 `Font.Color`，但精确 OpenXML 语义仍以拆包 XML 和
  `openpyxl` 为准。
  这不是完整 styles、full font control、full fill/pattern control、border/full alignment、date cell type、conditional
  formatting、rich text、existing-file style preservation 或 Excel formatting parity。
- OPC edit plan：基础，是 P4.0 之后窄 Patch MVP 的内部主线。当前是 internal manifest、relationships、
  content types、part write-mode、`PartIndex`、`RelationshipGraph`、
  `ContentTypeRegistry`、内部 `EditPlan`、`DependencyAnalyzer`、
  `ReferencePolicy` 和 `PartRewritePlanner` 基础；当前 planner 只生成
  part-level 决策，覆盖 copy-original、目标 part rewrite、显式 registered-part
  removal audit、fullCalcOnLoad 与 calcChain 计划语义；当前
  `plan_worksheet_stream_rewrite()` 在 `CalcChainAction::Remove` 下会直接产出 stale
  calcChain removed-part audit，供 worksheet replacement 消费；它不做 relationship pruning
  或 target repair；`DependencyAnalyzer` 现在还能沿 worksheet `.rels`
  中已知 internal relationship target 做保守遍历，例如 worksheet → table 和
  worksheet → drawing → image/chart，把这些 part 纳入 dependency summary；当前回归
  还覆盖递归到 drawing-owned `.rels` 后的 external、URI-qualified、invalid 和
  unresolved relationship target 审计 note。它不会把 external hyperlink target 当成
  package part，但会记录 external target 审计 note；
  对带 query/fragment 的 internal relationship target，会记录 URI-qualified 审计 note，
  并在 base target 解析到已注册 package part 时保守纳入 dependency summary；
  以 `/` 开头的 absolute internal target 会按 package part path 做 normalization；
  percent-encoded internal relationship target 会先解码 `%XX` 后再做 part-name
  normalization，已注册的 decoded target 会纳入 dependency summary；
  malformed percent escape 或解码后非法的 target 仍走 invalid-target 审计路径；逃出
  package root 或解析到内部路径但当前 manifest 未注册的 internal relationship target，
  不会虚构 part，只记录 package structure review 审计 note；这些 relationship target
  审计 note 和结构化 `RelationshipTargetAudit` 会携带 owner part、relationship id、
  relationship type、原始 target，以及可用时的 normalized base target part，并会从
  `DependencyAnalysis` 传播到 worksheet `EditPlan` 和 existing-file
  `PackageEditor::edit_plan()`，方便 Patch 审计但不代表 target validation 或 repair；
  `EditPlan` 会按 owner/id/type/raw target/normalized target 对重复
  `RelationshipTargetAudit` 做 upsert，并对完全相同的 audit note 去重，避免重复
  worksheet rewrite 让 Patch 审计元数据无限累积；这只是 traceability 稳定性，不是
  relationship rewrite、repair 或 pruning；
  未知 relationship type 只要 target normalize 后命中已注册 internal part，也会被保守纳入
  dependency summary / copy-original reason；这只是保守依赖审计，不代表理解或编辑该
  custom relationship 语义；
  copy-original dependency reason 会携带 relationship id、relationship type 和 normalized target part path，
  对 relationship-derived dependency，内部 `PartDependency` 还保留 owner part、
  relationship id/type 和原始 target 字段，并会把这些字段同步到对应的
  copy-original `EditPlanEntry` 结构化审计 metadata；relationship target
  anomaly 还会保留在 `EditPlan::relationship_target_audits()` 里；workbook/sharedStrings/styles
  这类静态依赖只保留 reason 文本，不伪造 relationship metadata；
  workbook part 的 dependency reason 还会
  明示 calcPr / definedNames review，并会把这些 dependency reason 回写到
  `EditPlan` 的 copy-original entries 里；内部 `EditPlan` 另有 package-entry
  审计项，记录 `[Content_Types].xml`、`_rels/.rels`、workbook `.rels` 等 metadata entry
  rewrite、calcChain owner `.rels` omission，以及窄 worksheet replacement 下 preserved
  worksheet / drawing / calcChain owner 等 source-owned `.rels` 的 copy-original 审计。
  这些审计项已有内部结构化 kind：content types、package relationships 和
  source-owned relationships；只有 source-owned relationships 携带 owner part，
  且 kind 必须匹配 `[Content_Types].xml`、`_rels/.rels` 或 owner-derived `.rels`，
  这只是 Patch 审计上下文，不是完整 relationship/content-type mutator。
  当前另有内部 `PackageReader` ZIP + metadata ingestion
  基础，可读 stored/no-compression entries；`FASTXLSX_ENABLE_MINIZIP_NG=ON`
  构建还可读 DEFLATE entries。读取 entry 时会校验解压后 payload CRC，拒绝
  local header CRC/method/name/size mismatch、encrypted flags、data descriptor entries、Zip64 和损坏 metadata 或 payload bytes，也会拒绝 owner part 缺失的
  source-owned `.rels`，包括根级
  `_rels/foo.xml.rels` owner relationship entry，并从 `[Content_Types].xml` / `.rels`
  建立内部 `PartIndex` / `RelationshipGraph` 视图；冲突 content type default / override
  和同一 `.rels` owner 内重复的 relationship id 会在 metadata ingestion 阶段被拒绝，
  这只是 reader 校验，不是 content-type 或 relationship repair；当前 reader-only 回归还覆盖
  unknown extension owner `.rels` metadata ingestion / `RelationshipGraph` 挂回。
  内部 `PackageEditor` copy/replace
  基础可把未替换 entries 按原始 bytes 写入新 package、替换一个既有目标 part，
  已有 core/app docProps generated-small-XML 窄切片同步 package rels /
  content types，并已有 worksheet replacement 窄切片同步 calcChain remove / workbook
  fullCalcOnLoad / workbook rels / content types；组合回归还覆盖 docProps 生成与
  worksheet replacement 共存时的 relationship/content-type 合并、calcChain 删除、
  stale calcChain owner `.rels` omission、workbook metadata rewrite 和 unknown entry
  保留、malformed workbook metadata / workbook calc metadata、invalid replacement、
  metadata-entry replacement 和 invalid removal no-state-pollution（含 structured
  audit、removed audit 和 calc policy 快照），以及 exact/path-equivalent
  source-overwrite rejection 和 empty-output / missing-parent / non-directory-parent / existing-directory output rejection。`PackageEditor` 从 reader 导入
  manifest 时保留原始 `[Content_Types].xml` defaults/overrides 形态；普通 part
  replacement、docProps generated parts 和 worksheet replacement 会把 write-mode /
  dirty / generated / preserve-original 状态同步到内部 manifest，且 worksheet
  replacement 会把 workbook metadata rewrite 同步为 `LocalDomRewrite`；内部 `EditPlan`
  现在还记录 `[Content_Types].xml`、package `_rels/.rels`、workbook `.rels`
  package-entry rewrite、calcChain owner `.rels` omission，以及 preserved source-owned `.rels`
  存在时的 copy-original 审计（包括 ordinary owner-part replacement 的根级
  `_rels/foo.xml.rels`、worksheet/drawing/sharedStrings 关系、preserved calcChain 关系，以及
  workbook metadata rewrite 时被原样保留的 workbook `.rels`），供
  relationship/content-type side effect 审计。
  当前还有内部 `PackageEditor::remove_part()` 显式 registered-part removal 窄切片：
  只接受源 package 中已有的普通 part，输出时省略目标 part 和存在时的 source-owned
  owner `.rels`，把目标 part 记录为 removed-part audit，必要时把 `[Content_Types].xml`
  作为 local-DOM rewrite 更新以移除目标 override；它不会修剪其它 part 指向该目标的
  inbound relationships，也不是通用删除、relationship pruning、object deletion 或事务式编辑。
  removed-part audit 现在会同时保留结构化 inbound relationship metadata
  （owner entry、owner part、id、type、raw target、normalized target part）和可读
  reason，记录仍指向被删除 part 的 inbound package/source relationship 上下文，
  方便 Patch traceability，但不代表修复。
  若 removed-part inbound 扫描遇到 malformed percent relationship target，会记录
  EditPlan / planned output audit note 并继续保留该 `.rels` bytes，不让无关坏
  target 阻塞显式 part removal；planned output 也暴露 removed target omission
  与 copy-original metadata entries，且不新增结构化 relationship target audit；
  这仍不是 target repair 或 relationship validation。
  linked-object fixture 还覆盖显式移除 `xl/workbook.xml`：输出省略 workbook
  part 及其 source-owned workbook `.rels`，移除 workbook content type override，保留
  package `_rels/.rels` 中的 inbound officeDocument relationship，且不修剪 worksheet、
  drawing、table、sharedStrings、styles、VBA、calcChain 或 unknown extension 等
  downstream/source parts；这只是 no-pruning / preservation 审计证据，不是 workbook
  deletion、sheet catalog sync、relationship repair 或完整 workbook editing。
  linked-object fixture 还覆盖显式移除 `xl/worksheets/sheet1.xml`：输出省略
  worksheet part 及其 source-owned worksheet `.rels`，移除 worksheet content type
  override，保留 workbook `.rels` 中的 inbound worksheet relationship，且不修剪
  drawing、table、sharedStrings、styles、VBA、calcChain 或 unknown extension 等
  downstream/source parts；这只是 no-pruning / preservation 审计证据，不是 sheet
  delete、workbook sheet catalog sync、relationship repair 或完整 worksheet editing。
  同一 worksheet 路径还覆盖 ordinary replace-then-remove：后续 removal 清理 active
  worksheet replacement、记录 removed-part 和 worksheet owner `.rels` omission audit，
  输出省略 worksheet part 及其 owner `.rels`、移除 worksheet content type override，
  但保留 workbook `.rels` 中指向缺失 worksheet 的 inbound worksheet relationship，
  以及 drawing/chart/media/table/sharedStrings/styles/VBA/calcChain/unknown
  downstream/source parts；这不是 sheet delete、workbook sheet catalog sync、
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
  calcChain/unknown downstream/source parts；这不是 drawing mutation、object deletion、
  relationship/content type repair、orphan cleanup、事务式 undo 或 public API。
  linked-object fixture 还覆盖显式移除 `xl/media/image1.png`：输出省略 media entry，
  保留 PNG default content type 和 drawing `.rels` 中的 inbound image relationship，
  不凭空创建 media owner `.rels` omission；这只是 no-pruning / preservation 审计证据，
  不是 existing-workbook 图片编辑或关系修复。
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
  `[Content_Types].xml` 回到 source bytes / copy-original audit；同时覆盖
  sharedStrings-specific 反向顺序：显式移除 `xl/sharedStrings.xml` 后再 ordinary
  `replace_part()` 会恢复 sharedStrings active replacement、恢复 source-owned
  sharedStrings `.rels` copy-original audit、保留 workbook `.rels` inbound
  sharedStrings relationship，并让 `[Content_Types].xml` 回到 source bytes /
  copy-original audit；同时覆盖 styles-specific 反向顺序：显式移除 `xl/styles.xml`
  后再 ordinary `replace_part()` 会恢复 styles active replacement、保留 workbook
  `.rels` inbound styles relationship，不凭空创建 styles owner `.rels`，并让
  `[Content_Types].xml` 回到 source bytes / copy-original audit。这不是
  sharedStrings 索引迁移、字符串表重建、worksheet cell 引用同步、style id 迁移、
  样式合并、cell `s` 引用同步、relationship repair、content type repair、事务式
  undo、语义合并或 public API；也不是 drawing mutation 或 object deletion。
  普通 `replace_part()` 显式拒绝 `[Content_Types].xml`、package `_rels/.rels` 和
  source-owned `.rels` metadata entry 作为 ordinary part replacement target；这只是
  防止绕过窄 metadata helper / package-entry audit 的安全边界，不是完整
  relationship/content-type 编辑器。
  workbook metadata rewrite 的 reason
  会保留 calcPr fullCalcOnLoad 和 definedNames review 上下文，供 Patch 审计；当前
  worksheet replacement 测试确认移除 calcChain override 时不会把 PNG media default
  无故提升为 image part override。完整 existing-file editing、
  sharedStrings/styles/tables/drawings/defined names 联动、drawing/image/chart/table 编辑、复杂对象保真和
  production package rewrite 仍需更多 fixture 和 targeted rewrite 测试闭环；
  另有 no-op `save_as()` roundtrip 结构测试验证 linked-object fixture 中全部源
  entries 的顺序、stored entry method / CRC / uncompressed size 和 bytes 保持一致，
  且初始 `EditPlan` 只有 copy-original part entries、没有 metadata package-entry side
  effect；这只是未修改包的 copy baseline。
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
  worksheet relationship audit / content types rewrite / empty
  removed_package_entries / no invented owner `.rels` 快照、
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
  remove-then-replace 的 active cache definition local-DOM rewrite / owner
  `.rels` copy-original / content types copy-original / preserved workbook/worksheet/
  pivot table/cache records/unknown entries 快照、pivot cache definition
  replace-then-remove final-removal 的 omitted cache definition part / owner
  `.rels` / workbook 与 pivot table inbound pivotCacheDefinition relationship
  audit / content types rewrite / preserved workbook/worksheet/pivot table/cache
  records/unknown entries 快照、pivot cache records remove-then-replace 的 active
  records stream-rewrite / content types copy-original / preserved pivot table/cache
  definition 链 / no invented records owner `.rels` 快照、pivot cache records
  replace-then-remove final-removal 的 omitted records part / cache-definition inbound
  pivotCacheRecords relationship audit / content types rewrite / preserved
  cache definition owner `.rels` / no invented records owner `.rels` 快照、comments
  ordinary replacement 的 active comments part local-DOM rewrite / preserved content
  types / package relationships / workbook / workbook `.rels` / worksheet /
  worksheet `.rels` / unknown entry / no invented comments owner `.rels` 快照、comments
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
  或通用 metadata mutator。
  malformed unrelated relationship target 的 explicit removal 回归现在验证
  EditPlan / planned output notes-only audit、omitted target、copy-original metadata
  entries、无结构化 relationship target / worksheet reference audit、无 package-entry
  rewrite/omission，以及 calc policy 保持不变；这不是 relationship repair。
  ordinary single-part replacement 回归还验证目标 entry 原位重写时，其它源
  entries 保持顺序、stored entry method / CRC / uncompressed size 和 bytes 一致；
  这只是窄 part-level rewrite copy-original 证据。
  linked-object fixture 上的 ordinary workbook replacement 回归还验证只重写
  `xl/workbook.xml` 时，workbook `.rels` 被记录为 copy-original package-entry audit，
  worksheet、drawing、media、sharedStrings、styles、VBA、calcChain 和 unknown extension
  等其它 source entries 仍按上述 copy-original 基线保留。
  同一路径还覆盖 ordinary workbook replace-then-remove：后续 removal 清理 active
  workbook replacement、记录 removed-part 和 workbook owner `.rels` omission audit，
  输出省略 workbook part 及其 owner `.rels`、移除 workbook content type override，
  但保留 package `_rels/.rels` 中指向缺失 workbook 的 officeDocument relationship
  以及 worksheet/drawing/table/sharedStrings/styles/VBA/calcChain/unknown downstream
  parts；这不是 workbook deletion semantics、sheet catalog sync、relationship/content
  type repair、orphan cleanup、事务式 undo 或 public API。
  linked-object fixture 上的 ordinary drawing replacement 回归还验证只重写
  `xl/drawings/drawing1.xml` 时，drawing `.rels` 被记录为 copy-original package-entry
  audit，chart、media 和 unknown extension 等其它 source entries 仍按上述
  copy-original 基线保留；这不是 drawing mutation、图片编辑或图表编辑。
  linked-object fixture 上的 ordinary unknown extension replacement 回归还验证只重写
  `custom/opaque-extension.bin` 时，其 owner `.rels` 被记录为 copy-original
  package-entry audit 并原样保留，workbook、worksheet、drawing、chart 和 media
  entries 仍按上述 copy-original 基线保留；这不是 unknown extension 语义编辑、
  custom relationship repair 或 public API。
  对同一 unknown extension 的 repeated ordinary replacement 回归还验证最终 bytes、
  manifest write-mode、edit-plan reason 和 owner `.rels` audit 会 upsert 到最后一次
  替换状态，owner `.rels` 仍按 copy-original 保留，且不会产生 removed-part 或
  removed package-entry audit；这不是事务式编辑或 unknown extension 语义合并。
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
  linked-object fixture 上的 ordinary media replacement 回归还验证只重写
  `xl/media/image1.png` 时，drawing `.rels`、PNG default content type、workbook、
  worksheet、drawing、chart 和 unknown extension entries 仍按上述 copy-original
  基线保留；这不是图片解码、drawing mutation 或 existing-workbook image editing。
  同一路径还覆盖 default-typed media 先显式移除再 ordinary replacement 的反向顺序：
  后续 replacement 会恢复 active media part、清理 stale removed-part audit、保留
  PNG default content type 且不把 `xl/media/image1.png` 提升成 override、保留
  inbound drawing `.rels`，并且不凭空创建 media owner `.rels`；这不是事务式
  undo、图片语义合并、relationship repair、content type repair 或完整 image
  preservation。
  同一路径还覆盖 media 先 ordinary replacement 再显式移除的顺序：后续 removal
  会清理 active media replacement、记录 removed-part audit 和 inbound drawing
  relationship metadata，输出省略 `xl/media/image1.png`，保留 PNG default content
  type 且不把 media 提升成 override，保留 inbound drawing `.rels`，并且不凭空创建
  media owner `.rels`；内部 output-plan 快照还暴露 omitted media part、drawing
  inbound audit、content types / drawing `.rels` copy-original 和无 media owner `.rels`
  条目；这同样不是事务式 undo、图片语义合并、relationship pruning/repair、
  content type repair、existing-workbook image editing 或完整 image preservation。
  linked-object fixture 上的 ordinary table replacement 回归还验证只重写
  `xl/tables/table1.xml` 时，worksheet `.rels`、table content type override、
  workbook、worksheet、drawing、chart、media 和 unknown extension entries 仍按上述
  copy-original 基线保留；这不是 table resize、calculated columns、totals generation
  或 existing-workbook table editing。
  同一路径还覆盖 table 先显式移除再 ordinary replacement 的反向顺序：后续
  replacement 会恢复 active table part、清理 stale removed-part audit、让
  `[Content_Types].xml` 回到 source/copy-original audit、保留 worksheet `.rels`
  inbound table relationship，且不凭空创建 table owner `.rels`；这不是 table resize、
  calculated columns、totals generation、事务式 undo、relationship repair、
  content type repair 或 existing-workbook table editing。
  同一路径还覆盖 table 先 ordinary replacement 再显式移除的顺序：后续
  removal 会清理 active table replacement、记录 removed-part audit 和 inbound
  worksheet relationship metadata、输出省略 table part、移除 table content type
  override、保留 worksheet `.rels` inbound table relationship，且不凭空创建
  table owner `.rels`；内部 output-plan 快照还暴露 omitted table part、worksheet
  inbound audit、content types local-DOM rewrite 和无 table owner `.rels` 条目；这不是
  table delete semantics、table resize、calculated columns、totals generation、
  事务式 undo、relationship pruning/repair、content type repair 或
  existing-workbook table editing。
  linked-object fixture 上的 ordinary sharedStrings replacement 回归还验证只重写
  `xl/sharedStrings.xml` 时，workbook `.rels`、sharedStrings owner `.rels`、
  sharedStrings content type override、styles、table、media、VBA 和 unknown
  extension entries 仍按上述 copy-original 基线保留；这不是 sharedStrings
  索引迁移、字符串表重建、worksheet cell 引用同步或 existing-file sharedStrings
  语义编辑。
  同一路径还覆盖 sharedStrings 先 ordinary replacement 再显式移除的顺序：后续
  removal 会清理 active sharedStrings replacement、记录 removed-part audit、输出省略
  `xl/sharedStrings.xml` 及其 source-owned owner `.rels`、移除 sharedStrings
  content type override、保留 workbook `.rels` 中的 inbound sharedStrings relationship；
  它不会修剪 worksheet `t="s"` 引用或重建字符串表。这不是 sharedStrings 索引迁移、
  字符串表重建、worksheet cell 引用同步、事务式 undo、relationship pruning/repair、
  content type repair、existing-file sharedStrings 语义编辑或 public API。
  linked-object fixture 上的 ordinary styles replacement 回归还验证只重写
  `xl/styles.xml` 时，workbook `.rels`、styles content type override、
  sharedStrings、sharedStrings owner `.rels`、table、media、VBA 和 unknown
  extension entries 仍按上述 copy-original 基线保留，且不会凭空创建
  `xl/_rels/styles.xml.rels`；这不是 style id 迁移、样式合并、cell `s`
  引用同步、existing-file style preservation 或完整样式编辑。
  同一路径还覆盖 styles 先 ordinary replacement 再显式移除的顺序：后续 removal
  会清理 active styles replacement、记录 removed-part audit、输出省略 `xl/styles.xml`、
  移除 styles content type override、保留 workbook `.rels` 中的 inbound styles
  relationship，且不凭空创建 `xl/_rels/styles.xml.rels`；它不会迁移 style id
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
  linked-object fixture 上的 ordinary chart replacement 回归还验证只重写
  `xl/charts/chart1.xml` 时，drawing `.rels` 中的 chart / URI-qualified chart
  relationships、chart content type override、media、table、sharedStrings、
  styles、VBA 和 unknown extension entries 仍按上述 copy-original 基线保留，
  且不会凭空创建 chart owner `.rels`；这不是 chart reference migration、
  series/cache update、drawing mutation、existing-workbook chart editing 或完整图表支持。
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
  linked-object fixture 上的 ordinary VBA project replacement 回归还验证只重写
  `xl/vbaProject.bin` 时，workbook `.rels` 和 VBA content type override 仍按上述
  copy-original 基线保留，且不会凭空创建 `xl/_rels/vbaProject.bin.rels`；worksheet、
  drawing、chart、media、table、sharedStrings、styles、calcChain 和 unknown extension
  entries 也保持 copy-original。它不是 macro generation、VBA 语义编辑、
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
  VBA project part、移除 VBA content type override、保留 workbook `.rels`
  中的 inbound VBA relationship，且不凭空创建 `xl/_rels/vbaProject.bin.rels`；
  这不是 macro generation、VBA 语义编辑、签名保真、事务式 undo、workbook
  relationship repair、content type repair 或完整宏支持。
  内部 `planned_output()` 快照还覆盖该 final-removal 状态：暴露 omitted VBA project
  part、removed_parts target/reason/inbound audit、workbook inbound VBA relationship
  metadata、content types rewrite、empty removed_package_entries，且不凭空创建
  `xl/_rels/vbaProject.bin.rels`；这只是 Patch audit，不是 public output planner、
  macro editing API、relationship repair、content type repair 或 transactional undo。
  linked-object fixture 上的 ordinary VML drawing replacement 回归还验证只重写
  `xl/drawings/vmlDrawing1.vml` 时，worksheet `.rels` 中的 URI-qualified
  `vmlDrawing` relationship、VML content type override、workbook、worksheet、
  drawing、chart、media、table、sharedStrings、styles、VBA、calcChain 和 unknown
  extension entries 仍按上述 copy-original 基线保留，且不会凭空创建
  `xl/drawings/_rels/vmlDrawing1.vml.rels`；这不是 VML shape editing、legacy drawing
  mutation、relationship repair 或完整 VML/drawing 支持。
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
  repair、content type repair 或完整 VML/drawing 支持。
  同一路径还覆盖 VML drawing 先 ordinary replacement 再显式移除的顺序：后续
  removal 会清理 active VML drawing replacement、记录 removed-part audit、输出省略
  VML drawing part、移除 VML content type override、保留 worksheet `.rels`
  中的 URI-qualified inbound `vmlDrawing` relationship，且不凭空创建 VML owner
  `.rels`；这不是 VML shape editing、legacy drawing mutation、事务式 undo、
  relationship pruning/repair、content type repair 或完整 VML/drawing 支持。
  内部 `planned_output()` 快照还覆盖该 final-removal 状态：暴露 omitted VML
  drawing part、removed_parts target/reason/inbound audit、URI-qualified worksheet
  inbound relationship metadata、content types rewrite、empty removed_package_entries，
  且不凭空创建 VML owner `.rels`；这仍只是 Patch audit，不是 public output
  planner、drawing editing API、relationship repair 或 transactional undo。
  linked-object fixture 上的 ordinary percent-decoded drawing replacement 回归还验证只重写
  `xl/drawings/drawing space.xml` 时，worksheet `.rels` 中原始
  `../drawings/drawing%20space.xml` relationship、drawing content type override、workbook、
  worksheet、drawing、chart、media、table、VML、sharedStrings、styles、VBA、
  calcChain 和 unknown extension entries 仍按上述 copy-original 基线保留，且不会凭空创建
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
  audit、输出省略 decoded drawing part、移除 drawing content type override、
  保留 worksheet `.rels` 中原始 encoded inbound
  `../drawings/drawing%20space.xml` relationship，且不凭空创建
  `xl/drawings/_rels/drawing space.xml.rels`；内部 `planned_output()` 也覆盖该
  final-removal 状态，暴露 omitted decoded drawing part、removed_parts
  target/reason/inbound audit、encoded inbound worksheet relationship metadata、
  content types rewrite、empty removed_package_entries 和 no invented owner `.rels`；
  这不是 percent-encoded target repair、
  relationship rewrite、drawing mutation、事务式 undo、relationship pruning/repair、
  content type repair 或完整 drawing 支持。
  当前仅有 worksheet replacement 下的 worksheet `.rels`、drawing/media/chart/table/
  sharedStrings/styles/VBA/unknown part byte-preservation 结构证明，包括替换后的
  worksheet XML 省略原 `<drawing>` / `<tableParts>` 引用时仍原样保留源 `.rels`
  和 linked object parts；另有 workbook definedNames 保留回归。
  不要把 `PackageEditor` / Patch API 做成 streaming writer 的事后补丁。
- Phase 5 worksheet metadata：基础。`WorksheetWriter::add_data_validation()`
  已有 streaming-only 新建文件切片，会在 worksheet XML 写出
  `<dataValidations>`；它不解析公式、不校验单元格值、不检查重叠、不编辑已有
  XLSX，也不新增 relationships 或 content types。当前还支持 worksheet-local
  prompt/error metadata：`DataValidationRule` 的 `show_input_message`、
  `show_error_message`、`hide_dropdown_arrow`、`error_style`、`prompt_title`、
  `prompt`、`error_title` 和 `error` 会写成 `<dataValidation>` attributes；空字符串
  省略，false 布尔值省略，不生成 `.rels`、`styles.xml` 或 content type 副作用，
  也不代表完整 Excel data validation UI。`hide_dropdown_arrow` 只对 list validation
  有效，写出 OpenXML 反向命名的 `showDropDown="1"`，表示隐藏 in-cell dropdown arrow。
  当前还支持一条 data validation 规则对应多个合法 `CellRange`：
  多区域 overload 会复制 range 列表，并写成同一个 `<dataValidation>` 的空格分隔
  `sqref`；`<dataValidations count>` 仍按规则数计算，不按区域数计算。它不排序、
  合并、去重或检查重叠区域。`WorksheetWriter::add_external_hyperlink()`
  已有 external-only 新建文件切片，会写 worksheet `<hyperlinks>` 和对应的
  `xl/worksheets/_rels/sheetN.xml.rels`；`WorksheetWriter::add_internal_hyperlink()`
  已有 internal workbook location 新建文件切片，只在 worksheet `<hyperlinks>` 写
  `location`，不创建 worksheet `.rels`、workbook relationships 或 content type overrides。
  hyperlink API 不写单元格文本、不创建 hyperlink 样式、不校验 URL 可达性或 internal
  target 是否存在，不支持已有文件编辑或完整 hyperlink 行为。`HyperlinkOptions`
  已有基础 `display` / `tooltip` 属性切片，只序列化 worksheet `<hyperlink>` 属性；
  它不写单元格文本、不创建 hyperlink 样式或 `styles.xml`。
  `WorksheetWriter::add_table()` 已有 streaming-only 新建文件切片，会写
  worksheet `<tableParts>`、`xl/tables/tableN.xml`、worksheet `.rels` 和 table
  content type override；`TableOptions::show_totals_row` 只声明 caller-supplied
  totals row 可见，`column_totals_functions` 只把用户传入的列级
  `totalsRowFunction` metadata 写入 table XML，`column_totals_labels` 只把用户传入的
  列级 `totalsRowLabel` metadata 写入 table XML。它不读取已写 header 行、不推断列名，
  会拒绝同一 worksheet 内 table range 重叠，但不检查与 data validations、images、
  merged ranges 或 autoFilter 的冲突；它不计算 totals、不生成公式文本、totals row
  单元格文本、样式或 `xl/styles.xml`，
  不支持 calculated columns、table resize、已有文件编辑或完整 Excel table UI。
  `WorksheetWriter::add_conditional_color_scale()` 已有 streaming-only 新建文件
  two-/three-color color scale 切片，会把 `TwoColorScaleRule` / `ThreeColorScaleRule` 写成 worksheet-local
  `<conditionalFormatting><cfRule type="colorScale">` XML；`WorksheetWriter::add_conditional_data_bar()`
  已有 streaming-only 新建文件 basic data bar 切片，会把 `DataBarRule` 写成 worksheet-local
  `<conditionalFormatting><cfRule type="dataBar">` XML；`DataBarRule::show_value=false`
  只写 `<dataBar showValue="0">` 来隐藏单元格数值显示；`WorksheetWriter::add_conditional_icon_set()`
  已有 streaming-only 新建文件 basic 3Arrows icon set 切片，会把 `IconSetRule` 写成
  worksheet-local `<conditionalFormatting><cfRule type="iconSet">` XML。`ArgbColor` 序列化为
  8 位大写 ARGB，`priority` 按同一 worksheet 内调用顺序从 1 开始分配，
  多区域 overload 会把 `CellRange` 列表复制进 writer state 并写成空格分隔
  `sqref`。basic icon set 当前只支持内建 `3Arrows`、三枚有限且严格递增的
  `Number` / `Percent` / `Percentile` 阈值、可选 `showValue="0"` 和 `reverse="1"`。
  当前正向 QA 已覆盖默认 percent 阈值、numeric priority 样例、percentile
  `10/50/90` 阈值样例和 multi-range `sqref`。
  该切片不生成 `styles.xml`、`dxfs`、worksheet `.rels`、workbook
  relationships、content type entries、cell text 或 `<calcPr>`；不支持 advanced data bar
  negative bar / axis / border / gradient / `extLst`、advanced/custom icon sets、
  cellIs/formula/top/bottom/duplicate/unique
  等完整 conditional formatting
  规则，不解析公式、不计算单元格值、不排序/合并/去重 ranges、不检查重叠，也不支持
  existing-file editing 或完整 Excel UI。
  `WorksheetWriter::add_image()` 已有默认 `stb` streaming-only 新建文件切片，
  会写 media/drawing parts、drawing `.rels`、worksheet `.rels`、worksheet
  `<drawing>` 和 content type entries；它不进入 row/cell 热路径，不持有完整
  worksheet cell matrix，不支持 existing-file editing、drawing mutation、裁剪、旋转、
  压缩或格式转换。`ImageOptions` 只把 `from_offset` / `to_offset` EMU 值、
  `edit_as` 轻量枚举、name/description 字符串和可选 external picture hyperlink
  字符串复制进 writer state，并在 drawing XML
  写 two-cell marker `xdr:colOff` / `xdr:rowOff`、`xdr:twoCellAnchor editAs` 和
  `xdr:cNvPr` `name` / `descr` attributes；非空
  `external_hyperlink_url` 还会写 `xdr:cNvPr/a:hlinkClick` 和 drawing-local
  external hyperlink relationship。它不代表 worksheet cell hyperlink、完整 alt
  text/accessibility UI、图片文件 metadata、existing-workbook 图片编辑或图片保真。

## 先读哪些文件

- 项目定位：`README.md`、`docs/PROJECT_POSITIONING.md`
- 架构与数据流：`docs/ARCHITECTURE.md`、`docs/EDITING_MODEL.md`
- Patch 保留能力回归明细：`docs/PATCH_PRESERVATION_COVERAGE.md`
- 依赖与环境：`docs/DEPENDENCIES.md`、`docs/DEVELOPMENT_ENVIRONMENT.md`
- 性能目标与路线图：`docs/PERFORMANCE_TARGETS.md`、`docs/ROADMAP.md`
- 测试流程：`docs/TESTING_WORKFLOW.md`
- API 设计和文档注释：`docs/API_DESIGN_AND_DOCUMENTATION.md`
- 与参考项目的边界：`docs/TECHNICAL_COMPARISON.md`
- 构建和测试骨架：`CMakeLists.txt`、`tests/CMakeLists.txt`

## 核心架构约束

- 新建 XLSX 和大数据写入路径必须使用 XML streaming。
- 大型 `worksheet.xml`、大型 `sharedStrings.xml`、批量导出和大型模板填充路径禁止 DOM。
- 小型 XML part 可以使用局部 DOM，例如 `workbook.xml`、关系文件、
  `[Content_Types].xml`、`docProps/*.xml`、较小的 `styles.xml`，
  以及规划中的小型 drawing/comments/table part；这只是 DOM 边界，不代表
  当前完整支持图片、VBA 或 table 编辑。
- 编辑已有 XLSX 时优先 part-level rewrite：未修改 part 原样复制，
  修改 part 才重新生成。
- 未知 part 默认保留，避免破坏图表、图片、宏和未知扩展。
- Patch / existing-file editing 规划应先生成或更新 EditPlan，明确哪些 part
  copy-original、stream-rewrite、local-DOM-rewrite 或 removed，并说明 sharedStrings、styles、
  worksheet `.rels`、tables、drawings、defined names、calcChain 和 workbook calc metadata
  的联动策略。
- In-memory API 是小文件随机编辑路径，可以规划 `get_cell()` / `set_cell()` 等便利
  能力，但不得成为大数据默认路径。
- 大数据 API 必须面向 row iterator 或 chunk writer，不要为了 API 方便
  持有完整 worksheet cell matrix。
- `FastXmlWriter`、`CellEncoder`、`RowStreamWriter` 是文档中的性能热路径；
  不要在 cell XML 热路径上直接依赖通用 XML serializer。
- public API 必须向性能主线靠齐。不能为了 API 易用性让大型 worksheet
  进入 DOM、完整 cell matrix 或 cell map。
- 编辑能力不能被降级为 streaming writer 的补丁；PackageEditor、In-memory editor 和
  EditPlan 应作为核心架构模块推进。
- public API 应写文档注释，说明模式、内存行为、随机访问限制和性能注意事项。

## 文件职责边界与模块化约束

- 新增功能、测试或 QA helper 时，优先保持文件职责清晰，避免把所有实现继续堆进
  少数几个“大文件”。
- `src/streaming_writer.cpp` 应保持为 streaming worksheet 写入流程协调层和热路径入口；
  不应无限承载 conditional formatting、data validations、hyperlinks、tables、
  images/drawings、styles、sharedStrings 等所有细节实现。
- `tests/test_streaming_writer.cpp` 应保留 streaming writer 主流程、关键边界和跨功能
  集成测试；独立 feature 的大量结构测试和负例应优先拆到 feature-specific 测试文件。
- 当一个功能已经有独立 public API、独立 XML 序列化、独立状态结构、独立 QA helper
  或大量边界用例时，应考虑拆到独立 `.cpp`、detail helper 或独立测试文件。
- public API 可以继续集中在现有 public headers，保持用户入口稳定；拆分主要针对内部
  实现、XML 生成、校验、状态转换和测试组织。
- 集成测试只保留真正跨模块的行为，例如 suffix 顺序、relationship id、content type
  side effects、package side effects 和多对象共存。
- QA helper 应保持 feature-specific；不要把无关功能混成一个巨型脚本。
- 新增 `.cpp` 或测试文件时，必须同步更新 `CMakeLists.txt` / `tests/CMakeLists.txt`。
- 拆分不能引入完整 worksheet cell matrix、DOM 热路径或无关重构。
- 不为很小的临时代码过度拆分；只有功能边界明确、代码继续增长或测试开始挤占主测试
  文件时才拆。

## 依赖策略

通用底层能力使用成熟库：

- `minizip-ng`：ZIP package 处理。
- `zlib-ng / zlib`：DEFLATE 压缩。
- `Expat`：大型 XML event parser。
- `pugixml`：小型 XML 局部 DOM 编辑。
- `stb`：Phase 5 图片读取/插入中的图片解码、尺寸和像素读取；当前作为默认
  vcpkg manifest 依赖接入，用于 `read_image_info()` 图片元数据 helper 和
  `WorksheetWriter::add_image()` 基础切片。
- `Catch2`：单元测试。
- `Google Benchmark`：性能基准。

FastXLSX 自己实现 XLSX 语义层：OPC part 索引、relationships、row/cell 编码、
`sharedStrings` / `inlineStr` 策略、styles registry、part-level rewrite。

真正接入依赖时，使用 `vcpkg` manifest mode。CMake 侧优先 `find_package`。
默认不要用 `FetchContent` 拉取核心依赖，也不要把第三方源码复制进 `src`
或 `include`。

当前已接入的第三方依赖是 opt-in 的 `minizip-ng[core,zlib]` package writer
backend：`find_package(minizip-ng CONFIG REQUIRED)`、`MINIZIP::minizip-ng`、
license 为 Zlib。它需要 `planned-runtime` feature；其中 `zlib-ng`、`expat`
和 `pugixml` 已能随 feature clean install，但当前源码尚未使用它们。

`OpenXLSX`、`xlnt`、`libxlsxwriter`、`QXlsx` 只能作为参考库、经验来源或
benchmark 对象，不作为 FastXLSX 的运行时底座。

项目使用 MIT License，见 `LICENSE`。

## 构建和测试命令

本项目以 Visual Studio 2026 / MSVC 2026 为主开发环境。推荐在 VS2026
Developer Command Prompt 中使用 preset：

```powershell
cmake --list-presets
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

验证 opt-in minizip-ng package writer backend 时，确保 `VCPKG_ROOT` 指向目标
vcpkg 根目录后运行：

```powershell
cmake --preset windows-nmake-release-minizip
cmake --build --preset windows-nmake-release-minizip
ctest --preset windows-nmake-release-minizip
```

手工 benchmark 必须显式 opt-in，不进入默认 CTest：

```powershell
cmake --preset windows-nmake-release-benchmark
cmake --build --preset windows-nmake-release-benchmark --target fastxlsx_bench_streaming_writer
```

需要验证 minizip backend 的 benchmark 时，使用
`windows-nmake-release-benchmark-minizip`。benchmark 结果必须记录数据规模、
字符串策略、ZIP backend/压缩设置、总耗时、峰值内存、输出大小和办公软件打开结果；
不要把 benchmark preset 当成默认质量门禁。
不传 `--output` / `--result` 时，当前 benchmark 工具默认写到 benchmark target
的 binary dir；手工工具限制 `--sheets <= 1024`，这只是 benchmark 输入护栏。
当前 benchmark JSON schema version 为 `4`，会记录 `string_pattern`、
`string_cells`、`unique_string_values`、`duplicate_string_cells`、
`string_dedup_ratio`、`package_entry_source_mode="worksheet-file-backed-chunked"`、
`temporary_worksheet_part_footprint="worksheet-body-file-bytes"` 和数值型
`temporary_worksheet_part_footprint_bytes`。字符串分布字段描述 benchmark 输入，
适用于 inline/shared 两种策略；footprint 值来自 benchmark-only instrumentation，
只累计 worksheet body row XML 写入字节数，不包含 worksheet header/footer、
sharedStrings 临时文件、小型 XML parts、media 文件、ZIP/backend 缓冲、
package assembly 峰值内存或 OS 文件系统开销；不能据此宣称完整低内存或大文件性能。
当前 `tools/run_benchmark_matrix.py` 是 opt-in 本地矩阵 runner，只包装一个已构建的
`fastxlsx_bench_streaming_writer` exe 并聚合 schema-v4 JSON；stored/minizip 要分别传入
各自 preset 的 exe 和输出目录。`--self-test` 只检查 runner 内部 parser / distribution /
report 假设，不调用 benchmark exe、不生成 `.xlsx` 或 JSON，不得写成 benchmark 或 Office
验证。`tools/verify_benchmark_matrix_excel.ps1` 可本机只读打开
report 中的部分 workbook，但不会改写 benchmark JSON 的 `office_open="not_run"`。

如果其他机器上的 Visual Studio 2026 对应新的 CMake 生成器名称，用下面命令确认：

```powershell
cmake --help
```

不要把旧 `build/CMakeCache.txt` 当成推荐构建配置。它曾显示为
`NMake Makefiles`，而开发文档已说明：未显式指定生成器时可能误选 NMake，
并在没有 `nmake` 的普通终端环境中失败。

后台运行普通单元测试时，核心测试超时时间设为 60s。当前 60s 边界来自
`CMakePresets.json` 的 CTest preset 和 `tests/CMakeLists.txt` 的测试属性。
如果手写 `-B build-nmake` 目录排障，必须显式给 `ctest --test-dir ...`
加 `--timeout 60`。大型 benchmark 不要混入默认单元测试。
当前 GitHub Actions workflow 使用 VS2026 runner。默认 job 跑
`windows-nmake-release`；独立 vcpkg matrix job 跑 `windows-nmake-release-minizip`。
Excel COM、openpyxl / XlsxWriter 参考 QA 和
benchmark 仍是本机/手工验证，不作为 CI 强依赖。

## 常见开发路径

- 架构定位、模块边界、当前实现状态：使用 `.agents/skills/fastxlsx-project-navigation`。
- Phase 1 最小可写 XLSX：使用 `.agents/skills/fastxlsx-minimal-writer`。
- CMake、本地构建、测试入口、target 形态：使用 `.agents/skills/fastxlsx-cmake-build`。
- 第三方依赖、vcpkg、license、参考库边界：使用 `.agents/skills/fastxlsx-dependency-policy`。
- worksheet writer/reader/rewriter、大数据路径：使用 `.agents/skills/fastxlsx-streaming-worksheet`。
- 已有 XLSX 编辑、OPC part rewrite、未知 part 保留：使用 `.agents/skills/fastxlsx-opc-editing`。
- 测试、benchmark、兼容性验证、质量排障：使用 `.agents/skills/fastxlsx-test-quality`。
- data validations、hyperlinks 等 worksheet metadata / Phase 5 早期切片：
  使用 `.agents/skills/fastxlsx-worksheet-metadata-features`。
- conditional formatting、color scale、basic data bar、basic 3Arrows icon set、priority 和本地 QA：
  使用 `.agents/skills/fastxlsx-conditional-formatting-features`。
- 图片读取/插入、media part、drawing rels 和 `stb` 解码边界：
  使用 `.agents/skills/fastxlsx-image-media-features`。

## 质量和兼容性检查

- Phase 1 输出不能只看编译通过；生成的 `.xlsx` 应校验 OpenXML 基本结构，
  并在可用时验证 Excel / WPS / LibreOffice 可打开。
- 本机有 Excel 时，关键 `.xlsx` 样例必须用 Excel 打开做可视化验证。
- 当前 `fastxlsx.unit` 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-phase1-minimal.xlsx`；本机已用
  Excel 可视化验证并核对 `Sheet1`、`A1`、`B1`、`C1`、`A2`、`B2`。
  本地旧 `build-nmake/tests/*.xlsx` 可能存在，但除非确认由当前源码重新生成，
  否则应视为过期 artifact。
- 当前 `fastxlsx.streaming` data validations 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-data-validations.xlsx`；
  本机已用 Excel 打开验证 `Validation` sheet、`A2:G10` validation 类型、
  operator 和公式返回值。Excel COM 会把 list 公式外层引号去掉，并给函数公式
  返回值加 `=` 前缀；结构测试仍以拆包后的 worksheet XML 语义为准。
  结构测试还覆盖 data validations 与 external hyperlinks / tables 共存时的
  worksheet suffix 顺序，确认 `<dataValidations>` 写在 `<hyperlinks>` 和
  `<tableParts>` 之前，且 data validation 不消耗 worksheet-local relationship id。
  结构测试还覆盖 validation-only worksheet 不声明 `xmlns:r`，以及 `formula2` 中
  `&`、`<`、`>` 的 XML text 转义。
  本机 Excel COM 已只读打开
  `build/windows-nmake-release/tests/fastxlsx-streaming-data-validation-formula2-escape.xlsx`，
  确认 `A2` validation 的 formula2 返回为 `=LEN(A2&"<max>")`。
  本机 Excel COM 已只读打开
  `build/windows-nmake-release/tests/fastxlsx-streaming-validation-relationship-metadata.xlsx`，
  确认 2 个 hyperlink、1 个 table，且 `A2` 有 validation。
  当前 data validation prompt/error metadata 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-data-validation-prompts.xlsx`。
  结构测试确认 `showInputMessage`、`showErrorMessage`、`errorStyle`、
  `showDropDown`、`promptTitle`、`prompt`、`errorTitle` 和 `error` 只写
  `<dataValidation>` attributes，覆盖 attribute escape、空字符串省略、false 布尔值
  省略、`stop` / `warning` / `information` 三种 error style，并确认不生成 worksheet
  `.rels`、`xl/metadata.xml`、`xl/styles.xml`、workbook relationships、content type
  side effects 或 `<calcPr>`。本机 `py tools\verify_data_validation_prompts.py`
  已验证 FastXLSX package XML、`openpyxl 3.1.2` 读取结果和 `showDropDown` 语义，
  并创建 `openpyxl` / `XlsxWriter 3.2.0` 参考 workbook；本机 Excel COM 已只读打开
  该样例并核对 `ValidationPrompt!A2:D2` 的 validation prompt/error 属性和
  `Validation.InCellDropdown = False` 的隐藏下拉箭头语义。Excel COM 会把 custom
  validation 公式 `LEN(D2)>0` 返回为 `=LEN(D2)>0`；公式和属性结构语义仍以拆包后的
  worksheet XML 为准。
  当前 data validation multi-range sqref 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-data-validation-multi-range.xlsx`。
  结构测试确认单条规则写出 `sqref="A2:A10 C2:C10 E2:E10"`、`count="1"`、
  `allowBlank="1"`、无 worksheet `.rels`、无 `xl/metadata.xml`、无 `xl/styles.xml`、
  无 content type side effects 或 `<calcPr>`；空 range list 和 multi-range 内非法
  range 会被拒绝。`tools/verify_data_validation_prompts.py` 已用 `--multi-range-input`
  检查该样例的 package XML、`openpyxl` 多区域读取结果，并创建 `openpyxl` 参考
  workbook；Excel COM 已只读打开该样例，确认 `ValidationRanges` 中 A/C/E 三段
  validation areas 为 `A2:A10`、`C2:C10`、`E2:E10`。
- 当前 `fastxlsx.streaming` external hyperlinks 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-external-hyperlinks.xlsx`；
  本机已用 Excel 打开验证 `Links`、`MoreLinks` 和 `Plain` sheet 的
  `Hyperlinks` 集合、Address 和 TextToDisplay，确认链接不替代单元格文本。
  结构测试还覆盖同一 worksheet 内多个 external hyperlink 的 owner-local
  `rId1` / `rId2` 分配，并确认不同 worksheet 的 relationship id 各自从 owner
  内局部计数，不污染 workbook relationships。
- 当前 `fastxlsx.streaming` internal hyperlinks 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-internal-hyperlinks.xlsx`；
  本机 Excel COM 已只读打开验证 `Internal` sheet 有 2 个 internal hyperlinks、
  `Mixed` sheet 有 2 个 external hyperlinks 和 1 个 internal hyperlink、`Plain` sheet
  为 0 个 hyperlinks。Excel COM 报告 internal links 的 `Address` 为 null、`SubAddress`
  分别为 `'Target & <Sheet>'!A1`、`'Target & <Sheet>'!B2:"quoted"` 和
  `'Target & <Sheet>'!A1`，external links 的 `Address` 为对应 URL，且单元格文本未被替换。
  本机 `openpyxl` 3.1.2 已读取 `cell.hyperlink.location` / `target` 语义，并用
  openpyxl 参考 workbook 拆包对比确认 internal-only worksheet 不生成 `.rels`、
  不声明 `xmlns:r`、不写 `r:id`；mixed sheet 的 `.rels` 只包含 external targets。
  结构测试还覆盖 internal hyperlink 与 table 共存时 table 仍使用 worksheet-local
  `rId1`，internal hyperlink 不消耗 relationship id。
- 当前 `fastxlsx.streaming` hyperlink display/tooltip 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-hyperlink-display-tooltips.xlsx`。
  `HyperlinkOptions` 会把非空 `display` / `tooltip` 只写成 worksheet `<hyperlink>`
  attributes；空字符串省略。结构测试覆盖 external/internal、both/display-only/
  tooltip-only、XML attribute escape、`.rels` 不含 display/tooltip、cell text 未被替换、
  不新增 hyperlink content type override 且不生成 `xl/styles.xml`。
  本机 `openpyxl` 3.1.2 已读取 external/internal `display` 和 `tooltip` 字段，并确认
  `.rels` / content types / styles 无副作用；本机 Excel COM 已只读打开验证 hyperlink
  counts、external `Address`、internal `SubAddress` 和 `ScreenTip`。Excel COM 的
  `TextToDisplay` 仍回退为单元格文本；不要把 `display` 属性写成会替换 cell text
  或生成样式的能力。
- 当前 `fastxlsx.streaming` tables 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-tables.xlsx`。本机已用
  Excel 打开验证 `Inventory`、`Totals` 和 `Plain` sheet 的 `ListObjects` 数量、
  表名、范围、header 文本、`TotalsTable.ShowTotals=True`、`TotalsTable`
  totals row 范围 `A3:B3` 和内建 table style flags；不要把这扩展成完整 table UI
  或已有文件编辑保证。结构测试还覆盖默认/false `totalsRowShown="0"`、true
  `totalsRowCount="1"`、用户声明的 `totalsRowFunction="sum"` 和
  `totalsRowLabel="Total"`，并确认不会生成公式文本、空 label attribute 或
  `xl/styles.xml`。
  本地 table totals QA 入口是 `tools/verify_table_totals_metadata.py` 和
  `tools/verify_table_totals_excel.ps1`，会做拆包 XML、`openpyxl`、`XlsxWriter`
  参考和 Excel COM 只读验证；生成物只属于 `build/qa/` 本地 artifact。
  当前 table overlap 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-table-range-overlap.xlsx`；本地 QA
  入口是 `tools/verify_table_overlap_metadata.py` 和 `tools/verify_table_overlap_excel.ps1`，
  会做拆包 XML、`openpyxl` 和 Excel COM 只读验证，确认同一 worksheet 相邻 tables
  与跨 worksheet 相同 range 可打开。
  结构测试还覆盖 `TableOptions` 的四个 table style flags，并确认这些内建样式标志
  只写入 `<tableStyleInfo>`，不会生成 `xl/styles.xml`。
  本机 Excel COM 已只读打开
  `build/windows-nmake-release/tests/fastxlsx-streaming-table-style-flags.xlsx`，
  确认 `StyleFlagTable` 的 first/last/row/column flags 为 `True/True/False/True`。
  结构测试还覆盖 table column name 属性转义，包括双引号、单引号、`&`、`<` 和 `>`；
  本机 Excel COM / `openpyxl` 已只读打开
  `build/windows-nmake-release/tests/fastxlsx-streaming-table-column-escape.xlsx`，
  确认 `EscapedColumnTable` 表头语义保持为 `Text "quoted"`、`Owner's Share`
  和 `A&B<Limit>`；本机 `XlsxWriter` 3.2.0 参考 workbook 拆包后列名语义一致。
  结构测试还覆盖 table 与 external hyperlinks 共存时 worksheet `.rels` 中的
  owner-local relationship id 分配，确认 `<tableParts>` 和 `<hyperlinks>` 引用
  各自匹配对应 `.rels` entry。
- 当前 `fastxlsx.streaming` conditional formatting 基础切片推荐 preset 输出样例包括 color scale：
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-two-color-scale.xlsx` 和
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-three-color-scale.xlsx`；
  metadata-order、多区域和 priority 回归样例分别为
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-metadata-order.xlsx`、
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-multi-range.xlsx` 和
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-priorities.xlsx`。
  结构测试确认 `<conditionalFormatting sqref>`、`<cfRule type="colorScale">`、
  两个 `<cfvo>`、两个 `<color rgb="AARRGGBB">`、priority 顺序、multi-range `sqref`、
  suffix 顺序 `mergeCells -> conditionalFormatting -> dataValidations`，以及不生成
  `styles.xml`、`dxfs`、worksheet `.rels`、content type、workbook relationship 或
  `<calcPr>` 副作用。本地 QA 入口是
  `tools/verify_conditional_formatting_color_scales.py` 和
  `tools/verify_conditional_formatting_color_scales_excel.ps1`；前者做拆包 XML、
  `openpyxl` 语义和可选 `XlsxWriter` 参考 workbook，后者用本机 Excel COM 只读打开
  two-color、three-color 与 multi-range 样例并核对 color scale 可见性。
  data bar 样例是
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-data-bar.xlsx`、
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-data-bar-metadata-order.xlsx`、
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-data-bar-multi-range.xlsx` 和
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-data-bar-priorities.xlsx`；
  结构测试确认 `<cfRule type="dataBar">`、两个 `<cfvo>`、一个 `<color rgb="AARRGGBB">`、
  可选 `showValue="0"`、shared priority、multi-range `sqref`、suffix 顺序和无 side effects。本地 QA 入口是
  `tools/verify_conditional_formatting_data_bars.py` 和
  `tools/verify_conditional_formatting_data_bars_excel.ps1`，分别做拆包 XML /
  `openpyxl` / 可选 `XlsxWriter` 参考和本机 Excel COM 只读可视化检查。不要把它扩展成完整
  conditional formatting、dxf/styles、公式规则、advanced data bar、advanced/custom icon sets、existing-file editing
  或 Excel UI parity。
  basic icon set 样例是
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-icon-set.xlsx`、
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-icon-set-metadata-order.xlsx`、
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-icon-set-multi-range.xlsx` 和
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-icon-set-priorities.xlsx`；
  结构测试确认 `<cfRule type="iconSet">`、`iconSet="3Arrows"`、三枚 `<cfvo>`、
  `showValue` / `reverse` 可选 attributes、shared priority、multi-range `sqref`、suffix
  顺序和无 side effects。本地 QA 入口是
  `tools/verify_conditional_formatting_icon_sets.py` 和
  `tools/verify_conditional_formatting_icon_sets_excel.ps1`，分别做拆包 XML /
  `openpyxl` / 可选 `XlsxWriter` 参考和本机 Excel COM 只读可视化检查。
  QA output note: the icon set helper also expects
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-icon-set-percentile.xlsx`
  for the existing `IconSetValueType::Percentile` threshold path.
- 当前 `fastxlsx.streaming` Phase 3 metadata 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-phase3-metadata.xlsx`。本机已用
  Excel COM 打开验证 `Metadata` sheet、`B2` / `C2` 公式、row 2 高度、A/C 列宽、
  `B2:D4` auto filter、`A3:B3` / `C4:D4` merge areas，以及
  `SplitRow=2` / `SplitColumn=3` frozen panes。公式 cell 会在 `xl/workbook.xml`
  写出 `<calcPr calcId="124519" fullCalcOnLoad="1"/>` 请求 Excel 打开后重算；
  本机 `openpyxl` 3.1.2 已读到 `calcId=124519` 和 `fullCalcOnLoad=True`，并确认
  没有 `xl/calcChain.xml`。当前本地 QA 入口是 `tools/verify_phase3_metadata.py`
  和 `tools/verify_phase3_metadata_excel.ps1`，分别做拆包 XML / `openpyxl` 检查和
  Excel COM 只读可视化检查；不要把这扩展成公式计算、cached values、calcChain、
  styles、默认 CI 强依赖或完整 Phase 3。
- 当前 `fastxlsx.streaming` styles 推荐 preset 输出样例包括 number-format：
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-number-formats.xlsx`；
  sharedStrings + styles 共存样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-shared-strings.xlsx`；
  limited alignment 样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-alignment.xlsx`；
  bold/italic/direct-color font 样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-fonts.xlsx`；solid fill 样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-fills.xlsx`。
  结构测试确认 `xl/styles.xml`、styles content type override、workbook styles
  relationship、custom `numFmtId`、默认 font/fill/border/cell style 骨架、
  worksheet `s="N"` 引用、默认 `s="0"` 省略、`applyFont="1"` / `<b/>` / `<i/>` /
  direct `<color rgb="FFC00000"/>`、`fontId` 复用、
  `applyFill="1"` / solid `<patternFill>` / `fgColor rgb`、`fillId` 复用、
  `applyAlignment="1"` /
  `<alignment wrapText="1"/>`、`<alignment horizontal="left|center|right"/>`、
  `<alignment vertical="top|center|bottom"/>`、combined alignment
  `<alignment wrapText="1" horizontal="center" vertical="center"/>`、
  alignment-only style 不创建 custom `numFmt`、
  font-only style 不创建 custom `numFmt`、fill-only style 不创建 custom `numFmt`、
  number format + bold / number format + font color 复用同一个 `fontId`、
  number format + fill / font + fill 复用同一个 `fillId`、
  `StyleId{}` 清除 per-cell style 且不写 `s="0"`、
  sharedStrings relationship 在 styles relationship 之前、styles workbook
  relationship 与 worksheet-local hyperlink/table relationships 共存且不偏移 worksheet
  `rId`，以及 foreign `StyleId`
  失败不会推进 row state 或生成
  `styles.xml`；非法 `add_style()` 注册失败不会消耗 style id 或生成多余
  `styles.xml` 记录；all-default optional alignment/font metadata 与其他有效
  style 属性组合时不会生成额外 style id、alignment/font 记录或 `cellXfs`；
  带样式公式 cell 仍会保留 `s="N"`、触发 workbook full-recalculation metadata，
  且不会创建 `xl/calcChain.xml`。
  本地 QA 入口是：
  `py tools\verify_styles_number_formats.py --input build\windows-nmake-release\tests\fastxlsx-streaming-styles-number-formats.xlsx --shared-input build\windows-nmake-release\tests\fastxlsx-streaming-styles-shared-strings.xlsx --alignment-input build\windows-nmake-release\tests\fastxlsx-streaming-styles-alignment.xlsx --font-input build\windows-nmake-release\tests\fastxlsx-streaming-styles-fonts.xlsx --fill-input build\windows-nmake-release\tests\fastxlsx-streaming-styles-fills.xlsx --work-dir build\qa\styles-number-formats`
  和
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_styles_excel.ps1 -Path build\windows-nmake-release\tests\fastxlsx-streaming-styles-number-formats.xlsx -SharedPath build\windows-nmake-release\tests\fastxlsx-streaming-styles-shared-strings.xlsx -AlignmentPath build\windows-nmake-release\tests\fastxlsx-streaming-styles-alignment.xlsx -FontPath build\windows-nmake-release\tests\fastxlsx-streaming-styles-fonts.xlsx -FillPath build\windows-nmake-release\tests\fastxlsx-streaming-styles-fills.xlsx`。
  Python helper 会拆包检查 styles XML，并用 `openpyxl` 核对 number format、
  wrap-text、horizontal/vertical alignment、bold/italic/direct-color font 和 solid fill 语义、
  可用时创建 `XlsxWriter` 参考 workbook；Excel helper 用本机 Excel
  COM 只读打开五个样例，核对 NumberFormat、WrapText、HorizontalAlignment、
  VerticalAlignment、Font.Bold、Font.Italic、Font.Color、Interior.Pattern、
  Interior.Color、值和公式。不要把这扩展成完整
  样式系统、date cell type、existing-file style preservation 或跨办公软件完整显示等价。
- 当前 `fastxlsx.streaming` 空行 dimension 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-empty-row-dimensions.xlsx`。
  结构测试确认无行 worksheet 和只含空行 worksheet 的 `<dimension ref="A1"/>`，
  空行会写出 `<row r="N"></row>`，前导空行 + 数据行 + 尾部空行样例写出
  `<dimension ref="A1:C3"/>`。本机 Excel COM 已只读打开 `NoRows`、`OnlyEmpty`
  和 `Sparse` 三个 sheet；Excel `UsedRange` 会按实际非空单元格显示，结构语义仍以
  拆包后的 worksheet XML 为准。不要为了匹配 Excel `UsedRange` 回扫 worksheet 或保留
  完整 cell matrix。
- 当前 `fastxlsx.streaming` 最大合法列结构测试输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-max-column-boundary.xlsx`。
  结构测试写出单行 16,384 个 numeric cells，确认 `<dimension ref="A1:XFD1"/>`、
  `A1`、`XFD1`，并确认没有写出 `XFE1`。本机 `openpyxl` 和 Excel COM 已只读
  打开验证 `MaxColumn` sheet、`A1=1`、`XFD1=1`；这是 cell reference / dimension
  边界测试，不是宽表性能 benchmark 或最大列大文件兼容性证明。
- 当前 `fastxlsx.streaming` 最大合法行结构测试输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-max-row-boundary.xlsx`。
  测试使用 `FASTXLSX_ENABLE_TEST_HOOKS` /
  `fastxlsx::detail::testing_set_worksheet_row_count()` 将内部 row count 注入到
  `1048575` 后追加一行，确认 `<dimension ref="A1:C1048576"/>`、
  `A1048576`、`B1048576`、`C1048576` 和公式重算 metadata，并确认没有写出
  `1048577`。本机 `openpyxl` 和 Excel COM 已只读打开验证 `MaxRow` sheet 的
  `A1048576=45500`、`B1048576=TRUE`、`C1048576` 公式；Excel `UsedRange`
  会定位到实际非空的 `A1048576:C1048576`，generated dimension 仍以拆包 XML 为准。
  这是 test-only 稀疏结构边界，不是百万行导出性能证明。
- 当前 `fastxlsx.streaming` 失败 append 状态测试输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-failed-append-state.xlsx`。
  结构测试确认非法 row height 或非有限 numeric cell 被拒绝时，不推进 row number /
  dimension，不写入被拒绝的 text 或 formula，不创建无用 `xl/sharedStrings.xml`，
  也不污染 workbook `<calcPr>`。本机 `openpyxl` 和 Excel COM 已只读打开验证
  `FailedAppend!A1:A2`；这是 `append_row()` 校验先于状态变更的回归测试。
- 当前 numeric XML 输出共享内部 `detail::append_number()` /
  `detail::format_number()` helpers；in-memory、CellStore 和 streaming XML buffer
  都走 finite-only `std::to_chars` fast path，其中 append helper 避免在追加型 XML
  热路径中为每个数字单元格先构造临时 `std::string`。这不是 date cell type、格式化
  语义或完整数值性能 benchmark。
- 当前 cell reference XML 输出共享内部 `detail::append_cell_reference()` /
  `detail::cell_reference()` helpers；in-memory、CellStore 和 streaming XML buffer
  都复用同一 Excel row/column 上限校验，其中 append helper 避免在 row/cell XML
  热路径中为每个单元格先构造临时 reference 字符串。
- 当前 unsigned integer XML append 共享内部 `detail::append_unsigned_decimal()`
  helper；cell reference row suffix、streaming `<row r>`、CellStore / streaming
  style `s` attribute 和 sharedStrings string-cell `<v>` index 会直接向既有 XML
  buffer 追加十进制 unsigned integer，避免这些局部路径通过 `std::to_string()`
  构造临时字符串。这不是 benchmark 证据、sharedStrings 策略变更、date encoding
  完成或完整 hot-path 优化结论。
- 当前 sharedStrings duplicate lookup 使用透明 `std::string_view` 查找
  workbook-scope shared string index map；repeated string cell 先用 caller view
  复用已有 index，只有新 unique string 才创建 owning key。这仍不是 sharedStrings
  生产就绪、峰值内存证明、benchmark 结果或 existing-file sharedStrings index
  migration。
- 当前 shared string index map 使用 `std::string_view` key 指向稳定的 unique-string
  storage，不再为每个 unique shared string 在 map key 中保存第二份 owning
  `std::string`。它仍保留全部 unique strings，不是完整低内存 sharedStrings
  存储、benchmark 结果或生产策略完成。
- 当前 XML text / attribute escaping 共享内部
  `detail::append_escaped_xml_text()` / `detail::append_escaped_xml_attribute()`
  helpers；in-memory、CellStore、streaming row/formula/metadata XML 和小型 OPC
  serializer 可直接向目标 buffer 追加转义内容，避免追加型路径先构造临时
  `std::string`。`escape_xml_text()` / `escape_xml_attribute()` 仍保留给需要
  owned string 的 replacement 路径。
- 当前 `fastxlsx.streaming` 行上限测试使用测试构建内部 hook
  `FASTXLSX_ENABLE_TEST_HOOKS` / `fastxlsx::detail::testing_set_worksheet_row_count()`
  将 `WorksheetWriterState::row_count` 注入到 `1048576`，再验证下一次
  `append_row()` 抛出 `FastXlsxError`。该 hook 只用于避免默认 CTest 真实写满
  1,048,576 行；不要把它当成 public API、性能验证或百万行导出证明。
- 当前 `fastxlsx.streaming` images 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-images.xlsx`。本机已用
  Excel COM 打开验证 workbook 可读、`Images` 和 `SecondImage` sheet 各 1 个
  shape、`Plain` sheet 为 0 个 shape，Excel 报告首图锚点为 `C1:F5`、第二图为
  `A1:B2`；不要把这扩展成 existing-workbook 图片保真、完整 drawing 编辑或图片 UI 保证。
- 当前 `fastxlsx.streaming` 还在默认 preset 下覆盖 JPEG 结构输出：
  `xl/media/image1.jpg`、`image/jpeg` content type default、worksheet drawing
  relationship、drawing EMU 尺寸和 drawing `.rels` 到 `../media/image1.jpg` 的关系。
- 当前混合 PNG/JPEG 结构测试还覆盖同一个 worksheet 中两张不同格式图片共享一个
  drawing part、两个 `<xdr:twoCellAnchor>`、`image1.png` / `image2.jpg` 全局 media
  编号，以及 drawing `.rels` 中 owner-local `rId1` / `rId2` target。
- 当前默认 preset 还覆盖最大合法 anchor marker 结构测试，确认
  `CellRange` 最大合法行列会序列化为 drawing XML 的 0-based marker，例如
  `<xdr:col>16383</xdr:col>` 和 `<xdr:row>1048575</xdr:row>`；这只是结构边界测试，
  不是百万行/最大列大文件性能证明。
- 当前默认 preset 还覆盖 drawing anchor metadata 和 non-visual metadata：
  `build/windows-nmake-release/tests/fastxlsx-streaming-image-metadata.xlsx`
  会在同一 worksheet 的三个 anchors 中写 `editAs="oneCell"`、`editAs="absolute"`、
  默认 `editAs="twoCell"`、自定义 `xdr:cNvPr name`、`descr`、XML attribute escape、
  空 description 省略和默认 `Picture N` 名称。本机 `tools/verify_image_metadata.py`
  已拆包检查 drawing XML，并用 `openpyxl` / `XlsxWriter` 做本地 QA；该 helper 现在
  还可通过 `--basic-input` 检查 `fastxlsx-streaming-images.xlsx`，通过
  `--mixed-object-input` 检查 `fastxlsx-streaming-mixed-object-rels.xlsx` 的 media /
  drawing / worksheet rels / table / hyperlink 关系，并通过 `--memory-input` 检查
  `fastxlsx-streaming-memory-images.xlsx` 的 media bytes、package XML、`openpyxl`
  smoke 和 `XlsxWriter` 参考 workbook。`tools/verify_image_metadata_excel.ps1`
  已用 Excel COM 检查 shape name、AlternativeText、`Placement` 映射和首图 marker
  offset 对 `Shape.Left` / `Top` / `Width` / `Height` 的 EMU-to-points 几何影响，
  也会通过 `-BasicPath`、`-MixedObjectPath` 和 `-MemoryPath` 核对基础图片、
  mixed-object 与 memory-source 样例的 shape / hyperlink / table counts 与 anchors。
  `openpyxl` 可能跳过 JPEG 图片读取；JPEG media/drawing 关系以拆包 XML 和 Excel COM
  为准。该测试确认 metadata 不新增额外 drawing part、worksheet relationships、
  content types 或 media part 语义。
- 当前默认 preset 还覆盖图片对象 external hyperlink metadata：
  `build/windows-nmake-release/tests/fastxlsx-streaming-image-hyperlinks.xlsx`
  会生成 path PNG 和 memory-source JPEG 两个图片对象 hyperlink。结构测试确认
  drawing XML 中 `xdr:cNvPr/a:hlinkClick`、URL/tooltip XML attribute escape、
  drawing `.rels` 中 `TargetMode="External"` 的 hyperlink relationship、图片
  `a:blip r:embed` 仍指向独立 image relationship，且 worksheet XML 不写
  `<hyperlinks>`、worksheet `.rels` 不增加图片对象 hyperlink、workbook relationships
  和 `[Content_Types].xml` 无额外副作用。`tools/verify_image_metadata.py` 通过
  `--hyperlink-input` 拆包检查该样例，并创建/对比 `XlsxWriter` 参考 workbook；
  `openpyxl` 不暴露图片对象 hyperlink metadata，相关语义以拆包 XML 和 Excel COM
  为准。`tools/verify_image_metadata_excel.ps1` 通过 `-HyperlinkPath` 用本机 Excel
  COM 只读打开 `ImageLinks` sheet，核对 shape 数量、shape hyperlink address /
  tooltip 和 anchors。该切片不代表 worksheet cell hyperlink、internal picture link、
  URL 可达性校验、完整 hyperlink UI 或 existing-file drawing mutation。
- 当前默认 preset 还覆盖图片对象 hyperlink 与 worksheet 对象混合关系：
  `build/windows-nmake-release/tests/fastxlsx-streaming-image-hyperlink-mixed-objects.xlsx`
  同一 worksheet 内包含 worksheet cell hyperlink、一个带 external picture hyperlink
  的 PNG 图片、一个普通 JPEG 图片和一个 table。结构测试和本地 QA 确认 worksheet
  owner-local relationships 为 cell hyperlink / drawing / table，drawing owner-local
  relationships 为 PNG media / JPEG media / picture hyperlink；`tools/verify_image_metadata.py`
  通过 `--mixed-hyperlink-input` / `--mixed-image-hyperlink-input` 拆包检查该样例，
  `tools/verify_image_metadata_excel.ps1` 通过 `-MixedHyperlinkPath` 用本机 Excel
  COM 只读核对 shape 数量、cell hyperlink、table count、picture hyperlink address
  和 anchors。`openpyxl` 只作为 reader-visible smoke，picture hyperlink 语义仍以
  拆包 XML 和 Excel COM 为准。
- 当前默认 preset 还覆盖多对象 relationship id 回归测试：同一 worksheet
  内多个 external hyperlink、一个 drawing、多个 table 共享 worksheet owner-local
  `rId` 序列，第二个 worksheet 重新从 `rId1` 计数，drawing `.rels` 内图片关系也按
  drawing owner 局部计数；这只是结构一致性测试，不代表完整对象功能。
  本机 Excel COM 已只读打开
  `build/windows-nmake-release/tests/fastxlsx-streaming-mixed-object-rels.xlsx`，
  确认 `Objects` 为 2 个 hyperlinks / 2 个 shapes / 2 个 tables，
  `MoreObjects` 为 1 / 1 / 1，`Plain` 为 0 / 0 / 0。
- 当前 sharedStrings benchmark 小样例位于
  `build/windows-nmake-release-benchmark/benchmarks/sharedstrings-v3-*.xlsx`。本机已用
  Excel COM 只读打开 `sharedstrings-v3-repeated-inline.xlsx`、
  `sharedstrings-v3-repeated-shared.xlsx`、`sharedstrings-v3-unique-inline.xlsx` 和
  `sharedstrings-v3-unique-shared.xlsx`，确认 `Sheet1` 使用范围为 `50000 x 10`，
  首尾值符合 repeated / unique 预期。benchmark JSON 的 `office_open` 字段仍是工具默认
  `not_run`；Excel COM 检查是独立本机记录。
- 当前 sharedStrings smoke 样例
  `build/windows-nmake-release/tests/fastxlsx-streaming-shared-strings.xlsx` 有两个本地
  QA 入口：`tools/verify_shared_strings_excel.ps1` 用 Excel COM 只读打开并核对
  `Shared!A1:D3`；`tools/verify_shared_strings_reference.py` 检查关键 OpenXML entry、
  sharedStrings 关系/index/count/uniqueCount，并创建 `openpyxl` / `XlsxWriter` 参考
  workbook。Python 环境缺少 `xlsxwriter` 时，该分支只记录为 skipped。
- 当前 image metadata 样例
  `build/windows-nmake-release/tests/fastxlsx-streaming-image-metadata.xlsx`
  有两个本地 QA 入口：`tools/verify_image_metadata.py` 拆包检查 drawing XML 的
  `xdr:cNvPr name` / `descr`、relationships、content types、media entries，并用
  `openpyxl` 打开确认 3 张图片、用 `XlsxWriter` 创建参考 workbook；同一 Python
  helper 还用 `--memory-input` 检查 memory-source 样例的 media bytes、drawing/rels
  和 content types，并用 `--hyperlink-input` 检查图片对象 hyperlink 的
  `a:hlinkClick`、drawing-owned hyperlink `.rels` 和 `XlsxWriter` 参考语义，还用
  `--mixed-hyperlink-input` 检查图片对象 hyperlink 与 worksheet cell hyperlink /
  table 共存时的 owner-local relationship id。
  `tools/verify_image_metadata_excel.ps1`
  用本机 Excel COM 只读打开，确认 3 个 shapes、自定义 `NamedOnly`、默认
  `Picture 3`、首图 `AlternativeText`、`Placement` 映射，以及首图 marker offset 对
  shape 几何的影响。本机已验证 Excel COM 会把 `descr` 映射到
  `Shape.AlternativeText`；`-MemoryPath` 还核对 memory-source 样例的两张图片
  anchors，`-HyperlinkPath` 核对图片对象 hyperlink address / tooltip。结构语义仍以
  拆包后的 drawing XML 为准。
- `.xlsx` 结构异常时，按 `docs/TESTING_WORKFLOW.md` 使用 Excel、`openpyxl`
  或 `XlsxWriter` 生成语义等价参考文件，拆包后对比 XML，重点检查
  content types、relationships、workbook、worksheet、shared strings 和 styles。
- 编辑已有 XLSX 时，不只验证目标单元格，还要验证未修改 part 被保留。
- 性能结论必须记录总耗时、峰值内存、输出文件大小和打开兼容性。
- benchmark 对比对象是文档中的 `OpenXLSX`、`xlnt` streaming writer 和旧 `FastExcel`。

## 高风险误区

- 把路线图里的类名当成已实现 API。
- 为了快速实现，把大型 worksheet 路径改成 DOM。
- 把 `OpenXLSX` / `xlnt` 包一层当 FastXLSX 引擎。
- 从已知 part 全量重建 package，导致未知 part 丢失。
- 在代码未使用前，把所有规划依赖提前接入 CMake。
- 把 `src/zip_store_writer.*`、`src/package_writer.*`、`src/package_reader.*` 或
  `src/package_editor.*`
  当成已有文件编辑 API。
  当前 package writer 只是内部边界：默认 backend 是 Phase 1 stored bootstrap，
  opt-in backend 是 minizip-ng/DEFLATE；内部 `PackageWriterOptions::compression_level`
  可选择 `-1` backend default、`0` no-compression/stored output 或 `1..9`
  minizip DEFLATE level，stored bootstrap 仍是 stored/no-compression；内部 writer
  会在打开输出路径前拒绝空 entry list、非法或重复 ZIP entry name、缺失或不可
  stat 的 file-backed chunk、需要 Zip64 的 entry count 或单 entry 未压缩大小，
  超过 ZIP 16-bit 字段的 entry name，以及同一 entry 混用 legacy `data` payload
  和 chunked payload、memory/file chunk-source 混用或未知 chunk kind；仍无 Zip64、true package streaming、
  public compression API 或 broad existing-file preservation。当前 package reader 只是 stored/no-compression
  ZIP entry 索引/读取，加上 opt-in minizip 构建下的 DEFLATE entry 读取、payload
  CRC 校验、local-header CRC/method/name/size mismatch、encrypted/data descriptor 拒绝、owner-missing source-owned `.rels` 拒绝
  （包括根级 `_rels/foo.xml.rels` owner relationships）和小型 OPC metadata ingestion 基础；当前 package editor 只是
  internal stored-package copy/replace foundation，外加 docProps generated-small-XML、
  worksheet replacement calcChain-remove/fullCalcOnLoad、组合回归和内部 manifest
  write-mode 审计同步、EditPlan package-entry 审计、exact/path-equivalent
  source-overwrite rejection / empty-output / missing-parent / non-directory-parent / existing-directory output rejection 安全护栏和 malformed workbook metadata / workbook calc
  metadata、invalid replacement、metadata-entry replacement、invalid removal
  no-state-pollution
  回归，以及 queued docProps 后 linked worksheet fail 不污染状态的窄回归；不是 public PackageEditor、
  完整 copy-original writer、完整 dependency sync 或 broad preservation 证明。
  即使 worksheet package entry 使用 file-backed/chunked source，也只是内部
  new-workbook output 的 entry-source 优化。
- 把 `stb` 当成完整图片 OpenXML 支持。`stb` 只负责图片解码/尺寸/像素读取；
  当前 `WorksheetWriter::add_image()` 只是 new-workbook PNG/JPEG path/memory-source
  插入基础切片，
  不代表 existing-workbook 图片保真、drawing 编辑、裁剪、旋转、压缩或格式转换。
- 把 `ImageOptions::edit_as` 写成 `oneCellAnchor` / `absoluteAnchor` 元素支持，
  或把 `ImageOptions::from_offset` / `to_offset` 写成 row/column resize 几何计算、
  跨办公软件 UI 保证、drawing mutation 或 existing-workbook 图片编辑；或把
  `ImageOptions::name` / `description` 写成完整图片 metadata、完整 alt text UI、
  EXIF/PNG/JPEG metadata、media filename 语义。
- 把 `ImageOptions::external_hyperlink_url` 写成 worksheet cell hyperlink、cell
  text、hyperlink style、internal picture hyperlink、URL 可达性校验、完整 Office
  hyperlink UI、existing-file drawing editing 或完整图片保真。当前它只写
  drawing XML `a:hlinkClick` 和 drawing-owned external hyperlink relationship。
- 把第三方源码复制进 `src` 或 `include`。
- 修改 `tests/CMakeLists.txt` 后让 `ctest` 回到 0 测试，或让默认测试超过 60s。
- 把 `FASTXLSX_ENABLE_TEST_HOOKS` 或
  `fastxlsx::detail::testing_set_worksheet_row_count()` 当作用户可用 API；它只服务
  默认单元测试中的低成本边界注入。
- 为了 API 易用性牺牲 streaming 性能主线。
- 允许 `NaN`、`+Inf`、`-Inf`、非正 row height 或非正 column width 写进 XML。
- 把 styles 基础切片写成完整样式系统。当前只支持 workbook-local `StyleId`、自定义
  number format、窄 wrap-text + limited horizontal/vertical alignment、
  窄 bold/italic/direct-color font、窄 solid foreground fill
  和 `xl/styles.xml` 基础骨架；不支持字号、字体名、下划线、theme/tint/indexed font color、
  gradient fill、任意 pattern fill、theme/tint/indexed palette fill、边框、完整对齐、rich text、dxf-backed conditional formatting、date cell type、existing-file style preservation
  或完整 Excel formatting parity。
- 把 `WorksheetWriter::add_conditional_color_scale()` /
  `WorksheetWriter::add_conditional_data_bar()` / `WorksheetWriter::add_conditional_icon_set()`
  写成完整 conditional formatting 或 styles/dxfs 支持。当前只写 streaming-only new-workbook
  two-/three-color color scale、basic data bar 与 basic 3Arrows icon set 的 worksheet-local XML；
  不支持公式规则、advanced data bars、advanced/custom icon sets、dxf 样式、冲突检测、
  existing-file editing 或完整 Excel UI。
- 把 `100000` / `500000` cells 的小规模手工 benchmark 快照写成 1,000 万 cells、
  大文件性能、完整低内存、Google Benchmark 或 sharedStrings 生产就绪结论；也不要把
  `worksheet-body-file-bytes` 写成完整 package、完整临时文件或进程峰值内存 footprint。
- public API 没有文档注释，或注释不说明内存/性能限制。

## 项目 Skills

项目专属 skills 位于 `.agents/skills/`：

- `fastxlsx-project-navigation`：架构导航、当前实现状态、模块边界。
- `fastxlsx-minimal-writer`：Phase 1 最小可写 XLSX。
- `fastxlsx-cmake-build`：本地配置、构建、测试入口和 CMake target。
- `fastxlsx-dependency-policy`：依赖、vcpkg、license 和参考库边界。
- `fastxlsx-streaming-worksheet`：大型 worksheet 流式路径和热路径约束。
- `fastxlsx-opc-editing`：part-level rewrite 和已有文件保真编辑。
- `fastxlsx-test-quality`：测试、benchmark、兼容性验证和质量排障。
- `fastxlsx-api-design-docs`：API 设计、文档注释和性能边界。
- `fastxlsx-worksheet-metadata-features`：worksheet metadata 和 Phase 5 早期切片。
- `fastxlsx-conditional-formatting-features`：streaming conditional formatting、color scale、
  basic data bar、basic 3Arrows icon set、priority、multi-range `sqref` 和 Excel/openpyxl/XlsxWriter QA。
- `fastxlsx-image-media-features`：图片读取/插入、stb 解码、media/drawing part 和关系同步。
- `fastxlsx-style-registry`：number format / wrap-text + limited horizontal/vertical alignment /
  bold-italic font / solid fill styles、`StyleId`、`CellAlignment`、
  `HorizontalAlignment`、`VerticalAlignment`、`CellFont`、`CellFill`、`CellStyle`、`xl/styles.xml`
  和样式 QA 验证。
