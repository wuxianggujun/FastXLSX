param(
    [string]$Path = "build\windows-nmake-release\tests\fastxlsx-streaming-styles-number-formats.xlsx",
    [string]$SharedPath = "build\windows-nmake-release\tests\fastxlsx-streaming-styles-shared-strings.xlsx",
    [string]$AlignmentPath = "build\windows-nmake-release\tests\fastxlsx-streaming-styles-alignment.xlsx",
    [string]$FontPath = "build\windows-nmake-release\tests\fastxlsx-streaming-styles-fonts.xlsx",
    [string]$FillPath = "build\windows-nmake-release\tests\fastxlsx-streaming-styles-fills.xlsx"
)

$ErrorActionPreference = "Stop"

function Assert-Equal {
    param(
        [object]$Actual,
        [object]$Expected,
        [string]$Message
    )

    if ($Actual -ne $Expected) {
        throw "$Message expected '$Expected' but got '$Actual'"
    }
}

function New-OleRgb {
    param(
        [int]$Red,
        [int]$Green,
        [int]$Blue
    )

    return $Red + ($Green * 256) + ($Blue * 65536)
}

function Verify-StylesWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $sheet = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $sheet = $workbook.Worksheets.Item("Styles")

        Assert-Equal $sheet.Range("A2").Value2 1234.5 "A2 value"
        Assert-Equal $sheet.Range("B2").Value2 7.25 "B2 value"
        Assert-Equal $sheet.Range("D2").Value2 "styled text" "D2 value"
        Assert-Equal $sheet.Range("E2").Value2 $true "E2 value"
        Assert-Equal $sheet.Range("F2").Formula "=A2*2" "F2 formula"

        Assert-Equal $sheet.Range("A2").NumberFormat '$#,##0.00' "A2 number format"
        Assert-Equal $sheet.Range("B2").NumberFormat '0.00 "kg & <unit>"' "B2 number format"
        Assert-Equal $sheet.Range("D2").NumberFormat '0.00 "kg & <unit>"' "D2 number format"
        Assert-Equal $sheet.Range("F2").NumberFormat '$#,##0.00' "F2 number format"

        Write-Host "OK: Excel opened styles workbook read-only: $resolved"
        Write-Host "OK: NumberFormat, values, and formula metadata verified"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        foreach ($object in @($sheet, $workbook)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-SharedStylesWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $sheet = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $sheet = $workbook.Worksheets.Item("StyledShared")

        Assert-Equal $sheet.Range("A1").Value2 "styled shared" "StyledShared A1 value"
        Assert-Equal $sheet.Range("B1").Value2 "plain shared" "StyledShared B1 value"
        Assert-Equal $sheet.Range("A1").NumberFormat '@' "StyledShared A1 number format"

        Write-Host "OK: Excel opened sharedStrings + styles workbook read-only: $resolved"
        Write-Host "OK: Shared string values and text NumberFormat verified"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        foreach ($object in @($sheet, $workbook)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-AlignmentStylesWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $sheet = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $sheet = $workbook.Worksheets.Item("Alignment")

        Assert-Equal $sheet.Range("A2").Value2 "line 1`nline 2" "Alignment A2 value"
        Assert-Equal $sheet.Range("B2").Value2 12.5 "Alignment B2 value"
        Assert-Equal $sheet.Range("C2").Value2 42.5 "Alignment C2 value"
        Assert-Equal $sheet.Range("D2").Value2 "plain" "Alignment D2 value"

        Assert-Equal $sheet.Range("A2").WrapText $true "Alignment A2 wrap text"
        Assert-Equal $sheet.Range("B2").NumberFormat '0.0' "Alignment B2 number format"
        Assert-Equal $sheet.Range("C2").NumberFormat '0.0' "Alignment C2 number format"
        Assert-Equal $sheet.Range("C2").WrapText $true "Alignment C2 wrap text"

        Write-Host "OK: Excel opened wrap-text alignment styles workbook read-only: $resolved"
        Write-Host "OK: WrapText and NumberFormat metadata verified"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        foreach ($object in @($sheet, $workbook)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-FontStylesWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $sheet = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $sheet = $workbook.Worksheets.Item("Fonts")

        Assert-Equal $sheet.Range("A2").Value2 "bold" "Fonts A2 value"
        Assert-Equal $sheet.Range("B2").Value2 "italic" "Fonts B2 value"
        Assert-Equal $sheet.Range("C2").Value2 $true "Fonts C2 value"
        Assert-Equal $sheet.Range("D2").Value2 12.5 "Fonts D2 value"
        Assert-Equal $sheet.Range("E2").Value2 "plain" "Fonts E2 value"

        Assert-Equal $sheet.Range("A2").Font.Bold $true "Fonts A2 bold"
        Assert-Equal $sheet.Range("A2").Font.Italic $false "Fonts A2 italic"
        Assert-Equal $sheet.Range("B2").Font.Bold $false "Fonts B2 bold"
        Assert-Equal $sheet.Range("B2").Font.Italic $true "Fonts B2 italic"
        Assert-Equal $sheet.Range("C2").Font.Bold $true "Fonts C2 bold"
        Assert-Equal $sheet.Range("C2").Font.Italic $true "Fonts C2 italic"
        Assert-Equal $sheet.Range("D2").Font.Bold $true "Fonts D2 bold"
        Assert-Equal $sheet.Range("D2").NumberFormat '0.0' "Fonts D2 number format"
        Assert-Equal $sheet.Range("E2").Font.Bold $false "Fonts E2 bold"
        Assert-Equal $sheet.Range("E2").Font.Italic $false "Fonts E2 italic"

        Write-Host "OK: Excel opened bold/italic font styles workbook read-only: $resolved"
        Write-Host "OK: Font.Bold, Font.Italic, and NumberFormat metadata verified"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        foreach ($object in @($sheet, $workbook)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-FillStylesWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $sheet = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $sheet = $workbook.Worksheets.Item("Fills")

        Assert-Equal $sheet.Range("A2").Value2 "yellow" "Fills A2 value"
        Assert-Equal $sheet.Range("B2").Value2 "blue" "Fills B2 value"
        Assert-Equal $sheet.Range("C2").Value2 12.5 "Fills C2 value"
        Assert-Equal $sheet.Range("D2").Value2 "bold yellow" "Fills D2 value"
        Assert-Equal $sheet.Range("E2").Value2 "plain" "Fills E2 value"

        $yellow = New-OleRgb -Red 255 -Green 235 -Blue 132
        $blue = New-OleRgb -Red 90 -Green 138 -Blue 214

        Assert-Equal $sheet.Range("A2").Interior.Pattern 1 "Fills A2 pattern"
        Assert-Equal $sheet.Range("A2").Interior.Color $yellow "Fills A2 color"
        Assert-Equal $sheet.Range("B2").Interior.Pattern 1 "Fills B2 pattern"
        Assert-Equal $sheet.Range("B2").Interior.Color $blue "Fills B2 color"
        Assert-Equal $sheet.Range("C2").Interior.Color $yellow "Fills C2 color"
        Assert-Equal $sheet.Range("C2").NumberFormat '0.0' "Fills C2 number format"
        Assert-Equal $sheet.Range("D2").Interior.Color $yellow "Fills D2 color"
        Assert-Equal $sheet.Range("D2").Font.Bold $true "Fills D2 bold"
        Assert-Equal $sheet.Range("E2").Interior.Pattern -4142 "Fills E2 default pattern"

        Write-Host "OK: Excel opened solid fill styles workbook read-only: $resolved"
        Write-Host "OK: Interior.Pattern, Interior.Color, NumberFormat, and Font.Bold verified"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        foreach ($object in @($sheet, $workbook)) {
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

    Verify-StylesWorkbook -Excel $excel -Path $Path
    Verify-SharedStylesWorkbook -Excel $excel -Path $SharedPath
    Verify-AlignmentStylesWorkbook -Excel $excel -Path $AlignmentPath
    Verify-FontStylesWorkbook -Excel $excel -Path $FontPath
    Verify-FillStylesWorkbook -Excel $excel -Path $FillPath
}
finally {
    if ($null -ne $excel) {
        $excel.Quit()
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($excel)
    }
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
}
