void BackupMissingClientRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto archive = fixture / L"out/selected.7z";
  const auto alpha = fs::path(L"Alpha/note.ctn");
  const auto beta = fs::path(L"Beta/note.ctn");
  const auto gamma = fs::path(L"Gamma/note.ctn");
  const auto held = fixture / L"saved-beta";
  fs::create_directories(archive.parent_path());
  Write(sites / alpha, "alpha original");
  Write(sites / beta, "beta original");
  Write(sites / gamma, "not selected");
  CtBackup::BackupOptions select;
  select.selectedClientNames = std::vector<std::wstring>{L"Beta", L"Alpha"};
  Check(BackUp(sites, archive, select).ok() && CrcValid(archive),
        "An explicit two-client backup starts with a valid archive");
  const auto originalId = Identity(archive);
  const auto original = Read(archive);
  const auto betaId = Identity(sites / beta);
  const auto gammaId = Identity(sites / gamma);
  fs::rename(sites / L"Beta", held);
  Write(sites / alpha, "alpha revised");
  const auto missing = BackUp(sites, archive, select);
  Check(missing.code == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND) &&
        missing.details.find(L"Beta") != std::wstring::npos,
        "A missing member of the requested selection is reported by name");
  Check(Identity(archive) == originalId && Read(archive) == original &&
        CrcValid(archive) && HasOnlyArchive(archive),
        "Refusal preserves the existing archive and creates no partial backup");
  CtBackup::StagedRestore originalStage;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"old-stage", nullptr, nullptr, originalStage).ok() &&
        originalStage.profileCount == 2 && Read(originalStage.payloadDir / alpha) == "alpha original" &&
        Read(originalStage.payloadDir / beta) == "beta original" && !fs::exists(originalStage.payloadDir / L"Gamma"),
        "The preserved archive still stages the complete original selection");
  fs::rename(held, sites / L"Beta");
  Check(BackUp(sites, archive, select).ok() && CrcValid(archive) && Identity(archive) != originalId &&
        HasOnlyArchive(archive), "Returning the original client makes the same selection publish successfully");
  CtBackup::StagedRestore newStage;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"new-stage", nullptr, nullptr, newStage).ok() &&
        newStage.profileCount == 2 && Read(newStage.payloadDir / alpha) == "alpha revised" &&
        Read(newStage.payloadDir / beta) == "beta original" && !fs::exists(newStage.payloadDir / L"Gamma"),
        "Retry stages the new Alpha and unchanged Beta without broadening the selection");
  Check(Identity(sites / beta) == betaId && Identity(sites / gamma) == gammaId &&
        Read(sites / gamma) == "not selected" && Read(sites / alpha) == "alpha revised",
        "Backup and staging preserve both the returned object and excluded client");
  std::cout << "BACKUP_MISSING_CLIENT_CASE=partial-selection-refused,complete-selection-retried\n";
}

void RestoreReadOnlyNoteRound(const fs::path &fixture) {
  const auto source = fixture / L"src/Sites";
  const auto note = fs::path(L"Alpha/note.ctn");
  const auto live = fixture / L"data/Sites";
  const auto recovery = live.parent_path() / L"R0001";
  const auto stage = live.parent_path() / L"stage";
  const auto archive = fixture / L"input.7z";
  Write(source / note, "new archived read-only note");
  Write(live / note, "prior live read-only note");
  const auto markReadOnly = [](const fs::path &p) {
    const DWORD attributes = GetFileAttributesW(p.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        SetFileAttributesW(p.c_str(), attributes | FILE_ATTRIBUTE_READONLY) != FALSE;
  };
  const auto readOnly = [](const fs::path &p) {
    const DWORD attributes = GetFileAttributesW(p.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0;
  };
  Check(markReadOnly(source / note) && markReadOnly(live / note),
        "Both the archived and current note use an ordinary read-only attribute");
  const auto sourceId = Identity(source / note);
  const auto oldRootId = Identity(live);
  const auto oldNoteId = Identity(live / note);
  Check(BackUp(source, archive).ok() && CrcValid(archive),
        "The backup reads and verifies the read-only source");
  const auto inputId = Identity(archive);
  const auto inputBytes = Read(archive);
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() &&
        staged.validation && staged.profileCount == 1 && readOnly(staged.payloadDir / note) &&
        Read(staged.payloadDir / note) == "new archived read-only note",
        "Staging preserves the note contents and read-only attribute");
  const auto stagedRootId = Identity(staged.payloadDir);
  const auto stagedNoteId = Identity(staged.payloadDir / note);
  bool recovered = false;
  const auto committed = CtBackup::CommitStagedRestore(staged, live, recovery, nullptr, nullptr, &recovered);
  Check(committed.ok() && committed.details.empty() && recovered && !fs::exists(stage),
        "Commit accepts the read-only notes and removes the completed staging root");
  Check(Identity(live) == stagedRootId && Identity(live / note) == stagedNoteId &&
        readOnly(live / note) && Read(live / note) == "new archived read-only note",
        "The exact staged note is installed with its attribute intact");
  Check(Identity(recovery) == oldRootId && Identity(recovery / note) == oldNoteId &&
        readOnly(recovery / note) && Read(recovery / note) == "prior live read-only note",
        "The exact previous read-only note survives in recovery");
  Check(Identity(source / note) == sourceId && readOnly(source / note) &&
        Read(source / note) == "new archived read-only note" && Identity(archive) == inputId &&
        Read(archive) == inputBytes && CrcValid(archive),
        "The original source and archive remain available and exact");
  std::cout << "RESTORE_READ_ONLY_NOTE_CASE=read-only-stage,read-only-live-and-recovery\n";
}
