struct PackedReadLock {
  const fs::path &archive;
  HANDLE blocker;
  bool attempted = false;
  bool locked = false;
  DWORD probeError = ERROR_SUCCESS;
  unsigned firstPercent = 100;
};

void LockArchivePackedData(void *user, _7zOp operation, unsigned percent,
                           const wchar_t *) {
  auto &state = *static_cast<PackedReadLock *>(user);
  if (operation != _7zOp::Extract || state.attempted) return;
  state.attempted = true;
  state.firstPercent = percent;

  // The extractor reports its total before decoding any packed streams.
  // Keep the signature header readable and lock only the remaining bytes.
  constexpr DWORD signatureBytes = 32;
  LARGE_INTEGER size{};
  OVERLAPPED position{};
  position.Offset = signatureBytes;
  if (!GetFileSizeEx(state.blocker, &size) || size.QuadPart <= signatureBytes ||
      size.QuadPart > MAXDWORD) return;
  state.locked = LockFileEx(state.blocker,
      LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
      static_cast<DWORD>(size.QuadPart) - signatureBytes, 0, &position) != FALSE;
  if (!state.locked) return;

  const FileHandle probe(CreateFileW(state.archive.c_str(), GENERIC_READ,
      FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  LARGE_INTEGER packedOffset{};
  packedOffset.QuadPart = signatureBytes;
  if (probe.value != INVALID_HANDLE_VALUE &&
      SetFilePointerEx(probe.value, packedOffset, nullptr, FILE_BEGIN))
    state.probeError = ProbeFixtureReadError(probe.value);
}

void RestorePackedLockRound(const fs::path &fixture, bool before) {
  const auto source = fixture / L"src/Sites";
  const auto relative = fs::path(L"Incoming/note.ctn");
  const auto archive = fixture / L"packed.7z";
  const auto data = fixture / L"data";
  const auto live = data / L"Sites";
  const auto oldNote = live / L"Previous/note.ctn";
  const auto stage = data / L"stage";
  const auto recovery = data / L"R0001";
  std::string payload(24 * 1024, '\0');
  unsigned random = 0x6426E060u;
  for (char &byte : payload) {
    random ^= random << 13;
    random ^= random >> 17;
    random ^= random << 5;
    byte = static_cast<char>(random >> 8);
  }
  const std::string original = "previous client preserved through packed read refusal";
  Write(source / relative, payload);
  Write(oldNote, original);
  Check(BackUp(source, archive).ok() && CrcValid(archive),
        "Create an ordinary CRC-valid archive with a nonempty packed stream");
  const auto archiveBytes = Read(archive);
  const auto archiveIdentity = Identity(archive);
  const auto sourceIdentity = Identity(source / relative);
  const auto oldRootIdentity = Identity(live);
  const auto oldNoteIdentity = Identity(oldNote);
  CtBackup::StagedRestore staged;
  {
    const FileHandle blocker(CreateFileW(archive.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Check(blocker.value != INVALID_HANDLE_VALUE,
          "Open a share-compatible independent reader on the original archive");
    PackedReadLock lock{archive, blocker.value};
    const auto refused = CtBackup::StageValidated7zRestore(
        archive, stage, LockArchivePackedData, &lock, staged);
    Check(lock.attempted && lock.firstPercent == 0 && lock.locked &&
          lock.probeError == ERROR_LOCK_VIOLATION,
          "Initial extraction progress introduces a real lock after inspection");
    const bool inUse = refused.details.find(L"in use") != std::wstring::npos;
    const bool generic = refused.details.find(L"reading or writing") != std::wstring::npos;
    std::cout << "RESTORE_PACKED_LOCK hresult=0x" << std::hex
              << static_cast<unsigned long>(refused.code) << std::dec
              << " in_use=" << inUse << " generic=" << generic << '\n';
    Check(refused.code == HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION),
          "The packed stream failure preserves the native lock HRESULT");
    Check(before ? (!inUse && generic) : (inUse && !generic),
          before ? "The baseline groups the packed read lock with generic Windows errors" :
                   "The packed read failure identifies the original backup as in use");
    Check(!staged.validation && staged.stagingRoot.empty() && staged.payloadDir.empty() &&
          staged.profileCount == 0 && (!fs::exists(stage) || fs::is_empty(stage)),
          "A packed read refusal clears its seal and removes unverified staging files");
    Check(Identity(live) == oldRootIdentity && Identity(oldNote) == oldNoteIdentity &&
          Read(oldNote) == original && !fs::exists(recovery),
          "Extraction refusal leaves the original live data and recovery name intact");
  }
  {
    const FileHandle exclusive(CreateFileW(archive.c_str(), GENERIC_READ, 0,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Check(exclusive.value != INVALID_HANDLE_VALUE &&
          ProbeFixtureReadError(exclusive.value) == ERROR_SUCCESS,
          "The refusal releases the archive lease after the independent lock closes");
  }
  Check(Identity(archive) == archiveIdentity && Read(archive) == archiveBytes && CrcValid(archive),
        "The archive survives the refused extraction with its original identity and CRC");
  Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() &&
        staged.validation && Read(staged.payloadDir / relative) == payload,
        "The same archive and stage name validate after unlocking the packed bytes");
  const auto incomingIdentity = Identity(staged.payloadDir / relative);
  bool recovered = false;
  Check(CtBackup::CommitStagedRestore(staged, live, recovery, nullptr, nullptr,
        &recovered).ok() && recovered && !fs::exists(stage),
        "The unlocked retry commits and disposes its completed stage");
  Check(Identity(live / relative) == incomingIdentity && Read(live / relative) == payload &&
        Identity(recovery) == oldRootIdentity &&
        Identity(recovery / L"Previous/note.ctn") == oldNoteIdentity &&
        Read(recovery / L"Previous/note.ctn") == original,
        "The retry installs the exact staged client and retains the exact original collection");
  Check(Identity(archive) == archiveIdentity && Read(archive) == archiveBytes &&
        Identity(source / relative) == sourceIdentity && Read(source / relative) == payload,
        "Both attempts preserve the exact original source and backup");
  std::cout << "RESTORE_PACKED_LOCK_RETRY=1\n";
}

struct DeepRestoreGate {
  bool allow = false;
  unsigned calls = 0;
};

bool DeepRestoreConsent(void *user, std::wstring &failure) {
  auto &gate = *static_cast<DeepRestoreGate *>(user);
  ++gate.calls;
  if (!gate.allow) failure = L"The private restore consent was withheld.";
  return gate.allow;
}

void RestoreDeepRound(const fs::path &fixture) {
  const auto source = fixture / L"src/Sites";
  const auto relative = fs::path(L"Incoming/note.ctn");
  const auto archive = fixture / L"incoming.7z";
  const auto stage = fixture / L"staging/ready";
  const auto live = fixture / L"live/Sites";
  const auto oldRelative = fs::path(L"Previous/note.ctn");
  const auto history = fixture / L"history";
  const auto recovery = history / L"R0001";
  const std::string newNote = "incoming note from the verified split-parent stage";
  const std::string oldNote = "previous note for the initially empty recovery parent";
  Write(source / relative, newNote);
  Write(live / oldRelative, oldNote);
  Check(BackUp(source, archive).ok() && CrcValid(archive),
        "The split-parent restore uses a valid ordinary backup");
  const auto archiveIdentity = Identity(archive);
  const auto archiveBytes = Read(archive);
  const auto oldRoot = Identity(live);
  const auto oldFile = Identity(live / oldRelative);
  const auto liveParent = Identity(live.parent_path());
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() &&
        staged.validation && staged.profileCount == 1,
        "Stage the exact payload independently of both destination parents");
  const auto seal = staged.validation;
  const auto stageIdentity = Identity(stage);
  const auto newRoot = Identity(staged.payloadDir);
  const auto newFile = Identity(staged.payloadDir / relative);
  const auto unchanged = [&] {
    return staged.validation == seal && Identity(stage) == stageIdentity &&
        Identity(staged.payloadDir) == newRoot &&
        Identity(staged.payloadDir / relative) == newFile &&
        Read(staged.payloadDir / relative) == newNote && Identity(live) == oldRoot &&
        Identity(live / oldRelative) == oldFile && Read(live / oldRelative) == oldNote &&
        fs::directory_iterator(live.parent_path())->path().filename() == live.filename() &&
        std::distance(fs::directory_iterator(live.parent_path()), fs::directory_iterator{}) == 1 &&
        !fs::exists(recovery);
  };
  DeepRestoreGate gate;
  bool recovered = true;
  const auto missing = CtBackup::CommitStagedRestore(staged, live, recovery,
      DeepRestoreConsent, &gate, &recovered);
  Check((missing.code == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
         missing.code == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) &&
        gate.calls == 0 && !recovered && !fs::exists(history),
        "The real missing recovery parent refuses before consent or either move");
  Check(unchanged(), "The missing parent refusal preserves the complete stage and live tree");

  Check(fs::create_directory(history) && fs::is_empty(history),
        "Make the independent recovery parent available as an empty directory");
  const auto historyParent = Identity(history);
  recovered = true;
  const auto veto = CtBackup::CommitStagedRestore(staged, live, recovery,
      DeepRestoreConsent, &gate, &recovered);
  Check(veto.code == HRESULT_FROM_WIN32(ERROR_BUSY) && gate.calls == 1 && !recovered &&
        veto.details == L"The private restore consent was withheld.",
        "With both parents bound, the final consent veto still prevents movement");
  Check(unchanged() && Identity(live.parent_path()) == liveParent &&
        Identity(history) == historyParent && fs::is_empty(history),
        "The veto releases both parent guards and keeps the same sealed stage reusable");

  gate.allow = true;
  const auto installed = CtBackup::CommitStagedRestore(staged, live, recovery,
      DeepRestoreConsent, &gate, &recovered);
  Check(installed.ok() && installed.details.empty() && gate.calls == 2 && recovered &&
        !fs::exists(stage), "The identical stage succeeds when the separate recovery parent and consent are ready");
  Check(Identity(live) == newRoot && Identity(live / relative) == newFile &&
        Read(live / relative) == newNote && Identity(recovery) == oldRoot &&
        Identity(recovery / oldRelative) == oldFile && Read(recovery / oldRelative) == oldNote,
        "Split-parent commit moves both exact trees and preserves their independent notes");
  Check(Identity(live.parent_path()) == liveParent && Identity(history) == historyParent &&
        std::distance(fs::directory_iterator(live.parent_path()), fs::directory_iterator{}) == 1 &&
        std::distance(fs::directory_iterator(history), fs::directory_iterator{}) == 1,
        "Successful commit leaves neither a live nor a recovery parent guard file");
  Check(Identity(archive) == archiveIdentity && Read(archive) == archiveBytes &&
        CrcValid(archive) && Read(source / relative) == newNote,
        "Both refusals and the final commit preserve the original backup and source");
  std::cout << "RESTORE_DEEP_CASES=missing_recovery_parent,split_parent_consent_retry\n";
}
