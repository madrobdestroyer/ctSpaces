#requires -Version 7.0
param(
    [ValidateSet('Release')][string]$Configuration = 'Release',
    [ValidateSet('x64')][string]$Platform = 'x64',
    [string]$ArtifactDirectory = '',
    [switch]$ExpectedBeforeRootFailure
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $ArtifactDirectory) {
    $ArtifactDirectory = Join-Path $root ('build\tests\btf-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
}
$artifacts = [IO.Path]::GetFullPath($ArtifactDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar)
$buildPrefix = (Join-Path $root 'build') + [IO.Path]::DirectorySeparatorChar
if (-not $artifacts.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Timestamp-failure evidence must remain below the workspace build directory.'
}
function Assert-NoReparseAncestor([string]$Path) {
    $current = [IO.DirectoryInfo]::new($Path)
    while ($null -ne $current) {
        if (Test-Path -LiteralPath $current.FullName) {
            $item = Get-Item -LiteralPath $current.FullName -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw ('Reparse fixture/output ancestor refused: ' + $item.FullName)
            }
        }
        $current = $current.Parent
    }
}
Assert-NoReparseAncestor $artifacts
if (Test-Path -LiteralPath $artifacts) {
    throw 'ArtifactDirectory must be new for this run; existing evidence is never reused or removed.'
}
# Keep the actual restored Alpha root under the app's unchanged final budget.
$fixture = Join-Path $artifacts 'f'
if ((Join-Path $fixture 'd\l\Sites\Alpha').Length + 150 -gt 259 -or
    (Join-Path $fixture 'd\l\Sites\Alpha').Length + 123 -gt 247) {
    throw 'Choose a shorter workspace artifact directory so the ordinary final client target fits production path limits.'
}
$project = Join-Path $PSScriptRoot 'BackupTimestampFailureTests.vcxproj'
$msbuild = (Get-Command msbuild.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $msbuild) {
    $candidate = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $msbuild = $candidate }
}
if (-not $msbuild -or -not (Test-Path -LiteralPath $msbuild -PathType Leaf)) { throw 'MSBuild was not found.' }
if (-not (Test-Path -LiteralPath (Join-Path $root 'dist\x64\Release\7zip.lib') -PathType Leaf)) {
    throw 'The bundled Release x64 archive library is required; this wrapper does not rebuild it.'
}
New-Item -ItemType Directory -Path $artifacts -ErrorAction Stop | Out-Null
Assert-NoReparseAncestor $artifacts
$outputDirectory = Join-Path $artifacts 'bin'
$intermediateDirectory = Join-Path $artifacts 'obj'
# Force-include an ordinary generated header to bind this binary to this run's
# checked artifact directory without embedding a workstation path in source.
$rootHeader = Join-Path $artifacts 'fixture-root.h'
$escapedRoot = $artifacts.Replace('\', '\\').Replace('"', '\"')
[IO.File]::WriteAllText($rootHeader, "#pragma once`r`n#define CTSPACES_TIMESTAMP_FAILURE_FIXTURE_PARENT L`"$escapedRoot`"`r`n", [Text.UTF8Encoding]::new($false))

function Invoke-HiddenTimestampFailureTool([string]$Exe, [string[]]$Arguments, [string]$Log, [int]$Timeout) {
    $info = [Diagnostics.ProcessStartInfo]::new([IO.Path]::GetFullPath($Exe))
    $info.WorkingDirectory = $root
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $started = [DateTime]::UtcNow
    $process = [Diagnostics.Process]::Start($info)
    try {
        try { if (-not $process.HasExited) { $process.PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal } }
        catch [InvalidOperationException] { if (-not $process.HasExited) { throw } }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($Timeout)) {
            $process.Kill($true)
            throw 'The hidden timestamp-failure tool timed out; its owned process tree was stopped and private evidence retained.'
        }
        $contents = $stdout.Result + $stderr.Result
        [IO.File]::WriteAllText($Log, $contents)
        $receipt = [ordered]@{
            Exe = $info.FileName; BinarySha256 = (Get-FileHash -LiteralPath $info.FileName).Hash
            Arguments = $Arguments; CreateNoWindow = $true; Priority = 'BelowNormal'
            ExitCode = $process.ExitCode; Seconds = ([DateTime]::UtcNow - $started).TotalSeconds
            Log = $Log; LogSha256 = (Get-FileHash -LiteralPath $Log).Hash
        }
        $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($Log + '.json')
        if ($process.ExitCode -ne 0) { throw ('Hidden timestamp-failure tool failed; inspect ' + $Log + '. All fixtures are retained.') }
        return $contents
    } finally { $process.Dispose() }
}
$sources = @(
    'BackupEngine.cpp', 'BackupEngine.h', 'InProc7z.cpp', 'InProc7z.h', 'RestoreIoPath.h',
    'SiblingStageName.h', 'ClientNameComparison.h', 'version.h',
    'tests\BackupTimestampFailureTests.cpp', 'tests\BackupTimestampFailureTests.vcxproj',
    'tests\BackupTimestampFailureNativeBoundary.h',
    'tests\Test-BackupTimestampFailure.ps1', 'dist\x64\Release\7zip.lib'
)
$sourcePaths = @($sources | ForEach-Object { Join-Path $root $_ }) + @($rootHeader)
function Read-InputHashes {
    @($sourcePaths | ForEach-Object { [pscustomobject]@{ Path = $_; Sha256 = (Get-FileHash -LiteralPath $_).Hash } })
}
$sourceBefore = Read-InputHashes
$sourceBefore | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
function Assert-SourceFence([string]$ReceiptName) {
    $current = Read-InputHashes
    $current | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifacts $ReceiptName)
    if ((ConvertTo-Json -InputObject $sourceBefore -Compress) -ne (ConvertTo-Json -InputObject $current -Compress)) {
        throw 'Timestamp-failure regression inputs changed during this run; all evidence is retained.'
    }
}
$null = Invoke-HiddenTimestampFailureTool $msbuild @(
    $project, '/m:1', '/nodeReuse:false', "/p:Configuration=$Configuration", "/p:Platform=$Platform",
    '/p:BuildProjectReferences=false', ('/p:OutDir=' + $outputDirectory + '\'),
    ('/p:IntDir=' + $intermediateDirectory + '\'), ('/p:TimestampFailureFixtureRootHeader=' + $rootHeader), '/v:minimal'
) (Join-Path $artifacts 'build.log') 300000
Assert-SourceFence 'source-after-build.json'
$mode = if ($ExpectedBeforeRootFailure) { '--before-root' } else { '--after' }
$binary = Join-Path $outputDirectory 'BackupTimestampFailureTests.exe'
$nativeLog = Invoke-HiddenTimestampFailureTool $binary @('--headless', $mode, $fixture) (Join-Path $artifacts 'native.log') 90000
Assert-SourceFence 'source-after-native.json'
$expectedMarker = if ($ExpectedBeforeRootFailure) { 0 } else { 1 }
if ($nativeLog -notmatch "(?m)^REGRESSION_PASSED=$expectedMarker\r?$" -or
    $nativeLog -notmatch '(?m)^UNEXPECTED_FAILURES=0\r?$' -or
    $nativeLog -notmatch '(?m)^PRODUCTION_ITEM_CAP=500000\r?$' -or
    $nativeLog -notmatch '(?m)^CASES=3\r?$' -or
    $nativeLog -notmatch '(?m)^ROOT_CASE_COMPLETED=1\r?$' -or
    $nativeLog -notmatch '(?m)^FILE_FAILURE_CASE_COMPLETED=1\r?$' -or
    $nativeLog -notmatch '(?m)^DIRECTORY_FAILURE_CASE_COMPLETED=1\r?$' -or
    $nativeLog -notmatch '(?m)^CHECKS=(\d+)\r?$') {
    throw 'The complete ordinary timestamp-failure regression scope was not confirmed.'
}
$nativeChecks = [int]$Matches[1]
if ($ExpectedBeforeRootFailure -and $nativeLog -notmatch '(?m)^EXPECTED_ROOT_DEFECT_REPRODUCED=1\r?$') {
    throw 'The explicit before-fix run did not reproduce the expected generic extraction root-access failure.'
}
$result = [ordered]@{
    Scope = 'Ordinary valid-backup generic wrapper-root access and one-shot file/directory timestamp access-denied failure propagation, fresh retry/commit and exact recovery'
    Mode = $mode; RegressionPassed = -not [bool]$ExpectedBeforeRootFailure
    ExpectedDefectReproduced = [bool]$ExpectedBeforeRootFailure; Checks = $nativeChecks; UnexpectedFailures = 0
    ProductionArchiveItemCap = 500000; ArchiveItemLimitOverride = $false; Cases = 3
    Headless = $true; NoGuiBrowserNetworkClipboardOrDeviceInput = $true
    FixturesRetained = $true; Fixture = $fixture; SourceFencePassed = $true
    Binary = $binary; BinarySha256 = (Get-FileHash -LiteralPath $binary).Hash
    Log = (Join-Path $artifacts 'native.log'); LogSha256 = (Get-FileHash -LiteralPath (Join-Path $artifacts 'native.log')).Hash
    SourceHashes = $sourceBefore
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output $nativeLog
