#requires -Version 7.0
param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [switch]$StagingNamespaceOnly,
    [string]$ArtifactDirectory = (Join-Path $PSScriptRoot '..\build\tests\DefaultSaveTransaction')
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts = [IO.Path]::GetFullPath($ArtifactDirectory)
if (-not $artifacts.StartsWith((Join-Path $root 'build\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Default save transaction evidence must be below the workspace build directory.'
}
New-Item -ItemType Directory -Path $artifacts -Force | Out-Null
$outputDirectory = Join-Path $artifacts 'bin'
$intermediateDirectory = Join-Path $artifacts 'obj'
$project = Join-Path $PSScriptRoot 'DefaultSaveTransactionTests.vcxproj'
$testSource = Join-Path $PSScriptRoot 'DefaultSaveTransactionTests.cpp'
$sourceDirectory = Join-Path $artifacts 'source'
New-Item -ItemType Directory -Path $sourceDirectory -Force | Out-Null
$applicationSource = Join-Path $root 'ctSpaces.cpp'
$sourceBytes = [IO.File]::ReadAllBytes($applicationSource)
$sourceText = [Text.Encoding]::UTF8.GetString($sourceBytes)
$sourceSha256 = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($sourceBytes))
$validation = [regex]::Match($sourceText, '(?s)static bool ValidateSanitizedDefaultProfileTree\(.*?(?=static void HandleClosedDefaultProfile\()')
$save = [regex]::Match($sourceText, '(?s)static void HandleClosedDefaultProfile\([^;{}]*\)\s*\{.*?(?=static bool HandleProfileExitOnUiThread\()')
if (-not $validation.Success -or -not $save.Success) { throw 'The reviewed Default-save functions could not be extracted.' }
$sourceSnapshot = Join-Path $sourceDirectory 'DefaultSaveTransactionSource.inl'
[IO.File]::WriteAllText($sourceSnapshot, $validation.Value + $save.Value)
[pscustomobject]@{ Source = $applicationSource; SourceSha256 = $sourceSha256; Extracted = $sourceSnapshot; ExtractedSha256 = (Get-FileHash -LiteralPath $sourceSnapshot).Hash } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifacts 'extracted-source.json')
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
        try {
            if (-not $process.HasExited) { $process.PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal }
        } catch [InvalidOperationException] {
            if (-not $process.HasExited) { throw }
        } catch [ComponentModel.Win32Exception] {
            if (-not $process.HasExited) { throw }
        }
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
        if ($process.ExitCode -ne 0) { throw "Default save transaction tool failed; inspect $Log" }
        return $contents
    } finally { $process.Dispose() }
}

$compileHashes = @('ctSpaces.cpp','ClientDeletionSafety.h','TransientProfileIdentity.h','InProc7z.cpp','InProc7z.h','tests\DefaultSaveTransactionTests.cpp','tests\DefaultSaveTransactionTests.vcxproj','version.h','Default.7z') |
    ForEach-Object { $path = Join-Path $root $_; [pscustomobject]@{ Path = $path; Sha256 = (Get-FileHash -LiteralPath $path).Hash } }
$compileHashes | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $artifacts 'source-hashes.json')
if ((Get-FileHash -LiteralPath $applicationSource).Hash -ne $sourceSha256) {
    throw 'Source changed while extracting the Default-save transaction.'
}
$buildArguments = @($project, '/m:1', '/nodeReuse:false', "/p:Configuration=$Configuration", "/p:Platform=$Platform",
                    ('/p:OutDir=' + $outputDirectory + '\'), ('/p:IntDir=' + $intermediateDirectory + '\'), ('/p:DefaultSaveSourceDirectory=' + $sourceDirectory))
$null = Invoke-HiddenFixtureTool -Exe $msbuild -Arguments $buildArguments -Log (Join-Path $artifacts 'build.log')
foreach ($record in $compileHashes) {
    if ((Get-FileHash -LiteralPath $record.Path).Hash -ne $record.Sha256) { throw 'Source changed during Default save transaction compilation.' }
}
$nativeArguments = if ($StagingNamespaceOnly) { @('--stage-namespace-only') } else { @() }
$testLog = Invoke-HiddenFixtureTool -Exe (Join-Path $outputDirectory 'DefaultSaveTransactionTests.exe') -Arguments $nativeArguments -Log (Join-Path $artifacts 'native.log')
$completionPattern = if ($StagingNamespaceOnly) { 'Default staging namespace checks passed: (\d+) \(2 scenarios\)\.' } else { 'Default save transaction checks passed: (\d+) \(11 scenarios\)\.' }
$minimumChecks = if ($StagingNamespaceOnly) { 15 } else { 100 }
if ($testLog -notmatch $completionPattern -or [int]$Matches[1] -lt $minimumChecks) {
    throw 'The complete native Default save transaction scope was not confirmed.'
}
Write-Output $testLog
