namespace notes_details_tab_close {
static unsigned failures = 0;

static void Expect(bool condition, const char *message) {
  ++checks;
  std::cout << (condition ? "PASS " : "FAIL_NOTES_DETAILS_CLOSE ") << message << '\n';
  if (!condition) ++failures;
}

static HWND OpenEmptyNotebook(const std::wstring &client) {
  Fixture(client);
  std::string bytes;
  Check(client_notes::SerializeNotebook({}, bytes), "Prepare a saved notebook with no default note");
  Put(g_sDataDir / L"Sites" / client / client_notes::kNotebookFileName, bytes);
  return Open(client);
}

static void AddBlankNote(HWND owner) {
  Command(owner, IDC_NOTES_ADD_TAB);
  auto &state = State(owner);
  Check(HasActiveNote(state) && state.pages[state.activePage].pristine &&
        state.pages[state.activePage].name == L"Untitled",
        "Create the ordinary empty Untitled tab through its Add command");
}

static void CheckInvalidDetails(UINT closeCommand) {
  const auto client = L"Blank details QA " + std::to_wstring(closeCommand);
  const auto owner = OpenEmptyNotebook(client);
  auto &state = State(owner);
  AddBlankNote(owner);
  const uint64_t noteId = state.attachedPageId;
  Command(owner, IDC_NOTES_DETAILS);
  const HWND panel = state.panel;
  Check(panel && IsWindow(panel), "Open Details for the blank tab");
  SetDlgItemTextW(panel, IDC_NOTES_INPUT2, L"unfinished URL");
  SendMessageW(panel, WM_TIMER, 1, 0);
  Check(state.detailsDrafts.contains(noteId) && state.pages[state.activePage].pristine,
        "The invalid URL is pending on an otherwise untouched note");

  Command(owner, closeCommand);
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  const auto note = std::ranges::find(saved.pages, noteId, &client_notes::NotePage::id);
  Check(!HasActiveNote(state) && !state.panel && IsWindow(owner),
        "Closing the tab leaves the notebook open with no active note");
  Expect(state.detailsDrafts.contains(noteId) && note != saved.pages.end() &&
         !note->open && note->ticketUrl.empty(),
         "Closing a blank tab keeps its invalid Details available without publishing the URL");

  if (state.detailsDrafts.contains(noteId)) {
    Check(note != saved.pages.end(), "The unfinished Details still belong to a saved note");
    lastNotice.clear();
    answers.push_back(IDNO);
    CloseClientNotesDialog(owner, state);
    Check(IsWindow(owner) && state.attachedPageId == noteId &&
          lastNotice.find(L"Untitled") != std::wstring::npos && state.panel &&
          NotesWindowText(GetDlgItem(state.panel, IDC_NOTES_INPUT2)) == L"unfinished URL",
          "Declining to discard reopens the original note and its unfinished field");
    answers.push_back(IDYES);
  }
  const auto root = state.clientRoot;
  CloseClientNotesDialog(owner, state);
  const auto final = client_notes::ReadNotebook(root);
  Check(!IsWindow(owner) && final.pages.empty(),
        "Explicit discard can close the notebook and remove its unused blank note");
  NotesBackgroundAudit("blank note with unfinished Details");
}

static void CheckNativeCloseWithPendingDetails() {
  const std::wstring client = L"Pending details tab X QA";
  const auto owner = OpenEmptyNotebook(client);
  auto &state = State(owner);
  AddBlankNote(owner);
  const uint64_t noteId = state.attachedPageId;
  Command(owner, IDC_NOTES_DETAILS);
  const HWND panel = state.panel;
  Check(panel && IsWindow(panel), "Open native Details before entering a pending ticket");
  SetDlgItemTextW(panel, IDC_NOTES_INPUT1, L"QA-421");
  Check(state.pages[state.activePage].ticket.empty() && state.detailsDrafts.empty(),
        "The ticket is still waiting for the Details timer or panel close");

  SendMessageW(panel, WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(owner));
  Check(IsWindow(panel), "The outside-click dismissal queues a close and leaves the panel alive for now");
  notes_inactive_tab_close::ClickClose(GetDlgItem(owner, IDC_NOTES_TABS), 0);
  NotesBackgroundAudit("queued Details dismissal and native tab close");
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  const auto note = std::ranges::find(saved.pages, noteId, &client_notes::NotePage::id);
  Expect(note != saved.pages.end() && note->ticket == L"QA-421" &&
         !note->open && !note->pristine,
         "The native tab X saves the pending ticket before deciding whether the note is blank");
  Check(!state.panel && !HasActiveNote(state), "The Details panel and its note tab are closed");
  CloseClientNotesDialog(owner, state);
}

static void CheckUntouchedBlankNote() {
  const std::wstring client = L"Blank tab control QA";
  const auto owner = OpenEmptyNotebook(client);
  auto &state = State(owner);
  AddBlankNote(owner);
  Command(owner, IDC_NOTES_CLOSE_TAB);
  Check(state.pages.empty() && client_notes::ReadNotebook(state.clientRoot).pages.empty(),
        "Closing a truly untouched blank note still removes it from the notebook");
  CloseClientNotesDialog(owner, state);
}

static bool Run() {
  CheckInvalidDetails(IDC_NOTES_CLOSE_TAB);
  CheckInvalidDetails(IDC_NOTES_CLOSE_ALL);
  CheckNativeCloseWithPendingDetails();
  CheckUntouchedBlankNote();
  return failures == 0;
}
}
