#requires -Version 7.0
param(
 [ValidateSet('Release')][string]$Configuration='Release',
 [ValidateSet('x64')][string]$Platform='x64',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\ArchiveUiLifetime')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){
 throw 'Archive UI lifetime evidence must remain below the workspace build directory.'
}
New-Item -ItemType Directory -Force -Path $artifacts|Out-Null
$source=Join-Path $root 'ctSpaces.cpp'
$sourceHash=(Get-FileHash -LiteralPath $source).Hash
$testHash=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'ArchiveUiLifetimeTests.cpp')).Hash
$outputDirectory=Join-Path $artifacts 'bin'
$intermediateDirectory=Join-Path $artifacts 'obj'
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if(-not(Test-Path -LiteralPath $msbuild -PathType Leaf)){throw 'MSBuild was not found.'}
function Invoke-HiddenArchiveUiTool([string]$Exe,[string[]]$NativeArguments,[string]$Log){
 $info=[Diagnostics.ProcessStartInfo]::new([IO.Path]::GetFullPath($Exe))
 $info.WorkingDirectory=$root;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $NativeArguments){$info.ArgumentList.Add($argument)}
 $started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($info)
 try{
  try{if(-not $process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal}}
  catch [InvalidOperationException]{if(-not $process.HasExited){throw}}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  $process.WaitForExit();$contents=$stdout.Result+$stderr.Result
  [IO.File]::WriteAllText($Log,$contents)
  [pscustomobject]@{Exe=$info.FileName;BinarySha256=(Get-FileHash -LiteralPath $info.FileName).Hash;SourceSha256=$sourceHash;TestSha256=$testHash;Arguments=$NativeArguments;ExitCode=$process.ExitCode;Seconds=([DateTime]::UtcNow-$started).TotalSeconds;Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($Log+'.json')
  if($process.ExitCode-ne 0){throw "Hidden archive UI lifetime tool failed; inspect $Log"}
  return $contents
 }finally{$process.Dispose()}
}
$null=Invoke-HiddenArchiveUiTool $msbuild @(
 (Join-Path $PSScriptRoot 'ArchiveUiLifetimeTests.vcxproj'),'/m:1','/nodeReuse:false',
 "/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false','/p:ArchiveUiTestDefines=',
 "/p:OutDir=$outputDirectory\","/p:IntDir=$intermediateDirectory\",'/v:minimal'
) (Join-Path $artifacts 'build.log')
if((Get-FileHash -LiteralPath $source).Hash-ne $sourceHash){throw 'Production source changed during the archive UI build; rerun after source freeze.'}
$nativeLog=Invoke-HiddenArchiveUiTool (Join-Path $outputDirectory 'ArchiveUiLifetimeTests.exe') @() (Join-Path $artifacts 'native.log')
if($nativeLog-notmatch '(?m)^Archive UI lifetime: 29 checks, 12 scenarios, 0 failures; suppressed=\d+; native show/focus/foreground=0/0/0; forbidden=0\.\r?$'){
 throw 'The complete hidden archive UI lifetime scope was not confirmed.'
}
if((Get-FileHash -LiteralPath $source).Hash-ne $sourceHash -or (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'ArchiveUiLifetimeTests.cpp')).Hash-ne $testHash){throw 'Source or fixture changed during the archive UI run.'}
Write-Output $nativeLog
