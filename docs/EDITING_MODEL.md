# 编辑模型

## 目标

FastXLSX 需要同时满足两个场景：

1. 高性能创建新 XLSX。
2. 编辑已有 XLSX，同时尽量保留原文件结构。

因此编辑模型不能是纯内存 workbook，也不能是完全无状态流。

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

### In-memory Mode

用于小文件和复杂编辑。

特点：

- API 最方便。
- 适合小数据量。
- 不承诺大文件低内存。

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

## 关键原则

不要为了编辑方便，让所有路径都变成 DOM。

也不要为了纯流式洁癖，把小型 XML 修改写得过度复杂。

FastXLSX 的边界是：

```text
大 part 流式，小 part 可 DOM，未知 part 原样保留。
```

