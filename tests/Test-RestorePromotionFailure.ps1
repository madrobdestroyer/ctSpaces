#requires -Version 7.0
param([string]$ArtifactDirectory = '')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $ArtifactDirectory) { $ArtifactDirectory = Join-Path $root ('build\tests\rpf-' + [guid]::NewGuid().ToString('N').Substring(0, 8)) }
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
if ((Join-Path $fixture '0\l\Sites\Alpha').Length + 150 -gt 259 -or (Join-Path $fixture '0\l\Sites\Alpha').Length + 123 -gt 247) { throw 'Choose a shorter evidence directory so final client roots fit production path budgets.' }
$msbuild = (Get-Command msbuild.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $msbuild) { $msbuild = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' }
if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) { throw 'MSBuild was not found.' }
New-Item -ItemType Directory -Path $artifacts -ErrorAction Stop | Out-Null
Assert-NoReparseAncestor $artifacts
$header = Join-Path $artifacts 'fixture-root.h'
$escapedRoot = $artifacts.Replace('\', '\\').Replace('"', '\"')
[IO.File]::WriteAllText($header, "#pragma once`r`n#define CTSPACES_RESTORE_PROMOTION_FIXTURE_PARENT L`"$escapedRoot`"`r`n", [Text.UTF8Encoding]::new($false))
$sourceNames = @('BackupEngine.cpp', 'BackupEngine.h', 'InProc7z.cpp', 'InProc7z.h', 'RestoreIoPath.h', 'SiblingStageName.h', 'ClientNameComparison.h', 'version.h', 'tests\RestorePromotionFailureTests.cpp', 'tests\RestorePromotionFailureTests.vcxproj', 'tests\Test-RestorePromotionFailure.ps1', 'tests\RestorePromotionNativeBoundary.h', 'dist\x64\Release\7zip.lib')
$paths = @($sourceNames | ForEach-Object { Join-Path $root $_ }) + @($header)
$sources = @($paths | ForEach-Object { [pscustomobject]@{Path = $_; Sha256 = (Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash} })
$sources | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
function Assert-Frozen {
    foreach ($source in $sources) {
        if ((Get-FileHash -LiteralPath $source.Path -Algorithm SHA256).Hash -ne $source.Sha256) { throw ('Restore lifecycle inputs changed: ' + $source.Path) }
    }
}
function Invoke-Hidden([string]$Exe, [string[]]$Arguments, [string]$Log, [int]$Timeout = 300000) {
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
        [ordered]@{Exe = $Exe; BinarySha256 = (Get-FileHash -LiteralPath $Exe).Hash; Arguments = $Arguments; ExitCode = $process.ExitCode; TimedOut = $timedOut; Seconds = ([DateTime]::UtcNow - $started).TotalSeconds; CreateNoWindow = $true; Priority = 'BelowNormal'; LogSha256 = (Get-FileHash -LiteralPath $Log).Hash} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($Log + '.json')
        Assert-Frozen
        if ($timedOut -or $process.ExitCode -ne 0) { throw ('Hidden restore promotion failure tool failed; inspect ' + $Log) }
        return $text
    } finally { $process.Dispose() }
}
$bin = Join-Path $artifacts 'bin'; $obj = Join-Path $artifacts 'obj'
$null = Invoke-Hidden $msbuild @((Join-Path $PSScriptRoot 'RestorePromotionFailureTests.vcxproj'), '/m:1', '/nodeReuse:false', '/p:Configuration=Release', '/p:Platform=x64', '/p:BuildProjectReferences=false', ('/p:OutDir=' + $bin + '\'), ('/p:IntDir=' + $obj + '\'), ('/p:RestorePromotionFailureFixtureRootHeader=' + $header), '/v:minimal') (Join-Path $artifacts 'build.log')
$binary = Join-Path $bin 'RestorePromotionFailureTests.exe'
$native = Invoke-Hidden $binary @('--headless', $fixture) (Join-Path $artifacts 'native.log') 90000
if ($native -notmatch '(?m)^CHECKS=(\d+)\r?$') { throw 'Restore lifecycle check count was missing.' }
$checks = [int]$Matches[1]
if ($native -notmatch '(?m)^PHYSICAL_FIXTURE_BYTES=(\d+)\r?$') { throw 'Restore lifecycle fixture size was missing.' }
$bytes = [long]$Matches[1]
$markers = @('ORDINARY_VALID_ARCHIVES=1', 'VERSION=6.3.5.0', 'CASES=3', 'UNEXPECTED_FAILURES=0', 'REGRESSION_PASSED=1',
 'CASE=0 POST_PROMOTION_READ_FAULT=1 ORIGINAL_MOVES=1 PROMOTIONS=1 PAYLOAD_RETURNS=1 ORIGINAL_RETURNS=1',
 'CASE=1 POST_PROMOTION_READ_FAULT=1 ORIGINAL_MOVES=1 PROMOTIONS=1 PAYLOAD_RETURNS=0 ORIGINAL_RETURNS=0',
 'CASE=2 POST_PROMOTION_READ_FAULT=1 ORIGINAL_MOVES=1 PROMOTIONS=1 PAYLOAD_RETURNS=1 ORIGINAL_RETURNS=0')
foreach ($marker in $markers) { if ($native -notmatch ('(?m)^' + [regex]::Escape($marker) + '\r?$')) { throw ('Restore promotion failure scope marker missing: ' + $marker) } }
if ($checks -ne 52 -or $bytes -gt 2MB -or $bytes -le 0) { throw 'Restore promotion failure assertion count or fixture byte budget was not confirmed.' }
$oldSeal=[regex]::Match($native,'(?m)^OLD_SEAL_STALE=([01]) GUARD_CALLS=([01])\r?$')
if(-not $oldSeal.Success){throw 'Old seal behavior was not reported.'}
[ordered]@{Passed = $true; Version = '6.3.5.0'; Checks = $checks; Cases = 3; PhysicalFixtureBytes = $bytes; FixtureBudgetBytes = 2MB; Scope = 'Ordinary valid-backup installed-verification read failure after both actual forward renames, exact rollback, occupied staging/live ownership races, old-seal validation and fresh-stage retry'; NativeFaultInjection = 'One ERROR_READ_FAULT only on the exact installed payload identity; ordinary CREATE_NEW competing directories'; NativeBoundaryTranslationUnits = @('BackupEngine.cpp'); MalformedArchives = $false; SourceFencePassed = $true; Headless = $true; NoGuiBrowserNetworkClipboardDeviceInput = $true; Fixture = $fixture; FixturesRetained = $true; OldSealStaleAfterRollback = ($oldSeal.Groups[1].Value -eq '1'); OldSealProbeGuardCalls = [int]$oldSeal.Groups[2].Value; ProductionSourceEdited = $false; Binary = $binary; BinarySha256 = (Get-FileHash -LiteralPath $binary).Hash; Log = (Join-Path $artifacts 'native.log'); LogSha256 = (Get-FileHash -LiteralPath (Join-Path $artifacts 'native.log')).Hash; SourceHashes = $sources} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output $native
