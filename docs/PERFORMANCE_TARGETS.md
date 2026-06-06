# 性能目标

## 目标基准

FastXLSX 的性能目标是明显优于 C++ `OpenXLSX` 的大数据写入场景。

同时，FastXLSX 也应该和 `xlnt` 的 streaming API 建立基准对比。
目标不是在所有功能上压过 `xlnt`，而是在流式写入、模板替换和 part-level rewrite
这些主路径上保持更低的内存占用和更直接的 API。

本文件中的 1,000 万 / 5,000 万 cells 数字是目标和验收方向，不是当前已记录的
实测结论。任何性能声明必须来自显式 opt-in benchmark，并记录数据规模、压缩设置、
字符串策略、总耗时、峰值内存、输出大小和办公软件打开结果。

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

- 数据规模。
- 压缩设置。
- 字符串策略。
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

目标上必须支持压缩等级配置；当前 public compression-level 配置尚未集成，
不能据此宣称已经支持压缩等级调优。

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

## 手工 Benchmark Workflow

当前最小 benchmark 入口是 opt-in 手工工具，不进入默认 CTest：

```powershell
cmake --preset windows-nmake-release-benchmark
cmake --build --preset windows-nmake-release-benchmark --target fastxlsx_bench_streaming_writer
.\build\windows-nmake-release-benchmark\benchmarks\fastxlsx_bench_streaming_writer.exe `
  --scenario numeric --rows 100000 --cols 10 --sheets 1 `
  --string-strategy inline `
  --output .\build\windows-nmake-release-benchmark\benchmarks\fastxlsx-bench-numeric.xlsx `
  --result .\build\windows-nmake-release-benchmark\benchmarks\fastxlsx-bench-numeric.json
```

如果需要验证 opt-in minizip backend，使用
`windows-nmake-release-benchmark-minizip` preset。生成的 JSON 中
`office_open` 初始为 `not_run`；只有实际用 Excel / WPS / LibreOffice 打开后，
才能把兼容性结果写成已验证事实。

如果不传 `--output` / `--result`，当前工具默认把结果写到 benchmark target 的
binary dir，例如 `build/windows-nmake-release-benchmark/benchmarks/`。手工工具会
拒绝超过 1024 个 worksheet 的输入；这只是 benchmark 工具边界，不代表
FastXLSX public API 的 worksheet 数量承诺。
