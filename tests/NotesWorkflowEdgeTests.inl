namespace notes_workflow_edges {
static unsigned failures=0;

static void Expect(bool condition,const char *message) {
  ++checks;
  std::cout<<(condition ? "PASS " : "FAIL_WORKFLOW ")<<message<<'\n';
  if (!condition) ++failures;
}

static uint64_t AddNote(HWND owner,const std::wstring &name) {
  auto &state=State(owner);
  Check(NotesAddPage(owner,state,{name,true,NotesPlainRtf(name+L" private content"),{}}),
        "Create and save an independent note");
  return state.attachedPageId;
}

static size_t PageIndex(const ClientNotesDialogState &state,uint64_t id) {
  const auto page=std::find_if(state.pages.begin(),state.pages.end(),
      [id](const auto &entry){return entry.id==id;});
  Check(page!=state.pages.end(),"Find the original note identity");
  return static_cast<size_t>(page-state.pages.begin());
}

static HWND OpenLibrary(HWND owner,NotesLibrary &library) {
  const auto window=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),
      owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  Check(window!=nullptr,"Create the production All notes dialog");
  return window;
}

static void ReopenPreviouslyClosedTab() {
  Fixture(L"Reopen tab QA");
  const auto owner=Open(L"Reopen tab QA");
  auto &state=State(owner);
  Command(owner,IDC_NOTES_CLOSE_ALL);
  const auto first=AddNote(owner,L"First");
  const auto second=AddNote(owner,L"Second");
  const auto third=AddNote(owner,L"Third");
  Command(owner,IDC_NOTES_CLOSE_TAB);
  Check(state.attachedPageId==second,"Closing Third activates Second");
  Command(owner,IDC_NOTES_CLOSE_TAB);
  Check(state.attachedPageId==first,"Closing Second activates First");

  NotesLibrary library{owner,&state};
  const auto window=OpenLibrary(owner,library);
  const auto index=PageIndex(state,second);
  const auto row=std::find(library.results.begin(),library.results.end(),index);
  Check(row!=library.results.end(),"All notes still lists the closed Second note");
  SendDlgItemMessageW(window,IDC_NOTES_RESULTS,LB_SETCURSEL,row-library.results.begin(),0);
  Command(window,IDOK);
  DestroyWindow(window);
  Check(state.attachedPageId==second && state.pages[PageIndex(state,second)].open,
        "Opening Second in All notes restores its tab");
  Check(NotesOpenSavedPage(owner,state,PageIndex(state,first)),
        "Return to the first note before using Reopen closed tab");
  Command(owner,IDC_NOTES_REOPEN);
  Expect(state.attachedPageId==third && state.pages[PageIndex(state,third)].open &&
         NotesWindowText(Edit(owner))==L"Third private content",
         "Reopen closed tab skips Second and restores the still-closed Third note");
  Check(state.pages[PageIndex(state,second)].open,"Second stays open after the shortcut");
  NotesBackgroundAudit("mixed library and shortcut reopening");
  CloseClientNotesDialog(owner,state);
}

static void PendingLibraryQuery() {
  Fixture(L"Pending library QA");
  const auto owner=Open(L"Pending library QA");
  auto &state=State(owner);
  Command(owner,IDC_NOTES_CLOSE_ALL);
  const auto id=AddNote(owner,L"Archive control");
  NotesLibrary library{owner,&state};
  const auto window=OpenLibrary(owner,library);
  const auto archive=GetDlgItem(window,IDC_NOTES_ARCHIVE);
  const auto index=PageIndex(state,id);
  const auto row=std::find(library.results.begin(),library.results.end(),index);
  Check(row!=library.results.end(),"All notes lists the intended archive target");
  SendDlgItemMessageW(window,IDC_NOTES_RESULTS,LB_SETCURSEL,row-library.results.begin(),0);
  SendMessageW(window,WM_COMMAND,MAKEWPARAM(IDC_NOTES_RESULTS,LBN_SELCHANGE),0);
  const auto before=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
  Check(IsWindowEnabled(archive),
        "The original current note can be archived from All notes");

  SetDlgItemTextW(window,IDC_NOTES_QUERY,L"Matches no note");
  Expect(!IsWindowEnabled(archive) && !IsWindowEnabled(GetDlgItem(window,IDOK)),
         "Changing the search disables actions while old results are pending");
  Command(window,IDC_NOTES_ARCHIVE);
  Expect(!state.pages[PageIndex(state,id)].archived && state.attachedPageId==id &&
         client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==before,
         "An Archive command before the search refresh cannot change the old row");
  SendMessageW(window,WM_TIMER,1,0);
  Check(library.results.empty() && !IsWindowEnabled(archive),
        "Refreshing the query produces an empty result with actions disabled");

  if (!state.pages[PageIndex(state,id)].archived) {
    SetDlgItemTextW(window,IDC_NOTES_QUERY,L"Archive control");
    SendMessageW(window,WM_TIMER,1,0);
    Check(library.results.size()==1 && IsWindowEnabled(archive),
          "Archive becomes available for the refreshed matching result");
    Command(window,IDC_NOTES_ARCHIVE);
    const auto saved=client_notes::ReadNotebook(state.clientRoot);
    Check(state.pages[PageIndex(state,id)].archived &&
          std::any_of(saved.pages.begin(),saved.pages.end(),
              [id](const auto &page){return page.id==id && page.archived;}),
          "Archive still saves the selected note after results refresh");
  }
  DestroyWindow(window);
  NotesBackgroundAudit("debounced library actions");
  CloseClientNotesDialog(owner,state);
}

static bool Run() {
  ReopenPreviouslyClosedTab();
  PendingLibraryQuery();
  return failures==0;
}
}
