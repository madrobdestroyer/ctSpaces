static void CheckArchiveRoundResult(const CtBackup::Result &result, const char *action) {
  if (!result.ok())
    std::wcerr << L"Archive round error 0x" << std::hex
               << static_cast<unsigned long>(result.code) << std::dec << L": "
               << result.details << L'\n';
  Check(result.ok(), action);
}

static void SelectArchiveRoundClients(const std::vector<std::wstring> &names) {
  notesDialogObserver = [names](LPCWSTR resource, DLGPROC procedure, LPARAM data) -> INT_PTR {
    Check(resource == MAKEINTRESOURCEW(IDD_BACKUP_CLIENTS) && procedure == BackupClientsDlgProc,
          "Archive round reaches the production client selector");
    auto &selection = *reinterpret_cast<BackupClientSelection *>(data);
    for (auto &client : selection.clients)
      client.checked = std::ranges::find(names, client.name) != names.end();
    auto chosen = BackupSelectedNames(selection);
    auto expected = names;
    std::ranges::sort(chosen);
    std::ranges::sort(expected);
    Check(chosen == expected, "The picker confirms exactly the selected clients");
    selection.accepted = true;
    return IDOK;
  };
}

static void PublishArchiveRound(const fs::path &archive, const std::vector<std::wstring> &names,
                                unsigned probesPerClient = 2) {
  SelectArchiveRoundClients(names);
  backupProbed.clear();
  files.clear();
  files.push_back(archive.wstring());
  lastNotice.clear();
  GuiProfExportAll();
  notesDialogObserver = {};
  Check(files.empty() && fs::is_regular_file(archive) &&
            lastNotice.find(L"Backup complete and verified.") != std::wstring::npos,
        "The ordinary launcher publishes and reports the verified backup");
  const auto probeMatches = std::ranges::all_of(names, [&](const auto &name) {
    return std::ranges::count(backupProbed, name) == probesPerClient;
  });
  Check(backupProbed.size() == names.size() * probesPerClient && probeMatches &&
            g_bUiEnabled && !g_bClientNotesBusy.load() && !g_bArchiveTaskInProgress,
        "Publication probes only selected clients and releases the launcher");
}

static std::string WriteArchiveRoundNote(const fs::path &client, const wchar_t *title,
                                         const char *rtf) {
  client_notes::NotePage note;
  note.name = title;
  note.id = 1;
  note.active = true;
  note.rich = true;
  note.rtf = rtf;
  Check(client_notes::WriteNotebook(client, {note}, client_notes::ReadNotebook(client)),
        "The ordinary Notes writer creates the private notebook");
  return FixtureBytes(client / client_notes::kNotebookFileName);
}

static std::vector<std::wstring> ArchiveRoundNames(const fs::path &root) {
  std::vector<std::wstring> names;
  for (const auto &entry : fs::directory_iterator(root))
    names.push_back(entry.path().filename().wstring());
  std::ranges::sort(names);
  return names;
}

static void CheckArchiveRoundTree(const fs::path &source, const fs::path &copy,
                                  std::vector<std::wstring> names) {
  std::ranges::sort(names);
  Check(ArchiveRoundNames(copy) == names, "Archive payload contains the exact client selection");
  for (const auto &name : names) {
    std::vector<fs::path> originalItems, copiedItems;
    const auto from = source / name, to = copy / name;
    for (const auto &entry : fs::recursive_directory_iterator(from))
      originalItems.push_back(entry.path().lexically_relative(from));
    for (const auto &entry : fs::recursive_directory_iterator(to))
      copiedItems.push_back(entry.path().lexically_relative(to));
    std::ranges::sort(originalItems);
    std::ranges::sort(copiedItems);
    Check(originalItems == copiedItems, "Restored client retains the exact ordinary file and directory layout");
    for (const auto &item : originalItems) {
      const bool directory = fs::is_directory(from / item);
      Check(directory ? fs::is_directory(to / item) :
                  fs::is_regular_file(to / item) && FixtureBytes(to / item) == FixtureBytes(from / item),
            "Restored data matches the corresponding private source item");
    }
  }
}

static bool ConfirmArchiveRoundCommit(void *context, std::wstring &details) {
  auto &calls = *static_cast<unsigned *>(context);
  ++calls;
  if (calls != 1) return true;
  details = L"Ordinary fixture asked to defer restore at final confirmation.";
  return false;
}

static void ArchiveRoundRestore(const fs::path &archive, const fs::path &artifacts,
                                 const std::vector<std::wstring> &names, unsigned round) {
  const auto originalArchive = FixtureBytes(archive);
  const auto source = g_sDataDir / L"Sites";
  const auto stage = artifacts / L"s";
  const auto liveParent = artifacts / L"d", live = liveParent / L"Sites";
  const auto recoveryParent = round == 9 ? artifacts / L"history" : liveParent;
  const auto recovery = recoveryParent / L"R";
  const std::string previous = "Previous private Sites data";
  fs::create_directories(liveParent);
  fs::create_directories(recoveryParent);
  if (round != 3) Put(live / L"Previous/old.txt", previous);
  if (round == 9) Put(recoveryParent / L"retained.txt", "Previous recovery is independent");

  CtBackup::StagedRestore staged;
  if (round == 5) {
    const auto reader = CreateFileW(archive.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(reader != INVALID_HANDLE_VALUE, "The selected verified archive is temporarily open exclusively");
    const auto refusal = CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged);
    CloseHandle(reader);
    Check(refusal.code == HRESULT_FROM_WIN32(ERROR_SHARING_VIOLATION) && !staged.validation &&
              !fs::exists(stage) && FixtureBytes(live / L"Previous/old.txt") == previous,
          "Restore refuses the in-use archive before changing existing data, then can retry");
  }
  CheckArchiveRoundResult(CtBackup::StageValidated7zRestore(archive, stage, nullptr, nullptr, staged),
                          "The published archive passes actual CRC validation and staging");
  Check(staged.profileCount == names.size() && staged.validation,
        "Staging seals the exact expected client count");
  CheckArchiveRoundTree(source, staged.payloadDir, names);

  bool previousKept = false;
  unsigned confirmations = 0;
  if (round == 6) {
    const auto occupied = liveParent / L"Busy";
    Put(occupied / L"retained.txt", "Existing recovery data");
    const auto refusal = CtBackup::CommitStagedRestore(staged, live, occupied, nullptr, nullptr, &previousKept);
    Check(refusal.code == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) && !previousKept &&
              FixtureBytes(occupied / L"retained.txt") == "Existing recovery data" &&
              FixtureBytes(live / L"Previous/old.txt") == previous && fs::exists(stage),
          "An occupied recovery path preserves existing recovery, live data and the staged backup");
  }
  const auto guard = round == 7 ? ConfirmArchiveRoundCommit : nullptr;
  if (guard) {
    const auto refusal = CtBackup::CommitStagedRestore(staged, live, recovery, guard, &confirmations, &previousKept);
    Check(refusal.code == HRESULT_FROM_WIN32(ERROR_BUSY) && !previousKept && confirmations == 1 &&
              !fs::exists(recovery) && FixtureBytes(live / L"Previous/old.txt") == previous && fs::exists(stage),
          "A final confirmation refusal preserves live and staged bytes for a later retry");
  }
  CheckArchiveRoundResult(CtBackup::CommitStagedRestore(
      staged, live, recovery, guard, guard ? &confirmations : nullptr, &previousKept),
      "The verified payload becomes the live private Sites collection");
  CheckArchiveRoundTree(source, live, names);
  Check(previousKept == (round != 3) &&
            (round == 3 ? !fs::exists(recovery) : FixtureBytes(recovery / L"Previous/old.txt") == previous),
        "Restore retains precisely the previous live collection when one exists");
  Check(!fs::exists(staged.payloadDir) && FixtureBytes(archive) == originalArchive,
        "Restore consumes its staged payload and leaves the input archive byte-exact");
  if (round == 7) Check(confirmations == 2, "The final confirmation is retried once");
  if (round == 9)
    Check(FixtureBytes(recoveryParent / L"retained.txt") == "Previous recovery is independent",
          "Using a separate recovery parent preserves unrelated recovery data");
}

static void ArchiveRoundDeleteSharedReader(const fs::path &artifacts, bool before, bool focused) {
  const std::wstring name = L"Open reader";
  const auto archive = artifacts / L"existing.7z";
  Fixture(name);
  HWND notes = ::Open(name);
  auto &state = State(notes);
  Type(notes, L"Previous notebook");
  Save(notes);
  PublishArchiveRound(archive, {name}, 3);
  const auto originalArchive = FixtureBytes(archive);
  const auto saved = client_notes::ReadNotebook(state.clientRoot).notebook_snapshot;
  Type(notes, L" and pending work");
  KillTimer(notes, 1);
  KillTimer(notes, 2);
  NotesShowPanel(notes, &state, true);
  const auto panel = state.panel;
  const auto editor = Edit(notes);
  Check(IsWindow(panel) && SendMessageW(editor, EM_CANUNDO, 0, 0),
        "The selected client has an open Details panel and a pending edit with Undo");

  BackupDestinationRestriction restriction{archive};
  unsigned picks = 0;
  notesFilePickerObserver = [&](OPENFILENAMEW *picker) -> BOOL {
    ++picks;
    restriction.reader = CreateFileW(archive.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(restriction.reader != INVALID_HANDLE_VALUE,
          "A private archive reader opens with all sharing flags during destination confirmation");
    return wcscpy_s(picker->lpstrFile, picker->nMaxFile, archive.c_str()) == 0;
  };
  SelectArchiveRoundClients({name});
  backupProbed.clear();
  lastNotice.clear();
  GuiProfExportAll();
  notesFilePickerObserver = {};
  notesDialogObserver = {};
  restriction.Release();
  const auto refused = lastNotice;
  const bool panelClosed = !IsWindow(panel) || state.panel != panel;
  const bool draftSaved = !client_notes::SameSnapshot(
      saved, client_notes::ReadNotebook(state.clientRoot).notebook_snapshot);
  const bool early = backupProbed.size() == 1;
  std::cout << "ARCHIVE_DELETE_SHARED_READER early=" << early << " panelClosed=" << panelClosed
            << " draftSaved=" << draftSaved << '\n';
  Check(picks == 1 && early == !before && panelClosed == before && draftSaved == before &&
            refused.find(before ? L"could not replace" : L"Close programs using") != std::wstring::npos &&
            FixtureBytes(archive) == originalArchive && ArchiveRoundNames(artifacts) == std::vector<std::wstring>{L"existing.7z"},
        "A reader that prevents replacement leaves the archive exact and is reported at the expected boundary");
  Check(g_hNotesWindow == notes && IsWindowEnabled(notes) &&
            NotesWindowText(editor) == L"Previous notebook and pending work" &&
            SendMessageW(editor, EM_CANUNDO, 0, 0),
        "The refusal preserves the exact editor text and its Undo");
  PublishArchiveRound(archive, {name}, 3);
  Check(FixtureBytes(archive) != originalArchive, "Retry replaces the same archive after the reader closes");
  const auto note = client_notes::ReadNotebook(state.clientRoot);
  Check(note.status == client_notes::ReadStatus::Ok &&
            std::ranges::any_of(note.pages, [](const auto &page) {
              return NotesPlain(page) == L"Previous notebook and pending work";
            }),
        "The accepted retry saves the exact pending note");
  DestroyWindow(notes);
  if (focused) {
    CtBackup::StagedRestore staged;
    CheckArchiveRoundResult(CtBackup::StageValidated7zRestore(
        archive, artifacts / L"s", nullptr, nullptr, staged),
        "The same-path retry passes actual CRC validation and staging");
    Check(staged.profileCount == 1 && staged.validation,
          "The same-path retry seals the selected profile");
    Check(FixtureBytes(staged.payloadDir / name / client_notes::kNotebookFileName) ==
              note.notebook_snapshot.bytes,
          "The same-path retry stages the exact updated notebook");
  } else {
    ArchiveRoundRestore(archive, artifacts, {name}, 10);
  }
}

static void RunArchiveRound(const fs::path &artifacts, unsigned round, bool before, bool focused) {
  ++scenarios;
  constexpr std::array labels{
      "unicode-selected-clients-and-excluded-notes",
      "overwrite-restores-newest-notebook",
      "notes-only-backup-installs-with-no-live-sites",
      "legacy-and-archived-client-selection",
      "locked-archive-refusal-and-retry",
      "occupied-recovery-name-preserves-both-copies",
      "final-confirmation-defers-and-retries-restore",
      "multi-browser-state-and-empty-profile-folders",
      "alternate-recovery-parent-preserves-other-data",
      "delete-shared-reader-defers-backup-until-closed"};
  if (round == 10) {
    ArchiveRoundDeleteSharedReader(artifacts, before, focused);
  } else {
    const std::wstring name = round == 1 ? L"R&D \u6771\u4eac" : L"Alpha";
    const auto sites = g_sDataDir / L"Sites", source = sites / name;
    const auto archive = artifacts / (round == 1 ? L"March copy \u6771\u4eac.7Z" : L"copy.7z");
    std::vector<std::wstring> names{name};
    if (round == 3) {
      fs::create_directories(source);
      Check(CreateV2ClientSkeleton(source) && fs::is_empty(source / L"Browsers"),
            "A Notes-only private client has its normal empty Browsers folder");
      useProductionProfileProbe = true;
    } else {
      Fixture(name);
    }
    if (round == 1 || round == 4) {
      const auto other = round == 1 ? L"Current Client" : L"Legacy Client";
      names.push_back(other);
      if (round == 4) {
        g_archivedClients = {name};
        Put(sites / other / L"Default/Preferences", "{\"legacy\":true}");
      } else {
        Fixture(other);
      }
      Fixture(L"Excluded");
      WriteArchiveRoundNote(sites / L"Excluded", L"Independent", "{\\rtf1\\ansi excluded note}");
      backupActive = L"Excluded";
    }
    if (round == 8) {
      Put(source / L"Browsers/firefox/ctSpaces-browser-v2", "ctSpaces-browser-schema=2\r\nbrowser=firefox\r\n");
      Put(source / L"Browsers/firefox/Profile/prefs.js", "user_pref(\"browser.startup.page\", 3);\n");
      fs::create_directories(source / L"Browsers/firefox/Profile/sessionstore-backups");
      fs::create_directories(source / L"Browsers/edge/Profile/Default/Empty");
    }
    const auto original = WriteArchiveRoundNote(source, L"Private note", "{\\rtf1\\ansi initial text}");
    PublishArchiveRound(archive, names);
    if (round == 2) {
      const auto first = FixtureBytes(archive);
      const auto updated = WriteArchiveRoundNote(source, L"Private note", "{\\rtf1\\ansi changed text}");
      Check(updated != original, "The second backup has a new saved notebook to publish");
      PublishArchiveRound(archive, names);
      Check(FixtureBytes(archive) != first, "Overwriting replaces the original verified archive");
    }
    if (round == 1 || round == 4)
      Check(fs::is_regular_file(sites / L"Excluded" / client_notes::kNotebookFileName),
            "Selecting a subset leaves the excluded client's original notebook alone");
    ArchiveRoundRestore(archive, artifacts, names, round);
  }
  NotesBackgroundAudit("private backup and restore round");
  std::cout << "APP_ARCHIVE_ROUND round=" << round << " case=" << labels.at(round - 1) << '\n';
}
