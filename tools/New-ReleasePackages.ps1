[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
    [string]$Version,
    [Alias('OutputDirectory')]
    [string]$ReleaseDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$versionHeader = Join-Path $projectRoot 'version.h'

function Get-ConfiguredVersion {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Missing version header: $Path"
    }
    $text = [IO.File]::ReadAllText($Path)
    $textMatch = [regex]::Match(
        $text, '(?m)^\s*#define\s+CTSPACES_VERSION_TEXT\s+"(\d+\.\d+\.\d+\.\d+)"\s*$')
    $numberMatch = [regex]::Match(
        $text, '(?m)^\s*#define\s+CTSPACES_VERSION_NUMBER\s+(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*$')
    if (-not $textMatch.Success -or -not $numberMatch.Success) {
        throw 'version.h does not contain both expected four-part version definitions.'
    }
    $numberVersion = '{0}.{1}.{2}.{3}' -f $numberMatch.Groups[1].Value,
        $numberMatch.Groups[2].Value, $numberMatch.Groups[3].Value,
        $numberMatch.Groups[4].Value
    if ($textMatch.Groups[1].Value -cne $numberVersion) {
        throw "The numeric and text versions in version.h disagree: '$numberVersion' and '$($textMatch.Groups[1].Value)'."
    }
    return $numberVersion
}

function Get-StreamSha256 {
    param([IO.Stream]$Stream)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString(
                $algorithm.ComputeHash($Stream))).Replace('-', '')
    } finally {
        $algorithm.Dispose()
    }
}

$configuredVersion = Get-ConfiguredVersion $versionHeader
if ([string]::IsNullOrWhiteSpace($Version)) { $Version = $configuredVersion }
if ($Version -cne $configuredVersion) {
    throw "Requested version '$Version' does not match version.h '$configuredVersion'."
}
if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
    $ReleaseDirectory = Join-Path $projectRoot 'dist\x64\Release'
}
$releasePath = [IO.Path]::GetFullPath($ReleaseDirectory)
if (Test-Path -LiteralPath $releasePath -PathType Leaf) {
    throw "Release output directory is a file: $releasePath"
}

$exePath = Join-Path $projectRoot 'dist\x64\Release\ctSpaces.exe'
if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
    throw "Missing release executable: $exePath"
}
$exeInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($exePath)
$exeVersion = '{0}.{1}.{2}.{3}' -f $exeInfo.FileMajorPart,
    $exeInfo.FileMinorPart, $exeInfo.FileBuildPart, $exeInfo.FilePrivatePart
if ($exeVersion -cne $Version -or $exeInfo.FileVersion -cne $Version) {
    throw "Executable file version '$($exeInfo.FileVersion)' does not match requested/configured version '$Version'."
}

$archiveName = 'ctSpaces{0}.zip' -f $Version
$archivePath = Join-Path $releasePath $archiveName
if (Test-Path -LiteralPath $archivePath) {
    throw "Refusing to overwrite release output: $archivePath"
}
if (-not (Test-Path -LiteralPath $releasePath -PathType Container)) {
    New-Item -ItemType Directory -Path $releasePath -Force | Out-Null
}
$stagingPath = Join-Path $releasePath (
    '.ctSpaces{0}.staging-{1}' -f $Version, [Guid]::NewGuid().ToString('N'))
$stagedArchive = Join-Path $stagingPath $archiveName

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

try {
    New-Item -ItemType Directory -Path $stagingPath | Out-Null
    $stream = [IO.FileStream]::new(
        $stagedArchive, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write,
        [IO.FileShare]::None)
    try {
        $archive = [IO.Compression.ZipArchive]::new(
            $stream, [IO.Compression.ZipArchiveMode]::Create, $true)
        try {
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $archive, $exePath, 'ctSpaces.exe',
                [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        } finally {
            $archive.Dispose()
        }
    } finally {
        $stream.Dispose()
    }

    $exeHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
    $archive = [IO.Compression.ZipFile]::OpenRead($stagedArchive)
    try {
        if ($archive.Entries.Count -ne 1 -or
            $archive.Entries[0].FullName -cne 'ctSpaces.exe') {
            $names = @($archive.Entries | ForEach-Object FullName) -join ', '
            throw "Runtime ZIP must contain exactly one root entry named 'ctSpaces.exe'; found: $names"
        }
        $entryStream = $archive.Entries[0].Open()
        try { $entryHash = Get-StreamSha256 $entryStream } finally { $entryStream.Dispose() }
        if ($entryHash -cne $exeHash) {
            throw "ZIP entry SHA-256 '$entryHash' does not match executable SHA-256 '$exeHash'."
        }
    } finally {
        $archive.Dispose()
    }

    if (Test-Path -LiteralPath $archivePath) {
        throw "Release output appeared during packaging; refusing to overwrite it: $archivePath"
    }
    [IO.File]::Move($stagedArchive, $archivePath)
    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    [pscustomobject]@{
        Version = $Version
        Archive = $archivePath
        ArchiveEntries = 1
        ExecutableSHA256 = $exeHash
        ArchiveSHA256 = $archiveHash
        Verified = $true
    }
} finally {
    if (Test-Path -LiteralPath $stagingPath) {
        $resolvedStaging = [IO.Path]::GetFullPath($stagingPath)
        $expectedPrefix = $releasePath.TrimEnd('\') + '\.ctSpaces'
        if ($resolvedStaging.StartsWith($expectedPrefix,
                [StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $resolvedStaging) -like '.ctSpaces*.staging-*') {
            Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
        } else {
            Write-Warning "Refusing to remove unexpected staging path: $resolvedStaging"
        }
    }
}
