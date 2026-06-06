---
name: fastxlsx-opc-editing
description: "处理或规划 FastXLSX 已有文件编辑、OPC package、relationships、part index、part-level rewrite、小型 XML part 局部 DOM、模板填充和未知 part 保留边界。用于审查当前内部 OPC manifest/relationships 基础，以及 PackageReader、PackageWriter、PartIndex、RelationshipGraph、TemplateEditor 等规划类行为；不要据此宣称完整图片/VBA/table 支持。"
---

# FastXLSX OPC Editing

## 必读文件

- `docs/EDITING_MODEL.md`
- `docs/ARCHITECTURE.md`
- `docs/TECHNICAL_COMPARISON.md`
- `docs/ROADMAP.md`
- `README.md`

引用实现类前，先检查 `include/` 和 `src/`。当前已有内部 `PartName`、
`RelationshipSet`、`ContentTypesManifest`、`PackageManifest`、`PartWriteMode`、
`PackagePart` edit-state metadata、最小 workbook manifest 构建和 content types /
relationships XML serializer 基础。当前最小新建 workbook 默认包含基础
`docProps/core.xml` 和 `docProps/app.xml` 小型 XML builder；已有文件编辑和
Phase 5 仍是计划。

本轮 OPC edit plan 只能写为基础或计划：当前有 copy-original、
generate-small-XML、stream-rewrite、local-DOM-rewrite 的 write-mode metadata，
但没有 end-to-end `PackageReader` / `PackageWriter` / `PartIndex` /
`RelationshipGraph` 编辑管线。

## 核心编辑模型

编辑基本单位是 OpenXML part，不是完整 workbook 对象图。

```text
读取 package
-> 建立 part index
-> 标记修改过的 part
-> 未修改 part 原样复制
-> 修改 part 重新生成
-> 写出 package
```

这套模型的目标是保留 FastXLSX 尚不了解的 Excel 结构，包括图表、图片、宏和
未知扩展；当前不能宣称完整图片、VBA 或 table 读写/编辑支持。

图片关系链至少包括：

```text
worksheet.xml drawing reference
-> xl/worksheets/_rels/sheetN.xml.rels
-> xl/drawings/drawingN.xml
-> xl/drawings/_rels/drawingN.xml.rels
-> xl/media/imageN.{png,jpeg}
-> [Content_Types].xml default/override
```

`stb` 只能证明图片 bytes 可解码或可读取尺寸，不证明 drawing/package 支持。
修改 relationships 时必须同步 content types 和 target part。

## DOM 边界

允许局部 DOM 的小型 XML part：

- `workbook.xml`
- workbook relationships
- `[Content_Types].xml`
- `docProps/*.xml`
- 较小的 `styles.xml`
- 规划中的小型 table、drawing、comments part

禁止 DOM：

- 大型 `worksheet.xml`
- 大型 `sharedStrings.xml`
- 批量数据写入路径
- 大型模板填充路径

## 设计锚点

当前内部基础，不是 public API：

- `PartName`
- `RelationshipSet`
- `ContentTypesManifest`
- `PackageManifest`
- `PartWriteMode`
- `PackagePart`
- `PackageManifest::set_part_write_mode`
- `PackageManifest::mark_part_dirty`
- `PackageManifest::mark_part_generated`
- `make_minimal_workbook_manifest`
- `serialize_content_types`
- `serialize_relationships`

这些是文档设计名，使用前先确认源码和任务阶段：

- `PackageReader`
- `PackageWriter`
- `PartIndex`
- `RelationshipGraph`
- `WorksheetRewriter`
- `OptionalDomDocument`
- `TemplateEditor`

## 推荐流程

1. 确认本次编辑影响哪些 XLSX parts。
2. 对每个 part 分类：原样复制、流式重写、局部 DOM 重写。
3. 同步维护 relationships 和 content types。
4. 大型 worksheet 走 event reader -> transformer -> stream writer。
5. 未知和未修改 part 尽量 byte-preserved。
6. 输出后做 package 结构和打开兼容性验证。

## 本轮计划边界

- OPC edit plan：基础。内部 manifest 和 write-mode metadata 可作为规划入口。
- 基础 docProps 输出：基础。它只是新建 package 的静态小型 XML part，不是完整
  document properties API，也不是已有文件编辑。
- Package read/copy/write：计划。需要生产 ZIP backend、`PackageReader`、
  `PackageWriter`、`PartIndex` 和 `RelationshipGraph`。
- 保真验证：计划。需要输入/输出 package 对比，证明未知和未修改 part 被保留。
- 不能因为有 write-mode metadata 就宣称已有 XLSX 编辑、图片、VBA、table 或 chart
  支持。
- 不能因为 `stb` 可用就宣称已有 XLSX image passthrough、图片插入或 drawing 编辑。

## 高风险区域

- 从已知 part 全量重建 package 会破坏未知内容。
- 修改 relationship 但不更新对应 part/content type 会生成无效 package。
- 大型 worksheet 或 sharedStrings 使用局部 DOM 会违反架构。
- 模板替换不能退化为完整 worksheet 随机访问模型。

## 验证

- 最小新建 XLSX：检查 package entries、`[Content_Types].xml`、relationships、
  workbook、worksheet、基础 `sheetData`。
- 编辑已有 XLSX：比较前后 package，确认未修改 part 被保留。
- 可用时验证 Excel / WPS / LibreOffice 能打开输出。
- 结构异常时，用 Excel / `openpyxl` / `XlsxWriter` 生成语义参考文件，拆包后比较
  content types、relationships、workbook、worksheet、shared strings、styles
  和相关 object part 的 XML 语义。
- 实现存在后，为 part index 和 relationship graph 更新补回归测试。
