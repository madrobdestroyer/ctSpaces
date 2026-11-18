#requires -Version 7.0
param(
 [ValidateSet('Release')][string]$Configuration='Release',
 [ValidateSet('x64')][string]$Platform='x64',
 [string]$ArtifactDirectory='',
 [ValidateSet('Before','After')][string]$Proof='After'
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $ArtifactDirectory){$ArtifactDirectory=Join-Path $root ('build\tests\brf-'+[Guid]::NewGuid().ToString('N').Substring(0,8))}
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Backup recovery evidence must remain below the workspace build directory.'}
# DirectoryInfo handles ancestor traversal for both existing and future outputs.
function Check-Ancestors([string]$Path){$cursor=[IO.DirectoryInfo]::new($Path);while($null-ne$cursor){if(Test-Path -LiteralPath $cursor.FullName){$item=Get-Item -LiteralPath $cursor.FullName -Force;if($item.Attributes-band[IO.FileAttributes]::ReparsePoint){throw ('Reparse ancestor refused: '+$item.FullName)}};$cursor=$cursor.Parent}}
Check-Ancestors $artifacts
if(Test-Path -LiteralPath $artifacts){throw 'Use a fresh evidence directory; previous evidence is never overwritten or removed.'}
if(@(Get-CimInstance Win32_Process|Where-Object Name -in @('MSBuild.exe','cl.exe','link.exe')).Count){throw 'Another compiler is active; this regression uses one serial lane only.'}
$worker=[Diagnostics.Process]::GetCurrentProcess()
$allowedAffinity=$worker.ProcessorAffinity.ToInt64();$serialAffinity=0L;$selectedProcessors=0
for($bit=0;$bit-lt([IntPtr]::Size*8)-and$selectedProcessors-lt2;$bit++){
 $flag=1L-shl$bit
 if(($allowedAffinity-band$flag)-ne0){$serialAffinity=$serialAffinity-bor$flag;$selectedProcessors++}
}
if(-not$selectedProcessors){throw 'No permitted logical processor was available for the serial test lane.'}
$worker.ProcessorAffinity=[IntPtr]$serialAffinity;$worker.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal
if($worker.ProcessorAffinity.ToInt64()-ne$serialAffinity){throw 'The selected serial processor affinity was not established.'}
New-Item -ItemType Directory -Path $artifacts|Out-Null
$outputDirectory=Join-Path $artifacts "$Platform\$Configuration"
$intermediateDirectory=Join-Path $artifacts "obj\$Platform\$Configuration"
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if(-not(Test-Path -LiteralPath $msbuild -PathType Leaf)){throw 'MSBuild was not found.'}
# Follow local quoted includes from the actual translation units and forced
# boundary header. Unrelated tests may be edited by a separate read-only lane.
$paths=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$queue=[Collections.Generic.Stack[string]]::new()
$names=@('BackupEngine.cpp','BackupEngine.h','InProc7z.cpp','InProc7z.h','version.h',
 'tests\BackupRecoveryFaultTests.cpp','tests\BackupRecoveryFaultTests.vcxproj',
 'tests\BackupRecoveryNativeBoundary.h','tests\Test-BackupRecoveryFault.ps1','dist\x64\Release\7zip.lib')
foreach($name in $names){$queue.Push((Join-Path $root $name))}
foreach($optional in @('Directory.Build.props','Directory.Build.targets')){if(Test-Path -LiteralPath (Join-Path $root $optional)){$queue.Push((Join-Path $root $optional))}}
while($queue.Count){
 $path=[IO.Path]::GetFullPath($queue.Pop());if(-not $paths.Add($path)){continue}
 Check-Ancestors $path
 if(-not(Test-Path -LiteralPath $path -PathType Leaf)){throw ('Missing source input: '+$path)}
 if([IO.Path]::GetExtension($path)-notin @('.h','.hpp','.cpp','.c','.inl')){continue}
 $text=[IO.File]::ReadAllText($path)
 foreach($include in [regex]::Matches($text,'(?m)^\s*#\s*include\s*"([^"\r\n]+)"')){
  $candidate=[IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetDirectoryName($path)) $include.Groups[1].Value))
  if(-not(Test-Path -LiteralPath $candidate -PathType Leaf)){$candidate=[IO.Path]::GetFullPath((Join-Path $root $include.Groups[1].Value))}
  if((Test-Path -LiteralPath $candidate -PathType Leaf)-and$candidate.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){$queue.Push($candidate)}
 }
}
$sources=@($paths|Sort-Object|ForEach-Object{[pscustomobject]@{Path=$_;Sha256=(Get-FileHash -LiteralPath $_).Hash}})
$sources|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
function Assert-Frozen([string]$Name){$now=@($sources|ForEach-Object{[pscustomobject]@{Path=$_.Path;Sha256=(Get-FileHash -LiteralPath $_.Path).Hash}});$now|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $artifacts $Name);if((ConvertTo-Json -InputObject $now -Compress)-ne(ConvertTo-Json -InputObject $sources -Compress)){throw 'Backup recovery source inputs changed; all evidence is retained.'}}
function Invoke-HiddenBackupTool([string]$Exe,[string[]]$NativeArguments,[string]$Log,[int]$Timeout=300000){
 $info=[Diagnostics.ProcessStartInfo]::new([IO.Path]::GetFullPath($Exe));$info.WorkingDirectory=$root;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $NativeArguments){$info.ArgumentList.Add($argument)}
 $started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($info)
 try{
  try{if(-not $process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal;$process.ProcessorAffinity=[IntPtr]$serialAffinity}}catch [InvalidOperationException]{if(-not $process.HasExited){throw}}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$timedOut=-not$process.WaitForExit($Timeout)
  if($timedOut){$process.Kill($true);$process.WaitForExit()}
  $contents=$stdout.Result+$stderr.Result;[IO.File]::WriteAllText($Log,$contents)
  [ordered]@{Exe=$info.FileName;BinarySha256=(Get-FileHash -LiteralPath $info.FileName).Hash;Arguments=$NativeArguments;ExitCode=$process.ExitCode;TimedOut=$timedOut;Seconds=([DateTime]::UtcNow-$started).TotalSeconds;CreateNoWindow=$true;Priority='BelowNormal';ProcessorAffinity=$serialAffinity;InheritedAllowedAffinity=$allowedAffinity;SelectedLogicalProcessors=$selectedProcessors;Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($Log+'.json')
  if($timedOut-or$process.ExitCode-ne0){throw "Hidden backup recovery tool failed; inspect $Log"};return $contents
 }finally{$process.Dispose()}
}
$null=Invoke-HiddenBackupTool $msbuild @((Join-Path $PSScriptRoot 'BackupRecoveryFaultTests.vcxproj'),'/m:1','/nodeReuse:false',"/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false',"/p:OutDir=$outputDirectory\","/p:IntDir=$intermediateDirectory\",'/v:minimal') (Join-Path $artifacts 'build.log')
Assert-Frozen 'source-after-build.json'
# Confirm every workspace file read by the compiler belongs to the initial
# source/include fence, rather than trusting an incomplete hand-written list.
$unfenced=[Collections.Generic.List[string]]::new()
foreach($log in Get-ChildItem -LiteralPath $intermediateDirectory -Filter 'CL.read.*.tlog' -Recurse -File){foreach($line in [IO.File]::ReadAllLines($log.FullName,[Text.Encoding]::Unicode)){
 $path=$line.TrimStart('^');if($path.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)-and-not$path.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)-and-not$paths.Contains($path)){$unfenced.Add($path)}
}}
if($unfenced.Count){$unfenced|Set-Content -LiteralPath (Join-Path $artifacts 'unfenced-compiler-inputs.txt');throw 'Compiler read a workspace source missing from the input fence.'}
$binary=Join-Path $outputDirectory 'BackupRecoveryFaultTests.exe';$fixture=Join-Path $artifacts 'f'
$nativeLog=Invoke-HiddenBackupTool $binary @($(if($Proof-eq'Before'){'--before'}else{'--after'}),'--fixture',$fixture) (Join-Path $artifacts 'native.log') 180000
Assert-Frozen 'source-after-native.json'
$expectedMissing=if($Proof-eq'Before'){2}else{0};$expectedRegression=if($Proof-eq'Before'){0}else{1}
$markers=@('ORIGINAL_CHECKS=95','ORIGINAL_SCENARIOS=11','TERMINAL_CASES=4','UNEXPECTED_FAILURES=0',"MISSING_RETENTION_PATHS=$expectedMissing","REGRESSION_PASSED=$expectedRegression")
foreach($marker in $markers){if($nativeLog-notmatch('(?m)^'+[regex]::Escape($marker)+'\r?$')){throw ('Incomplete backup recovery proof: '+$marker)}}
if($nativeLog-notmatch '(?m)^Backup recovery fault checks passed: 136 \(15 scenarios; seed0x7135A29B\)\.\r?$'){throw 'The complete original and terminal backup fault scope was not confirmed.'}
if($Proof-eq'Before'-and$nativeLog-notmatch '(?m)^EXPECTED_BEFORE_DEFECTS_REPRODUCED=2\r?$'){throw 'Before proof did not reproduce both missing retained-path diagnostics.'}
$bytes=[long]([regex]::Match($nativeLog,'(?m)^PHYSICAL_FIXTURE_BYTES=(\d+)\r?$').Groups[1].Value)
if($bytes-le0-or$bytes-gt2MB){throw 'The two MiB ordinary fixture budget was not established.'}
[ordered]@{Status=$(if($Proof-eq'Before'){'Expected defects reproduced'}else{'Passed'});Proof=$Proof;Version='6.3.5.0';Checks=136;Scenarios=15;OriginalChecks=95;OriginalScenarios=11;TerminalCases=4;ExpectedMissingRetentionDiagnostics=$expectedMissing;RegressionPassed=($Proof-eq'After');SourceFencePassed=$true;Sources=$sources;CompilerWorkspaceInputsFenced=$true;ProcessorAffinity=$serialAffinity;InheritedAllowedAffinity=$allowedAffinity;SelectedLogicalProcessors=$selectedProcessors;BelowNormal=$true;MSBuildParallelism=1;Fixture=$fixture;FixtureBytes=$bytes;FixtureBudgetBytes=2MB;NoUiBrowserNetworkClipboardOrDeviceInput=$true;Binary=$binary;BinarySha256=(Get-FileHash -LiteralPath $binary).Hash;NativeLog=(Join-Path $artifacts 'native.log');NativeLogSha256=(Get-FileHash -LiteralPath (Join-Path $artifacts 'native.log')).Hash;ProductionSourcesEdited=$false;Published=$false}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output $nativeLog
