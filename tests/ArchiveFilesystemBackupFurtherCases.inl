void BackupMissingDestinationRound(const fs::path &fixture, bool before) {
  const auto sites = fixture / L"src/Sites";
  const auto note = sites / L"Client/note.ctn";
  const auto existing = fixture / L"out/prior.7z";
  const auto archive = existing.parent_path() / L"missing/copy.7z";
  Write(note, "notebook for the missing destination folder");
  fs::create_directories(existing.parent_path());
  Check(BackUp(sites, existing).ok() && CrcValid(existing),
        "An independent valid backup already exists in the destination ancestor");
  const auto existingId = Identity(existing), sourceId = Identity(note);
  const auto existingBytes = Read(existing), sourceBytes = Read(note);
  const auto refused = BackUp(sites, archive);
  Check(refused.code == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) ||
        refused.code == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND),
        "Backup retains Windows' missing folder error");
  Check(before ? refused.details == L"A restore directory could not be bound safely."
               : refused.details.find(L"backup folder") != std::wstring::npos &&
                 refused.details.find(archive.parent_path().native()) != std::wstring::npos &&
                 refused.details.find(L"existing folder") != std::wstring::npos,
        before ? "The baseline backup error incorrectly identifies restore"
               : "The backup error identifies the unavailable folder and how to retry");
  Check(!fs::exists(archive.parent_path()) && HasOnlyArchive(existing) &&
        Identity(existing) == existingId && Read(existing) == existingBytes,
        "The failed attempt does not create the folder or affect the independent archive");
  fs::create_directory(archive.parent_path());
  Check(BackUp(sites, archive).ok() && CrcValid(archive) && HasOnlyArchive(archive),
        "The same destination works when its parent exists");
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"stage", nullptr, nullptr, staged).ok() &&
        staged.profileCount == 1 && Read(staged.payloadDir / L"Client/note.ctn") == sourceBytes,
        "The retried backup stages the exact source notebook");
  Check(Identity(existing) == existingId && Read(existing) == existingBytes &&
        Identity(note) == sourceId && Read(note) == sourceBytes,
        "Failure and retry preserve the independent archive and exact source object");
  std::cout << "BACKUP_MISSING_DESTINATION_CASE="
            << (before ? "restore-wording-observed" : "backup-folder-guidance") << '\n';
}

void BackupEmptySourceRetryRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto originalClient = sites / L"Departed";
  const auto retainedClient = sites.parent_path() / L"saved-client";
  const auto archive = fixture / L"out/backup.7z";
  const auto oldNote = originalClient / L"note.ctn";
  const std::string oldBytes = "prior notebook from the temporarily empty collection";
  Write(oldNote, oldBytes);
  fs::create_directories(archive.parent_path());
  Check(BackUp(sites, archive).ok() && CrcValid(archive),
        "The original one-client collection creates a valid backup");
  const auto archiveId = Identity(archive), clientId = Identity(originalClient), oldNoteId = Identity(oldNote);
  const auto archiveBytes = Read(archive);
  fs::rename(originalClient, retainedClient);
  Check(fs::is_empty(sites) && Identity(retainedClient) == clientId,
        "The source folder is genuinely empty and the prior client is retained separately");
  const auto refused = BackUp(sites, archive);
  Check(refused.code == HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        "An empty collection explicitly refuses to replace the last populated backup");
  Check(HasOnlyArchive(archive) && Identity(archive) == archiveId &&
        Read(archive) == archiveBytes && CrcValid(archive),
        "Refusal keeps the last archive exact and leaves no sibling artifacts");
  CtBackup::StagedRestore prior;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"prior", nullptr, nullptr, prior).ok() &&
        prior.profileCount == 1 && Read(prior.payloadDir / L"Departed/note.ctn") == oldBytes,
        "The preserved archive still stages the original notebook");
  const auto current = sites / L"Arrived/note.ctn";
  const std::string newBytes = "different notebook from the replacement client";
  Write(current, newBytes);
  const auto currentId = Identity(current);
  Check(BackUp(sites, archive).ok() && CrcValid(archive) && HasOnlyArchive(archive) &&
        Identity(archive) != archiveId && Read(archive) != archiveBytes,
        "After a new client arrives, retry publishes a different verified archive");
  CtBackup::StagedRestore retried;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"retried", nullptr, nullptr, retried).ok() &&
        retried.profileCount == 1 && Read(retried.payloadDir / L"Arrived/note.ctn") == newBytes &&
        !fs::exists(retried.payloadDir / L"Departed"),
        "The retry stages exactly the current client");
  Check(Identity(current) == currentId && Read(current) == newBytes &&
        Identity(retainedClient) == clientId && Identity(retainedClient / L"note.ctn") == oldNoteId &&
        Read(retainedClient / L"note.ctn") == oldBytes,
        "Both old and new source notebook objects remain exact");
  std::cout << "BACKUP_EMPTY_SOURCE_CASE=preserved-archive-and-retry\n";
}

struct SharedBackupGate {
  HANDLE ready;
  HANDLE peerReady;
  bool rendezvous = false;
  bool entered = false;
};

void MeetSharedBackupReader(void *user, const _7zProgressInfo &info) {
  auto &gate = *static_cast<SharedBackupGate *>(user);
  if (info.phase != _7zProgressPhase::Compressing || gate.entered) return;
  gate.entered = true;
  gate.rendezvous = SetEvent(gate.ready) &&
      WaitForSingleObject(gate.peerReady, 10000) == WAIT_OBJECT_0;
}

void BackupSharedSourceRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto shared = fs::path(L"Shared/note.ctn");
  const auto alpha = fs::path(L"Alpha/note.ctn"), beta = fs::path(L"Beta/note.ctn");
  const std::string sharedBytes = "shared source notebook";
  const std::string alphaBytes = "only in the first selected backup";
  const std::string betaBytes = "only in the second selected backup";
  Write(sites / shared, sharedBytes);
  Write(sites / alpha, alphaBytes);
  Write(sites / beta, betaBytes);
  const auto sharedId = Identity(sites / shared);
  const auto alphaId = Identity(sites / alpha), betaId = Identity(sites / beta);
  const auto first = fixture / L"first/copy.7z", second = fixture / L"second/copy.7z";
  fs::create_directories(first.parent_path());
  fs::create_directories(second.parent_path());
  const FileHandle firstReady(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  const FileHandle secondReady(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  Check(firstReady.value && secondReady.value, "Two private events coordinate the overlapping source reads");
  SharedBackupGate firstGate{firstReady.value, secondReady.value}, secondGate{secondReady.value, firstReady.value};
  CtBackup::BackupOptions firstOptions, secondOptions;
  firstOptions.selectedClientNames = std::vector<std::wstring>{L"Shared", L"Alpha"};
  secondOptions.selectedClientNames = std::vector<std::wstring>{L"Shared", L"Beta"};
  firstOptions.detailedProgress = MeetSharedBackupReader;
  secondOptions.detailedProgress = MeetSharedBackupReader;
  firstOptions.detailedProgressUser = &firstGate;
  secondOptions.detailedProgressUser = &secondGate;
  CtBackup::Result firstResult, secondResult;
  {
    const std::jthread firstWorker([&] { firstResult = BackUp(sites, first, firstOptions); });
    const std::jthread secondWorker([&] { secondResult = BackUp(sites, second, secondOptions); });
  }
  Check(firstGate.rendezvous && secondGate.rendezvous,
        "Both exports reach compression after binding their overlapping source selections");
  Check(firstResult.ok() && secondResult.ok() && CrcValid(first) && CrcValid(second),
        "Both overlapping exports publish independently verified archives");
  Check(HasOnlyArchive(first) && HasOnlyArchive(second), "Each output folder contains only its own archive");
  const auto firstId = Identity(first), secondId = Identity(second);
  const auto firstBytes = Read(first), secondBytes = Read(second);
  CtBackup::StagedRestore firstStage, secondStage;
  Check(CtBackup::StageValidated7zRestore(first, fixture / L"stage-first", nullptr, nullptr, firstStage).ok() &&
        CtBackup::StageValidated7zRestore(second, fixture / L"stage-second", nullptr, nullptr, secondStage).ok() &&
        firstStage.profileCount == 2 && secondStage.profileCount == 2,
        "Both archives stage exactly two clients");
  Check(Read(firstStage.payloadDir / shared) == sharedBytes && Read(firstStage.payloadDir / alpha) == alphaBytes &&
        !fs::exists(firstStage.payloadDir / beta), "The first archive contains its exact shared and unique notebook bytes");
  Check(Read(secondStage.payloadDir / shared) == sharedBytes && Read(secondStage.payloadDir / beta) == betaBytes &&
        !fs::exists(secondStage.payloadDir / alpha), "The second archive contains its exact shared and unique notebook bytes");
  Check(Identity(sites / shared) == sharedId && Read(sites / shared) == sharedBytes &&
        Identity(sites / alpha) == alphaId && Read(sites / alpha) == alphaBytes &&
        Identity(sites / beta) == betaId && Read(sites / beta) == betaBytes,
        "The three source notebook objects retain their exact bytes and identities");
  Check(Identity(first) == firstId && Read(first) == firstBytes &&
        Identity(second) == secondId && Read(second) == secondBytes,
        "Both exact archive objects survive independent staging");
  std::cout << "BACKUP_SHARED_SOURCE_CASE=overlapping-selected-exports\n";
}

void BackupCasePreservingDestinationRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto note = sites / L"Client/note.ctn";
  const auto parent = fixture / L"Destination Folder";
  const auto archive = parent / L"Nightly Copy.7z";
  const auto alias = fixture / L"DESTINATION FOLDER/nightly copy.7Z";
  const auto neighbor = parent / L"Other Export.7z";
  const std::string original = "old notebook used for both initial archives";
  const std::string revised = "new notebook for the case-insensitive overwrite";
  Write(note, original);
  fs::create_directory(parent);
  Check(BackUp(sites, archive).ok() && BackUp(sites, neighbor).ok() &&
        CrcValid(archive) && CrcValid(neighbor), "Both initial backup files pass the CRC scan");
  const auto archiveId = Identity(archive), neighborId = Identity(neighbor), sourceId = Identity(note);
  const auto parentId = Identity(parent);
  const auto archiveBytes = Read(archive), neighborBytes = Read(neighbor);
  Check(Identity(alias) == archiveId && Identity(alias.parent_path()) == parentId,
        "The mixed-case spelling names the exact existing archive and directory");
  Write(note, revised);
  Check(BackUp(sites, alias).ok() && CrcValid(alias), "Backup accepts the mixed-case filename and .7Z extension");
  Check(Identity(archive) == Identity(alias) && Identity(alias) != archiveId &&
        Read(archive) != archiveBytes && Identity(alias.parent_path()) == parentId,
        "The mixed-case publication replaces the same native destination");
  Check(std::distance(fs::directory_iterator(parent), fs::directory_iterator{}) == 2 &&
        Identity(neighbor) == neighborId && Read(neighbor) == neighborBytes,
        "The output folder holds only the new archive and the unchanged neighboring backup");
  CtBackup::StagedRestore latest, prior;
  Check(CtBackup::StageValidated7zRestore(alias, fixture / L"latest", nullptr, nullptr, latest).ok() &&
        latest.profileCount == 1 && Read(latest.payloadDir / L"Client/note.ctn") == revised,
        "Restore accepts the mixed-case archive name and stages the new bytes");
  Check(CtBackup::StageValidated7zRestore(neighbor, fixture / L"prior", nullptr, nullptr, prior).ok() &&
        prior.profileCount == 1 && Read(prior.payloadDir / L"Client/note.ctn") == original,
        "The separate neighboring backup still stages the original notebook");
  Check(Identity(note) == sourceId && Read(note) == revised,
        "Publication and staging leave the current source notebook intact");
  std::cout << "BACKUP_MIXED_CASE_DESTINATION_CASE=mixed-case-extension-and-native-path\n";
}
