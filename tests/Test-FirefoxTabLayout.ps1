param([string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\tab-layout-2026-10-21\firefox-targeted'))
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$out=[IO.Path]::GetFullPath($ArtifactDirectory)
if(-not$out.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){throw 'Evidence directory must be below workspace build.'}
if(Test-Path -LiteralPath (Join-Path $out 'result.json')){throw 'Existing completed evidence must not be overwritten.'}
New-Item -ItemType Directory -Force -Path $out|Out-Null
$files=@('FirefoxPreferences.h','ConfigPersistence.cpp','ConfigPersistence.h','SiblingStageName.h','tests\RetainedFixture.h','tests\FirefoxTabLayoutTests.cpp','tests\FirefoxTabLayoutTests.vcxproj','tests\Test-FirefoxTabLayout.ps1')
$snapshot=@($files|ForEach-Object{$p=Join-Path $root $_;[ordered]@{Path=$p;Sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash}})
$snapshot|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $out 'source-frozen.json')
function Assert-Frozen {foreach($row in $snapshot){if((Get-FileHash -LiteralPath $row.Path -Algorithm SHA256).Hash-ne$row.Sha256){throw ('Frozen Firefox tab-layout input changed: '+$row.Path)}}}
function Invoke-Hidden([string]$Exe,[string[]]$Arguments,[string]$Name,[int]$Timeout){
 $info=[Diagnostics.ProcessStartInfo]::new($Exe);$info.WorkingDirectory=$root;$info.CreateNoWindow=$true;$info.UseShellExecute=$false;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($argument in $Arguments){$info.ArgumentList.Add($argument)}
 $p=[Diagnostics.Process]::Start($info)
 try{
  try{if(-not$p.HasExited){$p.PriorityClass='BelowNormal'}}catch [InvalidOperationException]{if(-not$p.HasExited){throw}}
  $stdout=$p.StandardOutput.ReadToEndAsync();$stderr=$p.StandardError.ReadToEndAsync()
  if(-not$p.WaitForExit($Timeout)){$p.Kill($true);$p.WaitForExit();throw ('Owned bounded tool timed out: '+$Name)}
  $text=$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult();$log=Join-Path $out ($Name+'.log');[IO.File]::WriteAllText($log,$text)
  $record=[ordered]@{Exe=$Exe;Sha256=(Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash;Arguments=$Arguments;ExitCode=$p.ExitCode;CreateNoWindow=$true;Priority='BelowNormal';Log=$log;LogSha256=(Get-FileHash -LiteralPath $log -Algorithm SHA256).Hash}
  $record|ConvertTo-Json -Depth 6|Set-Content -LiteralPath ($log+'.json')
  return [pscustomobject]@{Record=$record;Text=$text}
 }finally{$p.Dispose()}
}
Assert-Frozen
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$build=Invoke-Hidden $msbuild @((Join-Path $PSScriptRoot 'FirefoxTabLayoutTests.vcxproj'),'/m:1','/nodeReuse:false','/p:Configuration=Release','/p:Platform=x64','/p:BuildProjectReferences=false',('/p:OutDir='+(Join-Path $out 'bin')+'\'),('/p:IntDir='+(Join-Path $out 'obj')+'\'),'/v:minimal') 'build' 600000
Assert-Frozen
$native=$null;$matches=$null
if($build.Record.ExitCode-eq0){
 $native=Invoke-Hidden (Join-Path $out 'bin\FirefoxTabLayoutTests.exe') @((Join-Path $out 'fixtures')) 'native' 120000
 Assert-Frozen
 $matches=[regex]::Match($native.Text,'(?m)^Firefox tab layout: (\d+) checks, (\d+) retained private scenarios, 0 failures; file-only, no browser or UI launch\.\r?$')
}
$complete=$build.Record.ExitCode-eq0-and$null-ne$native-and$native.Record.ExitCode-eq0-and$matches.Success-and[int]$matches.Groups[1].Value-ge330-and[int]$matches.Groups[2].Value-eq33-and$native.Text.Contains('Firefox tab-layout guards: visible=0; show=0; focus=0; foreground=0.')
$result=[ordered]@{Status=if($complete){'Passed'}else{'Failed'};Build=$build.Record;Native=if($native){$native.Record}else{$null};Checks=if($matches.Success){[int]$matches.Groups[1].Value}else{0};Scenarios=if($matches.Success){[int]$matches.Groups[2].Value}else{0};SourceFencePassed=$true;SourceInputs=$snapshot.Count;SourceManifest=(Join-Path $out 'source-frozen.json');SourceManifestSha256=(Get-FileHash -LiteralPath (Join-Path $out 'source-frozen.json') -Algorithm SHA256).Hash;Scope='Managed user.js block and atomic file operations in retained isolated fixture profiles only. No real browser, app entrypoint, clipboard or foreground input is used. Own-PID WinEvent observer and visible-window check must remain zero.';CompletedUtc=[DateTime]::UtcNow.ToString('o')}
$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $out 'result.json')
if(-not$complete){throw ('Firefox tab-layout targeted proof failed; retained '+$out)}
Write-Output $native.Text
Write-Output ('Firefox tab-layout source fence passed; inputs='+$snapshot.Count)
