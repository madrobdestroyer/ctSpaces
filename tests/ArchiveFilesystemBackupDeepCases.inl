struct BackupLateFile {
  fs::path path;
  std::string bytes;
  bool created = false;
};

void AddFileAtBackupRecheck(void *user, const _7zProgressInfo &info) {
  auto &addition = *static_cast<BackupLateFile *>(user);
  if (info.phase != _7zProgressPhase::CheckingSource || addition.created)
    return;
  Write(addition.path, addition.bytes);
  addition.created = true;
}

void BackupLateSelectedFile(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto payload = fs::path(L"Alpha/Profile/state.bin");
  const auto late = fs::path(L"Alpha/Profile/arrived.bin");
  const auto archive = fixture / L"out/previous.7z";
  const std::string original = "prior selected client state";
  const std::string revised = "current selected client state";
  Write(sites / payload, original);
  fs::create_directories(archive.parent_path());
  CtBackup::BackupOptions options;
  options.selectedClientNames = std::vector<std::wstring>{L"Alpha"};
  Check(BackUp(sites, archive, options).ok() && CrcValid(archive),
        "Initial selected backup publishes with valid CRC");
  const auto archiveId = Identity(archive);
  const auto archiveBytes = Read(archive);
  const auto sourceId = Identity(sites / payload);
  Write(sites / payload, revised);

  BackupLateFile addition{sites / late, "file created at the source recheck"};
  options.detailedProgress = AddFileAtBackupRecheck;
  options.detailedProgressUser = &addition;
  const auto refused = BackUp(sites, archive, options);
  Check(addition.created && Read(sites / late) == addition.bytes,
        "Ordinary file appears only when compression reaches its source recheck");
  const auto lateId = Identity(sites / late);
  Check(refused.code == HRESULT_FROM_WIN32(ERROR_FILE_INVALID),
        "Post-compression selected file inventory mismatch is refused");
  Check(Identity(archive) == archiveId && Read(archive) == archiveBytes,
        "Refusal preserves the exact prior destination object and bytes");
  Check(HasOnlyArchive(archive) && CrcValid(archive),
        "Refusal clears temporary artifacts and keeps the prior archive valid");
  CtBackup::StagedRestore prior;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"prior", nullptr, nullptr, prior).ok() &&
        prior.profileCount == 1 && Read(prior.payloadDir / payload) == original &&
        !fs::exists(prior.payloadDir / late),
        "Prior archive still stages the original file without the late arrival");

  options.detailedProgress = nullptr;
  options.detailedProgressUser = nullptr;
  Check(BackUp(sites, archive, options).ok() && CrcValid(archive),
        "Retry verifies the stabilized selected tree");
  Check(Identity(archive) != archiveId && Read(archive) != archiveBytes && HasOnlyArchive(archive),
        "Verified retry replaces only the destination with the new archive");
  CtBackup::StagedRestore retried;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"next", nullptr, nullptr, retried).ok() &&
        retried.profileCount == 1 && Read(retried.payloadDir / payload) == revised &&
        Read(retried.payloadDir / late) == addition.bytes,
        "Retried archive stages both exact files from the new selected inventory");
  Check(Identity(sites / payload) == sourceId && Read(sites / payload) == revised &&
        Identity(sites / late) == lateId && Read(sites / late) == addition.bytes,
        "Refusal and retry leave both ordinary source objects unchanged");
}

DWORD BackupFixtureAttributeBits(const fs::path &path, DWORD mask) {
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES)
    throw std::runtime_error("Could not read private fixture attributes");
  return attributes & mask;
}

void BackupNestedAttributes(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto hiddenDir = fs::path(L"Alpha/Profile/Hidden");
  const auto hiddenFile = hiddenDir / L"state.bin";
  const auto visibleFile = fs::path(L"Alpha/Profile/Visible/state.bin");
  constexpr char visibleData[] = "visible state\0with a binary suffix";
  const std::string visibleBytes(visibleData, sizeof(visibleData) - 1);
  Write(sites / hiddenFile, "");
  Write(sites / visibleFile, visibleBytes);
  constexpr DWORD fileFlags = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
  Check(SetFileAttributesW((sites / hiddenDir).c_str(), FILE_ATTRIBUTE_HIDDEN) != FALSE &&
        SetFileAttributesW((sites / hiddenFile).c_str(), fileFlags) != FALSE,
        "Fixture uses real native attributes on an ordinary directory and empty file");
  Check(BackupFixtureAttributeBits(sites / hiddenDir, fileFlags) == FILE_ATTRIBUTE_HIDDEN &&
        BackupFixtureAttributeBits(sites / hiddenFile, fileFlags) == fileFlags &&
        BackupFixtureAttributeBits(sites / visibleFile, fileFlags) == 0,
        "Hidden directory and system file flags differ from the ordinary twin basename");
  const auto sourceDirId = Identity(sites / hiddenDir);
  const auto sourceHiddenId = Identity(sites / hiddenFile);
  const auto sourceVisibleId = Identity(sites / visibleFile);
  const auto archive = fixture / L"out/flags.7z";
  fs::create_directories(archive.parent_path());
  Check(BackUp(sites, archive).ok() && CrcValid(archive) && HasOnlyArchive(archive),
        "Native backup includes the flagged ordinary subtree and completes verification");
  const auto archiveId = Identity(archive);
  const auto archiveBytes = Read(archive);

  CtBackup::StagedRestore staged;
  const auto stage = fixture / L"data/stage";
  Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() &&
        staged.profileCount == 1,
        "Verified archive stages one client through the real SDK");
  Check(Read(staged.payloadDir / hiddenFile).empty() &&
        Read(staged.payloadDir / visibleFile) == visibleBytes,
        "Staging preserves the empty hidden file and separate binary twin basename");
  Check(BackupFixtureAttributeBits(staged.payloadDir / hiddenDir, fileFlags) == FILE_ATTRIBUTE_HIDDEN &&
        BackupFixtureAttributeBits(staged.payloadDir / hiddenFile, fileFlags) == fileFlags &&
        BackupFixtureAttributeBits(staged.payloadDir / visibleFile, fileFlags) == 0,
        "Staging restores the exact hidden and system flags without marking the visible file");
  const auto stagedHiddenId = Identity(staged.payloadDir / hiddenFile);
  const auto stagedVisibleId = Identity(staged.payloadDir / visibleFile);

  const auto live = fixture / L"data/Sites";
  const auto recovery = fixture / L"data/R0001";
  const auto oldFile = fs::path(L"Original/old.bin");
  Write(live / oldFile, "original live collection");
  const auto oldRootId = Identity(live);
  const auto oldFileId = Identity(live / oldFile);
  bool previousInRecovery = false;
  Check(CtBackup::CommitStagedRestore(staged, live, recovery, nullptr, nullptr, &previousInRecovery).ok() &&
        previousInRecovery && !fs::exists(stage),
        "Actual restore installs the flagged subtree and moves the original collection to recovery");
  Check(Identity(live / hiddenFile) == stagedHiddenId && Read(live / hiddenFile).empty() &&
        Identity(live / visibleFile) == stagedVisibleId && Read(live / visibleFile) == visibleBytes &&
        BackupFixtureAttributeBits(live / hiddenDir, fileFlags) == FILE_ATTRIBUTE_HIDDEN &&
        BackupFixtureAttributeBits(live / hiddenFile, fileFlags) == fileFlags &&
        BackupFixtureAttributeBits(live / visibleFile, fileFlags) == 0,
        "Commit preserves verified object identities, bytes and distinct native attributes");
  Check(Identity(recovery) == oldRootId && Identity(recovery / oldFile) == oldFileId &&
        Read(recovery / oldFile) == "original live collection",
        "Recovery preserves the exact previous collection and data");
  Check(Identity(sites / hiddenDir) == sourceDirId && Identity(sites / hiddenFile) == sourceHiddenId &&
        Identity(sites / visibleFile) == sourceVisibleId && Read(sites / hiddenFile).empty() &&
        Read(sites / visibleFile) == visibleBytes &&
        BackupFixtureAttributeBits(sites / hiddenDir, fileFlags) == FILE_ATTRIBUTE_HIDDEN &&
        BackupFixtureAttributeBits(sites / hiddenFile, fileFlags) == fileFlags &&
        BackupFixtureAttributeBits(sites / visibleFile, fileFlags) == 0,
        "Backup and restore leave the source subtree and native flags intact");
  Check(Identity(archive) == archiveId && Read(archive) == archiveBytes && CrcValid(archive),
        "Staging and commit leave the exact published archive intact");
}

void BackupDeepRound(const fs::path &fixture) {
  BackupLateSelectedFile(fixture / L"late");
  BackupNestedAttributes(fixture / L"attr");
  std::cout << "BACKUP_DEEP_CASES=late-selected-file-refusal,stable-retry,hidden-system-round-trip\n";
}
