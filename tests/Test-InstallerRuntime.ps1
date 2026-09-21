param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project = Join-Path $PSScriptRoot 'InstallerRuntimeTests.vcxproj'
$sevenZipLibrary = Join-Path $root 'dist\x64\Release\7zip.lib'
$executable = Join-Path $root `
    "build\tests\InstallerRuntime\$Platform\$Configuration\InstallerRuntimeTests.exe"

function Test-RegularToolFile {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return $false
    }
    $item = Get-Item -LiteralPath $Path -Force
    return (-not $item.PSIsContainer -and
            ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0)
}

$msbuildPath = $null
$msbuildCommand = Get-Command msbuild.exe -CommandType Application `
    -ErrorAction SilentlyContinue | Select-Object -First 1
if ($msbuildCommand -and (Test-RegularToolFile $msbuildCommand.Source)) {
    $msbuildPath = $msbuildCommand.Source
}

if (-not $msbuildPath) {
    $vswherePath = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-RegularToolFile $vswherePath) {
        $installationPath = @(
            & $vswherePath -latest -products * `
                -requires Microsoft.Component.MSBuild `
                -property installationPath
        ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            Select-Object -First 1
        if ($LASTEXITCODE -eq 0 -and $installationPath) {
            $candidate = Join-Path $installationPath `
                'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-RegularToolFile $candidate) {
                $msbuildPath = [IO.Path]::GetFullPath($candidate)
            }
        }
    }
}

if (-not $msbuildPath) {
    throw 'MSBuild was not found. Install Visual Studio C++ Build Tools.'
}
if (-not (Test-RegularToolFile $sevenZipLibrary)) {
    throw "Build Release|x64 before running this test. Missing: $sevenZipLibrary"
}

& $msbuildPath $project /m:1 /t:Rebuild `
    /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal
if ($LASTEXITCODE -ne 0) {
    throw "Installer runtime test build failed with exit code $LASTEXITCODE."
}

& $executable
if ($LASTEXITCODE -ne 0) {
    throw "Installer runtime tests failed with exit code $LASTEXITCODE."
}
