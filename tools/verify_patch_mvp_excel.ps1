param(
    [string]$WriterRoundtripPath = "build\windows-nmake-release\tests\fastxlsx-package-editor-writer-roundtrip-output.xlsx",
    [string]$TemplateFillPath = "build\windows-nmake-release\tests\fastxlsx-package-editor-template-fill-output.xlsx"
)

$ErrorActionPreference = "Stop"

function Assert-CellValue {
    param(
        [object]$Worksheet,
        [string]$Address,
        [AllowNull()]
        [object]$Expected,
        [switch]$AllowEmpty
    )

    $value = $Worksheet.Range($Address).Value2
    if ($AllowEmpty) {
        if (($null -ne $value) -and ([string]$value -ne "")) {
            throw "$($Worksheet.Name)!$Address expected an empty visible value, got '$value'"
        }
        return
    }

    if ([string]$value -ne [string]$Expected) {
        throw "$($Worksheet.Name)!$Address expected '$Expected', got '$value'"
    }
}

function Verify-WriterRoundtrip {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $editable = $null
    $untouched = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $editable = $workbook.Worksheets.Item("Patch Source")
        $untouched = $workbook.Worksheets.Item("Untouched")

        Assert-CellValue $editable "A1" "old text"
        Assert-CellValue $editable "B1" $null -AllowEmpty
        Assert-CellValue $editable "A2" $null -AllowEmpty
        Assert-CellValue $untouched "A1" "keep me"
        Assert-CellValue $untouched "B1" 99

        Write-Host "OK: Excel opened Patch MVP writer-roundtrip workbook read-only: $resolved"
        Write-Host "OK: Patch Source replacement and Untouched preservation smoke values verified"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        if ($null -ne $untouched) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($untouched)
        }
        if ($null -ne $editable) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($editable)
        }
        if ($null -ne $workbook) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($workbook)
        }
    }
}

function Verify-TemplateFill {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $template = $null
    $untouched = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $template = $workbook.Worksheets.Item("Template Fill")
        $untouched = $workbook.Worksheets.Item("Untouched")

        Assert-CellValue $template "A1" "Acme Corp"
        Assert-CellValue $template "B1" 1234
        Assert-CellValue $template "A2" $null -AllowEmpty
        Assert-CellValue $untouched "A1" "keep me"

        Write-Host "OK: Excel opened Patch MVP template-fill workbook read-only: $resolved"
        Write-Host "OK: Template Fill replacement and Untouched preservation smoke values verified"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        if ($null -ne $untouched) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($untouched)
        }
        if ($null -ne $template) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($template)
        }
        if ($null -ne $workbook) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($workbook)
        }
    }
}

$excel = $null

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false

    Verify-WriterRoundtrip -Excel $excel -Path $WriterRoundtripPath
    Verify-TemplateFill -Excel $excel -Path $TemplateFillPath
}
finally {
    if ($null -ne $excel) {
        $excel.Quit() | Out-Null
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($excel)
    }
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
}
