[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+\.\d+\.\d+$')]
    [string]$Version,

    [Alias('OutputDirectory')]
    [string]$ReleaseDirectory,

    [switch]$IncludeSource
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$versionHeader = Join-Path $projectRoot 'version.h'
$packageDate = Get-Date -Format 'yyyyMMdd'

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

$configuredVersion = Get-ConfiguredVersion $versionHeader
if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = $configuredVersion
}
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

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Get-RelativeArchiveName {
    param([string]$Path)

    $full = [IO.Path]::GetFullPath($Path)
    $prefix = $projectRoot.TrimEnd('\') + '\'
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Package source is outside the project root: $full"
    }
    return $full.Substring($prefix.Length).Replace('\', '/')
}

function Assert-SafeArchiveName {
    param(
        [string]$Name,
        [switch]$SourceArchive
    )

    if ([string]::IsNullOrWhiteSpace($Name) -or $Name.Contains('\') -or
        $Name.StartsWith('/') -or $Name -match '^[A-Za-z]:' -or
        $Name.IndexOf([char]0) -ge 0) {
        throw "Unsafe archive entry name: '$Name'"
    }
    $segments = $Name.Split('/')
    if ($segments.Count -eq 0 -or
        @($segments | Where-Object { $_ -eq '' -or $_ -eq '.' -or $_ -eq '..' }).Count -ne 0) {
        throw "Archive entry contains an empty or traversal segment: '$Name'"
    }
    if ($Name -match '(?i)(^|/)(build|dist|\.git|backups|VM_HANDOFF)(/|$)' -or
        $Name -match '(?i)(^|/)(failure-diagnostics\.txt|failure\.log)$') {
        throw "Private or generated path is not allowed in an archive: '$Name'"
    }
    if ($SourceArchive -and $Name -ne 'Default.7z' -and
        $Name -match '(?i)\.(exe|dll|pdb|obj|lib|exp|ilk|ipdb|iobj|tlog|log|dmp|zip|7z|nupkg)$') {
        throw "Generated binary or log is not allowed in the source archive: '$Name'"
    }
}

function Test-ReparsePoint {
    param([IO.FileSystemInfo]$Item)
    return (($Item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
}

function Get-SafeTreeFiles {
    param([string]$Root)

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw "Missing package source directory: $Root"
    }
    $excludedDirectories = @{
        'build' = $true; 'dist' = $true; '.git' = $true; '.vs' = $true
        '.idea' = $true; '__pycache__' = $true; 'node_modules' = $true
    }
    $generatedExtensions = @{
        '.exe' = $true; '.dll' = $true; '.pdb' = $true; '.obj' = $true
        '.lib' = $true; '.exp' = $true; '.ilk' = $true; '.ipdb' = $true
        '.iobj' = $true; '.tlog' = $true; '.log' = $true; '.dmp' = $true
        '.zip' = $true; '.7z' = $true; '.nupkg' = $true
    }
    $pending = [Collections.Generic.Stack[IO.DirectoryInfo]]::new()
    $pending.Push([IO.DirectoryInfo]::new([IO.Path]::GetFullPath($Root)))
    while ($pending.Count -gt 0) {
        $directory = $pending.Pop()
        foreach ($item in $directory.GetFileSystemInfos()) {
            if (Test-ReparsePoint $item) { continue }
            if ($item -is [IO.DirectoryInfo]) {
                if (-not $excludedDirectories.ContainsKey($item.Name)) {
                    $pending.Push($item)
                }
                continue
            }
            if ($generatedExtensions.ContainsKey($item.Extension) -or
                $item.Name.Equals('.git', [StringComparison]::OrdinalIgnoreCase) -or
                $item.Name.EndsWith('.user', [StringComparison]::OrdinalIgnoreCase) -or
                $item.Name.EndsWith('.suo', [StringComparison]::OrdinalIgnoreCase) -or
                $item.Name.EndsWith('.cache', [StringComparison]::OrdinalIgnoreCase)) {
                continue
            }
            $item
        }
    }
}

function New-PackageMap {
    return ,([Collections.Generic.List[object]]::new())
}

function Add-PackageFile {
    param(
        [Collections.Generic.List[object]]$Map,
        [string]$Source,
        [string]$Entry,
        [switch]$SourceArchive
    )

    $sourcePath = [IO.Path]::GetFullPath($Source)
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Required package source is missing: $sourcePath"
    }
    $sourceItem = Get-Item -LiteralPath $sourcePath -Force
    if (Test-ReparsePoint $sourceItem) {
        throw "Refusing to package a reparse-point file: $sourcePath"
    }
    Assert-SafeArchiveName -Name $Entry -SourceArchive:$SourceArchive
    if (@($Map | Where-Object { $_.Entry.Equals(
                    $Entry, [StringComparison]::OrdinalIgnoreCase) }).Count -ne 0) {
        throw "Duplicate archive entry requested: $Entry"
    }
    $Map.Add([pscustomobject]@{ Source = $sourcePath; Entry = $Entry })
}

function Add-ProjectTree {
    param(
        [Collections.Generic.List[object]]$Map,
        [string]$RelativeRoot
    )
    $absoluteRoot = Join-Path $projectRoot $RelativeRoot
    foreach ($file in @(Get-SafeTreeFiles $absoluteRoot)) {
        Add-PackageFile -Map $Map -Source $file.FullName `
            -Entry (Get-RelativeArchiveName $file.FullName) -SourceArchive
    }
}

function New-ZipFromMap {
    param(
        [Collections.Generic.List[object]]$Map,
        [string]$Destination
    )

    if (Test-Path -LiteralPath $Destination) {
        throw "Refusing to overwrite archive: $Destination"
    }
    $stream = [IO.FileStream]::new(
        $Destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write,
        [IO.FileShare]::None)
    try {
        $archive = [IO.Compression.ZipArchive]::new(
            $stream, [IO.Compression.ZipArchiveMode]::Create, $true)
        try {
            foreach ($item in @($Map | Sort-Object Entry)) {
                [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $archive, $item.Source, $item.Entry,
                    [IO.Compression.CompressionLevel]::Optimal) | Out-Null
            }
        } finally {
            $archive.Dispose()
        }
    } finally {
        $stream.Dispose()
    }
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

function Test-ZipAgainstMap {
    param(
        [string]$ZipPath,
        [Collections.Generic.List[object]]$Map,
        [switch]$SourceArchive
    )

    $expected = [Collections.Generic.Dictionary[string, object]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($item in $Map) { $expected.Add($item.Entry, $item) }
    $observed = [Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    $archive = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        foreach ($entry in $archive.Entries) {
            Assert-SafeArchiveName -Name $entry.FullName -SourceArchive:$SourceArchive
            if (-not $observed.Add($entry.FullName)) {
                throw "ZIP contains a duplicate entry: $($entry.FullName)"
            }
            if (-not $expected.ContainsKey($entry.FullName)) {
                throw "ZIP contains an unmapped entry: $($entry.FullName)"
            }
            $entryStream = $entry.Open()
            try { $entryHash = Get-StreamSha256 $entryStream } finally { $entryStream.Dispose() }
            $sourceHash = (Get-FileHash -LiteralPath $expected[$entry.FullName].Source `
                -Algorithm SHA256).Hash
            if ($entryHash -cne $sourceHash) {
                throw "ZIP entry differs from its mapped source: $($entry.FullName)"
            }
        }
    } finally {
        $archive.Dispose()
    }
    if ($observed.Count -ne $expected.Count) {
        $missing = @($expected.Keys | Where-Object { -not $observed.Contains($_) })
        throw "ZIP is missing mapped entries: $($missing -join ', ')"
    }
    return ,$observed
}

function Assert-RequiredEntries {
    param(
        [Collections.Generic.HashSet[string]]$Entries,
        [string[]]$Required
    )
    $missing = @($Required | Where-Object { -not $Entries.Contains($_) })
    if ($missing.Count -ne 0) {
        throw "Archive is missing required entries: $($missing -join ', ')"
    }
}

function Assert-ProjectDependencies {
    param([Collections.Generic.HashSet[string]]$SourceEntries)

    foreach ($projectEntry in @($SourceEntries | Where-Object {
                $_.EndsWith('.vcxproj', [StringComparison]::OrdinalIgnoreCase)
            })) {
        $projectPath = Join-Path $projectRoot $projectEntry.Replace('/', '\')
        [xml]$xml = [IO.File]::ReadAllText($projectPath)
        $nodes = $xml.SelectNodes(
            '//*[local-name()="ClCompile" or local-name()="ClInclude" or ' +
            'local-name()="ResourceCompile" or local-name()="Image" or ' +
            'local-name()="None" or local-name()="ProjectReference" or ' +
            'local-name()="CustomBuild"][@Include]')
        foreach ($node in $nodes) {
            $include = [string]$node.Include
            if ($include -match '[\$%\*\?]') { continue }
            $dependency = [IO.Path]::GetFullPath((Join-Path (
                        Split-Path -Parent $projectPath) $include))
            if (-not (Test-Path -LiteralPath $dependency -PathType Leaf)) { continue }
            $entry = Get-RelativeArchiveName $dependency
            if (-not $SourceEntries.Contains($entry)) {
                throw "Source ZIP omitted project dependency '$entry' referenced by '$projectEntry'."
            }
        }
    }
}

$releaseNotesName = 'RELEASE_NOTES_{0}.md' -f $Version
$validationName = 'RELEASE_VALIDATION_{0}.md' -f $Version
$versionedDocuments = @(
    @((Join-Path $projectRoot $releaseNotesName), $Version),
    @((Join-Path $projectRoot 'USER_CHANGELOG.md'), $Version),
    @((Join-Path $projectRoot ('docs\' + $validationName)), $Version),
    @((Join-Path $projectRoot 'docs\DISTRIBUTION_README.txt'), $validationName)
)
foreach ($check in $versionedDocuments) {
    if (-not (Test-Path -LiteralPath $check[0] -PathType Leaf) -or
        [IO.File]::ReadAllText($check[0]).IndexOf(
            $check[1], [StringComparison]::Ordinal) -lt 0) {
        throw "Versioned package document '$($check[0])' does not reference '$($check[1])'."
    }
}
$sourceMap = New-PackageMap
$requiredSourceRoot = @()
if ($IncludeSource) {
    $rootExtensions = @('.cpp', '.h', '.rc', '.sln', '.vcxproj', '.filters')
    $rootFiles = @(Get-ChildItem -LiteralPath $projectRoot -File -Force | Where-Object {
            $rootExtensions -contains $_.Extension -or
            $_.Name -in @('.gitignore', '.gitmodules', 'LICENSE', 'README.md',
                'CONTRIBUTING.md', 'Default.7z', 'USER_CHANGELOG.md') -or
            $_.Name -like 'RELEASE_NOTES_*.md'
        })
    foreach ($file in $rootFiles) {
        Add-PackageFile -Map $sourceMap -Source $file.FullName -Entry $file.Name `
            -SourceArchive
    }
    foreach ($tree in @('icons', 'tools', 'tests', 'docs', '3p\7zip')) {
        Add-ProjectTree -Map $sourceMap -RelativeRoot $tree
    }
    foreach ($file in @(Get-ChildItem -LiteralPath (Join-Path $projectRoot '7zip') `
                -File -Force | Where-Object {
                    $_.Name -eq 'README.md' -or
                    $_.Extension -in @('.vcxproj', '.filters')
                })) {
        Add-PackageFile -Map $sourceMap -Source $file.FullName `
            -Entry (Get-RelativeArchiveName $file.FullName) -SourceArchive
    }

    $requiredSourceRoot = @(
        'ctSpaces.cpp', 'ctSpaces.rc', 'ctSpaces.sln', 'ctSpaces.vcxproj',
        'ctSpaces.vcxproj.filters', 'version.h', 'LICENSE', 'README.md',
        'CONTRIBUTING.md', '.gitignore', '.gitmodules', 'Default.7z',
        'USER_CHANGELOG.md', $releaseNotesName,
        '7zip/Format7z.vcxproj', '7zip/Format7z.vcxproj.filters', '7zip/README.md',
        'tools/New-ReleasePackages.ps1', 'docs/GUIDED_WALKTHROUGH.md',
        ('docs/{0}' -f $validationName)
    )
    foreach ($required in $requiredSourceRoot) {
        if (@($sourceMap | Where-Object { $_.Entry.Equals(
                        $required, [StringComparison]::OrdinalIgnoreCase) }).Count -eq 0) {
            throw "Required source-package entry is unavailable: $required"
        }
    }
}

$colleagueMap = New-PackageMap
Add-PackageFile -Map $colleagueMap -Source $exePath -Entry 'ctSpaces.exe'
Add-PackageFile -Map $colleagueMap `
    -Source (Join-Path $projectRoot 'docs\DISTRIBUTION_README.txt') `
    -Entry 'START_HERE.txt'
foreach ($rootDocument in @('LICENSE', 'USER_CHANGELOG.md', $releaseNotesName)) {
    Add-PackageFile -Map $colleagueMap -Source (Join-Path $projectRoot $rootDocument) `
        -Entry $rootDocument
}
foreach ($document in @(
        'COLLEAGUE_QUICK_START.md', 'USER_GUIDE.md', 'GUIDED_WALKTHROUGH.md',
        'FEATURE_REFERENCE.md', 'INSTALLATION_AND_UPDATES.md',
        'DATA_BACKUP_AND_PRIVACY.md', 'TROUBLESHOOTING.md', $validationName)) {
    Add-PackageFile -Map $colleagueMap -Source (Join-Path $projectRoot ('docs\' + $document)) `
        -Entry ('docs/' + $document)
}
foreach ($license in @('License.txt', 'copying.txt', 'unRarLicense.txt')) {
    Add-PackageFile -Map $colleagueMap `
        -Source (Join-Path $projectRoot ('3p\7zip\DOC\' + $license)) `
        -Entry ('licenses/7zip/' + $license)
}

if (-not (Test-Path -LiteralPath $releasePath -PathType Container)) {
    New-Item -ItemType Directory -Path $releasePath -Force | Out-Null
}
$stagingPath = Join-Path $releasePath (
    '.ctSpaces{0}.staging-{1}' -f $Version,
    [Guid]::NewGuid().ToString('N'))
if (Test-Path -LiteralPath $stagingPath) {
    throw "Unexpected staging collision: $stagingPath"
}

$colleagueName = 'ctSpaces{0}.zip' -f $Version
$sourceName = 'ctSpaces{0}-source.zip' -f $Version
$checksumsName = 'ctSpaces{0}-SHA256SUMS.txt' -f $Version
$metadataName = 'ctSpaces{0}-PACKAGE_METADATA.txt' -f $Version
$outputNames = @($colleagueName)
if ($IncludeSource) { $outputNames += $sourceName }
$outputNames += @($checksumsName, $metadataName)
foreach ($name in $outputNames) {
    $outputPath = Join-Path $releasePath $name
    if (Test-Path -LiteralPath $outputPath) {
        throw "Refusing to overwrite release output: $outputPath"
    }
}

$completed = $false
try {
    New-Item -ItemType Directory -Path $stagingPath | Out-Null
    $colleagueZip = Join-Path $stagingPath $colleagueName
    New-ZipFromMap -Map $colleagueMap -Destination $colleagueZip

    $colleagueEntries = Test-ZipAgainstMap -ZipPath $colleagueZip -Map $colleagueMap
    Assert-RequiredEntries -Entries $colleagueEntries -Required @(
        'ctSpaces.exe', 'START_HERE.txt', 'LICENSE', 'USER_CHANGELOG.md',
        $releaseNotesName, 'docs/GUIDED_WALKTHROUGH.md',
        ('docs/{0}' -f $validationName), 'licenses/7zip/License.txt',
        'licenses/7zip/copying.txt', 'licenses/7zip/unRarLicense.txt')

    $exeHash = (Get-FileHash -LiteralPath $exePath -Algorithm SHA256).Hash
    $colleagueHash = (Get-FileHash -LiteralPath $colleagueZip -Algorithm SHA256).Hash
    $checksumLines = @(
        '{0}  ctSpaces.exe' -f $exeHash
        '{0}  {1}' -f $colleagueHash, $colleagueName
    )
    $metadataLines = @(
        'Version={0}' -f $Version
        'PackageDate={0}' -f $packageDate
        'CreatedUtc={0}' -f [DateTime]::UtcNow.ToString('o')
        'ExecutableFileVersion={0}' -f $exeInfo.FileVersion
        'ExecutableSHA256={0}' -f $exeHash
        'DistributionArchive={0}' -f $colleagueName
        'DistributionEntries={0}' -f $colleagueEntries.Count
        'DistributionBytes={0}' -f (Get-Item -LiteralPath $colleagueZip).Length
        'DistributionSHA256={0}' -f $colleagueHash
    )

    $sourceZip = $null
    $sourceEntries = $null
    $sourceHash = $null
    if ($IncludeSource) {
        $sourceZip = Join-Path $stagingPath $sourceName
        New-ZipFromMap -Map $sourceMap -Destination $sourceZip
        $sourceEntries = Test-ZipAgainstMap -ZipPath $sourceZip -Map $sourceMap `
            -SourceArchive
        Assert-RequiredEntries -Entries $sourceEntries -Required $requiredSourceRoot
        Assert-ProjectDependencies -SourceEntries $sourceEntries
        $sourceHash = (Get-FileHash -LiteralPath $sourceZip -Algorithm SHA256).Hash
        $checksumLines += '{0}  {1}' -f $sourceHash, $sourceName
        $metadataLines += @(
            'SourceArchive={0}' -f $sourceName
            'SourceEntries={0}' -f $sourceEntries.Count
            'SourceBytes={0}' -f (Get-Item -LiteralPath $sourceZip).Length
            'SourceSHA256={0}' -f $sourceHash
            'ProjectDependenciesVerified=True'
        )
    } else {
        $metadataLines += 'SourceArchive=NotCreated'
    }
    $metadataLines += 'AllArchiveEntriesVerified=True'

    $checksumLines | Set-Content -LiteralPath (Join-Path $stagingPath $checksumsName) `
        -Encoding ascii
    $metadataLines | Set-Content -LiteralPath (Join-Path $stagingPath $metadataName) `
        -Encoding utf8

    foreach ($name in $outputNames) {
        $outputPath = Join-Path $releasePath $name
        if (Test-Path -LiteralPath $outputPath) {
            throw "Release output appeared during packaging; refusing to overwrite it: $outputPath"
        }
    }
    foreach ($name in $outputNames) {
        [IO.File]::Move((Join-Path $stagingPath $name), (Join-Path $releasePath $name))
    }
    $completed = $true

    [pscustomobject]@{
        Version = $Version
        ReleaseDirectory = $releasePath
        ExecutableSHA256 = $exeHash
        DistributionArchive = Join-Path $releasePath $colleagueName
        DistributionEntries = $colleagueEntries.Count
        DistributionSHA256 = $colleagueHash
        SourceArchive = if ($IncludeSource) { Join-Path $releasePath $sourceName } else { $null }
        SourceEntries = if ($IncludeSource) { $sourceEntries.Count } else { 0 }
        SourceSHA256 = $sourceHash
        Metadata = Join-Path $releasePath $metadataName
        Checksums = Join-Path $releasePath $checksumsName
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
