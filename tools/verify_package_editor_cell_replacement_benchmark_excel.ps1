param(
    [Parameter(Mandatory = $true)]
    [string[]]$ResultPath,

    [string]$ReportPath = ""
)

$ErrorActionPreference = "Stop"

function Assert-BoolField {
    param(
        [object]$Value,
        [string]$Label
    )

    if ($true -ne [bool]$Value) {
        throw "$Label expected true"
    }
}

function Assert-NumberEqual {
    param(
        [object]$Actual,
        [double]$Expected,
        [string]$Label
    )

    if ([double]$Actual -ne $Expected) {
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

function Get-SourceCellValue {
    param(
        [int]$Row,
        [int]$Column
    )

    return [double](([int64]$Row * 1000000L) + [int64]$Column)
}

function Resolve-JsonReferencedPath {
    param(
        [object]$Path,
        [string]$ResultFile,
        [string]$Label
    )

    if ($null -eq $Path -or [string]::IsNullOrWhiteSpace([string]$Path)) {
        throw "$Label path is empty"
    }

    $pathText = [string]$Path
    $candidates = New-Object System.Collections.ArrayList
    if ([System.IO.Path]::IsPathRooted($pathText)) {
        [void]$candidates.Add($pathText)
    } else {
        [void]$candidates.Add((Join-Path -Path (Get-Location).Path -ChildPath $pathText))
        [void]$candidates.Add((Join-Path -Path (Split-Path -Parent $ResultFile) -ChildPath $pathText))
        [void]$candidates.Add((Join-Path -Path (Split-Path -Parent $ResultFile) -ChildPath (Split-Path -Leaf $pathText)))
    }

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "$Label path does not exist: $pathText"
}

function Release-ComObject {
    param(
        [object]$Value
    )

    if ($null -ne $Value -and [System.Runtime.InteropServices.Marshal]::IsComObject($Value)) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($Value)
    }
}

function Write-OfficeReport {
    param(
        [string]$Path,
        [object]$Report
    )

    $Report["verified_at_utc"] = [DateTime]::UtcNow.ToString("o")
    $json = $Report | ConvertTo-Json -Depth 8
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $json + "`n", $utf8NoBom)
}

$resolvedResults = @()
foreach ($path in $ResultPath) {
    $resolvedResults += (Resolve-Path -LiteralPath $path).Path
}

if ([string]::IsNullOrWhiteSpace($ReportPath)) {
    $ReportPath = Join-Path -Path (Split-Path -Parent $resolvedResults[0]) `
        -ChildPath "package-editor-cell-replacement-office-report.json"
}
$resolvedReportPath = [System.IO.Path]::GetFullPath($ReportPath)
$reportDirectory = Split-Path -Parent $resolvedReportPath
if (-not [string]::IsNullOrWhiteSpace($reportDirectory) -and
    -not (Test-Path -LiteralPath $reportDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $reportDirectory | Out-Null
}

$officeCases = New-Object System.Collections.ArrayList
$officeReport = [ordered]@{
    package_editor_cell_replacement_office_report_schema_version = "1"
    source_results = $resolvedResults
    verified_at_utc = [DateTime]::UtcNow.ToString("o")
    office_application = "Excel"
    office_version = $null
    read_only = $true
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

    foreach ($resultFile in $resolvedResults) {
        $result = Get-Content -LiteralPath $resultFile -Raw -Encoding UTF8 | ConvertFrom-Json
        if ([string]$result.package_editor_cell_replacement_benchmark_schema_version -ne "1") {
            throw "unsupported package editor benchmark schema in $resultFile"
        }
        if ([string]$result.scenario -ne "source-entry-cell-replacement") {
            throw "unexpected scenario in ${resultFile}: $($result.scenario)"
        }

        $rows = [int]$result.rows
        $cols = [int]$result.cols
        $sourceCells = [int64]$result.source_cells
        $replacementCount = [int64]$result.replacement_count

        $caseReport = [ordered]@{
            result_json = $resultFile
            output = [string]$result.output
            status = "pending"
            sheet = "Data"
            expected_rows = $rows
            expected_cols = $cols
            used_range_rows = $null
            used_range_cols = $null
            first_replacement_cell = "A1"
            expected_first_replacement_value = 900000000
            actual_first_replacement_value = $null
            tail_cell_checked = $false
            expected_tail_value = $null
            actual_tail_value = $null
            output_verified = [bool]$result.output_verified
            output_contains_first_replacement = [bool]$result.output_contains_first_replacement
            output_contains_tail_cell = [bool]$result.output_contains_tail_cell
            package_entry_source_mode = [string]$result.package_entry_source_mode
            output_entry_mode = [string]$result.output_entry_mode
        }
        [void]$officeCases.Add($caseReport)

        try {
            Assert-BoolField $result.output_verified "$resultFile output_verified"
            Assert-BoolField $result.output_contains_first_replacement "$resultFile output_contains_first_replacement"
            Assert-BoolField $result.output_contains_tail_cell "$resultFile output_contains_tail_cell"
            Assert-BoolField $result.plan_reports_source_entry_chunk_source "$resultFile plan_reports_source_entry_chunk_source"
            Assert-BoolField $result.plan_reports_file_backed_stream_rewrite "$resultFile plan_reports_file_backed_stream_rewrite"
            Assert-BoolField $result.output_plan_staged_replacement_chunks "$resultFile output_plan_staged_replacement_chunks"
            if ([bool]$result.output_plan_materialized_replacement) {
                throw "$resultFile output_plan_materialized_replacement expected false"
            }
            if ([string]$result.package_entry_source_mode -ne "source-zip-entry-chunk-source") {
                throw "$resultFile package_entry_source_mode expected source-zip-entry-chunk-source"
            }
            if ([string]$result.output_entry_mode -ne "file-backed-stream-rewrite") {
                throw "$resultFile output_entry_mode expected file-backed-stream-rewrite"
            }

            $workbookPath = Resolve-JsonReferencedPath $result.output $resultFile "$resultFile output"
            $caseReport["output"] = $workbookPath

            $workbook = $excel.Workbooks.Open($workbookPath, 0, $true)
            ++$opened
            $officeReport["opened_count"] = $opened
            $worksheet = $workbook.Worksheets.Item("Data")
            $caseReport["sheet"] = [string]$worksheet.Name

            $usedRange = $worksheet.UsedRange
            $usedRows = [int]$usedRange.Rows.Count
            $usedCols = [int]$usedRange.Columns.Count
            $caseReport["used_range_rows"] = $usedRows
            $caseReport["used_range_cols"] = $usedCols
            if (($usedRows -ne $rows) -or ($usedCols -ne $cols)) {
                throw "$resultFile Data UsedRange expected ${rows}x${cols}, got ${usedRows}x${usedCols}"
            }

            $firstValue = $worksheet.Cells.Item(1, 1).Value2
            $caseReport["actual_first_replacement_value"] = Convert-CellValueForReport $firstValue
            Assert-NumberEqual $firstValue 900000000 "$resultFile Data!A1"

            if ($replacementCount -lt $sourceCells) {
                $expectedTail = Get-SourceCellValue $rows $cols
                $tailValue = $worksheet.Cells.Item($rows, $cols).Value2
                $caseReport["tail_cell_checked"] = $true
                $caseReport["expected_tail_value"] = Convert-CellValueForReport $expectedTail
                $caseReport["actual_tail_value"] = Convert-CellValueForReport $tailValue
                Assert-NumberEqual $tailValue $expectedTail "$resultFile Data tail cell"
            }

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
            Release-ComObject $usedRange
            Release-ComObject $worksheet
            Release-ComObject $workbook
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
    Release-ComObject $excel
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()

    try {
        $officeReport["case_count"] = $resolvedResults.Count
        Write-OfficeReport $resolvedReportPath $officeReport
    }
    catch {
        if ($hadFailure) {
            Write-Warning "failed to write Excel office sidecar report: $($_.Exception.Message)"
        } else {
            throw
        }
    }
}

Write-Host "OK: Excel opened $opened PackageEditor cell replacement workbook(s) read-only"
Write-Host "OK: Data UsedRange, first replacement cell, and tail source cell verified"
Write-Host "OK: Excel office sidecar report written to $resolvedReportPath"
