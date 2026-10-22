#requires -Version 7.0
param(
 [string]$Configuration='Release',
 [string]$Platform='x64',
 [switch]$ReservationOnly,
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\TransientLifecycle')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){
 throw 'Transient lifecycle evidence must remain below the workspace build directory.'
}
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$outputDirectory=Join-Path $artifacts "$Platform\$Configuration"
$intermediateDirectory=Join-Path $artifacts "obj\$Platform\$Configuration"
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if(-not(Test-Path -LiteralPath $msbuild -PathType Leaf)){throw 'MSBuild was not found.'}
$sourcePaths=@('ctSpaces.cpp','TransientProfileIdentity.h','version.h','tests\TransientLifecycleTests.cpp','tests\TransientLifecycleTests.vcxproj','tests\Test-TransientLifecycle.ps1')
$sourceSnapshot=@($sourcePaths|ForEach-Object{
 $path=Join-Path $root $_
 [pscustomobject]@{Path=$path;Sha256=(Get-FileHash -LiteralPath $path).Hash}
})
$sourceSnapshot|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
function Assert-TransientSourcesUnchanged([string]$Phase){
 foreach($source in $sourceSnapshot){
  $actual=(Get-FileHash -LiteralPath $source.Path).Hash
  if($actual-ne$source.Sha256){throw "Transient lifecycle source changed $Phase`: $($source.Path)"}
 }
}
function Invoke-HiddenTransientTool([string]$Exe,[string[]]$NativeArguments,[string]$Log){
 $info=[Diagnostics.ProcessStartInfo]::new([IO.Path]::GetFullPath($Exe))
 $info.WorkingDirectory=$root
 $info.UseShellExecute=$false
 $info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true
 $info.RedirectStandardError=$true
 foreach($argument in $NativeArguments){$info.ArgumentList.Add($argument)}
 $started=[DateTime]::UtcNow
 $process=[Diagnostics.Process]::Start($info)
 try{
  try{if(-not $process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal}}
  catch [InvalidOperationException]{if(-not $process.HasExited){throw}}
  $stdout=$process.StandardOutput.ReadToEndAsync()
  $stderr=$process.StandardError.ReadToEndAsync()
  $process.WaitForExit()
  $contents=$stdout.Result+$stderr.Result
  [IO.File]::WriteAllText($Log,$contents)
  $receipt=[pscustomobject]@{
   Exe=$info.FileName;BinarySha256=(Get-FileHash -LiteralPath $info.FileName).Hash
   Arguments=$NativeArguments;ExitCode=$process.ExitCode
   Seconds=([DateTime]::UtcNow-$started).TotalSeconds
   Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash
  }
  $receipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($Log+'.json')
  Write-Output $contents
  if($process.ExitCode-ne 0){throw "Hidden transient lifecycle tool failed; inspect $Log"}
 }finally{$process.Dispose()}
}
Assert-TransientSourcesUnchanged 'before build'
Invoke-HiddenTransientTool $msbuild @(
 (Join-Path $PSScriptRoot 'TransientLifecycleTests.vcxproj'),'/m:1','/nodeReuse:false',
 "/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false','/p:TransientTestDefines=',
 "/p:OutDir=$outputDirectory\","/p:IntDir=$intermediateDirectory\",'/v:minimal'
) (Join-Path $artifacts 'build.log')
Assert-TransientSourcesUnchanged 'after build and before native run'
$sourceSnapshot|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $artifacts 'source-after-build.json')
# Explicitly empty snapshot definitions force routine Core execution to current source.
$nativeArguments=if($ReservationOnly){@('--reservation-only')}else{@()}
Invoke-HiddenTransientTool (Join-Path $outputDirectory 'TransientLifecycleTests.exe') $nativeArguments (Join-Path $artifacts 'native.log')
Assert-TransientSourcesUnchanged 'after native run'
$sourceSnapshot|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $artifacts 'source-after-run.json')
