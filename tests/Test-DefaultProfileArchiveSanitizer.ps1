param(
    [string]$SanitizerPath =
        (Join-Path $PSScriptRoot '..\tools\Sanitize-DefaultProfile.ps1'),
    [string]$ValidArchive =
        (Join-Path $PSScriptRoot '..\Default.7z')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Assert-ArchiveSanitizerCheck {
    param([bool]$Condition, [string]$Message)

    if (-not $Condition) {
        throw $Message
    }
}

function New-TestZip {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()]
        [object[]]$Entries
    )

    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $stream = [IO.File]::Open(
        $Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write,
        [IO.FileShare]::None)
    try {
        $archive = [IO.Compression.ZipArchive]::new(
            $stream, [IO.Compression.ZipArchiveMode]::Create, $true)
        try {
            foreach ($specification in $Entries) {
                $entry = $archive.CreateEntry([string]$specification.Path)
                if (-not ([string]$specification.Path).EndsWith('/')) {
                    $entryStream = $entry.Open()
                    try {
                        $bytes = [Text.Encoding]::UTF8.GetBytes(
                            [string]$specification.Content)
                        $entryStream.Write($bytes, 0, $bytes.Length)
                    }
                    finally {
                        $entryStream.Dispose()
                    }
                }
            }
        }
        finally {
            $archive.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function New-DeclaredSizeZip {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$EntryName,
        [Parameter(Mandatory = $true)][uint32]$DeclaredBytes
    )

    $nameBytes = [Text.Encoding]::UTF8.GetBytes($EntryName)
    $stream = [IO.File]::Open(
        $Path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write,
        [IO.FileShare]::None)
    try {
        $writer = [IO.BinaryWriter]::new($stream, [Text.Encoding]::UTF8, $true)
        try {
            # A header-only stored ZIP entry lets tar report a large declared
            # size without allocating or reading a large test payload.
            $writer.Write([uint32]0x04034b50)
            $writer.Write([uint16]20)
            $writer.Write([uint16]0)
            $writer.Write([uint16]0)
            $writer.Write([uint16]0)
            $writer.Write([uint16]0)
            $writer.Write([uint32]0)
            $writer.Write([uint32]0)
            $writer.Write($DeclaredBytes)
            $writer.Write([uint16]$nameBytes.Length)
            $writer.Write([uint16]0)
            $writer.Write($nameBytes)

            $centralOffset = [uint32]$stream.Position
            $writer.Write([uint32]0x02014b50)
            $writer.Write([uint16]20)
            $writer.Write([uint16]20)
            $writer.Write([uint16]0)
            $writer.Write([uint16]0)
            $writer.Write([uint16]0)
            $writer.Write([uint16]0)
            $writer.Write([uint32]0)
            $writer.Write([uint32]0)
            $writer.Write($DeclaredBytes)
            $writer.Write([uint16]$nameBytes.Length)
            $writer.Write([uint16]0)
            $writer.Write([uint16]0)
            $writer.Write([uint16]0)
            $writer.Write([uint16]0)
            $writer.Write([uint32]0)
            $writer.Write([uint32]0)
            $writer.Write($nameBytes)
            $centralSize = [uint32]($stream.Position - $centralOffset)

            $writer.Write([uint32]0x06054b50)
            $writer.Write([uint16]0)
            $writer.Write([uint16]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]1)
            $writer.Write($centralSize)
            $writer.Write($centralOffset)
            $writer.Write([uint16]0)
            $writer.Flush()
        }
        finally {
            $writer.Dispose()
        }
    }
    finally {
        $stream.Dispose()
    }
}

function Assert-WrapperRejects {
    param(
        [Parameter(Mandatory = $true)][string]$InputArchive,
        [Parameter(Mandatory = $true)][string]$OutputArchive,
        [Parameter(Mandatory = $true)][string]$Message,
        [string]$ErrorPattern = ''
    )

    $rejected = $false
    $errorMessage = ''
    try {
        & $script:sanitizer -InputArchive $InputArchive `
            -OutputArchive $OutputArchive | Out-Null
    }
    catch {
        $rejected = $true
        $errorMessage = $_.Exception.Message
    }
    Assert-ArchiveSanitizerCheck $rejected $Message
    if (-not [string]::IsNullOrEmpty($ErrorPattern)) {
        Assert-ArchiveSanitizerCheck ($errorMessage -match $ErrorPattern) `
            "$Message Unexpected error: $errorMessage"
    }
}

$sanitizer = (Resolve-Path -LiteralPath $SanitizerPath).Path
$validInput = (Resolve-Path -LiteralPath $ValidArchive).Path
$systemTar = Join-Path ([Environment]::SystemDirectory) 'tar.exe'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ('ctSpaces default archive sanitizer ' +
     [guid]::NewGuid().ToString('N'))

try {
    New-Item -ItemType Directory -Path $testRoot | Out-Null

    $normalOutput = Join-Path $testRoot 'NormalOutput.7z'
    $normalResult = & $sanitizer -InputArchive $validInput `
        -OutputArchive $normalOutput
    Assert-ArchiveSanitizerCheck `
        ((Test-Path -LiteralPath $normalOutput -PathType Leaf) -and
         $normalResult.Archive -eq $normalOutput) `
        'The hardened wrapper did not produce its validated output.'
    $normalEntries = @(& $systemTar -tf $normalOutput)
    Assert-ArchiveSanitizerCheck `
        ($LASTEXITCODE -eq 0 -and
         'Default/Favicons' -in $normalEntries -and
         @($normalEntries | Where-Object { $_ -eq './' -or $_ -like './*' }).Count -eq 0) `
        'The hardened wrapper emitted a non-canonical archive.'

    $inPlaceArchive = Join-Path $testRoot 'InPlace.7z'
    [IO.File]::Copy($validInput, $inPlaceArchive)
    $inPlaceResult = & $sanitizer -InputArchive $inPlaceArchive `
        -OutputArchive $inPlaceArchive
    Assert-ArchiveSanitizerCheck `
        ($inPlaceResult.Archive -eq $inPlaceArchive -and
         (Test-Path -LiteralPath $inPlaceArchive -PathType Leaf)) `
        'InputArchive == OutputArchive did not commit safely.'
    $inPlaceEntries = @(& $systemTar -tf $inPlaceArchive)
    Assert-ArchiveSanitizerCheck `
        ($LASTEXITCODE -eq 0 -and 'Default/Favicons' -in $inPlaceEntries) `
        'The in-place archive is not readable after replacement.'

    $emptyArchive = Join-Path $testRoot 'Empty.zip'
    New-TestZip -Path $emptyArchive -Entries @()
    Assert-WrapperRejects -InputArchive $emptyArchive `
        -OutputArchive (Join-Path $testRoot 'EmptyOutput.7z') `
        -Message 'An empty input archive was accepted.'

    $traversalArchive = Join-Path $testRoot 'Traversal.zip'
    New-TestZip -Path $traversalArchive -Entries @(
        [pscustomobject]@{ Path = '../escape.txt'; Content = 'escape' }
    )
    $preservedOutput = Join-Path $testRoot 'PreservedOutput.7z'
    [IO.File]::WriteAllText($preservedOutput, 'existing-output')
    $preservedBytes = [Convert]::ToBase64String(
        [IO.File]::ReadAllBytes($preservedOutput))
    Assert-WrapperRejects -InputArchive $traversalArchive `
        -OutputArchive $preservedOutput `
        -Message 'A parent-traversal archive was accepted.'
    Assert-ArchiveSanitizerCheck `
        ([Convert]::ToBase64String(
            [IO.File]::ReadAllBytes($preservedOutput)) -ceq $preservedBytes) `
        'A failed sanitization changed the existing output.'

    $absoluteArchive = Join-Path $testRoot 'Absolute.zip'
    New-TestZip -Path $absoluteArchive -Entries @(
        [pscustomobject]@{ Path = 'C:/outside.txt'; Content = 'outside' }
    )
    Assert-WrapperRejects -InputArchive $absoluteArchive `
        -OutputArchive (Join-Path $testRoot 'AbsoluteOutput.7z') `
        -Message 'A drive-absolute archive path was accepted.'

    $duplicateArchive = Join-Path $testRoot 'Duplicate.zip'
    New-TestZip -Path $duplicateArchive -Entries @(
        [pscustomobject]@{ Path = 'Default/Bookmarks'; Content = 'one' },
        [pscustomobject]@{ Path = 'default/bookmarks'; Content = 'two' }
    )
    Assert-WrapperRejects -InputArchive $duplicateArchive `
        -OutputArchive (Join-Path $testRoot 'DuplicateOutput.7z') `
        -Message 'Case-insensitive duplicate file paths were accepted.'

    $deviceArchive = Join-Path $testRoot 'SuperscriptDevice.zip'
    New-TestZip -Path $deviceArchive -Entries @(
        [pscustomobject]@{
            Path = "Default/COM$([char]0x00B9).txt"
            Content = 'device'
        },
        [pscustomobject]@{
            Path = "Default/LPT$([char]0x00B2)"
            Content = 'device'
        }
    )
    Assert-WrapperRejects -InputArchive $deviceArchive `
        -OutputArchive (Join-Path $testRoot 'DeviceOutput.7z') `
        -Message 'A superscript COM/LPT Windows device name was accepted.' `
        -ErrorPattern 'unsafe on Windows'

    $oversizedArchive = Join-Path $testRoot 'OversizedDeclared.zip'
    New-DeclaredSizeZip -Path $oversizedArchive `
        -EntryName 'Default/huge.bin' -DeclaredBytes ([uint32](2GB + 1))
    Assert-ArchiveSanitizerCheck `
        ((Get-Item -LiteralPath $oversizedArchive).Length -lt 1KB) `
        'The oversized declared-length fixture unexpectedly contains large data.'
    Assert-WrapperRejects -InputArchive $oversizedArchive `
        -OutputArchive (Join-Path $testRoot 'OversizedOutput.7z') `
        -Message 'An oversized declared uncompressed entry was accepted.' `
        -ErrorPattern 'uncompressed size limit'

    $emptyPathArchive = Join-Path $testRoot 'EmptyPath.zip'
    New-TestZip -Path $emptyPathArchive -Entries @(
        [pscustomobject]@{ Path = './'; Content = '' }
    )
    Assert-WrapperRejects -InputArchive $emptyPathArchive `
        -OutputArchive (Join-Path $testRoot 'EmptyPathOutput.7z') `
        -Message 'An archive entry that normalizes empty was accepted.'

    $badInPlace = Join-Path $testRoot 'BadInPlace.zip'
    [IO.File]::Copy($traversalArchive, $badInPlace)
    $badInPlaceHash = (Get-FileHash -Algorithm SHA256 `
        -LiteralPath $badInPlace).Hash
    Assert-WrapperRejects -InputArchive $badInPlace `
        -OutputArchive $badInPlace `
        -Message 'An unsafe in-place archive was accepted.'
    Assert-ArchiveSanitizerCheck `
        ((Get-FileHash -Algorithm SHA256 -LiteralPath $badInPlace).Hash `
            -ceq $badInPlaceHash) `
        'A rejected in-place sanitization changed its input archive.'

    $lockedOutput = Join-Path $testRoot 'LockedOutput.7z'
    [IO.File]::WriteAllText($lockedOutput, 'locked-existing-output')
    $lockedBytes = [Convert]::ToBase64String(
        [IO.File]::ReadAllBytes($lockedOutput))
    $lock = [IO.File]::Open(
        $lockedOutput, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        [IO.FileShare]::None)
    try {
        Assert-WrapperRejects -InputArchive $validInput `
            -OutputArchive $lockedOutput `
            -Message 'Replacement unexpectedly succeeded against a locked output.'
    }
    finally {
        $lock.Dispose()
    }
    Assert-ArchiveSanitizerCheck `
        ([Convert]::ToBase64String(
            [IO.File]::ReadAllBytes($lockedOutput)) -ceq $lockedBytes) `
        'A failed atomic replacement changed the previous output.'

    Write-Host 'Default profile archive sanitizer checks passed.'
}
finally {
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
    if ($resolvedTestRoot.StartsWith(
            $tempRoot + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedTestRoot)) {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
