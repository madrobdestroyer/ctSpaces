#requires -Version 7.0
param(
 [string]$Configuration='Release',
 [string]$Platform='x64',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\ClientTabLayout')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Client tab-layout evidence must stay below workspace build.'}
New-Item -ItemType Directory -Force -Path $artifacts|Out-Null
foreach($name in @('build.log','native.log','result.json','source-hashes.json')){
 if(Test-Path -LiteralPath (Join-Path $artifacts $name)){throw 'Use a new evidence directory; an earlier tab-layout verification is retained here.'}
}
$output=Join-Path $artifacts 'bin'
$intermediate=Join-Path $artifacts 'obj'
$sourcePaths=@(
 'ClientTabLayout.h','ClientDeletionSafety.h','ConfigPersistence.cpp','ConfigPersistence.h','SiblingStageName.h',
 'tests\ClientTabLayoutTests.cpp','tests\ClientTabLayoutTests.vcxproj','tests\Test-ClientTabLayout.ps1',
 'tests\RetainedFixture.h','tests\DirectoryJunctionFixture.h')|ForEach-Object{Join-Path $root $_}
$snapshot=@($sourcePaths|Sort-Object -Unique|ForEach-Object{[pscustomobject]@{Path=$_;Hash=(Get-FileHash -LiteralPath $_).Hash}})
$snapshot|ConvertTo-Json -Depth 3|Set-Content -LiteralPath (Join-Path $artifacts 'source-hashes.json')
function Assert-SourceUnchanged {
 $after=@($snapshot|ForEach-Object{[pscustomobject]@{Path=$_.Path;Hash=(Get-FileHash -LiteralPath $_.Path).Hash}})
 $after|ConvertTo-Json -Depth 3|Set-Content -LiteralPath (Join-Path $artifacts 'source-after.json')
 foreach($record in $snapshot){if(($after|Where-Object Path -EQ $record.Path).Hash-ne$record.Hash){throw ('Client tab-layout input changed during verification: '+$record.Path)}}
 [pscustomobject]@{Inputs=$snapshot.Count;Unchanged=$true;DateUtc=[DateTime]::UtcNow.ToString('o')}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'source-fence.json')
}
function Invoke-HiddenTool([string]$Exe,[string[]]$NativeArguments,[string]$Log,[int]$Timeout){
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
  $receipt=[pscustomobject]@{Exe=$Exe;BinarySha256=(Get-FileHash -LiteralPath $Exe).Hash;ExitCode=$process.ExitCode;TimedOut=$timedOut;Arguments=$NativeArguments;CreateNoWindow=$true;Priority='BelowNormal';Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash}
  $receipt|ConvertTo-Json -Depth 4|Set-Content -LiteralPath ($Log+'.json')
  if($timedOut){throw ('Hidden client tab-layout process timed out: '+$Log)}
  if($process.ExitCode-ne0){throw ('Hidden client tab-layout process failed: '+$Log)}
  return $contents
 }finally{$process.Dispose()}
}
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
try{$build=Invoke-HiddenTool $msbuild @((Join-Path $PSScriptRoot 'ClientTabLayoutTests.vcxproj'),'/m:1','/nodeReuse:false',"/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false',"/p:OutDir=$output\","/p:IntDir=$intermediate\",'/v:minimal') (Join-Path $artifacts 'build.log') 180000}finally{Assert-SourceUnchanged}
if($build-match'(?im)\bwarning [A-Z]+\d+\s*:|\berror [A-Z]+\d+\s*:'){throw 'The client tab-layout target must compile without warnings or errors.'}
$exe=Join-Path $output 'ClientTabLayoutTests.exe'
$exeHash=(Get-FileHash -LiteralPath $exe).Hash
try{$native=Invoke-HiddenTool $exe @((Join-Path $artifacts 'fixtures')) (Join-Path $artifacts 'native.log') 60000}finally{Assert-SourceUnchanged}
if((Get-FileHash -LiteralPath $exe).Hash-ne$exeHash){throw 'The tested client tab-layout executable changed during its run.'}
$complete=[regex]::Match($native,'Client tab layout: (\d+) checks, 14 private scenarios, 0 failures; filesystem-only, no browser/input/clipboard calls\.')
if(-not$complete.Success-or[int]$complete.Groups[1].Value-lt190){throw 'The complete client tab-layout native scope did not pass.'}
if(-not$native.Contains('Client layout native guards: visible=0; show/foreground/focus=0/0/0; active-focus-capture=0')){throw 'Native background observations did not remain zero.'}
if(-not$native.Contains('Malformed schema corpus: 18 exact preserved records')){throw 'The malformed/future schema corpus did not complete.'}
if(-not$native.Contains('Deletion authority lease: valid=1; corrupt-safe=1; oversized=0; hardlink=0; wrong-root=0')){throw 'Metadata validation did not complete under the real deletion root lease.'}
$namespaces=[regex]::Matches($native,'(?m)^Pre-publisher namespace: mode=([01]); attempted=1; retargeted=0; WindowsError=\d+; saved=1\r?$')
if($namespaces.Count-ne2-or@($namespaces|ForEach-Object{$_.Groups[1].Value}|Sort-Object -Unique).Count-ne2){throw 'The original client and parent retarget attempts were not both natively blocked.'}
$partials=[regex]::Matches($native,'(?m)^Partial publication: mode=([12]); saved=0; original-restored=1\r?$')
if($partials.Count-ne2-or@($partials|ForEach-Object{$_.Groups[1].Value}|Sort-Object -Unique).Count-ne2){throw 'Both actual native partial-publication rollback stages did not pass.'}
$result=[pscustomobject]@{
 Status='Passed';DateUtc=[DateTime]::UtcNow.ToString('o');Executable=$exe;ExecutableSha256=$exeHash
 Checks=[int]$complete.Groups[1].Value;Scenarios=14;Failures=0;SourceFenceInputs=$snapshot.Count;SourceFencePassed=$true
 BuildWarnings=0;BuildErrors=0;BuildLog=(Join-Path $artifacts 'build.log');BuildLogSha256=(Get-FileHash -LiteralPath (Join-Path $artifacts 'build.log')).Hash
 NativeLog=(Join-Path $artifacts 'native.log');NativeLogSha256=(Get-FileHash -LiteralPath (Join-Path $artifacts 'native.log')).Hash
 NativeGuards=@{VisibleOwned=0;ShowEvents=0;ForegroundEvents=0;FocusEvents=0;ActiveFocusCapture=0}
 Scope='Production client-owned settings helper; isolated retained files only. No application UI, browser, device input or clipboard APIs invoked.'
}
$result|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output $native
