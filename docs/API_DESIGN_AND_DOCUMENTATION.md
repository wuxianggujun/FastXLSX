# API 设计与文档注释

## 目标

FastXLSX 的 API 可以追求易用，但不能为了易用性牺牲流式优先和性能主线。

API 设计必须和项目性能目标对齐：

- 大数据写入优先暴露 row iterator / chunk writer。
- 大型 worksheet 不能被高层 API 迫使进入 DOM 或完整 cell matrix。
- 便利 API 必须明确适用范围；如果只适合小文件或 in-memory 模式，要在文档注释里写明。
- 性能热路径不能因为 API 包装而落到通用 XML serializer 或全量对象模型上。

## API 分层原则

FastXLSX 可以提供多层 API，但每层都要标明成本。

### Streaming API

用于新建 XLSX、大数据导出、多 sheet 批量写入。

要求：

- 接受 row iterator、range、callback 或 chunk writer。
- 写入顺序应清晰，不承诺随机回写已输出历史行。
- 内存占用与当前行 buffer、XML output buffer、file-backed/chunked worksheet entry
  buffer、字符串策略和 package / ZIP writer 状态相关。
- 不持有完整 worksheet cell matrix；如果 finalization 避免完整 worksheet XML
  内存副本，也必须说明该保证只覆盖对应 worksheet entry，不覆盖所有 package parts。
- worksheet metadata API，例如 `WorksheetWriter::add_data_validation()`，应只保存
  轻量规则状态；内存成本按规则数量、multi-area `sqref` 的区域数量/文本长度、
  公式文本长度和 prompt/error 文本长度增长，不能为了校验规则而读取或持有完整
  worksheet cell matrix。

### Patch API

用于编辑已有 XLSX。

要求：

- 以 part-level rewrite 为基本模型。
- 未修改 part 原样复制。
- 大型 worksheet 流式重写。
- 小型 XML part 才允许局部 DOM。

### In-memory API

只用于小文件和复杂编辑。

要求：

- 必须显式标注不承诺大文件低内存。
- 不得成为大数据写入的默认路径。
- 不得让用户误以为随机访问 API 适合百万行级导出。

## 文件职责边界

API 设计不只要控制运行时成本，也要控制实现和测试的变化范围。

- public API 可以继续集中在现有 public headers，保持用户入口稳定。
- 核心 writer 文件应保留流程编排、生命周期管理和热路径调用，不应无限承载所有
  feature 的 XML 序列化、校验和状态转换。
- 当一个 feature 已经有独立 public API、独立 XML 结构、独立状态、独立 QA helper
  或大量边界测试时，任务设计应考虑 feature-specific 实现文件、detail helper 和
  独立测试文件。
- 测试文件应镜像功能边界：主 streaming 测试保留主流程、边界和跨功能集成；feature
  的细粒度结构测试、负例和 QA 样例应逐步拆到独立测试文件。
- QA helper 应保持单一功能职责；跨功能 helper 只用于验证真正跨对象的 package
  side effects、relationship id 或兼容性行为。
- 新增 `.cpp` 或测试文件时，任务计划必须包含 CMake 列表同步。
- 不要过度拆分：很小的协调代码、一次性修正或尚未稳定的实验切片，可以先留在现有
  文件中，等边界和增长趋势明确后再拆。

## 文档注释要求

公共头文件中的 public API 应编写文档注释。推荐使用 Doxygen 风格：

```cpp
/// Writes rows to the worksheet streaming path.
///
/// This API does not keep a full worksheet cell matrix in memory. Rows are
/// consumed in order and previously written rows cannot be randomly modified.
///
/// @param rows Row range or iterator source.
/// @throws FastXlsxError when XML encoding or package writing fails.
```

文档注释至少说明：

- API 所属模式：Streaming、Patch 或 In-memory。
- 是否保留完整 worksheet 状态。
- 输入顺序要求。
- 是否允许随机访问或回写历史行。
- 字符串策略相关行为。
- 样式、关系或 content types 的副作用。
- 错误处理方式。
- 性能/内存注意事项。

写入 OpenXML 数值或数值型 worksheet metadata 的 `double` API 还要写清
finite-only 边界。`Cell::number(double)`、`CellView::number(double)`、
`RowOptions::height` 和 `WorksheetWriter::set_column_width()` 不接受 `NaN`、
`+Inf` 或 `-Inf`。当前 streaming 路径由 `WorksheetWriter::append_row()` 拒绝
非有限 number / row height，`WorksheetWriter::set_column_width()` 立即拒绝非有限
width；小型 in-memory `Workbook` 路径在 `Workbook::save()` 序列化 worksheet XML
时报 `FastXlsxError`。不要把 `NaN/Inf` 写成字符串、空单元格或 OpenXML 数字文本。

当前 `Cell` / `CellView` 都没有专用 date cell 类型；日期/时间单元格只能由调用方按
Excel serial number 写入 numeric cell。P9 已有 streaming-only 自定义 number format
styles，但 number format 只控制显示格式，不编码 date cell type、不计算日期序列值，也不验证
日期语义。不要把 `DataValidationType::Date` 误写成 date cell encoding 已实现。

对 styles 相关 public API，注释还要写清当前只支持 Streaming / new-workbook-only 的
custom number format、窄 wrap-text + limited horizontal/vertical alignment、
窄 bold/italic font 和窄 solid foreground fill slices。`StyleId` 是 workbook-local handle，默认 id `0`
表示 workbook default style；非默认 id 必须来自同一个 `WorkbookWriter::add_style()`
返回值，不能跨 workbook 复用。`CellStyle::number_format` 可为空，表示不改变 number
format；`CellAlignment::wrap_text=true`、`HorizontalAlignment::{Left,Center,Right}`
和 `VerticalAlignment::{Top,Center,Bottom}` 是当前 alignment 子能力，false flags
和空 optional 不贡献 style 属性。`CellFont::bold` / `CellFont::italic` 是当前唯一 font 子能力，false
flags 或空 optional 不贡献 style 属性；`CellFill::foreground` 是当前唯一 fill 子能力，
使用 `ArgbColor` 写 solid foreground fill，空 optional 不贡献 style 属性。`WorkbookWriter::add_style()` 应拒绝完全空的
style；重复完整 style 复用同一个 `StyleId`，相同 number format 在不同 style 组合中
复用同一个 custom `numFmtId`，相同 bold/italic font 组合复用同一个 `fontId`，
相同 foreground ARGB fill 组合复用同一个 `fillId`，
不做 Excel 语义规范化。`CellView::with_style()` 只在 cell view 中携带
小句柄；`WorksheetWriter::append_row()` 必须在推进 row count、dimension、
sharedStrings 或 formula recalculation metadata 前拒绝无效或 foreign style id。
启用非默认样式会在 `close()` 时生成 `xl/styles.xml`、styles content type override 和
workbook styles relationship，并在 worksheet cell 上写 `s="N"`；默认 style 不写
`s="0"`，也不新增 worksheet `.rels`。alignment 只写 `cellXfs` 中的
`applyAlignment="1"` / `<alignment .../>` attributes：`wrapText="1"`、
`horizontal="left|center|right"` 和 `vertical="top|center|bottom"`；不计算
row height，也不代表完整 alignment。bold/italic font 只写 `<fonts>` 中的 `<b/>` / `<i/>`、`fontId` 和
`applyFont="1"`，不代表完整 font control。solid fill 只写 `<fills>` 中的 solid
`<patternFill>`、`fgColor rgb`、`bgColor indexed="64"`、`fillId` 和 `applyFill="1"`，
不代表完整 fill/pattern/theme/tint/indexed palette control。当前不支持 font color、size、family、
underline、border/full alignment、rich text、dxf-backed
conditional formatting、existing-file style preservation 或完整
Excel formatting parity。当前 two-/three-color color scale、basic data bar 和 basic
3Arrows icon set API 是 worksheet metadata，不代表 styles registry 或 `dxfs` 已支持。

对 conditional formatting 这类 worksheet metadata API，注释还要写清是否只支持
Streaming / new-workbook 路径、是否复制规则 endpoint 和 range 列表、内存是否按规则数
和 range 数增长、`ArgbColor` 是否序列化为 8 位大写 ARGB、`ColorScaleValueType` /
`DataBarValueType` / `IconSetValueType` 的 endpoint/threshold token 和 finite-only
数值边界、priority 是否按 worksheet 内调用顺序分配、
multi-range `sqref` 是否排序/合并/去重/检查重叠，以及是否新增 relationships、
content types、styles 或 calc metadata。当前
`WorksheetWriter::add_conditional_color_scale()` 只写 worksheet-local two-/three-color
`<conditionalFormatting><cfRule type="colorScale">` XML；
`WorksheetWriter::add_conditional_data_bar()` 只写 worksheet-local basic
`<cfRule type="dataBar">` XML，`DataBarRule::show_value=false` 只会写
`<dataBar showValue="0">`；`WorksheetWriter::add_conditional_icon_set()` 只写
worksheet-local basic built-in `3Arrows` `<cfRule type="iconSet">` XML，要求三枚有限且
严格递增的阈值。它们不写 `styles.xml`、`dxfs`、worksheet `.rels`、content type、
cell text 或 `<calcPr>`，不支持 formula/cellIs、advanced/custom icon sets、top/bottom、
duplicate/unique、advanced data bar negative color/axis/border/gradient/`extLst`、
dxf-backed styles、existing-file editing 或完整 Excel UI。

对 data validations 这类 worksheet metadata API，注释还要说明是否只写
worksheet XML、是否新增 relationships/content types/styles、是否复制 range 列表、
公式文本和 prompt/error 文本、multi-area `sqref` 是否排序/合并/去重/检查重叠、
是否按属性写出 `showInputMessage`、`showErrorMessage`、`showDropDown`、
`errorStyle`、`promptTitle`、`prompt`、`errorTitle`、`error`，是否省略空字符串和
false flag，是否解析公式或校验单元格值，以及是否支持 existing-file editing 或完整
Excel UI。当前 `hide_dropdown_arrow` 只对 list validation 有效，写出 OpenXML
反向命名的 `showDropDown="1"` 来隐藏 in-cell dropdown arrow；false 时省略。

对 hyperlinks 这类 worksheet metadata API，注释还要区分 external relationship-backed
链接和 internal location-only 链接。external API 要说明是否只支持 Streaming /
new-workbook 路径、是否复制 URL 文本、是否写 worksheet `<hyperlinks>` 和 worksheet
`.rels`、relationship id 是否只在 worksheet owner 内有效、是否新增 workbook
relationships 或 content type overrides、是否会写单元格文本或样式，以及是否校验 URL
可达性或支持 existing-file editing。internal API 要说明是否复制 location 文本、是否写
worksheet `<hyperlink location="...">`、是否不创建 `.rels` 或 `r:id`、是否校验目标
sheet/range/named range 是否存在，以及是否支持 existing-file editing。`HyperlinkOptions`
要说明非空 display/tooltip 会复制进 writer state 并写成 worksheet `<hyperlink>`
attributes，空字符串省略，且不会写单元格文本、创建 hyperlink 样式、生成 `styles.xml`
或改变 relationships/content types。

对 tables 这类跨 part worksheet metadata API，注释还要说明是否只支持
Streaming / new-workbook 路径、是否复制 table name / column names / style name /
`show_totals_row` / `column_totals_functions` / `column_totals_labels`、
是否生成 `xl/tables/tableN.xml`、worksheet `<tableParts>`、worksheet `.rels` 和
content type override、relationship id 是否只在 worksheet owner 内有效、是否读取
已写 header 行或推断列名、是否生成 `styles.xml`，以及是否仅支持 caller-supplied
totals row、列级 `totalsRowFunction` metadata 和列级 `totalsRowLabel` metadata。
注释还必须写清它只拒绝同一 worksheet 内 table-vs-table range overlap，不检查与
data validations、images、merged ranges 或 autoFilter 的冲突；它不计算 totals、
不生成公式文本、totals row 单元格文本、calculated columns、table resize、
existing-file editing 或完整 Excel table UI。

对 document properties 这类小型 workbook metadata API，注释还要说明它是否只支持
new-workbook 输出、是否只写 `docProps/core.xml` 和 `docProps/app.xml`、是否复制
字符串值、是否新增或保持 package relationships / content type overrides、是否创建
`docProps/custom.xml`，以及是否支持 created/modified timestamp、custom document
properties、existing-file editing 或未知 docProps part 保留。当前
`DocumentProperties`、`Workbook::set_document_properties()` 和
`WorkbookWriterOptions::document_properties` 只覆盖 core/app 小型 XML part；它们不进入
worksheet row/cell 热路径，也不代表完整 document properties API。

图片对象 hyperlink 必须和 worksheet cell hyperlink 分开描述。当前
`ImageOptions::external_hyperlink_url` 属于 drawing object metadata：它写 drawing XML
`a:hlinkClick` 和 drawing-owned external hyperlink relationship，不写 worksheet
`<hyperlinks>`、不写 cell text、不创建 hyperlink style、不校验 URL 可达性，也不编辑
已有 XLSX。

对 images 这类跨 part 对象 API，注释还要说明是否只支持 Streaming /
new-workbook 路径，还是属于 Patch / existing-workbook 路径。图片读取应使用 `stb`
获取尺寸、通道数和像素；但 `stb` 不负责 OpenXML media part、drawing XML、
drawing relationships、worksheet relationships、content types 或 anchors。
当前 `WorksheetWriter::add_image(path, anchor)` 和
`WorksheetWriter::add_image(bytes, anchor)` 是 Streaming / new-workbook PNG/JPEG
基础切片：它们用 `read_image_info()` 验证元数据，把原始图片字节复制到临时
file-backed media entry，并在 `close()` 时生成 media part、drawing XML、drawing
`.rels`、worksheet `.rels`、worksheet `<drawing>` 和 content type entries。
memory-source overload 接受 `std::span<const std::byte>`，caller-owned span 只需在调用
期间有效；FastXLSX 不保留该 span，不保留 decoded pixel buffer，也不把图片 bytes 放入
worksheet row/cell 热路径。
当前 `ImageOptions` 是同一 streaming image API 的窄 metadata options：
`from_offset` / `to_offset` EMU 值、`edit_as` 枚举和非空 `name` / `description`
字符串会复制进 writer state，并分别写到 drawing XML two-cell marker 的
`xdr:colOff` / `xdr:rowOff`、`xdr:twoCellAnchor editAs` 和 `xdr:cNvPr` 的
`name` / `descr` attributes；空 `name` 保留生成的 `Picture N`，空 `description`
省略。非空 `external_hyperlink_url` 会写 `a:hlinkClick` 并创建 drawing-local
external hyperlink relationship，`external_hyperlink_tooltip` 只写该 `a:hlinkClick`
的 `tooltip` attribute，且 tooltip without URL 会被拒绝。它只改变 drawing marker
metadata、non-visual picture properties 和可选 drawing-local hyperlink metadata，
不修改图片二进制、EXIF、media filename、anchor cell range、content types 或 cell
text，也不支持 `oneCellAnchor` / `absoluteAnchor` 元素或 row/column resize 几何计算。
`read_image_info()` 注释应限制为 PNG/JPEG metadata helper：读取格式、尺寸和
通道数；不要暗示它会生成 media part、drawing XML、relationships、content types、
anchors，或验证 Excel package 兼容性。
图片插入 API 还要说明是否复制原始图片字节、是否会解码像素、decoded pixel buffer
生命周期、内存成本是否与图片字节数或 `width * height * channels` 相关、是否生成
`xl/media/*`、`xl/drawings/drawing*.xml`、drawing `.rels`、worksheet `.rels`、
worksheet `<drawing>` 和 content type entries，以及是否支持裁剪、旋转、压缩、
格式转换、existing drawing mutation、existing-file editing 和图片保真复制。
memory-source API 的注释还必须说明 span 生命周期、同步复制语义、空 buffer 和
unsupported header 的错误边界，以及它不是任意 stream/URL/base64 图片源。
如果只是嵌入已有 PNG/JPEG，应优先说明原始字节保留路径；不要为了便利 API 把图片
处理放进 worksheet row/cell 热路径或让大型 worksheet 进入 DOM。
如果暴露图片对象 hyperlink metadata，应说明 relationship owner 是 drawing part，
不是 worksheet part；`a:blip r:embed` 的 image relationship id 必须与 `a:hlinkClick`
的 hyperlink relationship id 分离，不能复用 cell hyperlink 路径。

## 性能注释要求

涉及热路径或大数据行为的 API，注释必须包含性能边界。

需要说明：

- 是否 O(rows)、O(cells) 或与 unique strings 数量相关。
- 是否分配跨行缓存。
- 是否可能触发 shared strings 去重状态增长。
- 是否影响 ZIP 压缩等级或输出文件大小。
- 是否会触发 DOM。
- Finalization API 必须说明当前是否 assemble package entries、哪些 entries 可
  file-backed/chunked、哪些 parts 仍可能 in-memory、是否 true package streaming、
  是否有 Zip64 或 existing-file preservation 保证。

禁止写模糊承诺，例如“高性能”“低内存”，却不说明内存由哪些状态组成。

## API 设计禁忌

- 不要为了让 API 像普通 workbook 编辑器一样方便，而默认保存完整 worksheet。
- 不要让 `Workbook` 级 API 隐式把大数据路径转成 in-memory 模式。
- 不要把 streaming API 做成普通 DOM API 的附属补丁。
- 不要隐藏压缩等级、字符串策略、DOM 模式等会影响性能的关键选择。
- 不要让高级功能污染 cell XML 写入热路径。

## 任务计划要求

规划 API 或实现任务时，任务说明必须写清：

- 属于哪个阶段：Phase 1、Phase 2、Phase 3、Phase 4 或 Phase 5。
- 使用哪种 API 模式：Streaming、Patch 或 In-memory。
- 是否触碰性能热路径。
- 是否需要文档注释。
- 需要哪些结构测试、Excel 可视化验证、拆包 XML 对比或 benchmark。
- 是否会引入依赖或改变 CMake target。

如果任务要求“API 更易用”，必须同时说明为什么不会破坏流式性能主线。

## 验证清单

- public API 有文档注释。
- 文档注释写明模式、内存行为和限制。
- 大数据路径仍能 row/chunk 化。
- 大型 worksheet finalization 不会重新物化完整 worksheet XML；如果仍会发生，
  必须在 API 注释中明确限制。
- 便利 API 不会隐式 DOM 化大型 worksheet。
- 测试覆盖 API 行为和 OpenXML 结构。
- 需要时完成本机 Excel 可视化验证。
- styles API 还需验证文档注释写清 workbook-local `StyleId`、foreign id 拒绝、
  `xl/styles.xml` / workbook relationship / content type side effects、默认 `s="0"`
  省略、number format 不是 date cell type、wrap-text + limited horizontal/vertical
  alignment 不是完整 alignment、bold/italic font 不是完整 font control、
  solid fill 不是完整 fill control，
  以及当前不支持完整 formatting。
- 图片 API 还需验证文档注释没有把 `stb` 解码能力写成 OpenXML 图片支持，也没有省略
  decoded pixel buffer、caller-owned memory span、copy-to-temp-file-backed media entry
  或 media/drawing part state 的内存边界。
- 图片对象 hyperlink API 还需验证注释明确 `a:hlinkClick`、drawing-owned `.rels`、
  no worksheet `<hyperlinks>` / no cell style / no URL reachability validation，以及
  `a:blip r:embed` 与 hyperlink `r:id` 分离。
- document properties API 还需验证结构测试覆盖 `docProps/core.xml`、
  `docProps/app.xml`、relationships、content types、XML escape，并明确不生成
  `docProps/custom.xml`。
- 性能敏感 API 有 benchmark 或明确的后续 benchmark 任务。
