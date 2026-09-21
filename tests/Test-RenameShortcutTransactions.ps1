param()

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourcePath = Join-Path $projectRoot 'ctSpaces.cpp'
$source = Get-Content -Raw -LiteralPath $sourcePath
$stageNameHeader = Get-Content -Raw -LiteralPath `
    (Join-Path $projectRoot 'SiblingStageName.h')
$failures = [System.Collections.Generic.List[string]]::new()

function Assert-TransactionCheck {
    param(
        [bool]$Condition,
        [string]$Message
    )
    if (-not $Condition) {
        $failures.Add($Message)
    }
}

$stageHelper = [regex]::Match(
    $source,
    'static\s+bool\s+TryMakeUniqueSiblingStagePath\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}'
)
Assert-TransactionCheck $stageHelper.Success `
    'The unique sibling staging helper could not be inspected.'
if ($stageHelper.Success) {
    $body = $stageHelper.Groups['body'].Value
    Assert-TransactionCheck `
        ($body -match 'maximumComponentLength\s*=\s*\r?\n?\s*destination\.filename\(\)\.native\(\)\.size\(\)' -and
         $body -match 'singleCharacterBudget[\s\S]*?kSingleCharacterAlphabet\.size\(\)' -and
         $body -match 'BuildSingleCharacter\(\s*\r?\n?\s*alphabetIndex,\s*retainLinkExtension,\s*maximumComponentLength\)' -and
         $body -match 'tokenCharacters\s*<=\s*2[\s\S]*?1u\s*<<[\s\S]*?attemptLimit' -and
         $body -match 'sibling_stage_name::Build\(\s*\r?\n?\s*collisionToken,\s*retainLinkExtension,\s*maximumComponentLength\)' -and
         $body -match 'CompareStringOrdinal\(stagingPath\.c_str\(\),\s*-1,\s*destination\.c_str\(\)' -and
         $body -match 'ERROR_FILE_NOT_FOUND\s*\|\|\s*error\s*==\s*ERROR_PATH_NOT_FOUND') `
        'Sibling staging is not bounded by the final component with exhaustive tiny-token retries, alias-skipping, and fail-closed absence checks.'
    Assert-TransactionCheck `
        ($stageNameHeader -match 'kFullGuidHexCharacters\s*=\s*32' -and
         $stageNameHeader -match 'kSingleCharacterAlphabet\s*=\s*\r?\n?\s*L"0123456789abcdefghijklmnopqrstuvwxyz_-~"' -and
         $stageNameHeader -match 'maximumComponentLength\s*<=\s*extension\.size\(\)' -and
         $stageNameHeader -match 'result\.push_back\(L''~''\)' -and
         $stageNameHeader -match 'baseBudget\s*-\s*1' -and
         $stageNameHeader -match 'result\.append\(extension\)') `
        'The bounded sibling component no longer preserves the full GUID when possible, short/deep path headroom, or .lnk extension.'
}

Assert-TransactionCheck `
    ($source -match 'enum\s+class\s+StagedDirectoryMoveOutcome' -and
     $source -match 'HasExactSafeDirectoryEntryName' -and
     $source -match 'MoveDirectoryThroughUniqueSibling\(\s*oldPath,\s*newPath,\s*L"rename-stage"' -and
     $source -match 'MoveDirectoryThroughUniqueSibling\(\s*newPath,\s*oldPath,\s*L"rename-rollback"') `
    'Case-only rename and rollback are not both routed through the exact-name-verified two-hop helper.'

Assert-TransactionCheck `
    ($source -match 'GetClientShortcutFileName\([^;{}]*\)\s*\{\s*return\s+clientName\s*\+\s*L"\.lnk"' -and
     $source -match 'GetClientShortcutPathInDirectory[\s\S]*?fileName\.size\(\)\s*>\s*\*budget[\s\S]*?return\s+std::nullopt' -and
     $source -match 'GetHistoricalClientShortcutSuffix' -and
     $source -match 'GetHistoricalClientShortcutPathInDirectory' -and
     $source -match 'GetPriorTruncatedClientShortcutFileName\(\s*\r?\n?\s*const\s+std::wstring\s*&clientName,\s*std::optional<BrowserKind>\s+browser\)' -and
     $source -match 'firstHashedBudget[\s\S]*?for\s*\(size_t\s+budget\s*=\s*firstHashedBudget;[\s\S]*?budget\s*<=\s*client_shortcut_name::kMaxFileNameLength[\s\S]*?client_shortcut_name::Build\(clientName,\s*historicalSuffix,\s*budget\)' -and
     $source -match 'GetPriorTruncatedClientShortcutFileName\(clientName,\s*browser\)') `
    'Exact client-only names or complete browser-qualified/neutral historical-budget lookup were lost.'

$clientShortcutVerifier = [regex]::Match(
    $source,
    'static\s+bool\s+IsManagedClientDesktopShortcut\([^;{}]*\)\s*noexcept\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+IsManagedClientDesktopShortcutForClient'
)
Assert-TransactionCheck $clientShortcutVerifier.Success `
    'The managed client-shortcut verifier is not declared noexcept.'
if ($clientShortcutVerifier.Success) {
    $verifierBody = $clientShortcutVerifier.Groups['body'].Value
    $targetBufferOffset = $verifierBody.IndexOf(
        'std::vector<wchar_t> targetText(32768', [StringComparison]::Ordinal
    )
    $argumentBufferOffset = $verifierBody.IndexOf(
        'std::vector<wchar_t> argumentText(32768', [StringComparison]::Ordinal
    )
    $shellLinkOffset = $verifierBody.IndexOf(
        'IShellLinkW *link = nullptr', [StringComparison]::Ordinal
    )
    Assert-TransactionCheck `
        ($verifierBody -match 'try\s*\{[\s\S]*?catch\s*\(\.\.\.\)\s*\{[\s\S]*?return\s+false\s*;' -and
         $targetBufferOffset -ge 0 -and
         $argumentBufferOffset -ge 0 -and
         $shellLinkOffset -gt $targetBufferOffset -and
         $shellLinkOffset -gt $argumentBufferOffset -and
         $source -match 'IsManagedClientDesktopShortcutForClient\([^;{}]*\)\s*noexcept\s*\{') `
        'Managed shortcut verification can throw through recovery or allocate its 32K buffers after acquiring COM pointers.'
}

Assert-TransactionCheck `
    ($source -match 'MoveKnownRegularFileWithoutReplacement[\s\S]*?OpenLockedMatchingRegularFile\(source,\s*expected[\s\S]*?renameInfo->ReplaceIfExists\s*=\s*FALSE[\s\S]*?SetFileInformationByHandle\(fileHandle,\s*FileRenameInfo[\s\S]*?ReadSafeRegularFileSignature\(fileHandle,\s*committedHandle[\s\S]*?SameSafeRegularFileSignature\(committedHandle,\s*expected\)[\s\S]*?CloseHandle\(fileHandle\)[\s\S]*?TryGetSafeRegularFileSignature\(destination,\s*committedPath[\s\S]*?SameSafeRegularFileSignature\(committedPath,\s*expected\)' -and
     $source -notmatch 'MoveFileExW\(source\.c_str\(\),\s*destination\.c_str\(\)' -and
     $source -match 'QuarantineOwnedShortcut' -and
     $source -match 'CommitStagedOwnedShortcut' -and
     $source -match 'SetFileInformationByHandle\([^;]*FileDispositionInfo') `
    'Shortcut replacement/removal is not bound to a verified file handle with handle-based no-replace rename, handle verification before close, pathname verification after close, quarantine, and handle-based deletion.'

$ownershipEvaluation = [regex]::Match(
    $source,
    'static\s+bool\s+EvaluateShortcutOwnershipNoThrow\([^;{}]*\)\s*noexcept\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstruct\s+OwnedShortcutQuarantine'
)
Assert-TransactionCheck `
    ($ownershipEvaluation.Success -and
     $ownershipEvaluation.Groups['body'].Value -match 'try\s*\{[\s\S]*?return\s+check\(path\)\s*;[\s\S]*?catch\s*\(\.\.\.\)\s*\{[\s\S]*?return\s+false\s*;' -and
     $source -notmatch '(?:isOwned|isCommittedOwned)\s*\(') `
    'Shortcut ownership callbacks can throw through a transaction instead of failing closed through EvaluateShortcutOwnershipNoThrow.'

$shortcutSignatureComparison = [regex]::Match(
    $source,
    'static\s+bool\s+SameSafeRegularFileSignature\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}'
)
Assert-TransactionCheck $shortcutSignatureComparison.Success `
    'The shortcut file-signature comparison could not be inspected.'
if ($shortcutSignatureComparison.Success) {
    $body = $shortcutSignatureComparison.Groups['body'].Value
    Assert-TransactionCheck `
        ($body -match 'volumeSerialNumber' -and
         $body -match 'fileIndexHigh' -and
         $body -match 'fileIndexLow' -and
         $body -match 'fileSizeHigh' -and
         $body -match 'fileSizeLow' -and
         $body -match 'lastWriteTime' -and
         $body -notmatch 'creationTime') `
        'Shortcut identity still treats tunneled creation time as immutable, or no longer retains file ID/size/last-write race checks.'
}

$directoryMoveHelper = [regex]::Match(
    $source,
    'static\s+StagedDirectoryMoveOutcome\s+MoveDirectoryThroughUniqueSibling\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+RenameClientProfile'
)
Assert-TransactionCheck $directoryMoveHelper.Success 'The case-only directory move helper could not be inspected.'
if ($directoryMoveHelper.Success) {
    $body = $directoryMoveHelper.Groups['body'].Value
    Assert-TransactionCheck ($body -match 'TryMakeUniqueSiblingStagePath\([^;]*intermediatePath[\s\S]*?IsTreeTargetWithinLegacyPathBudget\(\s*\r?\n?\s*source,\s*intermediatePath[\s\S]*?preMoveGuard\(guardDetails\)[\s\S]*?MoveFileExW\(source\.c_str\(\),\s*intermediatePath\.c_str\(\)') 'Case-only rename can move into an over-budget GUID intermediate or mutate after its final guard has gone stale.'
    Assert-TransactionCheck (([regex]::Matches($body, 'preMoveGuard\s*\(\s*guardDetails\s*\)').Count) -eq 1) 'The logical-name guard must run exactly once before the first two-hop move; rerunning it after the logical root disappears blocks every valid case-only commit or rollback.'
}

$shortcutCommit = [regex]::Match(
    $source,
    'static\s+bool\s+CommitStagedOwnedShortcut\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+bool\s+CreateClientDesktopShortcut'
)
Assert-TransactionCheck $shortcutCommit.Success `
    'The shared shortcut commit helper could not be inspected.'
if ($shortcutCommit.Success) {
    $body = $shortcutCommit.Groups['body'].Value
    Assert-TransactionCheck `
        ($body -match 'destination now contains the verified new shortcut[\s\S]*?return\s+true') `
        'A post-commit quarantine cleanup warning is still reported as if shortcut commit failed.'
}

$clientShortcut = [regex]::Match(
    $source,
    'static\s+bool\s+CreateClientDesktopShortcut\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstruct\s+ClientLaunchRequest'
)
Assert-TransactionCheck $clientShortcut.Success `
    'The client Desktop shortcut creator could not be inspected.'
if ($clientShortcut.Success) {
    $body = $clientShortcut.Groups['body'].Value
    Assert-TransactionCheck `
        ($body -match 'GetClientDesktopShortcutPath\(clientName\)' -and
         $body -match 'IsManagedClientDesktopShortcutForClient\(\*shortcutPath,\s*clientName\)' -and
         $body -match 'ownsClient[\s\S]*?IsManagedClientDesktopShortcutForClient' -and
         $body -match 'ownsSelectedBrowser[\s\S]*?IsManagedClientDesktopShortcut\(candidate,\s*clientName,\s*\r?\n?\s*browser\)' -and
         $body -match 'CommitStagedOwnedShortcut\(stagingPath,\s*\*shortcutPath,\s*ownsClient,[\s\S]*?&ownsSelectedBrowser\)' -and
         $body -match 'retireHistoricalShortcut[\s\S]*?GetClientDesktopShortcutCandidatePaths\([\s\S]*?candidateBrowser[\s\S]*?GetClientDesktopShortcutCandidatePaths\(clientName,\s*std::nullopt\)' -and
         $body -notmatch 'MOVEFILE_REPLACE_EXISTING') `
        'Client shortcut creation does not safely retarget one same-client link, strictly verify the selected browser, and retire only verified historical links.'
}

$genericShortcut = [regex]::Match(
    $source,
    'bool\s+CreateShortcut\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nenum\s+class\s+AutorunValueState'
)
Assert-TransactionCheck $genericShortcut.Success `
    'The generic installer shortcut creator could not be inspected.'
if ($genericShortcut.Success) {
    $body = $genericShortcut.Groups['body'].Value
    Assert-TransactionCheck `
        ($body -match 'CommitStagedOwnedShortcut' -and
         $body -notmatch 'MOVEFILE_REPLACE_EXISTING') `
        'Generic installer shortcut commit can still replace a raced unrelated item by name.'
}

$renameBody = [regex]::Match(
    $source,
    'static\s+bool\s+RenameClientProfile\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+void\s+GuiRenameClient'
)
Assert-TransactionCheck $renameBody.Success `
    'RenameClientProfile could not be inspected.'
if ($renameBody.Success) {
    $body = $renameBody.Groups['body'].Value
    Assert-TransactionCheck `
        ($body -match 'caseOnlyRollbackGuard[\s\S]*?ProbeClientProfilesInUse\(newName,[\s\S]*?MoveDirectoryThroughUniqueSibling\(\s*\r?\n?\s*newPath,\s*oldPath,\s*L"rename-rollback",\s*rollbackDetails,\s*\r?\n?\s*caseOnlyRollbackGuard\)' -and
         $body -match 'caseOnlyForwardGuard[\s\S]*?ProbeClientProfilesInUse\(oldName,[\s\S]*?MoveDirectoryThroughUniqueSibling\(\s*\r?\n?\s*oldPath,\s*newPath,\s*L"rename-stage",\s*moveDetails,\s*\r?\n?\s*caseOnlyForwardGuard\)') `
        'Case-only forward and rollback helpers do not receive guards bound to the correct logical client names.'
    Assert-TransactionCheck `
        ($body -match 'std::wstring\s+rollbackProfileUseError;\s*if\s*\(ProbeClientProfilesInUse\(newName,[\s\S]*?MoveFileExW\(newPath\.c_str\(\),\s*oldPath\.c_str\(\)' -and
         $body -match 'std::wstring\s+immediateMoveProfileUseError;\s*if\s*\(ProbeClientProfilesInUse\(oldName,[\s\S]*?MoveFileExW\(oldPath\.c_str\(\),\s*newPath\.c_str\(\)') `
        'Normal rename forward or rollback can mutate after its final logical-client process probe has gone stale.'
    Assert-TransactionCheck `
        ($body -match 'oldCanonicalPath\s*=\s*GetClientDesktopShortcutPath\(oldName\)' -and
         $body -match 'shortcutsToRename\[index\]\.browser\s*==\s*g_selectedBrowser' -and
         $body -match 'newShortcutPath\s*=\s*GetClientDesktopShortcutPath\(newName\)' -and
         $body -match 'QuarantineOwnedShortcut' -and
         $body -match 'RestoreKnownShortcutQuarantine' -and
         $body -match 'DeleteExpectedOwnedShortcutSafely' -and
         $body -match 'browserToRecord\s*=[\s\S]*?preferredShortcut\.browser[\s\S]*?g_selectedBrowser' -and
         $body -match 'CreateClientDesktopShortcut\(\s*\r?\n?\s*newName,\s*browserToRecord,[\s\S]*?if\s*\(readyToCreate\)[\s\S]*?for\s*\(size_t\s+index\s*=\s*0;\s*index\s*<\s*shortcutsToRename\.size\(\)' -and
         $body -match 'appendShortcutToRename[\s\S]*?GetClientDesktopShortcutCandidatePaths\(oldName,\s*std::nullopt\)') `
        'Rename does not deterministically collapse current/historical links to one client-only shortcut with case-only quarantine and delete-after-commit safety.'
    Assert-TransactionCheck `
        ($body -match 'committed at this[\s\S]*?must never roll[\s\S]*?UpdateClientsComboBox') `
        'Rename post-commit shortcut/UI work is not clearly separated from rollback-capable core work.'
}

$archiveBody = [regex]::Match(
    $source,
    'static\s+bool\s+ArchiveClientProfile\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+void\s+GuiArchiveClient'
)
Assert-TransactionCheck $archiveBody.Success `
    'ArchiveClientProfile could not be inspected.'
if ($archiveBody.Success) {
    $body = $archiveBody.Groups['body'].Value
    Assert-TransactionCheck `
        ($body -match 'SaveConfigMutations\(L"archived and pinned client lists"' -and
         $body -match 'must not restore memory[\s\S]*?UpdateClientsComboBox' -and
         $body -match 'was archived and its settings were saved') `
        'Archive post-commit UI failure can still roll memory back against committed configuration.'
}

$restoreBody = [regex]::Match(
    $source,
    'static\s+bool\s+RestoreArchivedClient\([^;{}]*\)\s*\{(?<body>[\s\S]*?)\r?\n\}\r?\n\r?\nstatic\s+void\s+ShowArchivedClientsMenu'
)
Assert-TransactionCheck $restoreBody.Success `
    'RestoreArchivedClient could not be inspected.'
if ($restoreBody.Success) {
    $body = $restoreBody.Groups['body'].Value
    Assert-TransactionCheck `
        ($body -match 'originalArchivedClients' -and
         $body -match 'SaveArchivedClients\(\)' -and
         $body -match 'committed above[\s\S]*?must never put memory back[\s\S]*?UpdateClientsComboBox' -and
         $body -match 'was restored and its settings were saved') `
        'Restore can still roll memory back after config commit or lose post-commit UI warnings.'
}

Assert-TransactionCheck `
    ($source -match 'removeManagedShortcut[\s\S]*?DeleteOwnedShortcutSafely') `
    'Whole-client cleanup still deletes a managed shortcut by an unbound path check.'

if ($failures.Count -ne 0) {
    throw "Rename/shortcut transaction checks failed:`n - $($failures -join "`n - ")"
}

[pscustomobject]@{
    ShortBoundedStages = $true
    SymmetricCaseOnlyRollback = $true
    OwnershipSafeShortcutCommit = $true
    OwnershipSafeShortcutDeletion = $true
    PhaseSafeRenameArchive = $true
    PhaseSafeArchivedRestore = $true
    HistoricalBrowserAndNeutralShortcutCleanup = $true
    EveryHistoricalShortcutBudgetDiscoverable = $true
    ExactClientOnlyShortcutName = $true
}
