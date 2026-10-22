param([switch]$SourceOnly,[switch]$BackgroundOnly,[string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\ClientRenameUnicodeBackground'))
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Get-Content -LiteralPath (Join-Path $root 'ctSpaces.cpp') -Raw
foreach ($integration in @('#include "ClientNameComparison.h"',
    'client_name_comparison::EqualIgnoringCase(newName, oldName)',
    'client_name_comparison::EqualIgnoringCase(oldName, newName)',
    'client_name_comparison::EqualIgnoringCase(source.filename().native()')) {
    if (-not $source.Contains($integration)) { throw "Missing Unicode rename integration: $integration" }
}
if ($SourceOnly) { Write-Output 'Unicode rename production integration passed.'; return }
if($BackgroundOnly){& (Join-Path $PSScriptRoot 'Test-LegacyClientBackground.ps1') -Suite ClientRenameUnicode -ArtifactDirectory $ArtifactDirectory;return}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe') | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found.' }
& $msbuild (Join-Path $PSScriptRoot 'ClientRenameUnicodeTests.vcxproj') /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Unicode rename test build failed.' }
Push-Location $root
try {
    & (Join-Path $root 'build\tests\ClientRenameUnicode\x64\Release\ClientRenameUnicodeTests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Unicode rename tests failed; fixture retained.' }
} finally { Pop-Location }
