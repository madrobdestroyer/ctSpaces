#requires -Version 7.0
param(
 [string]$ArtifactDirectory='',
 [ValidateSet('Prepare','Run','All')][string]$Mode='All',
 [ValidateSet('Before','CurrentBefore','PublicationBefore','After')][string]$Proof='After'
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$round=Join-Path $root 'build'
if(-not$ArtifactDirectory){$ArtifactDirectory=Join-Path $round ('tests\backup-exception-'+[Guid]::NewGuid().ToString('N').Substring(0,8))}
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory).TrimEnd('\');$source=Join-Path $artifacts 'source'
$utf8=[Text.UTF8Encoding]::new($false)
if(-not$artifacts.StartsWith($round+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Evidence must stay in the workspace build directory.'}
function Ancestors([string]$Path){
 $cursor=[IO.Path]::GetFullPath($Path)
 while($cursor){
  if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes-band[IO.FileAttributes]::ReparsePoint){throw ('Reparse path/ancestor refused: '+$cursor)}}
  $parent=[IO.Path]::GetDirectoryName($cursor);if(-not$parent-or$parent-eq$cursor){break};$cursor=$parent
 }
}
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
function Json([string]$Path,$Value){[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 10),$utf8)}
Ancestors $artifacts
$self=[Diagnostics.Process]::GetCurrentProcess()
$allowedMask=$self.ProcessorAffinity.ToInt64();$preferredMask=12582912L;$mask=0L;$processors=0
if(($allowedMask-band$preferredMask)-eq$preferredMask){$mask=$preferredMask;$processors=2}
else{
 for($bit=0;$bit-lt([IntPtr]::Size*8)-and$processors-lt2;$bit++){
  $flag=1L-shl$bit;if(($allowedMask-band$flag)-ne0){$mask=$mask-bor$flag;$processors++}
 }
}
if(-not$processors){throw 'No permitted logical processor is available.'}
$self.ProcessorAffinity=[IntPtr]::new($mask);$self.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal
$manifestPath=Join-Path $artifacts 'source-frozen.json';$preparedPath=Join-Path $artifacts 'prepared.json'
if($Mode-ne'Run'){
 if(Test-Path -LiteralPath $artifacts){throw 'Prepare requires a fresh directory; previous proof evidence is never overwritten.'}
 New-Item -ItemType Directory -Path $source|Out-Null
 $map=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
 $queue=[Collections.Generic.Stack[object]]::new()
 foreach($name in @('BackupEngine.cpp','BackupEngine.h','InProc7z.cpp','InProc7z.h','version.h','ctSpaces.cpp','dist\x64\Release\7zip.lib')){
  # ctSpaces.cpp is provenance only and is not a console compilation unit.
  $queue.Push([pscustomobject]@{Origin=(Join-Path $root $name);Relative=$name;Scan=($name-ne'ctSpaces.cpp')})
 }
 foreach($name in @('BackupExceptionRecoveryTests.cpp','BackupExceptionRecoveryTests.vcxproj','BackupExceptionNativeBoundary.h','Test-BackupExceptionRecovery.ps1')){
  $queue.Push([pscustomobject]@{Origin=(Join-Path $PSScriptRoot $name);Relative=('tests\'+$name);Scan=$true})
 }
 foreach($optional in @('Directory.Build.props','Directory.Build.targets')){
  if(Test-Path -LiteralPath (Join-Path $root $optional)){$queue.Push([pscustomobject]@{Origin=(Join-Path $root $optional);Relative=$optional;Scan=$true})}
 }
 while($queue.Count){
  $item=$queue.Pop();$origin=[IO.Path]::GetFullPath($item.Origin);$relative=$item.Relative
  if($map.ContainsKey($relative)){if($map[$relative]-ne$origin){throw 'Frozen relative-path collision.'};continue}
  if(-not$origin.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Include closure escaped the workspace.'}
  Ancestors $origin;if(-not(Test-Path -LiteralPath $origin -PathType Leaf)){throw ('Missing local compiler input: '+$origin)}
  $map.Add($relative,$origin)
  if(-not$item.Scan-or[IO.Path]::GetExtension($origin)-notin@('.h','.hpp','.cpp','.c','.inl')){continue}
  foreach($include in [regex]::Matches([IO.File]::ReadAllText($origin),'(?m)^\s*#\s*include\s*"([^"\r\n]+)"')){
   $name=$include.Groups[1].Value
   $candidate=[IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($origin)) $name))
   if(-not(Test-Path -LiteralPath $candidate -PathType Leaf)){$candidate=[IO.Path]::GetFullPath((Join-Path $root $name))}
   if(-not(Test-Path -LiteralPath $candidate -PathType Leaf)){continue}
   $candidateRelative=if($candidate.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){$candidate.Substring($root.Length+1)}else{throw 'A local include escaped the workspace.'}
   $queue.Push([pscustomobject]@{Origin=$candidate;Relative=$candidateRelative;Scan=$true})
  }
 }
 $records=@(foreach($relative in $map.Keys|Sort-Object){
  $origin=$map[$relative];$destination=[IO.Path]::GetFullPath((Join-Path $source $relative))
  if(-not$destination.StartsWith($source+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Frozen-copy destination escaped its root.'}
  Ancestors $destination;[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))|Out-Null
  [IO.File]::Copy($origin,$destination,$false)
  $originHash=Hash $origin;$copyHash=Hash $destination;if($originHash-ne$copyHash){throw 'Frozen copy differs from its original.'}
  [pscustomobject]@{Origin=$origin;Relative=$relative;Path=$destination;Sha256=$copyHash;Bytes=(Get-Item -LiteralPath $destination).Length}
 })
 Json $manifestPath $records
 $zip=Join-Path $artifacts 'source-snapshot.zip'
 [IO.Compression.ZipFile]::CreateFromDirectory($source,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
 $prepared=[ordered]@{Status='PreparedNoBuild';Proof=$Proof;Version='6.3.5.0';Inputs=$records.Count;Manifest=$manifestPath;ManifestSha256=(Hash $manifestPath);SourceArchive=$zip;SourceArchiveSha256=(Hash $zip);CompilerRun=$false;FixtureRun=$false;AffinityMask=$mask;Priority='BelowNormal';ProductionSourcesEdited=$false}
 Json $preparedPath $prepared
 if($Mode-eq'Prepare'){$prepared|ConvertTo-Json -Depth 5;return}
}

# Run is a separate, explicitly granted sole compiler lane. It consumes only
# the already-frozen tree and never reconstructs the baseline after a failure.
$prepared=Get-Content -LiteralPath $preparedPath -Raw|ConvertFrom-Json
if($prepared.Proof-ne$Proof-or(Hash $manifestPath)-ne$prepared.ManifestSha256-or(Hash $prepared.SourceArchive)-ne$prepared.SourceArchiveSha256){throw 'Prepared proof/source identity differs.'}
$records=@(Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json)
function Frozen([string]$Boundary){foreach($record in $records){Ancestors $record.Path;if((Hash $record.Path)-ne$record.Sha256){throw ('Frozen source changed at '+$Boundary+': '+$record.Path)}}}
function Idle {
 if(@(Get-CimInstance Win32_Process -Filter "Name='MSBuild.exe' OR Name='cl.exe' OR Name='link.exe' OR Name='lib.exe' OR Name='rc.exe'").Count){throw 'Another compiler is active. Wait for explicit sole-lane grant.'}
}
function Hidden([string]$Exe,[string[]]$Arguments,[string]$Log,[int]$Timeout=300000){
 if(Test-Path -LiteralPath $Log){throw 'Native proof log already exists; preserve this attempt.'}
 $info=[Diagnostics.ProcessStartInfo]::new($Exe);$info.WorkingDirectory=$source;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 $started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($info)
 try{
  try{if(-not$process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal;$process.ProcessorAffinity=[IntPtr]::new($mask)}}catch [InvalidOperationException]{if(-not$process.HasExited){throw}}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$timedOut=-not$process.WaitForExit($Timeout)
  # Stop only this exact owned worker tree on a bounded test timeout.
  if($timedOut){$process.Kill($true);$process.WaitForExit()}
  $content=$stdout.Result+$stderr.Result;[IO.File]::WriteAllText($Log,$content,$utf8)
  Json ($Log+'.json') ([ordered]@{Exe=$Exe;Arguments=$Arguments;Pid=$process.Id;ExitCode=$process.ExitCode;TimedOut=$timedOut;Seconds=([DateTime]::UtcNow-$started).TotalSeconds;CreateNoWindow=$true;Priority='BelowNormal';AffinityMask=$mask;LogSha256=(Hash $Log)})
  if($timedOut-or$process.ExitCode-ne0){throw ('Owned hidden proof worker failed: '+$Log)}
  return $content
 }finally{$process.Dispose()}
}
Idle;Frozen 'pre-build'
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$bin=Join-Path $artifacts 'bin';$obj=Join-Path $artifacts 'obj'
$null=Hidden $msbuild @((Join-Path $source 'tests\BackupExceptionRecoveryTests.vcxproj'),'/m:1','/nodeReuse:false','/p:Configuration=Release','/p:Platform=x64','/p:BuildProjectReferences=false',"/p:OutDir=$bin\","/p:IntDir=$obj\",'/v:minimal') (Join-Path $artifacts 'build.log')
Frozen 'post-build'
$fenced=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);foreach($record in $records){$null=$fenced.Add($record.Path)}
$unfenced=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($log in Get-ChildItem -LiteralPath $obj -Filter 'CL.read.*.tlog' -Recurse -File){
 Ancestors $log.FullName
 foreach($line in [IO.File]::ReadAllLines($log.FullName,[Text.Encoding]::Unicode)){
  $path=$line.TrimStart('^')
  if($path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)-and-not$fenced.Contains($path)){$null=$unfenced.Add($path)}
 }
}
if($unfenced.Count){Json (Join-Path $artifacts 'unfenced-inputs.json') @($unfenced);throw 'Compiler consumed an unfrozen workspace input.'}
Idle;$binary=Join-Path $bin 'BackupExceptionRecoveryTests.exe';$fixture=Join-Path $artifacts 'f'
$proofArgument=switch($Proof){'Before'{'--before'} 'CurrentBefore'{'--current-before'} 'PublicationBefore'{'--publication-before'} 'After'{'--after'}}
$native=Hidden $binary @($proofArgument,'--fixture',$fixture) (Join-Path $artifacts 'native.log') 120000
Frozen 'post-native'
$expected=switch($Proof){'Before'{1} 'CurrentBefore'{2} 'PublicationBefore'{1} 'After'{0}}
$scenarios=switch($Proof){'Before'{4} 'CurrentBefore'{2} 'PublicationBefore'{1} 'After'{7}}
$regression=if($Proof-eq'After'){1}else{0}
foreach($marker in @("SCENARIOS=$scenarios","EXPECTED_DEFECTS=$expected","REGRESSION_PASSED=$regression",'NO_UI_OR_DEVICE_INPUT=1')){
 if($native-notmatch('(?m)^'+[regex]::Escape($marker)+'\r?$')){throw ('Missing focused proof marker: '+$marker)}
}
$postCommitProbe=$Proof-in'PublicationBefore','After'
if($postCommitProbe){
 $postCommitMarker=if($Proof-eq'PublicationBefore'){'COMMITTED_RESULT published=1 result_ok=0 allocation_hits=1 pending=0'}else{'COMMITTED_RESULT published=1 result_ok=1 allocation_hits=0 pending=1'}
 if($native-notmatch('(?m)^'+[regex]::Escape($postCommitMarker)+'\r?$')){throw 'Missing exact post-publication result marker.'}
}
$match=[regex]::Match($native,'(?m)^Backup exception recovery checks passed: (\d+)\.\r?$');if(-not$match.Success){throw 'Focused proof did not finish all assertions.'}
Json (Join-Path $artifacts 'result.json') ([ordered]@{Status=$(if($Proof-eq'After'){'Passed'}else{'Expected defect reproduced'});Proof=$Proof;Version='6.3.5.0';Checks=[int]$match.Groups[1].Value;Scenarios=$scenarios;ExpectedDefects=$expected;RegressionPassed=($Proof-eq'After');Manifest=$manifestPath;ManifestSha256=$prepared.ManifestSha256;SourceArchive=$prepared.SourceArchive;SourceArchiveSha256=$prepared.SourceArchiveSha256;SourcesFrozen=$true;CompilerWorkspaceInputsFenced=$true;Binary=$binary;BinarySha256=(Hash $binary);NativeLog=(Join-Path $artifacts 'native.log');NativeLogSha256=(Hash (Join-Path $artifacts 'native.log'));Fixture=$fixture;AllocationPressure=$false;OneShotThreadLocalAllocationFault=$true;NativeDispositionsBoundToExactOwnedArchive=$true;ExactSourceLeaseProbe=($Proof-in'CurrentBefore','After');ExactPostPublicationProbe=$postCommitProbe;NoUiBrowserNetworkClipboardOrDeviceInput=$true;ProductionSourcesEdited=$false;UsableCopiesUpdated=$false;Published=$false})
Get-Content -LiteralPath (Join-Path $artifacts 'result.json') -Raw
