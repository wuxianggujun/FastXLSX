param(
    [string]$ReportPath = "build\qa\benchmark-matrix\benchmark-matrix-report.json",
    [int]$MaxWorkbooks = 4
)

$ErrorActionPreference = "Stop"

function Assert-CellValue {
    param(
        [object]$Worksheet,
        [int]$Row,
        [int]$Column,
        [object]$Expected,
        [string]$Label
    )

    $value = $Worksheet.Cells.Item($Row, $Column).Value2
    if ($Expected -is [int] -or $Expected -is [long] -or $Expected -is [double]) {
        if ([double]$value -ne [double]$Expected) {
            throw "$Label expected '$Expected', got '$value'"
        }
        return
    }
    if ([string]$value -ne [string]$Expected) {
        throw "$Label expected '$Expected', got '$value'"
    }
}

$resolvedReport = (Resolve-Path -LiteralPath $ReportPath).Path
$report = Get-Content -LiteralPath $resolvedReport -Raw | ConvertFrom-Json
$cases = @($report.cases)
if ($cases.Count -eq 0) {
    throw "benchmark matrix report has no cases: $resolvedReport"
}
$limit = [Math]::Min($MaxWorkbooks, $cases.Count)

$excel = $null
$workbook = $null
$worksheet = $null
$usedRange = $null
$opened = 0

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false
    $excel.AskToUpdateLinks = $false

    for ($index = 0; $index -lt $limit; ++$index) {
        $case = $cases[$index]
        $workbookPath = (Resolve-Path -LiteralPath $case.output).Path
        $workbook = $excel.Workbooks.Open($workbookPath, 0, $true)
        $worksheet = $workbook.Worksheets.Item("Sheet1")
        $usedRange = $worksheet.UsedRange
        $rows = $usedRange.Rows.Count
        $columns = $usedRange.Columns.Count
        if (($rows -ne [int]$report.rows) -or ($columns -ne [int]$report.cols)) {
            throw "$($case.name) UsedRange expected $($report.rows)x$($report.cols), got ${rows}x${columns}"
        }

        Assert-CellValue $worksheet 1 1 $case.expected.sheet1_first_cell "$($case.name) Sheet1!A1"
        Assert-CellValue $worksheet ([int]$report.rows) ([int]$report.cols) `
            $case.expected.sheet1_last_cell "$($case.name) Sheet1 last cell"

        $workbook.Close($false) | Out-Null
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($usedRange)
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($worksheet)
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($workbook)
        $usedRange = $null
        $worksheet = $null
        $workbook = $null
        ++$opened
    }

    Write-Host "OK: Excel opened $opened benchmark matrix workbook(s) read-only from $resolvedReport"
    Write-Host "OK: Sheet1 UsedRange and first/last cells verified"
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
