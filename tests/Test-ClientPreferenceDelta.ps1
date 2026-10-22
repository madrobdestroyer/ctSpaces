#requires -Version 7.0
param(
 [string]$Configuration='Release',
 [string]$Platform='x64',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\ClientPreferenceDelta')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not$artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Client preference evidence must remain below the workspace build directory.'}
New-Item -ItemType Directory -Force -Path $artifacts|Out-Null
$output=Join-Path $artifacts 'bin'
$intermediate=Join-Path $artifacts 'obj'
$sources=@(Get-ChildItem -LiteralPath $root -File|Where-Object{$_.Extension-in'.cpp','.h','.inl','.rc'}|ForEach-Object{$_.FullName})
$sources+=@('ClientPreferenceDeltaTests.cpp','ClientPreferenceDeltaTests.vcxproj','Test-ClientPreferenceDelta.ps1','ClientPreferenceSnapshotTests.inl','RenamePreferenceDeltaTests.inl','ClientPreferenceQuotedTests.inl','BrowserLaunchFailureTests.cpp','RestoreTabsDeltaTests.inl','RetainedFixture.h')|ForEach-Object{Join-Path $PSScriptRoot $_}
$snapshot=@($sources|Sort-Object -Unique|ForEach-Object{[pscustomobject]@{Path=$_;Hash=(Get-FileHash -LiteralPath $_).Hash}})
$snapshot|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'source-hashes.json')
function Assert-SourceUnchanged {foreach($record in $snapshot){if((Get-FileHash -LiteralPath $record.Path).Hash-ne$record.Hash){throw ('Client preference source changed during verification: '+$record.Path)}}}
function Invoke-HiddenCollectionTool([string]$Exe,[string[]]$NativeArguments,[string]$Log,[int]$Timeout){
 $info=[Diagnostics.ProcessStartInfo]::new($Exe)
 $info.WorkingDirectory=$root;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $NativeArguments){$info.ArgumentList.Add($argument)}
 $process=[Diagnostics.Process]::Start($info)
 try{
  try{if(-not$process.HasExited){$process.PriorityClass='BelowNormal'}}catch [InvalidOperationException]{}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  $timedOut=-not$process.WaitForExit($Timeout)
  if($timedOut){$process.Kill($true);$process.WaitForExit()}
  $contents=$stdout.Result+$stderr.Result;[IO.File]::WriteAllText($Log,$contents)
  [pscustomobject]@{Exe=$Exe;BinarySha256=(Get-FileHash -LiteralPath $Exe).Hash;ExitCode=$process.ExitCode;TimedOut=$timedOut;Arguments=$NativeArguments;CreateNoWindow=$true;Priority='BelowNormal';Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath ($Log+'.json')
  if($timedOut){throw "Hidden client preference tool timed out; inspect $Log"}
  if($process.ExitCode-ne0){throw "Hidden client preference tool failed; inspect $Log"}
  return $contents
 }finally{$process.Dispose()}
}
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$null=Invoke-HiddenCollectionTool $msbuild @((Join-Path $PSScriptRoot 'ClientPreferenceDeltaTests.vcxproj'),'/m:1','/nodeReuse:false',"/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false',"/p:OutDir=$output\","/p:IntDir=$intermediate\",'/v:minimal') (Join-Path $artifacts 'build.log') 300000
Assert-SourceUnchanged
$native=Invoke-HiddenCollectionTool (Join-Path $output 'ClientPreferenceDeltaTests.exe') @((Join-Path $artifacts 'fixtures')) (Join-Path $artifacts 'native.log') 120000
Assert-SourceUnchanged
$complete=[regex]::Match($native,'Client preference delta: (\d+) checks, 135 private scenarios, 0 failures; actual HWND/input/foreground/browser=0\.')
if(-not$complete.Success-or[int]$complete.Groups[1].Value-lt1745) {throw 'The complete native client preference scope was not confirmed.'}
$membership=[regex]::Matches($native,'(?m)^Quoted identity membership: canonical=([01]); operation=([012]); exact native identity roundtrip passed\r?$')
if($membership.Count-ne6-or@($membership|ForEach-Object{$_.Groups[1].Value+':'+$_.Groups[2].Value}|Sort-Object -Unique).Count-ne6) {throw 'The six original quoted-identity reproductions did not all pass.'}
if(-not$native.Contains('Quoted client identity: 14 private scenarios; 8 name variants; canonical/fallback, rename, recent recall, initial serializers and locked publication passed.')-or-not$native.Contains('Rename preference delta: 17 private rename, collision, publication and partial-write scenarios passed.')) {throw 'The new quoted-identity and existing rename groups did not both complete.'}
Write-Output $native
