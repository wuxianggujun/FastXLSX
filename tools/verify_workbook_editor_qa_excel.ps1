param(
    [string]$ReportPath = "build\qa\workbook-editor\report.json",
    [string]$OfficeReportPath = ""
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

function Resolve-WorkbookPath {
    param(
        [string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "case did not provide an output workbook path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Get-WorksheetNames {
    param(
        [object]$Workbook
    )

    $names = @()
    foreach ($worksheet in @($Workbook.Worksheets)) {
        $names += [string]$worksheet.Name
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($worksheet)
    }
    return $names
}

function Get-Worksheet {
    param(
        [object]$Workbook,
        [string]$Name
    )

    return $Workbook.Worksheets.Item($Name)
}

function Assert-CellValue {
    param(
        [object]$Worksheet,
        [string]$Address,
        [AllowNull()]
        [object]$Expected,
        [string]$Message
    )

    $value = $Worksheet.Range($Address).Value2
    Assert-Equal $value $Expected $Message
}

function Assert-Formula {
    param(
        [object]$Worksheet,
        [string]$Address,
        [string]$Expected,
        [string]$Message
    )

    $formula = [string]$Worksheet.Range($Address).Formula
    Assert-Equal $formula $Expected $Message
}

function Assert-FormulaIn {
    param(
        [object]$Worksheet,
        [string]$Address,
        [string[]]$Expected,
        [string]$Message
    )

    $formula = [string]$Worksheet.Range($Address).Formula
    if ($Expected -notcontains $formula) {
        throw "$Message expected one of '$($Expected -join "', '")', got '$formula'"
    }
}

function Verify-GeneratedRenameMaterialized {
    param([object]$Workbook)

    $edited = $null
    $untouched = $null
    try {
        $edited = Get-Worksheet $Workbook "EditedData"
        $untouched = Get-Worksheet $Workbook "Untouched"
        Assert-CellValue $edited "A1" "materialized-edit" "EditedData!A1"
        Assert-CellValue $edited "B2" 42 "EditedData!B2"
        Assert-CellValue $untouched "A1" "keep-me" "Untouched!A1"
    }
    finally {
        foreach ($object in @($untouched, $edited)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedSharedFormulaMaterialization {
    param([object]$Workbook)

    $sheet = $null
    $untouched = $null
    try {
        $sheet = Get-Worksheet $Workbook "SharedFormula"
        $untouched = Get-Worksheet $Workbook "Untouched"
        Assert-CellValue $sheet "A1" 1 "SharedFormula!A1"
        Assert-CellValue $sheet "B3" 8 "SharedFormula!B3"
        Assert-Formula $sheet "C1" "=A1+B1" "SharedFormula!C1 formula"
        Assert-Formula $sheet "C2" "=A2+B2" "SharedFormula!C2 formula"
        Assert-Formula $sheet "C3" "=A3+B3" "SharedFormula!C3 formula"
        Assert-Formula $sheet "D2" '=SUM(A2:B2)+$A2+A$1+$A$1' "SharedFormula!D2 formula"
        Assert-Formula $sheet "D3" '=SUM(A3:B3)+$A3+A$1+$A$1' "SharedFormula!D3 formula"
        Assert-CellValue $sheet "E4" "shared-formula-qa-edit" "SharedFormula!E4"
        Assert-CellValue $untouched "A1" "keep-shared-formula-qa" "Untouched!A1"
    }
    finally {
        foreach ($object in @($untouched, $sheet)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedSharedFormulaOfficeLikeMaterialization {
    param([object]$Workbook)

    $sheet = $null
    $untouched = $null
    try {
        $sheet = Get-Worksheet $Workbook "OfficeLikeShared"
        $untouched = Get-Worksheet $Workbook "Untouched"
        Assert-Formula $sheet "C1" "=A1+B1" "OfficeLikeShared!C1 formula"
        Assert-Formula $sheet "D1" "=B1+C1" "OfficeLikeShared!D1 formula"
        Assert-Formula $sheet "C3" "=A3+B3" "OfficeLikeShared!C3 formula"
        Assert-Formula $sheet "D3" "=B3+C3" "OfficeLikeShared!D3 formula"
        Assert-Formula $sheet "G3" '=SUM($A3:C3)+D$1' "OfficeLikeShared!G3 formula"
        Assert-CellValue $sheet "H6" "office-like-shared-formula-edit" "OfficeLikeShared!H6"
        Assert-CellValue $untouched "A1" "keep-office-like-shared-formula-qa" "Untouched!A1"
    }
    finally {
        foreach ($object in @($untouched, $sheet)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedSourceFormulaAudit {
    param([object]$Workbook)

    $renamed = $null
    $formula = $null
    try {
        $renamed = Get-Worksheet $Workbook "RenamedData"
        $formula = Get-Worksheet $Workbook "Formula"
        Assert-CellValue $renamed "A1" 1 "RenamedData!A1"
        $formulaText = [string]$formula.Range("A1").Formula
        Assert-True (-not [string]::IsNullOrWhiteSpace($formulaText)) "Formula!A1 formula should be present"
    }
    finally {
        foreach ($object in @($formula, $renamed)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedFormulaRenameRewrite {
    param([object]$Workbook)

    $renamed = $null
    $formula = $null
    try {
        $renamed = Get-Worksheet $Workbook "RenamedData"
        $formula = Get-Worksheet $Workbook "Formula"
        Assert-CellValue $renamed "A1" 1 "RenamedData!A1"
        Assert-FormulaIn $formula "A1" @("=RenamedData!A1", "='RenamedData'!A1") "Formula!A1 formula"
        Assert-FormulaIn $formula "A2" @("=RenamedData!`$A`$1", "='RenamedData'!`$A`$1") "Formula!A2 formula"
        Assert-FormulaIn $formula "A5" @("=RenamedData!A1+`"Data!A1`"", "='RenamedData'!A1+`"Data!A1`"") "Formula!A5 string-literal formula"
    }
    finally {
        foreach ($object in @($formula, $renamed)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedFormulaRenameEscapedSheetName {
    param([object]$Workbook)

    $renamed = $null
    $formula = $null
    try {
        $renamed = Get-Worksheet $Workbook "Renamed & O'Brien"
        $formula = Get-Worksheet $Workbook "Formula"
        Assert-CellValue $renamed "A1" 1 "Renamed & O'Brien!A1"
        Assert-FormulaIn $formula "A1" @(
            "='Renamed & O''Brien'!A1",
            "='Renamed & O'Brien'!A1"
        ) "Formula!A1 escaped formula"
        Assert-FormulaIn $formula "A2" @(
            "='Renamed & O''Brien'!`$A`$1",
            "='Renamed & O'Brien'!`$A`$1"
        ) "Formula!A2 escaped formula"
        Assert-FormulaIn $formula "A5" @(
            "='Renamed & O''Brien'!A1+`"Data!A1`"",
            "='Renamed & O'Brien'!A1+`"Data!A1`""
        ) "Formula!A5 escaped string-literal formula"
    }
    finally {
        foreach ($object in @($formula, $renamed)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedFormulaRenameChainRewrite {
    param([object]$Workbook)

    $renamed = $null
    $formula = $null
    try {
        $renamed = Get-Worksheet $Workbook "FinalData"
        $formula = Get-Worksheet $Workbook "Formula"
        Assert-CellValue $renamed "A1" 1 "FinalData!A1"
        Assert-FormulaIn $formula "A1" @("=FinalData!A1", "='FinalData'!A1") "Formula!A1 chain formula"
        Assert-FormulaIn $formula "A2" @("=FinalData!B1", "='FinalData'!B1") "Formula!A2 chain formula"
        Assert-FormulaIn $formula "A5" @("=FinalData!A1+`"TemporaryData!B1`"", "='FinalData'!A1+`"TemporaryData!B1`"") "Formula!A5 chain string-literal formula"
    }
    finally {
        foreach ($object in @($formula, $renamed)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedFormulaRenameDefaultAudit {
    param([object]$Workbook)

    $renamed = $null
    $formula = $null
    try {
        $renamed = Get-Worksheet $Workbook "RenamedData"
        $formula = Get-Worksheet $Workbook "Formula"
        Assert-CellValue $renamed "A1" 1 "RenamedData!A1"
        Assert-FormulaIn $formula "A1" @("=Data!A1", "='Data'!A1") "Formula!A1 default formula"
        Assert-FormulaIn $formula "A2" @("=Data!`$A`$1", "='Data'!`$A`$1") "Formula!A2 default formula"
        Assert-FormulaIn $formula "A5" @("=Data!A1+`"Data!A1`"", "='Data'!A1+`"Data!A1`"") "Formula!A5 default string-literal formula"
    }
    finally {
        foreach ($object in @($formula, $renamed)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedFormulaRenameDefinedNamesOnly {
    param([object]$Workbook)

    $renamed = $null
    $formula = $null
    try {
        $renamed = Get-Worksheet $Workbook "RenamedData"
        $formula = Get-Worksheet $Workbook "Formula"
        Assert-CellValue $renamed "A1" 1 "RenamedData!A1"
        Assert-FormulaIn $formula "A1" @("=Data!A1", "='Data'!A1") "Formula!A1 definedNames-only formula"
        Assert-FormulaIn $formula "A2" @("=Data!`$A`$1", "='Data'!`$A`$1") "Formula!A2 definedNames-only formula"
        Assert-FormulaIn $formula "A5" @("=Data!A1+`"Data!A1`"", "='Data'!A1+`"Data!A1`"") "Formula!A5 definedNames-only string-literal formula"
    }
    finally {
        foreach ($object in @($formula, $renamed)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedStylePassthrough {
    param([object]$Workbook)

    $sheet = $null
    try {
        $sheet = Get-Worksheet $Workbook "Data"
        Assert-CellValue $sheet "A1" 9.5 "Data!A1"
        Assert-CellValue $sheet "B1" "explicit default" "Data!B1"
        Assert-Equal $sheet.Range("A1").NumberFormat "0.00" "Data!A1 NumberFormat"
    }
    finally {
        if ($null -ne $sheet) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($sheet)
        }
    }
}

function Verify-GeneratedImageReplace {
    param([object]$Workbook)

    $pictures = $null
    try {
        $pictures = Get-Worksheet $Workbook "Pictures"
        Assert-CellValue $pictures "A1" "image-sheet" "Pictures!A1"
        Assert-Equal $pictures.Shapes.Count 1 "Pictures shape count"
    }
    finally {
        if ($null -ne $pictures) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($pictures)
        }
    }
}

function Verify-GeneratedPublicE2E {
    param([object]$Workbook)

    $edited = $null
    $replaceMe = $null
    $pictures = $null
    try {
        $edited = Get-Worksheet $Workbook "EditedData"
        $replaceMe = Get-Worksheet $Workbook "ReplaceMe"
        $pictures = Get-Worksheet $Workbook "Pictures"
        Assert-CellValue $edited "A1" "materialized-edit" "EditedData!A1"
        Assert-CellValue $edited "B2" 42 "EditedData!B2"
        Assert-CellValue $replaceMe "A1" "sheetdata-final" "ReplaceMe!A1"
        Assert-CellValue $replaceMe "B1" 7 "ReplaceMe!B1"
        Assert-Equal $pictures.Shapes.Count 1 "Pictures shape count"
    }
    finally {
        foreach ($object in @($pictures, $replaceMe, $edited)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedInMemoryInsertFormula {
    param([object]$Workbook)

    $data = $null
    $notes = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        $notes = Get-Worksheet $Workbook "Notes"
        Assert-CellValue $data "A1" "item" "Data!A1"
        Assert-CellValue $data "A2" "inserted-row" "Data!A2"
        Assert-CellValue $data "B2" 5 "Data!B2"
        Assert-Formula $data "C2" "=B2*2" "Data!C2 formula"
        Assert-CellValue $data "A3" "source-row" "Data!A3"
        Assert-CellValue $data "B3" 3 "Data!B3"
        Assert-Formula $data "C3" "=B3*2" "Data!C3 formula"
        Assert-CellValue $notes "A1" "preserved" "Notes!A1"
    }
    finally {
        foreach ($object in @($notes, $data)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedInMemoryDeleteColumnFormula {
    param([object]$Workbook)

    $data = $null
    $notes = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        $notes = Get-Worksheet $Workbook "Notes"
        Assert-CellValue $data "A1" 7 "Data!A1"
        Assert-Formula $data "B1" "=A1+C1" "Data!B1 formula"
        Assert-CellValue $data "C1" "tail" "Data!C1"
        Assert-CellValue $notes "A1" "preserved" "Notes!A1"
    }
    finally {
        foreach ($object in @($notes, $data)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedInMemoryInsertColumnFormula {
    param([object]$Workbook)

    $data = $null
    $notes = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        $notes = Get-Worksheet $Workbook "Notes"
        Assert-CellValue $data "A1" "item" "Data!A1"
        Assert-CellValue $data "B1" "inserted-col" "Data!B1"
        Assert-CellValue $data "C1" 2 "Data!C1"
        Assert-Formula $data "D1" "=C1*2" "Data!D1 formula"
        Assert-CellValue $notes "A1" "preserved" "Notes!A1"
    }
    finally {
        foreach ($object in @($notes, $data)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedInMemoryDeleteRowFormula {
    param([object]$Workbook)

    $data = $null
    $notes = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        $notes = Get-Worksheet $Workbook "Notes"
        Assert-CellValue $data "A1" 4 "Data!A1"
        Assert-Formula $data "B1" "=A1+A2" "Data!B1 formula"
        Assert-CellValue $data "A2" 6 "Data!A2"
        Assert-CellValue $notes "A1" "preserved" "Notes!A1"
    }
    finally {
        foreach ($object in @($notes, $data)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedInMemoryStationaryFormulaShift {
    param([object]$Workbook)

    $data = $null
    $notes = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        $notes = Get-Worksheet $Workbook "Notes"
        Assert-CellValue $data "A1" "item" "Data!A1"
        Assert-CellValue $data "B1" "label" "Data!B1"
        Assert-FormulaIn $data "C1" @(
            '=SUM($A$4,C$4,Data!$A$4,$E$1,$E1,Data!$E$1,#REF!,#REF!,Data!#REF!,#REF!,Data!#REF!)',
            '=SUM($A$4,C$4,Data!$A$4,$E$1,$E1,Data!$E$1,#REF!,#REF!,#REF!,#REF!,#REF!)'
        ) "Data!C1 formula"
        Assert-CellValue $data "D1" $null "Data!D1 inserted blank"
        Assert-CellValue $data "E1" 4 "Data!E1"
        Assert-CellValue $data "F1" "survive-column" "Data!F1"
        Assert-CellValue $data "G1" $null "Data!G1 deleted column"
        Assert-CellValue $data "A4" "row-target" "Data!A4"
        Assert-CellValue $data "C4" "c3-target" "Data!C4"
        Assert-CellValue $data "A7" $null "Data!A7 deleted row"
        Assert-CellValue $notes "A1" "preserved" "Notes!A1"
    }
    finally {
        foreach ($object in @($notes, $data)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedInMemoryStationaryRangeFormulaShift {
    param([object]$Workbook)

    $data = $null
    $notes = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        $notes = Get-Worksheet $Workbook "Notes"
        Assert-CellValue $data "A1" "item" "Data!A1"
        Assert-CellValue $data "B1" "label" "Data!B1"
        Assert-Formula $data "C1" "=SUM(A4:B4)+4:4+SUM(E1:F1)+E:F" "Data!C1 formula"
        Assert-CellValue $data "D1" $null "Data!D1 inserted blank"
        Assert-CellValue $data "E1" 4 "Data!E1"
        Assert-CellValue $data "F1" 5 "Data!F1"
        Assert-CellValue $data "A4" 1 "Data!A4"
        Assert-CellValue $data "B4" 2 "Data!B4"
        Assert-CellValue $notes "A1" "preserved" "Notes!A1"
    }
    finally {
        foreach ($object in @($notes, $data)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedInMemoryClearErase {
    param([object]$Workbook)

    $data = $null
    $notes = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        $notes = Get-Worksheet $Workbook "Notes"
        Assert-CellValue $data "A1" "keep-a1" "Data!A1"
        Assert-CellValue $data "B1" $null "Data!B1 blank"
        Assert-CellValue $data "C1" $null "Data!C1 erased"
        Assert-CellValue $data "D1" "new-d1" "Data!D1"
        Assert-CellValue $data "A2" 8 "Data!A2"
        Assert-CellValue $notes "A1" "preserved" "Notes!A1"
    }
    finally {
        foreach ($object in @($notes, $data)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedInMemoryAppendRowFormula {
    param([object]$Workbook)

    $data = $null
    $notes = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        $notes = Get-Worksheet $Workbook "Notes"
        Assert-CellValue $data "A1" "item" "Data!A1"
        Assert-CellValue $data "B1" "value" "Data!B1"
        Assert-CellValue $data "C1" "double" "Data!C1"
        Assert-CellValue $data "A2" "source-row" "Data!A2"
        Assert-CellValue $data "B2" 10 "Data!B2"
        Assert-CellValue $data "A3" "appended-row" "Data!A3"
        Assert-CellValue $data "B3" 4 "Data!B3"
        Assert-Formula $data "C3" "=B3*2" "Data!C3 formula"
        Assert-CellValue $notes "A1" "preserved" "Notes!A1"
    }
    finally {
        foreach ($object in @($notes, $data)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedInMemoryOverwriteFormulaText {
    param([object]$Workbook)

    $data = $null
    $notes = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        $notes = Get-Worksheet $Workbook "Notes"
        Assert-CellValue $data "A1" "new-text" "Data!A1"
        Assert-CellValue $data "B1" 5 "Data!B1"
        Assert-Formula $data "C1" "=B1+10" "Data!C1 formula"
        Assert-CellValue $data "A2" "keep-row-two" "Data!A2"
        Assert-CellValue $notes "A1" "preserved" "Notes!A1"
    }
    finally {
        foreach ($object in @($notes, $data)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedInMemoryReopenModifySave {
    param([object]$Workbook)

    $data = $null
    $notes = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        $notes = Get-Worksheet $Workbook "Notes"
        Assert-CellValue $data "A1" "first-edit" "Data!A1"
        Assert-CellValue $data "B1" 10 "Data!B1"
        Assert-Formula $data "C1" "=B1+5" "Data!C1 formula"
        Assert-CellValue $data "D1" "second-edit" "Data!D1"
        Assert-CellValue $data "A2" "keep-row-two" "Data!A2"
        Assert-CellValue $data "A3" "reopened-row" "Data!A3"
        Assert-CellValue $data "B3" 4 "Data!B3"
        Assert-Formula $data "C3" "=B3*2" "Data!C3 formula"
        Assert-CellValue $notes "A1" "preserved" "Notes!A1"
    }
    finally {
        foreach ($object in @($notes, $data)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedInMemoryReopenModifyPostNoopThirdSave {
    param([object]$Workbook)

    Verify-GeneratedInMemoryReopenModifySave $Workbook

    $data = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        Assert-CellValue $data "E1" "third-edit" "Data!E1"
    }
    finally {
        if ($null -ne $data) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($data)
        }
    }
}

function Verify-GeneratedInMemoryMultiSheetSave {
    param([object]$Workbook)

    $data = $null
    $summary = $null
    $notes = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        $summary = Get-Worksheet $Workbook "Summary"
        $notes = Get-Worksheet $Workbook "Notes"
        Assert-CellValue $data "A1" "edited-data" "Data!A1"
        Assert-CellValue $data "B1" 7 "Data!B1"
        Assert-CellValue $data "A2" "keep-data-row" "Data!A2"
        Assert-CellValue $data "A3" "multi-row" "Data!A3"
        Assert-CellValue $data "B3" 3 "Data!B3"
        Assert-Formula $data "C3" "=B3+Data!B1" "Data!C3 formula"
        Assert-CellValue $summary "A1" "edited-summary" "Summary!A1"
        Assert-Formula $summary "B1" "=Data!B1+Data!B3" "Summary!B1 formula"
        Assert-CellValue $notes "A1" "preserved" "Notes!A1"
    }
    finally {
        foreach ($object in @($notes, $summary, $data)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedInMemoryMultiSheetRetryReopenModifySave {
    param([object]$Workbook)

    Verify-GeneratedInMemoryMultiSheetSave $Workbook

    $data = $null
    $summary = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        $summary = Get-Worksheet $Workbook "Summary"
        Assert-CellValue $data "D1" "retry-reopened-data" "Data!D1"
        Assert-Formula $summary "C1" "=Data!B1+10" "Summary!C1 formula"
    }
    finally {
        foreach ($object in @($summary, $data)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedInMemoryMultiSheetRetryReopenModifyPostNoopThirdSave {
    param([object]$Workbook)

    Verify-GeneratedInMemoryMultiSheetRetryReopenModifySave $Workbook

    $data = $null
    $summary = $null
    try {
        $data = Get-Worksheet $Workbook "Data"
        $summary = Get-Worksheet $Workbook "Summary"
        Assert-CellValue $data "E1" "retry-reopened-post-noop-data" "Data!E1"
        Assert-Formula $summary "D1" "=Data!B1+20" "Summary!D1 formula"
    }
    finally {
        foreach ($object in @($summary, $data)) {
            if ($null -ne $object) {
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($object)
            }
        }
    }
}

function Verify-GeneratedStyleRejection {
    param([object]$Workbook)

    $sheet = $null
    try {
        $sheet = Get-Worksheet $Workbook "Data"
        Assert-CellValue $sheet "A1" "placeholder-a1" "Data!A1"
        Assert-CellValue $sheet "B1" 1 "Data!B1"
    }
    finally {
        if ($null -ne $sheet) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($sheet)
        }
    }
}

function Verify-FixtureCase {
    param(
        [object]$Workbook,
        [object]$ToolReport
    )

    $sheetName = [string]$ToolReport.renamed_sheet_name
    if ([string]::IsNullOrWhiteSpace($sheetName)) {
        $sheetName = [string]$ToolReport.source_sheet_name
    }
    Assert-True (-not [string]::IsNullOrWhiteSpace($sheetName)) "fixture case did not report a worksheet name"

    $sheet = $null
    try {
        $sheet = Get-Worksheet $Workbook $sheetName
        Assert-CellValue $sheet "A1" "fixture-materialized-edit" "$sheetName!A1"
        Assert-CellValue $sheet "B2" 42 "$sheetName!B2"
    }
    finally {
        if ($null -ne $sheet) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($sheet)
        }
    }
}

function Verify-FixtureImageReplace {
    param(
        [object]$Workbook,
        [object]$ToolReport
    )

    $sheetName = [string]$ToolReport.source_sheet_name
    Assert-True (-not [string]::IsNullOrWhiteSpace($sheetName)) "fixture image replace did not report a worksheet name"

    $sheet = $null
    try {
        $sheet = Get-Worksheet $Workbook $sheetName
        Assert-True ($sheet.Shapes.Count -ge 1) "$sheetName shape count"
    }
    finally {
        if ($null -ne $sheet) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($sheet)
        }
    }
}

function Verify-Case {
    param(
        [object]$Excel,
        [object]$Case
    )

    $toolReport = $Case.tool_report
    $scenario = [string]$toolReport.scenario
    if ($scenario -eq "fixture_source_formula_audit") {
        return [ordered]@{
            name = [string]$Case.name
            status = "skipped"
            reason = "read-only source formula audit does not produce an output workbook"
        }
    }
    if ($scenario -eq "generated_shared_formula_boundary_materialization" -or
        $scenario -eq "generated_shared_formula_boundary_materialization_noop_save") {
        return [ordered]@{
            name = [string]$Case.name
            status = "skipped"
            reason = "synthetic parser-boundary formulas are validated by ZIP/XML and openpyxl, not Excel COM"
        }
    }

    $path = Resolve-WorkbookPath ([string]$toolReport.output)
    $workbook = $null

    try {
        $workbook = $Excel.Workbooks.Open($path, 0, $true)
        $sheetNames = Get-WorksheetNames $workbook

        switch ($scenario) {
            "generated_rename_materialized" { Verify-GeneratedRenameMaterialized $workbook }
            "generated_rename_materialized_noop_save" { Verify-GeneratedRenameMaterialized $workbook }
            "generated_in_memory_insert_formula" { Verify-GeneratedInMemoryInsertFormula $workbook }
            "generated_in_memory_insert_formula_noop_save" { Verify-GeneratedInMemoryInsertFormula $workbook }
            "generated_in_memory_full_calc_insert_formula" { Verify-GeneratedInMemoryInsertFormula $workbook }
            "generated_in_memory_full_calc_insert_formula_noop_save" { Verify-GeneratedInMemoryInsertFormula $workbook }
            "generated_in_memory_delete_column_formula" { Verify-GeneratedInMemoryDeleteColumnFormula $workbook }
            "generated_in_memory_delete_column_formula_noop_save" { Verify-GeneratedInMemoryDeleteColumnFormula $workbook }
            "generated_in_memory_full_calc_delete_column_formula" { Verify-GeneratedInMemoryDeleteColumnFormula $workbook }
            "generated_in_memory_full_calc_delete_column_formula_noop_save" { Verify-GeneratedInMemoryDeleteColumnFormula $workbook }
            "generated_in_memory_insert_column_formula" { Verify-GeneratedInMemoryInsertColumnFormula $workbook }
            "generated_in_memory_insert_column_formula_noop_save" { Verify-GeneratedInMemoryInsertColumnFormula $workbook }
            "generated_in_memory_full_calc_insert_column_formula" { Verify-GeneratedInMemoryInsertColumnFormula $workbook }
            "generated_in_memory_full_calc_insert_column_formula_noop_save" { Verify-GeneratedInMemoryInsertColumnFormula $workbook }
            "generated_in_memory_delete_row_formula" { Verify-GeneratedInMemoryDeleteRowFormula $workbook }
            "generated_in_memory_delete_row_formula_noop_save" { Verify-GeneratedInMemoryDeleteRowFormula $workbook }
            "generated_in_memory_full_calc_delete_row_formula" { Verify-GeneratedInMemoryDeleteRowFormula $workbook }
            "generated_in_memory_full_calc_delete_row_formula_noop_save" { Verify-GeneratedInMemoryDeleteRowFormula $workbook }
            "generated_in_memory_stationary_formula_shift" { Verify-GeneratedInMemoryStationaryFormulaShift $workbook }
            "generated_in_memory_stationary_formula_shift_noop_save" { Verify-GeneratedInMemoryStationaryFormulaShift $workbook }
            "generated_in_memory_full_calc_stationary_formula_shift" { Verify-GeneratedInMemoryStationaryFormulaShift $workbook }
            "generated_in_memory_full_calc_stationary_formula_shift_noop_save" { Verify-GeneratedInMemoryStationaryFormulaShift $workbook }
            "generated_in_memory_stationary_range_formula_shift" { Verify-GeneratedInMemoryStationaryRangeFormulaShift $workbook }
            "generated_in_memory_stationary_range_formula_shift_noop_save" { Verify-GeneratedInMemoryStationaryRangeFormulaShift $workbook }
            "generated_in_memory_full_calc_stationary_range_formula_shift" { Verify-GeneratedInMemoryStationaryRangeFormulaShift $workbook }
            "generated_in_memory_full_calc_stationary_range_formula_shift_noop_save" { Verify-GeneratedInMemoryStationaryRangeFormulaShift $workbook }
            "generated_in_memory_clear_erase" { Verify-GeneratedInMemoryClearErase $workbook }
            "generated_in_memory_clear_erase_noop_save" { Verify-GeneratedInMemoryClearErase $workbook }
            "generated_in_memory_append_row_formula" { Verify-GeneratedInMemoryAppendRowFormula $workbook }
            "generated_in_memory_append_row_formula_noop_save" { Verify-GeneratedInMemoryAppendRowFormula $workbook }
            "generated_in_memory_full_calc_append_row_formula" { Verify-GeneratedInMemoryAppendRowFormula $workbook }
            "generated_in_memory_full_calc_append_row_formula_noop_save" { Verify-GeneratedInMemoryAppendRowFormula $workbook }
            "generated_in_memory_overwrite_formula_text" { Verify-GeneratedInMemoryOverwriteFormulaText $workbook }
            "generated_in_memory_overwrite_formula_text_noop_save" { Verify-GeneratedInMemoryOverwriteFormulaText $workbook }
            "generated_in_memory_full_calc_overwrite_formula_text" { Verify-GeneratedInMemoryOverwriteFormulaText $workbook }
            "generated_in_memory_full_calc_overwrite_formula_text_noop_save" { Verify-GeneratedInMemoryOverwriteFormulaText $workbook }
            "generated_in_memory_retry_noop_save" { Verify-GeneratedInMemoryOverwriteFormulaText $workbook }
            "generated_in_memory_retry_path_equivalent_noop_save" { Verify-GeneratedInMemoryOverwriteFormulaText $workbook }
            "generated_in_memory_full_calc_retry_noop_save" { Verify-GeneratedInMemoryOverwriteFormulaText $workbook }
            "generated_in_memory_full_calc_retry_path_equivalent_noop_save" { Verify-GeneratedInMemoryOverwriteFormulaText $workbook }
            "generated_in_memory_retry_reopen_modify_noop_save" { Verify-GeneratedInMemoryReopenModifySave $workbook }
            "generated_in_memory_retry_path_equivalent_reopen_modify_noop_save" { Verify-GeneratedInMemoryReopenModifySave $workbook }
            "generated_in_memory_retry_path_equivalent_reopen_modify_post_noop_third_save" { Verify-GeneratedInMemoryReopenModifyPostNoopThirdSave $workbook }
            "generated_in_memory_retry_reopen_modify_post_noop_third_save" { Verify-GeneratedInMemoryReopenModifyPostNoopThirdSave $workbook }
            "generated_in_memory_reopen_modify_save" { Verify-GeneratedInMemoryReopenModifySave $workbook }
            "generated_in_memory_reopen_modify_noop_save" { Verify-GeneratedInMemoryReopenModifySave $workbook }
            "generated_in_memory_reopen_modify_post_noop_third_save" { Verify-GeneratedInMemoryReopenModifyPostNoopThirdSave $workbook }
            "generated_in_memory_multi_sheet_save" { Verify-GeneratedInMemoryMultiSheetSave $workbook }
            "generated_in_memory_multi_sheet_noop_save" { Verify-GeneratedInMemoryMultiSheetSave $workbook }
            "generated_in_memory_multi_sheet_retry_save" { Verify-GeneratedInMemoryMultiSheetSave $workbook }
            "generated_in_memory_multi_sheet_retry_noop_save" { Verify-GeneratedInMemoryMultiSheetSave $workbook }
            "generated_in_memory_multi_sheet_retry_path_equivalent_noop_save" { Verify-GeneratedInMemoryMultiSheetSave $workbook }
            "generated_in_memory_multi_sheet_retry_reopen_modify_save" { Verify-GeneratedInMemoryMultiSheetRetryReopenModifySave $workbook }
            "generated_in_memory_multi_sheet_retry_reopen_modify_noop_save" { Verify-GeneratedInMemoryMultiSheetRetryReopenModifySave $workbook }
            "generated_in_memory_multi_sheet_retry_path_equivalent_reopen_modify_noop_save" { Verify-GeneratedInMemoryMultiSheetRetryReopenModifySave $workbook }
            "generated_in_memory_multi_sheet_retry_path_equivalent_reopen_modify_post_noop_third_save" { Verify-GeneratedInMemoryMultiSheetRetryReopenModifyPostNoopThirdSave $workbook }
            "generated_in_memory_multi_sheet_retry_reopen_modify_post_noop_third_save" { Verify-GeneratedInMemoryMultiSheetRetryReopenModifyPostNoopThirdSave $workbook }
            "generated_source_formula_audit" { Verify-GeneratedSourceFormulaAudit $workbook }
            "generated_source_formula_audit_noop_save" { Verify-GeneratedSourceFormulaAudit $workbook }
            "generated_formula_rename_rewrite" { Verify-GeneratedFormulaRenameRewrite $workbook }
            "generated_formula_rename_rewrite_noop_save" { Verify-GeneratedFormulaRenameRewrite $workbook }
            "generated_formula_rename_escaped_sheet_name" { Verify-GeneratedFormulaRenameEscapedSheetName $workbook }
            "generated_formula_rename_escaped_sheet_name_noop_save" { Verify-GeneratedFormulaRenameEscapedSheetName $workbook }
            "generated_formula_rename_chain_rewrite" { Verify-GeneratedFormulaRenameChainRewrite $workbook }
            "generated_formula_rename_chain_rewrite_noop_save" { Verify-GeneratedFormulaRenameChainRewrite $workbook }
            "generated_formula_rename_defined_names_only" { Verify-GeneratedFormulaRenameDefinedNamesOnly $workbook }
            "generated_formula_rename_defined_names_only_noop_save" { Verify-GeneratedFormulaRenameDefinedNamesOnly $workbook }
            "generated_formula_rename_default_audit" { Verify-GeneratedFormulaRenameDefaultAudit $workbook }
            "generated_formula_rename_default_audit_noop_save" { Verify-GeneratedFormulaRenameDefaultAudit $workbook }
            "generated_shared_formula_materialization" { Verify-GeneratedSharedFormulaMaterialization $workbook }
            "generated_shared_formula_materialization_noop_save" { Verify-GeneratedSharedFormulaMaterialization $workbook }
            "generated_shared_formula_office_like_materialization" { Verify-GeneratedSharedFormulaOfficeLikeMaterialization $workbook }
            "generated_shared_formula_office_like_materialization_noop_save" { Verify-GeneratedSharedFormulaOfficeLikeMaterialization $workbook }
            "generated_style_passthrough" { Verify-GeneratedStylePassthrough $workbook }
            "generated_style_passthrough_noop_save" { Verify-GeneratedStylePassthrough $workbook }
            "generated_image_replace" { Verify-GeneratedImageReplace $workbook }
            "generated_image_replace_noop_save" { Verify-GeneratedImageReplace $workbook }
            "generated_public_e2e" { Verify-GeneratedPublicE2E $workbook }
            "generated_public_e2e_noop_save" { Verify-GeneratedPublicE2E $workbook }
            "generated_non_default_style_rejection" { Verify-GeneratedStyleRejection $workbook }
            "generated_non_default_style_rejection_noop_save" { Verify-GeneratedStyleRejection $workbook }
            "fixture_rename_materialized" { Verify-FixtureCase $workbook $toolReport }
            "fixture_materialized_only" { Verify-FixtureCase $workbook $toolReport }
            "fixture_image_replace" { Verify-FixtureImageReplace $workbook $toolReport }
            default { throw "unsupported workbook-editor QA Excel scenario '$scenario'" }
        }

        return [ordered]@{
            name = [string]$Case.name
            status = "ok"
            path = $path
            sheetnames = @($sheetNames)
        }
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($workbook)
        }
    }
}

$resolvedReportPath = (Resolve-Path -LiteralPath $ReportPath).Path
if ([string]::IsNullOrWhiteSpace($OfficeReportPath)) {
    $OfficeReportPath = Join-Path (Split-Path -Parent $resolvedReportPath) "office-report.json"
}

$existingExcelPids = @{}
foreach ($process in @(Get-Process EXCEL -ErrorAction SilentlyContinue)) {
    $existingExcelPids[[int]$process.Id] = $true
}

$report = Get-Content -Encoding UTF8 -Raw -LiteralPath $resolvedReportPath | ConvertFrom-Json
$excel = $null
$caseReports = @()

try {
    $excel = New-Object -ComObject Excel.Application
    $excel.Visible = $false
    $excel.DisplayAlerts = $false
    $excel.AskToUpdateLinks = $false

    foreach ($case in @($report.cases)) {
        if (-not [string]::IsNullOrWhiteSpace([string]$case.error)) {
            $caseReports += [ordered]@{
                name = [string]$case.name
                status = "skipped"
                reason = "case failed before Excel verification"
            }
            continue
        }

        try {
            $caseReports += Verify-Case $excel $case
        }
        catch {
            $caseReports += [ordered]@{
                name = [string]$case.name
                status = "failed"
                error = [string]$_.Exception.Message
            }
        }
    }
}
finally {
    if ($null -ne $excel) {
        $excel.Quit() | Out-Null
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($excel)
    }
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
    Start-Sleep -Milliseconds 500

    foreach ($process in @(Get-Process EXCEL -ErrorAction SilentlyContinue)) {
        if (-not $existingExcelPids.ContainsKey([int]$process.Id)) {
            Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

$failedCases = @($caseReports | Where-Object { $_.status -eq "failed" } | ForEach-Object { $_.name })
$officeReport = [ordered]@{
    status = if ($failedCases.Count -eq 0) { "ok" } else { "failed" }
    report = $resolvedReportPath
    case_count = $caseReports.Count
    failed_cases = $failedCases
    cases = $caseReports
}

$officeReportJson = $officeReport | ConvertTo-Json -Depth 8
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OfficeReportPath) | Out-Null
Set-Content -Encoding UTF8 -LiteralPath $OfficeReportPath -Value $officeReportJson
Write-Host $officeReportJson

if ($failedCases.Count -ne 0) {
    throw "Excel workbook-editor QA failed for cases: $($failedCases -join ', ')"
}
