#include "../BackupEngine.h"
#include "../InProc7z.h"
#include "../version.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct ProgressState {
  unsigned highestPercent = 0;
};

struct DeleteDuringCompressionState {
  fs::path filePath;
  bool attempted = false;
};

struct ReplaceDuringExtractionState {
  fs::path archivePath;
  fs::path replacementPath;
  unsigned highestPercent = 0;
  bool attempted = false;
  bool replaced = false;
  DWORD error = ERROR_SUCCESS;
};

struct RestoreGuardState {
  bool called = false;
};

static bool RejectRestoreCommit(void *user, std::wstring &failureDetails) {
  auto *state = static_cast<RestoreGuardState *>(user);
  if (state)
    state->called = true;
  failureDetails = L"Focused restore guard rejection.";
  return false;
}

static void RecordProgress(void *user, _7zOp, unsigned percent,
                           const wchar_t *) {
  auto *state = static_cast<ProgressState *>(user);
  if (state && percent > state->highestPercent)
    state->highestPercent = percent;
}

static void DeleteDuringCompression(void *user, _7zOp operation, unsigned,
                                    const wchar_t *) {
  auto *state = static_cast<DeleteDuringCompressionState *>(user);
  if (!state || state->attempted || operation != _7zOp::Compress)
    return;
  state->attempted = true;
  std::error_code error;
  fs::remove(state->filePath, error);
}

static void AttemptArchiveReplacement(void *user, _7zOp operation,
                                      unsigned percent, const wchar_t *) {
  auto *state = static_cast<ReplaceDuringExtractionState *>(user);
  if (!state)
    return;
  if (percent > state->highestPercent)
    state->highestPercent = percent;
  if (state->attempted || operation != _7zOp::Extract)
    return;

  state->attempted = true;
  SetLastError(ERROR_SUCCESS);
  state->replaced =
      MoveFileExW(state->replacementPath.c_str(), state->archivePath.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
  state->error = state->replaced ? ERROR_SUCCESS : GetLastError();
}

static bool WriteBytes(const fs::path &path, const std::vector<unsigned char> &data) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char *>(data.data()),
               static_cast<std::streamsize>(data.size()));
  return output.good();
}

static bool WriteBackupManifest(const fs::path &path,
                                unsigned long long profileCount) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    return false;
  output << "format=ctSpaces-backup\n"
         << "formatVersion=1\n"
         << "appVersion=" << CTSPACES_VERSION_TEXT << "\n"
         << "createdUtc=2026-08-30T00:00:00Z\n"
         << "profileCount=" << profileCount << "\n"
         << "payload=Sites\n";
  return output.good();
}

static bool TryCreateDirectorySymlink(const fs::path &linkPath,
                                      const fs::path &targetPath) {
  DWORD flags = SYMBOLIC_LINK_FLAG_DIRECTORY;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
  flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif
  if (CreateSymbolicLinkW(linkPath.c_str(), targetPath.c_str(), flags))
    return true;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
  if (GetLastError() == ERROR_INVALID_PARAMETER) {
    return CreateSymbolicLinkW(linkPath.c_str(), targetPath.c_str(),
                               SYMBOLIC_LINK_FLAG_DIRECTORY) != FALSE;
  }
#endif
  return false;
}

static std::vector<unsigned char> ReadBytes(const fs::path &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    return {};
  const std::streamsize size = input.tellg();
  if (size < 0)
    return {};
  input.seekg(0, std::ios::beg);
  std::vector<unsigned char> data(static_cast<size_t>(size));
  if (size > 0 && !input.read(reinterpret_cast<char *>(data.data()), size))
    return {};
  return data;
}

int wmain(int argc, wchar_t **argv) {
  const fs::path root =
      fs::temp_directory_path() /
      (L"ctSpaces-archive-roundtrip-" + std::to_wstring(GetCurrentProcessId()) +
       L"-" + std::to_wstring(GetTickCount64()));
  const fs::path source = root / L"source";
  const fs::path extracted = root / L"extracted";
  const fs::path archive = root / L"roundtrip.7z";

  struct Cleanup {
    fs::path path;
    ~Cleanup() {
      std::error_code error;
      fs::remove_all(path, error);
    }
  } cleanup{root};

  try {
    fs::create_directories(source / L"nested" / L"empty");

    const std::vector<unsigned char> rootData = {
        'c', 't', 'S', 'p', 'a', 'c', 'e', 's', '\r', '\n'};
    std::vector<unsigned char> binaryData(256 * 1024);
    for (size_t i = 0; i < binaryData.size(); ++i)
      binaryData[i] = static_cast<unsigned char>((i * 37u) & 0xFFu);

    if (!WriteBytes(source / L"root.txt", rootData) ||
        !WriteBytes(source / L"nested" / L"data.bin", binaryData) ||
        !WriteBytes(source / L"nested" / L"unicode-\x00E9.txt", rootData)) {
      std::wcerr << L"Could not create the archive test fixture.\n";
      return 1;
    }

    _7zSetHInstance(GetModuleHandleW(nullptr));

    const std::vector<std::wstring> invalidClientNames = {
        L"",          L" Default", L"Client ", L"Client.",
        L"Default",   L"tEmP",     L"CON",     L"con.txt",
        L"PRN",       L"AUX.log",  L"NUL",     L"CLOCK$",
        L"CONIN$",    L"CONOUT$",  L"COM1",    L"com9.txt",
        L"LPT1",      L"lpt9.log", L"COM\u00B9",    L"LPT\u00B2.txt",
        L"Bad/Name",  std::wstring(241, L'A')};
    for (const std::wstring &name : invalidClientNames) {
      if (CtBackup::Detail::IsValidClientDirectoryName(name)) {
        std::wcerr << L"An invalid or reserved client name was accepted: "
                   << name << L"\n";
        return 22;
      }
    }
    const std::vector<std::wstring> validClientNames = {
        L"Legacy Client", L"Default Client", L"Tempest", L"Client.Name",
        L"unicode-\u00E9", std::wstring(240, L'A')};
    for (const std::wstring &name : validClientNames) {
      if (!CtBackup::Detail::IsValidClientDirectoryName(name)) {
        std::wcerr << L"A valid client name was rejected: " << name << L"\n";
        return 23;
      }
    }

    const fs::path budgetSites =
        root / L"path-budget-location" / L"Sites";
    const size_t budgetSitesPrefix =
        fs::absolute(budgetSites).native().size() + 1;
    if (budgetSitesPrefix +
                CtBackup::Detail::kNewClientManagedFileTailCharacters >=
            CtBackup::Detail::kLegacyMaximumFilePathCharacters ||
        budgetSitesPrefix +
                CtBackup::Detail::kNewClientManagedDirectoryTailCharacters >=
            CtBackup::Detail::kLegacyMaximumDirectoryPathCharacters) {
      std::wcerr << L"The test root leaves no client path-budget fixture.\n";
      return 43;
    }
    const size_t maximumBudgetedClientName = (std::min)(
        CtBackup::Detail::kLegacyMaximumFilePathCharacters -
            CtBackup::Detail::kNewClientManagedFileTailCharacters -
            budgetSitesPrefix,
        CtBackup::Detail::kLegacyMaximumDirectoryPathCharacters -
            CtBackup::Detail::kNewClientManagedDirectoryTailCharacters -
            budgetSitesPrefix);
    const std::wstring maximumBudgetedName(maximumBudgetedClientName, L'N');
    const std::wstring overBudgetName(maximumBudgetedClientName + 1, L'N');
    if (!CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(
            budgetSites, maximumBudgetedName) ||
        CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(
            budgetSites, overBudgetName)) {
      std::wcerr << L"The new-client path budget boundary is incorrect.\n";
      return 44;
    }

    std::wstring treeBudgetError;
    if (!CtBackup::Detail::IsTreeTargetWithinLegacyPathBudget(
            source, root / L"tree-budget-target", &treeBudgetError) ||
        CtBackup::Detail::IsTreeTargetWithinLegacyPathBudget(
            source, root / std::wstring(240, L'Z'), &treeBudgetError)) {
      std::wcerr << L"The mapped tree path budget boundary is incorrect.\n";
      return 45;
    }

    const fs::path budgetRestoreStage = root / L"budget-restore-stage";
    const fs::path budgetRestorePayload = budgetRestoreStage / L"Sites";
    fs::create_directories(budgetRestorePayload / L"Alpha");
    if (!WriteBytes(budgetRestorePayload / L"Alpha" / L"state.bin",
                    rootData)) {
      return 46;
    }
    const size_t allowedClientRootLength =
        CtBackup::Detail::kLegacyMaximumFilePathCharacters -
        CtBackup::Detail::kNewClientManagedFileTailCharacters;
    const fs::path minimumPaddedLiveRoot = root / L"P";
    const size_t minimumClientRootLength =
        fs::absolute(minimumPaddedLiveRoot / L"Sites" / L"Alpha")
            .native()
            .size();
    const size_t livePaddingLength =
        minimumClientRootLength > allowedClientRootLength
            ? 1
            : 1 + allowedClientRootLength - minimumClientRootLength + 1;
    const fs::path budgetLiveRoot =
        root / std::wstring(livePaddingLength, L'P');
    const fs::path budgetLiveSites = budgetLiveRoot / L"Sites";
    const fs::path budgetRecovery = budgetLiveRoot / L"Recovery";
    fs::create_directories(budgetLiveSites / L"OldClient");
    if (!WriteBytes(budgetLiveSites / L"OldClient" / L"old.bin", rootData)) {
      return 47;
    }
    CtBackup::StagedRestore overBudgetRestore{
        budgetRestoreStage, budgetRestorePayload, 1};
    const CtBackup::Result overBudgetCommit =
        CtBackup::CommitStagedRestore(overBudgetRestore, budgetLiveSites,
                                      budgetRecovery);
    if (overBudgetCommit.ok() ||
        !fs::is_regular_file(budgetLiveSites / L"OldClient" / L"old.bin") ||
        !fs::is_regular_file(budgetRestorePayload / L"Alpha" / L"state.bin") ||
        fs::exists(budgetRecovery)) {
      std::wcerr
          << L"An over-budget restore moved or replaced current Sites data.\n";
      return 48;
    }

    const fs::path guardStage = root / L"guard-stage";
    const fs::path guardPayload = guardStage / L"Sites";
    const fs::path guardLiveRoot = root / L"g";
    const fs::path guardLiveSites = guardLiveRoot / L"Sites";
    const fs::path guardRecovery = guardLiveRoot / L"Recovery";
    fs::create_directories(guardPayload / L"Alpha");
    fs::create_directories(guardLiveSites / L"OldClient");
    if (!WriteBytes(guardPayload / L"Alpha" / L"state.bin", rootData) ||
        !WriteBytes(guardLiveSites / L"OldClient" / L"old.bin", rootData)) {
      return 49;
    }
    RestoreGuardState guardState;
    CtBackup::StagedRestore guardedRestore{guardStage, guardPayload, 1};
    const CtBackup::Result guardResult = CtBackup::CommitStagedRestore(
        guardedRestore, guardLiveSites, guardRecovery, RejectRestoreCommit,
        &guardState);
    if (guardResult.ok() || !guardState.called ||
        !fs::is_regular_file(guardLiveSites / L"OldClient" / L"old.bin") ||
        !fs::is_regular_file(guardPayload / L"Alpha" / L"state.bin") ||
        fs::exists(guardRecovery)) {
      std::wcerr << L"A rejected final restore guard changed live or staged "
                    L"data.\n";
      return 50;
    }

    const fs::path recoveryStage = root / L"recovery-stage";
    const fs::path recoveryPayload = recoveryStage / L"Sites";
    const fs::path recoveryLiveRoot = root / L"r";
    const fs::path recoveryLiveSites = recoveryLiveRoot / L"Sites";
    const fs::path overBudgetRecovery =
        recoveryLiveRoot / L"Sites_PreRestore_recovery_path_budget";
    fs::create_directories(recoveryPayload / L"Alpha");
    fs::create_directories(recoveryLiveSites / L"OldClient");
    if (!WriteBytes(recoveryPayload / L"Alpha" / L"state.bin", rootData)) {
      return 51;
    }
    const fs::path recoveryClientRoot =
        recoveryLiveSites / L"OldClient";
    const size_t recoveryClientRootLength =
        fs::absolute(recoveryClientRoot).native().size();
    if (recoveryClientRootLength + 2 >=
        CtBackup::Detail::kLegacyMaximumDirectoryPathCharacters) {
      return 52;
    }
    const size_t deepDirectoryNameLength =
        CtBackup::Detail::kLegacyMaximumDirectoryPathCharacters -
        recoveryClientRootLength - 2;
    const fs::path deepCurrentDirectory =
        recoveryClientRoot /
        std::wstring(deepDirectoryNameLength, L'D');
    fs::create_directories(deepCurrentDirectory);
    if (!WriteBytes(deepCurrentDirectory / L"x.bin", rootData)) {
      return 53;
    }
    CtBackup::StagedRestore recoveryBudgetRestore{
        recoveryStage, recoveryPayload, 1};
    RestoreGuardState recoveryGuardState;
    const CtBackup::Result recoveryBudgetResult =
        CtBackup::CommitStagedRestore(
            recoveryBudgetRestore, recoveryLiveSites, overBudgetRecovery,
            RejectRestoreCommit, &recoveryGuardState);
    if (recoveryBudgetResult.ok() || recoveryGuardState.called ||
        !fs::is_regular_file(deepCurrentDirectory / L"x.bin") ||
        !fs::is_regular_file(recoveryPayload / L"Alpha" / L"state.bin") ||
        fs::exists(overBudgetRecovery)) {
      std::wcerr << L"An over-budget recovery mapping changed live or staged "
                    L"data or reached the final guard.\n";
      return 54;
    }

    ProgressState compressProgress;
    const HRESULT compressResult =
        _7zCompress7z(archive.c_str(), source.c_str(), false, RecordProgress,
                      &compressProgress);
    if (FAILED(compressResult) || !fs::exists(archive) ||
        fs::file_size(archive) == 0 || compressProgress.highestPercent != 100) {
      std::wcerr << L"Compression failed: 0x" << std::hex
                 << static_cast<unsigned long>(compressResult) << L"\n";
      return 2;
    }

    ProgressState extractProgress;
    const HRESULT extractResult =
        _7zExtra_7z(archive.c_str(), extracted.c_str(), RecordProgress,
                    &extractProgress);
    if (FAILED(extractResult) || extractProgress.highestPercent != 100) {
      std::wcerr << L"Extraction failed: 0x" << std::hex
                 << static_cast<unsigned long>(extractResult) << L"\n";
      return 3;
    }

    if (ReadBytes(extracted / L"root.txt") != rootData ||
        ReadBytes(extracted / L"nested" / L"data.bin") != binaryData ||
        ReadBytes(extracted / L"nested" / L"unicode-\x00E9.txt") != rootData ||
        !fs::is_directory(extracted / L"nested" / L"empty")) {
      std::wcerr << L"Archive round-trip content did not match.\n";
      return 4;
    }

    if (argc >= 2) {
      const fs::path starterArchive = argv[1];
      const fs::path starterExtracted = root / L"starter";
      ProgressState starterProgress;
      const HRESULT starterResult =
          _7zExtra_7z(starterArchive.c_str(), starterExtracted.c_str(),
                      RecordProgress, &starterProgress);
      if (FAILED(starterResult) || starterProgress.highestPercent != 100 ||
          !fs::is_regular_file(starterExtracted / L"Default" / L"Bookmarks") ||
          !fs::is_regular_file(starterExtracted / L"Default" / L"Favicons")) {
        std::wcerr << L"Starter-profile extraction failed: 0x" << std::hex
                   << static_cast<unsigned long>(starterResult) << L"\n";
        return 5;
      }
    }

    const fs::path backupSites = root / L"backup-source" / L"Sites";
    const fs::path alphaProfile = backupSites / L"Alpha" / L"Default";
    const fs::path betaProfile = backupSites / L"Beta" / L"Default";
    fs::create_directories(alphaProfile);
    fs::create_directories(betaProfile / L"empty");
    const fs::path hiddenPath = alphaProfile / L"hidden-state.bin";
    if (!WriteBytes(alphaProfile / L"state.bin", binaryData) ||
        !WriteBytes(alphaProfile / L"unicode-\x00E9.txt", rootData) ||
        !WriteBytes(hiddenPath, rootData) ||
        !WriteBytes(betaProfile / L"bookmarks.bin", rootData) ||
        !SetFileAttributesW(hiddenPath.c_str(),
                            GetFileAttributesW(hiddenPath.c_str()) |
                                FILE_ATTRIBUTE_HIDDEN)) {
      std::wcerr << L"Could not create the backup workflow fixture.\n";
      return 5;
    }

    const fs::path managedDataRoot = backupSites.parent_path();
    const fs::path internalStarterArchive = managedDataRoot / L"Default.7z";
    if (!WriteBytes(internalStarterArchive, rootData))
      return 24;
    CtBackup::Result internalStarterResult =
        CtBackup::CreateValidated7zBackup(
            backupSites, internalStarterArchive, CTSPACES_VERSION_TEXT,
            nullptr, nullptr);
    if (internalStarterResult.ok() ||
        ReadBytes(internalStarterArchive) != rootData) {
      std::wcerr
          << L"An application-managed Default.7z destination was accepted.\n";
      return 25;
    }

    const fs::path internalNestedDestination =
        managedDataRoot / L"_DefBak" / L"nested-backup.7z";
    CtBackup::Result internalNestedResult =
        CtBackup::CreateValidated7zBackup(
            backupSites, internalNestedDestination, CTSPACES_VERSION_TEXT,
            nullptr, nullptr);
    if (internalNestedResult.ok() || fs::exists(internalNestedDestination)) {
      std::wcerr << L"A nested application-managed destination was accepted.\n";
      return 26;
    }

    const fs::path caseVariantInternalDestination =
        root / L"BACKUP-SOURCE" / L"case-variant.7z";
    CtBackup::Result caseVariantInternalResult =
        CtBackup::CreateValidated7zBackup(
            backupSites, caseVariantInternalDestination,
            CTSPACES_VERSION_TEXT, nullptr, nullptr);
    if (caseVariantInternalResult.ok() ||
        fs::exists(caseVariantInternalDestination)) {
      std::wcerr
          << L"A case-variant application-managed destination was accepted.\n";
      return 27;
    }

    const fs::path managedAlias = root / L"managed-data-alias";
    if (TryCreateDirectorySymlink(managedAlias, managedDataRoot)) {
      const fs::path aliasDestination = managedAlias / L"alias-backup.7z";
      CtBackup::Result aliasDestinationResult =
          CtBackup::CreateValidated7zBackup(
              backupSites, aliasDestination, CTSPACES_VERSION_TEXT, nullptr,
              nullptr);
      if (aliasDestinationResult.ok() || fs::exists(aliasDestination)) {
        std::wcerr
            << L"A reparse alias into application-managed data was accepted.\n";
        return 28;
      }
    }

    const fs::path rootFileSites =
        root / L"root-file-source" / L"Sites";
    fs::create_directories(rootFileSites / L"Legacy" / L"Default");
    if (!WriteBytes(rootFileSites / L"unexpected.bin", rootData))
      return 29;
    const fs::path rootFileBackup = root / L"root-file-backup.7z";
    CtBackup::Result rootFileBackupResult =
        CtBackup::CreateValidated7zBackup(
            rootFileSites, rootFileBackup, CTSPACES_VERSION_TEXT, nullptr,
            nullptr);
    if (rootFileBackupResult.ok() || fs::exists(rootFileBackup)) {
      std::wcerr << L"A direct file in Sites was accepted for backup.\n";
      return 30;
    }

    for (const std::wstring &reservedName :
         {std::wstring(L"Default"), std::wstring(L"Temp")}) {
      const fs::path invalidNameSites =
          root / (L"invalid-name-source-" + reservedName) / L"Sites";
      fs::create_directories(invalidNameSites / reservedName / L"ProfileData");
      const fs::path invalidNameBackup =
          root / (L"invalid-name-" + reservedName + L".7z");
      CtBackup::Result invalidNameResult =
          CtBackup::CreateValidated7zBackup(
              invalidNameSites, invalidNameBackup, CTSPACES_VERSION_TEXT,
              nullptr, nullptr);
      if (invalidNameResult.ok() || fs::exists(invalidNameBackup)) {
        std::wcerr << L"A reserved client directory was accepted: "
                   << reservedName << L"\n";
        return 31;
      }
    }

    const fs::path reparseSites =
        root / L"reparse-source" / L"Sites";
    const fs::path reparseTarget = root / L"reparse-client-target";
    fs::create_directories(reparseSites);
    fs::create_directories(reparseTarget / L"Default");
    const fs::path reparseClient = reparseSites / L"LinkedClient";
    if (TryCreateDirectorySymlink(reparseClient, reparseTarget)) {
      const fs::path reparseBackup = root / L"reparse-source.7z";
      CtBackup::Result reparseResult =
          CtBackup::CreateValidated7zBackup(
              reparseSites, reparseBackup, CTSPACES_VERSION_TEXT, nullptr,
              nullptr);
      if (reparseResult.ok() || fs::exists(reparseBackup)) {
        std::wcerr << L"A reparse-point client directory was accepted.\n";
        return 32;
      }
    }

    const fs::path rootFileArchiveFixture =
        root / L"root-file-archive-fixture";
    fs::create_directories(rootFileArchiveFixture / L"Sites" / L"Legacy" /
                           L"Default");
    if (!WriteBackupManifest(
            rootFileArchiveFixture / L"ctSpacesBackup.manifest", 1) ||
        !WriteBytes(rootFileArchiveFixture / L"Sites" / L"unexpected.bin",
                    rootData)) {
      return 33;
    }
    const fs::path rootFileArchive = root / L"root-file-archive.7z";
    if (FAILED(_7zCompress7z(rootFileArchive.c_str(),
                            rootFileArchiveFixture.c_str(), false, nullptr,
                            nullptr))) {
      return 34;
    }
    CtBackup::StagedRestore rejectedRootFileRestore;
    const fs::path rejectedRootFileStage = root / L"root-file-stage";
    CtBackup::Result rejectedRootFileResult =
        CtBackup::StageValidated7zRestore(
            rootFileArchive, rejectedRootFileStage, nullptr, nullptr,
            rejectedRootFileRestore);
    if (rejectedRootFileResult.ok() || fs::exists(rejectedRootFileStage)) {
      std::wcerr
          << L"A restored backup with a direct Sites file was accepted.\n";
      return 35;
    }

    const fs::path reservedArchiveFixture =
        root / L"reserved-archive-fixture";
    fs::create_directories(reservedArchiveFixture / L"Sites" / L"Default" /
                           L"ProfileData");
    if (!WriteBackupManifest(
            reservedArchiveFixture / L"ctSpacesBackup.manifest", 1)) {
      return 36;
    }
    const fs::path reservedArchive = root / L"reserved-client.7z";
    if (FAILED(_7zCompress7z(reservedArchive.c_str(),
                            reservedArchiveFixture.c_str(), false, nullptr,
                            nullptr))) {
      return 37;
    }
    CtBackup::StagedRestore rejectedReservedRestore;
    const fs::path rejectedReservedStage = root / L"reserved-client-stage";
    CtBackup::Result rejectedReservedResult =
        CtBackup::StageValidated7zRestore(
            reservedArchive, rejectedReservedStage, nullptr, nullptr,
            rejectedReservedRestore);
    if (rejectedReservedResult.ok() || fs::exists(rejectedReservedStage)) {
      std::wcerr << L"A restored backup with a reserved client name was "
                    L"accepted.\n";
      return 38;
    }

    const fs::path legacySites = root / L"legacy-source" / L"Sites";
    const fs::path legacyProfile =
        legacySites / L"Legacy Customer" / L"Default";
    fs::create_directories(legacyProfile);
    if (!WriteBytes(legacyProfile / L"legacy-state.bin", rootData) ||
        fs::exists(legacySites / L"Legacy Customer" / L"ctSpaces")) {
      return 39;
    }
    const fs::path legacyBackup = root / L"legacy-profile.7z";
    CtBackup::Result legacyBackupResult =
        CtBackup::CreateValidated7zBackup(
            legacySites, legacyBackup, CTSPACES_VERSION_TEXT, nullptr,
            nullptr);
    if (!legacyBackupResult.ok()) {
      std::wcerr << L"A valid markerless legacy profile was rejected: "
                 << legacyBackupResult.details << L"\n";
      return 40;
    }
    CtBackup::StagedRestore legacyRestore;
    const fs::path legacyStage = root / L"legacy-stage";
    CtBackup::Result legacyStageResult =
        CtBackup::StageValidated7zRestore(
            legacyBackup, legacyStage, nullptr, nullptr, legacyRestore);
    if (!legacyStageResult.ok() || legacyRestore.profileCount != 1 ||
        ReadBytes(legacyRestore.payloadDir / L"Legacy Customer" / L"Default" /
                  L"legacy-state.bin") != rootData ||
        fs::exists(legacyRestore.payloadDir / L"Legacy Customer" /
                   L"ctSpaces")) {
      std::wcerr << L"A valid markerless legacy profile did not round-trip: "
                 << legacyStageResult.details << L"\n";
      return 41;
    }

    const fs::path validatedBackup = root / L"ctSpaces_Backup_test.7z";
    ProgressState backupProgress;
    CtBackup::Result backupResult = CtBackup::CreateValidated7zBackup(
        backupSites, validatedBackup, CTSPACES_VERSION_TEXT, RecordProgress,
        &backupProgress);
    if (!backupResult.ok() || !fs::is_regular_file(validatedBackup) ||
        backupProgress.highestPercent != 100) {
      std::wcerr << L"Validated backup creation failed: "
                 << backupResult.details << L"\n";
      return 6;
    }

    CtBackup::StagedRestore stagedRestore;
    const fs::path replacementBackup = root / L"replacement.7z";
    fs::copy_file(validatedBackup, replacementBackup);
    ReplaceDuringExtractionState replacementState{
        validatedBackup, replacementBackup};
    const fs::path restoreStage = root / L"restore-stage";
    CtBackup::Result stageResult = CtBackup::StageValidated7zRestore(
        validatedBackup, restoreStage, AttemptArchiveReplacement,
        &replacementState, stagedRestore);
    const fs::path stagedAlpha =
        stagedRestore.payloadDir / L"Alpha" / L"Default";
    if (!stageResult.ok() || stagedRestore.profileCount != 2 ||
        replacementState.highestPercent != 100 ||
        !replacementState.attempted || replacementState.replaced ||
        (replacementState.error != ERROR_SHARING_VIOLATION &&
         replacementState.error != ERROR_ACCESS_DENIED) ||
        ReadBytes(stagedAlpha / L"state.bin") != binaryData ||
        ReadBytes(stagedAlpha / L"unicode-\x00E9.txt") != rootData ||
        (GetFileAttributesW((stagedAlpha / L"hidden-state.bin").c_str()) &
         FILE_ATTRIBUTE_HIDDEN) == 0 ||
        !fs::is_directory(stagedRestore.payloadDir / L"Beta" / L"Default" /
                          L"empty")) {
      std::wcerr << L"Validated backup staging failed: "
                 << stageResult.details << L"\n";
      return 7;
    }

    const fs::path liveRoot = root / L"live";
    const fs::path liveSites = liveRoot / L"Sites";
    const fs::path recovery = liveRoot / L"Sites_PreRestore_test";
    fs::create_directories(liveSites / L"OldClient");
    if (!WriteBytes(liveSites / L"OldClient" / L"old.bin", rootData))
      return 8;
    CtBackup::Result commitResult =
        CtBackup::CommitStagedRestore(stagedRestore, liveSites, recovery);
    if (!commitResult.ok() ||
        ReadBytes(liveSites / L"Alpha" / L"Default" / L"state.bin") !=
            binaryData ||
        ReadBytes(recovery / L"OldClient" / L"old.bin") != rootData ||
        fs::exists(restoreStage)) {
      std::wcerr << L"Transactional restore commit failed: "
                 << commitResult.details << L"\n";
      return 9;
    }

    const fs::path corruptBackup = root / L"corrupt.7z";
    std::vector<unsigned char> corruptData = ReadBytes(validatedBackup);
    if (corruptData.size() < 32)
      return 10;
    corruptData[corruptData.size() / 2] ^= 0x5Au;
    if (!WriteBytes(corruptBackup, corruptData))
      return 11;
    CtBackup::StagedRestore corruptRestore;
    const fs::path corruptStage = root / L"corrupt-stage";
    CtBackup::Result corruptResult = CtBackup::StageValidated7zRestore(
        corruptBackup, corruptStage, nullptr, nullptr, corruptRestore);
    if (corruptResult.ok() || fs::exists(corruptStage)) {
      std::wcerr << L"A corrupted backup was incorrectly accepted.\n";
      return 12;
    }

    const fs::path invalidManifest = root / L"invalid.manifest";
    const std::vector<unsigned char> invalidManifestData = {
        'f', 'o', 'r', 'm', 'a', 't', '=', 'n', 'o', 't', '-', 'c', 't', '\n'};
    if (!WriteBytes(invalidManifest, invalidManifestData))
      return 13;
    const fs::path invalidBackup = root / L"invalid-manifest.7z";
    HRESULT invalidArchiveResult = _7zCompress7zWithExtraFile(
        invalidBackup.c_str(), backupSites.c_str(), true,
        invalidManifest.c_str(), L"ctSpacesBackup.manifest", nullptr,
        nullptr);
    CtBackup::StagedRestore invalidRestore;
    const fs::path invalidStage = root / L"invalid-stage";
    CtBackup::Result invalidResult = CtBackup::StageValidated7zRestore(
        invalidBackup, invalidStage, nullptr, nullptr, invalidRestore);
    if (FAILED(invalidArchiveResult) || invalidResult.ok() ||
        fs::exists(invalidStage)) {
      std::wcerr << L"An invalid backup manifest was incorrectly accepted.\n";
      return 14;
    }

    const fs::path unsafeArchive = root / L"unsafe.7z";
    HRESULT unsafeResult = _7zCompress7zWithExtraFile(
        unsafeArchive.c_str(), backupSites.c_str(), true,
        invalidManifest.c_str(), L"..\\escape.manifest", nullptr, nullptr);
    if (SUCCEEDED(unsafeResult) || fs::exists(unsafeArchive)) {
      std::wcerr << L"An unsafe archive path was incorrectly accepted.\n";
      return 15;
    }

    const fs::path superscriptDeviceArchive =
        root / L"superscript-device-path.7z";
    const HRESULT superscriptDeviceResult = _7zCompress7zWithExtraFile(
        superscriptDeviceArchive.c_str(), backupSites.c_str(), true,
        invalidManifest.c_str(), L"Sites\\Alpha\\COM\x00B9\\payload.bin",
        nullptr, nullptr);
    if (SUCCEEDED(superscriptDeviceResult) ||
        fs::exists(superscriptDeviceArchive)) {
      std::wcerr << L"A superscript Windows device path was incorrectly "
                    L"accepted.\n";
      return 42;
    }

    const fs::path duplicateSource = root / L"duplicate-source";
    const fs::path duplicateExtra = root / L"duplicate-extra.txt";
    fs::create_directories(duplicateSource);
    if (!WriteBytes(duplicateSource / L"same.txt", rootData) ||
        !WriteBytes(duplicateExtra, binaryData)) {
      return 19;
    }
    const fs::path duplicateArchive = root / L"duplicate-paths.7z";
    const HRESULT duplicateCreateResult = _7zCompress7zWithExtraFile(
        duplicateArchive.c_str(), duplicateSource.c_str(), false,
        duplicateExtra.c_str(), L"./SAME.TXT", nullptr, nullptr);
    if (FAILED(duplicateCreateResult) ||
        !fs::is_regular_file(duplicateArchive)) {
      std::wcerr << L"Could not create the duplicate-path archive fixture.\n";
      return 20;
    }

    _7zArchiveInfo duplicateInfo{};
    const HRESULT duplicateInspectResult =
        _7zInspect7z(duplicateArchive.c_str(), &duplicateInfo);
    const fs::path duplicateExtracted = root / L"duplicate-extracted";
    const HRESULT duplicateExtractResult =
        _7zExtra_7z(duplicateArchive.c_str(), duplicateExtracted.c_str(),
                    nullptr, nullptr);
    const HRESULT duplicateExpected =
        HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if (duplicateInspectResult != duplicateExpected ||
        duplicateExtractResult != duplicateExpected ||
        fs::exists(duplicateExtracted)) {
      std::wcerr << L"A case-insensitive normalized duplicate archive path "
                    L"was incorrectly accepted.\n";
      return 21;
    }

    const fs::path disappearingSource = root / L"disappearing-source";
    fs::create_directories(disappearingSource);
    const fs::path disappearingFile = disappearingSource / L"large.bin";
    if (!WriteBytes(disappearingFile, binaryData))
      return 16;
    DeleteDuringCompressionState deleteState{disappearingFile};
    const fs::path incompleteArchive = root / L"incomplete.7z";
    HRESULT incompleteResult = _7zCompress7z(
        incompleteArchive.c_str(), disappearingSource.c_str(), false,
        DeleteDuringCompression, &deleteState);
    if (!deleteState.attempted || SUCCEEDED(incompleteResult)) {
      std::wcerr << L"A skipped source file was incorrectly reported as a "
                    L"successful backup.\n";
      return 17;
    }

    std::wcout
        << L"In-process archive, validated backup, corruption, and restore "
           L"tests passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Archive round-trip threw: " << error.what() << '\n';
    return 18;
  }
}
