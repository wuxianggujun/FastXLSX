param(
    [string]$Path = "build\windows-nmake-release\tests\fastxlsx-streaming-styles-number-formats.xlsx",
    [string]$SharedPath = "build\windows-nmake-release\tests\fastxlsx-streaming-styles-shared-strings.xlsx"
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

$excel = $null

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false

    Verify-StylesWorkbook -Excel $excel -Path $Path
    Verify-SharedStylesWorkbook -Excel $excel -Path $SharedPath
}
finally {
    if ($null -ne $excel) {
        $excel.Quit()
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($excel)
    }
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
}
