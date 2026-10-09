#include <windows.h>
#include <commdlg.h>
#include <deque>
namespace notes_test {
INT_PTR WINAPI Dialog(HINSTANCE,LPCWSTR,HWND,DLGPROC,LPARAM);
BOOL WINAPI PickFile(OPENFILENAMEW *);
}
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain CtSpacesProductionWinMain
#define DialogBoxParamW notes_test::Dialog
#define GetOpenFileNameW notes_test::PickFile
#define GetSaveFileNameW notes_test::PickFile
#include "../ctSpaces.cpp"
#undef DialogBoxParamW
#undef GetOpenFileNameW
#undef GetSaveFileNameW
#undef wWinMain

static std::deque<int> answers;
static std::deque<std::pair<std::wstring,std::wstring>> inputs;
static std::deque<std::wstring> files;
BOOL WINAPI notes_test::PickFile(OPENFILENAMEW *file) {
  if (files.empty()) throw std::runtime_error("Unexpected file picker");
  wcscpy_s(file->lpstrFile,file->nMaxFile,files.front().c_str()); files.pop_front(); return TRUE;
}
static unsigned checks=0;
static void Check(bool condition,const char *name) {
  if (!condition) throw std::runtime_error(name);
  ++checks; std::cout << "PASS " << name << std::endl;
}
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR text,LPCWSTR,UINT flags) {
  std::wcout << L"Notice: " << text << std::endl;
  if (!answers.empty()) { int result=answers.front(); answers.pop_front(); return result; }
  return (flags & MB_YESNO) ? IDNO : IDOK;
}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &) { return false; }
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &) {}
INT_PTR WINAPI notes_test::Dialog(HINSTANCE,LPCWSTR resource,HWND,DLGPROC,LPARAM data) {
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_INPUT) && !inputs.empty()) {
    auto *input=reinterpret_cast<NotesInput *>(data);
    input->first=inputs.front().first; input->second=inputs.front().second; inputs.pop_front(); return IDOK;
  }
  throw std::runtime_error("Unexpected modal dialog in test");
}
static void Put(const fs::path &path,const std::string &bytes) {
  fs::create_directories(path.parent_path()); std::ofstream(path,std::ios::binary)<<bytes;
}
static void Fixture(const std::wstring &name) {
  const auto root=g_sDataDir/L"Sites"/name;
  Put(root/L"ctSpaces-client-v2","ctSpaces-client-schema=2\r\n");
  Put(root/L"Browsers/edge/ctSpaces-browser-v2","ctSpaces-browser-schema=2\r\nbrowser=edge\r\n");
  Put(root/L"Browsers/edge/Profile/ctSpaces","ctSpaces-profile=2\r\n");
  Put(root/L"Browsers/edge/Profile/Default/Preferences","{}");
}
static HWND Open(const std::wstring &name) {
  const auto root=g_sDataDir/L"Sites"/name;
  Check(IsExistingClientProfile(name),"Disposable client recognized");
  auto *state=new ClientNotesDialogState{name,root,client_notes::ReadNotebook(root)};
  state->draftName=L"ctSpaces-notes-draft-"+std::to_wstring(GetCurrentProcessId())+L".ctn";
  state->richEditModule=LoadLibraryW(L"Msftedit.dll");
  HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_CLIENT_NOTES),g_hGui,ClientNotesDlgProc,reinterpret_cast<LPARAM>(state));
  Check(dialog!=nullptr,"Production notes dialog created"); g_hNotesWindow=dialog; return dialog;
}
static ClientNotesDialogState &State(HWND dialog) {
  return *reinterpret_cast<ClientNotesDialogState *>(GetWindowLongPtrW(dialog,DWLP_USER));
}
static HWND Edit(HWND dialog) { return GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT); }
static void Select(HWND edit,LONG a,LONG b) { CHARRANGE r{a,b}; SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&r)); }
static void Type(HWND dialog,const wchar_t *text) {
  SendMessageW(Edit(dialog),EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text));
  MarkNotesChanged(dialog,State(dialog));
}
static void Command(HWND dialog,UINT command) { SendMessageW(dialog,WM_COMMAND,command,0); }
static void Save(HWND dialog) { Check(SaveClientNotesDraft(dialog,State(dialog),true),"Notebook saved safely"); }
static void Tests(HWND dialog) {
  auto &s=State(dialog);
  Check(IsWindowEnabled(g_hGui)!=FALSE,"Launcher remains enabled");
  EnableWindow(dialog,FALSE);
  Check(!PrepareNotesForClientChange(L"Workspace QA") && IsWindow(dialog),"Client operation cannot destroy notes during a nested modal");
  EnableWindow(dialog,TRUE);
  Type(dialog,L"first text"); Save(dialog); HWND first=Edit(dialog); const auto firstId=s.pages[s.activePage].id;
  Command(dialog,IDC_NOTES_ADD_TAB);
  Check(s.pages.size()==2 && s.pages[s.activePage].name==L"Untitled","Plus immediately creates unnamed tab");
  Check(Edit(dialog)!=first,"Tabs keep separate native editors");
  Command(dialog,IDC_NOTES_CLOSE_TAB);
  Check(s.pages.size()==1 && Edit(dialog)==first,"Untouched blank tab discarded");
  Check(SendMessageW(first,EM_CANUNDO,0,0)!=0,"Undo survives tab switch");
  SendMessageW(first,EM_UNDO,0,0);
  Check(NotesWindowText(first).empty(),"Undo changes only original note");
  SendMessageW(first,EM_REDO,0,0); Save(dialog);
  Command(dialog,IDC_NOTES_CLOSE_TAB);
  Check(!HasActiveNote(s) && s.pages.size()==1 && !s.pages[0].open,"Close preserves saved note with zero open tabs");
  Check((GetWindowLongPtrW(first,GWL_STYLE)&WS_VISIBLE)==0,"Closed editor hidden");
  Command(dialog,IDC_NOTES_REOPEN);
  Check(Edit(dialog)==first && NotesWindowText(first)==L"first text","Reopen restores content and editor");
  Command(dialog,IDC_NOTES_DUPLICATE);
  Check(s.pages.size()==2 && s.pages[s.activePage].id!=firstId && NotesWindowText(Edit(dialog))==L"first text","Duplicate gets distinct identity and same content");
  Command(dialog,IDC_NOTES_PIN);
  Check(s.visiblePages.front()==s.activePage,"Pinned tab moves to front");
  Command(dialog,IDC_NOTES_ARCHIVE);
  Check(s.pages[1].archived && !s.pages[1].open,"Archive hides tab without discarding note");
  NotesLibrary library; library.owner=dialog; library.state=&s;
  HWND list=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),dialog,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  SendDlgItemMessageW(list,IDC_NOTES_FILTER,CB_SETCURSEL,2,0); NotesLibraryFill(list,library);
  Check(library.results.size()==1 && library.results[0]==1,"Archive library filter");
  SendMessageW(list,WM_COMMAND,IDC_NOTES_RESTORE,0);
  Check(!s.pages[1].archived && !s.pages[1].open,"Restored note remains available without forced tab");
  DestroyWindow(list);
  Command(dialog,IDC_NOTES_DELETE);
  Check(s.pages[0].deleted && !HasActiveNote(s),"Deletion goes to recently deleted");
  list=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),dialog,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  SendDlgItemMessageW(list,IDC_NOTES_FILTER,CB_SETCURSEL,3,0); NotesLibraryFill(list,library);
  Check(library.results.size()==1,"Recently deleted filter");
  SendMessageW(list,WM_COMMAND,IDC_NOTES_RESTORE,0); DestroyWindow(list);
  s.pages[0].open=true; s.activePage=0; s.metadataChanged=true; NotesSelectAvailable(dialog,s);
  Select(Edit(dialog),0,-1); Type(dialog,L"alpha beta alpha"); Save(dialog);
  SetDlgItemTextW(dialog,IDC_NOTES_FIND,L"beta"); Select(Edit(dialog),0,0); NotesFind(dialog,s,false);
  CHARRANGE r{}; NotesSelection(Edit(dialog),r); Check(r.cpMin==6 && r.cpMax==10,"Find selects matching range");
  inputs.push_back({L"alpha",L"gamma"}); answers.push_back(IDYES); Command(dialog,IDC_NOTES_REPLACE);
  Check(NotesWindowText(Edit(dialog))==L"gamma beta gamma","Replace all edits both matches");
  SendMessageW(Edit(dialog),EM_UNDO,0,0);
  Check(NotesWindowText(Edit(dialog))==L"alpha beta alpha","Replace all is one undo action");
  Select(Edit(dialog),0,5); inputs.push_back({L"Support",L"https://example.com/one"}); Command(dialog,IDC_NOTES_LINK);
  Check(s.pages[0].links.size()==1 && NotesRangeText(Edit(dialog),0,7)==L"Support","Labeled link inserted");
  inputs.push_back({L"Support",L"https://example.com/two"}); Command(dialog,IDC_NOTES_LINK);
  Check(s.pages[0].links[0].second==L"https://example.com/two","Existing labeled link address edited");
  Save(dialog); std::string before; StreamClientNotesOut(Edit(dialog),before);
  Command(dialog,IDC_NOTES_ZOOM_IN); std::string after; StreamClientNotesOut(Edit(dialog),after);
  Check(before==after && s.pages[0].zoom==110,"Zoom preserves document formatting");
  Select(Edit(dialog),0,-1); Type(dialog,L"\u2611\u2003Finished"); Select(Edit(dialog),10,10);
  Check(NotesListKey(Edit(dialog),WM_CHAR,VK_RETURN),"Checklist Enter handled");
  Check(NotesWindowText(Edit(dialog)).find(L"\u2610\u2003")!=std::wstring::npos,"Checklist Enter creates unchecked next item");
  Save(dialog);
  const auto snapshot=client_notes::ReadNotebook(s.clientRoot);
  auto external=snapshot.pages; external[0].rtf="{\\rtf1 External update}";
  Check(client_notes::WriteNotebook(s.clientRoot,external,snapshot),"External writer fixture committed");
  Type(dialog,L"local draft"); Check(!SaveClientNotesDraft(dialog,s,false) && s.saveFailed,"Concurrent writer prevents overwrite");
  Check(fs::exists(s.clientRoot/s.draftName),"Failed save retains recovery file");
  Check(!PrepareNotesForClientChange(L"Workspace QA") && IsWindow(dialog),"Client changes blocked while save unresolved");
  answers.push_back(IDYES); Command(dialog,IDC_NOTES_CONFLICT);
  Check(!s.saveFailed && s.pages.size()==3,"Conflict resolution keeps latest and recovered copy");
  Check(NotesPlain(s.pages[0]).find(L"External update")!=std::wstring::npos,"External content preserved");
  Check(NotesWindowText(Edit(dialog)).find(L"local draft")!=std::wstring::npos,"Local draft preserved as copy");
  inputs.push_back({L"Destination QA",L""}); answers.push_back(IDOK); Command(dialog,IDC_NOTES_TRANSFER);
  const auto destination=client_notes::ReadNotebook(g_sDataDir/L"Sites/Destination QA");
  Check(destination.status==client_notes::ReadStatus::Ok && destination.pages.size()==2,"Copy writes destination notebook");
  Check(s.pages.size()==3 && !s.pages[s.activePage].deleted,"Copy leaves source intact");
  Save(dialog); Command(dialog,IDC_NOTES_CLOSE_ALL);
  Check(!HasActiveNote(s),"Close all produces empty state"); Save(dialog);
}
static void Extended(HWND dialog) {
  auto &s=State(dialog);
  Command(dialog,IDC_NOTES_TEMPLATE+2);
  Check(NotesWindowText(Edit(dialog)).find(L"Maintenance checklist")!=std::wstring::npos,"Maintenance template populates document");
  Type(dialog,L" keyboard edit");
  SendMessageW(dialog,WM_TIMER,1,0);
  Check(NotesPlain(client_notes::ReadNotebook(s.clientRoot).pages.back()).find(L"keyboard edit")!=std::wstring::npos,"Autosave timer commits pending edit");
  HWND editor=Edit(dialog); Select(editor,0,5);
  const LRESULT canUndo=SendMessageW(editor,EM_CANUNDO,0,0);
  g_bThemeIsDark=false; BuildThemeColors(false,g_themeColors); UpdateThemeBrushes(); RefreshNotesForTheme();
  CHARFORMAT2W format{sizeof(format)}; SendMessageW(editor,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check(format.crTextColor==g_themeColors.crControlText && SendMessageW(editor,EM_CANUNDO,0,0)==canUndo,"Live theme updates text while preserving undo");
  g_bThemeIsDark=true; BuildThemeColors(true,g_themeColors); UpdateThemeBrushes(); RefreshNotesForTheme();
  const auto text=NotesWindowText(editor); std::string rtf; StreamClientNotesOut(editor,rtf);
  for (int width : {900,1100,1400,950}) {
    SetWindowPos(dialog,nullptr,0,0,width,600,SWP_NOZORDER|SWP_NOMOVE|SWP_NOACTIVATE);
    LayoutClientNotes(dialog,GetDpiForWindow(dialog));
  }
  std::string resized; StreamClientNotesOut(editor,resized);
  Check(text==NotesWindowText(editor) && rtf==resized,"Repeated resize preserves text and rich formatting");
  RECT outer{},inner{}; GetWindowRect(dialog,&outer); GetClientRect(dialog,&inner);
  Check(outer.right-outer.left==inner.right && outer.bottom-outer.top==inner.bottom,"Custom client frame occupies full window without native border");
  Check((GetWindowLongPtrW(GetDlgItem(dialog,IDC_NOTES_STATUS),GWL_STYLE)&WS_VISIBLE)==0,"No autosave footer consumes editor space");
  Select(editor,0,5); Command(dialog,IDC_NOTES_HIGHLIGHT);
  SendMessageW(editor,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check(!(format.dwEffects&CFE_AUTOBACKCOLOR) && format.crBackColor==s.highlightColor,"Highlight applies current swatch");
  Command(dialog,IDC_NOTES_HIGHLIGHT);
  SendMessageW(editor,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check((format.dwEffects&CFE_AUTOBACKCOLOR)!=0,"Highlight toggles off");
  Save(dialog);
  const auto exportPath=g_sDataDir/L"export.rtf"; files.push_back(exportPath.wstring()); Command(dialog,IDC_NOTES_EXPORT);
  const auto exported=client_notes::ReadFileSnapshot(g_sDataDir,L"export.rtf",client_notes::kMaxRichBytes);
  Check(exported.status==client_notes::ReadStatus::Ok && client_notes::ValidRichText(exported.bytes),"RTF export writes validated rich content");
  size_t count=s.pages.size(); files.push_back(exportPath.wstring()); answers.push_back(IDOK); Command(dialog,IDC_NOTES_IMPORT);
  Check(s.pages.size()==count+1 && NotesWindowText(Edit(dialog))==text,"RTF import preview adds independent note");
  const auto txt=g_sDataDir/L"export.txt"; files.push_back(txt.wstring()); Command(dialog,IDC_NOTES_EXPORT);
  auto bytes=client_notes::ReadFileSnapshot(g_sDataDir,L"export.txt",client_notes::kMaxBytes); std::wstring decoded;
  Check(client_notes::DecodeUtf8(bytes.bytes,decoded) && decoded==text,"Text export preserves readable content");
  files.push_back(txt.wstring()); answers.push_back(IDCANCEL); count=s.pages.size(); Command(dialog,IDC_NOTES_IMPORT);
  Check(s.pages.size()==count,"Cancelled import changes nothing");
  auto drafts=s.pages; drafts.back().rtf="{\\rtf1 Crash recovery content}";
  std::string draftBytes; Check(client_notes::SerializeNotebook(drafts,draftBytes),"Crash snapshot serializes");
  Check(client_notes::WriteAuxiliary(s.clientRoot,L"ctSpaces-notes-draft-4294967294.ctn",draftBytes),"Orphan recovery fixture written");
  answers.push_back(IDYES); NotesRecoverDrafts(dialog,s);
  Check(NotesWindowText(Edit(dialog))==L"Crash recovery content" && s.pages.size()==count+1,"Orphan crash draft recovered as separate note");
  Check(!fs::exists(s.clientRoot/L"ctSpaces-notes-draft-4294967294.ctn"),"Recovered snapshot removed after successful save");
  const auto history=std::string("{\\rtf1 Prior revision}");
  client_notes::RememberRevision(s.pages[s.activePage],history,true);
  NotesLibrary lib; lib.owner=dialog; lib.state=&s; lib.history=true; lib.page=s.activePage;
  HWND preview=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),dialog,NotesLibraryProc,reinterpret_cast<LPARAM>(&lib));
  CHARFORMAT2W previewFormat{sizeof(previewFormat)}; Select(GetDlgItem(preview,IDC_NOTES_PREVIEW),0,5);
  SendDlgItemMessageW(preview,IDC_NOTES_PREVIEW,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&previewFormat));
  Check(previewFormat.crTextColor==g_themeColors.crControlText,"Read-only history preview has readable theme text");
  SendMessageW(preview,WM_COMMAND,IDOK,0); DestroyWindow(preview);
  Check(NotesWindowText(Edit(dialog))==L"Prior revision","History preview restores selected revision");
  Check(std::any_of(s.pages[s.activePage].history.begin(),s.pages[s.activePage].history.end(),[](const auto &v){return v.rtf.find("Crash recovery content")!=std::string::npos;}),"History restore retains replaced content");
  inputs.push_back({L"Destination QA",L""}); answers.push_back(IDOK); const auto moved=s.activePage; Command(dialog,IDC_NOTES_MOVE);
  Check(s.pages[moved].deleted && client_notes::ReadNotebook(g_sDataDir/L"Sites/Destination QA").pages.size()==3,"Move commits destination then retains source in trash");
  lib=NotesLibrary{}; lib.owner=dialog; lib.state=&s;
  preview=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),dialog,NotesLibraryProc,reinterpret_cast<LPARAM>(&lib));
  SendDlgItemMessageW(preview,IDC_NOTES_FILTER,CB_SETCURSEL,3,0); NotesLibraryFill(preview,lib);
  count=s.pages.size(); answers.push_back(IDNO); SendMessageW(preview,WM_COMMAND,IDC_NOTES_DELETE,0);
  Check(s.pages.size()==count,"Permanent deletion cancellation preserves note");
  answers.push_back(IDYES); SendMessageW(preview,WM_COMMAND,IDC_NOTES_DELETE,0);
  Check(s.pages.size()==count-1,"Confirmed permanent deletion removes only selected trash note"); DestroyWindow(preview);
  const auto notebook=g_sDataDir/L"export.ctn"; files.push_back(notebook.wstring()); Command(dialog,IDC_NOTES_EXPORT_ALL);
  count=s.pages.size(); files.push_back(notebook.wstring()); answers.push_back(IDOK); Command(dialog,IDC_NOTES_IMPORT);
  Check(s.pages.size()==count*2,"Notebook export imports as uniquely named copies");
  const auto saved=client_notes::ReadNotebook(s.clientRoot);
  auto other=saved.pages; other[0].ticket=L"external";
  Check(client_notes::WriteNotebook(s.clientRoot,other,saved),"External metadata update fixture");
  s.pages[s.activePage].ticket=L"local ticket only"; s.metadataChanged=true;
  Check(!SaveClientNotesDraft(dialog,s,false),"Metadata-only conflict blocks stale save");
  answers.push_back(IDYES); Command(dialog,IDC_NOTES_CONFLICT);
  Check(s.pages[s.activePage].ticket==L"local ticket only" && !s.saveFailed,"Metadata-only draft survives conflict recovery");
  // Simulate the native tab mouse messages within this process, not an external app.
  Command(dialog,IDC_NOTES_CLOSE_ALL); Command(dialog,IDC_NOTES_TEMPLATE+1); Command(dialog,IDC_NOTES_TEMPLATE+3);
  HWND tabs=GetDlgItem(dialog,IDC_NOTES_TABS); RECT a{},b{}; TabCtrl_GetItemRect(tabs,0,&a); TabCtrl_GetItemRect(tabs,1,&b);
  const auto oldFirst=s.pages[s.visiblePages[0]].id;
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(a.left+20,a.top+10));
  SendMessageW(tabs,WM_LBUTTONUP,0,MAKELPARAM(a.left+20,a.top+10));
  Check(s.dragTab==-1,"Completed click clears pending tab drag");
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(a.left+20,a.top+10));
  SendMessageW(tabs,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(b.left+20,b.top+10));
  SendMessageW(tabs,WM_LBUTTONUP,0,MAKELPARAM(b.left+20,b.top+10));
  Check(s.pages[s.visiblePages[1]].id==oldFirst,"Tab drag persists requested order");
  Command(dialog,IDC_NOTES_CLOSE_ALL); Save(dialog);
}
int wmain() {
  try {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES}; InitCommonControlsEx(&controls);
    Gdiplus::GdiplusStartupInput graphics; Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);
    g_hInst=GetModuleHandleW(nullptr); g_bThemeIsDark=true; BuildThemeColors(true,g_themeColors); UpdateThemeBrushes();
    g_sDataDir=fs::current_path()/L"build"/(L"notes-workspace-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    fs::create_directories(g_sDataDir); g_sConfigPath=g_sDataDir/L"ctSpaces.ini";
    Fixture(L"Workspace QA"); Fixture(L"Destination QA");
    g_hGui=CreateWindowExW(0,L"STATIC",L"Notes test parent",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,g_hInst,nullptr);
    HWND dialog=Open(L"Workspace QA"); Tests(dialog); Extended(dialog); DestroyWindow(dialog);
    dialog=Open(L"Workspace QA");
    Check(!HasActiveNote(State(dialog)),"Zero open tabs persists across restart");
    Check((GetWindowLongPtrW(Edit(dialog),GWL_STYLE)&WS_VISIBLE)==0,"Restart empty state does not expose blank editor");
    Command(dialog,IDC_NOTES_ADD_TAB); Check(HasActiveNote(State(dialog)),"New note from empty state");
    DestroyWindow(dialog); DestroyWindow(g_hGui);
    std::cout << "Notes workspace: " << checks << " checks passed.\n"; return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << std::endl; return 1; }
}
