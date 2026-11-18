#requires -Version 7.0
param([string]$ArtifactDirectory='', [switch]$PrepareOnly, [switch]$RunPrepared)
$ErrorActionPreference='Stop'
$self=[Diagnostics.Process]::GetCurrentProcess();$self.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal;$self.ProcessorAffinity=[IntPtr]12582912
$workspace=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
if(-not$ArtifactDirectory){$ArtifactDirectory=Join-Path $workspace ('build\tests\rra-'+[guid]::NewGuid().ToString('N').Substring(0,8))}
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory).TrimEnd('\')
function Need([bool]$Condition,[string]$Message){if(-not$Condition){throw $Message}}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
function NoReparse([string]$Path){for($cursor=[IO.Path]::GetFullPath($Path);$cursor;){if(Test-Path -LiteralPath $cursor){Need (-not((Get-Item -LiteralPath $cursor -Force).Attributes-band[IO.FileAttributes]::ReparsePoint)) ('Reparse path refused: '+$cursor)};$parent=[IO.Path]::GetDirectoryName($cursor);if($parent-eq$cursor){break};$cursor=$parent}}
Need ($artifacts.StartsWith((Join-Path $workspace 'build')+'\',[StringComparison]::OrdinalIgnoreCase)) 'Evidence must be under workspace build.'
Need (-not($PrepareOnly-and$RunPrepared)) 'PrepareOnly and RunPrepared are exclusive.'
NoReparse $artifacts
$tree=Join-Path $artifacts 'source';$fencePath=Join-Path $artifacts 'source-frozen.json';$header=Join-Path $tree 'recovery-restore-fixture.h'
if(-not$RunPrepared){
 Need (-not(Test-Path -LiteralPath $artifacts)) 'Use a new evidence directory; existing evidence is retained.'
 [IO.Directory]::CreateDirectory($tree)|Out-Null
 $production=@(Get-Content -LiteralPath (Join-Path $workspace 'build\recovery-path-2026-10-31\qualification\production-source-frozen.json') -Raw|ConvertFrom-Json)
 Need ($production.Count-eq572) 'The complete combined production inventory is required.'
 $names=@($production|ForEach-Object{[IO.Path]::GetRelativePath($workspace,$_.Path)})
 $names+=@('tests\RecoveryRestoreAdversarialTests.cpp','tests\RecoveryRestoreAdversarialTests.vcxproj','tests\RecoveryRestoreNativeBoundary.h','tests\Test-RecoveryRestoreAdversarial.ps1','tests\ArchiveUiLifetimeTests.cpp','tests\RetainedFixture.h','tests\Test-FullApp.ps1')
 $rc=Get-Content -LiteralPath (Join-Path $workspace 'ctSpaces.rc') -Raw
 foreach($match in [regex]::Matches($rc,'(?m)\b(?:ICON|RCDATA|BITMAP|MANIFEST)\s+"([^"\r\n]+)"')){$names+=$match.Groups[1].Value.Replace('/','\')}
 $origins=@(foreach($name in @($names|Sort-Object -Unique)){
  Need (-not[IO.Path]::IsPathRooted($name)-and$name-notmatch'(^|[\\/])\.\.([\\/]|$)') 'Relative source escape refused.'
  $original=Join-Path $workspace $name;$copy=Join-Path $tree $name;NoReparse $original;Need (Test-Path -LiteralPath $original -PathType Leaf) ('Missing source/resource: '+$original)
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($copy))|Out-Null;[IO.File]::Copy($original,$copy)
  $sha=Hash $original;Need ((Hash $copy)-eq$sha) ('Source copy changed: '+$name)
  [pscustomobject]@{RelativePath=$name;Original=$original;Path=$copy;Bytes=(Get-Item -LiteralPath $copy).Length;Sha256=$sha}
 })
 $fixtureParent=Join-Path $workspace ('build\ba31\'+[guid]::NewGuid().ToString('N').Substring(0,12));NoReparse $fixtureParent;Need (-not(Test-Path -LiteralPath $fixtureParent)) 'Fresh short fixture parent required.';[IO.Directory]::CreateDirectory($fixtureParent)|Out-Null
 $sourceLiteral=(Join-Path $tree 'ctSpaces.cpp').Replace('\','\\').Replace('"','\"');$fixtureLiteral=$fixtureParent.Replace('\','\\').Replace('"','\"')
 [IO.File]::WriteAllText($header,"#pragma once`r`n#define CTSPACES_ARCHIVE_UI_PRODUCTION_SOURCE `"$sourceLiteral`"`r`n#define CTSPACES_RECOVERY_ADVERSARIAL_FIXTURE_PARENT L`"$fixtureLiteral`"`r`n",[Text.UTF8Encoding]::new($false))
 $origins|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $artifacts 'copied-origins.json') -Encoding utf8
 $frozen=@($origins|Select-Object Path,Bytes,Sha256)+@([pscustomobject]@{Path=$header;Bytes=(Get-Item -LiteralPath $header).Length;Sha256=(Hash $header)})
 $frozen|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $fencePath -Encoding utf8
 Add-Type -AssemblyName System.IO.Compression.FileSystem
 $zip=Join-Path $artifacts 'source-snapshot.zip';[IO.Compression.ZipFile]::CreateFromDirectory($tree,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
 $archive=[IO.Compression.ZipFile]::OpenRead($zip)
 try{Need ($archive.Entries.Count-eq$frozen.Count) 'Frozen source ZIP entry count differs.';foreach($entry in $archive.Entries){$path=Join-Path $tree $entry.FullName.Replace('/','\');$stream=$entry.Open();try{$digest=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($stream))}finally{$stream.Dispose()};Need ($digest-eq(Hash $path)) ('Frozen ZIP entry differs: '+$entry.FullName)}}finally{$archive.Dispose()}
 [ordered]@{Prepared=$true;CompleteCopiedSourceTree=$tree;Project=(Join-Path $tree 'tests\RecoveryRestoreAdversarialTests.vcxproj');Inputs=$frozen.Count;Manifest=$fencePath;ManifestSha256=(Hash $fencePath);ProductionSource=(Join-Path $tree 'ctSpaces.cpp');ProductionSourceSha256=(Hash (Join-Path $tree 'ctSpaces.cpp'));SourceZip=$zip;SourceZipSha256=(Hash $zip);EveryZipEntryVerified=$true;FixtureParent=$fixtureParent;Fixture=(Join-Path $fixtureParent 'f');ProductionInputs=$production.Count;NoLiveSourceCompilation=$true}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'prepared.json') -Encoding utf8
}
Need (Test-Path -LiteralPath (Join-Path $artifacts 'prepared.json')) 'Prepared frozen tree required.'
$prepared=Get-Content -LiteralPath (Join-Path $artifacts 'prepared.json') -Raw|ConvertFrom-Json
Need ((Hash $fencePath)-eq$prepared.ManifestSha256-and(Hash $prepared.SourceZip)-eq$prepared.SourceZipSha256) 'Frozen source manifest/ZIP changed.'
Need ($prepared.FixtureParent.StartsWith((Join-Path $workspace 'build\ba31')+'\',[StringComparison]::OrdinalIgnoreCase)-and[IO.Path]::GetDirectoryName($prepared.Fixture)-eq$prepared.FixtureParent) 'Fixture binding escaped the short workspace-private scope.'
NoReparse $prepared.FixtureParent
$frozen=@(Get-Content -LiteralPath $fencePath -Raw|ConvertFrom-Json)
function Fence{foreach($item in $frozen){NoReparse $item.Path;Need ((Hash $item.Path)-eq$item.Sha256) ('Frozen build input changed: '+$item.Path)}}
Fence
if($PrepareOnly){$prepared|ConvertTo-Json;return}
Need (-not(Test-Path -LiteralPath (Join-Path $artifacts 'bin'))-and-not(Test-Path -LiteralPath (Join-Path $artifacts 'result.json'))-and-not(Test-Path -LiteralPath $prepared.Fixture)) 'Prepared output or fixture exists; preserve it.'
function Hidden([string]$Exe,[string[]]$Arguments,[string]$Log){
 Fence;$info=[Diagnostics.ProcessStartInfo]::new($Exe);$info.WorkingDirectory=$tree;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)};$started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($info)
 try{if(-not$process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal};$stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$timeout=-not$process.WaitForExit(600000);if($timeout){$process.Kill($true);$process.WaitForExit()};$text=$stdout.Result+$stderr.Result;[IO.File]::WriteAllText($Log,$text)
  [ordered]@{Exe=$Exe;BinarySha256=(Hash $Exe);Arguments=$Arguments;ExitCode=$process.ExitCode;TimedOut=$timeout;Seconds=([DateTime]::UtcNow-$started).TotalSeconds;CreateNoWindow=$true;Priority='BelowNormal';AffinityMask=12582912;LogSha256=(Hash $Log)}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($Log+'.json') -Encoding utf8
  Fence;Need (-not$timeout-and$process.ExitCode-eq0) ('Hidden adversarial test failed: '+$Log);return $text
 }finally{$process.Dispose()}
}
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe';$bin=Join-Path $artifacts 'bin';$obj=Join-Path $artifacts 'obj'
$null=Hidden $msbuild @($prepared.Project,'/m:1','/nodeReuse:false','/p:Configuration=Release','/p:Platform=x64','/p:BuildProjectReferences=false',('/p:OutDir='+$bin+'\'),('/p:IntDir='+$obj+'\'),('/p:RecoveryRestoreSourceHeader='+$header),'/v:minimal') (Join-Path $artifacts 'build.log')
$exe=Join-Path $bin 'RecoveryRestoreAdversarialTests.exe';$native=Hidden $exe @('--headless',$prepared.Fixture) (Join-Path $artifacts 'native.log')
foreach($marker in @('NATIVE_EVENTS=0/0/0','FORBIDDEN=0','VERSION=6.3.5.0','ORDINARY_VALID_ARCHIVE=1','UNICODE_SEQUENTIAL=1','MISSING_SITES=1','LAST_INSTANT_RECOVERY_COLLISION=1','FOREIGN_LIVE_RETAINED=1','FRESH_RETRY=1','NATIVE_RENAME_BOUNDARY_ONLY=1')){Need ($native-match('(?m)^'+[regex]::Escape($marker)+'\r?$')) ('Missing guard/scope marker: '+$marker)}
$values=@{};foreach($key in @('CHECKS','CASES','GUARD_CASES','FAILURES','PHYSICAL_FIXTURE_BYTES')){Need ($native-match('(?m)^'+$key+'=(\d+)\r?$')) ('Missing result '+$key);$values[$key]=[long]$Matches[1]}
Need ($values.CASES-eq7-and$values.GUARD_CASES-eq3-and$values.CHECKS-ge45-and$values.FAILURES-eq0-and$values.PHYSICAL_FIXTURE_BYTES-le2MB-and$values.PHYSICAL_FIXTURE_BYTES-gt0) 'Adversarial case/guard/fixture budget not confirmed.'
[ordered]@{Passed=$true;Version='6.3.5.0';Checks=$values.CHECKS;Cases=$values.CASES;GuardCases=$values.GUARD_CASES;Failures=$values.FAILURES;PhysicalFixtureBytes=$values.PHYSICAL_FIXTURE_BYTES;FixtureBudgetBytes=2MB;NativeEvents=@(0,0,0);Forbidden=0;NativeRenameBoundaryOnly=$true;ActualDiskFaultOrCrossPcTest=$false;OrdinaryValidArchive=$true;Fixture=$prepared.Fixture;FixturesRetained=$true;Binary=$exe;BinarySha256=(Hash $exe);Log=(Join-Path $artifacts 'native.log');LogSha256=(Hash (Join-Path $artifacts 'native.log'));Prepared=$prepared;SourceFencePassed=$true;OnlyCopiedInputsCompiled=$true;NoMouseForegroundBrowserClipboardDeviceInput=$true;Published=$false}|ConvertTo-Json -Depth 9|Set-Content -LiteralPath (Join-Path $artifacts 'result.json') -Encoding utf8
Write-Output $native
