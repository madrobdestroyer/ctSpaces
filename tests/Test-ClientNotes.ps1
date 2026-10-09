param([switch]$SourceOnly)

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$source = Get-Content -LiteralPath (Join-Path $root 'ctSpaces.cpp') -Raw
foreach ($guard in @('ShowClientNotes(GetSelectedClientNameSanitized(false))',
    'ShowClientNotes(clientName)', 'IDD_CLIENT_NOTES',
    'client_notes::ReadNotebook(clientRoot)', 'client_notes::WriteNotebook(state.clientRoot')) {
    if (-not $source.Contains($guard)) {
        throw "Missing Client Notes integration: $guard"
    }
}
if ($SourceOnly) {
    'Client Notes integration source guards passed.'
    return
}

$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $PSScriptRoot 'ClientNotesTests.vcxproj') `
    /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Client Notes test build failed.' }
& (Join-Path $root 'build\tests\x64\Release\ClientNotesTests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Client Notes tests failed.' }
