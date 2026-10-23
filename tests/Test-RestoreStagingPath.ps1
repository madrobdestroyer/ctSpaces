#requires -Version 7.0
param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\build\tests\RestoreStagingPath')
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts = [IO.Path]::GetFullPath($ArtifactDirectory)
if (-not $artifacts.StartsWith((Join-Path $root 'build\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Restore staging path evidence must be below the workspace build directory.'
}
[IO.Directory]::CreateDirectory($artifacts) | Out-Null
$outputDirectory = Join-Path $artifacts 'bin'
$intermediateDirectory = Join-Path $artifacts 'obj'
$project = Join-Path $PSScriptRoot 'RestoreStagingPathTests.vcxproj'
$msbuild = (Get-Command msbuild.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $msbuild) {
    $candidate = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $msbuild = $candidate }
}
if (-not $msbuild -or -not (Test-Path -LiteralPath $msbuild -PathType Leaf)) { throw 'MSBuild was not found.' }
if (-not (Test-Path -LiteralPath (Join-Path $root 'dist\x64\Release\7zip.lib') -PathType Leaf)) { throw 'Build the bundled Release|x64 7zip library first.' }

function Invoke-HiddenTool([string]$Exe, [string[]]$Arguments, [string]$Log) {
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
        if (-not $process.WaitForExit(600000)) {
            $process.Kill($true)
            throw "Restore staging path tool timed out; its owned process tree was stopped. Private fixtures are retained."
        }
        $contents = $stdout.Result + $stderr.Result
        [IO.File]::WriteAllText($Log, $contents)
        [pscustomobject]@{
            Exe = $info.FileName; BinarySha256 = (Get-FileHash -LiteralPath $info.FileName).Hash
            Arguments = $Arguments; CreateNoWindow = $true; Priority = 'BelowNormal'
            ExitCode = $process.ExitCode; Seconds = ([DateTime]::UtcNow - $started).TotalSeconds
            Log = $Log; LogSha256 = (Get-FileHash -LiteralPath $Log).Hash
        } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($Log + '.json')
        if ($process.ExitCode -ne 0) { throw "Restore staging path tool failed; inspect $Log. All private fixtures are retained." }
        return $contents
    } finally { $process.Dispose() }
}

$sources = @('BackupEngine.cpp','BackupEngine.h','InProc7z.cpp','InProc7z.h','RestoreIoPath.h','SiblingStageName.h','ClientNameComparison.h','version.h','tests\RestoreStagingPathTests.cpp','tests\RestoreStagingPathTests.vcxproj','tests\Test-RestoreStagingPath.ps1','dist\x64\Release\7zip.lib')
$sourceBefore = @($sources | ForEach-Object { $path = Join-Path $root $_; [pscustomobject]@{ Path = $path; Sha256 = (Get-FileHash -LiteralPath $path).Hash } })
$sourceBefore | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
$null = Invoke-HiddenTool $msbuild @($project,'/m:1','/nodeReuse:false',"/p:Configuration=$Configuration","/p:Platform=$Platform",('/p:OutDir='+$outputDirectory+'\'),('/p:IntDir='+$intermediateDirectory+'\'),'/v:minimal') (Join-Path $artifacts 'build.log')
$sourceAfter = @($sources | ForEach-Object { $path = Join-Path $root $_; [pscustomobject]@{ Path = $path; Sha256 = (Get-FileHash -LiteralPath $path).Hash } })
$sourceAfter | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifacts 'source-after.json')
if ((ConvertTo-Json -InputObject $sourceBefore -Compress) -ne (ConvertTo-Json -InputObject $sourceAfter -Compress)) { throw 'Inputs changed during restore staging path compilation.' }

# Keep the ordinary fixture prefix short enough to exercise the documented
# Win32 boundary. It is always a new workspace-private directory, retained.
$fixture = Join-Path (Join-Path $root 'build\rsp') ([Guid]::NewGuid().ToString('N').Substring(0,12))
if (Test-Path -LiteralPath $fixture) { throw 'Fresh private fixture path was occupied; existing data was left alone.' }
$nativeLog = Invoke-HiddenTool (Join-Path $outputDirectory 'RestoreStagingPathTests.exe') @('--headless',$fixture) (Join-Path $artifacts 'native.log')
$sourceAfterNative = @($sources | ForEach-Object { $path = Join-Path $root $_; [pscustomobject]@{ Path = $path; Sha256 = (Get-FileHash -LiteralPath $path).Hash } })
$sourceAfterNative | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $artifacts 'source-after-native.json')
if ((ConvertTo-Json -InputObject $sourceBefore -Compress) -ne (ConvertTo-Json -InputObject $sourceAfterNative -Compress)) { throw 'Inputs changed during restore staging path native execution.' }
if ($nativeLog -notmatch 'Restore staging path tests passed: (\d+) checks\.' -or [int]$Matches[1] -ne 74 -or $nativeLog -notmatch 'FAILURES=0') {
    throw 'The complete ordinary restore staging path regression was not confirmed.'
}
$receipt = [ordered]@{
    Scope = 'Ordinary CRC-valid backup/restore through long staging paths, exact final path boundaries, occupied-output diagnostics, one-shot ownership allocation failure/close/retry, and pure Drive/UNC path conversion'
    Checks = 74; Failures = 0; Headless = $true; NoBrowserGuiOrNetwork = $true
    FixturesRetained = $true; Fixture = $fixture; ProductionInputsUnchanged = $true
    Binary = (Join-Path $outputDirectory 'RestoreStagingPathTests.exe')
    BinarySha256 = (Get-FileHash -LiteralPath (Join-Path $outputDirectory 'RestoreStagingPathTests.exe')).Hash
    Log = (Join-Path $artifacts 'native.log'); LogSha256 = (Get-FileHash -LiteralPath (Join-Path $artifacts 'native.log')).Hash
    SourceHashes = $sourceBefore
}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output $nativeLog
