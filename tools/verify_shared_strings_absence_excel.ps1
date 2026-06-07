param(
    [string]$Path = "build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings-empty-table.xlsx"
)

$ErrorActionPreference = "Stop"

function Assert-CellValue {
    param(
        [object]$Worksheet,
        [string]$Address,
        [object]$Expected
    )

    $value = $Worksheet.Range($Address).Value2
    if ([string]$value -ne [string]$Expected) {
        throw "$Address expected '$Expected', got '$value'"
    }
}

function Assert-CellFormula {
    param(
        [object]$Worksheet,
        [string]$Address,
        [string]$Expected
    )

    $formula = $Worksheet.Range($Address).Formula
    if ([string]$formula -ne $Expected) {
        throw "$Address expected formula '$Expected', got '$formula'"
    }
}

$resolved = (Resolve-Path -LiteralPath $Path).Path
$excel = $null
$workbook = $null
$worksheet = $null
$usedRange = $null

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false
    $excel.AskToUpdateLinks = $false

    $workbook = $excel.Workbooks.Open($resolved, 0, $true)
    $worksheet = $workbook.Worksheets.Item("NoStrings")

    Assert-CellValue $worksheet "A1" 42
    $b1 = $worksheet.Range("B1").Value2
    if ([bool]$b1 -ne $true) {
        throw "B1 expected TRUE, got '$b1'"
    }
    Assert-CellFormula $worksheet "C1" "=A1+1"

    $usedRange = $worksheet.UsedRange
    $rows = $usedRange.Rows.Count
    $columns = $usedRange.Columns.Count
    if (($rows -ne 1) -or ($columns -ne 3)) {
        throw "NoStrings sheet UsedRange expected 1x3, got ${rows}x${columns}"
    }

    Write-Host "OK: Excel opened sharedStrings absence workbook read-only: $resolved"
    Write-Host "OK: NoStrings sheet values/formula and UsedRange 1x3 verified"
}
finally {
    if ($null -ne $workbook) {
        $workbook.Close($false) | Out-Null
    }
    if ($null -ne $excel) {
        $excel.Quit() | Out-Null
    }
    if ($null -ne $usedRange) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($usedRange)
    }
    if ($null -ne $worksheet) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($worksheet)
    }
    if ($null -ne $workbook) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($workbook)
    }
    if ($null -ne $excel) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($excel)
    }
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
}
