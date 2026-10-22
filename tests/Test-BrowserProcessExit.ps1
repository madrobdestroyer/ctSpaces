param([switch]$SourceOnly)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Get-Content -LiteralPath (Join-Path $root 'ctSpaces.cpp') -Raw
foreach ($required in @('#include "BrowserProcessExit.h"',
    'OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE',
    'browser_process_exit::ConfirmExitAfterQueryFailure(process, finalStatus)',
    'commandError, processExited)', 'if (processExited)')) {
    if (-not $source.Contains($required)) { throw "Missing exit-guard production integration: $required" }
}
if ($source.Contains('IsProcessStillRunning(')) { throw 'Exit checks must not reopen a potentially reused PID.' }
if ($SourceOnly) { Write-Output 'Browser exit production integration passed.'; return }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe') | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found.' }
& $msbuild (Join-Path $PSScriptRoot 'BrowserProcessExitTests.vcxproj') /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Browser process exit test build failed.' }
& (Join-Path $root 'build\tests\x64\Release\BrowserProcessExitTests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Browser process exit tests failed.' }
