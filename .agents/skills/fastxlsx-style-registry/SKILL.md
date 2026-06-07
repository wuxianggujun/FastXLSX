---
name: fastxlsx-style-registry
description: "实现或审查 FastXLSX 样式注册表、StyleId、CellStyle、WorkbookWriter::add_style()、CellView::with_style()、xl/styles.xml、number formats、worksheet s 属性、sharedStrings + styles 共存和样式 QA。用于 P9 styles、未来 font/fill/border/alignment、conditional formatting 与 styles 交互，或排查 Excel 修复样式输出。"
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

- 当前 P9a 只支持 streaming-only new-workbook custom number format styles。
- `StyleId` 是 workbook-local handle；默认构造是 style `0`。非默认 id 必须来自同一个
  `WorkbookWriter::add_style()`。
- `CellStyle` 当前只包含 `number_format`，空字符串会被拒绝。重复 number format 只按
  字符串精确匹配去重，不做 Excel 语义规范化。
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
- 当前 two-color conditional color scale 不是 style registry 功能：它写 worksheet-local
  `<conditionalFormatting>`，不生成 `styles.xml` 或 `dxfs`。不要把它当成 P9a styles
  完整 conditional formatting 支持。

## 推荐流程

1. 判断任务是修补现有 number format slice，还是新增 font/fill/border/alignment 等后续 slice。
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
  --work-dir build\qa\styles-number-formats
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_styles_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-styles-number-formats.xlsx `
  -SharedPath build\windows-nmake-release\tests\fastxlsx-streaming-styles-shared-strings.xlsx
```

`verify_styles_number_formats.py` 做拆包 XML、`openpyxl` 语义和可选 `XlsxWriter`
参考 workbook。`verify_styles_excel.ps1` 用本机 Excel COM 只读打开样例，核对值、
公式和 NumberFormat。这些是本地 QA，不是运行时依赖，也不是默认 CI 强制项。

## 禁止事项

- 不要把 P9a 写成完整 styles 或 Excel formatting parity。
- 不要声称支持 font、fill、border、alignment、rich text、dxf-backed conditional formatting、
  named styles、date cell type 或 existing-file style preservation，除非代码和测试已覆盖。
- 不要把 `StyleId::value()` 当成跨 workbook 稳定 id。它只用于诊断和结构测试。
- 不要为了样式方便引入 worksheet DOM、完整 cell matrix 或 cell map。
- 不要把 `openpyxl`、`XlsxWriter` 或 Excel COM 加为运行时依赖。
