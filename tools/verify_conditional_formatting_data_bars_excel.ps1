param(
    [string]$Path = "build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar.xlsx",
    [string]$MetadataOrderPath = "build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar-metadata-order.xlsx",
    [string]$MultiRangePath = "build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-data-bar-multi-range.xlsx"
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

function Rgb-Long {
    param(
        [int]$Red,
        [int]$Green,
        [int]$Blue
    )

    return $Red + ($Green * 256) + ($Blue * 65536)
}

function Assert-DataBar {
    param(
        [object]$FormatCondition,
        [bool]$ShowValue,
        [string]$MessagePrefix
    )

    Assert-Equal $FormatCondition.Type 4 "$MessagePrefix FormatCondition type"
    Assert-Equal $FormatCondition.MinPoint.Type 1 "$MessagePrefix min point type"
    Assert-Equal $FormatCondition.MaxPoint.Type 2 "$MessagePrefix max point type"
    Assert-Equal $FormatCondition.BarColor.Color (Rgb-Long 99 142 198) "$MessagePrefix bar color"
    Assert-Bool $FormatCondition.ShowValue $ShowValue "$MessagePrefix ShowValue"
}

function Verify-BasicWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $sheet = $null
    $formatCondition = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $sheet = $workbook.Worksheets.Item("DataBar")

        Assert-Equal $sheet.Range("A1").Value2 "Score" "DataBar A1 value"
        Assert-Equal $sheet.Range("A2").Value2 1 "DataBar A2 value"
        Assert-Equal $sheet.Range("A10").Value2 9 "DataBar A10 value"
        Assert-Equal $sheet.Range("A2:A10").FormatConditions.Count 1 "DataBar FormatConditions count"

        $formatCondition = $sheet.Range("A2:A10").FormatConditions.Item(1)
        Assert-DataBar $formatCondition $true "DataBar A2:A10"

        Write-Host "OK: Excel opened conditional formatting data-bar workbook read-only: $resolved"
        Write-Host "OK: DataBar!A2:A10 has one basic data bar rule"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        foreach ($object in @($formatCondition, $sheet, $workbook)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-MetadataOrderWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $sheet = $null
    $formatCondition = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $sheet = $workbook.Worksheets.Item("DataBarObjects")

        Assert-Equal $sheet.Range("A2:A10").FormatConditions.Count 1 "DataBarObjects FormatConditions count"

        $formatCondition = $sheet.Range("A2:A10").FormatConditions.Item(1)
        Assert-DataBar $formatCondition $false "DataBarObjects A2:A10"

        Write-Host "OK: Excel opened data-bar metadata-order workbook read-only: $resolved"
        Write-Host "OK: DataBarObjects!A2:A10 preserves showValue=false data bar metadata"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        foreach ($object in @($formatCondition, $sheet, $workbook)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-MultiRangeWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $sheet = $null
    $formatCondition = $null
    $appliesTo = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $sheet = $workbook.Worksheets.Item("DataBarRanges")

        Assert-Equal $sheet.Range("A2").FormatConditions.Count 1 "A2 data bar count"
        Assert-Equal $sheet.Range("C2").FormatConditions.Count 1 "C2 data bar count"
        Assert-Equal $sheet.Range("E2").FormatConditions.Count 1 "E2 data bar count"
        Assert-Equal $sheet.Range("B2").FormatConditions.Count 0 "B2 should not have data bar"

        $formatCondition = $sheet.Range("A2").FormatConditions.Item(1)
        Assert-DataBar $formatCondition $true "DataBarRanges multi-range"

        $appliesTo = $formatCondition.AppliesTo
        Assert-Equal $appliesTo.Areas.Count 3 "DataBarRanges AppliesTo area count"
        Assert-Equal $appliesTo.Areas.Item(1).Address($false, $false) "A2:A3" "AppliesTo area 1"
        Assert-Equal $appliesTo.Areas.Item(2).Address($false, $false) "C2:C3" "AppliesTo area 2"
        Assert-Equal $appliesTo.Areas.Item(3).Address($false, $false) "E2:E3" "AppliesTo area 3"

        Write-Host "OK: Excel opened conditional formatting data-bar multi-range workbook read-only: $resolved"
        Write-Host "OK: DataBarRanges multi-area data bar is visible in Excel COM"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        foreach ($object in @($appliesTo, $formatCondition, $sheet, $workbook)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

$excel = $null

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false

    Verify-BasicWorkbook -Excel $excel -Path $Path
    Verify-MetadataOrderWorkbook -Excel $excel -Path $MetadataOrderPath
    Verify-MultiRangeWorkbook -Excel $excel -Path $MultiRangePath
}
finally {
    if ($null -ne $excel) {
        $excel.Quit() | Out-Null
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($excel)
    }
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
}
