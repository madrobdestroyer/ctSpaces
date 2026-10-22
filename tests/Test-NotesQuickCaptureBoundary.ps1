#requires -Version 7.0
param(
 [string]$Configuration='Release',
 [string]$Platform='x64',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\NotesQuickCaptureBoundary')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){
 throw 'Quick-capture boundary evidence must remain below the workspace build directory.'
}
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$outputDirectory=Join-Path $artifacts "$Platform\$Configuration"
$intermediateDirectory=Join-Path $artifacts "obj\$Platform\$Configuration"
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if(-not(Test-Path -LiteralPath $msbuild -PathType Leaf)){throw 'MSBuild was not found.'}
function Invoke-HiddenQuickCaptureTool([string]$Exe,[string[]]$NativeArguments,[string]$Log){
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
  if($process.ExitCode-ne 0){throw "Hidden Notes quick-capture boundary tool failed; inspect $Log"}
 }finally{$process.Dispose()}
}
$sourcePaths=@('ctSpaces.cpp','NotesProductivityUi.inl','NotesWorkspaceUi.inl','NotesTemplatesUi.inl','NotesInspectorUi.inl','tests\NotesWorkspaceTests.cpp','tests\NotesBackgroundGuards.inl','tests\NotesQuickCaptureBoundaryTests.cpp','tests\NotesQuickCaptureBoundaryTests.vcxproj','tests\Test-NotesQuickCaptureBoundary.ps1')
$snapshot=@($sourcePaths|ForEach-Object {$path=Join-Path $root $_;[pscustomobject]@{Path=$path;Hash=(Get-FileHash -LiteralPath $path).Hash}})
$snapshot|ConvertTo-Json|Set-Content (Join-Path $artifacts 'source-hashes.json')
function Assert-QuickCaptureSourceUnchanged {
 foreach($record in $snapshot){if((Get-FileHash -LiteralPath $record.Path).Hash-ne$record.Hash){throw 'Quick-capture boundary source changed during verification.'}}
}
Invoke-HiddenQuickCaptureTool $msbuild @(
 (Join-Path $PSScriptRoot 'NotesQuickCaptureBoundaryTests.vcxproj'),'/m:1','/nodeReuse:false',
 "/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false','/p:SnapshotDefines=','/p:NotesRedrawTestDefinitions=',
 "/p:OutDir=$outputDirectory\","/p:IntDir=$intermediateDirectory\",'/v:minimal'
) (Join-Path $artifacts 'build.log')
Assert-QuickCaptureSourceUnchanged
# Explicitly empty snapshot definitions force the current production include.
Invoke-HiddenQuickCaptureTool (Join-Path $outputDirectory 'NotesQuickCaptureBoundaryTests.exe') @() (Join-Path $artifacts 'native.log')
Assert-QuickCaptureSourceUnchanged
$native=Get-Content (Join-Path $artifacts 'native.log') -Raw
if($native-notmatch 'Notes quick capture boundaries: 24 checks, 4 scenarios, 0 failures; actual input/foreground/clipboard=0') {throw 'Complete background quick-capture boundary evidence was not recorded.'}
if($native-notmatch '"forbiddenClipboard":0,"forbiddenKeyboard":0,"forbiddenRedraw":0,"nativeVisibleEvents":0,"nativeForegroundEvents":0,"nativeFocusEvents":0') {throw 'Quick-capture background native guards did not remain clean.'}
