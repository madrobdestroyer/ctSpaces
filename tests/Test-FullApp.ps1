param([ValidateSet('Core','Integration','Final')][string]$Stage='Core',
      [string[]]$TestNames=@(),
      [switch]$BackgroundOnly,
      [string]$ExePath=(Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
      [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\full-app-stress'))
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($ArtifactDirectory)
if (-not $output.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)) {throw 'Evidence directory must be below workspace build.'}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$runner=(Get-Command pwsh -CommandType Application).Source
$resolvedExe=[IO.Path]::GetFullPath($ExePath)
$testedExeHash=if(Test-Path -LiteralPath $resolvedExe -PathType Leaf){(Get-FileHash -LiteralPath $resolvedExe -Algorithm SHA256).Hash}else{''}
$cases=switch($Stage) {
 'Core' { @('DefaultProfileSanitizer','DefaultProfileArchiveSanitizer','BrowserPreferencesJson','ConfigPersistence','GuidedWalkthrough','ArchiveRoundTrip','ArchiveInterruption','StartupPreferencePublication','BackupRecoveryFault','RenameShortcutTransactions','HandleBoundRename','ClientRenameUnicode','ClientIdentityUnicode','ClientIsolation','ClientShortcutName','ClientActivity','ClientCacheMaintenance','ClientNotes','InstallerRuntime','InstallerPublication','MainChrome','LauncherSessionMenu','StartupActionFailure','BrowserTitle','BrowserLaunchFailure','ProcessLifetimeSafety','FirefoxPreferences','BrowserProcessExit','ChromiumSessionFixture','TransientLifecycle','DefaultSaveTransaction','ClientPreferenceDelta','ClientIconFailure','ThemeState','LauncherDragMutation','ClientMaintenanceConsent','GuideDialogState','ManualIconConsent','PinnedDragLifetime','ArchiveUiLifetime','IconCommandOwnership','NotesClientRecall','ClientCommandLifetime','ConfigMenuLifetime','NotesQuickCaptureBoundary','ClipboardUrlBoundary','NotesLauncherLifetime','NotesLinkContext','BackupProgress','BackupSelection','LauncherInputValidation','NotesFind','NotesSpelling','NotesMarkdown','NotesEditingExtras','ClientTabLayout','BrowserTabLayoutPreferences','FirefoxTabLayout','BrowserTabLayoutUi') }
 'Integration' { @('WorkflowFeatures','RestoreTabsToggle','ExternalBrowserProfileUse','GuidedWalkthroughUi','QuickTourUi','AutoFetchDialogLayout','ThemeResponsiveness','SameClientMultiBrowser','CoordinatedShutdown','RestoreAfterBrowserClose','RestoreAfterBrowserCloseMultiTab','LiveIconRefresh','CleanupDialogFaultAndDpi','CleanupScanUi','DefaultEditorDiscard','InactiveClientCleanup','ClientProductivityUi','ClientLifecycleStress') }
 'Final' { @('NotesWorkspace','Release') }
}
$results=@()
$TestNames=@($TestNames | ForEach-Object {$_ -split ','})
if($TestNames.Count) {
 foreach($test in $TestNames) {if($test -notin $cases) {throw 'Requested test is not in this stage.'}}
 $cases=$TestNames
}
if($BackgroundOnly -and ($Stage-eq'Integration' -or 'NotesWorkspace'-in$cases)) {
 throw 'This selection includes foreground UI tests. Choose Core, or select Release only in Final, for background execution.'
}
foreach($name in $cases) {
 $log=Join-Path $output ($Stage+'-'+$name+'.log')
 $arguments=@('-NoProfile','-File',(Join-Path $PSScriptRoot ('Test-'+$name+'.ps1')))
 if(($Stage-eq'Integration'-and$name-ne'CoordinatedShutdown')-or$name-eq'Release'){
  if(-not(Test-Path -LiteralPath $resolvedExe -PathType Leaf)){throw "Executable under test does not exist: $resolvedExe"}
  $arguments+=@('-ExePath',$resolvedExe)
 }
 if($name -eq 'ThemeResponsiveness') {$arguments+=@('-SelectionChanges','1000')}
 if($name -eq 'LiveIconRefresh') {$arguments+=@('-ManualIterations','2')}
 if($name -eq 'ClientProductivityUi') {$arguments+='-LauncherOnly'}
 if($name -eq 'InstallerRuntime' -and $BackgroundOnly) {$arguments+=@('-HeadlessOnly','-ArtifactDirectory',(Join-Path $output $name))}
 if($BackgroundOnly -and $name -in @('ClientIsolation','ClientRenameUnicode','ClientIdentityUnicode')) {$arguments+=@('-BackgroundOnly','-ArtifactDirectory',(Join-Path $output $name))}
 if($name -in @('TransientLifecycle','DefaultSaveTransaction','ClientPreferenceDelta','ClientIconFailure','LauncherDragMutation','ClientMaintenanceConsent','ManualIconConsent','PinnedDragLifetime','ArchiveUiLifetime','IconCommandOwnership','NotesClientRecall','ClientCommandLifetime','ConfigMenuLifetime','NotesQuickCaptureBoundary','ClipboardUrlBoundary','ArchiveInterruption','StartupPreferencePublication','NotesLauncherLifetime','NotesLinkContext','BackupProgress','BackupSelection','LauncherInputValidation','NotesFind','NotesSpelling','NotesMarkdown','NotesEditingExtras','ClientTabLayout','BrowserTabLayoutPreferences','FirefoxTabLayout','BrowserTabLayoutUi')) {$arguments+=@('-ArtifactDirectory',(Join-Path $output $name))}
 if($name -eq 'ThemeState') {$arguments+=@('-OutputRoot',(Join-Path $output $name))}
 if($name -in @('RestoreAfterBrowserClose','RestoreAfterBrowserCloseMultiTab')) {$arguments+=@('-ArtifactDirectory',(Join-Path $output $name))}
 if($name -eq 'ClientLifecycleStress') {$arguments+=@('-Cycles','200','-UnrelatedBrowserExitChurn','3000','-EvidenceDirectory',(Join-Path $output 'client-lifecycle'))}
 $started=Get-Date
 Write-Output ('START '+$name)
 if($BackgroundOnly) {
  $launch=[Diagnostics.ProcessStartInfo]::new($runner)
  $launch.WorkingDirectory=$root;$launch.UseShellExecute=$false;$launch.CreateNoWindow=$true
  $launch.RedirectStandardOutput=$true;$launch.RedirectStandardError=$true
  foreach($argument in $arguments){$launch.ArgumentList.Add($argument)}
  $process=[Diagnostics.Process]::Start($launch)
  try {
   try {if(-not$process.HasExited){$process.PriorityClass=[Diagnostics.ProcessPriorityClass]::BelowNormal}}
   catch [InvalidOperationException] {if(-not$process.HasExited){throw}}
   $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
   if(-not$process.WaitForExit(600000)){$process.Kill($true);throw ('Background suite timed out: '+$name)}
   [IO.File]::WriteAllText($log,$stdout.Result+$stderr.Result)
   $code=$process.ExitCode
   [pscustomobject]@{Exe=$runner;Arguments=$arguments;CreateNoWindow=$true;Priority='BelowNormal';ExitCode=$code;Log=$log;LogSha256=(Get-FileHash -LiteralPath $log).Hash}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath ($log+'.json')
  } finally {$process.Dispose()}
 } else {
  & $runner @arguments *> $log
  $code=$LASTEXITCODE
 }
 if($code -eq 0 -and $name -eq 'InstallerRuntime' -and $BackgroundOnly) {
  $runtimeLog=Get-Content -LiteralPath $log -Raw
  if($runtimeLog -notmatch 'Installer runtime tests passed \(20 scenarios\)' -or $runtimeLog -match 'Installed handoff readiness passed') {
   $code=1
   Write-Output 'FAIL InstallerRuntime background scope was not confirmed by its native test log.'
  }
 }
 $usesExe=($Stage-eq'Integration'-and$name-ne'CoordinatedShutdown')-or$name-eq'Release'
 if($usesExe-and(Get-FileHash -LiteralPath $resolvedExe -Algorithm SHA256).Hash-ne$testedExeHash){throw 'Executable changed during the stage; its evidence would mix builds.'}
 $scope=if($name-eq'InstallerRuntime'-and$BackgroundOnly){'20 headless scenarios; installed-child foreground handoff deferred'}elseif($BackgroundOnly-and$name-in@('ClientIsolation','ClientRenameUnicode','ClientIdentityUnicode')){'Guarded legacy client native suite; zero actual show/focus/foreground events required'}else{'Default suite'}
 $results += [PSCustomObject]@{Test=$name;Scope=$scope;ExitCode=$code;Seconds=[Math]::Round(((Get-Date)-$started).TotalSeconds,2);Log=$log;TestedExe=$(if($usesExe){$resolvedExe}else{'Current source native suite or browser protocol test'});TestedExeSha256=$(if($usesExe){$testedExeHash}else{''})}
 $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output ($Stage+'-results.json'))
 Write-Output ('FINISH '+$name+' exit='+$code)
}
if(@($results | Where-Object ExitCode -ne 0).Count) {throw 'Some cases failed; inspect the stage results and retained logs.'}
