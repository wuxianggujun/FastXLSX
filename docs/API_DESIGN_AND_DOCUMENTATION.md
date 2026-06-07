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
Streaming / new-workbook 路径、是否复制 table name / column names / style name、
是否生成 `xl/tables/tableN.xml`、worksheet `<tableParts>`、worksheet `.rels` 和
content type override、relationship id 是否只在 worksheet owner 内有效、是否读取
已写 header 行或推断列名、是否生成 `styles.xml`，以及是否支持 totals row、
calculated columns、table resize、overlap checks、existing-file editing 和完整 Excel
table UI。

对 document properties 这类小型 workbook metadata API，注释还要说明它是否只支持
new-workbook 输出、是否只写 `docProps/core.xml` 和 `docProps/app.xml`、是否复制
字符串值、是否新增或保持 package relationships / content type overrides、是否创建
`docProps/custom.xml`，以及是否支持 created/modified timestamp、custom document
properties、existing-file editing 或未知 docProps part 保留。当前
`DocumentProperties`、`Workbook::set_document_properties()` 和
`WorkbookWriterOptions::document_properties` 只覆盖 core/app 小型 XML part；它们不进入
worksheet row/cell 热路径，也不代表完整 document properties API。

对 images 这类跨 part 对象 API，注释还要说明是否只支持 Streaming /
new-workbook 路径，还是属于 Patch / existing-workbook 路径。图片读取应使用 `stb`
获取尺寸、通道数和像素；但 `stb` 不负责 OpenXML media part、drawing XML、
drawing relationships、worksheet relationships、content types 或 anchors。
当前 `WorksheetWriter::add_image(path, anchor)` 是 Streaming / new-workbook
PNG/JPEG 基础切片：它用 `read_image_info()` 验证元数据，把原始图片字节复制到临时
file-backed media entry，并在 `close()` 时生成 media part、drawing XML、drawing
`.rels`、worksheet `.rels`、worksheet `<drawing>` 和 content type entries。
当前 `ImageOptions` 是同一 streaming image API 的窄 metadata options：`edit_as`
枚举和非空 `name` / `description` 字符串会复制进 writer state，并分别写到
drawing XML `xdr:twoCellAnchor editAs` 和 `xdr:cNvPr` 的 `name` / `descr`
attributes；空 `name` 保留生成的 `Picture N`，空 `description` 省略。它只改变
drawing anchor metadata 和 non-visual picture properties，不修改图片二进制、EXIF、
media filename、anchor 坐标、relationships、content types 或 cell text，也不支持
`oneCellAnchor` / `absoluteAnchor` 元素或 row/column resize 几何计算。
`read_image_info()` 注释应限制为 PNG/JPEG metadata helper：读取格式、尺寸和
通道数；不要暗示它会生成 media part、drawing XML、relationships、content types、
anchors，或验证 Excel package 兼容性。
图片插入 API 还要说明是否复制原始图片字节、是否会解码像素、decoded pixel buffer
生命周期、内存成本是否与图片字节数或 `width * height * channels` 相关、是否生成
`xl/media/*`、`xl/drawings/drawing*.xml`、drawing `.rels`、worksheet `.rels`、
worksheet `<drawing>` 和 content type entries，以及是否支持裁剪、旋转、压缩、
格式转换、existing drawing mutation、existing-file editing 和图片保真复制。
如果只是嵌入已有 PNG/JPEG，应优先说明原始字节保留路径；不要为了便利 API 把图片
处理放进 worksheet row/cell 热路径或让大型 worksheet 进入 DOM。

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
- 图片 API 还需验证文档注释没有把 `stb` 解码能力写成 OpenXML 图片支持，也没有省略
  decoded pixel buffer 或 media/drawing part state 的内存边界。
- document properties API 还需验证结构测试覆盖 `docProps/core.xml`、
  `docProps/app.xml`、relationships、content types、XML escape，并明确不生成
  `docProps/custom.xml`。
- 性能敏感 API 有 benchmark 或明确的后续 benchmark 任务。
