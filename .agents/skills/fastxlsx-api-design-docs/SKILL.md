---
name: fastxlsx-api-design-docs
description: "设计或审查 FastXLSX public API、API 文档注释、任务计划和性能边界。用于 Workbook/WorksheetWriter/PackageEditor 类接口、Doxygen 注释、Streaming/Patch/In-memory 模式说明、API 易用性与性能取舍，以及防止便利 API 破坏流式主线。"
---

# FastXLSX API Design Docs

## 必读文件

- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`
- `docs/PERFORMANCE_TARGETS.md`
- `docs/ROADMAP.md`
- `README.md`

再检查 `include/` 和 `src/`，确认 API 是否已经实现。当前已实现的 public API
包括 `Workbook`、`Worksheet`、`Cell`、`DocumentProperties`、`WorkbookWriter`、
`WorksheetWriter`、`CellView`、`DataValidationRule`、`DataValidationType`、
`DataValidationOperator`、`DataValidationErrorStyle`、`ImageEditAs`、`ImageOptions` 和
`FastXlsxError`。
`Workbook::set_document_properties()`
和 `WorkbookWriterOptions::document_properties` 是当前 new-workbook core/app
docProps metadata API；它们只写 `docProps/core.xml` 和 `docProps/app.xml`。
`HyperlinkOptions` 是当前 streaming hyperlink display/tooltip metadata API。
`WorksheetWriter::add_external_hyperlink()` 是当前 external URL
worksheet hyperlink API；`WorksheetWriter::add_internal_hyperlink()` 是当前 internal
workbook location worksheet hyperlink API；`WorksheetWriter::add_table()` 和 `TableOptions` 是当前
streaming-only table API。`WorkbookWriter` / `WorksheetWriter` / `CellView`
是流式写入骨架；data validation、external/internal hyperlink 和 table API 是 worksheet
metadata 基础切片，不等同完整 Phase 3 或完整 Phase 5。
当前 `ImageFormat`、`ImageInfo` 和 `read_image_info()` 是默认 `stb` PNG/JPEG
图片元数据 API，只读取格式、尺寸和通道。当前 `WorksheetWriter::add_image()`
是默认 `stb` streaming-only new-workbook PNG/JPEG 图片插入基础 API，会写
OpenXML media/drawing parts，但不代表 existing-workbook 图片保真或完整 drawing 编辑。
当前 `ImageOptions` 只给该插入 API 增加 drawing XML anchor / non-visual metadata：
`edit_as` 写 `xdr:twoCellAnchor editAs`，非空 `name` / `description` 写
`xdr:cNvPr name` / `descr`，空值走默认名或省略。

## 核心原则

API 可以易用，但不能为了易用性牺牲性能主线。

- 大数据写入 API 优先 row iterator / chunk writer。
- 大型 worksheet 不能被 public API 隐式转入 DOM 或完整 cell matrix。
- 便利 API 必须写清适用范围。
- 只适合小文件的 API 应明确标记为 in-memory 路径。
- 性能热路径不能因为高层包装落到通用 XML serializer。
- 当前 `Worksheet::append_row()` 是 append-only、streaming-oriented public API，
  但 Phase 1 实现会临时 buffer rows；不要把这个 buffer 当成长期大文件架构。
- 当前 `WorksheetWriter` 骨架覆盖公式、行高、列宽、冻结窗格、自动筛选、
  合并单元格和 data validations 的写入 XML；这些不代表完整 Phase 3 或
  Phase 5 功能集。
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
  editing。
- `DocumentProperties` 是 new-workbook 小型 metadata API：字符串值被复制进
  `Workbook` 或 `WorkbookWriter` state，并写入 `docProps/core.xml` /
  `docProps/app.xml`。它不创建 `docProps/custom.xml`，不支持任意 timestamps、
  custom document properties 或 existing-file editing，也不进入 worksheet row/cell
  热路径。
- `read_image_info()` 是图片元数据 helper：当前默认 vcpkg manifest 依赖 `stb`，
  并用 `stb_image` 的 header probing 读取 PNG/JPEG 格式、尺寸和通道。它不创建
  media part、drawing XML、relationships、
  content types 或 anchors，也不代表图片插入或 existing-file 图片保真。
- `WorksheetWriter::add_image()` 是 Streaming metadata/object API：当前通过默认
  `stb` 依赖验证 PNG/JPEG 元数据，复制原始图片字节到临时 file-backed media entry，
  并在 `close()`
  写 `xl/media/*`、`xl/drawings/drawing*.xml`、drawing `.rels`、worksheet `.rels`、
  worksheet `<drawing>` 和 content type entries。它不解码完整像素、不进入 row/cell
  热路径、不持有完整 worksheet matrix，也不支持裁剪、旋转、压缩、格式转换、
  existing drawing mutation 或 existing-file editing。
- `ImageOptions` 是 Streaming image metadata options：`edit_as` 枚举和非空
  `name` / `description` 被复制进 writer state，并作为 drawing XML
  `xdr:twoCellAnchor editAs` 和 `xdr:cNvPr` 的 `name` / `descr` attributes 输出；空
  `name` 使用生成的 `Picture N`，空 `description` 省略。它不新增 media parts、
  relationships、content types、styles 或 cell text，不改变 anchor 坐标，不写
  EXIF/PNG/JPEG metadata，也不代表完整 alt text/accessibility UI。
- 当前 `Workbook::save()` 使用 internal package writer boundary；默认 ZIP backend 走
  stored ZIP bootstrap，`FASTXLSX_ENABLE_MINIZIP_NG=ON` 走 minizip-ng DEFLATE
  backend。两者都不是已有文件编辑 API，也不承诺 Zip64 或 true package streaming。
- OPC/Phase 5 仍是内部 manifest / relationships 基础和规划，不要把
  `PackageEditor`、图片、VBA 或 table 支持写成当前完整能力。

## API 模式

设计或审查 API 时，先标记模式：

- `Streaming`：新建 XLSX、大数据导出、多 sheet 批量写入。
- `Patch`：已有 XLSX 编辑、part-level rewrite、模板替换。
- `In-memory`：小文件复杂编辑，不承诺大文件低内存。

任何 public API 都要说明它属于哪种模式，以及它是否允许随机访问或回写历史行。

## 文档注释要求

public header 中的 API 应有 Doxygen 风格注释，至少说明：

- API 所属模式。
- 是否保留完整 worksheet 状态。
- 输入顺序要求。
- 是否允许随机访问。
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
Excel serial number 写入 numeric cell，样式/number format 仍不是当前能力。
不要把 `DataValidationType::Date` 误写成 date cell encoding 已实现。
不要设计或描述把 `NaN/Inf` 转成字符串、空单元格或 OpenXML 数字文本的行为。

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
`totalsRowFunction` attributes 和 caller-supplied `totalsRowLabel` attributes、无公式
生成、无 totals row 单元格文本、无 table resize、无 existing-file editing，以及不代表
完整 table 支持。
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
当前 `WorksheetWriter::add_image()` 注释应保持说明：Streaming / new-workbook-only、
原始图片字节 file-backed、two-cell anchor、package side effects、无完整像素解码、
无 existing-file editing、无 drawing mutation，且不牺牲 worksheet streaming 热路径。
涉及 `ImageOptions` 时还要写清：`edit_as` 枚举和 name/description 字符串复制成本、
OpenXML token / XML attribute escape、空值行为、只写 `xdr:twoCellAnchor editAs` 和
`xdr:cNvPr name` / `descr`、不修改图片二进制/EXIF/media filename/anchor 坐标/cell
text、不支持 `oneCellAnchor` / `absoluteAnchor` 元素或 row/column resize 几何计算，
以及不承诺完整 Excel UI 或跨办公软件 accessibility 行为。

涉及热路径的 API，还要说明是否会触发 DOM、跨行缓存、shared strings 状态增长、
压缩等级影响或输出文件大小变化。

## 任务计划要求

规划 API 任务时，任务说明必须写清：

- 所属 Phase。
- API 模式。
- 是否触碰性能热路径。
- 是否需要文档注释。
- 需要哪些单元测试、OpenXML 结构测试、Excel 可视化验证、拆包 XML 对比或 benchmark。
- 是否改变 CMake target 或引入依赖。

如果任务要求“更易用”，必须同时说明为什么不会破坏 streaming 性能主线。

## 禁止事项

- 不要让 `Workbook` 级便利 API 默认持有完整 worksheet。
- 不要让 large worksheet 因 API 简化进入 DOM。
- 不要把 streaming API 做成 DOM API 的附属补丁。
- 不要隐藏压缩等级、字符串策略或 DOM 模式这类性能关键选择。
- 不要用“高性能”“低内存”等模糊描述替代明确边界。

## 验证

- public API 有文档注释。
- 注释写明模式、内存行为、限制和性能注意事项。
- 大数据路径仍然 row/chunk 化。
- 便利 API 不会隐式 DOM 化大型 worksheet。
- 测试计划包含结构验证和必要的 Excel 可视化验证。
- document properties 测试覆盖 core/app docProps 字段、XML escape、relationships、
  content types，并确认不生成 `docProps/custom.xml`。
- 数值相关 API 注释写清 `NaN` / `+Inf` / `-Inf` 拒绝边界，且测试覆盖拒绝路径。
- 性能敏感 API 有 benchmark 或明确后续 benchmark 任务。
