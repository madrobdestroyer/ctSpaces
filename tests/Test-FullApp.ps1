param([ValidateSet('Core','Integration','Final')][string]$Stage='Core',
      [string[]]$TestNames=@(),
      [string]$ArtifactDirectory=(Join-Path $PSScriptRoot '..\build\full-app-stress'))
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$output=[IO.Path]::GetFullPath($ArtifactDirectory)
if (-not $output.StartsWith((Join-Path $root 'build\'),[StringComparison]::OrdinalIgnoreCase)) {throw 'Evidence directory must be below workspace build.'}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$runner=(Get-Command pwsh -CommandType Application).Source
$cases=switch($Stage) {
 'Core' { @('DefaultProfileSanitizer','DefaultProfileArchiveSanitizer','BrowserPreferencesJson','ConfigPersistence','GuidedWalkthrough','ArchiveRoundTrip','RenameShortcutTransactions','HandleBoundRename','ClientShortcutName','ClientActivity','ClientNotes','InstallerRuntime') }
 'Integration' { @('WorkflowFeatures','RestoreTabsToggle','ExternalBrowserProfileUse','GuidedWalkthroughUi','QuickTourUi','AutoFetchDialogLayout','ThemeResponsiveness','SameClientMultiBrowser','CoordinatedShutdown','RestoreAfterBrowserClose','LiveIconRefresh','CleanupDialogFaultAndDpi','CleanupScanUi','DefaultEditorDiscard','InactiveClientCleanup','ClientProductivityUi') }
 'Final' { @('NotesWorkspace','Release') }
}
$results=@()
$TestNames=@($TestNames | ForEach-Object {$_ -split ','})
if($TestNames.Count) {
 foreach($test in $TestNames) {if($test -notin $cases) {throw 'Requested test is not in this stage.'}}
 $cases=$TestNames
}
foreach($name in $cases) {
 $log=Join-Path $output ($Stage+'-'+$name+'.log')
 $arguments=@('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $PSScriptRoot ('Test-'+$name+'.ps1')))
 if($name -eq 'ThemeResponsiveness') {$arguments+=@('-SelectionChanges','1000')}
 if($name -eq 'LiveIconRefresh') {$arguments+=@('-ManualIterations','2')}
 if($name -eq 'ClientProductivityUi') {$arguments+='-LauncherOnly'}
 $started=Get-Date
 Write-Output ('START '+$name)
 & $runner @arguments *> $log
 $code=$LASTEXITCODE
 $results += [PSCustomObject]@{Test=$name;ExitCode=$code;Seconds=[Math]::Round(((Get-Date)-$started).TotalSeconds,2);Log=$log}
 $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $output ($Stage+'-results.json'))
 Write-Output ('FINISH '+$name+' exit='+$code)
}
if(@($results | Where-Object ExitCode -ne 0).Count) {throw 'Some cases failed; inspect the stage results and retained logs.'}
