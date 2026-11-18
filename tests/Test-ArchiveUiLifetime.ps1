#requires -Version 7.0
param(
 [ValidateSet('Release')][string]$Configuration='Release',
 [ValidateSet('x64')][string]$Platform='x64',
 [string]$ArtifactDirectory='',
 [switch]$NotesSaveRefusalProof,
 [switch]$PostStageBrowserProof,
 [switch]$UnsupportedArchiveProof,
 [switch]$BrowserDuringConfirmationProof
)
$ErrorActionPreference='Stop'
$selectedProofs=@($NotesSaveRefusalProof,$PostStageBrowserProof,$UnsupportedArchiveProof,$BrowserDuringConfirmationProof |
 Where-Object { $_ })
if($selectedProofs.Count -gt 1){
 throw 'Select only one focused archive UI scenario.'
}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $ArtifactDirectory){$ArtifactDirectory=Join-Path $root ('build\tests\aui-'+[Guid]::NewGuid().ToString('N').Substring(0,8))}
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){
 throw 'Archive UI lifetime evidence must remain below the workspace build directory.'
}
function Assert-NoReparse([string]$Path){
 $cursor=[IO.DirectoryInfo]::new($Path)
 while($null-ne$cursor){
  if(Test-Path -LiteralPath $cursor.FullName){$item=Get-Item -LiteralPath $cursor.FullName -Force;if($item.Attributes-band[IO.FileAttributes]::ReparsePoint){throw ('Reparse output/fixture ancestor refused: '+$item.FullName)}}
  $cursor=$cursor.Parent
 }
}
Assert-NoReparse $artifacts
Assert-NoReparse (Join-Path $root 'build\qa-aui')
if(Test-Path -LiteralPath $artifacts){throw 'Archive UI evidence directory must be fresh; prior files are never overwritten or removed.'}
New-Item -ItemType Directory -Path $artifacts|Out-Null
$source=Join-Path $root 'ctSpaces.cpp'
$sourceHash=(Get-FileHash -LiteralPath $source).Hash
$testHash=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'ArchiveUiLifetimeTests.cpp')).Hash
$sourcePaths=@(Get-ChildItem -LiteralPath $root -File|Where-Object Extension -in @('.cpp','.h','.inl','.rc')|Select-Object -ExpandProperty FullName)+@(
 (Join-Path $PSScriptRoot 'ArchiveUiLifetimeTests.cpp'),(Join-Path $PSScriptRoot 'ArchiveUiLifetimeTests.vcxproj'),
 (Join-Path $PSScriptRoot 'Test-ArchiveUiLifetime.ps1'),(Join-Path $PSScriptRoot 'RetainedFixture.h'),
 (Join-Path $root 'dist\x64\Release\7zip.lib'))
$sourceBefore=@($sourcePaths|ForEach-Object{[pscustomobject]@{Path=$_;Sha256=(Get-FileHash -LiteralPath $_).Hash}})
$sourceBefore|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
function Assert-SourceFence([string]$Name){
 $now=@($sourcePaths|ForEach-Object{[pscustomobject]@{Path=$_;Sha256=(Get-FileHash -LiteralPath $_).Hash}})
 $now|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $artifacts $Name)
 if((ConvertTo-Json -InputObject $now -Compress)-ne(ConvertTo-Json -InputObject $sourceBefore -Compress)){throw 'Archive UI source inputs changed during this run; evidence retained.'}
}
$outputDirectory=Join-Path $artifacts 'bin'
$intermediateDirectory=Join-Path $artifacts 'obj'
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if(-not(Test-Path -LiteralPath $msbuild -PathType Leaf)){throw 'MSBuild was not found.'}
function Invoke-HiddenArchiveUiTool([string]$Exe,[string[]]$NativeArguments,[string]$Log,[int]$Timeout=300000){
 $info=[Diagnostics.ProcessStartInfo]::new([IO.Path]::GetFullPath($Exe))
 $info.WorkingDirectory=$root;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $NativeArguments){$info.ArgumentList.Add($argument)}
 $started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($info)
 try{
  try{if(-not $process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal}}
  catch [InvalidOperationException]{if(-not $process.HasExited){throw}}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  $timedOut=-not $process.WaitForExit($Timeout)
  if($timedOut){$process.Kill($true);$process.WaitForExit()}
  $contents=$stdout.Result+$stderr.Result
  [IO.File]::WriteAllText($Log,$contents)
  [pscustomobject]@{Exe=$info.FileName;BinarySha256=(Get-FileHash -LiteralPath $info.FileName).Hash;SourceSha256=$sourceHash;TestSha256=$testHash;Arguments=$NativeArguments;ExitCode=$process.ExitCode;Seconds=([DateTime]::UtcNow-$started).TotalSeconds;TimedOut=$timedOut;CreateNoWindow=$true;Priority='BelowNormal';Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($Log+'.json')
  if($timedOut-or$process.ExitCode-ne0){throw "Hidden archive UI lifetime tool failed; inspect $Log"}
  return $contents
 }finally{$process.Dispose()}
}
$null=Invoke-HiddenArchiveUiTool $msbuild @(
 (Join-Path $PSScriptRoot 'ArchiveUiLifetimeTests.vcxproj'),'/m:1','/nodeReuse:false',
 "/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false','/p:ArchiveUiTestDefines=',
 "/p:OutDir=$outputDirectory\","/p:IntDir=$intermediateDirectory\",'/v:minimal'
) (Join-Path $artifacts 'build.log')
Assert-SourceFence 'source-after-build.json'
$binary=Join-Path $outputDirectory 'ArchiveUiLifetimeTests.exe'
$nativeArguments=@()
if($NotesSaveRefusalProof){$nativeArguments+='--notes-save-refusal'}
if($PostStageBrowserProof){$nativeArguments+='--post-stage-browser'}
if($UnsupportedArchiveProof){$nativeArguments+='--unsupported-archive'}
if($BrowserDuringConfirmationProof){$nativeArguments+='--browser-during-confirmation'}
$nativeLog=Invoke-HiddenArchiveUiTool $binary $nativeArguments (Join-Path $artifacts 'native.log') 120000
$focusedProof=$NotesSaveRefusalProof -or $PostStageBrowserProof
$beforeStageProof=$UnsupportedArchiveProof -or $BrowserDuringConfirmationProof
if($beforeStageProof){
 $marker=if($UnsupportedArchiveProof){'RESTORE_UNSUPPORTED_EXTENSION'}else{'RESTORE_BROWSER_DURING_CONFIRMATION'}
 if($nativeLog-notmatch '(?m)^Archive UI lifetime: 11 checks, 1 scenarios, 0 failures; suppressed=\d+; native show/focus/foreground=0/0/0; forbidden=0\.\r?$' -or
    $nativeLog-notmatch "(?m)^${marker} refused=1 undoRetained=1 retry=1 recoveredDraft=1\r?$"){
  throw 'The focused hidden before-staging refusal and retry were not confirmed.'
 }
}elseif($focusedProof){
 $marker=if($NotesSaveRefusalProof){'RESTORE_NOTES_SAVE_REFUSAL'}else{'RESTORE_POST_STAGE_BROWSER'}
 if($nativeLog-notmatch '(?m)^Archive UI lifetime: 14 checks, 1 scenarios, 0 failures; suppressed=\d+; native show/focus/foreground=0/0/0; forbidden=0\.\r?$' -or
    $nativeLog-notmatch "(?m)^${marker} blocked=1 undoRetained=1 stageRetained=1 reported=1 retry=1 recoveredDraft=1\r?$"){
  throw 'The focused hidden post-staging refusal and retry were not confirmed.'
 }
}elseif($nativeLog-notmatch '(?m)^Archive UI lifetime: 173 checks, 27 scenarios, 0 failures; suppressed=\d+; native show/focus/foreground=0/0/0; forbidden=0\.\r?$' -or
   $nativeLog-notmatch '(?m)^RESTORE_COMMIT_WORKER=1 PUMP=1 BUSY=1 REENTRY_BLOCKED=1\r?$' -or
   $nativeLog-notmatch '(?m)^RESTORE_COMMIT_LIFECYCLE mode=5 worker=1 pump=1 busy=1 result=0x80070005 quit=0\r?$' -or
   $nativeLog-notmatch '(?m)^RESTORE_COMMIT_LIFECYCLE mode=6 worker=1 pump=1 busy=1 result=0x80070005 quit=1\r?$' -or
   $nativeLog-notmatch '(?m)^RESTORE_COMMIT_LIFECYCLE mode=7 worker=1 pump=1 busy=1 result=0x0 quit=1\r?$' -or
   $nativeLog-notmatch '(?m)^TRACKED_RESTORE_SCOPE profile=Temp picker=1 stage=1 recoveries=1\r?$' -or
   $nativeLog-notmatch '(?m)^TRACKED_RESTORE_SCOPE profile=Default picker=1 stage=1 recoveries=1\r?$' -or
   $nativeLog-notmatch '(?m)^TRACKED_RESTORE_SCOPE profile=Alpha picker=0 stage=0 recoveries=0\r?$' -or
   $nativeLog-notmatch '(?m)^FOREIGN_RECOVERY_NOTICE case=precommit-collision occupant=1 falselyNamed=0\r?$' -or
   $nativeLog-notmatch '(?m)^FOREIGN_RECOVERY_NOTICE case=missing-sites occupant=1 falselyNamed=0\r?$' -or
   $nativeLog-notmatch '(?m)^RESTORE_CLEANUP_NOTICE retained=1 sharing=32 reported=1\r?$' -or
   $nativeLog-notmatch '(?m)^RESTORED_ICON_CACHE installed=1 cacheFresh=1\r?$' -or
   $nativeLog-notmatch '(?m)^RESTORE_NOTES_CANCEL step=picker undoRetained=1\r?$' -or
   $nativeLog-notmatch '(?m)^RESTORE_NOTES_CANCEL step=confirmation undoRetained=1\r?$' -or
   $nativeLog-notmatch '(?m)^RESTORE_NOTES_COMMIT stagedOpen=1 recoveredDraft=1\r?$' -or
   $nativeLog-notmatch '(?m)^RESTORE_SHUTDOWN_QUERY refused=1 busy=1 undoRetained=1\r?$' -or
   $nativeLog-notmatch '(?m)^READY_WORKER_QUIT posted=1 found=1 message=18 code=88\r?$'){
 throw 'The complete hidden archive UI lifecycle scope was not confirmed.'
}
Assert-SourceFence 'source-after-native.json'
$fixture=[regex]::Match($nativeLog,'(?m)^FIXTURE_ROOT=(.+)\r?$').Groups[1].Value.Trim()
if(-not $fixture){throw 'Retained private fixture identity was not reported.'}
Assert-NoReparse $fixture
if($beforeStageProof){
 [ordered]@{
  Status='Passed';Version='6.3.5.0';Checks=11;Scenarios=1;Failures=0
  NativeShows=0;NativeFocus=0;NativeForeground=0;Forbidden=0
  SourceFencePassed=$true;SourceInputs=$sourceBefore.Count;Binary=$binary;BinarySha256=(Get-FileHash -LiteralPath $binary).Hash
  Fixture=$fixture;FixturesRetained=$true;ProductionSourceChanged=$false
  UnsupportedArchiveProof=[bool]$UnsupportedArchiveProof;BrowserDuringConfirmationProof=[bool]$BrowserDuringConfirmationProof
  RefusedBeforeStaging=$true;PendingDraftAndUndoPreserved=$true
  SupportedRetryPassed=$true;PendingDraftRecovered=$true;MouseOrForegroundUsed=$false;Published=$false
 }|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
 Write-Output $nativeLog
 return
}
if($focusedProof){
 [ordered]@{
  Status='Passed';Version='6.3.5.0';Checks=14;Scenarios=1;Failures=0
  NativeShows=0;NativeFocus=0;NativeForeground=0;Forbidden=0
  SourceFencePassed=$true;SourceInputs=$sourceBefore.Count;Binary=$binary;BinarySha256=(Get-FileHash -LiteralPath $binary).Hash
  Fixture=$fixture;FixturesRetained=$true;ProductionSourceChanged=$false
  NotesSaveRefusalProof=[bool]$NotesSaveRefusalProof;PostStageBrowserProof=[bool]$PostStageBrowserProof
  RestoreCommitBlocked=$true;PendingDraftAndUndoPreserved=$true
  VerifiedStageRetained=$true;VerifiedStageReported=$true;UnlockedRetryPassed=$true;PendingDraftRecovered=$true
  MouseOrForegroundUsed=$false;Published=$false
 }|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
 Write-Output $nativeLog
 return
}
[ordered]@{Status='Passed';Version='6.3.5.0';Checks=173;Scenarios=27;Failures=0;NativeShows=0;NativeFocus=0;NativeForeground=0;Forbidden=0;SourceFencePassed=$true;SourceInputs=$sourceBefore.Count;Binary=$binary;BinarySha256=(Get-FileHash -LiteralPath $binary).Hash;Fixture=$fixture;FixturesRetained=$true;ProductionSourceChanged=$false;RealCommitFailureRollback=$true;RealCommitQuitSuccessAndFailure=$true;ReadyWorkerQuitPreserved=$true;DeferredBrowserExitOwned=$true;CloseAndExplicitExitBlockedWhileBusy=$true;TrackedProfileRestoreScope=$true;RecoveryNoticeOwnership=$true;RestoreCleanupRefusalPath=$true;RestoreClientIconCache=$true;CancelledRestorePreservesNotesUndo=$true;CommittedRestoreSavesNotesBeforeSwap=$true;ShutdownQueryPreservesNotesWhileRestoreBusy=$true;MouseOrForegroundUsed=$false;Published=$false}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output $nativeLog
