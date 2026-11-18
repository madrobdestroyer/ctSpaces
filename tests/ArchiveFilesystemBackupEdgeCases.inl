void BackupAliasRound(const fs::path &fixture, bool before) {
  const auto sites = fixture / L"src/Sites";
  const auto alias = sites / L".";
  const auto note = fs::path(L"Alpha/note.ctn");
  const auto archive = fixture / L"out/previous.7z";
  const std::string current = "new notebook through the same Sites directory";
  Write(sites / note, "previous ordinary notebook");
  fs::create_directories(archive.parent_path());
  Check(BackUp(sites, archive).ok() && CrcValid(archive),
        "Canonical Sites produces a valid previous destination");
  const auto oldArchiveId = Identity(archive);
  const auto oldArchiveBytes = Read(archive);
  const auto sourceId = Identity(sites / note);
  Write(sites / note, current);

  const DWORD aliasAttributes = GetFileAttributesW(alias.c_str());
  Check(aliasAttributes != INVALID_FILE_ATTRIBUTES &&
        (aliasAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(aliasAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        Identity(alias) == Identity(sites),
        "The dot component still names the same ordinary native directory");
  const auto aliased = BackUp(alias, archive);
  std::cout << "BACKUP_SITES_ALIAS hresult=0x" << std::hex
            << static_cast<unsigned long>(aliased.code) << std::dec
            << " published=" << aliased.ok() << '\n';
  Check(aliased.ok() != before,
        before ? "The baseline refuses the ordinary spelling of Sites" :
                 "The ordinary spelling publishes a validated Sites backup");
  if (before) {
    Check(Identity(archive) == oldArchiveId && Read(archive) == oldArchiveBytes &&
          CrcValid(archive) && HasOnlyArchive(archive),
          "The alias refusal preserves the exact prior backup and leaves no temporary files");
    Check(BackUp(sites, archive).ok(),
          "The same source and destination succeed with the canonical spelling");
  } else {
    Check(Identity(archive) != oldArchiveId && Read(archive) != oldArchiveBytes,
          "The successful alias replaces the previous destination with new bytes");
    Check(HasOnlyArchive(archive), "Alias publication leaves only the final archive");
  }
  Check(Identity(archive) != oldArchiveId && Read(archive) != oldArchiveBytes &&
        CrcValid(archive) && HasOnlyArchive(archive),
        "The successful archive is a new CRC-valid object with no temporary files");
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"stage", nullptr, nullptr, staged).ok() &&
        staged.profileCount == 1 && Read(staged.payloadDir / note) == current,
        "The successful archive has a valid Sites wrapper and stages the exact notebook");
  Check(Identity(sites / note) == sourceId && Read(sites / note) == current,
        "Backup and staging preserve the current source object");
  std::cout << "BACKUP_SITES_ALIAS_ROUND_TRIP=1\n";
}

void BackupEdgeRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto selected = fs::path(L"\u00C9lan/note.ctn");
  const auto excluded = fs::path(L"Other/note.ctn");
  const auto outputBase = fixture / L"out";
  constexpr size_t directoryLimit = CtBackup::Detail::kLegacyMaximumDirectoryPathCharacters;
  constexpr size_t fileLimit = CtBackup::Detail::kLegacyMaximumFilePathCharacters;
  const auto parentLength = outputBase.native().size();
  if (parentLength + 1 >= directoryLimit)
    throw std::runtime_error("Private fixture path leaves no boundary directory component");
  const auto parent = outputBase / std::wstring(directoryLimit - parentLength - 1, L'x');
  const auto archive = parent / L"target00.7z";
  const auto overLimit = parent / L"target000.7z";
  Check(parent.native().size() == directoryLimit && archive.native().size() == fileLimit &&
        overLimit.native().size() == fileLimit + 1,
        "Fixture reaches the exact ordinary Windows backup directory and file path boundaries");
  fs::create_directories(parent);

  Write(sites / selected, "previous boundary notebook");
  Write(sites / excluded, "other client's independent notebook");
  CtBackup::BackupOptions selection;
  selection.selectedClientNames = std::vector<std::wstring>{L"\u00E9LAN"};
  Check(BackUp(sites, archive, selection).ok() && CrcValid(archive) && HasOnlyArchive(archive),
        "A case-insensitive Unicode selection publishes at the exact ordinary path limit");
  const auto oldArchiveId = Identity(archive);
  const auto oldArchiveBytes = Read(archive);
  const auto sourceId = Identity(sites / selected);
  const auto excludedId = Identity(sites / excluded);
  const std::string current = "updated boundary notebook";
  Write(sites / selected, current);

  const auto refused = BackUp(sites, overLimit, selection);
  std::cout << "BACKUP_DESTINATION_260 hresult=0x" << std::hex
            << static_cast<unsigned long>(refused.code) << std::dec << '\n';
  Check(refused.code == HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE) ||
        refused.code == E_INVALIDARG,
        "A one-character-over-limit backup destination is refused as an invalid path");
  Check(Identity(archive) == oldArchiveId && Read(archive) == oldArchiveBytes &&
        CrcValid(archive) && HasOnlyArchive(archive),
        "Path refusal leaves the exact previous archive and creates no other output");
  Check(BackUp(sites, archive, selection).ok() && CrcValid(archive),
        "Retry replaces the destination at the exact ordinary path limit");
  Check(Identity(archive) != oldArchiveId && Read(archive) != oldArchiveBytes &&
        HasOnlyArchive(archive),
        "Native replacement publishes the new bytes without temporary siblings");
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"stage", nullptr, nullptr, staged).ok() &&
        staged.profileCount == 1 && Read(staged.payloadDir / selected) == current &&
        !fs::exists(staged.payloadDir / L"Other"),
        "The boundary archive stages the exact selected notebook with the on-disk Unicode name");
  Check(Identity(sites / selected) == sourceId && Read(sites / selected) == current &&
        Identity(sites / excluded) == excludedId &&
        Read(sites / excluded) == "other client's independent notebook",
        "Refusal, replacement, and staging preserve both exact source clients");
  std::cout << "BACKUP_EDGE_CASES=destination-259-replace,destination-260-refusal,unicode-case-selection\n";
}
