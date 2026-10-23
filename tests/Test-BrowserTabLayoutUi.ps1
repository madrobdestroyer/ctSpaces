#requires -Version 7.0
param([string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\BrowserTabLayoutUi'))
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Tab-layout UI evidence must remain below workspace build.'}
New-Item -ItemType Directory -Force -Path $artifacts|Out-Null
foreach($name in 'result.json','build.log','native.log','source-hashes.json'){
 if(Test-Path -LiteralPath (Join-Path $artifacts $name)){throw 'Existing evidence must remain intact; choose a fresh artifact directory.'}
}
# This native fixture includes the entire current app translation unit. Fence
# all local implementation/header/resource inputs rather than accepting an old
# source exception. Root coordinates a source freeze before invoking it.
$inputs=@(Get-ChildItem -LiteralPath $root -File|Where-Object{$_.Extension -in '.cpp','.h','.inl','.rc'}|Select-Object -ExpandProperty FullName)
$inputs+=@('BrowserTabLayoutUiTests.cpp','BrowserTabLayoutUiTests.vcxproj','Test-BrowserTabLayoutUi.ps1','RetainedFixture.h')|ForEach-Object{Join-Path $PSScriptRoot $_}
$inputs+=Join-Path $root 'dist\x64\Release\7zip.lib'
$snapshot=@($inputs|Sort-Object -Unique|ForEach-Object{[ordered]@{Path=$_;Sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}})
$sourcePath=Join-Path $artifacts 'source-hashes.json'
$snapshot|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $sourcePath
function Fence([string]$Stage){foreach($row in $snapshot){if((Get-FileHash -LiteralPath $row.Path -Algorithm SHA256).Hash-ne$row.Sha256){throw ('Tab-layout UI source changed '+$Stage+': '+$row.Path)}}}
function Hidden([string]$Exe,[string[]]$Arguments,[string]$Log,[int]$Timeout){
 $info=[Diagnostics.ProcessStartInfo]::new($Exe);$info.WorkingDirectory=$root;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 $started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($info)
 try{
  try{if(-not$process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal}}catch [InvalidOperationException]{if(-not$process.HasExited){throw}}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync();$timedOut=-not$process.WaitForExit($Timeout)
  if($timedOut){$process.Kill($true);$process.WaitForExit()}
  [IO.File]::WriteAllText($Log,$stdout.Result+$stderr.Result)
  $receipt=[ordered]@{Exe=$Exe;Sha256=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash;Arguments=$Arguments;ExitCode=$process.ExitCode;TimedOut=$timedOut;CreateNoWindow=$true;Priority='BelowNormal';Seconds=([DateTime]::UtcNow-$started).TotalSeconds;Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log -Algorithm SHA256).Hash}
  $receipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($Log+'.json')
  if($timedOut-or$process.ExitCode-ne0){throw ('Owned tab-layout UI tool failed or timed out; preserve '+$Log)}
  return [pscustomobject]$receipt
 }finally{$process.Dispose()}
}
Fence 'before build'
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$build=Hidden $msbuild @((Join-Path $PSScriptRoot 'BrowserTabLayoutUiTests.vcxproj'),'/m:1','/nodeReuse:false','/p:Configuration=Release','/p:Platform=x64','/p:BuildProjectReferences=false',('/p:OutDir='+(Join-Path $artifacts 'bin')+'\'),('/p:IntDir='+(Join-Path $artifacts 'obj')+'\'),'/v:minimal') (Join-Path $artifacts 'build.log') 360000
Fence 'after build'
$buildText=[IO.File]::ReadAllText($build.Log)
$warnings=@([regex]::Matches($buildText,'(?im)^.*\bwarning\s+[A-Z]+\d+\s*:.*$')).Count
$errors=@([regex]::Matches($buildText,'(?im)^.*\berror\s+[A-Z]+\d+\s*:.*$')).Count
# The full-app fixture compiles at Level4 and replaces MessageBox with an inert
# test hook. Its preexisting diagnostics are independent of the final app's
# zero-warning release gate. Seal the29 reviewed exact sites/messages from the
# first build; reject any new diagnostic, including any in this test or feature.
$reviewedWarnings=@'
[{"Path":"ctSpaces.cpp","Line":2861,"Code":"C4189","Message":"'bDark': local variable is initialized but not referenced"},{"Path":"ctSpaces.cpp","Line":3692,"Code":"C4389","Message":"'==': signed/unsigned mismatch"},{"Path":"ctSpaces.cpp","Line":3678,"Code":"C4100","Message":"'op': unreferenced parameter"},{"Path":"ctSpaces.cpp","Line":5980,"Code":"C4457","Message":"declaration of 'message' hides function parameter"},{"Path":"ctSpaces.cpp","Line":5989,"Code":"C4457","Message":"declaration of 'message' hides function parameter"},{"Path":"ctSpaces.cpp","Line":13405,"Code":"C4389","Message":"'!=': signed/unsigned mismatch"},{"Path":"ctSpaces.cpp","Line":13409,"Code":"C4389","Message":"'!=': signed/unsigned mismatch"},{"Path":"ctSpaces.cpp","Line":18732,"Code":"C4457","Message":"declaration of 'id' hides function parameter"},{"Path":"ctSpaces.cpp","Line":19383,"Code":"C4456","Message":"declaration of 'bounds' hides previous local declaration"},{"Path":"ctSpaces.cpp","Line":20275,"Code":"C4456","Message":"declaration of 'dialog' hides previous local declaration"},{"Path":"ctSpaces.cpp","Line":20374,"Code":"C4456","Message":"declaration of 'target' hides previous local declaration"},{"Path":"NotesWorkspaceUi.inl","Line":109,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch"},{"Path":"NotesWorkspaceUi.inl","Line":312,"Code":"C4100","Message":"'dialog': unreferenced parameter"},{"Path":"NotesProductivityUi.inl","Line":28,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch"},{"Path":"NotesCodeUi.inl","Line":17,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch"},{"Path":"NotesSpellingUi.inl","Line":135,"Code":"C4456","Message":"declaration of 'current' hides previous local declaration"},{"Path":"NotesWorkspaceUi.inl","Line":670,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch"},{"Path":"NotesWorkspaceUi.inl","Line":687,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch"},{"Path":"NotesWorkspaceUi.inl","Line":688,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch"},{"Path":"NotesTemplatesUi.inl","Line":247,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch"},{"Path":"NotesWorkspaceUi.inl","Line":1308,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch"},{"Path":"NotesInspectorUi.inl","Line":266,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch"},{"Path":"NotesInspectorUi.inl","Line":278,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch"},{"Path":"ctSpaces.cpp","Line":25106,"Code":"C4100","Message":"'hWnd': unreferenced parameter"},{"Path":"ctSpaces.cpp","Line":21315,"Code":"C4702","Message":"unreachable code"},{"Path":"ctSpaces.cpp","Line":21316,"Code":"C4702","Message":"unreachable code"},{"Path":"ctSpaces.cpp","Line":21317,"Code":"C4702","Message":"unreachable code"},{"Path":"ctSpaces.cpp","Line":21323,"Code":"C4702","Message":"unreachable code"},{"Path":"ctSpaces.cpp","Line":21324,"Code":"C4702","Message":"unreachable code"}]
'@|ConvertFrom-Json
$observedWarnings=@([regex]::Matches($buildText,'(?m)^([^\r\n]+)\((\d+)(?:,\d+)?\): warning (C\d+): ([^\r\n]+?) \[.*?\]\r?$')|ForEach-Object{[ordered]@{Path=[IO.Path]::GetRelativePath($root,$_.Groups[1].Value);Line=[int]$_.Groups[2].Value;Code=$_.Groups[3].Value;Message=$_.Groups[4].Value}})
function WarningKey($Row){'{0}|{1}|{2}|{3}'-f$Row.Path,$Row.Line,$Row.Code,$Row.Message}
$expectedKeys=@($reviewedWarnings|ForEach-Object{WarningKey $_}|Sort-Object)
$actualKeys=@($observedWarnings|ForEach-Object{WarningKey $_}|Sort-Object)
if($errors-or$warnings-ne29-or$observedWarnings.Count-ne$warnings-or(($expectedKeys-join"`n")-cne($actualKeys-join"`n"))){throw "Unexpected native inclusion diagnostic: $warnings warnings and $errors errors; compare reviewed exact baseline."}
$warningPath=Join-Path $artifacts 'native-inclusion-warnings.json'
$observedWarnings|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $warningPath
$native=Hidden (Join-Path $artifacts 'bin\BrowserTabLayoutUiTests.exe') @() (Join-Path $artifacts 'native.log') 180000
Fence 'after native run'
$text=[IO.File]::ReadAllText($native.Log)
$summary=[regex]::Match($text,'(?m)^Browser tab layout UI: (\d+) checks, (\d+) scenarios, 0 failures; native windows/show/focus/foreground=0/0/0/0; intercepted process requests=(\d+); external=0; foreign paint=0; clipboard reads=0\.\r?$')
if(-not$summary.Success-or[int]$summary.Groups[1].Value-ne57-or[int]$summary.Groups[2].Value-ne31-or[int]$summary.Groups[3].Value-ne33){throw 'Complete actual menu/launch coverage or zero native event guards missing.'}
$fixture=[regex]::Match($text,'(?m)^PRIVATE_FIXTURE (.+?)\r?$')
if(-not$fixture.Success){throw 'Retained private fixture identity missing.'}
[ordered]@{Passed=$true;Checks=57;Scenarios=31;InterceptedProcessRequests=33;BuildWarnings=$warnings;BuildErrors=$errors;BuildWarningScope='29 reviewed existing Level4/test-hook inclusion warnings; zero unexpected/newfeature warnings. Final production warning gate is separate.';WarningEvidence=$warningPath;WarningEvidenceSha256=(Get-FileHash -LiteralPath $warningPath).Hash;SourceSnapshot=$sourcePath;SourceSnapshotSha256=(Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash;SourceFencePassed=$true;Build=$build;Native=$native;Fixture=$fixture.Groups[1].Value;NativeWindows=0;NativeShows=0;NativeFocus=0;NativeForeground=0;ExternalOperations=0;ForeignPaint=0;ClipboardReads=0;Scope='Actual production settings menus and managed-profile/Firefox Temp launch paths on private fixtures. Process and clipboard/popup/presentation boundaries intercepted. Chromium Temp and Default extraction not repeated; no real browser or normal-review launch.'}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output 'Browser tab layout UI: 57 checks / 31 scenarios; exact source fence and zero native foreground/input/process side effects.'
