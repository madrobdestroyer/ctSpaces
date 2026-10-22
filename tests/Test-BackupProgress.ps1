#requires -Version 7.0
param(
 [ValidateSet('Bridge','Functional','RealSdk')][string]$Mode='Functional',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\BackupProgress'),
 [switch]$ExpectedBridgeFailure
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not$artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Progress evidence must remain below workspace build.'}
if($ExpectedBridgeFailure-and$Mode-ne'Bridge'){throw 'Expected-failure evidence is only supported for the deterministic bridge reproduction.'}
New-Item -ItemType Directory -Force -Path $artifacts|Out-Null
$hashes=@('BackupEngine.cpp','BackupEngine.h','InProc7z.cpp','InProc7z.h','ClientNameComparison.h','SiblingStageName.h','tests\BackupProgressTests.cpp','tests\BackupProgressTests.vcxproj','tests\Test-BackupProgress.ps1','version.h','dist\x64\Release\7zip.lib')|ForEach-Object{$p=Join-Path $root $_;[pscustomobject]@{Path=$p;Sha256=(Get-FileHash -LiteralPath $p).Hash}}
$hashes|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'source-hashes.json')
function Assert-Frozen {foreach($item in $hashes){if((Get-FileHash -LiteralPath $item.Path).Hash-ne$item.Sha256){throw ('Progress source changed: '+$item.Path)}}}
function Invoke-Hidden([string]$Exe,[string[]]$Arguments,[string]$Log,[int]$Timeout=180000,[int]$Expected=0){
 $info=[Diagnostics.ProcessStartInfo]::new($Exe);$info.WorkingDirectory=$root;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 $start=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($info)
 try{
  try{if(-not$process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal}}catch [InvalidOperationException]{if(-not$process.HasExited){throw}}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  if(-not$process.WaitForExit($Timeout)){$process.Kill($true);throw 'Owned progress probe timed out.'}
  $contents=$stdout.Result+$stderr.Result;[IO.File]::WriteAllText($Log,$contents)
  [pscustomobject]@{Exe=$Exe;Sha256=(Get-FileHash -LiteralPath $Exe).Hash;Arguments=$Arguments;ExitCode=$process.ExitCode;ExpectedExitCode=$Expected;Seconds=([DateTime]::UtcNow-$start).TotalSeconds;CreateNoWindow=$true;Priority='BelowNormal';Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath ($Log+'.json')
  if($process.ExitCode-ne$Expected){throw ('Progress tool failed: '+$Log)}
  return $contents
 }finally{$process.Dispose()}
}
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$bin=Join-Path $artifacts 'bin';$obj=Join-Path $artifacts 'obj'
$null=Invoke-Hidden $msbuild @((Join-Path $PSScriptRoot 'BackupProgressTests.vcxproj'),'/m:1','/nodeReuse:false','/p:Configuration=Release','/p:Platform=x64','/p:BuildProjectReferences=false',('/p:OutDir='+$bin+'\'),('/p:IntDir='+$obj+'\'),'/v:minimal') (Join-Path $artifacts 'build.log')
Assert-Frozen
$arguments=if($Mode-eq'Bridge'){@('--bridge')}else{@($(if($Mode-eq'Functional'){'--functional'}else{'--real-sdk'}),(Join-Path $root ('build\bp16-'+[guid]::NewGuid().ToString('N').Substring(0,10))))}
$expected=if($ExpectedBridgeFailure){1}else{0}
$native=Invoke-Hidden (Join-Path $bin 'BackupProgressTests.exe') $arguments (Join-Path $artifacts 'native.log') 240000 $expected
Assert-Frozen
if($ExpectedBridgeFailure){if(-not$native.Contains('Backup progress checks=2 failures=2')){throw 'Deterministic before-bridge scope was not confirmed.'}}
else {
 $checks=switch($Mode){'Functional'{29};'Bridge'{7};'RealSdk'{5}}
 if($native-notmatch ('(?m)^Backup progress checks='+$checks+' failures=0\r?$')){throw 'Complete progress scope was not confirmed.'}
}
[pscustomobject]@{Passed=(-not$ExpectedBridgeFailure);ExpectedBeforeFailure=$ExpectedBridgeFailure.IsPresent;Mode=$Mode;SourceFreeze=$true;SourceHashes=Join-Path $artifacts 'source-hashes.json';NativeReceipt=Join-Path $artifacts 'native.log.json'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output $native
