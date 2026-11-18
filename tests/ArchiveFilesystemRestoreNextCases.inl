void RestoreInputReturnRound(const fs::path &root) {
  restore_further_cases::Fixture fixture(root);
  const auto previous = fs::path(L"Previous/note.ctn");
  const auto heldArchive = root / L"held.7z";
  Write(fixture.source / fixture.incoming, fixture.incomingBytes);
  Write(fixture.live / previous, fixture.previousBytes);
  Check(BackUp(fixture.source, fixture.archive).ok() && CrcValid(fixture.archive),
        "Create a CRC-valid ordinary backup independently of the old collection");
  const auto archiveIdentity = Identity(fixture.archive);
  const auto archiveBytes = Read(fixture.archive);
  const auto sourceIdentity = Identity(fixture.source / fixture.incoming);
  const auto oldRoot = Identity(fixture.live);
  const auto oldNote = Identity(fixture.live / previous);

  fs::rename(fixture.archive, heldArchive);
  Check(!fs::exists(fixture.archive) && Identity(heldArchive) == archiveIdentity &&
        Read(heldArchive) == archiveBytes, "Move the selected archive aside without changing its object or bytes");
  const auto missing = CtBackup::StageValidated7zRestore(
      fixture.archive, fixture.stage, nullptr, nullptr, fixture.staged);
  Check(missing.code == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) &&
        missing.details.find(L"does not exist") != std::wstring::npos && !fixture.staged.validation &&
        fixture.staged.stagingRoot.empty() && fixture.staged.payloadDir.empty() && fixture.staged.profileCount == 0,
        "A genuinely missing selected archive returns file not found and clears all output fields");
  Check(!fs::exists(fixture.stage) && !fs::exists(fixture.recovery) &&
        Identity(fixture.live) == oldRoot && Identity(fixture.live / previous) == oldNote &&
        Read(fixture.live / previous) == fixture.previousBytes && Identity(heldArchive) == archiveIdentity,
        "Missing input creates no staging or recovery directory and leaves the exact current collection in place");

  fs::rename(heldArchive, fixture.archive);
  Check(!fs::exists(heldArchive) && Identity(fixture.archive) == archiveIdentity &&
        Read(fixture.archive) == archiveBytes && CrcValid(fixture.archive),
        "Return the exact original CRC-valid backup to its selected path");
  Check(CtBackup::StageValidated7zRestore(fixture.archive, fixture.stage, nullptr, nullptr, fixture.staged).ok() &&
        fixture.staged.validation && fixture.staged.profileCount == 1 &&
        Read(fixture.staged.payloadDir / fixture.incoming) == fixture.incomingBytes,
        "The same archive and staging paths can now stage the exact incoming notebook");
  const auto payloadIdentity = Identity(fixture.staged.payloadDir);
  const auto noteIdentity = Identity(fixture.staged.payloadDir / fixture.incoming);
  const auto committed = fixture.Commit(fixture.live, fixture.recovery);
  Check(committed.ok() && committed.details.empty() && fixture.guardCalls == 1 &&
        fixture.recovered && !fs::exists(fixture.stage),
        "Retry commits once and removes its owned temporary stage");
  Check(Identity(fixture.live) == payloadIdentity && Identity(fixture.live / fixture.incoming) == noteIdentity &&
        Read(fixture.live / fixture.incoming) == fixture.incomingBytes && Identity(fixture.recovery) == oldRoot &&
        Identity(fixture.recovery / previous) == oldNote && Read(fixture.recovery / previous) == fixture.previousBytes,
        "Retry installs the exact staged tree and retains the exact prior tree in recovery");
  Check(fixture.OnlyLiveAndRecovery() && Identity(fixture.archive) == archiveIdentity &&
        Read(fixture.archive) == archiveBytes && CrcValid(fixture.archive) &&
        Identity(fixture.source / fixture.incoming) == sourceIdentity &&
        Read(fixture.source / fixture.incoming) == fixture.incomingBytes,
        "The full retry preserves the source and returned archive and leaves no guard file");
  std::cout << "RESTORE_INPUT_RETURN_CASES=missing-input-no-stage,exact-archive-returned,successful-retry\n";
}

void RestoreStageDirectoryShareRound(const fs::path &root) {
  restore_further_cases::Fixture fixture(root);
  const auto previous = fs::path(L"Previous/note.ctn");
  const auto nested = fs::path(L"Incoming/Browser/Profile");
  const auto preference = nested / L"Preferences";
  const std::string preferenceBytes = "independent file inside the staged browser directory";
  Write(fixture.source / preference, preferenceBytes);
  Write(fixture.live / previous, fixture.previousBytes);
  const auto oldRoot = Identity(fixture.live);
  const auto oldNote = Identity(fixture.live / previous);
  fixture.BackUpAndStage();
  const auto seal = fixture.staged.validation;
  const auto held = fixture.staged.payloadDir / nested;
  const auto directoryIdentity = Identity(held);
  const auto preferenceIdentity = Identity(fixture.staged.payloadDir / preference);
  Check(Read(fixture.staged.payloadDir / preference) == preferenceBytes,
        "The target is a real staged browser directory holding its own exact file");
  {
    const FileHandle exclusive(CreateFileW(held.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Check(exclusive.value != INVALID_HANDLE_VALUE,
          "Acquire a genuine exclusive read handle on the exact staged subdirectory");
    const auto refused = fixture.Commit(fixture.live, fixture.recovery);
    Check(refused.code == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) &&
          fixture.guardCalls == 0 && !fixture.recovered,
          "Initial snapshot verification refuses the directory lease with its native HRESULT before consent");
  }
  Check(fixture.staged.validation == seal && fixture.StagePreserved() && Identity(held) == directoryIdentity &&
        Identity(fixture.staged.payloadDir / preference) == preferenceIdentity &&
        Read(fixture.staged.payloadDir / preference) == preferenceBytes &&
        Identity(fixture.live) == oldRoot && Identity(fixture.live / previous) == oldNote &&
        Read(fixture.live / previous) == fixture.previousBytes && !fs::exists(fixture.recovery),
        "The refusal retains the exact original seal, staged directory and live collection");
  const auto committed = fixture.Commit(fixture.live, fixture.recovery);
  Check(committed.ok() && committed.details.empty() && fixture.guardCalls == 1 && fixture.recovered &&
        !fs::exists(fixture.stage), "The same sealed stage commits after closing the directory handle");
  Check(fixture.Installed() && Identity(fixture.live / nested) == directoryIdentity &&
        Identity(fixture.live / preference) == preferenceIdentity && Read(fixture.live / preference) == preferenceBytes &&
        Identity(fixture.recovery) == oldRoot && Identity(fixture.recovery / previous) == oldNote &&
        Read(fixture.recovery / previous) == fixture.previousBytes,
        "Retry installs both exact staged files and the staged directory, and recovers the previous collection");
  Check(fixture.InputsPreserved() && fixture.OnlyLiveAndRecovery() && Read(fixture.source / preference) == preferenceBytes,
        "Both attempts preserve the original backup and source and leave no destination guard file");
  std::cout << "RESTORE_STAGE_DIRECTORY_SHARE_CASES=exclusive-directory-refusal,same-seal-retry\n";
}

void RestoreEmptyStageReuseRound(const fs::path &root) {
  restore_further_cases::Fixture fixture(root);
  const auto previous = fs::path(L"Previous/note.ctn");
  const auto sibling = root / L"independent/notes.txt";
  const std::string siblingBytes = "unrelated file beside the reserved stage";
  Write(sibling, siblingBytes);
  Write(fixture.live / previous, fixture.previousBytes);
  const auto oldRoot = Identity(fixture.live);
  const auto oldNote = Identity(fixture.live / previous);
  const auto siblingIdentity = Identity(sibling);
  const auto siblingParentIdentity = Identity(sibling.parent_path());
  Check(fs::create_directory(fixture.stage) && fs::is_empty(fixture.stage),
        "Precreate an ordinary empty stage as the application does before validation");
  const auto originalStage = Identity(fixture.stage);
  {
    const FileHandle reader(CreateFileW(fixture.stage.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Check(reader.value != INVALID_HANDLE_VALUE, "Open a compatible metadata reader on the exact empty stage");
    fixture.BackUpAndStage();
    BY_HANDLE_FILE_INFORMATION information{};
    Check(fixture.stageIdentity == originalStage && GetFileInformationByHandle(reader.value, &information) &&
          FileIdentity{information.dwVolumeSerialNumber, information.nFileIndexHigh,
                       information.nFileIndexLow} == originalStage,
          "Staging fills the exact reserved directory while the metadata reader stays open");
  }
  Check(Identity(sibling.parent_path()) == siblingParentIdentity && Identity(sibling) == siblingIdentity &&
        Read(sibling) == siblingBytes && Identity(fixture.live) == oldRoot &&
        Identity(fixture.live / previous) == oldNote && Read(fixture.live / previous) == fixture.previousBytes,
        "Filling the reserved stage leaves both adjacent and live data unchanged");
  const auto committed = fixture.Commit(fixture.live, fixture.recovery);
  Check(committed.ok() && committed.details.empty() && fixture.guardCalls == 1 && fixture.recovered &&
        !fs::exists(fixture.stage), "Commit removes the completed reserved stage after the metadata reader closes");
  Check(fixture.Installed() && Identity(fixture.recovery) == oldRoot &&
        Identity(fixture.recovery / previous) == oldNote && Read(fixture.recovery / previous) == fixture.previousBytes,
        "The exact verified and original collections occupy only their intended locations");
  Check(fixture.InputsPreserved() && fixture.OnlyLiveAndRecovery() &&
        Identity(sibling.parent_path()) == siblingParentIdentity && Identity(sibling) == siblingIdentity &&
        Read(sibling) == siblingBytes, "Cleanup keeps the unrelated sibling and exact archive and source");
  std::cout << "RESTORE_EMPTY_STAGE_REUSE_CASES=precreated-stage-identity,compatible-metadata-reader,independent-sibling-preserved\n";
}
