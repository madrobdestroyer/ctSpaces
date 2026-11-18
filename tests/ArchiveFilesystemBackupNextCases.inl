void BackupCaseRenamedClientRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto originalName = sites / L"Client Q";
  const auto currentName = sites / L"CLIENT q";
  const auto interim = sites / L"rename-intermediate";
  const auto originalArchive = fixture / L"first/prior.7z";
  const auto selectedArchive = fixture / L"second/selected.7z";
  const auto excluded = sites / L"Other/note.ctn";
  const std::string note = "notebook carried through a case-only client rename";
  const std::string other = "unselected neighboring notebook";
  Write(originalName / L"note.ctn", note);
  Write(excluded, other);
  fs::create_directories(originalArchive.parent_path());
  fs::create_directories(selectedArchive.parent_path());
  const auto clientId = Identity(originalName), noteId = Identity(originalName / L"note.ctn");
  const auto excludedId = Identity(excluded);
  Check(BackUp(sites, originalArchive).ok() && CrcValid(originalArchive),
        "The prior archive verifies with the original client spelling");
  const auto originalId = Identity(originalArchive);
  const auto originalBytes = Read(originalArchive);
  fs::rename(originalName, interim);
  fs::rename(interim, currentName);
  const auto exactSpelling = [](const fs::path &parent, const fs::path &spelling) {
    for (const auto &entry : fs::directory_iterator(parent))
      if (entry.path().filename() == spelling) return true;
    return false;
  };
  Check(exactSpelling(sites, currentName.filename()) && !exactSpelling(sites, originalName.filename()) &&
        Identity(currentName) == clientId && Identity(currentName / L"note.ctn") == noteId,
        "The renamed native directory has the new exact casing and retains the same objects");
  CtBackup::BackupOptions options;
  options.selectedClientNames = std::vector<std::wstring>{originalName.filename().native()};
  Check(BackUp(sites, selectedArchive, options).ok() && CrcValid(selectedArchive) && HasOnlyArchive(selectedArchive),
        "Backup resolves the older case spelling to the currently selected native directory");
  CtBackup::StagedRestore prior, selected;
  Check(CtBackup::StageValidated7zRestore(originalArchive, fixture / L"stage-prior", nullptr, nullptr, prior).ok() &&
        prior.profileCount == 2 && exactSpelling(prior.payloadDir, originalName.filename()) &&
        Read(prior.payloadDir / originalName.filename() / L"note.ctn") == note,
        "The independent old archive retains its original client spelling and note");
  Check(CtBackup::StageValidated7zRestore(selectedArchive, fixture / L"stage-selected", nullptr, nullptr, selected).ok() &&
        selected.profileCount == 1 && exactSpelling(selected.payloadDir, currentName.filename()) &&
        !exactSpelling(selected.payloadDir, originalName.filename()),
        "The new selected archive stages the current on-disk spelling exactly");
  Check(Read(selected.payloadDir / currentName.filename() / L"note.ctn") == note &&
        !fs::exists(selected.payloadDir / L"Other"), "The case-only rename does not broaden the selected archive");
  Check(Identity(currentName) == clientId && Identity(currentName / L"note.ctn") == noteId &&
        Read(currentName / L"note.ctn") == note && Identity(excluded) == excludedId && Read(excluded) == other &&
        Identity(originalArchive) == originalId && Read(originalArchive) == originalBytes,
        "Both source clients and the independent old archive retain their exact data");
  std::cout << "BACKUP_CASE_RENAMED_CLIENT_CASE=exact-new-directory-spelling\n";
}

void BackupLiteralPathsRound(const fs::path &fixture) {
  const auto sites = fixture / L"Owner's [\u00E9quipe]/Sites";
  const auto first = fs::path(L"Client [A]/O'Brien's note.ctn");
  const auto second = fs::path(L"Client [B]/\u6771\u4EAC notes/note.ctn");
  const auto archive = fixture / L"Exports [\u6771\u4EAC]/O'Brien [nightly].7z";
  const std::string original("before\0first", 12);
  const std::string revised("after\0first", 11);
  const std::string other("second\0client", 13);
  Write(sites / first, original);
  Write(sites / second, other);
  fs::create_directories(archive.parent_path());
  const auto firstId = Identity(sites / first), secondId = Identity(sites / second);
  Check(BackUp(sites, archive).ok() && CrcValid(archive) && HasOnlyArchive(archive),
        "Backup publishes to an ordinary destination with literal brackets, an apostrophe and Unicode");
  const auto previousId = Identity(archive);
  const auto previousBytes = Read(archive);
  CtBackup::StagedRestore prior;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"Stage [prior]", nullptr, nullptr, prior).ok() &&
        prior.profileCount == 2, "The verified backup stages to a literal bracketed directory");
  Check(Read(prior.payloadDir / first) == original && Read(prior.payloadDir / second) == other,
        "Both independent binary notebooks retain their exact paths and bytes");
  Write(sites / first, revised);
  Check(BackUp(sites, archive).ok() && CrcValid(archive) && HasOnlyArchive(archive) &&
        Identity(archive) != previousId && Read(archive) != previousBytes,
        "A replacement uses the same literal destination without leaving temporary files");
  CtBackup::StagedRestore latest;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"Stage [latest]", nullptr, nullptr, latest).ok() &&
        latest.profileCount == 2 && Read(latest.payloadDir / first) == revised && Read(latest.payloadDir / second) == other,
        "The updated archive keeps exact client membership and the changed notebook bytes");
  Check(Read(prior.payloadDir / first) == original && Read(prior.payloadDir / second) == other,
        "Staging the replacement leaves the earlier staged notebook objects intact");
  Check(Identity(sites / first) == firstId && Identity(sites / second) == secondId &&
        Read(sites / first) == revised && Read(sites / second) == other,
        "The source notebook objects retain their exact bytes");
  std::cout << "BACKUP_LITERAL_PATHS_CASE=literal-brackets-apostrophe-and-unicode\n";
}

void BackupExtensionRefusalRound(const fs::path &fixture) {
  const auto sites = fixture / L"src/Sites";
  const auto note = sites / L"Client/note.ctn";
  const auto legacy = fixture / L"out/previous.zip";
  const auto archive = fixture / L"out/previous.7z";
  const std::string noteBytes = "current notebook for the new .7z backup";
  const std::string emptyZip("PK\x05\x06" "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 22);
  Write(note, noteBytes);
  Write(legacy, emptyZip);
  const auto legacyId = Identity(legacy), noteId = Identity(note);
  const auto refused = BackUp(sites, legacy);
  Check(refused.code == E_INVALIDARG && refused.details.find(L".7z extension") != std::wstring::npos,
        "An existing ZIP pathname is refused with the current backup format guidance");
  Check(Identity(legacy) == legacyId && Read(legacy) == emptyZip &&
        HasOnlyArchive(legacy) && !fs::exists(archive),
        "The refusal preserves the exact independent ZIP and creates no archive or sibling files");
  Check(BackUp(sites, archive).ok() && CrcValid(archive),
        "Selecting the .7z suffix publishes a verified backup beside the unrelated ZIP");
  const auto archiveId = Identity(archive);
  const auto archiveBytes = Read(archive);
  Check(std::distance(fs::directory_iterator(archive.parent_path()), fs::directory_iterator{}) == 2 &&
        Identity(legacy) == legacyId && Read(legacy) == emptyZip,
        "The destination contains only the untouched ZIP and new backup");
  CtBackup::StagedRestore staged;
  Check(CtBackup::StageValidated7zRestore(archive, fixture / L"stage", nullptr, nullptr, staged).ok() &&
        staged.profileCount == 1 && Read(staged.payloadDir / L"Client/note.ctn") == noteBytes,
        "The new backup stages the correct client notebook");
  Check(Identity(note) == noteId && Read(note) == noteBytes &&
        Identity(archive) == archiveId && Read(archive) == archiveBytes,
        "The source and finished archive remain exact after staging");
  std::cout << "BACKUP_EXTENSION_REFUSAL_CASE=unrelated-zip-preservation-and-retry\n";
}
