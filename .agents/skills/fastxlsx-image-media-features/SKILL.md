---
name: fastxlsx-image-media-features
description: "规划或实现 FastXLSX 图片读取/插入、stb 图片解码、media parts、drawing XML、drawing relationships、worksheet relationships、content types、anchors 和图片保真验证。用于 Phase 5 图片功能、现有 workbook 图片 passthrough、图片尺寸读取、默认 vcpkg stb 接入，或判断图片能力是否会破坏 OPC/streaming 边界。"
---

# FastXLSX Image Media Features

## 必读文件

- `docs/DEPENDENCIES.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`
- `docs/TESTING_WORKFLOW.md`
- `include/fastxlsx/detail/opc.hpp`
- `src/opc.cpp`
- `vcpkg.json`

先用 `rg` 确认符号是否真实存在。当前已有 `ImageFormat`、`ImageInfo`、
`ImagePixels`、`read_image_info()` 和 `read_image_pixels(path/span)` 这些
PNG/JPEG 图片读取 helper；它们通过默认 vcpkg `stb` 依赖读取 metadata 或显式
解码像素。`read_image_pixels()` 返回 caller-owned `ImagePixels::pixels`，会分配
完整 decoded pixel buffer；它不创建 OpenXML parts，也不参与
`WorksheetWriter::add_image()` 的 streaming 插入热路径。当前还已有
`WorksheetWriter::add_image(path, anchor)` 和 memory-source overload 的
streaming-only new-workbook PNG/JPEG 图片插入基础切片，会写 media part、drawing XML、
drawing `.rels`、worksheet `.rels`、worksheet `<drawing>` 和 content type entries。
memory-source overload 接受 `std::span<const std::byte>`，span 只需在调用期间有效，
会立即复制 caller bytes 到临时 file-backed media entry。仍没有已有 XLSX 图片编辑、
已有 drawing mutation 或保真复制闭环。
当前还已有窄 `ImageOptions` metadata：`from_offset` / `to_offset` 是
`ImageAnchorOffset` EMU 值，只写入现有 `xdr:twoCellAnchor` marker 的 `xdr:colOff` /
`xdr:rowOff`；`edit_as` 只写入 drawing XML 的 `xdr:twoCellAnchor editAs`
attribute；非空 `name` / `description` 只写入 `xdr:cNvPr name` / `descr`
attributes；空 `name` 保留生成的 `Picture N`，空 `description` 省略。它不是
`oneCellAnchor` / `absoluteAnchor` 元素支持、row/column resize 几何计算、图片文件
metadata、EXIF、media filename、cell text、existing drawing mutation 或完整
alt text/accessibility UI。
当前 `ImageOptions` 还支持图片对象级 external hyperlink metadata：
`external_hyperlink_url` / `external_hyperlink_tooltip` 只属于
`WorksheetWriter::add_image()` 的 streaming-only new-workbook drawing object metadata；
非空 URL 会在 drawing XML 的 `xdr:cNvPr` 下写 `a:hlinkClick`，并在 drawing `.rels`
中创建 `TargetMode="External"` 的 hyperlink relationship。它不写 worksheet
`<hyperlinks>`、不创建 cell text / hyperlink style、不校验 URL、也不支持 internal
picture link、existing-file editing 或完整 hyperlink UI。
`read_image_info()` 的 public 注释只能声明 PNG/JPEG 格式、尺寸和通道读取；不要写成
OpenXML image package、anchor、relationships、content types 或 Excel 兼容性验证能力。

## 当前事实

- 图片 OpenXML 插入/编辑整体仍属于 Phase 5；当前只完成 streaming-only
  new-workbook PNG/JPEG 插入基础切片。
- `vcpkg.json` 把 `stb` 记录在默认 `dependencies`；当前 `CMakeLists.txt`
  通过 vcpkg installed tree 的 `share/stb/FindStb.cmake`、
  预解析的 `${Stb_INCLUDE_DIR}` 和 `find_package(Stb MODULE REQUIRED)` 接入
  `read_image_info()` 和 `read_image_pixels()`。
- 本机 vcpkg 已验证 port 名称是 `stb`，描述为 public domain header-only
  libraries，license 是 `MIT OR CC-PDDC`。
- `stb` 只负责图片解码、尺寸、通道数和像素读取。
- FastXLSX 自己负责 OpenXML / OPC 语义：media part、drawing part、drawing
  `.rels`、worksheet `.rels`、content type override/default、anchors 和 package
  preservation。
- `read_image_pixels(path/span)` 是显式像素 helper，会用 `stb` 解码 PNG/JPEG 并返回
  owned `ImagePixels::pixels`。它会分配完整 decoded pixel buffer，不创建 media part、
  drawing XML、relationships 或 content types，也不提供 existing-workbook 图片保真、
  drawing 编辑、格式转换、压缩、裁剪或旋转。
- `WorksheetWriter::add_image()` 用 `read_image_info()` 验证 PNG/JPEG metadata，把
  原始图片字节复制到临时 file-backed media entry；memory-source overload 不保留
  caller span 或 decoded pixel buffer。它不解码完整像素，不把图片数据放入 row/cell
  热路径，也不触发 worksheet DOM。
- `ImageOptions` 只把 from/to marker EMU offsets、`edit_as` 枚举和
  name/description 字符串复制进 worksheet image state，并在 drawing XML 输出阶段写
  `xdr:colOff` / `xdr:rowOff`、OpenXML token 和 XML attribute；它不新增
  relationship/content type/media 副作用，也不改变 anchor cell range。
- `ImageOptions::external_hyperlink_url` / `external_hyperlink_tooltip` 只复制到
  worksheet image state，并在 drawing object 层写 `xdr:cNvPr/a:hlinkClick`；
  hyperlink relationship 属于 drawing `.rels` owner，`TargetMode="External"`。
  该能力不走 worksheet `<hyperlinks>`，不消耗 worksheet hyperlink API 的语义，
  不创建 cell text、styles、content type override 或 workbook relationship。

## 推荐流程

1. 先判断任务是图片解码、图片插入、图片读取，还是已有 workbook 图片保真。
2. 如果只是依赖规划，更新 `vcpkg.json`、`docs/DEPENDENCIES.md`、任务文档和本 skill；
   不要添加 `find_package` 或 public API。
3. 修改 `stb` 接入时，验证默认 vcpkg dependency、include 路径、license、CI
   安装耗时和 CMake package/include 行为。
4. 图片插入可以基于当前内部 OPC graph / relationship id 分配基础设计，但仍需
   media/drawing part allocator 和 worksheet relationship wiring。
5. 写出图片时同步维护 media part name、drawing part、drawing rels、worksheet rels、
   content types 和 worksheet drawing reference。
6. 读取或编辑已有 workbook 图片时，先证明未知和未修改 media/drawing/chart/VBA parts
   能保留。
7. 第一切片优先保持窄范围：new workbook only、PNG/JPEG、明确一种 anchor 策略；
   不支持 crop、rotation、effects、group shape 或 existing drawing mutation，除非
   单独任务设计和验证。

## P17 阶段拆分

1. P17.0 依赖发现和图片元数据 / 像素 helper：验证默认 `stb` 依赖的 vcpkg 解析、
   include 路径、license、CMake package/include 行为、CI 安装成本和失败模式。当前
   P17a 已有默认 `stb` 依赖、`read_image_info()` 和 `read_image_pixels(path/span)`；
   前者只代表 PNG/JPEG 格式、尺寸和通道读取，后者只代表 caller 显式请求时的
   owned decoded pixel buffer。
2. P17.1 插入/编辑 API 设计和文档注释：先声明 Streaming、Patch 或 In-memory 模式，写清原始图片
   字节、decoded pixel buffer、anchor metadata、drawing/media part state 和 package
   finalization 的内存成本；便利 API 必须说明为什么不会把大型 worksheet 带入 DOM、
   完整 cell matrix 或 row/cell XML 热路径。
3. P17.2 new-workbook-only 插入：当前基础切片是
   `WorksheetWriter::add_image(path, anchor)` 和 `WorksheetWriter::add_image(bytes, anchor)`，
   限制为 PNG/JPEG、一种 two-cell anchor
   策略、generated workbook，同步生成 media part、drawing part、drawing `.rels`、
   worksheet `.rels`、worksheet `<drawing>` 引用和 content types；不做 existing
   drawing mutation。memory-source overload 只同步复制 caller-owned bytes，不调用
   `read_image_pixels()`，不代表任意 stream/URL/base64 图片源。当前 `ImageOptions`
   增量只在该 drawing XML 中写
   two-cell marker `xdr:colOff` / `xdr:rowOff`、`xdr:twoCellAnchor editAs` 和
   `xdr:cNvPr` 的 `name` / `descr`，以及可选 `a:hlinkClick` 图片对象外部链接；
   外部链接 relationship 只写 drawing `.rels` 并标记 `TargetMode="External"`。这些
   都不是 `oneCellAnchor` / `absoluteAnchor` 元素支持。
4. P17.3 验证闭环：当前 `fastxlsx.streaming` 已覆盖 media/drawing/rels/content
   types/anchor 结构测试；本机 Excel COM 已打开
   `build/windows-nmake-release/tests/fastxlsx-streaming-images.xlsx`，确认
   `Images` / `SecondImage` 各 1 个 shape、`Plain` 为 0 个 shape，锚点为 `C1:F5`
   和 `A1:B2`。当前 image metadata 样例
   `build/windows-nmake-release/tests/fastxlsx-streaming-image-metadata.xlsx`
   已通过 `tools/verify_image_metadata.py` 的 drawing XML / openpyxl / XlsxWriter
   检查和 `tools/verify_image_metadata_excel.ps1` 的 Excel COM shape metadata /
   placement / marker-offset geometry 检查。当前 memory-source 样例
   `build/windows-nmake-release/tests/fastxlsx-streaming-memory-images.xlsx`
   通过同一 helper 的 `--memory-input` / `-MemoryPath` 检查 media bytes、package XML、
   openpyxl smoke、XlsxWriter reference 和 Excel COM anchors。当前图片 hyperlink
   mixed-object 样例
   `build/windows-nmake-release/tests/fastxlsx-streaming-image-hyperlink-mixed-objects.xlsx`
   通过同一 helper 的 `--mixed-hyperlink-input` / `-MixedHyperlinkPath` 检查 worksheet
   cell hyperlink / drawing / table relationships、drawing picture hyperlink、openpyxl
   cell hyperlink/table smoke，以及 Excel COM shape / cell hyperlink / table。
   结构异常时用 Excel、`openpyxl` 或 `XlsxWriter` 生成参考 workbook 并拆包对比 XML 语义。
5. P17.4 existing-workbook 图片读取、编辑或保真：只在 package reader/writer 和保真
   fixtures 证明未修改 media/drawing/chart/VBA/unknown parts 保留后推进。

## 必须拆开的边界

- `stb`：解码、尺寸、通道、像素。
- Package writer/reader：ZIP entry 读写和未修改 part 保留。
- OPC graph：part 索引、relationship id、target、content type。
- Drawing writer：anchors、drawing XML、drawing rels。
- Worksheet writer：worksheet `<drawing r:id="...">` 引用。

不要把这些边界合并成一个便利 API，也不要把图片逻辑放进 worksheet row/cell 热路径。

## 禁止事项

- 不要把 `stb` 可用写成完整图片功能已实现；当前只可写为
  `WorksheetWriter::add_image()` 的 new-workbook PNG/JPEG path/memory-source 基础切片。
- 不要把 `stb` 复制进 `src` 或 `include`；优先使用 vcpkg manifest。
- 不要引入 OpenCV、FreeImage、Qt 等重型图像框架，除非后续任务有单独证据和审批。
- 不要只因为已有 RelationshipGraph/PartIndex 就实现 public 图片 API；还必须有
  media part、drawing XML、worksheet rels、content types、anchors 和 Excel 验证。
- 不要从已知 part 全量重建 package 导致已有图片、drawing、chart、VBA 或未知扩展丢失。
- 不要承诺图片编辑、裁剪、压缩、格式转换或 chart/VBA support，除非代码和测试已经覆盖。
- 不要把 `read_image_pixels()` 写成 `WorksheetWriter::add_image()` 的实现依赖、低内存
  streaming 插入路径、格式转换/压缩管线或 existing-workbook 图片保真能力；它只是会分配
  完整像素缓冲的显式 caller helper。
- 不要把 memory-source overload 写成任意 stream、URL、base64、existing-workbook
  preservation、drawing mutation 或完整低内存 package streaming 支持。
- 不要把 `ImageOptions::edit_as` 写成 `oneCellAnchor` / `absoluteAnchor` 元素支持，
  或把 `ImageOptions::from_offset` / `to_offset` 写成 row/column resize 几何计算、
  跨办公软件 UI 保证、drawing mutation 或 existing-workbook 图片编辑。不要把
  `ImageOptions::name` / `description` 写成完整图片 metadata、EXIF/PNG/JPEG
  metadata、完整 alt text/accessibility UI 或 media filename 语义。
- 不要把 `ImageOptions::external_hyperlink_url` 写成 worksheet hyperlink、cell text、
  hyperlink style、URL 可达性校验、internal picture link、existing-file editing 或完整
  hyperlink UI；它只是 drawing object 的 external click metadata。

## 验证

- 依赖阶段：默认 vcpkg manifest 中的 `stb` 可解析。
- P17a 图片元数据 / 像素阶段：默认构建下 `fastxlsx.image` 读取 PNG/JPEG 文件和内存
  尺寸、格式、通道通过；`read_image_pixels(path/span)` 覆盖 PNG/JPEG 文件/内存像素
  解码、owned buffer size，以及 unsupported memory/file header、empty memory buffer、
  empty file 和 missing file。
- P17b 图片插入阶段：默认构建下验证 PNG/JPEG media、drawing、rels、
  content types 和 worksheet `<drawing>` 结构；JPEG 覆盖 `.jpg` media entry、
  `image/jpeg` default、drawing EMU 尺寸和 drawing `.rels` target；混合 PNG/JPEG
  覆盖同一 worksheet 共享 drawing part、多 anchor、全局 media 编号和 owner-local
  drawing relationship id。
  多对象 relationship id 回归还应覆盖同一 worksheet 内多个 external hyperlink、
  一个 drawing 和多个 table 的 worksheet-owner-local `rId` 顺序，以及跨 worksheet
  owner 和 drawing owner 的 relationship id 重置。
  最大合法 anchor marker 结构测试还应覆盖 Excel 边界单元格到 drawing marker 的
  0-based 序列化，例如 `<xdr:col>16383</xdr:col>` 和
  `<xdr:row>1048575</xdr:row>`；这只是结构边界测试，不是大文件性能证明。
  图片 metadata 结构测试还应覆盖 `from_offset` / `to_offset` 到 two-cell marker
  `xdr:colOff` / `xdr:rowOff` 的序列化、`xdr:twoCellAnchor editAs` 的 `oneCell` /
  `absolute` / 默认 `twoCell`、`xdr:cNvPr name` / `descr`、XML attribute escape、
  空 description 省略、默认 `Picture N` 名称，以及 metadata 不新增 media/rels/content
  type 副作用且不改变 anchor cell range。
  图片对象 external hyperlink metadata 还应覆盖 `xdr:cNvPr` 下的
  `a:hlinkClick`、tooltip attribute escape、drawing `.rels` hyperlink relationship、
  `TargetMode="External"`、relationship id 引用一致性，以及不写 worksheet
  `<hyperlinks>`、不创建 cell text、styles、content type override 或 workbook
  relationship。
- CMake 接入阶段：VS2026/NMake configure/build/test 通过；CI 行为需要在 workflow
  真正运行 default preset 后再记录。
- 结构测试：检查 `xl/media/*`、`xl/drawings/drawing*.xml`、
  `xl/drawings/_rels/*.rels`、`xl/worksheets/_rels/*.rels`、content types 和 worksheet
  drawing reference；如果涉及 `ImageOptions`，还要检查 two-cell marker
  `xdr:colOff` / `xdr:rowOff`、`xdr:twoCellAnchor editAs` 和
  `xdr:cNvPr name` / `descr`；如果涉及图片对象 external hyperlink，还要检查
  `a:hlinkClick`、drawing `.rels`、`TargetMode="External"` 和 worksheet hyperlink
  absence。memory-source 样例还要检查 caller buffer mutation 后
  package 内 media bytes 不变、PNG/JPEG 两张图片共享同一 worksheet drawing part。
- Excel 可视化验证：本机有 Excel 时必须打开 `.xlsx` 样例，确认无修复弹窗且图片显示
  位置/尺寸符合预期；涉及 marker offset 时，还要核对 Excel shape 几何偏移与
  drawing XML 中的 `xdr:colOff` / `xdr:rowOff` 语义一致。
- 当前固定本地 QA 入口是 `tools/verify_image_metadata.py` 和
  `tools/verify_image_metadata_excel.ps1`。Python helper 接受 `--input`、
  `--basic-input`、`--mixed-object-input`、`--memory-input`、`--hyperlink-input` 和
  `--mixed-hyperlink-input`，分别检查 image metadata、基础图片插入、mixed-object
  relationship、memory-source image、图片对象 hyperlink 和 mixed cell/table/picture
  hyperlink 样例；Excel helper 接受 `-Path`、`-BasicPath`、`-MixedObjectPath`、
  `-MemoryPath`、`-HyperlinkPath` 和 `-MixedHyperlinkPath`。mixed hyperlink 样例检查
  worksheet cell hyperlink / drawing / table relationships、drawing picture hyperlink、
  Excel COM shape / cell hyperlink / table。`openpyxl` 可能跳过
  JPEG 图片读取，JPEG media/drawing 关系以
  拆包 XML 和 Excel COM 为准。
- 参考对比：结构异常或 Excel 修复行为不清楚时，用 Excel、`openpyxl` 或 `XlsxWriter`
  创建参考 workbook，拆包对比 XML 语义。
- 保真测试：编辑无关 part 后，确认未修改 media/drawing/chart/VBA/unknown parts 仍存在且
  relationships 有效。
