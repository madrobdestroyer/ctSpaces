#requires -Version 7.0
param(
 [Parameter(Mandatory)][ValidateSet('ClientIsolation','ClientRenameUnicode','ClientIdentityUnicode')][string]$Suite,
 [Parameter(Mandatory)][string]$ArtifactDirectory,
 [switch]$CompileOnly
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not$out.StartsWith($root+'\build\',[StringComparison]::OrdinalIgnoreCase)){throw 'Background client artifacts must remain below workspace build.'}
New-Item -ItemType Directory -Force -Path $out|Out-Null
$generated=Join-Path $out 'source';New-Item -ItemType Directory -Force -Path $generated|Out-Null
if($Suite-eq'ClientIdentityUnicode'){
 $source=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'ClientIdentityUnicodeTests.cpp'))
 if(([regex]::Matches($source,'int wmain\(\) \{')).Count-ne1){throw 'Identity test entry boundary changed; background variant refuses to guess.'}
 $source=$source.Replace('#include "ClientRenameUnicodeTests.cpp"',('#include "'+(Join-Path $PSScriptRoot 'ClientRenameUnicodeTests.cpp').Replace('\','/')+'"')).Replace('int wmain() {','int LegacyClientMain() {')
 [IO.File]::WriteAllText((Join-Path $generated 'IdentityBackgroundEntry.inl'),$source)
}
$define=switch($Suite){
 'ClientIsolation'{'CTSPACES_BACKGROUND_CLIENT_ISOLATION'}
 'ClientRenameUnicode'{'CTSPACES_BACKGROUND_CLIENT_RENAME_UNICODE'}
 'ClientIdentityUnicode'{'CTSPACES_BACKGROUND_CLIENT_IDENTITY_UNICODE'}
 default{throw 'Unsupported background client suite.'}
}
$sourceFiles=@(Get-ChildItem -LiteralPath $root -File|Where-Object Extension -in '.cpp','.h','.inl','.rc')
$sourceFiles+=@('LegacyClientBackgroundTests.cpp','LegacyClientBackgroundTests.vcxproj','Test-LegacyClientBackground.ps1','ClientIsolationTests.cpp','ClientRenameUnicodeTests.cpp','ClientIdentityUnicodeTests.cpp','DirectoryJunctionFixture.h'|ForEach-Object{Get-Item -LiteralPath (Join-Path $PSScriptRoot $_)})
$snapshot=@($sourceFiles|ForEach-Object{[pscustomobject]@{Path=$_.FullName;Sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
$snapshot|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $out 'source-hashes.json')
function Assert-BackgroundSourceFreeze{
 foreach($item in $snapshot){if((Get-FileHash -LiteralPath $item.Path).Hash-ne$item.Sha256){throw ('Source changed while verifying background client suite: '+$item.Path)}}
}
function Invoke-BackgroundClientTool([string]$Exe,[string[]]$Arguments,[string]$Log,[int]$TimeoutMilliseconds=600000){
 $info=[Diagnostics.ProcessStartInfo]::new($Exe);$info.WorkingDirectory=$root;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 $started=[DateTime]::UtcNow;$process=[Diagnostics.Process]::Start($info)
 try{
  try{if(-not$process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal}}catch [InvalidOperationException]{if(-not$process.HasExited){throw}}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  if(-not$process.WaitForExit($TimeoutMilliseconds)){$process.Kill($true);throw 'Background client tool exceeded its bounded timeout.'}
  $text=$stdout.Result+$stderr.Result;[IO.File]::WriteAllText($Log,$text)
  [pscustomobject]@{Exe=$Exe;Sha256=(Get-FileHash -LiteralPath $Exe).Hash;Arguments=$Arguments;ExitCode=$process.ExitCode;Seconds=([DateTime]::UtcNow-$started).TotalSeconds;CreateNoWindow=$true;Log=$Log;LogSha256=(Get-FileHash -LiteralPath $Log).Hash}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($Log+'.json')
  if($process.ExitCode-ne0){throw ('Background client tool failed; inspect '+$Log)}
  return $text
 }finally{$process.Dispose()}
}
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$bin=Join-Path $out 'bin';$obj=Join-Path $out 'obj'
$null=Invoke-BackgroundClientTool $msbuild @((Join-Path $PSScriptRoot 'LegacyClientBackgroundTests.vcxproj'),'/m:1','/nodeReuse:false','/p:Configuration=Release','/p:Platform=x64','/p:BuildProjectReferences=false',('/p:LegacyClientDefines='+$define),('/p:LegacyGeneratedDirectory='+$generated),('/p:OutDir='+$bin+'\'),('/p:IntDir='+$obj+'\'),'/v:minimal') (Join-Path $out 'build.log')
Assert-BackgroundSourceFreeze
if($CompileOnly){Write-Output ('Compiled background client guard variant without execution: '+$Suite);return}
$native=Invoke-BackgroundClientTool (Join-Path $bin 'LegacyClientBackgroundTests.exe') @() (Join-Path $out 'native.log') 60000
Assert-BackgroundSourceFreeze
if($native-notmatch 'Legacy client background audit: shows=0 focus=0 foreground=0 visible=0 launch=0 clipboard=0; presentation requests suppressed=\d+') {throw 'Native completion did not confirm the background safety boundary.'}
Write-Output $native
