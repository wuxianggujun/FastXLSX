# FastXLSX Agent Guide

## 项目快照

FastXLSX 是一个 C++20 / MSVC 2026 优先的 XLSX 引擎。项目文档固定的
核心方向是：流式优先、局部 DOM 可选、面向 OpenXML。

当前仓库处于 Phase 1 最小可写 XLSX 的早期实现阶段：

- `CMakeLists.txt` 定义了 compiled `fastxlsx` library。
- 目标别名是 `FastXLSX::fastxlsx`。
- `include/fastxlsx/fastxlsx.hpp`、`include/fastxlsx/workbook.hpp`、
  `include/fastxlsx/streaming_writer.hpp`、`include/fastxlsx/document_properties.hpp`
  和 `include/fastxlsx/image.hpp` 提供当前 public API：
  `Workbook`、`Worksheet`、`Cell`、`DocumentProperties`、`WorkbookWriter`、
  `WorksheetWriter`、`CellView`、`StyleId`、`CellStyle`、`DataValidationRule`、
  `DataValidationType`、`DataValidationOperator`、`DataValidationErrorStyle`、`HyperlinkOptions`、
  `ArgbColor`、`ColorScaleValueType`、`ColorScalePoint`、`TwoColorScaleRule`、
  `ThreeColorScaleRule`、`DataBarValueType`、`DataBarEndpoint`、`DataBarRule`、
  `IconSetStyle`、`IconSetValueType`、`IconSetRule`、
  `TableOptions`、`TableTotalsFunction`、`TableOptions::show_totals_row`、
  `TableOptions::column_totals_functions`、`TableOptions::column_totals_labels`、
  `ImageFormat`、`ImageInfo`、
  `ImageEditAs`、`ImageAnchorOffset`、`ImageOptions`、`read_image_info()`、
  `Workbook::set_document_properties()`、`WorkbookWriterOptions::document_properties`、
  `WorkbookWriter::add_style()`、`CellView::with_style()`、
  `WorksheetWriter::add_conditional_color_scale()`、`WorksheetWriter::add_conditional_data_bar()`、
  `WorksheetWriter::add_conditional_icon_set()`、
  `WorksheetWriter::add_external_hyperlink()`、`WorksheetWriter::add_internal_hyperlink()`、
  `WorksheetWriter::add_image()` 和 `FastXlsxError`。
- `src/image.cpp`、`src/opc.cpp`、`src/package_writer.cpp`、`src/workbook.cpp`、
  `src/streaming_writer.cpp`、`src/xml.cpp`、`src/zip_store_writer.cpp`
  是当前已接入 CMake 的实现入口。
- `include/fastxlsx/detail/opc.hpp` 和 `src/opc.cpp` 是内部 OPC
  manifest / relationships / XML serializer 基础，不代表完整已有 XLSX 编辑能力。
- `tests/test_minimal_xlsx.cpp` 通过 CTest 注册为 `fastxlsx.unit`，覆盖 XML escape、
  cell reference、最小 OpenXML package 结构、基础单元格编码、in-memory 公式 XML
  escape、row height metadata、空 worksheet / 单空行 dimension、`XFD1` 列边界和
  16385 列拒绝路径。
- `tests/test_streaming_writer.cpp` 通过 CTest 注册为 `fastxlsx.streaming`，覆盖
  流式 writer 骨架、公式、行高、列宽、冻结窗格、自动筛选、合并单元格和
  空行 dimension、合法最大列/最大行结构边界、失败 append 不污染状态、
  Excel 行/列上限拒绝路径、data validations / external and internal hyperlinks /
  tables / two-/three-color conditional color scales / basic conditional data bars /
  basic 3Arrows conditional icon sets /
  streaming number-format styles /
  默认 stb image drawing XML 输出。
- `tests/test_opc.cpp` 通过 CTest 注册为 `fastxlsx.opc`，覆盖内部 OPC part name、
  content types、relationships、manifest 和 serializer 基础。
- `tests/test_image.cpp` 通过 CTest 注册为 `fastxlsx.image`，覆盖默认 `stb`
  构建下 PNG/JPEG 文件/内存尺寸和通道读取、unsupported memory/file header、
  empty memory buffer、empty file 和 missing file。
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

当前 P17a 图片元数据切片已存在：默认 vcpkg manifest 依赖 `stb`，CMake 使用
vcpkg installed tree 中的 `share/stb/FindStb.cmake`、
`find_package(Stb MODULE REQUIRED)` 和 `${Stb_INCLUDE_DIR}`。`read_image_info()` 只读取 PNG/JPEG 的尺寸、格式
和通道数；它不生成 media part、drawing XML、relationships、content types、
anchor，也不代表图片插入、图片编辑或 existing-workbook 图片保真。

当前 P17b 图片插入基础切片也已存在：`WorksheetWriter::add_image(path, anchor)`
是 streaming-only new-workbook API，默认接受 PNG/JPEG 文件。它用
`read_image_info()` 验证格式/尺寸/通道，把原始图片字节复制到
临时 file-backed media entry，并在 `close()` 写出 `xl/media/image*.png|jpg`、
`xl/drawings/drawing*.xml`、drawing `.rels`、worksheet `.rels`、worksheet
`<drawing>` 和 content type entries。它不裁剪、不旋转、不压缩、不格式转换、不编辑
已有 drawing，也不代表 existing-workbook 图片保真或完整 drawing 支持。
当前还支持窄 `ImageOptions` metadata：`from_offset` / `to_offset` 是
`ImageAnchorOffset` EMU 值，只写到现有 two-cell anchor marker 的 `xdr:colOff` /
`xdr:rowOff`；`edit_as` 只写成 drawing XML `xdr:twoCellAnchor editAs` attribute；
非空 `name` 会写成 drawing XML `<xdr:cNvPr name="...">`，非空 `description`
会写成 `descr="..."`；空 `name` 保留生成的 `Picture N`，空 `description` 省略。
这些 metadata 只改变 drawing marker metadata 和 non-visual picture properties，
不修改图片二进制、EXIF、media filename、anchor cell range、relationships、
content types 或 worksheet cell text，也不支持 `oneCellAnchor` / `absoluteAnchor`
元素或 row/column resize 几何计算。

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
API，也不是 existing-file editing。`PackageReader`、`CellEncoder` 等名称仍主要
来自架构文档或路线图；只有在 `include/` 或 `src/` 中找到对应实现后，才能当作
真实 API。OPC 当前有 `PartWriteMode` 和 package part edit-state 基础，
当前新建 workbook 写出路径有内部 `PackageEntry` / `write_package` 边界。
worksheet part finalization 已支持 file-backed/chunked package entry，避免在
`WorkbookWriter::close()` 阶段重新物化完整 worksheet XML 字符串。默认 ZIP backend
仍由 stored ZIP bootstrap 支撑；启用 `FASTXLSX_ENABLE_MINIZIP_NG` 时会使用
minizip-ng DEFLATE backend。它仍不是已有文件编辑用 public `PackageWriter`，
也不代表 true package streaming、Zip64、完整低内存或已验证大文件性能。
`PackageReader`、已有文件编辑和 unknown part preservation 仍是计划。当前已有内部
`PartIndex`、`RelationshipGraph` 和 `ContentTypeRegistry` 基础，但这只代表
owner-aware relationship / content type groundwork；不要宣称 package editing、
完整 hyperlink、完整 conditional formatting、完整图片、VBA 或完整 table 支持。

当前 P9a styles number format 基础切片已存在：`WorkbookWriter::add_style(CellStyle)`
会在 workbook state 注册 streaming-only new-workbook 样式，当前只支持自定义
number format；重复 number format 复用同一个 `StyleId`。`CellView::with_style()`
只携带 workbook-local `StyleId` 小句柄，`WorksheetWriter::append_row()` 会在推进
row count、dimension、sharedStrings 或公式重算状态前验证非默认 style id。使用样式时
`close()` 会写 `xl/styles.xml`、styles content type override 和 workbook relationship；
worksheet cell 会写 `s="N"`，默认样式不写 `s="0"`。该切片不支持字体、填充、边框、
对齐、rich text、dxf-backed conditional formatting、date cell type、
existing-file style preservation 或完整 Excel formatting parity。当前 two-/three-color
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
  `50000 x 10 x 1 = 500000` cells。schema v3 结果记录了
  `temporary_worksheet_part_footprint="worksheet-body-file-bytes"`：repeated/inline
  为 `493 ms`、`4.97266 MB`、`27927834` worksheet body bytes、`27931711` output
  bytes；repeated/shared 为 `392 ms`、`4.98828 MB`、`16927834` worksheet body
  bytes、`16932289` output bytes；unique/inline 为 `658 ms`、`4.97266 MB`、
  `30866774` worksheet body bytes、`30870651` output bytes；unique/shared 为
  `1045 ms`、`70.1055 MB`、`19316724` worksheet body bytes、`33260102` output
  bytes。四个输出已用本机 Excel COM 只读打开并核对 `Sheet1` 使用范围和首尾值；
  这只是 stored-bootstrap ZIP 下的小规模趋势快照，不是 sharedStrings 生产就绪、
  默认最佳策略、完整低内存或大文件性能结论。
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
  不要据此宣称 Zip64、true package streaming、existing-file editing、完整低内存或
  大文件性能。
- document properties：基础。`DocumentProperties`、`Workbook::set_document_properties()`
  和 `WorkbookWriterOptions::document_properties` 已支持新建 workbook 的基础
  `docProps/core.xml` / `docProps/app.xml` 配置；当前只覆盖 core/app 小型 XML
  part，不创建 `docProps/custom.xml`，不编辑已有文件，也不代表完整文档属性 API。
  当前本地 QA 入口是 `tools/verify_document_properties.py` 和
  `tools/verify_document_properties_excel.ps1`，覆盖 in-memory / streaming 样例的
  XML、`openpyxl` 和 Excel COM 只读打开 smoke；Excel COM 不稳定暴露的属性以
  拆包 XML / `openpyxl` 结果为准。
- P9 styles number formats：基础。`StyleId` / `CellStyle` /
  `WorkbookWriter::add_style()` / `CellView::with_style()` 已支持 streaming-only
  new-workbook 自定义 number format 样式注册和 cell style 引用；`xl/styles.xml`
  是 workbook-level 小型 XML part，styles relationship 位于 workbook `.rels`，
  不新增 worksheet `.rels`。当前结构测试覆盖 style id 去重、XML attribute escape、
  默认 `s="0"` 省略、sharedStrings + styles 共存、以及非法 foreign `StyleId`
  在污染 row state 前被拒绝。固定本地 QA 入口是
  `tools/verify_styles_number_formats.py` 和 `tools/verify_styles_excel.ps1`，分别做
  拆包 XML / `openpyxl` / `XlsxWriter` 参考检查和本机 Excel COM 只读可视化检查。
  这不是完整 styles、font/fill/border/alignment、date cell type、conditional
  formatting、rich text、existing-file style preservation 或 Excel formatting parity。
- OPC edit plan：基础。当前是 internal manifest、relationships、content types、
  part write-mode、`PartIndex`、`RelationshipGraph` 和 `ContentTypeRegistry`
  基础；已有 XLSX 编辑、未知 part 保真复制和 package rewrite 仍需 reader/writer
  和 preservation 测试闭环。
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
  `edit_as` 轻量枚举和 name/description 字符串复制进 writer state，并在 drawing XML
  写 two-cell marker `xdr:colOff` / `xdr:rowOff`、`xdr:twoCellAnchor editAs` 和
  `xdr:cNvPr` `name` / `descr` attributes；它不代表完整 alt text/accessibility UI、
  图片文件 metadata、existing-workbook 图片编辑或图片保真。

## 先读哪些文件

- 项目定位：`README.md`、`docs/PROJECT_POSITIONING.md`
- 架构与数据流：`docs/ARCHITECTURE.md`、`docs/EDITING_MODEL.md`
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
- 大数据 API 必须面向 row iterator 或 chunk writer，不要为了 API 方便
  持有完整 worksheet cell matrix。
- `FastXmlWriter`、`CellEncoder`、`RowStreamWriter` 是文档中的性能热路径；
  不要在 cell XML 热路径上直接依赖通用 XML serializer。
- public API 必须向性能主线靠齐。不能为了 API 易用性让大型 worksheet
  进入 DOM、完整 cell matrix 或 cell map。
- public API 应写文档注释，说明模式、内存行为、随机访问限制和性能注意事项。

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
当前 benchmark JSON schema version 为 `3`，会记录 `string_pattern`、
`package_entry_source_mode="worksheet-file-backed-chunked"`、
`temporary_worksheet_part_footprint="worksheet-body-file-bytes"` 和数值型
`temporary_worksheet_part_footprint_bytes`。该值来自 benchmark-only instrumentation，
只累计 worksheet body row XML 写入字节数，不包含 worksheet header/footer、
sharedStrings 临时文件、小型 XML parts、media 文件、ZIP/backend 缓冲、
package assembly 峰值内存或 OS 文件系统开销；不能据此宣称完整低内存或大文件性能。
当前 `tools/run_benchmark_matrix.py` 是 opt-in 本地矩阵 runner，只包装一个已构建的
`fastxlsx_bench_streaming_writer` exe 并聚合 schema-v3 JSON；stored/minizip 要分别传入
各自 preset 的 exe 和输出目录。`tools/verify_benchmark_matrix_excel.ps1` 可本机只读打开
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
- 当前 `fastxlsx.streaming` number-format styles 推荐 preset 输出样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-number-formats.xlsx`；
  sharedStrings + styles 共存样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-shared-strings.xlsx`。
  结构测试确认 `xl/styles.xml`、styles content type override、workbook styles
  relationship、custom `numFmtId`、默认 font/fill/border/cell style 骨架、
  worksheet `s="N"` 引用、默认 `s="0"` 省略、sharedStrings relationship 在 styles
  relationship 之前，以及 foreign `StyleId` 失败不会推进 row state 或生成
  `styles.xml`。本地 QA 入口是：
  `py tools\verify_styles_number_formats.py --input build\windows-nmake-release\tests\fastxlsx-streaming-styles-number-formats.xlsx --shared-input build\windows-nmake-release\tests\fastxlsx-streaming-styles-shared-strings.xlsx --work-dir build\qa\styles-number-formats`
  和
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_styles_excel.ps1 -Path build\windows-nmake-release\tests\fastxlsx-streaming-styles-number-formats.xlsx -SharedPath build\windows-nmake-release\tests\fastxlsx-streaming-styles-shared-strings.xlsx`。
  Python helper 会拆包检查 styles XML，并用 `openpyxl` 核对 number format 语义、
  可用时创建 `XlsxWriter` 参考 workbook；Excel helper 用本机 Excel COM 只读打开
  两个样例，核对 NumberFormat、值和公式。不要把这扩展成完整样式系统、date cell
  type、existing-file style preservation 或跨办公软件完整显示等价。
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
  还可通过 `--basic-input` 检查 `fastxlsx-streaming-images.xlsx`，并通过
  `--mixed-object-input` 检查 `fastxlsx-streaming-mixed-object-rels.xlsx` 的 media /
  drawing / worksheet rels / table / hyperlink 关系。`tools/verify_image_metadata_excel.ps1`
  已用 Excel COM 检查 shape name、AlternativeText、`Placement` 映射和首图 marker
  offset 对 `Shape.Left` / `Top` / `Width` / `Height` 的 EMU-to-points 几何影响，
  也会核对基础图片和 mixed-object 样例的 shape / hyperlink / table counts 与 anchors。
  `openpyxl` 可能跳过 JPEG 图片读取；JPEG media/drawing 关系以拆包 XML 和 Excel COM
  为准。该测试确认 metadata 不新增额外 drawing part、worksheet relationships、
  content types 或 media part 语义。
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
  `openpyxl` 打开确认 3 张图片、用 `XlsxWriter` 创建参考 workbook；`tools/verify_image_metadata_excel.ps1`
  用本机 Excel COM 只读打开，确认 3 个 shapes、自定义 `NamedOnly`、默认
  `Picture 3`、首图 `AlternativeText`、`Placement` 映射，以及首图 marker offset 对
  shape 几何的影响。本机已验证 Excel COM 会把 `descr` 映射到
  `Shape.AlternativeText`；结构语义仍以拆包后的 drawing XML 为准。
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
- 把 `src/zip_store_writer.*` 或 `src/package_writer.*` 当成已有文件编辑 API。
  当前 package writer 只是内部边界：默认 backend 是 Phase 1 stored bootstrap，
  opt-in backend 是 minizip-ng/DEFLATE；仍无 Zip64、true package streaming、
  `PackageReader` 或 existing-file preservation。
  即使 worksheet package entry 使用 file-backed/chunked source，也只是内部
  new-workbook output 的 entry-source 优化。
- 把 `stb` 当成完整图片 OpenXML 支持。`stb` 只负责图片解码/尺寸/像素读取；
  当前 `WorksheetWriter::add_image()` 只是 new-workbook PNG/JPEG 插入基础切片，
  不代表 existing-workbook 图片保真、drawing 编辑、裁剪、旋转、压缩或格式转换。
- 把 `ImageOptions::edit_as` 写成 `oneCellAnchor` / `absoluteAnchor` 元素支持，
  或把 `ImageOptions::from_offset` / `to_offset` 写成 row/column resize 几何计算、
  跨办公软件 UI 保证、drawing mutation 或 existing-workbook 图片编辑；或把
  `ImageOptions::name` / `description` 写成完整图片 metadata、完整 alt text UI、
  EXIF/PNG/JPEG metadata、media filename 语义。
- 把第三方源码复制进 `src` 或 `include`。
- 修改 `tests/CMakeLists.txt` 后让 `ctest` 回到 0 测试，或让默认测试超过 60s。
- 把 `FASTXLSX_ENABLE_TEST_HOOKS` 或
  `fastxlsx::detail::testing_set_worksheet_row_count()` 当作用户可用 API；它只服务
  默认单元测试中的低成本边界注入。
- 为了 API 易用性牺牲 streaming 性能主线。
- 允许 `NaN`、`+Inf`、`-Inf`、非正 row height 或非正 column width 写进 XML。
- 把 P9 number-format styles 写成完整样式系统。当前只支持 workbook-local
  `StyleId`、自定义 number format 和 `xl/styles.xml` 基础骨架；不支持字体、填充、
  边框、对齐、rich text、dxf-backed conditional formatting、date cell type、existing-file style preservation
  或完整 Excel formatting parity。
- 把 `WorksheetWriter::add_conditional_color_scale()` /
  `WorksheetWriter::add_conditional_data_bar()` / `WorksheetWriter::add_conditional_icon_set()`
  写成完整 conditional formatting 或 styles/dxfs 支持。当前只写 streaming-only new-workbook
  two-/three-color color scale、basic data bar 与 basic 3Arrows icon set 的 worksheet-local XML；
  不支持公式规则、advanced data bars、advanced/custom icon sets、dxf 样式、冲突检测、
  existing-file editing 或完整 Excel UI。
- 把 `500000` cells 的小规模手工 benchmark 快照写成 1,000 万 cells、大文件性能、
  完整低内存、Google Benchmark 或 sharedStrings 生产就绪结论；也不要把
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
- `fastxlsx-style-registry`：number format styles、`StyleId`、`CellStyle`、`xl/styles.xml`
  和样式 QA 验证。
