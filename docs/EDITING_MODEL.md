# 编辑模型

## 目标

FastXLSX 需要同时满足三个场景：

1. 高性能创建新 XLSX。
2. 编辑已有 XLSX，同时尽量保留原文件结构。
3. 小文件复杂随机编辑，提供接近普通 workbook 对象模型的体验。

因此编辑模型不能是纯内存 workbook，也不能是完全无状态流。正确模型是共享
OpenXML / OPC 底座上的三条路径：Streaming、Patch、In-memory。

## 编辑策略

### Part-level rewrite

XLSX 是 ZIP + OpenXML parts。编辑已有文件时，基本单位应该是 part。

```text
读取 package
→ 建立 part 索引
→ 标记修改过的 part
→ 未修改 part 原样复制
→ 修改 part 重新生成
→ 输出新 package
```

这个策略能最大程度保留 Excel 文件里的未知结构。

### EditPlan / dependency analysis

编辑已有 XLSX 前，应先把本次操作转换成 EditPlan。EditPlan 不是完整 workbook DOM，
而是一个 part 级别的影响范围说明。

```text
用户操作
→ 识别目标 sheet / range / workbook metadata
→ 分析受影响 part
→ 决定 copy-original / stream-rewrite / local-DOM-rewrite
→ 写出新 package
```

单个 sheet 修改可能联动：

- `xl/worksheets/sheetN.xml`
- `xl/worksheets/_rels/sheetN.xml.rels`
- `xl/sharedStrings.xml`
- `xl/styles.xml`
- `xl/tables/tableN.xml`
- `xl/workbook.xml`
- `xl/calcChain.xml`
- drawings、charts、pivot cache、VBA 和未知扩展 part

默认策略应保守：未知或未修改 part 原样复制；修改数据后可设置 fullCalcOnLoad，并对
`calcChain.xml` 采用删除、重建或显式保留策略，不能静默留下错误联动。

## 大型 worksheet 编辑

大型 worksheet 不使用 DOM。

推荐方式：

```text
旧 sheet.xml event reader
→ row/cell transformer
→ 新 sheet.xml stream writer
```

适合：

- 修改某些单元格
- 追加行
- 删除/替换某些行
- 模板占位符替换
- 更新维度信息

不适合：

- 任意随机访问大量单元格
- 频繁反复修改同一个大型 sheet

这类场景需要用户选择内存模式或临时索引模式。

大型 worksheet 允许受控编辑，例如：

- 替换整个 sheet 数据。
- 替换某个连续 range。
- 根据 row/cell 条件做一次性 patch。
- 模板占位符替换。

这些能力通常是 O(rows) 扫描和重写，不是 O(1) 随机写入。

## 小型 XML 编辑

小型 XML part 可以使用 DOM，因为这样更可靠：

- 插入一个 sheet 关系。
- 修改 workbook sheet 列表。
- 修改 docProps。
- 修改 content types。
- 更新少量 style。

这些文件通常很小，DOM 成本可控。

## 三种模式

### Streaming Mode

用于新建和大数据写入。

特点：

- 最快。
- 内存最低。
- 不能随机修改已写出的历史行。

### Patch Mode

用于编辑已有文件。

特点：

- part 级别复制和替换。
- 大 part 流式重写。
- 小 part 可选 DOM。
- 通过 EditPlan 管理 relationships、content types、sharedStrings、styles、tables、
  drawings、defined names 和 calc metadata 的联动。
- 默认保留未知和未修改 part。

### In-memory Mode

用于小文件和复杂编辑。

特点：

- API 最方便。
- 适合小数据量。
- 不承诺大文件低内存。
- 可以提供真正随机编辑，例如 `get_cell()`、`set_cell()`、`erase_cell()`、局部样式修改。

## 建议 API 形态

```cpp
auto wb = fastxlsx::Workbook::create("out.xlsx");
auto ws = wb.addSheet("Data");

ws.writeRows(rows);        // 流式主路径
wb.save();
```

```cpp
auto editor = fastxlsx::PackageEditor::open("template.xlsx");
editor.replaceSheet("Data", rows);       // 流式替换
editor.setDocumentProperty("Author", "FastXLSX");
editor.saveAs("output.xlsx");
```

```cpp
auto book = fastxlsx::Workbook::open("small.xlsx");
auto& sheet = book.sheet("Data");
sheet.setCell(10, 3, fastxlsx::Cell::text("hello")); // 小文件 in-memory 随机编辑
book.saveAs("output.xlsx");
```

## 关键原则

不要为了编辑方便，让所有路径都变成 DOM。

也不要为了纯流式洁癖，把小型 XML 修改写得过度复杂。

FastXLSX 的边界是：

```text
大 part 流式，小 part 可 DOM，未知 part 原样保留，编辑前先分析联动 part。
```

编辑能力不是流式 writer 的附属补丁。`PackageEditor`、`WorksheetRewriter`、
小文件 `Workbook::open()` 和 EditPlan / dependency analysis 都应作为核心架构能力推进。
