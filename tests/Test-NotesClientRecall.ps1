#requires -Version 7.0
param(
 [string]$Configuration='Release',
 [string]$Platform='x64',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\NotesClientRecall')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){
 throw 'Notes-client recall evidence must remain below the workspace build directory.'
}
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$outputDirectory=Join-Path $artifacts "$Platform\$Configuration"
$intermediateDirectory=Join-Path $artifacts "obj\$Platform\$Configuration"
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if(-not(Test-Path -LiteralPath $msbuild -PathType Leaf)){throw 'MSBuild was not found.'}
function Invoke-HiddenRecallTool([string]$Exe,[string[]]$NativeArguments,[string]$Log){
 $info=[Diagnostics.ProcessStartInfo]::new([IO.Path]::GetFullPath($Exe))
 $info.WorkingDirectory=$root
 $info.UseShellExecute=$false
 $info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true
 $info.RedirectStandardError=$true
 foreach($argument in $NativeArguments){$info.ArgumentList.Add($argument)}
 $started=[DateTime]::UtcNow
 $process=[Diagnostics.Process]::Start($info)
 try{
  try{if(-not $process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal}}
  catch [InvalidOperationException]{if(-not $process.HasExited){throw}}
  $stdout=$process.StandardOutput.ReadToEndAsync()
  $stderr=$process.StandardError.ReadToEndAsync()
  if(-not$process.WaitForExit(300000)){$process.Kill($true);throw ('Bounded hidden recall tool timed out: '+$Exe)}
  $contents=$stdout.Result+$stderr.Result
  [IO.File]::WriteAllText($Log,$contents)
  $receipt=[pscustomobject]@{
   Exe=$info.FileName;BinarySha256=(Get-FileHash -LiteralPath $info.FileName).Hash
   Arguments=$NativeArguments;ExitCode=$process.ExitCode
   Seconds=([DateTime]::UtcNow-$started).TotalSeconds
   Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash
  }
  $receipt|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($Log+'.json')
  Write-Output $contents
  if($process.ExitCode-ne 0){throw "Hidden Notes-client-recall tool failed; inspect $Log"}
 }finally{$process.Dispose()}
}
$sourcePaths=@(Get-ChildItem -LiteralPath $root -File|Where-Object Extension -in '.cpp','.h','.inl','.rc'|Select-Object -ExpandProperty FullName)
$sourcePaths+=@(Get-ChildItem -LiteralPath $PSScriptRoot -File|Where-Object {$_.Name-like'Notes*' -or $_.Name-eq'Test-NotesClientRecall.ps1'}|Select-Object -ExpandProperty FullName)
$snapshot=@($sourcePaths|Sort-Object -Unique|ForEach-Object {[pscustomobject]@{Path=$_;Hash=(Get-FileHash -LiteralPath $_).Hash}})
$snapshot|ConvertTo-Json|Set-Content (Join-Path $artifacts 'source-hashes.json')
function Assert-SourceUnchanged {
 foreach($record in $snapshot){if((Get-FileHash -LiteralPath $record.Path).Hash-ne$record.Hash){throw ('Notes-client recall source changed during verification: '+$record.Path)}}
}
Invoke-HiddenRecallTool $msbuild @(
 (Join-Path $PSScriptRoot 'NotesClientRecallTests.vcxproj'),'/m:1','/nodeReuse:false',
 "/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false','/p:NotesRedrawTestDefinitions=',
 "/p:OutDir=$outputDirectory\","/p:IntDir=$intermediateDirectory\",'/v:minimal'
) (Join-Path $artifacts 'build.log')
Assert-SourceUnchanged
# Explicitly empty snapshot definitions force current-source routine execution.
Invoke-HiddenRecallTool (Join-Path $outputDirectory 'NotesClientRecallTests.exe') @() (Join-Path $artifacts 'native.log')
Assert-SourceUnchanged
$native=Get-Content (Join-Path $artifacts 'native.log') -Raw
if($native-notmatch 'Notes client recall: 18 checks, 10 scenarios, 0 failures; actual input/foreground/clipboard=0') {throw 'Complete background Notes-client recall evidence was not recorded.'}
$match=[regex]::Match($native,'Background guards: (\{[^\r\n]+\})')
if(-not$match.Success){throw 'Background event audit is missing.'}
$guard=$match.Groups[1].Value|ConvertFrom-Json
foreach($field in 'forbiddenClipboard','forbiddenKeyboard','forbiddenRedraw','nativeVisibleEvents','nativeForegroundEvents','nativeFocusEvents'){
 if($null-eq$guard.$field -or $guard.$field-ne0){throw ('Background guard did not remain zero: '+$field)}
}
@{Version='6.3.3.0';Checks=18;Scenarios=10;SourceFencePassed=$true;NativeGuard=$guard;NativeLog=(Join-Path $artifacts 'native.log')}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $artifacts 'result.json')
