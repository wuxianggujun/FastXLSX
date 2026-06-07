---
name: fastxlsx-test-quality
description: "添加或排查 FastXLSX 测试、CTest/Catch2 集成、OpenXML 兼容性检查、本机 Excel 可视化验证、参考 XLSX 拆包 XML 对比、benchmark、内存/性能验证和质量门禁。用于让 ctest 跑起来、补单元测试、设计基准、验证生成 XLSX，或调查结构、性能、内存、兼容性回退。"
---

# FastXLSX Test Quality

## 必读文件

- `tests/CMakeLists.txt`
- `CMakeLists.txt`
- `docs/PERFORMANCE_TARGETS.md`
- `docs/TESTING_WORKFLOW.md`
- `docs/ROADMAP.md`
- `docs/TECHNICAL_COMPARISON.md`
- `README.md`
- `docs/DEPENDENCIES.md`

当前已有轻量 CTest 测试入口。尚未接入 Catch2。

## 当前测试事实

- `FASTXLSX_BUILD_TESTS` 默认 `ON`。
- 顶层 CMake 在启用测试时调用 `enable_testing()` 和 `add_subdirectory(tests)`。
- 当前测试入口包括：
  - `fastxlsx_tests` / `fastxlsx.unit`。
  - `fastxlsx_streaming_writer_tests` / `fastxlsx.streaming`。
  - `fastxlsx_opc_tests` / `fastxlsx.opc`。
  - `fastxlsx_image_tests` / `fastxlsx.image`。
- 当前测试生成基础和 streaming smoke `.xlsx`，通过 backend-neutral ZIP helper
  读取解压后的 entries，并检查 content types、relationships、worksheet XML、XML escape、cell reference、
  streaming writer metadata、data validations、sharedStrings 结构、基础可配置
  docProps 输出和内部 OPC manifest 基础。
- 当前 sharedStrings 结构测试还覆盖空表 package hygiene：启用
  `StringStrategy::SharedString` 但没有字符串 cell 时，不生成空 `xl/sharedStrings.xml`、
  sharedStrings content type 或 workbook relationship，worksheet 不写 `t="s"` 或
  `inlineStr`，公式仍触发 workbook recalculation metadata。
- 当前 streaming 测试还覆盖 file-backed/chunked worksheet package entry 的结构语义：
  解压后的 worksheet XML、entry name 唯一性、content type、relationships，以及
  stored/minizip backend 可用时的 backend-neutral 结果。
- 当前 streaming dimension 测试覆盖无行 worksheet、只含空行 worksheet、前导空行 +
  数据行 + 尾部空行：结构测试以拆包后的 worksheet XML 为准，确认空行写出
  `<row r="N"></row>`，只含空行时 dimension 仍为 `A1`，尾部空行会进入最后行
  dimension；不要用 Excel `UsedRange` 反推生成 XML 语义。
- 当前 streaming hot-path 边界测试还覆盖合法最大列 `XFD1` 正向输出、test-only
  hook 下合法最大行 `1048576` 稀疏输出，以及失败 `append_row()` 不推进 row number /
  dimension、不写入被拒绝 text/formula、不创建无用 sharedStrings part、不污染
  workbook `<calcPr>` 的状态卫生。
- 当前 streaming 行上限测试用 `FASTXLSX_ENABLE_TEST_HOOKS` 下的
  `fastxlsx::detail::testing_set_worksheet_row_count()` 注入内部 `row_count`，
  低成本触发第 1,048,577 行拒绝路径；这是测试 hook，不是 public API，也不是百万行性能
  benchmark。
- 当前 streaming Phase 3 metadata 测试覆盖公式 XML escape、row height、多个
  column width records、last-call-wins frozen panes、last-call-wins auto filters、
  多个 merged ranges、worksheet suffix ordering、workbook `<calcPr
  fullCalcOnLoad="1"/>` 重算请求，以及这些 metadata-only 功能不新增 worksheet
  relationships、workbook relationships 或 content type side effects；公式 cell 仍不生成
  `xl/calcChain.xml`。
- 当前数值边界测试覆盖：in-memory `Workbook::save()` 拒绝 numeric cell 的非有限值，
  并拒绝 row height 的 `0`、负数、`NaN`、`+Inf` 和 `-Inf`；streaming
  `WorksheetWriter::append_row()` 拒绝 numeric cell 的非有限值，并拒绝 row height 的
  `0`、负数、`NaN`、`+Inf` 和 `-Inf`；
  `WorksheetWriter::set_column_width()` 拒绝非正和非有限 width，避免 worksheet XML 写出
  `nan`、`inf`、`-inf` 或非法正数约束值。
- 当前 in-memory dimension / column-limit 测试覆盖空 worksheet、单个空行、
  `XFD1` 最大列 dimension，以及 16385 列在 `Workbook::save()` 序列化时被拒绝；
  这些是结构边界测试，不是大文件性能 benchmark。
- 当前 document properties 测试覆盖 in-memory `Workbook::set_document_properties()`
  和 streaming `WorkbookWriterOptions::document_properties`，检查 `docProps/core.xml`、
  `docProps/app.xml`、XML escape、package relationships、content type overrides，
  并确认不生成 `docProps/custom.xml`。固定本地 QA 入口是
  `tools/verify_document_properties.py` 和 `tools/verify_document_properties_excel.ps1`；
  前者做 ZIP/XML 与 `openpyxl` core-property 检查，后者只读打开 in-memory /
  streaming 样例并做 Excel COM smoke，属性精确语义以 XML/openpyxl 为准。
- 当前 streaming data validations 测试覆盖 `count`、`sqref`、`type`、`operator`、
  `allowBlank`、`formula1`、`formula2`、XML escape、invalid ranges、
  invalid rule shapes、package relationship absence、与 relationship-backed
  metadata 共存时不消耗 worksheet-local `rId`、suffix 顺序、validation-only
  worksheet 不声明 `xmlns:r`、`formula2` XML text escape、prompt/error attributes、
  attribute escape、prompt-only、error-only、empty string omission、false flag omission、
  `stop` / `warning` / `information` error styles、list-only `showDropDown="1"`
  反向隐藏 dropdown arrow 语义、无 `styles.xml` / content type side effects、
  multi-area `sqref` token / `count` 语义、空 range list 和 multi-range 内非法 range
  拒绝，以及 mutation-after-close。
- 当前 streaming external hyperlinks 测试覆盖 worksheet `<hyperlinks>`、worksheet
  `.rels`、target XML escape、同一 worksheet 多个 hyperlink、跨 worksheet
  owner-local `rId`、plain sheet absence、workbook relationship absence、content
  type override absence、optional display/tooltip attributes、invalid cell、empty target
  和 mutation-after-close。
- 当前 streaming internal hyperlinks 测试覆盖 worksheet `<hyperlink location="...">`、
  location attribute XML escape、internal-only sheet 不生成 worksheet `.rels`、不声明
  `xmlns:r`、不写 `r:id`、mixed external/internal 场景下 external `rId` 不被 internal
  消耗、不污染 workbook relationships 或 content types、optional display/tooltip
  attributes、invalid cell、empty location 和 mutation-after-close。
- 当前 streaming tables 测试覆盖 `xl/tables/tableN.xml`、worksheet `<tableParts>`、
  worksheet `.rels`、table content type override、owner-local `rId`、与 hyperlinks
  共存时的关系 id、多对象 relationship id 回归、XML escape、table column
  attribute escape、invalid
  ranges/options、table style flags 且不生成 `xl/styles.xml`、`show_totals_row`
  true/false/default metadata、caller-supplied `totalsRowFunction` / `totalsRowLabel`、
  无公式文本 / 空 label attribute、duplicate names、same-worksheet table range overlap
  拒绝、adjacent/cross-worksheet non-overlap 允许和 mutation-after-close。
- 当前 streaming conditional formatting 测试覆盖 two-/three-color color scale、
  basic data bar 和 basic 3Arrows icon set。
- color scale 覆盖 two-/three-color
  `<conditionalFormatting>` XML、`cfvo` min/max/percentile midpoint 与 numeric/percentile endpoints、
  ARGB 大写色值、multi-range `sqref`、priority 同 worksheet 递增和跨 worksheet 重置、
  与 mergeCells/dataValidations/hyperlinks/tables 的 suffix 顺序、relationship id 不偏移、
  无 `styles.xml` / `dxfs` / `xl/metadata.xml` / worksheet `.rels` / content type /
  workbook relationship / `<calcPr>` 副作用、invalid range、empty range list、非有限
  endpoint、lower `Maximum`、upper `Minimum`、失败调用不污染 state 和 mutation-after-close。
- data bar 覆盖 `<cfRule type="dataBar">`、两个 `<cfvo>`、一个 `<color rgb>`、
  optional `showValue="0"`、numeric/percentile endpoints、multi-range `sqref`、与 color scale 共享 worksheet-local
  priority、跨 worksheet priority 重置、suffix 顺序、relationship id 不偏移、无
  `styles.xml` / `dxfs` / `xl/metadata.xml` / worksheet `.rels` / content type /
  workbook relationship / `<calcPr>` 副作用、invalid range、empty range list、非有限
  endpoint、lower `Maximum`、upper `Minimum`、失败调用不污染 state 和 mutation-after-close。
- icon set 覆盖 `<cfRule type="iconSet">`、built-in `3Arrows`、三枚 `<cfvo>`、
  numeric/percent/percentile thresholds、showValue/reverse metadata、multi-range `sqref`、
  与 color scale/data bar 共享 worksheet-local priority、跨 worksheet priority 重置、
  suffix 顺序、relationship id 不偏移、无 `styles.xml` / `dxfs` / `xl/metadata.xml` /
  worksheet `.rels` / content type / workbook relationship / `<calcPr>` 副作用、
  invalid range、empty range list、非有限 threshold、descending/duplicate thresholds、
  unknown enum、失败调用不污染 state 和 mutation-after-close。
- 当前 image 测试覆盖默认 `stb` 构建下 PNG/JPEG 文件/内存尺寸和通道读取、
  unsupported memory/file header、empty memory buffer、empty file 和 missing file。
  当前 streaming 测试还覆盖默认 preset 下 PNG/JPEG media、drawing、rels、
  content types、worksheet `<drawing>` 结构。当前还
  覆盖 `ImageOptions` drawing metadata：`from_offset` / `to_offset` EMU offsets、
  two-cell marker `xdr:colOff` / `xdr:rowOff`、`xdr:twoCellAnchor editAs` 的
  `oneCell` / `absolute` / 默认 `twoCell`、`xdr:cNvPr name` / `descr`、XML
  attribute escape、空 description 省略、默认 `Picture N` 名称，以及无额外
  relationship / content type / media side effects。
- 当前没有 Catch2 集成。
- `Catch2` 和 `Google Benchmark` 是 planned-dev 依赖，尚未接入 CMake。
- GitHub Actions workflow 保留默认 vcpkg-backed `windows-nmake-release` job，并有
  opt-in vcpkg matrix job 跑 `windows-nmake-release-minizip`。CI 只做 configure/build/CTest；Excel COM、
  openpyxl / XlsxWriter 参考脚本和 benchmark 仍是本机或手工 QA。

## 单元测试优先级

优先补小而确定的测试：

- XML escape。
- 单元格引用生成。
- 数字/日期/布尔/字符串编码，包括非有限数字、row height 正数/有限值要求和
  column width 正数/有限值要求的拒绝路径。
- `inlineStr` 和 `sharedStrings` 策略。
- worksheet metadata：列宽、冻结窗格、自动筛选、合并单元格、data validations、
  external/internal hyperlinks、two-/three-color conditional color scales、basic data bars、
  basic 3Arrows icon sets 和 tables。
- dimension tracking。
- Excel 行/列边界正向和拒绝路径；行上限测试不要真实写满 1,048,576 行进默认
  CTest，合法最后一行可以用 test-only hook 做稀疏结构覆盖。
- row buffer 复用不变量。
- OPC content types 和 relationships。
- part index 与未修改 part 保留。

普通单元测试不要混入大型 benchmark，并遵守 60s 核心测试边界。

## 兼容性 QA

Phase 1 质量不只是“能编译”。生成的 `.xlsx` 应检查：

- ZIP package 结构。
- `[Content_Types].xml`。
- relationships。
- `docProps/core.xml` 和 `docProps/app.xml`。
- configurable document properties 场景还要检查 creator、lastModifiedBy、title、
  subject、description、keywords、category、Application、AppVersion，以及是否没有
  意外的 `docProps/custom.xml`。
- workbook part。
- worksheet part。
- 基础 `sheetData`。
- 本机有 Excel 时，必须用 Excel 打开关键样例做可视化验证。
- 当前 Phase 3 metadata 推荐 preset 样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-phase3-metadata.xlsx`；
  本机 Excel COM 已验证 `Metadata` sheet、`B2` / `C2` 公式、row 2 高度、A/C
  列宽、`B2:D4` auto filter、`A3:B3` / `C4:D4` merge areas，以及
  `SplitRow=2` / `SplitColumn=3` frozen panes；本机 `openpyxl` 3.1.2 已读取
  `calcId=124519` 和 `fullCalcOnLoad=True`，并确认无 `xl/calcChain.xml`。不要据此宣称
  公式计算、cached values、calcChain、styles 或完整 Phase 3。
  固定本地 QA 入口是 `tools/verify_phase3_metadata.py` 和
  `tools/verify_phase3_metadata_excel.ps1`；它们分别做拆包 XML / `openpyxl`
  检查和 Excel COM 只读可视化检查，不接入默认 CTest/CI。
- 当前 P9 number-format styles 推荐 preset 样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-number-formats.xlsx`，
  sharedStrings + styles 样例为
  `build/windows-nmake-release/tests/fastxlsx-streaming-styles-shared-strings.xlsx`。
  结构测试必须覆盖 `xl/styles.xml`、styles content type override、workbook styles
  relationship、custom `numFmts` / `cellXfs`、worksheet `s="N"`、默认 `s="0"` 省略、
  sharedStrings + styles 共存，以及 foreign `StyleId` 数值碰撞也在 row state 变更前
  被拒绝。固定本地 QA 入口是 `tools/verify_styles_number_formats.py` 和
  `tools/verify_styles_excel.ps1`；它们分别做拆包 XML / `openpyxl` / 可选
  `XlsxWriter` 检查和 Excel COM 只读 NumberFormat 检查，不接入默认 CTest/CI，也不是
  运行时依赖。
- 在可用时验证 Excel / WPS / LibreOffice 能打开。
- 当前 Phase 1 smoke 样例通常位于测试工作目录；推荐 preset 路径是
  `build/windows-nmake-release/tests/fastxlsx-phase1-minimal.xlsx`。旧
  `build-nmake/tests/*.xlsx` 可能是过期 artifact，人工验证前必须确认重新生成。

编辑已有文件时，还要验证未修改 part 被保留，尤其是图表、图片、宏和未知扩展。

## Excel 可视化和 XML 对比

遇到 `.xlsx` 打不开、Excel 修复弹窗、单元格显示不对或结构测试失败时，
按 `docs/TESTING_WORKFLOW.md` 的流程排障：

1. 用本机 Excel 创建语义等价的参考 `.xlsx`，或用 `openpyxl` / `XlsxWriter`
   创建参考文件。
2. 将 FastXLSX 输出和参考文件都复制为 `.zip` 并解压。
3. 对比 `[Content_Types].xml`、`_rels/.rels`、`docProps/core.xml`、
   `docProps/app.xml`、`xl/workbook.xml`、`xl/_rels/workbook.xml.rels`、
   `xl/worksheets/sheet*.xml`。
4. 如果涉及 shared strings 或 styles，再对比 `xl/sharedStrings.xml` 和 `xl/styles.xml`。
5. 如果涉及 data validations，重点对比 worksheet XML 中的 `<dataValidations>`、
   `count`、`sqref`、`type`、`operator`、`allowBlank`、`formula1`、`formula2`，
   以及 `showInputMessage`、`showErrorMessage`、`errorStyle`、`promptTitle`、
   `prompt`、`errorTitle`、`error` 等 prompt/error attributes；涉及隐藏下拉箭头时
   还要检查 `showDropDown="1"`，并记住该 OpenXML attribute 表示隐藏而不是显示。
   multi-area `sqref`
   还要对比空格分隔 token、`count` 是否仍按 dataValidation 元素计算，以及是否没有
   未承诺的排序、合并、去重或重叠检查行为。
6. 如果涉及 external hyperlinks，重点对比 worksheet `<hyperlinks>`、
   `xl/worksheets/_rels/sheet*.xml.rels`、relationship `Type`、`Target`、
   `TargetMode="External"`、worksheet-local `rId`、workbook relationships 是否未污染，
   以及 `[Content_Types].xml` 是否只依赖 `.rels` default。
7. 如果涉及 internal hyperlinks，重点对比 worksheet `<hyperlinks>` 中的 `location`
   attribute、是否没有 `r:id`、internal-only sheet 是否没有
   `xl/worksheets/_rels/sheet*.xml.rels` 和 `xmlns:r`，mixed external/internal 场景下
   internal 是否不消耗 worksheet-local `rId`，以及 workbook relationships /
   `[Content_Types].xml` 是否未污染。
8. 如果涉及 hyperlink display/tooltip，重点对比 worksheet `<hyperlink>` 的
   `display` / `tooltip` attributes、XML attribute escape、display-only、tooltip-only、
   显式空 options 是否省略、`.rels` 是否只保留 target relationship、cell text 是否未被替换，
   以及是否没有 `styles.xml` / content type side effects。
9. 如果涉及 tables，重点对比 `xl/tables/table*.xml`、worksheet `<tableParts>`、
   `xl/worksheets/_rels/sheet*.xml.rels`、relationship `Type` / `Target`、
   table content type override、table `id` / `name` / `displayName` / `ref`、
   `autoFilter`、`tableColumns`、`tableStyleInfo`、`totalsRowShown` /
   `totalsRowCount`、caller-supplied `totalsRowFunction` / `totalsRowLabel`，并确认
   不会意外生成公式文本、空 label attribute 或 `xl/styles.xml`；table overlap 只检查
   同一 worksheet 内 table-vs-table range overlap，不扩展到与 validations、images、
   merged ranges 或 autoFilter 的通用冲突检查。
10. 不要求 byte-level 完全一致，重点比较 OpenXML 语义、关系、content type、
   cell reference、cell type 和 value。

Python XLSX 库只作为测试/排障参考，不是 FastXLSX 运行时依赖。
当前本地 table overlap QA helper 是：
```powershell
py tools\verify_table_overlap_metadata.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-table-range-overlap.xlsx
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_table_overlap_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-table-range-overlap.xlsx
```
第一个脚本检查 package XML、`openpyxl` table ranges、无 `table5.xml` 和无
`xl/styles.xml`；第二个脚本用本机 Excel COM 只读打开 workbook，确认相邻 tables 和
跨 worksheet 相同 range table 可见。它们是本地 QA/排障工具，不接入默认 CTest。
当前已有本地 sharedStrings QA helper：

```powershell
py tools\verify_shared_strings_reference.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings.xlsx
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_shared_strings_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings.xlsx
```

第一个脚本检查 FastXLSX sharedStrings package 关键 entry、content type、relationship、
worksheet `t="s"` index、`xl/sharedStrings.xml` count/uniqueCount 和当前 smoke 值，
并创建 `openpyxl` 参考 workbook；本机 `py` 当前还能创建 `XlsxWriter` 参考 workbook，
但 bundled Python 缺少 `xlsxwriter` 时会明确跳过该分支。第二个脚本用本机 Excel COM
只读打开样例并核对 `Shared!A1:D3` 可见值。它们是本地 QA/排障工具，不接入默认 CTest，
也不是运行时依赖。

如果只改 sharedStrings 空表边界，默认 CTest 的结构断言是主验证：确认 no-string-cell
样例没有 `xl/sharedStrings.xml`、sharedStrings content type、workbook relationship、
worksheet `t="s"` 或 `inlineStr`。已有 sharedStrings smoke 仍应跑上述 Python/Excel
本地 QA，防止正常有字符串路径回退。
当前空表样例也有固定本地 QA helper：
```powershell
py tools\verify_shared_strings_absence.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings-empty-table.xlsx `
  --work-dir build\qa\shared-strings-absence
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_shared_strings_absence_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings-empty-table.xlsx
```

第一个脚本检查 ZIP absence 语义、`openpyxl` 可读值/公式，并在可用时创建
`XlsxWriter` 参考 workbook；第二个脚本用本机 Excel COM 只读打开并核对
`NoStrings!A1:C1`。它们是本地 QA/排障工具，不接入默认 CTest，也不是运行时依赖。

当前已有本地 data validation prompt/error QA helper：
```powershell
py tools\verify_data_validation_prompts.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-prompts.xlsx `
  --multi-range-input build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-multi-range.xlsx
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_data_validation_prompts_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-prompts.xlsx `
  -MultiRangePath build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-multi-range.xlsx
```

Python helper 检查 package XML、prompt/error attributes、`showDropDown`、无 `.rels`
/ `styles.xml` / content type 副作用，并创建 `openpyxl` / `XlsxWriter` 参考 workbook；
它还检查 multi-range 样例的 package XML、`sqref` token、`count` 语义和 `openpyxl`
多区域读取，并创建 `openpyxl` multi-range 参考 workbook。Excel helper 只读打开
workbook 并核对 `ValidationPrompt!A2:D2` 的 validation 属性；`showDropDown="1"`
对应 Excel COM `Validation.InCellDropdown = False`。它还确认 multi-range 样例中
A/C/E 三段 validation areas。Excel COM 会把 custom validation 公式返回为带 `=` 的形式，
结构语义仍以拆包后的 worksheet XML 为准。

当前已有本地 conditional color scale QA helper：
```powershell
py tools\verify_conditional_formatting_color_scales.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-two-color-scale.xlsx `
  --metadata-order-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-metadata-order.xlsx `
  --three-color-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-three-color-scale.xlsx `
  --multi-range-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-multi-range.xlsx `
  --priorities-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-priorities.xlsx `
  --work-dir build\qa\conditional-formatting-color-scales
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_conditional_formatting_color_scales_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-two-color-scale.xlsx `
  -ThreeColorPath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-three-color-scale.xlsx `
  -MultiRangePath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-multi-range.xlsx
```

Python helper 检查 package XML、suffix 顺序、relationship id 不偏移、multi-range `sqref`、
priority、无 styles/dxfs/rels/content type/calcPr 副作用，并用 `openpyxl` 读取 basic
、three-color 和 multi-range 条件格式；可用时会用 `XlsxWriter` 创建参考 workbook。
Excel helper 只读打开 two-color、three-color 和 multi-range workbook，核对 color scale
规则和多区域 AppliesTo。它们是本地 QA/排障工具，不接入默认 CTest/CI，也不是运行时依赖。

当前已有本地 conditional data bar QA helper：
```powershell
py tools\verify_conditional_formatting_data_bars.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar.xlsx `
  --metadata-order-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar-metadata-order.xlsx `
  --multi-range-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar-multi-range.xlsx `
  --priorities-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar-priorities.xlsx `
  --work-dir build\qa\conditional-formatting-data-bars
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_conditional_formatting_data_bars_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar.xlsx `
  -MetadataOrderPath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar-metadata-order.xlsx `
  -MultiRangePath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar-multi-range.xlsx
```
Python helper 检查 package XML、`openpyxl` 读取和可选 `XlsxWriter` reference；
Excel helper 只读打开 basic 和 multi-range workbook，核对 data bar、bar color、
ShowValue 和多区域 AppliesTo；metadata-order workbook 应核对 `ShowValue=False`。
它们是本地 QA/排障工具，不接入默认 CTest/CI，也不是运行时依赖。

当前已有本地 conditional icon set QA helper：
```powershell
py tools\verify_conditional_formatting_icon_sets.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set.xlsx `
  --metadata-order-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-metadata-order.xlsx `
  --percentile-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-percentile.xlsx `
  --multi-range-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-multi-range.xlsx `
  --priorities-input build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-priorities.xlsx `
  --work-dir build\qa\conditional-formatting-icon-sets
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_conditional_formatting_icon_sets_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set.xlsx `
  -MetadataOrderPath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-metadata-order.xlsx `
  -PercentilePath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-percentile.xlsx `
  -MultiRangePath build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-multi-range.xlsx
```
Python helper 检查 package XML、`openpyxl` 读取和可选 `XlsxWriter` reference；
Excel helper 只读打开 basic、metadata-order、percentile 和 multi-range workbook，
核对 icon set、IconSet ID、Percent / Percentile criteria、ShowIconOnly、
ReverseOrder 和多区域 AppliesTo。它们是本地 QA/排障工具，不接入默认 CTest/CI，
也不是运行时依赖。

## ZIP backend 验证

OpenXML 结构测试应比较解压后的 package entries 和 XML 语义，不要断言 ZIP
central directory method 必须是 `0`。默认构建的 stored bootstrap 会写 method 0；
`FASTXLSX_ENABLE_MINIZIP_NG=ON` 构建会写 method 8（DEFLATE）。二者 entry 顺序、
压缩大小和 archive size 可以不同。

验证 opt-in minizip backend 时运行：

```powershell
cmake --preset windows-nmake-release-minizip
cmake --build --preset windows-nmake-release-minizip
ctest --preset windows-nmake-release-minizip
```

验证默认 stb image 路径时运行：

```powershell
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

file-backed/chunked entry 测试也必须走 decompressed package semantics，不要断言
entry source、entry order、compressed size、archive size 或 chunk 边界。测试可以断言
duplicate entry 被拒绝，以及 worksheet XML 内容没有因 chunk 边界被截断或重排。

## 图片和复杂对象验收

当前完整图片 OpenXML 读取/插入仍是 Phase 5；但
`WorksheetWriter::add_image()` 已有 streaming-only new-workbook PNG/JPEG 基础插入
切片。`read_image_info()` 只表示 PNG/JPEG 图片元数据 helper 已有默认 `stb`
路径；不要把它写成 existing-workbook 图片保真、drawing 编辑或完整图片支持已完成。
图片结构测试至少检查：

- `xl/media/image*.png|jpg|jpeg` 存在。
- `xl/drawings/drawing*.xml` 存在。
- `xl/drawings/_rels/drawing*.xml.rels` 指向 media part。
- `xl/worksheets/_rels/sheet*.xml.rels` 指向 drawing part。
- worksheet XML 中 drawing `r:id` 与 worksheet rels 一致。
- `[Content_Types].xml` 有图片格式 default 或 override。
- 如果涉及 image metadata，还要检查 drawing XML 中 `xdr:twoCellAnchor editAs`、
  `xdr:cNvPr name` / `descr`、XML attribute escape、空值省略和默认名称规则。
当前 JPEG 结构测试还应覆盖 `.jpg` media entry、`image/jpeg` content type default、
drawing intrinsic EMU 尺寸和 drawing `.rels` target。
混合 PNG/JPEG 测试还应覆盖同一个 worksheet 内多图片共享一个 drawing part、
多个 `<xdr:twoCellAnchor>`、全局 media 编号和 drawing-owner-local relationship id。
多对象 relationship id 回归还应覆盖同一 worksheet 内多个 external hyperlink、
一个 drawing 和多个 table 的 worksheet-owner-local `rId` 顺序，以及跨 worksheet
owner 和 drawing owner 的 relationship id 重置。
最大合法 anchor marker 测试应验证 Excel 最大合法行列边界序列化为 drawing XML 的
0-based marker，例如 `<xdr:col>16383</xdr:col>` 和
`<xdr:row>1048575</xdr:row>`。
当前推荐样例是
`build/windows-nmake-release/tests/fastxlsx-streaming-images.xlsx`。
当前 image metadata 推荐样例是
`build/windows-nmake-release/tests/fastxlsx-streaming-image-metadata.xlsx`；
本地 QA 可运行 `tools/verify_image_metadata.py` 做 package XML / openpyxl /
XlsxWriter 检查，并运行 `tools/verify_image_metadata_excel.ps1` 做 Excel COM
shape name / AlternativeText / Placement / marker-offset geometry 检查。当前这两个
helper 还支持 basic image 和 mixed-object relationship 样例参数：
`--basic-input` / `-BasicPath` 与 `--mixed-object-input` / `-MixedObjectPath`；`openpyxl`
可能跳过 JPEG 图片读取，JPEG media/drawing 关系以拆包 XML 和 Excel COM 为准。

Anchor 测试要覆盖起始/结束单元格、two-cell marker EMU offset、零尺寸、负尺寸、
越界 anchor、负 offset 和超出 OpenXML coordinate 上界的 offset；不要为了 anchor
计算引入完整 worksheet DOM。图片读取应使用 `stb` 处理解码、尺寸、通道和像素，
但结构测试必须验证 FastXLSX 自己生成的 OpenXML media/drawing package 语义。
这些 anchor 边界测试是结构测试，不是大 sheet 导出性能证明，也不要求真实写满
worksheet 数据。
兼容性测试要用本机 Excel 打开 `.xlsx` 样例，确认无修复弹窗并检查图片显示位置/尺寸；
当前本机 Excel COM 验证结果应记录为 `Images` / `SecondImage` 各 1 个 shape、
`Plain` 为 0 个 shape，锚点 `C1:F5` 和 `A1:B2`。
image metadata 验证结果还应记录 shape count、custom/default shape names、
`AlternativeText`、`Placement` 和首图 marker offset 对 `Shape.Left` / `Top` /
`Width` / `Height` 的影响；但 marker EMU offset 语义仍以拆包后的 drawing XML 为准，
不要宣称完整 UI parity、row/column resize 几何计算或 `oneCellAnchor` /
`absoluteAnchor` 元素支持。
结构异常时用 Excel、`openpyxl` 或 `XlsxWriter` 参考文件拆包对比 XML。已有文件编辑
场景还要证明未修改 drawings、media、charts、macros 和 unknown parts 没有丢失，
relationships 仍指向有效 target。

涉及 public 图片 API 时，测试计划还要检查文档注释是否写清 Streaming/Patch/In-memory
模式、原始图片字节和 decoded pixel buffer 的内存成本、OpenXML part 副作用、是否触发
DOM，以及为什么不会破坏 worksheet streaming 热路径。

## Benchmark 优先级

Benchmark 应记录：

- 构建类型。
- 数据规模。
- 压缩等级或 ZIP backend。
- 字符串策略。
- 总耗时。
- 峰值内存。
- 输出文件大小。
- 办公软件打开兼容性。

文档中的对比对象是 `OpenXLSX`、`xlnt` streaming writer 和旧 `FastExcel`。
不要把这些 benchmark 对象变成 FastXLSX 运行时依赖。

当前最小 P6 benchmark 入口是 `FASTXLSX_BUILD_BENCHMARKS=ON` 下的手工工具
`fastxlsx_bench_streaming_writer`。它不使用 Google Benchmark，不注册 CTest，
不进入默认 CI；`planned-dev` 中的 `benchmark` 仍不是当前 CMake 事实。
不传 `--output` / `--result` 时，该工具默认写到 benchmark target 的 binary dir；
`--sheets` 超过 1024 会被拒绝，这是 benchmark 工具护栏，不是 public API 限制。
当前本地矩阵 helper 是 `tools/run_benchmark_matrix.py`，它只包装一个已构建的
benchmark exe，保留每个 case 的 `.xlsx` 和 schema-v3 `.json`，并写聚合
`benchmark-matrix-report.json`。不同 backend 要分别传入 stored/minizip preset 的
benchmark exe；不要让 runner 改写 `office_open`。
当前 benchmark JSON schema version 为 `3`，会写 `string_pattern`、
`package_entry_source_mode="worksheet-file-backed-chunked"`、
`temporary_worksheet_part_footprint="worksheet-body-file-bytes"` 和数值型
`temporary_worksheet_part_footprint_bytes`。该字段来自 benchmark-only instrumentation，
只累计 worksheet body row XML 写入字节数，不包含 worksheet header/footer、
sharedStrings 临时文件、小型 XML parts、media 文件、ZIP/backend 缓冲、package
assembly 峰值内存或 OS 文件系统开销；不要把它写成完整低内存证据。

当前 `docs/PERFORMANCE_TARGETS.md` 记录了 2026-06-07 本机小规模 sharedStrings
benchmark 快照：`strings` 场景、`50000 x 10 x 1 = 500000` cells、
repeated/unique string pattern、inline/shared strategy、stored-bootstrap ZIP。
四个输出已用本机 Excel COM 只读打开并核对 `Sheet1` 使用范围和首尾值。继续把它
当作 opt-in 手工结果；不要把 `office_open=not_run` 的 JSON 字段写成工具自动完成
Office 验证，也不要把 worksheet-body-only footprint 写成完整 package、完整临时文件
或进程峰值内存 footprint。

重点覆盖：

- 百万行级导出。
- 多 sheet 批量导出。
- 数字/日期/布尔密集写入。
- 字符串密集下的 `inlineStr` 与 `sharedStrings`。
- repeated / unique string pattern 下的 sharedStrings 体积、耗时和峰值内存对比。
- 模板 sheet 替换。
- ZIP 压缩等级。
- package entry source mode：in-memory / file-backed / chunked。
- close-time package assembly peak memory。
- temporary worksheet part footprint。
- 矩阵字段至少记录 preset/backend、scenario、rows、cols、sheets、cells、
  string_ratio、string_pattern、string_strategy、zip_backend、compression、
  package_entry_source_mode、elapsed_ms、peak_memory_mb、output_bytes、
  `temporary_worksheet_part_footprint_bytes` 和 Office/openpyxl 验证状态。

## 排障路径

- `ctest` 跑出 0 个测试：先查 `tests/CMakeLists.txt` 是否仍注册
  `fastxlsx.unit`、`fastxlsx.streaming` 和 `fastxlsx.opc`。
- 生成 XLSX 打不开：先查 package entries、content types、relationships、
  workbook、worksheet、`sheetData`。
- Excel 提示修复：保存修复后的文件，与原输出和参考文件拆包对比 XML。
- 内存异常：优先查 DOM、完整 worksheet matrix、cell map、跨行缓存。
- 内存异常还要查 close-time 是否重新物化完整 worksheet XML、file-backed entry 是否退化为
  in-memory entry、chunk buffer 是否无界增长、临时文件生命周期是否异常。
- 性能回退：优先查 XML 编码、escape、数字转换、cell reference、
  压缩等级、sharedStrings、row buffer。
- XML 结构异常或 Excel 修复时，也要检查是否错误写出了 `nan`、`inf` 或 `-inf`
  这类非法 OpenXML 数字文本。

## 验证命令

按 `docs/DEVELOPMENT_ENVIRONMENT.md` 的生成器建议：

```powershell
cmake --preset windows-nmake-release
cmake --build --preset windows-nmake-release
ctest --preset windows-nmake-release
```

当前本机可用路径是 VS2026 Developer Command Prompt + NMake；其他机器可用
`cmake --help` 检查是否有更合适的 Visual Studio 2026 生成器。

后台跑普通单元测试时使用 60s 超时；preset 和 `tests/CMakeLists.txt` 已承载该
边界。手写 build dir 时显式加 `ctest --test-dir ... --timeout 60`。大型
benchmark 必须显式 opt-in。
