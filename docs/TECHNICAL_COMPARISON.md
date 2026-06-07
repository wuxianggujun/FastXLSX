# 技术对比

## 目的

这个文档用于固定 FastXLSX 与现有 XLSX C++ 库的技术边界。

当前重点参考对象：

- `OpenXLSX`
- `xlnt`
- 旧项目 `FastExcel`
- 实验项目 `TinaXlsx`

FastXLSX 不以复刻其中任意一个库为目标，而是吸收可用经验后重新划分主路径。

## 一句话结论

```text
OpenXLSX = DOM 编辑优先
xlnt     = 内存 workbook 模型 + 事件式 XML 读写 + streaming API 补充
FastXLSX = 共享 OpenXML/OPC 底座 + Streaming / Patch / In-memory 三路径
```

## 和 OpenXLSX 的区别

OpenXLSX 的优势是编辑体验直接，API 容易理解。

它的典型路径是：

```text
ZIP part
→ XML DOM
→ C++ 对象封装
→ 保存时序列化回 ZIP
```

这条路线适合：

- 小文件编辑。
- workbook、sheet、cell 的随机访问。
- API 易用性优先的场景。

但它不适合作为 FastXLSX 的大文件主路径：

- 大型 `worksheet.xml` 进入 DOM 后内存占用会快速放大。
- 修改大型 sheet 时容易变成整表加载和整表重写。
- 未知 XML 结构如果被解析重建，存在兼容性风险。

FastXLSX 的选择：

- 不采用完整 worksheet DOM 作为主路径。
- 小型 XML part 可以使用局部 DOM。
- 大型 worksheet 只能走事件流读取、过滤、改写或生成。
- 未修改 part 默认原样透传。
- 小文件随机编辑走独立 In-memory 路径，不把该模型用于百万行级默认路径。
- 已有文件编辑走 Patch / EditPlan / part-level rewrite，而不是 streaming writer 的补丁。

## 和 xlnt 的区别

xlnt 比 OpenXLSX 更接近 FastXLSX 的方向。

它有明确的事件式 XML 读写组件：

- `streaming_workbook_reader`
- `streaming_workbook_writer`
- `xml::parser`
- `xml::serializer`
- `xlsx_consumer`
- `xlsx_producer`

这些设计值得参考，尤其是 producer / consumer 分层。

但 xlnt 的常规 API 仍然以完整 workbook / worksheet 内存模型为中心。
普通 worksheet 内部会维护类似下面的结构：

```cpp
std::unordered_map<cell_reference, cell_impl> cell_map_;
std::unordered_map<column_t, column_properties> column_properties_;
std::unordered_map<row_t, row_properties> row_properties_;
std::vector<range_reference> merged_cells_;
```

这说明常规路径仍然会持有大量单元格和 worksheet 状态。

FastXLSX 应该吸收 xlnt 的部分：

- event parser / serializer。
- producer / consumer 分层。
- streaming reader / writer API。
- OpenXML part 级别的序列化组织。

FastXLSX 不应该照搬 xlnt 的部分：

- 不让完整 workbook 内存模型统治大文件路径。
- 不要求大数据写入先构造完整 worksheet。
- 不在百万行级导出时维护完整 cell map。
- 不把 streaming API 做成普通 workbook API 的附属补丁。

## FastXLSX 的目标位置

FastXLSX 应该位于 OpenXLSX 和 xlnt 的另一侧：

```text
编辑便利性：
OpenXLSX ~= FastXLSX In-memory 目标路径 > xlnt 常规 API > FastXLSX Streaming API

大数据写入内存稳定性：
FastXLSX 流式 API > xlnt streaming API > OpenXLSX DOM API

已有文件保真编辑：
FastXLSX Patch / part-level rewrite > OpenXLSX 全量 DOM 重建路径
```

这里的目标不是让所有场景都最快，而是让关键场景有明确优势：

- 百万行级导出。
- 多 sheet 批量生成。
- 模板 sheet 替换。
- 大型 worksheet 流式改写。
- 小型元数据 part 的可靠编辑。
- 未知 part、图表、图片、宏和扩展结构尽量保留。

## 对旧项目的取舍

`FastExcel` 是经验来源和旧实现参考。

FastXLSX 可以继承：

- OpenXML / OPC 的已有理解。
- 读写路径中的性能经验。
- 测试用例和兼容性样本。
- 已验证过的功能边界。

FastXLSX 不应该继承：

- 模块边界不清晰的历史包袱。
- 会迫使大数据路径持有完整 worksheet 的设计。
- 已经证明难以扩展的 API 假设。

`TinaXlsx` 更适合作为性能思想参考，不作为主线架构来源。

## 许可证参考

- `OpenXLSX`：BSD 3-Clause License。
- `xlnt`：MIT License。

许可证信息只用于评估参考和依赖风险。
FastXLSX 项目本身使用 MIT License，见根目录 `LICENSE`。

## 设计结论

FastXLSX 的架构原则应固定为：

```text
大 part 流式处理
小 part 局部 DOM
修改 part 精准重写
未知 part 原样保留
EditPlan 分析 sheet 和 workbook 联动
小文件 In-memory 随机编辑与大文件 Streaming/Patch 分离
高频 API 向 OpenXLSX 类库靠近
底层读写分层吸收 xlnt 的 producer / consumer 思路
```
