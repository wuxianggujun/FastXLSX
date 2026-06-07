param(
    [string]$InMemoryPath = "build\windows-nmake-release\tests\fastxlsx-document-properties.xlsx",
    [string]$StreamingPath = "build\windows-nmake-release\tests\fastxlsx-streaming-document-properties.xlsx"
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

function Get-BuiltinDocumentPropertyValue {
    param(
        [object]$Workbook,
        [string]$Name
    )

    try {
        $properties = $Workbook.BuiltinDocumentProperties
        if ($null -eq $properties) {
            return $null
        }

        foreach ($property in @($properties)) {
            try {
                if ([string]$property.Name -eq $Name) {
                    return $property.Value
                }
            }
            catch {
                continue
            }
        }
    }
    catch {
        return $null
    }

    return $null
}

function Verify-Workbook {
    param(
        [object]$Excel,
        [string]$Path,
        [string]$SheetName,
        [string]$ExpectedA1,
        [string]$ExpectedTitle,
        [string]$ExpectedAuthor,
        [string]$ExpectedSubject,
        [string]$ExpectedKeywords,
        [string]$ExpectedCategory
    )

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $workbook = $null
    $worksheet = $null

    try {
        $workbook = $Excel.Workbooks.Open($resolved, 0, $true)
        $worksheet = $workbook.Worksheets.Item($SheetName)
        Assert-Equal $worksheet.Range("A1").Value2 $ExpectedA1 "$SheetName A1 smoke value"

        $checkedProperties = @()
        $propertyExpectations = @(
            @("Title", $ExpectedTitle),
            @("Author", $ExpectedAuthor),
            @("Subject", $ExpectedSubject),
            @("Keywords", $ExpectedKeywords),
            @("Category", $ExpectedCategory)
        )
        foreach ($entry in $propertyExpectations) {
            $actual = Get-BuiltinDocumentPropertyValue $workbook $entry[0]
            if ($null -ne $actual) {
                Assert-Equal $actual $entry[1] "$SheetName document property $($entry[0])"
                $checkedProperties += $entry[0]
            }
        }

        if ($checkedProperties.Count -eq 0) {
            Write-Host "OK: Excel opened document properties workbook read-only: $resolved"
            Write-Host "OK: $SheetName sheet smoke value verified; BuiltinDocumentProperties were not exposed in this Excel COM session, XML/openpyxl helper remains authoritative"
        }
        else {
            Write-Host "OK: Excel opened document properties workbook read-only: $resolved"
            Write-Host "OK: $SheetName sheet smoke value and BuiltinDocumentProperties verified: $($checkedProperties -join ', ')"
        }
    }
    finally {
        if ($null -ne $workbook) {
            $workbook.Close($false) | Out-Null
        }
        if ($null -ne $worksheet) {
            [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($worksheet)
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

    Verify-Workbook `
        -Excel $excel `
        -Path $InMemoryPath `
        -SheetName "Props" `
        -ExpectedA1 "doc props" `
        -ExpectedTitle "Quarterly <Report>" `
        -ExpectedAuthor "Alice & Bob" `
        -ExpectedSubject "Metadata & API" `
        -ExpectedKeywords "xlsx;docprops" `
        -ExpectedCategory "Validation"

    Verify-Workbook `
        -Excel $excel `
        -Path $StreamingPath `
        -SheetName "StreamProps" `
        -ExpectedA1 "document properties" `
        -ExpectedTitle "Streaming <Props>" `
        -ExpectedAuthor "Stream & Author" `
        -ExpectedSubject "DocProps & Streaming" `
        -ExpectedKeywords "streaming;docprops" `
        -ExpectedCategory "Validation"
}
finally {
    if ($null -ne $excel) {
        $excel.Quit() | Out-Null
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($excel)
    }
    [System.GC]::Collect()
    [System.GC]::WaitForPendingFinalizers()
}
