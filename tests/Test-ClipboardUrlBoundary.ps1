#requires -Version 7.0
param(
 [string]$Configuration='Release',
 [string]$Platform='x64',
 [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tests\ClipboardUrlBoundary')
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$artifacts=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not $artifacts.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){
 throw 'Clipboard boundary evidence must remain below the workspace build directory.'
}
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
# Compile exact current production bodies while replacing only OS clipboard APIs.
$sourcePath=Join-Path $root 'ctSpaces.cpp'
$source=[IO.File]::ReadAllText($sourcePath)
function Extract-ProductionFunction([string]$Start,[string]$End){
 $a=$source.IndexOf($Start,[StringComparison]::Ordinal)
 if($a-lt0){throw "Production function start was not found: $Start"}
 $b=$source.IndexOf($End,$a,[StringComparison]::Ordinal)
 if($b-lt0){throw "Production function end was not found: $End"}
 return $source.Substring($a,$b-$a).Trim()
}
$url=Extract-ProductionFunction 'static bool IsValidWebUrl(const std::wstring &url) {' 'static bool SetFirefoxStartupPreference('
$clipboard=Extract-ProductionFunction 'static std::optional<std::wstring> GetClipboardWebUrl() {' 'static bool OpenWebUrlForClient('
$generated=Join-Path $artifacts 'ClipboardUrlBoundary.generated.inl'
[IO.File]::WriteAllText($generated,$url+"`r`n`r`n"+$clipboard+"`r`n",[Text.UTF8Encoding]::new($false))
[pscustomobject]@{Source=$sourcePath;SourceSha256=(Get-FileHash -LiteralPath $sourcePath).Hash;Generated=$generated;GeneratedSha256=(Get-FileHash -LiteralPath $generated).Hash;Boundary='Only synthetic clipboard APIs; actual WinHttpCrackUrl parser, no network session.'}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $artifacts 'production-extraction.json')
$outputDirectory=Join-Path $artifacts "$Platform\$Configuration"
$intermediateDirectory=Join-Path $artifacts "obj\$Platform\$Configuration"
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
if(-not(Test-Path -LiteralPath $msbuild -PathType Leaf)){throw 'MSBuild was not found.'}
function Invoke-HiddenClipboardBoundaryTool([string]$Exe,[string[]]$NativeArguments,[string]$Log){
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
  $process.WaitForExit()
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
  if($process.ExitCode-ne 0){throw "Hidden clipboard boundary tool failed; inspect $Log"}
 }finally{$process.Dispose()}
}
Invoke-HiddenClipboardBoundaryTool $msbuild @(
 (Join-Path $PSScriptRoot 'ClipboardUrlBoundaryTests.vcxproj'),'/m:1','/nodeReuse:false',
 "/p:Configuration=$Configuration","/p:Platform=$Platform",'/p:BuildProjectReferences=false','/p:SnapshotDefines=',
 "/p:ClipboardFixtureDirectory=$artifacts",
 "/p:OutDir=$outputDirectory\","/p:IntDir=$intermediateDirectory\",'/v:minimal'
) (Join-Path $artifacts 'build.log')
# Snapshot definitions are explicitly cleared: routine Core execution tests current source.
Invoke-HiddenClipboardBoundaryTool (Join-Path $outputDirectory 'ClipboardUrlBoundaryTests.exe') @() (Join-Path $artifacts 'native.log')

