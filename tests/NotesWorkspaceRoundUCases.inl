namespace notes_workspace_round_u_cases {
using notes_workspace_round_cases::ById;
using notes_workspace_round_cases::OpenLibrary;
using notes_workspace_round_cases::OpenNotebook;
using notes_workspace_round_cases::Page;
using notes_workspace_round_cases::SelectLibraryNote;

static bool CloseAndRetry(bool bulk) {
  const auto owner = OpenNotebook(bulk ? L"Bulk close retry" : L"Active close retry",
      {Page(L"First draft", L"Saved with editor Undo"), Page(L"Second draft", L"Second body"),
       Page(L"Third draft", L"Third body")});
  auto &state = State(owner);
  const auto editor = Edit(owner);
  Select(editor, -1, -1);
  Type(owner, L" and a new sentence");
  Save(owner);
  Check(SendMessageW(editor, EM_CANUNDO, 0, 0) != 0, "Keep a native Undo record on the saved tab");
  const auto before = client_notes::ReadNotebook(state.clientRoot);
  const auto lock = CreateFileW((state.clientRoot / client_notes::kNotebookFileName).c_str(), GENERIC_READ,
      FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  Check(lock != INVALID_HANDLE_VALUE, "Keep the ordinary notebook open for reading during tab close");
  lastNotice.clear();
  const auto command = bulk ? IDC_NOTES_CLOSE_ALL : IDC_NOTES_CLOSE_TAB;
  Command(owner, command);
  CloseHandle(lock);
  const bool retained = state.attachedPageId == 1 && Edit(owner) == editor &&
      TabIds(state) == std::vector<uint64_t>{1, 2, 3} && SendMessageW(editor, EM_CANUNDO, 0, 0) != 0;
  const bool exact = client_notes::SameSnapshot(before.notebook_snapshot,
      client_notes::ReadNotebook(state.clientRoot).notebook_snapshot);
  const bool retryHistory = state.closedPages.empty();
  const bool explained = lastNotice.find(L"could not be saved") != std::wstring::npos;
  if (retained && exact && retryHistory && explained) {
    Command(owner, command);
    const auto saved = client_notes::ReadNotebook(state.clientRoot);
    Check(!ById(saved.pages, 1).open && ById(saved.pages, 2).open == !bulk &&
          ById(saved.pages, 3).open == !bulk && state.attachedPageId == (bulk ? 0 : 2),
          "Retry commits only the requested tab closures");
    Command(owner, IDC_NOTES_REOPEN);
    Check(state.attachedPageId == 1 && NotesWindowText(Edit(owner)) == L"Saved with editor Undo and a new sentence",
          "Reopening after retry restores the correct saved tab and text");
  }
  std::cout << "NOTES_TAB_CLOSE_RETRY retained=" << retained << " exact=" << exact
            << " retryHistory=" << retryHistory << " explained=" << explained << '\n';
  DestroyWindow(owner);
  return retained && exact && retryHistory && explained;
}

static void CloseOthersKeepsDetails() {
  const auto owner = OpenNotebook(L"Close others with unfinished Details",
      {Page(L"Keep open", L"The active editor"), Page(L"File later", L"Note with a Details draft")});
  auto &state = State(owner);
  Command(owner, IDC_NOTES_ADD_TAB);
  const auto blank = state.attachedPageId;
  Check(NotesOpenSavedPage(owner, state, 1), "Load the note with unfinished Details");
  Command(owner, IDC_NOTES_DETAILS);
  Check(state.panel != nullptr, "Open Details on the note to be closed");
  SetDlgItemTextW(state.panel, IDC_NOTES_INPUT1, L"QA-701");
  SetDlgItemTextW(state.panel, IDC_NOTES_INPUT2, L"draft URL");
  SendMessageW(state.panel, WM_CLOSE, 0, 0);
  Check(NotesOpenSavedPage(owner, state, 0), "Return to the tab being kept");
  const auto editor = Edit(owner);
  Command(owner, IDC_NOTES_CLOSE_OTHERS);
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  Check(TabIds(state) == std::vector<uint64_t>{1} && Edit(owner) == editor &&
        !ById(saved.pages, 2).open && state.detailsDrafts.contains(2) &&
        std::ranges::find(saved.pages, blank, &client_notes::NotePage::id) == saved.pages.end(),
        "Close other tabs retains the active editor and unfinished Details while discarding the untouched empty tab");
  Command(owner, IDC_NOTES_REOPEN);
  Command(owner, IDC_NOTES_DETAILS);
  Check(state.attachedPageId == 2 && state.panel &&
        NotesWindowText(GetDlgItem(state.panel, IDC_NOTES_INPUT1)) == L"QA-701",
        "Reopen selects the saved note and restores its own unfinished Details");
  SetDlgItemTextW(state.panel, IDC_NOTES_INPUT2, L"https://example.test/QA-701");
  SendMessageW(state.panel, WM_CLOSE, 0, 0);
  CloseClientNotesDialog(owner, state);
}

static void ReopenSkipsArchivedClosedTab() {
  const auto owner = OpenNotebook(L"Reopen skips archived entries",
      {Page(L"Older closed", L"Reopen this one"), Page(L"Newer closed", L"Keep this archived"),
       Page(L"Current", L"Independent active body")});
  auto &state = State(owner);
  Command(owner, IDC_NOTES_CLOSE_TAB);
  Command(owner, IDC_NOTES_CLOSE_TAB);
  Check(state.attachedPageId == 3 && state.closedPages == std::vector<uint64_t>{1, 2},
        "Two successful closes record their original note identities in order");
  NotesLibrary library{owner, &state};
  const auto dialog = OpenLibrary(owner, library);
  SelectLibraryNote(dialog, library, 2);
  Command(dialog, IDC_NOTES_ARCHIVE);
  DestroyWindow(dialog);
  Command(owner, IDC_NOTES_REOPEN);
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  Check(state.attachedPageId == 1 && NotesWindowText(Edit(owner)) == L"Reopen this one" &&
        ById(saved.pages, 2).archived && !ById(saved.pages, 2).open,
        "Reopen skips the latest entry after it moved to Archive");
  CloseClientNotesDialog(owner, state);
}

static void HandoverSelectedNotesOnly() {
  auto first = Page(L"Contact <client>", L"Send an update & collect a reply");
  first.tags = {L"handover"};
  auto second = Page(L"Private follow-up", L"Omit this body");
  auto third = Page(L"Resolved case", L"Sent the response");
  third.status = 4;
  const auto owner = OpenNotebook(L"Selected handover export", {first, second, third});
  auto &state = State(owner);
  const auto before = client_notes::ReadNotebook(state.clientRoot);
  NotesLibrary library{owner, &state};
  const auto dialog = OpenLibrary(owner, library);
  library.selecting = true;
  library.selected = {1, 3};
  NotesLibraryFill(dialog, library);
  const auto destination = g_sDataDir / L"selected-handover.html";
  files.push_back(destination.wstring());
  Command(dialog, IDC_NOTES_BATCH_EXPORT);
  const auto saved = client_notes::ReadFileSnapshot(g_sDataDir, destination.filename().c_str(),
      client_notes::kMaxNotebookBytes);
  Check(saved.status == client_notes::ReadStatus::Ok && saved.bytes.find("Contact &lt;client&gt;") != std::string::npos &&
        saved.bytes.find("update &amp; collect") != std::string::npos &&
        saved.bytes.find("Sent the response") != std::string::npos && saved.bytes.find("Omit this body") == std::string::npos,
        "Handover emits only selected, HTML-escaped note content");
  Check(client_notes::SameSnapshot(before.notebook_snapshot, client_notes::ReadNotebook(state.clientRoot).notebook_snapshot),
        "Handover leaves the notebook byte-identical");
  DestroyWindow(dialog);
  CloseClientNotesDialog(owner, state);
}

static void InvalidTextImportKeepsDraft() {
  const auto owner = OpenNotebook(L"Invalid text import beside editor", {Page(L"Working note", L"Working body")});
  auto &state = State(owner);
  const auto editor = Edit(owner);
  Select(editor, -1, -1);
  Type(owner, L" with a pending edit");
  const auto source = g_sDataDir / L"invalid-text.txt";
  Put(source, std::string(1, static_cast<char>(0xff)));
  files.push_back(source.wstring());
  lastNotice.clear();
  Command(owner, IDC_NOTES_IMPORT);
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  Check(files.empty() && lastNotice.find(L"UTF-8") != std::wstring::npos && saved.pages.size() == 1 &&
        NotesPlain(saved.pages.front()) == L"Working body with a pending edit" && Edit(owner) == editor,
        "Invalid UTF-8 import keeps the existing note and reports the required encoding");
  Command(owner, IDC_NOTES_UNDO);
  Check(NotesWindowText(editor) == L"Working body", "Import refusal retains the editor's Undo record");
  CloseClientNotesDialog(owner, state);
}

static void FindSwitchesAcrossCachedEditors() {
  const auto owner = OpenNotebook(L"Find rebinds on cached tabs",
      {Page(L"First matches", L"alpha beta alpha"), Page(L"Second matches", L"beta alpha")});
  auto &state = State(owner);
  Command(owner, IDC_NOTES_FIND_TOGGLE);
  SetDlgItemTextW(owner, IDC_NOTES_FIND, L"alpha");
  Select(Edit(owner), 0, 5);
  NotesFindRefresh(owner, state);
  Check(NotesWindowText(GetDlgItem(owner, IDC_NOTES_FIND_INFO)) == L"1 of 2",
        "Find counts the current note's selected occurrence");
  const auto first = Edit(owner);
  Check(NotesOpenSavedPage(owner, state, 1) && NotesWindowText(GetDlgItem(owner, IDC_NOTES_FIND_INFO)) == L"1 match",
        "Switching notes refreshes the same Find query against the second document");
  Check(NotesOpenSavedPage(owner, state, 0) && Edit(owner) == first &&
        NotesWindowText(GetDlgItem(owner, IDC_NOTES_FIND_INFO)) == L"1 of 2",
        "Returning to the cached tab restores its independent selection and Find count");
  CloseClientNotesDialog(owner, state);
}

static void QuickCaptureUsesUniqueTitle() {
  const auto owner = OpenNotebook(L"Quick capture title collision", {Page(L"Handover", L"Existing report")});
  const auto root = State(owner).clientRoot;
  CloseClientNotesDialog(owner, State(owner));
  const auto before = client_notes::ReadNotebook(root);
  NotesQuickState quick{L"Quick capture title collision", root, before, before.pages};
  const auto capture = CreateDialogParamW(g_hInst, MAKEINTRESOURCEW(IDD_NOTES_QUICK), g_hGui,
      NotesQuickProc, reinterpret_cast<LPARAM>(&quick));
  Check(capture != nullptr, "Open the private Quick capture dialog");
  SetDlgItemTextW(capture, IDC_NOTES_QUICK_TITLE, L"handover");
  SetDlgItemTextW(capture, IDC_NOTES_QUICK_TEXT, L"New quick capture");
  SendMessageW(capture, WM_TIMER, 1, 0);
  const auto first = client_notes::ReadNotebook(root);
  Check(quick.id > 1 && first.pages.size() == 2 && ById(first.pages, quick.id).name == L"handover 2" &&
        !ById(first.pages, quick.id).open, "Quick capture resolves a case-insensitive title collision as a saved note");
  SetDlgItemTextW(capture, IDC_NOTES_QUICK_TEXT, L"Updated quick capture");
  SendMessageW(capture, WM_TIMER, 1, 0);
  const auto changed = client_notes::ReadNotebook(root);
  Check(changed.pages.size() == 2 && ById(changed.pages, quick.id).rtf == NotesPlainRtf(L"Updated quick capture") &&
        ById(changed.pages, 1).rtf == ById(before.pages, 1).rtf,
        "The next Quick capture autosave updates the same note and preserves the existing report");
  DestroyWindow(capture);
}

static void TicketSortAndStatusFilter() {
  auto later = Page(L"Later callback", L"Second ticket by numeric order");
  later.ticket = L"QA-10";
  later.status = 3;
  auto early = Page(L"First callback", L"First ticket by numeric order");
  early.ticket = L"QA-2";
  early.status = 3;
  auto resolved = Page(L"Resolved", L"Leave in the notebook");
  resolved.ticket = L"QA-1";
  resolved.status = 4;
  const auto owner = OpenNotebook(L"Natural ticket sort by status", {later, early, resolved});
  auto &state = State(owner);
  const auto before = client_notes::ReadNotebook(state.clientRoot);
  NotesLibrary library{owner, &state};
  const auto dialog = OpenLibrary(owner, library);
  SendDlgItemMessageW(dialog, IDC_NOTES_SORT, CB_SETCURSEL, 3, 0);
  SendDlgItemMessageW(dialog, IDC_NOTES_STATUS_FILTER, CB_SETCURSEL, 4, 0);
  NotesLibraryFill(dialog, library);
  Check(library.results == std::vector<size_t>{1, 0},
        "Ticket sort orders numeric suffixes naturally within the selected work status");
  Check(client_notes::SameSnapshot(before.notebook_snapshot, client_notes::ReadNotebook(state.clientRoot).notebook_snapshot),
        "Sorting and filtering preserve the notebook's saved page order and contents");
  DestroyWindow(dialog);
  CloseClientNotesDialog(owner, state);
}

static void WorklogUndoKeepsPriorNote() {
  const auto owner = OpenNotebook(L"Worklog Undo with second draft",
      {Page(L"Work in progress", L"Progress update"), Page(L"Adjacent draft", L"Independent body")});
  auto &state = State(owner);
  Command(owner, IDC_NOTES_WORKLOG);
  const auto editor = Edit(owner);
  const auto expanded = NotesWindowText(editor);
  Check(expanded.starts_with(L"Progress update") && expanded.find(L"Update - ") != std::wstring::npos,
        "Work log appends a dated update to the selected note");
  Check(NotesOpenSavedPage(owner, state, 1) && NotesOpenSavedPage(owner, state, 0) && Edit(owner) == editor,
        "Returning from another tab restores the Work log's native editor");
  Command(owner, IDC_NOTES_UNDO);
  Save(owner);
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  Check(NotesPlain(ById(saved.pages, 1)) == L"Progress update" &&
        NotesPlain(ById(saved.pages, 2)) == L"Independent body",
        "One Undo removes the Work log update and leaves the other note unchanged");
  CloseClientNotesDialog(owner, state);
}

static bool Run(unsigned round) {
  const char *name = nullptr;
  bool passed = true;
  switch (round) {
  case 1: name = "active-tab-close-refused-with-undo"; passed = CloseAndRetry(false); break;
  case 2: name = "close-all-refused-before-reopen-history"; passed = CloseAndRetry(true); break;
  case 3: name = "close-others-preserves-draft-details"; CloseOthersKeepsDetails(); break;
  case 4: name = "reopen-skips-archived-closed-tab"; ReopenSkipsArchivedClosedTab(); break;
  case 5: name = "handover-includes-selected-notes"; HandoverSelectedNotesOnly(); break;
  case 6: name = "invalid-text-import-retains-editor-undo"; InvalidTextImportKeepsDraft(); break;
  case 7: name = "find-rebinds-across-cached-editors"; FindSwitchesAcrossCachedEditors(); break;
  case 8: name = "quick-capture-case-collision-and-update"; QuickCaptureUsesUniqueTitle(); break;
  case 9: name = "ticket-natural-sort-with-status-filter"; TicketSortAndStatusFilter(); break;
  case 10: name = "worklog-undo-after-tab-navigation"; WorklogUndoKeepsPriorNote(); break;
  default: throw std::runtime_error("Unexpected Notes round selector");
  }
  std::cout << "APP_NOTES_ROUND round=" << round << " case=" << name << '\n';
  return passed;
}
}
