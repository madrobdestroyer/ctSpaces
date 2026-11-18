void BackupMissingSitesRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto movedSites = fixture / L"src/Sites aside";
  const auto note = fs::path(L"Client/note.ctn");
  const auto archive = fixture / L"out/current.7z";
  fs::create_directories(archive.parent_path());
  Write(sites / note, "initial notebook");
  Check(BackUp(sites, archive).ok() && CrcValid(archive),
        "Create a verified archive before moving the source collection");
  const auto archivedId = Identity(archive);
  const auto archivedBytes = Read(archive);
  const auto sitesId = Identity(sites);
  const auto noteId = Identity(sites / note);

  fs::rename(sites, movedSites);
  Check(!fs::exists(sites) && Identity(movedSites) == sitesId,
        "Move the entire collection to an ordinary sibling");
  const auto refused = BackUp(sites, archive);
  Check(refused.code == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) &&
        refused.details.find(L"Sites") != std::wstring::npos &&
        !fs::exists(sites) && Identity(archive) == archivedId &&
        Read(archive) == archivedBytes && CrcValid(archive) && HasOnlyArchive(archive),
        "Missing Sites does not create source data or replace the previous backup");
  CtBackup::StagedRestore oldStage;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"original", nullptr, nullptr, oldStage).ok() &&
        oldStage.profileCount == 1 && Read(oldStage.payloadDir / note) == "initial notebook",
        "The previous backup still stages the original notebook while the source is absent");

  fs::rename(movedSites, sites);
  Write(sites / note, "revised notebook");
  Check(Identity(sites) == sitesId && Identity(sites / note) == noteId &&
        BackUp(sites, archive).ok() && Identity(archive) != archivedId &&
        CrcValid(archive) && HasOnlyArchive(archive),
        "Returning the exact source enables a successful retry at the same destination");
  CtBackup::StagedRestore revised;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"revised", nullptr, nullptr, revised).ok() &&
        revised.profileCount == 1 && Read(revised.payloadDir / note) == "revised notebook" &&
        Read(oldStage.payloadDir / note) == "initial notebook",
        "The retry stages the edited notebook without altering the earlier stage");
  Check(Identity(sites / note) == noteId && Read(sites / note) == "revised notebook",
        "Both backup attempts preserve the original notebook object");
  std::cout << "BACKUP_MISSING_SITES_CASE=missing-source-preserves-backup,returned-source-retry\n";
}

void RestoreDistinctParentsRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto note = fs::path(L"Client/note.ctn");
  const auto archive = fixture / L"input.7z";
  const auto live = fixture / L"live-parent/Sites";
  const auto recovery = fixture / L"recovery-parent/R0001";
  const auto stage = fixture / L"stages/new-stage";
  const auto liveSibling = live.parent_path() / L"user.txt";
  const auto recoverySibling = recovery.parent_path() / L"user.txt";
  const auto stageSibling = stage.parent_path() / L"user.txt";
  Write(sites / note, "replacement collection");
  Write(live / note, "previous collection");
  Write(liveSibling, "unrelated live sibling");
  Write(recoverySibling, "unrelated recovery sibling");
  Write(stageSibling, "unrelated stage sibling");
  Check(BackUp(sites, archive).ok() && CrcValid(archive),
        "Create a verified archive outside all three restore destinations");
  const auto archiveId = Identity(archive);
  const auto archiveBytes = Read(archive);
  const auto oldRoot = Identity(live);
  const auto oldFile = Identity(live / note);
  const auto l = Identity(liveSibling);
  const auto r = Identity(recoverySibling);
  const auto s = Identity(stageSibling);
  const auto liveParentId = Identity(live.parent_path());
  const auto recoveryParentId = Identity(recovery.parent_path());
  const auto stageParentId = Identity(stage.parent_path());
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() &&
        staged.validation && staged.profileCount == 1 && Read(staged.payloadDir / note) == "replacement collection",
        "Stage the archive beneath a third ordinary parent");
  const auto replacementRoot = Identity(staged.payloadDir);
  const auto replacementFile = Identity(staged.payloadDir / note);
  unsigned approvals = 0;
  const auto approve = [](void *user, std::wstring &) {
    ++*static_cast<unsigned *>(user);
    return true;
  };
  bool recovered = false;
  const auto result = CtBackup::CommitStagedRestore(staged, live, recovery, approve, &approvals, &recovered);
  Check(result.ok() && result.details.empty() && approvals == 1 && recovered && !fs::exists(stage),
        "Commit across all three parents succeeds and disposes only the completed stage");
  Check(Identity(live) == replacementRoot && Identity(live / note) == replacementFile &&
        Read(live / note) == "replacement collection",
        "The exact staged tree becomes live");
  Check(Identity(recovery) == oldRoot && Identity(recovery / note) == oldFile &&
        Read(recovery / note) == "previous collection",
        "The exact previous collection moves to its independently chosen parent");
  Check(Identity(liveSibling) == l && Identity(recoverySibling) == r && Identity(stageSibling) == s &&
        Read(liveSibling) == "unrelated live sibling" && Read(recoverySibling) == "unrelated recovery sibling" &&
        Read(stageSibling) == "unrelated stage sibling",
        "All three unrelated sibling files survive with their original identities");
  Check(Identity(live.parent_path()) == liveParentId &&
        Identity(recovery.parent_path()) == recoveryParentId && Identity(stage.parent_path()) == stageParentId &&
        std::distance(fs::directory_iterator(live.parent_path()), fs::directory_iterator{}) == 2 &&
        std::distance(fs::directory_iterator(recovery.parent_path()), fs::directory_iterator{}) == 2 &&
        std::distance(fs::directory_iterator(stage.parent_path()), fs::directory_iterator{}) == 1,
        "No destination guard files or extra stage entries remain");
  Check(Identity(archive) == archiveId && Read(archive) == archiveBytes && CrcValid(archive) &&
        Read(sites / note) == "replacement collection", "Commit preserves the source and original archive");
  std::cout << "RESTORE_DISTINCT_PARENTS_CASE=three-parents,exact-siblings-and-recovery\n";
}
