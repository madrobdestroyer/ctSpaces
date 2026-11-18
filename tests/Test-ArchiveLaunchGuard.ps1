#requires -Version 7.0
param([string]$ArtifactDirectory = '', [string]$ProductionSource = '', [switch]$ExpectedBeforeFailure)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $ArtifactDirectory) { $ArtifactDirectory = Join-Path $root ('build\tests\alg-' + [guid]::NewGuid().ToString('N').Substring(0, 8)) }
$artifacts = [IO.Path]::GetFullPath($ArtifactDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar)
if (-not $artifacts.StartsWith((Join-Path $root 'build') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Archive launch evidence must stay below workspace build.' }
if (-not $ProductionSource) { $ProductionSource = Join-Path $root 'ctSpaces.cpp' }
$production = [IO.Path]::GetFullPath($ProductionSource)
if (-not $production.StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $production -PathType Leaf)) { throw 'The exact production source must be a workspace file.' }
function Assert-NoReparseAncestor([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) { if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw ('Reparse path refused: ' + $current) } }
        $parent = [IO.Path]::GetDirectoryName($current)
        if ($parent -eq $current) { break }
        $current = $parent
    }
}
Assert-NoReparseAncestor $artifacts; Assert-NoReparseAncestor $production
if (Test-Path -LiteralPath $artifacts) { throw 'Use a new evidence directory; existing evidence is never replaced or removed.' }
$msbuild = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) { throw 'MSBuild was not found.' }
New-Item -ItemType Directory -Path $artifacts -ErrorAction Stop | Out-Null
Assert-NoReparseAncestor $artifacts
$sourceFolder = Join-Path $artifacts 'production-source'; New-Item -ItemType Directory -Path $sourceFolder | Out-Null
$snapshot = Join-Path $sourceFolder 'ctSpaces.cpp'; [IO.File]::Copy($production, $snapshot)
$productionHash = (Get-FileHash -LiteralPath $production).Hash
if ((Get-FileHash -LiteralPath $snapshot).Hash -ne $productionHash) { throw 'The literal production snapshot did not match its source.' }
$header = Join-Path $artifacts 'source-fixture.h'
$escapedFixture = $artifacts.Replace('\', '\\').Replace('"', '\"'); $escapedSource = $snapshot.Replace('\', '\\').Replace('"', '\"')
[IO.File]::WriteAllText($header, "#pragma once`r`n#define CTSPACES_ARCHIVE_LAUNCH_FIXTURE_PARENT L`"$escapedFixture`"`r`n#define CTSPACES_ARCHIVE_LAUNCH_SOURCE `"$escapedSource`"`r`n", [Text.UTF8Encoding]::new($false))
$sourceNames = @('BackupEngine.cpp', 'BackupEngine.h', 'InProc7z.cpp', 'InProc7z.h', 'RestoreIoPath.h', 'SiblingStageName.h', 'ClientNameComparison.h', 'BrowserPreferencesJson.cpp', 'ConfigPersistence.cpp', 'GuidedWalkthrough.cpp', 'ProgressUI.cpp', 'ProgressUI.h', 'BackupSelectionUi.inl', 'ctSpaces.rc', 'version.h', 'tests\ArchiveLaunchGuardTests.cpp', 'tests\ArchiveLaunchGuardTests.vcxproj', 'tests\Test-ArchiveLaunchGuard.ps1', 'dist\x64\Release\7zip.lib')
$paths = @($sourceNames | ForEach-Object { Join-Path $root $_ }) + @($production, $snapshot, $header)
$sources = @($paths | ForEach-Object { [pscustomobject]@{Path = $_; Sha256 = (Get-FileHash -LiteralPath $_).Hash} })
$sources | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
function Assert-Frozen { foreach ($source in $sources) { if ((Get-FileHash -LiteralPath $source.Path).Hash -ne $source.Sha256) { throw ('Archive launch input changed: ' + $source.Path) } } }
function Invoke-Hidden([string]$Exe, [string[]]$Arguments, [string]$Log, [int]$Expected = 0, [int]$Timeout = 600000) {
    Assert-Frozen
    $info = [Diagnostics.ProcessStartInfo]::new($Exe); $info.WorkingDirectory = $root; $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $started = [DateTime]::UtcNow; $process = [Diagnostics.Process]::Start($info)
    try {
        try { if (-not $process.HasExited) { $process.PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal } } catch [InvalidOperationException] { if (-not $process.HasExited) { throw } }
        $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync(); $timedOut = -not $process.WaitForExit($Timeout)
        if ($timedOut) { $process.Kill($true); $process.WaitForExit() }
        $text = $stdout.Result + $stderr.Result; [IO.File]::WriteAllText($Log, $text)
        [ordered]@{Exe = $Exe; BinarySha256 = (Get-FileHash -LiteralPath $Exe).Hash; Arguments = $Arguments; ExitCode = $process.ExitCode; ExpectedExitCode = $Expected; TimedOut = $timedOut; Seconds = ([DateTime]::UtcNow - $started).TotalSeconds; CreateNoWindow = $true; Priority = 'BelowNormal'; ProductionSource = $production; ProductionSourceSha256 = $productionHash; LogSha256 = (Get-FileHash -LiteralPath $Log).Hash} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($Log + '.json')
        Assert-Frozen
        if ($timedOut -or $process.ExitCode -ne $Expected) { throw ('Hidden archive launch tool failed; inspect ' + $Log) }
        return $text
    } finally { $process.Dispose() }
}
$bin = Join-Path $artifacts 'bin'; $obj = Join-Path $artifacts 'obj'
$null = Invoke-Hidden $msbuild @((Join-Path $PSScriptRoot 'ArchiveLaunchGuardTests.vcxproj'), '/m:1', '/nodeReuse:false', '/p:Configuration=Release', '/p:Platform=x64', '/p:BuildProjectReferences=false', ('/p:OutDir=' + $bin + '\'), ('/p:IntDir=' + $obj + '\'), ('/p:ArchiveLaunchGuardSourceHeader=' + $header), '/v:minimal') (Join-Path $artifacts 'build.log')
$binary = Join-Path $bin 'ArchiveLaunchGuardTests.exe'; $expectedExit = if ($ExpectedBeforeFailure) { 1 } else { 0 }
$native = Invoke-Hidden $binary @('--headless', (Join-Path $artifacts 'f')) (Join-Path $artifacts 'native.log') $expectedExit 90000
$expectedFailures = if ($ExpectedBeforeFailure) { 2 } else { 0 }; $expectedPicker = if ($ExpectedBeforeFailure) { 1 } else { 0 }
$markers = @("FAILURES=$expectedFailures", "PENDING_BACKUP_SELECTORS=$expectedPicker", "PENDING_RESTORE_PICKERS=$expectedPicker", 'SCENARIOS=4', 'NATIVE_EVENTS=0/0/0', 'FORBIDDEN=0', 'QUEUED_COMMAND_BOUNDARY_ONLY=1', 'VERSION=6.3.5.0')
foreach ($marker in $markers) { if ($native -notmatch ('(?m)^' + [regex]::Escape($marker) + '\r?$')) { throw ('Archive launch scope missing: ' + $marker) } }
if ($native -notmatch '(?m)^CHECKS=(\d+)\r?$') { throw 'Archive launch check count missing.' }; $checks = [int]$Matches[1]
if ($native -notmatch '(?m)^PHYSICAL_FIXTURE_BYTES=(\d+)\r?$') { throw 'Archive launch fixture size missing.' }; $bytes = [long]$Matches[1]
if ($checks -lt 18 -or $bytes -gt 64KB) { throw 'Archive launch scope or fixture budget was not confirmed.' }
[ordered]@{RegressionPassed = -not [bool]$ExpectedBeforeFailure; ExpectedDefectReproduced = [bool]$ExpectedBeforeFailure; Checks = $checks; Failures = $expectedFailures; Scenarios = 4; PhysicalFixtureBytes = $bytes; SourceFencePassed = $true; ProductionSource = $production; ProductionSourceSha256 = $productionHash; LiteralSnapshot = $snapshot; QueuedCommandBoundaryOnly = $true; PhysicalBrowserLaunchRaceProven = $false; Headless = $true; NativeEvents = @(0, 0, 0); Forbidden = 0; Binary = $binary; BinarySha256 = (Get-FileHash -LiteralPath $binary).Hash; Fixture = (Join-Path $artifacts 'f'); SourceHashes = $sources} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output $native
