---
name: fastxlsx-image-media-features
description: "规划或实现 FastXLSX 图片读取/插入、stb 图片解码、media parts、drawing XML、drawing relationships、worksheet relationships、content types、anchors 和图片保真验证。用于当前图片功能、现有 workbook 图片 passthrough、图片尺寸读取、默认 vcpkg stb 接入，或判断图片能力是否会破坏 OPC/streaming 边界。"
---

# FastXLSX Image Media Features

## 两条路径

- Streaming：`WorksheetWriter::add_image()` 创建 new-workbook media/drawing/relationships。
- Patch：`WorkbookEditor::replace_image()` 只替换已有 PNG/JPEG media part bytes。

两者不能混写为完整 drawing editing。

## 关键 Part

`xl/media/*`、drawing XML、drawing relationships、worksheet relationships、content types 和 anchors。图片解码/尺寸由 stb helper 提供，XLSX 语义仍由 FastXLSX 维护。

## 内存与安全

校验格式、尺寸、decoded pixel budget、输入生命周期和 package target。Streaming state 随图片 metadata/bytes 增长，但不得 materialize worksheet cells。

## 非目标

Existing drawing/anchor/relationship mutation、chart editing、任意 media type conversion 和完整图片保真对象模型。

## 验证

PNG/JPEG metadata、media bytes、drawing/rels/content types、multi-image ordering、invalid input、Excel/openpyxl smoke 和 existing-file preservation。