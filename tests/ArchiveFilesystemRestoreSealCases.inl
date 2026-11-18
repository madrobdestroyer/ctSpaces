void RestoreSealRound(const fs::path &fixture) {
  const auto firstSource = fixture / L"a/Sites";
  const auto secondSource = fixture / L"b/Sites";
  const auto firstNote = fs::path(L"Incoming/note.ctn");
  const auto secondNote = fs::path(L"Alternate/note.ctn");
  const auto selectedArchive = fixture / L"selected.7z";
  const auto replacementArchive = fixture / L"replacement.7z";
  const auto savedArchive = fixture / L"original.7z";
  const auto firstStage = fixture / L"stage";
  const auto secondStage = fixture / L"second-stage";
  const auto data = fixture / L"data";
  const auto live = data / L"Sites";
  const auto recovery = data / L"R0001";
  const auto previousNote = fs::path(L"Previous/note.ctn");
  const std::string firstBytes = "notebook from the archive originally selected";
  const std::string secondBytes = "different notebook from the replacement archive";
  const std::string previousBytes = "notebook owned by the current live collection";
  Write(firstSource / firstNote, firstBytes);
  Write(secondSource / secondNote, secondBytes);
  Write(live / previousNote, previousBytes);
  Check(BackUp(firstSource, selectedArchive).ok() && CrcValid(selectedArchive) &&
        BackUp(secondSource, replacementArchive).ok() && CrcValid(replacementArchive),
        "Create two independently valid backups through the production writer");
  const auto selectedIdentity = Identity(selectedArchive);
  const auto selectedBytes = Read(selectedArchive);
  const auto replacementIdentity = Identity(replacementArchive);
  const auto replacementBytes = Read(replacementArchive);
  const auto previousIdentity = Identity(live);
  const auto previousNoteIdentity = Identity(live / previousNote);
  const auto firstSourceIdentity = Identity(firstSource / firstNote);
  const auto secondSourceIdentity = Identity(secondSource / secondNote);
  Check(selectedIdentity != replacementIdentity && selectedBytes != replacementBytes,
        "The archives have different file identities and contents");

  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(selectedArchive, firstStage, nullptr, nullptr, staged).ok() &&
        staged.validation && staged.profileCount == 1 && Read(staged.payloadDir / firstNote) == firstBytes,
        "The original archive stages and seals its own notebook");
  const auto seal = staged.validation;
  const auto stageIdentity = Identity(firstStage);
  const auto payloadIdentity = Identity(staged.payloadDir);
  const auto stagedNoteIdentity = Identity(staged.payloadDir / firstNote);
  fs::rename(selectedArchive, savedArchive);
  Check(!fs::exists(selectedArchive) && Identity(savedArchive) == selectedIdentity &&
        Read(savedArchive) == selectedBytes,
        "Staging releases the original input so its exact archive object can move to a sibling");

  fs::rename(replacementArchive, selectedArchive);
  const auto archivesPreserved = [&] {
    return Identity(savedArchive) == selectedIdentity && Read(savedArchive) == selectedBytes &&
        CrcValid(savedArchive) && Identity(selectedArchive) == replacementIdentity &&
        Read(selectedArchive) == replacementBytes && CrcValid(selectedArchive);
  };
  Check(!fs::exists(replacementArchive) && archivesPreserved(),
        "Reuse the original filename for the independent archive while preserving both valid objects");
  Check(staged.validation == seal && Identity(firstStage) == stageIdentity &&
        Identity(staged.payloadDir) == payloadIdentity &&
        Identity(staged.payloadDir / firstNote) == stagedNoteIdentity &&
        Read(staged.payloadDir / firstNote) == firstBytes && !fs::exists(staged.payloadDir / secondNote),
        "Replacing the archive path leaves the exact sealed stage independent of its former input name");

  bool previousInRecovery = false;
  const auto committed = CtBackup::CommitStagedRestore(
      staged, live, recovery, nullptr, nullptr, &previousInRecovery);
  Check(committed.ok() && committed.details.empty() && previousInRecovery && !fs::exists(firstStage),
        "Commit accepts the original seal after archive movement and disposes its completed stage");
  const auto installedAndRecovered = [&] {
    return Identity(live) == payloadIdentity && Identity(live / firstNote) == stagedNoteIdentity &&
        Read(live / firstNote) == firstBytes && !fs::exists(live / secondNote) &&
        Identity(recovery) == previousIdentity && Identity(recovery / previousNote) == previousNoteIdentity &&
        Read(recovery / previousNote) == previousBytes &&
        std::distance(fs::directory_iterator(data), fs::directory_iterator{}) == 2;
  };
  Check(installedAndRecovered(),
        "The commit installs the original exact staged object and recovers the previous exact Sites tree");
  Check(archivesPreserved() && Identity(firstSource / firstNote) == firstSourceIdentity &&
        Read(firstSource / firstNote) == firstBytes && Identity(secondSource / secondNote) == secondSourceIdentity &&
        Read(secondSource / secondNote) == secondBytes,
        "The first commit preserves both archive objects and both independent source notebooks");

  CtBackup::StagedRestore replacement;
  Check(CtBackup::StageValidated7zRestore(selectedArchive, secondStage, nullptr, nullptr, replacement).ok() &&
        replacement.validation && replacement.validation != seal && replacement.profileCount == 1 &&
        Read(replacement.payloadDir / secondNote) == secondBytes && !fs::exists(replacement.payloadDir / firstNote),
        "A new stage at the reused archive path contains only the replacement archive's notebook");
  Check(installedAndRecovered() && archivesPreserved(),
        "Independently staging the replacement preserves the installed tree, recovery and both backups");
  std::cout << "RESTORE_SEAL_CASES=input-released-after-stage,archive-path-reused,original-seal-commits,replacement-stages-independently\n";
}
