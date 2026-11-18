struct BackupFixtureMetadata {
  ULONGLONG created;
  ULONGLONG modified;
  DWORD attributes;
  bool operator==(const BackupFixtureMetadata &) const = default;
};

constexpr BackupFixtureMetadata kOriginalBackupMetadata{
    127000000000000000ULL, 128790414900000000ULL, 0};
constexpr BackupFixtureMetadata kEditedBackupMetadata{
    130000000000000000ULL, 133445970560000000ULL, FILE_ATTRIBUTE_HIDDEN};

void AssignBackupFixtureMetadata(const fs::path &path, const BackupFixtureMetadata &metadata) {
  const auto fileTime = [](ULONGLONG ticks) {
    return FILETIME{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
  };
  const FILETIME created = fileTime(metadata.created), modified = fileTime(metadata.modified);
  {
    const FileHandle file(CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (file.value == INVALID_HANDLE_VALUE || !SetFileTime(file.value, &created, nullptr, &modified))
      throw std::runtime_error("Cannot assign private backup fixture timestamps");
  }
  if (!SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_ARCHIVE | metadata.attributes))
    throw std::runtime_error("Cannot assign private backup fixture attributes");
}

BackupFixtureMetadata ReadBackupFixtureMetadata(const fs::path &path) {
  const auto ticks = [](const FILETIME &value) {
    return (static_cast<ULONGLONG>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
  };
  const FileHandle file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (file.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.value, &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
    throw std::runtime_error("Cannot read metadata from the private regular backup fixture file");
  return {ticks(info.ftCreationTime), ticks(info.ftLastWriteTime), info.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN};
}

struct BackupMetadataEdit {
  fs::path first;
  fs::path second;
  fs::path edited;
};

void EditBackupSiblingDuringScan(void *user, const _7zProgressInfo &info) {
  auto &edit = *static_cast<BackupMetadataEdit *>(user);
  if (info.phase != _7zProgressPhase::Scanning || !info.currentItem || !edit.edited.empty())
    return;
  const auto item = fs::path(info.currentItem).filename();
  if (item == edit.first.filename()) edit.edited = edit.second;
  if (item == edit.second.filename()) edit.edited = edit.first;
  if (edit.edited.empty()) return;
  AssignBackupFixtureMetadata(edit.edited, kEditedBackupMetadata);
}

void BackupMetadataRound(const fs::path &fixture, bool before) {
  const auto sites = fixture / L"src/Sites";
  const auto archive = fixture / L"out/notes.7z";
  BackupMetadataEdit edit{sites / L"Only/A-note.ctn", sites / L"Only/Z-note.ctn", {}};
  for (const auto &note : {edit.first, edit.second}) {
    Write(note, "");
    AssignBackupFixtureMetadata(note, kOriginalBackupMetadata);
  }
  fs::create_directories(archive.parent_path());
  Check(ReadBackupFixtureMetadata(edit.first) == kOriginalBackupMetadata &&
        ReadBackupFixtureMetadata(edit.second) == kOriginalBackupMetadata,
        "The two ordinary empty source files start with known timestamps and visible attributes");
  Check(BackUp(sites, archive).ok() && CrcValid(archive),
        "The initial archive is valid before the coordinated metadata update");
  const auto previousId = Identity(archive);
  const auto firstId = Identity(edit.first), secondId = Identity(edit.second);
  CtBackup::BackupOptions options;
  options.detailedProgress = EditBackupSiblingDuringScan;
  options.detailedProgressUser = &edit;
  const auto refreshed = BackUp(sites, archive, options);
  Check(!edit.edited.empty() && Read(edit.edited).empty() &&
        ReadBackupFixtureMetadata(edit.edited) == kEditedBackupMetadata,
        "An ordinary metadata update finishes on the later empty sibling during the native scan");
  Check(refreshed.ok() && CrcValid(archive) && HasOnlyArchive(archive) &&
        Identity(archive) != previousId,
        "The refreshed CRC-valid backup replaces the previous archive without leaving temporary files");
  const auto archiveId = Identity(archive);
  const auto archiveBytes = Read(archive);
  CtBackup::StagedRestore staged;
  const auto stable = edit.edited == edit.first ? edit.second : edit.first;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"stage", nullptr, nullptr, staged).ok() &&
        staged.profileCount == 1, "The refreshed archive stages through the real SDK");
  const auto stagedEdit = staged.payloadDir / edit.edited.lexically_relative(sites);
  const auto stagedStable = staged.payloadDir / stable.lexically_relative(sites);
  Check(Read(stagedEdit).empty() && Read(stagedStable).empty(),
        "Both files stage as exact zero-byte regular files");
  Check(ReadBackupFixtureMetadata(stagedEdit) == (before ? kOriginalBackupMetadata : kEditedBackupMetadata),
        before ? "Baseline restores stale native enumeration metadata on the updated empty file"
               : "The updated empty file restores the exact metadata from the bound source");
  Check(ReadBackupFixtureMetadata(stagedStable) == kOriginalBackupMetadata,
        "The unchanged sibling retains its original timestamps and visible attributes");
  Check(Identity(edit.first) == firstId && Identity(edit.second) == secondId &&
        ReadBackupFixtureMetadata(edit.edited) == kEditedBackupMetadata &&
        ReadBackupFixtureMetadata(stable) == kOriginalBackupMetadata &&
        Identity(archive) == archiveId && Read(archive) == archiveBytes,
        "Staging preserves both exact source objects and the refreshed archive");
  std::cout << "BACKUP_METADATA_CASE=" << (before ? "stale-enumeration-observed" : "bound-metadata-preserved") << '\n';
}

void BackupEmptyClientsRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto archive = fixture / L"out/empty-clients.7z";
  const auto first = sites / L"Blank First", second = sites / L"Blank Second";
  const auto excluded = sites / L"Existing/note.ctn";
  const std::string notebook = "unselected client with actual notebook bytes";
  fs::create_directories(first);
  fs::create_directory(second);
  fs::create_directories(archive.parent_path());
  Write(excluded, notebook);
  const auto firstId = Identity(first), secondId = Identity(second), excludedId = Identity(excluded);
  CtBackup::BackupOptions options;
  options.selectedClientNames = std::vector<std::wstring>{L"Blank Second", L"Blank First"};
  Check(BackUp(sites, archive, options).ok() && CrcValid(archive) && HasOnlyArchive(archive),
        "A selection containing only empty client directories publishes a valid archive");
  const auto archiveId = Identity(archive);
  const auto archiveBytes = Read(archive);
  const auto data = fixture / L"data";
  const auto staging = data / L"stage";
  const auto live = data / L"Sites", recovery = data / L"recovery";
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, staging, nullptr, nullptr, staged).ok() &&
        staged.profileCount == 2, "Staging finds exactly the two selected empty clients");
  Check(fs::is_empty(staged.payloadDir / first.filename()) &&
        fs::is_empty(staged.payloadDir / second.filename()) &&
        !fs::exists(staged.payloadDir / L"Existing"),
        "The staged directories are both empty and the populated unselected client is absent");
  bool previousInRecovery = true;
  Check(CtBackup::CommitStagedRestore(staged, live, recovery, nullptr, nullptr, &previousInRecovery).ok() &&
        !previousInRecovery && !fs::exists(recovery) && !fs::exists(staging),
        "Restore installs the directory-only selection into an empty data root");
  Check(fs::is_empty(live / first.filename()) && fs::is_empty(live / second.filename()) &&
        !fs::exists(live / L"Existing"), "The live collection contains exactly the selected empty clients");
  Check(Identity(first) == firstId && Identity(second) == secondId && Identity(excluded) == excludedId &&
        fs::is_empty(first) && fs::is_empty(second) && Read(excluded) == notebook &&
        Identity(archive) == archiveId && Read(archive) == archiveBytes,
        "The backup and restore preserve the original client objects and the exact archive");
  std::cout << "BACKUP_EMPTY_CLIENTS_CASE=selected-directories-only\n";
}
