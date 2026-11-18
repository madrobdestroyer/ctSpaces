void StopBackupBeforeScanning(void *user, const _7zProgressInfo &info) {
  auto &phases = *static_cast<std::vector<_7zProgressPhase> *>(user);
  phases.push_back(info.phase);
  if (info.phase == _7zProgressPhase::Preparing)
    throw std::runtime_error("Stop the private backup before scanning");
}

void BackupPreparingStopRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto note = sites / L"Client/note.ctn";
  const auto archive = fixture / L"out/prior.7z";
  const std::string oldBytes = "notebook in the previous backup";
  const std::string currentBytes = "notebook revised before the early stop";
  Write(note, oldBytes);
  fs::create_directories(archive.parent_path());
  Check(BackUp(sites, archive).ok() && CrcValid(archive),
        "The prior notebook produces a verified archive");
  const auto priorId = Identity(archive), sourceId = Identity(note);
  const auto priorBytes = Read(archive);
  Write(note, currentBytes);
  std::vector<_7zProgressPhase> phases;
  CtBackup::BackupOptions options;
  options.selectedClientNames = std::vector<std::wstring>{L"Client"};
  options.detailedProgress = StopBackupBeforeScanning;
  options.detailedProgressUser = &phases;
  const auto stopped = BackUp(sites, archive, options);
  Check(stopped.code == E_ABORT && phases == std::vector<_7zProgressPhase>{_7zProgressPhase::Preparing},
        "Stopping at Preparing aborts before scanning, verification or publication");
  Check(Identity(archive) == priorId && Read(archive) == priorBytes &&
        CrcValid(archive) && HasOnlyArchive(archive),
        "The early stop preserves the exact previous archive without creating temporary files");
  CtBackup::StagedRestore prior;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"prior", nullptr, nullptr, prior).ok() &&
        prior.profileCount == 1 && Read(prior.payloadDir / L"Client/note.ctn") == oldBytes,
        "The preserved archive still stages the previous notebook");
  options.detailedProgress = nullptr;
  options.detailedProgressUser = nullptr;
  Check(BackUp(sites, archive, options).ok() && CrcValid(archive) && HasOnlyArchive(archive) &&
        Identity(archive) != priorId, "The same destination publishes on a clean retry");
  CtBackup::StagedRestore current;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"current", nullptr, nullptr, current).ok() &&
        current.profileCount == 1 && Read(current.payloadDir / L"Client/note.ctn") == currentBytes &&
        Read(prior.payloadDir / L"Client/note.ctn") == oldBytes,
        "Retry stages the revised notebook and leaves the earlier stage intact");
  Check(Identity(note) == sourceId && Read(note) == currentBytes,
        "The source notebook object is unchanged by both attempts");
  std::cout << "BACKUP_PREPARING_STOP_CASE=early-abort-preservation-retry\n";
}

struct BackupPreciseTimes {
  ULONGLONG created, accessed, written;
  bool MatchesCreationAndWrite(const BackupPreciseTimes &other) const {
    return created == other.created && written == other.written;
  }
};

BackupPreciseTimes CaptureBackupPreciseTimes(const fs::path &path) {
  const auto ticks = [](const FILETIME &value) {
    return (static_cast<ULONGLONG>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
  };
  const FileHandle file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (file.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.value, &info) ||
      (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    throw std::runtime_error("Cannot read times from an ordinary private backup fixture object");
  return {ticks(info.ftCreationTime), ticks(info.ftLastAccessTime), ticks(info.ftLastWriteTime)};
}

void AssignBackupPreciseTimes(const fs::path &path, const BackupPreciseTimes &times) {
  const auto convert = [](ULONGLONG ticks) {
    return FILETIME{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
  };
  const FILETIME created = convert(times.created), accessed = convert(times.accessed), written = convert(times.written);
  const FileHandle file(CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (file.value == INVALID_HANDLE_VALUE || !SetFileTime(file.value, &created, &accessed, &written))
    throw std::runtime_error("Cannot assign precise times on the private backup fixture object");
}

void BackupTimestampPrecisionRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto note = fs::path(L"Client/note.ctn"), empty = fs::path(L"Client/Empty folder");
  const auto archive = fixture / L"out/times.7z";
  const std::string contents = "notebook with subsecond native timestamps";
  constexpr BackupPreciseTimes noteTimes{127000000000000001ULL, 131000000000000017ULL, 133000000000000121ULL};
  constexpr BackupPreciseTimes folderTimes{128000000000000023ULL, 132000000000000077ULL, 134000000000000199ULL};
  Write(sites / note, contents);
  fs::create_directory(sites / empty);
  fs::create_directories(archive.parent_path());
  const auto noteId = Identity(sites / note), folderId = Identity(sites / empty);
  AssignBackupPreciseTimes(sites / note, noteTimes);
  AssignBackupPreciseTimes(sites / empty, folderTimes);
  const auto sourceNoteTimes = CaptureBackupPreciseTimes(sites / note);
  const auto sourceFolderTimes = CaptureBackupPreciseTimes(sites / empty);
  Check(sourceNoteTimes.MatchesCreationAndWrite(noteTimes) && sourceFolderTimes.MatchesCreationAndWrite(folderTimes),
        "The source file and directory use distinct exact NTFS creation and write times");
  Check(BackUp(sites, archive).ok() && CrcValid(archive) && HasOnlyArchive(archive),
        "The source with precise timestamps produces a verified archive");
  const auto archiveId = Identity(archive);
  const auto archiveBytes = Read(archive);
  const auto data = fixture / L"data", live = data / L"Sites", recovery = data / L"recovery", stage = data / L"stage";
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() && staged.profileCount == 1,
        "Restore stages the exact timestamp fixture");
  const auto stagedNoteTimes = CaptureBackupPreciseTimes(staged.payloadDir / note);
  const auto stagedFolderTimes = CaptureBackupPreciseTimes(staged.payloadDir / empty);
  Check(stagedNoteTimes.MatchesCreationAndWrite(noteTimes) && stagedFolderTimes.MatchesCreationAndWrite(folderTimes),
        "Both staged objects preserve exact creation and write times to 100 ns");
  bool previousInRecovery = true;
  Check(CtBackup::CommitStagedRestore(staged, live, recovery, nullptr, nullptr, &previousInRecovery).ok() &&
        !previousInRecovery && !fs::exists(recovery) && !fs::exists(stage),
        "The verified stage installs into an initially absent Sites");
  const auto liveNoteTimes = CaptureBackupPreciseTimes(live / note);
  const auto liveFolderTimes = CaptureBackupPreciseTimes(live / empty);
  Check(liveNoteTimes.MatchesCreationAndWrite(noteTimes) && liveFolderTimes.MatchesCreationAndWrite(folderTimes),
        "Committed file and folder preserve exact creation and write times");
  Check(Read(live / note) == contents && fs::is_empty(live / empty),
        "The committed payload contains the exact notebook and empty directory");
  Check(Identity(sites / note) == noteId && Identity(sites / empty) == folderId &&
        CaptureBackupPreciseTimes(sites / note).MatchesCreationAndWrite(noteTimes) &&
        CaptureBackupPreciseTimes(sites / empty).MatchesCreationAndWrite(folderTimes) &&
        Identity(archive) == archiveId && Read(archive) == archiveBytes,
        "The source objects and archive stay exact through staging and installation");
  // Windows may update access time independently when a file is read.
  std::cout << "BACKUP_ACCESS_TIME_OBSERVED=source-file:" << sourceNoteTimes.accessed
            << ",staged-file:" << stagedNoteTimes.accessed << ",live-file:" << liveNoteTimes.accessed
            << ",source-directory:" << sourceFolderTimes.accessed
            << ",staged-directory:" << stagedFolderTimes.accessed << ",live-directory:" << liveFolderTimes.accessed << '\n';
  std::cout << "BACKUP_TIMESTAMP_PRECISION_CASE=exact-created-and-written\n";
}

struct BackupDestinationPublicationGate {
  HANDLE ready;
  HANDLE proceed;
  bool reached = false;
};

void GateBackupDestinationPublication(void *user, const _7zProgressInfo &info) {
  if (info.phase != _7zProgressPhase::Publishing) return;
  auto &gate = *static_cast<BackupDestinationPublicationGate *>(user);
  gate.reached = SetEvent(gate.ready) != FALSE;
  if (!gate.reached || WaitForSingleObject(gate.proceed, 15000) != WAIT_OBJECT_0)
    throw std::runtime_error("The private backup publication rendezvous timed out");
}

void BackupSharedDestinationRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto firstNote = fs::path(L"First/note.ctn"), secondNote = fs::path(L"Second/note.ctn");
  const auto archive = fixture / L"out/shared.7z";
  const std::string firstBytes = "first independently verified notebook";
  const std::string secondBytes = "second independently verified notebook";
  Write(sites / firstNote, firstBytes);
  Write(sites / secondNote, secondBytes);
  fs::create_directories(archive.parent_path());
  const auto firstSourceId = Identity(sites / firstNote), secondSourceId = Identity(sites / secondNote);
  Check(BackUp(sites, archive).ok() && CrcValid(archive), "The initial two-client backup verifies");
  const auto priorId = Identity(archive);
  const auto priorBytes = Read(archive);
  const FileHandle firstReady(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  const FileHandle secondReady(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  const FileHandle firstProceed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  const FileHandle secondProceed(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  Check(firstReady.value && secondReady.value && firstProceed.value && secondProceed.value,
        "Private events coordinate both independent verified publications");
  BackupDestinationPublicationGate firstGate{firstReady.value, firstProceed.value};
  BackupDestinationPublicationGate secondGate{secondReady.value, secondProceed.value};
  CtBackup::BackupOptions firstOptions, secondOptions;
  firstOptions.selectedClientNames = std::vector<std::wstring>{L"First"};
  secondOptions.selectedClientNames = std::vector<std::wstring>{L"Second"};
  firstOptions.detailedProgress = GateBackupDestinationPublication;
  secondOptions.detailedProgress = GateBackupDestinationPublication;
  firstOptions.detailedProgressUser = &firstGate;
  secondOptions.detailedProgressUser = &secondGate;
  CtBackup::Result firstResult, secondResult;
  std::jthread firstWorker([&] { firstResult = BackUp(sites, archive, firstOptions); });
  const bool firstWaiting = WaitForSingleObject(firstReady.value, 5000) == WAIT_OBJECT_0;
  std::jthread secondWorker([&] { secondResult = BackUp(sites, archive, secondOptions); });
  const bool secondWaiting = WaitForSingleObject(secondReady.value, 5000) == WAIT_OBJECT_0;
  const bool priorExact = firstWaiting && secondWaiting && Identity(archive) == priorId && Read(archive) == priorBytes;
  const bool firstReleased = SetEvent(firstProceed.value) != FALSE;
  firstWorker.join();
  Check(firstWaiting && secondWaiting && firstGate.reached && priorExact,
        "Both jobs independently verify their staged archives before either replaces the original");
  Check(firstReleased && firstResult.ok() && CrcValid(archive) && Identity(archive) != priorId,
        "The first job publishes one complete verified archive");
  const auto firstId = Identity(archive);
  CtBackup::StagedRestore firstStage;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"stage-first", nullptr, nullptr, firstStage).ok() &&
        firstStage.profileCount == 1 && Read(firstStage.payloadDir / firstNote) == firstBytes &&
        !fs::exists(firstStage.payloadDir / secondNote),
        "The first publication independently stages only its selected client");
  const bool secondReleased = SetEvent(secondProceed.value) != FALSE;
  secondWorker.join();
  Check(secondReleased && secondGate.reached && secondResult.ok() &&
        CrcValid(archive) && Identity(archive) != firstId && HasOnlyArchive(archive),
        "The second job atomically publishes its own complete archive and leaves no temporary files");
  CtBackup::StagedRestore secondStage;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"stage-second", nullptr, nullptr, secondStage).ok() &&
        secondStage.profileCount == 1 && Read(secondStage.payloadDir / secondNote) == secondBytes &&
        !fs::exists(secondStage.payloadDir / firstNote) && Read(firstStage.payloadDir / firstNote) == firstBytes,
        "The final archive stages only the second client and leaves the first stage exact");
  Check(Identity(sites / firstNote) == firstSourceId && Read(sites / firstNote) == firstBytes &&
        Identity(sites / secondNote) == secondSourceId && Read(sites / secondNote) == secondBytes,
        "Both source notebook objects survive the overlapping publications unchanged");
  std::cout << "BACKUP_SHARED_DESTINATION_CASE=serialized-complete-publications\n";
}
