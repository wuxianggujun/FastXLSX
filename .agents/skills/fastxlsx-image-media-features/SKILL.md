---
name: fastxlsx-image-media-features
description: "规划或实现 FastXLSX 图片读取/插入、stb 图片解码、media parts、drawing XML、drawing relationships、worksheet relationships、content types、anchors 和图片保真验证。用于当前图片功能、现有 workbook 图片 passthrough、图片尺寸读取、默认 vcpkg stb 接入，或判断图片能力是否会破坏 OPC/streaming 边界。"
---

# FastXLSX Image Media Features

## 三条 public 路径

- Streaming write：`WorksheetWriter::add_image()` 创建 new-workbook media/drawing/relationships。
- Streaming read：`WorkbookReader::read_worksheet_images()` 有界读取已有 writer-compatible two-cell picture anchor，并流式审计 PNG/JPEG media。
- Patch：`WorkbookEditor::replace_image()` 只替换已有 PNG/JPEG media part bytes。

三者不能混写为完整 drawing editing。

## 关键 Part

`xl/media/*`、drawing XML、drawing relationships、worksheet relationships、content types 和 anchors。图片解码/尺寸由 stb helper 提供，XLSX 语义仍由 FastXLSX 维护。

## Bounded Read

- Worksheet 至多接受一个 direct standard `<drawing>`；经 owner-local internal relationship 解析 standard spreadsheet drawing part。
- Drawing 只接受 direct `xdr:twoCellAnchor` + writer-compatible `xdr:pic`，投影 owning `editAs`、from/to marker/EMU offset、transform extent、name/description、PNG/JPEG format 与 encoded size。
- Embedded image relationship 必须是 drawing-local standard internal relationship；target 需 percent-decoded/normalized、存在且 content type/signature 匹配。
- 每个 unique media entry 每次调用只完整 drain 一次以验证 ZIP size/CRC，不保留 bytes、不解码 pixels。Callback failure 原样传播，后续调用从 worksheet 重新开始。
- XML/nesting/image/id/target/name/description/numeric/media 均有 public guardrail；`FASTXLSX_HAS_IMAGES=0` 保留 symbol 但调用抛 `FastXlsxError`。

## 内存与安全

校验格式、尺寸、decoded pixel budget、输入生命周期和 package target。Streaming write state 随图片 metadata/bytes 增长，但不得 materialize worksheet cells；Streaming read 只缓存受限 anchor metadata 与 unique-media format/size map，media chunk 用后即释放。

## 非目标

Existing drawing/anchor/relationship mutation、`xdr:oneCellAnchor` / `xdr:absoluteAnchor` element projection、chart/shape/group/connector、crop/rotation/position transform、picture hyperlink、任意 media type conversion 和完整图片保真对象模型。

## 验证

Writer/Patch 验证 PNG/JPEG metadata、media bytes、drawing/rels/content types、multi-image ordering、invalid input、Excel/openpyxl smoke 和 existing-file preservation。Bounded reader 另测 stored/production DEFLATE、source order、owning anchor/metadata、unique media reuse、entity decode、callback retry、absent drawing、九类 guardrail、worksheet/drawing relationship/target/content-type audit、signature/CRC、unsupported drawing shape 与 source package no-side-effect；no-images runtime smoke 必须实际调用 reader symbol。
