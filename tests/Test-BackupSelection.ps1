#requires -Version 7.0
param(
 [string]$Configuration='Release',
 [string]$Platform='x64',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\BackupSelection'),
 [ValidateSet('All','FocusedBefore')][string]$Proof='All'
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not$artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Evidence must remain inside the workspace build directory.'}
New-Item -ItemType Directory -Force -Path $artifacts|Out-Null
$sourcePaths=@(Get-ChildItem -LiteralPath $root -File|Where-Object Extension -in '.cpp','.h','.inl','.rc'|Select-Object -ExpandProperty FullName)
$sourcePaths+=@(Get-ChildItem -LiteralPath $PSScriptRoot -File|Where-Object {$_.Name-like'Notes*' -or $_.Name-like'*BackupSelection*'}|Select-Object -ExpandProperty FullName)
$snapshot=@($sourcePaths|Sort-Object -Unique|ForEach-Object{Get-FileHash -LiteralPath $_})
$snapshot|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
function Fence {
 foreach($item in $snapshot){if((Get-FileHash -LiteralPath $item.Path).Hash-ne$item.Hash){throw ('Backup selection inputs changed during verification: '+$item.Path)}}
}
function Hidden([string]$Exe,[string[]]$Arguments,[string]$Log,[int]$Timeout) {
 $info=[Diagnostics.ProcessStartInfo]::new($Exe);$info.WorkingDirectory=$root;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 $started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($info)
 try {
  try{if(-not$process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal}}catch [InvalidOperationException]{if(-not$process.HasExited){throw}}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  if(-not$process.WaitForExit($Timeout)){$process.Kill($true);throw ('Bounded hidden test timed out: '+$Exe)}
  [IO.File]::WriteAllText($Log,$stdout.Result+$stderr.Result)
  $receipt=[pscustomobject]@{Exe=$Exe;Sha256=(Get-FileHash -LiteralPath $Exe).Hash;Arguments=$Arguments;ExitCode=$process.ExitCode;Seconds=([DateTime]::UtcNow-$started).TotalSeconds;Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash;CreateNoWindow=$true}
  $receipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($Log+'.json')
  if($receipt.ExitCode-ne0){throw ('Hidden tool failed; see '+$Log)}
  return $receipt
 }finally{$process.Dispose()}
}
function ReadGuard([string]$Text,[string]$Scope) {
 $match=[regex]::Match($Text,'Background guards: (\{[^\r\n]+\})')
 if(-not$match.Success){throw ('Background event audit is missing for '+$Scope)}
 $guard=$match.Groups[1].Value|ConvertFrom-Json
 foreach($field in 'forbiddenClipboard','forbiddenKeyboard','forbiddenRedraw','nativeVisibleEvents','nativeForegroundEvents','nativeFocusEvents'){
  if($null-eq$guard.$field -or $guard.$field-ne0){throw ('Background guard did not remain zero for '+$Scope+' '+$field)}
 }
 return $guard
}
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$bin=(Join-Path $artifacts 'bin')+'\';$obj=(Join-Path $artifacts 'obj')+'\'
$build=Hidden $msbuild @((Join-Path $PSScriptRoot 'BackupSelectionTests.vcxproj'),'/m:1','/nodeReuse:false',"/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false','/p:NotesRedrawTestDefinitions=',('/p:OutDir='+$bin),('/p:IntDir='+$obj),'/v:minimal') (Join-Path $artifacts 'build.log') 300000
Fence
$binary=Join-Path $bin 'BackupSelectionTests.exe'
$focusedArtifacts=Join-Path $artifacts 'focused';New-Item -ItemType Directory -Force -Path $focusedArtifacts|Out-Null
$before=$Proof-eq'FocusedBefore'
$focusMode=if($before){'--focused-before'}else{'--focused-after'}
$focused=Hidden $binary @('--headless-background',$focusedArtifacts,$focusMode) (Join-Path $artifacts 'native-focused.log') 90000
Fence
$focusedText=[IO.File]::ReadAllText($focused.Log)
$expected=if($before){2}else{0}
$focusedChecks=if($before){17}else{18}
$reported=if($before){0}else{1}
if($focusedText-notmatch "Backup selection: $focusedChecks checks; 2 private groups; 0 failures." -or
   $focusedText-notmatch "(?m)^FOCUSED_EXPECTED_DEFECTS=$expected\r?$" -or
   $focusedText-notmatch "(?m)^MANIFEST_NOTICE path=.+ exists=1 reported=$reported\r?$" -or
   $focusedText-notmatch "(?m)^NOTES_ONLY_BACKUP picker=$reported published=$reported\r?$"){
 throw 'The retained-manifest and notes-only backup proofs did not finish with the expected outcomes.'
}
$focusedGuard=ReadGuard $focusedText 'focused backup selection'
if($before){
 @{Checks=$focusedChecks;Scenarios=2;ExpectedDefects=$expected;Proof=$Proof;Build=$build;FocusedNative=$focused;SourceFencePassed=$true;FocusedGuard=$focusedGuard}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
 Write-Output "Backup selection before proof: $focusedChecks checks, 2 groups, 2 defects reproduced; actual visible/focus/foreground/clipboard/input=0."
 return
}
$native=Hidden $binary @('--headless-background',$artifacts) (Join-Path $artifacts 'native.log') 90000
Fence
$text=[IO.File]::ReadAllText($native.Log)
if($text-notmatch 'Backup selection: 74 checks; 5 private groups; 0 failures.'){throw 'The complete selection and excluded-Notes scenarios did not finish.'}
$guard=ReadGuard $text 'ordinary backup selection'
$modalArtifacts=Join-Path $artifacts 'modal';New-Item -ItemType Directory -Force -Path $modalArtifacts|Out-Null
$modal=Hidden $binary @('--headless-background',$modalArtifacts,'--modal-snapshot') (Join-Path $artifacts 'native-modal.log') 90000
Fence
$modalText=[IO.File]::ReadAllText($modal.Log)
if($modalText-notmatch 'Backup selection: 16 checks; 1 private groups; 0 failures.'){throw 'The native owned-modal snapshot scenario did not finish.'}
$modalGuard=ReadGuard $modalText 'owned-modal backup selection'
$allChecks=90+$focusedChecks
@{Checks=$allChecks;Scenarios=8;ExpectedDefects=$expected;Build=$build;Native=$native;ModalNative=$modal;FocusedNative=$focused;SourceFencePassed=$true;NativeGuard=$guard;ModalGuard=$modalGuard;FocusedGuard=$focusedGuard}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output "Backup selection: $allChecks checks, 8 groups, 0 failures; actual visible/focus/foreground/clipboard/input=0."
