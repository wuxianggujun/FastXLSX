param(
    [string]$Path = "build\windows-nmake-release\tests\fastxlsx-streaming-table-range-overlap.xlsx"
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

function Get-ListObjectByName {
    param(
        [object]$ListObjects,
        [string]$Name
    )

    foreach ($table in @($ListObjects)) {
        if ([string]$table.Name -eq $Name) {
            return $table
        }
    }
    return $null
}

$resolved = (Resolve-Path -LiteralPath $Path).Path
$excel = $null
$workbook = $null
$tables = $null
$otherTables = $null
$tablesListObjects = $null
$otherListObjects = $null
$firstTable = $null
$adjacentColumnsTable = $null
$adjacentRowsTable = $null
$otherSheetTable = $null
$firstRange = $null
$adjacentColumnsRange = $null
$adjacentRowsRange = $null
$otherRange = $null

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false

    $workbook = $excel.Workbooks.Open($resolved, 0, $true)
    $tables = $workbook.Worksheets.Item("Tables")
    $otherTables = $workbook.Worksheets.Item("OtherTables")

    $tablesListObjects = $tables.ListObjects
    $otherListObjects = $otherTables.ListObjects

    Assert-Equal $tablesListObjects.Count 3 "Tables ListObjects count"
    Assert-Equal $otherListObjects.Count 1 "OtherTables ListObjects count"

    $firstTable = Get-ListObjectByName $tablesListObjects "FirstTable"
    $adjacentColumnsTable = Get-ListObjectByName $tablesListObjects "AdjacentColumnsTable"
    $adjacentRowsTable = Get-ListObjectByName $tablesListObjects "AdjacentRowsTable"
    $otherSheetTable = Get-ListObjectByName $otherListObjects "OtherSheetTable"
    Assert-True ($null -ne $firstTable) "Excel did not expose FirstTable"
    Assert-True ($null -ne $adjacentColumnsTable) "Excel did not expose AdjacentColumnsTable"
    Assert-True ($null -ne $adjacentRowsTable) "Excel did not expose AdjacentRowsTable"
    Assert-True ($null -ne $otherSheetTable) "Excel did not expose OtherSheetTable"

    $firstRange = $firstTable.Range
    $adjacentColumnsRange = $adjacentColumnsTable.Range
    $adjacentRowsRange = $adjacentRowsTable.Range
    $otherRange = $otherSheetTable.Range

    Assert-Equal $firstRange.Address($false, $false) "A1:B2" "FirstTable range"
    Assert-Equal $adjacentColumnsRange.Address($false, $false) "C1:D2" "AdjacentColumnsTable range"
    Assert-Equal $adjacentRowsRange.Address($false, $false) "A3:B4" "AdjacentRowsTable range"
    Assert-Equal $otherRange.Address($false, $false) "A1:B2" "OtherSheetTable range"

    Write-Host "OK: Excel opened table overlap workbook read-only: $resolved"
    Write-Host "OK: Same-sheet adjacent tables and cross-sheet same range were exposed without repair"
}
finally {
    if ($null -ne $workbook) {
        $workbook.Close($false) | Out-Null
    }
    if ($null -ne $excel) {
        $excel.Quit() | Out-Null
    }
    foreach ($object in @(
            $otherRange,
            $adjacentRowsRange,
            $adjacentColumnsRange,
            $firstRange,
            $otherSheetTable,
            $adjacentRowsTable,
            $adjacentColumnsTable,
            $firstTable,
            $otherListObjects,
            $tablesListObjects,
            $otherTables,
            $tables,
            $workbook,
            $excel)) {
        if ($null -ne $object) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
        }
    }
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
}
