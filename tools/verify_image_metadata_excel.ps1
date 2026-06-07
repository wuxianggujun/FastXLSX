param(
    [string]$Path = "build\windows-nmake-release\tests\fastxlsx-streaming-image-metadata.xlsx"
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

function Get-ShapeByName {
    param(
        [object]$Worksheet,
        [string]$Name
    )

    foreach ($shape in @($Worksheet.Shapes)) {
        if ([string]$shape.Name -eq $Name) {
            return $shape
        }
    }
    return $null
}

function Get-ShapeByAlternativeText {
    param(
        [object]$Worksheet,
        [string]$AlternativeText
    )

    foreach ($shape in @($Worksheet.Shapes)) {
        if ([string]$shape.AlternativeText -eq $AlternativeText) {
            return $shape
        }
    }
    return $null
}

$resolved = (Resolve-Path -LiteralPath $Path).Path
$excel = $null
$workbook = $null
$worksheet = $null

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false

    $workbook = $excel.Workbooks.Open($resolved, 0, $true)
    $worksheet = $workbook.Worksheets.Item("ImageMetadata")

    Assert-Equal $worksheet.Shapes.Count 3 "ImageMetadata shape count"

    $namedOnly = Get-ShapeByName $worksheet "NamedOnly"
    Assert-True ($null -ne $namedOnly) "Excel did not expose the custom image name 'NamedOnly'"

    $defaultNamed = Get-ShapeByName $worksheet "Picture 3"
    Assert-True ($null -ne $defaultNamed) "Excel did not expose the default generated image name 'Picture 3'"

    $expectedDescription = "Alt ""quoted"" & <tag> 'owner'"
    $describedShape = Get-ShapeByAlternativeText $worksheet $expectedDescription
    Assert-True ($null -ne $describedShape) "Excel did not expose the custom image description as AlternativeText"

    Assert-Equal $describedShape.Placement 2 "oneCell editAs Excel placement"
    Assert-Equal $namedOnly.Placement 3 "absolute editAs Excel placement"
    Assert-Equal $defaultNamed.Placement 1 "default twoCell editAs Excel placement"

    $descriptionMatched = $false
    $shapeSummaries = @()
    foreach ($shape in @($worksheet.Shapes)) {
        $shapeSummaries += "$($shape.Name) alt='$($shape.AlternativeText)' placement='$($shape.Placement)' topLeft='$($shape.TopLeftCell.Address($false, $false))'"
        if ([string]$shape.AlternativeText -eq $expectedDescription) {
            $descriptionMatched = $true
        }
    }
    Assert-True $descriptionMatched "Excel did not expose the custom image description as AlternativeText"

    Write-Host "OK: Excel opened image metadata workbook read-only: $resolved"
    Write-Host "OK: Shape count, custom name, default name, AlternativeText, and editAs placement verified"
    foreach ($summary in $shapeSummaries) {
        Write-Host "OK: Shape $summary"
    }
}
finally {
    if ($null -ne $workbook) {
        $workbook.Close($false) | Out-Null
    }
    if ($null -ne $excel) {
        $excel.Quit() | Out-Null
    }
    if ($null -ne $worksheet) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($worksheet)
    }
    if ($null -ne $workbook) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($workbook)
    }
    if ($null -ne $excel) {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($excel)
    }
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
}
