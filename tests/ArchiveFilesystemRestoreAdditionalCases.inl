struct RestoreProgressEvent {
  _7zOp operation;
  unsigned percent;
  fs::path item;
};

void RestoreProgressRound(const fs::path &root) {
  restore_further_cases::Fixture fixture(root);
  const auto previous = fs::path(L"Previous/note.ctn");
  Write(fixture.source / fixture.incoming, fixture.incomingBytes);
  Write(fixture.live / previous, fixture.previousBytes);
  const auto oldRoot = Identity(fixture.live);
  const auto oldNote = Identity(fixture.live / previous);
  Check(BackUp(fixture.source, fixture.archive).ok() && CrcValid(fixture.archive),
        "Create an ordinary backup for a complete successful progress trace");
  const auto archiveIdentity = Identity(fixture.archive);
  const auto archiveBytes = Read(fixture.archive);
  const auto sourceIdentity = Identity(fixture.source / fixture.incoming);
  std::vector<RestoreProgressEvent> reports;
  const auto record = [](void *user, _7zOp operation, unsigned percent, const wchar_t *item) {
    auto &events = *static_cast<std::vector<RestoreProgressEvent> *>(user);
    if (events.size() == 1024) throw std::runtime_error("Unexpected private progress report count");
    events.push_back({operation, percent, item ? fs::path(item) : fs::path{}});
  };
  Check(CtBackup::StageValidated7zRestore(fixture.archive, fixture.stage, record, &reports, fixture.staged).ok() &&
        fixture.staged.validation && fixture.staged.profileCount == 1 &&
        Read(fixture.staged.payloadDir / fixture.incoming) == fixture.incomingBytes,
        "Progress reporting completes normally while staging the exact incoming notebook");
  Check(!reports.empty() && reports.front().percent == 0 && reports.back().percent == 100 && reports.back().item.empty(),
        "A successful trace starts at zero and ends with an explicit completion report");
  bool allReportsValid = true;
  bool sawItem = false;
  unsigned last = 0;
  unsigned terminal = 0;
  for (const auto &event : reports) {
    allReportsValid = allReportsValid && event.operation == _7zOp::Extract &&
        event.percent >= last && event.percent <= 100;
    last = event.percent;
    if (event.item.empty()) {
      if (event.percent == 100) ++terminal;
    } else {
      const auto item = event.item.lexically_normal();
      allReportsValid = allReportsValid && (item == L"Sites" || item == fs::path(L"Sites/Incoming") ||
          item == fs::path(L"Sites") / fixture.incoming || item == L"ctSpacesBackup.manifest");
      sawItem = true;
    }
  }
  Check(allReportsValid && sawItem, "Every extraction report has a bounded monotonic percent and refers only to archive items");
  Check(terminal == 1, "The completed operation reports exactly one terminal 100 percent event without an item");
  const auto stagedIdentity = Identity(fixture.staged.payloadDir);
  const auto stagedNote = Identity(fixture.staged.payloadDir / fixture.incoming);
  Check(Identity(fixture.live) == oldRoot && Identity(fixture.live / previous) == oldNote &&
        Read(fixture.live / previous) == fixture.previousBytes && !fs::exists(fixture.recovery),
        "Reporting extraction progress leaves the current collection in place until commit");
  const auto committed = fixture.Commit(fixture.live, fixture.recovery);
  Check(committed.ok() && committed.details.empty() && fixture.guardCalls == 1 && fixture.recovered &&
        !fs::exists(fixture.stage), "Commit installs the verified progress fixture and removes its temporary stage");
  Check(Identity(fixture.live) == stagedIdentity && Identity(fixture.live / fixture.incoming) == stagedNote &&
        Read(fixture.live / fixture.incoming) == fixture.incomingBytes && Identity(fixture.recovery) == oldRoot &&
        Identity(fixture.recovery / previous) == oldNote && Read(fixture.recovery / previous) == fixture.previousBytes,
        "Commit moves the exact staged notebook and retains the exact prior tree");
  Check(fixture.OnlyLiveAndRecovery() && Identity(fixture.archive) == archiveIdentity &&
        Read(fixture.archive) == archiveBytes && CrcValid(fixture.archive) &&
        Identity(fixture.source / fixture.incoming) == sourceIdentity &&
        Read(fixture.source / fixture.incoming) == fixture.incomingBytes,
        "Reporting and commit preserve the source and exact CRC-valid backup");
  std::cout << "RESTORE_PROGRESS_CASE=bounded-extract-reports,terminal-completion,exact-commit\n";
}

void RestoreExclusiveLiveNoteRound(const fs::path &root) {
  restore_further_cases::Fixture fixture(root);
  const auto previous = fs::path(L"Previous/note.ctn");
  const auto previousFile = fixture.live / previous;
  Write(previousFile, fixture.previousBytes);
  const auto oldRoot = Identity(fixture.live);
  const auto oldNote = Identity(previousFile);
  fixture.BackUpAndStage();
  const auto seal = fixture.staged.validation;
  {
    const FileHandle exclusive(CreateFileW(previousFile.c_str(), GENERIC_READ, 0, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Check(exclusive.value != INVALID_HANDLE_VALUE, "Acquire a real exclusive handle on the old live notebook");
    const auto refused = fixture.Commit(fixture.live, fixture.recovery);
    Check((refused.code == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) ||
           refused.code == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED)) &&
          fixture.guardCalls == 1 && !fixture.recovered,
          "Windows refuses the old directory move while its notebook is held and reports its native error");
    Check(fixture.staged.validation == seal && fixture.StagePreserved() &&
          Identity(fixture.live) == oldRoot && !fs::exists(fixture.recovery),
          "The refusal leaves the verified replacement and exact old collection at their original paths");
  }
  Check(Identity(previousFile) == oldNote && Read(previousFile) == fixture.previousBytes &&
        std::distance(fs::directory_iterator(fixture.data), fs::directory_iterator{}) == 1,
        "The released old notebook has its exact identity and bytes with no recovery or guard artifact");
  const auto committed = fixture.Commit(fixture.live, fixture.recovery);
  Check(committed.ok() && committed.details.empty() && fixture.guardCalls == 2 && fixture.recovered &&
        !fs::exists(fixture.stage), "The same seal commits after closing the old notebook handle");
  Check(fixture.Installed() && Identity(fixture.recovery) == oldRoot &&
        Identity(fixture.recovery / previous) == oldNote && Read(fixture.recovery / previous) == fixture.previousBytes,
        "Retry installs the exact staged notebook and retains the original notebook and root");
  Check(fixture.InputsPreserved() && fixture.OnlyLiveAndRecovery(),
        "Both attempts preserve the source and backup without leaving a guard file");
  std::cout << "RESTORE_EXCLUSIVE_LIVE_NOTE_CASE=locked-previous-notebook,same-seal-retry\n";
}

void RestoreStageParentFileRound(const fs::path &root) {
  restore_further_cases::Fixture fixture(root);
  const auto previous = fs::path(L"Previous/note.ctn");
  const auto proposedParent = root / L"temporary";
  const auto proposedStage = proposedParent / L"stage";
  const auto heldFile = root / L"saved-temporary.txt";
  const std::string occupant = "independent file occupying the staging parent name";
  Write(proposedParent, occupant);
  Write(fixture.source / fixture.incoming, fixture.incomingBytes);
  Write(fixture.live / previous, fixture.previousBytes);
  const auto occupantIdentity = Identity(proposedParent);
  const auto oldRoot = Identity(fixture.live);
  const auto oldNote = Identity(fixture.live / previous);
  Check(BackUp(fixture.source, fixture.archive).ok() && CrcValid(fixture.archive),
        "Create a normal CRC-valid backup independently of the occupied staging parent");
  const auto archiveIdentity = Identity(fixture.archive);
  const auto archiveBytes = Read(fixture.archive);
  const auto sourceIdentity = Identity(fixture.source / fixture.incoming);
  const auto refused = CtBackup::StageValidated7zRestore(fixture.archive, proposedStage, nullptr, nullptr, fixture.staged);
  Check(!refused.ok() && !fixture.staged.validation && fixture.staged.stagingRoot.empty() &&
        fixture.staged.payloadDir.empty() && fixture.staged.profileCount == 0,
        "A regular file at the proposed stage parent prevents creating a reusable staged restore");
  Check(Identity(proposedParent) == occupantIdentity && Read(proposedParent) == occupant &&
        Identity(fixture.live) == oldRoot && Identity(fixture.live / previous) == oldNote &&
        Read(fixture.live / previous) == fixture.previousBytes && !fs::exists(fixture.recovery),
        "The refused stage preserves the exact independent file and prior collection");
  fs::rename(proposedParent, heldFile);
  Check(!fs::exists(proposedParent) && Identity(heldFile) == occupantIdentity && Read(heldFile) == occupant,
        "Move the exact private occupant aside to make the intended parent available");
  Check(CtBackup::StageValidated7zRestore(fixture.archive, proposedStage, nullptr, nullptr, fixture.staged).ok() &&
        fixture.staged.validation && fixture.staged.profileCount == 1 &&
        Read(fixture.staged.payloadDir / fixture.incoming) == fixture.incomingBytes,
        "Retry creates the intended stage at the same requested path and validates the exact notebook");
  const auto stagedIdentity = Identity(fixture.staged.payloadDir);
  const auto stagedNote = Identity(fixture.staged.payloadDir / fixture.incoming);
  const auto committed = fixture.Commit(fixture.live, fixture.recovery);
  Check(committed.ok() && committed.details.empty() && fixture.guardCalls == 1 && fixture.recovered &&
        !fs::exists(proposedStage), "The verified retry commits and removes its completed stage");
  Check(Identity(fixture.live) == stagedIdentity && Identity(fixture.live / fixture.incoming) == stagedNote &&
        Read(fixture.live / fixture.incoming) == fixture.incomingBytes && Identity(fixture.recovery) == oldRoot &&
        Identity(fixture.recovery / previous) == oldNote && Read(fixture.recovery / previous) == fixture.previousBytes,
        "Retry installs the exact staged and prior collection trees at the intended locations");
  Check(Identity(heldFile) == occupantIdentity && Read(heldFile) == occupant && fs::is_empty(proposedParent),
        "The moved independent file stays exact and no extraction data remains in the parent");
  Check(fixture.OnlyLiveAndRecovery() && Identity(fixture.archive) == archiveIdentity &&
        Read(fixture.archive) == archiveBytes && CrcValid(fixture.archive) &&
        Identity(fixture.source / fixture.incoming) == sourceIdentity &&
        Read(fixture.source / fixture.incoming) == fixture.incomingBytes,
        "Both attempts preserve the original input and source and leave no destination guard file");
  std::cout << "RESTORE_STAGE_PARENT_FILE_CASE=parent-file-preserved,same-path-retry\n";
}

void RestoreMovedInputRound(const fs::path &root, bool before) {
  restore_further_cases::Fixture fixture(root);
  const auto previous = fs::path(L"Previous/note.ctn");
  const auto held = root / L"moved.7z";
  Write(fixture.source / fixture.incoming, fixture.incomingBytes);
  Write(fixture.live / previous, fixture.previousBytes);
  Check(BackUp(fixture.source, fixture.archive).ok() && CrcValid(fixture.archive),
        "Create a CRC-valid ordinary archive at the selected path");
  const auto archiveBytes = Read(fixture.archive);
  const auto archiveIdentity = Identity(fixture.archive);
  const auto sourceIdentity = Identity(fixture.source / fixture.incoming);
  const auto oldRoot = Identity(fixture.live);
  const auto oldNote = Identity(fixture.live / previous);

  CheckOrdinaryAncestors(fixture.stage);
  CheckOrdinaryAncestors(fixture.archive);
  CheckOrdinaryAncestors(held);
  archive_restore_boundary::MoveFixture move{fixture.stage, fixture.archive, held};
  {
    const FileHandle archive(CreateFileW(fixture.archive.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Check(root.parent_path() == fs::path(CTSPACES_ARCHIVE_FILESYSTEM_FIXTURE) &&
          fixture.stage.parent_path() == root && fixture.archive.parent_path() == root && held.parent_path() == root &&
          !fs::exists(fixture.stage) && !fs::exists(held) && archive.value != INVALID_HANDLE_VALUE &&
          GetFileInformationByHandle(archive.value, &move.archiveIdentity) && move.archiveIdentity.nNumberOfLinks == 1,
          "Bind the exact ordinary archive and vacant stage and sibling in the private fixture");
  }
  CtBackup::Result refused;
  {
    struct ScopedMove {
      explicit ScopedMove(archive_restore_boundary::MoveFixture &move) { archive_restore_boundary::active = &move; }
      ~ScopedMove() { archive_restore_boundary::active = nullptr; }
    } arm(move);
    refused = CtBackup::StageValidated7zRestore(fixture.archive, fixture.stage, nullptr, nullptr, fixture.staged);
  }
  Check(move.attempted && move.moved && !fs::exists(fixture.archive) &&
        Identity(held) == archiveIdentity && Read(held) == archiveBytes && CrcValid(held),
        "A real Windows move takes the exact archive to its vacant sibling after the stage is opened");
  std::cout << "RESTORE_MOVED_INPUT_HRESULT=0x" << std::hex << static_cast<unsigned long>(refused.code)
            << std::dec << '\n';
  const bool callsItDamaged = refused.details.find(L"damaged") != std::wstring::npos;
  const bool reportsMissing = refused.details.find(L"does not exist") != std::wstring::npos ||
      refused.details.find(L"no longer available") != std::wstring::npos;
  Check(refused.code == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) &&
        (before ? (callsItDamaged && !reportsMissing) : (reportsMissing && !callsItDamaged)),
        before ? "The native file-not-found cause is mislabeled as archive damage" :
                 "The native file-not-found cause identifies the missing selected backup");
  Check(!fixture.staged.validation && fixture.staged.stagingRoot.empty() && fixture.staged.payloadDir.empty() &&
        fixture.staged.profileCount == 0 && !fs::exists(fixture.stage) && !fs::exists(fixture.recovery) &&
        Identity(fixture.live) == oldRoot && Identity(fixture.live / previous) == oldNote &&
        Read(fixture.live / previous) == fixture.previousBytes,
        "The failed open clears its output and disposes its empty stage while preserving the exact live collection");
  fs::rename(held, fixture.archive);
  Check(!fs::exists(held) && Identity(fixture.archive) == archiveIdentity && Read(fixture.archive) == archiveBytes,
        "Return the exact original archive to its selected path");
  Check(CtBackup::StageValidated7zRestore(fixture.archive, fixture.stage, nullptr, nullptr, fixture.staged).ok() &&
        fixture.staged.validation && fixture.staged.profileCount == 1 &&
        Read(fixture.staged.payloadDir / fixture.incoming) == fixture.incomingBytes,
        "The returned input validates using the same stage after disarming the scoped move");
  const auto payloadIdentity = Identity(fixture.staged.payloadDir);
  const auto noteIdentity = Identity(fixture.staged.payloadDir / fixture.incoming);
  const auto committed = fixture.Commit(fixture.live, fixture.recovery);
  Check(committed.ok() && committed.details.empty() && fixture.guardCalls == 1 && fixture.recovered &&
        !fs::exists(fixture.stage) && Identity(fixture.live) == payloadIdentity &&
        Identity(fixture.live / fixture.incoming) == noteIdentity && Read(fixture.live / fixture.incoming) == fixture.incomingBytes &&
        Identity(fixture.recovery) == oldRoot && Identity(fixture.recovery / previous) == oldNote &&
        Read(fixture.recovery / previous) == fixture.previousBytes,
        "Retry installs the exact staged tree and recovers the original live collection");
  Check(Identity(fixture.archive) == archiveIdentity && Read(fixture.archive) == archiveBytes && CrcValid(fixture.archive) &&
        Identity(fixture.source / fixture.incoming) == sourceIdentity &&
        Read(fixture.source / fixture.incoming) == fixture.incomingBytes && fixture.OnlyLiveAndRecovery(),
        "Both attempts preserve the original source and CRC-valid archive and leave no destination guard");
  std::cout << "RESTORE_MOVED_INPUT_CASE=" << (before ? "damage-wording-observed" : "missing-input-reported") << '\n';
}
