param(
    [Parameter(Mandatory = $false)]
    [string]$InputArchive = (Join-Path $PSScriptRoot '..\Default.7z'),

    [Parameter(Mandatory = $false)]
    [string]$OutputArchive = (Join-Path $PSScriptRoot '..\Default.sanitized.7z')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$maximumArchiveEntries = 100000
$maximumArchiveListingBytes = 64MB
$maximumArchiveEntryBytes = 2GB
$maximumArchiveUncompressedBytes = 4GB
$reservedDeviceNamePattern =
    '^(CON|PRN|AUX|NUL|CLOCK\$|COM(?:[1-9]|\u00B9|\u00B2|\u00B3)|LPT(?:[1-9]|\u00B9|\u00B2|\u00B3))(?:\..*)?$'

if (-not ('CtSpacesArchiveSanitizer.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace CtSpacesArchiveSanitizer
{
    public static class NativeMethods
    {
        [DllImport("kernel32.dll")]
        public static extern uint GetACP();
    }
}
'@
}

function Get-FullPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    return [IO.Path]::GetFullPath($Path)
}

function Get-RegularNonReparseFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Label
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Label is missing or is not a regular file: $Path"
    }
    $item = Get-Item -LiteralPath $Path -Force
    if ($item.PSIsContainer -or
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "$Label must be a regular non-reparse file: $Path"
    }
    return $item
}

function Get-SafeArchivePath {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Entry)

    if ([string]::IsNullOrWhiteSpace($Entry) -or
        $Entry.IndexOf([char]0) -ge 0) {
        throw 'The archive contains an empty or NUL entry name.'
    }

    $slashes = $Entry.Replace([char]92, [char]47)
    if ($slashes.StartsWith('/', [StringComparison]::Ordinal) -or
        $slashes.StartsWith('//', [StringComparison]::Ordinal) -or
        $slashes -match '^[A-Za-z]:' -or
        [IO.Path]::IsPathRooted($Entry)) {
        throw "The archive contains an absolute, UNC, or drive path: $Entry"
    }

    $safeSegments = [Collections.Generic.List[string]]::new()
    foreach ($segment in $slashes.Split([char]47)) {
        if ([string]::IsNullOrEmpty($segment) -or $segment -ceq '.') {
            continue
        }
        if ($segment -ceq '..') {
            throw "The archive contains a parent traversal path: $Entry"
        }
        if ($segment -match '[\x00-\x1f<>:"|?*]' -or
            $segment.EndsWith(' ', [StringComparison]::Ordinal) -or
            $segment.EndsWith('.', [StringComparison]::Ordinal) -or
            $segment -match $reservedDeviceNamePattern) {
            throw "The archive contains a path that is unsafe on Windows: $Entry"
        }
        $safeSegments.Add($segment)
    }
    if ($safeSegments.Count -eq 0) {
        throw "The archive entry normalizes to an empty path: $Entry"
    }
    return [string]::Join('/', $safeSegments)
}

function ConvertTo-NativeCommandLineArgument {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value)

    # ProcessStartInfo.ArgumentList is unavailable in Windows PowerShell 5.1.
    # Quote one argument according to CommandLineToArgvW rules instead.
    $quoted = [Text.StringBuilder]::new()
    [void]$quoted.Append([char]34)
    $backslashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq [char]92) {
            $backslashes++
            continue
        }
        if ($character -eq [char]34) {
            [void]$quoted.Append(([string][char]92) * (2 * $backslashes + 1))
            [void]$quoted.Append([char]34)
        }
        else {
            if ($backslashes -gt 0) {
                [void]$quoted.Append(([string][char]92) * $backslashes)
            }
            [void]$quoted.Append($character)
        }
        $backslashes = 0
    }
    if ($backslashes -gt 0) {
        [void]$quoted.Append(([string][char]92) * (2 * $backslashes))
    }
    [void]$quoted.Append([char]34)
    return $quoted.ToString()
}

function Invoke-TarListing {
    param(
        [Parameter(Mandatory = $true)][string]$TarPath,
        [Parameter(Mandatory = $true)][ValidateSet('-tf', '-tvf')]
        [string]$ListMode,
        [Parameter(Mandatory = $true)][string]$ArchivePath
    )

    # Windows bsdtar writes filenames in the active ANSI code page, even when
    # PowerShell's native-output encoding is UTF-8. Capture bytes directly so
    # characters such as the Windows device aliases COM¹/LPT² are not replaced
    # before validation. Invalid byte sequences fail closed.
    $codePage = [int][CtSpacesArchiveSanitizer.NativeMethods]::GetACP()
    $encoding = [Text.Encoding]::GetEncoding(
        $codePage,
        [Text.EncoderExceptionFallback]::new(),
        [Text.DecoderExceptionFallback]::new())
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $TarPath
    $startInfo.Arguments = $ListMode + ' ' +
        (ConvertTo-NativeCommandLineArgument $ArchivePath)
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.StandardErrorEncoding = $encoding

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $bytes = [IO.MemoryStream]::new()
    $started = $false
    try {
        if (-not $process.Start()) {
            throw 'Could not start System tar.exe to inspect the archive.'
        }
        $started = $true
        $errorTask = $process.StandardError.ReadToEndAsync()
        $buffer = [byte[]]::new(8192)
        while (($read = $process.StandardOutput.BaseStream.Read(
                    $buffer, 0, $buffer.Length)) -gt 0) {
            if (($bytes.Length + $read) -gt $maximumArchiveListingBytes) {
                throw 'The archive listing exceeds the safe inspection limit.'
            }
            $bytes.Write($buffer, 0, $read)
        }
        $process.WaitForExit()
        $errorText = $errorTask.GetAwaiter().GetResult()
        try {
            $outputText = $encoding.GetString($bytes.ToArray())
        }
        catch [Text.DecoderFallbackException] {
            throw 'The archive listing contains a filename that cannot be decoded safely.'
        }

        $lines = @()
        if ($outputText.Length -gt 0) {
            $splitLines = [regex]::Split($outputText, "`r`n|`n|`r")
            if ($splitLines.Count -gt 0 -and
                $splitLines[$splitLines.Count - 1].Length -eq 0) {
                $lines = @($splitLines[0..($splitLines.Count - 2)])
            }
            else {
                $lines = @($splitLines)
            }
        }
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Lines = $lines
            Error = $errorText
        }
    }
    finally {
        if ($started) {
            try {
                if (-not $process.HasExited) {
                    $process.Kill()
                    $process.WaitForExit()
                }
            }
            catch {
                # Preserve the validation failure that led to cleanup.
            }
        }
        $bytes.Dispose()
        $process.Dispose()
    }
}

function Get-ValidatedArchiveListing {
    param(
        [Parameter(Mandatory = $true)][string]$TarPath,
        [Parameter(Mandatory = $true)][string]$ArchivePath
    )

    $rawListing = Invoke-TarListing -TarPath $TarPath -ListMode '-tf' `
        -ArchivePath $ArchivePath
    $verboseListing = Invoke-TarListing -TarPath $TarPath -ListMode '-tvf' `
        -ArchivePath $ArchivePath
    $rawEntries = @($rawListing.Lines)
    $verboseEntries = @($verboseListing.Lines)
    if ($rawListing.ExitCode -ne 0) {
        throw "Could not list archive: $ArchivePath"
    }
    if ($rawEntries.Count -eq 0) {
        throw "The archive is empty: $ArchivePath"
    }
    if ($rawEntries.Count -gt $maximumArchiveEntries) {
        throw 'The archive contains too many entries.'
    }

    if ($verboseListing.ExitCode -ne 0 -or
        $verboseEntries.Count -ne $rawEntries.Count) {
        throw 'The archive entry types could not be verified safely.'
    }

    $entries = [Collections.Generic.List[string]]::new()
    $filePaths = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $seenKinds = [Collections.Generic.Dictionary[string, bool]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $declaredBytes = [uint64]0
    for ($index = 0; $index -lt $rawEntries.Count; $index++) {
        $verboseEntry = [string]$verboseEntries[$index]
        if ([string]::IsNullOrEmpty($verboseEntry)) {
            throw 'The archive contains an entry with an unverifiable type.'
        }
        $verboseMatch = [regex]::Match(
            $verboseEntry,
            '^(?<mode>\S+)\s+\d+\s+\S+\s+\S+\s+(?<size>[0-9]+)\s+')
        if (-not $verboseMatch.Success) {
            throw 'The archive contains an entry with an unverifiable declared size.'
        }
        $type = $verboseMatch.Groups['mode'].Value[0]
        if ($type -ne [char]'-' -and $type -ne [char]'d') {
            throw "The archive contains a link or unsupported entry type: $($rawEntries[$index])"
        }
        $isDirectory = $type -eq [char]'d'
        $entryBytes = [uint64]0
        if (-not [uint64]::TryParse(
                $verboseMatch.Groups['size'].Value, [ref]$entryBytes)) {
            throw 'The archive contains an invalid declared entry size.'
        }
        if ($entryBytes -gt [uint64]$maximumArchiveEntryBytes) {
            throw "The archive entry exceeds the uncompressed size limit: $($rawEntries[$index])"
        }
        if ($entryBytes -gt
            ([uint64]$maximumArchiveUncompressedBytes - $declaredBytes)) {
            throw 'The archive exceeds the total uncompressed size limit.'
        }
        $declaredBytes += $entryBytes
        $normalized = Get-SafeArchivePath ([string]$rawEntries[$index])

        $existingIsDirectory = $false
        if ($seenKinds.TryGetValue($normalized, [ref]$existingIsDirectory)) {
            if (-not $isDirectory -or -not $existingIsDirectory) {
                throw "The archive contains a case-insensitive duplicate file path: $normalized"
            }
            continue
        }
        $seenKinds.Add($normalized, $isDirectory)
        $entries.Add($normalized)
        if (-not $isDirectory) {
            [void]$filePaths.Add($normalized)
        }
    }

    foreach ($filePath in $filePaths) {
        $segments = $filePath.Split([char]47)
        $prefix = ''
        for ($index = 0; $index -lt ($segments.Count - 1); $index++) {
            $prefix = if ($index -eq 0) {
                $segments[$index]
            }
            else {
                $prefix + '/' + $segments[$index]
            }
            if ($filePaths.Contains($prefix)) {
                throw "The archive uses a file as a parent directory: $prefix"
            }
        }
    }

    return [pscustomobject]@{
        Entries = $entries.ToArray()
        Files = $filePaths.Count
        DeclaredBytes = $declaredBytes
    }
}

function Assert-SafeExtractedTree {
    param([Parameter(Mandatory = $true)][string]$Root)

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw 'The archive extraction root is missing.'
    }
    $rootItem = Get-Item -LiteralPath $Root -Force
    if ($rootItem -isnot [IO.DirectoryInfo] -or
        ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'The archive extraction root is not a real directory.'
    }

    $pending = [Collections.Generic.Stack[IO.DirectoryInfo]]::new()
    $pending.Push($rootItem)
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        foreach ($item in $directory.EnumerateFileSystemInfos()) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "The archive extracted a reparse point: $($item.FullName)"
            }
            if ($item -is [IO.DirectoryInfo]) {
                $pending.Push($item)
            }
            elseif ($item -isnot [IO.FileInfo]) {
                throw "The archive extracted a non-file/non-directory item: $($item.FullName)"
            }
        }
    }
}

function Get-ValidatedOutputDirectory {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (Test-Path -LiteralPath $Path) {
        if (-not (Test-Path -LiteralPath $Path -PathType Container)) {
            throw "The archive output directory path is not a directory: $Path"
        }
    }
    else {
        New-Item -ItemType Directory -Path $Path | Out-Null
    }
    $item = Get-Item -LiteralPath $Path -Force
    if ($item -isnot [IO.DirectoryInfo] -or
        ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'The archive output directory must be a real non-reparse directory.'
    }
    return $item.FullName
}

$tarPath = Join-Path ([Environment]::SystemDirectory) 'tar.exe'
$tarItem = Get-RegularNonReparseFile -Path $tarPath -Label 'System tar.exe'
$directorySanitizer = Get-FullPath (
    (Join-Path $PSScriptRoot 'Sanitize-DefaultProfileDirectory.ps1'))
$sanitizerItem = Get-RegularNonReparseFile -Path $directorySanitizer `
    -Label 'Shared Default-profile sanitizer'
$inputItem = Get-RegularNonReparseFile -Path (
    (Get-FullPath $InputArchive)) -Label 'Input archive'
$inputPath = $inputItem.FullName
$outputPath = Get-FullPath $OutputArchive

if (Test-Path -LiteralPath $outputPath) {
    [void](Get-RegularNonReparseFile -Path $outputPath -Label 'Existing output archive')
}

[void](Get-ValidatedArchiveListing -TarPath $tarItem.FullName `
    -ArchivePath $inputPath)

$workRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ('ctSpaces-template-' + [guid]::NewGuid().ToString('N'))
$extractRoot = Join-Path $workRoot 'source'
$cleanRoot = Join-Path $workRoot 'clean'
$sourceDefault = Join-Path $extractRoot 'Default'
$stagedArchive = $null
$replacementBackup = $null

try {
    New-Item -ItemType Directory -Path $extractRoot | Out-Null
    & $tarItem.FullName -xf $inputPath -C $extractRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Could not extract $inputPath"
    }
    Assert-SafeExtractedTree $extractRoot
    if (-not (Test-Path -LiteralPath $sourceDefault -PathType Container)) {
        throw 'The archive does not contain a Default profile directory.'
    }

    $sanitizerResult = & $sanitizerItem.FullName `
        -SourceProfile $extractRoot -OutputRoot $cleanRoot
    if (-not (Test-Path -LiteralPath (Join-Path $cleanRoot 'Default') `
                                    -PathType Container)) {
        throw 'The shared sanitizer did not create a Default profile payload.'
    }

    $outputDirectory = Get-ValidatedOutputDirectory `
        (Split-Path -Parent $outputPath)
    $outputLeaf = [IO.Path]::GetFileName($outputPath)
    if ([string]::IsNullOrWhiteSpace($outputLeaf)) {
        throw 'The output archive path has no file name.'
    }
    $stagedArchive = Join-Path $outputDirectory `
        ('.' + $outputLeaf + '.ctspaces-' +
         [guid]::NewGuid().ToString('N') + '.staged.7z')

    & $tarItem.FullName -a -cf $stagedArchive -C $cleanRoot Default
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not create the sanitized archive.'
    }
    $stagedItem = Get-RegularNonReparseFile -Path $stagedArchive `
        -Label 'Staged sanitized archive'
    $listing = Get-ValidatedArchiveListing -TarPath $tarItem.FullName `
        -ArchivePath $stagedItem.FullName

    $requiredEntries = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::Ordinal)
    foreach ($requiredEntry in @(
        'Default/Bookmarks',
        'Default/Favicons',
        'Default/Preferences',
        'Default/Secure Preferences'
    )) {
        [void]$requiredEntries.Add($requiredEntry)
    }
    foreach ($entry in $listing.Entries) {
        if ($entry -cmatch
            '^Default/(Bookmarks\.bak|Favicons-(journal|wal|shm))($|/)') {
            throw "Derived or backup browser data remains: $entry"
        }
        $allowed =
            $entry -ceq 'Default' -or
            $entry -cmatch
                '^Default/(Bookmarks|Favicons|Preferences|Secure Preferences)$' -or
            $entry -ceq 'Default/Extensions' -or
            $entry -cmatch '^Default/Extensions/[a-p]{32}(/.*)?$'
        if (-not $allowed) {
            throw "Unexpected starter-template entry remains: $entry"
        }
        [void]$requiredEntries.Remove($entry)
    }
    if ($requiredEntries.Count -ne 0) {
        throw "Required starter-template entry is missing: $($requiredEntries -join ', ')"
    }

    $stagedBytes = $stagedItem.Length
    $stagedHash = (Get-FileHash -Algorithm SHA256 `
        -LiteralPath $stagedItem.FullName).Hash

    if (Test-Path -LiteralPath $outputPath) {
        [void](Get-RegularNonReparseFile -Path $outputPath `
            -Label 'Existing output archive')
        $replacementBackup = Join-Path $outputDirectory `
            ('.' + $outputLeaf + '.ctspaces-' +
             [guid]::NewGuid().ToString('N') + '.previous')
        [IO.File]::Replace(
            $stagedItem.FullName, $outputPath, $replacementBackup)
        $stagedArchive = $null
        if (Test-Path -LiteralPath $replacementBackup) {
            try {
                $backupItem = Get-RegularNonReparseFile `
                    -Path $replacementBackup -Label 'Replaced output backup'
                Remove-Item -LiteralPath $backupItem.FullName -Force
            }
            catch {
                Write-Warning `
                    "The previous output remains at $replacementBackup"
            }
        }
        $replacementBackup = $null
    }
    else {
        [IO.File]::Move($stagedItem.FullName, $outputPath)
        $stagedArchive = $null
    }

    [pscustomobject]@{
        Archive = $outputPath
        Bytes = $stagedBytes
        Entries = $listing.Entries.Count
        Extensions = $sanitizerResult.Extensions
        SHA256 = $stagedHash
    }
}
finally {
    if ($stagedArchive -and (Test-Path -LiteralPath $stagedArchive)) {
        $stagedCleanupItem = Get-Item -LiteralPath $stagedArchive -Force
        if ($stagedCleanupItem -is [IO.FileInfo] -and
            ($stagedCleanupItem.Attributes -band
             [IO.FileAttributes]::ReparsePoint) -eq 0) {
            Remove-Item -LiteralPath $stagedCleanupItem.FullName -Force
        }
    }
    if (Test-Path -LiteralPath $workRoot) {
        $workItem = Get-Item -LiteralPath $workRoot -Force
        $tempRoot = (Get-FullPath ([IO.Path]::GetTempPath())).TrimEnd('\', '/')
        $workFullPath = Get-FullPath $workItem.FullName
        if ($workItem -is [IO.DirectoryInfo] -and
            ($workItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0 -and
            $workFullPath.StartsWith(
                $tempRoot + [IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) {
            Remove-Item -LiteralPath $workFullPath -Recurse -Force
        }
    }
}
