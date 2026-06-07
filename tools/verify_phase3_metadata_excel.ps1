param(
    [string]$Path = "build\windows-nmake-release\tests\fastxlsx-streaming-phase3-metadata.xlsx"
)

$ErrorActionPreference = "Stop"

function Assert-Equal {
    param(
        [AllowNull()]
        [object]$Actual,
        [AllowNull()]
        [object]$Expected,
        [string]$Message
    )

    if ([string]$Actual -ne [string]$Expected) {
        throw "$Message expected '$Expected', got '$Actual'"
    }
}

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Assert-Near {
    param(
        [double]$Actual,
        [double]$Expected,
        [double]$Tolerance,
        [string]$Message
    )

    if ([Math]::Abs($Actual - $Expected) -gt $Tolerance) {
        throw "$Message expected $Expected +/- $Tolerance, got $Actual"
    }
}

$resolved = (Resolve-Path -LiteralPath $Path).Path
$excel = $null
$workbook = $null
$worksheet = $null

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false

    $workbook = $excel.Workbooks.Open($resolved, 0, $true)
    $workbook.Activate() | Out-Null
    $worksheet = $workbook.Worksheets.Item("Metadata")
    $worksheet.Activate() | Out-Null

    $usedRange = $worksheet.UsedRange
    Assert-Equal $usedRange.Row 1 "Metadata UsedRange first row"
    Assert-Equal $usedRange.Column 1 "Metadata UsedRange first column"
    Assert-Equal $usedRange.Rows.Count 4 "Metadata UsedRange row count"
    Assert-Equal $usedRange.Columns.Count 4 "Metadata UsedRange column count"

    Assert-Equal $worksheet.Range("A2").Value2 42 "A2 numeric value"
    Assert-Equal $worksheet.Range("B2").Formula "=A2*2" "B2 formula"
    Assert-Equal $worksheet.Range("C2").Formula "=IF(A2>0,""<yes>"",""&no"")" "C2 escaped formula"
    Assert-Equal $worksheet.Range("D2").Value2 $true "D2 boolean value"
    Assert-Equal $worksheet.Range("B4").Value2 7 "B4 numeric value"

    Assert-Near $worksheet.Rows.Item(2).RowHeight 19.25 0.1 "row 2 height"
    $columnAWidth = [double]$worksheet.Columns.Item(1).ColumnWidth
    $columnBWidth = [double]$worksheet.Columns.Item(2).ColumnWidth
    $columnCWidth = [double]$worksheet.Columns.Item(3).ColumnWidth
    $columnDWidth = [double]$worksheet.Columns.Item(4).ColumnWidth
    Assert-True ($columnAWidth -gt $columnBWidth) "column A should be visibly wider than default column B"
    Assert-Near $columnCWidth $columnDWidth 0.01 "column C/D shared width"
    Assert-True ([Math]::Abs($columnCWidth - $columnBWidth) -gt 0.1) `
        "column C/D should visibly differ from default column B"

    $window = $excel.ActiveWindow
    Assert-Equal $window.FreezePanes $true "freeze panes enabled"
    Assert-Equal $window.SplitRow 2 "freeze panes split row"
    Assert-Equal $window.SplitColumn 3 "freeze panes split column"

    Assert-True ($null -ne $worksheet.AutoFilter) "Metadata worksheet should expose AutoFilter"
    Assert-Equal $worksheet.AutoFilter.Range.Address($false, $false) "B2:D4" "autoFilter range"
    Assert-Equal $worksheet.Range("A3").MergeArea.Address($false, $false) "A3:B3" "A3 merge area"
    Assert-Equal $worksheet.Range("C4").MergeArea.Address($false, $false) "C4:D4" "C4 merge area"

    Write-Host "OK: Excel opened Phase 3 metadata workbook read-only: $resolved"
    Write-Host "OK: UsedRange, formulas, row height, column widths, freeze panes, autoFilter, and merged ranges verified"
}
finally {
    if ($null -ne $workbook) {
        $workbook.Close($false) | Out-Null
    }
    if ($null -ne $excel) {
        $excel.Quit() | Out-Null
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
