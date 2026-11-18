namespace notes_workspace_round_cases {
static HWND OpenNotebook(const wchar_t *client, std::vector<client_notes::NotePage> pages) {
  Fixture(client);
  for (size_t index = 0; index < pages.size(); ++index) {
    pages[index].id = index + 1;
    pages[index].active = index == 0;
  }
  std::string bytes;
  Check(client_notes::SerializeNotebook(pages, bytes), "Prepare the private Notes notebook");
  Put(g_sDataDir / L"Sites" / client / client_notes::kNotebookFileName, bytes);
  return Open(client);
}

static client_notes::NotePage Page(const wchar_t *name, const wchar_t *text) {
  return {name, true, NotesPlainRtf(text), {}};
}

static const client_notes::NotePage &ById(const std::vector<client_notes::NotePage> &pages,
                                         uint64_t id) {
  const auto page = std::ranges::find(pages, id, &client_notes::NotePage::id);
  Check(page != pages.end(), "Find the expected note identity");
  return *page;
}

static HWND OpenLibrary(HWND owner, NotesLibrary &library) {
  const auto dialog = CreateDialogParamW(g_hInst, MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),
      owner, NotesLibraryProc, reinterpret_cast<LPARAM>(&library));
  Check(dialog != nullptr, "Open the private Notes library");
  return dialog;
}

static void SelectLibraryNote(HWND dialog, const NotesLibrary &library, uint64_t id) {
  const auto row = std::ranges::find_if(library.results, [&](size_t index) {
    return library.state->pages[index].id == id;
  });
  Check(row != library.results.end(), "Find the expected note in the filtered library");
  SendDlgItemMessageW(dialog, IDC_NOTES_RESULTS, LB_SETCURSEL, row - library.results.begin(), 0);
  SendMessageW(dialog, WM_COMMAND, MAKEWPARAM(IDC_NOTES_RESULTS, LBN_SELCHANGE), 0);
}

static bool ArchiveWithLockedNotebook() {
  const auto owner = OpenNotebook(L"Archive save and retry",
      {Page(L"Current update", L"Keep the source editor"), Page(L"Neighbor", L"Independent text")});
  auto &state = State(owner);
  const auto editor = Edit(owner);
  Select(editor, -1, -1);
  Type(owner, L" and Undo");
  Save(owner);
  Check(SendMessageW(editor, EM_CANUNDO, 0, 0) != 0, "The source editor has native Undo");
  const auto previous = client_notes::ReadNotebook(state.clientRoot);
  NotesLibrary library{owner, &state};
  const auto dialog = OpenLibrary(owner, library);
  SelectLibraryNote(dialog, library, 1);
  const auto path = state.clientRoot / client_notes::kNotebookFileName;
  const auto lock = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  Check(lock != INVALID_HANDLE_VALUE, "Lock the private notebook against replacement");
  lastNotice.clear();
  Command(dialog, IDC_NOTES_ARCHIVE);
  CloseHandle(lock);
  const bool retained = state.attachedPageId == 1 && Edit(owner) == editor &&
      ById(state.pages, 1).open && !ById(state.pages, 1).archived &&
      SendMessageW(editor, EM_CANUNDO, 0, 0) != 0;
  const bool exact = client_notes::SameSnapshot(previous.notebook_snapshot,
      client_notes::ReadNotebook(state.clientRoot).notebook_snapshot);
  const bool explained = lastNotice.find(L"could not be saved") != std::wstring::npos;
  Save(owner);
  const bool laterSaveKeptNote = ById(client_notes::ReadNotebook(state.clientRoot).pages, 1).open;
  std::cout << "NOTES_LIBRARY_ARCHIVE_COMMIT retained=" << retained << " exact=" << exact
      << " explained=" << explained << " laterSaveKeptNote=" << laterSaveKeptNote << '\n';
  if (retained && exact && explained && laterSaveKeptNote) {
    SelectLibraryNote(dialog, library, 1);
    Command(dialog, IDC_NOTES_ARCHIVE);
    const auto saved = client_notes::ReadNotebook(state.clientRoot);
    Check(ById(saved.pages, 1).archived && !ById(saved.pages, 1).open &&
          state.attachedPageId == 2, "Retry archives only the selected note and opens its neighbor");
  }
  DestroyWindow(dialog);
  CloseClientNotesDialog(owner, state);
  return retained && exact && explained && laterSaveKeptNote;
}

static void PinnedZoomAfterReopen() {
  auto parked = Page(L"Reading view", L"Zoom and view belong to this note");
  parked.open = false;
  const auto owner = OpenNotebook(L"Reopen pinned zoom",
      {Page(L"Current", L"Current work"), parked});
  auto &state = State(owner);
  Check(NotesOpenSavedPage(owner, state, 1), "Open the saved reading note");
  Command(owner, IDC_NOTES_PIN);
  Command(owner, IDC_NOTES_ZOOM_IN);
  Command(owner, IDC_NOTES_ZOOM_IN);
  Command(owner, IDC_NOTES_CLOSE_TAB);
  Command(owner, IDC_NOTES_REOPEN);
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  Check(state.attachedPageId == 2 && ById(saved.pages, 2).pinned &&
        ById(saved.pages, 2).zoom == 120 && TabIds(state).front() == 2,
        "Reopening restores pin order and the saved zoom together");
  Check(ById(saved.pages, 1).zoom == 100 && NotesWindowText(Edit(owner)) == L"Zoom and view belong to this note",
        "Reading settings leave the other note and body unchanged");
  CloseClientNotesDialog(owner, state);
}

static void DuplicateDetailsAndFormatting() {
  auto original = Page(L"Release note", L"Review link");
  original.ticket = L"QA-604";
  original.ticketUrl = L"https://example.test/QA-604";
  original.tags = {L"review", L"release"};
  original.status = 3;
  const auto owner = OpenNotebook(L"Duplicate linked note", {original});
  auto &state = State(owner);
  Select(Edit(owner), 0, 6);
  inputs.push_back({L"Review", L"https://example.test/review"});
  Command(owner, IDC_NOTES_LINK);
  Save(owner);
  Command(owner, IDC_NOTES_DUPLICATE);
  const auto copyId = state.attachedPageId;
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  const auto &source = ById(saved.pages, 1);
  const auto &copy = ById(saved.pages, copyId);
  Check(copyId != 1 && copy.name != source.name && copy.tags == original.tags &&
        copy.status == original.status && copy.ticketUrl == original.ticketUrl &&
        copy.links == source.links && copy.linkRanges == source.linkRanges,
        "A duplicate retains its source metadata and exact saved link binding");
  Select(Edit(owner), -1, -1);
  Type(owner, L" copy only");
  Save(owner);
  const auto revised = client_notes::ReadNotebook(state.clientRoot);
  Check(NotesPlain(ById(revised.pages, 1)) == L"Review link" &&
        NotesPlain(ById(revised.pages, copyId)) == L"Review link copy only",
        "Editing the duplicate does not edit its source note");
  CloseClientNotesDialog(owner, state);
}

static void CopyIntoNameCollision() {
  const auto owner = OpenNotebook(L"Copy source", {Page(L"Handover", L"Transfer this content")});
  auto &state = State(owner);
  const auto destination = OpenNotebook(L"Copy target", {Page(L"handover", L"Keep existing content")});
  const auto destinationRoot = State(destination).clientRoot;
  CloseClientNotesDialog(destination, State(destination));
  g_hNotesWindow = owner;
  const auto source = client_notes::ReadNotebook(state.clientRoot);
  inputs.push_back({L"Copy target", L""});
  answers.push_back(IDOK);
  Command(owner, IDC_NOTES_TRANSFER);
  const auto saved = client_notes::ReadNotebook(destinationRoot);
  Check(saved.pages.size() == 2 && NotesPlain(saved.pages.front()) == L"Keep existing content" &&
        NotesPlain(saved.pages.back()) == L"Transfer this content" && !saved.pages.back().open &&
        !client_notes::SamePageName(saved.pages.front().name, saved.pages.back().name),
        "Transfer resolves a case-insensitive title collision without changing the existing note");
  Check(client_notes::SameSnapshot(source.notebook_snapshot,
        client_notes::ReadNotebook(state.clientRoot).notebook_snapshot) && state.attachedPageId == 1,
        "Copy keeps the source notebook bytes and active note");
  CloseClientNotesDialog(owner, state);
}

static void RestoreHistoryAfterEdit() {
  auto current = Page(L"Status report", L"Latest confirmed text");
  current.history.push_back({client_notes::Now(), NotesPlainRtf(L"Earlier verified text"), {}});
  const auto owner = OpenNotebook(L"History restore and reopen",
      {current, Page(L"Independent", L"Second tab remains exact")});
  auto &state = State(owner);
  Select(Edit(owner), -1, -1);
  Type(owner, L" and fresh edit");
  Save(owner);
  NotesLibrary library{owner, &state};
  library.history = true;
  library.page = state.activePage;
  const auto dialog = OpenLibrary(owner, library);
  const auto oldest = std::ranges::find(library.results, size_t{0});
  Check(oldest != library.results.end(), "The original revision remains available");
  SendDlgItemMessageW(dialog, IDC_NOTES_RESULTS, LB_SETCURSEL, oldest - library.results.begin(), 0);
  Command(dialog, IDOK);
  if (IsWindow(dialog)) DestroyWindow(dialog);
  Command(owner, IDC_NOTES_CLOSE_TAB);
  Command(owner, IDC_NOTES_REOPEN);
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  const auto &restored = ById(saved.pages, 1);
  Check(NotesWindowText(Edit(owner)) == L"Earlier verified text" &&
        std::ranges::any_of(restored.history, [](const auto &revision) {
          return revision.rtf.find("and fresh edit") != std::string::npos;
        }), "Reopening shows the restored revision and keeps the displaced edit in history");
  Check(NotesPlain(ById(saved.pages, 2)) == L"Second tab remains exact",
        "Restoring history leaves the independent tab text intact");
  CloseClientNotesDialog(owner, state);
}

static void FilterClosedNoteByTicketAndTag() {
  auto matching = Page(L"Follow up", L"Recovered from a closed tab");
  matching.ticket = L"QA-611";
  matching.tags = {L"handover"};
  matching.open = false;
  auto other = Page(L"Other follow up", L"Ticket match only");
  other.ticket = matching.ticket;
  other.tags = {L"later"};
  const auto owner = OpenNotebook(L"Combined library filter",
      {Page(L"Active note", L"Keep open"), matching, other});
  auto &state = State(owner);
  NotesLibrary library{owner, &state};
  const auto dialog = OpenLibrary(owner, library);
  SetDlgItemTextW(dialog, IDC_NOTES_QUERY, L"QA-611");
  SetDlgItemTextW(dialog, IDC_NOTES_TAG_FILTER, L"HANDOVER");
  SendMessageW(dialog, WM_TIMER, 1, 0);
  Check(library.results.size() == 1, "Ticket and case-insensitive tag filters intersect");
  SelectLibraryNote(dialog, library, 2);
  Command(dialog, IDOK);
  if (IsWindow(dialog)) DestroyWindow(dialog);
  Check(state.attachedPageId == 2 && NotesWindowText(Edit(owner)) == L"Recovered from a closed tab" &&
        ById(state.pages, 3).tags == std::vector<std::wstring>{L"later"},
        "Opening the filtered result restores the correct closed note");
  CloseClientNotesDialog(owner, state);
}

static void RestoreSelectionLeavesArchiveSibling() {
  auto first = Page(L"Archived restore", L"First archived body");
  auto second = Page(L"Archive sibling", L"Second archived body");
  first.archived = second.archived = true;
  first.open = second.open = false;
  const auto owner = OpenNotebook(L"Restore filtered archived note",
      {Page(L"Open note", L"Keep this active"), first, second});
  auto &state = State(owner);
  NotesLibrary library{owner, &state};
  library.initialFilter = 2;
  const auto dialog = OpenLibrary(owner, library);
  SetDlgItemTextW(dialog, IDC_NOTES_QUERY, L"First archived body");
  SendMessageW(dialog, WM_TIMER, 1, 0);
  SelectLibraryNote(dialog, library, 2);
  Command(dialog, IDC_NOTES_RESTORE);
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  Check(!ById(saved.pages, 2).archived && !ById(saved.pages, 2).open &&
        ById(saved.pages, 3).archived && state.attachedPageId == 1,
        "Restoring a filtered archive result keeps it closed and leaves the sibling archived");
  DestroyWindow(dialog);
  Check(NotesOpenSavedPage(owner, state, 1) &&
        NotesWindowText(Edit(owner)) == L"First archived body", "The restored note can be opened separately");
  CloseClientNotesDialog(owner, state);
}

static void WordWrapPersistsAcrossCachedTabs() {
  const auto owner = OpenNotebook(L"Word wrap and cached tabs",
      {Page(L"Long first", L"First line and wrapping preference"),
       Page(L"Long second", L"Independent line in second editor")});
  auto &state = State(owner);
  Check(NotesOpenSavedPage(owner, state, 1), "Load the second native editor");
  const auto editor = Edit(owner);
  Check(NotesOpenSavedPage(owner, state, 0), "Return to the first cached editor");
  const bool previous = state.wordWrap;
  Command(owner, IDC_NOTES_WORD_WRAP);
  Check(state.wordWrap != previous, "Word wrap changes after its configuration write");
  Check(NotesOpenSavedPage(owner, state, 1) && Edit(owner) == editor &&
        NotesWindowText(Edit(owner)) == L"Independent line in second editor",
        "Toggling wrapping keeps the cached editor and its body");
  CloseClientNotesDialog(owner, state);
  const auto reopened = Open(L"Word wrap and cached tabs");
  Check(State(reopened).wordWrap != previous && State(reopened).attachedPageId == 2,
        "Reopening keeps both wrapping and the last active note");
  CloseClientNotesDialog(reopened, State(reopened));
}

static void CancelImportThenExportUnicode() {
  const auto owner = OpenNotebook(L"Unicode export after canceled import",
      {Page(L"Private report", L"Review \x4e2d\x6587\r\nContinue")});
  auto &state = State(owner);
  const auto input = g_sDataDir / L"private-input.txt";
  const auto output = g_sDataDir / L"private-output.txt";
  Put(input, "Independent import preview");
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  files.push_back(input.wstring());
  answers.push_back(IDCANCEL);
  Command(owner, IDC_NOTES_IMPORT);
  files.push_back(output.wstring());
  Command(owner, IDC_NOTES_EXPORT);
  const auto result = client_notes::ReadFileSnapshot(g_sDataDir, output.filename().c_str(), client_notes::kMaxBytes);
  std::wstring exported;
  Check(result.status == client_notes::ReadStatus::Ok &&
        client_notes::DecodeUtf8(result.bytes, exported) && exported == L"Review \x4e2d\x6587\r\nContinue",
        "Export after a canceled import uses the original multiline Unicode editor");
  Check(client_notes::SameSnapshot(saved.notebook_snapshot,
        client_notes::ReadNotebook(state.clientRoot).notebook_snapshot) && files.empty() && answers.empty(),
        "Cancel and export leave the saved notebook and its only note intact");
  CloseClientNotesDialog(owner, state);
}

static void DetailsDraftSurvivesNavigationAndRetry() {
  const auto owner = OpenNotebook(L"Repair Details after navigation",
      {Page(L"Source Details", L"Source body"), Page(L"Other note", L"Other body")});
  auto &state = State(owner);
  Command(owner, IDC_NOTES_DETAILS);
  Check(state.panel && IsWindow(state.panel), "Open the private Details panel");
  SetDlgItemTextW(state.panel, IDC_NOTES_INPUT1, L"QA-612");
  SetDlgItemTextW(state.panel, IDC_NOTES_INPUT2, L"unfinished URL");
  Command(owner, IDC_NOTES_CLOSE_TAB);
  Check(state.attachedPageId == 2 && state.detailsDrafts.contains(1),
        "An unfinished Details edit stays with the closed source tab");
  Command(owner, IDC_NOTES_REOPEN);
  Command(owner, IDC_NOTES_DETAILS);
  Check(state.panel && state.attachedPageId == 1 &&
        NotesWindowText(GetDlgItem(state.panel, IDC_NOTES_INPUT1)) == L"QA-612",
        "Reopening restores the unfinished ticket fields to their original note");
  SetDlgItemTextW(state.panel, IDC_NOTES_INPUT2, L"https://example.test/QA-612");
  SendMessageW(state.panel, WM_CLOSE, 0, 0);
  const auto saved = client_notes::ReadNotebook(state.clientRoot);
  Check(state.detailsDrafts.empty() && ById(saved.pages, 1).ticket == L"QA-612" &&
        ById(saved.pages, 1).ticketUrl == L"https://example.test/QA-612" &&
        ById(saved.pages, 2).ticket.empty(), "Corrected Details save to the original note alone");
  CloseClientNotesDialog(owner, state);
}

static bool Run(unsigned round) {
  const char *name = nullptr;
  bool passed = true;
  switch (round) {
  case 1: name = "library-archive-locked-notebook-retry"; passed = ArchiveWithLockedNotebook(); break;
  case 2: name = "pinned-zoom-after-closed-tab-reopen"; PinnedZoomAfterReopen(); break;
  case 3: name = "duplicate-details-and-link-binding"; DuplicateDetailsAndFormatting(); break;
  case 4: name = "cross-client-copy-with-title-collision"; CopyIntoNameCollision(); break;
  case 5: name = "restore-history-then-reopen"; RestoreHistoryAfterEdit(); break;
  case 6: name = "closed-note-ticket-and-tag-filter"; FilterClosedNoteByTicketAndTag(); break;
  case 7: name = "restore-filtered-archive-with-sibling"; RestoreSelectionLeavesArchiveSibling(); break;
  case 8: name = "word-wrap-across-cached-tabs"; WordWrapPersistsAcrossCachedTabs(); break;
  case 9: name = "unicode-export-after-import-cancel"; CancelImportThenExportUnicode(); break;
  case 10: name = "correct-details-after-closed-tab-reopen"; DetailsDraftSurvivesNavigationAndRetry(); break;
  default: throw std::runtime_error("Unexpected Notes round selector");
  }
  std::cout << "APP_NOTES_ROUND round=" << round << " case=" << name << '\n';
  return passed;
}
}
