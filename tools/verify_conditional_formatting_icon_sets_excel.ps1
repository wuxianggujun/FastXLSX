param(
    [string]$Path = "build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set.xlsx",
    [string]$MetadataOrderPath = "build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-metadata-order.xlsx",
    [string]$PercentilePath = "build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-percentile.xlsx",
    [string]$MultiRangePath = "build\windows-nmake-release\tests\fastxlsx-streaming-conditional-formatting-icon-set-multi-range.xlsx"
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

function Release-ComObjects {
    param([object[]]$Objects)

    foreach ($object in $Objects) {
        if ($null -ne $object) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
        }
    }
}

function Assert-IconSet {
    param(
        [object]$FormatCondition,
        [bool]$ShowIconOnly,
        [bool]$ReverseOrder,
        [string]$MessagePrefix,
        [int[]]$ExpectedCriterionTypes = @(3, 3, 3),
        [double[]]$ExpectedCriterionValues = @(0, 33, 67)
    )

    Assert-Equal $FormatCondition.Type 6 "$MessagePrefix FormatCondition type"
    Assert-Equal $FormatCondition.IconSet.ID 1 "$MessagePrefix icon set id"
    Assert-Bool $FormatCondition.ShowIconOnly $ShowIconOnly "$MessagePrefix ShowIconOnly"
    Assert-Bool $FormatCondition.ReverseOrder $ReverseOrder "$MessagePrefix ReverseOrder"
    Assert-Equal $FormatCondition.IconCriteria.Count 3 "$MessagePrefix icon criteria count"
    Assert-Equal $ExpectedCriterionTypes.Count 3 "$MessagePrefix expected criterion type count"
    Assert-Equal $ExpectedCriterionValues.Count 3 "$MessagePrefix expected criterion value count"

    $criteria = @()
    try {
        for ($i = 1; $i -le 3; $i++) {
            $criteria += $FormatCondition.IconCriteria.Item($i)
        }

        for ($i = 0; $i -lt 3; $i++) {
            $displayIndex = $i + 1
            Assert-Equal $criteria[$i].Type $ExpectedCriterionTypes[$i] "$MessagePrefix criterion $displayIndex type"
            Assert-Equal $criteria[$i].Value $ExpectedCriterionValues[$i] "$MessagePrefix criterion $displayIndex value"
        }
    }
    finally {
        Release-ComObjects -Objects $criteria
    }
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
        $sheet = $workbook.Worksheets.Item("IconSet")

        Assert-Equal $sheet.Range("A1").Value2 "Score" "IconSet A1 value"
        Assert-Equal $sheet.Range("A2").Value2 1 "IconSet A2 value"
        Assert-Equal $sheet.Range("A10").Value2 9 "IconSet A10 value"
        Assert-Equal $sheet.Range("A2:A10").FormatConditions.Count 1 "IconSet FormatConditions count"

        $formatCondition = $sheet.Range("A2:A10").FormatConditions.Item(1)
        Assert-IconSet $formatCondition $false $false "IconSet A2:A10"

        Write-Host "OK: Excel opened conditional formatting icon-set workbook read-only: $resolved"
        Write-Host "OK: IconSet!A2:A10 has one 3Arrows icon set rule"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        Release-ComObjects -Objects @($formatCondition, $sheet, $workbook)
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
        $sheet = $workbook.Worksheets.Item("IconSetObjects")
        Assert-Equal $sheet.Range("A2:A10").FormatConditions.Count 1 "IconSetObjects FormatConditions count"

        $formatCondition = $sheet.Range("A2:A10").FormatConditions.Item(1)
        Assert-IconSet $formatCondition $true $true "IconSetObjects A2:A10"

        Write-Host "OK: Excel opened icon-set metadata-order workbook read-only: $resolved"
        Write-Host "OK: IconSetObjects!A2:A10 preserves showValue/reverse icon metadata"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        Release-ComObjects -Objects @($formatCondition, $sheet, $workbook)
    }
}

function Verify-PercentileWorkbook {
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
        $sheet = $workbook.Worksheets.Item("IconSetPercentile")

        Assert-Equal $sheet.Range("A1").Value2 "Score" "IconSetPercentile A1 value"
        Assert-Equal $sheet.Range("A2").Value2 1 "IconSetPercentile A2 value"
        Assert-Equal $sheet.Range("A10").Value2 9 "IconSetPercentile A10 value"
        Assert-Equal $sheet.Range("A2:A10").FormatConditions.Count 1 "IconSetPercentile FormatConditions count"

        $formatCondition = $sheet.Range("A2:A10").FormatConditions.Item(1)
        Assert-IconSet $formatCondition $true $true "IconSetPercentile A2:A10" @(5, 5, 5) @(10, 50, 90)

        Write-Host "OK: Excel opened conditional formatting percentile icon-set workbook read-only: $resolved"
        Write-Host "OK: IconSetPercentile!A2:A10 preserves percentile thresholds and icon-only/reverse metadata"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        Release-ComObjects -Objects @($formatCondition, $sheet, $workbook)
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
        $sheet = $workbook.Worksheets.Item("IconSetRanges")

        Assert-Equal $sheet.Range("A2").FormatConditions.Count 1 "A2 icon set count"
        Assert-Equal $sheet.Range("C2").FormatConditions.Count 1 "C2 icon set count"
        Assert-Equal $sheet.Range("E2").FormatConditions.Count 1 "E2 icon set count"
        Assert-Equal $sheet.Range("B2").FormatConditions.Count 0 "B2 should not have icon set"

        $formatCondition = $sheet.Range("A2").FormatConditions.Item(1)
        Assert-IconSet $formatCondition $false $false "IconSetRanges multi-range"

        $appliesTo = $formatCondition.AppliesTo
        Assert-Equal $appliesTo.Areas.Count 3 "IconSetRanges AppliesTo area count"
        Assert-Equal $appliesTo.Areas.Item(1).Address($false, $false) "A2:A3" "AppliesTo area 1"
        Assert-Equal $appliesTo.Areas.Item(2).Address($false, $false) "C2:C3" "AppliesTo area 2"
        Assert-Equal $appliesTo.Areas.Item(3).Address($false, $false) "E2:E3" "AppliesTo area 3"

        Write-Host "OK: Excel opened conditional formatting icon-set multi-range workbook read-only: $resolved"
        Write-Host "OK: IconSetRanges multi-area icon set is visible in Excel COM"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        Release-ComObjects -Objects @($appliesTo, $formatCondition, $sheet, $workbook)
    }
}

$excel = $null

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false

    Verify-BasicWorkbook -Excel $excel -Path $Path
    Verify-MetadataOrderWorkbook -Excel $excel -Path $MetadataOrderPath
    Verify-PercentileWorkbook -Excel $excel -Path $PercentilePath
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
