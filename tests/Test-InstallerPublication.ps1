#requires -Version 7.0
param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\build\tests\InstallerPublication')
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts = [IO.Path]::GetFullPath($ArtifactDirectory)
if (-not $artifacts.StartsWith((Join-Path $root 'build\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Installer publication evidence must be below the workspace build directory.'
}
New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
$outputDirectory = Join-Path $artifacts 'bin'
$intermediateDirectory = Join-Path $artifacts 'obj'
$project = Join-Path $PSScriptRoot 'InstallerPublicationTests.vcxproj'
$testSource = Join-Path $PSScriptRoot 'InstallerPublicationTests.cpp'
$source = Get-Content -Raw -LiteralPath $testSource
if ($source -notmatch '#define\s+wWinMain\s+CtSpacesPublicationUnusedWinMain' -or
    $source -notmatch 'CopyFileWithRetry\(current,\s*installed' -or
    $source -notmatch 'RunCase\(Fault::CreateCollision' -or
    $source -notmatch 'RunCase\(Fault::ReplaceBeforeCommit' -or
    $source -notmatch 'RunCase\(Fault::FailCleanup') {
    throw 'The fixture no longer exposes the reviewed production copy and ownership failure cases.'
}

function Test-RegularFile([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    return ((Get-Item -LiteralPath $Path -Force).Attributes -band
            [IO.FileAttributes]::ReparsePoint) -eq 0
}

$msbuild = (Get-Command msbuild.exe -CommandType Application -ErrorAction SilentlyContinue |
            Select-Object -First 1).Source
if (-not $msbuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-RegularFile $vswhere) {
        # vswhere is an inert console inventory tool; keep its process hidden too.
        $locatorInfo = [Diagnostics.ProcessStartInfo]::new($vswhere)
        $locatorInfo.UseShellExecute = $false
        $locatorInfo.CreateNoWindow = $true
        $locatorInfo.RedirectStandardOutput = $true
        $locatorInfo.RedirectStandardError = $true
        foreach ($argument in @('-latest','-products','*','-requires','Microsoft.Component.MSBuild','-property','installationPath')) {
            $locatorInfo.ArgumentList.Add($argument)
        }
        $locator = [Diagnostics.Process]::Start($locatorInfo)
        try {
            try {
                if (-not $locator.HasExited) { $locator.PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal }
            } catch [InvalidOperationException] {
                if (-not $locator.HasExited) { throw }
            }
            $located = $locator.StandardOutput.ReadToEndAsync()
            $locatorError = $locator.StandardError.ReadToEndAsync()
            $locator.WaitForExit()
            if ($locator.ExitCode -eq 0) {
                $installation = ($located.Result -split '\r?\n' | Where-Object { $_.Trim() } | Select-Object -First 1)
                if ($installation) { $msbuild = Join-Path $installation 'MSBuild\Current\Bin\MSBuild.exe' }
            }
        } finally { $locator.Dispose() }
    }
}
if (-not $msbuild -or -not (Test-RegularFile $msbuild)) { throw 'MSBuild was not found.' }
if (-not (Test-RegularFile (Join-Path $root 'dist\x64\Release\7zip.lib'))) { throw 'Build the bundled Release|x64 7zip library first.' }

function Invoke-HiddenFixtureTool([string]$Exe, [string[]]$Arguments, [string]$Log) {
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
        if (-not $process.HasExited) { $process.PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $process.WaitForExit()
        $contents = $stdout.Result + $stderr.Result
        [IO.File]::WriteAllText($Log, $contents)
        [pscustomobject]@{
            Exe = $info.FileName; BinarySha256 = (Get-FileHash -LiteralPath $info.FileName).Hash
            Arguments = $Arguments; CreateNoWindow = $true; Priority = 'BelowNormal'
            ExitCode = $process.ExitCode; Seconds = ([DateTime]::UtcNow - $started).TotalSeconds
            Log = $Log; LogSha256 = (Get-FileHash -LiteralPath $Log).Hash
        } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ($Log + '.json')
        if ($process.ExitCode -ne 0) { throw "Installer publication tool failed; inspect $Log" }
        return $contents
    } finally { $process.Dispose() }
}

$compileHashes = @('ctSpaces.cpp','tests\InstallerPublicationTests.cpp','tests\InstallerPublicationTests.vcxproj','version.h') |
    ForEach-Object { $path = Join-Path $root $_; [pscustomobject]@{ Path = $path; Sha256 = (Get-FileHash -LiteralPath $path).Hash } }
$compileHashes | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifacts 'source-hashes.json')
$buildArguments = @($project, '/m:1', '/nodeReuse:false', "/p:Configuration=$Configuration", "/p:Platform=$Platform",
                    ('/p:OutDir=' + $outputDirectory + '\'), ('/p:IntDir=' + $intermediateDirectory + '\'))
$null = Invoke-HiddenFixtureTool -Exe $msbuild -Arguments $buildArguments -Log (Join-Path $artifacts 'build.log')
foreach ($record in $compileHashes) {
    if ((Get-FileHash -LiteralPath $record.Path).Hash -ne $record.Sha256) { throw 'Source changed during installer publication compilation.' }
}
$testLog = Invoke-HiddenFixtureTool -Exe (Join-Path $outputDirectory 'InstallerPublicationTests.exe') -Arguments @() -Log (Join-Path $artifacts 'native.log')
if ($testLog -notmatch 'Installer publication boundary checks passed: (\d+)\.' -or [int]$Matches[1] -lt 70) {
    throw 'The complete native installer publication scope was not confirmed.'
}
Write-Output $testLog
