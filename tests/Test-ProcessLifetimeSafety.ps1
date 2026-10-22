$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $PSScriptRoot 'ProcessLifetimeSafetyTests.vcxproj') /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /v:minimal
if($LASTEXITCODE-ne 0){throw 'Process lifetime safety regression build failed.'}
Push-Location $root
try {
 & (Join-Path $root 'build\tests\ProcessLifetimeSafety\x64\Release\ProcessLifetimeSafetyTests.exe')
 if($LASTEXITCODE-ne 0){throw 'Process lifetime safety regression checks failed.'}
} finally {Pop-Location}
