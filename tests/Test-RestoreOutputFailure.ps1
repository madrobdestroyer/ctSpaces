#requires -Version 7.0
param(
    [string]$ArtifactDirectory = '',
    [ValidateSet('Before','After')][string]$AttributeProof = 'After'
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $ArtifactDirectory) { $ArtifactDirectory = Join-Path $root ('build\tests\rof-' + [guid]::NewGuid().ToString('N').Substring(0, 8)) }
$artifacts = [IO.Path]::GetFullPath($ArtifactDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar)
if (-not $artifacts.StartsWith((Join-Path $root 'build') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Restore output evidence must stay below workspace build.' }
function Assert-NoReparseAncestor([string]$Path) { $current = [IO.Path]::GetFullPath($Path); while ($current) { if (Test-Path -LiteralPath $current) { if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw ('Reparse path refused: ' + $current) } }; $parent = [IO.Path]::GetDirectoryName($current); if ($parent -eq $current) { break }; $current = $parent } }
Assert-NoReparseAncestor $artifacts
if (Test-Path -LiteralPath $artifacts) { throw 'Use a fresh evidence directory; existing evidence is never replaced or removed.' }
$fixture = Join-Path $artifacts 'f'
if ((Join-Path $fixture 'a\file\l\Sites\Alpha').Length + 150 -gt 259 -or (Join-Path $fixture 'a\file\l\Sites\Alpha').Length + 123 -gt 247) { throw 'Choose a shorter evidence directory for ordinary final client budgets.' }
$msbuild = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) { throw 'MSBuild was not found.' }
New-Item -ItemType Directory -Path $artifacts -ErrorAction Stop | Out-Null; Assert-NoReparseAncestor $artifacts
$header = Join-Path $artifacts 'fixture-root.h'; $escaped = $artifacts.Replace('\', '\\').Replace('"', '\"')
[IO.File]::WriteAllText($header, "#pragma once`r`n#define CTSPACES_RESTORE_OUTPUT_FIXTURE_PARENT L`"$escaped`"`r`n", [Text.UTF8Encoding]::new($false))
$sourceNames = @('BackupEngine.cpp', 'BackupEngine.h', 'InProc7z.cpp', 'InProc7z.h', 'RestoreIoPath.h', 'SiblingStageName.h', 'ClientNameComparison.h', 'version.h', 'tests\RestoreOutputFailureTests.cpp', 'tests\RestoreOutputFailureTests.vcxproj', 'tests\RestoreOutputFailureStreamBoundary.h', 'tests\Test-RestoreOutputFailure.ps1', '3p\7zip\CPP\7zip\Common\FileStreams.h', '3p\7zip\CPP\Windows\FileDir.h', '3p\7zip\CPP\Windows\FileIO.h', 'dist\x64\Release\7zip.lib')
$paths = @($sourceNames | ForEach-Object { Join-Path $root $_ }) + @($header)
$sources = @($paths | ForEach-Object { [pscustomobject]@{Path = $_; Sha256 = (Get-FileHash -LiteralPath $_).Hash} })
$sources | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
function Assert-Frozen { foreach ($source in $sources) { if ((Get-FileHash -LiteralPath $source.Path).Hash -ne $source.Sha256) { throw ('Restore output input changed: ' + $source.Path) } } }
function Invoke-Hidden([string]$Exe, [string[]]$Arguments, [string]$Log, [int]$Timeout = 300000) {
    Assert-Frozen
    $info = [Diagnostics.ProcessStartInfo]::new($Exe); $info.WorkingDirectory = $root; $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true; foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $started = [DateTime]::UtcNow; $process = [Diagnostics.Process]::Start($info)
    try {
        try { if (-not $process.HasExited) { $process.PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal } } catch [InvalidOperationException] { if (-not $process.HasExited) { throw } }
        $stdout = $process.StandardOutput.ReadToEndAsync(); $stderr = $process.StandardError.ReadToEndAsync(); $timedOut = -not $process.WaitForExit($Timeout)
        if ($timedOut) { $process.Kill($true); $process.WaitForExit() }
        $text = $stdout.Result + $stderr.Result; [IO.File]::WriteAllText($Log, $text)
        [ordered]@{Exe = $Exe; BinarySha256 = (Get-FileHash -LiteralPath $Exe).Hash; Arguments = $Arguments; ExitCode = $process.ExitCode; TimedOut = $timedOut; Seconds = ([DateTime]::UtcNow - $started).TotalSeconds; CreateNoWindow = $true; Priority = 'BelowNormal'; LogSha256 = (Get-FileHash -LiteralPath $Log).Hash} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($Log + '.json')
        Assert-Frozen
        if ($timedOut -or $process.ExitCode -ne 0) { throw ('Hidden restore output tool failed; inspect ' + $Log) }; return $text
    } finally { $process.Dispose() }
}
$bin = Join-Path $artifacts 'bin'; $obj = Join-Path $artifacts 'obj'
$null = Invoke-Hidden $msbuild @((Join-Path $PSScriptRoot 'RestoreOutputFailureTests.vcxproj'), '/m:1', '/nodeReuse:false', '/p:Configuration=Release', '/p:Platform=x64', '/p:BuildProjectReferences=false', ('/p:OutDir=' + $bin + '\'), ('/p:IntDir=' + $obj + '\'), ('/p:RestoreOutputFixtureRootHeader=' + $header), '/v:minimal') (Join-Path $artifacts 'build.log')
$binary = Join-Path $bin 'RestoreOutputFailureTests.exe'
$native = Invoke-Hidden $binary @('--headless', $fixture, ('--attribute-' + $AttributeProof.ToLowerInvariant())) (Join-Path $artifacts 'native.log') 90000
$attributeStatus = if ($AttributeProof -eq 'Before') { '0x00000000' } else { '0x80070005' }
$markers = @('STREAM_RESULT_SIMULATION_ONLY=1', 'NATIVE_WRITEFILE_INJECTION=0', 'DISK_EXHAUSTION_TESTED=0', 'FLUSH_FAILURE_TESTED=0', 'SHORT_WRITE_COMPLETED=1', 'PARTIAL_DISKFULL_REJECTED=1', 'ZERO_CONTRACT_VIOLATION_SAFE=1', 'CLOSE_RESULT_REJECTED=1', 'FOREIGN_ADDITION_RETAINED=1', 'FRESH_RETRY_COMMITTED=1', 'VERSION=6.3.5.0', 'CASES=8', 'UNEXPECTED_FAILURES=0', 'REGRESSION_PASSED=1',
    'ATTRIBUTE_REFUSAL_SIMULATION_ONLY=1', 'ATTRIBUTE_RETRY_COMMITTED=1', "ATTRIBUTE_REFUSAL_MODE=$AttributeProof",
    "RESTORE_ATTR_FILE_HRESULT=$attributeStatus", "RESTORE_ATTR_DIRECTORY_HRESULT=$attributeStatus")
foreach ($marker in $markers) { if ($native -notmatch ('(?m)^' + [regex]::Escape($marker) + '\r?$')) { throw ('Restore output scope marker missing: ' + $marker) } }
if ($native -notmatch '(?m)^CHECKS=(\d+)\r?$') { throw 'Restore output check count missing.' }; $checks = [int]$Matches[1]
if ($native -notmatch '(?m)^PHYSICAL_FIXTURE_BYTES=(\d+)\r?$') { throw 'Restore output fixture size missing.' }; $bytes = [long]$Matches[1]
if ($native -notmatch '(?m)^ZERO_CONTRACT_OUTCOME=(RecoveredExact|RejectedUnsealed)\r?$') { throw 'Zero-progress contract-violation outcome missing.' }; $zeroOutcome = $Matches[1]
if ($checks -ne 59 -or $bytes -gt 2MB) { throw 'Restore output scope or byte budget was not confirmed.' }
$before = $AttributeProof -eq 'Before'
[ordered]@{Passed = $true; Status = if ($before) { 'ExpectedDefectReproduced' } else { 'Passed' }; Version = '6.3.5.0'; Checks = $checks; Cases = 8; PhysicalFixtureBytes = $bytes; FixtureBudgetBytes = 2MB; StreamResultSimulationOnly = $true; NativeWriteFileInjection = $false; DiskExhaustionTested = $false; FlushFailureTested = $false; ZeroProgressProducerContractViolation = $true; ZeroProgressOutcome = $zeroOutcome; AttributeProof = $AttributeProof; AttributeRefusalSimulationOnly = $true; AttributeRefusalCases = 2; AttributeRetryCommitted = $true; ExpectedDefects = if ($before) { 2 } else { 0 }; LibraryUnmodified = $true; SourceFencePassed = $true; Headless = $true; Binary = $binary; BinarySha256 = (Get-FileHash -LiteralPath $binary).Hash; Fixture = $fixture; FixturesRetained = $true; SourceHashes = $sources} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output $native
