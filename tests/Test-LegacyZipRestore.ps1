#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$ArtifactDirectory,
    [string]$ProductionManifest,
    [ValidatePattern('^[A-Fa-f0-9]{64}$')]
    [string]$ProductionManifestSha256 = 'A16DE396550FA54E25A72CFF00876F39A34105412362DE578CE5ECC6248A0BEA',
    [ValidateSet('Before','After')] [string]$SharingProof = 'After',
    [ValidateSet('Before','After','Skip')] [string]$FilenameProof = 'After',
    [ValidateSet('Before','After','Skip')] [string]$RangeProof = 'After',
    [ValidateSet('Before','After','Skip')] [string]$DestinationProof = 'After',
    [ValidateSet('Before','After','Skip')] [string]$FlatSitesProof = 'After',
    [ValidateSet('Before','After','Skip')] [string]$LegacyBindingProof = 'Skip',
    [ValidateSet('Before','After','Skip')] [string]$UnmarkedSitesProof = 'Skip',
    [switch]$MarkedSitesOnly,
    [switch]$WindowsEntryPaths,
    [switch]$ZipWriteTimes,
    [switch]$UnicodeEntryNames
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
# Keep the original sharing-only reproduction usable against its historical source.
if ($SharingProof -eq 'Before' -and -not $PSBoundParameters.ContainsKey('FilenameProof')) {
    $FilenameProof = 'Skip'
}
if (($SharingProof -eq 'Before' -or $FilenameProof -eq 'Before') -and
    -not $PSBoundParameters.ContainsKey('RangeProof')) {
    $RangeProof = 'Skip'
}
if (($SharingProof -eq 'Before' -or $FilenameProof -eq 'Before' -or $RangeProof -eq 'Before') -and
    -not $PSBoundParameters.ContainsKey('DestinationProof')) {
    $DestinationProof = 'Skip'
}
if (($SharingProof -eq 'Before' -or $FilenameProof -eq 'Before' -or $RangeProof -eq 'Before' -or
     $DestinationProof -eq 'Before') -and -not $PSBoundParameters.ContainsKey('FlatSitesProof')) {
    $FlatSitesProof = 'Skip'
}
$workspace = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildRoot = Join-Path $workspace 'build'
$artifacts = [IO.Path]::GetFullPath($ArtifactDirectory).TrimEnd('\')
$snapshot = Join-Path $artifacts 'source'
$fixture = Join-Path $artifacts 'f'
$temp = Join-Path $artifacts 'temp'
$bin = Join-Path $artifacts 'bin'
$obj = Join-Path $artifacts 'obj'
$fencePath = Join-Path $artifacts 'source-frozen.json'
$utf8 = [Text.UTF8Encoding]::new($false)
$useSealedSnapshot = -not [string]::IsNullOrEmpty($ProductionManifest)
if ($useSealedSnapshot) { $ProductionManifest = [IO.Path]::GetFullPath($ProductionManifest) }

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Hash([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
}

function Is-Child([string]$Path, [string]$Parent) {
    return $Path.StartsWith($Parent.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)
}

function No-Reparse([string]$Path) {
    for ($part = [IO.Path]::GetFullPath($Path); $part; $part = [IO.Path]::GetDirectoryName($part)) {
        if (Test-Path -LiteralPath $part) {
            Require (-not ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) "Reparse path refused: $part"
        }
    }
}

function Write-Json([string]$Path, $Value) {
    Require (-not (Test-Path -LiteralPath $Path)) "Existing evidence refused: $Path"
    [IO.File]::WriteAllText($Path, (ConvertTo-Json -InputObject $Value -Depth 9), $utf8)
}

function Input-Record([string]$Path) {
    No-Reparse $Path
    $item = Get-Item -LiteralPath $Path
    Require (-not $item.PSIsContainer) "Expected a regular input file: $Path"
    return [ordered]@{ Path = $Path; Bytes = $item.Length; Sha256 = Hash $Path }
}

function Check-Input($Record) {
    $item = Input-Record $Record.Path
    Require ($item.Bytes -eq $Record.Bytes -and $item.Sha256 -eq $Record.Sha256) "Input bytes changed: $($Record.Path)"
}

function New-OrdinaryZip([string]$Name, [Collections.IDictionary]$Entries, [Collections.IDictionary]$WriteTimes = @{}) {
    $path = Join-Path $fixture $Name
    $file = [IO.FileStream]::new($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        $archive = [IO.Compression.ZipArchive]::new($file, [IO.Compression.ZipArchiveMode]::Create, $true, $utf8)
        try {
            foreach ($name in $Entries.Keys) {
                $entry = $archive.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
                if ($WriteTimes.Contains($name)) { $entry.LastWriteTime = $WriteTimes[$name] }
                if ($null -ne $Entries[$name]) {
                    $bytes = $utf8.GetBytes([string]$Entries[$name])
                    $stream = $entry.Open()
                    try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
                }
            }
        } finally { $archive.Dispose() }
        $file.Flush($true)
    } finally { $file.Dispose() }

    $verify = [IO.Compression.ZipFile]::OpenRead($path)
    try {
        Require ($verify.Entries.Count -eq $Entries.Count) "Unexpected ZIP entry count: $path"
        foreach ($name in $Entries.Keys) {
            $entry = $verify.GetEntry($name)
            Require ($null -ne $entry) "Missing exact ordinary ZIP entry: $name"
            if ($WriteTimes.Contains($name)) {
                Require ($entry.LastWriteTime.DateTime -eq $WriteTimes[$name].DateTime) "ZIP entry write time differs: $name"
            }
            if ($null -eq $Entries[$name]) {
                Require ($entry.Length -eq 0 -and ($entry.FullName.EndsWith('/') -or $entry.FullName.EndsWith('\'))) "Unexpected directory ZIP entry: $name"
                continue
            }
            $reader = [IO.StreamReader]::new($entry.Open(), $utf8)
            try { Require ($reader.ReadToEnd() -ceq $Entries[$name]) "ZIP file bytes differ: $name" }
            finally { $reader.Dispose() }
        }
    } finally { $verify.Dispose() }
    $record = Input-Record $path
    Require ($record.Bytes -le 64KB) "Ordinary ZIP fixture exceeds its bounded size: $path"
    $record['EntryNames'] = @($Entries.Keys)
    return $record
}

function Invoke-Hidden([string]$Exe, [string[]]$Arguments, [string]$Name, [int]$TimeoutSeconds) {
    $log = Join-Path $artifacts ($Name + '.log')
    $start = [Diagnostics.ProcessStartInfo]::new($Exe)
    $start.WorkingDirectory = $snapshot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.Environment['TEMP'] = $temp
    $start.Environment['TMP'] = $temp
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $exeHash = Hash $Exe
    $started = [DateTime]::UtcNow
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $timedOut = -not $process.WaitForExit($TimeoutSeconds * 1000)
        if ($timedOut) { $process.Kill($true); $process.WaitForExit() }
        $text = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        [IO.File]::WriteAllText($log, $text, $utf8)
        $receipt = [ordered]@{
            Exe = $Exe; ExeSha256 = $exeHash; Arguments = $Arguments; Pid = $process.Id
            WorkingDirectory = $snapshot; Temp = $temp; CreateNoWindow = $true
            InheritedPriority = $lane.PriorityClass.ToString(); AffinityMask = $mask
            ExitCode = $process.ExitCode; TimedOut = $timedOut; Seconds = ([DateTime]::UtcNow - $started).TotalSeconds
            Log = $log; LogSha256 = Hash $log
        }
        Write-Json ($log + '.json') $receipt
        Require (-not $timedOut -and $process.ExitCode -eq 0 -and (Hash $Exe) -eq $exeHash) "Hidden $Name failed or was refused; inspect $log."
        return $receipt
    } finally { $process.Dispose() }
}

Require ((Is-Child $artifacts $buildRoot) -and (Join-Path $fixture 'wrapped\Sites\Équipe QA').Length + 150 -le 259) 'Use a short artifact path strictly below the workspace build root.'
if ($DestinationProof -ne 'Skip') {
    Require ((Join-Path $fixture 'literal [archive]\Sites\Legacy Client').Length + 150 -le 259) 'Use a short artifact path for the literal bracket staging proof.'
}
No-Reparse $artifacts
Require (-not (Test-Path -LiteralPath $artifacts)) 'Use a fresh artifact directory; earlier fixtures and logs are retained.'
$production = @(if ($useSealedSnapshot) {
    Require ((Is-Child $ProductionManifest $buildRoot) -and -not (Is-Child $ProductionManifest $artifacts)) 'Use an existing sealed production manifest outside the new artifact root.'
    No-Reparse $ProductionManifest
    Require ((Hash $ProductionManifest) -eq $ProductionManifestSha256) 'Production manifest differs from the supplied sealed hash.'
    foreach ($item in (Get-Content -LiteralPath $ProductionManifest -Raw | ConvertFrom-Json)) {
        Require ((Is-Child $item.Path $workspace) -and (Is-Child $item.Snapshot $buildRoot) -and
            -not (Is-Child $item.Snapshot $artifacts)) "Frozen source escaped workspace: $($item.Path)"
        $input = [ordered]@{ Path = $item.Snapshot; Bytes = $item.Bytes; Sha256 = $item.Sha256 }
        Check-Input $input
        [ordered]@{ WorkspacePath = $item.Path; Input = $input }
    }
} else {
    $vendorRoot = Join-Path $workspace '3p\7zip'
    No-Reparse $vendorRoot
    $applicationPaths = @(Get-ChildItem -LiteralPath $workspace -File | Where-Object Extension -in '.cpp','.h','.inl' | ForEach-Object FullName)
    $vendorHeaders = @(Get-ChildItem -LiteralPath $vendorRoot -Recurse -File | Where-Object Extension -in '.h','.hpp','.inl','.inc' | ForEach-Object FullName)
    $library = Join-Path $workspace 'dist\x64\Release\7zip.lib'
    foreach ($path in ($applicationPaths + $vendorHeaders + $library | Sort-Object -Unique)) {
        [ordered]@{ WorkspacePath = $path; Input = (Input-Record $path) }
    }
})
foreach ($directory in $snapshot, $fixture, $temp) { [IO.Directory]::CreateDirectory($directory) | Out-Null }
$copies = @(foreach ($item in $production) {
    $relative = [IO.Path]::GetRelativePath($workspace, $item.WorkspacePath)
    Require (-not [IO.Path]::IsPathRooted($relative) -and $relative -notmatch '(^|[\\/])\.\.([\\/]|$)') "Source path escaped workspace: $($item.WorkspacePath)"
    $input = $item.Input
    $destination = Join-Path $snapshot $relative
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
    [IO.File]::Copy($input.Path, $destination, $false)
    $copy = Input-Record $destination
    Require ($copy.Bytes -eq $input.Bytes -and $copy.Sha256 -eq $input.Sha256) "Frozen source copy differs: $relative"
    $copy['Origin'] = $input.Path
    $copy
})
Require ($copies.Count -eq $production.Count -and $copies.Count -gt 0 -and
    (Test-Path -LiteralPath (Join-Path $snapshot 'ctSpaces.cpp') -PathType Leaf) -and
    (Test-Path -LiteralPath (Join-Path $snapshot 'dist\x64\Release\7zip.lib') -PathType Leaf)) 'Production source or static dependency missing.'

foreach ($name in 'LegacyZipRestoreTests.cpp','LegacyZipRestoreTests.vcxproj','Test-LegacyZipRestore.ps1') {
    $input = Input-Record (Join-Path $PSScriptRoot $name)
    $destination = Join-Path $snapshot (Join-Path 'tests' $name)
    [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
    [IO.File]::Copy($input.Path, $destination, $false)
    $copy = Input-Record $destination
    Require ($copy.Sha256 -eq $input.Sha256) "Test input copy differs: $name"
    $copy['Origin'] = $input.Path
    $copies += $copy
}
$header = Join-Path $snapshot 'legacy-zip-fixture.h'
$literal = $fixture.Replace('\', '\\').Replace('"', '\"')
[IO.File]::WriteAllText($header, "#pragma once`r`n#define CTSPACES_LEGACY_ZIP_FIXTURE_ROOT L`"$literal`"`r`n", $utf8)
$copies += Input-Record $header
Write-Json $fencePath $copies
$fenceHash = Hash $fencePath

$marker = Join-Path $fixture 'scope.txt'
[IO.File]::WriteAllText($marker, "CTSPACES ORDINARY LEGACY ZIP FIXTURE`n", $utf8)
$unicodeClient = [string][char]0x00C9 + 'quipe QA'
$wrapped = [ordered]@{ 'Sites/Alpha/content.txt' = "WRAPPED ALPHA`n"; 'Sites/Alpha/empty/' = $null }
$wrapped["Sites/$unicodeClient/profile/state.bin"] = "WRAPPED UNICODE CLIENT`n"
$archives = @(
    (New-OrdinaryZip 'wrapped archive.zip' $wrapped),
    (New-OrdinaryZip 'flat.zip' ([ordered]@{ 'Legacy Client/content.txt' = "FLAT CLIENT`n"; 'Legacy Client/empty/' = $null })),
    (New-OrdinaryZip 'mixed.zip' ([ordered]@{ 'Sites/Alpha/content.txt' = "MIXED CLIENT`n"; 'readme.txt' = "ORDINARY ROOT README`n" }))
)
if ($FlatSitesProof -ne 'Skip') {
    $archives += New-OrdinaryZip 'flat-sites.zip' ([ordered]@{
        'sItEs/ctSpaces-client-v2' = "ctSpaces-client-schema=2`r`n"
        'sItEs/content.txt' = "FLAT SITES CLIENT`n"
        'Beta/content.txt' = "FLAT BETA`n"
    })
}
if ($MarkedSitesOnly) {
    $archives += New-OrdinaryZip 'marked-sites-only.zip' ([ordered]@{
        'sItEs/ctSpaces-client-v2' = "ctSpaces-client-schema=2`r`n"
        'sItEs/Browsers/edge/ctSpaces-browser-v2' = "ctSpaces-browser-schema=2`r`nbrowser=edge`r`n"
        'sItEs/Browsers/edge/Profile/ctSpaces' = "ctSpaces-profile=2`r`n"
        'sItEs/Browsers/edge/Profile/Default/Preferences' = '{"profile":{"name":"Legacy Sites"}}' + "`n"
    })
}
if ($LegacyBindingProof -ne 'Skip') {
    $archives += New-OrdinaryZip 'flat-bound-sites.zip' ([ordered]@{
        'Sites/ctSpaces-legacy-browser-v2' = "ctSpaces-legacy-browser-schema=2`r`nbrowser=edge`r`n"
        'Sites/Local State' = '{"profile":{"last_used":"Default"}}' + "`n"
        'Sites/Default/Preferences' = '{"profile":{"name":"Legacy Sites"}}' + "`n"
        'Beta/content.txt' = "FLAT BETA`n"
    })
}
if ($WindowsEntryPaths) {
    $archives += New-OrdinaryZip 'Windows archive.ZIP' ([ordered]@{
        'Sites\Windows Client\Browser Profile\Preferences' = '{"profile":{"name":"Windows Client"}}' + "`n"
        'Sites\Windows Client\Browser Profile\State.v2\' = $null
    })
}
if ($UnmarkedSitesProof -ne 'Skip') {
    $archives += New-OrdinaryZip 'flat-unmarked-sites.zip' ([ordered]@{
        'Sites/Local State' = '{"profile":{"last_used":"Default"}}' + "`n"
        'Sites/Default/Preferences' = '{"profile":{"name":"Legacy Sites"}}' + "`n"
        'Beta/content.txt' = "FLAT BETA`n"
    })
}
if ($ZipWriteTimes) {
    $archives += New-OrdinaryZip 'archive-times.zip' ([ordered]@{
        'sItEs/MiXeD Client/Preferences' = "OLDER WINDOWS ZIP PREFERENCES`n"
        'sItEs/MiXeD Client/Empty Log' = ''
    }) @{
        'sItEs/MiXeD Client/Preferences' = [DateTimeOffset]::new(2002, 1, 18, 12, 4, 56, [TimeSpan]::Zero)
        'sItEs/MiXeD Client/Empty Log' = [DateTimeOffset]::new(2024, 11, 19, 16, 32, 10, [TimeSpan]::Zero)
    }
}
if ($UnicodeEntryNames) {
    $unicodeEntries = [ordered]@{}
    $unicodeEntries["Sites/東京 Équipe 💼/Δossier 📄.txt"] = "MIXED SCRIPT CLIENT`n"
    $unicodeEntries["Sites/東京 Équipe 💼/Архив/"] = $null
    $archives += New-OrdinaryZip '東京 📦 backup.zip' $unicodeEntries
}
if ($FilenameProof -ne 'Skip') {
    $flatZip = $archives[1]
    foreach ($filename in "O'Brien backup.zip", ('O' + [char]0x2019 + 'Brien backup.zip')) {
        $copyPath = Join-Path $fixture $filename
        [IO.File]::Copy($flatZip.Path, $copyPath, $false)
        $copy = Input-Record $copyPath
        Require ($copy.Bytes -eq $flatZip.Bytes -and $copy.Sha256 -eq $flatZip.Sha256) 'Filename proof ZIP bytes differ from the ordinary flat ZIP.'
        $copy['EntryNames'] = $flatZip.EntryNames
        $archives += $copy
    }
}
$markerRecord = Input-Record $marker
Write-Json (Join-Path $artifacts 'archive-inputs.json') $archives

function Check-Fence {
    Require ((Hash $fencePath) -eq $fenceHash -and (-not $useSealedSnapshot -or (Hash $ProductionManifest) -eq $ProductionManifestSha256)) 'Sealed source records changed.'
    foreach ($item in $copies) {
        Check-Input $item
        if ($item.Contains('Origin')) { Check-Input @{ Path = $item.Origin; Bytes = $item.Bytes; Sha256 = $item.Sha256 } }
    }
    foreach ($item in $archives) { Check-Input $item }
    Check-Input $markerRecord
    Require (-not (Test-Path -LiteralPath (Join-Path $fixture 'missing.zip'))) 'The intentionally absent fixture path appeared.'
}

$lane = [Diagnostics.Process]::GetCurrentProcess()
$mask = [long]12582912
Require (($lane.ProcessorAffinity.ToInt64() -band $mask) -eq $mask) 'The agreed two logical processors are unavailable.'
$lane.ProcessorAffinity = [IntPtr]$mask
$lane.PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio\Installer\vswhere.exe'
Require (Test-Path -LiteralPath $vswhere -PathType Leaf) 'Visual Studio discovery is unavailable.'
$locations = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe')
$discoveryExitCode = $LASTEXITCODE
$msbuild = $locations | Select-Object -First 1
Require ($discoveryExitCode -eq 0 -and $msbuild -and (Test-Path -LiteralPath $msbuild -PathType Leaf)) 'MSBuild is unavailable.'
$project = Join-Path $snapshot 'tests\LegacyZipRestoreTests.vcxproj'
Check-Fence
$compiled = Invoke-Hidden $msbuild @($project, '/m:1', '/nodeReuse:false', '/p:Configuration=Release', '/p:Platform=x64',
    '/p:BuildProjectReferences=false', "/p:OutDir=$bin\", "/p:IntDir=$obj\", "/p:LegacyZipFixtureHeader=$header", '/v:minimal') 'build' 600
Check-Fence
$binary = Join-Path $bin 'LegacyZipRestoreTests.exe'
$sharingBefore = $SharingProof -eq 'Before'
$filenameBefore = $FilenameProof -eq 'Before'
$rangeBefore = $RangeProof -eq 'Before'
$destinationBefore = $DestinationProof -eq 'Before'
$flatSitesBefore = $FlatSitesProof -eq 'Before'
$legacyBindingBefore = $LegacyBindingProof -eq 'Before'
$unmarkedSitesBefore = $UnmarkedSitesProof -eq 'Before'
$proofArgument = if ($sharingBefore) { '--sharing-before' } else { '--sharing-after' }
$expectedChildren = if ($sharingBefore) { 5 } else { 4 }
$expectedCases = 5
$expectedChecks = 78
$proofMarkers = if ($sharingBefore) { @('LEGACY_ZIP_SHARING_HRESULT=0x80004005','LEGACY_ZIP_SHARING_GENERIC_BEFORE=1') }
    else { @('LEGACY_ZIP_SHARING_HRESULT=0x80070020','LEGACY_ZIP_SHARING_IN_USE_AFTER=1') }
$nativeArguments = @('--headless', $fixture, $proofArgument)
if ($FilenameProof -ne 'Skip') {
    $nativeArguments += '--filename-' + $FilenameProof.ToLowerInvariant()
    $expectedChildren += 2
    $expectedCases += 2
    $proofMarkers += 'LEGACY_ZIP_CASE=ascii-apostrophe result=passed'
    if ($filenameBefore) {
        $expectedChecks += 25
        $proofMarkers += @('LEGACY_ZIP_SMART_APOSTROPHE_HRESULT=0x80004005',
            'LEGACY_ZIP_CASE=smart-apostrophe result=expected-refusal')
    } else {
        $expectedChecks += 31
        $proofMarkers += 'LEGACY_ZIP_CASE=smart-apostrophe result=passed'
    }
}
if ($RangeProof -ne 'Skip') {
    $nativeArguments += '--range-' + $RangeProof.ToLowerInvariant()
    $expectedChildren += 2
    $expectedCases += 1
    $expectedChecks += 28
    $proofMarkers += @('LEGACY_ZIP_RANGE_NATIVE_ERROR=33','LEGACY_ZIP_RANGE_PRESERVED=1',
        'LEGACY_ZIP_CASE=range-retry result=passed','LEGACY_ZIP_CASE=range-lock result=passed')
    if ($rangeBefore) {
        $proofMarkers += @('LEGACY_ZIP_RANGE_HRESULT=0x80004005','LEGACY_ZIP_RANGE_GENERIC_BEFORE=1')
    } else {
        $proofMarkers += @('LEGACY_ZIP_RANGE_HRESULT=0x80070021','LEGACY_ZIP_RANGE_IN_USE_AFTER=1')
    }
}
if ($DestinationProof -ne 'Skip') {
    $nativeArguments += '--destination-' + $DestinationProof.ToLowerInvariant()
    $expectedChildren += 1
    $expectedCases += 1
    $proofMarkers += 'LEGACY_ZIP_LITERAL_DESTINATION_PRESERVED=1'
    if ($destinationBefore) {
        $expectedChecks += 10
        $proofMarkers += @('LEGACY_ZIP_LITERAL_DESTINATION_HRESULT=0x80004005',
            'LEGACY_ZIP_CASE=literal-stage-path result=expected-refusal')
    } else {
        $expectedChecks += 18
        $proofMarkers += 'LEGACY_ZIP_CASE=literal-stage-path result=passed'
    }
}
if ($FlatSitesProof -ne 'Skip') {
    $nativeArguments += '--flat-sites-' + $FlatSitesProof.ToLowerInvariant()
    $expectedChildren += 1
    $expectedCases += 1
    if ($flatSitesBefore) {
        $expectedChecks += 12
        $proofMarkers += @('LEGACY_ZIP_FLAT_SITES_HRESULT=0x80070057',
            'LEGACY_ZIP_CASE=flat-sites-client result=expected-refusal')
    } else {
        $expectedChecks += 15
        $proofMarkers += @('LEGACY_ZIP_FLAT_SITES_HRESULT=0x0',
            'LEGACY_ZIP_CASE=flat-sites-client result=passed')
    }
}
if ($MarkedSitesOnly) {
    $nativeArguments += '--marked-sites-only'
    $expectedChildren += 1
    $expectedCases += 1
    $expectedChecks += 15
    $proofMarkers += 'LEGACY_ZIP_CASE=marked-sites-only result=passed'
}
if ($LegacyBindingProof -ne 'Skip') {
    $nativeArguments += '--bound-sites-' + $LegacyBindingProof.ToLowerInvariant()
    $expectedChildren += 1
    $expectedCases += 1
    if ($legacyBindingBefore) {
        $expectedChecks += 12
        $proofMarkers += @('LEGACY_ZIP_FLAT_BOUND_SITES_HRESULT=0x80070057',
            'LEGACY_ZIP_CASE=flat-bound-sites-client result=expected-refusal')
    } else {
        $expectedChecks += 15
        $proofMarkers += @('LEGACY_ZIP_FLAT_BOUND_SITES_HRESULT=0x0',
            'LEGACY_ZIP_CASE=flat-bound-sites-client result=passed')
    }
}
if ($WindowsEntryPaths) {
    $nativeArguments += '--windows-entry-paths'
    $expectedChildren += 1
    $expectedCases += 1
    $expectedChecks += 15
    $proofMarkers += 'LEGACY_ZIP_CASE=windows-entry-paths result=passed'
}
if ($UnmarkedSitesProof -ne 'Skip') {
    $nativeArguments += '--unmarked-sites-' + $UnmarkedSitesProof.ToLowerInvariant()
    $expectedChildren += 1
    $expectedCases += 1
    if ($unmarkedSitesBefore) {
        $expectedChecks += 12
        $proofMarkers += @('LEGACY_ZIP_FLAT_UNMARKED_SITES_HRESULT=0x80070057',
            'LEGACY_ZIP_CASE=flat-unmarked-sites-client result=expected-refusal')
    } else {
        $expectedChecks += 15
        $proofMarkers += @('LEGACY_ZIP_FLAT_UNMARKED_SITES_HRESULT=0x0',
            'LEGACY_ZIP_CASE=flat-unmarked-sites-client result=passed')
    }
}
if ($ZipWriteTimes) {
    $nativeArguments += '--zip-write-times'
    $expectedChildren += 1
    $expectedCases += 1
    $expectedChecks += 19
    $proofMarkers += 'LEGACY_ZIP_CASE=zip-write-times result=passed'
}
if ($UnicodeEntryNames) {
    $nativeArguments += '--unicode-entry-names'
    $expectedChildren += 1
    $expectedCases += 1
    $expectedChecks += 15
    $proofMarkers += 'LEGACY_ZIP_CASE=unicode-entry-names result=passed'
}
$native = Invoke-Hidden $binary $nativeArguments 'native' 90
Check-Fence
$log = Get-Content -LiteralPath $native.Log -Raw
foreach ($marker in (@('VERSION=6.3.5.0',"CASES=$expectedCases",'FAILURES=0',"EXPECTED_SYSTEM_POWERSHELL_CHILDREN=$expectedChildren",'NATIVE_EVENTS=0/0/0','FORBIDDEN=0',
    'LEGACY_ZIP_CASE=wrapped result=passed','LEGACY_ZIP_CASE=flat result=passed','LEGACY_ZIP_CASE=mixed-layout result=passed','LEGACY_ZIP_CASE=missing result=passed',
    'LEGACY_ZIP_SHARING_PRESERVED=1','LEGACY_ZIP_CASE=sharing result=passed','LEGACY_ZIP_CASE=sharing-retry result=passed') + $proofMarkers)) {
    Require ($log -match ('(?m)^' + [regex]::Escape($marker) + '\r?$')) "Missing legacy ZIP coverage marker: $marker"
}
$checks = [regex]::Match($log, '(?m)^CHECKS=(\d+)\r?$')
$children = @([regex]::Matches($log, '(?m)^LEGACY_ZIP_HIDDEN_CHILD_PID=(\d+)\r?$') | ForEach-Object { [int]$_.Groups[1].Value })
Require ($checks.Success -and [int]$checks.Groups[1].Value -eq $expectedChecks -and $children.Count -eq $expectedChildren) 'Native assertion or child count differs.'
$result = [ordered]@{
    Status = if ($sharingBefore -or $filenameBefore -or $rangeBefore -or $destinationBefore -or
                 $flatSitesBefore -or $legacyBindingBefore -or $unmarkedSitesBefore) { 'ExpectedDefectReproduced' } else { 'Passed' }
    Version = '6.3.5.0'; Checks = [int]$checks.Groups[1].Value; Cases = $expectedCases; Failures = 0
    SourceMode = if ($useSealedSnapshot) { 'SealedSnapshot' } else { 'CurrentWorkspace' }
    SourceManifest = $ProductionManifest; SourceManifestSha256 = if ($useSealedSnapshot) { $ProductionManifestSha256 } else { $null }
    SourceInputs = $copies.Count
    SourceFence = $fencePath; SourceFenceSha256 = $fenceHash; SourceFencePassed = $true; OnlyCopiedInputsCompiled = $true
    WrappedRestore = $true; FlatRestore = $true; MixedLayoutRefusedBeforeCommit = $true; MissingArchiveRefusedBeforeChildLaunch = $true
    SharingProof = $SharingProof; SharingViolationConfirmedByWindows = $true; SharingRefusalPreservedData = $true; SharingRetryPassed = $true
    SharingRefusalChildLaunches = if ($sharingBefore) { 1 } else { 0 }
    FilenameProof = $FilenameProof; AsciiApostropheRestore = $FilenameProof -ne 'Skip'
    TypographicApostropheFailureReproduced = $filenameBefore; TypographicApostropheRestore = $FilenameProof -eq 'After'
    RangeProof = $RangeProof; RangeViolationConfirmedByWindows = $RangeProof -ne 'Skip'
    RangeRefusalPreservedData = $RangeProof -ne 'Skip'; RangeRetryPassed = $RangeProof -ne 'Skip'
    DestinationProof = $DestinationProof; LiteralDestinationFailureReproduced = $destinationBefore
    LiteralDestinationRestored = $DestinationProof -eq 'After'
    FlatSitesProof = $FlatSitesProof; FlatSitesFailureReproduced = $flatSitesBefore
    FlatSitesClientRestored = $FlatSitesProof -eq 'After'
    LegacyBindingProof = $LegacyBindingProof; LegacyBindingFailureReproduced = $legacyBindingBefore
    FlatBoundSitesClientRestored = $LegacyBindingProof -eq 'After'
    MarkedSitesOnlyRestored = [bool]$MarkedSitesOnly
    WindowsEntryPathsRestored = [bool]$WindowsEntryPaths
    UnmarkedSitesProof = $UnmarkedSitesProof; UnmarkedSitesFailureReproduced = $unmarkedSitesBefore
    FlatUnmarkedSitesClientRestored = $UnmarkedSitesProof -eq 'After'
    ZipWriteTimesRestored = [bool]$ZipWriteTimes
    UnicodeEntryNamesRestored = [bool]$UnicodeEntryNames
    OrdinaryZipFixtures = $archives; Fixture = $fixture; FixturesRetained = $true
    SystemPowerShellChildPids = $children; ExpectedSystemPowerShellChildren = $expectedChildren
    NativeEvents = @(0, 0, 0); Forbidden = 0; ShippingApplicationLaunched = $false
    Build = $compiled; Native = $native; Binary = $binary; BinarySha256 = $native.ExeSha256
}
Write-Json (Join-Path $artifacts 'result.json') $result
Write-Output $log
