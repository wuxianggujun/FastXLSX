param(
    [string]$Path = "build\windows-nmake-release\tests\fastxlsx-streaming-tables.xlsx"
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
$inventory = $null
$totals = $null
$plain = $null
$inventoryTable = $null
$totalsTable = $null
$inventoryTables = $null
$totalsTables = $null
$plainTables = $null
$inventoryRange = $null
$totalsRange = $null
$headerRange = $null
$dataBodyRange = $null
$totalsRowRange = $null
$totalsCells = $null
$totalsLabelCell = $null
$totalsValueCell = $null
$totalsColumns = $null
$totalsValueColumn = $null

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false

    $workbook = $excel.Workbooks.Open($resolved, 0, $true)
    $inventory = $workbook.Worksheets.Item("Inventory")
    $totals = $workbook.Worksheets.Item("Totals")
    $plain = $workbook.Worksheets.Item("Plain")

    $inventoryTables = $inventory.ListObjects
    $totalsTables = $totals.ListObjects
    $plainTables = $plain.ListObjects

    Assert-Equal $inventoryTables.Count 1 "Inventory ListObjects count"
    Assert-Equal $totalsTables.Count 1 "Totals ListObjects count"
    Assert-Equal $plainTables.Count 0 "Plain ListObjects count"

    $inventoryTable = Get-ListObjectByName $inventoryTables "InventoryTable"
    $totalsTable = Get-ListObjectByName $totalsTables "TotalsTable"
    Assert-True ($null -ne $inventoryTable) "Excel did not expose InventoryTable"
    Assert-True ($null -ne $totalsTable) "Excel did not expose TotalsTable"

    Assert-Equal $inventoryTable.ShowTotals $false "InventoryTable ShowTotals"
    $inventoryRange = $inventoryTable.Range
    Assert-Equal $inventoryRange.Address($false, $false) "A1:C3" "InventoryTable range"

    Assert-Equal $totalsTable.ShowTotals $true "TotalsTable ShowTotals"
    $totalsRange = $totalsTable.Range
    $headerRange = $totalsTable.HeaderRowRange
    $dataBodyRange = $totalsTable.DataBodyRange
    $totalsRowRange = $totalsTable.TotalsRowRange
    $totalsCells = $totalsRowRange.Cells
    $totalsLabelCell = $totalsCells.Item(1, 1)
    $totalsValueCell = $totalsCells.Item(1, 2)
    $totalsColumns = $totalsTable.ListColumns
    $totalsValueColumn = $totalsColumns.Item("Value")
    Assert-Equal $totalsRange.Address($false, $false) "A1:B3" "TotalsTable range"
    Assert-Equal $headerRange.Address($false, $false) "A1:B1" "TotalsTable header range"
    Assert-Equal $dataBodyRange.Address($false, $false) "A2:B2" "TotalsTable data body range"
    Assert-Equal $totalsRowRange.Address($false, $false) "A3:B3" "TotalsTable totals row range"
    Assert-Equal $totalsLabelCell.Value2 "Total" "TotalsTable totals label cell"
    Assert-Equal $totalsValueCell.Value2 2 "TotalsTable totals value cell"
    Assert-Equal $totalsValueColumn.TotalsCalculation 1 "TotalsTable Value totals calculation"

    Write-Host "OK: Excel opened table totals workbook read-only: $resolved"
    Write-Host "OK: InventoryTable is hidden totals metadata; TotalsTable exposes ShowTotals with A3:B3 totals row"
}
finally {
    if ($null -ne $workbook) {
        $workbook.Close($false) | Out-Null
    }
    if ($null -ne $excel) {
        $excel.Quit() | Out-Null
    }
    foreach ($object in @(
            $totalsValueColumn,
            $totalsColumns,
            $totalsValueCell,
            $totalsLabelCell,
            $totalsCells,
            $totalsRowRange,
            $dataBodyRange,
            $headerRange,
            $totalsRange,
            $inventoryRange,
            $totalsTable,
            $inventoryTable,
            $plainTables,
            $totalsTables,
            $inventoryTables,
            $plain,
            $totals,
            $inventory,
            $workbook,
            $excel)) {
        if ($null -ne $object) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
        }
    }
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
}
