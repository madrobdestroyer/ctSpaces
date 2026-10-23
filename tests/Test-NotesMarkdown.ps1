#requires -Version 7.0
param(
 [ValidateSet('Core','Native','All')][string]$Mode='All',
 [string]$Configuration='Release',
 [string]$Platform='x64',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\NotesMarkdown')
)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $projectRoot 'build\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Markdown evidence must remain inside the project build directory.'}
New-Item -ItemType Directory -Force -Path $artifacts|Out-Null
$sourcePaths=@(Get-ChildItem -LiteralPath $projectRoot -File|Where-Object Extension -in '.cpp','.h','.inl','.rc'|Select-Object -ExpandProperty FullName)
$sourcePaths+=@(Get-ChildItem -LiteralPath $PSScriptRoot -File|Where-Object {$_.Name -like 'Notes*' -or $_.Name -eq 'Test-NotesMarkdown.ps1'}|Select-Object -ExpandProperty FullName)
$snapshot=@($sourcePaths|Sort-Object -Unique|ForEach-Object{Get-FileHash -LiteralPath $_})
$snapshot|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'source-before.json')
function Fence {
 foreach($inputFile in $snapshot){if((Get-FileHash -LiteralPath $inputFile.Path).Hash -ne $inputFile.Hash){throw ('Markdown source changed during verification: '+$inputFile.Path)}}
}
function Hidden([string]$Exe,[string[]]$Arguments,[string]$Log,[int]$Timeout) {
 $info=[Diagnostics.ProcessStartInfo]::new($Exe);$info.WorkingDirectory=$projectRoot;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 $started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($info)
 try {
  try{if(-not $process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal}}catch [InvalidOperationException]{if(-not $process.HasExited){throw}}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  if(-not $process.WaitForExit($Timeout)){$process.Kill($true);throw ('Bounded Markdown tool timed out: '+$Exe)}
  [IO.File]::WriteAllText($Log,$stdout.Result+$stderr.Result)
  $receipt=[pscustomobject]@{Exe=$Exe;Sha256=(Get-FileHash -LiteralPath $Exe).Hash;Arguments=$Arguments;ExitCode=$process.ExitCode;Seconds=([DateTime]::UtcNow-$started).TotalSeconds;CreateNoWindow=$true;Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash}
  $receipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($Log+'.json')
  if($receipt.ExitCode -ne 0){throw ('Markdown tool failed: '+$Log)}
  return $receipt
 }finally{$process.Dispose()}
}
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$results=@()
foreach($lane in $(if($Mode -eq 'All'){@('Core','Native')}else{@($Mode)})) {
 $output=Join-Path $artifacts $lane;New-Item -ItemType Directory -Force -Path $output|Out-Null
 $bin=(Join-Path $output 'bin')+'\';$obj=(Join-Path $output 'obj')+'\'
 $project=if($lane -eq 'Core'){'NotesMarkdownCoreTests'}else{'NotesMarkdownTests'}
 $build=Hidden $msbuild @((Join-Path $PSScriptRoot ($project+'.vcxproj')),'/m:1','/nodeReuse:false',"/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false','/p:NotesRedrawTestDefinitions=',('/p:OutDir='+$bin),('/p:IntDir='+$obj),'/v:minimal') (Join-Path $output 'build.log') 300000
 Fence
 $arguments=if($lane -eq 'Core'){@()}else{@('--headless-background',$output)}
 $native=Hidden (Join-Path $bin ($project+'.exe')) $arguments (Join-Path $output 'native.log') 90000
 Fence
 $contents=[IO.File]::ReadAllText($native.Log);$guard=$null
 if($lane -eq 'Core') {
  if($contents -notmatch 'Markdown core: 58 checks; 1 groups; 0 failures; windows/input/clipboard/browser=0\.'){throw 'The complete pure Markdown core scenarios did not finish.'}
  $count=58;$groups=1
 } else {
  if($contents -notmatch 'Markdown interchange: 222 checks; 8 private groups; 0 failures\.'){throw 'The complete native Markdown and table scenarios did not finish.'}
  $count=222;$groups=8
  $match=[regex]::Match($contents,'Background guards: (\{[^\r\n]+\})');if(-not $match.Success){throw 'Native Markdown safety receipt is missing.'}
  $guard=$match.Groups[1].Value|ConvertFrom-Json
  foreach($field in 'forbiddenClipboard','forbiddenKeyboard','forbiddenRedraw','nativeVisibleEvents','nativeForegroundEvents','nativeFocusEvents') {
   if($null -eq $guard.$field -or $guard.$field -ne 0){throw ('Native Markdown background boundary crossed: '+$field)}
  }
 }
 $results+=[pscustomobject]@{Lane=$lane;Checks=$count;Groups=$groups;Build=$build;Native=$native;Guard=$guard}
}
@{Mode=$Mode;SourceFencePassed=$true;Lanes=$results}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
Write-Output ('Markdown '+$Mode+': '+(($results|Measure-Object Checks -Sum).Sum)+' checks passed; actual foreground/input/clipboard=0.')
