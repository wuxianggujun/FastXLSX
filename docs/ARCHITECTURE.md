# FastXLSX 架构设计

## 核心判断

FastXLSX 采用 **流式主线 + 局部 DOM 编辑辅助** 的架构。

主开发环境为 **C++20 / MSVC 2026**。架构设计优先保证该环境下的工程质量、
性能和可维护性，再逐步扩展到其他编译器。

原因很直接：

- 大数据写入时，DOM 会带来不可接受的内存占用。
- 编辑已有 XLSX 时，完全拒绝 DOM 会让小型 XML part 的修改变得复杂且脆弱。
- 最合理的边界是：worksheet 大 part 流式处理，小型元数据 part 可选择 DOM。

## 参考库取舍

`OpenXLSX` 证明了 DOM 路线适合小文件随机编辑，但不适合作为大型 worksheet 的
主路径。

`xlnt` 证明了 event parser / serializer 和 `xlsx_consumer` / `xlsx_producer`
分层适合 XLSX 序列化，但它的普通 workbook API 仍然以完整 worksheet 内存模型为中心。

FastXLSX 的架构选择是：

- 吸收 `xlnt` 的 producer / consumer 思路。
- 保留 `OpenXLSX` 类库的高频编辑体验。
- 大数据路径不持有完整 worksheet。
- 未修改 part 默认原样透传。

## 依赖边界

FastXLSX 只把通用底层能力交给第三方库。

```text
minizip-ng      ZIP package 读写
zlib-ng / zlib  DEFLATE 压缩
Expat           大型 XML event parser
pugixml         小型 XML DOM
```

XLSX 语义层必须由 FastXLSX 自己实现：

- OPC part 索引。
- relationships 管理。
- worksheet row / cell 编码。
- sharedStrings / inlineStr 策略。
- styles registry。
- part-level rewrite。

更详细的依赖选择见 [依赖策略](DEPENDENCIES.md)。

## 目标总体分层（部分模块尚未实现）

当前已实现的是 `Workbook` / `Worksheet` / `Cell`、`WorkbookWriter` /
`WorksheetWriter` / `CellView`，以及内部 OPC manifest / relationships 基础。
`PackageReader`、`PackageWriter`、`PartIndex`、`RelationshipGraph`、
`WorksheetReader`、`TemplateEditor` 等仍是规划模块。

```text
FastXLSX
├── api
│   ├── Workbook
│   ├── WorksheetWriter
│   ├── WorksheetReader
│   └── TemplateEditor
├── opc
│   ├── PackageReader
│   ├── PackageWriter
│   ├── PartIndex
│   └── RelationshipGraph
├── xml
│   ├── EventReader
│   ├── EventWriter
│   ├── FastXmlWriter
│   └── OptionalDomDocument
├── workbook
│   ├── WorkbookModel
│   ├── SheetCatalog
│   ├── DefinedNames
│   └── DocumentProperties
├── worksheet
│   ├── RowStreamWriter
│   ├── RowStreamReader
│   ├── WorksheetRewriter
│   ├── CellEncoder
│   └── DimensionTracker
├── style
│   ├── StyleRegistry
│   ├── NumberFormatRegistry
│   └── StyleXmlWriter
└── strings
    ├── InlineStringPolicy
    ├── SharedStringTable
    └── StringEscaper
```

## 两条主路径

### 1. 创建新 XLSX

```text
WorkbookBuilder
→ WorksheetWriter
→ XML streaming
→ OPC package writer
→ .xlsx
```

这条路径禁止 DOM。

### 2. 编辑已有 XLSX

```text
PackageReader
→ PartIndex
→ 修改目标 part
→ 未修改 part 原样复制
→ 被修改 part 流式重写或局部 DOM 重写
→ PackageWriter
```

这条路径允许局部 DOM，但不能对大型 worksheet 使用 DOM。

## DOM 使用边界

允许 DOM：

- `workbook.xml`
- `workbook.xml.rels`
- `[Content_Types].xml`
- `docProps/*.xml`
- 较小的 `styles.xml`
- 小型 drawing / comments / table part

禁止 DOM：

- 大型 `worksheet.xml`
- 大型 `sharedStrings.xml`
- 批量数据写入路径
- 大型模板填充路径

## 核心约束

- 所有大数据 API 必须接受 row iterator / chunk writer。
- 不允许为了方便 API 而强制持有完整 worksheet。
- 未知 part 默认保留。
- 修改已有文件时，优先 part-level rewrite，而不是全量解析重建。
- API 设计必须向性能主线靠齐，不能为了易用性让大型 worksheet 进入 DOM 或完整 cell matrix。
- 便利 API 必须显式标明适用范围；只适合小文件的 API 应归入 in-memory 路径。
- public API 需要文档注释，说明模式、内存行为、随机访问限制和性能注意事项。

更详细的 API 设计和文档注释要求见 [API 设计与文档注释](API_DESIGN_AND_DOCUMENTATION.md)。
