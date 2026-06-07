param(
    [string]$Path = "build\windows-nmake-release\tests\fastxlsx-streaming-image-metadata.xlsx",
    [string]$BasicPath = "build\windows-nmake-release\tests\fastxlsx-streaming-images.xlsx",
    [string]$MixedObjectPath = "build\windows-nmake-release\tests\fastxlsx-streaming-mixed-object-rels.xlsx"
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

function Assert-Near {
    param(
        [double]$Actual,
        [double]$Expected,
        [double]$Tolerance,
        [string]$Message
    )

    if ([Math]::Abs($Actual - $Expected) -gt $Tolerance) {
        throw "$Message expected $Expected +/- $Tolerance, got $Actual"
    }
}

function Convert-EmuToPoint {
    param(
        [double]$Emu
    )

    return $Emu / 12700.0
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

function Verify-BasicImageWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $images = $null
    $second = $null
    $plain = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $images = $workbook.Worksheets.Item("Images")
        $second = $workbook.Worksheets.Item("SecondImage")
        $plain = $workbook.Worksheets.Item("Plain")

        Assert-Equal $images.Shapes.Count 1 "Images shape count"
        Assert-Equal $second.Shapes.Count 1 "SecondImage shape count"
        Assert-Equal $plain.Shapes.Count 0 "Plain shape count"
        Assert-Equal $images.Hyperlinks.Count 1 "Images hyperlink count"
        Assert-Equal $images.ListObjects.Count 1 "Images table count"

        $firstShape = $images.Shapes.Item(1)
        $secondShape = $second.Shapes.Item(1)
        Assert-Equal $firstShape.TopLeftCell.Address($false, $false) "C1" "Images shape top-left"
        Assert-Equal $firstShape.BottomRightCell.Address($false, $false) "F5" "Images shape bottom-right"
        Assert-Equal $secondShape.TopLeftCell.Address($false, $false) "A1" "SecondImage shape top-left"
        Assert-Equal $secondShape.BottomRightCell.Address($false, $false) "B2" "SecondImage shape bottom-right"

        Write-Host "OK: Excel opened basic image workbook read-only: $resolved"
        Write-Host "OK: Images/SecondImage/Plain shape counts, hyperlink count, table count, and anchors verified"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        foreach ($object in @($plain, $second, $images, $workbook)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-MixedObjectWorkbook {
    param(
        [object]$Excel,
        [string]$Path
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $objects = $null
    $moreObjects = $null
    $plain = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $objects = $workbook.Worksheets.Item("Objects")
        $moreObjects = $workbook.Worksheets.Item("MoreObjects")
        $plain = $workbook.Worksheets.Item("Plain")

        Assert-Equal $objects.Hyperlinks.Count 2 "Objects hyperlink count"
        Assert-Equal $objects.Shapes.Count 2 "Objects shape count"
        Assert-Equal $objects.ListObjects.Count 2 "Objects table count"
        Assert-Equal $moreObjects.Hyperlinks.Count 1 "MoreObjects hyperlink count"
        Assert-Equal $moreObjects.Shapes.Count 1 "MoreObjects shape count"
        Assert-Equal $moreObjects.ListObjects.Count 1 "MoreObjects table count"
        Assert-Equal $plain.Hyperlinks.Count 0 "Plain hyperlink count"
        Assert-Equal $plain.Shapes.Count 0 "Plain shape count"
        Assert-Equal $plain.ListObjects.Count 0 "Plain table count"

        Assert-Equal $objects.Shapes.Item(1).TopLeftCell.Address($false, $false) "C1" `
            "Objects first shape top-left"
        Assert-Equal $objects.Shapes.Item(1).BottomRightCell.Address($false, $false) "D3" `
            "Objects first shape bottom-right"
        Assert-Equal $objects.Shapes.Item(2).TopLeftCell.Address($false, $false) "C3" `
            "Objects second shape top-left"
        Assert-Equal $objects.Shapes.Item(2).BottomRightCell.Address($false, $false) "D5" `
            "Objects second shape bottom-right"
        Assert-Equal $moreObjects.Shapes.Item(1).TopLeftCell.Address($false, $false) "C1" `
            "MoreObjects shape top-left"
        Assert-Equal $moreObjects.Shapes.Item(1).BottomRightCell.Address($false, $false) "D3" `
            "MoreObjects shape bottom-right"

        Write-Host "OK: Excel opened mixed-object workbook read-only: $resolved"
        Write-Host "OK: Objects/MoreObjects/Plain hyperlink, shape, table counts and anchors verified"
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        foreach ($object in @($plain, $moreObjects, $objects, $workbook)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
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

    Assert-Equal $describedShape.TopLeftCell.Address($false, $false) "A1" "offset image top-left cell"
    Assert-Equal $describedShape.BottomRightCell.Address($false, $false) "C3" "offset image bottom-right cell"

    $pointTolerance = 0.001
    $fromColumnOffset = $describedShape.Left - $describedShape.TopLeftCell.Left
    $fromRowOffset = $describedShape.Top - $describedShape.TopLeftCell.Top
    $toColumnOffset =
        ($describedShape.Left + $describedShape.Width) - $describedShape.BottomRightCell.Left
    $toRowOffset =
        ($describedShape.Top + $describedShape.Height) - $describedShape.BottomRightCell.Top

    Assert-Near $fromColumnOffset (Convert-EmuToPoint 111.0) $pointTolerance `
        "from marker column offset points"
    Assert-Near $fromRowOffset (Convert-EmuToPoint 222.0) $pointTolerance `
        "from marker row offset points"
    Assert-Near $toColumnOffset (Convert-EmuToPoint 333.0) $pointTolerance `
        "to marker column offset points"
    Assert-Near $toRowOffset (Convert-EmuToPoint 444.0) $pointTolerance `
        "to marker row offset points"

    $descriptionMatched = $false
    $shapeSummaries = @()
    foreach ($shape in @($worksheet.Shapes)) {
        $shapeSummaries += (
            "$($shape.Name) alt='$($shape.AlternativeText)' placement='$($shape.Placement)' " +
            "topLeft='$($shape.TopLeftCell.Address($false, $false))' " +
            "bottomRight='$($shape.BottomRightCell.Address($false, $false))' " +
            "left='$($shape.Left)' top='$($shape.Top)' width='$($shape.Width)' height='$($shape.Height)'"
        )
        if ([string]$shape.AlternativeText -eq $expectedDescription) {
            $descriptionMatched = $true
        }
    }
    Assert-True $descriptionMatched "Excel did not expose the custom image description as AlternativeText"

    Write-Host "OK: Excel opened image metadata workbook read-only: $resolved"
    Write-Host "OK: Shape count, custom name, default name, AlternativeText, editAs placement, and marker offsets verified"
    foreach ($summary in $shapeSummaries) {
        Write-Host "OK: Shape $summary"
    }

    Verify-BasicImageWorkbook -Excel $excel -Path $BasicPath
    Verify-MixedObjectWorkbook -Excel $excel -Path $MixedObjectPath
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
