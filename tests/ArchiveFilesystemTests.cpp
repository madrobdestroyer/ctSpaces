#include "../BackupEngine.h"
#include "../version.h"
#include "ArchiveFilesystemRestoreBoundary.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifndef CTSPACES_ARCHIVE_FILESYSTEM_FIXTURE
#error Supply the exact private fixture root in ArchiveFilesystemFixtureHeader.
#endif

namespace fs = std::filesystem;

namespace {
unsigned checks = 0;

void Check(bool condition, const char *description) {
  ++checks;
  if (!condition)
    throw std::runtime_error(description);
  std::cout << "PASS " << description << '\n';
}

struct FileHandle {
  HANDLE value;
  explicit FileHandle(HANDLE handle) : value(handle) {}
  FileHandle(const FileHandle &) = delete;
  FileHandle &operator=(const FileHandle &) = delete;
  ~FileHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

void CheckOrdinaryAncestors(fs::path path) {
  while (!path.empty()) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES &&
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
      throw std::runtime_error("Reparse path refused in private archive fixture");
    }
    const auto parent = path.parent_path();
    if (parent == path) break;
    path = parent;
  }
}

void Write(const fs::path &path, const std::string &bytes) {
  CheckOrdinaryAncestors(path);
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  output.close();
  if (!output) throw std::runtime_error("Could not write private fixture bytes");
}

std::string Read(const fs::path &path) {
  CheckOrdinaryAncestors(path);
  if (!fs::is_regular_file(path) || fs::file_size(path) > 64 * 1024)
    throw std::runtime_error("Expected a bounded regular archive fixture file");
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("Could not read private fixture bytes");
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

struct FileIdentity {
  DWORD volume;
  DWORD high;
  DWORD low;
  bool operator==(const FileIdentity &) const = default;
};

FileIdentity Identity(const fs::path &path) {
  CheckOrdinaryAncestors(path);
  const FileHandle file(CreateFileW(path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (file.value == INVALID_HANDLE_VALUE ||
      !GetFileInformationByHandle(file.value, &info)) {
    throw std::runtime_error("Could not read exact fixture object identity");
  }
  return {info.dwVolumeSerialNumber, info.nFileIndexHigh, info.nFileIndexLow};
}

bool CrcValid(const fs::path &archive) {
  return _7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK;
}

CtBackup::Result BackUp(const fs::path &sites, const fs::path &destination,
                        const CtBackup::BackupOptions &options = {}) {
  return CtBackup::CreateValidated7zBackup(sites, destination,
      CTSPACES_VERSION_TEXT, nullptr, nullptr, options);
}

bool HasOnlyArchive(const fs::path &archive) {
  auto item = fs::directory_iterator(archive.parent_path());
  if (item == fs::directory_iterator{} || item->path().filename() != archive.filename())
    return false;
  return ++item == fs::directory_iterator{};
}

void SharingRound(const fs::path &fixture) {
  const auto sites = fixture / L"d/Sites";
  const auto selected = sites / L"Alpha/note.ctn";
  const auto excluded = sites / L"Beta/note.ctn";
  const auto archive = fixture / L"out/existing.7z";
  fs::create_directories(archive.parent_path());
  Write(selected, "previous selected notebook bytes");
  Write(excluded, "independent excluded notebook bytes");
  Check(BackUp(sites, archive).ok() && CrcValid(archive),
        "Original backup is valid and published");
  const auto oldBytes = Read(archive);
  const auto oldId = Identity(archive);
  const auto selectedId = Identity(selected);
  const auto excludedId = Identity(excluded);
  const std::string updated("current selected notebook\0with binary bytes", 43);
  Write(selected, updated);
  Check(Identity(selected) == selectedId, "Fixture edit keeps the selected file identity");
  CtBackup::BackupOptions selection;
  selection.selectedClientNames = std::vector<std::wstring>{L"Alpha"};

  {
    const FileHandle lock(CreateFileW(archive.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Check(lock.value != INVALID_HANDLE_VALUE, "Actual reader holds the previous archive against replacement");
    const auto refused = BackUp(sites, archive, selection);
    Check(!refused.ok() && (refused.code == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) ||
                           refused.code == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)),
          "Real Windows sharing prevents replacing a destination with a live reader");
    Check(Read(archive) == oldBytes && Identity(archive) == oldId && CrcValid(archive),
          "The denied publication preserves the exact previous archive");
    Check(HasOnlyArchive(archive), "The denied publication removes its temporary artifacts");
  }

  {
    const FileHandle lock(CreateFileW(selected.c_str(), GENERIC_READ, 0,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Check(lock.value != INVALID_HANDLE_VALUE, "Actual exclusive reader blocks the selected source file");
    const auto refused = BackUp(sites, archive, selection);
    Check(!refused.ok() && refused.code == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION),
          "Unreadable selected input produces the real sharing failure");
    Check(Read(archive) == oldBytes && Identity(archive) == oldId && HasOnlyArchive(archive),
          "A refused source read preserves the exact existing backup");
  }

  const FileHandle compatible(CreateFileW(selected.c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  Check(compatible.value != INVALID_HANDLE_VALUE, "Hold a compatible real reader on the selected source");
  Check(BackUp(sites, archive, selection).ok() && CrcValid(archive),
        "Retry publishes after the conflicting handles close while the compatible reader stays open");
  Check(Identity(archive) != oldId && Read(archive) != oldBytes && HasOnlyArchive(archive),
        "The validated selected backup replaces the old file without stray artifacts");
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"verified", nullptr, nullptr, staged).ok(),
        "The published selected backup stages through the real SDK");
  Check(staged.profileCount == 1 && Read(staged.payloadDir / L"Alpha/note.ctn") == updated &&
        !fs::exists(staged.payloadDir / L"Beta"), "Only the selected client and its exact binary notebook are staged");
  Check(Read(selected) == updated && Identity(selected) == selectedId &&
        Read(excluded) == "independent excluded notebook bytes" && Identity(excluded) == excludedId,
        "Both source client files retain exact bytes and identities");
  std::cout << "SHARING_CASES=real-destination-refusal,exclusive-source-refusal,compatible-reader-retry\n";
}

void RecoveryInputRound(const fs::path &fixture) {
  const auto source = fixture / L"src/Sites";
  const auto client = fs::path(L"\u00C9quipe O'Brien");
  const auto note = source / client / L"note.ctn";
  const auto originalArchive = fixture / L"out/O'Brien backup.7z";
  fs::create_directories(originalArchive.parent_path());
  Write(note, "new collection from exact archived notebook");
  fs::create_directories(source / client / L"empty");
  Check(BackUp(source, originalArchive).ok() && CrcValid(originalArchive),
        "Ordinary valid archive preserves a Unicode client and an empty directory");
  const auto archiveBytes = Read(originalArchive);
  const auto originalId = Identity(originalArchive);
  const auto data = fixture / L"data";
  const auto live = data / L"Sites";
  const auto oldNote = live / L"Existing/note.ctn";
  const auto input = live / L"Existing/user-copy.7z";
  Write(oldNote, "previous collection that owns the restore input");
  Check(fs::copy_file(originalArchive, input), "Keep a private copy of the valid archive inside the current collection");
  const auto inputId = Identity(input);
  const auto liveId = Identity(live);
  const auto oldNoteId = Identity(oldNote);
  Check(SetFileAttributesW(input.c_str(), FILE_ATTRIBUTE_READONLY) != FALSE,
        "The copied input can have an ordinary read-only attribute");
  CtBackup::StagedRestore staged;
  const auto stage = data / L"Restore_Temp_00000001";
  {
    const FileHandle reader(CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Check(reader.value != INVALID_HANDLE_VALUE, "A compatible reader holds the exact restore input during extraction");
    Check(CtBackup::StageValidated7zRestore(input, stage, nullptr, nullptr, staged).ok(),
          "The real SDK stages an ordinary input from inside the current collection");
  }
  Check(Read(input) == archiveBytes && Identity(input) == inputId && Identity(live) == liveId,
        "Staging leaves the restore input and active collection in place");
  const auto recovered = data / L"R0001";
  bool previousInRecovery = false;
  Check(CtBackup::CommitStagedRestore(staged, live, recovered, nullptr, nullptr, &previousInRecovery).ok() &&
        previousInRecovery, "The transaction retains the original collection that held its archive input");
  const auto recoveredInput = recovered / L"Existing/user-copy.7z";
  Check(Identity(recovered) == liveId && Identity(recovered / L"Existing/note.ctn") == oldNoteId &&
        Read(recovered / L"Existing/note.ctn") == "previous collection that owns the restore input",
        "The exact original root and previous notebook move to recovery");
  Check(Identity(recoveredInput) == inputId && Read(recoveredInput) == archiveBytes && CrcValid(recoveredInput) &&
        (GetFileAttributesW(recoveredInput.c_str()) & FILE_ATTRIBUTE_READONLY),
        "Recovery retains the exact read-only archive object and it remains CRC-valid");
  Check(!fs::exists(input) && !fs::exists(stage) && Read(live / client / L"note.ctn") == Read(note) &&
        fs::is_empty(live / client / L"empty"), "The replacement contains the exact incoming payload and empty directory");

  const auto replay = fixture / L"replay";
  fs::create_directory(replay);
  CtBackup::StagedRestore replayStage;
  Check(CtBackup::StageValidated7zRestore(recoveredInput, replay / L"stage", nullptr, nullptr, replayStage).ok(),
        "The recovered archive can stage again in a separate data root");
  previousInRecovery = true;
  Check(CtBackup::CommitStagedRestore(replayStage, replay / L"Sites", replay / L"R0002", nullptr, nullptr,
        &previousInRecovery).ok() && !previousInRecovery && !fs::exists(replay / L"R0002"),
        "Replay into missing Sites reports no previous collection");
  Check(Read(replay / L"Sites" / client / L"note.ctn") == Read(note) &&
        Read(recoveredInput) == archiveBytes && Identity(recoveredInput) == inputId &&
        Read(originalArchive) == archiveBytes && Identity(originalArchive) == originalId,
        "Replay leaves both archived copies exact and restores the same bytes");
  std::cout << "RECOVERY_INPUT_CASES=in-collection-read-only-input,replay-from-recovery\n";
}

#include "ArchiveFilesystemBackupCases.inl"
#include "ArchiveFilesystemRestoreCases.inl"
#include "ArchiveFilesystemBackupDeepCases.inl"
#include "ArchiveFilesystemRestoreDeepCases.inl"
#include "ArchiveFilesystemBackupEdgeCases.inl"
#include "ArchiveFilesystemRestoreEdgeCases.inl"
#include "ArchiveFilesystemBackupMetadataCases.inl"
#include "ArchiveFilesystemRestoreSealCases.inl"
#include "ArchiveFilesystemBackupMoreCases.inl"
#include "ArchiveFilesystemRestoreMoreCases.inl"
#include "ArchiveFilesystemPathOwnershipCases.inl"
#include "ArchiveFilesystemBackupFurtherCases.inl"
#include "ArchiveFilesystemRestoreFurtherCases.inl"
#include "ArchiveFilesystemCollectionCases.inl"
#include "ArchiveFilesystemBackupNextCases.inl"
#include "ArchiveFilesystemRestoreNextCases.inl"
#include "ArchiveFilesystemContinuityCases.inl"
#include "ArchiveFilesystemBackupAdditionalCases.inl"
#include "ArchiveFilesystemRestoreAdditionalCases.inl"
#include "ArchiveFilesystemIndependentCases.inl"
} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    if (argc != 2) return 2;
    const std::wstring_view mode = argv[1];
    if (mode != L"--sharing" && mode != L"--recovery-input" &&
        mode != L"--backup-locks" && mode != L"--restore-input-lock-before" &&
        mode != L"--restore-input-lock" && mode != L"--restore-commit-locks" &&
        mode != L"--backup-deep" && mode != L"--restore-deep" &&
        mode != L"--restore-packed-lock-before" && mode != L"--restore-packed-lock" &&
        mode != L"--backup-alias-before" && mode != L"--backup-alias" &&
        mode != L"--backup-edge" && mode != L"--restore-edge" &&
        mode != L"--backup-metadata-before" && mode != L"--backup-metadata" &&
        mode != L"--backup-empty-clients" && mode != L"--restore-seal" &&
        mode != L"--backup-dir-metadata-before" && mode != L"--backup-dir-metadata" &&
        mode != L"--backup-folder-destination" && mode != L"--backup-source-growth" &&
        mode != L"--backup-unicode-forms" && mode != L"--restore-recovery-share" &&
        mode != L"--restore-compatible-live" && mode != L"--restore-slot-fidelity" &&
        mode != L"--backup-managed-destination" && mode != L"--restore-live-file" &&
        mode != L"--backup-missing-destination-before" && mode != L"--backup-missing-destination" &&
        mode != L"--backup-empty-source-retry" && mode != L"--backup-shared-source" &&
        mode != L"--backup-mixed-case-destination" && mode != L"--restore-recovery-budget" &&
        mode != L"--restore-manifest-share" && mode != L"--restore-stage-overlap" &&
        mode != L"--backup-root-file" && mode != L"--restore-two-recoveries" &&
        mode != L"--backup-case-renamed-client" && mode != L"--backup-literal-paths" &&
        mode != L"--backup-extension-refusal" && mode != L"--restore-input-return" &&
        mode != L"--restore-stage-directory-share" && mode != L"--restore-empty-stage-reuse" &&
        mode != L"--backup-missing-client" && mode != L"--restore-read-only-note" &&
        mode != L"--backup-preparing-stop" && mode != L"--backup-timestamp-precision" &&
        mode != L"--backup-shared-destination" && mode != L"--restore-progress" &&
        mode != L"--restore-exclusive-live-note" && mode != L"--restore-stage-parent-file" &&
        mode != L"--backup-missing-sites" && mode != L"--restore-distinct-parents" &&
        mode != L"--restore-moved-input-before" && mode != L"--restore-moved-input")
      return 2;
    const fs::path root = CTSPACES_ARCHIVE_FILESYSTEM_FIXTURE;
    if (!root.is_absolute() || root.lexically_normal() != root) return 2;
    CheckOrdinaryAncestors(root);
    const auto fixture = root / mode.substr(2);
    Check(fs::create_directory(fixture), "Fresh owned fixture directory created");
    _7zSetHInstance(GetModuleHandleW(nullptr));
    if (mode == L"--sharing") SharingRound(fixture);
    else if (mode == L"--recovery-input") RecoveryInputRound(fixture);
    else if (mode == L"--backup-locks") BackupLocksRound(fixture);
    else if (mode == L"--restore-commit-locks") RestoreCommitLocksRound(fixture);
    else if (mode == L"--backup-deep") BackupDeepRound(fixture);
    else if (mode == L"--restore-deep") RestoreDeepRound(fixture);
    else if (mode == L"--restore-packed-lock-before" || mode == L"--restore-packed-lock")
      RestorePackedLockRound(fixture, mode == L"--restore-packed-lock-before");
    else if (mode == L"--backup-alias-before" || mode == L"--backup-alias")
      BackupAliasRound(fixture, mode == L"--backup-alias-before");
    else if (mode == L"--backup-edge") BackupEdgeRound(fixture);
    else if (mode == L"--restore-edge") RestoreEdgeRound(fixture);
    else if (mode == L"--backup-metadata-before" || mode == L"--backup-metadata")
      BackupMetadataRound(fixture, mode == L"--backup-metadata-before");
    else if (mode == L"--backup-empty-clients") BackupEmptyClientsRound(fixture);
    else if (mode == L"--restore-seal") RestoreSealRound(fixture);
    else if (mode == L"--backup-dir-metadata-before" || mode == L"--backup-dir-metadata")
      BackupDirectoryMetadataRound(fixture, mode == L"--backup-dir-metadata-before");
    else if (mode == L"--backup-folder-destination") BackupFolderDestinationRound(fixture);
    else if (mode == L"--backup-source-growth") BackupSourceGrowthRound(fixture);
    else if (mode == L"--backup-unicode-forms") BackupUnicodeFormsRound(fixture);
    else if (mode == L"--restore-recovery-share") RestoreRecoveryShareRound(fixture);
    else if (mode == L"--restore-compatible-live") RestoreCompatibleLiveRound(fixture);
    else if (mode == L"--restore-slot-fidelity") RestoreSlotFidelityRound(fixture);
    else if (mode == L"--backup-managed-destination") BackupManagedDestinationRound(fixture);
    else if (mode == L"--restore-live-file") RestoreLiveFileRound(fixture);
    else if (mode == L"--backup-missing-destination-before" || mode == L"--backup-missing-destination")
      BackupMissingDestinationRound(fixture, mode == L"--backup-missing-destination-before");
    else if (mode == L"--backup-empty-source-retry") BackupEmptySourceRetryRound(fixture);
    else if (mode == L"--backup-shared-source") BackupSharedSourceRound(fixture);
    else if (mode == L"--backup-mixed-case-destination") BackupCasePreservingDestinationRound(fixture);
    else if (mode == L"--restore-recovery-budget") RestoreRecoveryBudgetRound(fixture);
    else if (mode == L"--restore-manifest-share") RestoreManifestShareRound(fixture);
    else if (mode == L"--restore-stage-overlap") RestoreStageOverlapRound(fixture);
    else if (mode == L"--backup-root-file") BackupRootFileSelectionRound(fixture);
    else if (mode == L"--restore-two-recoveries") RestoreTwoRecoveriesRound(fixture);
    else if (mode == L"--backup-case-renamed-client") BackupCaseRenamedClientRound(fixture);
    else if (mode == L"--backup-literal-paths") BackupLiteralPathsRound(fixture);
    else if (mode == L"--backup-extension-refusal") BackupExtensionRefusalRound(fixture);
    else if (mode == L"--restore-input-return") RestoreInputReturnRound(fixture);
    else if (mode == L"--restore-stage-directory-share") RestoreStageDirectoryShareRound(fixture);
    else if (mode == L"--restore-empty-stage-reuse") RestoreEmptyStageReuseRound(fixture);
    else if (mode == L"--backup-missing-client") BackupMissingClientRound(fixture);
    else if (mode == L"--restore-read-only-note") RestoreReadOnlyNoteRound(fixture);
    else if (mode == L"--backup-preparing-stop") BackupPreparingStopRound(fixture);
    else if (mode == L"--backup-timestamp-precision") BackupTimestampPrecisionRound(fixture);
    else if (mode == L"--backup-shared-destination") BackupSharedDestinationRound(fixture);
    else if (mode == L"--restore-progress") RestoreProgressRound(fixture);
    else if (mode == L"--restore-exclusive-live-note") RestoreExclusiveLiveNoteRound(fixture);
    else if (mode == L"--restore-stage-parent-file") RestoreStageParentFileRound(fixture);
    else if (mode == L"--backup-missing-sites") BackupMissingSitesRound(fixture);
    else if (mode == L"--restore-distinct-parents") RestoreDistinctParentsRound(fixture);
    else if (mode == L"--restore-moved-input-before" || mode == L"--restore-moved-input")
      RestoreMovedInputRound(fixture, mode == L"--restore-moved-input-before");
    else RestoreInputLockRound(fixture, mode == L"--restore-input-lock-before");
    std::cout << "VERSION=" << CTSPACES_VERSION_TEXT << "\nCHECKS=" << checks << "\nFAILURES=0\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Archive filesystem round failed after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
