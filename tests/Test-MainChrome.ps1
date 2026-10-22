param([string]$CaptureDirectory='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild (Join-Path $PSScriptRoot 'MainChromeTests.vcxproj') /m:1 /nodeReuse:false /p:Configuration=Release /p:Platform=x64 /v:minimal
if($LASTEXITCODE-ne 0){throw 'Main chrome regression build failed.'}
Push-Location $root
try{
    $arguments=@();if($CaptureDirectory){$arguments+=([IO.Path]::GetFullPath($CaptureDirectory))}
    & (Join-Path $root 'build\tests\MainChrome\x64\Release\MainChromeTests.exe') @arguments
    if($LASTEXITCODE-ne 0){throw 'Main chrome regression checks failed.'}
}finally{Pop-Location}
