# 测试流程

## 目标

FastXLSX 的测试不能只证明代码能编译，也不能只证明 XML 字符串看起来正确。
生成或修改 `.xlsx` 后，必须同时关注：

- C++ 单元测试是否覆盖核心编码逻辑。
- OpenXML package 结构是否正确。
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
`rId` 一致性，以及 anchor 的起始/结束单元格和 offset 语义。
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

数值编码负例不需要 Excel 可视化验证，因为期望结果是不生成有效 `.xlsx`。测试应覆盖
`NaN`、`+Inf` 和 `-Inf`，并确认 in-memory 路径在 `Workbook::save()` 抛
`FastXlsxError`，streaming 路径在 `WorksheetWriter::append_row()` 拒绝非有限
number / row height，`WorksheetWriter::set_column_width()` 拒绝非有限 width。
结构测试或排障时还要确认 worksheet XML 中没有写出 `nan`、`inf` 或 `-inf` 这类
非法数字文本。

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
- 图片 metadata 功能还必须确认 drawing XML 的 `xdr:cNvPr name` / `descr` 与
  Excel 可见 shape metadata 对应；当前推荐样例是
  `build/windows-nmake-release/tests/fastxlsx-streaming-image-metadata.xlsx`。
- table totals-row visibility metadata 必须用本机 Excel COM 或人工打开确认
  `ListObject.ShowTotals` 和 totals row 范围。当前推荐样例是
  `build/windows-nmake-release/tests/fastxlsx-streaming-tables.xlsx`，其中
  `InventoryTable` 应保持隐藏 totals row，`TotalsTable` 应显示 totals row 且范围为
  `A1:B3` / totals row `A3:B3`。
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
Excel COM 验证可以运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_image_metadata_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-image-metadata.xlsx
```

该脚本只读打开 workbook，核对 `ImageMetadata` sheet 的 3 个 shapes、自定义
`NamedOnly` 名称、默认 `Picture 3` 名称，以及首图 `AlternativeText`。Excel COM
当前会把 drawing XML 的 `descr` 暴露为 `Shape.AlternativeText`；结构真相仍以拆包后的
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

当前 document properties 样例由 `fastxlsx.unit` 和 `fastxlsx.streaming` 在默认
preset 下生成，推荐路径分别是
`build/windows-nmake-release/tests/fastxlsx-document-properties.xlsx` 和
`build/windows-nmake-release/tests/fastxlsx-streaming-document-properties.xlsx`。
本机 Excel COM 验证应优先确认 workbook 可打开、无修复弹窗，并在可访问时核对
BuiltinDocumentProperties 中的 Title、Author、Subject、Keywords 和 Category。
Excel COM 未暴露或规范化的字段以拆包后的 `docProps/core.xml` / `docProps/app.xml`
结构测试为准。

当前 sharedStrings 样例由 `fastxlsx.streaming` 在默认 preset 下生成，推荐路径是
`build/windows-nmake-release/tests/fastxlsx-streaming-shared-strings.xlsx`。本机
Excel COM 验证可以运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_shared_strings_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings.xlsx
```

该脚本只读打开 workbook，核对 `Shared` sheet、`A1:D3` used range 和当前 smoke
样例中的字符串值；它不保存 workbook，也不进入默认 CI。

## 结构异常时的参考对比流程

当生成的 `.xlsx` 打不开、Excel 提示修复、或结构测试失败时，不要只猜测 XML。
应创建一个语义等价的参考 `.xlsx`，拆包后对比 XML。

参考文件可以来自两种来源：

1. 本机 Excel 创建同等内容后保存。
2. Python XLSX 库创建同等内容，例如 `openpyxl` 或 `XlsxWriter`。

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
  --input build\windows-nmake-release\tests\fastxlsx-streaming-image-metadata.xlsx
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_image_metadata_excel.ps1 `
  -Path build\windows-nmake-release\tests\fastxlsx-streaming-image-metadata.xlsx
```

Python helper 检查 FastXLSX package XML、drawing `xdr:cNvPr name` / `descr`、XML
attribute escape、relationships、content types 和 media entries，使用 `openpyxl`
打开确认 3 张图片，并用 `XlsxWriter` 创建参考 workbook。Excel helper 只读打开 workbook
并核对 shape 数量、custom/default shape name 和 `AlternativeText`。这些仍是本地
QA artifact，不要提交，也不是默认 CTest 或运行时依赖。

当前 table totals-row visibility metadata 样例也有固定本地 QA 脚本：

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

当前 table range-overlap 样例也有固定本地 QA 脚本：

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
  还要核对 `xdr:twoCellAnchor editAs`、`xdr:cNvPr` 的 `name` / `descr` attributes、
  XML attribute escape、空 description 省略和默认 `Picture N` 名称，而不是要求 XML
  字节完全一致。不要把 `editAs` 检查写成 `oneCellAnchor` / `absoluteAnchor` 元素支持
  或 row/column resize 几何计算保证。
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
