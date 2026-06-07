param(
    [string]$Path = "build\windows-nmake-release\tests\fastxlsx-streaming-shared-strings.xlsx"
)

$ErrorActionPreference = "Stop"

function Assert-CellValue {
    param(
        [object]$Worksheet,
        [string]$Address,
        [AllowNull()]
        [string]$Expected,
        [switch]$AllowEmpty
    )

    $value = $Worksheet.Range($Address).Value2
    if ($AllowEmpty) {
        if (($null -ne $value) -and ([string]$value -ne "")) {
            throw "$Address expected an empty visible value, got '$value'"
        }
        return
    }

    if ([string]$value -ne $Expected) {
        throw "$Address expected '$Expected', got '$value'"
    }
}

$resolved = (Resolve-Path -LiteralPath $Path).Path
$excel = $null
$workbook = $null

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false

    $workbook = $excel.Workbooks.Open($resolved, 0, $true)
    $worksheet = $workbook.Worksheets.Item("Shared")

    Assert-CellValue $worksheet "A1" "repeat"
    Assert-CellValue $worksheet "B1" "space "
    Assert-CellValue $worksheet "C1" "escaped & <tag>"
    Assert-CellValue $worksheet "A2" "repeat"
    Assert-CellValue $worksheet "B2" "space "
    Assert-CellValue $worksheet "A3" $null -AllowEmpty
    Assert-CellValue $worksheet "B3" " leading"
    Assert-CellValue $worksheet "C3" "`tindent"
    Assert-CellValue $worksheet "D3" "repeat"

    $usedRange = $worksheet.UsedRange
    $rows = $usedRange.Rows.Count
    $columns = $usedRange.Columns.Count
    if (($rows -ne 3) -or ($columns -ne 4)) {
        throw "Shared sheet UsedRange expected 3x4, got ${rows}x${columns}"
    }

    Write-Host "OK: Excel opened sharedStrings workbook read-only: $resolved"
    Write-Host "OK: Shared sheet values and UsedRange 3x4 verified"
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
