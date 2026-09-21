$ErrorActionPreference = 'Stop'

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$testProject = Join-Path $PSScriptRoot 'ArchiveRoundTrip.vcxproj'
$sevenZipLibrary = Join-Path $projectRoot 'dist\x64\Release\7zip.lib'
$starterArchive = Join-Path $projectRoot 'Default.7z'
$testExe = Join-Path $projectRoot 'build\tests\x64\Release\ArchiveRoundTrip.exe'

if (-not (Test-Path -LiteralPath $sevenZipLibrary -PathType Leaf)) {
    throw "Build Release|x64 before running this test. Missing: $sevenZipLibrary"
}
if (-not (Test-Path -LiteralPath $starterArchive -PathType Leaf)) {
    throw "Starter profile is missing: $starterArchive"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'Visual Studio Installer vswhere.exe was not found.'
}

$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
    Select-Object -First 1
if (-not $msbuild) {
    throw 'MSBuild.exe was not found.'
}

$savedPath = $env:PATH
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = $savedPath
try {
    & $msbuild $testProject /m:1 /t:Rebuild /p:Configuration=Release /p:Platform=x64 /v:minimal
    if ($LASTEXITCODE -ne 0) {
        throw "Archive round-trip test build failed with exit code $LASTEXITCODE."
    }

    & $testExe $starterArchive
    if ($LASTEXITCODE -ne 0) {
        throw "Archive round-trip failed with exit code $LASTEXITCODE."
    }
}
finally {
    $env:Path = $savedPath
}
