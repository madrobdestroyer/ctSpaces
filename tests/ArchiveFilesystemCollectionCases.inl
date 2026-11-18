void BackupRootFileSelectionRound(const fs::path &fixture) {
  const auto sites = fixture / L"source/Sites";
  const auto alpha = fs::path(L"Alpha/note.ctn");
  const auto beta = fs::path(L"Beta/note.ctn");
  const auto rootFile = sites / L"desktop.ini";
  const auto archive = fixture / L"out/copy.7z";
  fs::create_directories(archive.parent_path());
  Write(sites / alpha, "old alpha");
  Write(sites / beta, "independent beta");
  Check(BackUp(sites, archive).ok() && CrcValid(archive),
        "A clean full backup starts with two independent clients");
  const auto oldBytes = Read(archive);
  const auto oldId = Identity(archive);
  Write(sites / alpha, "new alpha only");
  Write(rootFile, "ordinary folder customization file");
  const auto alphaId = Identity(sites / alpha);
  const auto betaId = Identity(sites / beta);
  const auto rootId = Identity(rootFile);
  const auto refused = BackUp(sites, archive);
  Check(refused.code == E_INVALIDARG && refused.details.find(L"desktop.ini") != std::wstring::npos,
        "Full backup reports the ordinary file outside any client");
  Check(Identity(archive) == oldId && Read(archive) == oldBytes && CrcValid(archive) &&
        HasOnlyArchive(archive), "Full-backup refusal preserves the existing verified archive");
  CtBackup::BackupOptions selection;
  selection.selectedClientNames = std::vector<std::wstring>{L"Alpha"};
  Check(BackUp(sites, archive, selection).ok() && CrcValid(archive) &&
        Identity(archive) != oldId && HasOnlyArchive(archive),
        "An explicit backup of Alpha publishes while the unrelated root file remains in place");
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"stage", nullptr, nullptr, staged).ok() &&
        staged.profileCount == 1 && Read(staged.payloadDir / alpha) == "new alpha only" &&
        !fs::exists(staged.payloadDir / L"Beta") && !fs::exists(staged.payloadDir / rootFile.filename()),
        "Only the selected client appears in the verified archive");
  Check(Identity(sites / alpha) == alphaId && Read(sites / alpha) == "new alpha only" &&
        Identity(sites / beta) == betaId && Read(sites / beta) == "independent beta" &&
        Identity(rootFile) == rootId && Read(rootFile) == "ordinary folder customization file",
        "Both backup attempts preserve the exact source clients and independent folder file");
  std::cout << "BACKUP_ROOT_FILE_CASE=full-refusal,selected-client-isolation\n";
}

void RestoreTwoRecoveriesRound(const fs::path &fixture) {
  const auto source = fixture / L"src/Sites";
  const auto note = fs::path(L"Alpha/note.ctn");
  const auto firstArchive = fixture / L"first.7z";
  const auto secondArchive = fixture / L"second.7z";
  const auto data = fixture / L"data";
  const auto live = data / L"Sites";
  const auto firstRecovery = data / L"R0001";
  const auto secondRecovery = data / L"R0002";
  const auto firstStage = data / L"stage-one";
  const auto secondStage = data / L"stage-two";
  Write(live / note, "original");
  const auto originalRoot = Identity(live);
  const auto originalFile = Identity(live / note);
  Write(source / note, "first backup");
  Check(BackUp(source, firstArchive).ok() && CrcValid(firstArchive),
        "Create a first verified archive");
  Write(source / note, "second backup");
  Check(BackUp(source, secondArchive).ok() && CrcValid(secondArchive),
        "Create an independent second verified archive");
  const auto firstArchiveId = Identity(firstArchive);
  const auto secondArchiveId = Identity(secondArchive);
  const auto firstBytes = Read(firstArchive);
  const auto secondBytes = Read(secondArchive);

  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(firstArchive, firstStage, nullptr, nullptr, staged).ok() &&
        staged.validation && Read(staged.payloadDir / note) == "first backup",
        "Seal the first replacement");
  const auto firstRoot = Identity(staged.payloadDir);
  const auto firstFile = Identity(staged.payloadDir / note);
  bool recovered = false;
  Check(CtBackup::CommitStagedRestore(staged, live, firstRecovery, nullptr, nullptr, &recovered).ok() &&
        recovered && !fs::exists(firstStage) && Identity(live) == firstRoot &&
        Identity(firstRecovery) == originalRoot && Identity(firstRecovery / note) == originalFile &&
        Read(firstRecovery / note) == "original", "First restore retains the original collection in recovery");
  Check(CtBackup::StageValidated7zRestore(secondArchive, secondStage, nullptr, nullptr, staged).ok() &&
        staged.validation && Read(staged.payloadDir / note) == "second backup",
        "Reuse the result object to seal the second replacement");
  const auto secondRoot = Identity(staged.payloadDir);
  const auto secondFile = Identity(staged.payloadDir / note);
  recovered = true;
  const auto collision = CtBackup::CommitStagedRestore(staged, live, firstRecovery, nullptr, nullptr, &recovered);
  Check(collision.code == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) && !recovered &&
        Identity(live) == firstRoot && Identity(firstRecovery) == originalRoot &&
        Identity(staged.payloadDir) == secondRoot, "A second restore cannot overwrite the first recovery");
  Check(CtBackup::CommitStagedRestore(staged, live, secondRecovery, nullptr, nullptr, &recovered).ok() &&
        recovered && !fs::exists(secondStage), "Retrying the second restore with a fresh recovery name succeeds");
  Check(Identity(live) == secondRoot && Identity(live / note) == secondFile && Read(live / note) == "second backup" &&
        Identity(secondRecovery) == firstRoot && Identity(secondRecovery / note) == firstFile &&
        Read(secondRecovery / note) == "first backup" && Identity(firstRecovery) == originalRoot &&
        Identity(firstRecovery / note) == originalFile && Read(firstRecovery / note) == "original" &&
        std::distance(fs::directory_iterator(data), fs::directory_iterator{}) == 3,
        "Both earlier generations and the new live collection retain exact objects and bytes");
  Check(Identity(firstArchive) == firstArchiveId && Read(firstArchive) == firstBytes && CrcValid(firstArchive) &&
        Identity(secondArchive) == secondArchiveId && Read(secondArchive) == secondBytes && CrcValid(secondArchive),
        "Both original backup archives remain exact");
  std::cout << "RESTORE_TWO_RECOVERIES_CASE=two-restores,first-recovery-preserved\n";
}
