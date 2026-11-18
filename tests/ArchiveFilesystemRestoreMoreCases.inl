void RestoreRecoveryShareRound(const fs::path &fixture) {
  const auto source = fixture / L"src/Sites";
  const auto incoming = fs::path(L"Incoming/note.ctn");
  const auto previous = fs::path(L"Previous/note.ctn");
  const auto archive = fixture / L"input.7z";
  const auto stage = fixture / L"stage";
  const auto live = fixture / L"data/Sites";
  const auto history = fixture / L"history";
  const auto recovery = history / L"R0001";
  const std::string incomingBytes = "notebook from an independently staged backup";
  const std::string previousBytes = "previous notebook before recovery parent sharing refusal";
  Write(source / incoming, incomingBytes);
  Write(live / previous, previousBytes);
  fs::create_directory(history);
  Check(BackUp(source, archive).ok() && CrcValid(archive),
        "Create a valid ordinary backup for a separate recovery parent");
  const auto archiveIdentity = Identity(archive);
  const auto archiveBytes = Read(archive);
  const auto sourceIdentity = Identity(source / incoming);
  const auto previousIdentity = Identity(live);
  const auto previousNoteIdentity = Identity(live / previous);
  const auto dataIdentity = Identity(live.parent_path());
  const auto historyIdentity = Identity(history);

  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() &&
        staged.validation && staged.profileCount == 1 && Read(staged.payloadDir / incoming) == incomingBytes,
        "Stage the exact incoming notebook before locking the separate recovery parent");
  const auto seal = staged.validation;
  const auto stageIdentity = Identity(stage);
  const auto payloadIdentity = Identity(staged.payloadDir);
  const auto noteIdentity = Identity(staged.payloadDir / incoming);
  const auto countGuard = [](void *user, std::wstring &) {
    ++*static_cast<unsigned *>(user);
    return true;
  };
  unsigned guardCalls = 0;
  bool previousInRecovery = true;
  {
    const FileHandle exclusive(CreateFileW(history.c_str(), GENERIC_READ, 0,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Check(exclusive.value != INVALID_HANDLE_VALUE,
          "An independent reader holds the exact existing recovery parent exclusively");
    const auto refused = CtBackup::CommitStagedRestore(
        staged, live, recovery, countGuard, &guardCalls, &previousInRecovery);
    std::cout << "RESTORE_RECOVERY_SHARE_HRESULT=0x" << std::hex
              << static_cast<unsigned long>(refused.code) << std::dec << '\n';
    Check(refused.code == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) &&
          guardCalls == 0 && !previousInRecovery,
          "The actual recovery parent sharing refusal is reported before consent or either move");
    Check(staged.validation == seal && Identity(stage) == stageIdentity &&
          Identity(staged.payloadDir) == payloadIdentity &&
          Identity(staged.payloadDir / incoming) == noteIdentity &&
          Read(staged.payloadDir / incoming) == incomingBytes && Identity(live) == previousIdentity &&
          Identity(live / previous) == previousNoteIdentity && Read(live / previous) == previousBytes,
          "Refusal leaves the exact stage and current collection available");
  }
  Check(Identity(history) == historyIdentity && fs::is_empty(history) &&
        Identity(live.parent_path()) == dataIdentity &&
        std::distance(fs::directory_iterator(live.parent_path()), fs::directory_iterator{}) == 1,
        "The released recovery parent and live parent have no leftover guard file");

  const auto installed = CtBackup::CommitStagedRestore(
      staged, live, recovery, countGuard, &guardCalls, &previousInRecovery);
  Check(installed.ok() && installed.details.empty() && guardCalls == 1 &&
        previousInRecovery && !fs::exists(stage),
        "The same seal commits with one consent after the recovery parent reader closes");
  Check(Identity(live) == payloadIdentity && Identity(live / incoming) == noteIdentity &&
        Read(live / incoming) == incomingBytes && Identity(recovery) == previousIdentity &&
        Identity(recovery / previous) == previousNoteIdentity && Read(recovery / previous) == previousBytes,
        "Retry installs the exact incoming tree and recovers the exact previous tree");
  Check(Identity(history) == historyIdentity && Identity(live.parent_path()) == dataIdentity &&
        std::distance(fs::directory_iterator(history), fs::directory_iterator{}) == 1 &&
        std::distance(fs::directory_iterator(live.parent_path()), fs::directory_iterator{}) == 1,
        "Successful retry leaves exactly the requested directories under both parents");
  Check(Identity(archive) == archiveIdentity && Read(archive) == archiveBytes && CrcValid(archive) &&
        Identity(source / incoming) == sourceIdentity && Read(source / incoming) == incomingBytes,
        "Both attempts preserve the exact source notebook and CRC-valid backup");
  std::cout << "RESTORE_RECOVERY_SHARE_CASES=exclusive-recovery-parent-refusal,same-seal-retry\n";
}

void RestoreCompatibleLiveRound(const fs::path &fixture) {
  const auto source = fixture / L"src/Sites";
  const auto incoming = fs::path(L"Incoming/note.ctn");
  const auto previous = fs::path(L"Previous/note.ctn");
  const auto archive = fixture / L"input.7z";
  const auto stage = fixture / L"stage";
  const auto data = fixture / L"data";
  const auto live = data / L"Sites";
  const auto recovery = data / L"R0001";
  const std::string incomingBytes = "incoming data while a share-delete reader is open";
  const std::string previousBytes = "previous data bound to the compatible reader";
  Write(source / incoming, incomingBytes);
  Write(live / previous, previousBytes);
  Check(BackUp(source, archive).ok() && CrcValid(archive),
        "Create an ordinary CRC-valid backup independently of the live collection");
  const auto archiveIdentity = Identity(archive);
  const auto archiveBytes = Read(archive);
  const auto sourceIdentity = Identity(source / incoming);
  const auto previousIdentity = Identity(live);
  const auto previousNoteIdentity = Identity(live / previous);

  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() &&
        staged.validation && Read(staged.payloadDir / incoming) == incomingBytes,
        "Stage the exact incoming collection before acquiring the compatible reader");
  const auto payloadIdentity = Identity(staged.payloadDir);
  const auto noteIdentity = Identity(staged.payloadDir / incoming);
  const FileHandle reader(CreateFileW(live.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  const auto readerOwnsPrevious = [&] {
    BY_HANDLE_FILE_INFORMATION information{};
    return reader.value != INVALID_HANDLE_VALUE && GetFileInformationByHandle(reader.value, &information) &&
        FileIdentity{information.dwVolumeSerialNumber, information.nFileIndexHigh,
                     information.nFileIndexLow} == previousIdentity;
  };
  Check(readerOwnsPrevious(), "The real delete-sharing reader holds the exact current Sites directory");
  bool previousInRecovery = false;
  const auto installed = CtBackup::CommitStagedRestore(
      staged, live, recovery, nullptr, nullptr, &previousInRecovery);
  std::cout << "RESTORE_COMPATIBLE_LIVE_HRESULT=0x" << std::hex
            << static_cast<unsigned long>(installed.code) << std::dec << '\n';
  Check(installed.ok() && installed.details.empty() && previousInRecovery && !fs::exists(stage),
        "The live reader stays open while the verified restore commits and removes its stage");
  wchar_t finalPath[512]{};
  const DWORD length = GetFinalPathNameByHandleW(reader.value, finalPath,
      static_cast<DWORD>(std::size(finalPath)), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  const auto expectedPath = L"\\\\?\\" + recovery.native();
  Check(readerOwnsPrevious() && length && length < std::size(finalPath) &&
        _wcsicmp(finalPath, expectedPath.c_str()) == 0,
        "The still-open reader retains the original identity and resolves to the exact recovery path");
  Check(Identity(live) == payloadIdentity && Identity(live / incoming) == noteIdentity &&
        Read(live / incoming) == incomingBytes && Identity(recovery) == previousIdentity &&
        Identity(recovery / previous) == previousNoteIdentity && Read(recovery / previous) == previousBytes,
        "The live path contains the exact staged object while the old notebook remains in recovery");
  Check(std::distance(fs::directory_iterator(data), fs::directory_iterator{}) == 2 &&
        Identity(archive) == archiveIdentity && Read(archive) == archiveBytes && CrcValid(archive) &&
        Identity(source / incoming) == sourceIdentity && Read(source / incoming) == incomingBytes,
        "The compatible-reader commit leaves no guard file and preserves the independent backup and source");
  std::cout << "RESTORE_COMPATIBLE_LIVE_CASES=delete-sharing-reader,reader-follows-original-to-recovery\n";
}

void RestoreSlotFidelityRound(const fs::path &fixture) {
  const auto source = fixture / L"src/Sites";
  const auto archive = fixture / L"input.7z";
  const auto stage = fixture / L"stage";
  const auto data = fixture / L"data";
  const auto live = data / L"Sites";
  const auto recovery = data / L"R0001";
  const auto previousEdge = fs::path(L"aLPHA/Browsers/edge/Profile/Default/Preferences");
  const auto previousNote = fs::path(L"BRAVO/ctSpaces-client-notes.ctn");
  const std::string previousEdgeBytes = "previous Alpha Edge profile";
  const std::string previousNoteBytes = "previous Bravo notebook";
  struct SlotFile {
    fs::path name;
    std::string bytes;
  };
  const std::vector<SlotFile> files{
    {L"Alpha/Browsers/edge/Profile/Default/Preferences", "archived Alpha Edge profile"},
    {L"Alpha/Browsers/chrome/Profile/Default/Preferences", "archived Alpha Chrome profile"},
    {L"Alpha/ctSpaces-client-notes.ctn", "archived Alpha notebook"},
    {L"Bravo/Browsers/edge/Profile/Default/Preferences", "archived Bravo Edge profile"},
    {L"Bravo/Browsers/chrome/Profile/Default/Preferences", "archived Bravo Chrome profile"},
    {L"Bravo/ctSpaces-client-notes.ctn", "archived Bravo notebook"}
  };
  std::vector<FileIdentity> sourceIdentities;
  for (const auto &file : files) {
    Write(source / file.name, file.bytes);
    sourceIdentities.push_back(Identity(source / file.name));
  }
  Write(live / previousEdge, previousEdgeBytes);
  Write(live / previousNote, previousNoteBytes);
  Check(BackUp(source, archive).ok() && CrcValid(archive),
        "Back up independent client notebooks and browser slots with matching leaf filenames");
  const auto archiveIdentity = Identity(archive);
  const auto archiveBytes = Read(archive);
  const auto previousIdentity = Identity(live);
  const auto previousEdgeIdentity = Identity(live / previousEdge);
  const auto previousNoteIdentity = Identity(live / previousNote);
  const auto hasExactClients = [](const fs::path &sites, std::wstring_view first,
                                  std::wstring_view second) {
    bool foundFirst = false;
    bool foundSecond = false;
    for (const auto &entry : fs::directory_iterator(sites)) {
      if (!entry.is_directory()) return false;
      const auto name = entry.path().filename().native();
      if (name == first) foundFirst = true;
      else if (name == second) foundSecond = true;
      else return false;
    }
    return foundFirst && foundSecond;
  };

  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() &&
        staged.validation && staged.profileCount == 2,
        "The production stage seals both archived clients");
  const auto payloadIdentity = Identity(staged.payloadDir);
  std::vector<FileIdentity> stagedIdentities;
  bool exactStage = hasExactClients(staged.payloadDir, L"Alpha", L"Bravo");
  for (const auto &file : files) {
    exactStage = exactStage && Read(staged.payloadDir / file.name) == file.bytes;
    stagedIdentities.push_back(Identity(staged.payloadDir / file.name));
  }
  Check(exactStage && hasExactClients(live, L"aLPHA", L"BRAVO"),
        "Staging keeps every client and browser slot distinct while live spellings are unchanged");

  bool previousInRecovery = false;
  const auto installed = CtBackup::CommitStagedRestore(
      staged, live, recovery, nullptr, nullptr, &previousInRecovery);
  Check(installed.ok() && installed.details.empty() && previousInRecovery && !fs::exists(stage),
        "Promote both verified clients and recover the previous collection in one transaction");
  bool exactInstalled = hasExactClients(live, L"Alpha", L"Bravo") && Identity(live) == payloadIdentity;
  bool exactSources = true;
  for (size_t index = 0; index < files.size(); ++index) {
    const auto &file = files[index];
    exactInstalled = exactInstalled && Identity(live / file.name) == stagedIdentities[index] &&
        Read(live / file.name) == file.bytes;
    exactSources = exactSources && Identity(source / file.name) == sourceIdentities[index] &&
        Read(source / file.name) == file.bytes;
  }
  Check(exactInstalled, "Installation keeps both archived spellings and the exact file for each browser slot and notebook");
  Check(hasExactClients(recovery, L"aLPHA", L"BRAVO") && Identity(recovery) == previousIdentity &&
        Identity(recovery / previousEdge) == previousEdgeIdentity && Read(recovery / previousEdge) == previousEdgeBytes &&
        Identity(recovery / previousNote) == previousNoteIdentity && Read(recovery / previousNote) == previousNoteBytes,
        "Recovery keeps the prior spellings and both independent original files");
  Check(exactSources && Identity(archive) == archiveIdentity && Read(archive) == archiveBytes && CrcValid(archive),
        "Restore preserves the exact source profiles, notebooks and original CRC-valid backup");
  Check(std::distance(fs::directory_iterator(data), fs::directory_iterator{}) == 2,
        "The data parent retains only the requested live and recovery directories");
  std::cout << "RESTORE_SLOT_FIDELITY_CASES=separate-browser-slots,archived-client-spelling,previous-spelling-recovered\n";
}
