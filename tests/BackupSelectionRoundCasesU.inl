static std::vector<std::wstring> ArchiveRoundStagingDirectories() {
  auto names = ArchiveRoundNames(g_sDataDir);
  std::erase_if(names, [](const auto &name) { return !name.starts_with(L"Restore_Temp_"); });
  return names;
}

static void MovedArchiveStageRecovery(const fs::path &artifacts, bool before) {
  const std::wstring name = L"Transfer";
  Fixture(name);
  const auto sites = g_sDataDir / L"Sites", source = sites / name;
  const auto archive = artifacts / L"transfer.7z", moved = artifacts / L"arrived.7z";
  WriteArchiveRoundNote(source, L"Transfer note", "{\\rtf1\\ansi archived transfer}");
  PublishArchiveRound(archive, {name});
  const auto bytes = FixtureBytes(archive);
  const auto preferences = source / L"Browsers/edge/Profile/Default/Preferences";
  Put(preferences, "private current preferences");
  auto notes = ::Open(name);
  Type(notes, L" with unsaved changes");
  KillTimer(notes, 1);
  KillTimer(notes, 2);
  const auto editor = Edit(notes);
  const auto pending = NotesWindowText(editor);
  const auto started = ArchiveRoundStagingDirectories();
  Check(started.empty(), "The isolated restore begins without a staging directory");

  notesFilePickerObserver = [&](OPENFILENAMEW *picker) -> BOOL {
    fs::rename(archive, moved);
    return wcscpy_s(picker->lpstrFile, picker->nMaxFile, archive.c_str()) == 0;
  };
  answers.push_back(IDYES);
  GuiProfRestoreAll();
  notesFilePickerObserver = {};
  const auto leftover = ArchiveRoundStagingDirectories();
  const bool hadEmptyStage = leftover.size() == 1 && fs::is_empty(g_sDataDir / leftover.front());
  const bool reportedStage = lastNotice.find(L"incomplete staging data was retained") != std::wstring::npos;
  std::cout << "RESTORE_MOVED_ARCHIVE emptyStage=" << hadEmptyStage << " reportedStage=" << reportedStage << '\n';
  Check((before ? hadEmptyStage && reportedStage : leftover.empty() && !reportedStage) &&
            (lastNotice.find(L"does not exist") != std::wstring::npos ||
             lastNotice.find(L"no longer available") != std::wstring::npos),
        "An unavailable selected archive reports the missing input without retaining an empty restore folder after repair");
  Check(g_hNotesWindow == notes && IsWindowEnabled(notes) && NotesWindowText(editor) == pending &&
            SendMessageW(editor, EM_CANUNDO, 0, 0) && FixtureBytes(preferences) == "private current preferences" &&
            FixtureBytes(moved) == bytes && g_bUiEnabled && !g_bClientNotesBusy.load() && !g_bArchiveTaskInProgress,
        "The refusal leaves the existing collection, draft, Undo and moved backup available");

  files.push_back(moved.wstring());
  answers.push_back(IDYES);
  GuiProfRestoreAll();
  Check(files.empty() && answers.empty() && lastNotice.find(L"Restore complete.") != std::wstring::npos &&
            !g_hNotesWindow && FixtureBytes(preferences) == "{}" && FixtureBytes(moved) == bytes &&
            ArchiveRoundStagingDirectories() == leftover,
        "Picking the moved archive restores the original profile and consumes only its own staging folder");
  const auto entries = ArchiveRoundNames(g_sDataDir);
  const auto recovery = std::ranges::find_if(entries, [](const auto &entry) {
    return entry.size() == 5 && entry.front() == L'R';
  });
  Check(recovery != entries.end(), "The retry places the previous collection in one recovery folder");
  const auto recovered = g_sDataDir / *recovery / name;
  const auto recoveredNotes = client_notes::ReadNotebook(recovered);
  const HMODULE richEdit = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  Check(richEdit != nullptr, "The guarded reader can inspect the recovered formatted draft");
  const bool recoveredDraft = recoveredNotes.status == client_notes::ReadStatus::Ok &&
      std::ranges::any_of(recoveredNotes.pages, [&](const auto &page) { return NotesPlain(page) == pending; });
  FreeLibrary(richEdit);
  Check(FixtureBytes(recovered / L"Browsers/edge/Profile/Default/Preferences") == "private current preferences" &&
            recoveredDraft,
        "The recovery contains the prior browser state and the exact saved draft");
}

static void RestoreArchiveRoundU(const fs::path &archive, const fs::path &source,
                                 const fs::path &fixture, const std::vector<std::wstring> &names,
                                 const fs::path &expectedPrevious = {}) {
  const auto stage = fixture / L"s", live = fixture / L"Sites", recovery = fixture / L"R";
  fs::create_directories(fixture);
  if (!expectedPrevious.empty())
    fs::copy(expectedPrevious, live, fs::copy_options::recursive);
  const auto original = FixtureBytes(archive);
  CtBackup::StagedRestore staged;
  CheckArchiveRoundResult(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged),
                          "The published archive validates and extracts through the ordinary restore engine");
  Check(staged.validation && staged.profileCount == names.size(), "The restored manifest confirms the exact client count");
  CheckArchiveRoundTree(source, staged.payloadDir, names);
  if (!expectedPrevious.empty()) CheckArchiveRoundTree(expectedPrevious, live, ArchiveRoundNames(expectedPrevious));
  bool kept = false;
  CheckArchiveRoundResult(CtBackup::CommitStagedRestore(staged, live, recovery, nullptr, nullptr, &kept),
                          "The verified collection commits at its private destination");
  CheckArchiveRoundTree(source, live, names);
  Check(kept == !expectedPrevious.empty() &&
            (expectedPrevious.empty() ? !fs::exists(recovery) : fs::is_directory(recovery)) &&
            !fs::exists(stage) && FixtureBytes(archive) == original,
        "Restore consumes its stage, preserves its archive and reports the actual recovery outcome");
  if (kept) CheckArchiveRoundTree(expectedPrevious, recovery, ArchiveRoundNames(expectedPrevious));
}

static void RestoreSeparateArchiveGenerations(const fs::path &artifacts) {
  const std::wstring name = L"Snapshots";
  const auto sites = g_sDataDir / L"Sites", client = sites / name;
  const auto first = artifacts / L"first.7z", second = artifacts / L"second.7z";
  const auto oldSource = artifacts / L"record";
  Fixture(name);
  WriteArchiveRoundNote(client, L"Snapshot", "{\\rtf1\\ansi first generation}");
  Put(client / L"old-only.txt", "Available in the first backup only");
  PublishArchiveRound(first, {name});
  const auto firstBytes = FixtureBytes(first);
  fs::copy(sites, oldSource, fs::copy_options::recursive);
  Check(fs::remove(client / L"old-only.txt"), "The source legitimately removes an obsolete page-side file");
  Put(client / L"new-only.txt", "Available in the second backup only");
  WriteArchiveRoundNote(client, L"Snapshot", "{\\rtf1\\ansi second generation}");
  PublishArchiveRound(second, {name});
  Check(FixtureBytes(first) == firstBytes && FixtureBytes(second) != firstBytes,
        "Saving a second backup keeps the older separately named copy byte-exact");
  RestoreArchiveRoundU(second, sites, artifacts / L"v2", {name}, oldSource);
  RestoreArchiveRoundU(first, oldSource, artifacts / L"v1", {name}, sites);
}

static void RestoreMultistateNotebook(const fs::path &artifacts) {
  const std::wstring name = L"Ticket notes";
  const auto sites = g_sDataDir / L"Sites", client = sites / name;
  const auto archive = artifacts / L"notes.7z";
  Fixture(name);
  std::vector<client_notes::NotePage> pages(4);
  for (size_t index = 0; index < pages.size(); ++index) {
    auto &page = pages[index];
    page.id = index + 1;
    page.name = L"Ticket " + std::to_wstring(page.id);
    page.rich = true;
    page.rtf = "{\\rtf1\\ansi \\b note status " + std::to_string(page.id) + "}";
  }
  pages[0].active = pages[0].pinned = true;
  pages[0].ticket = L"CT-352";
  pages[0].status = 3;
  pages[0].tags = {L"follow-up", L"customer"};
  client_notes::RememberRevision(pages[0], "{\\rtf1\\ansi previous body}", true);
  pages[1].open = pages[2].open = pages[3].open = false;
  pages[2].archived = true;
  pages[3].deleted = true;
  Check(client_notes::WriteNotebook(client, pages, client_notes::ReadNotebook(client)),
        "Production Notes saves pinned, closed, archived and trashed pages in one notebook");
  PublishArchiveRound(archive, {name});
  const auto restore = artifacts / L"d";
  RestoreArchiveRoundU(archive, sites, restore, {name});
  const auto restored = client_notes::ReadNotebook(restore / L"Sites" / name);
  Check(restored.status == client_notes::ReadStatus::Ok && restored.pages.size() == pages.size() &&
            restored.pages[0].active && restored.pages[0].pinned && restored.pages[0].ticket == L"CT-352" &&
            restored.pages[0].status == 3 && restored.pages[0].tags == pages[0].tags &&
            restored.pages[0].history.size() == 1 && !restored.pages[1].open &&
            restored.pages[2].archived && restored.pages[3].deleted,
        "Opening the restored notebook retains ticket metadata, revision history and all four page states");
}

static void RestoreSharedBrowserNames(const fs::path &artifacts) {
  const std::vector<std::wstring> names{L"East", L"West"};
  const auto sites = g_sDataDir / L"Sites";
  const auto archive = artifacts / L"per-client.7z";
  const auto edge = fs::path(L"Browsers/edge/Profile/Default/Network/Cookies");
  const auto firefox = fs::path(L"Browsers/firefox/Profile/cookies.sqlite");
  for (const auto &name : names) Fixture(name);
  for (const auto &name : names)
    Put(sites / name / L"Browsers/firefox/ctSpaces-browser-v2", "ctSpaces-browser-schema=2\r\nbrowser=firefox\r\n");
  Put(sites / L"East" / edge, "east-edge-private");
  Put(sites / L"West" / edge, "west-edge-private");
  Put(sites / L"East" / firefox, "east-firefox-private");
  Put(sites / L"West" / firefox, "west-firefox-private");
  PublishArchiveRound(archive, names);
  RestoreArchiveRoundU(archive, sites, artifacts / L"d", names);
}

static void ArchiveRoundOrdinaryData(const fs::path &artifacts, unsigned round) {
  const auto sites = g_sDataDir / L"Sites", archive = artifacts / L"snapshot.7z";
  const std::wstring name = round == 9 ? L"Sites" : L"Field work";
  const auto client = sites / name;
  Fixture(name);
  std::vector<std::wstring> names{name};
  fs::path previous;
  switch (round) {
    case 5:
      Put(client / client_notes::kFileName, "Ordinary legacy text note\r\n");
      Put(g_sDataDir / L"config.ini", "[ui]\r\ntheme=dark\r\n");
      Put(g_sDataDir / L"Temp/Default/Preferences", "temporary browser fixture");
      Put(g_sDataDir / L"Default/Default/Preferences", "starter editor fixture");
      break;
    case 6:
      Put(client / L"Browsers/edge/Profile/Default/Local Storage/data.bin",
          std::string("first\0second\xff\x80", 14));
      Put(client / L"Browsers/edge/Profile/Default/Local Storage/empty.bin", "");
      break;
    case 7:
      previous = artifacts / L"old";
      Put(previous / name / L"Browsers/edge/Profile/Default/Preferences", "different live preferences");
      Put(previous / name / L"Previous issue.txt", "retain only in recovery");
      Put(previous / L"Other active client/Other data.txt", "retain an excluded current client in recovery");
      WriteArchiveRoundNote(client, L"Incoming", "{\\rtf1\\ansi replacement note}");
      break;
    case 8: {
      const auto archived = sites / L"Status";
      Fixture(L"Status");
      g_archivedClients = {L"Status"};
      WriteArchiveRoundNote(archived, L"Archive me", "{\\rtf1\\ansi archived client only}");
      names = {L"Status"};
      break;
    }
    case 9:
      Put(client / L"Profile imports/2026/[Draft] customer (final).txt", "Inside the actual client named Sites");
      break;
    case 10:
      Put(client / client_notes::kRichFileName, "{\\rtf1\\ansi prior formatted notes}");
      WriteArchiveRoundNote(client, L"New notes", "{\\rtf1\\ansi current notebook takes priority}");
      break;
  }
  PublishArchiveRound(archive, names);
  const auto restored = artifacts / L"d";
  RestoreArchiveRoundU(archive, sites, restored, names, previous);
  const auto live = restored / L"Sites" / name;
  if (round == 5) {
    const auto notes = client_notes::ReadNotebook(live);
    Check(notes.status == client_notes::ReadStatus::Ok && notes.pages.size() == 1 &&
              !notes.pages.front().rich && notes.pages.front().text.find(L"Ordinary legacy text note") != std::wstring::npos &&
              ArchiveRoundNames(restored) == std::vector<std::wstring>{L"Sites"},
          "The restored legacy text note opens while shared configuration and outside profiles stay outside the backup");
  }
  if (round == 10) {
    const auto notes = client_notes::ReadNotebook(live);
    Check(notes.status == client_notes::ReadStatus::Ok && notes.pages.size() == 1 &&
              notes.pages.front().name == L"New notes" && fs::is_regular_file(live / client_notes::kRichFileName),
          "The restored current notebook takes priority while the earlier rich file remains preserved");
  }
}

static void RunArchiveRoundU(const fs::path &artifacts, unsigned round, bool before) {
  ++scenarios;
  constexpr std::array labels{
      "moved-archive-cleans-empty-stage-and-retries",
      "separate-archive-generations-restore-both-directions",
      "formatted-notebook-page-states-and-revision-history",
      "same-browser-filenames-retain-per-client-data",
      "legacy-text-note-with-shared-settings-excluded",
      "binary-browser-storage-and-zero-length-peer",
      "same-name-client-replacement-with-recovery",
      "archived-client-only-selection-and-restore",
      "client-named-sites-retains-ordinary-nested-files",
      "current-notebook-retains-older-rich-note-file"};
  switch (round) {
    case 1: MovedArchiveStageRecovery(artifacts, before); break;
    case 2: RestoreSeparateArchiveGenerations(artifacts); break;
    case 3: RestoreMultistateNotebook(artifacts); break;
    case 4: RestoreSharedBrowserNames(artifacts); break;
    default: ArchiveRoundOrdinaryData(artifacts, round); break;
  }
  NotesBackgroundAudit("private backup and restore round");
  std::cout << "APP_ARCHIVE_ROUND round=" << round << " case=" << labels.at(round - 1) << '\n';
}
