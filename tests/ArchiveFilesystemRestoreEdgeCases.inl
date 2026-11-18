void RestoreEdgeRound(const fs::path &fixture) {
  const auto source = fixture / L"src/Sites";
  const auto note = fs::path(L"Incoming/note.ctn");
  const auto archive = fixture / L"input.7z";
  const auto stage = fixture / L"stage";
  const auto data = fixture / L"data";
  const auto live = data / L"Sites";
  const auto recovery = data / L"R0001";
  const auto savedOccupant = data / L"saved-occupant.bin";
  const std::string incoming = "verified note to replace an existing empty collection";
  const std::string occupant = "independent regular file at the recovery name";
  Write(source / note, incoming);
  Check(fs::create_directories(live) && fs::is_empty(live),
        "The current Sites exists as an empty ordinary directory");
  Write(recovery, occupant);
  Check(BackUp(source, archive).ok() && CrcValid(archive),
        "Create a CRC-valid archive independently of the live and recovery paths");
  const auto sourceIdentity = Identity(source / note);
  const auto archiveIdentity = Identity(archive);
  const auto archiveBytes = Read(archive);
  const auto liveIdentity = Identity(live);
  const auto dataIdentity = Identity(data);
  const auto occupantIdentity = Identity(recovery);

  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() &&
        staged.validation && staged.profileCount == 1 && Read(staged.payloadDir / note) == incoming,
        "Stage the incoming notebook with a valid seal");
  const auto seal = staged.validation;
  const auto stageIdentity = Identity(stage);
  const auto payloadIdentity = Identity(staged.payloadDir);
  const auto stagedNoteIdentity = Identity(staged.payloadDir / note);
  const auto countConsent = [](void *user, std::wstring &) {
    ++*static_cast<unsigned *>(user);
    return true;
  };
  unsigned guardCalls = 0;
  bool previousInRecovery = true;
  const auto refused = CtBackup::CommitStagedRestore(
      staged, live, recovery, countConsent, &guardCalls, &previousInRecovery);
  Check(refused.code == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) &&
        guardCalls == 0 && !previousInRecovery,
        "A regular file at the recovery name refuses before consent or recovery ownership");
  Check(staged.validation == seal && Identity(stage) == stageIdentity &&
        Identity(staged.payloadDir) == payloadIdentity &&
        Identity(staged.payloadDir / note) == stagedNoteIdentity &&
        Read(staged.payloadDir / note) == incoming,
        "The refused commit preserves the exact sealed stage and incoming notebook");
  Check(Identity(data) == dataIdentity && Identity(live) == liveIdentity && fs::is_empty(live) &&
        Identity(recovery) == occupantIdentity && Read(recovery) == occupant &&
        std::distance(fs::directory_iterator(data), fs::directory_iterator{}) == 2,
        "The refusal preserves the empty live root and the independent file without leaving a guard file");

  fs::rename(recovery, savedOccupant);
  Check(!fs::exists(recovery) && Identity(savedOccupant) == occupantIdentity &&
        Read(savedOccupant) == occupant,
        "Move the independent fixture file aside without replacing it");
  const auto installed = CtBackup::CommitStagedRestore(
      staged, live, recovery, countConsent, &guardCalls, &previousInRecovery);
  Check(installed.ok() && installed.details.empty() && guardCalls == 1 &&
        previousInRecovery && !fs::exists(stage),
        "The same sealed stage commits once the recovery name is free and removes its stage");
  Check(Identity(live) == payloadIdentity && Identity(live / note) == stagedNoteIdentity &&
        Read(live / note) == incoming && Identity(recovery) == liveIdentity && fs::is_empty(recovery),
        "The commit installs the exact staged notebook and recovers the original empty Sites");
  Check(Identity(data) == dataIdentity && Identity(savedOccupant) == occupantIdentity &&
        Read(savedOccupant) == occupant &&
        std::distance(fs::directory_iterator(data), fs::directory_iterator{}) == 3,
        "The successful retry preserves the independent file and leaves no destination guard file");
  Check(Identity(archive) == archiveIdentity && Read(archive) == archiveBytes && CrcValid(archive) &&
        Identity(source / note) == sourceIdentity && Read(source / note) == incoming,
        "Both attempts preserve the exact source notebook and original backup");
  std::cout << "RESTORE_EDGE_CASES=regular-file-recovery-collision,same-seal-retry,existing-empty-sites-recovered\n";
}
