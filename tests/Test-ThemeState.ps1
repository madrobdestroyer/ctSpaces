param(
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\build\tests\ThemeState')
)
$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$resolvedOutput = [IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $resolvedOutput -Force | Out-Null
$binDir = Join-Path $resolvedOutput 'bin'
$objDir = Join-Path $resolvedOutput 'obj'
$buildLog = Join-Path $resolvedOutput 'build.log'
$runLog = Join-Path $resolvedOutput 'run.log'
$errorLog = Join-Path $resolvedOutput 'stderr.log'
$sourcePath = Join-Path $projectRoot 'ctSpaces.cpp'
$sourceHashBefore = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $PSScriptRoot 'ThemeStateTests.vcxproj') /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /p:ThemeStateDefines= "/p:OutDir=$binDir\" "/p:IntDir=$objDir\" /v:minimal *> $buildLog
if ($LASTEXITCODE -ne 0) { throw "Theme-state native build failed. See $buildLog" }
if ((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -ne $sourceHashBefore) {
    throw 'The shared application source changed during the theme-state build; run again after the source freeze.'
}
$binary = Join-Path $binDir 'ThemeStateTests.exe'
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $binary
$info.WorkingDirectory = $projectRoot
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$process = [Diagnostics.Process]::new()
$process.StartInfo = $info
try {
    if (-not $process.Start()) { throw 'Hidden theme-state fixture could not start.' }
    $process.PriorityClass = 'BelowNormal'
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $outputText = $stdout.GetAwaiter().GetResult()
    $errorText = $stderr.GetAwaiter().GetResult()
    [IO.File]::WriteAllText($runLog, $outputText, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText($errorLog, $errorText, [Text.UTF8Encoding]::new($false))
    $banner = [regex]::Match($outputText, '(?m)^Theme state: (\d+) checks, (\d+) scenarios, 0 failures; suppressed presentation=\d+; native show/focus/foreground=0/0/0; forbidden=0\.\r?$')
    if ($process.ExitCode -ne 0 -or -not $banner.Success -or $errorText) {
        throw "Hidden theme-state fixture failed or did not complete. See $runLog and $errorLog"
    }
    if ((Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -ne $sourceHashBefore) {
        throw 'The shared application source changed during the theme-state run.'
    }
    Write-Host $banner.Value
    Write-Host "Theme-state native evidence: $runLog"
} finally {
    $process.Dispose()
}
