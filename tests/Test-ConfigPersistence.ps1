param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [switch]$SourceOnly
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project = Join-Path $PSScriptRoot 'ConfigPersistenceTests.vcxproj'
$executable = Join-Path $root `
    "build\tests\$Platform\$Configuration\ConfigPersistenceTests.exe"
$moduleSource = Get-Content -LiteralPath `
    (Join-Path $root 'ConfigPersistence.cpp') -Raw
$appSource = Get-Content -LiteralPath (Join-Path $root 'ctSpaces.cpp') -Raw
$mainProjectPath = Join-Path $root 'ctSpaces.vcxproj'
[xml]$mainProject = Get-Content -LiteralPath $mainProjectPath -Raw

foreach ($requiredGuard in @(
    'InspectConfigFile',
    'FILE_FLAG_OPEN_REPARSE_POINT',
    'PrepareUnicodeIniStage',
    'kMaximumIniMigrationBytes',
    'CP_UTF8',
    'MB_ERR_INVALID_CHARS',
    'unsupported UTF-32 text',
    "converted.find(L'\0')",
    'WritePrivateProfileStringW(nullptr, nullptr, nullptr',
    'FlushFileBuffers',
    'MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH',
    'DirectChildDirectoryState::Indeterminate'
)) {
    if (-not $moduleSource.Contains($requiredGuard)) {
        throw "Config persistence source is missing guard: $requiredGuard"
    }
}

foreach ($requiredIntegration in @(
    '#include "ConfigPersistence.h"',
    'config_persistence::ApplyIniMutationsAtomically(',
    'static bool SavePinnedClients()',
    'static bool SaveArchivedClients()',
    'static bool SaveRestoreTabsPreferences()',
    'ProbeClientProfileForPruning',
    'HasReadableConfigFile()'
)) {
    if (-not $appSource.Contains($requiredIntegration)) {
        throw "ctSpaces source is missing config integration: $requiredIntegration"
    }
}

if ($appSource.Contains('WritePrivateProfileStringW(')) {
    throw 'ctSpaces still writes directly to the live INI instead of the atomic module.'
}

foreach ($pruneFunction in @(
    'PrunePinnedClients',
    'PruneArchivedClients',
    'PruneRestoreTabsPreferences'
)) {
    $functionPattern = '(?s)static void ' + $pruneFunction + `
        '\(\)\s*\{(?<body>.*?)\r?\n\}\r?\n\r?\nstatic '
    $functionMatch = [regex]::Match($appSource, $functionPattern)
    if (-not $functionMatch.Success) {
        throw "$pruneFunction could not be inspected."
    }
    $body = $functionMatch.Groups['body'].Value
    if ($body -notmatch `
        'DirectChildDirectoryState::Indeterminate\)\s*\{\s*return;') {
        throw "$pruneFunction does not abort on indeterminate filesystem state."
    }
}

$namespace = New-Object Xml.XmlNamespaceManager($mainProject.NameTable)
$namespace.AddNamespace('msb', $mainProject.DocumentElement.NamespaceURI)
foreach ($requiredItem in @(
    [pscustomobject]@{ Type = 'ClInclude'; Include = 'ConfigPersistence.h' },
    [pscustomobject]@{ Type = 'ClCompile'; Include = 'ConfigPersistence.cpp' }
)) {
    $nodes = $mainProject.SelectNodes(
        "//msb:$($requiredItem.Type)[@Include='$($requiredItem.Include)']",
        $namespace)
    if ($nodes.Count -ne 1) {
        throw "ctSpaces.vcxproj must contain exactly one $($requiredItem.Type) entry for $($requiredItem.Include); found $($nodes.Count)."
    }
}

if ($SourceOnly) {
    Write-Host 'Config persistence source and project integration checks passed.'
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
    throw 'MSBuild was not found. Install Visual Studio C++ Build Tools, or use -SourceOnly.'
}

& $msbuildPath $project /m /p:Configuration=$Configuration /p:Platform=$Platform
if ($LASTEXITCODE -ne 0) {
    throw "ConfigPersistenceTests build failed with exit code $LASTEXITCODE."
}

& $executable
if ($LASTEXITCODE -ne 0) {
    throw "ConfigPersistenceTests failed with exit code $LASTEXITCODE."
}
