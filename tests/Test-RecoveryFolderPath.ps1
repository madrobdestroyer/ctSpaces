#requires -Version 7.0
param([string]$ArtifactDirectory='', [string]$SourceDirectory='', [string]$ProductionSource='', [switch]$ExpectedBeforeFailure, [switch]$PrepareOnly, [switch]$RunPrepared)
$ErrorActionPreference='Stop'
$self=[Diagnostics.Process]::GetCurrentProcess();$self.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal;$self.ProcessorAffinity=[IntPtr]12582912
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not$SourceDirectory){$SourceDirectory=$workspace};$source=[IO.Path]::GetFullPath($SourceDirectory).TrimEnd('\')
if(-not$ArtifactDirectory){$ArtifactDirectory=Join-Path $workspace ('build\tests\rfp-'+[guid]::NewGuid().ToString('N').Substring(0,8))}
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory).TrimEnd('\')
function Need([bool]$condition,[string]$message){if(-not$condition){throw $message}}
function NoReparse([string]$path){for($cursor=[IO.Path]::GetFullPath($path);$cursor;){if(Test-Path -LiteralPath $cursor){Need (-not((Get-Item -LiteralPath $cursor -Force).Attributes-band[IO.FileAttributes]::ReparsePoint)) ('Reparse path refused: '+$cursor)};$parent=[IO.Path]::GetDirectoryName($cursor);if($parent-eq$cursor){break};$cursor=$parent}}
function Hash([string]$path){(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
Need ($artifacts.StartsWith((Join-Path $workspace 'build')+'\',[StringComparison]::OrdinalIgnoreCase)) 'Evidence must be under workspace build.'
Need ($source-eq$workspace-or$source.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)) 'Source must remain workspace-bound.'
Need (-not($PrepareOnly-and$RunPrepared)) 'PrepareOnly and RunPrepared are exclusive.'
NoReparse $artifacts;NoReparse $source
if($ProductionSource){$ProductionSource=[IO.Path]::GetFullPath($ProductionSource);Need ($ProductionSource.StartsWith($workspace+'\',[StringComparison]::OrdinalIgnoreCase)) 'Production override must be workspace-bound.';NoReparse $ProductionSource}
$tree=Join-Path $artifacts 'source';$fencePath=Join-Path $artifacts 'source-frozen.json';$header=Join-Path $tree 'recovery-fixture.h'
if(-not$RunPrepared){
 Need (-not(Test-Path -LiteralPath $artifacts)) 'Use a new evidence directory; existing evidence is retained.'
 [IO.Directory]::CreateDirectory($tree)|Out-Null
 $production=@(Get-Content -LiteralPath (Join-Path $workspace 'build\live-ui-2026-10-31\pass3\production-source-frozen.json') -Raw|ConvertFrom-Json)
 $names=@($production|ForEach-Object {[IO.Path]::GetRelativePath($workspace,$_.Path)})
 foreach($sdkRoot in @('3p\7zip\C','3p\7zip\CPP')){foreach($sdk in Get-ChildItem -LiteralPath (Join-Path $source $sdkRoot) -Recurse -File -Filter '*.h'){$names+=[IO.Path]::GetRelativePath($source,$sdk.FullName)}}
 $names+=@('tests\RecoveryFolderPathTests.cpp','tests\RecoveryFolderPathTests.vcxproj','tests\Test-RecoveryFolderPath.ps1','tests\ArchiveUiLifetimeTests.cpp','tests\RetainedFixture.h','tests\Test-FullApp.ps1')
 $rc=Get-Content -LiteralPath (Join-Path $source 'ctSpaces.rc') -Raw
 foreach($match in [regex]::Matches($rc,'(?m)\b(?:ICON|RCDATA|BITMAP|MANIFEST)\s+"([^"\r\n]+)"')){$names+=$match.Groups[1].Value.Replace('/','\')}
 $origins=@(foreach($name in @($names|Sort-Object -Unique)){
  Need (-not[IO.Path]::IsPathRooted($name)-and$name-notmatch'(^|[\\/])\.\.([\\/]|$)') 'Relative source escape refused.'
  $original=if($name-eq'ctSpaces.cpp'-and$ProductionSource){$ProductionSource}else{Join-Path $source $name};$copy=Join-Path $tree $name;NoReparse $original;Need (Test-Path -LiteralPath $original -PathType Leaf) ('Missing source/resource: '+$original)
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($copy))|Out-Null;[IO.File]::Copy($original,$copy)
  $sha=Hash $original;Need ((Hash $copy)-eq$sha) ('Source copy changed: '+$name)
  if($ExpectedBeforeFailure){$baseline=@($production|Where-Object {[IO.Path]::GetRelativePath($workspace,$_.Path)-eq$name});if($baseline.Count){Need ($sha-eq$baseline[0].Sha256) ('Before production input differs from A50: '+$name)}}
  [pscustomobject]@{RelativePath=$name;Original=$original;Path=$copy;Bytes=(Get-Item -LiteralPath $copy).Length;Sha256=$sha}
 })
 $sourceLiteral=(Join-Path $tree 'ctSpaces.cpp').Replace('\','\\').Replace('"','\"');$fixtureLiteral=$artifacts.Replace('\','\\').Replace('"','\"')
 [IO.File]::WriteAllText($header,"#pragma once`r`n#define CTSPACES_ARCHIVE_UI_PRODUCTION_SOURCE `"$sourceLiteral`"`r`n#define CTSPACES_RECOVERY_FIXTURE_PARENT L`"$fixtureLiteral`"`r`n",[Text.UTF8Encoding]::new($false))
 $origins|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $artifacts 'copied-origins.json') -Encoding utf8
 $frozen=@($origins|Select-Object Path,Bytes,Sha256)+@([pscustomobject]@{Path=$header;Bytes=(Get-Item -LiteralPath $header).Length;Sha256=(Hash $header)})
 $frozen|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $fencePath -Encoding utf8
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 $zip=Join-Path $artifacts 'source-snapshot.zip';[IO.Compression.ZipFile]::CreateFromDirectory($tree,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
 $archive=[IO.Compression.ZipFile]::OpenRead($zip)
 try{foreach($entry in $archive.Entries){$path=Join-Path $tree $entry.FullName.Replace('/','\');$stream=$entry.Open();try{$digest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))}finally{$stream.Dispose()};Need ($digest-eq(Hash $path)) ('Frozen ZIP entry differs: '+$entry.FullName)}}finally{$archive.Dispose()}
 [ordered]@{Prepared=$true;CompleteCopiedSourceTree=$tree;Project=(Join-Path $tree 'tests\RecoveryFolderPathTests.vcxproj');Inputs=$frozen.Count;Manifest=$fencePath;ManifestSha256=(Hash $fencePath);ProductionSource=(Join-Path $tree 'ctSpaces.cpp');ProductionSourceSha256=(Hash (Join-Path $tree 'ctSpaces.cpp'));SourceZip=$zip;SourceZipSha256=(Hash $zip);EveryZipEntryVerified=$true;ExpectedBeforeFailure=[bool]$ExpectedBeforeFailure}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'prepared.json') -Encoding utf8
}
Need (Test-Path -LiteralPath (Join-Path $artifacts 'prepared.json')) 'Prepared frozen tree required.'
$prepared=Get-Content -LiteralPath (Join-Path $artifacts 'prepared.json') -Raw|ConvertFrom-Json
Need ((Hash $fencePath)-eq$prepared.ManifestSha256-and(Hash $prepared.SourceZip)-eq$prepared.SourceZipSha256) 'Frozen source manifest/ZIP changed.'
$frozen=@(Get-Content -LiteralPath $fencePath -Raw|ConvertFrom-Json)
function Fence{foreach($item in $frozen){NoReparse $item.Path;Need ((Hash $item.Path)-eq$item.Sha256) ('Frozen build input changed: '+$item.Path)}}
Fence
if($PrepareOnly){$prepared|ConvertTo-Json;return}
Need (-not(Test-Path -LiteralPath (Join-Path $artifacts 'bin'))-and-not(Test-Path -LiteralPath (Join-Path $artifacts 'result.json'))) 'Prepared build output exists; preserve it.'
function Hidden([string]$exe,[string[]]$arguments,[string]$log,[int]$expected=0){
 Fence;$info=[Diagnostics.ProcessStartInfo]::new($exe);$info.WorkingDirectory=$tree;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $arguments){$info.ArgumentList.Add($argument)};$started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($info)
 try{$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal;$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$timeout=-not$process.WaitForExit(600000);if($timeout){$process.Kill($true);$process.WaitForExit()};$text=$stdout.Result+$stderr.Result;[IO.File]::WriteAllText($log,$text)
  [ordered]@{Exe=$exe;BinarySha256=(Hash $exe);Arguments=$arguments;ExitCode=$process.ExitCode;ExpectedExit=$expected;TimedOut=$timeout;Seconds=([DateTime]::UtcNow-$started).TotalSeconds;CreateNoWindow=$true;Priority='BelowNormal';AffinityMask=12582912;LogSha256=(Hash $log)}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($log+'.json') -Encoding utf8
  Fence;Need (-not$timeout-and$process.ExitCode-eq$expected) ('Hidden proof failed: '+$log);return $text
 }finally{$process.Dispose()}
}
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe';$bin=Join-Path $artifacts 'bin';$obj=Join-Path $artifacts 'obj'
$null=Hidden $msbuild @($prepared.Project,'/m:1','/nodeReuse:false','/p:Configuration=Release','/p:Platform=x64','/p:BuildProjectReferences=false',('/p:OutDir='+$bin+'\'),('/p:IntDir='+$obj+'\'),('/p:RecoveryFolderSourceHeader='+$header),'/v:minimal') (Join-Path $artifacts 'build.log')
$exe=Join-Path $bin 'RecoveryFolderPathTests.exe';$expected=if($ExpectedBeforeFailure){1}else{0};$native=Hidden $exe @('--headless',(Join-Path $artifacts 'f')) (Join-Path $artifacts 'native.log') $expected
foreach($marker in @('NATIVE_EVENTS=0/0/0','FORBIDDEN=0','VERSION=6.3.5.0','API_OCCUPANCY_SIMULATION_ONLY=1')){Need ($native-match('(?m)^'+[regex]::Escape($marker)+'\r?$')) ('Missing guard/scope marker: '+$marker)}
$values=@{};foreach($key in @('CHECKS','CASES','FAILURES','LONG_COMMIT','CALLER_BEFORE_STAGE','PHYSICAL_FIXTURE_BYTES')){Need ($native-match('(?m)^'+$key+'=(\d+)\r?$')) ('Missing result '+$key);$values[$key]=[long]$Matches[1]}
Need ($values.CASES-eq9-and$values.CHECKS-ge27-and$values.PHYSICAL_FIXTURE_BYTES-le2MB) 'Focused case/fixture budget not confirmed.'
if($ExpectedBeforeFailure){Need ($values.FAILURES-gt0-and$values.LONG_COMMIT-eq0-and$values.CALLER_BEFORE_STAGE-eq0) 'Original recovery defect not reproduced.'}else{Need ($values.FAILURES-eq0-and$values.LONG_COMMIT-eq1-and$values.CALLER_BEFORE_STAGE-eq1) 'Recovery repair did not pass.'}
[ordered]@{RegressionPassed=-not[bool]$ExpectedBeforeFailure;ExpectedDefectReproduced=[bool]$ExpectedBeforeFailure;Version='6.3.5.0';Checks=$values.CHECKS;Cases=$values.CASES;Failures=$values.FAILURES;LongCurrentTreeCommit=[bool]$values.LONG_COMMIT;CallerFailsBeforeStage=[bool]$values.CALLER_BEFORE_STAGE;PhysicalFixtureBytes=$values.PHYSICAL_FIXTURE_BYTES;NativeEvents=@(0,0,0);Forbidden=0;ApiOccupancySimulationOnly=$true;RealDiskExhaustionTested=$false;Binary=$exe;BinarySha256=(Hash $exe);Log=(Join-Path $artifacts 'native.log');LogSha256=(Hash (Join-Path $artifacts 'native.log'));Prepared=$prepared;SourceFencePassed=$true;OnlyCopiedInputsCompiled=$true}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $artifacts 'result.json') -Encoding utf8
Write-Output $native
