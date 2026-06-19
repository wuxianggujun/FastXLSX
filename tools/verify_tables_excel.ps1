param(
    [string]$TablesPath = "build\windows-nmake-release\tests\fastxlsx-streaming-tables.xlsx",
    [string]$StyleFlagsPath = "build\windows-nmake-release\tests\fastxlsx-streaming-table-style-flags.xlsx",
    [string]$ColumnEscapePath = "build\windows-nmake-release\tests\fastxlsx-streaming-table-column-escape.xlsx",
    [string]$OverlapPath = "build\windows-nmake-release\tests\fastxlsx-streaming-table-range-overlap.xlsx"
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

function Assert-Bool {
    param(
        [object]$Actual,
        [bool]$Expected,
        [string]$Message
    )

    if ([bool]$Actual -ne $Expected) {
        throw "$Message expected '$Expected', got '$Actual'"
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

function Close-Workbook {
    param([AllowNull()][object]$Workbook)

    if ($null -ne $Workbook) {
        $Workbook.Close($false) | Out-Null
    }
}

function Release-ComObject {
    param([AllowNull()][object]$Object)

    if ($null -ne $Object) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($Object)
    }
}

function Verify-TablesWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $inventory = $null
    $totals = $null
    $plain = $null
    $inventoryTables = $null
    $totalsTables = $null
    $plainTables = $null
    $inventoryTable = $null
    $totalsTable = $null
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
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
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

        Assert-Bool $inventoryTable.ShowTotals $false "InventoryTable ShowTotals"
        $inventoryRange = $inventoryTable.Range
        Assert-Equal $inventoryRange.Address($false, $false) "A1:C3" "InventoryTable range"

        Assert-Bool $totalsTable.ShowTotals $true "TotalsTable ShowTotals"
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
        Write-Host "OK: InventoryTable and TotalsTable ranges/totals row metadata verified"
    }
    finally {
        Close-Workbook $workbook
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
                $workbook)) {
            Release-ComObject $object
        }
    }
}

function Verify-StyleFlagsWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $worksheet = $null
    $listObjects = $null
    $table = $null
    $style = $null
    $range = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $worksheet = $workbook.Worksheets.Item("StyleFlags")
        $listObjects = $worksheet.ListObjects

        Assert-Equal $listObjects.Count 1 "StyleFlags ListObjects count"
        $table = Get-ListObjectByName $listObjects "StyleFlagTable"
        Assert-True ($null -ne $table) "Excel did not expose StyleFlagTable"
        $range = $table.Range

        Assert-Equal $range.Address($false, $false) "A1:B2" "StyleFlagTable range"
        $style = $table.TableStyle
        Assert-Equal $style.Name "TableStyleMedium4" "StyleFlagTable style"
        Assert-Bool $table.ShowTableStyleFirstColumn $true "StyleFlagTable first-column style flag"
        Assert-Bool $table.ShowTableStyleLastColumn $true "StyleFlagTable last-column style flag"
        Assert-Bool $table.ShowTableStyleRowStripes $false "StyleFlagTable row-stripes style flag"
        Assert-Bool $table.ShowTableStyleColumnStripes $true "StyleFlagTable column-stripes style flag"

        Write-Host "OK: Excel opened table style-flags workbook read-only: $resolved"
        Write-Host "OK: StyleFlagTable style flags verified as True/True/False/True"
    }
    finally {
        Close-Workbook $workbook
        foreach ($object in @($range, $style, $table, $listObjects, $worksheet, $workbook)) {
            Release-ComObject $object
        }
    }
}

function Verify-ColumnEscapeWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $worksheet = $null
    $listObjects = $null
    $table = $null
    $range = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $worksheet = $workbook.Worksheets.Item("TableEscapes")
        $listObjects = $worksheet.ListObjects

        Assert-Equal $listObjects.Count 1 "TableEscapes ListObjects count"
        $table = Get-ListObjectByName $listObjects "EscapedColumnTable"
        Assert-True ($null -ne $table) "Excel did not expose EscapedColumnTable"
        $range = $table.Range

        Assert-Equal $range.Address($false, $false) "A1:C3" "EscapedColumnTable range"
        Assert-Bool $table.ShowTotals $true "EscapedColumnTable ShowTotals"
        Assert-Equal $worksheet.Range("A1").Value2 'Text "quoted"' "escaped header A1"
        Assert-Equal $worksheet.Range("B1").Value2 "Owner's Share" "escaped header B1"
        Assert-Equal $worksheet.Range("C1").Value2 "A&B<Limit>" "escaped header C1"
        Assert-Equal $worksheet.Range("A3").Value2 'Total "quoted" & <done>' "escaped totals label A3"
        Assert-Equal $worksheet.Range("B3").Value2 42 "escaped totals value B3"

        Write-Host "OK: Excel opened table column-escape workbook read-only: $resolved"
        Write-Host "OK: EscapedColumnTable headers and visible totals row values verified"
    }
    finally {
        Close-Workbook $workbook
        foreach ($object in @($range, $table, $listObjects, $worksheet, $workbook)) {
            Release-ComObject $object
        }
    }
}

function Verify-OverlapWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
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
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
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
        Close-Workbook $workbook
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
                $workbook)) {
            Release-ComObject $object
        }
    }
}

$excel = $null

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false

    Verify-TablesWorkbook $excel $TablesPath
    Verify-StyleFlagsWorkbook $excel $StyleFlagsPath
    Verify-ColumnEscapeWorkbook $excel $ColumnEscapePath
    Verify-OverlapWorkbook $excel $OverlapPath
}
finally {
    if ($null -ne $excel) {
        $excel.Quit() | Out-Null
        Release-ComObject $excel
    }
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
}
