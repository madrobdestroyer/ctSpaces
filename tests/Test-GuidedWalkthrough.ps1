param(
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [switch]$SourceOnly
)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project = Join-Path $PSScriptRoot 'GuidedWalkthroughTests.vcxproj'
$executable = Join-Path $root `
    "build\tests\$Platform\$Configuration\GuidedWalkthroughTests.exe"

foreach ($requiredPath in @(
    (Join-Path $root 'GuidedWalkthrough.h'),
    (Join-Path $root 'GuidedWalkthrough.cpp'),
    $project,
    (Join-Path $PSScriptRoot 'GuidedWalkthroughTests.cpp')
)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Guided walkthrough test input is missing: $requiredPath"
    }
}

function Get-ProductionSources {
    Get-ChildItem -LiteralPath $root -File |
        Where-Object { $_.Extension -in @('.cpp', '.h', '.rc') }
}

function Assert-SourceText {
    param([string]$Text, [string]$Failure)
    if (-not $allSource.Contains($Text)) { throw $Failure }
}

$guideFiles = @(
    Get-ChildItem -LiteralPath $root -File |
        Where-Object { $_.Name -match '(?i)(guide|walkthrough)' -and
            $_.Extension -in @('.cpp', '.h') }
)
if ($guideFiles.Count -lt 2) {
    throw 'Guided walkthrough implementation must have separate header and source files.'
}
$guideSource = (($guideFiles | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n")
$appSource = Get-Content -LiteralPath (Join-Path $root 'ctSpaces.cpp') -Raw
$allSource = $guideSource + "`n" + $appSource
$appProject = Get-Content -LiteralPath (Join-Path $root 'ctSpaces.vcxproj') -Raw

# These checks are deliberately source-level safety rails: they can run on a
# build machine before a GUI session is available and prevent a later refactor
# from silently dropping the durable state or read-only guarantees.
Assert-SourceText 'kTopics' 'Guide source has no explicit topic catalog.'
Assert-SourceText 'welcome_pending' 'Guide startup state is not durable.'
Assert-SourceText 'IsFreshDataFolder' 'Fresh-versus-existing detection is missing.'
Assert-SourceText 'readRevisions' 'Guide state is not revision-aware.'
Assert-SourceText 'ApplyIniMutationsAtomically' 'Guide state is not saved atomically.'
Assert-SourceText 'VK_F1' 'F1 replay is not wired to the guide.'
Assert-SourceText 'WM_COPYDATA' 'Shortcut handoff is not represented in the application.'
Assert-SourceText 'HasUnreadAnnouncement' "What's new does not filter announcements."

foreach ($requiredIntegration in @(
    '#include "GuidedWalkthrough.h"',
    'guided_walkthrough::LoadState',
    'guided_walkthrough::IsFreshDataFolder',
    'guided_walkthrough::HasUnreadAnnouncement'
)) {
    if ($allSource -notmatch [regex]::Escape($requiredIntegration)) {
        throw "Guided walkthrough integration is missing: $requiredIntegration"
    }
}
if ($appProject -notmatch 'GuidedWalkthrough\.cpp') {
    throw 'ctSpaces.vcxproj does not compile GuidedWalkthrough.cpp.'
}

if ($SourceOnly) {
    Write-Host 'Guided walkthrough source contract checks passed.'
    return
}

function Test-RegularToolFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
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
        $installationPath = @(& $vswherePath -latest -products * `
            -requires Microsoft.Component.MSBuild -property installationPath) |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
            Select-Object -First 1
        if ($LASTEXITCODE -eq 0 -and $installationPath) {
            $candidate = Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-RegularToolFile $candidate) { $msbuildPath = $candidate }
        }
    }
}
if (-not $msbuildPath) {
    throw 'MSBuild was not found. Install Visual Studio C++ Build Tools, or use -SourceOnly.'
}

& $msbuildPath $project /m:1 /t:Rebuild `
    /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal
if ($LASTEXITCODE -ne 0) { throw "Guided walkthrough test build failed with exit code $LASTEXITCODE." }
if (-not (Test-RegularToolFile $executable)) { throw "Test executable not found: $executable" }
& $executable
if ($LASTEXITCODE -ne 0) { throw "Guided walkthrough unit tests failed with exit code $LASTEXITCODE." }
