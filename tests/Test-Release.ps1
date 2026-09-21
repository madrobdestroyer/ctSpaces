param(
    [string]$ExePath = (Join-Path $PSScriptRoot '..\dist\x64\Release\ctSpaces.exe'),
    [switch]$SourceOnly
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$archivePath = Join-Path $projectRoot 'Default.7z'
$versionHeaderPath = Join-Path $projectRoot 'version.h'
$sevenZipVersionHeaderPath = Join-Path $projectRoot '3p\7zip\C\7zVersion.h'
$sourcePath = Join-Path $projectRoot 'ctSpaces.cpp'
$browserTitleHeaderPath = Join-Path $projectRoot 'BrowserTitle.h'
$configPersistenceSourcePath = Join-Path $projectRoot 'ConfigPersistence.cpp'
$configPersistenceHeaderPath = Join-Path $projectRoot 'ConfigPersistence.h'
$backupEnginePath = Join-Path $projectRoot 'BackupEngine.cpp'
$backupEngineHeaderPath = Join-Path $projectRoot 'BackupEngine.h'
$siblingStageNameHeaderPath = Join-Path $projectRoot 'SiblingStageName.h'
$progressUiPath = Join-Path $projectRoot 'ProgressUI.cpp'
$inProcHeaderPath = Join-Path $projectRoot 'InProc7z.h'
$inProcSourcePath = Join-Path $projectRoot 'InProc7z.cpp'
$resourceHeaderPath = Join-Path $projectRoot 'Resource.h'
$resourcePath = Join-Path $projectRoot 'ctSpaces.rc'
$appProjectPath = Join-Path $projectRoot 'ctSpaces.vcxproj'
$directorySanitizerPath =
    Join-Path $projectRoot 'tools\Sanitize-DefaultProfileDirectory.ps1'
$archiveSanitizerPath = Join-Path $projectRoot 'tools\Sanitize-DefaultProfile.ps1'
$defaultSanitizerQaPath =
    Join-Path $projectRoot 'tests\Test-DefaultProfileSanitizer.ps1'
$archiveSanitizerQaPath =
    Join-Path $projectRoot 'tests\Test-DefaultProfileArchiveSanitizer.ps1'
$sevenZipProjectPath = Join-Path $projectRoot '7zip\Format7z.vcxproj'
$autoFetchQaPath = Join-Path $projectRoot 'tests\Test-AutoFetchDialogLayout.ps1'
$liveIconQaPath = Join-Path $projectRoot 'tests\Test-LiveIconRefresh.ps1'
$workflowFeaturesQaPath =
    Join-Path $projectRoot 'tests\Test-WorkflowFeatures.ps1'
$expectedSevenZipVersion = '26.02'
$failures = [System.Collections.Generic.List[string]]::new()

function Assert-ReleaseCheck {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        $failures.Add($Message)
    }
}

$versionHeader = Get-Content -Raw -LiteralPath $versionHeaderPath
$versionMatch = [regex]::Match(
    $versionHeader,
    '#define\s+CTSPACES_VERSION_TEXT\s+"(?<version>\d+\.\d+\.\d+\.\d+)"'
)
Assert-ReleaseCheck $versionMatch.Success 'version.h does not define CTSPACES_VERSION_TEXT.'
$expectedVersion = if ($versionMatch.Success) { $versionMatch.Groups['version'].Value } else { '' }
$displayVersionMatch = [regex]::Match(
    $versionHeader,
    '#define\s+CTSPACES_DISPLAY_VERSION_TEXT\s+"(?<version>\d+\.\d+)"'
)
Assert-ReleaseCheck $displayVersionMatch.Success 'version.h does not define CTSPACES_DISPLAY_VERSION_TEXT.'
if ($versionMatch.Success -and $displayVersionMatch.Success) {
    $expectedShortVersion = ($expectedVersion -split '\.')[0..1] -join '.'
    Assert-ReleaseCheck ($displayVersionMatch.Groups['version'].Value -eq $expectedShortVersion) 'The display version does not match the Windows major/minor version.'
}

Assert-ReleaseCheck (Test-Path -LiteralPath $sevenZipVersionHeaderPath -PathType Leaf) 'The bundled 7-Zip version header is missing.'
if (Test-Path -LiteralPath $sevenZipVersionHeaderPath -PathType Leaf) {
    $sevenZipVersionHeader = Get-Content -Raw -LiteralPath $sevenZipVersionHeaderPath
    $sevenZipVersionMatch = [regex]::Match(
        $sevenZipVersionHeader,
        '#define\s+MY_VERSION_NUMBERS\s+"(?<version>\d+\.\d+)"'
    )
    Assert-ReleaseCheck $sevenZipVersionMatch.Success 'The bundled 7-Zip version could not be determined.'
    if ($sevenZipVersionMatch.Success) {
        Assert-ReleaseCheck ($sevenZipVersionMatch.Groups['version'].Value -eq $expectedSevenZipVersion) "Bundled 7-Zip is '$($sevenZipVersionMatch.Groups['version'].Value)', expected '$expectedSevenZipVersion'."
    }
}

$source = Get-Content -Raw -LiteralPath $sourcePath
$browserTitleHeader = Get-Content -Raw -LiteralPath $browserTitleHeaderPath
$workflowFeaturesQa = Get-Content -Raw -LiteralPath $workflowFeaturesQaPath
$configPersistenceSource =
    Get-Content -Raw -LiteralPath $configPersistenceSourcePath
$configPersistenceHeader =
    Get-Content -Raw -LiteralPath $configPersistenceHeaderPath
$prepareUnicodeIniStageBodyMatch = [regex]::Match(
    $configPersistenceSource,
    'bool\s+PrepareUnicodeIniStage\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nbool\s+IsSafeExistingDirectory'
)
$mutateIniFileBodyMatch = [regex]::Match(
    $configPersistenceSource,
    'bool\s+MutateIniFileAtomically\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nbool\s+ApplyIniMutationsAtomically'
)
$backupEngineHeader =
    Get-Content -Raw -LiteralPath $backupEngineHeaderPath
$siblingStageNameHeader =
    Get-Content -Raw -LiteralPath $siblingStageNameHeaderPath
$closeAllBodyMatch = [regex]::Match(
    $source,
    'void\s+RequestCloseAllProfiles\(\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstd::wstring\s+GetExeVersion'
)
$sessionShutdownBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+RequestSessionShutdown\(DWORD\s+pid\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+void\s+CloseSessionTab'
)
$launchProfileBodyMatch = [regex]::Match(
    $source,
    'DWORD\s+LaunchProfile\([^)]*BrowserKind\s+&browser,[^)]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nvoid\s+UpdateClientsComboBox'
)
$startBrowserBodyMatch = [regex]::Match(
    $source,
    'static\s+DWORD\s+StartBrowserProcess\(BrowserKind\s+browser,[\s\S]*?\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+RemoveProfileDirectoryAndVerify'
)
$startupPreferenceBodyMatch = [regex]::Match(
    $source,
    'bool\s+SetBrowserStartupPreference\(const\s+fs::path\s+&profilePath,\s*bool\s+restoreTabs\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nbool\s+ClearBrowserStartupState'
)
$deleteClientBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+DeleteEntireClient\([\s\S]*?\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nvoid\s+GuiProfDel'
)
$saveRestoreTabsBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+SaveRestoreTabsPreferences\(\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+void\s+PruneRestoreTabsPreferences'
)
$vacuumBodyMatch = [regex]::Match(
    $source,
    'void\s+GuiProfVacuum\(\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+void\s+RetireCachedIcons'
)
$exportAllBodyMatch = [regex]::Match(
    $source,
    'void\s+GuiProfExportAll\(\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nvoid\s+GuiProfRestoreAll'
)
$restoreAllBodyMatch = [regex]::Match(
    $source,
    'void\s+GuiProfRestoreAll\(\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nvoid\s+GuiOpenTmp'
)
$browserProcessProbeBodyMatch = [regex]::Match(
    $source,
    'static\s+ProfileUseState\s+ProbeBrowserProcessesAgainstProfiles\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+ProfileUseState\s+ProbeExactBrowserProfileInUse'
)
$exactProfileProbeBodyMatch = [regex]::Match(
    $source,
    'static\s+ProfileUseState\s+ProbeExactBrowserProfileInUse\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+ProfileUseState\s+ProbeClientProfilesInUse'
)
$clientProfilesProbeBodyMatch = [regex]::Match(
    $source,
    'static\s+ProfileUseState\s+ProbeClientProfilesInUse\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+ProfileUseState\s+ProbeAnySitesProfileInUse'
)
$anySitesProbeBodyMatch = [regex]::Match(
    $source,
    'static\s+ProfileUseState\s+ProbeAnySitesProfileInUse\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+IsValidWebUrl'
)
$resolveStandardProfileBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+ResolveStandardBrowserProfile\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nDWORD\s+LaunchProfile'
)
$renameClientBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+RenameClientProfile\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+void\s+GuiRenameClient'
)
$shortcutSignatureComparisonBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+SameSafeRegularFileSignature\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}'
)
$siblingStageHelperBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+TryMakeUniqueSiblingStagePath\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+void\s+RemoveSafeStagingFile'
)
$stagedDirectoryMoveBodyMatch = [regex]::Match(
    $source,
    'static\s+StagedDirectoryMoveOutcome\s+MoveDirectoryThroughUniqueSibling\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+RenameClientProfile'
)
$archiveClientBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+ArchiveClientProfile\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+void\s+GuiArchiveClient'
)
$copyInstalledBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+CopyFileWithRetry\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+CopyCurrentBuildToInstalled'
)
$installedExeValidationBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+ValidateInstalledExecutablePath\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+ValidateInstallCopyPaths'
)
$installCopyPathsBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+ValidateInstallCopyPaths\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+TryMakeUniqueSiblingStagePath'
)
$launchInstalledBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+LaunchInstalledVersion\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+IsManagedApplicationShortcut'
)
$genericShortcutBodyMatch = [regex]::Match(
    $source,
    'bool\s+CreateShortcut\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nenum\s+class\s+AutorunValueState'
)
$managedApplicationShortcutBodyMatch = [regex]::Match(
    $source,
    'static\s+ManagedApplicationShortcutState\s+InspectManagedApplicationShortcut\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+IsManagedApplicationShortcut'
)
$setWindowAppIdBodyMatch = [regex]::Match(
    $source,
    'bool\s+SetWindowAppId\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+ApplyWindowIcons'
)
$queryAutorunBodyMatch = [regex]::Match(
    $source,
    'static\s+AutorunValueState\s+QueryAutorunValue\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+AutorunValueState\s+InspectAutorunValue'
)
$configureAutorunBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+ConfigureAutorun\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nbool\s+doInstall'
)
$clientShortcutBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+CreateClientDesktopShortcut\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstruct\s+ClientLaunchRequest'
)
$managedClientShortcutVerifierBodyMatch = [regex]::Match(
    $source,
    'static\s+bool\s+IsManagedClientDesktopShortcut\([^;{}]*\)\s*noexcept\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+IsManagedClientDesktopShortcutForClient'
)
$settingsShortcutCommandBodyMatch = [regex]::Match(
    $source,
    'else\s+if\s*\(wmId\s*==\s*IDM_CTX_CREATE_SHORTCUT\)\s*\{(?<body>[\s\S]*?)\r?\n\s*\}\s*else\s+if\s*\(wmId\s*==\s*IDM_CTX_EDIT_DEFAULT_PROFILE\)'
)
$sessionTabTextBodyMatch = [regex]::Match(
    $source,
    'static\s+std::wstring\s+GetSessionTabText\(int\s+iTab\)\s*\{(?<body>[\s\S]*?)\r?\n\}'
)
$clientWindowCallbackBodyMatch = [regex]::Match(
    $source,
    'static\s+BOOL\s+CALLBACK\s+EnumClientWindowsCallback\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+std::vector<HWND>\s+FindClientWindows'
)
$openClientFolderBodyMatch = [regex]::Match(
    $source,
    'static\s+void\s+OpenSelectedClientFolder\(\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+void\s+UpdateIconPreviewForSelection'
)
$configMenuBodyMatch = [regex]::Match(
    $source,
    'static\s+void\s+UpdateConfigMenuEnabledState\(\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+HICON\s+GetClientIconForUi'
)
$clearClientIconCacheBodyMatch = [regex]::Match(
    $source,
    'static\s+void\s+ClearClientIconCache\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+fs::path\s+MakeClientIconWorkPath'
)
$toggleRestoreTabsBodyMatch = [regex]::Match(
    $source,
    'static\s+void\s+ToggleRestoreTabsForSelectedClient\(\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+void\s+UpdatePinButtonState'
)
$togglePinBodyMatch = [regex]::Match(
    $source,
    'static\s+void\s+ToggleSelectedClientPin\(\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+SelectPinnedClient'
)
$finishPinnedDragBodyMatch = [regex]::Match(
    $source,
    'static\s+void\s+FinishPinnedDragPersistence\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+void\s+MovePinnedClientForDrag'
)
$qaReorderPinnedBodyMatch = [regex]::Match(
    $source,
    'case\s+WM_APP_QA_REORDER_PINNED:\s*\{(?<body>[\s\S]*?)\r?\n\s*\}\r?\n\s*case\s+WM_APP_QA_RESTORE_ARCHIVED:'
)
$backupEngine = Get-Content -Raw -LiteralPath $backupEnginePath
$progressUi = Get-Content -Raw -LiteralPath $progressUiPath
$inProcHeader = Get-Content -Raw -LiteralPath $inProcHeaderPath
$inProcSource = Get-Content -Raw -LiteralPath $inProcSourcePath
$resourceHeader = Get-Content -Raw -LiteralPath $resourceHeaderPath
$resources = Get-Content -Raw -LiteralPath $resourcePath
$appProjectText = Get-Content -Raw -LiteralPath $appProjectPath
$appProject = [xml]$appProjectText
$sevenZipProject = [xml](Get-Content -Raw -LiteralPath $sevenZipProjectPath)
$directorySanitizer = Get-Content -Raw -LiteralPath $directorySanitizerPath
$archiveSanitizer = Get-Content -Raw -LiteralPath $archiveSanitizerPath
$defaultSanitizerQa = Get-Content -Raw -LiteralPath $defaultSanitizerQaPath
$archiveSanitizerQa = Get-Content -Raw -LiteralPath $archiveSanitizerQaPath
$appReleaseX64 = @(
    $appProject.Project.ItemDefinitionGroup |
        Where-Object { $_.Condition -match 'Release\|x64' }
) | Select-Object -First 1
$sevenZipReleaseX64 = @(
    $sevenZipProject.Project.ItemDefinitionGroup |
        Where-Object { $_.Condition -match 'Release\|x64' }
) | Select-Object -First 1
$autoFetchQa = Get-Content -Raw -LiteralPath $autoFetchQaPath
$liveIconQa = Get-Content -Raw -LiteralPath $liveIconQaPath
Assert-ReleaseCheck ($source -match 'APP_VERSION\s*=\s*CTSPACES_DISPLAY_VERSION_WTEXT') 'ctSpaces.cpp is not using the shared display-version macro.'
Assert-ReleaseCheck ($resources -match 'FILEVERSION\s+CTSPACES_VERSION_NUMBER') 'ctSpaces.rc FILEVERSION is not using the shared version macro.'
Assert-ReleaseCheck ($resources -match 'PRODUCTVERSION\s+CTSPACES_VERSION_NUMBER') 'ctSpaces.rc PRODUCTVERSION is not using the shared version macro.'
Assert-ReleaseCheck ($resourceHeader -match '#define\s+IDR_DEFAULT_PROFILE_SANITIZER\s+131') 'The embedded Default-profile sanitizer resource ID is missing.'
Assert-ReleaseCheck ($resourceHeader -match '#define\s+_APS_NEXT_RESOURCE_VALUE\s+13[2-9]') 'The next resource ID does not advance past the embedded sanitizer.'
Assert-ReleaseCheck ($resources -match 'IDR_DEFAULT_PROFILE_SANITIZER\s+BINARY\s+"tools\\\\Sanitize-DefaultProfileDirectory\.ps1"') 'ctSpaces.rc does not embed the shared Default-profile sanitizer.'
Assert-ReleaseCheck ($appProjectText -match '<None\s+Include="tools\\Sanitize-DefaultProfileDirectory\.ps1"\s*/>') 'The shared Default-profile sanitizer is missing from the project.'
Assert-ReleaseCheck (Test-Path -LiteralPath $defaultSanitizerQaPath -PathType Leaf) 'The focused Default-profile sanitizer QA script is missing.'
Assert-ReleaseCheck (Test-Path -LiteralPath $archiveSanitizerQaPath -PathType Leaf) 'The focused Default-profile archive-wrapper QA script is missing.'
Assert-ReleaseCheck ($source -match 'DEFAULT_TEMPLATE_REVISION\s*=\s*4') 'The corrected sanitized starter migration revision is not 4.'
Assert-ReleaseCheck ($source -notmatch 'RestoreDefaultFaviconsIfMissing|_FaviconRepair') 'Legacy starter Favicons injection remains reachable.'
Assert-ReleaseCheck ($source -match 'IDR_DEFAULT_PROFILE_SANITIZER[\s\S]*?WindowsPowerShell[\s\S]*?RunHiddenCommandAndWait[\s\S]*?_7zInspect7z[\s\S]*?_7zTest7z') 'Runtime Default saving does not use the embedded bounded sanitizer and archive validation path.'
Assert-ReleaseCheck ($source -match 'ValidateSanitizedDefaultProfileTree\(sanitizedRoot') 'Runtime Default saving does not exact-validate the sanitized filesystem before compression.'
Assert-ReleaseCheck ($source -notmatch 'aKeepDefault|CleanupProfile\s*\(') 'The removed cleanup-in-place Default allowlist remains in source.'
Assert-ReleaseCheck ($source -match 'isTemp\s*\|\|\s*isDefault[\s\S]*?RemoveTransientProfileIfIdleAndVerify\(profilePath') 'Disposable Default editor state is not exact-probed, removed, and verified before extraction.'
Assert-ReleaseCheck ($source -match 'RemoveProfileDirectoryAndVerify\(profilePath,[\s\S]*?if\s*\(saved\s*&&\s*profileRemoved\)') 'Raw Default editor data is not removed and verified after every save outcome.'
Assert-ReleaseCheck ($source -match 'Sanitized-Default(?:-v\{\})?\.7z') 'Default recovery copies are not explicitly labeled as sanitized.'
Assert-ReleaseCheck ($source -notmatch 'Default-before-template|backupPath\s*=\s*backupDir\s*/\s*\(GetBackupTimestamp\(\)\s*\+\s*L"-Default\.7z"') 'A pre-sanitization Default archive can still be copied into _DefBak.'
Assert-ReleaseCheck ($directorySanitizer -match 'Copy-LockedRegularFile\s+-Source\s+\$sourceBookmarks[\s\S]*?-MaximumBytes\s+\$maximumBookmarksBytes') 'Bookmarks is not required, bounded, and lock-copied.'
Assert-ReleaseCheck ($directorySanitizer -match 'Copy-LockedRegularFile\s+-Source\s+\$sourceFavicons[\s\S]*?Sanitize-FaviconsDatabase') 'Required Favicons is not sanitized from a locked copy.'
Assert-ReleaseCheck ($directorySanitizer -match 'System32\\winsqlite3\.dll[\s\S]*?PRAGMA integrity_check[\s\S]*?DELETE FROM icon_mapping[\s\S]*?VACUUM') 'Favicons is not proof-pruned with native Windows SQLite.'
Assert-ReleaseCheck ($directorySanitizer -notmatch 'python(?:\.exe)?') 'The runtime Default sanitizer must not depend on Python.'
Assert-ReleaseCheck ($directorySanitizer -match 'Assert-NotReparsePoint\s+\$sourceItem[\s\S]*?Assert-NotReparsePoint\s+\$sourceDefaultItem') 'The shared sanitizer does not reject reparse source roots.'
Assert-ReleaseCheck ($directorySanitizer -match 'function\s+Copy-IncludedTreeSafely[\s\S]*?Get-ChildItem[\s\S]*?Copy-LockedRegularFile') 'Extension packages are not copied through the bounded single-pass traversal.'
Assert-ReleaseCheck ($directorySanitizer -match 'Get-ValidatedExtensionRelativePath[\s\S]*?\$segments\[0\]\s+-cne\s+\$ExtensionId[\s\S]*?copied package target') 'Secure Preferences package paths are not bound to exact copied extension targets.'
Assert-ReleaseCheck ($directorySanitizer -notmatch "Add-PropertyIfPresent\s+\$cleanPreferences\s+\$preferences\s+'(?:bookmark_bar|intl|spellcheck|toolbar)'") 'The shared sanitizer copies an entire mutable Preferences section.'
Assert-ReleaseCheck ($directorySanitizer -notmatch '\$cleanSettings\[\$id\]\s*=\s*\$entry\.Value') 'The shared sanitizer copies complete extension runtime settings.'
Assert-ReleaseCheck ($archiveSanitizer -match '&\s+\$sanitizerItem\.FullName\s+`?\s*-SourceProfile\s+\$extractRoot\s+-OutputRoot\s+\$cleanRoot') 'The build-time archive sanitizer does not delegate to the validated shared directory policy.'
Assert-ReleaseCheck ($archiveSanitizer -match 'Unexpected starter-template entry remains') 'The build-time archive sanitizer lacks an exact output allowlist.'
Assert-ReleaseCheck ($archiveSanitizer -match 'Bookmarks\\\.bak\|Favicons-\(journal\|wal\|shm\)') 'The build-time archive sanitizer does not explicitly reject Bookmarks.bak and Favicons sidecars.'
Assert-ReleaseCheck ($archiveSanitizer -match '\[Environment\]::SystemDirectory[\s\S]*?Get-RegularNonReparseFile[\s\S]*?Get-ValidatedArchiveListing') 'The archive sanitizer does not bind and validate System32 tar plus its file inputs.'
Assert-ReleaseCheck ($archiveSanitizer -match 'empty or NUL[\s\S]*?absolute, UNC, or drive[\s\S]*?parent traversal[\s\S]*?case-insensitive duplicate file path') 'The archive sanitizer preflight does not reject unsafe or duplicate paths.'
Assert-ReleaseCheck ($archiveSanitizer -match 'maximumArchiveEntryBytes[\s\S]*?maximumArchiveUncompressedBytes[\s\S]*?unverifiable declared size[\s\S]*?total uncompressed size limit') 'The archive sanitizer does not bound declared uncompressed sizes before extraction.'
Assert-ReleaseCheck ($archiveSanitizer -match 'COM\(\?:\[1-9\]\|\\u00B9\|\\u00B2\|\\u00B3\)[\s\S]*?LPT\(\?:\[1-9\]\|\\u00B9\|\\u00B2\|\\u00B3\)') 'The archive sanitizer does not reject superscript COM/LPT device names.'
Assert-ReleaseCheck ($archiveSanitizer -match 'GetACP\(\)[\s\S]*?DecoderExceptionFallback[\s\S]*?StandardOutput\.BaseStream\.Read[\s\S]*?archive listing exceeds the safe inspection limit' -and $archiveSanitizer -notmatch '\[Console\]::OutputEncoding') 'The archive sanitizer does not capture bounded tar listing bytes in the active Windows code page.'
Assert-ReleaseCheck ($archiveSanitizer -match 'Assert-SafeExtractedTree[\s\S]*?ReparsePoint[\s\S]*?non-file/non-directory') 'The archive sanitizer does not type-check the extracted filesystem before sanitizing.'
Assert-ReleaseCheck ($archiveSanitizer -match '\[IO\.File\]::Replace[\s\S]*?\[IO\.File\]::Move' -and $archiveSanitizer -notmatch 'Move-Item\s+-LiteralPath\s+\$stagedArchive[\s\S]*?-Force') 'The archive sanitizer does not use a same-volume non-destructive atomic commit.'
Assert-ReleaseCheck ($defaultSanitizerQa -match 'existing-client-data[\s\S]*?removed_bytes_survive[\s\S]*?EmptyBookmarksOutput[\s\S]*?corruptRejected[\s\S]*?sidecarRejected[\s\S]*?oversizedRejected') 'The focused sanitizer QA does not cover Sites isolation, favicon pruning, proof failures, and pre-copy size rejection.'
Assert-ReleaseCheck ($archiveSanitizerQa -match 'InPlace[\s\S]*?Traversal[\s\S]*?Duplicate[\s\S]*?SuperscriptDevice[\s\S]*?OversizedDeclared[\s\S]*?LockedOutput[\s\S]*?failed atomic replacement changed') 'The archive-wrapper QA does not cover in-place commit, unsafe paths, devices, declared sizes, duplicates, and old-output preservation.'
Assert-ReleaseCheck ($source -notmatch 'Compress-Archive') 'New backups still invoke PowerShell Compress-Archive.'
Assert-ReleaseCheck ($source -match 'Back Up All Client Data') 'The backup command is not using its current user-facing name.'
Assert-ReleaseCheck ($source -match 'Legacy Backup \(\*\.zip\)') 'Legacy ZIP restore compatibility is missing from the file picker.'
Assert-ReleaseCheck ($backupEngine -match 'CreateValidated7zBackup') 'The validated in-process backup engine is missing.'
Assert-ReleaseCheck ($backupEngine -match '_7zTest7z') 'New backups are not integrity-tested after creation.'
Assert-ReleaseCheck ($backupEngine -match 'ctSpacesBackup\.manifest') 'New backups do not include the ctSpaces manifest.'
Assert-ReleaseCheck ($inProcHeader -match "kMaxItems\s*=\s*500'000") 'Archive item enumeration is not capped at the reviewed limit.'
Assert-ReleaseCheck ($inProcSource -match 'bool\s+_extractMode=false;[\s\S]*?UInt32\s+Attrib=0;[\s\S]*?_processed\{\};[\s\S]*?_outFileStreamSpec=nullptr;') 'The in-process extraction callback state is not initialized at declaration.'
Assert-ReleaseCheck ($inProcSource -match 'GetStream\(UInt32\s+index[\s\S]*?_outFileStream\.Release\(\);\s*_outFileStreamSpec=nullptr;\s*_processed=\{\};' -and $inProcSource -match '_outFileStream\.Release\(\);\s*_outFileStreamSpec=nullptr;[\s\S]*?if\(_extractMode') 'The in-process extraction callback does not reset per-item state and clear its raw stream pointer.'
Assert-ReleaseCheck ($inProcHeader -match 'kMaxUncompressedBytes') 'Archive expanded bytes are not capped.'
Assert-ReleaseCheck ($inProcHeader -match 'kMaxDirectoryDepth\s*=\s*128') 'Archive recursion depth is not capped.'
Assert-ReleaseCheck ($inProcSource -match 'FILE_ATTRIBUTE_REPARSE_POINT') 'Backup creation does not reject reparse points.'
Assert-ReleaseCheck ($inProcSource -match 'IsUnsafeArchiveAttrib') '7-Zip restore does not reject link/reparse attributes.'
Assert-ReleaseCheck ($inProcSource -match 'itemCount\s*>\s*CtArchiveSafety::kMaxItems') '7-Zip inspection does not reject excessive item counts before enumeration.'
Assert-ReleaseCheck ($inProcSource -match 'currentInfo\.Size\s*!=\s*it\.Size') 'Backup creation does not revalidate source files immediately before opening them.'
Assert-ReleaseCheck ($inProcSource -match 'componentCount\s*>\s*CtArchiveSafety::kMaxDirectoryDepth') 'Archive paths do not enforce the reviewed component-depth cap.'
Assert-ReleaseCheck ($backupEngine -match 'kRestoreMetadataBytesPerItem') 'Restore disk checks do not reserve metadata overhead.'
Assert-ReleaseCheck ($backupEngine -match 'kMinimumFreeSpaceReserve') 'Restore disk checks do not preserve system headroom.'
Assert-ReleaseCheck ($backupEngineHeader -match 'kLegacyMaximumFilePathCharacters\s*=\s*259' -and $backupEngineHeader -match 'kLegacyMaximumDirectoryPathCharacters\s*=\s*247' -and $backupEngineHeader -match 'kNewClientManagedFileTailCharacters\s*=\s*150' -and $backupEngineHeader -match 'kNewClientManagedDirectoryTailCharacters\s*=\s*123') 'The reviewed non-longPathAware file, directory, or managed-client headroom constants changed.'
Assert-ReleaseCheck ($siblingStageHelperBodyMatch.Success -and $siblingStageHelperBodyMatch.Groups['body'].Value -match 'maximumComponentLength\s*=\s*\r?\n?\s*destination\.filename\(\)\.native\(\)\.size\(\)' -and $siblingStageHelperBodyMatch.Groups['body'].Value -match 'singleCharacterBudget[\s\S]*?kSingleCharacterAlphabet\.size\(\)' -and $siblingStageHelperBodyMatch.Groups['body'].Value -match 'BuildSingleCharacter\(\s*\r?\n?\s*alphabetIndex,\s*retainLinkExtension,\s*maximumComponentLength\)' -and $siblingStageHelperBodyMatch.Groups['body'].Value -match 'tokenCharacters\s*<=\s*2[\s\S]*?1u\s*<<[\s\S]*?attemptLimit' -and $siblingStageHelperBodyMatch.Groups['body'].Value -match 'sibling_stage_name::Build\(\s*\r?\n?\s*collisionToken,\s*retainLinkExtension,\s*maximumComponentLength\)' -and $siblingStageHelperBodyMatch.Groups['body'].Value -match 'CompareStringOrdinal\(stagingPath\.c_str\(\),\s*-1,\s*destination\.c_str\(\)' -and $siblingStageHelperBodyMatch.Groups['body'].Value -match 'ERROR_FILE_NOT_FOUND\s*\|\|\s*error\s*==\s*ERROR_PATH_NOT_FOUND' -and $siblingStageNameHeader -match 'kFullGuidHexCharacters\s*=\s*32' -and $siblingStageNameHeader -match 'kSingleCharacterAlphabet\s*=\s*\r?\n?\s*L"0123456789abcdefghijklmnopqrstuvwxyz_-~"' -and $siblingStageNameHeader -match 'maximumComponentLength\s*<=\s*extension\.size\(\)' -and $siblingStageNameHeader -match 'result\.push_back\(L''~''\)' -and $siblingStageNameHeader -match 'result\.append\(extension\)') 'Sibling staging is not bounded by the destination component with exhaustive safe one-character rotation/tiny-token retries, full GUID retention when space permits, .lnk retention, ordinal alias skipping, and fail-closed absence checks.'
Assert-ReleaseCheck ($source -match 'EnsureBrowserSlotForClient[\s\S]*?IsNewClientTargetPathWithinLegacyBudget[\s\S]*?IsTreeTargetWithinLegacyPathBudget\(\s*\r?\n?\s*stagedSlot,\s*slotRoot[\s\S]*?MoveFileExW\(stagedSlot' -and $source -match 'CreateNewV2Client[\s\S]*?IsNewClientTargetPathWithinLegacyBudget[\s\S]*?IsTreeTargetWithinLegacyPathBudget\(\s*\r?\n?\s*stagedClient,\s*targetRoot[\s\S]*?MoveFileExW\(stagedClient' -and $renameClientBodyMatch.Success -and $renameClientBodyMatch.Groups['body'].Value -match 'IsNewClientTargetPathWithinLegacyBudget[\s\S]*?IsTreeTargetWithinLegacyPathBudget') 'New clients, missing browser slots, and rename targets do not all enforce reserved headroom and complete mapped-tree path budgets before mutation.'
Assert-ReleaseCheck ($stagedDirectoryMoveBodyMatch.Success -and $stagedDirectoryMoveBodyMatch.Groups['body'].Value -match 'IsTreeTargetWithinLegacyPathBudget\(\s*\r?\n?\s*source,\s*intermediatePath[\s\S]*?preMoveGuard\(guardDetails\)[\s\S]*?MoveFileExW\(source\.c_str\(\),\s*intermediatePath\.c_str\(\)' -and ([regex]::Matches($stagedDirectoryMoveBodyMatch.Groups['body'].Value, 'preMoveGuard\s*\(\s*guardDetails\s*\)').Count) -eq 1 -and $renameClientBodyMatch.Success -and $renameClientBodyMatch.Groups['body'].Value -match 'caseOnlyForwardGuard[\s\S]*?ProbeClientProfilesInUse\(oldName,[\s\S]*?MoveDirectoryThroughUniqueSibling\(\s*\r?\n?\s*oldPath,\s*newPath,\s*L"rename-stage",\s*moveDetails,\s*\r?\n?\s*caseOnlyForwardGuard\)') 'Case-only rename does not map the GUID intermediate tree and then run exactly one oldName-bound process guard immediately before its first move.'
Assert-ReleaseCheck ($renameClientBodyMatch.Success -and $renameClientBodyMatch.Groups['body'].Value -match 'caseOnlyRollbackGuard[\s\S]*?ProbeClientProfilesInUse\(newName,[\s\S]*?MoveDirectoryThroughUniqueSibling\(\s*\r?\n?\s*newPath,\s*oldPath,\s*L"rename-rollback",\s*rollbackDetails,\s*\r?\n?\s*caseOnlyRollbackGuard\)' -and $renameClientBodyMatch.Groups['body'].Value -match 'std::wstring\s+rollbackProfileUseError;\s*if\s*\(ProbeClientProfilesInUse\(newName,[\s\S]*?MoveFileExW\(newPath\.c_str\(\),\s*oldPath\.c_str\(\)') 'Case-only or normal rename rollback can move the renamed profile without a fresh newName-bound external-process guard.'
Assert-ReleaseCheck ($source -match 'renameCommitted\s*&&\s*!committed[\s\S]*?committed browser-slot path was preserved[\s\S]*?renameCommitted\s*&&\s*!committed[\s\S]*?committed client path was preserved' -and $source -notmatch 'renameCommitted\s*&&\s*!committed\)\s*\{[\s\S]{0,180}?remove_all\((?:slotRoot|targetRoot)') 'A postcommit diagnostic failure can still remove a raced slot/client replacement by pathname instead of preserving it.'
Assert-ReleaseCheck ($source -match 'stagedSlot\s*=\s*workRoot\s*/\s*L"Slot"[\s\S]*?IsNewClientTargetPathWithinLegacyBudget\(\s*\r?\n?\s*workRoot,\s*L"Slot"\)[\s\S]*?CreateStagedBrowserSlot\(stagedSlot' -and $source -match 'stagedClient\s*=\s*workRoot\s*/\s*L"Client"[\s\S]*?IsNewClientTargetPathWithinLegacyBudget\(\s*\r?\n?\s*workRoot,\s*L"Client"\)[\s\S]*?CreateDirectoryW\(stagedClient') 'Temporary browser-slot and client stages do not reserve the same conservative managed path headroom before extraction.'
Assert-ReleaseCheck ($backupEngine -match 'CommitStagedRestore[\s\S]*?IsNewClientTargetPathWithinLegacyBudget[\s\S]*?IsTreeTargetWithinLegacyPathBudget\(\s*\r?\n?\s*entry\.path\(\),\s*currentSitesDir\s*/\s*clientName[\s\S]*?IsTreeTargetWithinLegacyPathBudget\(\s*\r?\n?\s*currentSitesDir,\s*recoveryDir[\s\S]*?precommitGuard\(precommitGuardUser,\s*guardFailure\)[\s\S]*?fs::rename\(currentSitesDir,\s*recoveryDir\)') 'Restore does not validate client headroom, restored targets, and recovery targets before running the final guard immediately ahead of Sites mutation.'
Assert-ReleaseCheck ($source -match 'finalRestoreCommitGuard[\s\S]*?ProbeAnySitesProfileInUse\(finalProfileUseError\)[\s\S]*?CommitStagedRestore\(\s*\r?\n?\s*stagedRestore,\s*sitesDir,\s*backupDir,\s*finalRestoreCommitGuard[\s\S]*?if\s*\(!commitResult\.ok\(\)\)\s*\{[\s\S]*?remove_all\(tempDir,\s*cleanupError\)') 'The restore UI does not supply a fail-closed final browser-process guard or clean rejected unique staging data.'
Assert-ReleaseCheck `
    ($source -match 'GetClientShortcutFileName\([^;{}]*\)\s*\{\s*return\s+clientName\s*\+\s*L"\.lnk";\s*\}' -and
     $source -match 'GetClientShortcutPathInDirectory[\s\S]*?GetClientShortcutFileName\(clientName\)[\s\S]*?fileName\.size\(\)\s*>\s*\*budget[\s\S]*?return\s+std::nullopt' -and
     $source -match 'GetPriorTruncatedClientShortcutFileName\(\s*\r?\n?\s*const\s+std::wstring\s*&clientName,\s*std::optional<BrowserKind>\s+browser\)' -and
     $source -match 'GetClientDesktopShortcutCandidatePaths[\s\S]*?GetClientShortcutPathInDirectory\(\*directory,[\s\S]*?GetHistoricalClientShortcutPathInDirectory[\s\S]*?firstHashedBudget[\s\S]*?for\s*\(size_t\s+budget\s*=\s*firstHashedBudget;[\s\S]*?budget\s*<=\s*client_shortcut_name::kMaxFileNameLength[\s\S]*?client_shortcut_name::Build\(clientName,\s*historicalSuffix,\s*budget\)[\s\S]*?GetHistoricalClientShortcutFileName\(clientName,\s*browser\)[\s\S]*?GetPriorTruncatedClientShortcutFileName\(clientName,\s*browser\)[\s\S]*?clientName\s*\+\s*GetHistoricalClientShortcutSuffix\(browser\)') `
    'Desktop shortcut lookup does not preserve the exact client-only name plus every path-budgeted, fixed-240, prior 32-bit, and untruncated browser-qualified/neutral historical candidate.'
$clientShortcutNameHeader = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'ClientShortcutName.h')
Assert-ReleaseCheck ($clientShortcutNameHeader -match 'hashToken\.size\(\)\s*>\s*availableBaseLength\)\s*\r?\n?\s*return\s+L"";' -and $clientShortcutNameHeader -notmatch 'hashToken\.substr') 'A tight Desktop budget can truncate the shortcut collision hash instead of failing safely.'
$firstPartyProductionSources = @(
    Get-ChildItem -LiteralPath $projectRoot -File |
        Where-Object { $_.Extension -in @('.cpp', '.h', '.rc') }
)
$nonAsciiProductionSources = @(
    $firstPartyProductionSources | Where-Object {
        (Get-Content -Raw -LiteralPath $_.FullName) -match '[^\x00-\x7F]'
    }
)
Assert-ReleaseCheck `
    ($nonAsciiProductionSources.Count -eq 0) `
    ('First-party production source contains raw non-ASCII text that can be miscompiled without an explicit source charset: ' +
        (($nonAsciiProductionSources.Name | Sort-Object) -join ', '))
$progressPayloadPostHelper = [regex]::Match(
    $progressUi,
    'static\s+void\s+PostProgressPayload\s*\([\s\S]*?std::unique_ptr\s*<\s*CtProgressPayload\s*>\s+payload\s*\)\s*\{\s*if\s*\(\s*mainWnd\s*&&\s*IsWindow\s*\(\s*mainWnd\s*\)\s*&&\s*PostMessageW\s*\([\s\S]*?payload\.get\s*\(\s*\)[\s\S]*?\)\s*\)\s*\{\s*payload\.release\s*\(\s*\)\s*;\s*\}'
)
$progressPayloadAllocations = [regex]::Matches(
    $progressUi,
    'std::make_unique\s*<\s*CtProgressPayload\s*>'
)
$progressPayloadSafePosts = [regex]::Matches(
    $progressUi,
    'PostProgressPayload\s*\([^;]*?std::make_unique\s*<\s*CtProgressPayload\s*>',
    [Text.RegularExpressions.RegexOptions]::Singleline
)
Assert-ReleaseCheck $progressPayloadPostHelper.Success 'Progress payload ownership is released without a valid target window and a successful PostMessageW call.'
Assert-ReleaseCheck ($progressUi -notmatch 'new\s+CtProgressPayload' -and $progressPayloadAllocations.Count -gt 0 -and $progressPayloadAllocations.Count -eq $progressPayloadSafePosts.Count) 'A progress payload allocation bypasses the ownership-safe posting helper.'
Assert-ReleaseCheck ($source -match 'JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE') 'Legacy ZIP extraction can leave a hidden child process behind.'
Assert-ReleaseCheck ($source -match 'kLegacyZipTimeoutMilliseconds') 'Legacy ZIP extraction has no bounded timeout.'
Assert-ReleaseCheck ($source -notmatch 'WaitForSingleObject\(pi\.hProcess,\s*INFINITE\)') 'A launched child process still uses an infinite wait.'
Assert-ReleaseCheck ($source -match 'ZIP reparse points and links are not allowed') 'Legacy ZIP preflight does not reject links.'
Assert-ReleaseCheck ($source -match 'kMaxImportedImageFileBytes') 'Imported image files have no encoded-size cap.'
Assert-ReleaseCheck ($source -match 'kMaxImportedImagePixels') 'Decoded imported images have no pixel cap.'
Assert-ReleaseCheck ($source -match 'IsSafeIcoPayload') 'ICO payload dimensions are not prevalidated.'
Assert-ReleaseCheck ($source -match 'ResizeBitmap_StretchSquare\(src\.get\(\),\s*s,\s*s\)') 'Icon conversion is not resizing directly from the bounded source.'
Assert-ReleaseCheck ($source -notmatch 'ResizeBitmap_StretchSquare\(scaled\.get') 'Icon conversion restored the source-sized square intermediate.'
Assert-ReleaseCheck ($source -notmatch 'EnumThreadWindows') 'Theme switching still enumerates popup/thread windows.'
Assert-ReleaseCheck ($source -match 'THEME_PREVIEW_DELAY_MS\s*=\s*100') 'Theme previews are not debounced.'
Assert-ReleaseCheck ($source -match 'InvalidateThemeWindows\(!bPreview\)') 'Theme previews still update DWM window chrome.'
Assert-ReleaseCheck ($source -match '--qa-data-dir') 'QA instances cannot isolate automated tests from live user data.'
Assert-ReleaseCheck ($source -match 'IsStrictChildPath\s*\(') 'The QA data-folder override is not constrained to its disposable QA folder.'
Assert-ReleaseCheck ($source -match 'A QA instance requires an explicit isolated') 'QA instances can still start without an isolated data folder.'
Assert-ReleaseCheck ($autoFetchQa -match '--qa-data-dir=') 'The auto-fetch dialog QA launcher does not pass an isolated data folder.'
Assert-ReleaseCheck ($liveIconQa -match '--qa-data-dir=') 'The live-icon QA launcher does not pass an isolated data folder.'
Assert-ReleaseCheck ($autoFetchQa -notmatch 'InfinitySys[\\/]ctSpaces') 'The auto-fetch dialog QA launcher still targets live ctSpaces data.'
Assert-ReleaseCheck ($liveIconQa -notmatch 'InfinitySys[\\/]ctSpaces') 'The live-icon QA launcher still targets live ctSpaces data.'
Assert-ReleaseCheck ($source -match 'TryGetSafeClientProfilePath') 'Client profile operations lack a direct-child/reparse safety check.'
Assert-ReleaseCheck ($source -match 'EM_SETLIMITTEXT') 'Client-name input is not length bounded.'
Assert-ReleaseCheck ($source -match 'COM\(\?:\[1-9\]\|\\u00B9\|\\u00B2\|\\u00B3\)[\s\S]*?LPT\(\?:\[1-9\]\|\\u00B9\|\\u00B2\|\\u00B3\)') 'Client names do not reject Windows superscript COM/LPT device aliases.'
Assert-ReleaseCheck ($source -match 'enum\s+class\s+ProfileUseState\s*\{\s*NotInUse,\s*InUse,\s*Indeterminate\s*\}') 'External browser-profile use does not have a fail-closed tri-state result.'
Assert-ReleaseCheck ($source -match 'TryGetProcessCommandLine\([^;{}]*\)\s*\{[\s\S]*?GetProcAddress\(ntdll,\s*"NtQueryInformationProcess"\)[\s\S]*?queryProcess\(process,\s*60,[\s\S]*?nextSize\s*>\s*256\s*\*\s*1024') 'Relevant browser command lines are not read through a bounded Windows process query.'
Assert-ReleaseCheck ($source -match 'BrowserKindFromProcessName\([^;{}]*\)\s*\{[\s\S]*?L"msedge\.exe"[\s\S]*?L"chrome\.exe"[\s\S]*?L"brave\.exe"[\s\S]*?L"firefox\.exe"') 'External profile-use inspection does not restrict itself to the four supported browser executables.'
Assert-ReleaseCheck ($source -match 'ExtractBrowserProfileArgument\([^;{}]*\)\s*\{[\s\S]*?L"--profile"[\s\S]*?L"--user-data-dir"[\s\S]*?L"-profile"[\s\S]*?argument\[longOptionLength\]\s*==\s*L''=''[\s\S]*?!result\.path\.is_absolute\(\)') 'External profile-use inspection does not safely parse Chromium and Firefox profile switches in both supported forms.'
Assert-ReleaseCheck $browserProcessProbeBodyMatch.Success 'The external browser-process profile probe could not be inspected.'
if ($browserProcessProbeBodyMatch.Success) {
    $browserProcessProbeBody = $browserProcessProbeBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($browserProcessProbeBody -match 'CreateToolhelp32Snapshot\(TH32CS_SNAPPROCESS,\s*0\)' -and $browserProcessProbeBody -match 'TryGetProcessCommandLine' -and $browserProcessProbeBody -match '!IsProcessStillRunning' -and $browserProcessProbeBody -match 'return\s+ProfileUseState::Indeterminate') 'Unreadable live browser processes do not fail destructive operations closed.'
    Assert-ReleaseCheck ($browserProcessProbeBody -match '!profileArgument\.valid[\s\S]*?return\s+ProfileUseState::Indeterminate' -and $browserProcessProbeBody -match 'IsStrictChildPath\(profileArgument\.path,\s*\*sitesScope\)' -and $browserProcessProbeBody -match 'SameExecutablePath\(profileArgument\.path,\s*profile\)' -and $browserProcessProbeBody -match 'return\s+ProfileUseState::InUse') 'Browser profile arguments are not validated and matched to exact client profiles or the bounded Sites scope.'
    Assert-ReleaseCheck ($browserProcessProbeBody -match 'enumerationEnd\s*!=\s*ERROR_NO_MORE_FILES\s*\|\|\s*!closed[\s\S]*?ProfileUseState::Indeterminate') 'An incomplete process enumeration can be mistaken for proof that profiles are unused.'
}
Assert-ReleaseCheck $clientProfilesProbeBodyMatch.Success 'The per-client browser-use probe could not be inspected.'
if ($clientProfilesProbeBodyMatch.Success) {
    $clientProfilesProbeBody = $clientProfilesProbeBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($clientProfilesProbeBody -match 'g_activeProfiles[\s\S]*?ProfileUseState::InUse' -and $clientProfilesProbeBody -match 'CollectClientBrowserProfileLocations' -and $clientProfilesProbeBody -match 'ProbeBrowserProcessesAgainstProfiles\(profiles,\s*std::nullopt') 'Per-client use checks do not combine tracked sessions with OS-level exact-profile inspection.'
}
Assert-ReleaseCheck $exactProfileProbeBodyMatch.Success 'The exact browser-profile use probe could not be inspected.'
if ($exactProfileProbeBodyMatch.Success) {
    $exactProfileProbeBody = $exactProfileProbeBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($exactProfileProbeBody -match 'profilePath\.empty\(\)\s*\|\|\s*!profilePath\.is_absolute\(\)' -and $exactProfileProbeBody -match 'g_activeProfiles[\s\S]*?SameExecutablePath\(active\.second\.profilePath,\s*profilePath\)[\s\S]*?ProfileUseState::InUse' -and $exactProfileProbeBody -match 'ProbeBrowserProcessesAgainstProfiles\(\{profilePath\},\s*std::nullopt') 'Exact-profile use checks do not combine tracked path identity with fail-closed OS process inspection.'
}
Assert-ReleaseCheck $anySitesProbeBodyMatch.Success 'The whole-Sites browser-use probe could not be inspected.'
if ($anySitesProbeBodyMatch.Success) {
    $anySitesProbeBody = $anySitesProbeBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($anySitesProbeBody -match '!g_activeProfiles\.empty\(\)[\s\S]*?ProfileUseState::InUse' -and $anySitesProbeBody -match 'IsSafeExistingDirectory\(sitesRoot\)' -and $anySitesProbeBody -match 'ProbeBrowserProcessesAgainstProfiles\(\{\},\s*sitesRoot') 'Whole-Sites use checks do not combine tracked sessions with a safe OS-level Sites-scope inspection.'
}
Assert-ReleaseCheck ($source -match 'ValidateNoReparsePointsInTree[\s\S]*?recursive_directory_iterator[\s\S]*?FILE_ATTRIBUTE_REPARSE_POINT') 'Whole-client deletion does not inspect the complete target tree for reparse points.'
Assert-ReleaseCheck ($source -match 'ValidateWholeClientDeleteTarget[\s\S]*?IsDirectChildPath[\s\S]*?IsV2ClientContainer[\s\S]*?IsSafeHybridClientRoot[\s\S]*?ValidateNoReparsePointsInTree') 'Whole-client deletion does not exact-validate v2 and hybrid profile layouts.'
Assert-ReleaseCheck $deleteClientBodyMatch.Success 'The whole-client deletion function could not be inspected.'
if ($deleteClientBodyMatch.Success) {
    $deleteClientBody = $deleteClientBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ([regex]::Matches($deleteClientBody, 'ValidateWholeClientDeleteTarget\(').Count -ge 2) 'Whole-client deletion is not freshly revalidated after confirmation.'
    Assert-ReleaseCheck ([regex]::Matches($deleteClientBody, 'ProbeClientProfilesInUse\(').Count -ge 2) 'Whole-client deletion lacks both pre-confirmation and immediate pre-delete OS-level browser-use checks.'
    Assert-ReleaseCheck ($deleteClientBody -match 'all legacy profile data[\s\S]*?Edge, Chrome,[\s\S]*?Brave, and Firefox[\s\S]*?logins,[\s\S]*?cookies,[\s\S]*?history,[\s\S]*?bookmarks,[\s\S]*?extensions,[\s\S]*?sessions[\s\S]*?cannot[\s\S]*?be undone') 'Whole-client deletion does not explicitly warn about permanent all-browser data loss.'
    $usesAuthorityPreservingCommit =
        $deleteClientBody -match 'RemoveClientPayloadPreservingDeleteAuthority\(' -and
        $deleteClientBody -match 'ClientTreeContainsOnlyDeleteAuthority\(\s*deleteRoot' -and
        $deleteClientBody -match 'MoveFileExW\(deleteRoot\.c_str\(\),\s*quarantineRoot\.c_str\(\),\s*MOVEFILE_WRITE_THROUGH\)' -and
        $deleteClientBody -match 'GetFileAttributesW\(deleteRoot\.c_str\(\)\)[\s\S]*?liveRootVerifiedAbsent[\s\S]*?ERROR_FILE_NOT_FOUND[\s\S]*?ERROR_PATH_NOT_FOUND' -and
        $deleteClientBody -match 'GetFileAttributesW\(quarantineRoot\.c_str\(\)\)[\s\S]*?quarantineVerifiedAbsent[\s\S]*?ERROR_FILE_NOT_FOUND[\s\S]*?ERROR_PATH_NOT_FOUND' -and
        $deleteClientBody -match 'ClientTreeContainsOnlyDeleteAuthority\(\s*quarantineRoot,\s*deleteAuthority,\s*remainingDetails,\s*false\)'
    Assert-ReleaseCheck $usesAuthorityPreservingCommit 'Whole-client deletion does not preserve retry authority, atomically quarantine the exact validated root, and prove live/quarantine absence or metadata-only residue before reporting success.'
    Assert-ReleaseCheck ($deleteClientBody -match 'IsManagedClientDesktopShortcut' -and $deleteClientBody -match 'BrowserKind::Edge[\s\S]*?BrowserKind::Chrome[\s\S]*?BrowserKind::Brave[\s\S]*?BrowserKind::Firefox' -and $deleteClientBody -match 'GetClientDesktopShortcutCandidatePaths\(clientName,\s*browser\)' -and $deleteClientBody -match 'GetClientDesktopShortcutCandidatePaths\(clientName,\s*std::nullopt\)' -and $deleteClientBody -match 'DeleteOwnedShortcutSafely') 'Whole-client deletion does not remove only identity-bound verified current and all historical browser-qualified/neutral shortcut candidates, including the prior 32-bit neutral form.'
}
Assert-ReleaseCheck $vacuumBodyMatch.Success 'The cache-vacuum function could not be inspected.'
if ($vacuumBodyMatch.Success) {
    Assert-ReleaseCheck ([regex]::Matches($vacuumBodyMatch.Groups['body'].Value, 'ProbeClientProfilesInUse\(').Count -ge 2) 'Cache vacuum lacks both pre-confirmation and immediate pre-removal OS-level browser-use checks.'
}
Assert-ReleaseCheck $restoreAllBodyMatch.Success 'The whole-Sites restore function could not be inspected.'
if ($restoreAllBodyMatch.Success) {
    $restoreAllBody = $restoreAllBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ([regex]::Matches($restoreAllBody, 'ProbeAnySitesProfileInUse\(').Count -ge 3) 'Whole-Sites restore lacks initial, post-confirmation, and immediate pre-commit OS-level browser-use checks.'
    Assert-ReleaseCheck ($restoreAllBody -match 'if\s*\(!stageResult\.ok\(\)\)[\s\S]*?ProbeAnySitesProfileInUse\(profileUseError\)[\s\S]*?CommitStagedRestore') 'Whole-Sites restore can commit staged data without a final browser-use check.'
}
Assert-ReleaseCheck $exportAllBodyMatch.Success 'The whole-Sites backup function could not be inspected.'
if ($exportAllBodyMatch.Success) {
    Assert-ReleaseCheck ([regex]::Matches($exportAllBodyMatch.Groups['body'].Value, 'ProbeAnySitesProfileInUse\(').Count -ge 2) 'Whole-Sites backup lacks both initial and immediate pre-archive OS-level browser-use checks.'
}
Assert-ReleaseCheck $renameClientBodyMatch.Success 'The client rename function could not be inspected.'
if ($renameClientBodyMatch.Success) {
    Assert-ReleaseCheck ([regex]::Matches($renameClientBodyMatch.Groups['body'].Value, 'ProbeClientProfilesInUse\(').Count -ge 2) 'Client rename lacks both initial and immediate pre-rename OS-level browser-use checks.'
}
Assert-ReleaseCheck $archiveClientBodyMatch.Success 'The client archive function could not be inspected.'
if ($archiveClientBodyMatch.Success) {
    Assert-ReleaseCheck ($archiveClientBodyMatch.Groups['body'].Value -match 'ProbeClientProfilesInUse\([^;]*?ProfileUseState::NotInUse') 'Client archive does not fail closed when the underlying profile may be open in an external browser.'
}
Assert-ReleaseCheck $resolveStandardProfileBodyMatch.Success 'The legacy browser-binding function could not be inspected.'
if ($resolveStandardProfileBodyMatch.Success) {
    $resolveStandardProfileBody = $resolveStandardProfileBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($resolveStandardProfileBody -match 'ProbeExactBrowserProfileInUse\(clientRoot,\s*profileUseError\)[^;]*?ProfileUseState::NotInUse[\s\S]*?WriteDurableMarkerAtomically') 'A legacy Chromium binding marker can be written without a fresh exact-root OS browser-use check.'
    Assert-ReleaseCheck ($resolveStandardProfileBody -notmatch 'ProbeClientProfilesInUse\(clientName') 'Legacy-root binding incorrectly blocks an independent browser slot already open for the same client.'
}
Assert-ReleaseCheck ($source -match 'NormalizeLegacyZipStaging[\s\S]*?FILE_ATTRIBUTE_REPARSE_POINT[\s\S]*?FILE_ATTRIBUTE_DIRECTORY[\s\S]*?CtBackup::Detail::IsValidClientDirectoryName') 'Legacy ZIP restore does not require every direct Sites item to be a safe, valid client directory.'
Assert-ReleaseCheck ($source -match 'COM\(\?:\[1-9\]\|\\\\u00B9\|\\\\u00B2\|\\\\u00B3\)[\s\S]*?LPT\(\?:\[1-9\]\|\\\\u00B9\|\\\\u00B2\|\\\\u00B3\)') 'Legacy ZIP entry validation does not reject Windows superscript COM/LPT device aliases.'
Assert-ReleaseCheck ($inProcSource -match 'suffix==L''\\x00B9''[\s\S]*?suffix==L''\\x00B2''[\s\S]*?suffix==L''\\x00B3''') 'In-process 7z path validation does not reject Windows superscript COM/LPT device aliases.'
Assert-ReleaseCheck ($source -match 'labelLength\s*>\s*63') 'Favicon domain labels are not strictly validated.'
Assert-ReleaseCheck ($source -match 'GetCurrentExecutablePath') 'Executable-path discovery can silently truncate long paths.'
Assert-ReleaseCheck ($source -match 'CreateProcessW\(exePath\.c_str\(\)') 'Browser launch does not supply an explicit application path.'
Assert-ReleaseCheck ($source -match 'EnumClientWindowsCallback[\s\S]*?GetWindow\(hWnd,\s*GW_OWNER\)[\s\S]*?WS_EX_TOOLWINDOW\s*\|\s*WS_EX_NOACTIVATE') 'Browser-owned autofill and suggestion popups can be mistaken for client windows.'
Assert-ReleaseCheck $clientWindowCallbackBodyMatch.Success 'The client-window enumeration callback could not be inspected.'
if ($clientWindowCallbackBodyMatch.Success) {
    $clientWindowCallbackBody = $clientWindowCallbackBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($clientWindowCallbackBody -match 'GetWindowThreadProcessId\(hWnd,\s*&processId\)' -and $clientWindowCallbackBody -match 'search->pid\s*!=\s*0\s*&&\s*processId\s*==\s*search->pid' -and $clientWindowCallbackBody -notmatch 'clientName|GetWindowTextW|_wcsicmp') 'Browser-window discovery can fall back from the retained process ID to a title or client-name guess.'
}
Assert-ReleaseCheck ($source -match 'FindClientWindows\(DWORD\s+pid,[^;{}]*\)\s*\{[\s\S]*?\(void\)clientName;[\s\S]*?\(void\)browser;[\s\S]*?ClientWindowSearch\s+search\{pid,\s*\{\}\};[\s\S]*?EnumWindows\(EnumClientWindowsCallback') 'Client-window discovery is not driven solely by the retained browser process ID.'
Assert-ReleaseCheck $openClientFolderBodyMatch.Success 'The client-folder shell action could not be inspected.'
if ($openClientFolderBodyMatch.Success) {
    $openClientFolderBody = $openClientFolderBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($openClientFolderBody -match 'const\s+fs::path\s+target\s*=\s*GetProfileDirFromName\(name\)' -and $openClientFolderBody -match 'target\.empty\(\)\s*\|\|\s*!IsSafeExistingDirectory\(target\)[\s\S]*?return;' -and $openClientFolderBody -notmatch 'Sites') 'Opening a client folder can silently fall back from an invalid selection to another directory.'
    Assert-ReleaseCheck ($openClientFolderBody -match 'if\s*\(\s*!ShellExecuteExW\(&executeInfo\)\s*\|\|[\s\S]*?executeInfo\.hInstApp\)\s*<=\s*32\s*\)[\s\S]*?MessageBoxW') 'Opening a client folder can report success without checking the shell launch result.'
}
Assert-ReleaseCheck $configMenuBodyMatch.Success 'The configuration-menu enablement function could not be inspected.'
if ($configMenuBodyMatch.Success) {
    $configMenuBody = $configMenuBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($configMenuBody -match 'hasExistingClient\s*=\s*\r?\n?\s*hasSelection\s*&&\s*IsExistingClientProfile\(name\)\s*&&\s*\r?\n?\s*!IsClientArchived\(name\)' -and $configMenuBody -match 'selectedClientIsClosed\s*=\s*\r?\n?\s*hasExistingClient\s*&&\s*!IsClientActive\(name\)') 'Configuration actions are not based on a visible existing client and a proven closed state.'
    foreach ($commandId in @('IDM_CTX_RENAME_PROFILE', 'IDM_CTX_ARCHIVE_PROFILE', 'IDM_CTX_RESET_PROFILE', 'IDM_CTX_VACUUM_PROFILE', 'IDM_CTX_DELETE_PROFILE')) {
        Assert-ReleaseCheck ($configMenuBody -match ('en\(' + [regex]::Escape($commandId) + ',\s*selectedClientIsClosed\)')) "The $commandId menu action is not fail-closed for missing, archived, or active clients."
    }
    Assert-ReleaseCheck ($configMenuBody -match 'en\(IDM_CTX_SET_PROFILE_ICON,\s*hasExistingClient\)' -and $configMenuBody -match 'en\(IDM_CTX_CREATE_SHORTCUT,\s*hasExistingClient\)' -and $configMenuBody -match 'en\(IDM_CTX_FETCH_ICON,\s*hasExistingClient\)' -and $configMenuBody -match 'en\(IDM_CTX_REMOVE_ICON,\s*hasCustomIcon\)') 'Client icon and shortcut menu actions can be enabled for a missing, archived, or unsafe client.'
}
Assert-ReleaseCheck ($source -match 'ApplyWindowIcons[\s\S]*?if\s*\(!forceNotify\s*&&\s*!iconChanged\)\s*\r?\n\s*return\s+iconsApplied;') 'Unchanged browser icons can still force repeated frame redraws.'
Assert-ReleaseCheck ($source -match 'WatcherThread[\s\S]*?assigned->second\.signature\s*!=\s*identitySignature[\s\S]*?const\s+bool\s+iconsApplied\s*=\s*ApplyWindowIcons\(ew,\s*hSmall,\s*hBig\)[\s\S]*?const\s+bool\s+identityApplied\s*=[\s\S]*?if\s*\(iconsApplied\s*&&\s*identityApplied\)\s*\r?\n\s*assignedWindowIdentities\[ew\]\s*=\s*expectedIdentity') 'The watcher can cache a browser identity that was not fully applied, preventing transient failures from being retried.'
Assert-ReleaseCheck ($source -match 'sleep_for\(std::chrono::milliseconds\(500\)\)') 'The browser watcher polling interval has returned to the disruptive 200 ms loop.'
Assert-ReleaseCheck ($source -match 'IDC_BTN_RESTORE_TABS') 'The Restore tabs footer control is missing.'
Assert-ReleaseCheck ($source -match 'DrawRestoreTabsToggle') 'The Restore tabs control no longer follows the themed owner-draw path.'
Assert-ReleaseCheck ($source -match '#include\s+"ConfigPersistence\.h"' -and $appProjectText -match '<ClCompile\s+Include="ConfigPersistence\.cpp"\s*/>' -and $appProjectText -match '<ClInclude\s+Include="ConfigPersistence\.h"\s*/>') 'The atomic configuration persistence module is not integrated into the application build.'
Assert-ReleaseCheck ($source -match 'static\s+bool\s+SaveConfigMutations\([^;{}]*\)\s*\{[\s\S]*?config_persistence::ApplyIniMutationsAtomically\(\s*g_sConfigPath,\s*mutations,\s*&details\)') 'Saved settings do not route through the atomic configuration mutation helper.'
Assert-ReleaseCheck $saveRestoreTabsBodyMatch.Success 'The Restore tabs persistence function could not be inspected.'
if ($saveRestoreTabsBodyMatch.Success) {
    $saveRestoreTabsBody = $saveRestoreTabsBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($saveRestoreTabsBody -match 'std::vector<config_persistence::IniMutation>' -and $saveRestoreTabsBody -match '\{L"restore_tabs",\s*std::nullopt,\s*std::nullopt\}' -and $saveRestoreTabsBody -match 'disabled_count' -and $saveRestoreTabsBody -match 'disabled_client' -and $saveRestoreTabsBody -match 'SaveConfigMutations\(L"Restore tabs preference",\s*mutations\)') 'Per-client Restore tabs choices are not committed as one atomic section replacement.'
    Assert-ReleaseCheck ($saveRestoreTabsBody -notmatch 'WritePrivateProfileStringW') 'Restore tabs persistence bypasses the shared atomic configuration transaction.'
}
Assert-ReleaseCheck ($configPersistenceHeader -match 'ApplyIniMutationsAtomically') 'The configuration persistence interface does not expose atomic multi-key mutation.'
Assert-ReleaseCheck $prepareUnicodeIniStageBodyMatch.Success 'The staged-configuration Unicode preparation helper could not be inspected.'
if ($prepareUnicodeIniStageBodyMatch.Success) {
    $prepareUnicodeIniStageBody =
        $prepareUnicodeIniStageBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck `
        ($prepareUnicodeIniStageBody -match '0xFF\s*&&\s*original\[1\]\s*==\s*0xFE[\s\S]*?0x00\s*&&\s*original\[3\]\s*==\s*0x00' -and
         $prepareUnicodeIniStageBody -match '0x00\s*&&\s*original\[1\]\s*==\s*0x00[\s\S]*?0xFE\s*&&\s*original\[3\]\s*==\s*0xFF' -and
         $prepareUnicodeIniStageBody -match 'unsupported UTF-32' -and
         $prepareUnicodeIniStageBody -match '\(original\.size\(\)\s*-\s*2\)\s*%\s*sizeof\(wchar_t\)\s*!=\s*0' -and
         $prepareUnicodeIniStageBody -match 'original\[index\]\s*==\s*0x00\s*&&\s*original\[index\s*\+\s*1\]\s*==\s*0x00' -and
         $prepareUnicodeIniStageBody -match 'sourceCodePage\s*=\s*CP_ACP' -and
         $prepareUnicodeIniStageBody -match '0xEF[\s\S]*?0xBB[\s\S]*?0xBF[\s\S]*?sourceCodePage\s*=\s*CP_UTF8' -and
         ([regex]::Matches($prepareUnicodeIniStageBody, 'MultiByteToWideChar\(\s*\r?\n?\s*sourceCodePage,\s*MB_ERR_INVALID_CHARS').Count -ge 2) -and
         $prepareUnicodeIniStageBody -match 'converted\.find\(L''\\0''\)\s*!=\s*std::wstring::npos' -and
         $prepareUnicodeIniStageBody -match 'unicodeBom\[\]\s*=\s*\{0xFF,\s*0xFE\}[\s\S]*?WriteAll\(file,\s*unicodeBom') `
        'Staged configuration migration no longer rejects UTF-32, odd UTF-16, embedded NULs, and invalid byte sequences while normalizing ANSI/UTF-8 BOM input to UTF-16LE BOM.'
}
Assert-ReleaseCheck $mutateIniFileBodyMatch.Success 'The atomic INI mutation helper could not be inspected for Unicode preparation ordering.'
if ($mutateIniFileBodyMatch.Success) {
    $mutateIniFileBody = $mutateIniFileBodyMatch.Groups['body'].Value
    $prepareUnicodeStageOffset = $mutateIniFileBody.IndexOf(
        'PrepareUnicodeIniStage(stagedPath, errorDetails)',
        [StringComparison]::Ordinal
    )
    $invokeIniMutationOffset = $mutateIniFileBody.IndexOf(
        'mutated = mutation(stagedPath, mutationError)',
        [StringComparison]::Ordinal
    )
    Assert-ReleaseCheck `
        ($prepareUnicodeStageOffset -ge 0 -and
         $invokeIniMutationOffset -gt $prepareUnicodeStageOffset) `
        'The staged INI can be mutated before its encoding is safely normalized to UTF-16LE.'
}
Assert-ReleaseCheck ($configPersistenceSource -match 'parentPath\s*/\s*\r?\n?\s*std::format\(L"\.\{\}\.ctspaces-' -and $configPersistenceSource -match 'CreateFileW\([\s\S]*?stagedPath\.c_str\(\)[\s\S]*?CREATE_NEW' -and $configPersistenceSource -match 'mutation\(stagedPath,\s*mutationError\)' -and $configPersistenceSource -match 'FlushRegularFile\(stagedPath,\s*errorDetails\)' -and $configPersistenceSource -match 'MoveFileExW\(stagedPath\.c_str\(\),\s*configPath\.c_str\(\),\s*\r?\n?\s*MOVEFILE_REPLACE_EXISTING\s*\|\s*MOVEFILE_WRITE_THROUGH\)') 'Configuration updates are not written to a unique sibling, flushed, and atomically committed.'
Assert-ReleaseCheck ($configPersistenceSource -match 'WritePrivateProfileStringW\(mutation\.section\.c_str\(\),\s*key,\s*value,\s*\r?\n?\s*stagedPath\.c_str\(\)\)' -and $configPersistenceSource -notmatch 'WritePrivateProfileStringW\([^;]*configPath\.c_str\(\)') 'INI mutations are not confined to the staged configuration file.'
Assert-ReleaseCheck $clearClientIconCacheBodyMatch.Success 'The client icon-cache invalidation function could not be inspected.'
if ($clearClientIconCacheBodyMatch.Success) {
    $clearClientIconCacheBody = $clearClientIconCacheBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($clearClientIconCacheBody -match 'lock_guard<std::mutex>\s+lifecycleLock\(g_watcherLifecycleMutex\)[\s\S]*?watcherQuiescent\s*=\s*!g_isWatcherRunning\.load\(\)[\s\S]*?RetireCachedIcons\([^;]*\)[\s\S]*?if\s*\(watcherQuiescent\)\s*\r?\n?\s*DestroyRetiredIconsForClientLocked' -and $clearClientIconCacheBody -notmatch 'IsClientActiveAnyBrowser') 'Client icon-cache invalidation can destroy HICONs while the watcher may still hold a raw handle from a stale active-profile snapshot.'
}
Assert-ReleaseCheck $toggleRestoreTabsBodyMatch.Success 'The Restore-tabs toggle function could not be inspected.'
if ($toggleRestoreTabsBodyMatch.Success) {
    $toggleRestoreTabsBody = $toggleRestoreTabsBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($toggleRestoreTabsBody -match 'originalPreferences\s*=\s*g_clientsWithoutTabRestore[\s\S]*?if\s*\(!SaveRestoreTabsPreferences\(\)\)[\s\S]*?g_clientsWithoutTabRestore\s*=\s*std::move\(originalPreferences\)') 'A failed Restore-tabs configuration commit leaves the live preference vector mutated.'
}
Assert-ReleaseCheck $togglePinBodyMatch.Success 'The client pin toggle function could not be inspected.'
if ($togglePinBodyMatch.Success) {
    $togglePinBody = $togglePinBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($togglePinBody -match 'originalPinnedClients\s*=\s*g_pinnedClients[\s\S]*?if\s*\(!SavePinnedClients\(\)\)\s*\r?\n?\s*g_pinnedClients\s*=\s*originalPinnedClients') 'A failed pin/unpin configuration commit leaves the live pinned-client vector mutated.'
}
Assert-ReleaseCheck $finishPinnedDragBodyMatch.Success 'The pinned-client drag persistence helper could not be inspected.'
if ($finishPinnedDragBodyMatch.Success) {
    $finishPinnedDragBody = $finishPinnedDragBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($finishPinnedDragBody -match 'snapshotValid\s*=\s*g_bPinnedDragSnapshotValid[\s\S]*?if\s*\(SavePinnedClients\(\)\)[\s\S]*?if\s*\(!snapshotValid\)[\s\S]*?g_pinnedClients\s*=\s*std::move\(originalPinnedClients\)') 'A failed pinned drag-order commit does not restore the pre-drag live order.'
}
Assert-ReleaseCheck ($source -match 'kind\s*==\s*MainDragKind::PinnedClient[\s\S]*?g_pinnedClientsBeforeDrag\s*=\s*g_pinnedClients[\s\S]*?g_bPinnedDragSnapshotValid\s*=\s*true' -and $source -match 'case\s+WM_CAPTURECHANGED:[\s\S]*?FinishPinnedDragPersistence\(savePinnedOrder\)' -and $source -match 'static\s+bool\s+FinishMainDrag\([^;{}]*\)\s*\{[\s\S]*?FinishPinnedDragPersistence\(savePinnedOrder\)') 'Pinned drag setup, normal completion, and capture-loss completion are not all routed through rollback-capable persistence.'
Assert-ReleaseCheck $qaReorderPinnedBodyMatch.Success 'The QA pinned-client reorder handler could not be inspected.'
if ($qaReorderPinnedBodyMatch.Success) {
    $qaReorderPinnedBody = $qaReorderPinnedBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($qaReorderPinnedBody -match 'originalPinnedClients\s*=\s*g_pinnedClients[\s\S]*?if\s*\(!SavePinnedClients\(\)\)[\s\S]*?g_pinnedClients\s*=\s*originalPinnedClients[\s\S]*?return\s+FALSE') 'A failed QA pinned reorder commit leaves the live pinned-client vector mutated.'
}
Assert-ReleaseCheck ($source -match 'ShowPinnedClientOptionsMenu[\s\S]*?L"Select client"[\s\S]*?L"Restore tabs"') 'Pinned clients have no route for selecting or changing Restore tabs before launch.'
Assert-ReleaseCheck ($source -match 'MovePinnedClientForDrag[\s\S]*?SavePinnedClients\(\)') 'Pinned clients cannot be reordered and persisted by dragging.'
Assert-ReleaseCheck ($source -match 'MoveSessionTabForDrag[\s\S]*?g_sessions\.erase[\s\S]*?g_sessions\.insert') 'Open client tabs cannot be reordered by dragging.'
Assert-ReleaseCheck ($source -match 'GetClipboardWebUrl[\s\S]*?IsValidWebUrl[\s\S]*?Open copied link') 'Pinned clients cannot safely open a copied website link.'
Assert-ReleaseCheck ($source -match 'CoCreateInstance\(CLSID_ShellLink[\s\S]*?SetIconLocation[\s\S]*?--client') 'Customer-icon desktop shortcuts are not created as client launchers.'
Assert-ReleaseCheck ($source -match 'IsManagedClientDesktopShortcut[\s\S]*?SameExecutablePath[\s\S]*?expectedArguments\s*==\s*argumentText') 'Safe rename can no longer distinguish a ctSpaces-created shortcut from an unrelated Desktop file.'
Assert-ReleaseCheck $managedClientShortcutVerifierBodyMatch.Success 'The managed client-shortcut verifier is not declared noexcept.'
if ($managedClientShortcutVerifierBodyMatch.Success) {
    $managedClientShortcutVerifierBody =
        $managedClientShortcutVerifierBodyMatch.Groups['body'].Value
    $managedTargetBufferOffset = $managedClientShortcutVerifierBody.IndexOf(
        'std::vector<wchar_t> targetText(32768', [StringComparison]::Ordinal
    )
    $managedArgumentBufferOffset = $managedClientShortcutVerifierBody.IndexOf(
        'std::vector<wchar_t> argumentText(32768', [StringComparison]::Ordinal
    )
    $managedShellLinkOffset = $managedClientShortcutVerifierBody.IndexOf(
        'IShellLinkW *link = nullptr', [StringComparison]::Ordinal
    )
    Assert-ReleaseCheck `
        ($managedClientShortcutVerifierBody -match 'try\s*\{[\s\S]*?catch\s*\(\.\.\.\)\s*\{[\s\S]*?return\s+false\s*;' -and
         $managedTargetBufferOffset -ge 0 -and
         $managedArgumentBufferOffset -ge 0 -and
         $managedShellLinkOffset -gt $managedTargetBufferOffset -and
         $managedShellLinkOffset -gt $managedArgumentBufferOffset -and
         $source -match 'IsManagedClientDesktopShortcutForClient\([^;{}]*\)\s*noexcept\s*\{') `
        'Managed shortcut verification can throw through recovery or allocate its 32K buffers after acquiring COM pointers.'
}
Assert-ReleaseCheck ($source -match 'IsDesktopDropPoint[\s\S]*?CreateClientDesktopShortcut\(desktopShortcutClient,\s*g_selectedBrowser\)') 'Dropping a pinned client on the Desktop no longer creates its one client-only shortcut for the selected browser.'
Assert-ReleaseCheck ($settingsShortcutCommandBodyMatch.Success -and $settingsShortcutCommandBodyMatch.Groups['body'].Value -match 'CreateClientDesktopShortcut\(clientName,\s*g_selectedBrowser\)' -and $settingsShortcutCommandBodyMatch.Groups['body'].Value -notmatch 'GetBrowserForCurrentSelection') 'Settings > Create Desktop Shortcut does not use the browser shown in the visible selector.'
Assert-ReleaseCheck ($source -match 'CreateClientDesktopShortcut\(const\s+std::wstring\s*&clientName,\s*\r?\n?\s*std::optional<BrowserKind>\s+browser') 'Client shortcuts cannot retain a strict selected-browser identity while recognizing historical browser-neutral launchers.'
Assert-ReleaseCheck $clientShortcutBodyMatch.Success 'The client Desktop shortcut function could not be inspected.'
if ($clientShortcutBodyMatch.Success) {
    $clientShortcutBody = $clientShortcutBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($clientShortcutBody -match '!IsExistingClientProfile\(clientName\)\s*\|\|\s*IsClientArchived\(clientName\)' -and $clientShortcutBody -match 'GetClientDesktopShortcutPath\(clientName\)' -and $clientShortcutBody -notmatch 'GetClientDesktopShortcutPath\(clientName,') 'Client shortcut creation is not confined to a visible existing client and the exact <Client>.lnk path.'
    Assert-ReleaseCheck ($clientShortcutBody -match 'if\s*\(browser\)\s*\{[\s\S]*?arguments\s*\+=\s*L" --browser "[\s\S]*?GetBrowserId\(\*browser\)' -and $clientShortcutBody -match 'if\s*\(SUCCEEDED\(result\)\s*&&\s*browser\)[\s\S]*?GetClientAppUserModelId\(clientName,\s*\*browser\)') 'The one client-only shortcut does not preserve the exact selected-browser argument and AppUserModelID.'
    Assert-ReleaseCheck ($clientShortcutBody -match 'shortcutAttributes\s*!=\s*INVALID_FILE_ATTRIBUTES\s*&&\s*!IsManagedClientDesktopShortcutForClient\(\*shortcutPath,\s*clientName\)[\s\S]*?left it unchanged' -and $clientShortcutBody -match 'TryMakeUniqueSiblingStagePath\(\*shortcutPath,\s*L"stage\.lnk"' -and $clientShortcutBody -match 'IsManagedClientDesktopShortcut\(stagingPath,\s*clientName,\s*browser\)' -and $clientShortcutBody -match 'ownsClient[\s\S]*?IsManagedClientDesktopShortcutForClient' -and $clientShortcutBody -match 'ownsSelectedBrowser[\s\S]*?IsManagedClientDesktopShortcut\(candidate,\s*clientName,[\s\S]*?browser\)' -and $clientShortcutBody -match 'CommitStagedOwnedShortcut\(stagingPath,\s*\*shortcutPath,\s*ownsClient,[\s\S]*?&ownsSelectedBrowser\)' -and $clientShortcutBody -match 'retireHistoricalShortcut[\s\S]*?GetClientDesktopShortcutCandidatePaths\([\s\S]*?candidateBrowser[\s\S]*?GetClientDesktopShortcutCandidatePaths\(clientName,\s*std::nullopt\)' -and $clientShortcutBody -match 'RemoveSafeStagingFile\(stagingPath\)' -and $clientShortcutBody -notmatch 'MOVEFILE_REPLACE_EXISTING') 'Client shortcuts are not safely retargeted at one same-client path, strictly reverified for the selected browser, cleaned of verified historical links, and protected from unrelated same-named files.'
}
Assert-ReleaseCheck `
    ($renameClientBodyMatch.Success -and
     $renameClientBodyMatch.Groups['body'].Value -match 'appendShortcutToRename[\s\S]*?GetClientDesktopShortcutCandidatePaths\(oldName,\s*std::nullopt\)' -and
     $renameClientBodyMatch.Groups['body'].Value -match 'oldCanonicalPath\s*=\s*GetClientDesktopShortcutPath\(oldName\)[\s\S]*?if\s*\(!preferredChosen\)[\s\S]*?browser\s*==\s*g_selectedBrowser' -and
     $renameClientBodyMatch.Groups['body'].Value -match 'newShortcutPath\s*=\s*GetClientDesktopShortcutPath\(newName\)[\s\S]*?QuarantineOwnedShortcut[\s\S]*?CreateClientDesktopShortcut\(\s*\r?\n?\s*newName,\s*browserToRecord[\s\S]*?if\s*\(readyToCreate\)[\s\S]*?for\s*\(size_t\s+index\s*=\s*0;\s*index\s*<\s*shortcutsToRename\.size\(\)') `
    'Rename does not deterministically collapse verified canonical and historical links to exactly one <NewClient>.lnk after a successful commit, including case-only rename.'
Assert-ReleaseCheck ($source -match 'WM_COPYDATA[\s\S]*?ProcessClientLaunchRequest') 'Client shortcuts cannot hand off to an already-running launcher.'
Assert-ReleaseCheck ($source -match 'RenameClientProfile[\s\S]*?MoveFileExW\(oldPath\.c_str\(\),\s*newPath\.c_str\(\)[\s\S]*?ReplaceClientName\(g_pinnedClients[\s\S]*?ReplaceClientName\(g_clientsWithoutTabRestore') 'Safe client rename does not migrate the profile and saved client preferences.'
Assert-ReleaseCheck ($source -match 'ArchiveClientProfile[\s\S]*?g_archivedClients\.push_back[\s\S]*?SaveArchivedClients\(\)') 'Archiving no longer preserves a hidden client record.'
Assert-ReleaseCheck ($source -match 'UpdateClientsComboBox[\s\S]*?!IsClientArchived\(clientName\)') 'Archived clients are not hidden from the normal client list.'
Assert-ReleaseCheck ($source -match 'RestoreArchivedClient[\s\S]*?g_archivedClients\.erase[\s\S]*?UpdateClientsComboBox\(\)') 'Archived clients cannot be restored to the normal client list.'
Assert-ReleaseCheck ($source -match 'client_title_first[\s\S]*?g_bClientTitleFirst[\s\S]*?GetBrowserPageTitle') 'The optional client-first browser title setting is missing.'
Assert-ReleaseCheck ($source -match 'constexpr\s+wchar_t\s+LAUNCHER_APP_USER_MODEL_ID\[\]\s*=\s*L"ctSpaces\.launcher"' -and $source.IndexOf('SetCurrentProcessExplicitAppUserModelID(LAUNCHER_APP_USER_MODEL_ID)') -ge 0 -and $source.IndexOf('SetCurrentProcessExplicitAppUserModelID(LAUNCHER_APP_USER_MODEL_ID)') -lt $source.IndexOf('if (!InitInstance(hInstance, nCmdShow))')) 'The launcher does not establish its stable explicit AppUserModelID before creating the main window.'
Assert-ReleaseCheck ($source -match 'SetWindowAppId\(\s*g_hGui,\s*LAUNCHER_APP_USER_MODEL_ID,\s*fs::path\(\),\s*\r?\n?\s*fs::path\(\),\s*false\)') 'The main launcher HWND AppUserModelID is not explicitly set and verified without a premature taskbar refresh.'
Assert-ReleaseCheck ($source -match 'const\s+bool\s+previousValue\s*=\s*g_bClientTitleFirst\.load\(\)[\s\S]*?if\s*\(!SaveClientTitlePreference\(\)\)\s*\{[\s\S]*?g_bClientTitleFirst\s*=\s*previousValue[\s\S]*?\}\s*else\s*\{[\s\S]*?EnsureWatcherIsRunning\(\)') 'The live title toggle does not rollback failed persistence and restart the watcher only after a successful save.'
Assert-ReleaseCheck ($browserTitleHeader -match 'edgeBrandSuffixes' -and $browserTitleHeader.Contains('L" - Microsoft Edge"') -and $browserTitleHeader.Contains('L" - Microsoft\u200BEdge"') -and $browserTitleHeader.Contains('L" \u2014 Mozilla Firefox"') -and $browserTitleHeader -match 'profileMarker\s*=\s*L" - Profile "' -and $browserTitleHeader -notmatch '[^\x00-\x7F]') 'Browser-title normalization does not use exact, encoding-independent Edge/Firefox suffixes while preserving numbered-profile stripping.'
Assert-ReleaseCheck ($sessionTabTextBodyMatch.Success -and $sessionTabTextBodyMatch.Groups['body'].Value -match 'const\s+Session\s*&session\s*=\s*g_sessions\[iSession\][\s\S]*?return\s+session\.clientName\s*;' -and $sessionTabTextBodyMatch.Groups['body'].Value -notmatch 'browser|GetBrowserDisplayName|\\u00B7') 'Session tabs do not return only the client name.'
Assert-ReleaseCheck ($source -match 'case\s+WM_APP_QA_REORDER_PINNED:[\s\S]*?if\s*\(!g_bQaInstance\)[\s\S]*?case\s+WM_APP_QA_ARCHIVE_CLIENT:') 'Workflow-only commands are not restricted to isolated QA instances.'
Assert-ReleaseCheck $launchProfileBodyMatch.Success 'The client launch function could not be inspected.'
if ($launchProfileBodyMatch.Success) {
    $launchProfileBody = $launchProfileBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($launchProfileBody -match 'ShouldRestoreTabsForClient\(clientName,\s*browser\)') 'Client launch ignores the browser-specific Restore tabs preference.'
    Assert-ReleaseCheck ($launchProfileBody -match 'SetBrowserStartupPreference\(profilePath,\s*restoreLastSession\)') 'Client launch does not persist both browser restore and normal-new-tab modes.'
    Assert-ReleaseCheck ($launchProfileBody -match 'StartBrowserProcess\(browser,\s*profilePath,[\s\S]*?processHandle,\s*executablePath,\s*\r?\n?\s*launchError\)') 'Client launch does not bind the browser kind, exact executable, profile, retained process handle, and launch error result.'
    Assert-ReleaseCheck ($launchProfileBody -match 'RemoveTransientProfileIfIdleAndVerify\(profilePath' -and [regex]::Matches($launchProfileBody, 'profileUseBlocksAction\(').Count -ge 2 -and $launchProfileBody -match 'profileUseBlocksAction\([\s\S]*?SetBrowserStartupPreference[\s\S]*?profileUseBlocksAction\([\s\S]*?StartBrowserProcess') 'Profile launch does not exact-probe before transient reset, preference mutation, and final process creation.'
}
Assert-ReleaseCheck $startBrowserBodyMatch.Success 'The browser process command builder could not be inspected.'
if ($startBrowserBodyMatch.Success) {
    $startBrowserBody = $startBrowserBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($startBrowserBody -match 'if\s*\(restoreLastSession\)[\s\S]*?--restore-last-session') 'Restore tabs On no longer supplies the browser restore switch.'
    Assert-ReleaseCheck ($startBrowserBody -match '--disable-background-mode') 'Closing the last browser window can leave the client profile running in background mode.'
    Assert-ReleaseCheck ($startBrowserBody -match '--new-tab[\s\S]*?QuoteCommandLineArgument\(startupUrl\)') 'Copied URLs are not safely quoted for browser launch.'
    Assert-ReleaseCheck ($startBrowserBody -match '--profile[\s\S]*?--no-remote\s+--new-instance[\s\S]*?--new-window') 'Firefox does not use its official isolated-profile launch switches.'
    Assert-ReleaseCheck ($startBrowserBody -notmatch '--allow-downgrade') 'Firefox launch enables unsafe profile downgrade compatibility.'
}
Assert-ReleaseCheck $startupPreferenceBodyMatch.Success 'The non-destructive browser startup preference function could not be inspected.'
if ($startupPreferenceBodyMatch.Success) {
    $startupPreferenceBody = $startupPreferenceBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($startupPreferenceBody -notmatch 'remove_all|Current Session|Last Session|Sessions') 'Restore tabs Off deletes saved session files.'
    Assert-ReleaseCheck ($startupPreferenceBody -match 'browser_preferences::UpdateStartupPreferencesFile\([\s\S]*?restoreTabs,\s*true\)') 'Browser startup preferences do not use the validated JSON utility with required session preference insertion.'
}
Assert-ReleaseCheck $closeAllBodyMatch.Success 'The coordinated close-all function could not be inspected.'
if ($closeAllBodyMatch.Success) {
    Assert-ReleaseCheck ($closeAllBodyMatch.Groups['body'].Value -match 'RequestSessionShutdown\(pid\)') 'Main-window exit still uses the browser window-close path.'
    Assert-ReleaseCheck ($closeAllBodyMatch.Groups['body'].Value -notmatch 'RequestSessionClose\(pid\)') 'Main-window exit routes clients through ordinary browser close prompts.'
}
Assert-ReleaseCheck $sessionShutdownBodyMatch.Success 'The clean browser shutdown function could not be inspected.'
if ($sessionShutdownBodyMatch.Success) {
    Assert-ReleaseCheck ($sessionShutdownBodyMatch.Groups['body'].Value -match 'WM_QUERYENDSESSION[\s\S]*?WM_ENDSESSION') 'Confirmed app exit does not use the Chromium clean no-prompt session-ending path.'
}
Assert-ReleaseCheck ($source -match 'kBrowserShutdownGraceMs\s*=\s*8000') 'Confirmed app exit has no bounded fallback for a hung browser.'
Assert-ReleaseCheck ($source -match 'std::jthread\s+g_launchThread[\s\S]*?std::atomic<bool>\s+g_isLaunchInFlight') 'Profile launch work is not explicitly tracked by a managed worker.'
Assert-ReleaseCheck ($source -match 'case\s+WM_CLOSE:[\s\S]*?g_isLaunchInFlight\.load\(\)[\s\S]*?g_bClosePendingDuringLaunch\s*=\s*true[\s\S]*?case\s+WM_DESTROY:') 'The main window can close while a profile launch is still in flight.'
Assert-ReleaseCheck ($source -match 'case\s+WM_APP_TASK_COMPLETE:[\s\S]*?StopLaunchWorker\(\)[\s\S]*?g_bClosePendingDuringLaunch[\s\S]*?PostMessageW\(hWnd,\s*WM_CLOSE') 'Deferred main-window close is not replayed after launch completion.'
Assert-ReleaseCheck ($source -match 'static\s+bool\s+PostOwnedStringMessage\([^;{}]*\)\s*\{[\s\S]*?std::make_unique<std::wstring>[\s\S]*?catch\s*\(\.\.\.\)[\s\S]*?for\s*\(;;\)[\s\S]*?const\s+HWND\s+notifyWindow\s*=\s*g_hGui[\s\S]*?!notifyWindow\s*\|\|\s*!IsWindow\(notifyWindow\)[\s\S]*?PostMessageW\([^;]*?payload\.get\(\)[\s\S]*?payload\.release\(\);[\s\S]*?return\s+true;[\s\S]*?!retryWhileWindowExists[\s\S]*?return\s+false;') 'The lifecycle payload helper does not retain ownership through allocation, target-window, and PostMessage failure/retry.'
Assert-ReleaseCheck ($source -match 'sessionAnnounced\s*=\s*PostOwnedStringMessage\(\s*WM_APP_SESSION_STARTED[\s\S]*?if\s*\(\s*!sessionAnnounced\s*\)[\s\S]*?launchFailed\s*=\s*true' -and $source -match 'PostOwnedStringMessage\(WM_APP_LAUNCH_FAILED,\s*0,\s*failureMessage,\s*\r?\n?\s*true\)' -and $source -match 'static\s+bool\s+DeliverTaskComplete\([^;{}]*\)\s*\{[\s\S]*?PostTaskComplete\(name\)[\s\S]*?SendMessageTimeoutW\([^;]*?WM_APP_TASK_COMPLETE[\s\S]*?for\s*\(;;\)[\s\S]*?PostTaskComplete\(name\)' -and $source -match 'g_isLaunchInFlight\s*=\s*false;\s*\r?\n\s*DeliverTaskComplete\(name\)') 'Launch start, failure, and completion messages do not use checked ownership-safe delivery with a UI recovery fallback.'
Assert-ReleaseCheck ($source -match 'ReaperThread\(DWORD\s+pid,[\s\S]*?HANDLE\s+processHandle\)[\s\S]*?WaitForSingleObject\(processHandle,\s*INFINITE\)[\s\S]*?waitResult\s*==\s*WAIT_OBJECT_0[\s\S]*?FinalizeProfileExit') 'The browser reaper does not require confirmed exit from the retained process handle.'
Assert-ReleaseCheck ($source -notmatch 'ReaperThread\(DWORD\s+pid,[\s\S]{0,500}?OpenProcess\(SYNCHRONIZE') 'The browser reaper still treats a failed PID reopen as process exit.'
Assert-ReleaseCheck ($source -match 'std::vector<ManagedReaperWorker>\s+g_reaperThreads[\s\S]*?StartReaperThread[\s\S]*?std::jthread[\s\S]*?StopReaperThreads') 'Browser reapers are not owned as managed jthreads.'
Assert-ReleaseCheck ($source -match 'StopLaunchWorker\(\);\s*StopShutdownWorker\(\);\s*StopReaperThreads\(\);\s*StopWatcher\(\);') 'Browser reapers are not joined before global GUI/watcher teardown.'
Assert-ReleaseCheck ($source -notmatch '\.detach\s*\(') 'A detached worker can still outlive application globals.'
Assert-ReleaseCheck ($source -match 'StartReaperThread\(pid,\s*name,\s*browser,\s*profileType,[\s\S]*?else\s*\{[\s\S]*?launchFailed\s*=\s*true' -and $source -match 'if\s*\(launchFailed\)[\s\S]*?TryTerminateProcessHandle\(processHandle\)[\s\S]*?CloseHandle\(processHandle\)[\s\S]*?activeRegistered\s*&&\s*processStopped[\s\S]*?PostProfileExitMessage\(\s*pid,\s*name,\s*browser,\s*profileType,\s*true,\s*sessionAnnounced\)[\s\S]*?if\s*\(!profileExitHandedOff\)[\s\S]*?remains marked open') 'Reaper construction failure does not terminate the browser and hand lifecycle ownership back to the UI without prematurely unmarking the profile.'
Assert-ReleaseCheck ($source -match 'TryTerminateProcessHandle\(HANDLE\s+processHandle\)[\s\S]*?TerminateProcess\(processHandle,\s*0\)[\s\S]*?WaitForSingleObject\([\s\S]*?terminationRequested\s*\?[\s\S]*?kTerminationFailureRaceWaitMs[\s\S]*?finalWait\s*==\s*WAIT_OBJECT_0') 'Process termination does not re-wait the retained handle across the exit race.'
Assert-ReleaseCheck (($source -match 'retryProcessIds[\s\S]*?PostMessageW\([^;]*WM_APP_PROFILE_SHUTDOWN_FAILED') -and ($source -match 'case\s+WM_APP_PROFILE_SHUTDOWN_FAILED:[\s\S]*?g_bExitWhenProfilesClose\s*=\s*false[\s\S]*?SetUiState\(true\)')) 'Coordinated shutdown cannot recover the UI when browser process handles remain unavailable.'
Assert-ReleaseCheck ($source -match 'enum\s+class\s+BrowserKind[\s\S]*?Edge[\s\S]*?Chrome[\s\S]*?Brave[\s\S]*?Firefox') 'The four supported browser identities are not explicit.'
Assert-ReleaseCheck ($source -match 'struct\s+ClientBrowserKey[\s\S]*?std::map<ClientBrowserKey,\s*ActiveProfileInfo') 'Active profiles are not keyed by client and browser.'
Assert-ReleaseCheck ($source -match 'ctSpaces-client-schema=2[\s\S]*?ctSpaces-browser-schema=2[\s\S]*?Browsers') 'Durable v2 client and browser-slot schema markers are missing.'
Assert-ReleaseCheck ($source -match 'static\s+const\s+char\s*\*GetBrowserIdAscii[\s\S]*?"edge"[\s\S]*?"chrome"[\s\S]*?"brave"[\s\S]*?"firefox"' -and $source -match 'GetBrowserMarkerText\(BrowserKind\s+browser\)[\s\S]*?GetBrowserIdAscii\(browser\)' -and $source -match 'GetLegacyBrowserBindingText\(BrowserKind\s+browser\)[\s\S]*?GetBrowserIdAscii\(browser\)') 'Durable browser markers do not use the explicit narrow browser-ID mapping.'
Assert-ReleaseCheck ($source -match 'value\.size\(\)\s*==\s*1[\s\S]*?value\[0\]\s*>=\s*L''0''[\s\S]*?value\[0\]\s*<=\s*L''3''[\s\S]*?static_cast<BrowserKind>\(value\[0\]\s*-\s*L''0''\)') 'Legacy numeric browser IDs are narrowed without explicit validated conversion.'
Assert-ReleaseCheck ($source -match 'ResolveStandardBrowserProfile[\s\S]*?kLegacyBrowserBindingMarkerName[\s\S]*?GetLegacyBrowserBindingText[\s\S]*?profileRoot\s*=\s*clientRoot') 'Legacy Chromium roots are not bound in place without moving their profile data.'
Assert-ReleaseCheck ($source -match 'CreateNewV2Client[\s\S]*?_ProfileCreate[\s\S]*?MoveFileExW\(stagedClient\.c_str\(\),\s*targetRoot\.c_str\(\)') 'New v2 clients are not staged and atomically committed.'
Assert-ReleaseCheck ($source -match 'CreateStagedBrowserSlot[\s\S]*?IsChromiumBrowser\(browser\)[\s\S]*?extDef\(profileRoot[\s\S]*?SetFirefoxStartupPreference\(profileRoot,\s*false\)') 'Firefox slots are not cleanly separated from Chromium starter extraction.'
Assert-ReleaseCheck ($source -match 'browser\s*==\s*BrowserKind::Firefox[\s\S]*?did not start a second process') 'Active Firefox profiles can receive an unsafe second process.'
Assert-ReleaseCheck ($source -match 'GetClientAppUserModelId[\s\S]*?kMaximumSlugLength[\s\S]*?case-insensitive[\s\S]*?GetBrowserId\(browser\)') 'Browser-specific AppUserModelIDs are not bounded, normalized, and hashed.'
Assert-ReleaseCheck ($source -match 'GetBoundedFileContentHash[\s\S]*?ctSpaces-taskbar-\{:\s*016X\}[\s\S]*?CleanupObsoleteTaskbarIconResources') 'Taskbar icon resources are not content-keyed and boundedly cleaned.'
Assert-ReleaseCheck ($source -match 'std::map<std::wstring,\s*std::vector<HICON>>\s+g_retiredIconHandles' -and $source -match 'observedWindowIdentities\[ew\]\s*=\s*expectedIdentity' -and $source -match 'observed\s*==\s*observedWindowIdentities\.end\(\)[\s\S]*?clientIconAssignmentsCurrent\[it->second\.clientName\]\s*=\s*false' -and $source -match 'readiness->second[\s\S]*?DestroyRetiredIconsForClientLocked') 'Retired client icons can be destroyed before a watcher pass proves every live assigned window has the current identity.'
Assert-ReleaseCheck ($source -match 'IsManagedClientDesktopShortcutForClient[\s\S]*?IsSafeExistingRegularFile\(shortcutPath\)[\s\S]*?A different or unsafe shortcut already uses this name') 'Shortcut creation can overwrite an unrelated or reparse Desktop item.'
Assert-ReleaseCheck ($source -notmatch 'const\s+fs::path\s+resetRoot\s*=\s*g_sDataDir\s*/\s*L"_ResetWork"') 'The unreachable legacy whole-client reset implementation remains in source.'
$mutexOffset = $source.IndexOf('CreateMutexW(NULL, TRUE, mutexName.c_str())', [StringComparison]::Ordinal)
$configOffset = $source.IndexOf('g_sConfigPath = g_sDataDir / L"config.ini"', [StringComparison]::Ordinal)
Assert-ReleaseCheck ($mutexOffset -ge 0 -and $configOffset -gt $mutexOffset) 'The single-instance mutex is not acquired before configuration/profile inspection.'
Assert-ReleaseCheck ($source -match 'CreateMutexW\(NULL,\s*TRUE,\s*mutexName\.c_str\(\)\)[\s\S]*?if\s*\(!hMutex\)[\s\S]*?single-instance guard') 'CreateMutex failure does not fail closed with a useful startup error.'
Assert-ReleaseCheck ($source -match 'isQaInstance\s*&&\s*mutexAlreadyExists[\s\S]*?unique --qa-instance') 'QA single-instance isolation is not explicit.'
Assert-ReleaseCheck ($source -match 'GetExistingInstanceExitCode[\s\S]*?GetProcessImagePath[\s\S]*?SameExecutablePath[\s\S]*?SendMessageTimeoutW[\s\S]*?if\s*\(!delivered\)') 'Existing-instance routing can exit without verified process identity and acknowledged IPC delivery.'
Assert-ReleaseCheck ($source -match 'GetProcessImagePath[\s\S]*?std::vector<wchar_t>[\s\S]*?ERROR_INSUFFICIENT_BUFFER[\s\S]*?32768') 'Existing-process image paths are still truncated to MAX_PATH.'
Assert-ReleaseCheck ($source -notmatch 'GetModuleFileNameW\(NULL,\s*[^,]+,\s*MAX_PATH\)') 'Install/update still uses an unchecked MAX_PATH executable lookup.'
Assert-ReleaseCheck ($source -match 'bool\s+chkUpdate\(\)[\s\S]*?GetCurrentExecutablePath\(\)[\s\S]*?fs::equivalent\(currentExePath,\s*installedExePath,\s*equivalentError\)') 'Update comparison does not use the dynamic executable path and guarded equivalence check.'
Assert-ReleaseCheck $installedExeValidationBodyMatch.Success 'The installed-executable path validator could not be inspected.'
if ($installedExeValidationBodyMatch.Success) {
    $installedExeValidationBody = $installedExeValidationBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($installedExeValidationBody -match 'IsSafeExistingDirectory\(g_sDataDir\)' -and $installedExeValidationBody -match 'NormalizePathForScope\(installedPath\.parent_path\(\)\)' -and $installedExeValidationBody -match 'IsDirectChildPath\(installedPath,\s*g_sDataDir\)' -and $installedExeValidationBody -match 'installedPath\.filename\(\)\.c_str\(\),\s*L"ctSpaces\.exe"' -and $installedExeValidationBody -match 'FILE_ATTRIBUTE_DIRECTORY\s*\|\s*FILE_ATTRIBUTE_REPARSE_POINT') 'Install/update does not constrain the destination to the exact regular ctSpaces.exe child of the safe application-data folder.'
}
Assert-ReleaseCheck $installCopyPathsBodyMatch.Success 'The install/update source-and-destination validator could not be inspected.'
if ($installCopyPathsBodyMatch.Success) {
    $installCopyPathsBody = $installCopyPathsBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($installCopyPathsBody -match 'GetCurrentExecutablePath\(\)' -and $installCopyPathsBody -match '!IsSafeExistingRegularFile\(sourcePath\)' -and $installCopyPathsBody -match '!SameExecutablePath\(sourcePath,\s*currentExecutable\)' -and $installCopyPathsBody -match 'ValidateInstalledExecutablePath\(destPath,\s*true' -and $installCopyPathsBody -match 'SameExecutablePath\(sourcePath,\s*destPath\)') 'Install/update does not bind the source to the current regular executable and reject an unsafe or identical destination.'
}
Assert-ReleaseCheck ($siblingStageHelperBodyMatch.Success -and $source -match 'FormatStageCollisionToken\([^;{}]*\)\s*\{[\s\S]*?guid\.Data1[\s\S]*?guid\.Data4\[7\]' -and $siblingStageHelperBodyMatch.Groups['body'].Value -match 'CoCreateGuid\(&seedGuid\)' -and $siblingStageHelperBodyMatch.Groups['body'].Value -match 'destination\.parent_path\(\)\s*/\s*component' -and $siblingStageHelperBodyMatch.Groups['body'].Value -match 'GetFileAttributesW\(stagingPath\.c_str\(\)\)[\s\S]*?ERROR_FILE_NOT_FOUND') 'Installer and shortcut staging paths are not bounded random siblings proven absent before use.'
Assert-ReleaseCheck ($shortcutSignatureComparisonBodyMatch.Success -and $shortcutSignatureComparisonBodyMatch.Groups['body'].Value -match 'volumeSerialNumber' -and $shortcutSignatureComparisonBodyMatch.Groups['body'].Value -match 'fileIndexHigh' -and $shortcutSignatureComparisonBodyMatch.Groups['body'].Value -match 'fileIndexLow' -and $shortcutSignatureComparisonBodyMatch.Groups['body'].Value -match 'fileSizeHigh' -and $shortcutSignatureComparisonBodyMatch.Groups['body'].Value -match 'fileSizeLow' -and $shortcutSignatureComparisonBodyMatch.Groups['body'].Value -match 'lastWriteTime' -and $shortcutSignatureComparisonBodyMatch.Groups['body'].Value -notmatch 'creationTime') 'Shortcut identity either treats tunneled creation time as immutable or no longer retains volume/file ID, size, and last-write race checks.'
Assert-ReleaseCheck (([regex]::Matches($workflowFeaturesQa, '\(0x8000 \+ 12\)').Count) -ge 3 -and $workflowFeaturesQa -match '\$alphaName\s*\+\s*''\.lnk''' -and $workflowFeaturesQa -match 'Microsoft Edge - ctSpaces\.lnk' -and $workflowFeaturesQa -match '\$alphaName\s*\+\s*'' - ctSpaces\.lnk''' -and $workflowFeaturesQa -match 'SetCreationTimeUtc\(\s*\r?\n?\s*\$alphaShortcutPath' -and $workflowFeaturesQa -match 'Invoke-AppCommand\s+42002' -and $workflowFeaturesQa -match '"--client \$alphaName --browser edge"' -and $workflowFeaturesQa -match '"--client \$alphaName --browser chrome"' -and $workflowFeaturesQa -match 'GetExpectedAppUserModelId\(\$alphaName,\s*''edge''\)' -and $workflowFeaturesQa -match 'GetExpectedAppUserModelId\(\$alphaName,\s*''chrome''\)' -and ([regex]::Matches($workflowFeaturesQa, 'GetShortcutAppId\(\$alphaShortcutPath\)').Count) -ge 2 -and $workflowFeaturesQa -match 'expectedTunneledCreationTime' -and $workflowFeaturesQa -match '\$unrelatedReplacementResult\s*-ne\s*\[UIntPtr\]::Zero' -and $workflowFeaturesQa -match 'Get-FileHash\s+-LiteralPath\s+\$betaShortcutPath' -and $workflowFeaturesQa -match '\$caseOnlyName\s*\+\s*''\.lnk''' -and $workflowFeaturesQa -match 'Archiving removed the client shortcut' -and $workflowFeaturesQa -match "-like '~\*\.lnk'") 'The workflow no longer proves exact client-only naming, verified legacy cleanup, exact selected-browser arguments/AppIDs, same-path Edge-to-Chrome retargeting, no-overwrite refusal, deterministic case-only/normal rename, archive retention, creation-time tunneling, and clean transaction residue.'
Assert-ReleaseCheck ($source -match 'MoveKnownRegularFileWithoutReplacement\([^;{}]*\)\s*\{[\s\S]*?OpenLockedMatchingRegularFile\(source,\s*expected[\s\S]*?renameInfo->ReplaceIfExists\s*=\s*FALSE[\s\S]*?SetFileInformationByHandle\(fileHandle,\s*FileRenameInfo[\s\S]*?ReadSafeRegularFileSignature\(fileHandle,\s*committedHandle[\s\S]*?SameSafeRegularFileSignature\(committedHandle,\s*expected\)[\s\S]*?CloseHandle\(fileHandle\)[\s\S]*?TryGetSafeRegularFileSignature\(destination,\s*committedPath[\s\S]*?SameSafeRegularFileSignature\(committedPath,\s*expected\)' -and $source -notmatch 'MoveFileExW\(source\.c_str\(\),\s*destination\.c_str\(\)') 'Shortcut quarantine/commit is not a handle-bound no-replace rename with handle verification before close and destination-path verification after close.'
Assert-ReleaseCheck ($source -match 'CopyExecutableToNewStage\([^;{}]*\)\s*\{[\s\S]*?FILE_FLAG_OPEN_REPARSE_POINT[\s\S]*?CREATE_NEW[\s\S]*?FlushFileBuffers\(staging\)' -and $source -match 'FilesMatchExactly\([^;{}]*\)\s*\{[\s\S]*?GetFileSizeEx\(left,[\s\S]*?leftSize\.QuadPart\s*==\s*rightSize\.QuadPart[\s\S]*?std::memcmp') 'The staged executable copy is not created exclusively, flushed, and compared byte-for-byte.'
Assert-ReleaseCheck $copyInstalledBodyMatch.Success 'The atomic installed-copy replacement function could not be inspected.'
if ($copyInstalledBodyMatch.Success) {
    $copyInstalledBody = $copyInstalledBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($copyInstalledBody -match 'ValidateInstallCopyPaths\(sourcePath,\s*destPath' -and $copyInstalledBody -match 'TryMakeUniqueSiblingStagePath\(destPath,\s*L"stage"' -and $copyInstalledBody -match 'CopyExecutableToNewStage\(sourcePath,\s*stagingPath' -and [regex]::Matches($copyInstalledBody, 'FilesMatchExactly\(sourcePath,').Count -ge 3) 'Install/update does not validate exact paths and byte-verify the staged and committed executable.'
    Assert-ReleaseCheck ($copyInstalledBody -match 'MoveFileExW\(stagingPath\.c_str\(\),\s*destPath\.c_str\(\),\s*\r?\n?\s*MOVEFILE_REPLACE_EXISTING\s*\|\s*MOVEFILE_WRITE_THROUGH\)' -and $copyInstalledBody -match 'ValidateInstalledExecutablePath\(destPath,\s*false' -and $copyInstalledBody -notmatch 'CopyFile(?:Ex)?W?\(') 'Install/update can bypass the same-volume atomic replacement and final destination verification.'
}
Assert-ReleaseCheck $launchInstalledBodyMatch.Success 'The installed-copy relaunch function could not be inspected.'
if ($launchInstalledBodyMatch.Success) {
    $launchInstalledBody = $launchInstalledBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($launchInstalledBody -match 'ValidateInstalledExecutablePath\(installedExePath,\s*false' -and $launchInstalledBody -match 'SEE_MASK_NOCLOSEPROCESS' -and $launchInstalledBody -match '!ShellExecuteExW\(&executeInfo\)\s*\|\|\s*!executeInfo\.hProcess' -and $launchInstalledBody -match 'WaitForSingleObject\(executeInfo\.hProcess,\s*0\)' -and $launchInstalledBody -match 'immediateState\s*!=\s*WAIT_TIMEOUT[\s\S]*?return\s+false;' -and $launchInstalledBody -match 'CloseHandle\(executeInfo\.hProcess\);\s*\r?\n\s*return\s+true;') 'Installed-copy relaunch can report success without a checked shell process handle and immediate-alive proof.'
}
Assert-ReleaseCheck $managedApplicationShortcutBodyMatch.Success 'The generic ctSpaces shortcut ownership check could not be inspected.'
if ($managedApplicationShortcutBodyMatch.Success) {
    $managedApplicationShortcutBody = $managedApplicationShortcutBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($managedApplicationShortcutBody -match 'IsSafeExistingRegularFile\(shortcutPath\)' -and $managedApplicationShortcutBody -match 'IsSafeExistingRegularFile\(targetPath\)' -and $managedApplicationShortcutBody -match 'GetPath' -and $managedApplicationShortcutBody -match 'GetArguments' -and $managedApplicationShortcutBody -match 'argumentText\[0\]\s*!=\s*L''\\0''' -and $managedApplicationShortcutBody -match 'SameExecutablePath\(targetText(?:\.data\(\))?,\s*targetPath\)' -and $managedApplicationShortcutBody -match 'GetValue\(PKEY_AppUserModel_ID' -and $managedApplicationShortcutBody -match 'appIdValue\.vt\s*==\s*VT_EMPTY[\s\S]*?LegacyWithoutAppId' -and $managedApplicationShortcutBody -match 'wcscmp\(currentAppId,\s*LAUNCHER_APP_USER_MODEL_ID\)\s*==\s*0[\s\S]*?ManagedApplicationShortcutState::Current') 'Generic shortcut ownership is not proven by a safe link, exact target, empty arguments, and either a missing legacy or exact launcher AppID.'
}
Assert-ReleaseCheck $genericShortcutBodyMatch.Success 'The generic ctSpaces shortcut creation function could not be inspected.'
if ($genericShortcutBodyMatch.Success) {
    $genericShortcutBody = $genericShortcutBodyMatch.Groups['body'].Value
    $setLauncherShortcutAppIdOffset = $genericShortcutBody.IndexOf('SetValue(PKEY_AppUserModel_ID', [StringComparison]::Ordinal)
    $saveLauncherShortcutOffset = $genericShortcutBody.IndexOf('persist->Save(stagingPath.c_str(), TRUE)', [StringComparison]::Ordinal)
    Assert-ReleaseCheck ($genericShortcutBody -match 'IsManagedApplicationShortcut\(shortcutPath,\s*targetPath,\s*true\)' -and $genericShortcutBody -match 'TryMakeUniqueSiblingStagePath\(shortcutPath,\s*L"stage\.lnk"' -and $setLauncherShortcutAppIdOffset -ge 0 -and $saveLauncherShortcutOffset -gt $setLauncherShortcutAppIdOffset -and $genericShortcutBody -match 'InitPropVariantFromString\(LAUNCHER_APP_USER_MODEL_ID' -and $genericShortcutBody -match 'IsManagedApplicationShortcut\(stagingPath,\s*targetPath,\s*false\)' -and $genericShortcutBody -match 'IsManagedApplicationShortcut\(candidate,\s*targetPath,\s*true\)' -and $genericShortcutBody -match 'IsManagedApplicationShortcut\(candidate,\s*targetPath,\s*false\)' -and $genericShortcutBody -match 'CommitStagedOwnedShortcut\(stagingPath,\s*shortcutPath,\s*isOwned,[\s\S]*?&isCommittedOwned\)' -and $genericShortcutBody -notmatch 'MOVEFILE_REPLACE_EXISTING') 'Generic setup shortcuts do not safely migrate only legacy links, persist the launcher AppID before staging, and require it on staged/final no-replace commits.'
}
Assert-ReleaseCheck ($source -match 'EvaluateShortcutOwnershipNoThrow\([^;{}]*\)\s*noexcept\s*\{[\s\S]*?try\s*\{[\s\S]*?return\s+check\(path\)\s*;[\s\S]*?catch\s*\(\.\.\.\)\s*\{[\s\S]*?return\s+false\s*;' -and $source -match 'CommitStagedOwnedShortcut\([^;{}]*committedOwnership[\s\S]*?EvaluateShortcutOwnershipNoThrow\(isCommittedOwned,\s*stagingPath\)[\s\S]*?EvaluateShortcutOwnershipNoThrow\(isCommittedOwned,\s*destination\)' -and $source -notmatch '(?:isOwned|isCommittedOwned)\s*\(') 'The shared shortcut transaction can throw through an ownership callback or cannot require a stricter staged/final identity than legacy rollback ownership.'
Assert-ReleaseCheck $setWindowAppIdBodyMatch.Success 'The window AppUserModelID writer could not be inspected.'
if ($setWindowAppIdBodyMatch.Success) {
    $setWindowAppIdBody = $setWindowAppIdBodyMatch.Groups['body'].Value
    $setRelaunchIconOffset = $setWindowAppIdBody.IndexOf('SetValue(PKEY_AppUserModel_RelaunchIconResource', [StringComparison]::Ordinal)
    $setWindowIdentityOffset = $setWindowAppIdBody.IndexOf('SetValue(PKEY_AppUserModel_ID', [StringComparison]::Ordinal)
    $commitWindowIdentityOffset = $setWindowAppIdBody.IndexOf('propertyStore->Commit()', [StringComparison]::Ordinal)
    $readWindowIdentityOffset = $setWindowAppIdBody.IndexOf('GetValue(PKEY_AppUserModel_ID', [StringComparison]::Ordinal)
    $readRelaunchIconOffset = $setWindowAppIdBody.IndexOf('GetValue(', $readWindowIdentityOffset + 1, [StringComparison]::Ordinal)
    Assert-ReleaseCheck ($setRelaunchIconOffset -ge 0 -and $setWindowIdentityOffset -gt $setRelaunchIconOffset -and $commitWindowIdentityOffset -gt $setWindowIdentityOffset -and $readWindowIdentityOffset -gt $commitWindowIdentityOffset -and $readRelaunchIconOffset -gt $readWindowIdentityOffset) 'Window relaunch/icon properties are not written before AppID notification and both properties are not read back after Commit.'
}
Assert-ReleaseCheck ($source -match 'enum\s+class\s+AutorunValueState\s*\{\s*Missing,\s*Managed,\s*Unrelated,\s*Indeterminate\s*\}') 'The autorun value does not use a fail-closed ownership state.'
Assert-ReleaseCheck $queryAutorunBodyMatch.Success 'The autorun ownership query could not be inspected.'
if ($queryAutorunBodyMatch.Success) {
    $queryAutorunBody = $queryAutorunBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($queryAutorunBody -match 'type\s*!=\s*REG_SZ' -and $queryAutorunBody -match 'byteCount\s*>\s*64\s*\*\s*1024' -and [regex]::Matches($queryAutorunBody, 'RegQueryValueExW\(').Count -ge 2 -and $queryAutorunBody -match 'actualValue\s*==\s*expectedValue\s*\?\s*AutorunValueState::Managed\s*\r?\n?\s*:\s*AutorunValueState::Unrelated') 'Autorun ownership is not based on a bounded, stable, exact REG_SZ value.'
}
Assert-ReleaseCheck $configureAutorunBodyMatch.Success 'The autorun configuration function could not be inspected.'
if ($configureAutorunBodyMatch.Success) {
    $configureAutorunBody = $configureAutorunBodyMatch.Groups['body'].Value
    Assert-ReleaseCheck ($configureAutorunBody -match 'L"\\\""\s*\+\s*installedExePath\.wstring\(\)\s*\+\s*L"\\\""' -and $configureAutorunBody -match 'enabled\s*&&\s*state\s*==\s*AutorunValueState::Unrelated[\s\S]*?return\s+false;' -and [regex]::Matches($configureAutorunBody, 'QueryAutorunValue\(').Count -ge 3) 'Autorun does not preserve unrelated same-name values or revalidate ownership at commit and verification time.'
    Assert-ReleaseCheck ($configureAutorunBody -match 'if\s*\(state\s*==\s*AutorunValueState::Missing\)[\s\S]*?RegSetValueExW' -and $configureAutorunBody -match 'if\s*\(state\s*==\s*AutorunValueState::Managed\)[\s\S]*?RegDeleteValueW' -and $configureAutorunBody -match 'state\s*!=\s*AutorunValueState::Missing[\s\S]*?return\s+finish\(false\)') 'Autorun can overwrite or delete an unrelated value, or report removal without verifying absence.'
    Assert-ReleaseCheck ($configureAutorunBody -match 'const\s+auto\s+finish[\s\S]*?RegCloseKey\(key\)[\s\S]*?return\s+false;') 'Autorun updates do not check that the registry key closes cleanly.'
}
Assert-ReleaseCheck ($source -match 'doInstall\(\)[\s\S]*?if\s*\(!LaunchInstalledVersion\(installedExePath,\s*&launchError\)\)[\s\S]*?This copy will continue running[\s\S]*?return\s+true;[\s\S]*?return\s+false;' -and $source -match 'chkUpdate\(\)[\s\S]*?if\s*\(LaunchInstalledVersion\(installedExePath,\s*&launchError\)\)\s*\r?\n\s*return\s+false;[\s\S]*?This copy will continue running[\s\S]*?return\s+true;') 'Setup/update can exit the current copy before proving the installed replacement is alive.'
Assert-ReleaseCheck ($source -match 'InputBoxWindow[\s\S]*?MSG\s+msg\{\}[\s\S]*?GetMessageW[\s\S]*?messageResult\s*==\s*0[\s\S]*?PostQuitMessage[\s\S]*?messageResult\s*==\s*-1') 'The nested InputBox message loop does not preserve WM_QUIT and distinguish GetMessageW errors.'
Assert-ReleaseCheck ($source -match 'wWinMain[\s\S]*?MSG\s+msg\{\}[\s\S]*?GetMessageW[\s\S]*?messageResult\s*==\s*0[\s\S]*?messageResult\s*==\s*-1[\s\S]*?messageLoopFailed[\s\S]*?return\s+exitCode') 'The main message loop does not distinguish clean quit from GetMessageW failure.'
Assert-ReleaseCheck ($source -match 'FAILED\(comResult\)[\s\S]*?ComApartmentCleanup[\s\S]*?gdiplusResult\s*!=\s*Gdiplus::Ok[\s\S]*?GdiplusCleanup[\s\S]*?!InitCommonControlsEx[\s\S]*?!windowClass[\s\S]*?!InitInstance') 'Startup subsystem failures are not checked and RAII-cleaned consistently.'
Assert-ReleaseCheck ($null -ne $appReleaseX64) 'The app Release|x64 build settings are missing.'
if ($null -ne $appReleaseX64) {
    Assert-ReleaseCheck ($appReleaseX64.ClCompile.ControlFlowGuard -eq 'Guard') 'The app Release|x64 compiler does not enable Control Flow Guard.'
    Assert-ReleaseCheck ($appReleaseX64.Link.AdditionalOptions -match '/GUARD:CF') 'The app Release|x64 linker does not enable Control Flow Guard.'
    Assert-ReleaseCheck ($appReleaseX64.Link.AdditionalOptions -match '/CETCOMPAT') 'The app Release|x64 linker does not mark CET compatibility.'
}
Assert-ReleaseCheck ($null -ne $sevenZipReleaseX64) 'The 7-Zip Release|x64 build settings are missing.'
if ($null -ne $sevenZipReleaseX64) {
    Assert-ReleaseCheck ($sevenZipReleaseX64.ClCompile.ControlFlowGuard -eq 'Guard') 'The 7-Zip Release|x64 compiler does not enable Control Flow Guard.'
}
Assert-ReleaseCheck ($source -match 'MAX_VISIBLE_PINNED_CLIENTS\s*=\s*4') 'The compact pinned-client row no longer exposes four clients.'
Assert-ReleaseCheck ($source -match 'GetPinnedClientRowRect\s*\(') 'The pinned label and client shortcuts no longer have separate rows.'
Assert-ReleaseCheck ($source -notmatch 'const\s+int\s+labelW\s*=\s*ScaleByDpi\(54') 'The Pinned label is taking horizontal space away from client shortcuts.'
Assert-ReleaseCheck ($source -match 'MAIN_GUI_HEIGHT_DIP\s*=\s*190') 'The compact launcher no longer leaves enough vertical space between its rows.'
Assert-ReleaseCheck ($source -match 'MAIN_GUI_EMPTY_PIN_HEIGHT_DIP\s*=\s*148') 'The empty pinned-client row is reserving vertical whitespace again.'
Assert-ReleaseCheck ($source -match 'GetMainGuiHeightDip[\s\S]*?g_pinnedClients\.empty\(\)') 'The launcher no longer switches between empty-pin and pinned heights.'
Assert-ReleaseCheck ($source -match 'GetClientComboIconRect\s*\(') 'The customer logo is no longer integrated with the editable client selector.'
Assert-ReleaseCheck ($source -match 'LayoutClientComboChildren\s*\(') 'The client name edit no longer receives a DPI-safe lane between the logo and chevron.'
Assert-ReleaseCheck ($source -match 'EM_SETMARGINS') 'The editable client name no longer reserves text space for the logo and chevron.'
Assert-ReleaseCheck ($source -notmatch 'g_hClientEdit\s*=\s*comboInfo\.hwndItem') 'The visible client editor is using the combo box internal edit again, which causes white repaint artifacts.'
Assert-ReleaseCheck ($source -match 'IDC_CLIENT_EDIT_SURFACE') 'The themed surface behind the borderless client editor is missing.'
Assert-ReleaseCheck ($source -match 'RECT\s+surfaceRect\s*=\s*comboRect') 'The themed selector surface no longer covers the complete client field.'
Assert-ReleaseCheck ($source -match 'HideClientComboChrome[\s\S]*?CreateRectRgn\(0,\s*0,\s*0,\s*0\)[\s\S]*?SetWindowRgn\(hCombo,\s*hiddenRegion') 'The native combo face can repaint over the custom client selector.'
Assert-ReleaseCheck ($source -match 'if\s*\(hWnd\s*!=\s*g_hComboClient\)\s*\r?\n\s*DrawThemedComboFrame') 'The hidden client combo can draw a second frame over the selector.'
Assert-ReleaseCheck ($source -match 'case\s+CB_SHOWDROPDOWN:[\s\S]*?HideClientComboChrome\(hWnd\)[\s\S]*?StackClientSelectorWindows\(\)') 'Opening the client list can raise the native combo face over the custom selector.'
Assert-ReleaseCheck ($source -match 'WM_APP_RESTACK_CLIENT_SELECTOR') 'The selector is no longer restacked after the native dropdown transition finishes.'
Assert-ReleaseCheck ($source -match 'CBN_DROPDOWN[\s\S]*?QueueClientSelectorRestack\(\)') 'Opening the client list no longer queues the post-transition selector restack.'
Assert-ReleaseCheck ($source -match 'CBN_CLOSEUP[\s\S]*?QueueClientSelectorRestack\(\)') 'Closing the client list no longer queues the post-transition selector restack.'
Assert-ReleaseCheck ($source -match 'CLIENT_SELECTOR_DROP_LANE_DIP\s*=\s*28') 'The dropdown chevron lane has regained excess whitespace.'
Assert-ReleaseCheck ($source -notmatch 'RECT\s+hotRect\s*=\s*dropZone') 'The client-list open state has restored the stray pill beside the chevron.'
Assert-ReleaseCheck ($source -notmatch 'SetFocus\(g_hComboClient\)') 'Keyboard focus can return to the hidden native combo face.'
Assert-ReleaseCheck ($source -match 'textHeight\s*=\s*max\(textHeight') 'The client editor is no longer vertically centered from its measured font height.'
Assert-ReleaseCheck ($source -notmatch 'DrawClientEditIcon\s*\(') 'The customer logo is being painted inside the native edit control again.'
Assert-ReleaseCheck ($source -match 'CreateClientFont\s*\([\s\S]*?pointSize\s*=\s*11') 'The client name has fallen back to the undersized general UI font.'
Assert-ReleaseCheck ($source -notmatch 'editStyle\s*&\s*~WS_BORDER') 'Legacy native combo-edit border surgery has returned.'
Assert-ReleaseCheck ($source -notmatch 'editExStyle\s*&\s*~WS_EX_CLIENTEDGE') 'Legacy native combo-edit edge surgery has returned.'
Assert-ReleaseCheck ($source -match 'ClientEditSubclassProc[\s\S]*?case\s+WM_NCPAINT:\s*return\s+0;') 'The native edit frame can repaint over the themed selector.'
Assert-ReleaseCheck ($source -match 'selectionH\s*=\s*ScaleByDpi\(32') 'The client selector can collapse below the adjacent action controls.'
Assert-ReleaseCheck ($source -match 'CBS_DROPDOWN[\s\S]*?CBS_AUTOHSCROLL[\s\S]*?CBS_OWNERDRAWFIXED') 'The client selector owner-draw style is applied too late for its requested height.'
Assert-ReleaseCheck ($source -match 'g_hComboClient\s*=\s*CreateWindowW\([\s\S]*?WS_CLIPSIBLINGS[\s\S]*?CBS_DROPDOWN') 'The native combo can paint through the higher selector layers.'
Assert-ReleaseCheck ($source -match 'g_hClientEditSurface\s*=\s*CreateWindowW\([\s\S]*?WS_CLIPSIBLINGS[\s\S]*?SS_OWNERDRAW') 'The themed selector surface is not clipped against its overlapping siblings.'
Assert-ReleaseCheck ($source -match 'g_hClientEdit\s*=\s*CreateWindowExW\([\s\S]*?WS_CLIPSIBLINGS[\s\S]*?ES_AUTOHSCROLL') 'The client editor is not clipped against its overlapping siblings.'
Assert-ReleaseCheck ($source -match 'WM_APP_RESTACK_CLIENT_SELECTOR[\s\S]*?RedrawWindow\(g_hClientEdit[\s\S]*?RDW_ERASE') 'The client editor is not erased and repainted after the native selector restack.'
Assert-ReleaseCheck ($source -match 'if\s*\(hCombo\s*==\s*g_hComboClient\)') 'Integrated-logo behavior is leaking into non-client combo boxes.'
Assert-ReleaseCheck ($source -match 'DrawClientIconTile\(ds\.hwndItem,\s*ds\.hDC\)') 'The customer logo is no longer drawn on the unified selector surface.'
Assert-ReleaseCheck ($source -match 'DrawModernComboChevron\s*\(') 'The client selector has lost its modern chevron.'
Assert-ReleaseCheck ($source -notmatch 'DrawIconPreviewBox\s*\(') 'The separate client-icon box has returned.'
Assert-ReleaseCheck ($source -match 'DrawPushpinGlyph\s*\(') 'The pin control is no longer using the angled pushpin glyph.'
Assert-ReleaseCheck ($source -notmatch 'DrawBookmarkGlyph\s*\(') 'The old bookmark-ribbon glyph has returned.'

$themeSelectionHandler = [regex]::Match(
    $source,
    'if\s*\(iId\s*==\s*IDC_THEME_COMBO\s*&&\s*iEvent\s*==\s*CBN_SELCHANGE\)(?<body>[\s\S]*?)if\s*\(iId\s*==\s*IDC_THEME_APPLY\)'
)
Assert-ReleaseCheck $themeSelectionHandler.Success 'The theme selection handler could not be inspected.'
if ($themeSelectionHandler.Success) {
    $selectionBody = $themeSelectionHandler.Groups['body'].Value
    Assert-ReleaseCheck ($selectionBody -match 'vSchedulePreview\(\)') 'Theme selection does not schedule a debounced preview.'
    Assert-ReleaseCheck ($selectionBody -notmatch 'ApplyTheme\s*\(') 'Theme selection still applies themes synchronously.'
}

if ($SourceOnly) {
    if ($failures.Count -gt 0) {
        throw "Source checks failed:`n - $($failures -join "`n - ")"
    }
    Write-Host "Source checks passed for ctSpaces $expectedVersion."
    return
}

$resolvedExe = [IO.Path]::GetFullPath($ExePath)
Assert-ReleaseCheck (Test-Path -LiteralPath $resolvedExe -PathType Leaf) "Release executable is missing: $resolvedExe"
if (Test-Path -LiteralPath $resolvedExe -PathType Leaf) {
    $exe = Get-Item -LiteralPath $resolvedExe
    Assert-ReleaseCheck ($exe.VersionInfo.FileVersion -eq $expectedVersion) "Executable file version is '$($exe.VersionInfo.FileVersion)', expected '$expectedVersion'."
    Assert-ReleaseCheck ($exe.VersionInfo.ProductVersion -eq $expectedVersion) "Executable product version is '$($exe.VersionInfo.ProductVersion)', expected '$expectedVersion'."
}

$releaseDir = Split-Path -Parent $resolvedExe
Assert-ReleaseCheck (-not (Test-Path -LiteralPath (Join-Path $releaseDir 'ctSpaces-preview.exe'))) 'QA preview executable remains in the release folder.'
Assert-ReleaseCheck (-not (Test-Path -LiteralPath (Join-Path $releaseDir 'ctSpaces-updater-test.exe'))) 'Updater QA executable remains in the release folder.'
Assert-ReleaseCheck (-not (Test-Path -LiteralPath (Join-Path $releaseDir 'ctSpaces.portable'))) 'Portable QA marker remains in the release folder.'

Assert-ReleaseCheck (Test-Path -LiteralPath $archivePath -PathType Leaf) 'Default.7z is missing.'
$entries = @()
if (Test-Path -LiteralPath $archivePath -PathType Leaf) {
    $entries = @(
        & tar.exe -tf $archivePath |
            ForEach-Object { (($_ -replace '\\', '/') -replace '^\./', '') }
    )
    Assert-ReleaseCheck ($LASTEXITCODE -eq 0) 'Default.7z could not be listed.'
}

if ($entries.Count -gt 0) {
    $normalizedArchiveEntries = @(
        $entries | ForEach-Object { ($_ -replace '/', '\').TrimEnd('\') }
    )
    $maximumStarterFileLength = (
        $entries |
            Where-Object { -not $_.EndsWith('/') } |
            ForEach-Object { ($_ -replace '/', '\').Length } |
            Measure-Object -Maximum
    ).Maximum
    $maximumStarterDirectoryLength = (
        $entries |
            ForEach-Object {
                $normalized = ($_ -replace '/', '\').TrimEnd('\')
                if ($_.EndsWith('/')) {
                    $normalized.Length
                } else {
                    [IO.Path]::GetDirectoryName($normalized).Length
                }
            } |
            Measure-Object -Maximum
    ).Maximum
    $chromiumProfilePrefixLength = 1 + 'Browsers\chrome\Profile\'.Length
    Assert-ReleaseCheck (($chromiumProfilePrefixLength + $maximumStarterFileLength) -le 150) 'Default.7z now exceeds the reserved managed-client legacy file-path tail.'
    Assert-ReleaseCheck (($chromiumProfilePrefixLength + $maximumStarterDirectoryLength) -le 123) 'Default.7z now exceeds the reserved managed-client legacy directory-path tail.'
}

foreach ($entry in $entries) {
    $normalizedEntry = $entry.TrimEnd('/')
    if ([string]::IsNullOrWhiteSpace($normalizedEntry)) {
        continue
    }
    if ($normalizedEntry -match '^Default/(Bookmarks\.bak|Favicons-(journal|wal|shm))($|/)') {
        $failures.Add("Derived or backup browser data remains in Default.7z: $entry")
    }
    $allowedEntry =
        $normalizedEntry -eq 'Default' -or
        $normalizedEntry -match '^Default/(Bookmarks|Favicons|Preferences|Secure Preferences)$' -or
        $normalizedEntry -eq 'Default/Extensions' -or
        $normalizedEntry -match '^Default/Extensions/[a-p]{32}(/.*)?$'
    if (-not $allowedEntry) {
        $failures.Add("Unexpected starter-profile entry: $entry")
    }
}

foreach ($requiredEntry in @(
    'Default/Bookmarks',
    'Default/Favicons',
    'Default/Preferences',
    'Default/Secure Preferences'
)) {
    Assert-ReleaseCheck ($requiredEntry -in $entries) "Default.7z is missing $requiredEntry."
}

$expectedExtensionIds = @(
    'efnbkdcfmcmnhlkaijjjmhjjgladedno',
    'jmjflgjpcpepeafmmgdpfkogkghcpiha',
    'mgijmajocgfcbeboacabfgobmjgjcoja',
    'ncppfjladdkdaemaghochfikpmghbcpc'
)
$actualExtensionIds = @(
    $entries |
        ForEach-Object {
            if ($_ -match '^Default/Extensions/(?<id>[a-p]{32})/') {
                $Matches['id']
            }
        } |
        Sort-Object -Unique
)
$extensionDifference = @(Compare-Object $expectedExtensionIds $actualExtensionIds)
Assert-ReleaseCheck ($extensionDifference.Count -eq 0) "Default.7z extension IDs do not match the expected starter set: $($extensionDifference | Out-String)"
Assert-ReleaseCheck (-not ($entries -match '^Default/Extensions/Temp(/|$)')) 'Default.7z contains the Chromium Extensions\Temp scratch folder.'

$verifyRoot = Join-Path ([IO.Path]::GetTempPath()) ('ctSpaces-release-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $verifyRoot | Out-Null
    & tar.exe -xf $archivePath -C $verifyRoot
    Assert-ReleaseCheck ($LASTEXITCODE -eq 0) 'Default.7z could not be extracted.'

    $defaultDir = Join-Path $verifyRoot 'Default'
    $preferences = Get-Content -Raw -LiteralPath (Join-Path $defaultDir 'Preferences') | ConvertFrom-Json
    foreach ($privateKey in @('account_info', 'signin', 'sync', 'gaia_cookie', 'password_manager', 'autofill')) {
        Assert-ReleaseCheck (-not $preferences.PSObject.Properties[$privateKey]) "Starter Preferences contains private key '$privateKey'."
    }
    $allowedPreferenceKeys = @(
        'bookmark_bar', 'browser', 'distribution', 'extensions', 'intl',
        'profile', 'spellcheck', 'toolbar'
    )
    foreach ($preferenceKey in $preferences.PSObject.Properties.Name) {
        Assert-ReleaseCheck ($preferenceKey -in $allowedPreferenceKeys) "Starter Preferences contains an unexpected top-level key '$preferenceKey'."
    }

    $securePreferences = Get-Content -Raw -LiteralPath (Join-Path $defaultDir 'Secure Preferences') | ConvertFrom-Json
    $secureExtensionIds = @($securePreferences.extensions.settings.PSObject.Properties.Name | Sort-Object)
    Assert-ReleaseCheck (@(Compare-Object $expectedExtensionIds $secureExtensionIds).Count -eq 0) 'Secure Preferences does not match the packaged extension set.'
    $runtimeExtensionKeys = @(
        'content_settings', 'incognito_content_settings',
        'incognito_preferences', 'preferences', 'regular_only_preferences',
        'events', 'serviceworkerevents', 'service_worker_registration_info',
        'first_install_time', 'last_update_time', 'lastpingday',
        'edge_last_update_check_time'
    )
    foreach ($extensionId in $secureExtensionIds) {
        $extensionEntry = $securePreferences.extensions.settings.$extensionId
        foreach ($runtimeKey in $runtimeExtensionKeys) {
            Assert-ReleaseCheck (-not $extensionEntry.PSObject.Properties[$runtimeKey]) "Secure Preferences extension '$extensionId' contains runtime field '$runtimeKey'."
        }
        $extensionPath = [string]$extensionEntry.path
        Assert-ReleaseCheck `
            (-not [IO.Path]::IsPathRooted($extensionPath) -and
             $extensionPath.StartsWith($extensionId + '\', [StringComparison]::Ordinal) -and
             $extensionPath -notmatch '(^|[\\/])\.\.([\\/]|$)' -and
             (Test-Path -LiteralPath (Join-Path (Join-Path $defaultDir 'Extensions') $extensionPath) -PathType Container)) `
            "Secure Preferences extension '$extensionId' does not resolve under its exact packaged target."
    }

    Assert-ReleaseCheck ((Get-Item -LiteralPath (Join-Path $defaultDir 'Bookmarks')).Length -gt 0) 'Starter Bookmarks is empty.'
    $faviconsPath = Join-Path $defaultDir 'Favicons'
    $faviconsItem = Get-Item -LiteralPath $faviconsPath -Force
    Assert-ReleaseCheck `
        (-not $faviconsItem.PSIsContainer -and $faviconsItem.Length -gt 0 -and
         ($faviconsItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) `
        'Starter Favicons is not a bounded regular main database.'
    foreach ($suffix in @('-journal', '-wal', '-shm')) {
        Assert-ReleaseCheck (-not (Test-Path -LiteralPath ($faviconsPath + $suffix))) `
            "Default.7z contains a Favicons$suffix sidecar."
    }
}
finally {
    $resolvedVerifyRoot = [IO.Path]::GetFullPath($verifyRoot)
    $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
    if ($resolvedVerifyRoot.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Test-Path -LiteralPath $resolvedVerifyRoot)) {
        Remove-Item -LiteralPath $resolvedVerifyRoot -Recurse -Force
    }
}

if ($failures.Count -gt 0) {
    throw "Release checks failed:`n - $($failures -join "`n - ")"
}

$archiveHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivePath).Hash
Write-Host "Release checks passed for ctSpaces $expectedVersion."
Write-Host "Bundled 7-Zip: $expectedSevenZipVersion."
Write-Host "Default.7z SHA256: $archiveHash"
