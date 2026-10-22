// Actual launcher backup path with immediate synthetic file selection. The
// archive worker, save barriers, and safe Notes persistence remain production.
struct NotesBackupEnableEvents {
  unsigned disabled = 0, enabled = 0;
  bool busyWhileFrozen = true;
};
static LRESULT CALLBACK ObserveNotesBackupEnable(HWND window, UINT message,
    WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
  auto &events = *reinterpret_cast<NotesBackupEnableEvents *>(data);
  if (message == WM_ENABLE) {
    if (w) ++events.enabled;
    else { ++events.disabled; events.busyWhileFrozen &= g_bClientNotesBusy.load(); }
  }
  if (message == WM_NCDESTROY)
    RemoveWindowSubclass(window, ObserveNotesBackupEnable, id);
  return DefSubclassProc(window, message, w, l);
}
static void NotesAuditBackupSnapshot() {
  const unsigned start = checks;
  // This scenario enters the real launcher backup handler, whose SetUiState
  // repaints its client selector. Other Notes-only scenarios need no launcher
  // children, so provide the required owned surface only for this scope.
  struct LauncherSurface {
    HWND previous = g_hClientEditSurface;
    HWND owned = nullptr;
    LauncherSurface() {
      if (g_hClientEditSurface && IsWindow(g_hClientEditSurface)) return;
      owned = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Backup client selector",
          WS_CHILD, 0, 0, 100, 24, g_hGui, nullptr, g_hInst, nullptr);
      if (!owned) throw std::runtime_error("Backup launcher selector fixture failed");
      g_hClientEditSurface = owned;
    }
    ~LauncherSurface() {
      if (owned) {
        g_hClientEditSurface = previous;
        DestroyWindow(owned);
      }
    }
  } launcherSurface;
  Check(g_hClientEditSurface && IsWindow(g_hClientEditSurface) &&
            GetParent(g_hClientEditSurface) == g_hGui &&
            !IsWindowVisible(g_hClientEditSurface),
        "Backup owns its required hidden launcher selector surface");
  const std::wstring name = L"Backup Snapshot QA";
  Fixture(name);
  const fs::path clientRoot = g_sDataDir / L"Sites" / name;
  const fs::path output = g_sDataDir.parent_path() /
      (L"notes-backup-regression-" + std::to_wstring(GetCurrentProcessId()) +
       L"-" + std::to_wstring(GetTickCount64()));
  fs::create_directory(output);
  struct Cleanup {
    fs::path output, client;
    decltype(notesDialogObserver) previousDialogObserver=std::move(notesDialogObserver);
    ~Cleanup() {
      notesDialogObserver=std::move(previousDialogObserver);
      beforeFileSelection = {};
      files.clear();
      std::error_code error;
      fs::remove_all(output, error);
      fs::remove_all(client, error);
    }
  } cleanup{output, clientRoot};
  // This persistence regression explicitly keeps the all-client scope. The
  // separate selector suite exercises its native checklist and cancellation.
  notesDialogObserver=[](LPCWSTR resource,DLGPROC procedure,LPARAM data)->INT_PTR {
    if(resource!=MAKEINTRESOURCEW(IDD_BACKUP_CLIENTS) || procedure!=BackupClientsDlgProc || !data)
      throw std::runtime_error("Unexpected modal dialog in backup snapshot test");
    auto &selection=*reinterpret_cast<BackupClientSelection *>(data);
    for(auto &client:selection.clients) client.checked=true;
    selection.accepted=!selection.clients.empty() && !selection.failed;
    return selection.accepted?IDOK:IDCANCEL;
  };
  _7zSetHInstance(g_hInst);
  HWND owner = Open(name);
  auto &state = State(owner);
  if (!HasActiveNote(state)) Command(owner, IDC_NOTES_ADD_TAB);
  Type(owner, L"Saved baseline\r\n");
  Save(owner);
  const uint64_t id = state.pages[state.activePage].id;
  NotesBackupEnableEvents events;
  SetWindowSubclass(owner, ObserveNotesBackupEnable, 893,
                    reinterpret_cast<DWORD_PTR>(&events));
  const auto archivedText = [&](const fs::path &archive) {
    CtBackup::StagedRestore staged;
    const fs::path staging = output / L"staged";
    const auto result = CtBackup::StageValidated7zRestore(
        archive, staging, nullptr, nullptr, staged);
    Check(result.ok(), "Launcher backup passes integrity and restores safely");
    const auto notebook = client_notes::ReadNotebook(staged.payloadDir / name);
    const auto page = std::find_if(notebook.pages.begin(), notebook.pages.end(),
        [&](const auto &p) { return p.id == id; });
    Check(page != notebook.pages.end(), "Backup contains the intended client and note identity");
    const std::wstring text = NotesPlain(*page);
    fs::remove_all(staging);
    return text;
  };
  const auto checkRestoredUi = [&]() {
    Check(g_hNotesWindow == owner && IsWindowEnabled(owner) &&
              !g_bClientNotesBusy.load() && g_bUiEnabled,
          "Backup restores the same Notes window and releases both UI busy states");
  };

  Select(Edit(owner), -1, -1);
  Type(owner, L"Pending edit before autosave\r\n");
  const fs::path immediate = output / L"immediate.7z";
  files.push_back(immediate.wstring());
  GuiProfExportAll();
  Check(fs::is_regular_file(immediate) && files.empty(),
        "Immediate production backup completes before the autosave timer");
  Check(archivedText(immediate).find(L"Pending edit before autosave") != std::wstring::npos,
        "Backup includes edits that were still waiting for autosave");
  Check(events.disabled == 1 && events.enabled == 1 && events.busyWhileFrozen,
        "Notes interaction is frozen and restored around the archive snapshot");
  checkRestoredUi();

  const fs::path duringPicker = output / L"during-picker.7z";
  beforeFileSelection = [&]() {
    Check(g_bClientNotesBusy.load() && !g_bUiEnabled && IsWindowEnabled(owner),
          "Picker keeps Notes available while client-tree actions are blocked");
    GuiProfExportAll();
    Check(files.size() == 1, "Nested backup request does not open another picker");
    Select(Edit(owner), -1, -1);
    Type(owner, L"Edit while file picker is open\r\n");
  };
  files.push_back(duringPicker.wstring());
  GuiProfExportAll();
  beforeFileSelection = {};
  Check(archivedText(duringPicker).find(L"Edit while file picker is open") != std::wstring::npos,
        "Second snapshot barrier includes changes made during file selection");
  checkRestoredUi();

  state.detailsDrafts[id] = {L"0", L"", L"", L"not a valid URL"};
  const fs::path invalid = output / L"invalid-details.7z";
  files.push_back(invalid.wstring());
  GuiProfExportAll();
  Check(!fs::exists(invalid) && files.size() == 1 &&
            lastNotice.find(L"No backup was created") != std::wstring::npos,
        "Invalid Details blocks backup before file selection without losing its draft");
  Check(state.detailsDrafts.contains(id), "Rejected backup preserves the invalid Details draft");
  files.clear(); state.detailsDrafts.clear(); SetNotesStatus(owner, state);
  checkRestoredUi();

  beforeFileSelection = [&]() { state.detailsDrafts[id] = {L"0", L"", L"", L"invalid"}; };
  files.push_back(invalid.wstring());
  GuiProfExportAll(); beforeFileSelection = {};
  Check(!fs::exists(invalid) && files.empty() && state.detailsDrafts.contains(id),
        "Details invalidated during the picker blocks the final snapshot");
  state.detailsDrafts.clear(); SetNotesStatus(owner, state);
  checkRestoredUi();

  HANDLE locked = CreateFileW((clientRoot / client_notes::kNotebookFileName).c_str(),
      GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, nullptr);
  Check(locked != INVALID_HANDLE_VALUE, "Notebook locked against atomic save replacement");
  Select(Edit(owner), -1, -1); Type(owner, L"Unsaved locked-file update\r\n");
  const fs::path failedSave = output / L"failed-save.7z";
  files.push_back(failedSave.wstring());
  GuiProfExportAll(); CloseHandle(locked);
  Check(!fs::exists(failedSave) && files.size() == 1 && state.saveFailed &&
            NotesWindowText(Edit(owner)).find(L"Unsaved locked-file update") != std::wstring::npos,
        "Save failure cancels backup and retains the full editable note");
  files.clear(); checkRestoredUi(); Save(owner);

  const fs::path conflictBackup = output / L"conflict.7z";
  beforeFileSelection = [&]() {
    auto external = state.original.pages;
    auto page = std::find_if(external.begin(), external.end(), [&](const auto &p) { return p.id == id; });
    page->ticket = L"External writer wins";
    Check(client_notes::WriteNotebook(clientRoot, external, state.original),
          "Concurrent writer changes the notebook during file selection");
    Select(Edit(owner), -1, -1); Type(owner, L"Local concurrent draft\r\n");
  };
  files.push_back(conflictBackup.wstring());
  GuiProfExportAll(); beforeFileSelection = {};
  Check(!fs::exists(conflictBackup) && state.saveFailed && files.empty(),
        "Concurrent save conflict is revalidated and cancels backup after the picker");
  const auto external = client_notes::ReadNotebook(clientRoot);
  Check(std::any_of(external.pages.begin(), external.pages.end(),
            [&](const auto &p) { return p.id == id && p.ticket == L"External writer wins"; }) &&
            NotesWindowText(Edit(owner)).find(L"Local concurrent draft") != std::wstring::npos,
        "Conflict protects the external notebook and the local draft");
  checkRestoredUi();
  // Resolve only this disposable test conflict for an additional enabled-state
  // check. No production conflict resolution behavior is replaced.
  state.original = external; Save(owner);
  EnableWindow(owner, FALSE);
  files.push_back((output / L"already-disabled.7z").wstring());
  GuiProfExportAll();
  Check(!IsWindowEnabled(owner) && !g_bClientNotesBusy.load(),
        "Backup preserves an already-disabled Notes window's original state");
  EnableWindow(owner, TRUE);
  RemoveWindowSubclass(owner, ObserveNotesBackupEnable, 893);
  DestroyWindow(owner);
  std::cout << "Notes backup snapshot: " << checks - start << " regression checks passed.\n";
}
