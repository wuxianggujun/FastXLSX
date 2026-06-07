# 项目定位

## 一句话定位

FastXLSX 是一个 **C++20 / MSVC 2026 优先、可编辑、流式优先、局部 DOM 可选**
的高性能 XLSX/OpenXML 引擎。

它的目标是覆盖 C++ `OpenXLSX` 的高频编辑和写入场景，同时在大数据写入、
模板替换、批量导出和内存占用上明显优于 `OpenXLSX` 的 DOM 主路径。

## 不是做什么

FastXLSX 不是 `FastExcel` 的小版本升级。

它也不是简单包装 `OpenXLSX`、`xlnt`、`libxlsxwriter` 或 `QXlsx`。

它更不是只追求“能生成 xlsx”的轻量 writer。

FastXLSX 的目标是成为一个新的核心引擎：

```text
OpenXML package engine
+ streaming worksheet writer
+ editable XLSX package model
+ OpenXLSX-like high-level API
```

这个定位不是把编辑能力降级为流式 writer 的附属功能。正确边界是：

```text
共享 OpenXML / OPC 底座
+ Streaming API：新建大文件和批量导出
+ Patch API：已有 XLSX 的 part-level 编辑和未知 part 保留
+ In-memory API：小文件完整随机编辑体验
```

## 和参考库的关系

FastXLSX 会参考现有 C++ XLSX 库，但不以复刻为目标。

`OpenXLSX` 的核心价值是编辑体验和 API 直观性。
它的问题是大型 worksheet 走 DOM 后内存占用过高，所以 FastXLSX 不采用完整
worksheet DOM 作为大文件主路径。

`xlnt` 的核心价值是事件式 XML 读写和 producer / consumer 分层。
它比 `OpenXLSX` 更接近 FastXLSX 的方向，但普通 workbook API 仍然以完整内存
模型为中心，所以 FastXLSX 只吸收它的分层思想，不照搬它的内存模型。

更详细的对比见 [技术对比](TECHNICAL_COMPARISON.md)。

## 和 FastExcel 的关系

`FastExcel` 是旧架构和经验来源。

FastXLSX 是新架构项目：

- 重新划分模块边界。
- 重新设计大数据写入路径。
- 避免大型 XML DOM。
- 保留局部 DOM 作为编辑辅助。
- 以更清晰的 OpenXML / OPC 分层作为基础。

建议关系：

```text
FastExcel   旧项目、参考实现、经验来源
FastXLSX    新项目、新架构、新性能目标
TinaXlsx    性能思想参考，不作为主线
```

## 核心卖点

### 1. 编辑能力是一等主线

FastXLSX 的目标不是单纯 high-performance writer，而是可编辑的 OpenXML 引擎。
编辑能力必须和流式写入共享同一套 OPC、relationships、content types、styles、
sharedStrings 和 worksheet XML 语义层。

核心编辑路径分三类：

- `Patch`：已有 XLSX 编辑、模板填充、part-level rewrite、未知 part 原样保留。
- `In-memory`：小文件随机编辑，例如未来 `get_cell()` / `set_cell()` / 局部样式修改。
- `Streaming rewrite`：大型 worksheet 的受控范围修改、sheet 替换和条件 patch。

不承诺的是“百万行 worksheet 像二维数组一样任意随机读写，同时仍保持低内存”。

### 2. 流式优先

大数据写入必须走流式路径。

尤其是：

- `worksheet.xml`
- 大型 `sharedStrings.xml`
- 模板 sheet 替换
- 百万行级导出

这些路径不允许依赖 DOM。

### 3. 局部 DOM 编辑

DOM 不是被完全禁止，而是被限制在合适的位置。

允许 DOM 的典型场景：

- `workbook.xml`
- relationships
- `[Content_Types].xml`
- `docProps/*.xml`
- 小型 `styles.xml`
- 小型 table、drawing、comments part

这让编辑已有 XLSX 更可靠，也不会牺牲大数据路径性能。

### 4. 对标 OpenXLSX 高频功能

优先覆盖：

- 创建 workbook。
- 多 sheet 写入。
- 数字、文本、布尔、日期、公式。
- 样式、列宽、行高。
- 合并单元格。
- 冻结窗格。
- 自动筛选。
- 文档属性。
- 命名区域。
- 模板填充。

第一阶段不追求：

- 完整公式计算引擎。
- 任意大型 worksheet 随机编辑。
- 透视表创建。
- 宏编辑。
- 复杂图表完整创建。

### 5. 性能优先但不牺牲编辑能力

FastXLSX 的技术边界是：

```text
大 part 流式处理
小 part 可 DOM 编辑
未知 part 原样保留
修改 part 重写输出
EditPlan / dependency analysis 决定联动 part
```

这比“纯 DOM workbook”更适合大文件，也比“绝对纯流式”更适合编辑已有文件。

## 目标用户

FastXLSX 面向：

- 需要批量导出 Excel 报表的 C++ 项目。
- 需要处理百万行级 XLSX 的工具。
- 需要替代 `OpenXLSX` 性能瓶颈的场景。
- 需要在服务端生成或改写 XLSX 的系统。
- 需要可控 OpenXML 输出的工程项目。

## 技术关键词

```text
C++20
MSVC 2026 first
OpenXML
OPC package
streaming XML
part-level rewrite
optional local DOM
Patch API
In-memory editor
EditPlan
dependency analyzer
OpenXLSX-like API
xlnt-inspired producer consumer split
high-performance editable XLSX engine
editable XLSX package
```

## 成功标准

FastXLSX 的成功标准不是“API 看起来像 OpenXLSX”。

真正的成功标准是：

1. OpenXLSX 高频功能可覆盖。
2. 大数据写入性能明显更好。
3. 大型 worksheet 内存占用稳定。
4. 小文件随机编辑体验可靠且边界清晰。
5. 编辑已有 XLSX 时尽量保留未知结构。
6. 架构上能长期扩展，而不是堆功能。
