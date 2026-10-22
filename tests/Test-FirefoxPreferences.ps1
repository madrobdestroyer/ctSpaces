param([switch]$SourceOnly)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Get-Content -LiteralPath (Join-Path $root 'ctSpaces.cpp') -Raw
if (-not $source.Contains('#include "FirefoxPreferences.h"') -or
    -not $source.Contains('firefox_preferences::UpdateStartupPreferenceFile(profilePath, restoreTabs, errorDetails)')) {
    throw 'The application must use the production Firefox preference helper tested here.'
}
if ($SourceOnly) { Write-Output 'Firefox production helper integration passed.'; return }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe') | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found.' }
& $msbuild (Join-Path $PSScriptRoot 'FirefoxPreferencesTests.vcxproj') /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Firefox preference test build failed.' }
& (Join-Path $root 'build\tests\x64\Release\FirefoxPreferencesTests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Firefox preference tests failed; fixture path is recorded above.' }
