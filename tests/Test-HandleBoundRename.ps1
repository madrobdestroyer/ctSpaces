param(
    [ValidateSet('Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('x64')]
    [string]$Platform = 'x64',
    [switch]$SourceOnly
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project = Join-Path $PSScriptRoot 'HandleBoundRenameTests.vcxproj'
$testExe = Join-Path $projectRoot `
    "build\tests\$Platform\$Configuration\HandleBoundRenameTests.exe"
$appSource = Get-Content -LiteralPath `
    (Join-Path $projectRoot 'ctSpaces.cpp') -Raw

$moveStart = $appSource.IndexOf(
    'static bool MoveKnownRegularFileWithoutReplacement(')
$moveEndMarker = 'using ShortcutOwnershipCheck ='
$moveEnd = $appSource.IndexOf($moveEndMarker, $moveStart)
if ($moveStart -lt 0 -or $moveEnd -le $moveStart) {
    throw 'MoveKnownRegularFileWithoutReplacement could not be inspected.'
}
$moveBody = $appSource.Substring($moveStart, $moveEnd - $moveStart)

foreach ($requiredGuard in @(
    'OpenLockedMatchingRegularFile(',
    'SetFileInformationByHandle(',
    'FileRenameInfo',
    'ReplaceIfExists',
    'FileNameLength',
    'renameInfoTerminatorSize = sizeof(wchar_t)',
    'HEAP_ZERO_MEMORY'
)) {
    if (-not $moveBody.Contains($requiredGuard)) {
        throw "Handle-bound rename source is missing guard: $requiredGuard"
    }
}

if ($moveBody.Contains('MoveFileExW(')) {
    throw 'The verified shortcut rename still falls back to a pathname-bound MoveFileExW call.'
}

$renameIndex = $moveBody.IndexOf('SetFileInformationByHandle(')
$handleVerifyIndex = $moveBody.IndexOf(
    'ReadSafeRegularFileSignature(fileHandle', $renameIndex)
$verifiedCloseIndex = if ($handleVerifyIndex -ge 0) {
    $moveBody.IndexOf('CloseHandle(fileHandle)', $handleVerifyIndex)
} else {
    -1
}
$pathVerifyIndex = if ($verifiedCloseIndex -ge 0) {
    $moveBody.IndexOf(
        'TryGetSafeRegularFileSignature(destination', $verifiedCloseIndex)
} else {
    -1
}
if ($renameIndex -lt 0 -or
    $handleVerifyIndex -le $renameIndex -or
    $verifiedCloseIndex -le $handleVerifyIndex -or
    $pathVerifyIndex -le $verifiedCloseIndex) {
    throw 'The successful rename must verify the bound handle, close it, then verify the published destination path.'
}

if ($moveBody -notmatch 'ReplaceIfExists\s*=\s*FALSE') {
    throw 'The handle-bound rename is not explicitly configured as no-replace.'
}
if ($moveBody -notmatch
    'renameInfoHeaderSize\s*\+\s*destinationNameBytes\s*\+\s*renameInfoTerminatorSize') {
    throw 'The rename request does not reserve a zero WCHAR beyond FileNameLength.'
}

if ($SourceOnly) {
    Write-Host 'Handle-bound rename source checks passed.'
    return
}

$msbuild = `
    'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild -PathType Leaf)) {
    throw "MSBuild was not found: $msbuild"
}

& $msbuild $project /m:1 /t:Rebuild `
    /p:Configuration=$Configuration /p:Platform=$Platform /v:minimal
if ($LASTEXITCODE -ne 0) {
    throw "HandleBoundRenameTests build failed with exit code $LASTEXITCODE."
}

& $testExe
if ($LASTEXITCODE -ne 0) {
    throw "HandleBoundRenameTests failed with exit code $LASTEXITCODE."
}
