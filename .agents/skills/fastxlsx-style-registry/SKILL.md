---
name: fastxlsx-style-registry
description: "实现或审查 FastXLSX 样式注册表、StyleId、CellAlignment、CellFont、CellFill、CellStyle、WorkbookWriter::add_style()、CellView::with_style()、xl/styles.xml、number formats、wrap-text alignment、bold/italic font、solid fill、worksheet s 属性、sharedStrings + styles 共存和样式 QA。用于 P9 styles、未来 full-font/full-fill/border/full-alignment、conditional formatting 与 styles 交互，或排查 Excel 修复样式输出。"
---

# FastXLSX Style Registry

## 必读文件

- `include/fastxlsx/streaming_writer.hpp`
- `src/streaming_writer.cpp`
- `include/fastxlsx/detail/opc.hpp`
- `src/opc.cpp`
- `tests/test_streaming_writer.cpp`
- `tests/test_opc.cpp`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`
- `docs/TESTING_WORKFLOW.md`
- `docs/TASK_PLAN.md`
- `docs/NEXT_STEPS.md`

先用 `rg` 确认当前符号和测试是否真实存在。不要把路线图里的完整 style subsystem
当成已实现能力。

## 当前事实

- 当前 P9 基础切片支持 streaming-only new-workbook custom number format styles、窄
  wrap-text alignment styles、窄 bold/italic font styles 和窄 solid foreground fill styles。
- `StyleId` 是 workbook-local handle；默认构造是 style `0`。非默认 id 必须来自同一个
  `WorkbookWriter::add_style()`。
- `CellAlignment` 当前只支持 `wrap_text`；`CellFont` 当前只支持 `bold` / `italic`；
  `CellFill` 当前只支持 solid foreground `ArgbColor`；`CellStyle` 当前包含
  `number_format`、可选 `alignment`、可选 `font` 和可选 `fill`。完全空的 style
  会被拒绝；`alignment` 为空或 `wrap_text=false` 不贡献 style 属性；`font` 为空或
  `bold=false && italic=false` 不贡献 style 属性；`fill` 为空不贡献 style 属性。
- 重复完整 style 复用同一个 `StyleId`；相同 number format 在不同 style 组合中复用同一个
  custom `numFmtId`；相同 bold/italic font 组合在不同 style 组合中复用同一个 `fontId`；
  相同 foreground ARGB fill 在不同 style 组合中复用同一个 `fillId`。
  number format 字符串只按精确文本匹配，不做 Excel 语义规范化。
- `CellView::with_style()` 只把 style id 作为小句柄放进 cell view，不持有完整 cell
  对象或 worksheet matrix。
- `WorksheetWriter::append_row()` 必须在推进 row count、dimension、sharedStrings state
  或 formula recalculation metadata 前拒绝无效或 foreign `StyleId`，包括 foreign id
  数值与目标 workbook 已有 style id 碰撞的情况。
- 注册非默认 style 后，`WorkbookWriter::close()` 会写 `xl/styles.xml`、styles content
  type override 和 workbook styles relationship。styles 是 workbook-level 小型 XML part，
  不创建 worksheet `.rels`。
- worksheet cell 使用非默认 style 时写 `s="N"`；默认 style 不写 `s="0"`。
- sharedStrings + styles 可以共存：styled shared string cell 同时写 `s="N"` 和 `t="s"`；
  workbook relationship 顺序保持 sheets、sharedStrings、styles。
- wrap-text alignment 只写 `cellXfs` 里的 `applyAlignment="1"` 和
  `<alignment wrapText="1"/>`；不计算 row height，不代表完整 alignment。
- bold/italic font 只写 `<fonts>` 里的 `<b/>` / `<i/>`、`fontId` 和 `applyFont="1"`；
  不代表完整 font control、font color、font size、font name、underline 或 rich text。
- solid fill 只写 `<fills>` 里的 solid `<patternFill>`、`fgColor rgb`、
  `bgColor indexed="64"`、`fillId` 和 `applyFill="1"`；不代表 gradient fill、
  任意 pattern fill、theme/tint/indexed palette fill 或 `dxfs`。
- 当前 two-/three-color conditional color scale、basic data bar 和 basic 3Arrows icon set
  不是 style registry 功能：它们写 worksheet-local
  `<conditionalFormatting>`，不生成 `styles.xml` 或 `dxfs`。不要把它当成 P9 styles
  完整 conditional formatting 支持。

## 推荐流程

1. 判断任务是修补现有 number format / wrap-text / bold-italic font / solid-fill slice，还是新增 full-font/full-fill/border/full-alignment 等后续 slice。
2. 保持 style registry 在 workbook scope；不要在 worksheet row/cell 热路径里拼 ad hoc
   style XML。
3. 修改 public API 时同步 Doxygen 注释：Streaming / new-workbook-only、workbook-local
   id、内存随注册样式增长、package side effects 和不支持项。
4. 修改 `xl/styles.xml` 时同时检查 content type override 和 workbook relationship。
5. 修改 cell XML 时确认默认 style 仍省略 `s="0"`，并确认字符串、布尔、数字和公式 cell
   都能携带 `s="N"`。
6. 修改 validation 时先写失败不污染状态的测试，再改实现。
7. 结构不确定或 Excel 修复时，用 Excel、`openpyxl` 或 `XlsxWriter` 生成参考 workbook，
   拆包对比 XML 语义。

## 测试要求

默认 CTest 应覆盖：

- `xl/styles.xml` 存在且包含默认 font/fill/border/cellStyle skeleton。
- custom `numFmts`、`numFmtId >= 164`、formatCode XML attribute escape。
- wrap-text alignment 写 `applyAlignment="1"` 和 `<alignment wrapText="1"/>`。
- alignment-only style 不创建 custom `numFmt`；number format + alignment 组合复用相同
  format string 的 custom `numFmtId`。
- bold/italic font 写 `<fonts>`、`<b/>`、`<i/>`、`fontId` 和 `applyFont="1"`。
- font-only style 不创建 custom `numFmt`；number format + bold 组合复用相同 font 的
  `fontId`。
- solid fill 写 `<fills>`、solid `<patternFill>`、`fgColor rgb`、`fillId` 和
  `applyFill="1"`。
- fill-only style 不创建 custom `numFmt`；number format + fill / font + fill 组合复用相同
  foreground ARGB 的 `fillId`。
- `cellXfs` 默认 style + custom style 计数。
- worksheet `s="N"` 引用，默认 cell 不写 `s="0"`。
- number/string/boolean/formula cell 都能携带 style。
- styles 不创建 worksheet `.rels`。
- sharedStrings + styles relationship 顺序和 styled shared string cell。
- foreign `StyleId` 拒绝，且失败不推进 row number / dimension / sharedStrings /
  formula recalculation metadata。
- 空 `CellStyle`、close 后 `add_style()` 等公开错误路径。

本地 QA 固定命令：

```powershell
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
py tools\verify_styles_number_formats.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-styles-number-formats.xlsx `
  --shared-input build\windows-nmake-release\tests\fastxlsx-streaming-styles-shared-strings.xlsx `
  --alignment-input build\windows-nmake-release\tests\fastxlsx-streaming-styles-alignment.xlsx `
  --font-input build\windows-nmake-release\tests\fastxlsx-streaming-styles-fonts.xlsx `
  --fill-input build\windows-nmake-release\tests\fastxlsx-streaming-styles-fills.xlsx `
  --work-dir build\qa\styles-number-formats
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_styles_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-styles-number-formats.xlsx `
  -SharedPath build\windows-nmake-release\tests\fastxlsx-streaming-styles-shared-strings.xlsx `
  -AlignmentPath build\windows-nmake-release\tests\fastxlsx-streaming-styles-alignment.xlsx `
  -FontPath build\windows-nmake-release\tests\fastxlsx-streaming-styles-fonts.xlsx `
  -FillPath build\windows-nmake-release\tests\fastxlsx-streaming-styles-fills.xlsx
```

`verify_styles_number_formats.py` 做拆包 XML、`openpyxl` 语义和可选 `XlsxWriter`
参考 workbook。`verify_styles_excel.ps1` 用本机 Excel COM 只读打开样例，核对值、
公式、NumberFormat、WrapText、Font.Bold、Font.Italic、Interior.Pattern 和 Interior.Color。
这些是本地 QA，不是运行时依赖，也不是默认 CI 强制项。

## 禁止事项

- 不要把 P9 写成完整 styles 或 Excel formatting parity。
- 不要声称支持 full font control、full fill/pattern control、border、full alignment、rich text、dxf-backed conditional formatting、
  named styles、date cell type 或 existing-file style preservation，除非代码和测试已覆盖。
- 不要把 `StyleId::value()` 当成跨 workbook 稳定 id。它只用于诊断和结构测试。
- 不要为了样式方便引入 worksheet DOM、完整 cell matrix 或 cell map。
- 不要把 `openpyxl`、`XlsxWriter` 或 Excel COM 加为运行时依赖。
