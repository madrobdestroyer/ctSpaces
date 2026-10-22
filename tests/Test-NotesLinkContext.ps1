#requires -Version 7.0
param(
 [string]$Configuration='Release',
 [string]$Platform='x64',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\NotesLinkContext')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){
 throw 'Link-context evidence must remain below the workspace build directory.'
}
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$outputDirectory=Join-Path $artifacts "$Platform\$Configuration"
$intermediateDirectory=Join-Path $artifacts "obj\$Platform\$Configuration"
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if(-not(Test-Path -LiteralPath $msbuild -PathType Leaf)){throw 'MSBuild was not found.'}
function Invoke-HiddenLinkContextTool([string]$Exe,[string[]]$NativeArguments,[string]$Log){
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
  $timedOut=-not $process.WaitForExit(300000)
  if($timedOut){$process.Kill($true);$process.WaitForExit()}
  $contents=$stdout.Result+$stderr.Result
  [IO.File]::WriteAllText($Log,$contents)
  $receipt=[pscustomobject]@{
   Exe=$info.FileName;BinarySha256=(Get-FileHash -LiteralPath $info.FileName).Hash
   Arguments=$NativeArguments;ExitCode=$process.ExitCode;TimedOut=$timedOut
   Seconds=([DateTime]::UtcNow-$started).TotalSeconds
   Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash
  }
  $receipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($Log+'.json')
  Write-Output $contents
  if($timedOut-or$process.ExitCode-ne 0){throw "Hidden Notes link-context tool failed; inspect $Log"}
 }finally{$process.Dispose()}
}
$sourcePaths=@(Get-ChildItem -LiteralPath $root -File|Where-Object{$_.Extension-in'.cpp','.h','.inl','.rc'}|ForEach-Object{$_.Name})
$sourcePaths+=@('tests\NotesWorkspaceTests.cpp','tests\NotesBackgroundGuards.inl','tests\NotesLinkContextTests.cpp','tests\NotesLinkContextTests.inl','tests\NotesLinkContextTests.vcxproj','tests\Test-NotesLinkContext.ps1','tests\RetainedFixture.h')
$snapshot=@($sourcePaths|ForEach-Object {$path=Join-Path $root $_;[pscustomobject]@{Path=$path;Hash=(Get-FileHash -LiteralPath $path).Hash}})
$snapshot|ConvertTo-Json|Set-Content (Join-Path $artifacts 'source-hashes.json')
function Assert-LinkContextSourceUnchanged {
 foreach($record in $snapshot){if((Get-FileHash -LiteralPath $record.Path).Hash-ne$record.Hash){throw 'Link-context source changed during verification.'}}
}
Invoke-HiddenLinkContextTool $msbuild @(
 (Join-Path $PSScriptRoot 'NotesLinkContextTests.vcxproj'),'/m:1','/nodeReuse:false',
 "/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false','/p:NotesLinkContextTestDefinitions=',
 "/p:OutDir=$outputDirectory\","/p:IntDir=$intermediateDirectory\",'/v:minimal'
) (Join-Path $artifacts 'build.log')
Assert-LinkContextSourceUnchanged
# Explicitly empty snapshot definitions force the current production include.
Invoke-HiddenLinkContextTool (Join-Path $outputDirectory 'NotesLinkContextTests.exe') @() (Join-Path $artifacts 'native.log')
Assert-LinkContextSourceUnchanged
$native=Get-Content (Join-Path $artifacts 'native.log') -Raw
if($native-notmatch '"forbiddenClipboard":0,"forbiddenKeyboard":0,"forbiddenRedraw":0,"nativeVisibleEvents":0,"nativeForegroundEvents":0,"nativeFocusEvents":0') {throw 'Link-context background native guards did not remain clean.'}
$nativeLog=Get-Content -LiteralPath (Join-Path $artifacts 'native.log') -Raw
if($nativeLog-notmatch 'Notes link context: (\d+) checks, 78 cases, 0 failures; actual input/foreground/clipboard/browser=0\.'-or[int]$Matches[1]-lt150){throw 'Complete link-context native coverage was not confirmed.'}
if($nativeLog-notmatch 'Link glyph coverage: 8 variants completed; native measured glyphs, right halves, final glyphs, wrapped boundaries, adjacent targets, Unicode clusters and immutable state passed\.') {throw 'Complete measured full-width native glyph coverage was not confirmed.'}

