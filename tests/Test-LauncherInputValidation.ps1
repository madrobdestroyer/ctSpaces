param([Alias('ArtifactDirectory')][string]$EvidenceDirectory='')
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $EvidenceDirectory){$EvidenceDirectory=Join-Path $root 'build\app-quality-2026-10-19\clients\regression'}
$EvidenceDirectory=[IO.Path]::GetFullPath($EvidenceDirectory)
if(-not $EvidenceDirectory.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)){
  throw 'Launcher validation evidence must remain below the workspace build directory.'
}
[IO.Directory]::CreateDirectory($EvidenceDirectory)|Out-Null
function Run-Hidden([string]$Program,[string[]]$Arguments,[string]$Label,[int]$TimeoutSeconds){
  $start=[Diagnostics.ProcessStartInfo]::new($Program)
  $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.WorkingDirectory=$root
  $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
  foreach($argument in $Arguments){$start.ArgumentList.Add($argument)}
  $process=[Diagnostics.Process]::Start($start)
  try{$process.PriorityClass='BelowNormal'}catch{}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  if(-not $process.WaitForExit($TimeoutSeconds*1000)){$process.Kill($true);throw "$Label timed out."}
  [IO.File]::WriteAllText((Join-Path $EvidenceDirectory "$Label.log"),$stdout.GetAwaiter().GetResult()+$stderr.GetAwaiter().GetResult())
  if($process.ExitCode-ne 0){throw "$Label failed with exit code $($process.ExitCode). See $EvidenceDirectory\$Label.log"}
  $process.Dispose()
}
$sourcePaths=@(Get-ChildItem -LiteralPath $root -File|Where-Object Extension -in '.cpp','.h','.inl','.rc','.vcxproj'|Select-Object -ExpandProperty FullName)
$sourcePaths+=@('LauncherInputValidationTests.cpp','LauncherInputValidationTests.vcxproj','Test-LauncherInputValidation.ps1'|ForEach-Object {Join-Path $PSScriptRoot $_})
$snapshot=@($sourcePaths|Sort-Object -Unique|ForEach-Object {[pscustomobject]@{Path=$_;Hash=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}})
$snapshot|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'source-hashes.json') -Encoding utf8
function Assert-SourceUnchanged {
  foreach($record in $snapshot){if((Get-FileHash -LiteralPath $record.Path -Algorithm SHA256).Hash-ne $record.Hash){throw ('Launcher validation source changed during verification: '+$record.Path)}}
}
$msbuild='C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
Run-Hidden $msbuild @((Join-Path $PSScriptRoot 'LauncherInputValidationTests.vcxproj'),'/m:1','/nodeReuse:false','/p:Configuration=Release','/p:Platform=x64','/v:minimal') 'build' 600
Assert-SourceUnchanged
$executable=Join-Path $root 'build\tests\LauncherInputValidation\x64\Release\LauncherInputValidationTests.exe'
$sourceHash=($snapshot|Where-Object Path -eq (Join-Path $root 'ctSpaces.cpp')).Hash
Run-Hidden $executable @() 'native' 90
Assert-SourceUnchanged
$native=Get-Content -LiteralPath (Join-Path $EvidenceDirectory 'native.log') -Raw
if($native-notmatch 'Native launcher validation: 15 scenarios; 139 checks; 0 failures; native changes=\d+; suppressed popups=\d+; suppressed presentation=\d+'){
  throw 'Complete launcher input validation coverage was not recorded.'
}
$match=[regex]::Match($native,'Launcher background guards: (\{[^\r\n]+\})')
if(-not $match.Success){throw 'Launcher native background guard evidence is missing.'}
$guard=$match.Groups[1].Value|ConvertFrom-Json
foreach($field in 'visibleOwnedWindows','nativeShowEvents','nativeFocusEvents','nativeForegroundEvents','clipboardRequests','externalProcessRequests','foreignWindowRequests'){
  if($null-eq $guard.$field -or $guard.$field-ne 0){throw ('Launcher background guard did not remain zero: '+$field)}
}
if($null-eq $guard.nativeEditNotifications -or $guard.nativeEditNotifications-le 0){throw 'Real hidden EDIT notifications were not observed.'}
[ordered]@{Status='Passed';Version='6.3.3.0';Scenarios=15;Checks=139;Source=$sourceHash;SourceFencePassed=$true;SourceInputCount=$snapshot.Count;Executable=$executable;ExecutableSha256=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash;BackgroundOnly=$true;NativeGuard=$guard;Limitations='Hidden native controls and parser paths; excludes physical input, visible compositor, actual browser processes and clipboard.'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $EvidenceDirectory 'result.json') -Encoding utf8
Get-Content -LiteralPath (Join-Path $EvidenceDirectory 'native.log')|Select-Object -Last 3
