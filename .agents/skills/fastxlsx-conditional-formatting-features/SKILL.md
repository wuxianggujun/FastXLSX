---
name: fastxlsx-conditional-formatting-features
description: "实现或审查 FastXLSX streaming conditional formatting。用于 two-/three-color color scales、basic data bars、basic 3Arrows icon sets、priority 共享、multi-range sqref、本地 Excel/openpyxl/XlsxWriter 验证，以及判断 advanced/custom icon sets、advanced data bars、dxf/styles、formula/cellIs 是否越界。"
---

# FastXLSX Conditional Formatting Features

## 何时使用

- 修改 `WorksheetWriter::add_conditional_color_scale()`、
  `WorksheetWriter::add_conditional_data_bar()` 或
  `WorksheetWriter::add_conditional_icon_set()`。
- 添加新的 conditional formatting 规则类型，例如 advanced/custom icon set、formula/cellIs、
  top/bottom、duplicate/unique 或 advanced data bar 属性。
- 排查 Excel 修复弹窗、`<conditionalFormatting>` XML、priority、multi-range
  `sqref`、styles/dxfs 副作用或本地 QA helper 失败。

## 必读文件

- `include/fastxlsx/streaming_writer.hpp`
- `src/streaming_writer.cpp`
- `tests/test_streaming_writer.cpp`
- `docs/TESTING_WORKFLOW.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/TASK_PLAN.md`
- `tools/verify_conditional_formatting_color_scales.py`
- `tools/verify_conditional_formatting_color_scales_excel.ps1`
- `tools/verify_conditional_formatting_data_bars.py`
- `tools/verify_conditional_formatting_data_bars_excel.ps1`
- `tools/verify_conditional_formatting_icon_sets.py`
- `tools/verify_conditional_formatting_icon_sets_excel.ps1`

## 当前事实

- Color scale 是 streaming-only new-workbook worksheet metadata。当前支持
  two-/three-color `colorScale`，写 worksheet-local
  `<conditionalFormatting><cfRule type="colorScale">`。
- Data bar 是 streaming-only new-workbook worksheet metadata。当前只支持 basic
  `dataBar`，写两个 `<cfvo>` 和一个 inline ARGB `<color>`。
- Icon set 是 streaming-only new-workbook worksheet metadata。当前只支持 built-in
  `3Arrows`，写三个 finite 且严格递增的 `<cfvo>` threshold，可选写
  `showValue="0"` 和 `reverse="1"`。
- `ColorScaleValueType` 和 `DataBarValueType` 的 `Minimum` / `Maximum` 不写
  `val`；`Number` / `Percent` / `Percentile` 必须是 finite numeric value。
- `IconSetValueType` 只支持 `Number` / `Percent` / `Percentile`，threshold 必须是
  finite numeric value。
- Color scale、data bar 与 icon set 共享同一 worksheet-local priority 序列；失败调用不能消耗
  priority 或污染 worksheet metadata。
- Multi-range overload 会复制 `CellRange` 列表并写成一个空格分隔 `sqref`；当前不排序、
  合并、去重或检查重叠。
- 当前基础切片不生成 `styles.xml`、`dxfs`、`xl/metadata.xml`、worksheet `.rels`、
  workbook relationships、content type entries、cell text 或 `<calcPr>`。

## 推荐流程

1. 先判断新规则是否能仅写 worksheet XML。若需要 styles/dxfs、relationships、
   content types 或 existing-file preservation，先拆成更小设计任务。
2. Public header 中新增 API 时同步 Doxygen 注释，写清 Streaming/new-workbook-only、
   range/rule 拷贝成本、finite endpoint、priority、multi-range 和副作用边界。
3. 在 `src/streaming_writer.cpp` 中保持 lightweight metadata state，不读取历史 rows，
   不持有完整 worksheet cell matrix。
4. 所有 validation 通过后再 push state 和分配/推进 priority，确保失败调用无副作用。
5. 结构测试先覆盖拆包 XML，再跑本地 Python helper 和 Excel COM helper。

## 常用命令

```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release --output-on-failure

py tools\verify_conditional_formatting_color_scales.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_conditional_formatting_color_scales_excel.ps1

py tools\verify_conditional_formatting_data_bars.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_conditional_formatting_data_bars_excel.ps1

py tools\verify_conditional_formatting_icon_sets.py
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_conditional_formatting_icon_sets_excel.ps1
```

## 禁止事项

- 不要把 color scale、basic data bar 或 basic 3Arrows icon set 写成完整 conditional formatting。
- 不要为了 API 易用性让 large worksheet 进入 DOM、cell map 或完整 worksheet matrix。
- 不要把 advanced data bar 的 negative color、axis、border、gradient、`extLst` 当成已支持。
- 不要声称支持 formula/cellIs、advanced/custom icon sets、top/bottom、duplicate/unique、dxf-backed styles、
  existing-file editing、conflict handling 或完整 Excel UI，除非对应代码和测试已落地。
- 不要把 `openpyxl`、`XlsxWriter` 或 Excel COM 当成运行时依赖；它们只用于本地 QA/参考。

## 验证清单

- CTest 覆盖 `<conditionalFormatting sqref>`、`<cfRule type>`、`<cfvo>`、`<color rgb>`、
  priority、multi-range `sqref` 和 suffix 顺序。
- 失败路径覆盖 invalid range、empty range list、non-finite endpoint、非法 endpoint shape、
  icon set descending/duplicate thresholds、failed call state hygiene 和 mutation-after-close。
- Package absence 覆盖无 `styles.xml`、`dxfs`、`xl/metadata.xml`、worksheet `.rels`、
  workbook relationship、content type entries、cell text 和 `<calcPr>`。
- 本机 Excel 可用时，用 Excel COM 只读打开样例，确认无修复弹窗并核对规则可见性。
- 结构异常时，用 Excel、`openpyxl` 或 `XlsxWriter` 创建语义等价参考 workbook，拆包对比
  `[Content_Types].xml`、workbook relationships 和 worksheet XML。
