void BackupByteRangeLock(const fs::path &fixture) {
  const auto sites = fixture / L"d/Sites";
  const auto source = sites / L"Alpha/note.ctn";
  const auto archive = fixture / L"out/existing.7z";
  fs::create_directories(archive.parent_path());
  Write(source, "original notebook for byte-range lock round");
  Check(BackUp(sites, archive).ok() && CrcValid(archive),
        "The byte-range lock fixture starts with a real verified backup");
  const auto oldBytes = Read(archive);
  const auto oldId = Identity(archive);
  const auto sourceId = Identity(source);
  const std::string updated = "new notebook bytes that cross the locked interval";
  Write(source, updated);

  {
    const FileHandle lock(CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Check(lock.value != INVALID_HANDLE_VALUE, "Open the source with a compatible read share");
    OVERLAPPED range{};
    range.Offset = 9;
    constexpr DWORD lockBytes = 13;
    Check(LockFileEx(lock.value, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
        0, lockBytes, 0, &range) != FALSE, "Acquire a real exclusive byte-range lock");

    const FileHandle probe(CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    Check(probe.value != INVALID_HANDLE_VALUE, "The byte-range lock still allows an independent source open");
    char probeBytes[32]{};
    DWORD read = 0;
    const BOOL readable = ReadFile(probe.value, probeBytes, static_cast<DWORD>(sizeof(probeBytes)), &read, nullptr);
    const DWORD readError = readable ? ERROR_SUCCESS : GetLastError();
    Check(!readable && readError == ERROR_LOCK_VIOLATION,
          "Windows denies a real read through the independently opened handle");

    const auto refused = BackUp(sites, archive);
    std::cout << "BACKUP_SOURCE_RANGE_LOCK_HRESULT=0x" << std::hex
              << static_cast<unsigned long>(refused.code) << std::dec << '\n';
    Check(!refused.ok() && refused.code == HRESULT_FROM_WIN32(ERROR_LOCK_VIOLATION),
          "Backup surfaces the actual source byte-range read failure");
    Check(Read(archive) == oldBytes && Identity(archive) == oldId && CrcValid(archive) &&
          HasOnlyArchive(archive), "The failed read preserves the exact old archive and cleans its temporary files");
    Check(UnlockFileEx(lock.value, 0, lockBytes, 0, &range) != FALSE,
          "Release the original source byte-range lock for retry");
  }

  Check(Read(source) == updated && Identity(source) == sourceId,
        "The failed backup preserves the current source object and notebook bytes");
  Check(BackUp(sites, archive).ok() && CrcValid(archive) && HasOnlyArchive(archive),
        "The retry publishes a CRC-valid backup after the byte-range lock is released");
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"verified", nullptr, nullptr, staged).ok() &&
        staged.profileCount == 1 && Read(staged.payloadDir / L"Alpha/note.ctn") == updated &&
        Identity(archive) != oldId && Read(source) == updated && Identity(source) == sourceId,
        "The retry stages the exact current notebook and preserves the source object");
}

void BackupReadOnlyDestination(const fs::path &fixture) {
  const auto sites = fixture / L"d/Sites";
  const auto source = sites / L"Alpha/note.ctn";
  const auto archive = fixture / L"out/existing.7z";
  fs::create_directories(archive.parent_path());
  Write(source, "original notebook for read-only destination round");
  Check(BackUp(sites, archive).ok() && CrcValid(archive),
        "The read-only destination fixture starts with a real verified backup");
  const auto oldBytes = Read(archive);
  const auto oldId = Identity(archive);
  const auto sourceId = Identity(source);
  const std::string updated = "new notebook bytes for the destination retry";
  Write(source, updated);

  const auto originalAttributes = GetFileAttributesW(archive.c_str());
  const DWORD readOnlyAttributes = (originalAttributes & ~FILE_ATTRIBUTE_NORMAL) | FILE_ATTRIBUTE_READONLY;
  Check(originalAttributes != INVALID_FILE_ATTRIBUTES &&
        (originalAttributes & FILE_ATTRIBUTE_READONLY) == 0 &&
        SetFileAttributesW(archive.c_str(), readOnlyAttributes) != FALSE,
        "Set the read-only attribute on the exact existing archive");
  const auto refused = BackUp(sites, archive);
  std::cout << "BACKUP_READONLY_DESTINATION_HRESULT=0x" << std::hex
            << static_cast<unsigned long>(refused.code) << std::dec << '\n';
  Check(!refused.ok() && (refused.code == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) ||
                         refused.code == HRESULT_FROM_WIN32(ERROR_WRITE_PROTECT)),
        "Native publication refuses replacing the read-only destination");
  Check(Read(archive) == oldBytes && Identity(archive) == oldId && CrcValid(archive) &&
        GetFileAttributesW(archive.c_str()) == readOnlyAttributes && HasOnlyArchive(archive),
        "Refusal preserves the exact read-only archive and cleans temporary files");
  Check(Read(source) == updated && Identity(source) == sourceId,
        "Read-only destination refusal preserves current source bytes and identity");
  Check(SetFileAttributesW(archive.c_str(), originalAttributes) != FALSE &&
        GetFileAttributesW(archive.c_str()) == originalAttributes,
        "Remove only the read-only attribute from the existing archive before retry");

  Check(BackUp(sites, archive).ok() && CrcValid(archive) && HasOnlyArchive(archive),
        "Retry publishes after the exact destination is writable again");
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"verified", nullptr, nullptr, staged).ok() &&
        staged.profileCount == 1 && Read(staged.payloadDir / L"Alpha/note.ctn") == updated &&
        Identity(archive) != oldId && Read(source) == updated && Identity(source) == sourceId,
        "The retry stages the exact notebook and preserves the source object");
}

void BackupLocksRound(const fs::path &fixture) {
  BackupByteRangeLock(fixture / L"source-range");
  BackupReadOnlyDestination(fixture / L"readonly-output");
  std::cout << "BACKUP_LOCKS_CASES=source-byte-range-lock,readonly-existing-destination\n";
}
