static void NotesBackgroundGuardSelfTest() {
  HWND requested=CreateWindowExW(0,L"STATIC",L"Hidden visibility request",WS_POPUP|WS_VISIBLE,10,10,120,50,nullptr,nullptr,g_hInst,nullptr);
  Check(requested && !IsWindowVisible(requested) && (GetWindowLongPtrW(requested,GWL_EXSTYLE)&WS_EX_NOACTIVATE),"Background creation strips requested visibility and prevents activation");
  ShowWindow(requested,SW_SHOW);SetWindowPos(requested,HWND_TOPMOST,20,20,130,55,SWP_SHOWWINDOW);SetFocus(requested);SetForegroundWindow(requested);SetActiveWindow(requested);BringWindowToTop(requested);
  Check(!IsWindowVisible(requested) && GetFocus()!=requested && GetForegroundWindow()!=requested,"Requested show, placement, focus and foreground remain suppressed");
  HWND owner=Open(L"Workspace QA");
  Check(!IsWindowVisible(owner) && backgroundGuard.initialFocusSuppressed>0,"Production dialog initializes hidden without native default focus selection");
  NotesBackgroundAudit("guard requests");DestroyWindow(requested);DestroyWindow(owner);
  Check(backgroundGuard.creationVisibleRequested==backgroundGuard.creationVisibleSuppressed && backgroundGuard.positionShowRequested==backgroundGuard.positionShowSuppressed && backgroundGuard.focusRequested==backgroundGuard.focusSuppressed && backgroundGuard.foregroundRequested==backgroundGuard.foregroundSuppressed,"Requested activation and visibility counters match suppressed calls");
}
static void NotesBackgroundLifecycle() {
  Fixture(L"Hidden lifecycle A");Fixture(L"Hidden lifecycle B");HWND owner=Open(L"Hidden lifecycle A");auto &state=State(owner);Command(owner,IDC_NOTES_CLOSE_ALL);
  for(const auto *name:{L"Alpha",L"Bravo",L"Charlie"}) {client_notes::NotePage page{name,true,NotesPlainRtf(std::wstring(name)+L" isolated content"),{}};Check(NotesAddPage(owner,state,std::move(page)),"Hidden lifecycle saves an independent native note");}
  const auto initial=TabIds(state);const auto active=state.attachedPageId;HWND editor=Edit(owner);Select(editor,7,7);Type(owner,L" pending");
  state.pages[state.activePage].ticket=L"T20260921.0622";state.pages[state.activePage].status=2;state.pages[state.activePage].tags={L"handover"};state.metadataChanged=true;
  Check(NotesReorderTab(owner,state,2,0) && state.attachedPageId==active && Edit(owner)==editor && SendMessageW(editor,EM_CANUNDO,0,0),"Reorder by production command preserves identity, pending text and native Undo");
  Save(owner);const auto text=NotesWindowText(editor);const auto order=TabIds(state);Command(owner,IDC_NOTES_CLOSE_TAB);
  Check(!IsWindow(editor) && state.closedPages.back()==active,"Closing a saved tab retires its native editor without deleting its note");
  const auto index=static_cast<size_t>(std::find_if(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==active;})-state.pages.begin());
  Check(NotesOpenSavedPage(owner,state,index) && state.attachedPageId==active && TabIds(state).back()==active && NotesWindowText(Edit(owner))==text,"Reopening closed reordered note appends the tab and restores exact text");
  Check(state.pages[state.activePage].ticket==L"T20260921.0622" && state.pages[state.activePage].status==2 && state.pages[state.activePage].tags==std::vector<std::wstring>{L"handover"},"Tab lifecycle preserves ticket, status and tags with the same note");
  const auto before=client_notes::ReadNotebook(state.clientRoot);Type(owner,L" retained while locked");
  HANDLE locked=CreateFileW((state.clientRoot/client_notes::kNotebookFileName).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  Check(locked!=INVALID_HANDLE_VALUE,"Hidden switch test acquires its synthetic notebook lock");const auto pending=NotesWindowText(Edit(owner));
  const auto target=g_sDataDir/L"Sites"/L"Hidden lifecycle B";
  Check(!SwitchClientNotes(owner,state,L"Hidden lifecycle B",target) && state.clientName==L"Hidden lifecycle A" && state.attachedPageId==active && NotesWindowText(Edit(owner))==pending && client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==before.notebook_snapshot.bytes,"Failed save blocks client switch and retains exact editor and client ownership");
  CloseHandle(locked);Save(owner);
  Check(SwitchClientNotes(owner,state,L"Hidden lifecycle B",target) && g_hNotesWindow==owner,"Successful background client switch retains the same native workspace window");
  Type(owner,L"Only client B");Save(owner);const auto other=client_notes::ReadNotebook(target);
  Check(SwitchClientNotes(owner,state,L"Hidden lifecycle A",g_sDataDir/L"Sites"/L"Hidden lifecycle A") && state.clientName==L"Hidden lifecycle A","Return switch reloads only the original client's notebook");
  const auto reopened=std::find_if(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==active;});
  Check(reopened!=state.pages.end() && NotesPlain(*reopened)==pending && client_notes::ReadNotebook(target).notebook_snapshot.bytes==other.notebook_snapshot.bytes,"Client switches preserve both clients' separately saved content");
  NotesBackgroundAudit("tab and client lifecycle");CloseClientNotesDialog(owner,state);
}
static void NotesBackgroundImportExport() {
  Fixture(L"Hidden export source");Fixture(L"Hidden import destination");HWND owner=Open(L"Hidden export source");auto &state=State(owner);
  Type(owner,L"Source \x4e2d\x6587 \xD83D\xDE80\r\nSecond line");state.pages[state.activePage].name=L"Archive source";state.metadataChanged=true;Save(owner);
  Command(owner,IDC_NOTES_ARCHIVE);Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Recoverable trash content");state.pages[state.activePage].name=L"Trash source";state.metadataChanged=true;Save(owner);Command(owner,IDC_NOTES_DELETE);
  const auto source=client_notes::ReadNotebook(state.clientRoot);const auto path=g_sDataDir/L"cross-client-export.ctn";files.push_back(path.wstring());Command(owner,IDC_NOTES_EXPORT_ALL);
  const auto file=client_notes::ReadFileSnapshot(g_sDataDir,path.filename().c_str(),client_notes::kMaxNotebookBytes);std::vector<client_notes::NotePage> exported;
  Check(files.empty() && file.status==client_notes::ReadStatus::Ok && client_notes::ParseNotebook(file.bytes,exported) && exported.size()==source.pages.size(),"Notebook export contains both archived and deleted saved notes without opening them");
  Check(SwitchClientNotes(owner,state,L"Hidden import destination",g_sDataDir/L"Sites"/L"Hidden import destination"),"Cross-client import uses an independent existing destination");
  Command(owner,IDC_NOTES_CLOSE_ALL);const auto existing=state.pages.size();files.push_back(path.wstring());answers.push_back(IDOK);Command(owner,IDC_NOTES_IMPORT);
  Check(files.empty() && answers.empty() && !state.saveFailed && state.pages.size()==existing+exported.size(),"Notebook import confirms and saves every cross-client copy");
  Check(std::all_of(state.pages.begin()+existing,state.pages.end(),[](const auto &p){return !p.deleted && !p.archived && p.open;}),"Imported archived and trash entries become independent available notes");
  for(size_t i=0;i<exported.size();++i) Check(NotesPlain(state.pages[existing+i])==NotesPlain(exported[i]),"Notebook import preserves the archived or deleted source text including Unicode");
  Check(client_notes::ReadNotebook(g_sDataDir/L"Sites"/L"Hidden export source").notebook_snapshot.bytes==source.notebook_snapshot.bytes,"Import leaves the exporting client's notebook byte-exact");
  const auto count=state.pages.size();const auto destination=client_notes::ReadNotebook(state.clientRoot);files.push_back(path.wstring());answers.push_back(IDCANCEL);Command(owner,IDC_NOTES_IMPORT);
  Check(state.pages.size()==count && client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==destination.notebook_snapshot.bytes,"Canceled notebook import retains every destination note and saved byte");
  Put(g_sDataDir/L"bad-import.ctn","CTNBOOK1 invalid payload");files.push_back((g_sDataDir/L"bad-import.ctn").wstring());Command(owner,IDC_NOTES_IMPORT);
  Check(state.pages.size()==count && client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==destination.notebook_snapshot.bytes,"Malformed notebook import refuses before adding or saving any note");
  Put(g_sDataDir/L"unicode-import.txt","\xef\xbb\xbfUTF-8 \xe4\xb8\xad\xe6\x96\x87\r\nNext");files.push_back((g_sDataDir/L"unicode-import.txt").wstring());answers.push_back(IDOK);Command(owner,IDC_NOTES_IMPORT);
  Check(NotesWindowText(Edit(owner))==L"UTF-8 \x4e2d\x6587\r\nNext" && state.pages.size()==count+1,"Confirmed UTF-8 BOM text import creates one independent Unicode note");
  const auto plain=g_sDataDir/L"unicode-export.txt";files.push_back(plain.wstring());Command(owner,IDC_NOTES_EXPORT);std::wstring decoded;const auto plainFile=client_notes::ReadFileSnapshot(g_sDataDir,plain.filename().c_str(),client_notes::kMaxBytes);
  Check(plainFile.status==client_notes::ReadStatus::Ok && client_notes::DecodeUtf8(plainFile.bytes,decoded) && decoded==NotesWindowText(Edit(owner)),"Plain export round trip preserves exact imported text and CRLF");
  const auto rich=g_sDataDir/L"unicode-export.rtf";files.push_back(rich.wstring());Command(owner,IDC_NOTES_EXPORT);files.push_back(rich.wstring());answers.push_back(IDOK);Command(owner,IDC_NOTES_IMPORT);
  Check(state.pages.size()==count+2 && NotesWindowText(Edit(owner))==decoded,"RTF export and confirmed import produce an independent matching native note");
  NotesBackgroundAudit("cross-client import and export");CloseClientNotesDialog(owner,state);
}
static void NotesBackgroundBulkLibrary() {
  Fixture(L"Hidden library QA");HWND owner=Open(L"Hidden library QA");auto &state=State(owner);Command(owner,IDC_NOTES_CLOSE_ALL);
  std::vector<uint64_t> ids;for(const auto *name:{L"Keep current",L"Select Alpha",L"Select Bravo"}) {client_notes::NotePage page{name,true,NotesPlainRtf(std::wstring(name)+L" body"),{}};Check(NotesAddPage(owner,state,std::move(page)),"Hidden bulk fixture saves each distinct note");ids.push_back(state.attachedPageId);}
  NotesLibrary library{owner,&state};HWND window=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  library.selecting=true;library.selected={ids[1],ids[2]};NotesLibraryFill(window,library);Command(window,IDC_NOTES_ARCHIVE);
  const auto matching=[&](bool archived,bool deleted){return std::count_if(state.pages.begin(),state.pages.end(),[&](const auto &p){return (p.id==ids[1] || p.id==ids[2]) && p.archived==archived && p.deleted==deleted;});};
  Check(matching(true,false)==2 && std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==ids[0] && !p.archived && !p.deleted;}),"Bulk archive affects only both selected note identities");
  SendDlgItemMessageW(window,IDC_NOTES_FILTER,CB_SETCURSEL,2,0);library.selected={ids[1],ids[2]};NotesLibraryFill(window,library);Command(window,IDC_NOTES_RESTORE);
  Check(matching(false,false)==2,"Bulk restore returns both selected archived notes");
  SendDlgItemMessageW(window,IDC_NOTES_FILTER,CB_SETCURSEL,0,0);library.selected={ids[1],ids[2]};NotesLibraryFill(window,library);Command(window,IDC_NOTES_DELETE);
  Check(matching(false,true)==2,"Bulk trash retains both selected notes as recoverable entries");
  SendDlgItemMessageW(window,IDC_NOTES_FILTER,CB_SETCURSEL,3,0);SetDlgItemTextW(window,IDC_NOTES_QUERY,L"no match");NotesLibraryFill(window,library);
  const auto before=client_notes::ReadNotebook(state.clientRoot);answers.push_back(IDNO);Command(window,IDC_NOTES_EMPTY_TRASH);
  Check(client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==before.notebook_snapshot.bytes && matching(false,true)==2,"Empty trash cancellation preserves hidden-by-search notes and all saved bytes");
  answers.push_back(IDYES);Command(window,IDC_NOTES_EMPTY_TRASH);
  Check(std::none_of(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==ids[1] || p.id==ids[2];}) && std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==ids[0];}),"Confirmed Empty trash removes both deleted identities and preserves the current note");
  Check(client_notes::ReadNotebook(state.clientRoot).pages.size()==state.pages.size(),"Bulk deletion persists the complete remaining notebook");
  NotesBackgroundAudit("library bulk actions");DestroyWindow(window);CloseClientNotesDialog(owner,state);
}
static void NotesBackgroundWorkflows() {
  NotesBackgroundLifecycle();NotesBackgroundAudit("lifecycle cleanup");
  NotesBackgroundImportExport();NotesBackgroundAudit("import cleanup");
  NotesBackgroundBulkLibrary();NotesBackgroundAudit("library cleanup");
  DirectSnippetEditing();NotesBackgroundAudit("direct snippet authoring, reuse and conflict cleanup");
}
