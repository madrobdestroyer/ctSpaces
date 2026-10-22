#requires -Version 7.0
param(
 [string]$Configuration='Release',
 [string]$Platform='x64',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\PinnedDragLifetime')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){
 throw 'Pinned-drag evidence must remain below the workspace build directory.'
}
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$outputDirectory=Join-Path $artifacts "$Platform\$Configuration"
$intermediateDirectory=Join-Path $artifacts "obj\$Platform\$Configuration"
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if(-not(Test-Path -LiteralPath $msbuild -PathType Leaf)){throw 'MSBuild was not found.'}
$sourceFiles=@('ctSpaces.cpp','tests\PinnedDragLifetimeTests.cpp','ClientActivity.h','BrowserProcessExit.h','ClientNameComparison.h','TransientProfileIdentity.h','ConfigPersistence.cpp','ConfigPersistence.h')
$sourceBefore=@(foreach($file in $sourceFiles){[pscustomobject]@{Path=$file;Sha256=(Get-FileHash -LiteralPath (Join-Path $root $file)).Hash}})
$sourceBefore|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
function Invoke-HiddenLauncherTool([string]$Exe,[string[]]$NativeArguments,[string]$Log){
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
  if($process.ExitCode-ne 0){throw "Hidden pinned-drag tool failed; inspect $Log"}
 }finally{$process.Dispose()}
}
Invoke-HiddenLauncherTool $msbuild @(
 (Join-Path $PSScriptRoot 'PinnedDragLifetimeTests.vcxproj'),'/m:1','/nodeReuse:false',
 "/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false','/p:PinnedDragTestDefines=',
 "/p:OutDir=$outputDirectory\","/p:IntDir=$intermediateDirectory\",'/v:minimal'
) (Join-Path $artifacts 'build.log')
# No snapshot definition is supplied: routine Core execution tests current source.
Invoke-HiddenLauncherTool (Join-Path $outputDirectory 'PinnedDragLifetimeTests.exe') @() (Join-Path $artifacts 'native.log')
$sourceAfter=@(foreach($file in $sourceFiles){[pscustomobject]@{Path=$file;Sha256=(Get-FileHash -LiteralPath (Join-Path $root $file)).Hash}})
$sourceAfter|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $artifacts 'source-after.json')
if(($sourceBefore|ConvertTo-Json -Compress)-ne($sourceAfter|ConvertTo-Json -Compress)){throw 'Pinned fixture source changed during build/run; rerun after the source freeze.'}
$nativeLog=Get-Content -LiteralPath (Join-Path $artifacts 'native.log') -Raw
if($nativeLog-notmatch 'Pinned drag lifetime: \d+ checks, \d+ scenarios, 0 failures;' -or $nativeLog-notmatch 'PASS Native audit: zero visible windows, show, focus or foreground events'){
 throw 'Pinned drag fixture did not confirm its full native completion and background audit.'
}

