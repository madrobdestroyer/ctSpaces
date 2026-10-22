param([switch]$BackgroundOnly,[string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\ClientIsolationBackground'))
$ErrorActionPreference = 'Stop'
if($BackgroundOnly){& (Join-Path $PSScriptRoot 'Test-LegacyClientBackground.ps1') -Suite ClientIsolation -ArtifactDirectory $ArtifactDirectory;return}
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = @(& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe') | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild not found.' }
& $msbuild (Join-Path $PSScriptRoot 'ClientIsolationTests.vcxproj') /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Client isolation test build failed.' }
Push-Location $root
try {
    & (Join-Path $root 'build\tests\ClientIsolation\x64\Release\ClientIsolationTests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Client isolation tests failed; fixture retained.' }
} finally { Pop-Location }

