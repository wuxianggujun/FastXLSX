param(
    [string]$Path = "build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-prompts.xlsx",
    [string]$MultiRangePath = "build\windows-nmake-release\tests\fastxlsx-streaming-data-validation-multi-range.xlsx"
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

function Assert-Validation {
    param(
        [object]$Worksheet,
        [string]$Address,
        [hashtable]$Expected
    )

    $validation = $Worksheet.Range($Address).Validation

    foreach ($key in $Expected.Keys) {
        switch ($key) {
            "ShowInput" { Assert-Bool $validation.ShowInput $Expected[$key] "$Address ShowInput" }
            "ShowError" { Assert-Bool $validation.ShowError $Expected[$key] "$Address ShowError" }
            default { Assert-Equal $validation.$key $Expected[$key] "$Address $key" }
        }
    }
}

function Assert-ValidationAreas {
    param(
        [object]$Worksheet,
        [string[]]$ExpectedAreas
    )

    $validationRange = $Worksheet.Cells.SpecialCells(-4174)
    $actualAreas = @()
    for ($i = 1; $i -le $validationRange.Areas.Count; $i++) {
        $actualAreas += $validationRange.Areas.Item($i).Address($false, $false)
    }

    Assert-Equal $actualAreas.Count $ExpectedAreas.Count "Validation area count"
    for ($i = 0; $i -lt $ExpectedAreas.Count; $i++) {
        Assert-Equal $actualAreas[$i] $ExpectedAreas[$i] "Validation area $($i + 1)"
    }
}

$resolved = (Resolve-Path -LiteralPath $Path).Path
$multiRangeResolved = (Resolve-Path -LiteralPath $MultiRangePath).Path
$excel = $null
$workbook = $null
$worksheet = $null
$multiRangeWorkbook = $null
$multiRangeWorksheet = $null

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false

    $workbook = $excel.Workbooks.Open($resolved, 0, $true)
    $worksheet = $workbook.Worksheets.Item("ValidationPrompt")

    Assert-Validation $worksheet "A2" @{
        Type = 1
        Operator = 1
        Formula1 = "1"
        Formula2 = "10"
        IgnoreBlank = $true
        ShowInput = $true
        InputTitle = "Input <Title> & ""Quote"""
        InputMessage = "Enter 'whole' & <value>"
        ShowError = $true
        ErrorTitle = "Error ""Title"" & <bad>"
        ErrorMessage = "Bad 'value' & <cell>"
        AlertStyle = 2
    }

    Assert-Validation $worksheet "B2" @{
        Type = 3
        ShowInput = $true
        InputTitle = "Choice"
        InputMessage = "Pick A, B, or C"
    }

    Assert-Validation $worksheet "C2" @{
        Type = 2
        ShowError = $true
        ErrorTitle = "Decimal"
        ErrorMessage = "Use a positive decimal"
        AlertStyle = 3
    }

    Assert-Validation $worksheet "D2" @{
        Type = 7
        Formula1 = "=LEN(D2)>0"
        AlertStyle = 1
    }

    Write-Host "OK: Excel opened data-validation prompt/error workbook read-only: $resolved"
    Write-Host "OK: Validation prompt/error properties verified for A2:D2"

    $workbook.Close($false) | Out-Null
    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($worksheet)
    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($workbook)
    $worksheet = $null
    $workbook = $null

    $multiRangeWorkbook = $excel.Workbooks.Open($multiRangeResolved, 0, $true)
    $multiRangeWorksheet = $multiRangeWorkbook.Worksheets.Item("ValidationRanges")

    $expectedMultiRange = @{
        Type = 1
        Operator = 1
        Formula1 = "1"
        Formula2 = "10"
        IgnoreBlank = $true
    }

    Assert-Validation $multiRangeWorksheet "A2" $expectedMultiRange
    Assert-Validation $multiRangeWorksheet "A10" $expectedMultiRange
    Assert-Validation $multiRangeWorksheet "C2" $expectedMultiRange
    Assert-Validation $multiRangeWorksheet "C10" $expectedMultiRange
    Assert-Validation $multiRangeWorksheet "E2" $expectedMultiRange
    Assert-Validation $multiRangeWorksheet "E10" $expectedMultiRange
    Assert-ValidationAreas $multiRangeWorksheet @("A2:A10", "C2:C10", "E2:E10")

    Write-Host "OK: Excel opened data-validation multi-range workbook read-only: $multiRangeResolved"
    Write-Host "OK: Multi-range validation properties verified for A/C/E validation areas"
}
finally {
    if ($null -ne $multiRangeWorkbook) {
        $multiRangeWorkbook.Close($false) | Out-Null
    }
    if ($null -ne $workbook) {
        $workbook.Close($false) | Out-Null
    }
    if ($null -ne $excel) {
        $excel.Quit() | Out-Null
    }
    if ($null -ne $worksheet) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($worksheet)
    }
    if ($null -ne $multiRangeWorksheet) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($multiRangeWorksheet)
    }
    if ($null -ne $workbook) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($workbook)
    }
    if ($null -ne $multiRangeWorkbook) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($multiRangeWorkbook)
    }
    if ($null -ne $excel) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($excel)
    }
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
}
