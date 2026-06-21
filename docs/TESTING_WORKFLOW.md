# 测试流程

## 目标

FastXLSX 的测试不能只证明代码能编译，也不能只证明 XML 字符串看起来正确。
生成或修改 `.xlsx` 后，必须同时关注：

- C++ 单元测试是否覆盖核心编码逻辑。
- OpenXML package 结构是否正确。
- 关键 `.xlsx` 样例是否按层验证：CTest 结构测试、Python `openpyxl` /
  可选 `XlsxWriter` 参考、必要时本机 Excel COM 只读或可视化验证。
- 本机 Excel 可视化打开是否正常。
- 生成结构异常时，能否用参考 `.xlsx` 拆包对比 XML 定位差异。
- 性能路径是否仍然符合流式优先和低内存目标。

## 测试分层

### 1. 单元测试

当前普通单元测试由轻量测试可执行文件通过 `CTest` 注册。`Catch2` 是
`vcpkg.json` planned-dev 依赖，尚未接入 CMake；后续接入 Catch2 后，再将
普通单元测试迁移到 Catch2。核心测试集必须能在 60s 内完成，不能把大型
benchmark 混进默认单元测试。

优先覆盖：

- XML escape。
- 单元格引用生成。
- 数字、日期、布尔、字符串编码，以及 `NaN`、`+Inf`、`-Inf` 非有限数值拒绝。
- `inlineStr` 和 `sharedStrings` 策略。
- dimension tracking。
- row buffer 复用。
- content types 和 relationships。
- part index 与未修改 part 保留。

测试文件也应遵守职责边界。`tests/test_streaming_writer.cpp` 适合保留 streaming writer
主流程、核心边界和跨功能集成测试；当某个 feature 已经有大量结构断言、负例、参考
样例或独立 QA helper 时，应优先拆成 feature-specific 测试文件，例如 conditional
formatting、data validations、hyperlinks、tables、images、styles 或 sharedStrings。
集成测试只保留 suffix 顺序、relationship id、content type side effects、package
side effects 和多对象共存等真正跨模块行为。不要为了少量小测试强行拆分；新增测试
target 或源文件时必须同步更新 `tests/CMakeLists.txt`。

### 2. OpenXML 结构测试

生成最小 `.xlsx` 后，至少检查这些 ZIP entry 和 XML part：

```text
[Content_Types].xml
_rels/.rels
xl/workbook.xml
xl/_rels/workbook.xml.rels
xl/worksheets/sheet1.xml
```

如果功能使用了 shared strings、styles 或 doc props，还要检查：

```text
xl/sharedStrings.xml
xl/styles.xml
docProps/core.xml
docProps/app.xml
```

sharedStrings 检查要区分 presence 和 absence：实际写入字符串 cell 并产生 shared
string entry 时，检查 `xl/sharedStrings.xml`、content type override、workbook
relationship 和 worksheet `t="s"` 引用；如果只是启用了 `StringStrategy::SharedString`
但没有字符串 cell，则应反向确认这些 sharedStrings package artifacts 都不存在。

styles 检查要区分当前 P9 number-format / wrap-text + limited horizontal/vertical alignment /
bold/italic/direct ARGB font color / solid fill 切片和完整样式系统。使用
`WorkbookWriter::add_style()` / `CellView::with_style()` 后，应检查 `xl/styles.xml`、
styles content type override、workbook styles relationship、custom `numFmts`、
`cellXfs` 默认/自定义 style 记录、alignment 的 `applyAlignment="1"` /
`<alignment wrapText="1"/>`、`horizontal="left|center|right"`、
`vertical="top|center|bottom"` 和 combined alignment attributes、bold/italic/direct ARGB font color 的
`<fonts count>`、`<b/>`、`<i/>`、可选 `<color rgb="AARRGGBB"/>`、
`fontId` 和 `applyFont="1"`、solid fill 的 `<fills count>`、solid `<patternFill>`、
`fgColor rgb`、`fillId` 和 `applyFill="1"`、worksheet cell `s="N"` 引用、默认 style 不写
`s="0"`，以及 styles 不创建 worksheet `.rels`。alignment-only style 不应创建
custom `numFmt`；number format + alignment 组合应复用相同 number format 的 custom
`numFmtId`；font-only style 不应创建 custom `numFmt`；number format + font 组合应复用
相同 bold/italic/direct-color font 组合的 `fontId`；fill-only style 不应创建 custom `numFmt`；number format + fill
和 font + fill 组合应复用相同 foreground ARGB 的 `fillId`。sharedStrings + styles 共存时，
还要检查 workbook `.rels` 中 sharedStrings relationship 在 styles relationship 之前，
并检查 styled shared string cell 同时写出 `s="N"` 和 `t="s"`。foreign `StyleId`
失败路径必须确认在推进 row number、dimension、sharedStrings 或公式重算 metadata 前
被拒绝。

如果功能使用了 configurable document properties，还要检查 `docProps/core.xml`
中的 creator、lastModifiedBy、title、subject、description、keywords、category，
以及 `docProps/app.xml` 中的 Application 和 AppVersion。结构测试必须覆盖 XML
escape、package relationships、content type overrides，并确认不会意外生成
`docProps/custom.xml`。

如果功能涉及图片或 drawing，还要检查：

```text
xl/media/image*.png|jpg|jpeg
xl/drawings/drawing*.xml
xl/drawings/_rels/drawing*.xml.rels
xl/worksheets/_rels/sheet*.xml.rels
worksheet XML 中的 <drawing r:id="...">
[Content_Types].xml 中的图片格式 default 或 override
```

图片读取 helper 的结构/单元测试还应覆盖 `read_image_info()` 和
`read_image_pixels()`。`read_image_pixels()` 返回 `ImagePixels`，会为完整 decoded
pixel buffer 分配 caller-owned `pixels` 存储，内存成本随 `width * height * channels`
增长；这属于图片解码 helper 验证，不应混入 streaming worksheet row/cell 热路径，也不
代表图片插入 API 会保留 decoded pixel buffer。

结构测试应检查 relationships、content type override/default、sheet id、
relationship target、worksheet `sheetData`、cell reference 和 value type。
data validations 结构测试还应检查 worksheet `<dataValidations>` 的 `count`、
`sqref`、`type`、`operator`、`allowBlank`、`formula1`、`formula2`，以及 prompt/error
metadata attributes：`showInputMessage`、`showErrorMessage`、`errorStyle`、
`promptTitle`、`prompt`、`errorTitle` 和 `error`。如果涉及隐藏 list dropdown arrow，
还要检查 `showDropDown="1"`；OpenXML 这个属性名是反向语义，表示隐藏 in-cell
dropdown arrow，省略/false 表示保留默认可见箭头。这些 prompt/error 字段应做 XML
attribute escape，空字符串和 false flags 应省略，并确认不会新增 worksheet `.rels`、
`xl/metadata.xml`、`xl/styles.xml`、workbook relationships、content type side effects
或 `<calcPr>`。multi-area `sqref` 测试还应检查空格分隔的区域 token、`count` 仍按
`<dataValidation>` 元素数量计算、空 range list 拒绝路径、multi-range 内非法 range
拒绝路径，以及不做未承诺的排序、合并、去重或重叠检测。
Phase 3 worksheet metadata 结构测试还应检查公式 XML escape、row height、
column width records、last-call-wins frozen panes、last-call-wins auto filters、
merged ranges、suffix ordering，以及不会为纯 worksheet metadata 误加
relationships 或 content type entries。
图片结构测试还应检查 media part target、drawing relationship target、worksheet-local
`rId` 一致性，以及 anchor 的起始/结束单元格和 two-cell marker EMU offset
(`xdr:colOff` / `xdr:rowOff`) 语义。
图片对象 hyperlink 还应检查 drawing XML 的 `xdr:cNvPr/a:hlinkClick`、
drawing `.rels` 中 `Type=.../hyperlink` 且 `TargetMode="External"` 的关系、
URL / tooltip XML attribute escape、`a:blip r:embed` 仍指向独立 image relationship、
worksheet XML 中没有 `<hyperlinks>`，并确认 worksheet `.rels` 没有为图片对象
hyperlink 新增 worksheet-owned hyperlink relationship。
table 结构测试还应检查 `xl/tables/table*.xml`、worksheet `<tableParts>`、
worksheet `.rels`、content type override、owner-local `rId`、`tableColumns`、
`tableStyleInfo` 和 totals-row visibility metadata。当前
`TableOptions::show_totals_row=false` / 默认路径显式写 `totalsRowShown="0"`；
true 路径写 `totalsRowCount="1"`，且 `<autoFilter>` 范围只覆盖 header/data rows。
如果使用 `column_totals_functions` / `column_totals_labels`，测试必须确认只写调用方
声明的 `totalsRowFunction` / `totalsRowLabel` attributes，不计算 totals、不生成公式文本、
totals row 单元格文本、空 label attribute 或 `xl/styles.xml`。
table range overlap 测试只覆盖同一 worksheet 内 table-vs-table 矩形相交拒绝、相邻
table 允许、不同 worksheet 上相同 range 允许；不要把这扩展成与 data validations、
images、merged ranges 或 autoFilter 的通用冲突检查。

conditional formatting 结构测试要区分当前 two-/three-color color scale / basic data bar /
basic 3Arrows icon set
worksheet metadata 切片和完整 conditional formatting。当前
`WorksheetWriter::add_conditional_color_scale()` 应检查
worksheet `<conditionalFormatting sqref>`、`<cfRule type="colorScale" priority>`、
two-color 的两个 `<cfvo>` / 两个 `<color rgb="AARRGGBB">`、three-color 的三个
`<cfvo>` / 三个 `<color rgb="AARRGGBB">`、multi-range 空格分隔 `sqref`、同一 worksheet
内 priority 递增且跨 worksheet 重置，以及 suffix 顺序
`mergeCells -> conditionalFormatting -> dataValidations`。当前
`WorksheetWriter::add_conditional_data_bar()` 应检查
`<cfRule type="dataBar" priority>`、两个 `<cfvo>`、一个 `<color rgb="AARRGGBB">`、
可选 `showValue="0"`、
multi-range 空格分隔 `sqref`、与 color scale 共享同一 worksheet-local priority 序列、
同一 worksheet 内 priority 递增且跨 worksheet 重置，以及相同 suffix 顺序。当前
`WorksheetWriter::add_conditional_icon_set()` 应检查
`<cfRule type="iconSet" priority>`、`<iconSet iconSet="3Arrows">`、三枚 `<cfvo>`、
finite 且严格递增阈值、可选 `showValue="0"` / `reverse="1"`、multi-range 空格分隔
`sqref`、与 color scale / data bar 共享同一 worksheet-local priority 序列、同一 worksheet
内 priority 递增且跨 worksheet 重置，以及相同 suffix 顺序。还要确认它们不生成
`xl/styles.xml`、`dxfs`、`xl/metadata.xml`、worksheet `.rels`、workbook relationships、
content type entries、cell text 或 `<calcPr>`。非法 range、越界行列、空 multi-range、
非有限 numeric endpoint / threshold、lower `Maximum`、upper `Minimum`、icon set
descending/duplicate thresholds 和 close 后 mutation 都应
被拒绝，并且失败调用不能污染 worksheet metadata 或 dimension。不要把该切片扩展成
formula/cellIs、advanced/custom icon sets、advanced data bar、dxf-backed styles、重叠检测、existing-file editing 或
完整 Excel UI。

数值编码负例不需要 Excel 可视化验证，因为期望结果是不生成有效 `.xlsx`。测试应覆盖
`NaN`、`+Inf` 和 `-Inf`，并确认 in-memory 路径在 `Workbook::save()` 抛
`FastXlsxError`，streaming 路径在 `WorksheetWriter::append_row()` 拒绝非有限
number / row height，`WorksheetWriter::set_column_width()` 拒绝非有限 width。
结构测试或排障时还要确认 worksheet XML 中没有写出 `nan`、`inf` 或 `-inf` 这类
非法数字文本。

Streaming writer hot-path 边界样例应优先做拆包 XML 结构检查：

- `fastxlsx-streaming-empty-row-dimensions.xlsx`：确认无行和只含空行 worksheet 的
  `<dimension ref="A1"/>`，确认空行写出 `<row r="N"></row>`，确认前导空行 +
  数据行 + 尾部空行样例的 generated dimension 是 `A1:C3`。Excel `UsedRange`
  可能更窄，不能用它反推生成 XML 语义。
- `fastxlsx-streaming-max-column-boundary.xlsx`：确认单行 16,384 个 cells 写到
  `XFD1`，dimension 是 `A1:XFD1`，且没有 `XFE1`。这是结构边界，不是宽表
  benchmark。本机 `openpyxl` 和 Excel COM 已只读打开验证 `MaxColumn!A1=1` 和
  `MaxColumn!XFD1=1`。
- `fastxlsx-streaming-max-row-boundary.xlsx`：用 test-only hook 低成本覆盖合法
  `1048576` 行写出，确认 `A1048576` / `B1048576` / `C1048576`、dimension
  `A1:C1048576` 和公式重算 metadata，且没有 `1048577`。不要把
  `FASTXLSX_ENABLE_TEST_HOOKS` 或 `testing_set_worksheet_row_count()` 写成
  public API、百万行导出证明或性能 benchmark。本机 `openpyxl` 和 Excel COM 已只读
  打开验证 `MaxRow!A1048576=45500`、`B1048576=TRUE` 和 `C1048576` 公式；Excel
  `UsedRange` 会定位到实际非空的 `A1048576:C1048576`，不能用
  `UsedRange.Rows.Count` 反推 generated dimension。
- `fastxlsx-streaming-failed-append-state.xlsx`：确认非法 `append_row()` 在抛错前
  不推进 row number / dimension，不写被拒绝 text / formula，不创建空
  `xl/sharedStrings.xml`，也不污染 workbook `<calcPr>`。本机 `openpyxl` 和 Excel COM
  已只读打开验证 `FailedAppend!A1=7` 和 `FailedAppend!A2=TRUE`。

### 3. 本机 Excel 可视化验证

本机有 Excel 时，生成的关键样例必须用 Excel 打开做可视化验证。

验证点：

- Excel 打开时没有修复弹窗。
- sheet 名、行列位置、单元格值与预期一致。
- 数字、布尔、字符串、日期、公式写入可见结果符合预期。
- 样式、列宽、行高、合并单元格、冻结窗格等高级功能在后续实现时可见。
- 图片功能必须确认图片显示、位置和尺寸符合预期；当前
  `WorksheetWriter::add_image()` 基础切片的推荐样例是
  `build/windows-nmake-release/tests/fastxlsx-streaming-images.xlsx`。
- memory-source 图片样例还必须确认 caller buffer 后续修改不会改变 package 内
  media bytes；当前推荐样例是
  `build/windows-nmake-release/tests/fastxlsx-streaming-memory-images.xlsx`。
- 图片 metadata 功能还必须确认 drawing XML 的 `xdr:cNvPr name` / `descr` 与
  Excel 可见 shape metadata 对应；当前推荐样例是
  `build/windows-nmake-release/tests/fastxlsx-streaming-image-metadata.xlsx`。
- 图片对象 hyperlink 功能还必须确认 drawing XML 的 `a:hlinkClick`、drawing `.rels`
  external hyperlink relationship 和 Excel 可见 shape hyperlink 对应；当前推荐样例是
  `build/windows-nmake-release/tests/fastxlsx-streaming-image-hyperlinks.xlsx`。
- table totals-row visibility metadata 必须用本机 Excel COM 或人工打开确认
  `ListObject.ShowTotals` 和 totals row 范围。当前推荐样例是
  `build/windows-nmake-release/tests/fastxlsx-streaming-tables.xlsx`，其中
  `InventoryTable` 应保持隐藏 totals row，`TotalsTable` 应显示 totals row 且范围为
  `A1:B3` / totals row `A3:B3`。当前首选统一 QA 入口是
  `tools/verify_tables.py` 和 `tools/verify_tables_excel.ps1`，它们同时覆盖
  totals row、table style flags、table column attribute escaping 和 same-worksheet
  range-overlap 样例。
- conditional formatting 基础切片必须用本机 Excel COM 或人工打开确认无修复弹窗，并检查
  two-/three-color color scale、basic data bar 或 basic 3Arrows icon set 规则可见。当前基础 color scale 样例是
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-two-color-scale.xlsx`
  和 `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-three-color-scale.xlsx`，
  multi-range 样例是
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-multi-range.xlsx`。
  当前基础 data bar 样例是
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-data-bar.xlsx`
  和 `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-data-bar-multi-range.xlsx`。
  当前基础 icon set 样例是
  `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-icon-set.xlsx`
  和 `build/windows-nmake-release/tests/fastxlsx-streaming-conditional-formatting-icon-set-multi-range.xlsx`。
- 保存后再打开仍然正常。

Excel 可视化验证是本地验收步骤，不应作为默认 CI 的强依赖。CI 可以做结构检查
和库级兼容测试；需要 Excel 的验证应明确标记为本机人工或本机自动化验证。
当前 GitHub Actions workflow 的默认 `windows-nmake-release` job 已使用 vcpkg
toolchain 并覆盖默认 `stb` 图片路径；独立 opt-in vcpkg matrix job 跑
`windows-nmake-release-minizip`。这些 CI job 仍只执行 configure/build/CTest，
不会运行 Excel COM、openpyxl / XlsxWriter 参考脚本或 benchmark。

建议记录字段：

```text
样例文件：
生成命令或测试名：
构建类型：
Windows / Office 版本：
Excel 打开结果：无修复弹窗 / 有修复弹窗 / 超时 / 未验证
单元格核对：通过 / 失败，差异：
保存后再打开：通过 / 失败 / 未验证
WPS / LibreOffice：通过 / 失败 / 未验证
参考文件来源：Excel / openpyxl / XlsxWriter / 无
XML 对比结论：
验证日期：
```

## Existing-workbook editing test matrix

The current public editing surface is tracked separately in
`docs/EDITING_TEST_MATRIX.md`. Use that matrix when deciding whether a change is
covered by public `WorkbookEditor` facade tests, internal `PackageEditor`
preservation tests, or still needs a new regression. The matrix also records the
explicit non-goals: no broad semantic object editing, no relationship
repair/pruning promise, no transaction/undo/rollback model, and no large-file
low-memory random editing claim from the current public facade.

For a focused local editing gate, run:

```powershell
cmake --build --preset windows-nmake-release --target fastxlsx_workbook_editor_tests
ctest --preset windows-nmake-release -R "fastxlsx\.workbook_editor\.facade" --output-on-failure --timeout 60
```

Shared formula materialization is covered by default CTest through
`fastxlsx.unit` and `fastxlsx.workbook_editor.source-success`. For the local
openpyxl / optional XlsxWriter QA layer, build the opt-in QA tool and run the
focused generated scenario:

```powershell
cmake --preset windows-nmake-release -DFASTXLSX_BUILD_QA_TOOLS=ON
cmake --build --preset windows-nmake-release --target fastxlsx_workbook_editor_qa_tool
py tools\run_workbook_editor_qa.py `
  --scenario generated_shared_formula_materialization `
  --work-dir build\qa\workbook-editor-shared-formula
```

To smoke-test third-party fixture workbooks such as xlnt or OpenXLSX samples,
keep them outside the repository and pass a fixture root explicitly. These
fixtures are local QA inputs only, not FastXLSX runtime dependencies and not
default CI data:

```powershell
py tools\run_workbook_editor_qa.py `
  --fixture-root C:\path\to\xlnt\tests\data `
  --scenario external_fixture_materialized_smoke `
  --fixture-limit 25 `
  --work-dir build\qa\workbook-editor-xlnt-fixtures
```

## Benchmark 本地 QA

Benchmark 必须显式 opt-in，不进入默认 CTest/CI。当前可用本地矩阵 helper：

```powershell
py tools\run_benchmark_matrix.py --self-test
py tools\run_benchmark_matrix.py `
  --bench-exe build\windows-nmake-release-benchmark\benchmarks\fastxlsx_bench_streaming_writer.exe `
  --output-dir build\qa\benchmark-matrix `
  --rows 1000 --cols 10 --sheets 1 `
  --verify-openpyxl
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_benchmark_matrix_excel.ps1 `
  -ReportPath build\qa\benchmark-matrix\benchmark-matrix-report.json `
  -MaxWorkbooks 4
py tools\summarize_benchmark_results.py --self-test
py tools\summarize_benchmark_results.py build\qa\benchmark-matrix `
  --output-markdown build\qa\benchmark-matrix\summary.md `
  --output-json build\qa\benchmark-matrix\summary.json
```

`run_benchmark_matrix.py` 只调用已构建的 `fastxlsx_bench_streaming_writer`，检查
schema-v4 JSON 字段并可选用 `openpyxl` 读取输出 workbook。Excel helper 只读打开
report 中的前几个 workbook，核对 `Sheet1` 的 used range 和首尾值，并写独立
`benchmark-matrix-office-report.json` sidecar；它不回写 benchmark case JSON，也不改写
`benchmark-matrix-report.json`。不同 ZIP backend
要分别构建并传入对应 benchmark exe；不要把 `office_open=not_run` 写成自动 Office
验证，也不要把小矩阵写成大文件性能结论。需要更新 sharedStrings 趋势记录时，再显式用
`--rows 50000 --cols 10 --sheets 1` 运行 strings repeated/unique 的 inline/shared
矩阵，并单独记录 Excel COM / openpyxl 读取结果。
`summarize_benchmark_results.py` 只读取已有 schema-v4 case JSON 或
`benchmark-matrix-report.json`，输出 Markdown / JSON 摘要、吞吐量、输出体积和保守
风险提示；它不重新运行 benchmark、不生成 workbook、不替代 Office/openpyxl 兼容性检查。
summary helper 的 `--self-test` 只验证已有 result / matrix report collection、目录输入
skip summary JSON、warning、Markdown rendering 和 Markdown / JSON output writes，不调用
benchmark exe、不生成 workbook，也不替代实际 benchmark / Office / openpyxl 检查。

当前 Phase 1 smoke 样例由 `fastxlsx.unit` 生成，位于测试工作目录。推荐
preset 路径通常是 `build/windows-nmake-release/tests/fastxlsx-phase1-minimal.xlsx`。
如果使用手写 `-B build-nmake` 命令，也可能生成
`build-nmake/tests/fastxlsx-phase1-minimal.xlsx`；人工检查前必须确认该目录是
当前源码重新构建后的输出。本机验证可以用 Excel COM 只读打开该文件并核对
`Sheet1`、`A1`、`B1`、`C1`、`A2`、`B2`。

当前 streaming 图片样例由 `fastxlsx.streaming` 在
`windows-nmake-release` preset 下生成，推荐路径是
`build/windows-nmake-release/tests/fastxlsx-streaming-images.xlsx`。本机
Excel COM 验证应确认 workbook 可打开、`Images` 和 `SecondImage` sheet 各有一个
shape、`Plain` sheet 没有 shape，并记录 Excel 报告的 `TopLeftCell` /
`BottomRightCell`。当前本机验证结果为 `Images` 上 `C1:F5`，`SecondImage` 上
`A1:B2`。

当前 streaming image metadata 样例由 `fastxlsx.streaming` 在
`windows-nmake-release` preset 下生成，推荐路径是
`build/windows-nmake-release/tests/fastxlsx-streaming-image-metadata.xlsx`。本机
image QA 还会复用基础图片样例
`build/windows-nmake-release/tests/fastxlsx-streaming-images.xlsx` 和混合对象关系样例
`build/windows-nmake-release/tests/fastxlsx-streaming-mixed-object-rels.xlsx`，以及
memory-source 图片样例
`build/windows-nmake-release/tests/fastxlsx-streaming-memory-images.xlsx`，以及图片对象
hyperlink 样例
`build/windows-nmake-release/tests/fastxlsx-streaming-image-hyperlinks.xlsx`。本机
XML / `openpyxl` / Excel COM 验证可以运行：

Current hyperlink smoke samples have a fixed local Python QA helper:

```powershell
py tools\verify_hyperlinks.py `
  --external-input build\windows-nmake-release\tests\fastxlsx-streaming-external-hyperlinks.xlsx `
  --internal-input build\windows-nmake-release\tests\fastxlsx-streaming-internal-hyperlinks.xlsx `
  --display-tooltip-input build\windows-nmake-release\tests\fastxlsx-streaming-hyperlink-display-tooltips.xlsx
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_hyperlinks_excel.ps1 `
  -ExternalPath build\windows-nmake-release\tests\fastxlsx-streaming-external-hyperlinks.xlsx `
  -InternalPath build\windows-nmake-release\tests\fastxlsx-streaming-internal-hyperlinks.xlsx `
  -DisplayTooltipPath build\windows-nmake-release\tests\fastxlsx-streaming-hyperlink-display-tooltips.xlsx
```

The helper checks package XML, worksheet-owned `.rels`, owner-local `rId`
allocation, external `target`, internal `location`, `display` / `tooltip`, no
content type/style/calc side effects, and `openpyxl` hyperlink semantics. It is
local QA only and is not wired into default CTest/CI. The Excel COM helper opens
the same samples read-only and checks hyperlink counts, `Address`, `SubAddress`,
`ScreenTip`, and unchanged cell text / `TextToDisplay`.

```powershell
py tools\verify_image_metadata.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-image-metadata.xlsx `
  --basic-input build\windows-nmake-release\tests\fastxlsx-streaming-images.xlsx `
  --mixed-object-input build\windows-nmake-release\tests\fastxlsx-streaming-mixed-object-rels.xlsx `
  --memory-input build\windows-nmake-release\tests\fastxlsx-streaming-memory-images.xlsx `
  --hyperlink-input build\windows-nmake-release\tests\fastxlsx-streaming-image-hyperlinks.xlsx `
  --mixed-hyperlink-input build\windows-nmake-release\tests\fastxlsx-streaming-image-hyperlink-mixed-objects.xlsx `
  --work-dir build\qa\image-metadata
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_image_metadata_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-image-metadata.xlsx `
  -BasicPath build\windows-nmake-release\tests\fastxlsx-streaming-images.xlsx `
  -MixedObjectPath build\windows-nmake-release\tests\fastxlsx-streaming-mixed-object-rels.xlsx `
  -MemoryPath build\windows-nmake-release\tests\fastxlsx-streaming-memory-images.xlsx `
  -HyperlinkPath build\windows-nmake-release\tests\fastxlsx-streaming-image-hyperlinks.xlsx `
  -MixedHyperlinkPath build\windows-nmake-release\tests\fastxlsx-streaming-image-hyperlink-mixed-objects.xlsx
```

Python helper 会拆包检查 metadata drawing XML、basic media/drawing/table/hyperlink
关系、mixed-object owner-local relationship ids、memory-source media bytes /
drawing relationships / content types、图片对象 `a:hlinkClick` 与 drawing-local
hyperlink `.rels`，并用 `openpyxl` 核对 workbook、table 和 cell hyperlink semantics；
`openpyxl` 可能跳过 JPEG 图片读取，且不暴露图片对象 hyperlink metadata，因此
mixed-object / memory-source JPEG 图片数量和图片对象 hyperlink 语义以拆包 XML 和 Excel
COM 为准。Excel helper 只读打开 workbook，核对
`ImageMetadata` sheet 的 3 个 shapes、自定义
`NamedOnly` 名称、默认 `Picture 3` 名称、首图 `AlternativeText`、`editAs` 到
Excel `Placement` 的映射，以及首图 `from_offset` / `to_offset` 对 `Shape.Left` /
`Top` / `Width` / `Height` 的 EMU-to-points 偏移；同时核对基础图片样例的
`Images` / `SecondImage` / `Plain` shape counts、hyperlink/table counts 和 anchors，
mixed-object 样例的 hyperlinks / shapes / tables 数量，以及 `MemoryImages` 中
两张 memory-source 图片的 anchors，并核对 `ImageLinks` 中两张图片对象 hyperlink 的
shape hyperlink address / tooltip。Excel COM 当前会把
drawing XML 的 `descr` 暴露为 `Shape.AlternativeText`；结构真相仍以拆包后的
`xl/drawings/drawing*.xml` 为准。

当前 streaming Phase 3 metadata 样例由 `fastxlsx.streaming` 在默认 preset 下生成，
推荐路径是
`build/windows-nmake-release/tests/fastxlsx-streaming-phase3-metadata.xlsx`。本机
Excel COM 验证应确认 `Metadata` sheet 可打开，公式、row height、column width、
auto filter、merge areas 和 frozen panes 可见。当前本机验证结果为 `B2` 公式
`=A2*2`、`C2` 公式 `=IF(A2>0,"<yes>","&no")`、row 2 高度约 `19.3`、
auto filter `B2:D4`、merge areas `A3:B3` / `C4:D4`、`SplitRow=2` 和
`SplitColumn=3`。这不代表公式计算、cached values、calcChain、styles 或完整
Phase 3 已完成。

固定本地 QA 入口：

```powershell
py tools\verify_phase3_metadata.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-phase3-metadata.xlsx `
  --work-dir build\qa\phase3-metadata
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_phase3_metadata_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-phase3-metadata.xlsx
```

Python helper 会检查 package entries、side-effect absence、worksheet XML、`calcPr`、
formula XML escape、row height、column width records、frozen pane、auto filter、
merge ranges、suffix ordering，并用 `openpyxl` 核对 reader-visible semantics。
Excel helper 只读打开 workbook，核对可见公式、row height、列宽可见变化、
冻结窗格、自动筛选和合并区域。Excel COM 会对 row height / column width 做显示单位
换算；精确 XML 数值仍以拆包结构测试为准。

当前 document properties 样例由 `fastxlsx.unit` 和 `fastxlsx.streaming` 在默认
preset 下生成，推荐路径分别是
`build/windows-nmake-release/tests/fastxlsx-document-properties.xlsx` 和
`build/windows-nmake-release/tests/fastxlsx-streaming-document-properties.xlsx`。
本机 Excel COM 验证应优先确认 workbook 可打开、无修复弹窗，并在可访问时核对
BuiltinDocumentProperties 中的 Title、Author、Subject、Keywords 和 Category。
Excel COM 未暴露或规范化的字段以拆包后的 `docProps/core.xml` / `docProps/app.xml`
结构测试为准。

固定本地 QA 入口：

```powershell
py tools\verify_document_properties.py `
  --in-memory-input build\windows-nmake-release\tests\fastxlsx-document-properties.xlsx `
  --streaming-input build\windows-nmake-release\tests\fastxlsx-streaming-document-properties.xlsx `
  --work-dir build\qa\document-properties
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_document_properties_excel.ps1 `
  -InMemoryPath build\windows-nmake-release\tests\fastxlsx-document-properties.xlsx `
  -StreamingPath build\windows-nmake-release\tests\fastxlsx-streaming-document-properties.xlsx
```

Python helper 会检查两个 workbook 的 `docProps/core.xml`、`docProps/app.xml`、
content type overrides、package relationships、XML escape、无 `docProps/custom.xml`，
并用 `openpyxl` 核对 core properties 和 smoke sheet value。Excel helper 只读打开
两个 workbook 并核对 `Props!A1` / `StreamProps!A1`；如果当前 Excel COM session 能
稳定暴露 `BuiltinDocumentProperties`，脚本会同步核对 Title / Author / Subject /
Keywords / Category，否则记录为 XML/openpyxl helper authoritative。

当前 sharedStrings 样例由 `fastxlsx.streaming` 在默认 preset 下生成，推荐路径是
`build/windows-nmake-release/tests/fastxlsx-streaming-shared-strings.xlsx`。本机
Excel COM 验证可以运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_shared_strings_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings.xlsx
```

该脚本只读打开 workbook，核对 `Shared` sheet、`A1:D3` used range 和当前 smoke
样例中的字符串值；它不保存 workbook，也不进入默认 CI。

当前 `fastxlsx.streaming` 还包含 sharedStrings 空表结构回归样例：
`build/windows-nmake-release/tests/fastxlsx-streaming-shared-strings-empty-table.xlsx`。
该样例在 `StringStrategy::SharedString` 下只写数字、布尔和公式 cell；结构测试确认
不生成空 `xl/sharedStrings.xml`、sharedStrings content type 或 workbook relationship，
worksheet 不写 `t="s"` 或 `inlineStr`，同时公式仍写出 workbook recalculation metadata。
它主要是 package hygiene 回归，不接入默认 CI；本地 QA 可以运行：

```powershell
py tools\verify_shared_strings_absence.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings-empty-table.xlsx `
  --work-dir build\qa\shared-strings-absence
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_shared_strings_absence_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings-empty-table.xlsx
```

Python helper 会检查 ZIP absence 语义、`openpyxl` 读取值/公式，并在可用时创建
`XlsxWriter` 参考 workbook。Excel helper 会只读打开 workbook，确认无修复弹窗并核对
`NoStrings!A1:C1`。如果出现 Excel 修复弹窗，再按下面参考对比流程用 Excel /
`openpyxl` / `XlsxWriter` 生成等价参考文件并拆包对比。

## 结构异常时的参考对比流程

当生成的 `.xlsx` 结构异常、打不开、Excel 提示修复、UI 表现异常或结构测试失败时，
不要只猜测 XML。应创建一个语义等价的参考 `.xlsx`，拆包后对比 relevant XML。

参考文件可以来自两种来源：

1. 本机 Excel 创建同等内容后保存。
2. Python XLSX 库创建同等内容，例如 `openpyxl` 或 `XlsxWriter`。

对比范围按功能收敛：基础 package 先看 `[Content_Types].xml`、relationships、
workbook 和 worksheet XML；涉及 styles、drawing、media、tables、sharedStrings 或
docProps 时，再对比对应 relevant XML 和 part。

这些 Python 库只用于测试/排障参考，不是 FastXLSX 的运行时依赖。
本机当前优先使用 Windows Python launcher `py` 运行参考脚本；其他机器可以根据
实际环境使用 `python`、`python3` 或虚拟环境入口。不要把 Python XLSX 库写进
FastXLSX 的运行时依赖。

### 用 Excel 创建参考文件

可以手工创建，也可以在本机 Excel 可用时用 COM 自动化生成简单参考文件：

```powershell
$path = Join-Path (Get-Location) "reference.xlsx"
$excel = New-Object -ComObject Excel.Application
$excel.Visible = $false
$wb = $excel.Workbooks.Add()
$ws = $wb.Worksheets.Item(1)
$ws.Name = "Sheet1"
$ws.Cells.Item(1, 1).Value2 = 123
$ws.Cells.Item(1, 2).Value2 = "text"
$ws.Cells.Item(1, 3).Value2 = $true
$wb.SaveAs($path, 51)
$wb.Close($false)
$excel.Quit()
```

### 用 Python XLSX 库创建参考文件

如果本机已安装 `openpyxl`：

```python
from openpyxl import Workbook

wb = Workbook()
ws = wb.active
ws.title = "Sheet1"
ws["A1"] = 123
ws["B1"] = "text"
ws["C1"] = True
wb.save("reference.xlsx")
```

如果使用 `XlsxWriter`：

```python
import xlsxwriter

workbook = xlsxwriter.Workbook("reference.xlsx")
worksheet = workbook.add_worksheet("Sheet1")
worksheet.write_number(0, 0, 123)
worksheet.write_string(0, 1, "text")
worksheet.write_boolean(0, 2, True)
workbook.close()
```

当前 sharedStrings smoke 样例有固定参考检查脚本：

```powershell
py tools\verify_shared_strings_reference.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings.xlsx
```

在 Codex 桌面环境中，如果系统 `py` 没有 `openpyxl`，先用
`load_workspace_dependencies` 解析 bundled Python，再用该 Python 运行同一脚本。
当前本机系统 `py` 已验证 `openpyxl 3.1.2` 和 `xlsxwriter 3.2.0` 可用；Codex bundled
Python 已验证有 `openpyxl 3.1.5`，但没有 `xlsxwriter`。因此该脚本会尽量创建
并验证 `openpyxl` 和 `XlsxWriter` 参考 workbook；`XlsxWriter` 模块不存在时明确跳过。
脚本输出目录默认是 `build/qa/shared-strings-reference/`，其中的 report 和参考 `.xlsx`
都是本地 QA artifact，不要提交，也不要作为 FastXLSX 运行时依赖。

当前 styles 样例也有固定本地 QA 脚本：

```powershell
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

Python helper 检查 FastXLSX package XML、`xl/styles.xml`、content types、workbook
relationships、worksheet `s` attributes、custom number format XML escape、
sharedStrings + styles relationship ordering、wrap-text / horizontal / vertical alignment XML、bold/italic
font XML、direct ARGB font color XML 和 solid fill XML，并用 `openpyxl` 核对 `NumberFormat` /
`wrap_text` / `horizontal` / `vertical` / font flags / font color / fill 语义；可用时还会创建 `XlsxWriter`
参考 workbook。Excel helper 只读打开 number-format、sharedStrings + styles 和
limited alignment / font / fill 样例，核对值、公式、可见 NumberFormat、WrapText、
HorizontalAlignment、VerticalAlignment、Font.Bold、Font.Italic、Font.Color、
Interior.Pattern 和 Interior.Color。两者都是本地
QA artifact 和兼容性验证入口，不是运行时依赖，也不进入默认 CI。当前样式范围只覆盖
自定义 number format、窄 wrap-text + limited horizontal/vertical alignment、
窄 bold/italic/direct ARGB font color 和窄 solid foreground fill；
theme/tint/indexed font color、字号/名称/下划线、gradient fill、任意 pattern fill、theme/tint/indexed palette fill、
边框、完整对齐、date cell type、rich text、
dxf-backed conditional formatting、existing-file style preservation 和完整 Excel
formatting parity 仍需单独任务和验证。

当前 data validation prompt/error smoke 样例也有固定本地 QA 脚本：

```powershell
py tools\verify_data_validation_prompts.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-prompts.xlsx `
  --multi-range-input build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-multi-range.xlsx
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_data_validation_prompts_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-prompts.xlsx `
  -MultiRangePath build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-multi-range.xlsx
```

Python helper 检查 FastXLSX package XML、prompt/error attributes、`showDropDown`
attribute、无 `.rels` / `styles.xml` / content type 副作用，使用 `openpyxl` 读取
四条 data validation，并创建 `openpyxl` / `XlsxWriter` 参考 workbook。它还会检查
multi-range 样例的 package XML、`sqref="A2:A10 C2:C10 E2:E10"`、`count="1"` 和
`openpyxl` 多区域读取结果，并创建 `openpyxl` multi-range 参考 workbook。Excel helper
只读打开 workbook 并核对 `ValidationPrompt!A2:D2` 的 `Validation` 属性，其中
`showDropDown="1"` 对应 Excel COM `Validation.InCellDropdown = False`；同时打开
multi-range 样例确认 `ValidationRanges` 中 A/C/E 三段 validation areas。Excel COM
会把 custom validation 公式 `LEN(D2)>0` 返回为 `=LEN(D2)>0`，所以结构语义仍以拆包后的
worksheet XML 为准。

当前 image metadata smoke 样例也有固定本地 QA 脚本：

```powershell
py tools\verify_image_metadata.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-image-metadata.xlsx `
  --memory-input build\windows-nmake-release\tests\fastxlsx-streaming-memory-images.xlsx
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_image_metadata_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-image-metadata.xlsx `
  -MemoryPath build\windows-nmake-release\tests\fastxlsx-streaming-memory-images.xlsx
```

Python helper 检查 FastXLSX package XML、drawing `xdr:cNvPr name` / `descr`、XML
attribute escape、relationships、content types 和 media entries，使用 `openpyxl`
打开确认 3 张图片，并用 `XlsxWriter` 创建参考 workbook；传入 `--memory-input` 时还会
检查 memory-source package XML、media 签名、`openpyxl` smoke 和 `XlsxWriter` 参考
workbook。Excel helper 只读打开 workbook
并核对 shape 数量、custom/default shape name、`AlternativeText`、`Placement` 和
首图 marker offset 对 shape 几何的影响。这些仍是本地 QA artifact，不要提交，也不是
默认 CTest 或运行时依赖。

当前 table QA 首选统一入口覆盖 totals-row visibility metadata、style flags、
column attribute escaping 和 same-worksheet range-overlap：

```powershell
py tools\verify_tables.py `
  --tables-input build\windows-nmake-release\tests\fastxlsx-streaming-tables.xlsx `
  --style-flags-input build\windows-nmake-release\tests\fastxlsx-streaming-table-style-flags.xlsx `
  --column-escape-input build\windows-nmake-release\tests\fastxlsx-streaming-table-column-escape.xlsx `
  --overlap-input build\windows-nmake-release\tests\fastxlsx-streaming-table-range-overlap.xlsx `
  --work-dir build\qa\tables
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_tables_excel.ps1 `
  -TablesPath build\windows-nmake-release\tests\fastxlsx-streaming-tables.xlsx `
  -StyleFlagsPath build\windows-nmake-release\tests\fastxlsx-streaming-table-style-flags.xlsx `
  -ColumnEscapePath build\windows-nmake-release\tests\fastxlsx-streaming-table-column-escape.xlsx `
  -OverlapPath build\windows-nmake-release\tests\fastxlsx-streaming-table-range-overlap.xlsx
```

Python helper 检查 FastXLSX package XML、content types、worksheet `.rels`、
owner-local `rId`、`totalsRowShown` / `totalsRowCount`、caller-supplied
`totalsRowFunction` / `totalsRowLabel`、table style flags、table column attribute
escape、无 `table5.xml` 的 overlap 拒绝、无 `xl/styles.xml` / `xl/metadata.xml` /
`xl/calcChain.xml` 副作用，并用 `openpyxl` 读取 table semantics；可用时会在
`build/qa/tables/` 下创建 `XlsxWriter` 参考 workbook。Excel COM helper 只读打开
四个样例并核对 `ListObjects`、ranges、headers、totals row、style flags 和相邻 /
跨 worksheet tables 可见性。它们是本地 QA/排障工具，不接入默认 CTest/CI，也不是
运行时依赖。

旧的 table totals-row visibility metadata 样例仍可单独运行：

```powershell
py tools\verify_table_totals_metadata.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-tables.xlsx
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_table_totals_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-tables.xlsx
```

Python helper 检查 FastXLSX package XML、`totalsRowShown` / `totalsRowCount`、
`totalsRowFunction`、`totalsRowLabel`、空 label 省略、`autoFilter`、relationship ids、
content types、无 `xl/styles.xml`，
并用 `openpyxl` 读取 table metadata、用 `XlsxWriter` 生成语义参考 workbook。Excel
helper 只读打开 workbook 并核对 `InventoryTable.ShowTotals=False`、
`TotalsTable.ShowTotals=True`、`TotalsTable` 范围、totals row 范围和 Value 列 totals
calculation。结构异常时仍以拆包后的 table XML 语义为准。脚本应从项目根目录运行；
`build/qa/table-totals/` 下的 report 和参考 workbook 只是本地 QA artifact，不提交。

旧的 table range-overlap 样例也可单独运行：

```powershell
py tools\verify_table_overlap_metadata.py `
  --input build\windows-nmake-release\tests\fastxlsx-streaming-table-range-overlap.xlsx
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_table_overlap_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-table-range-overlap.xlsx
```

Python helper 检查 FastXLSX package XML、`tableParts` count、`table1.xml` 到
`table4.xml` 的 table name/ref、无 `table5.xml` 和无 `xl/styles.xml`，并用
`openpyxl` 读取 `Tables` / `OtherTables` 的 table ranges。Excel helper 只读打开
workbook 并核对 `Tables` sheet 有 3 个相邻 tables，`OtherTables` sheet 有 1 个
同 range table。它们只验证 table-vs-table overlap 边界，不代表完整 table conflict
analysis。

当前 conditional color scale 样例也有固定本地 QA 脚本：

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

Python helper 检查 package XML、suffix ordering、relationship id 不偏移、multi-range
`sqref`、priority、无 styles/dxfs/rels/content type/calcPr 副作用，并用 `openpyxl`
读取 two-color、three-color 和 multi-range 条件格式；可用时会用 `XlsxWriter` 创建参考 workbook。
Excel helper 只读打开 two-color、three-color 和 multi-range workbook，核对
`ColorScale!A2:A10`、`ThreeColorScale!A2:A10` 的 color scale 规则，以及
multi-range 中 A/C/E 三段区域有规则而 B 列间隔区没有规则。
这些是本地 QA artifact 和兼容性验证入口，不是运行时依赖，也不进入默认 CI。

当前 conditional data bar 样例也有固定本地 QA 脚本：

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

Python helper 检查 package XML、`<cfRule type="dataBar">`、两个 `<cfvo>`、一个
`<color rgb>`、可选 `showValue="0"`、multi-range `sqref`、与 color scale 共享 priority、无
styles/dxfs/rels/content type/calcPr 副作用，并用 `openpyxl` 读取 basic 和 multi-range
data bar；可用时会用 `XlsxWriter` 创建参考 workbook。Excel helper 只读打开 basic 和
metadata-order、multi-range workbook，核对 Excel COM 可见的 data bar、bar color、ShowValue 和
multi-area AppliesTo。XML 结构仍是权威；Excel/openpyxl/XlsxWriter 只作为本地
QA/排障参考，不接入默认 CTest/CI，也不是运行时依赖。

当前 conditional icon set 样例也有固定本地 QA 脚本：

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

QA note: this icon-set helper now covers the existing `IconSetValueType::Percentile`
path with `percentile` thresholds `10/50/90`, plus `showValue="0"` and
`reverse="1"`. This is QA hardening for the current basic built-in `3Arrows`
slice, not support for advanced/custom icon sets.

Python helper 检查 package XML、`<cfRule type="iconSet">`、内建 `3Arrows`、
三枚 `<cfvo>`、`showValue` / `reverse` metadata、multi-range `sqref`、与 color
scale / data bar 共享 priority、无 styles/dxfs/rels/content type/calcPr 副作用，
并用 `openpyxl` 读取 basic 和 multi-range icon set；可用时会用 `XlsxWriter`
创建参考 workbook。Excel helper 只读打开 basic 和 multi-range workbook，核对
Excel COM 可见的 icon set、IconSet ID、ShowIconOnly、ReverseOrder 和 multi-area
AppliesTo。XML 结构仍是权威；不要把该 helper 当成运行时依赖或默认 CI 门禁。

## 拆包和 XML 对比

XLSX 本质是 ZIP package。对比时先复制为 `.zip` 再解压：

```powershell
Copy-Item .\fastxlsx-output.xlsx .\fastxlsx-output.zip -Force
Copy-Item .\reference.xlsx .\reference.zip -Force
Expand-Archive .\fastxlsx-output.zip .\out-fastxlsx -Force
Expand-Archive .\reference.zip .\out-reference -Force
```

优先对比：

```text
[Content_Types].xml
_rels/.rels
xl/workbook.xml
xl/_rels/workbook.xml.rels
xl/worksheets/sheet1.xml
xl/worksheets/_rels/sheet*.xml.rels
xl/tables/table*.xml
xl/sharedStrings.xml
xl/styles.xml
docProps/core.xml
docProps/app.xml
```

如果涉及 configurable document properties，重点对比 core/app docProps 字段、
XML escape、package relationships、content type overrides，并确认没有额外的
`docProps/custom.xml`。

如果涉及图片或 drawing，还要额外对比：

```text
xl/media/*
xl/drawings/drawing*.xml
xl/drawings/_rels/drawing*.xml.rels
xl/worksheets/_rels/sheet*.xml.rels
```

对比时注意：

- Excel、openpyxl、XlsxWriter 生成的 XML 不一定 byte-level 相同。
- 重点比较 OpenXML 语义：part 是否存在、关系是否正确、content type 是否正确、
  sheet/cell/value/type 是否正确。
- 图片对比应重点看 media part 是否存在、relationship target 是否有效、worksheet
  `<drawing>` 引用是否匹配、anchor 语义是否等价；如果涉及图片 `ImageOptions`，
  还要核对 two-cell marker `xdr:colOff` / `xdr:rowOff` EMU offsets、
  `xdr:twoCellAnchor editAs`、`xdr:cNvPr` 的 `name` / `descr` attributes、XML
  attribute escape、空 description 省略和默认 `Picture N` 名称，而不是要求 XML
  字节完全一致。不要把 `editAs` 或 marker offset 检查写成 `oneCellAnchor` /
  `absoluteAnchor` 元素支持或 row/column resize 几何计算保证。
- namespace、属性顺序、默认值、压缩方式可能不同，不应直接当成错误。
- 如果 Excel 打开后自动修复，应保存 Excel 修复后的文件，再拆包比较修复前后差异。

## 性能路径验证

性能测试不能替代结构验证，但任何 API 或实现改动都不能牺牲 FastXLSX 的主线性能。

性能验证至少记录：

- 构建类型。
- 数据规模。
- 压缩等级。
- 字符串策略。
- 总耗时。
- 峰值内存。
- 输出文件大小。
- Excel / WPS / LibreOffice 打开结果。

性能测试应独立于默认单元测试，使用显式 label、target 或手工命令触发。

## 排障优先级

1. `ctest` 没有测试：先检查 `tests/CMakeLists.txt` 是否仍注册
   `fastxlsx.unit`、`fastxlsx.streaming` 和 `fastxlsx.opc`，或是否被误改回空测试入口。
2. Excel 打不开：先拆包检查 content types、relationships、workbook、worksheet。
3. Excel 提示修复：保存修复后的文件，与原文件和参考文件拆包对比 XML。
4. 单元格显示不对：检查 cell reference、type、value、shared strings 和 styles。
5. 内存异常：检查是否引入 DOM、完整 cell matrix、cell map 或跨行缓存。
6. 性能回退：检查 XML 编码、escape、数字转换、cell reference、压缩等级、
   sharedStrings 去重和 row buffer。
