#requires -Version 7.0
param(
 [string]$Configuration='Release',
 [string]$Platform='x64',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\NotesFind')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not$artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Evidence must remain inside the workspace build directory.'}
New-Item -ItemType Directory -Force -Path $artifacts|Out-Null
$sourcePaths=@(Get-ChildItem -LiteralPath $root -File|Where-Object Extension -in '.cpp','.h','.inl','.rc'|Select-Object -ExpandProperty FullName)
$sourcePaths+=@(Get-ChildItem -LiteralPath $PSScriptRoot -File|Where-Object {$_.Name-like'Notes*' -or $_.Name-eq'Test-NotesFind.ps1'}|Select-Object -ExpandProperty FullName)
$snapshot=@($sourcePaths|Sort-Object -Unique|ForEach-Object{Get-FileHash -LiteralPath $_})
$snapshot|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
function Fence {
 foreach($item in $snapshot){if((Get-FileHash -LiteralPath $item.Path).Hash-ne$item.Hash){throw ('Find test inputs changed during verification: '+$item.Path)}}
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
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$bin=(Join-Path $artifacts 'bin')+'\';$obj=(Join-Path $artifacts 'obj')+'\'
$build=Hidden $msbuild @((Join-Path $PSScriptRoot 'NotesFindTests.vcxproj'),'/m:1','/nodeReuse:false',"/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false','/p:NotesRedrawTestDefinitions=',('/p:OutDir='+$bin),('/p:IntDir='+$obj),'/v:minimal') (Join-Path $artifacts 'build.log') 300000
Fence
$native=Hidden (Join-Path $bin 'NotesFindTests.exe') @('--headless-background') (Join-Path $artifacts 'native.log') 90000
Fence
$text=[IO.File]::ReadAllText($native.Log)
$summary=[regex]::Match($text,'Notes Find and Replace: (\d+) checks; (\d+) scenarios; 0 failures');if(-not$summary.Success-or[int]$summary.Groups[1].Value-lt422-or[int]$summary.Groups[2].Value-ne8){throw 'The complete search, replacement, transaction and native-edit accessibility cases did not finish.'}
$productionCases=@([regex]::Matches($text,'(?m)^Rollback edge probe: method=production-model-recovery;[^\r\n]+; supported=1\r?$'))
if($productionCases.Count-ne10-or$text-notmatch'Actual Model recovery matrix: 10 native topology/limit cases plus empty-transaction/terminal-ownership guards completed'-or$text-notmatch'PASS A throwing pre-edit callback with no mutation leaves exact raw RTF'-or$text-notmatch'PASS An unowned throwing mutation is not mistaken for a restored transaction'-or$text-notmatch'Prospective save-budget proof:'){
 throw 'Default Find verification must complete all ten actual Model/recovery cases, legitimate Redo/terminal ownership guards, and prospective save/reopen regressions.'
}
$match=[regex]::Match($text,'Background guards: (\{[^\r\n]+\})')
if(-not$match.Success){throw 'Background event audit is missing.'}
$guard=$match.Groups[1].Value|ConvertFrom-Json
foreach($field in 'forbiddenClipboard','forbiddenKeyboard','forbiddenRedraw','nativeVisibleEvents','nativeForegroundEvents','nativeFocusEvents'){
 if($null-eq$guard.$field -or $guard.$field-ne0){throw ('Background guard did not remain zero: '+$field)}
}
@{Version='6.3.3.0';Checks=[int]$summary.Groups[1].Value;Scenarios=[int]$summary.Groups[2].Value;Build=$build;Native=$native;SourceFencePassed=$true;NativeGuard=$guard}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output ('Notes Find and Replace: '+$summary.Groups[1].Value+' checks, '+$summary.Groups[2].Value+' scenarios, 0 failures; actual visible/focus/foreground/clipboard/input=0.')
