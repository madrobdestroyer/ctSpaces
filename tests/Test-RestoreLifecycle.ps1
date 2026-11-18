#requires -Version 7.0
param([string]$ArtifactDirectory = '', [switch]$ExpectedTerminalCallbackFailure)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $ArtifactDirectory) { $ArtifactDirectory = Join-Path $root ('build\tests\rl-' + [guid]::NewGuid().ToString('N').Substring(0, 8)) }
$artifacts = [IO.Path]::GetFullPath($ArtifactDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar)
if (-not $artifacts.StartsWith((Join-Path $root 'build') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Restore lifecycle evidence must stay below the workspace build directory.' }
function Assert-NoReparseAncestor([string]$Path) {
    $current = [IO.DirectoryInfo]::new($Path)
    while ($null -ne $current) {
        if (Test-Path -LiteralPath $current.FullName) {
            $item = Get-Item -LiteralPath $current.FullName -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw ('Reparse output ancestor refused: ' + $item.FullName) }
        }
        $current = $current.Parent
    }
}
Assert-NoReparseAncestor $artifacts
if (Test-Path -LiteralPath $artifacts) { throw 'Restore lifecycle evidence must use a fresh directory; existing evidence is never replaced or removed.' }
$fixture = Join-Path $artifacts 'f'
if ((Join-Path $fixture 'l\Sites\Alpha').Length + 150 -gt 259 -or (Join-Path $fixture 'l\Sites\Alpha').Length + 123 -gt 247) { throw 'Choose a shorter evidence directory so final client roots fit production path budgets.' }
$msbuild = (Get-Command msbuild.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $msbuild) { $msbuild = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' }
if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) { throw 'MSBuild was not found.' }
New-Item -ItemType Directory -Path $artifacts -ErrorAction Stop | Out-Null
Assert-NoReparseAncestor $artifacts
$header = Join-Path $artifacts 'fixture-root.h'
$escapedRoot = $artifacts.Replace('\', '\\').Replace('"', '\"')
[IO.File]::WriteAllText($header, "#pragma once`r`n#define CTSPACES_RESTORE_LIFECYCLE_FIXTURE_PARENT L`"$escapedRoot`"`r`n", [Text.UTF8Encoding]::new($false))
$sourceNames = @('BackupEngine.cpp', 'BackupEngine.h', 'InProc7z.cpp', 'InProc7z.h', 'RestoreIoPath.h', 'SiblingStageName.h', 'ClientNameComparison.h', 'version.h', 'tests\RestoreLifecycleTests.cpp', 'tests\RestoreLifecycleTests.vcxproj', 'tests\Test-RestoreLifecycle.ps1', 'dist\x64\Release\7zip.lib')
$paths = @($sourceNames | ForEach-Object { Join-Path $root $_ }) + @($header)
$sources = @($paths | ForEach-Object { [pscustomobject]@{Path = $_; Sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash} })
$sources | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
function Assert-Frozen {
    foreach ($source in $sources) {
        if ((Get-FileHash -LiteralPath $source.Path -Algorithm SHA256).Hash -ne $source.Sha256) { throw ('Restore lifecycle inputs changed: ' + $source.Path) }
    }
}
function Invoke-Hidden([string]$Exe, [string[]]$Arguments, [string]$Log, [int]$Timeout = 300000, [int]$ExpectedExitCode = 0) {
    Assert-Frozen
    $info = [Diagnostics.ProcessStartInfo]::new($Exe)
    $info.WorkingDirectory = $root; $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $started = [DateTime]::UtcNow; $process = [Diagnostics.Process]::Start($info)
    try {
        try { if (-not $process.HasExited) { $process.PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal } }
        catch [InvalidOperationException] { if (-not $process.HasExited) { throw } }
        $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync()
        $timedOut = -not $process.WaitForExit($Timeout)
        if ($timedOut) { $process.Kill($true); $process.WaitForExit() }
        $text = $stdout.Result + $stderr.Result; [IO.File]::WriteAllText($Log, $text)
        [ordered]@{Exe = $Exe; BinarySha256 = (Get-FileHash -LiteralPath $Exe).Hash; Arguments = $Arguments; ExitCode = $process.ExitCode; ExpectedExitCode = $ExpectedExitCode; TimedOut = $timedOut; Seconds = ([DateTime]::UtcNow - $started).TotalSeconds; CreateNoWindow = $true; Priority = 'BelowNormal'; LogSha256 = (Get-FileHash -LiteralPath $Log).Hash} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($Log + '.json')
        Assert-Frozen
        if ($timedOut -or $process.ExitCode -ne $ExpectedExitCode) { throw ('Hidden restore lifecycle tool failed; inspect ' + $Log) }
        return $text
    } finally { $process.Dispose() }
}
$bin = Join-Path $artifacts 'bin'; $obj = Join-Path $artifacts 'obj'
$null = Invoke-Hidden $msbuild @((Join-Path $PSScriptRoot 'RestoreLifecycleTests.vcxproj'), '/m:1', '/nodeReuse:false', '/p:Configuration=Release', '/p:Platform=x64', '/p:BuildProjectReferences=false', ('/p:OutDir=' + $bin + '\'), ('/p:IntDir=' + $obj + '\'), ('/p:RestoreLifecycleFixtureRootHeader=' + $header), '/v:minimal') (Join-Path $artifacts 'build.log')
$binary = Join-Path $bin 'RestoreLifecycleTests.exe'
$expectedExit = if ($ExpectedTerminalCallbackFailure) { 1 } else { 0 }
$native = Invoke-Hidden $binary @('--headless', $fixture) (Join-Path $artifacts 'native.log') 90000 $expectedExit
if ($native -notmatch '(?m)^CHECKS=(\d+)\r?$') { throw 'Restore lifecycle check count was missing.' }
$checks = [int]$Matches[1]
if ($native -notmatch '(?m)^PHYSICAL_FIXTURE_BYTES=(\d+)\r?$') { throw 'Restore lifecycle fixture size was missing.' }
$bytes = [long]$Matches[1]
$expectedContainmentFailures = if ($ExpectedTerminalCallbackFailure) { 3 } else { 0 }
$expectedPassed = if ($ExpectedTerminalCallbackFailure) { 0 } else { 1 }
$markers = @('ORDINARY_VALID_ARCHIVE=1', 'DIRECT_FAILED_STAGE_COMMIT_REJECTED=1', 'REUSED_OUTPUT_CLEARED=1', 'FIRST_AND_COMPLETION_PROGRESS_ABORTED=1', 'RETAINED_SEAL_COMMITTED=1', 'STALE_COMMIT_REJECTED=1', 'VERSION=6.3.5.0', 'BASE_CHECKS=51', 'BASE_CASES=7', 'CASES=11', 'EXACT_TERMINAL_CASES=4', 'EXACT_TERMINAL_HITS=4', "EXACT_TERMINAL_ESCAPES=$expectedContainmentFailures", "TERMINAL_CONTAINMENT_FAILURES=$expectedContainmentFailures", 'EXACT_TERMINAL_OWNERSHIP_PASSED=1', 'EXACT_TERMINAL_FRESH_RETRY_PASSED=1', 'UNEXPECTED_FAILURES=0', "REGRESSION_PASSED=$expectedPassed")
foreach ($marker in $markers) { if ($native -notmatch ('(?m)^' + [regex]::Escape($marker) + '\r?$')) { throw ('Restore lifecycle scope marker missing: ' + $marker) } }
if ($checks -lt 75 -or $bytes -gt 2MB) { throw 'Restore lifecycle assertion count or fixture byte budget was not confirmed.' }
[ordered]@{Passed = -not [bool]$ExpectedTerminalCallbackFailure; ExpectedTerminalCallbackFailure = [bool]$ExpectedTerminalCallbackFailure; Version = '6.3.5.0'; Checks = $checks; Cases = 11; BaseChecks = 51; BaseCases = 7; ExactTerminalCases = 4; ExactTerminalHits = 4; TerminalContainmentFailures = $expectedContainmentFailures; TerminalEscapes = $expectedContainmentFailures; DataOwnershipFailures = 0; PhysicalFixtureBytes = $bytes; FixtureBudgetBytes = 2MB; Scope = 'Ordinary valid-backup lifecycle plus exact null-item terminal legacy callback std/nonstd containment in StageValidated7zRestore and _7zTest7z'; NativeFaultInjection = $false; MalformedArchives = $false; SourceFencePassed = $true; Headless = $true; Fixture = $fixture; FixturesRetained = $true; Binary = $binary; BinarySha256 = (Get-FileHash -LiteralPath $binary).Hash; Log = (Join-Path $artifacts 'native.log'); SourceHashes = $sources} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output $native
