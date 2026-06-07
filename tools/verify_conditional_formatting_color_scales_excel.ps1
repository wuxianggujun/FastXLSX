param(
    [string]$Path = "build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-two-color-scale.xlsx",
    [string]$ThreeColorPath = "build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-three-color-scale.xlsx",
    [string]$MultiRangePath = "build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-multi-range.xlsx"
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

function Rgb-Long {
    param(
        [int]$Red,
        [int]$Green,
        [int]$Blue
    )

    return $Red + ($Green * 256) + ($Blue * 65536)
}

function Assert-ColorScale {
    param(
        [object]$FormatCondition,
        [string]$MessagePrefix
    )

    Assert-Equal $FormatCondition.Type 3 "$MessagePrefix FormatCondition type"
    Assert-Equal $FormatCondition.ColorScaleCriteria.Count 2 "$MessagePrefix criteria count"

    $low = $FormatCondition.ColorScaleCriteria.Item(1)
    $high = $FormatCondition.ColorScaleCriteria.Item(2)
    Assert-Equal $low.Type 1 "$MessagePrefix low criterion type"
    Assert-Equal $high.Type 2 "$MessagePrefix high criterion type"

    Assert-Equal $low.FormatColor.Color (Rgb-Long 255 0 0) "$MessagePrefix low color"
    Assert-Equal $high.FormatColor.Color (Rgb-Long 0 176 80) "$MessagePrefix high color"
}

function Assert-ThreeColorScale {
    param(
        [object]$FormatCondition,
        [string]$MessagePrefix
    )

    Assert-Equal $FormatCondition.Type 3 "$MessagePrefix FormatCondition type"
    Assert-Equal $FormatCondition.ColorScaleCriteria.Count 3 "$MessagePrefix criteria count"

    $low = $FormatCondition.ColorScaleCriteria.Item(1)
    $middle = $FormatCondition.ColorScaleCriteria.Item(2)
    $high = $FormatCondition.ColorScaleCriteria.Item(3)
    Assert-Equal $low.Type 1 "$MessagePrefix low criterion type"
    Assert-Equal $middle.Type 5 "$MessagePrefix middle criterion type"
    Assert-Equal $middle.Value 50 "$MessagePrefix middle criterion value"
    Assert-Equal $high.Type 2 "$MessagePrefix high criterion type"

    Assert-Equal $low.FormatColor.Color (Rgb-Long 248 105 107) "$MessagePrefix low color"
    Assert-Equal $middle.FormatColor.Color (Rgb-Long 255 235 132) "$MessagePrefix middle color"
    Assert-Equal $high.FormatColor.Color (Rgb-Long 99 190 123) "$MessagePrefix high color"
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
        $sheet = $workbook.Worksheets.Item("ColorScale")

        Assert-Equal $sheet.Range("A1").Value2 "Score" "ColorScale A1 value"
        Assert-Equal $sheet.Range("A2").Value2 1 "ColorScale A2 value"
        Assert-Equal $sheet.Range("A10").Value2 9 "ColorScale A10 value"
        Assert-Equal $sheet.Range("A2:A10").FormatConditions.Count 1 "ColorScale FormatConditions count"

        $formatCondition = $sheet.Range("A2:A10").FormatConditions.Item(1)
        Assert-ColorScale $formatCondition "ColorScale A2:A10"

        Write-Host "OK: Excel opened conditional formatting color-scale workbook read-only: $resolved"
        Write-Host "OK: ColorScale!A2:A10 has one two-color scale rule"
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

function Verify-ThreeColorWorkbook {
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
        $sheet = $workbook.Worksheets.Item("ThreeColorScale")

        Assert-Equal $sheet.Range("A1").Value2 "Score" "ThreeColorScale A1 value"
        Assert-Equal $sheet.Range("A2").Value2 1 "ThreeColorScale A2 value"
        Assert-Equal $sheet.Range("A10").Value2 9 "ThreeColorScale A10 value"
        Assert-Equal $sheet.Range("A2:A10").FormatConditions.Count 1 "ThreeColorScale FormatConditions count"

        $formatCondition = $sheet.Range("A2:A10").FormatConditions.Item(1)
        Assert-ThreeColorScale $formatCondition "ThreeColorScale A2:A10"

        Write-Host "OK: Excel opened conditional formatting three-color workbook read-only: $resolved"
        Write-Host "OK: ThreeColorScale!A2:A10 has one three-color scale rule"
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
        $sheet = $workbook.Worksheets.Item("ColorScaleRanges")

        Assert-Equal $sheet.Range("A2").FormatConditions.Count 1 "A2 color scale count"
        Assert-Equal $sheet.Range("C2").FormatConditions.Count 1 "C2 color scale count"
        Assert-Equal $sheet.Range("E2").FormatConditions.Count 1 "E2 color scale count"
        Assert-Equal $sheet.Range("B2").FormatConditions.Count 0 "B2 should not have color scale"

        $formatCondition = $sheet.Range("A2").FormatConditions.Item(1)
        Assert-ColorScale $formatCondition "ColorScaleRanges multi-range"

        $appliesTo = $formatCondition.AppliesTo
        Assert-Equal $appliesTo.Areas.Count 3 "ColorScaleRanges AppliesTo area count"
        Assert-Equal $appliesTo.Areas.Item(1).Address($false, $false) "A2:A3" "AppliesTo area 1"
        Assert-Equal $appliesTo.Areas.Item(2).Address($false, $false) "C2:C3" "AppliesTo area 2"
        Assert-Equal $appliesTo.Areas.Item(3).Address($false, $false) "E2:E3" "AppliesTo area 3"

        Write-Host "OK: Excel opened conditional formatting multi-range workbook read-only: $resolved"
        Write-Host "OK: ColorScaleRanges multi-area color scale is visible in Excel COM"
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
    Verify-ThreeColorWorkbook -Excel $excel -Path $ThreeColorPath
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
