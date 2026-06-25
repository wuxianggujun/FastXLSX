---
name: fastxlsx-test-quality
description: "添加或排查 FastXLSX 测试、CTest/Catch2 集成、OpenXML 兼容性检查、本机 Excel 可视化验证、参考 XLSX 拆包 XML 对比、benchmark、内存/性能验证和质量门禁。用于让 ctest 跑起来、补单元测试、设计基准、验证生成 XLSX，或调查结构、性能、内存、兼容性回退。"
---

# FastXLSX Test Quality

## 必读文件

- `tests/CMakeLists.txt`
- `CMakeLists.txt`
- `docs/PERFORMANCE_TARGETS.md`
- `docs/TESTING_WORKFLOW.md`
- `docs/ROADMAP.md`
- `docs/TECHNICAL_COMPARISON.md`
- `README.md`
- `docs/DEPENDENCIES.md`

当前已有轻量 CTest 测试入口。尚未接入 Catch2。

## 当前测试事实

- `FASTXLSX_BUILD_TESTS` 默认 `ON`。
- 顶层 CMake 在启用测试时调用 `enable_testing()` 和 `add_subdirectory(tests)`。
- 当前测试入口包括：
  - `fastxlsx_tests` / `fastxlsx.unit`。
  - `fastxlsx_streaming_writer_tests` / `fastxlsx.streaming`。
  - `fastxlsx_opc_tests` / `fastxlsx.opc`。
  - `fastxlsx_worksheet_event_reader_tests` / `fastxlsx.worksheet_event_reader`。
  - `fastxlsx_worksheet_cell_index_tests` / `fastxlsx.worksheet_cell_index`。
  - `fastxlsx_worksheet_transformer_tests` / `fastxlsx.worksheet_transformer`。
  - `fastxlsx_package_reader_tests` / `fastxlsx.package_reader`。
  - `fastxlsx_package_editor_tests` / `fastxlsx.package_editor`。
  - `fastxlsx_workbook_editor_*_tests` / `fastxlsx.workbook_editor*` shard family,
    including `fastxlsx.workbook_editor_public_patch_cells` for the public
    targeted-cell Patch facade.
  - `fastxlsx_image_tests` / `fastxlsx.image`。
- 当前 `fastxlsx.workbook_editor_public_patch_cells` 只覆盖
  `WorkbookEditor::replace_cells()` 和 explicit
  `CellPatchMissingCellPolicy::Insert` public upsert policy；不要重新增加单独
  upsert-named public wrapper delegation 测试。内部 PackageEditor upsert helper
  仍由 transformer / package-editor tests 覆盖。
- 当前 `fastxlsx.worksheet_transformer` 还覆盖 targeted-cell Patch 的 completed-target
  tail pass-through：早段 replace/upsert targets 全部完成后，尾部 source cells
  仍原样保留，并且 strict replace 在进入 tail fast path 前仍替换重复 source
  target。当前 `fastxlsx.worksheet_cell_index` 覆盖 internal source-offset cell
  index foundation 和 indexed rewrite planning foundation 的 exact byte range /
  source-order plan / chunk-boundary / invalid-source 边界；`fastxlsx.worksheet_transformer`
  还覆盖 transform action source offset 与 synthetic insert offset 边界；它不是
  public Patch 默认算法、metadata repair、Office 兼容性或性能证明。性能声明仍要靠
  opt-in benchmark。
- 当前测试生成基础和 streaming smoke `.xlsx`，通过 backend-neutral ZIP helper
  读取解压后的 entries，并检查 content types、relationships、worksheet XML、XML escape、cell reference、
  streaming writer metadata、data validations、sharedStrings 结构、基础可配置
  docProps 输出和内部 OPC manifest 基础。
- 当前 sharedStrings 结构测试还覆盖空表 package hygiene：启用
  `StringStrategy::SharedString` 但没有字符串 cell 时，不生成空 `xl/sharedStrings.xml`、
  sharedStrings content type 或 workbook relationship，worksheet 不写 `t="s"` 或
  `inlineStr`，公式仍触发 workbook recalculation metadata。
- 当前 streaming 测试还覆盖 file-backed/chunked worksheet package entry 的结构语义：
  解压后的 worksheet XML、entry name 唯一性、content type、relationships，以及
  stored/minizip backend 可用时的 backend-neutral 结果。
- 当前 package reader 测试覆盖非法 ZIP entry name 拒绝：绝对路径、尾部斜杠、
  反斜杠、query/fragment components、空段、dot 段和 parent 段都应在
  `PackageReader::open()` 阶段失败。
- 当前 package reader 测试还覆盖 `[Content_Types].xml` / `.rels` metadata root
  validation：第一个真实 XML 元素不是 `Types` / `Relationships` 时应在
  `PackageReader::open()` 阶段失败；`Default` / `Override` / `Relationship`
  declaration 藏在 unsupported child 下的 nested decoy 也应失败；metadata
  attributes 必须未命名空间（namespace declarations 除外），namespaced metadata
  attribute decoy 和重复未命名空间 metadata attribute 也应在
  `PackageReader::open()` 阶段失败；非 whitespace metadata text 和 start/end tag
  QName mismatch 也应失败。这只是 reader
  validation，不是 content-type 或 relationship repair。
- 当前 streaming dimension 测试覆盖无行 worksheet、只含空行 worksheet、前导空行 +
  数据行 + 尾部空行：结构测试以拆包后的 worksheet XML 为准，确认空行写出
  `<row r="N"></row>`，只含空行时 dimension 仍为 `A1`，尾部空行会进入最后行
  dimension；不要用 Excel `UsedRange` 反推生成 XML 语义。
- 当前 streaming hot-path 边界测试还覆盖合法最大列 `XFD1` 正向输出、test-only
  hook 下合法最大行 `1048576` 稀疏输出，以及失败 `append_row()` 不推进 row number /
  dimension、不写入被拒绝 text/formula、不创建无用 sharedStrings part、不污染
  workbook `<calcPr>` 的状态卫生。
- 当前 streaming 行上限测试用 `FASTXLSX_ENABLE_TEST_HOOKS` 下的
  `fastxlsx::detail::testing_set_worksheet_row_count()` 注入内部 `row_count`，
  低成本触发第 1,048,577 行拒绝路径；这是测试 hook，不是 public API，也不是百万行性能
  benchmark。
- 当前 streaming Phase 3 metadata 测试覆盖公式 XML escape、row height、多个
  column width records、last-call-wins frozen panes、last-call-wins auto filters、
  多个 merged ranges、worksheet suffix ordering、workbook `<calcPr
  fullCalcOnLoad="1"/>` 重算请求，以及这些 metadata-only 功能不新增 worksheet
  relationships、workbook relationships 或 content type side effects；公式 cell 仍不生成
  `xl/calcChain.xml`。
- 当前数值边界测试覆盖：in-memory `Workbook::save()` 拒绝 numeric cell 的非有限值，
  并拒绝 row height 的 `0`、负数、`NaN`、`+Inf` 和 `-Inf`；streaming
  `WorksheetWriter::append_row()` 拒绝 numeric cell 的非有限值，并拒绝 row height 的
  `0`、负数、`NaN`、`+Inf` 和 `-Inf`；
  `WorksheetWriter::set_column_width()` 拒绝非正和非有限 width，避免 worksheet XML 写出
  `nan`、`inf`、`-inf` 或非法正数约束值。
- 当前 in-memory dimension / column-limit 测试覆盖空 worksheet、单个空行、
  `XFD1` 最大列 dimension，以及 16385 列在 `Workbook::save()` 序列化时被拒绝；
  这些是结构边界测试，不是大文件性能 benchmark。
- 当前 document properties 测试覆盖 in-memory `Workbook::set_document_properties()`
  和 streaming `WorkbookWriterOptions::document_properties`，检查 `docProps/core.xml`、
  `docProps/app.xml`、XML escape、package relationships、content type overrides，
  并确认不生成 `docProps/custom.xml`。固定本地 QA 入口是
  `tools/verify_document_properties.py` 和 `tools/verify_document_properties_excel.ps1`；
  前者做 ZIP/XML 与 `openpyxl` core-property 检查，后者只读打开 in-memory /
  streaming 样例并做 Excel COM smoke，属性精确语义以 XML/openpyxl 为准。
- 当前 internal existing-package docProps Patch 回归还覆盖 core/app docProps helper
  重写 package relationships / content types 时保留已有 `docProps/custom.xml`、
  custom-properties package relationship、custom properties content type override 和
  unrelated unknown bytes；这只是 preservation-only 测试，不是 custom properties
  编辑或 public existing-file document-properties API。
- 当前 streaming data validations 测试覆盖 `count`、`sqref`、`type`、`operator`、
  `allowBlank`、`formula1`、`formula2`、XML escape、invalid ranges、
  invalid rule shapes、package relationship absence、与 relationship-backed
  metadata 共存时不消耗 worksheet-local `rId`、suffix 顺序、validation-only
  worksheet 不声明 `xmlns:r`、`formula2` XML text escape、prompt/error attributes、
  attribute escape、prompt-only、error-only、empty string omission、false flag omission、
  `stop` / `warning` / `information` error styles、list-only `showDropDown="1"`
  反向隐藏 dropdown arrow 语义、无 `styles.xml` / content type side effects、
  multi-area `sqref` token / `count` 语义、空 range list 和 multi-range 内非法 range
  拒绝，以及 mutation-after-close。
- 当前 streaming external hyperlinks 测试覆盖 worksheet `<hyperlinks>`、worksheet
  `.rels`、target XML escape、同一 worksheet 多个 hyperlink、跨 worksheet
  owner-local `rId`、plain sheet absence、workbook relationship absence、content
  type override absence、optional display/tooltip attributes、invalid cell、empty target
  和 mutation-after-close。
- 当前 streaming internal hyperlinks 测试覆盖 worksheet `<hyperlink location="...">`、
  location attribute XML escape、internal-only sheet 不生成 worksheet `.rels`、不声明
  `xmlns:r`、不写 `r:id`、mixed external/internal 场景下 external `rId` 不被 internal
  消耗、不污染 workbook relationships 或 content types、optional display/tooltip
  attributes、invalid cell、empty location 和 mutation-after-close。
- 当前 streaming tables 测试覆盖 `xl/tables/tableN.xml`、worksheet `<tableParts>`、
  worksheet `.rels`、table content type override、owner-local `rId`、与 hyperlinks
  共存时的关系 id、多对象 relationship id 回归、XML escape、table column
  attribute escape、invalid
  ranges/options、table style flags 且不生成 `xl/styles.xml`、`show_totals_row`
  true/false/default metadata、caller-supplied `totalsRowFunction` / `totalsRowLabel`、
  无公式文本 / 空 label attribute、duplicate names、same-worksheet table range overlap
  拒绝、adjacent/cross-worksheet non-overlap 允许和 mutation-after-close。
- 当前 streaming conditional formatting 测试覆盖 two-/three-color color scale、
  basic data bar 和 basic 3Arrows icon set。
- color scale 覆盖 two-/three-color
  `<conditionalFormatting>` XML、`cfvo` min/max/percentile midpoint 与 numeric/percentile endpoints、
  ARGB 大写色值、multi-range `sqref`、priority 同 worksheet 递增和跨 worksheet 重置、
  与 mergeCells/dataValidations/hyperlinks/tables 的 suffix 顺序、relationship id 不偏移、
  无 `styles.xml` / `dxfs` / `xl/metadata.xml` / worksheet `.rels` / content type /
  workbook relationship / `<calcPr>` 副作用、invalid range、empty range list、非有限
  endpoint、lower `Maximum`、upper `Minimum`、失败调用不污染 state 和 mutation-after-close。
- data bar 覆盖 `<cfRule type="dataBar">`、两个 `<cfvo>`、一个 `<color rgb>`、
  optional `showValue="0"`、numeric/percentile endpoints、multi-range `sqref`、与 color scale 共享 worksheet-local
  priority、跨 worksheet priority 重置、suffix 顺序、relationship id 不偏移、无
  `styles.xml` / `dxfs` / `xl/metadata.xml` / worksheet `.rels` / content type /
  workbook relationship / `<calcPr>` 副作用、invalid range、empty range list、非有限
  endpoint、lower `Maximum`、upper `Minimum`、失败调用不污染 state 和 mutation-after-close。
- icon set 覆盖 `<cfRule type="iconSet">`、built-in `3Arrows`、三枚 `<cfvo>`、
  numeric/percent/percentile thresholds、showValue/reverse metadata、multi-range `sqref`、
  与 color scale/data bar 共享 worksheet-local priority、跨 worksheet priority 重置、
  suffix 顺序、relationship id 不偏移、无 `styles.xml` / `dxfs` / `xl/metadata.xml` /
  worksheet `.rels` / content type / workbook relationship / `<calcPr>` 副作用、
  invalid range、empty range list、非有限 threshold、descending/duplicate thresholds、
  unknown enum、失败调用不污染 state 和 mutation-after-close。
- 当前 image 测试覆盖默认 `stb` 构建下 PNG/JPEG 文件/内存尺寸和通道读取，
  以及 `read_image_pixels(path/span)` 的 PNG/JPEG 文件/内存像素解码、owned pixel
  buffer size、unsupported memory/file header、empty memory buffer、empty file 和
  missing file。
  当前 streaming 测试还覆盖默认 preset 下 PNG/JPEG media、drawing、rels、
  content types、worksheet `<drawing>` 结构。memory-source 图片测试还覆盖 caller
  buffer mutation 后 package 内 media bytes 不变、empty buffer / unsupported header /
  mutation-after-close 拒绝路径。当前还
  覆盖 `ImageOptions` drawing metadata：`from_offset` / `to_offset` EMU offsets、
  two-cell marker `xdr:colOff` / `xdr:rowOff`、`xdr:twoCellAnchor editAs` 的
  `oneCell` / `absolute` / 默认 `twoCell`、`xdr:cNvPr name` / `descr`、XML
  attribute escape、空 description 省略、默认 `Picture N` 名称，以及无额外
  relationship / content type / media side effects。当前还应覆盖图片对象 external
  hyperlink metadata：`external_hyperlink_url` / `external_hyperlink_tooltip` 写入
  drawing XML `xdr:cNvPr/a:hlinkClick`、drawing `.rels` hyperlink relationship、
  `TargetMode="External"`、tooltip escape，并确认不写 worksheet `<hyperlinks>`、
  不创建 cell text / styles / workbook relationship / content type side effects。
- 当前没有 Catch2 集成。
- 当前 `fastxlsx.package_reader` 覆盖 stored/no-compression entry 索引/读取、
  payload CRC、content-types / relationships ingestion、unknown extension owner
  `.rels` 挂回，以及默认构建拒绝 compressed input；在
  `FASTXLSX_ENABLE_MINIZIP_NG=ON` / `FASTXLSX_TEST_HAS_MINIZIP_NG=1` 下还覆盖
  真实 method 8 DEFLATE entry 读取、DEFLATE unknown part / owner `.rels` ingestion、
  以及损坏 DEFLATE payload 在 `read_entry()` 阶段失败。当前仍拒绝 data descriptor、
  encrypted input、Zip64 和 multi-disk。
- 当前 `fastxlsx.package_editor` 覆盖内部 existing-package copy/replace、docProps
  generated-small-XML、worksheet replacement calc metadata、linked-object preservation
  和 comments / threaded comments / persons / sharedStrings / styles / chart / VBA project preservation
  与 same-path ordering output-plan 状态卫生结构测试；default-typed media
  replace-then-remove final-removal 还应检查 aggregate `planned_output()` 的单个
  removed part、target/reason/inbound audit、drawing inbound relationship、
  content types / drawing `.rels` copy-original、empty removed_package_entries
  和 no invented media owner `.rels`；percent-decoded drawing replace-then-remove
  final-removal 还应检查 aggregate `planned_output()` 的单个 removed part、
  target/reason/inbound audit、encoded worksheet inbound relationship、content types
  rewrite、empty removed_package_entries 和 no invented owner `.rels`。
  当前还覆盖 internal
  `replace_worksheet_sheet_data()`：只替换既有 worksheet XML 的 `<sheetData>` /
  `<sheetData/>`，保留 dimension、sheetViews、sheetProtection、protectedRanges、
  autoFilter、mergeCells、dataValidations、conditionalFormatting、hyperlinks、
  ignoredErrors、drawing、legacyDrawing、tableParts 和 extLst 等外围 worksheet metadata，
  并验证这些保留的 worksheet-local
  ranges/references 会写入内部 `EditPlan` audit notes。当前还验证输出包中
  保留的 worksheet `.rels` legacyDrawing `rId7` target
  `../drawings/vmlDrawing1.vml#shape1` 可由 `PackageReader` / `RelationshipGraph`
  重读；这仍是 preservation 证据，不是 VML/drawing 编辑。invalid/malformed replacement XML、
  source worksheet 缺失 `sheetData`，以及 source worksheet `<sheetData>` 起始标签存在但
  闭合标签损坏/缺失时，不污染 EditPlan / manifest / package-entry audit / calc policy /
  输出 bytes；这不是 XML repair。当前还覆盖先排队 worksheet replacement 再执行 sheetData patch 的组合回归，
  验证 helper 基于当前 planned worksheet bytes 替换，覆盖 queued worksheet 中普通
  `<sheetData>` 和 self-closing `<sheetData/>` 两种形态、保留 queued wrapper metadata，
  且不复活 source-only worksheet metadata。当前还覆盖源 worksheet 使用 self-closing
  `<sheetData/>` 的成功替换回归：输出改为普通 `<sheetData>...</sheetData>`，
  保留 dimension / autoFilter，沿默认 calcChain remove / fullCalcOnLoad 路径清理
  stale 计算 metadata，并保留 unknown bytes。当前还覆盖 replacement payload 自身为
  self-closing `<sheetData/>` 的成功替换回归：可清空旧 row/cell，输出保留
  `<sheetData/>` 和外围 dimension / autoFilter，并继续沿默认 calcChain remove /
  fullCalcOnLoad 与 unknown bytes preservation 路径。当前还覆盖 source worksheet
  和 replacement payload 使用 `<x:worksheet>` / `<x:sheetData>` 前缀形式时的成功替换：
  按 local-name 匹配，输出保留原 wrapper / replacement 字面前缀，仍沿默认 calcChain
  cleanup 与 unknown bytes preservation 路径；这不是通用 namespace repair。这不是 random cell editing、sharedStrings/styles/table migration、
  relationship repair 或 public API。当前完整 worksheet replacement relationship-id
  audit 还覆盖 `<pageSetup r:id>` 的 printerSettings 边界：missing id 和 type mismatch
  会进入 EditPlan / planned output notes 与结构化
  `WorksheetRelationshipReferenceAudit`，输出不会合成 printerSettings relationship 或
  printerSettings part；这不是打印设置语义编辑、relationship repair、orphan cleanup
  或 public API。当前 relationship-id audit 还覆盖 namespace 过滤：只有绑定到
  officeDocument relationships namespace 的 `*:id` 会被当作引用；非 `r` 前缀可用，
  错误 namespace 的 `x:id` 会被忽略；这不是 namespace validation、XML repair、
  relationship repair/pruning 或 public API。当前 planned workbook catalog 回归还覆盖
  by-name sheetData patch 接受绑定到 officeDocument relationships namespace 的非 `r`
  前缀 sheet id attribute，并把错误 namespace 的 `x:id` 和普通 `id` 当作缺失且不污染
  已排队状态；reader-only 回归还直接覆盖内部 `PackageReader::workbook_sheets_from_xml()`
  planned workbook XML 入口只暴露直接 `<sheets><sheet>`、忽略 catalog 外或嵌套
  decoy sheet，接受同样的替代前缀规则，并把错误 namespace 或普通 `id` 当作缺失，
  且拒绝缺失的 planned relationship id 和解析到未注册 worksheet part 的 planned relationship。
  当前 planned workbook catalog 回归还覆盖 namespace 合法但 workbook `.rels` 缺失的
  planned sheet relationship id，以及通过 workbook `.rels` 指向未注册 worksheet part
  的 planned sheet relationship；PackageEditor 的完整 worksheet by-name replacement
  和 by-name `sheetData` helper 都会在状态变更前失败且不污染 EditPlan、manifest、
  calc policy、package-entry audit 或输出 bytes。sheet catalog rename 回归也覆盖同样的
  坏 planned workbook catalog，要求失败前保留 queued workbook replacement、
  EditPlan / audit、manifest、calc policy、package-entry audit 和输出 bytes。
  当前 source workbook catalog 回归还覆盖 workbook `.rels` 中缺失的 sheet relationship
  id，以及 worksheet relationship 指向未注册 worksheet part 的失败路径，并验证
  PackageEditor by-name helpers 在状态变更前失败且不污染 EditPlan、manifest、calc policy、
  package-entry audit 或输出 bytes。
  当前还覆盖 sheetData Patch 下 worksheet-owned printerSettings
  opaque part preservation：保留 `<pageSetup r:id>`、worksheet `.rels` 中的
  printerSettings relationship、`xl/printerSettings/printerSettings1.bin` bytes、
  content type override，以及 EditPlan / planned output 的 relationship context；
  这不是打印设置语义编辑、relationship repair/pruning、orphan cleanup、content
  type repair、public API 或完整 object lifecycle 支持。当前还覆盖
  worksheet-owned OLE/control object part preservation、explicit removal inbound audit
  和 same-path ordering：`sheetData` patch 会保留 OLE/control 引用、worksheet
  `.rels`、`xl/embeddings/oleObject1.bin`、`xl/ctrlProps/control1.xml` 和 content type
  overrides；显式 removal 会省略目标 part、移除对应 override、保留 inbound
  worksheet relationship，并在 EditPlan / planned output 暴露结构化 inbound audit；
  remove-then-replace OLE 会清理 stale removed-part audit 并恢复 content types
  copy-original 状态，replace-then-remove control 会清理 active replacement 并记录最终
  removed-part inbound audit；还应检查 aggregate `planned_output()`：OLE restore
  状态暴露 active OLE `LocalDomRewrite`、content types metadata /
  `ContentTypes` audit、sibling control preservation、无 stale removals、
  无 relationship target audits、fullCalcOnLoad=false、
  `CalcChainAction::Preserve` 且不发明 OLE owner `.rels`；control final-removal
  状态暴露 omitted control part、removed-part inbound audit、content types metadata
  rewrite、sibling OLE preservation、无 relationship target audits、
  fullCalcOnLoad=false、`CalcChainAction::Preserve` 且不发明 control owner
  `.rels`。这不是 OLE / ActiveX / control 语义编辑、relationship
  repair/pruning、orphan cleanup、content type repair、public API 或完整 object
  lifecycle 支持。linked-object VML drawing same-path output-plan 回归还应检查：
  remove-then-replace restore 状态暴露 active VML drawing `LocalDomRewrite`、
  content types copy-original、preserved package/workbook/worksheet/drawing
  relationships、preserved linked/unknown entries、empty removed_parts /
  removed_package_entries，且不发明 VML owner `.rels`；replace-then-remove
  final-removal 状态暴露 omitted VML drawing part、removed_parts
  target/reason/inbound audit、URI-qualified worksheet inbound relationship
  metadata、content types rewrite、empty removed_package_entries，且不发明 VML
  owner `.rels`。这不是 VML shape editing、relationship repair/pruning、orphan
  cleanup、content type repair、public API 或完整 drawing support。当前还要覆盖
  worksheet-owned background picture / header-footer
  VML object part preservation、explicit removal inbound audit 和 same-path ordering：
  `sheetData` patch 会保留 `<picture>` / `<legacyDrawingHF>` 引用、worksheet
  `.rels`、`xl/media/background.png`、`xl/drawings/vmlDrawingHF1.vml`、PNG default
  和 VML content type override；显式 removal 会省略目标 part、保留 inbound
  worksheet relationship，并验证 PNG default 不提升为 override、VML override 被移除；
  remove-then-replace background picture 会清理 stale removed-part audit、恢复 active
  replacement 并保持 content types source-copy 状态，replace-then-remove header/footer
  VML 会清理 active replacement 并记录最终 removed-part inbound audit；还应检查
  aggregate `planned_output()`：background restore 状态暴露 active picture
  `LocalDomRewrite`、content types metadata copy-original、sibling
  header/footer VML preservation、无 stale removals、无 relationship target audits、
  fullCalcOnLoad=false、`CalcChainAction::Preserve` 且不发明 picture owner
  `.rels`；header/footer VML final-removal 状态暴露 omitted VML part、removed-part
  inbound audit、content types metadata rewrite、sibling background-picture
  preservation、无 relationship target audits、fullCalcOnLoad=false、
  `CalcChainAction::Preserve` 且不发明 VML owner `.rels`。这不是图片/VML/
  header-footer 语义编辑、relationship repair/pruning、orphan cleanup、content type
  repair、public API 或完整 object lifecycle 支持。遇到 malformed relationship target
  的 removed-part inbound audit 回归还应同时检查 EditPlan / planned output notes、
  omitted target、copy-original metadata entry、无 package-entry rewrite/omission 和
  calc policy 不变，不应新增结构化 relationship target / worksheet reference audit。
  malformed workbook metadata / workbook calc metadata、invalid replacement、
  metadata-entry replacement 和 invalid removal
  no-state-pollution 回归，以及 inbound-linked removal `ReferencePolicyAction::Fail`
  回归（含已有 ordinary workbook replacement 排队后的失败），还应检查 aggregate `planned_output()` 和 legacy
  output-entry preview 保持 source copy-original 快照；其中 invalid replacement、
  metadata-entry replacement、invalid removal 和 inbound-linked removal `ReferencePolicyAction::Fail`
  还应检查 worksheet/workbook payload
  audit、removed audit 和 calc policy 快照。source-overwrite / output-path
  guard 回归还应检查 exact/path-equivalent、empty-output、missing-parent、non-directory-parent 或 existing-directory `save_as()` 失败后
  queued part replacement、structured audit snapshots、calc policy 和 removal audits
  仍保留在 EditPlan、manifest 和 planned output 中，且后续安全 `save_as()`
  仍可写出 queued rewrite 并保留 untouched worksheet / unknown bytes；同一 guard
  还应覆盖 queued worksheet replacement 的 fullCalcOnLoad / calcChain removal /
  package-entry audit / planned output 状态，且后续安全输出仍按计划省略 calcChain；
  基础 linked worksheet policy failure 应检查 aggregate `planned_output()` 保持
  source copy-original 快照，且 worksheet/workbook payload audit、removed audit 和
  calc policy 不污染；
  queued ordinary workbook replacement 后的 linked worksheet policy failure 也应检查
  workbook rewrite、source-owned `.rels` audit 和 preserved source entries 的 aggregate
  `planned_output()` 快照；
  queued core/app docProps 后的 linked worksheet policy failure 也应检查
  generated docProps、content types、package relationships 和 preserved source entries
  的 aggregate `planned_output()` 快照；
  这只是
  reader-backed copy guard，不是 atomic in-place editor。worksheet rewrite preservation fixture 现在还覆盖
  threaded comments / persons，以及 pivot table / pivot cache 链：
  `xl/pivotTables/pivotTable1.xml`、`xl/pivotCache/pivotCacheDefinition1.xml`、
  `xl/pivotCache/pivotCacheRecords1.xml`、source worksheet `.rels`、pivot table owner
  `.rels`、pivot cache definition owner `.rels` 和 workbook `.rels` 的 copy-original
  保留与 `PackageReader` / `RelationshipGraph` 重读。这不是 pivot table 编辑、
  pivot cache rebuild、relationship repair、orphan cleanup 或 public API。当前还覆盖
  workbook external links 小 fixture：worksheet rewrite 在重写 `xl/workbook.xml`
  calc metadata 时保留 workbook `<externalReferences>`、workbook `.rels` 中的
  externalLink relationship、`xl/externalLinks/externalLink1.xml`、externalLink owner
  `.rels`、external `externalLinkPath` target、content type override 和 unknown entry，
  并可由 `PackageReader` / `RelationshipGraph` 重读。这不是 external links 编辑、
  外部数据刷新、路径校验、relationship repair、orphan cleanup 或 public API。当前还覆盖
  custom XML 小 fixture：worksheet rewrite 保留 package `_rels/.rels` 中的 customXml
  relationship、`customXml/item1.xml`、custom XML item owner `.rels`、
  `customXml/itemProps1.xml`、custom XML properties content type override 和 unknown
  entry，并可由 `PackageReader` / `RelationshipGraph` 重读。这不是 custom XML 编辑、
  schema/data binding、relationship repair、orphan cleanup 或 public API。
- `Catch2` 和 `Google Benchmark` 是 planned-dev 依赖，尚未接入 CMake。
- GitHub Actions workflow 保留默认 vcpkg-backed `windows-nmake-release` job，并有
  opt-in vcpkg matrix job 跑 `windows-nmake-release-minizip`。CI 只做 configure/build/CTest；Excel COM、
  openpyxl / XlsxWriter 参考脚本和 benchmark 仍是本机或手工 QA。

## 单元测试优先级

优先补小而确定的测试：

- XML escape。
- 单元格引用生成。
- 数字/日期/布尔/字符串编码，包括非有限数字、row height 正数/有限值要求和
  column width 正数/有限值要求的拒绝路径。
- `inlineStr` 和 `sharedStrings` 策略。
- worksheet metadata：列宽、冻结窗格、自动筛选、合并单元格、data validations、
  external/internal hyperlinks、two-/three-color conditional color scales、basic data bars、
  basic 3Arrows icon sets 和 tables。
- dimension tracking。
- Excel 行/列边界正向和拒绝路径；行上限测试不要真实写满 1,048,576 行进默认
  CTest，合法最后一行可以用 test-only hook 做稀疏结构覆盖。
- row buffer 复用不变量。
- OPC content types 和 relationships。
- part index 与未修改 part 保留。

普通单元测试不要混入大型 benchmark，并遵守 60s 核心测试边界。

## 测试文件职责边界

- `tests/test_streaming_writer.cpp` 保留 streaming writer 主流程、核心边界和跨功能集成。
- 当某个 feature 已有大量结构断言、负例、参考样例或独立 QA helper 时，优先拆成
  feature-specific 测试文件，例如 conditional formatting、data validations、
  hyperlinks、tables、images、styles 或 sharedStrings。
- 集成测试只保留 suffix 顺序、relationship id、content type side effects、
  package side effects 和多对象共存等真正跨模块行为。
- QA helper 应保持 feature-specific；跨功能 helper 只用于验证真正跨对象的 package
  或兼容性行为。
- 不要为了少量小测试强行拆分；新增测试 target 或源文件时必须同步更新
  `tests/CMakeLists.txt`。

## 兼容性 QA

Phase 1 质量不只是“能编译”。生成的 `.xlsx` 应检查：

- ZIP package 结构。
- `[Content_Types].xml`。
- relationships。
- `docProps/core.xml` 和 `docProps/app.xml`。
- configurable document properties 场景还要检查 creator、lastModifiedBy、title、
  subject、description、keywords、category、Application、AppVersion，以及是否没有
  意外的 `docProps/custom.xml`。
- workbook part。
- worksheet part。
- 基础 `sheetData`。
- 本机有 Excel 时，必须用 Excel 打开关键样例做可视化验证。
- 当前 Phase 3 metadata 推荐 preset 样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-phase3-metadata.xlsx`；
  本机 Excel COM 已验证 `Metadata` sheet、`B2` / `C2` 公式、row 2 高度、A/C
  列宽、`B2:D4` auto filter、`A3:B3` / `C4:D4` merge areas，以及
  `SplitRow=2` / `SplitColumn=3` frozen panes；本机 `openpyxl` 3.1.2 已读取
  `calcId=124519` 和 `fullCalcOnLoad=True`，并确认无 `xl/calcChain.xml`。不要据此宣称
  公式计算、cached values、calcChain、styles 或完整 Phase 3。
  固定本地 QA 入口是 `tools/verify_phase3_metadata.py` 和
  `tools/verify_phase3_metadata_excel.ps1`；它们分别做拆包 XML / `openpyxl`
  检查和 Excel COM 只读可视化检查，不接入默认 CTest/CI。
- 当前 P9 number-format + wrap-text + limited horizontal/vertical alignment +
  bold/italic/direct-color font + solid fill styles 推荐 preset 样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-number-formats.xlsx`，
  sharedStrings + styles 样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-shared-strings.xlsx`，
  limited alignment 样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-alignment.xlsx`，bold/italic
  font 样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-fonts.xlsx`，solid fill 样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-fills.xlsx`。
  结构测试必须覆盖 `xl/styles.xml`、styles content type override、workbook styles
  relationship、custom `numFmts` / `cellXfs`、worksheet `s="N"`、默认 `s="0"` 省略、
  alignment-only style 不创建 custom `numFmt`、`applyAlignment="1"` /
  `<alignment wrapText="1"/>`、`horizontal="left|center|right"`、
  `vertical="top|center|bottom"`、combined alignment attributes、
  number format + alignment 复用 custom `numFmtId`、
  font-only style 不创建 custom `numFmt`、`<fonts>`、`<b/>`、`<i/>`、direct
  `<color rgb="..."/>`、`fontId` 复用、`applyFont="1"`、number format + bold/color
  复用 custom `numFmtId` 和 `fontId`、
  fill-only style 不创建 custom `numFmt`、solid `<patternFill>`、`fgColor rgb`、`fillId` 复用、
  `applyFill="1"`、number format + fill / font + fill 复用 `fillId`、
  sharedStrings + styles 共存，以及 foreign `StyleId` 数值碰撞也在 row state 变更前
  被拒绝。固定本地 QA 入口是 `tools/verify_styles_number_formats.py` 和
  `tools/verify_styles_excel.ps1`；它们分别做拆包 XML / `openpyxl` / 可选
  `XlsxWriter` 检查和 Excel COM 只读 NumberFormat / WrapText /
  HorizontalAlignment / VerticalAlignment / Font.Bold / Font.Italic / Font.Color /
  Interior.Pattern / Interior.Color
  检查，不接入默认
  CTest/CI，也不是运行时依赖。当前 direct font color QA 以拆包 `styles.xml` 的
  direct ARGB `<color rgb="..."/>`、`openpyxl` 读取的 `font.color.rgb` 和 Excel COM
  `Font.Color` 三层结果为准。
- 在可用时验证 Excel / WPS / LibreOffice 能打开。
- 当前 Phase 1 smoke 样例通常位于测试工作目录；推荐 preset 路径是
  `build/windows-nmake-release/tests/fastxlsx-phase1-minimal.xlsx`。旧
  `build-nmake/tests/*.xlsx` 可能是过期 artifact，人工验证前必须确认重新生成。

编辑已有文件时，还要验证未修改 part 被保留，尤其是图表、图片、宏和未知扩展。

## Excel 可视化和 XML 对比

遇到 `.xlsx` 打不开、Excel 修复弹窗、单元格显示不对或结构测试失败时，
按 `docs/TESTING_WORKFLOW.md` 的流程排障。固定思路是三层验证：本机 Excel/WPS/
LibreOffice 只读打开做可视化 smoke，`openpyxl` / `XlsxWriter` 生成或读取参考
workbook，最后拆包对比 FastXLSX 输出和参考文件的 OpenXML 语义：

1. 用本机 Excel 创建语义等价的参考 `.xlsx`，或用 `openpyxl` / `XlsxWriter`
   创建参考文件。
2. 将 FastXLSX 输出和参考文件都复制为 `.zip` 并解压。
3. 对比 `[Content_Types].xml`、`_rels/.rels`、`docProps/core.xml`、
   `docProps/app.xml`、`xl/workbook.xml`、`xl/_rels/workbook.xml.rels`、
   `xl/worksheets/sheet*.xml`。
4. 如果涉及 shared strings 或 styles，再对比 `xl/sharedStrings.xml` 和 `xl/styles.xml`。
5. 如果涉及 data validations，重点对比 worksheet XML 中的 `<dataValidations>`、
   `count`、`sqref`、`type`、`operator`、`allowBlank`、`formula1`、`formula2`，
   以及 `showInputMessage`、`showErrorMessage`、`errorStyle`、`promptTitle`、
   `prompt`、`errorTitle`、`error` 等 prompt/error attributes；涉及隐藏下拉箭头时
   还要检查 `showDropDown="1"`，并记住该 OpenXML attribute 表示隐藏而不是显示。
   multi-area `sqref`
   还要对比空格分隔 token、`count` 是否仍按 dataValidation 元素计算，以及是否没有
   未承诺的排序、合并、去重或重叠检查行为。
6. 如果涉及 external hyperlinks，重点对比 worksheet `<hyperlinks>`、
   `xl/worksheets/_rels/sheet*.xml.rels`、relationship `Type`、`Target`、
   `TargetMode="External"`、worksheet-local `rId`、workbook relationships 是否未污染，
   以及 `[Content_Types].xml` 是否只依赖 `.rels` default。
7. 如果涉及 internal hyperlinks，重点对比 worksheet `<hyperlinks>` 中的 `location`
   attribute、是否没有 `r:id`、internal-only sheet 是否没有
   `xl/worksheets/_rels/sheet*.xml.rels` 和 `xmlns:r`，mixed external/internal 场景下
   internal 是否不消耗 worksheet-local `rId`，以及 workbook relationships /
   `[Content_Types].xml` 是否未污染。
8. 如果涉及 hyperlink display/tooltip，重点对比 worksheet `<hyperlink>` 的
   `display` / `tooltip` attributes、XML attribute escape、display-only、tooltip-only、
   显式空 options 是否省略、`.rels` 是否只保留 target relationship、cell text 是否未被替换，
   以及是否没有 `styles.xml` / content type side effects。
9. 如果涉及图片对象 external hyperlink metadata，重点对比 drawing XML 中
   `xdr:cNvPr/a:hlinkClick`、tooltip attribute、`r:id` 引用、`xl/drawings/_rels/drawing*.xml.rels`
   中的 hyperlink relationship、`TargetMode="External"` 和 target URL；同时确认
   worksheet XML 没有 `<hyperlinks>`，worksheet `.rels` 只承载 drawing/table 等对象关系，
   不生成 cell text、hyperlink style、workbook relationship 或 content type override。
10. 如果涉及 tables，重点对比 `xl/tables/table*.xml`、worksheet `<tableParts>`、
   `xl/worksheets/_rels/sheet*.xml.rels`、relationship `Type` / `Target`、
   table content type override、table `id` / `name` / `displayName` / `ref`、
   `autoFilter`、`tableColumns`、`tableStyleInfo`、`totalsRowShown` /
   `totalsRowCount`、caller-supplied `totalsRowFunction` / `totalsRowLabel`，并确认
   不会意外生成公式文本、空 label attribute 或 `xl/styles.xml`；table overlap 只检查
   同一 worksheet 内 table-vs-table range overlap，不扩展到与 validations、images、
   merged ranges 或 autoFilter 的通用冲突检查。
11. 不要求 byte-level 完全一致，重点比较 OpenXML 语义、关系、content type、
   cell reference、cell type 和 value。

Python XLSX 库只作为测试/排障参考，不是 FastXLSX 运行时依赖。
当前本地 table overlap QA helper 是：
```powershell
py tools\verify_table_overlap_metadata.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-table-range-overlap.xlsx
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_table_overlap_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-table-range-overlap.xlsx
```
第一个脚本检查 package XML、`openpyxl` table ranges、无 `table5.xml` 和无
`xl/styles.xml`；第二个脚本用本机 Excel COM 只读打开 workbook，确认相邻 tables 和
跨 worksheet 相同 range table 可见。它们是本地 QA/排障工具，不接入默认 CTest。
当前已有本地 sharedStrings QA helper：

```powershell
py tools\verify_shared_strings_reference.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings.xlsx
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_shared_strings_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings.xlsx
```

第一个脚本检查 FastXLSX sharedStrings package 关键 entry、content type、relationship、
worksheet `t="s"` index、`xl/sharedStrings.xml` count/uniqueCount 和当前 smoke 值，
并创建 `openpyxl` 参考 workbook；本机 `py` 当前还能创建 `XlsxWriter` 参考 workbook，
但 bundled Python 缺少 `xlsxwriter` 时会明确跳过该分支。第二个脚本用本机 Excel COM
只读打开样例并核对 `Shared!A1:D3` 可见值。它们是本地 QA/排障工具，不接入默认 CTest，
也不是运行时依赖。

如果只改 sharedStrings 空表边界，默认 CTest 的结构断言是主验证：确认 no-string-cell
样例没有 `xl/sharedStrings.xml`、sharedStrings content type、workbook relationship、
worksheet `t="s"` 或 `inlineStr`。已有 sharedStrings smoke 仍应跑上述 Python/Excel
本地 QA，防止正常有字符串路径回退。
当前空表样例也有固定本地 QA helper：
```powershell
py tools\verify_shared_strings_absence.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings-empty-table.xlsx `
  --work-dir build\qa\shared-strings-absence
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_shared_strings_absence_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings-empty-table.xlsx
```

第一个脚本检查 ZIP absence 语义、`openpyxl` 可读值/公式，并在可用时创建
`XlsxWriter` 参考 workbook；第二个脚本用本机 Excel COM 只读打开并核对
`NoStrings!A1:C1`。它们是本地 QA/排障工具，不接入默认 CTest，也不是运行时依赖。

当前已有本地 data validation prompt/error QA helper：
```powershell
py tools\verify_data_validation_prompts.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-prompts.xlsx `
  --multi-range-input build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-multi-range.xlsx
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_data_validation_prompts_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-prompts.xlsx `
  -MultiRangePath build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-multi-range.xlsx
```

Python helper 检查 package XML、prompt/error attributes、`showDropDown`、无 `.rels`
/ `styles.xml` / content type 副作用，并创建 `openpyxl` / `XlsxWriter` 参考 workbook；
它还检查 multi-range 样例的 package XML、`sqref` token、`count` 语义和 `openpyxl`
多区域读取，并创建 `openpyxl` multi-range 参考 workbook。Excel helper 只读打开
workbook 并核对 `ValidationPrompt!A2:D2` 的 validation 属性；`showDropDown="1"`
对应 Excel COM `Validation.InCellDropdown = False`。它还确认 multi-range 样例中
A/C/E 三段 validation areas。Excel COM 会把 custom validation 公式返回为带 `=` 的形式，
结构语义仍以拆包后的 worksheet XML 为准。

当前已有本地 conditional color scale QA helper：
```powershell
py tools\verify_conditional_formatting_color_scales.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-two-color-scale.xlsx `
  --metadata-order-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-metadata-order.xlsx `
  --three-color-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-three-color-scale.xlsx `
  --multi-range-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-multi-range.xlsx `
  --priorities-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-priorities.xlsx `
  --work-dir build\qa\conditional-formatting-color-scales
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_conditional_formatting_color_scales_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-two-color-scale.xlsx `
  -ThreeColorPath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-three-color-scale.xlsx `
  -MultiRangePath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-multi-range.xlsx
```

Python helper 检查 package XML、suffix 顺序、relationship id 不偏移、multi-range `sqref`、
priority、无 styles/dxfs/rels/content type/calcPr 副作用，并用 `openpyxl` 读取 basic
、three-color 和 multi-range 条件格式；可用时会用 `XlsxWriter` 创建参考 workbook。
Excel helper 只读打开 two-color、three-color 和 multi-range workbook，核对 color scale
规则和多区域 AppliesTo。它们是本地 QA/排障工具，不接入默认 CTest/CI，也不是运行时依赖。

当前已有本地 conditional data bar QA helper：
```powershell
py tools\verify_conditional_formatting_data_bars.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar.xlsx `
  --metadata-order-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar-metadata-order.xlsx `
  --multi-range-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar-multi-range.xlsx `
  --priorities-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar-priorities.xlsx `
  --work-dir build\qa\conditional-formatting-data-bars
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_conditional_formatting_data_bars_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar.xlsx `
  -MetadataOrderPath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar-metadata-order.xlsx `
  -MultiRangePath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar-multi-range.xlsx
```
Python helper 检查 package XML、`openpyxl` 读取和可选 `XlsxWriter` reference；
Excel helper 只读打开 basic 和 multi-range workbook，核对 data bar、bar color、
ShowValue 和多区域 AppliesTo；metadata-order workbook 应核对 `ShowValue=False`。
它们是本地 QA/排障工具，不接入默认 CTest/CI，也不是运行时依赖。

当前已有本地 conditional icon set QA helper：
```powershell
py tools\verify_conditional_formatting_icon_sets.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set.xlsx `
  --metadata-order-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-metadata-order.xlsx `
  --percentile-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-percentile.xlsx `
  --multi-range-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-multi-range.xlsx `
  --priorities-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-priorities.xlsx `
  --work-dir build\qa\conditional-formatting-icon-sets
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_conditional_formatting_icon_sets_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set.xlsx `
  -MetadataOrderPath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-metadata-order.xlsx `
  -PercentilePath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-percentile.xlsx `
  -MultiRangePath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-multi-range.xlsx
```
Python helper 检查 package XML、`openpyxl` 读取和可选 `XlsxWriter` reference；
Excel helper 只读打开 basic、metadata-order、percentile 和 multi-range workbook，
核对 icon set、IconSet ID、Percent / Percentile criteria、ShowIconOnly、
ReverseOrder 和多区域 AppliesTo。它们是本地 QA/排障工具，不接入默认 CTest/CI，
也不是运行时依赖。

## ZIP backend 验证

OpenXML 结构测试应比较解压后的 package entries 和 XML 语义，不要断言 ZIP
central directory method 必须是 `0`。默认构建的 stored bootstrap 会写 method 0；
`FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建会写 method 8（DEFLATE）。二者 entry 顺序、
压缩大小和 archive size 可以不同。
当前 FastXLSX minizip writer 会关闭 data descriptor，以便 current
`PackageReader` DEFLATE fixture 仍有可校验的 local header size/CRC；这不是对任意
data-descriptor ZIP input 的支持。

验证 opt-in minizip backend 时运行：

```powershell
cmake --preset windows-nmake-release-minizip
cmake --build --preset windows-nmake-release-minizip
ctest --preset windows-nmake-release-minizip
```

验证默认 stb image 路径时运行：

```powershell
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

file-backed/chunked entry 测试也必须走 decompressed package semantics，不要断言
entry source、entry order、compressed size、archive size 或 chunk 边界。测试可以断言
duplicate entry 被拒绝，以及 worksheet XML 内容没有因 chunk 边界被截断或重排。

## 图片和复杂对象验收

当前完整图片 OpenXML 读取/插入仍是 Phase 5；但
`WorksheetWriter::add_image()` 已有 streaming-only new-workbook PNG/JPEG path 和
memory-source 基础插入切片。`read_image_info()` 只表示 PNG/JPEG 图片元数据 helper 已有默认 `stb`
路径；`read_image_pixels(path/span)` 只表示 caller 显式请求时会分配完整 owned decoded
pixel buffer 的 PNG/JPEG 像素 helper。不要把这些 helper 写成 existing-workbook 图片保真、
drawing 编辑、格式转换、压缩、裁剪、旋转或完整图片支持已完成。
`WorksheetWriter::add_image()` 的质量门禁仍应验证它复制原始 PNG/JPEG bytes 到
file-backed media entry，不调用像素解码，不把图片 bytes 或 decoded pixels 放入 row/cell
streaming 热路径。
图片结构测试至少检查：

- `xl/media/image*.png|jpg|jpeg` 存在。
- `xl/drawings/drawing*.xml` 存在。
- `xl/drawings/_rels/drawing*.xml.rels` 指向 media part。
- `xl/worksheets/_rels/sheet*.xml.rels` 指向 drawing part。
- worksheet XML 中 drawing `r:id` 与 worksheet rels 一致。
- `[Content_Types].xml` 有图片格式 default 或 override。
- 如果涉及 image metadata，还要检查 drawing XML 中 `xdr:twoCellAnchor editAs`、
  `xdr:cNvPr name` / `descr`、XML attribute escape、空值省略和默认名称规则。
- 如果涉及图片对象 external hyperlink metadata，还要检查 drawing XML 中
  `xdr:cNvPr/a:hlinkClick`、tooltip attribute、drawing `.rels` hyperlink relationship、
  `TargetMode="External"`、relationship id 引用一致性，并确认不写 worksheet
  `<hyperlinks>`、不创建 cell text、styles、workbook relationship 或 content type
  side effects。
当前 JPEG 结构测试还应覆盖 `.jpg` media entry、`image/jpeg` content type default、
drawing intrinsic EMU 尺寸和 drawing `.rels` target。
混合 PNG/JPEG 测试还应覆盖同一个 worksheet 内多图片共享一个 drawing part、
多个 `<xdr:twoCellAnchor>`、全局 media 编号和 drawing-owner-local relationship id。
多对象 relationship id 回归还应覆盖同一 worksheet 内多个 external hyperlink、
一个 drawing 和多个 table 的 worksheet-owner-local `rId` 顺序，以及跨 worksheet
owner 和 drawing owner 的 relationship id 重置。
最大合法 anchor marker 测试应验证 Excel 最大合法行列边界序列化为 drawing XML 的
0-based marker，例如 `<xdr:col>16383</xdr:col>` 和
`<xdr:row>1048575</xdr:row>`。
当前推荐样例是
`build/windows-nmake-release/tests/fastxlsx-streaming-images.xlsx`。
当前 memory-source image 推荐样例是
`build/windows-nmake-release/tests/fastxlsx-streaming-memory-images.xlsx`。
当前 image metadata 推荐样例是
`build/windows-nmake-release/tests/fastxlsx-streaming-image-metadata.xlsx`；
本地 QA 可运行 `tools/verify_image_metadata.py` 做 package XML / openpyxl /
XlsxWriter 检查，并运行 `tools/verify_image_metadata_excel.ps1` 做 Excel COM
shape name / AlternativeText / Placement / marker-offset geometry 检查。当前这两个
helper 还支持 basic image 和 mixed-object relationship 样例参数：
`--basic-input` / `-BasicPath`、`--mixed-object-input` / `-MixedObjectPath` 与
`--memory-input` / `-MemoryPath`、`--hyperlink-input` / `-HyperlinkPath` 和
`--mixed-hyperlink-input` / `-MixedHyperlinkPath`；memory-source helper 检查 media bytes、package XML、
`openpyxl` smoke、`XlsxWriter` reference 和 Excel COM anchors。`openpyxl`
可能跳过 JPEG 图片读取，JPEG media/drawing 关系以拆包 XML 和 Excel COM 为准。
图片对象 external hyperlink metadata 的质量门禁应分层记录：默认 CTest 覆盖
package 结构和负例；Python helper 拆包检查 drawing XML / drawing `.rels` /
worksheet hyperlink absence，并用 `openpyxl` 做可读 smoke、用 `XlsxWriter` 创建参考
workbook 对比语义；本机 Excel COM 只读打开样例，确认无修复弹窗、shape 可见，并在可行时
核对图片 click hyperlink / tooltip 可见性。Excel UI 行为只作为可视化 smoke，精确语义
仍以拆包 XML 和 relationship 为准。
当前 mixed hyperlink 样例是
`build/windows-nmake-release/tests/fastxlsx-streaming-image-hyperlink-mixed-objects.xlsx`；
Python helper 检查 worksheet cell hyperlink / drawing / table rels、drawing-local
picture hyperlink relationship、cell text 保留和 workbook hyperlink relationship absence；
Excel helper 通过 `-MixedHyperlinkPath` 只读打开并核对 shape count、B2 cell hyperlink、
table count、linked picture hyperlink 和两张图片 anchor。

Anchor 测试要覆盖起始/结束单元格、two-cell marker EMU offset、零尺寸、负尺寸、
越界 anchor、负 offset 和超出 OpenXML coordinate 上界的 offset；不要为了 anchor
计算引入完整 worksheet DOM。图片读取应使用 `stb` 处理解码、尺寸、通道和像素，
但结构测试必须验证 FastXLSX 自己生成的 OpenXML media/drawing package 语义。
这些 anchor 边界测试是结构测试，不是大 sheet 导出性能证明，也不要求真实写满
worksheet 数据。
兼容性测试要用本机 Excel 打开 `.xlsx` 样例，确认无修复弹窗并检查图片显示位置/尺寸；
当前本机 Excel COM 验证结果应记录为 `Images` / `SecondImage` 各 1 个 shape、
`Plain` 为 0 个 shape，锚点 `C1:F5` 和 `A1:B2`。
image metadata 验证结果还应记录 shape count、custom/default shape names、
`AlternativeText`、`Placement` 和首图 marker offset 对 `Shape.Left` / `Top` /
`Width` / `Height` 的影响；但 marker EMU offset 语义仍以拆包后的 drawing XML 为准，
不要宣称完整 UI parity、row/column resize 几何计算或 `oneCellAnchor` /
`absoluteAnchor` 元素支持。
结构异常时用 Excel、`openpyxl` 或 `XlsxWriter` 参考文件拆包对比 XML。已有文件编辑
场景还要证明未修改 drawings、media、charts、macros 和 unknown parts 没有丢失，
relationships 仍指向有效 target。

涉及 public 图片 API 时，测试计划还要检查文档注释是否写清 Streaming/Patch/In-memory
模式、原始图片字节和 decoded pixel buffer 的内存成本、memory-source span 生命周期、
copy-to-temp-file-backed media entry 语义、OpenXML part 副作用、是否触发 DOM，以及
为什么不会破坏 worksheet streaming 热路径。

## Benchmark 优先级

Benchmark 应记录：

- 构建类型。
- 数据规模。
- 压缩等级或 ZIP backend。
- 字符串策略。
- 总耗时。
- 峰值内存。
- 输出文件大小。
- 办公软件打开兼容性。

文档中的对比对象是 `OpenXLSX`、`xlnt` streaming writer 和旧 `FastExcel`。
不要把这些 benchmark 对象变成 FastXLSX 运行时依赖。

当前最小 P6 benchmark 入口是 `FASTXLSX_BUILD_BENCHMARKS=ON` 下的手工工具
`fastxlsx_bench_streaming_writer`。它不使用 Google Benchmark，不注册 CTest，
不进入默认 CI；`planned-dev` 中的 `benchmark` 仍不是当前 CMake 事实。
不传 `--output` / `--result` 时，该工具默认写到 benchmark target 的 binary dir；
`--sheets` 超过 1024 会被拒绝，这是 benchmark 工具护栏，不是 public API 限制。
当前本地矩阵 helper 是 `tools/run_benchmark_matrix.py`，它只包装一个已构建的
benchmark exe，保留每个 case 的 `.xlsx` 和 schema-v3 `.json`，并写聚合
`benchmark-matrix-report.json`。不同 backend 要分别传入 stored/minizip preset 的
benchmark exe；不要让 runner 改写 `office_open`。
本机 Excel sidecar 验证入口是 `tools/verify_benchmark_matrix_excel.ps1`；它读取
`benchmark-matrix-report.json` 中的部分 workbook，只读打开并核对 `Sheet1` UsedRange
和首尾单元格，然后在同目录写 `benchmark-matrix-office-report.json`。该 sidecar 不回写
benchmark schema-v3 JSON，也不改变各 case 的 `office_open=not_run` 字段。
当前 benchmark JSON schema version 为 `3`，会写 `string_pattern`、
`package_entry_source_mode="worksheet-file-backed-chunked"`、
`temporary_worksheet_part_footprint="worksheet-body-file-bytes"` 和数值型
`temporary_worksheet_part_footprint_bytes`。该字段来自 benchmark-only instrumentation，
只累计 worksheet body row XML 写入字节数，不包含 worksheet header/footer、
sharedStrings 临时文件、小型 XML parts、media 文件、ZIP/backend 缓冲、package
assembly 峰值内存或 OS 文件系统开销；不要把它写成完整低内存证据。

当前 `docs/PERFORMANCE_TARGETS.md` 记录了 2026-06-07 本机小规模 sharedStrings
benchmark 快照：`strings` 场景、`50000 x 10 x 1 = 500000` cells、
repeated/unique string pattern、inline/shared strategy、stored-bootstrap ZIP。
四个输出已用本机 Excel COM 只读打开并核对 `Sheet1` 使用范围和首尾值。继续把它
当作 opt-in 手工结果；不要把 `office_open=not_run` 的 JSON 字段写成工具自动完成
Office 验证，也不要把 worksheet-body-only footprint 写成完整 package、完整临时文件
或进程峰值内存 footprint。

重点覆盖：

- 百万行级导出。
- 多 sheet 批量导出。
- 数字/日期/布尔密集写入。
- 字符串密集下的 `inlineStr` 与 `sharedStrings`。
- repeated / unique string pattern 下的 sharedStrings 体积、耗时和峰值内存对比。
- 模板 sheet 替换。
- ZIP 压缩等级。
- package entry source mode：in-memory / file-backed / chunked。
- close-time package assembly peak memory。
- temporary worksheet part footprint。
- 矩阵字段至少记录 preset/backend、scenario、rows、cols、sheets、cells、
  string_ratio、string_pattern、string_strategy、zip_backend、compression、
  package_entry_source_mode、elapsed_ms、peak_memory_mb、output_bytes、
  `temporary_worksheet_part_footprint_bytes` 和 Office/openpyxl 验证状态。

## 排障路径

- `ctest` 跑出 0 个测试：先查 `tests/CMakeLists.txt` 是否仍注册
  `fastxlsx.unit`、`fastxlsx.streaming` 和 `fastxlsx.opc`。
- 生成 XLSX 打不开：先查 package entries、content types、relationships、
  workbook、worksheet、`sheetData`。
- Excel 提示修复：保存修复后的文件，与原输出和参考文件拆包对比 XML。
- 内存异常：优先查 DOM、完整 worksheet matrix、cell map、跨行缓存。
- 内存异常还要查 close-time 是否重新物化完整 worksheet XML、file-backed entry 是否退化为
  in-memory entry、chunk buffer 是否无界增长、临时文件生命周期是否异常。
- 性能回退：优先查 XML 编码、escape、数字转换、cell reference、
  压缩等级、sharedStrings、row buffer。
- XML 结构异常或 Excel 修复时，也要检查是否错误写出了 `nan`、`inf` 或 `-inf`
  这类非法 OpenXML 数字文本。

## 验证命令

按 `docs/DEVELOPMENT_ENVIRONMENT.md` 的生成器建议：

```powershell
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

当前本机可用路径是 VS2026 Developer Command Prompt + NMake；其他机器可用
`cmake --help` 检查是否有更合适的 Visual Studio 2026 生成器。

后台跑普通单元测试时使用 60s 超时；preset 和 `tests/CMakeLists.txt` 已承载该
边界。手写 build dir 时显式加 `ctest --test-dir ... --timeout 60`。大型
benchmark 必须显式 opt-in。
