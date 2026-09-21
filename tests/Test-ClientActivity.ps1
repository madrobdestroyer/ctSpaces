param([switch]$SourceOnly)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Get-Content -LiteralPath (Join-Path $root 'ctSpaces.cpp') -Raw
foreach ($guard in @('client_activity::Write(activityRoot',
    'client_activity::Read(deleteRoot) != expectedActivity',
    'client_activity::IsInactive(*expectedActivity', 'IDC_INACTIVE_ACK',
    'IDC_INACTIVE_ALL', 'IDC_INACTIVE_NONE', 'LB_GETSELITEMS',
    'manualSelection ? std::nullopt',
    'Cameron Kincer (Client Cleanup Idea)',
    'L"Clean Up Inactive Clients...", iIcoVacuum',
    'HandleCleanupDialogTheme(dialog, message, wParam, lParam)',
    'ApplyCleanupDialogTheme(dialog)',
    'ShowCleanupResult(summary, L"Cleanup Complete")',
    'DeleteEntireClient(client.name, true, expectedActivity, outcome)')) {
    if (-not $source.Contains($guard)) { throw "Missing cleanup guard: $guard" }
}
if ($source -notmatch '(?s)ProbeClientProfilesInUse\s*\(\s*name,\s*details,\s*&cancellation\s*\)' -or
    $source -notmatch '(?s)ValidateWholeClientDeleteTarget\s*\(\s*name,\s*root,\s*validatedRoot,\s*details,\s*&cancellation\s*\)') {
    throw 'Cleanup preview validation is not wired to cooperative scan cancellation.'
}
$launchMatch = [regex]::Match($source, '(?s)DWORD LaunchProfile\([^;{]*\)\s*\{.*?\n\}\r?\n\r?\nvoid UpdateClientsComboBox')
$launch = $launchMatch.Value
if (-not $launchMatch.Success -or
    $launch.IndexOf('client_activity::Write(activityRoot') -lt 0 -or
    $launch.IndexOf('client_activity::Write(activityRoot') -gt $launch.LastIndexOf('StartBrowserProcess(')) {
    throw 'Browser starts before activity is saved'
}
if ($SourceOnly) { 'Client activity integration source guards passed.'; return }
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $PSScriptRoot 'ClientActivityTests.vcxproj') /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Client activity test build failed' }
& (Join-Path $root 'build\tests\x64\Release\ClientActivityTests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Client activity tests failed' }
