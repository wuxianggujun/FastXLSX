# 性能目标

## 目标基准

FastXLSX 的性能目标是明显优于 C++ `OpenXLSX` 的大数据写入场景。

同时，FastXLSX 也应该和 `xlnt` 的 streaming API 建立基准对比。
目标不是在所有功能上压过 `xlnt`，而是在流式写入、模板替换和 part-level rewrite
这些主路径上保持更低的内存占用和更直接的 API。

重点不是在所有功能上最快，而是在以下场景建立优势：

- 百万行级数据导出。
- 多 sheet 批量导出。
- 数字、日期、布尔值密集写入。
- 字符串较多但可配置 inlineStr / sharedStrings。
- 模板 sheet 替换。

## 初始性能目标

### 1,000 万 cells

```text
数据形态：数字为主
内存目标：< 256 MB
耗时目标：30s - 180s
```

### 5,000 万 cells

```text
数据形态：数字 + 少量字符串
内存目标：< 1 GB
耗时目标：5min - 20min
```

### 字符串密集场景

```text
inlineStr 模式：优先低内存
sharedStrings 模式：优先文件体积
```

## 热点路径

优先优化：

- 单元格 XML 编码。
- XML 字符转义。
- 数字转字符串。
- 单元格引用生成。
- ZIP 压缩等级。
- sharedStrings 去重。
- 行级 buffer 复用。

## 对比对象

第一阶段基准至少覆盖：

- `OpenXLSX`：DOM 主路径写入和保存。
- `xlnt`：streaming writer 和常规 workbook API。
- `FastExcel`：旧项目中已有的高性能写入路径。

基准指标：

- 总耗时。
- 峰值内存。
- 输出文件大小。
- Excel / WPS / LibreOffice 打开兼容性。

## 非目标

第一阶段不追求：

- 完整公式计算引擎。
- 任意复杂图表生成。
- 任意大型 worksheet 随机编辑。
- 透视表创建。
- 宏编辑。

## 压缩策略

必须支持压缩等级配置：

```text
store       最快，文件最大
level 1-3   推荐默认，性能和体积平衡
level 6+    文件更小，但 CPU 成本更高
```

## 内存原则

大数据路径内存应该主要由以下部分组成：

- 当前行 buffer。
- 输出 buffer。
- 样式 registry。
- 字符串策略状态。
- ZIP writer 状态。

不得持有完整 worksheet cell matrix。
