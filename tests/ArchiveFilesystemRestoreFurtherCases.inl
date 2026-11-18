namespace restore_further_cases {
struct Fixture {
  explicit Fixture(const fs::path &root)
      : source(root / L"src/Sites"), archive(root / L"input.7z"),
        stage(root / L"stage"), data(root / L"data"), live(data / L"Sites"),
        recovery(data / L"R0001") {}

  const fs::path source, archive, stage, data, live, recovery;
  const fs::path incoming = L"Incoming/note.ctn";
  const std::string incomingBytes = "exact archived notebook for a private restore retry";
  const std::string previousBytes = "exact previous collection notebook";
  CtBackup::StagedRestore staged;
  FileIdentity archiveIdentity{}, sourceIdentity{}, stageIdentity{}, payloadIdentity{}, noteIdentity{};
  std::string archiveBytes;
  unsigned guardCalls = 0;
  bool recovered = true;

  void BackUpAndStage() {
    Write(source / incoming, incomingBytes);
    Check(BackUp(source, archive).ok() && CrcValid(archive),
          "Create an ordinary production backup and verify its CRC");
    archiveIdentity = Identity(archive);
    archiveBytes = Read(archive);
    sourceIdentity = Identity(source / incoming);
    Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() &&
          staged.validation && staged.profileCount == 1 && Read(staged.payloadDir / incoming) == incomingBytes,
          "The production stage seals the exact incoming notebook");
    stageIdentity = Identity(stage);
    payloadIdentity = Identity(staged.payloadDir);
    noteIdentity = Identity(staged.payloadDir / incoming);
  }

  CtBackup::Result Commit(const fs::path &livePath, const fs::path &recoveryPath) {
    return CtBackup::CommitStagedRestore(staged, livePath, recoveryPath,
        [](void *user, std::wstring &) {
          ++*static_cast<unsigned *>(user);
          return true;
        }, &guardCalls, &recovered);
  }

  bool StagePreserved() const {
    return Identity(stage) == stageIdentity && Identity(staged.payloadDir) == payloadIdentity &&
        Identity(staged.payloadDir / incoming) == noteIdentity &&
        Read(staged.payloadDir / incoming) == incomingBytes;
  }

  bool Installed() const {
    return Identity(live) == payloadIdentity && Identity(live / incoming) == noteIdentity &&
        Read(live / incoming) == incomingBytes;
  }

  bool InputsPreserved() const {
    return Identity(archive) == archiveIdentity && Read(archive) == archiveBytes && CrcValid(archive) &&
        Identity(source / incoming) == sourceIdentity && Read(source / incoming) == incomingBytes;
  }

  bool OnlyLiveAndRecovery() const {
    return std::distance(fs::directory_iterator(data), fs::directory_iterator{}) == 2;
  }
};
} // namespace restore_further_cases

void RestoreRecoveryBudgetRound(const fs::path &root) {
  restore_further_cases::Fixture fixture(root);
  const auto client = fixture.live / L"Previous";
  constexpr auto directoryLimit = CtBackup::Detail::kLegacyMaximumDirectoryPathCharacters;
  constexpr auto fileLimit = CtBackup::Detail::kLegacyMaximumFilePathCharacters;
  const auto prefixLength = client.native().size() + 1;
  Check(prefixLength < directoryLimit, "The private fixture has enough path budget to place an exact boundary file");
  const auto directory = client / std::wstring(directoryLimit - prefixLength, L'p');
  const auto previous = directory / L"stateAB.bin";
  const auto previousRelative = previous.lexically_relative(fixture.live);
  const auto longRecovery = fixture.data / L"Sites_PreRestore_2026-11-13";
  Write(previous, fixture.previousBytes);
  Check(directory.native().size() == directoryLimit && previous.native().size() == fileLimit &&
        GetFileAttributesW(previous.c_str()) != INVALID_FILE_ATTRIBUTES,
        "Current data has an ordinary Windows-readable 247-character directory and 259-character file");
  const auto oldRoot = Identity(fixture.live);
  const auto oldNote = Identity(previous);
  fixture.BackUpAndStage();
  const auto seal = fixture.staged.validation;

  const auto refused = fixture.Commit(fixture.live, longRecovery);
  Check(refused.code == HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE) &&
        refused.details.find(L"pre-restore recovery folder") != std::wstring::npos &&
        fixture.guardCalls == 0 && !fixture.recovered,
        "A longer recovery name is refused for the current tree before consent or any move");
  Check(fixture.staged.validation == seal && fixture.StagePreserved() &&
        Identity(fixture.live) == oldRoot && Identity(previous) == oldNote &&
        Read(previous) == fixture.previousBytes && !fs::exists(longRecovery) && !fs::exists(fixture.recovery),
        "The refusal retains the exact seal and previous boundary file without claiming either recovery name");
  const auto committed = fixture.Commit(fixture.live, fixture.recovery);
  Check(committed.ok() && committed.details.empty() && fixture.guardCalls == 1 &&
        fixture.recovered && !fs::exists(fixture.stage),
        "The same seal commits using an ordinary compact recovery sibling");
  const auto recoveredNote = fixture.recovery / previousRelative;
  Check(fixture.Installed() && Identity(fixture.recovery) == oldRoot && Identity(recoveredNote) == oldNote &&
        recoveredNote.native().size() == fileLimit && Read(recoveredNote) == fixture.previousBytes,
        "The exact previous file stays at the Windows boundary while the staged notebook is installed");
  Check(fixture.InputsPreserved() && fixture.OnlyLiveAndRecovery() && !fs::exists(longRecovery),
        "Both attempts preserve the backup and source and leave no abandoned destination guard");
  std::cout << "RESTORE_RECOVERY_BUDGET_CASES=long-name-refusal,compact-sibling-same-seal\n";
}

void RestoreManifestShareRound(const fs::path &root) {
  restore_further_cases::Fixture fixture(root);
  const auto previous = fs::path(L"Previous/note.ctn");
  Write(fixture.live / previous, fixture.previousBytes);
  const auto oldRoot = Identity(fixture.live);
  const auto oldNote = Identity(fixture.live / previous);
  fixture.BackUpAndStage();
  const auto seal = fixture.staged.validation;
  const auto manifest = fixture.stage / L"ctSpacesBackup.manifest";
  const auto manifestIdentity = Identity(manifest);
  const auto manifestBytes = Read(manifest);
  Check(manifestBytes.find("format=ctSpaces-backup\n") == 0,
        "The reader target is the exact ordinary manifest emitted by the production writer");
  {
    const FileHandle exclusive(CreateFileW(manifest.c_str(), GENERIC_READ, 0, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    Check(exclusive.value != INVALID_HANDLE_VALUE,
          "Acquire a real exclusive read lease on the exact staged manifest");
    const auto refused = fixture.Commit(fixture.live, fixture.recovery);
    Check(refused.code == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) &&
          fixture.guardCalls == 0 && !fixture.recovered,
          "Initial snapshot verification reports the native manifest sharing error before consent");
  }
  Check(fixture.staged.validation == seal && fixture.StagePreserved() &&
        Identity(manifest) == manifestIdentity && Read(manifest) == manifestBytes &&
        Identity(fixture.live) == oldRoot && Identity(fixture.live / previous) == oldNote &&
        Read(fixture.live / previous) == fixture.previousBytes && !fs::exists(fixture.recovery),
        "After closing the lease, exact staged data, manifest and live data remain at their original paths");
  const auto committed = fixture.Commit(fixture.live, fixture.recovery);
  Check(committed.ok() && committed.details.empty() && fixture.guardCalls == 1 && fixture.recovered &&
        !fs::exists(fixture.stage), "The same seal commits after the reader closes and removes its wrapper and manifest");
  Check(fixture.Installed() && Identity(fixture.recovery) == oldRoot &&
        Identity(fixture.recovery / previous) == oldNote && Read(fixture.recovery / previous) == fixture.previousBytes,
        "Retry installs the exact staged notebook and retains the original collection in recovery");
  Check(fixture.InputsPreserved() && fixture.OnlyLiveAndRecovery(),
        "Both attempts preserve the input archive and source without leaving a destination guard file");
  std::cout << "RESTORE_MANIFEST_SHARE_CASES=exclusive-manifest-refusal,same-seal-retry\n";
}

void RestoreStageOverlapRound(const fs::path &root) {
  restore_further_cases::Fixture fixture(root);
  const auto previous = fs::path(L"Previous/note.ctn");
  Write(fixture.live / previous, fixture.previousBytes);
  const auto oldRoot = Identity(fixture.live);
  const auto oldNote = Identity(fixture.live / previous);
  fixture.BackUpAndStage();
  const auto seal = fixture.staged.validation;
  const auto nestedLive = fixture.stage / L"NewSites";
  const auto nestedRecovery = fixture.live / L"R0001";
  Check(!fs::exists(nestedLive) && !fs::exists(nestedRecovery) && !fs::exists(fixture.recovery),
        "The proposed overlapping names are vacant ordinary paths inside the private trees");

  const auto stageOverlap = fixture.Commit(nestedLive, fixture.recovery);
  Check(stageOverlap.code == E_INVALIDARG && stageOverlap.details.find(L"distinct trees") != std::wstring::npos &&
        fixture.guardCalls == 0 && !fixture.recovered,
        "A live destination inside the staging tree is refused before consent");
  fixture.recovered = true;
  const auto recoveryOverlap = fixture.Commit(fixture.live, nestedRecovery);
  Check(recoveryOverlap.code == E_INVALIDARG && recoveryOverlap.details.find(L"distinct trees") != std::wstring::npos &&
        fixture.guardCalls == 0 && !fixture.recovered,
        "A recovery destination inside the live tree is refused before consent");
  Check(fixture.staged.validation == seal && fixture.StagePreserved() &&
        Identity(fixture.live) == oldRoot && Identity(fixture.live / previous) == oldNote &&
        Read(fixture.live / previous) == fixture.previousBytes && !fs::exists(nestedLive) &&
        !fs::exists(nestedRecovery) && !fs::exists(fixture.recovery),
        "Both refusals retain the exact verified and current trees without claiming any alternate path");
  const auto committed = fixture.Commit(fixture.live, fixture.recovery);
  Check(committed.ok() && committed.details.empty() && fixture.guardCalls == 1 &&
        fixture.recovered && !fs::exists(fixture.stage),
        "The same sealed stage commits to distinct live and recovery paths");
  Check(fixture.Installed() && Identity(fixture.recovery) == oldRoot &&
        Identity(fixture.recovery / previous) == oldNote && Read(fixture.recovery / previous) == fixture.previousBytes &&
        !fs::exists(fixture.recovery / L"R0001"), "Commit moves both exact trees without creating an overlapping target");
  Check(fixture.InputsPreserved() && fixture.OnlyLiveAndRecovery(),
        "All attempts preserve the source and backup without leaving a destination guard file");
  std::cout << "RESTORE_STAGE_OVERLAP_CASES=stage-destination-refusal,nested-recovery-refusal,distinct-path-retry\n";
}
