#requires -Version 7.0
param([string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\BrowserTabLayoutPreferences'))
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not$artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Evidence must remain below workspace build.'}
New-Item -ItemType Directory -Force -Path $artifacts|Out-Null
foreach($name in 'result.json','build.log','native.log','source-hashes.json'){
 if(Test-Path -LiteralPath (Join-Path $artifacts $name)){throw 'Evidence already exists; choose a fresh artifact directory.'}
}
$inputs=@('BrowserPreferencesJson.cpp','BrowserPreferencesJson.h','ConfigPersistence.cpp','ConfigPersistence.h','SiblingStageName.h')|ForEach-Object{Join-Path $root $_}
$inputs+=@('BrowserTabLayoutPreferencesTests.cpp','BrowserTabLayoutPreferencesTests.vcxproj','Test-BrowserTabLayoutPreferences.ps1','RetainedFixture.h')|ForEach-Object{Join-Path $PSScriptRoot $_}
$snapshot=@($inputs|Sort-Object -Unique|ForEach-Object{[ordered]@{Path=$_;Sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}})
$snapshot|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'source-hashes.json')
function Fence{foreach($row in $snapshot){if((Get-FileHash -LiteralPath $row.Path -Algorithm SHA256).Hash-ne$row.Sha256){throw ('Frozen tab-layout input changed: '+$row.Path)}}}
function Hidden([string]$Exe,[string[]]$Arguments,[string]$Log,[int]$Timeout){
 $info=[Diagnostics.ProcessStartInfo]::new($Exe);$info.WorkingDirectory=$root;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 $started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($info)
 try{
  try{if(-not$process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal}}catch [InvalidOperationException]{if(-not$process.HasExited){throw}}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  if(-not$process.WaitForExit($Timeout)){$process.Kill($true);throw ('Owned tab-layout tool timed out: '+$Exe)}
  $text=$stdout.Result+$stderr.Result;[IO.File]::WriteAllText($Log,$text)
  $receipt=[ordered]@{Exe=$Exe;Sha256=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash;Arguments=$Arguments;ExitCode=$process.ExitCode;CreateNoWindow=$true;Priority='BelowNormal';Seconds=([DateTime]::UtcNow-$started).TotalSeconds;Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log -Algorithm SHA256).Hash}
  $receipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($Log+'.json')
  if($process.ExitCode-ne0){throw ('Tab-layout tool failed; preserve '+$Log)}
  return [pscustomobject]$receipt
 }finally{$process.Dispose()}
}
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$build=Hidden $msbuild @((Join-Path $PSScriptRoot 'BrowserTabLayoutPreferencesTests.vcxproj'),'/m:1','/nodeReuse:false','/p:Configuration=Release','/p:Platform=x64','/p:BuildProjectReferences=false',('/p:OutDir='+(Join-Path $artifacts 'bin')+'\'),('/p:IntDir='+(Join-Path $artifacts 'obj')+'\'),'/v:minimal') (Join-Path $artifacts 'build.log') 300000
Fence
$native=Hidden (Join-Path $artifacts 'bin\BrowserTabLayoutPreferencesTests.exe') @((Join-Path $artifacts 'fixtures')) (Join-Path $artifacts 'native.log') 180000
Fence
$text=[IO.File]::ReadAllText($native.Log)
if($text-notmatch'(?m)^Browser tab layout preferences: 124 checks, 15 scenarios, 0 failures; native windows/show/focus/foreground=0/0/0/0\.\r?$'){throw 'Complete native tab-layout coverage or zero actual event guard is missing.'}
[ordered]@{Version='6.3.3.0';Passed=$true;Checks=124;Scenarios=15;SourceSnapshot=(Join-Path $artifacts 'source-hashes.json');SourceSnapshotSha256=(Get-FileHash -LiteralPath (Join-Path $artifacts 'source-hashes.json') -Algorithm SHA256).Hash;SourceFencePassed=$true;Build=$build;Native=$native;NativeWindows=0;NativeShows=0;NativeFocus=0;NativeForeground=0;Scope='Synthetic profile JSON and actual guarded file publication only; no browser, GUI, clipboard or input operations.'}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output 'Browser tab layout preferences: 124 checks, 15 scenarios, 0 failures; actual owned windows/show/focus/foreground=0.'
