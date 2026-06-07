param(
    [string]$ReportPath = "build\qa\benchmark-matrix\benchmark-matrix-report.json",
    [int]$MaxWorkbooks = 4
)

$ErrorActionPreference = "Stop"

function Assert-CellValue {
    param(
        [object]$Actual,
        [object]$Expected,
        [string]$Label
    )

    if ($Expected -is [int] -or $Expected -is [long] -or $Expected -is [double]) {
        if ([double]$Actual -ne [double]$Expected) {
            throw "$Label expected '$Expected', got '$Actual'"
        }
        return
    }
    if ([string]$Actual -ne [string]$Expected) {
        throw "$Label expected '$Expected', got '$Actual'"
    }
}

function Convert-CellValueForReport {
    param(
        [object]$Value
    )

    if ($null -eq $Value) {
        return $null
    }
    if ($Value -is [double]) {
        $rounded = [Math]::Round([double]$Value)
        if ([double]$Value -eq $rounded) {
            return [long]$rounded
        }
    }
    return $Value
}

function Resolve-ExistingPathString {
    param(
        [object]$Path,
        [string]$Label
    )

    if ($null -eq $Path -or [string]::IsNullOrWhiteSpace([string]$Path)) {
        throw "$Label path is empty"
    }
    return (Resolve-Path -LiteralPath ([string]$Path)).Path
}

function Write-OfficeReport {
    param(
        [string]$Path,
        [object]$Report,
        [int]$CaseCount
    )

    $Report["verified_at_utc"] = [DateTime]::UtcNow.ToString("o")
    $Report["case_count"] = $CaseCount
    $Report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding utf8
}

$resolvedReport = (Resolve-Path -LiteralPath $ReportPath).Path
$officeReportPath = Join-Path -Path (Split-Path -Parent $resolvedReport) `
    -ChildPath "benchmark-matrix-office-report.json"
$report = Get-Content -LiteralPath $resolvedReport -Raw | ConvertFrom-Json
$cases = @($report.cases)
if ($cases.Count -eq 0) {
    throw "benchmark matrix report has no cases: $resolvedReport"
}
$limit = [Math]::Min($MaxWorkbooks, $cases.Count)
$expectedRows = [int]$report.rows
$expectedCols = [int]$report.cols

$officeCases = New-Object System.Collections.ArrayList
$officeReport = [ordered]@{
    benchmark_matrix_office_report_schema_version = "1"
    source_report = $resolvedReport
    verified_at_utc = [DateTime]::UtcNow.ToString("o")
    office_application = "Excel"
    office_version = $null
    read_only = $true
    max_workbooks = [int]$MaxWorkbooks
    case_count = 0
    opened_count = 0
    cases = $officeCases
}

$excel = $null
$workbook = $null
$worksheet = $null
$usedRange = $null
$opened = 0
$hadFailure = $false

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false
    $excel.AskToUpdateLinks = $false
    $officeReport["office_version"] = [string]$excel.Version

    for ($index = 0; $index -lt $limit; ++$index) {
        $case = $cases[$index]
        $caseReport = [ordered]@{
            name = [string]$case.name
            output = [string]$case.output
            result_json = [string]$case.result_json
            status = "pending"
            sheet = "Sheet1"
            used_range_rows = $null
            used_range_cols = $null
            expected_rows = $expectedRows
            expected_cols = $expectedCols
            first_cell = $null
            last_cell = $null
        }
        [void]$officeCases.Add($caseReport)

        try {
            $workbookPath = Resolve-ExistingPathString $case.output "$($case.name) output"
            $resultJsonPath = Resolve-ExistingPathString $case.result_json "$($case.name) result_json"
            $caseReport["output"] = $workbookPath
            $caseReport["result_json"] = $resultJsonPath

            $workbook = $excel.Workbooks.Open($workbookPath, 0, $true)
            ++$opened
            $officeReport["opened_count"] = $opened
            $worksheet = $workbook.Worksheets.Item("Sheet1")
            $caseReport["sheet"] = [string]$worksheet.Name
            $usedRange = $worksheet.UsedRange
            $rows = [int]$usedRange.Rows.Count
            $columns = [int]$usedRange.Columns.Count
            $caseReport["used_range_rows"] = $rows
            $caseReport["used_range_cols"] = $columns
            if (($rows -ne $expectedRows) -or ($columns -ne $expectedCols)) {
                throw "$($case.name) UsedRange expected ${expectedRows}x${expectedCols}, got ${rows}x${columns}"
            }

            $firstCell = $worksheet.Cells.Item(1, 1).Value2
            $lastCell = $worksheet.Cells.Item($expectedRows, $expectedCols).Value2
            $caseReport["first_cell"] = Convert-CellValueForReport $firstCell
            $caseReport["last_cell"] = Convert-CellValueForReport $lastCell

            Assert-CellValue $firstCell $case.expected.sheet1_first_cell "$($case.name) Sheet1!A1"
            Assert-CellValue $lastCell $case.expected.sheet1_last_cell "$($case.name) Sheet1 last cell"

            $caseReport["status"] = "opened"
        }
        catch {
            $hadFailure = $true
            $caseReport["status"] = "failed"
            $caseReport["error"] = $_.Exception.Message
            throw
        }
        finally {
            if ($null -ne $workbook) {
                $workbook.Close($false) | Out-Null
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
            $usedRange = $null
            $worksheet = $null
            $workbook = $null
        }
    }
}
finally {
    if ($null -ne $excel) {
        $excel.Quit() | Out-Null
    }
    if ($null -ne $excel) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($excel)
    }
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()

    try {
        Write-OfficeReport $officeReportPath $officeReport $cases.Count
    }
    catch {
        if ($hadFailure) {
            Write-Warning "failed to write Excel office sidecar report: $($_.Exception.Message)"
        } else {
            throw
        }
    }
}

Write-Host "OK: Excel opened $opened benchmark matrix workbook(s) read-only from $resolvedReport"
Write-Host "OK: Sheet1 UsedRange and first/last cells verified"
Write-Host "OK: Excel office sidecar report written to $officeReportPath"
