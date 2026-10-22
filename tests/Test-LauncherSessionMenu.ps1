$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $PSScriptRoot 'LauncherSessionMenuTests.vcxproj') /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /v:minimal
if($LASTEXITCODE-ne 0){throw 'Launcher session menu regression build failed.'}
Push-Location $root
try {
 & (Join-Path $root 'build\tests\LauncherSessionMenu\x64\Release\LauncherSessionMenuTests.exe')
 if($LASTEXITCODE-ne 0){throw 'Launcher session menu regression checks failed.'}
} finally {Pop-Location}
