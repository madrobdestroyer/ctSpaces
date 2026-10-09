param()
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $PSScriptRoot 'NotesWorkspaceTests.vcxproj') /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /v:minimal
if ($LASTEXITCODE -ne 0) { throw 'Notes workspace test build failed.' }
Push-Location $root
try {
    & (Join-Path $root 'build\tests\NotesWorkspace\x64\Release\NotesWorkspaceTests.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Notes workspace tests failed.' }
} finally { Pop-Location }
