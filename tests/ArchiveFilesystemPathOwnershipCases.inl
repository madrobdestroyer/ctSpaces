void BackupManagedDestinationRound(const fs::path &fixture) {
  const auto data = fixture / L"data";
  const auto sites = data / L"Sites";
  const auto relative = fs::path(L"Alpha/note.ctn");
  const auto source = sites / relative;
  const auto original = fixture / L"out/current.7z";
  const auto managedCopy = data / L"user-copy.7z";
  const std::string first = "original note preserved in an ordinary backup";
  const std::string edited = "later independent notebook bytes for a new backup";
  fs::create_directories(original.parent_path());
  Write(source, first);
  Check(BackUp(sites, original).ok() && CrcValid(original),
        "Create a valid backup at an ordinary external destination");
  Check(fs::copy_file(original, managedCopy),
        "Keep a user-owned copy directly inside the managed data folder");
  const auto managedId = Identity(managedCopy);
  const auto originalId = Identity(original);
  const auto firstArchive = Read(original);
  Write(source, edited);
  const auto editedId = Identity(source);

  const auto refused = BackUp(sites, managedCopy);
  Check(refused.code == E_INVALIDARG && refused.details.find(L"data folder") != std::wstring::npos,
        "Backup refuses a chosen destination directly inside the managed data folder");
  Check(Identity(managedCopy) == managedId && Read(managedCopy) == firstArchive && CrcValid(managedCopy),
        "Refusal keeps the exact existing managed copy valid");
  Check(Identity(source) == editedId && Read(source) == edited &&
        std::distance(fs::directory_iterator(data), fs::directory_iterator{}) == 2 &&
        Identity(original) == originalId && Read(original) == firstArchive && HasOnlyArchive(original),
        "Refusal preserves the live edit and existing external archive without creating temporary files");
  Check(BackUp(sites, original).ok() && Identity(original) != originalId &&
        CrcValid(original) && HasOnlyArchive(original),
        "Retry at the valid external path publishes a verified replacement");
  CtBackup::StagedRestore oldStage, newStage;
  Check(CtBackup::StageValidated7zRestore(managedCopy, fixture / L"old", nullptr, nullptr, oldStage).ok() &&
        oldStage.profileCount == 1 && Read(oldStage.payloadDir / relative) == first,
        "The preserved managed copy still stages the original notebook");
  Check(CtBackup::StageValidated7zRestore(original, fixture / L"new", nullptr, nullptr, newStage).ok() &&
        newStage.profileCount == 1 && Read(newStage.payloadDir / relative) == edited,
        "The external retry stages the later notebook independently");
  Check(Identity(managedCopy) == managedId && Read(managedCopy) == firstArchive &&
        Identity(source) == editedId && Read(source) == edited,
        "Staging both backups preserves the managed copy and live notebook");
  std::cout << "BACKUP_MANAGED_DESTINATION_CASES=managed-copy-refusal,external-retry,old-and-new-stage\n";
}

void RestoreLiveFileRound(const fs::path &fixture) {
  const auto source = fixture / L"src/Sites";
  const auto note = fs::path(L"Incoming/note.ctn");
  const auto archive = fixture / L"input.7z";
  const auto stage = fixture / L"stage";
  const auto data = fixture / L"data";
  const auto live = data / L"Sites";
  const auto recovery = data / L"R0001";
  const auto saved = data / L"user-file.bin";
  const std::string incoming = "verified notebook from an ordinary independent backup";
  const std::string occupant = "regular user-owned file occupying the live name";
  Write(source / note, incoming);
  Write(live, occupant);
  Check(BackUp(source, archive).ok() && CrcValid(archive),
        "Create a verified archive independently of the occupied live path");
  const auto archiveId = Identity(archive);
  const auto archiveBytes = Read(archive);
  const auto liveId = Identity(live);
  const auto parentId = Identity(data);
  const auto sourceId = Identity(source / note);
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() &&
        staged.validation && staged.profileCount == 1 && Read(staged.payloadDir / note) == incoming,
        "Stage the original archive with a verified notebook and seal");
  const auto seal = staged.validation;
  const auto stageId = Identity(stage);
  const auto payloadId = Identity(staged.payloadDir);
  const auto noteId = Identity(staged.payloadDir / note);
  unsigned guardCalls = 0;
  const auto guard = [](void *user, std::wstring &) {
    ++*static_cast<unsigned *>(user);
    return true;
  };
  bool previous = true;
  const auto refused = CtBackup::CommitStagedRestore(staged, live, recovery, guard, &guardCalls, &previous);
  Check(refused.code == E_INVALIDARG && guardCalls == 0 && !previous && !fs::exists(recovery),
        "A regular file at the live name refuses before consent without claiming recovery ownership");
  Check(Identity(live) == liveId && Read(live) == occupant && Identity(data) == parentId &&
        std::distance(fs::directory_iterator(data), fs::directory_iterator{}) == 1,
        "Refusal preserves the exact occupying file and creates no guard or recovery file");
  Check(staged.validation == seal && Identity(stage) == stageId &&
        Identity(staged.payloadDir) == payloadId && Identity(staged.payloadDir / note) == noteId &&
        Read(staged.payloadDir / note) == incoming,
        "Refusal preserves the original sealed stage for a retry");
  fs::rename(live, saved);
  Check(!fs::exists(live) && Identity(saved) == liveId && Read(saved) == occupant,
        "Move the regular fixture file aside without changing its identity");
  const auto installed = CtBackup::CommitStagedRestore(staged, live, recovery, guard, &guardCalls, &previous);
  Check(installed.ok() && installed.details.empty() && guardCalls == 1 && !previous &&
        !fs::exists(recovery) && !fs::exists(stage),
        "Retry installs the same seal with no previous collection and removes the completed stage");
  Check(Identity(live) == payloadId && Identity(live / note) == noteId && Read(live / note) == incoming &&
        Identity(saved) == liveId && Read(saved) == occupant && Identity(data) == parentId &&
        std::distance(fs::directory_iterator(data), fs::directory_iterator{}) == 2,
        "Retry installs the exact staged objects and preserves the relocated user file");
  Check(Identity(archive) == archiveId && Read(archive) == archiveBytes && CrcValid(archive) &&
        Identity(source / note) == sourceId && Read(source / note) == incoming,
        "Both attempts preserve the original backup and source notebook");
  std::cout << "RESTORE_LIVE_FILE_CASES=occupied-live-refusal,same-seal-retry,no-previous-collection\n";
}
