$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $PSScriptRoot 'ClientCacheMaintenanceTests.vcxproj') /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /v:minimal
if($LASTEXITCODE-ne0){throw 'Client cache maintenance test build failed'}
$native=Join-Path $root 'build\tests\ClientCacheMaintenance\x64\Release\ClientCacheMaintenanceTests.exe'
foreach($mode in 0..7){
    & $native ([string]$mode)
    if($LASTEXITCODE-ne0){throw ('Client cache maintenance mode '+$mode+' failed; disposable fixture retained')}
}
