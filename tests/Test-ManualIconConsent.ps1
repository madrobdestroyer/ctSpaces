#requires -Version 7.0
param(
 [string]$Configuration='Release',
 [string]$Platform='x64',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\ManualIconConsent')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){
 throw 'Client-icon evidence must remain below the workspace build directory.'
}
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$outputDirectory=Join-Path $artifacts "$Platform\$Configuration"
$intermediateDirectory=Join-Path $artifacts "obj\$Platform\$Configuration"
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if(-not(Test-Path -LiteralPath $msbuild -PathType Leaf)){throw 'MSBuild was not found.'}
function Invoke-HiddenManualIconTool([string]$Exe,[string[]]$NativeArguments,[string]$Log){
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
  if($process.ExitCode-ne 0){throw "Hidden manual-icon tool failed; inspect $Log"}
 }finally{$process.Dispose()}
}
$sourcePaths=@('ctSpaces.cpp','ClientDeletionSafety.h','tests\ManualIconConsentTests.cpp','tests\ManualIconConsentTests.vcxproj','tests\Test-ManualIconConsent.ps1')
$snapshot=@($sourcePaths|ForEach-Object {$path=Join-Path $root $_;[pscustomobject]@{Path=$path;Hash=(Get-FileHash -LiteralPath $path).Hash}})
$snapshot|ConvertTo-Json|Set-Content (Join-Path $artifacts 'source-hashes.json')
function Assert-SourceUnchanged {
 foreach($record in $snapshot){if((Get-FileHash -LiteralPath $record.Path).Hash-ne$record.Hash){throw 'Manual icon consent source changed during verification.'}}
}
Invoke-HiddenManualIconTool $msbuild @(
 (Join-Path $PSScriptRoot 'ManualIconConsentTests.vcxproj'),'/m:1','/nodeReuse:false',
 "/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false','/p:ManualIconDefines=',
 "/p:OutDir=$outputDirectory\","/p:IntDir=$intermediateDirectory\",'/v:minimal'
) (Join-Path $artifacts 'build.log')
Assert-SourceUnchanged
# Explicitly empty snapshot definitions force current-source routine execution.
Invoke-HiddenManualIconTool (Join-Path $outputDirectory 'ManualIconConsentTests.exe') @() (Join-Path $artifacts 'native.log')
Assert-SourceUnchanged
$native=Get-Content (Join-Path $artifacts 'native.log') -Raw
if($native-notmatch 'Manual icon consent: 14 checks, 4 scenarios, 0 failures; HWNDs=0; actual picker/foreground/launches=0') {throw 'Complete background manual icon consent evidence was not recorded.'}
