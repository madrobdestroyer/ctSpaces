param(
    [ValidateSet('Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('x64')]
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project = Join-Path $PSScriptRoot 'ClientShortcutNameTests.vcxproj'
$testExe = Join-Path $projectRoot `
    "build\tests\$Platform\$Configuration\ClientShortcutNameTests.exe"
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'

if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) {
    throw "MSBuild was not found: $msbuild"
}

& $msbuild $project /m:1 /t:Rebuild `
    /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal
if ($LASTEXITCODE -ne 0) {
    throw "ClientShortcutNameTests build failed with exit code $LASTEXITCODE."
}

& $testExe
if ($LASTEXITCODE -ne 0) {
    throw "ClientShortcutNameTests failed with exit code $LASTEXITCODE."
}
