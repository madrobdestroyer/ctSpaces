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
$inputs+=@('BrowserTabLayoutUiTests.cpp','BrowserTabLayoutUiTests.vcxproj','RetainedFixture.h')|ForEach-Object{Join-Path $PSScriptRoot $_}
$inputs+=$PSCommandPath
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
# zero-warning release gate. Match the reviewed sites and messages by their exact
# five-line source excerpts; unrelated insertions can move a site without
# weakening rejection of a new warning or changed context. Line remains useful
# in the diagnostic evidence, but is not the identity of an unchanged site.
$reviewedWarnings=@'
[{"Path":"ctSpaces.cpp","Line":2861,"Code":"C4189","Message":"'bDark': local variable is initialized but not referenced","SiteSha256":"F38B8726EA1894835F6F7EFCF1468617440007FD4B41B9EC0B2E3783F161B27D"},{"Path":"ctSpaces.cpp","Line":3692,"Code":"C4389","Message":"'==': signed/unsigned mismatch","SiteSha256":"7F24CF25977A141B05A800235BF9D51AE7788A57A63B5534C97F12D145202CD3"},{"Path":"ctSpaces.cpp","Line":3678,"Code":"C4100","Message":"'op': unreferenced parameter","SiteSha256":"2C0E033992480E6CDDAB53D6A43A58DDFFB12EEE4776C8C15BFD130E8FC6991E"},{"Path":"ctSpaces.cpp","Line":5980,"Code":"C4457","Message":"declaration of 'message' hides function parameter","SiteSha256":"CFF105EF3DE37CF9A0B1579A1A918D013201CC1AE616030FF968239E42C250F5"},{"Path":"ctSpaces.cpp","Line":5989,"Code":"C4457","Message":"declaration of 'message' hides function parameter","SiteSha256":"6A1B16EA84FFA7378F7B8AF30F84213A122C77C984D642F283AA5E038B1FD7AB"},{"Path":"ctSpaces.cpp","Line":13405,"Code":"C4389","Message":"'!=': signed/unsigned mismatch","SiteSha256":"035536314B314E831FEB409659B72BA2A7CA885CDBA76FF9BE2FE57796A270AB"},{"Path":"ctSpaces.cpp","Line":13409,"Code":"C4389","Message":"'!=': signed/unsigned mismatch","SiteSha256":"B53C03ACB4454A0D7EB74CF9F87DF3331887A82FE89FFD1BDB3257B340F81B47"},{"Path":"ctSpaces.cpp","Line":18732,"Code":"C4457","Message":"declaration of 'id' hides function parameter","SiteSha256":"A6DB45B7B16A293E432F37FF6CB1FD0C2B1EAAB42F1C4BCEF8C74FA41CB8885A"},{"Path":"ctSpaces.cpp","Line":19383,"Code":"C4456","Message":"declaration of 'bounds' hides previous local declaration","SiteSha256":"5D3C4B2854C5C103AB6B8EFB490F0B06B91E0AA79A32E8B545D6B22E1CE27B5F"},{"Path":"ctSpaces.cpp","Line":20275,"Code":"C4456","Message":"declaration of 'dialog' hides previous local declaration","SiteSha256":"4A7380E833B10A9A98481226157FAD60791C2014758828AEA6CA87AD3902FC36"},{"Path":"ctSpaces.cpp","Line":20374,"Code":"C4456","Message":"declaration of 'target' hides previous local declaration","SiteSha256":"BD86A4F9F606AD721BCD8BC2473CABFEBC866D17980365911F8BA95806F9C321"},{"Path":"NotesWorkspaceUi.inl","Line":109,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch","SiteSha256":"410BE9DAD1C10C88C82A82AC0582E7E8E046A66DCD3BEAB3B298448D326113F7"},{"Path":"NotesWorkspaceUi.inl","Line":312,"Code":"C4100","Message":"'dialog': unreferenced parameter","SiteSha256":"2013C0AF25E70397E5733AE49DD023B152352BAB2D90D35F8192094C454FE886"},{"Path":"NotesProductivityUi.inl","Line":28,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch","SiteSha256":"B96CF3CC9865D24E23C05114A4B8F16D76FAAECB39995E20E3E37A7DD4445242"},{"Path":"NotesCodeUi.inl","Line":17,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch","SiteSha256":"4F9EAA81C30F2FBEBEF8EABCBD661D1C9E90D2889AA4A8CEC7120A0966DFE774"},{"Path":"NotesSpellingUi.inl","Line":135,"Code":"C4456","Message":"declaration of 'current' hides previous local declaration","SiteSha256":"CDC7308FA78E37DC1A0F45A6C0FF91F0B7E413EEA19E60DF93DE2A1FD74B87BA"},{"Path":"NotesWorkspaceUi.inl","Line":670,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch","SiteSha256":"32234A22BF3C6CB9411E24C977169C3E7B3B5D21BDA820564B27BA8F26717292"},{"Path":"NotesWorkspaceUi.inl","Line":687,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch","SiteSha256":"5FEB9FD490DA069D1BF27EE40B6E63BF3A99E8BC894756AE1184831B98B25611"},{"Path":"NotesWorkspaceUi.inl","Line":688,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch","SiteSha256":"858DEAD78ABC34609A867482AD4CBF0709E5CD47AFC6EE78F5111BE179761DE0"},{"Path":"NotesTemplatesUi.inl","Line":247,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch","SiteSha256":"AD1DF47AAD6A1AECCDFC9D86E95401433F4C4F44CDAD3151497A76BF3DFC3A14"},{"Path":"NotesWorkspaceUi.inl","Line":1308,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch","SiteSha256":"CD23F2384D353418F6D8732326317DC4E8840076C8307C822F9E45FB72C8A6B5"},{"Path":"NotesInspectorUi.inl","Line":266,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch","SiteSha256":"1788AC6607053AF514B69EEA2B14EF98F3295F1EAC5601579F0AB5A624A57C53"},{"Path":"NotesInspectorUi.inl","Line":287,"Code":"C4245","Message":"'argument': conversion from 'int' to 'WPARAM', signed/unsigned mismatch","SiteSha256":"8A5ABFA3A2C87E3C995D766D92BC05342A55EAC2FB05C3F74A0AF63C5A761820"},{"Path":"ctSpaces.cpp","Line":25106,"Code":"C4100","Message":"'hWnd': unreferenced parameter","SiteSha256":"33EF703A7FF12F3A409CE161EFC902F8694F0F3CB82B4245774E6415F442065A"},{"Path":"ctSpaces.cpp","Line":21315,"Code":"C4702","Message":"unreachable code","SiteSha256":"B1F121AB2FE0ADF46AA9B70DB83A25BD0FCD4A51B8FF2B41080511E3C067EDC0"},{"Path":"ctSpaces.cpp","Line":21316,"Code":"C4702","Message":"unreachable code","SiteSha256":"8821B5BF5958503A65AE60F7C02C53C5EEFC28A9F50009EE0000C986EEB445FC"},{"Path":"ctSpaces.cpp","Line":21317,"Code":"C4702","Message":"unreachable code","SiteSha256":"F2826DBE065F02DD38517994A40718BAA2E14AB18D22132B90444A47B0DDEEB1"},{"Path":"ctSpaces.cpp","Line":21323,"Code":"C4702","Message":"unreachable code","SiteSha256":"B53ED3CBAB6FB55AFA17D1D986D8F2701C09C4C8910A6CA4ACDBA3DA93E806D2"},{"Path":"ctSpaces.cpp","Line":21324,"Code":"C4702","Message":"unreachable code","SiteSha256":"F0081C275ADEB91BCB3DBDF87B725D2DAC1AABCA4EE64728E867E8BC3E8BBB0B"},{"Path":"NotesAccessibility.h","Line":43,"Code":"C4245","Message":"'argument': conversion from 'LONG' to 'DWORD', signed/unsigned mismatch","SiteSha256":"C2CCFBE22369761B52D17519EC73BCFF26534633209F18F8FF34D3F82CF3DCC1"},{"Path":"NotesAccessibility.h","Line":56,"Code":"C4245","Message":"'argument': conversion from 'LONG' to 'DWORD', signed/unsigned mismatch","SiteSha256":"C4B292B098FA18CB8C166A0F4B31146CCA38A97DDA46F3C954FB8A1FD89D1958"},{"Path":"NotesWorkspaceUi.inl","Line":1552,"Code":"C4456","Message":"declaration of 'edit' hides previous local declaration","SiteSha256":"6B941882180A8226143E2DB17C98489B66A1F50905D607F06BBB4D3D52F53561"}]
'@|ConvertFrom-Json
$observedWarnings=@([regex]::Matches($buildText,'(?m)^([^\r\n]+)\((\d+)(?:,\d+)?\): warning (C\d+): ([^\r\n]+?) \[.*?\]\r?$')|ForEach-Object{[ordered]@{Path=[IO.Path]::GetRelativePath($root,$_.Groups[1].Value);Line=[int]$_.Groups[2].Value;Code=$_.Groups[3].Value;Message=$_.Groups[4].Value}})
$warningSourceLines=@{}
function WarningSiteHash($Row){
 if(-not $warningSourceLines.ContainsKey($Row.Path)){$warningSourceLines[$Row.Path]=[IO.File]::ReadAllLines((Join-Path $root $Row.Path))}
 $lines=$warningSourceLines[$Row.Path]
 if($Row.Line -lt 1 -or $Row.Line -gt $lines.Length){throw 'A warning has an invalid source line.'}
 $first=[Math]::Max(0,$Row.Line-3);$last=[Math]::Min($lines.Length-1,$Row.Line+1)
 $site=$lines[$first..$last]-join "`n"
 return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($site)))
}
foreach($row in $observedWarnings){$row.SiteSha256=WarningSiteHash $row}
function WarningKey($Row){'{0}|{1}|{2}|{3}'-f$Row.Path,$Row.SiteSha256,$Row.Code,$Row.Message}
$expectedKeys=@($reviewedWarnings|ForEach-Object{WarningKey $_}|Sort-Object)
$actualKeys=@($observedWarnings|ForEach-Object{WarningKey $_}|Sort-Object)
if($errors-or$warnings-ne$reviewedWarnings.Count-or$observedWarnings.Count-ne$warnings-or(($expectedKeys-join"`n")-cne($actualKeys-join"`n"))){throw "Unexpected native inclusion diagnostic: $warnings warnings and $errors errors; compare reviewed exact baseline."}
$warningPath=Join-Path $artifacts 'native-inclusion-warnings.json'
$observedWarnings|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $warningPath
$native=Hidden (Join-Path $artifacts 'bin\BrowserTabLayoutUiTests.exe') @() (Join-Path $artifacts 'native.log') 180000
Fence 'after native run'
$text=[IO.File]::ReadAllText($native.Log)
$summary=[regex]::Match($text,'(?m)^Browser tab layout UI: (\d+) checks, (\d+) scenarios, 0 failures; native windows/show/focus/foreground=0/0/0/0; intercepted process requests=(\d+); external=0; foreign paint=0; clipboard reads=0\.\r?$')
if(-not$summary.Success-or[int]$summary.Groups[1].Value-ne57-or[int]$summary.Groups[2].Value-ne31-or[int]$summary.Groups[3].Value-ne33){throw 'Complete actual menu/launch coverage or zero native event guards missing.'}
$fixture=[regex]::Match($text,'(?m)^PRIVATE_FIXTURE (.+?)\r?$')
if(-not$fixture.Success){throw 'Retained private fixture identity missing.'}
[ordered]@{Passed=$true;Checks=57;Scenarios=31;InterceptedProcessRequests=33;BuildWarnings=$warnings;BuildErrors=$errors;BuildWarningScope='32 reviewed Level4 and test-hook inclusion warnings. No unexpected warnings. Final production warning gate is separate.';WarningEvidence=$warningPath;WarningEvidenceSha256=(Get-FileHash -LiteralPath $warningPath).Hash;SourceSnapshot=$sourcePath;SourceSnapshotSha256=(Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash;SourceFencePassed=$true;Build=$build;Native=$native;Fixture=$fixture.Groups[1].Value;NativeWindows=0;NativeShows=0;NativeFocus=0;NativeForeground=0;ExternalOperations=0;ForeignPaint=0;ClipboardReads=0;Scope='Actual production settings menus and managed-profile/Firefox Temp launch paths on private fixtures. Process and clipboard/popup/presentation boundaries intercepted. Chromium Temp and Default extraction not repeated; no real browser or normal-review launch.'}|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output 'Browser tab layout UI: 57 checks / 31 scenarios; exact source fence and zero native foreground/input/process side effects.'
