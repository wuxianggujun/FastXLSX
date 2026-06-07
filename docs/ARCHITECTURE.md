# FastXLSX 架构设计

## 核心判断

FastXLSX 采用 **共享 OpenXML/OPC 底座 + Streaming / Patch / In-memory 三条 API
路径** 的架构。流式写入是大文件性能主线，但编辑能力也是核心主线，不应被设计成
streaming writer 的附属补丁。

主开发环境为 **C++20 / MSVC 2026**。架构设计优先保证该环境下的工程质量、
性能和可维护性，再逐步扩展到其他编译器。

原因很直接：

- 大数据写入时，DOM 会带来不可接受的内存占用。
- 编辑已有 XLSX 时，完全拒绝 DOM 会让小型 XML part 的修改变得复杂且脆弱。
- 最合理的边界是：worksheet 大 part 流式处理，小型元数据 part 可选择 DOM。
- 小文件随机编辑和大文件受控 patch 是两种不同能力，不能用同一个完整 worksheet
  cell matrix 模型覆盖所有场景。

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
- 通过 EditPlan / dependency analysis 管理 sheet、relationships、sharedStrings、
  styles、tables、drawings、defined names 和 calc metadata 的联动。

## 依赖边界

FastXLSX 只把通用底层能力交给第三方库。

```text
minizip-ng      ZIP package 读写
zlib-ng / zlib  DEFLATE 压缩
Expat           大型 XML event parser
pugixml         小型 XML DOM
stb             Phase 5 图片解码和尺寸读取
```

XLSX 语义层必须由 FastXLSX 自己实现：

- OPC part 索引。
- relationships 管理。
- worksheet row / cell 编码。
- sharedStrings / inlineStr 策略。
- styles registry。
- part-level rewrite。
- 图片 media part、drawing XML、relationships、content types 和 anchor 语义。

更详细的依赖选择见 [依赖策略](DEPENDENCIES.md)。

## 目标总体分层（部分模块尚未实现）

当前已实现的是 `Workbook` / `Worksheet` / `Cell`、`WorkbookWriter` /
`WorksheetWriter` / `CellView`，以及内部 OPC manifest / relationships /
`PartIndex` / `RelationshipGraph` / content type registry 基础。新建 workbook
输出已有内部 package writer boundary，默认 stored bootstrap，opt-in minizip-ng
DEFLATE backend。`PackageReader`、已有文件编辑用 public `PackageWriter`、
`WorksheetReader`、`TemplateEditor` 等仍是规划模块。

```text
FastXLSX
├── api
│   ├── Workbook
│   ├── WorksheetWriter
│   ├── PackageEditor
│   ├── InMemoryWorkbook
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
├── edit
│   ├── EditPlan
│   ├── DependencyAnalyzer
│   ├── ReferencePolicy
│   └── PartRewritePlanner
├── style
│   ├── StyleRegistry
│   ├── NumberFormatRegistry
│   └── StyleXmlWriter
└── strings
    ├── InlineStringPolicy
    ├── SharedStringTable
    └── StringEscaper
```

## 三条 API 路径

### 1. Streaming：创建新 XLSX

```text
WorkbookBuilder
→ WorksheetWriter
→ XML streaming
→ OPC package writer
→ .xlsx
```

这条路径禁止 DOM。

### 2. Patch：编辑已有 XLSX（规划路径）

```text
PackageReader
→ PartIndex
→ EditPlan / DependencyAnalyzer
→ 修改目标 part
→ 未修改 part 原样复制
→ 被修改 part 流式重写或局部 DOM 重写
→ PackageWriter
```

这条路径允许局部 DOM，但不能对大型 worksheet 使用 DOM。它负责模板填充、
sheet 替换、已有文件局部修改、未知 part 保留和必要的联动 part 更新。

### 3. In-memory：小文件随机编辑（规划路径）

```text
Workbook::open
→ 小型 WorkbookModel / WorksheetModel
→ get_cell / set_cell / local object edits
→ PackageWriter
```

这条路径可以提供接近 `OpenXLSX` 的编辑体验，但必须显式声明适用范围：它服务小文件
和复杂局部编辑，不承诺百万行 worksheet 的低内存随机访问。

## EditPlan 和联动边界

编辑单个 sheet 不一定只影响一个 `sheetN.xml`。任何 Patch API 都应先生成或更新
EditPlan，明确哪些 part 原样复制、哪些 part 流式重写、哪些 part 局部 DOM 重写。

典型联动包括：

- `sharedStrings.xml`：字符串策略、索引迁移或重建。
- `styles.xml`：样式 id 复用、迁移或追加。
- worksheet `.rels`：hyperlinks、tables、drawings、images。
- `xl/tables/tableN.xml`：table range、totals metadata、style flags。
- `workbook.xml`：sheet 列表、defined names、calcPr。
- `calcChain.xml`：修改公式或数据后可能删除、重建或标记重算。
- drawings / charts / pivot caches / VBA / unknown parts：默认保留，只有明确支持时才改写。

第一阶段可采用保守策略：替换或 patch sheet 数据时保留未知 part，并设置
fullCalcOnLoad；涉及 sheet 重命名、删除、移动、table resize、chart reference 或
pivot cache 时，必须有明确的 ReferencePolicy，不能静默破坏联动。

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
- 编辑能力不能被降级为 streaming writer 的补丁；PackageEditor、In-memory editor 和
  EditPlan 应作为核心架构模块推进。
- public API 需要文档注释，说明模式、内存行为、随机访问限制和性能注意事项。

## 文件职责边界

FastXLSX 的实现应保持高内聚模块化，不要把所有功能持续堆进少数几个文件。

核心 writer 文件可以保留流程编排、生命周期管理、热路径调用和跨功能协调逻辑。
当功能形成独立语义边界时，细节实现应优先拆到独立模块或 detail helper，例如：

- conditional formatting
- data validations
- hyperlinks
- tables
- images / drawings
- styles
- sharedStrings
- OPC relationships / content types

拆分判断以实际复杂度为准：如果功能已有独立 public API、独立 XML 序列化、独立状态
结构、独立 QA helper 或大量边界测试，就应考虑拆分；如果只是少量协调代码或临时小改动，
不要为了“模块化”强行制造碎片。

public headers 可以继续保持稳定入口；内部 `.cpp`、XML writer helper、校验逻辑、
状态转换和测试文件应随功能成熟逐步按边界拆分。新增 `.cpp` 或测试文件时，必须同步
更新 `CMakeLists.txt` 或 `tests/CMakeLists.txt`。任何拆分都不能引入完整 worksheet
cell matrix、DOM 热路径或无关重构。

更详细的 API 设计和文档注释要求见 [API 设计与文档注释](API_DESIGN_AND_DOCUMENTATION.md)。
