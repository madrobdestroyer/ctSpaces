#requires -Version 7.0
param(
    [ValidateSet('Release')][string]$Configuration = 'Release',
    [ValidateSet('x64')][string]$Platform = 'x64',
    [string]$ArtifactDirectory = ''
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if (-not $ArtifactDirectory) {
    $ArtifactDirectory = Join-Path $root ('build\tests\rhe-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
}
$artifacts = [IO.Path]::GetFullPath($ArtifactDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar)
$fixture = Join-Path $artifacts 'f'
$buildPrefix = (Join-Path $root 'build') + [IO.Path]::DirectorySeparatorChar
if (-not $artifacts.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Hash-efficiency evidence must remain below the workspace build directory.'
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
$project = Join-Path $PSScriptRoot 'RestoreHashEfficiencyTests.vcxproj'
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
[IO.File]::WriteAllText($rootHeader, "#pragma once`r`n#define CTSPACES_HASH_FIXTURE_PARENT L`"$escapedRoot`"`r`n", [Text.UTF8Encoding]::new($false))

function Invoke-HiddenHashEfficiencyTool([string]$Exe, [string[]]$Arguments, [string]$Log, [int]$Timeout) {
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
            throw 'The hidden hash-efficiency tool timed out; its owned process tree was stopped and private evidence retained.'
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
        if ($process.ExitCode -ne 0) { throw ('Hidden hash-efficiency tool failed; inspect ' + $Log + '. All fixtures are retained.') }
        return $contents
    } finally { $process.Dispose() }
}
$sources = @(
    'BackupEngine.cpp', 'BackupEngine.h', 'InProc7z.cpp', 'InProc7z.h', 'RestoreIoPath.h',
    'SiblingStageName.h', 'ClientNameComparison.h', 'version.h',
    'tests\RestoreHashEfficiencyTests.cpp', 'tests\RestoreHashEfficiencyTests.vcxproj',
    'tests\Test-RestoreHashEfficiency.ps1', 'dist\x64\Release\7zip.lib'
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
        throw 'Hash-efficiency regression inputs changed during this run; all evidence is retained.'
    }
}
$null = Invoke-HiddenHashEfficiencyTool $msbuild @(
    $project, '/m:1', '/nodeReuse:false', "/p:Configuration=$Configuration", "/p:Platform=$Platform",
    '/p:BuildProjectReferences=false', ('/p:OutDir=' + $outputDirectory + '\'),
    ('/p:IntDir=' + $intermediateDirectory + '\'), ('/p:HashFixtureRootHeader=' + $rootHeader), '/v:minimal'
) (Join-Path $artifacts 'build.log') 300000
Assert-SourceFence 'source-after-build.json'
$binary = Join-Path $outputDirectory 'RestoreHashEfficiencyTests.exe'
$nativeLog = Invoke-HiddenHashEfficiencyTool $binary @('--headless', $fixture) (Join-Path $artifacts 'native.log') 180000
Assert-SourceFence 'source-after-native.json'
if ($nativeLog -notmatch '(?m)^REGRESSION_PASSED=1\r?$' -or
    $nativeLog -notmatch '(?m)^FILES=2051\r?$' -or
    $nativeLog -notmatch '(?m)^TRIALS=3\r?$') { throw 'The complete bounded hashing comparison was not confirmed.' }
$nativeChecks = [int]([regex]::Match($nativeLog, '(?m)^CHECKS=(\d+)\r?$').Groups[1].Value)
$bytes = [long]([regex]::Match($nativeLog, '(?m)^PAYLOAD_BYTES=(\d+)\r?$').Groups[1].Value)
$beforeSeconds = [double]::Parse([regex]::Match($nativeLog, '(?m)^MEDIAN_BEFORE_SECONDS=([\d.]+)\r?$').Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
$afterSeconds = [double]::Parse([regex]::Match($nativeLog, '(?m)^MEDIAN_AFTER_SECONDS=([\d.]+)\r?$').Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
$largeBeforeSeconds = [double]::Parse([regex]::Match($nativeLog, '(?m)^LARGE_MEDIAN_BEFORE_SECONDS=([\d.]+)\r?$').Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
$largeAfterSeconds = [double]::Parse([regex]::Match($nativeLog, '(?m)^LARGE_MEDIAN_AFTER_SECONDS=([\d.]+)\r?$').Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
if ($nativeChecks -ne 67 -or $bytes -le 0 -or $bytes -gt 32MB -or $beforeSeconds -le 0 -or $afterSeconds -le 0 -or
    $largeBeforeSeconds -le 0 -or $largeAfterSeconds -le 0 -or
    $nativeLog -notmatch '(?m)^BASELINE_TREE_READS=4356\r?$' -or
    $nativeLog -notmatch '(?m)^CURRENT_TREE_READS=4164\r?$' -or
    $nativeLog -notmatch '(?m)^LARGE_TRIALS=5\r?$') { throw 'Incomplete checks/size/timing evidence.' }
$result = [ordered]@{
    Scope = 'Real frozen baseline and optimized SHA-256 hashing on the same <=32MiB private ordinary fixture, independent digest references, provider/read/hash failure retry, lifecycle accounting and recursive capture/corruption checks'
    RegressionPassed = $true; Checks = $nativeChecks; Files = 2051; PayloadBytes = $bytes; PayloadBudgetBytes = 32MB
    Trials = 3; MedianBaselineSeconds = $beforeSeconds; MedianOptimizedSeconds = $afterSeconds
    BaselineProviderOpensPerScan = 2051; OptimizedProviderOpensPerScan = 1
    BaselineBufferBytes = 65536; OptimizedBufferBytes = 262144
    BaselineTreeReads = 4356; OptimizedTreeReads = 4164
    Focused16MiBTrials = 5; FocusedHashesPerTrial = 8
    FocusedBaselineMedianSeconds = $largeBeforeSeconds; FocusedOptimizedMedianSeconds = $largeAfterSeconds
    Headless = $true; NoGuiBrowserNetworkClipboardOrDeviceInput = $true
    FixturesRetained = $true; Fixture = $fixture; SourceFencePassed = $true
    Version = '6.3.5.0'; Published = $false; ProductionScale30GiBClaim = $false
    Binary = $binary; BinarySha256 = (Get-FileHash -LiteralPath $binary).Hash
    Log = (Join-Path $artifacts 'native.log'); LogSha256 = (Get-FileHash -LiteralPath (Join-Path $artifacts 'native.log')).Hash
    SourceHashes = $sourceBefore
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output $nativeLog
