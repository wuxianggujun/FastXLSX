param(
    [string]$ExternalPath = "build\windows-nmake-release\tests\fastxlsx-streaming-external-hyperlinks.xlsx",
    [string]$InternalPath = "build\windows-nmake-release\tests\fastxlsx-streaming-internal-hyperlinks.xlsx",
    [string]$DisplayTooltipPath = "build\windows-nmake-release\tests\fastxlsx-streaming-hyperlink-display-tooltips.xlsx"
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

function Assert-Hyperlink {
    param(
        [object]$Worksheet,
        [string]$Cell,
        [hashtable]$Expected
    )

    $range = $Worksheet.Range($Cell)
    Assert-Equal $range.Hyperlinks.Count 1 "$($Worksheet.Name)!$Cell hyperlink count"
    $hyperlink = $range.Hyperlinks.Item(1)

    foreach ($key in $Expected.Keys) {
        Assert-Equal $hyperlink.$key $Expected[$key] "$($Worksheet.Name)!$Cell $key"
    }
}

function Close-Workbook {
    param([AllowNull()][object]$Workbook)

    if ($null -ne $Workbook) {
        $Workbook.Close($false) | Out-Null
    }
}

$externalResolved = (Resolve-Path -LiteralPath $ExternalPath).Path
$internalResolved = (Resolve-Path -LiteralPath $InternalPath).Path
$displayTooltipResolved = (Resolve-Path -LiteralPath $DisplayTooltipPath).Path

$excel = $null
$workbook = $null
$worksheet = $null

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false

    $workbook = $excel.Workbooks.Open($externalResolved, 0, $true)
    $worksheet = $workbook.Worksheets.Item("Links")
    Assert-Equal $worksheet.Hyperlinks.Count 2 "Links hyperlink count"
    Assert-Hyperlink $worksheet "A1" @{
        Address = "https://openai.com/"
        SubAddress = ""
        TextToDisplay = "OpenAI"
        ScreenTip = ""
    }
    Assert-Hyperlink $worksheet "B2" @{
        Address = "https://example.com/path?a=1&b=2"
        SubAddress = ""
        TextToDisplay = "Docs & <API>"
        ScreenTip = ""
    }
    $worksheet = $workbook.Worksheets.Item("MoreLinks")
    Assert-Equal $worksheet.Hyperlinks.Count 1 "MoreLinks hyperlink count"
    Assert-Hyperlink $worksheet "A1" @{
        Address = "mailto:test@example.com"
        SubAddress = ""
        TextToDisplay = "Second sheet"
        ScreenTip = ""
    }
    $worksheet = $workbook.Worksheets.Item("Plain")
    Assert-Equal $worksheet.Hyperlinks.Count 0 "Plain external hyperlink count"
    Write-Host "OK: Excel opened external hyperlink workbook read-only: $externalResolved"
    Write-Host "OK: External hyperlink Address/TextToDisplay values verified"
    Close-Workbook $workbook
    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($workbook)
    $workbook = $null
    $worksheet = $null

    $workbook = $excel.Workbooks.Open($internalResolved, 0, $true)
    $worksheet = $workbook.Worksheets.Item("Internal")
    Assert-Equal $worksheet.Hyperlinks.Count 2 "Internal hyperlink count"
    Assert-Hyperlink $worksheet "A1" @{
        Address = ""
        SubAddress = "'Target & <Sheet>'!A1"
        TextToDisplay = "Jump to target"
        ScreenTip = ""
    }
    Assert-Hyperlink $worksheet "A2" @{
        Address = ""
        SubAddress = "'Target & <Sheet>'!B2:""quoted"""
        TextToDisplay = "Second jump"
        ScreenTip = ""
    }
    $worksheet = $workbook.Worksheets.Item("Mixed")
    Assert-Equal $worksheet.Hyperlinks.Count 3 "Mixed hyperlink count"
    Assert-Hyperlink $worksheet "A1" @{
        Address = "https://example.com/"
        SubAddress = ""
        TextToDisplay = "External"
        ScreenTip = ""
    }
    Assert-Hyperlink $worksheet "B1" @{
        Address = ""
        SubAddress = "'Target & <Sheet>'!A1"
        TextToDisplay = "Internal"
        ScreenTip = ""
    }
    Assert-Hyperlink $worksheet "A2" @{
        Address = "https://example.com/more"
        SubAddress = ""
        TextToDisplay = "External 2"
        ScreenTip = ""
    }
    $worksheet = $workbook.Worksheets.Item("Plain")
    Assert-Equal $worksheet.Hyperlinks.Count 0 "Plain internal hyperlink count"
    Write-Host "OK: Excel opened internal hyperlink workbook read-only: $internalResolved"
    Write-Host "OK: Internal hyperlink SubAddress and mixed Address values verified"
    Close-Workbook $workbook
    [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($workbook)
    $workbook = $null
    $worksheet = $null

    $workbook = $excel.Workbooks.Open($displayTooltipResolved, 0, $true)
    $worksheet = $workbook.Worksheets.Item("ExternalAttrs")
    Assert-Equal $worksheet.Hyperlinks.Count 3 "ExternalAttrs hyperlink count"
    Assert-Hyperlink $worksheet "A1" @{
        Address = "https://example.com/both"
        SubAddress = ""
        TextToDisplay = "External both"
        ScreenTip = "External tip & <more> ""Q"" 'A'"
    }
    Assert-Hyperlink $worksheet "B1" @{
        Address = "https://example.com/display"
        SubAddress = ""
        TextToDisplay = "External display"
        ScreenTip = ""
    }
    Assert-Hyperlink $worksheet "C1" @{
        Address = "https://example.com/tooltip"
        SubAddress = ""
        TextToDisplay = "External tooltip"
        ScreenTip = "Tooltip only"
    }
    $worksheet = $workbook.Worksheets.Item("InternalAttrs")
    Assert-Equal $worksheet.Hyperlinks.Count 4 "InternalAttrs hyperlink count"
    Assert-Hyperlink $worksheet "A1" @{
        Address = ""
        SubAddress = "Target!A1"
        TextToDisplay = "Internal both"
        ScreenTip = "Internal tip & <more> ""Q"" 'A'"
    }
    Assert-Hyperlink $worksheet "B1" @{
        Address = ""
        SubAddress = "Target!A2"
        TextToDisplay = "Internal display"
        ScreenTip = ""
    }
    Assert-Hyperlink $worksheet "C1" @{
        Address = ""
        SubAddress = "Target!A3"
        TextToDisplay = "Internal tooltip"
        ScreenTip = "Internal tooltip only"
    }
    Assert-Hyperlink $worksheet "D1" @{
        Address = ""
        SubAddress = "Target!D4"
        TextToDisplay = "Internal empty options"
        ScreenTip = ""
    }
    Write-Host "OK: Excel opened hyperlink display/tooltip workbook read-only: $displayTooltipResolved"
    Write-Host "OK: Hyperlink ScreenTip, Address, SubAddress, and cell text values verified"
}
finally {
    Close-Workbook $workbook
    if ($null -ne $worksheet) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($worksheet)
    }
    if ($null -ne $workbook) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($workbook)
    }
    if ($null -ne $excel) {
        $excel.Quit() | Out-Null
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($excel)
    }
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
}
