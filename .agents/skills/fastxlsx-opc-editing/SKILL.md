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
`RelationshipSet`、`ContentTypesManifest`、`PackageManifest`、最小 workbook
manifest 构建和 content types / relationships XML serializer 基础；已有文件编辑
和 Phase 5 仍是规划。

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
- 实现存在后，为 part index 和 relationship graph 更新补回归测试。
