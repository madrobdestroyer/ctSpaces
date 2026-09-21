param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [switch]$SourceOnly
)

$ErrorActionPreference = 'Stop'
$project = Join-Path $PSScriptRoot 'BrowserPreferencesJsonTests.vcxproj'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$executable = Join-Path $root "build\tests\$Platform\$Configuration\BrowserPreferencesJsonTests.exe"
$source = Get-Content -LiteralPath (Join-Path $root 'BrowserPreferencesJson.cpp') -Raw
$appSource = Get-Content -LiteralPath (Join-Path $root 'ctSpaces.cpp') -Raw
$mainProjectPath = Join-Path $root 'ctSpaces.vcxproj'
[xml]$mainProject = Get-Content -LiteralPath $mainProjectPath -Raw

foreach ($requiredGuard in @(
    'kMaximumPreferencesJsonBytes',
    'FILE_FLAG_OPEN_REPARSE_POINT',
    'FILE_ATTRIBUTE_REPARSE_POINT',
    'ReplaceFileW'
)) {
    if (-not $source.Contains($requiredGuard)) {
        throw "Browser preferences source is missing safety guard: $requiredGuard"
    }
}

foreach ($requiredIntegration in @(
    '#include "BrowserPreferencesJson.h"',
    'browser_preferences::UpdateStartupPreferencesFile('
)) {
    if (-not $appSource.Contains($requiredIntegration)) {
        throw "ctSpaces source is missing browser-preferences integration: $requiredIntegration"
    }
}

$namespace = New-Object Xml.XmlNamespaceManager($mainProject.NameTable)
$namespace.AddNamespace('msb', $mainProject.DocumentElement.NamespaceURI)
foreach ($requiredItem in @(
    [pscustomobject]@{
        Type = 'ClInclude'
        Include = 'BrowserPreferencesJson.h'
    },
    [pscustomobject]@{
        Type = 'ClCompile'
        Include = 'BrowserPreferencesJson.cpp'
    }
)) {
    $nodes = $mainProject.SelectNodes(
        "//msb:$($requiredItem.Type)[@Include='$($requiredItem.Include)']",
        $namespace)
    if ($nodes.Count -ne 1) {
        throw "ctSpaces.vcxproj must contain exactly one $($requiredItem.Type) entry for $($requiredItem.Include); found $($nodes.Count)."
    }
}

if ($SourceOnly) {
    Write-Host 'Browser preferences source and project integration checks passed.'
    return
}

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
            & $vswherePath -latest `
                -products Microsoft.VisualStudio.Product.BuildTools `
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
    throw 'MSBuild was not found on PATH or through Visual Studio Build Tools via vswhere. Install the Visual Studio C++ Build Tools, or run this test with -SourceOnly.'
}

& $msbuildPath $project /m /p:Configuration=$Configuration /p:Platform=$Platform
if ($LASTEXITCODE -ne 0) {
    throw "BrowserPreferencesJsonTests build failed with exit code $LASTEXITCODE."
}

& $executable
if ($LASTEXITCODE -ne 0) {
    throw "BrowserPreferencesJsonTests failed with exit code $LASTEXITCODE."
}
