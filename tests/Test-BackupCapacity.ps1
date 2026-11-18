#requires -Version 7.0
param([string]$ArtifactDirectory = '')
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $ArtifactDirectory) { $ArtifactDirectory = Join-Path $root ('build\tests\bc-' + [guid]::NewGuid().ToString('N').Substring(0, 8)) }
$artifacts = [IO.Path]::GetFullPath($ArtifactDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar)
if (-not $artifacts.StartsWith((Join-Path $root 'build') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Capacity evidence must stay below the workspace build directory.' }
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
if (Test-Path -LiteralPath $artifacts) { throw 'Capacity evidence must use a fresh directory; existing files are never replaced or removed.' }
$msbuild = (Get-Command msbuild.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $msbuild) { $msbuild = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe' }
if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) { throw 'MSBuild was not found.' }
$sourceNames = @('BackupEngine.cpp', 'BackupEngine.h', 'InProc7z.cpp', 'InProc7z.h', 'RestoreIoPath.h', 'SiblingStageName.h', 'ClientNameComparison.h', 'version.h', 'tests\BackupCapacityTests.cpp', 'tests\BackupCapacityTests.vcxproj', 'tests\Test-BackupCapacity.ps1', 'dist\x64\Release\7zip.lib')
$sources = @($sourceNames | ForEach-Object { $path = Join-Path $root $_; [pscustomobject]@{Path = $path; Sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash} })
New-Item -ItemType Directory -Path $artifacts -ErrorAction Stop | Out-Null
Assert-NoReparseAncestor $artifacts
$sources | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
function Assert-Frozen {
    foreach ($source in $sources) {
        if ((Get-FileHash -LiteralPath $source.Path -Algorithm SHA256).Hash -ne $source.Sha256) { throw ('Capacity inputs changed: ' + $source.Path) }
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
        if ($timedOut -or $process.ExitCode -ne 0) { throw ('Hidden capacity tool failed; inspect ' + $Log) }
        return $text
    } finally { $process.Dispose() }
}
$bin = Join-Path $artifacts 'bin'; $obj = Join-Path $artifacts 'obj'
$null = Invoke-Hidden $msbuild @((Join-Path $PSScriptRoot 'BackupCapacityTests.vcxproj'), '/m:1', '/nodeReuse:false', '/p:Configuration=Release', '/p:Platform=x64', '/p:BuildProjectReferences=false', ('/p:OutDir=' + $bin + '\'), ('/p:IntDir=' + $obj + '\'), '/v:minimal') (Join-Path $artifacts 'build.log')
$binary = Join-Path $bin 'BackupCapacityTests.exe'
$native = Invoke-Hidden $binary @('--headless') (Join-Path $artifacts 'native.log') 60000
if ($native -notmatch '(?m)^CHECKS=(\d+)\r?$') { throw 'Capacity check count was missing.' }
$checks = [int]$Matches[1]
if ($checks -ne 27 -or $native -notmatch '(?m)^UNEXPECTED_FAILURES=0\r?$' -or $native -notmatch '(?m)^PHYSICAL_PAYLOAD_BYTES=0\r?$' -or $native -notmatch '(?m)^SYNTHETIC_THIRTY_GIB_CHECKED=1\r?$' -or $native -notmatch '(?m)^VERSION=6\.3\.5\.0\r?$') { throw 'The complete metadata-only capacity scope was not confirmed.' }
[ordered]@{Passed = $true; Version = '6.3.5.0'; Checks = $checks; PhysicalPayloadBytes = 0; MetadataOnly = $true; Scope = 'Actual restore capacity arithmetic with mocked caller-available/total-free space; 4/30/256 GiB, production item limits, exact thresholds and native errors'; ArithmeticOverflowBranchCovered = $false; OutOfRangeTotalsRejectedByPriorCaps = $true; SourceFencePassed = $true; Headless = $true; Binary = $binary; BinarySha256 = (Get-FileHash -LiteralPath $binary).Hash; Log = (Join-Path $artifacts 'native.log'); SourceHashes = $sources} | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output $native
