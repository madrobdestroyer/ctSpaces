bool LockFixturePrefix(HANDLE file, DWORD count) {
  OVERLAPPED position{};
  return LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                    0, count, 0, &position) != FALSE;
}

DWORD ProbeFixtureReadError(HANDLE file) {
  char byte = 0;
  DWORD read = 0;
  return ReadFile(file, &byte, 1, &read, nullptr) ? ERROR_SUCCESS : GetLastError();
}

void RestoreInputLockRound(const fs::path &fixture, bool before) {
  const auto source = fixture / L"src/Sites";
  const auto sourceNote = source / L"Incoming/note.ctn";
  const auto archive = fixture / L"input.7z";
  const auto data = fixture / L"data";
  const auto live = data / L"Sites";
  const auto oldNote = live / L"Previous/note.ctn";
  const auto stage = data / L"stage";
  const auto recovery = data / L"R0001";
  const std::string incoming = "incoming bytes from a CRC-valid archive";
  const std::string previous = "previous live notebook bytes";
  Write(sourceNote, incoming);
  Write(oldNote, previous);
  Check(BackUp(source, archive).ok() && CrcValid(archive),
        "The ordinary restore input is a published CRC-valid archive");
  const auto archiveBytes = Read(archive);
  const auto archiveIdentity = Identity(archive);
  const auto sourceIdentity = Identity(sourceNote);
  const auto liveIdentity = Identity(live);
  const auto oldIdentity = Identity(oldNote);
  CtBackup::StagedRestore staged;
  {
    const FileHandle blocker(CreateFileW(archive.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    constexpr DWORD signatureBytes = 32;
    Check(blocker.value != INVALID_HANDLE_VALUE && archiveBytes.size() >= signatureBytes &&
          LockFixturePrefix(blocker.value, signatureBytes),
          "A genuine exclusive byte-range lock covers the input archive signature");
    {
      const FileHandle probe(CreateFileW(archive.c_str(), GENERIC_READ,
          FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
      Check(probe.value != INVALID_HANDLE_VALUE &&
            ProbeFixtureReadError(probe.value) == ERROR_LOCK_VIOLATION,
            "A compatible second open succeeds and its read gets native lock error 33");
    }
    const auto refused = CtBackup::StageValidated7zRestore(
        archive, stage, nullptr, nullptr, staged);
    Check(!refused.ok() && refused.code == HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION),
          "Restore preserves the native byte-range lock HRESULT");
    const bool inUse = refused.details.find(L"in use") != std::wstring::npos;
    const bool damaged = refused.details.find(L"damaged") != std::wstring::npos;
    Check(before ? (!inUse && damaged) : (inUse && !damaged),
          before ? "The baseline calls a valid locked archive damaged" :
                   "The refusal identifies the valid archive as in use");
    Check(!staged.validation && staged.stagingRoot.empty() && staged.payloadDir.empty() &&
          staged.profileCount == 0 && !fs::exists(stage),
          "Input read refusal clears the seal and removes its empty owned staging root");
    Check(Identity(live) == liveIdentity && Identity(oldNote) == oldIdentity &&
          Read(oldNote) == previous && !fs::exists(recovery),
          "The failed stage leaves the previous live notebook and root exact");
    std::cout << "RESTORE_INPUT_LOCK native=33 in_use=" << inUse
              << " damaged=" << damaged << '\n';
  }
  {
    const FileHandle exclusive(CreateFileW(archive.c_str(), GENERIC_READ, 0, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Check(exclusive.value != INVALID_HANDLE_VALUE &&
          ProbeFixtureReadError(exclusive.value) == ERROR_SUCCESS,
          "An exclusive reopen proves the refusal released its archive reader");
  }
  Check(Read(archive) == archiveBytes && Identity(archive) == archiveIdentity &&
        CrcValid(archive), "Unlocking leaves the exact original archive CRC-valid");
  Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() &&
        staged.validation && staged.profileCount == 1,
        "The same archive and staging path work after the native lock releases");
  const auto incomingPath = fs::path(L"Incoming/note.ctn");
  const auto stagedIdentity = Identity(staged.payloadDir / incomingPath);
  bool previousInRecovery = false;
  Check(CtBackup::CommitStagedRestore(staged, live, recovery, nullptr, nullptr,
        &previousInRecovery).ok() && previousInRecovery && !fs::exists(stage),
        "The retry commits and removes its completed staging root");
  Check(Identity(live / incomingPath) == stagedIdentity &&
        Read(live / incomingPath) == incoming && Identity(recovery) == liveIdentity &&
        Identity(recovery / L"Previous/note.ctn") == oldIdentity &&
        Read(recovery / L"Previous/note.ctn") == previous,
        "The retry installs exact payload bytes and moves the exact old data to recovery");
  Check(Identity(sourceNote) == sourceIdentity && Read(sourceNote) == incoming &&
        Identity(archive) == archiveIdentity && Read(archive) == archiveBytes,
        "The failed attempt and successful retry preserve their source and archive");
  std::cout << "RESTORE_INPUT_LOCK_RETRY=1\n";
}

struct RestoreRangeGuard {
  fs::path path;
  FileHandle blocker{INVALID_HANDLE_VALUE};
  unsigned calls = 0;
  DWORD probeError = ERROR_SUCCESS;
};

bool LockStagedFileInGuard(void *context, std::wstring &details) {
  auto &guard = *static_cast<RestoreRangeGuard *>(context);
  ++guard.calls;
  constexpr DWORD sharing = FILE_SHARE_READ | FILE_SHARE_DELETE;
  guard.blocker.value = CreateFileW(guard.path.c_str(), GENERIC_READ, sharing,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (guard.blocker.value != INVALID_HANDLE_VALUE &&
      LockFixturePrefix(guard.blocker.value, 1)) {
    const FileHandle probe(CreateFileW(guard.path.c_str(), GENERIC_READ, sharing,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (probe.value != INVALID_HANDLE_VALUE) {
      guard.probeError = ProbeFixtureReadError(probe.value);
      if (guard.probeError == ERROR_LOCK_VIOLATION) return true;
    }
  }
  details = L"The private fixture could not establish its native staged-file lock.";
  return false;
}

void RestoreCommitLocksRound(const fs::path &fixture) {
  const auto source = fixture / L"src/Sites";
  const auto incomingPath = fs::path(L"Incoming/note.ctn");
  const auto previousPath = fs::path(L"Previous/note.ctn");
  const auto archive = fixture / L"input.7z";
  const auto data = fixture / L"data";
  const auto live = data / L"Sites";
  const auto stage = data / L"stage";
  const auto recovery = data / L"R0001";
  const std::string incoming = "verified incoming notebook";
  const std::string previous = "exact old live notebook";
  Write(source / incomingPath, incoming);
  Write(live / previousPath, previous);
  Check(BackUp(source, archive).ok() && CrcValid(archive),
        "The commit-lock fixture uses an ordinary CRC-valid backup");
  const auto archiveBytes = Read(archive);
  const auto archiveIdentity = Identity(archive);
  const auto liveIdentity = Identity(live);
  const auto previousIdentity = Identity(live / previousPath);
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged).ok() &&
        staged.validation && staged.profileCount == 1,
        "The replacement is verified and sealed before either Windows lock");
  const auto stagedNote = staged.payloadDir / incomingPath;
  const auto stageIdentity = Identity(stage);
  const auto payloadIdentity = Identity(staged.payloadDir);
  const auto noteIdentity = Identity(stagedNote);
  const auto sealed = staged.validation;
  const auto unchanged = [&] {
    return !fs::exists(recovery) && Identity(live) == liveIdentity &&
        Identity(live / previousPath) == previousIdentity &&
        Read(live / previousPath) == previous && Identity(stage) == stageIdentity &&
        Identity(staged.payloadDir) == payloadIdentity && Identity(stagedNote) == noteIdentity &&
        Read(stagedNote) == incoming && staged.validation == sealed;
  };
  {
    constexpr DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE;
    constexpr DWORD flags = FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;
    const FileHandle blocker(CreateFileW(live.c_str(), GENERIC_READ, sharing,
        nullptr, OPEN_EXISTING, flags, nullptr));
    Check(blocker.value != INVALID_HANDLE_VALUE,
          "A genuine live Sites directory reader omits delete sharing");
    const FileHandle probe(CreateFileW(live.c_str(), GENERIC_READ | DELETE, sharing,
        nullptr, OPEN_EXISTING, flags, nullptr));
    const DWORD probeError = probe.value == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    Check(probeError == ERROR_SHARING_VIOLATION,
          "The matching native DELETE open fails with sharing error 32");
    unsigned guardCalls = 0;
    const auto countGuard = [](void *context, std::wstring &) {
      ++*static_cast<unsigned *>(context);
      return true;
    };
    bool previousInRecovery = true;
    const auto refused = CtBackup::CommitStagedRestore(
        staged, live, recovery, countGuard, &guardCalls, &previousInRecovery);
    Check(refused.code == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) &&
          guardCalls == 0 && !previousInRecovery,
          "Commit preserves the real sharing error and stops before its final guard");
    Check(unchanged(), "Directory sharing refusal preserves the exact live and staged trees");
  }
  {
    RestoreRangeGuard guard{stagedNote};
    bool previousInRecovery = true;
    const auto refused = CtBackup::CommitStagedRestore(
        staged, live, recovery, LockStagedFileInGuard, &guard, &previousInRecovery);
    Check(guard.calls == 1 && guard.probeError == ERROR_LOCK_VIOLATION,
          "The final guard introduces a real byte-range lock after its initial snapshot checks");
    Check(refused.code == HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION) && !previousInRecovery,
          "The post-guard snapshot keeps the exact read-lock HRESULT before either move");
  }
  Check(unchanged(), "The post-guard read refusal leaves the same seal, exact data, and no recovery");
  bool previousInRecovery = false;
  Check(CtBackup::CommitStagedRestore(staged, live, recovery, nullptr, nullptr,
        &previousInRecovery).ok() && previousInRecovery && !fs::exists(stage),
        "The same sealed stage commits once both real locks are released");
  Check(Identity(live) == payloadIdentity && Identity(live / incomingPath) == noteIdentity &&
        Read(live / incomingPath) == incoming,
        "Successful retry moves the exact verified payload into live Sites");
  Check(Identity(recovery) == liveIdentity && Identity(recovery / previousPath) == previousIdentity &&
        Read(recovery / previousPath) == previous && Identity(archive) == archiveIdentity &&
        Read(archive) == archiveBytes && CrcValid(archive),
        "Successful retry keeps the exact old collection and original archive");
  std::cout << "RESTORE_COMMIT_LOCKS live_share=32 post_guard_range=33 same_seal_retry=1\n";
}
