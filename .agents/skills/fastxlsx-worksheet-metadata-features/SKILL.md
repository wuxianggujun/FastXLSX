---
name: fastxlsx-worksheet-metadata-features
description: "规划或实现 FastXLSX worksheet metadata 功能。用于 data validations、hyperlinks、conditional formatting、table references、worksheet .rels、Phase 5 早期切片，以及判断这些功能是否会破坏 streaming worksheet 热路径或需要 RelationshipGraph。"
---

# FastXLSX Worksheet Metadata Features

## 必读文件

- `include/fastxlsx/streaming_writer.hpp`
- `src/streaming_writer.cpp`
- `tests/test_streaming_writer.cpp`
- `include/fastxlsx/detail/opc.hpp`
- `src/opc.cpp`
- `docs/ARCHITECTURE.md`
- `docs/EDITING_MODEL.md`
- `docs/TASK_PLAN.md`
- `docs/TESTING_WORKFLOW.md`
- `docs/API_DESIGN_AND_DOCUMENTATION.md`

先用 `rg` 确认符号是否真实存在。当前没有完整 Phase 5，也没有
`PackageReader`、生产 `PackageWriter` 或已有文件编辑管线。内部
`PartIndex` / `RelationshipGraph` 已存在，但它们只是 detail groundwork。

## 当前事实

- `WorkbookWriter` / `WorksheetWriter` 是当前 streaming 新建 workbook 路径。
- worksheet 已有 metadata 写出骨架：列宽、冻结窗格、自动筛选、合并单元格、
  streaming-only data validations、external/internal hyperlinks 和 tables。
- `WorksheetWriter::add_data_validation()` 当前只支持新建 workbook 的 worksheet XML
  metadata，写出 `<dataValidations>`，不新增 worksheet `.rels`、content types 或
  package relationships。当前 `DataValidationRule` 还支持 prompt/error metadata：
  `show_input_message`、`show_error_message`、`error_style`、`prompt_title`、`prompt`、
  `error_title` 和 `error` 只写成 `<dataValidation>` attributes，空字符串和 false
  flags 省略，不生成 `styles.xml`，也不代表完整 Excel UI。`hide_dropdown_arrow`
  只对 list validation 有效，写出 OpenXML 反向命名的 `showDropDown="1"` 来隐藏
  in-cell dropdown arrow；false 时省略。当前还支持一条
  data validation 规则对应多个 `CellRange`：range 列表复制进 writer state，并写成
  单个 `<dataValidation>` 的空格分隔 `sqref`；`count` 仍按规则数计算，不按区域数
  计算。
- `WorksheetWriter::add_external_hyperlink()` 当前只支持新建 workbook 的 external URL
  hyperlinks，写出 worksheet `<hyperlinks>` 和
  `xl/worksheets/_rels/sheetN.xml.rels`，relationship 使用 `TargetMode="External"`，
  `rId` 只在 worksheet owner 内分配。
- `WorksheetWriter::add_internal_hyperlink()` 当前只支持新建 workbook 的 internal
  workbook location hyperlinks，只在 worksheet `<hyperlinks>` 写 `location`，不创建
  worksheet `.rels`、workbook relationships、content type overrides，也不消耗
  worksheet-local `rId`。
- `WorksheetWriter::add_table()` 当前只支持新建 workbook 的 streaming-only tables，
  写出 worksheet `<tableParts>`、worksheet `.rels`、`xl/tables/tableN.xml` 和 table
  content type override；`TableOptions::show_totals_row` 只写 totals-row visibility
  metadata，`column_totals_functions` 只写 caller-supplied `totalsRowFunction`
  attributes。它不读取已写 header 行，不推断列名，也不生成 `styles.xml`。
- 大型 worksheet 路径禁止 DOM，metadata 必须以小向量/轻量结构记录，再在
  worksheet XML 正确位置输出。
- `RelationshipSet` 可表达 external target，但只有同时写 worksheet `<hyperlinks>`、
  worksheet `.rels` 并保持 `r:id` 一致时，才是实际 hyperlink feature。
- 图片不是 worksheet-only metadata。图片需要 media part、drawing part、drawing
  relationships、worksheet relationships、content types 和 anchors；`WorksheetWriter`
  最多保存轻量 anchor/reference metadata，package wiring 归 OPC graph/package work。
- `docProps/core.xml` 和 `docProps/app.xml` 是当前基础可配置 workbook/package
  小型 XML part 输出，不属于 worksheet metadata；它不生成 `docProps/custom.xml`，
  也不代表完整 document-properties API 或已有文件编辑。

## 推荐流程

1. 先判定功能是否只需要 worksheet XML metadata。
2. 若只改 worksheet XML，例如当前 data validations 基础切片，可以留在
   streaming new-workbook 路径，不引入 package relationship。
3. 若功能需要 worksheet `.rels`、新 part、content type override 或跨 part id，
   先基于内部 relationship graph / part index 做窄范围 package wiring，不要直接
   宣称完整 public feature。
4. 在 public header 增加 API 时，同步 Doxygen 注释，写清 Streaming 模式、
   顺序写入限制、内存行为、是否新增 relationships/content types。
5. 测试先覆盖 XML 结构，再做本机 Excel 可视化验证；结构异常时创建 Excel、
   `openpyxl` 或 `XlsxWriter` 参考文件，拆包对比 XML。

## 可切入切片

- Data validations：已有基础 streaming-only、新建 workbook、worksheet metadata 版本。
  当前覆盖 whole/decimal/list/date/time/textLength/custom 公式文本结构，以及
  prompt/error metadata attributes、attribute escape、空值省略、false flag 省略和
  `stop` / `warning` / `information` error styles，以及 list-only `showDropDown`
  反向语义；当前还覆盖 multi-area `sqref` 序列化、空 range list 拒绝和 multi-range
  内非法 range 拒绝。继续禁止已有文件
  编辑、DOM、公式解析、单元格值校验、区域排序/合并/去重、重叠检查和完整 Excel UI
  保证。
- Hyperlinks：已有基础 streaming-only、新建 workbook 版本，覆盖 external URL links
  和 internal workbook location links。External 写 cell ref + target URL + worksheet
  `.rels`；internal 写 cell ref + `location` 且不写 `.rels`。当前不写单元格文本、
  不创建 hyperlink 样式、不支持 target 存在性校验、已有文件编辑或完整 Excel UI 行为。
  `HyperlinkOptions` 当前只支持基础 `display` / `tooltip` worksheet attributes；非空
  字符串会复制进 writer state，空字符串省略，不改变 `.rels` / content types / styles。
- Conditional formatting：保持计划；如果只写 worksheet metadata，也必须确认样式、
  公式和 range 依赖不会进入大型 DOM。
- Tables：已有基础 streaming-only、新建 workbook 版本。当前只保存 range、table name、
  column names、style flags、`show_totals_row` 和 `column_totals_functions`；
  `show_totals_row` 只表示 table totals row 可见性，`column_totals_functions` 只写
  caller-supplied `totalsRowFunction` attributes，不计算 totals、不生成公式文本、
  `totalsRowLabel` 或样式。
  不支持 calculated columns、sort/filter criteria、custom styles、table resize、
  已有文件编辑或完整 Excel table UI。
- Images/Pictures：保持计划；不要把它当成 data validations 那样的纯 worksheet XML
  切片。需要 `fastxlsx-image-media-features` 和 OPC graph/package 边界。

## 禁止事项

- 不要把 Phase 5 写成已实现。
- 不要因为基础 hyperlink slices（external URL + internal location）已存在就宣称完整 hyperlinks 已支持。
- 不要因为 streaming-only table slice 已存在就宣称完整 tables 已支持。
- 不要让 data validation / conditional formatting API 持有完整 worksheet cell matrix。
- 不要在没有 PackageReader/PackageWriter 前宣称 existing XLSX editing 或 unknown part passthrough。
- 不要把 Excel、`openpyxl` 或 `XlsxWriter` 加为运行时依赖；它们只能用于测试和排障参考。

## 验证

- `ctest` 默认测试 60s 内完成。
- worksheet XML 中 metadata 位置符合 OpenXML 结构要求。
- data validations 结构测试应检查 `count`、`sqref`、`type`、`operator`、
  `allowBlank`、`formula1`、`formula2`、XML escape、invalid ranges、
  invalid rule shapes、关系缺失、与 relationship-backed metadata 共存时不消耗
  worksheet-local `rId`、validation-only worksheet 不声明 `xmlns:r`、`formula2`
  XML text escape、prompt/error attributes、attribute escape、empty string omission、
  false flag omission、`errorStyle` values、list-only `showDropDown="1"` 且不写
  `showDropDown="0"`、无 `styles.xml` / content type side effects，multi-area
  `sqref` token / `count` 语义、空 range list 和 multi-range 内非法 range，以及
  close 后 mutation。
- external hyperlinks 结构测试应检查 worksheet XML `r:id` 与 worksheet `.rels` 一致、
  target XML escape、同一 worksheet 多个 hyperlink、跨 worksheet owner-local `rId`、
  plain sheet 不生成 `.rels`、不污染 workbook relationships、不新增 content type
  override、invalid cell、empty target 和 close 后 mutation。
- internal hyperlinks 结构测试应检查 worksheet XML `location` attribute escape、
  internal-only worksheet 不生成 `.rels`、不声明 `xmlns:r`、不写 `r:id`、不污染 workbook
  relationships 或 content types，mixed external/internal 场景下 external `rId` 不被
  internal hyperlink 消耗，以及 invalid cell、empty location 和 close 后 mutation。
- hyperlink display/tooltip 结构测试应检查 external/internal 两条路径的 `display` 和
  `tooltip` attribute XML escape、display-only、tooltip-only、显式空 options 被省略、
  `.rels` 不写 display/tooltip、不新增 styles/content types，并确认 relationship id
  不偏移。
- tables 结构测试应检查 `xl/tables/tableN.xml`、worksheet `<tableParts>`、
  worksheet `.rels`、table content type override、owner-local `rId`、与 hyperlinks
  共存时的关系 id、多对象关系 id 回归、XML escape、table column attribute
  escape、invalid range/options、table style flags 且不生成 `xl/styles.xml`、
  `show_totals_row` true/false/default metadata、caller-supplied
  `totalsRowFunction`、无公式文本 / `totalsRowLabel` 生成、duplicate names 和 close
  后 mutation。
- 如果功能新增 relationships，检查 worksheet XML 引用、`.rels` id、content types 同步。
- 如果 data validations 与 hyperlinks / tables 共存，检查 `<dataValidations>` 仍在
  `<hyperlinks>` 和 `<tableParts>` 之前，且 hyperlinks/table 的 `rId` 不被 data
  validations 偏移。
- 本机有 Excel 时打开关键 `.xlsx`，确认无修复弹窗并检查可视化结果。
- 结构失败时拆包对比 FastXLSX 输出、Excel 修复文件和参考文件的 XML 语义。
