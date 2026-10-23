#requires -Version 7.0
param([string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\NotesSpelling'))
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Spelling evidence must remain beneath the workspace build directory.'}
[IO.Directory]::CreateDirectory($artifacts)|Out-Null
$output=Join-Path $artifacts 'bin';$intermediate=Join-Path $artifacts 'obj'
$sourcePaths=@(Get-ChildItem -LiteralPath $root -File|Where-Object Extension -in '.cpp','.h','.inl','.rc','.vcxproj'|Select-Object -ExpandProperty FullName)
$sourcePaths+=@(Get-ChildItem -LiteralPath $PSScriptRoot -File|Where-Object {$_.Name-like'Notes*' -or $_.Name-eq'Test-NotesSpelling.ps1' -or $_.Name-eq'RetainedFixture.h'}|Select-Object -ExpandProperty FullName)
$snapshot=@($sourcePaths|Sort-Object -Unique|ForEach-Object {[pscustomobject]@{Path=$_;Hash=(Get-FileHash -LiteralPath $_).Hash}})
$snapshot|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'source-hashes.json')
function Assert-SourceUnchanged {foreach($record in $snapshot){if((Get-FileHash -LiteralPath $record.Path).Hash-ne $record.Hash){throw ('Spelling source changed during verification: '+$record.Path)}}}
function Invoke-HiddenSpellingTool([string]$Program,[string[]]$Arguments,[string]$Label,[int]$Timeout){
 $info=[Diagnostics.ProcessStartInfo]::new($Program);$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.WorkingDirectory=$root;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 $process=[Diagnostics.Process]::Start($info)
 try{
  try{$process.PriorityClass='BelowNormal'}catch{}
  $out=$process.StandardOutput.ReadToEndAsync();$err=$process.StandardError.ReadToEndAsync()
  if(-not $process.WaitForExit($Timeout*1000)){$process.Kill($true);throw ('Bounded spelling '+$Label+' timed out.')}
  $log=Join-Path $artifacts ($Label+'.log');[IO.File]::WriteAllText($log,$out.Result+$err.Result)
  if($process.ExitCode-ne 0){throw ('Spelling '+$Label+' failed; inspect '+$log)}
 }finally{$process.Dispose()}
}
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
Invoke-HiddenSpellingTool $msbuild @((Join-Path $PSScriptRoot 'NotesSpellingTests.vcxproj'),'/m:1','/nodeReuse:false','/p:Configuration=Release','/p:Platform=x64','/p:BuildProjectReferences=false',"/p:OutDir=$output\","/p:IntDir=$intermediate\",'/v:minimal') 'build' 600
Assert-SourceUnchanged
$executable=Join-Path $output 'NotesSpellingTests.exe'
Invoke-HiddenSpellingTool $executable @((Join-Path $artifacts 'captures')) 'native' 120
Assert-SourceUnchanged
$native=Get-Content -LiteralPath (Join-Path $artifacts 'native.log') -Raw
$summary=[regex]::Match($native,'Notes spelling: (\d+) UI checks, (\d+) helper checks, 0 failures; actual input/foreground/clipboard=0\.')
if(-not$summary.Success-or[int]$summary.Groups[1].Value-ne89-or[int]$summary.Groups[2].Value-ne66) {throw 'Complete production spelling coverage was not recorded.'}
$partialDiagnostic='Actual spelling partial-correction: attempted=1; exactTarget=1; partialNativeObserved=1; loading=1; accepted=0; checkChanged=0; rawNativeExact=1; nativeBindingsExact=1; durableExact=1; storedExact=1; canUndo=1; canRedo=0'
$friendlyDiagnostic='Actual spelling unrelated-link success: accepted=1; changed=1; expectedText=1; nativeURL=1; savedURL=1; savedChanged=1'
foreach($diagnostic in @($partialDiagnostic,$friendlyDiagnostic)){
 if([regex]::Matches($native,'(?m)^'+[regex]::Escape($diagnostic)+'\r?$').Count-ne1){throw 'The actual partial-correction refusal and unrelated-link successful-correction diagnostics must both complete exactly once.'}
}
$partialAssertions=@(
 'The actual spelling UI reaches the exact word and observes a real partial native insertion under loading suppression'
 'A refused partial spelling correction is never reported as an accepted change'
 'A refused partial spelling correction restores exact raw native content and binding runs'
 'The actual spelling failure cannot publish partial text to stored pages or durable notebook bytes'
 'Refused spelling leaves its original dictionary snapshot, scope and pending error intact'
 'Recovered spelling preserves dirty/save/loading flags, event routing and safe editor ownership'
 'Refused spelling preserves original native selection, scroll, limits, auto-detection and modified state'
 'Refused spelling keeps the older user''s Undo and cannot leave its partial insertion in Redo'
 'Pure Redo after the refused spelling correction cannot resurrect its partial insertion'
 'One Undo after refused spelling still removes only the user''s older completed edit'
 'Redo after the older spelling-fixture Undo restores only the original valid user edit'
 'Refused spelling and native history checks leave the independent client''s bytes unchanged'
)
$friendlyAssertions=@(
 'An actual full spelling correction succeeds without changing neighboring prose or link text'
 'Successful spelling retains the unrelated friendly link''s real native binding and URL'
 'Successful spelling saves the corrected word and the unrelated exact label, range and target mapping'
 'Saving the full spelling correction cannot change another client''s notebook'
 'One Undo of actual spelling restores the complete original word and unrelated link in one action'
 'One Redo of actual spelling restores the corrected word and unrelated URL together'
 'A fresh client-note reopen retains the corrected prose and unrelated friendly-link destination'
 'Fresh reopening the successful correction preserves both clients'' exact saved notebook bytes'
)
foreach($assertion in @($partialAssertions)+@($friendlyAssertions)){
 if([regex]::Matches($native,'(?m)^PASS '+[regex]::Escape($assertion)+'\r?$').Count-ne1){throw ('Required actual spelling regression did not pass: '+$assertion)}
}
$match=[regex]::Match($native,'Background guards: (\{[^\r\n]+\})')
if(-not $match.Success){throw 'Spelling background event audit is missing.'}
$guard=$match.Groups[1].Value|ConvertFrom-Json
foreach($field in 'forbiddenClipboard','forbiddenKeyboard','forbiddenRedraw','nativeVisibleEvents','nativeForegroundEvents','nativeFocusEvents') {if($null-eq $guard.$field -or $guard.$field-ne 0){throw ('Spelling background guard did not remain zero: '+$field)}}
[ordered]@{Status='Passed';UiChecks=[int]$summary.Groups[1].Value;HelperChecks=[int]$summary.Groups[2].Value;SourceFencePassed=$true;SourceInputCount=$snapshot.Count;NativeGuard=$guard;Executable=$executable;ExecutableSha256=(Get-FileHash -LiteralPath $executable).Hash;Captures=@((Join-Path $artifacts 'captures\spelling-dark.png'),(Join-Path $artifacts 'captures\spelling-light.png'));Limitation='Hidden native controls, installed Windows dictionaries and exact explicit corrections; physical input and visible compositor remain outside this background suite.'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Get-Content -LiteralPath (Join-Path $artifacts 'native.log')|Select-Object -Last 3
