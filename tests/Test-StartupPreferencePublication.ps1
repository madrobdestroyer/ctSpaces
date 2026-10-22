#requires -Version 7.0
param(
 [string]$Configuration='Release',
 [string]$Platform='x64',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\StartupPreferencePublication')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not$artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Startup preference evidence must remain below the workspace build directory.'}
New-Item -ItemType Directory -Force -Path $artifacts|Out-Null
$output=Join-Path $artifacts 'bin'
$intermediate=Join-Path $artifacts 'obj'
$sourcePaths=@('ConfigPersistence.cpp','ConfigPersistence.h','BrowserPreferencesJson.cpp','BrowserPreferencesJson.h','FirefoxPreferences.h','tests\StartupPreferencePublicationTests.cpp','tests\StartupPreferencePublicationTests.vcxproj','tests\Test-StartupPreferencePublication.ps1')
$snapshot=@($sourcePaths|ForEach-Object{$path=Join-Path $root $_;[pscustomobject]@{Path=$path;Hash=(Get-FileHash -LiteralPath $path).Hash}})
$snapshot|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $artifacts 'source-hashes.json')
function Assert-SourceUnchanged {foreach($record in $snapshot){if((Get-FileHash -LiteralPath $record.Path).Hash-ne$record.Hash){throw 'Startup preference publication source changed during verification.'}}}
function Invoke-HiddenPreferenceTool([string]$Exe,[string[]]$NativeArguments,[string]$Log){
 $info=[Diagnostics.ProcessStartInfo]::new($Exe)
 $info.WorkingDirectory=$root;$info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $NativeArguments){$info.ArgumentList.Add($argument)}
 $process=[Diagnostics.Process]::Start($info)
 try{
  try{if(-not$process.HasExited){$process.PriorityClass='BelowNormal'}}catch [InvalidOperationException]{}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  $process.WaitForExit();$contents=$stdout.Result+$stderr.Result
  [IO.File]::WriteAllText($Log,$contents)
  [pscustomobject]@{Exe=$Exe;BinarySha256=(Get-FileHash -LiteralPath $Exe).Hash;ExitCode=$process.ExitCode;Arguments=$NativeArguments;Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath ($Log+'.json')
  if($process.ExitCode-ne0){throw "Hidden startup preference tool failed; inspect $Log"}
  return $contents
 }finally{$process.Dispose()}
}
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$null=Invoke-HiddenPreferenceTool $msbuild @((Join-Path $PSScriptRoot 'StartupPreferencePublicationTests.vcxproj'),'/m:1','/nodeReuse:false',"/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false',"/p:OutDir=$output\","/p:IntDir=$intermediate\",'/v:minimal') (Join-Path $artifacts 'build.log')
Assert-SourceUnchanged
$native=Invoke-HiddenPreferenceTool (Join-Path $output 'StartupPreferencePublicationTests.exe') @((Join-Path $artifacts 'fixtures')) (Join-Path $artifacts 'native.log')
Assert-SourceUnchanged
if($native-notmatch 'Startup preference publication: 107 checks, 20 adapter scenarios, 0 failures; actual HWND/input/foreground/clipboard/browser/network=0\.') {throw 'The complete private native preference publication scope was not confirmed.'}
Write-Output $native
