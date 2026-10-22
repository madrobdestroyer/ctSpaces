param([switch]$SourceOnly,[switch]$BackgroundOnly,[string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\ClientIdentityUnicodeBackground'))
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Get-Content -LiteralPath (Join-Path $root 'ctSpaces.cpp') -Raw
foreach ($integration in @('#include "ClientNameComparison.h"',
    'client_name_comparison::CompareIgnoringCase(left, right)',
    'client_name_comparison::EqualIgnoringCase(pinnedName, clientName)',
    'ContainsClientName(g_clientsWithoutTabRestore, preferenceKey)',
    'client_name_comparison::EqualIgnoringCase(storedName, clientName)',
    'client_name_comparison::EqualIgnoringCase(storedName, restoredClientName)',
    'client_name_comparison::EqualIgnoringCase(existing, sanitized)')) {
    if (-not $source.Contains($integration)) {
        throw "Missing Unicode client identity integration: $integration"
    }
}
if ($SourceOnly) {
    Write-Output 'Unicode client identity production integration passed.'
    return
}
if($BackgroundOnly){& (Join-Path $PSScriptRoot 'Test-LegacyClientBackground.ps1') -Suite ClientIdentityUnicode -ArtifactDirectory $ArtifactDirectory;return}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild `
    -find 'MSBuild\**\Bin\MSBuild.exe') | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found.' }
& $msbuild (Join-Path $PSScriptRoot 'ClientIdentityUnicodeTests.vcxproj') `
    /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Unicode client identity test build failed.' }
Push-Location $root
try {
    & (Join-Path $root 'build\tests\ClientIdentityUnicode\x64\Release\ClientIdentityUnicodeTests.exe')
    if ($LASTEXITCODE -ne 0) {
        throw 'Unicode client identity tests failed; fixture retained.'
    }
}
finally { Pop-Location }
