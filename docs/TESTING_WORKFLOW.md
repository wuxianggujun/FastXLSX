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
- 数字、日期、布尔、字符串编码。
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

结构测试应检查 relationships、content type override/default、sheet id、
relationship target、worksheet `sheetData`、cell reference 和 value type。

### 3. 本机 Excel 可视化验证

本机有 Excel 时，生成的关键样例必须用 Excel 打开做可视化验证。

验证点：

- Excel 打开时没有修复弹窗。
- sheet 名、行列位置、单元格值与预期一致。
- 数字、布尔、字符串、日期、公式写入可见结果符合预期。
- 样式、列宽、行高、合并单元格、冻结窗格等高级功能在后续实现时可见。
- 保存后再打开仍然正常。

Excel 可视化验证是本地验收步骤，不应作为默认 CI 的强依赖。CI 可以做结构检查
和库级兼容测试；需要 Excel 的验证应明确标记为本机人工或本机自动化验证。

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

当前 Phase 1 smoke 样例由 `fastxlsx.unit` 生成，默认位于测试工作目录，例如
`build-nmake/tests/fastxlsx-phase1-minimal.xlsx`。本机验证可以用 Excel COM
只读打开该文件并核对 `Sheet1`、`A1`、`B1`、`C1`、`A2`、`B2`。

## 结构异常时的参考对比流程

当生成的 `.xlsx` 打不开、Excel 提示修复、或结构测试失败时，不要只猜测 XML。
应创建一个语义等价的参考 `.xlsx`，拆包后对比 XML。

参考文件可以来自两种来源：

1. 本机 Excel 创建同等内容后保存。
2. Python XLSX 库创建同等内容，例如 `openpyxl` 或 `XlsxWriter`。

这些 Python 库只用于测试/排障参考，不是 FastXLSX 的运行时依赖。

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
xl/sharedStrings.xml
xl/styles.xml
```

对比时注意：

- Excel、openpyxl、XlsxWriter 生成的 XML 不一定 byte-level 相同。
- 重点比较 OpenXML 语义：part 是否存在、关系是否正确、content type 是否正确、
  sheet/cell/value/type 是否正确。
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
