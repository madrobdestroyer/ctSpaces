$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $PSScriptRoot 'BrowserTitleTests.vcxproj') /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /v:minimal
if($LASTEXITCODE-ne 0){throw 'Browser title regression build failed.'}
& (Join-Path $root 'build\tests\x64\Release\BrowserTitleTests.exe')
if($LASTEXITCODE-ne 0){throw 'Browser title regression checks failed.'}
