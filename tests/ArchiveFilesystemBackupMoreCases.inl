void AssignBackupDirectoryMetadata(const fs::path &path, const BackupFixtureMetadata &metadata) {
  const auto fileTime = [](ULONGLONG ticks) {
    return FILETIME{static_cast<DWORD>(ticks), static_cast<DWORD>(ticks >> 32)};
  };
  const FILETIME created = fileTime(metadata.created), modified = fileTime(metadata.modified);
  {
    const FileHandle directory(CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (directory.value == INVALID_HANDLE_VALUE || !SetFileTime(directory.value, &created, nullptr, &modified))
      throw std::runtime_error("Cannot assign timestamps on the private backup fixture directory");
  }
  if (!SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_ARCHIVE | metadata.attributes))
    throw std::runtime_error("Cannot assign attributes on the private backup fixture directory");
}

BackupFixtureMetadata ReadBackupDirectoryMetadata(const fs::path &path) {
  const auto ticks = [](const FILETIME &value) {
    return (static_cast<ULONGLONG>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
  };
  const FileHandle directory(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  BY_HANDLE_FILE_INFORMATION info{};
  if (directory.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(directory.value, &info) ||
      !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
    throw std::runtime_error("Cannot read metadata from the private ordinary backup fixture directory");
  return {ticks(info.ftCreationTime), ticks(info.ftLastWriteTime), info.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN};
}

void EditBackupDirectoryDuringScan(void *user, const _7zProgressInfo &info) {
  auto &edit = *static_cast<BackupMetadataEdit *>(user);
  if (info.phase != _7zProgressPhase::Scanning || !info.currentItem || !edit.edited.empty())
    return;
  const auto item = fs::path(info.currentItem).filename();
  if (item == edit.first.filename()) edit.edited = edit.second;
  if (item == edit.second.filename()) edit.edited = edit.first;
  if (!edit.edited.empty()) AssignBackupDirectoryMetadata(edit.edited, kEditedBackupMetadata);
}

void BackupDirectoryMetadataRound(const fs::path &fixture, bool before) {
  const auto sites = fixture / L"src/Sites";
  const auto archive = fixture / L"out/notes.7z";
  BackupMetadataEdit edit{sites / L"Client/A-folder", sites / L"Client/Z-folder", {}};
  for (const auto &directory : {edit.first, edit.second}) {
    fs::create_directories(directory);
    AssignBackupDirectoryMetadata(directory, kOriginalBackupMetadata);
  }
  fs::create_directories(archive.parent_path());
  Check(ReadBackupDirectoryMetadata(edit.first) == kOriginalBackupMetadata &&
        ReadBackupDirectoryMetadata(edit.second) == kOriginalBackupMetadata,
        "Both source folders begin with verified timestamps and visible attributes");
  Check(BackUp(sites, archive).ok() && CrcValid(archive),
        "The initial directory backup verifies before the coordinated metadata update");
  const auto previousId = Identity(archive);
  const auto firstId = Identity(edit.first), secondId = Identity(edit.second);
  CtBackup::BackupOptions options;
  options.detailedProgress = EditBackupDirectoryDuringScan;
  options.detailedProgressUser = &edit;
  const auto refreshed = BackUp(sites, archive, options);
  Check(!edit.edited.empty() && ReadBackupDirectoryMetadata(edit.edited) == kEditedBackupMetadata,
        "The ordinary metadata change completes on the later sibling during scanning");
  Check(refreshed.ok() && CrcValid(archive) && HasOnlyArchive(archive) && Identity(archive) != previousId,
        "The updated archive publishes and clears its temporary files");
  const auto archiveId = Identity(archive);
  const auto archiveBytes = Read(archive);
  CtBackup::StagedRestore staged;
  const auto stable = edit.edited == edit.first ? edit.second : edit.first;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"stage", nullptr, nullptr, staged).ok() &&
        staged.profileCount == 1, "The updated archive stages through the real SDK");
  const auto stagedEdit = staged.payloadDir / edit.edited.lexically_relative(sites);
  const auto stagedStable = staged.payloadDir / stable.lexically_relative(sites);
  Check(fs::is_directory(stagedEdit) && fs::is_empty(stagedEdit) &&
        fs::is_directory(stagedStable) && fs::is_empty(stagedStable),
        "Both empty source directories stage intact");
  Check(ReadBackupDirectoryMetadata(stagedEdit) == (before ? kOriginalBackupMetadata : kEditedBackupMetadata),
        before ? "Baseline restores stale native enumeration metadata on the updated directory"
               : "Restore preserves the exact metadata from the bound source directory");
  Check(ReadBackupDirectoryMetadata(stagedStable) == kOriginalBackupMetadata,
        "The unchanged sibling retains its exact original metadata");
  Check(Identity(edit.first) == firstId && Identity(edit.second) == secondId &&
        ReadBackupDirectoryMetadata(edit.edited) == kEditedBackupMetadata &&
        ReadBackupDirectoryMetadata(stable) == kOriginalBackupMetadata &&
        Identity(archive) == archiveId && Read(archive) == archiveBytes,
        "Staging preserves both exact source folders and the updated archive");
  std::cout << "BACKUP_DIRECTORY_METADATA_CASE="
            << (before ? "stale-enumeration-observed" : "bound-metadata-preserved") << '\n';
}

void ObserveBackupPublishing(void *user, const _7zProgressInfo &info) {
  if (info.phase == _7zProgressPhase::Publishing) *static_cast<bool *>(user) = true;
}

void BackupFolderDestinationRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto note = sites / L"Client/note.ctn";
  const auto archive = fixture / L"out/copy.7z";
  const auto folderNote = archive / L"keep.txt";
  const auto movedFolder = archive.parent_path() / L"original-folder";
  Write(note, "notebook for the retry");
  Write(folderNote, "independent contents already at the destination");
  const auto sourceId = Identity(note), folderId = Identity(archive), folderNoteId = Identity(folderNote);
  const auto sourceBytes = Read(note), folderBytes = Read(folderNote);
  bool publishing = false;
  CtBackup::BackupOptions options;
  options.detailedProgress = ObserveBackupPublishing;
  options.detailedProgressUser = &publishing;
  const auto refused = BackUp(sites, archive, options);
  Check(!publishing && refused.code == HRESULT_FROM_WIN32(ERROR_DIRECTORY) &&
            refused.details.find(L"already used by a folder") != std::wstring::npos,
        "The occupied folder is reported before backup reaches publication");
  Check(fs::is_directory(archive) && Identity(archive) == folderId &&
        Identity(folderNote) == folderNoteId && Read(folderNote) == folderBytes,
        "The existing destination directory and its exact contents survive the refusal");
  Check(HasOnlyArchive(archive), "The refusal leaves no backup temporary files");
  fs::rename(archive, movedFolder);
  Check(BackUp(sites, archive).ok() && CrcValid(archive),
        "Retry to the same filename publishes once the original directory is moved aside");
  Check(std::distance(fs::directory_iterator(archive.parent_path()), fs::directory_iterator{}) == 2 &&
        Identity(movedFolder) == folderId && Identity(movedFolder / folderNote.filename()) == folderNoteId &&
        Read(movedFolder / folderNote.filename()) == folderBytes,
        "The retry leaves only the valid archive and the exact independent directory");
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"stage", nullptr, nullptr, staged).ok() &&
        staged.profileCount == 1 && Read(staged.payloadDir / L"Client/note.ctn") == sourceBytes,
        "The retried archive stages the exact notebook");
  Check(Identity(note) == sourceId && Read(note) == sourceBytes,
        "Refusal and retry preserve the original source object");
  std::cout << "BACKUP_FOLDER_DESTINATION_CASE=refusal-preservation-retry\n";
}

constexpr std::string_view kGrownBackupBytes = "the same source file gained an ordinary second block";

void GrowBackupSiblingDuringScan(void *user, const _7zProgressInfo &info) {
  auto &edit = *static_cast<BackupMetadataEdit *>(user);
  if (info.phase != _7zProgressPhase::Scanning || !info.currentItem || !edit.edited.empty())
    return;
  const auto item = fs::path(info.currentItem).filename();
  if (item == edit.first.filename()) edit.edited = edit.second;
  if (item == edit.second.filename()) edit.edited = edit.first;
  if (!edit.edited.empty()) Write(edit.edited, std::string(kGrownBackupBytes));
}

void BackupSourceGrowthRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto archive = fixture / L"out/copy.7z";
  BackupMetadataEdit edit{sites / L"Client/A-note.ctn", sites / L"Client/Z-note.ctn", {}};
  const std::string initial = "old bytes";
  Write(edit.first, initial);
  Write(edit.second, initial);
  fs::create_directories(archive.parent_path());
  const auto firstId = Identity(edit.first), secondId = Identity(edit.second);
  Check(BackUp(sites, archive).ok() && CrcValid(archive), "A prior stable backup verifies");
  const auto previousId = Identity(archive);
  const auto previousBytes = Read(archive);
  CtBackup::BackupOptions options;
  options.selectedClientNames = std::vector<std::wstring>{L"Client"};
  options.detailedProgress = GrowBackupSiblingDuringScan;
  options.detailedProgressUser = &edit;
  const auto refused = BackUp(sites, archive, options);
  Check(!edit.edited.empty() && Read(edit.edited) == kGrownBackupBytes,
        "The later sibling grows while the selected client is being enumerated");
  Check(refused.code == HRESULT_FROM_WIN32(ERROR_FILE_INVALID),
        "A source size change between native enumeration and binding refuses the backup");
  Check(Identity(archive) == previousId && Read(archive) == previousBytes &&
        CrcValid(archive) && HasOnlyArchive(archive),
        "The refusal preserves the exact prior archive without leaving temporary files");
  CtBackup::StagedRestore prior;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"prior", nullptr, nullptr, prior).ok() &&
        Read(prior.payloadDir / L"Client/A-note.ctn") == initial &&
        Read(prior.payloadDir / L"Client/Z-note.ctn") == initial,
        "The preserved archive still stages both original files");
  options.detailedProgress = nullptr;
  options.detailedProgressUser = nullptr;
  Check(BackUp(sites, archive, options).ok() && CrcValid(archive) &&
        Identity(archive) != previousId && HasOnlyArchive(archive),
        "A stable retry publishes and verifies the selected client");
  CtBackup::StagedRestore retried;
  const auto stable = edit.edited == edit.first ? edit.second : edit.first;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"retry", nullptr, nullptr, retried).ok() &&
        retried.profileCount == 1 && Read(retried.payloadDir / edit.edited.lexically_relative(sites)) == kGrownBackupBytes &&
        Read(retried.payloadDir / stable.lexically_relative(sites)) == initial,
        "The retried archive stages the grown file and the exact unchanged sibling");
  Check(Identity(edit.first) == firstId && Identity(edit.second) == secondId &&
        Read(edit.edited) == kGrownBackupBytes && Read(stable) == initial,
        "The two original objects retain their independent bytes after refusal and retry");
  std::cout << "BACKUP_SOURCE_GROWTH_CASE=refusal-preservation-retry\n";
}

void BackupUnicodeFormsRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto composed = fs::path(L"Caf\u00E9") / L"note.ctn";
  const auto decomposed = fs::path(L"Cafe\u0301") / L"note.ctn";
  const auto full = fixture / L"full/all.7z", selected = fixture / L"selection/one.7z";
  const std::string firstBytes = "notebook in the composed name";
  const std::string secondBytes = "independent notebook in the decomposed name";
  Write(sites / composed, firstBytes);
  Write(sites / decomposed, secondBytes);
  const auto firstId = Identity(sites / composed), secondId = Identity(sites / decomposed);
  Check(firstId != secondId && Read(sites / composed) == firstBytes &&
        Read(sites / decomposed) == secondBytes, "Both Unicode spellings identify independent native files");
  fs::create_directories(full.parent_path());
  fs::create_directories(selected.parent_path());
  Check(BackUp(sites, full).ok() && CrcValid(full) && HasOnlyArchive(full),
        "Full backup publishes both Unicode client directories");
  const auto fullId = Identity(full);
  const auto fullBytes = Read(full);
  CtBackup::StagedRestore all;
  Check(CtBackup::StageValidated7zRestore(full, fixture / L"stage-all", nullptr, nullptr, all).ok() &&
        all.profileCount == 2, "Full restore stages exactly two distinct clients");
  Check(Read(all.payloadDir / composed) == firstBytes && Read(all.payloadDir / decomposed) == secondBytes,
        "Both Unicode directory spellings preserve their own notebook");
  CtBackup::BackupOptions options;
  options.selectedClientNames = std::vector<std::wstring>{decomposed.parent_path().native()};
  Check(BackUp(sites, selected, options).ok() && CrcValid(selected) && HasOnlyArchive(selected),
        "A separate backup publishes the explicitly selected decomposed spelling");
  CtBackup::StagedRestore one;
  Check(CtBackup::StageValidated7zRestore(selected, fixture / L"stage-one", nullptr, nullptr, one).ok() &&
        one.profileCount == 1, "The selected backup stages exactly one client");
  Check(Read(one.payloadDir / decomposed) == secondBytes && !fs::exists(one.payloadDir / composed),
        "The selected notebook stays isolated from the other Unicode spelling");
  Check(Identity(sites / composed) == firstId && Identity(sites / decomposed) == secondId &&
        Read(sites / composed) == firstBytes && Read(sites / decomposed) == secondBytes,
        "Both original clients retain their exact files");
  Check(Identity(full) == fullId && Read(full) == fullBytes,
        "The separate selected backup leaves the full backup exact");
  std::cout << "BACKUP_UNICODE_FORMS_CASE=full-and-selected-distinct\n";
}
