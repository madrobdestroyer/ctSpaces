#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shldisp.h>
#include <deque>
namespace notes_test {
INT_PTR WINAPI Dialog(HINSTANCE,LPCWSTR,HWND,DLGPROC,LPARAM);
BOOL WINAPI PickFile(OPENFILENAMEW *);
HINSTANCE WINAPI ShellOpen(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT);
BOOL WINAPI Tray(DWORD,PNOTIFYICONDATAW);
BOOL WINAPI Popup(HMENU,UINT,int,int,HWND,LPTPMPARAMS);
}
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain CtSpacesProductionWinMain
#define DialogBoxParamW notes_test::Dialog
#define GetOpenFileNameW notes_test::PickFile
#define GetSaveFileNameW notes_test::PickFile
#define ShellExecuteW notes_test::ShellOpen
#define Shell_NotifyIconW notes_test::Tray
#define TrackPopupMenuEx notes_test::Popup
#include "../ctSpaces.cpp"
#undef DialogBoxParamW
#undef GetOpenFileNameW
#undef GetSaveFileNameW
#undef ShellExecuteW
#undef Shell_NotifyIconW
#undef TrackPopupMenuEx
#undef wWinMain

static std::deque<int> answers;
static std::deque<std::pair<std::wstring,std::wstring>> inputs;
static std::deque<std::wstring> files;
static std::wstring lastNotice;
static unsigned walkthroughOpens=0;
static unsigned shellOpens=0,metadataOpens=0;
static int openedLibraryFilter=-1;
static std::wstring shellTarget;
static INT_PTR shellResult=33;
static bool trayAddWorks=true,trayVersionWorks=true;
static bool realTrayApi=false;
static std::vector<DWORD> trayCommands;
static UINT popupAction=0,popupCalls=0;
static bool popupPasteEnabled=false,popupCutEnabled=false;
static bool popupLeftEnabled=false,popupRightEnabled=false;
BOOL WINAPI notes_test::Tray(DWORD command,PNOTIFYICONDATAW icon) {
  if (!icon || icon->cbSize!=sizeof(*icon) || icon->uID!=1) throw std::runtime_error("Unexpected tray identity");
  trayCommands.push_back(command);
  if (realTrayApi) return ::Shell_NotifyIconW(command,icon);
  if (command==NIM_ADD) return trayAddWorks;
  if (command==NIM_SETVERSION) return trayVersionWorks && icon->uVersion==NOTIFYICON_VERSION_4;
  return TRUE;
}
BOOL WINAPI notes_test::Popup(HMENU menu,UINT,int,int,HWND,LPTPMPARAMS) {
  ++popupCalls;popupPasteEnabled=(GetMenuState(menu,5,MF_BYCOMMAND)&MF_GRAYED)==0;
  popupCutEnabled=(GetMenuState(menu,3,MF_BYCOMMAND)&MF_GRAYED)==0;
  popupLeftEnabled=(GetMenuState(menu,IDC_NOTES_TAB_LEFT,MF_BYCOMMAND)&MF_GRAYED)==0;
  popupRightEnabled=(GetMenuState(menu,IDC_NOTES_TAB_RIGHT,MF_BYCOMMAND)&MF_GRAYED)==0;
  const UINT action=popupAction;popupAction=0;return action;
}
HINSTANCE WINAPI notes_test::ShellOpen(HWND,LPCWSTR verb,LPCWSTR target,LPCWSTR parameters,LPCWSTR directory,INT) {
  if (wcscmp(verb,L"open") || parameters || directory) throw std::runtime_error("Unexpected shell URL arguments");
  ++shellOpens;shellTarget=target;return reinterpret_cast<HINSTANCE>(shellResult);
}
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
  lastNotice=text;
  std::wcout << L"Notice: " << text << std::endl;
  if (!answers.empty()) { int result=answers.front(); answers.pop_front(); return result; }
  return (flags & MB_YESNO) ? IDNO : IDOK;
}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &) { return false; }
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &) {}
INT_PTR WINAPI notes_test::Dialog(HINSTANCE,LPCWSTR resource,HWND,DLGPROC procedure,LPARAM data) {
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_HELP)) return IDOK;
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_WALKTHROUGH)) { ++walkthroughOpens; return IDOK; }
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_METADATA)) {++metadataOpens;return IDCANCEL;}
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_LIBRARY) && procedure==NotesLibraryProc) {openedLibraryFilter=reinterpret_cast<NotesLibrary *>(data)->initialFilter;return IDCANCEL;}
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_CODE_BLOCK) && !inputs.empty()) {
    auto *input=reinterpret_cast<NotesCodeInput *>(data);input->language=4;input->source=inputs.front().second;inputs.pop_front();return IDOK;
  }
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR) && !inputs.empty()) {
    HWND manager=CreateDialogParamW(g_hInst,resource,nullptr,NotesTemplateEditorProc,data);
    const auto values=inputs.front();inputs.pop_front();
    SetDlgItemTextW(manager,IDC_NOTES_TEMPLATE_NAME,values.first.c_str());
    SetDlgItemTextW(manager,IDC_NOTES_TEMPLATE_HINT,values.second.c_str());
    const bool saved=NotesTemplateEditorSave(manager,*reinterpret_cast<NotesTemplates *>(data));
    DestroyWindow(manager);return saved ? IDOK : IDCANCEL;
  }
  if (resource==MAKEINTRESOURCEW(IDD_NOTES_CLIENT_PICKER) && !inputs.empty()) {
    auto *picker=reinterpret_cast<NotesClientPicker *>(data);
    picker->selected=inputs.front().first; inputs.pop_front(); return IDOK;
  }
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
struct ResizePaintCounts { unsigned paints=0, streams=0, ownerDraws=0; };
static LRESULT CALLBACK ObserveResize(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  auto &counts=*reinterpret_cast<ResizePaintCounts *>(data);
  if (message==WM_PAINT) ++counts.paints;
  if (message==WM_DRAWITEM) ++counts.ownerDraws;
  if (message==EM_STREAMIN || message==EM_STREAMOUT) ++counts.streams;
  if (message==WM_NCDESTROY) RemoveWindowSubclass(window,ObserveResize,id);
  return DefSubclassProc(window,message,w,l);
}
static COLORREF OverflowPixel(HWND arrows) {
  RECT area{}; GetClientRect(arrows,&area);
  HDC screen=GetDC(arrows), dc=CreateCompatibleDC(screen);
  HBITMAP bitmap=CreateCompatibleBitmap(screen,area.right,area.bottom);
  HGDIOBJ old=SelectObject(dc,bitmap);
  SendMessageW(arrows,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
  const COLORREF color=GetPixel(dc,4,area.bottom/2);
  SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(arrows,screen);
  return color;
}
static LRESULT CALLBACK ObserveNativeFrame(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
  if (message==WM_NCPAINT || message==WM_NCACTIVATE) ++*reinterpret_cast<unsigned *>(data);
  return DefSubclassProc(window,message,w,l);
}
static void FrameRegression(HWND dialog) {
  BOOL dwmFrame=FALSE;
  Check(SUCCEEDED(DwmGetWindowAttribute(dialog,DWMWA_NCRENDERING_ENABLED,&dwmFrame,sizeof(dwmFrame))) && dwmFrame,"Notes retains DWM instead of falling back to a classic white thick frame");
  unsigned nativePaints=0;
  // Put the observer below the production subclass to detect frame requests
  // that escape to the default Windows/dialog painter.
  RemoveWindowSubclass(dialog,NotesWindowSubclass,1);
  SetWindowSubclass(dialog,ObserveNativeFrame,98,reinterpret_cast<DWORD_PTR>(&nativePaints));
  SetWindowSubclass(dialog,NotesWindowSubclass,1,0);
  SendMessageW(dialog,WM_NCPAINT,1,0);
  Check(nativePaints==0,"Custom notes frame prevents classic frame repaint from reaching default painter");
  Check(SendMessageW(dialog,WM_NCACTIVATE,FALSE,0)!=0,"Notes can lose activation without painting a native border");
  SendMessageW(dialog,WM_NCACTIVATE,TRUE,0);
  Check(nativePaints==0,"Activation changes never reach the default native frame handler");
  Check((GetWindowLongPtrW(dialog,GWL_STYLE)&WS_THICKFRAME)!=0,"Notes retains the native style required for real corner resizing");
  RemoveWindowSubclass(dialog,ObserveNativeFrame,98);
  RECT bounds{}; GetWindowRect(dialog,&bounds);
  NCCALCSIZE_PARAMS size{}; size.rgrc[0]=size.rgrc[1]=size.rgrc[2]=bounds;
  WINDOWPOS position{dialog,nullptr,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,0}; size.lppos=&position;
  Check(SendMessageW(dialog,WM_NCCALCSIZE,TRUE,reinterpret_cast<LPARAM>(&size))==0 && EqualRect(&size.rgrc[0],&bounds),"Resize preserves client pixels rather than requesting full hierarchy redraw");
  auto hit=[&](int x,int y) { return SendMessageW(dialog,WM_NCHITTEST,0,MAKELPARAM(x,y)); };
  Check(hit(bounds.left+2,bounds.top+2)==HTTOPLEFT && hit(bounds.right-2,bounds.bottom-2)==HTBOTTOMRIGHT,"Custom frame keeps both native corner resize handles");
  Check(hit(bounds.left+300,bounds.top+ScaleByDpi(38,GetDpiForWindow(dialog)))==HTCAPTION,"Empty custom header space remains draggable");
  RECT close{}; GetWindowRect(GetDlgItem(dialog,IDCANCEL),&close);
  Check(hit((close.left+close.right)/2,(close.top+close.bottom)/2)==HTCLIENT,"Close button stays clickable inside custom caption");
  for (int control : {IDC_NOTES_SWITCH_CLIENT,IDC_NOTES_LIBRARY,IDC_NOTES_TRASH,IDC_NOTES_MORE,IDC_NOTES_HELP}) {
    RECT button{};GetWindowRect(GetDlgItem(dialog,control),&button);
    Check(hit((button.left+button.right)/2,(button.top+button.bottom)/2)==HTCLIENT,"Notes header action receives mouse clicks instead of window dragging");
  }
}
static void ResizeAndOverflow(HWND dialog) {
  auto &s=State(dialog);
  for (int i=0;i<10;++i) Command(dialog,IDC_NOTES_TEMPLATE+3);
  HWND tabs=GetDlgItem(dialog,IDC_NOTES_TABS);
  HWND arrows=FindWindowExW(tabs,nullptr,UPDOWN_CLASSW,nullptr);
  DWORD_PTR data=0;
  Check(arrows && (GetWindowLongPtrW(arrows,GWL_STYLE)&WS_VISIBLE) && GetWindowSubclass(arrows,NotesOverflowSubclass,1,&data),"Overflow child receives notes theme after tabs overflow");
  const auto dark=OverflowPixel(arrows);
  Check(dark!=CLR_INVALID && GetRValue(dark)<180 && GetGValue(dark)<180 && GetBValue(dark)<180,"Overflow paint has dark theme surface rather than white system buttons");
  RECT before{},after{}; TabCtrl_GetItemRect(tabs,0,&before);
  RECT arrowRect{}; GetClientRect(arrows,&arrowRect);
  RECT tabRect{}; GetClientRect(tabs,&tabRect);
  Check(arrowRect.bottom==tabRect.bottom-2 && arrowRect.right==ScaleByDpi(52,GetDpiForWindow(tabs)),"Overflow navigation aligns to the full tab row");
  const LPARAM left=MAKELPARAM(arrowRect.right/4,arrowRect.bottom/2);
  SendMessageW(arrows,WM_LBUTTONDOWN,MK_LBUTTON,left); SendMessageW(arrows,WM_LBUTTONUP,0,left);
  TabCtrl_GetItemRect(tabs,0,&after);
  Check(after.left>before.left,"Themed overflow left arrow still scrolls native tabs");
  const LPARAM right=MAKELPARAM(arrowRect.right*3/4,arrowRect.bottom/2);
  SendMessageW(arrows,WM_LBUTTONDOWN,MK_LBUTTON,right); SendMessageW(arrows,WM_LBUTTONUP,0,right);
  TabCtrl_GetItemRect(tabs,0,&after);
  if (after.left!=before.left) std::cout<<"Overflow return: before="<<before.left<<", after="<<after.left<<", pos="<<SendMessageW(arrows,UDM_GETPOS32,0,0)<<std::endl;
  Check(after.left==before.left,"Themed overflow right arrow returns to original tab position");
  g_bThemeIsDark=false; BuildThemeColors(false,g_themeColors); UpdateThemeBrushes(); RefreshNotesForTheme();
  Check(OverflowPixel(arrows)!=dark,"Overflow follows a live switch to light theme");
  g_bThemeIsDark=true; BuildThemeColors(true,g_themeColors); UpdateThemeBrushes(); RefreshNotesForTheme();
  RedrawWindow(dialog,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  ResizePaintCounts counts;
  EnumChildWindows(dialog,[](HWND child,LPARAM p)->BOOL { SetWindowSubclass(child,ObserveResize,97,p); return TRUE; },reinterpret_cast<LPARAM>(&counts));
  SetWindowSubclass(dialog,ObserveResize,97,reinterpret_cast<DWORD_PTR>(&counts));
  std::string original; StreamClientNotesOut(Edit(dialog),original); counts={};
  for (int i=0;i<30;++i) SetWindowPos(dialog,nullptr,0,0,900+i*7,540+i*4,SWP_NOZORDER|SWP_NOMOVE|SWP_NOACTIVATE);
  Check(counts.paints==0,"Resize queues paints instead of forcing whole window paints inside layout");
  Check(counts.ownerDraws==0,"Resize avoids immediate owner-drawn toolbar paints too");
  Check(counts.streams==0,"Resize never reloads or saves RichEdit content");
  Check(!(GetWindowLongPtrW(dialog,GWL_EXSTYLE)&WS_EX_COMPOSITED),"Notes resizing does not composite all native child controls");
  EnumChildWindows(dialog,[](HWND child,LPARAM)->BOOL { RemoveWindowSubclass(child,ObserveResize,97); return TRUE; },0);
  RemoveWindowSubclass(dialog,ObserveResize,97);
  RedrawWindow(dialog,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  std::string resized; StreamClientNotesOut(Edit(dialog),resized);
  Check(original==resized && HasActiveNote(s),"Overflow resize preserves active note and formatted content");
  TabCtrl_GetItemRect(tabs,0,&before);
  SetWindowPos(dialog,nullptr,0,0,2000,650,SWP_NOZORDER|SWP_NOMOVE|SWP_NOACTIVATE);
  Check(!(GetWindowLongPtrW(arrows,GWL_STYLE)&WS_VISIBLE),"Overflow hides when all tabs fit");
  SetWindowPos(dialog,nullptr,0,0,900,540,SWP_NOZORDER|SWP_NOMOVE|SWP_NOACTIVATE);
  Check((GetWindowLongPtrW(arrows,GWL_STYLE)&WS_VISIBLE) && GetWindowSubclass(arrows,NotesOverflowSubclass,1,&data),"Overflow returns themed when resized narrow again");
  Command(dialog,IDC_NOTES_CLOSE_ALL); Save(dialog);
}
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
  RECT savedLabel{},body{};GetWindowRect(GetDlgItem(dialog,IDC_NOTES_STATUS),&savedLabel);GetWindowRect(Edit(dialog),&body);
  Check(savedLabel.bottom<body.top,"Autosave indicator stays in the heading without an editor footer");
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
static void PickerAndTemplates(HWND owner) {
  NotesClientPicker picker{L"Workspace QA"};
  HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_CLIENT_PICKER),owner,NotesClientPickerProc,reinterpret_cast<LPARAM>(&picker));
  Check(dialog && picker.clients==std::vector<std::wstring>{L"Destination QA"},"Client picker lists validated destinations and excludes source");
  HWND combo=GetDlgItem(dialog,IDC_NOTES_CLIENT_PICKER);
  DWORD_PTR pickerSubclass=0;
  Check(GetWindowSubclass(combo,NotesComboSubclass,1,&pickerSubclass)!=FALSE,"Client picker uses the notes themed dropdown surface");
  SetWindowTextW(combo,L"dEsT"); SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_CLIENT_PICKER,CBN_EDITCHANGE),0);
  Check(SendMessageW(combo,CB_GETCOUNT,0,0)==1 && NotesWindowText(combo)==L"dEsT","Client suggestions are case insensitive and preserve typed text");
  SetWindowTextW(combo,L"no match"); SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_CLIENT_PICKER,CBN_EDITCHANGE),0);
  Check(SendMessageW(combo,CB_GETCOUNT,0,0)==0,"Client suggestions hide unmatched destinations");
  SetWindowTextW(combo,L"destination qa"); SendMessageW(dialog,WM_COMMAND,IDOK,0);
  Check(picker.selected==L"Destination QA","Client picker resolves typed names to canonical destination"); DestroyWindow(dialog);
  NotesInput one{L"One field",L"Name",L"",L"",L""};
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_INPUT),owner,NotesInputProc,reinterpret_cast<LPARAM>(&one));
  Check(!(GetWindowLongPtrW(GetDlgItem(dialog,IDC_NOTES_INPUT2),GWL_STYLE)&WS_VISIBLE),"Single-field prompt hides unused second input");
  RECT area{},hidden{}; GetClientRect(dialog,&area); GetWindowRect(GetDlgItem(dialog,IDC_NOTES_INPUT2),&hidden); MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&hidden),2);
  HDC screen=GetDC(dialog),dc=CreateCompatibleDC(screen); HBITMAP bitmap=CreateCompatibleBitmap(screen,area.right,area.bottom); auto old=SelectObject(dc,bitmap);
  SendMessageW(dialog,WM_ERASEBKGND,reinterpret_cast<WPARAM>(dc),0);
  Check(hidden.top-1>=area.bottom || GetPixel(dc,hidden.left+20,hidden.top-1)==g_themeColors.crWindow,"Theme painter does not draw a ghost border around hidden input");
  SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(dialog,screen); DestroyWindow(dialog);
  auto &state=State(owner); Command(owner,IDC_NOTES_TEMPLATE+2); Type(owner,L" custom checklist step"); Save(owner);
  const auto sourceText=NotesWindowText(Edit(owner));
  inputs.push_back({L"My maintenance",L"A reusable checklist for our team"}); Command(owner,IDC_NOTES_SAVE_TEMPLATE);
  NotesTemplates templates{owner,&state}; Check(NotesLoadTemplates(templates) && templates.pages.size()==5,"Custom template persists alongside four described built-ins");
  Check(templates.pages.back().ticket==L"A reusable checklist for our team" && NotesPlain(templates.pages.back())==sourceText,"Template saves hint and formatted source content");
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&templates));
  SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_SETCURSEL,4,0); SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_RESULTS,LBN_SELCHANGE),0);
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_TEMPLATE_HINT))==templates.pages.back().ticket,"Template editor exposes the selected hint");
  g_bThemeIsDark=false; BuildThemeColors(false,g_themeColors); UpdateThemeBrushes(); RefreshNotesLibraryForTheme(dialog);
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==sourceText,"Template theme refresh uses the correct dialog state");
  g_bThemeIsDark=true; BuildThemeColors(true,g_themeColors); UpdateThemeBrushes(); RefreshNotesLibraryForTheme(dialog);
  SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_NAME,L"My revised maintenance");SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_HINT,L"Edited hint");SendMessageW(dialog,WM_COMMAND,IDC_NOTES_RESTORE,0);
  Check(templates.pages.back().name==L"My revised maintenance" && templates.pages.back().ticket==L"Edited hint","Template name and hint can be edited");
  const auto stale=templates;
  SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_SETSEL,-1,-1);SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L" revised template body"));
  SendMessageW(dialog,WM_COMMAND,IDC_NOTES_RESTORE,0);
  Check(NotesPlain(templates.pages.back()).find(L"revised template body")!=std::wstring::npos && NotesWindowText(Edit(owner))==sourceText,"Direct template edits save without altering the client's current note");
  auto conflict=stale; Check(!NotesSaveTemplates(owner,conflict,conflict.pages),"Stale template changes cannot overwrite newer content");
  const auto count=state.pages.size(); SendMessageW(dialog,WM_COMMAND,IDOK,0);
  Check(state.pages.size()==count+1 && state.pages.back().ticket.empty() && state.pages.back().history.empty(),"Creating from template adds independent note without leaking hint into ticket metadata");
  Check(NotesWindowText(Edit(owner))==NotesPlain(templates.pages.back()),"Template note retains updated document content");
  const auto created=NotesWindowText(Edit(owner)); answers.push_back(IDOK); SendMessageW(dialog,WM_COMMAND,IDC_NOTES_DELETE,0);
  Check(templates.pages.size()==4 && NotesWindowText(Edit(owner))==created,"Deleting a template preserves notes already created from it");
  DestroyWindow(dialog);
  NotesTemplates reloaded{owner,&state}; Check(NotesLoadTemplates(reloaded) && reloaded.pages.size()==4,"Template deletion persists across library reopening");
  Command(owner,IDC_NOTES_DELETE);
  NotesLibrary library{owner,&state}; dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETCURSEL,3,0); NotesLibraryFill(dialog,library);
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_INFO)).find(L"no automatic expiry")!=std::wstring::npos,"Recently deleted preview explains indefinite retention");
  DestroyWindow(dialog); Command(owner,IDC_NOTES_CLOSE_ALL); Save(owner);
}
static void Productivity(HWND owner) {
  Command(owner,IDC_NOTES_ADD_TAB);auto &state=State(owner);
  Type(owner,L"Original work\r\nReusable block");Save(owner);
  std::vector<std::wstring> tags;
  Check(NotesParseTags(L" network, Urgent, NETWORK, ",tags) && tags.size()==2,"Tags trim and deduplicate without case sensitivity");
  Check(!NotesParseTags(std::wstring(33,L'x'),tags),"Oversize UI tag rejected");
  auto page=state.pages[state.activePage];
  HWND metadata=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_METADATA),owner,NotesMetadataProc,reinterpret_cast<LPARAM>(&page));
  SendDlgItemMessageW(metadata,IDC_NOTES_STATUS_PICK,CB_SETCURSEL,2,0);SetDlgItemTextW(metadata,IDC_NOTES_TAGS_EDIT,L"network, Urgent");SendMessageW(metadata,WM_COMMAND,IDOK,0);
  Check(page.status==2 && page.tags.size()==2,"Status dialog applies status and tags");DestroyWindow(metadata);
  state.pages[state.activePage].status=page.status;state.pages[state.activePage].tags=page.tags;state.metadataChanged=true;Save(owner);
  auto loaded=client_notes::ReadNotebook(state.clientRoot);
  Check(loaded.pages[state.activePage].status==2 && loaded.pages[state.activePage].tags==page.tags,"Status and tags persist to disk");
  const auto before=NotesWindowText(Edit(owner));Command(owner,IDC_NOTES_WORKLOG);
  const auto after=NotesWindowText(Edit(owner));
  Check(after.find(before)==0 && after.find(L"Update - ")!=std::wstring::npos,"Work log appends a dated update without replacing earlier work");
  SendMessageW(Edit(owner),EM_UNDO,0,0);Check(NotesWindowText(Edit(owner))==before,"Work log append and formatting are one undo action");SendMessageW(Edit(owner),EM_REDO,0,0);Save(owner);
  Select(Edit(owner),14,28);auto snippet=NotesSnippetFromSelection(owner,state.pages[state.activePage]);
  Check(NotesPlain(snippet)==L"Reusable block","Snippet captures only selected rich text");
  Check(snippet.status==0 && snippet.tags.empty() && snippet.ticket.empty(),"Snippet strips note status, tags and ticket metadata");
  NotesTemplates snippets{owner,&state};snippets.snippets=true;
  Check(NotesLoadTemplates(snippets) && snippets.pages.size()==4 && snippets.root.filename()==L"NotesSnippets","Snippet library is separate from templates and has useful starter blocks");
  snippet.id=client_notes::NextId(snippets.pages);snippet.name=L"Saved QA snippet";snippets.pages.push_back(snippet);
  Check(NotesSaveTemplates(owner,snippets,snippets.pages),"Custom snippets save safely");
  Select(Edit(owner),0,0);const auto count=state.pages.size();
  HWND picker=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesTemplatesProc,reinterpret_cast<LPARAM>(&snippets));
  SendDlgItemMessageW(picker,IDC_NOTES_RESULTS,LB_SETCURSEL,4,0);SendMessageW(picker,WM_COMMAND,IDOK,0);
  Check(state.pages.size()==count && NotesWindowText(Edit(owner)).find(L"Reusable block")==0,"Snippet inserts at cursor in existing note without creating a tab");DestroyWindow(picker);
  NotesActionsState actions{true};HWND action=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_ACTIONS),owner,NotesActionsProc,reinterpret_cast<LPARAM>(&actions));
  SetDlgItemTextW(action,IDC_NOTES_QUERY,L"tags");Check(actions.results.size()==1 && kNotesActions[actions.results[0]].id==IDC_NOTES_METADATA,"Actions search matches status and tags command");
  SetDlgItemTextW(action,IDC_NOTES_QUERY,L"no-such-action");Check(actions.results.empty() && !IsWindowEnabled(GetDlgItem(action,IDOK)),"Empty action search cannot invoke an unrelated command");DestroyWindow(action);
  const auto html=NotesHandoverHtml(state,{state.activePage});
  Check(html.find(L"Waiting on client")!=std::wstring::npos && html.find(L"network, Urgent")!=std::wstring::npos && html.find(L"Reusable block")!=std::wstring::npos,"Handover includes readable content, status and tags");
  Check(NotesHtmlEscape(L"<script>&\"'")==L"&lt;script&gt;&amp;&quot;&#39;","HTML handover escapes markup and attribute characters");
  files.push_back((g_sDataDir/L"handover.html").wstring());Check(NotesExportHandover(owner,state,{state.activePage}),"Handover writes a standalone HTML file");
  Check(client_notes::ReadFileSnapshot(g_sDataDir,L"handover.html",client_notes::kMaxNotebookBytes).bytes.find("<!doctype html>")==0,"Handover file has portable HTML bytes");
  NotesLibrary library{owner,&state};HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  RECT heading{},queryRect{};GetWindowRect(GetDlgItem(dialog,IDC_NOTES_LIBRARY_HEADING),&heading);GetWindowRect(GetDlgItem(dialog,IDC_NOTES_QUERY),&queryRect);
  Check(heading.bottom<queryRect.top,"Library heading stays above all four search field borders");
  MenuItemData menuData;menuData.text=L"Select all visible notes";MEASUREITEMSTRUCT measure{};measure.CtlType=ODT_MENU;measure.itemData=reinterpret_cast<ULONG_PTR>(&menuData);
  Check(HandleCleanupDialogTheme(dialog,WM_MEASUREITEM,0,reinterpret_cast<LPARAM>(&measure)).value_or(FALSE) && measure.itemWidth>100 && measure.itemHeight>=20,"Library popup uses the themed menu sizing handler instead of a blank narrow box");
  SetDlgItemTextW(dialog,IDC_NOTES_TAG_FILTER,L"network");SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_FILTER,CB_SETCURSEL,3,0);NotesLibraryFill(dialog,library);
  Check(library.results.size()==1 && library.results[0]==state.activePage,"All notes combines tag and status filters");
  SetDlgItemTextW(dialog,IDC_NOTES_TAG_FILTER,L"missing");NotesLibraryFill(dialog,library);Check(library.results.empty(),"Nonmatching tags give no results");
  SetDlgItemTextW(dialog,IDC_NOTES_TAG_FILTER,L"");SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_FILTER,CB_SETCURSEL,0,0);SendDlgItemMessageW(dialog,IDC_NOTES_SORT,CB_SETCURSEL,2,0);NotesLibraryFill(dialog,library);
  bool sorted=true;for(size_t i=1;i<library.results.size();++i) sorted=sorted && NotesLower(state.pages[library.results[i-1]].name)<=NotesLower(state.pages[library.results[i]].name);
  Check(sorted,"Title sort orders results without reordering stored pages");
  library.selecting=true;library.selected={state.pages[state.activePage].id};NotesLibraryFill(dialog,library);
  Check(NotesLibrarySelection(dialog,library).size()==1 && !IsWindowEnabled(GetDlgItem(dialog,IDOK)),"Selection mode scopes actions to ticked notes and blocks ambiguous Open");
  SendMessageW(dialog,WM_COMMAND,IDC_NOTES_ARCHIVE,0);Check(std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==page.id && p.archived;}),"Bulk archive applies to selected note");
  SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETCURSEL,2,0);
  library.selected={page.id};NotesLibraryFill(dialog,library);SendMessageW(dialog,WM_COMMAND,IDC_NOTES_RESTORE,0);
  Check(std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==page.id && !p.archived;}),"Bulk restore returns selected archived note");DestroyWindow(dialog);
  client_notes::NotePage second{L"Bulk companion",true,"{\\rtf1 Companion}",{}};second.id=client_notes::NextId(state.pages);second.open=false;const auto secondId=second.id;state.pages.push_back(second);state.baselines.emplace_back();state.metadataChanged=true;Save(owner);
  NotesLibrary several{owner,&state};several.selecting=true;several.selected={page.id,secondId};
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&several));
  Check(NotesLibrarySelection(dialog,several).size()==2,"Two independently selected notes form one bulk action scope");
  SendMessageW(dialog,WM_COMMAND,IDC_NOTES_ARCHIVE,0);
  Check(std::count_if(state.pages.begin(),state.pages.end(),[&](const auto &p){return (p.id==page.id || p.id==secondId) && p.archived;})==2,"One bulk archive updates both selected notes");
  SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETCURSEL,2,0);several.selected={page.id,secondId};NotesLibraryFill(dialog,several);
  SendMessageW(dialog,WM_COMMAND,IDC_NOTES_DELETE,0);Check(std::count_if(state.pages.begin(),state.pages.end(),[&](const auto &p){return (p.id==page.id || p.id==secondId) && p.deleted;})==2,"One bulk trash operation retains both recoverable notes");
  SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETCURSEL,3,0);several.selected={page.id,secondId};NotesLibraryFill(dialog,several);
  const auto beforeDelete=state.pages.size();answers.push_back(IDNO);SendMessageW(dialog,WM_COMMAND,IDC_NOTES_DELETE,0);Check(state.pages.size()==beforeDelete,"Canceling selected permanent deletion preserves both notes");
  answers.push_back(IDYES);SendMessageW(dialog,WM_COMMAND,IDC_NOTES_DELETE,0);Check(state.pages.size()==beforeDelete-2,"One confirmed permanent delete removes both selected notes");DestroyWindow(dialog);
  const auto quickRoot=g_sDataDir/L"Sites"/L"Destination QA";NotesQuickState quick{L"Destination QA",quickRoot,client_notes::ReadNotebook(quickRoot)};quick.pages=quick.baseline.pages;
  HWND capture=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_QUICK),owner,NotesQuickProc,reinterpret_cast<LPARAM>(&quick));
  Check(capture && GetDlgItem(capture,IDC_NOTES_QUICK_TEXT),"Quick capture has an editable body");
  Check(NotesQuickSave(capture,quick) && quick.id==0,"Untouched Quick note creates no empty note");
  SetDlgItemTextW(capture,IDC_NOTES_QUICK_TEXT,L"Captured {details} \\ safely\r\nUnicode \x4e2d\x6587");
  Check(!NotesWindowText(GetDlgItem(capture,IDC_NOTES_QUICK_TEXT)).empty(),"Quick capture accepts typed text");
  const bool quickSaved=NotesQuickSave(capture,quick);
  Check(quickSaved && quick.id!=0,"Quick capture saves body without requiring a title");
  const auto quickCount=quick.pages.size();SetDlgItemTextW(capture,IDC_NOTES_QUICK_TEXT,L"Updated capture");Check(NotesQuickSave(capture,quick) && quick.pages.size()==quickCount,"Quick capture updates one stable note instead of duplicating it");
  auto stale=quick;auto changed=quick.pages;changed.back().ticket=L"external";Check(client_notes::WriteNotebook(quickRoot,changed,quick.baseline),"Quick capture conflict fixture saves competing change");
  SetDlgItemTextW(capture,IDC_NOTES_QUICK_TEXT,L"Unsaved local draft");Check(!NotesQuickSave(capture,stale) && NotesWindowText(GetDlgItem(capture,IDC_NOTES_QUICK_TEXT))==L"Unsaved local draft","Quick capture conflict preserves typed draft and refuses overwrite");DestroyWindow(capture);
  Fixture(L"Quick fresh QA");const auto freshRoot=g_sDataDir/L"Sites"/L"Quick fresh QA";NotesQuickState fresh{L"Quick fresh QA",freshRoot,client_notes::ReadNotebook(freshRoot)};fresh.pages=fresh.baseline.pages;
  capture=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_QUICK),owner,NotesQuickProc,reinterpret_cast<LPARAM>(&fresh));SetDlgItemTextW(capture,IDC_NOTES_QUICK_TEXT,L"First captured thought");
  Check(NotesQuickSave(capture,fresh) && fresh.pages.size()==1 && fresh.pages[0].name==L"Quick note" && !fresh.pages[0].open,"First capture saves exactly one closed note without an empty placeholder");DestroyWindow(capture);
  Command(owner,IDC_NOTES_CLOSE_ALL);Save(owner);
}
static void Walkthrough(HWND owner) {
  const auto &workspace=State(owner);
  const auto count=workspace.pages.size(),active=workspace.activePage;
  const auto text=NotesWindowText(Edit(owner)); const auto saved=client_notes::ReadNotebook(workspace.clientRoot);
  Command(owner,IDC_NOTES_WALKTHROUGH); Check(walkthroughOpens==1,"Walkthrough command opens the dedicated read-only guide");
  HWND overview=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_HELP),owner,NotesHelpProc,0);
  Check(GetDlgItem(overview,IDC_NOTES_WALKTHROUGH)!=nullptr,"Quick Help offers a named step-by-step walkthrough entry"); DestroyWindow(overview);
  NotesTourState tour;
  HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_WALKTHROUGH),owner,NotesTourProc,reinterpret_cast<LPARAM>(&tour));
  Check(dialog && SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_GETCOUNT,0,0)==kNotesTourSteps.size(),"Walkthrough exposes every everyday-use topic");
  Check(!IsWindowEnabled(GetDlgItem(dialog,IDC_GUIDE_BACK)) && tour.position==0,"Walkthrough starts at first step with Back disabled");
  SendMessageW(dialog,WM_COMMAND,IDOK,0); Check(tour.position==1,"Next advances the walkthrough");
  SendMessageW(dialog,WM_COMMAND,IDC_GUIDE_BACK,0); SendMessageW(dialog,WM_COMMAND,IDC_GUIDE_BACK,0);
  Check(tour.position==0,"Back returns to first step and cannot underflow");
  bool images=true,steps=true;
  for (size_t i=0;i<kNotesTourSteps.size();++i) {
    SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_SETCURSEL,i,0);
    SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_TOUR_TOPICS,CBN_SELCHANGE),0);
    images=images && tour.image && tour.image->GetWidth()>0 && tour.image->GetHeight()>0;
    steps=steps && tour.position==i && NotesWindowText(GetDlgItem(dialog,IDC_GUIDE_TITLE))==kNotesTourSteps[i].title;
  }
  Check(images,"Every walkthrough topic decodes its embedded real screenshot");
  Check(steps,"Topic selector reaches every step and updates its title");
  Check(NotesWindowText(GetDlgItem(dialog,IDOK))==L"Finish","Last step changes Next to Finish");
  SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_SETCURSEL,8,0);
  SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_TOUR_TOPICS,CBN_SELCHANGE),0);
  const auto warning=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_TOUR_WARNING));
  const auto body=NotesWindowText(GetDlgItem(dialog,IDC_GUIDE_BODY));
  Check(warning.find(L"PERMANENT DELETION")!=std::wstring::npos && warning.find(L"cannot be undone")!=std::wstring::npos &&
      body.find(L"hidden by a search")!=std::wstring::npos && body.find(L"revision histories")!=std::wstring::npos,"Permanent deletion step warns about history, hidden results and no undo");
  Check((GetWindowLongPtrW(GetDlgItem(dialog,IDC_GUIDE_BODY),GWL_STYLE)&ES_READONLY)!=0 && GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH)==nullptr,"Walkthrough text is read-only and has no deletion command control");
  SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_SETCURSEL,CB_ERR,0); SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_TOUR_TOPICS,CBN_SELCHANGE),0);
  Check(tour.position==8,"Invalid topic selection preserves the current step");
  MINMAXINFO limits{}; SendMessageW(dialog,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
  SetWindowPos(dialog,nullptr,0,0,limits.ptMinTrackSize.x,limits.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER);
  RECT content{},picture{},caption{},next{},close{};
  GetWindowRect(GetDlgItem(dialog,IDC_GUIDE_BODY),&content); GetWindowRect(GetDlgItem(dialog,IDC_NOTES_TOUR_IMAGE),&picture);
  GetWindowRect(GetDlgItem(dialog,IDC_NOTES_TOUR_CAPTION),&caption); GetWindowRect(GetDlgItem(dialog,IDOK),&next); GetWindowRect(GetDlgItem(dialog,IDCANCEL),&close);
  Check(content.right<picture.left && picture.bottom<caption.top && caption.bottom<next.top && next.right<close.left,"Walkthrough content, screenshot, caption and buttons remain separate at minimum size");
  DestroyWindow(dialog);
  Check(workspace.pages.size()==count && workspace.activePage==active && NotesWindowText(Edit(owner))==text &&
      client_notes::ReadNotebook(workspace.clientRoot).notebook_snapshot.bytes==saved.notebook_snapshot.bytes,"Reading all walkthrough steps leaves notebook and editor unchanged");
}
static void GuidanceAndBulkTrash(HWND owner) {
  auto &state=State(owner);
  Command(owner,IDC_NOTES_ADD_TAB); Type(owner,L"Ticket investigation for the current client");
  inputs.push_back({L"44553",L"https://example.com/ticket/44553"}); Command(owner,IDC_NOTES_TICKET);
  const auto activeId=state.pages[state.activePage].id;
  NotesLibrary library{owner,&state};
  HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  const LRESULT selected=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);
  Check(selected>=0 && library.results[selected]==state.activePage,"All notes starts with current note selected");
  wchar_t label[1024]{}; SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETTEXT,selected,reinterpret_cast<LPARAM>(label));
  Check(std::wstring(label).find(L"[Ticket 44553]")!=std::wstring::npos,"Ticket number is visible directly in All notes result list");
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_INFO)).find(L"\r\nTicket: 44553")!=std::wstring::npos,"Ticket reference gets its own line in preview information");
  Check(IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_OPEN_TICKET)),"Valid ticket URL enables a labeled action beside preview");
  Check(!(GetWindowLongPtrW(GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH),GWL_STYLE)&WS_VISIBLE),"Empty trash is hidden outside Recently deleted");
  SetDlgItemTextW(dialog,IDC_NOTES_QUERY,L"44553"); NotesLibraryFill(dialog,library);
  Check(library.results.size()==1 && state.pages[library.results[0]].id==activeId,"Ticket number search resolves the linked note");
  DestroyWindow(dialog);
  NotesInput input{L"Ticket details",L"Ticket number",L"Ticket URL",L"44553",L"",L"The number appears in All notes. A URL enables Open ticket link."};
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_INPUT),owner,NotesInputProc,reinterpret_cast<LPARAM>(&input));
  RECT hint{},field{}; GetWindowRect(GetDlgItem(dialog,IDC_NOTES_INPUT_HINT),&hint); GetWindowRect(GetDlgItem(dialog,IDC_NOTES_LABEL1),&field);
  Check((GetWindowLongPtrW(GetDlgItem(dialog,IDC_NOTES_INPUT_HINT),GWL_STYLE)&WS_VISIBLE) && hint.bottom<=field.top,"Ticket dialog explains fields above the inputs without overlap"); DestroyWindow(dialog);
  Check(GetDlgItem(owner,IDC_NOTES_HELP)!=nullptr && (GetWindowLongPtrW(GetDlgItem(owner,IDC_NOTES_HELP),GWL_STYLE)&WS_VISIBLE),"Notes keeps a discoverable Help button");
  Command(owner,IDC_NOTES_HELP); Check(GetPrivateProfileIntW(L"notes_window",L"help_seen",0,g_sConfigPath.c_str())==1,"Reading quick guide persists first-use dismissal");
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_HELP),owner,NotesHelpProc,0);
  Check(dialog && GetDlgItem(dialog,IDC_NOTES_TEMPLATES) && GetDlgItem(dialog,IDC_NOTES_LIBRARY),"Quick guide includes labeled routes to templates and All notes"); DestroyWindow(dialog);
  client_notes::NotePage archived{L"Archived survivor",true,"{\\rtf1\\ansi Archived content}",{}};
  archived.id=client_notes::NextId(state.pages); archived.archived=true; archived.open=false;
  const auto archivedId=archived.id; state.pages.push_back(std::move(archived)); state.baselines.emplace_back();
  for (int i=0;i<100;++i) {
    client_notes::NotePage deleted{L"Deleted item "+std::to_wstring(i),true,"{\\rtf1\\ansi Trash content}",{}};
    deleted.id=client_notes::NextId(state.pages); deleted.deleted=true; deleted.open=false;
    deleted.history.push_back({client_notes::Now(),"{\\rtf1\\ansi Old trash content}"});
    state.pages.push_back(std::move(deleted)); state.baselines.emplace_back();
  }
  state.metadataChanged=true; Save(owner);
  const auto before=state.pages.size(),trash=static_cast<size_t>(std::count_if(state.pages.begin(),state.pages.end(),[](const auto &p){return p.deleted;}));
  const auto destination=client_notes::ReadNotebook(g_sDataDir/L"Sites/Destination QA");
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  SendMessageW(dialog,WM_COMMAND,IDC_NOTES_EMPTY_TRASH,0); Check(state.pages.size()==before,"Bulk deletion cannot run from Current notes view");
  SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETCURSEL,3,0); SetDlgItemTextW(dialog,IDC_NOTES_QUERY,L"Deleted item 99"); NotesLibraryFill(dialog,library);
  Check(library.results.size()==1 && IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH)),"Empty trash is available even with only one matching search result");
  answers.push_back(IDNO); SendMessageW(dialog,WM_COMMAND,IDC_NOTES_EMPTY_TRASH,0);
  Check(state.pages.size()==before && lastNotice.find(std::to_wstring(trash))!=std::wstring::npos && lastNotice.find(L"hidden by the current search")!=std::wstring::npos,"Bulk trash confirmation states total scope and cancel preserves every note");
  library.readOnly=true; SendMessageW(dialog,WM_COMMAND,IDC_NOTES_EMPTY_TRASH,0); library.readOnly=false;
  Check(state.pages.size()==before,"Read-only library rejects Empty trash");
  state.metadataChanged=true;
  HANDLE locked=CreateFileW((state.clientRoot/client_notes::kNotebookFileName).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  Check(locked!=INVALID_HANDLE_VALUE,"Notebook locked to exercise bulk save failure");
  answers.push_back(IDYES); SendMessageW(dialog,WM_COMMAND,IDC_NOTES_EMPTY_TRASH,0); CloseHandle(locked);
  Check(state.pages.size()==before && state.saveFailed,"Failed save blocks bulk deletion before any note is erased");
  Save(owner); SetDlgItemTextW(dialog,IDC_NOTES_QUERY,L"no matching results"); NotesLibraryFill(dialog,library);
  Check(library.results.empty() && IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH)),"Empty trash works independently of selected or visible results");
  answers.push_back(IDYES); SendMessageW(dialog,WM_COMMAND,IDC_NOTES_EMPTY_TRASH,0);
  Check(state.pages.size()==before-trash && std::none_of(state.pages.begin(),state.pages.end(),[](const auto &p){return p.deleted;}),"One confirmed operation removes all deleted notes and their histories");
  Check(std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==activeId;}) &&
      std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==archivedId && p.archived;}),"Empty trash preserves current and archived notes");
  const auto saved=client_notes::ReadNotebook(state.clientRoot);
  Check(saved.pages.size()==state.pages.size() && std::none_of(saved.pages.begin(),saved.pages.end(),[](const auto &p){return p.deleted;}),"Emptied trash persists in the saved notebook");
  Check(client_notes::ReadNotebook(g_sDataDir/L"Sites/Destination QA").notebook_snapshot.bytes==destination.notebook_snapshot.bytes,"Empty trash affects only the selected client's notebook");
  Check(!IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH)),"Empty trash disables when nothing remains deleted");
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_DELETE))==L"Delete forever","Empty trash view keeps the permanent deletion label");
  MINMAXINFO libraryLimits{}; SendMessageW(dialog,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&libraryLimits));
  SetWindowPos(dialog,nullptr,0,0,libraryLimits.ptMinTrackSize.x,libraryLimits.ptMinTrackSize.y,SWP_NOMOVE|SWP_NOZORDER);
  RECT trashButton{},closeButton{};
  GetWindowRect(GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH),&trashButton); GetWindowRect(GetDlgItem(dialog,IDCANCEL),&closeButton);
  Check(trashButton.right<closeButton.left,"Empty trash and Close do not overlap at the minimum library size");
  DestroyWindow(dialog); Command(owner,IDC_NOTES_CLOSE_ALL); Save(owner);
}
static void TicketCodeAndTopActions(HWND owner) {
  Command(owner,IDC_NOTES_ADD_TAB);auto &state=State(owner);
  Type(owner,L"Before code\r\nSELECTED\r\nAfter code");Save(owner);
  auto &page=state.pages[state.activePage];page.ticket=L"44553";page.ticketUrl=L"https://example.com/tickets/44553";state.metadataChanged=true;Save(owner);
  auto count=shellOpens;answers.push_back(IDNO);Command(owner,IDC_NOTES_OPEN_TICKET);
  Check(shellOpens==count && lastNotice.find(L"default browser")!=std::wstring::npos,"Ticket confirmation explains regular default browser session and cancel does not launch");
  answers.push_back(IDYES);Command(owner,IDC_NOTES_OPEN_TICKET);
  Check(shellOpens==count+1 && shellTarget==page.ticketUrl,"Editor ticket opens URL with Windows association and no client profile arguments");
  answers.push_back(IDYES);OpenNotesUrl(owner,state,page.ticketUrl);
  Check(shellOpens==count+2,"Ticket URL inside note body follows default browser route");
  OpenNotesUrl(owner,state,L"file:///unsafe",true);Check(shellOpens==count+2,"Ticket browser route rejects non-web URLs");
  shellResult=31;answers.push_back(IDYES);Command(owner,IDC_NOTES_OPEN_TICKET);shellResult=33;
  Check(lastNotice.find(L"Windows could not open")!=std::wstring::npos,"Default browser launch failure is explained");
  NotesLibrary lib{owner,&state};HWND library=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&lib));
  for (size_t i=0;i<lib.results.size();++i) if (lib.results[i]==state.activePage) SendDlgItemMessageW(library,IDC_NOTES_RESULTS,LB_SETCURSEL,i,0);
  answers.push_back(IDYES);SendMessageW(library,WM_COMMAND,IDC_NOTES_OPEN_TICKET,0);
  Check(shellTarget==page.ticketUrl && shellOpens==count+4,"All notes ticket uses the same default-browser route");DestroyWindow(library);
  Command(owner,IDC_NOTES_METADATA);Check(state.panel && IsWindow(state.panel),"Top Status button opens the unified details panel");CloseNotesPanel(state);
  page.status=3;UpdateNotesToolbar(owner,state);
  Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_METADATA))==L"Status: Follow-up","Top Status button displays current local ticket-work status");
  Command(owner,IDC_NOTES_FIND_TOGGLE);Check(state.findVisible,"Top Find button exposes note search");Command(owner,IDC_NOTES_FIND_CLOSE);
  for (int width : {900,1240,1500,900}) {
    SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,GetDpiForWindow(owner)),ScaleByDpi(600,GetDpiForWindow(owner)),SWP_NOMOVE|SWP_NOZORDER);
    RECT prior{};bool first=true;for (int id : {IDC_NOTES_SNIPPETS,IDC_NOTES_TEMPLATES,IDC_NOTES_DETAILS}) {
      RECT bounds{};GetWindowRect(GetDlgItem(owner,id),&bounds);Check((GetWindowLongPtrW(GetDlgItem(owner,id),GWL_STYLE)&WS_VISIBLE) && (first || bounds.left>=prior.right),"Document heading actions remain visible and separated across responsive layouts");prior=bounds;first=false;
    }
    RECT trash{},actions{};GetWindowRect(GetDlgItem(owner,IDC_NOTES_TRASH),&trash);GetWindowRect(GetDlgItem(owner,IDC_NOTES_MORE),&actions);
    RECT all{};GetWindowRect(GetDlgItem(owner,IDC_NOTES_LIBRARY),&all);
    Check(all.right<=trash.left && trash.right<=actions.left && (GetWindowLongPtrW(GetDlgItem(owner,IDC_NOTES_TRASH),GWL_STYLE)&WS_VISIBLE),"Direct Trash action fits beside All notes across window sizes");
    RECT format{},editor{},heading{},allNotes{};GetWindowRect(GetDlgItem(owner,IDC_NOTES_STYLE),&format);GetWindowRect(Edit(owner),&editor);
    GetWindowRect(GetDlgItem(owner,IDC_NOTES_CLIENT_HEADING),&heading);GetWindowRect(GetDlgItem(owner,IDC_NOTES_LIBRARY),&allNotes);
    Check(prior.bottom<format.top && format.top<editor.top && heading.right<=allNotes.left,"Document heading, formatting, editor and client picker do not overlap");
  }
  NotesTemplates templates{owner,&state};Check(NotesLoadTemplates(templates),"Templates load for top action management");
  HWND manager=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&templates));
  Check((GetWindowLongPtrW(GetDlgItem(manager,IDC_NOTES_SAVE_TEMPLATE),GWL_STYLE)&WS_VISIBLE) && IsWindowEnabled(GetDlgItem(manager,IDC_NOTES_SAVE_TEMPLATE)),"Templates offers a direct From current note action");
  count=static_cast<unsigned>(templates.pages.size());SendMessageW(manager,WM_COMMAND,IDC_NOTES_SAVE_TEMPLATE,0);
  SetDlgItemTextW(manager,IDC_NOTES_TEMPLATE_NAME,L"Top row QA template");SetDlgItemTextW(manager,IDC_NOTES_TEMPLATE_HINT,L"Created from the templates manager");SendMessageW(manager,WM_COMMAND,IDC_NOTES_RESTORE,0);
  Check(templates.pages.size()==count+1 && templates.pages.back().ticket==L"Created from the templates manager","Template manager creates and reloads a new reusable template");DestroyWindow(manager);
  NotesCodeInput input{4,L"print(\"{hello}\\world\")\r\n\t# Unicode: \x4e2d\x6587"};
  HWND code=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_CODE_BLOCK),owner,NotesCodeProc,reinterpret_cast<LPARAM>(&input));
  Check(SendDlgItemMessageW(code,IDC_NOTES_CODE_LANGUAGE,CB_GETCOUNT,0,0)==std::size(kNotesCodeLanguages) && NotesWindowText(GetDlgItem(code,IDC_NOTES_CODE_SOURCE))==input.source,"Code dialog exposes language choices and editable source");DestroyWindow(code);
  const auto before=NotesWindowText(Edit(owner));Select(Edit(owner),13,21);
  Check(NotesInsertCode(owner,state,input),"Selected prose converts into a labeled native code box");
  const auto after=NotesWindowText(Edit(owner));Check(after.find(L"Python - Code")!=std::wstring::npos && after.find(L"{hello}\\world")!=std::wstring::npos && after.find(L"\x4e2d\x6587")!=std::wstring::npos && after.find(L"Before code")!=std::wstring::npos && after.find(L"After code")!=std::wstring::npos,"Code label, literal syntax, Unicode and surrounding prose survive insertion");
  const auto body=static_cast<LONG>(after.find(L"print"));Select(Edit(owner),body,body+5);
  PARAFORMAT2 paragraph{sizeof(paragraph)};SendMessageW(Edit(owner),EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  CHARFORMAT2W format{sizeof(format)};SendMessageW(Edit(owner),EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check((paragraph.wEffects&PFE_TABLE) && _wcsicmp(format.szFaceName,L"Consolas")==0 && format.crTextColor==g_themeColors.crControlText,"Code body is inside a real table box with monospace theme text");
  Check(!NotesInsertCode(owner,state,input) && NotesWindowText(Edit(owner))==after,"Nested code box insertion is blocked without changing source");
  SendMessageW(Edit(owner),EM_UNDO,0,0);Check(NotesWindowText(Edit(owner))==before,"Code conversion is one undo action");SendMessageW(Edit(owner),EM_REDO,0,0);Save(owner);
  const auto saved=client_notes::ReadNotebook(state.clientRoot);Check(saved.pages[state.activePage].rtf.find("\\trowd")!=std::string::npos && NotesPlain(saved.pages[state.activePage]).find(L"Python - Code")!=std::wstring::npos,"Code box and language survive notebook save and read");
  const auto handover=NotesHandoverHtml(state,{state.activePage});Check(handover.find(L"Python - Code")!=std::wstring::npos && handover.find(L'\v')==std::wstring::npos && handover.find(L'\a')==std::wstring::npos && handover.find(L"# Unicode")!=std::wstring::npos,"Readable handover retains code caption and portable line separators");
  g_bThemeIsDark=false;BuildThemeColors(false,g_themeColors);UpdateThemeBrushes();RefreshNotesForTheme();Select(Edit(owner),body,body+5);SendMessageW(Edit(owner),EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check(format.crTextColor==g_themeColors.crControlText,"Code remains readable after changing to light theme");
  g_bThemeIsDark=true;BuildThemeColors(true,g_themeColors);UpdateThemeBrushes();RefreshNotesForTheme();
  Command(owner,IDC_NOTES_CLOSE_ALL);Save(owner);
}
static void PumpPanelMessages() {
  MSG msg{};while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
}
static void InspectorNavigation() {
  Fixture(L"Inspector A");Fixture(L"Inspector B");
  HWND owner=Open(L"Inspector A");Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Original client text");Save(owner);
  auto &state=State(owner);const auto aRoot=state.clientRoot;
  const auto pageCount=state.pages.size();
  SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_NOTES_ADD_TAB,BN_SETFOCUS),0);
  Check(state.pages.size()==pageCount,"Focusing New note does not create a note");
  SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_NOTES_SWITCH_CLIENT,BN_SETFOCUS),0);
  Check(!state.panel,"Focusing the client picker does not execute its action");
  Check(NotesWindowText(GetDlgItem(owner,IDC_NOTES_CLIENT_HEADING))==L"Notes" && NotesWindowText(GetDlgItem(owner,IDC_NOTES_MORE))==L"More \u2026","Notes header has one client selector and Windows More label");
  RECT before{},after{};GetWindowRect(Edit(owner),&before);Command(owner,IDC_NOTES_DETAILS);HWND panel=state.panel;
  Check(IsWindow(panel) && IsWindowEnabled(owner),"Details is a temporary panel with an enabled owner");
  SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_NOTES_DETAILS,BN_KILLFOCUS),0);
  Check(IsWindow(panel) && state.panel==panel,"A panel taking focus does not dismiss or reopen itself");
  SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_CLIENT_NOTES_TEXT,EN_KILLFOCUS),0);
  Check(IsWindow(panel) && state.panel==panel,"Editor focus loss does not dismiss the active inspector");
  ShowWindow(panel,SW_SHOW);
  HWND statusCombo=GetDlgItem(panel,IDC_NOTES_STATUS_PICK);SetFocus(statusCombo);
  SendMessageW(statusCombo,CB_SHOWDROPDOWN,TRUE,0);
  MSG accept{};accept.hwnd=statusCombo;accept.message=WM_KEYDOWN;accept.wParam=VK_RETURN;
  if (!ProcessNotesPanelMessage(accept)) {TranslateMessage(&accept);DispatchMessageW(&accept);}
  Check(IsWindow(panel) && state.panel==panel,"Enter accepts the open status dropdown without closing Details");
  SendMessageW(statusCombo,CB_SHOWDROPDOWN,FALSE,0);
  RECT panelRect{},field{};GetClientRect(panel,&panelRect);GetWindowRect(GetDlgItem(panel,IDC_NOTES_TAGS_EDIT),&field);
  MapWindowPoints(nullptr,panel,reinterpret_cast<POINT *>(&field),2);
  HDC screen=GetDC(panel),buffer=CreateCompatibleDC(screen);HBITMAP bitmap=CreateCompatibleBitmap(screen,panelRect.right,panelRect.bottom);auto oldBitmap=SelectObject(buffer,bitmap);
  SendMessageW(panel,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(buffer),PRF_CLIENT);
  Check(GetPixel(buffer,field.left+10,field.top-1)==GetPixel(buffer,field.left+10,field.bottom) && GetPixel(buffer,field.left+10,field.top-1)!=g_themeColors.crWindow,"Details edit fields paint their complete top and bottom borders");
  SelectObject(buffer,oldBitmap);DeleteObject(bitmap);DeleteDC(buffer);ReleaseDC(panel,screen);
  GetWindowRect(Edit(owner),&after);Check(EqualRect(&before,&after),"Details never shrinks or moves the editor");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT1,L"48301");SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"https://tickets.example.com/48301");SetDlgItemTextW(panel,IDC_NOTES_TAGS_EDIT,L"network, handover");
  SendDlgItemMessageW(panel,IDC_NOTES_STATUS_PICK,CB_SETCURSEL,2,0);SendMessageW(panel,WM_TIMER,1,0);
  auto disk=client_notes::ReadNotebook(aRoot);
  Check(disk.pages[state.activePage].ticket==L"48301" && disk.pages[state.activePage].tags.size()==2 && disk.pages[state.activePage].status==2,"Unified details autosave ticket, status and tags to the original client");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"file:///bad");SendMessageW(panel,WM_TIMER,1,0);
  Check(client_notes::ReadNotebook(aRoot).pages[state.activePage].ticketUrl==L"https://tickets.example.com/48301" && NotesWindowText(GetDlgItem(panel,IDC_NOTES_DETAILS_HINT)).find(L"Not saved")!=std::wstring::npos,"Invalid details show an explanation without overwriting valid metadata");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"https://tickets.example.com/48301");SendMessageW(panel,WM_ACTIVATE,WA_INACTIVE,reinterpret_cast<LPARAM>(owner));PumpPanelMessages();
  Check(!IsWindow(panel) && !state.panel,"Clicking outside Details closes it and flushes pending valid changes");
  Command(owner,IDC_NOTES_DETAILS);panel=state.panel;SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"https://unfinished");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"unfinished");CloseNotesPanel(state);
  Check(state.detailsDrafts.size()==1 && client_notes::ReadNotebook(aRoot).pages[state.activePage].ticketUrl==L"https://tickets.example.com/48301","Outside dismissal retains an invalid details draft without changing saved metadata");
  Command(owner,IDC_NOTES_DETAILS);panel=state.panel;
  Check(NotesWindowText(GetDlgItem(panel,IDC_NOTES_INPUT2))==L"unfinished","Reopening Details restores its unfinished invalid field");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT2,L"https://tickets.example.com/48301");CloseNotesPanel(state);
  Check(state.detailsDrafts.empty(),"Correcting Details clears its retained draft");
  Command(owner,IDC_NOTES_DETAILS);panel=state.panel;const std::wstring longTicket(100,L'A');
  SetDlgItemTextW(panel,IDC_NOTES_INPUT1,longTicket.c_str());SendMessageW(panel,WM_TIMER,1,0);
  Check(client_notes::ReadNotebook(aRoot).pages[state.activePage].ticket==longTicket,"Details preserves the existing 128-character ticket-reference allowance");
  SetDlgItemTextW(panel,IDC_NOTES_INPUT1,L"48301");CloseNotesPanel(state);
  Command(owner,IDC_NOTES_MORE);panel=state.panel;
  Check(IsWindow(panel) && IsWindowEnabled(owner),"More actions is searchable in a temporary panel");
  SetDlgItemTextW(panel,IDC_NOTES_QUERY,L"handover");
  auto &actions=*reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(panel,DWLP_USER));
  Check(actions.actionState.results.size()==1 && kNotesActions[actions.actionState.results.front()].id==IDC_NOTES_HANDOVER,"More action search finds a specific action and its explanation");
  SendMessageW(panel,WM_ACTIVATE,WA_INACTIVE,reinterpret_cast<LPARAM>(owner));PumpPanelMessages();
  Check(!IsWindow(panel),"Clicking outside More actions dismisses it");
  for (int i=0;i<100;++i) Fixture(L"Search fixture "+std::to_wstring(i));
  const auto pins=g_pinnedClients;g_pinnedClients={L"Inspector B"};
  Command(owner,IDC_NOTES_SWITCH_CLIENT);panel=state.panel;
  Check(IsWindow(panel) && SendDlgItemMessageW(panel,IDC_NOTES_CLIENT_MATCHES,LB_GETCOUNT,0,0)==8,"A hundred clients produces a short eight-result panel");
  auto &picker=*reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(panel,DWLP_USER));
  Check(picker.matches.front()==L"Inspector B","Pinned clients appear first without scrolling");
  SetDlgItemTextW(panel,IDC_NOTES_CLIENT_SEARCH,L"Search fixture 99");
  Check(picker.matches.size()==1 && picker.matches.front()==L"Search fixture 99","Typing searches every client beyond the initial shortlist");
  SendMessageW(panel,WM_ACTIVATE,WA_INACTIVE,reinterpret_cast<LPARAM>(owner));PumpPanelMessages();
  Check(!IsWindow(panel) && state.clientName==L"Inspector A","Clicking outside Switch client closes it without switching");
  Command(owner,IDC_NOTES_SWITCH_CLIENT);panel=state.panel;MSG key{GetDlgItem(panel,IDC_NOTES_CLIENT_SEARCH),WM_KEYDOWN,VK_ESCAPE};
  Check(ProcessNotesPanelMessage(key) && !IsWindow(panel),"Escape dismisses a temporary panel");g_pinnedClients=pins;
  // Store B with the same local note ID as A: cross-client search must not use
  // local IDs as global keys or mutation targets.
  const auto bRoot=g_sDataDir/L"Sites"/L"Inspector B";
  client_notes::NotePage b{L"B independent",true,NotesPlainRtf(L"Unique B content"),{}};b.id=state.pages[state.activePage].id;b.created=b.modified=client_notes::Now();
  Check(client_notes::WriteNotebook(bRoot,{b},client_notes::ReadNotebook(bRoot)),"Independent client notebook accepts colliding local note IDs");
  const auto aBytes=client_notes::ReadNotebook(aRoot).notebook_snapshot.bytes,bBytes=client_notes::ReadNotebook(bRoot).notebook_snapshot.bytes;
  NotesLibrary lib{owner,&state};HWND manager=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&lib));
  {
    HWND scope=GetDlgItem(manager,IDC_NOTES_SCOPE);HDC screen=GetDC(scope),buffer=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,320,32);auto old=SelectObject(buffer,bitmap);
    RECT row{0,0,320,32};FillRect(buffer,&row,reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    DRAWITEMSTRUCT draw{ODT_COMBOBOX,IDC_NOTES_SCOPE,1,ODA_DRAWENTIRE,0,scope,buffer,row,0};
    SendMessageW(manager,WM_DRAWITEM,IDC_NOTES_SCOPE,reinterpret_cast<LPARAM>(&draw));
    bool visibleText=false;for(int y=3;y<29 && !visibleText;++y)for(int x=12;x<280;++x)if(GetPixel(buffer,x,y)!=g_themeColors.crControl){visibleText=true;break;}
    Check(GetPixel(buffer,2,2)==g_themeColors.crControl && visibleText,"All clients dropdown paints readable options on the Notes theme instead of a blank white box");
    SelectObject(buffer,old);DeleteObject(bitmap);DeleteDC(buffer);ReleaseDC(scope,screen);
  }
  SendDlgItemMessageW(manager,IDC_NOTES_SCOPE,CB_SETCURSEL,1,0);SendMessageW(manager,WM_COMMAND,MAKEWPARAM(IDC_NOTES_SCOPE,CBN_SELCHANGE),0);
  Check(lib.allClients && lib.readOnly && lib.state!=&state && lib.origins.size()==lib.state->pages.size(),"All clients uses a separate read-only preview snapshot with explicit ownership");
  SetDlgItemTextW(manager,IDC_NOTES_QUERY,L"Unique B content");NotesLibraryFill(manager,lib);
  Check(lib.results.size()==1 && lib.origins[lib.results.front()].first==L"Inspector B" && NotesWindowText(GetDlgItem(manager,IDC_NOTES_INFO)).find(L"Client: Inspector B")!=std::wstring::npos,"Combined search resolves duplicate IDs to the right client and shows ownership");
  for (auto action:{IDC_NOTES_ARCHIVE,IDC_NOTES_DELETE,IDC_NOTES_RESTORE,IDC_NOTES_EMPTY_TRASH}) Command(manager,action);
  Check(client_notes::ReadNotebook(aRoot).notebook_snapshot.bytes==aBytes && client_notes::ReadNotebook(bRoot).notebook_snapshot.bytes==bBytes,"Cross-client preview cannot mutate either notebook even through direct command messages");
  SetDlgItemTextW(manager,IDC_NOTES_QUERY,L"");SendDlgItemMessageW(manager,IDC_NOTES_SCOPE,CB_SETCURSEL,0,0);SendMessageW(manager,WM_COMMAND,MAKEWPARAM(IDC_NOTES_SCOPE,CBN_SELCHANGE),0);
  Check(!lib.readOnly && lib.state==&state && lib.selected.empty(),"Returning to This client restores its notebook and clears selections");
  const auto dpi=GetDpiForWindow(manager);SetWindowPos(manager,nullptr,0,0,ScaleByDpi(1080,dpi),ScaleByDpi(720,dpi),SWP_NOMOVE|SWP_NOZORDER);DestroyWindow(manager);
  Check(GetPrivateProfileIntW(L"notes_library",L"width",0,g_sConfigPath.c_str())==1080 && GetPrivateProfileIntW(L"notes_library",L"height",0,g_sConfigPath.c_str())==720,"All notes remembers its dimensions in DPI-independent units");
  Type(owner,L" - pending before switch");OpenRequestedNotesClient(reinterpret_cast<LPARAM>(new NotesClientRequest{L"Inspector B",0}));
  owner=g_hNotesWindow;Check(owner && State(owner).clientName==L"Inspector B" && NotesPlain(client_notes::ReadNotebook(aRoot).pages.back()).find(L"pending before switch")!=std::wstring::npos,"Switching clients flushes pending edits to the source before opening the destination");
  CloseClientNotesDialog(owner,State(owner));owner=Open(L"Inspector A");Type(owner,L" save must fail");
  auto newer=State(owner).original.pages;newer.front().ticket=L"Other writer";Check(client_notes::WriteNotebook(aRoot,newer,State(owner).original),"Concurrent source edit creates a switch conflict");
  OpenRequestedNotesClient(reinterpret_cast<LPARAM>(new NotesClientRequest{L"Inspector B",0}));
  Check(g_hNotesWindow==owner && State(owner).clientName==L"Inspector A" && State(owner).saveFailed,"A failed source save refuses the client switch and keeps its draft");DestroyWindow(owner);
  // Exercise production client deletion, then re-read B byte for byte.
  const auto retained=client_notes::ReadNotebook(bRoot).notebook_snapshot.bytes;
  std::wstring deleteOutcome;
  Check(DeleteEntireClient(L"Inspector A",true,std::nullopt,deleteOutcome),"Production client deletion succeeds for the owned disposable client");
  Check(client_notes::ReadNotebook(bRoot).notebook_snapshot.bytes==retained,"Removing one client container leaves the other client's notebook unchanged");
  OpenRequestedNotesClient(reinterpret_cast<LPARAM>(new NotesClientRequest{L"Inspector A",0}));
  Check(!g_hNotesWindow,"A stale picker selection cannot reopen a deleted client");
}
static void NotesResizeWrapping() {
  Fixture(L"Wrap QA");HWND owner=Open(L"Wrap QA");Command(owner,IDC_NOTES_ADD_TAB);
  Type(owner,L"Two new office printers are ready for installation. Existing queues on PRINT01 remain in service while the vendor confirms the rollout window. Two new office printers are ready for installation. Existing queues on PRINT01 remain in service while the vendor confirms the rollout window. Two new office printers are ready for installation. Existing queues on PRINT01 remain in service while the vendor confirms the rollout window.");Save(owner);
  const auto dpi=GetDpiForWindow(owner);
  ShowWindow(owner,SW_SHOWNOACTIVATE);
  SetWindowPos(owner,nullptr,0,0,ScaleByDpi(1500,dpi),ScaleByDpi(700,dpi),SWP_NOMOVE|SWP_NOZORDER);
  RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  const auto wide=SendMessageW(Edit(owner),EM_GETLINECOUNT,0,0);
  RECT wideArea{},wideFormat{};GetClientRect(Edit(owner),&wideArea);SendMessageW(Edit(owner),EM_GETRECT,0,reinterpret_cast<LPARAM>(&wideFormat));
  SetWindowPos(owner,nullptr,0,0,ScaleByDpi(900,dpi),ScaleByDpi(700,dpi),SWP_NOMOVE|SWP_NOZORDER);
  RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  const auto narrow=SendMessageW(Edit(owner),EM_GETLINECOUNT,0,0);
  RECT format{},area{};SendMessageW(Edit(owner),EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));GetClientRect(Edit(owner),&area);
  std::cout<<"Wrapping: "<<wide<<" wide, "<<narrow<<" narrow; rectangle "<<format.right<<" / "<<area.right<<"; wide rectangle "<<wideFormat.right<<" / "<<wideArea.right<<"; options "<<SendMessageW(Edit(owner),EM_GETOPTIONS,0,0)<<std::endl;
  Check(narrow>wide && format.right<area.right,"Narrowing Notes reflows long paragraphs inside its padded editor");
  Command(owner,IDC_NOTES_ADD_TAB);
  NotesCharacterFormat(owner,State(owner),CFM_SIZE,0,0,220);
  Type(owner,L"Two new office printers are ready for installation. Existing queues on PRINT01 remain in service while the vendor confirms the rollout window.");
  const auto prose=NotesWindowText(Edit(owner));
  SetWindowPos(owner,nullptr,0,0,ScaleByDpi(1120,dpi),ScaleByDpi(960,dpi),SWP_NOMOVE|SWP_NOZORDER);
  RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  // Calibrate the fixture to fit this host's wide editor but cross its narrow
  // edge. Font metrics and DPI virtualization vary between native test hosts.
  RECT wideProseRect{};SendMessageW(Edit(owner),EM_GETRECT,0,reinterpret_cast<LPARAM>(&wideProseRect));
  bool calibrated=false;
  for(LONG height=200;height<=400;height+=5) {
    Select(Edit(owner),0,-1);NotesCharacterFormat(owner,State(owner),CFM_SIZE,0,0,height);
    RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    POINT end{};SendMessageW(Edit(owner),EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&end),prose.size()-1);
    if(SendMessageW(Edit(owner),EM_GETLINECOUNT,0,0)==1 && end.x>wideProseRect.right-ScaleByDpi(100,dpi)+10 && end.x<wideProseRect.right-12) {calibrated=true;break;}
  }
  const auto beforeSmallDrag=SendMessageW(Edit(owner),EM_GETLINECOUNT,0,0);
  for(int width=1110;width>=1020;width-=10) {
    SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,dpi),ScaleByDpi(width-160,dpi),SWP_NOMOVE|SWP_NOZORDER);
    RedrawWindow(owner,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
  }
  std::cout<<"Small drag wrapping: "<<beforeSmallDrag<<" -> "<<SendMessageW(Edit(owner),EM_GETLINECOUNT,0,0)<<"; DPI "<<dpi<<"; text preserved "<<(NotesWindowText(Edit(owner))==prose)<<std::endl;
  Check(calibrated && SendMessageW(Edit(owner),EM_GETLINECOUNT,0,0)>beforeSmallDrag && NotesWindowText(Edit(owner))==prose,"Incremental small drag reflows a single short paragraph without changing its text");
  CloseClientNotesDialog(owner,State(owner));
}
struct NoteTransitionCounts {
  unsigned visibleStreams=0,prematureEditors=0,newEditors=0;
};
static LRESULT CALLBACK ObserveNoteTransition(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  auto &counts=*reinterpret_cast<NoteTransitionCounts *>(data);
  if (message==EM_STREAMIN && (GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE)) ++counts.visibleStreams;
  if (message==WM_WINDOWPOSCHANGED && (GetWindowLongPtrW(window,GWL_STYLE)&WS_VISIBLE) && GetDlgCtrlID(window)==IDC_CLIENT_NOTES_TEXT) {
    RECT area{};GetClientRect(window,&area);
    if (area.right<ScaleByDpi(500,GetDpiForWindow(window)) || area.bottom<ScaleByDpi(150,GetDpiForWindow(window))) ++counts.prematureEditors;
  }
  if (message==WM_PARENTNOTIFY && LOWORD(w)==WM_CREATE) {
    HWND child=reinterpret_cast<HWND>(l);wchar_t name[32]{};GetClassNameW(child,name,32);
    if (!wcscmp(name,L"RICHEDIT50W")) {
      ++counts.newEditors;
      if (GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE) ++counts.prematureEditors;
      SetWindowSubclass(child,ObserveNoteTransition,id,data);
    }
  }
  if (message==WM_NCDESTROY) RemoveWindowSubclass(window,ObserveNoteTransition,id);
  return DefSubclassProc(window,message,w,l);
}
static void AddDeleteTransitions(HWND owner) {
  auto &workspace=State(owner);
  for (int i=0;i<3;++i) {Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Neighbor selection fixture");Save(owner);}
  const auto left=workspace.pages[workspace.visiblePages[0]].id,middle=workspace.pages[workspace.visiblePages[1]].id;
  Command(owner,IDC_NOTES_DELETE);Check(workspace.pages[workspace.activePage].id==middle,"Deleting the rightmost tab selects its neighbor instead of jumping to the first tab");
  Command(owner,IDC_NOTES_CLOSE_TAB);Check(workspace.pages[workspace.activePage].id==left,"Closing the active middle tab selects its left neighbor");
  Command(owner,IDC_NOTES_CLOSE_ALL);
  Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Keep this document and its undo history");Save(owner);
  const HWND kept=Edit(owner);const auto text=NotesWindowText(kept);
  NoteTransitionCounts counts;
  SetWindowSubclass(owner,ObserveNoteTransition,27,reinterpret_cast<DWORD_PTR>(&counts));
  SetWindowSubclass(kept,ObserveNoteTransition,27,reinterpret_cast<DWORD_PTR>(&counts));
  SetWindowSubclass(GetDlgItem(owner,IDC_NOTES_TABS),ObserveNoteTransition,27,reinterpret_cast<DWORD_PTR>(&counts));
  std::vector<double> times,removeTimes;
  for (int i=0;i<12;++i) {
    auto start=std::chrono::steady_clock::now();Command(owner,IDC_NOTES_ADD_TAB);
    times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    const HWND transient=Edit(owner);
    Check(transient!=kept && (GetWindowLongPtrW(transient,GWL_STYLE)&WS_VISIBLE),"Added note exposes only its fully sized editor");
    if (i%2) {Type(owner,L"Disposable deletion test");Save(owner);}
    start=std::chrono::steady_clock::now();Command(owner,i%2 ? IDC_NOTES_DELETE : IDC_NOTES_CLOSE_TAB);
    removeTimes.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    Check(Edit(owner)==kept && NotesWindowText(kept)==text,"Delete or close switches back to the cached document without restreaming it");
    if (!(i%2)) Check(!IsWindow(transient),"Pristine editor retires after the replacement is ready");
  }
  Check(counts.newEditors==12 && counts.visibleStreams==0 && counts.prematureEditors==0,"Repeated add/delete never streams visible content or shows a temporary 100x100 editor");
  std::sort(times.begin(),times.end());std::cout<<"Add-note timings (ms): median="<<times[times.size()/2]<<", max="<<times.back()<<std::endl;
  std::sort(removeTimes.begin(),removeTimes.end());std::cout<<"Delete/close-note timings (ms): median="<<removeTimes[removeTimes.size()/2]<<", max="<<removeTimes.back()<<std::endl;
  Command(owner,IDC_NOTES_CLOSE_TAB);
  Check(!HasActiveNote(workspace) && !workspace.attachedEditor && (GetWindowLongPtrW(GetDlgItem(owner,IDC_NOTES_EMPTY),GWL_STYLE)&WS_VISIBLE),"Closing the last tab reveals a clean empty state");
  RemoveWindowSubclass(owner,ObserveNoteTransition,27);
  RemoveWindowSubclass(GetDlgItem(owner,IDC_NOTES_TABS),ObserveNoteTransition,27);
  for (const auto &[id,edit] : workspace.editors) RemoveWindowSubclass(edit,ObserveNoteTransition,27);
  Save(owner);
}
static void DirectTemplateEditing(HWND owner) {
  auto &workspace=State(owner);NotesTemplates library{owner,&workspace};Check(NotesLoadTemplates(library),"Direct template library loads with no client note open");
  const auto count=workspace.pages.size();
  HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&library));
  Check(!(SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_GETOPTIONS,0,0)&ECO_READONLY) && !IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_SAVE_TEMPLATE)),"Templates can be authored directly without an open client note");
  Command(dialog,IDC_NOTES_ARCHIVE);
  Check(!library.draft.id && library.dirty && workspace.pages.size()==count,"New template starts a draft without creating a client note");
  CHARFORMAT2W blankFormat{sizeof(blankFormat)};SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&blankFormat));
  Check(blankFormat.yHeight==240 && !(blankFormat.dwEffects&CFE_BOLD),"Blank templates start in 12-point normal text rather than inheriting a prior heading");
  PARAFORMAT2 blankParagraph{sizeof(blankParagraph)};SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&blankParagraph));
  Check(blankParagraph.dySpaceBefore==0 && blankParagraph.dySpaceAfter==0,"Blank template drafts have no added paragraph gaps");
  blankParagraph.dwMask=PFM_SPACEBEFORE|PFM_SPACEAFTER|PFM_LINESPACING;blankParagraph.dySpaceBefore=120;blankParagraph.dySpaceAfter=80;blankParagraph.bLineSpacingRule=2;
  SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&blankParagraph));
  SendDlgItemMessageW(dialog,IDC_NOTES_STYLE,CB_SETCURSEL,0,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_STYLE,CBN_SELENDOK),0);
  SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&blankParagraph));
  Check(blankParagraph.dySpaceBefore==0 && blankParagraph.dySpaceAfter==0 && blankParagraph.bLineSpacingRule==5 && blankParagraph.dyLineSpacing==20,"Reselecting Normal text resets template paragraph and line spacing");
  Check(OpenClipboard(dialog)!=FALSE,"Template clipboard fixture opens");EmptyClipboard();
  const wchar_t paste[]=L"Pasted Unicode template \x4e2d\x6587";HGLOBAL clipboard=GlobalAlloc(GMEM_MOVEABLE,sizeof(paste));
  memcpy(GlobalLock(clipboard),paste,sizeof(paste));GlobalUnlock(clipboard);SetClipboardData(CF_UNICODETEXT,clipboard);CloseClipboard();
  SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,WM_PASTE,0,0);
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==paste && library.dirty,"Template editor accepts bounded plain-text paste despite embedded-object protection");
  BYTE priorKeys[256]{},keys[256]{};GetKeyboardState(priorKeys);memcpy(keys,priorKeys,256);keys[VK_CONTROL]=0x80;
  SetWindowTextW(GetDlgItem(dialog,IDC_NOTES_PREVIEW),L"");SetKeyboardState(keys);SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,WM_KEYDOWN,'V',0);SetKeyboardState(priorKeys);
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==paste,"Ctrl+V uses the template's bounded paste route rather than RichEdit's blocked internal route");
  SetWindowTextW(GetDlgItem(dialog,IDC_NOTES_PREVIEW),L"");memcpy(keys,priorKeys,256);keys[VK_SHIFT]=0x80;
  SetKeyboardState(keys);SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,WM_KEYDOWN,VK_INSERT,0);SetKeyboardState(priorKeys);
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==paste,"Shift+Insert pastes reusable template text");
  SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_NAME,L"Direct template QA");SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_HINT,L"Use during a maintenance handover");
  HWND body=GetDlgItem(dialog,IDC_NOTES_PREVIEW);SetWindowTextW(body,L"Template heading\r\nOwner:\r\nNext action:");
  Select(body,0,16);SendDlgItemMessageW(dialog,IDC_NOTES_STYLE,CB_SETCURSEL,1,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_STYLE,CBN_SELCHANGE),0);
  CHARFORMAT2W format{sizeof(format)};SendMessageW(body,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
  Check(format.yHeight==480 && (format.dwEffects&CFE_BOLD),"Direct editor applies real heading formatting");
  Check(NotesTemplateEditorSave(dialog,library) && library.draft.id && !library.dirty,"Direct formatted template persists independently");
  const auto id=library.draft.id;const auto committed=library.draft.rtf;const auto plain=NotesWindowText(body);
  Select(body,-1,-1);SendMessageW(body,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"\r\nUnsaved draft"));
  g_bThemeIsDark=false;BuildThemeColors(false,g_themeColors);UpdateThemeBrushes();RefreshNotesLibraryForTheme(dialog);
  Check(library.dirty && NotesWindowText(body).find(L"Unsaved draft")!=std::wstring::npos,"Theme change keeps unsaved template content");
  g_bThemeIsDark=true;BuildThemeColors(true,g_themeColors);UpdateThemeBrushes();RefreshNotesLibraryForTheme(dialog);
  answers.push_back(IDCANCEL);SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_SETCURSEL,0,0);SendMessageW(dialog,WM_COMMAND,MAKEWPARAM(IDC_NOTES_RESULTS,LBN_SELCHANGE),0);
  Check(library.draft.id==id && library.dirty && SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0)==library.selected,"Cancel switching templates keeps the draft and list selection");
  answers.push_back(IDNO);Command(dialog,IDC_NOTES_ARCHIVE);
  Check(!library.draft.id && library.draft.rtf.find("Unsaved draft")==std::string::npos,"Discard opens a fresh template without saving unwanted changes");
  SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_NAME,library.pages.front().name.c_str());
  Check(!NotesTemplateEditorSave(dialog,library) && library.dirty && !library.draft.id,"Duplicate template names retain the editable draft");
  SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_NAME,L"Use immediately QA");SetWindowTextW(body,L"Save and use this template in one place");
  answers.push_back(IDYES);Check(NotesTemplateEditorLeave(dialog,library) && !library.dirty,"Save on navigation commits the template draft");
  const auto pageCount=workspace.pages.size();SetWindowTextW(body,L"Latest edit before Use");Command(dialog,IDC_NOTES_TEMPLATE_USE);
  Check(workspace.pages.size()==pageCount+1 && NotesWindowText(Edit(owner))==L"Latest edit before Use" && workspace.pages.back().ticket.empty(),"Use template saves the latest editor changes and creates an independent client note");
  Select(Edit(owner),-1,-1);memcpy(keys,priorKeys,256);keys[VK_CONTROL]=0x80;SetKeyboardState(keys);
  SendMessageW(Edit(owner),WM_KEYDOWN,'V',0);SetKeyboardState(priorKeys);
  Check(NotesWindowText(Edit(owner)).find(paste)!=std::wstring::npos,"Client notes Ctrl+V also uses the bounded plain-text paste route");
  DestroyWindow(dialog);
  NotesTemplates stale{owner,&workspace};Check(NotesLoadTemplates(stale),"Conflict test loads a saved template snapshot");
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&stale));
  SetWindowTextW(GetDlgItem(dialog,IDC_NOTES_PREVIEW),L"Keep this conflicting draft");
  auto newer=stale;auto pages=newer.pages;pages.front().ticket=L"Changed by another window";Check(NotesSaveTemplates(owner,newer,pages),"Concurrent template edit commits safely");
  Check(!NotesTemplateEditorSave(dialog,stale) && stale.dirty && NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==L"Keep this conflicting draft","Failed concurrent save preserves the entire template draft");
  DestroyWindow(dialog);
  NotesTemplates reopened{owner,&workspace};Check(NotesLoadTemplates(reopened),"Direct template library reopens");
  const auto found=std::find_if(reopened.pages.begin(),reopened.pages.end(),[&](const auto &page){return page.id==id;});
  Check(found!=reopened.pages.end() && found->rtf==committed && NotesPlain(*found)==plain,"Formatted template survives reopening without discarded edits");
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&reopened));
  for (int width : {900,1000,1500}) {
    SetWindowPos(dialog,nullptr,0,0,ScaleByDpi(width,GetDpiForWindow(dialog)),ScaleByDpi(600,GetDpiForWindow(dialog)),SWP_NOMOVE|SWP_NOZORDER);
    RECT prior{};bool first=true;for (int button : {IDC_NOTES_ARCHIVE,IDC_NOTES_SAVE_TEMPLATE,IDC_NOTES_DELETE,IDC_NOTES_RESTORE,IDC_NOTES_TEMPLATE_USE,IDCANCEL}) {
      RECT bounds{};GetWindowRect(GetDlgItem(dialog,button),&bounds);Check(first || bounds.left>=prior.right,"Template actions stay separated across window sizes");prior=bounds;first=false;
    }
  }
  Check(SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETITEMHEIGHT,0,0)>=ScaleByDpi(24,GetDpiForWindow(dialog)),"Template list uses readable Notes-sized rows");
  Check(SendDlgItemMessageW(dialog,IDC_NOTES_TEMPLATE_NAME,WM_GETFONT,0,0)==reinterpret_cast<LRESULT>(workspace.uiFont),"Template fields share the workspace's readable font");
  DestroyWindow(dialog);Command(owner,IDC_NOTES_CLOSE_ALL);Save(owner);
}
static void DirectTrashAndCompactHelp(HWND owner) {
  auto &state=State(owner);
  Command(owner,IDC_NOTES_TRASH);Check(openedLibraryFilter==3,"Direct Trash opens the deleted view immediately");
  NotesLibrary trash{owner,&state};trash.initialFilter=3;
  HWND library=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&trash));
  Check(SendDlgItemMessageW(library,IDC_NOTES_FILTER,CB_GETCURSEL,0,0)==3 && NotesWindowText(library).find(L"Trash - ")==0,"Trash window starts with a named Trash filter and caption");
  Check(std::all_of(trash.results.begin(),trash.results.end(),[&](size_t i){return state.pages[i].deleted;}),"Direct Trash excludes kept notes");
  Check((GetWindowLongPtrW(GetDlgItem(library,IDC_NOTES_EMPTY_TRASH),GWL_STYLE)&WS_VISIBLE)!=0,"Direct Trash exposes Empty trash");DestroyWindow(library);
  HWND help=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_HELP),owner,NotesHelpProc,0);
  Check(GetDlgItem(help,IDC_NOTES_TRASH)!=nullptr,"Compact Help offers a direct Trash entry");
  Check(NotesWindowText(GetDlgItem(help,IDC_NOTES_HELP_TICKETS)).find(L"default browser")!=std::wstring::npos,"Quick Help describes default-browser ticket routing");
  Check(NotesWindowText(GetDlgItem(help,IDC_NOTES_HELP_TEMPLATES)).find(L"directly")!=std::wstring::npos,"Quick Help describes direct template editing");DestroyWindow(help);
  Check(std::any_of(std::begin(kNotesActions),std::end(kNotesActions),[](const auto &action){return action.id==IDC_NOTES_TRASH && !action.needsNote;}),"Searchable Trash action works without an open note");
}
static void NotesLinksAndContextMenu(HWND owner) {
  Command(owner,IDC_NOTES_ADD_TAB);auto &state=State(owner);
  const auto count=shellOpens;
  answers.push_back(IDNO);OpenNotesUrl(owner,state,L"https://example.com/ordinary");
  Check(shellOpens==count && lastNotice.find(L"default browser")!=std::wstring::npos,"Ordinary note link explains the PC browser and cancel does not launch");
  answers.push_back(IDYES);OpenNotesUrl(owner,state,L"https://example.com/ordinary");
  Check(shellOpens==count+1 && shellTarget==L"https://example.com/ordinary","Ordinary note link uses Windows association without client-profile arguments");
  OpenNotesUrl(owner,state,L"file:///unsafe");Check(shellOpens==count+1,"Ordinary note links reject non-web URLs");
  Check(OpenClipboard(owner)!=FALSE,"Context menu clipboard fixture opens");EmptyClipboard();
  const wchar_t text[]=L"Right-click pasted \x4e2d\x6587";HGLOBAL clip=GlobalAlloc(GMEM_MOVEABLE,sizeof(text));
  memcpy(GlobalLock(clip),text,sizeof(text));GlobalUnlock(clip);SetClipboardData(CF_UNICODETEXT,clip);CloseClipboard();
  const auto menus=popupCalls;popupAction=5;SendMessageW(Edit(owner),WM_RBUTTONUP,0,MAKELPARAM(20,20));
  if (!(popupCalls==menus+1 && popupPasteEnabled && !popupCutEnabled && NotesWindowText(Edit(owner))==text)) std::cout << "Context fixture: calls " << popupCalls-menus << " paste " << popupPasteEnabled << " cut " << popupCutEnabled << " text length " << NotesWindowText(Edit(owner)).size() << std::endl;
  Check(popupCalls==menus+1 && popupPasteEnabled && !popupCutEnabled && NotesWindowText(Edit(owner))==text,"Actual right-button message exposes Paste and inserts Unicode text");
  Select(Edit(owner),0,5);SendMessageW(Edit(owner),WM_CONTEXTMENU,reinterpret_cast<WPARAM>(Edit(owner)),MAKELPARAM(-1,-1));
  CHARRANGE selection{};NotesSelection(Edit(owner),selection);
  Check(popupCutEnabled && selection.cpMin==0 && selection.cpMax==5,"Keyboard context menu keeps the selection and enables editing actions");
  Save(owner);Command(owner,IDC_NOTES_CLOSE_ALL);Save(owner);
}
static void EmptyDraftRestart() {
  Fixture(L"Empty draft QA");HWND owner=Open(L"Empty draft QA");
  Command(owner,IDC_NOTES_ADD_TAB);auto &state=State(owner);state.pages[state.activePage].name=L"Planned checklist";state.metadataChanged=true;
  Command(owner,IDC_NOTES_ADD_TAB);state.pages[state.activePage].status=2;state.metadataChanged=true;
  Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Written content");state.pages[state.activePage].name=L"Written work";state.metadataChanged=true;
  Command(owner,IDC_NOTES_ADD_TAB);Command(owner,IDC_NOTES_ADD_TAB);
  CloseClientNotesDialog(owner,state);Check(!IsWindow(owner),"Closing Notes completes after pruning unused blank drafts");
  owner=Open(L"Empty draft QA");auto &restored=State(owner);
  Check(std::none_of(restored.pages.begin(),restored.pages.end(),NotesIsUnusedBlankDraft),"Unused blank Untitled drafts do not accumulate across restart");
  Check(std::any_of(restored.pages.begin(),restored.pages.end(),[](const auto &p){return p.name==L"Planned checklist";}),"Named blank notes survive draft cleanup");
  Check(std::any_of(restored.pages.begin(),restored.pages.end(),[](const auto &p){return p.status==2;}),"Blank notes with meaningful metadata survive cleanup");
  Check(std::any_of(restored.pages.begin(),restored.pages.end(),[](const auto &p){return p.name==L"Written work" && NotesPlain(p)==L"Written content";}),"Written notes survive draft cleanup");
  Command(owner,IDC_NOTES_ADD_TAB);Check(State(owner).pages[State(owner).activePage].name==L"Untitled 2","New blank draft reuses the lowest available name while preserving the metadata note named Untitled");
  CloseClientNotesDialog(owner,State(owner));
}
static void CompactNormalSpacing() {
  Fixture(L"Spacing QA");HWND owner=Open(L"Spacing QA");Command(owner,IDC_NOTES_ADD_TAB);
  HWND edit=Edit(owner);CHARFORMAT2W font{sizeof(font)};SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&font));
  PARAFORMAT2 paragraph{sizeof(paragraph)};SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  Check(font.yHeight==240,"New blank notes use 12-point normal text");
  Check(paragraph.dySpaceBefore==0 && paragraph.dySpaceAfter==0 && paragraph.bLineSpacingRule==5 && paragraph.dyLineSpacing==20,"New notes have single spacing and no added paragraph gaps");
  Type(owner,L"First line\r\n\r\nSecond line\r\n\r\nThird line");const auto text=NotesWindowText(edit);
  Select(edit,0,-1);paragraph.dwMask=PFM_SPACEBEFORE|PFM_SPACEAFTER|PFM_LINESPACING;paragraph.dySpaceBefore=120;paragraph.dySpaceAfter=80;paragraph.bLineSpacingRule=2;
  SendMessageW(edit,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  const auto lineY=[&](LONG line) {POINTL point{};const auto first=SendMessageW(edit,EM_LINEINDEX,line,0);SendMessageW(edit,EM_POSFROMCHAR,reinterpret_cast<WPARAM>(&point),first);return point.y;};
  const auto spacious=lineY(2)-lineY(0);
  SendDlgItemMessageW(owner,IDC_NOTES_STYLE,CB_SETCURSEL,0,0);SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_NOTES_STYLE,CBN_SELENDOK),0);
  SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  Check(paragraph.dySpaceBefore==0 && paragraph.dySpaceAfter==0 && paragraph.bLineSpacingRule==5 && paragraph.dyLineSpacing==20,"Choosing the already-selected Normal text removes saved paragraph and double-line spacing");
  Check(lineY(2)-lineY(0)<spacious && lineY(1)-lineY(0)==lineY(2)-lineY(1),"Actual editor positions tighten while keeping blank lines one line high");
  Check(NotesWindowText(edit)==text,"Tightening spacing preserves every character and blank line");
  Select(edit,0,10);SendDlgItemMessageW(owner,IDC_NOTES_STYLE,CB_SETCURSEL,1,0);SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_NOTES_STYLE,CBN_SELCHANGE),0);
  Select(edit,0,0);SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&font));
  Check(font.yHeight==560 && paragraph.dySpaceAfter==200,"Heading size and paragraph spacing remain unchanged");
  Select(edit,0,10);SendDlgItemMessageW(owner,IDC_NOTES_STYLE,CB_SETCURSEL,2,0);SendMessageW(owner,WM_COMMAND,MAKEWPARAM(IDC_NOTES_STYLE,CBN_SELCHANGE),0);
  Select(edit,0,0);SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  Check(paragraph.dySpaceBefore==240 && paragraph.dySpaceAfter==160,"Subheading paragraph spacing remains unchanged");
  Select(edit,0,-1);Command(owner,IDC_NOTES_CLEAR);SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&font));
  Check(font.yHeight==240 && paragraph.dySpaceBefore==0 && paragraph.dySpaceAfter==0,"Clear formatting restores compact 12-point normal text");
  Save(owner);CloseClientNotesDialog(owner,State(owner));owner=Open(L"Spacing QA");edit=Edit(owner);Select(edit,0,-1);
  SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  Check(NotesWindowText(edit)==text && paragraph.dySpaceBefore==0 && paragraph.dySpaceAfter==0,"Compact spacing and original blank lines survive notebook close and reopen");
  client_notes::NotePage plain{L"Captured text",true,NotesPlainRtf(L"Capture\n\nNext line"),{}};NotesAddPage(owner,State(owner),std::move(plain));edit=Edit(owner);Select(edit,0,-1);
  SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&font));SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  Check(font.yHeight==240 && paragraph.dySpaceBefore==0 && paragraph.dySpaceAfter==0,"Generated plain-text notes use compact spacing and 12-point text");
  CloseClientNotesDialog(owner,State(owner));
}
static void OldNotesSpacingUpgrade() {
  const auto seed=[&](const std::wstring &name) {
    Fixture(name);const auto root=g_sDataDir/L"Sites"/name;
    client_notes::NotePage page{L"Existing note",true,
      "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs28\\pard\\sb0\\sa80\\sl240\\slmult1 Body one\\par\\par Body two\\par "
      "\\pard\\sb0\\sa200\\fs56\\b Heading\\b0\\par "
      "\\pard\\sb240\\sa160\\fs32 Subheading\\par "
      "\\pard\\sb0\\sa80\\sl480\\slmult1\\fs28 Double spaced\\par "
      "\\pard\\sb120\\sa80\\sl240\\slmult1 Before gap\\par "
      "\\pard\\sb0\\sa100 Custom after}",{}};
    page.id=1;page.open=page.active=true;page.cursor=5;page.zoom=125;
    const auto original=client_notes::ReadNotebook(root);
    Check(client_notes::WriteNotebook(root,{page},original),"Old-spacing notebook fixture saves");return page;
  };
  const auto old=seed(L"Old spacing QA");HWND owner=Open(L"Old spacing QA");HWND edit=Edit(owner);
  const auto paraAt=[&](const wchar_t *word) {
    const auto text=NotesWindowText(edit);const auto offset=text.find(word);
    // GetWindowText uses CRLF, while RichEdit character ranges use one CR.
    const LONG position=static_cast<LONG>(offset-std::count(text.begin(),text.begin()+offset,L'\n'));
    Select(edit,position,position);PARAFORMAT2 p{sizeof(p)};SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&p));return p;
  };
  CHARRANGE cursor{};NotesSelection(edit,cursor);
  int numerator=0,denominator=0;SendMessageW(edit,EM_GETZOOM,reinterpret_cast<WPARAM>(&numerator),reinterpret_cast<LPARAM>(&denominator));
  Check(cursor.cpMin==5 && numerator*100==125*denominator,"Old-note migration restores the remembered caret and zoom");
  Check(paraAt(L"Body one").dySpaceAfter==0 && paraAt(L"Body two").dySpaceAfter==0,"Existing body paragraphs automatically lose the old four-point gap");
  Select(edit,0,0);CHARFORMAT2W font{sizeof(font)};SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&font));
  Check(font.yHeight==280,"Old-note spacing migration preserves the chosen font size");
  Check(paraAt(L"Heading").dySpaceAfter==200 && paraAt(L"Subheading").dySpaceBefore==240 && paraAt(L"Subheading").dySpaceAfter==160,"Old-note migration preserves heading and subheading spacing");
  Check(paraAt(L"Double spaced").dySpaceAfter==80 && paraAt(L"Before gap").dySpaceBefore==120 && paraAt(L"Custom after").dySpaceAfter==100,"Custom paragraph and line spacing is kept");
  const auto text=NotesWindowText(edit);Check(text.find(L"Body one\r\n\r\nBody two")!=std::wstring::npos,"Old-note migration keeps explicit blank lines");
  Check(State(owner).pages.front().history.size()==1 && State(owner).pages.front().history.front().rtf==old.rtf,"The original formatted note is retained in version history before migration");
  SendMessageW(owner,WM_TIMER,1,0);Check(!State(owner).saveFailed && !State(owner).metadataChanged,"Automatic spacing upgrade uses normal autosave");
  CloseClientNotesDialog(owner,State(owner));owner=Open(L"Old spacing QA");edit=Edit(owner);
  Check(NotesWindowText(edit)==text && State(owner).pages.front().history.size()==1 && !State(owner).metadataChanged,"Migrated spacing persists and reopening creates no duplicate revisions");
  CloseClientNotesDialog(owner,State(owner));
  seed(L"Spacing conflict QA");owner=Open(L"Spacing conflict QA");auto &state=State(owner);
  auto concurrent=state.original.pages;concurrent.front().ticket=L"Concurrent writer";
  Check(client_notes::WriteNotebook(state.clientRoot,concurrent,state.original),"Concurrent edit arrives before migration autosave");
  Check(!SaveClientNotesDraft(owner,state,false) && state.saveFailed && client_notes::ReadNotebook(state.clientRoot).pages.front().ticket==L"Concurrent writer","Spacing migration cannot overwrite a concurrent notebook change");
  Check(state.pages.front().history.size()==1 && fs::exists(state.clientRoot/state.draftName),"Failed spacing migration save keeps its draft and original revision");DestroyWindow(owner);
}
static void DirectSnippetEditing() {
  Fixture(L"Snippet editor QA");HWND owner=Open(L"Snippet editor QA");Command(owner,IDC_NOTES_CLOSE_ALL);
  auto &workspace=State(owner);NotesTemplates library{owner,&workspace};library.snippets=true;
  Check(NotesLoadTemplates(library),"Direct snippet library loads");const auto count=workspace.pages.size(),savedBlocks=library.pages.size();
  HWND dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&library));
  Check(IsWindowEnabled(GetDlgItem(owner,IDC_NOTES_SNIPPETS)) && NotesWindowText(GetDlgItem(dialog,IDC_NOTES_ARCHIVE))==L"New snippet","Snippets stays available with a visible New snippet action when no note is open");
  Check(!IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_TEMPLATE_USE)) && !IsWindowEnabled(GetDlgItem(dialog,IDC_NOTES_SAVE_TEMPLATE)),"Snippet authoring does not require a note, while insertion and capture do");
  Command(dialog,IDC_NOTES_ARCHIVE);SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_NAME,L"Direct QA block");SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_HINT,L"Use before handover");
  HWND body=GetDlgItem(dialog,IDC_NOTES_PREVIEW);SetWindowTextW(body,L"Reusable heading\r\nNext step:");Select(body,0,16);Command(dialog,IDC_NOTES_BOLD);
  Check(NotesTemplateEditorSave(dialog,library) && workspace.pages.size()==count,"A formatted snippet can be written and saved directly without creating a note");const auto id=library.draft.id;const auto rtf=library.draft.rtf;
  Select(body,-1,-1);SendMessageW(body,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L" unwanted"));answers.push_back(IDCANCEL);Command(dialog,IDC_NOTES_ARCHIVE);
  Check(library.draft.id==id && library.dirty && NotesWindowText(body).find(L"unwanted")!=std::wstring::npos,"Cancelling snippet navigation keeps unsaved text");DestroyWindow(dialog);
  NotesTemplates reopened{owner,&workspace};reopened.snippets=true;Check(NotesLoadTemplates(reopened) && reopened.pages.back().rtf==rtf,"Saved snippet and formatting survive reopening independently of discarded edits");
  Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Before\r\nAfter");Save(owner);Select(Edit(owner),0,6);
  reopened.startFromNote=true;dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&reopened));
  Check(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==L"Before" && !reopened.draft.id && reopened.dirty,"From selection opens an editable snippet draft containing only selected text");
  SetDlgItemTextW(dialog,IDC_NOTES_TEMPLATE_NAME,L"Captured QA block");SetWindowTextW(GetDlgItem(dialog,IDC_NOTES_PREVIEW),L"Edited captured block");
  const auto pages=workspace.pages.size();Command(dialog,IDC_NOTES_TEMPLATE_USE);
  Check(workspace.pages.size()==pages && NotesWindowText(Edit(owner))==L"Edited captured block\r\nAfter","Insert saves the latest snippet draft and replaces only the selected text without opening a tab");
  const auto inserted=NotesWindowText(Edit(owner));answers.push_back(IDOK);Command(dialog,IDC_NOTES_DELETE);
  Check(NotesWindowText(Edit(owner))==inserted && reopened.pages.size()==savedBlocks+1,"Deleting a saved snippet leaves already inserted text intact");DestroyWindow(dialog);
  NotesTemplates stale{owner,&workspace};stale.snippets=true;Check(NotesLoadTemplates(stale),"Snippet conflict fixture loads");
  dialog=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),owner,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&stale));SetWindowTextW(GetDlgItem(dialog,IDC_NOTES_PREVIEW),L"Conflicting snippet draft");
  auto newer=stale;auto changed=newer.pages;changed.front().ticket=L"Other editor";Check(NotesSaveTemplates(owner,newer,changed),"Another snippet editor commits");
  Check(!NotesTemplateEditorSave(dialog,stale) && stale.dirty && NotesWindowText(GetDlgItem(dialog,IDC_NOTES_PREVIEW))==L"Conflicting snippet draft","Concurrent snippet save keeps the draft and protects newer library content");
  DestroyWindow(dialog);CloseClientNotesDialog(owner,workspace);
}
static std::vector<uint64_t> TabIds(const ClientNotesDialogState &state) {
  std::vector<uint64_t> ids;for(auto index:state.visiblePages) ids.push_back(state.pages[index].id);return ids;
}
static void NotesTabOrdering() {
  Fixture(L"Tab order QA");HWND owner=Open(L"Tab order QA");Command(owner,IDC_NOTES_CLOSE_ALL);
  auto &state=State(owner);
  for(const auto *name:{L"Alpha",L"Beta",L"Gamma"}) {
    client_notes::NotePage page{name,true,NotesPlainRtf(std::wstring(name)+L" content"),{}};
    Check(NotesAddPage(owner,state,std::move(page)),"Tab-order fixture saves");
  }
  const auto initial=TabIds(state);HWND tabs=GetDlgItem(owner,IDC_NOTES_TABS);
  ShowWindow(owner,SW_SHOWNOACTIVATE);
  RECT first{},last{};TabCtrl_GetItemRect(tabs,0,&first);TabCtrl_GetItemRect(tabs,2,&last);
  const auto down=MAKELPARAM(first.left+18,(first.top+first.bottom)/2);
  const auto move=MAKELPARAM(last.left+18,(last.top+last.bottom)/2);
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,down);
  HWND retained=Edit(owner);Select(retained,2,2);Type(owner,L" draft");
  state.pages[state.activePage].ticket=L"QA-173";state.pages[state.activePage].tags={L"handover"};state.metadataChanged=true;
  const auto text=NotesWindowText(retained);
  Check(GetCapture()==tabs && state.dragTab==0,"Visible native tab takes capture after selecting the drag source");
  SendMessageW(tabs,WM_MOUSEMOVE,MK_LBUTTON,move);
  Check(GetCapture()==tabs && state.dragging && state.dragTarget==2,"Native mouse movement retains capture and identifies its drop target");
  RECT area{};GetClientRect(tabs,&area);HDC screen=GetDC(tabs),dc=CreateCompatibleDC(screen);
  HBITMAP bitmap=CreateCompatibleBitmap(screen,area.right,area.bottom);auto old=SelectObject(dc,bitmap);
  SendMessageW(tabs,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
  Check(GetPixel(dc,last.right-1,(last.top+last.bottom)/2)==g_themeColors.crAccent,"Native drag paints a themed insertion marker");
  SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(tabs,screen);
  SendMessageW(tabs,WM_LBUTTONUP,0,move);
  Check(TabIds(state)==std::vector<uint64_t>({initial[1],initial[2],initial[0]}) && state.dragTab==-1 && GetCapture()!=tabs,"Dropping moves the source to the destination and releases capture");
  Check(Edit(owner)==retained && state.pages[state.activePage].id==initial[0] && NotesWindowText(retained)==text && SendMessageW(retained,EM_CANUNDO,0,0),"Reorder preserves active note identity, cached editor, content and undo");
  Check(state.pages[state.activePage].ticket==L"QA-173" && state.pages[state.activePage].tags==std::vector<std::wstring>({L"handover"}),"Reorder keeps ticket and tags attached to their original note");
  bool aligned=true;for(size_t i=0;i<state.pages.size();++i) aligned=aligned && state.baselines[i]==state.pages[i].rtf;
  Check(aligned,"Reorder keeps formatting baselines aligned with their pages");
  const auto reordered=TabIds(state);TabCtrl_GetItemRect(tabs,0,&first);TabCtrl_GetItemRect(tabs,2,&last);
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(first.left+18,15));
  SendMessageW(tabs,WM_LBUTTONUP,0,MAKELPARAM(first.left+18,15));
  Check(TabIds(state)==reordered && state.dragTab==-1 && GetCapture()!=tabs,"A normal tab click does not reorder or retain capture");
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(first.left+18,15));
  SendMessageW(tabs,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(last.left+18,15));SendMessageW(tabs,WM_CANCELMODE,0,0);
  Check(TabIds(state)==reordered && state.dragTarget==-1 && !state.dragging && GetCapture()!=tabs,"Cancelling a drag clears its marker and leaves order intact");
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(first.left+18,15));
  SendMessageW(tabs,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(last.left+18,15));
  SendMessageW(tabs,WM_LBUTTONUP,0,MAKELPARAM(last.left+18,area.bottom+10));
  Check(TabIds(state)==reordered,"Dropping outside the tab row cancels the move");
  POINT context{first.left+18,15};ClientToScreen(tabs,&context);
  popupAction=0;SendMessageW(tabs,WM_CONTEXTMENU,reinterpret_cast<WPARAM>(tabs),MAKELPARAM(context.x,context.y));
  Check(!popupLeftEnabled && popupRightEnabled,"Tab context menu disables moves past the left edge");
  Command(owner,IDC_NOTES_TAB_RIGHT);Check(TabIds(state)==std::vector<uint64_t>({initial[2],initial[1],initial[0]}),"Move-right command changes the selected tab's order");
  Command(owner,IDC_NOTES_CLOSE_TAB);
  const auto closed=initial[1];size_t index=0;while(state.pages[index].id!=closed) ++index;
  NotesLibrary library{owner,&state};HWND manager=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&library));
  const auto row=std::find(library.results.begin(),library.results.end(),index)-library.results.begin();
  SendDlgItemMessageW(manager,IDC_NOTES_RESULTS,LB_SETCURSEL,row,0);Command(manager,IDOK);DestroyWindow(manager);
  Check(TabIds(state).back()==closed && state.pages[state.activePage].id==closed,"Opening a closed middle note through All notes appends its tab");
  const auto reopened=TabIds(state);index=state.activePage;
  Check(NotesOpenSavedPage(owner,state,index) && TabIds(state)==reopened,"Opening an already-open note activates it without moving its tab");
  Command(owner,IDC_NOTES_PIN);Check(TabIds(state).front()==closed && !NotesCanReorderTab(state,0,1),"Pinned tabs stay together and cannot be dragged into the ordinary group");
  Command(owner,IDC_NOTES_CLOSE_TAB);Command(owner,IDC_NOTES_REOPEN);
  Check(TabIds(state).front()==closed && state.pages[state.activePage].pinned,"Reopen-last-closed retains the pinned group");
  CloseClientNotesDialog(owner,state);owner=Open(L"Tab order QA");
  Check(TabIds(State(owner))==std::vector<uint64_t>({closed,initial[2],initial[0]}),"Tab order and pin grouping survive an application restart");
  CloseClientNotesDialog(owner,State(owner));

  Fixture(L"Tab stress QA");owner=Open(L"Tab stress QA");Command(owner,IDC_NOTES_CLOSE_ALL);auto &stress=State(owner);
  std::vector<uint64_t> expected;std::map<uint64_t,std::wstring> contents;
  for(unsigned n=0;n<64;++n) {
    const auto body=L"Unique stress document "+std::to_wstring(n);
    client_notes::NotePage page{L"Stress "+std::to_wstring(n),true,NotesPlainRtf(body),{}};
    if(!NotesAddPage(owner,stress,std::move(page))) throw std::runtime_error("Stress setup save");
    auto &saved=stress.pages.back();saved.pinned=n<8;saved.ticket=L"T-"+std::to_wstring(n);saved.tags={L"stress",std::to_wstring(n)};
    saved.status=n%5;stress.metadataChanged=true;expected.push_back(saved.id);contents[saved.id]=body;
  }
  RefreshNotesTabs(owner,stress);Save(owner);uint32_t random=0x173ab49u;
  const auto next=[&]() {random^=random<<13;random^=random>>17;random^=random<<5;return random;};
  for(unsigned cycle=0;cycle<320;++cycle) {
    const size_t from=next()%expected.size();const bool pinned=from<8;
    if(cycle%3==0) {
      size_t target=pinned ? next()%8 : 8+next()%56;
      if(target!=from && !NotesReorderTab(owner,stress,static_cast<int>(from),static_cast<int>(target))) throw std::runtime_error("Stress reorder save");
      const auto id=expected[from];expected.erase(expected.begin()+from);expected.insert(expected.begin()+target,id);
    } else {
      const auto id=expected[from];size_t page=0;while(stress.pages[page].id!=id) ++page;
      stress.activePage=page;NotesSelectAvailable(owner,stress);Command(owner,IDC_NOTES_CLOSE_TAB);
      expected.erase(expected.begin()+from);page=0;while(stress.pages[page].id!=id) ++page;
      if(!NotesOpenSavedPage(owner,stress,page)) throw std::runtime_error("Stress reopen save");
      expected.insert(expected.begin()+(pinned ? 7 : expected.size()),id);
    }
    if(TabIds(stress)!=expected || !HasActiveNote(stress) || NotesWindowText(Edit(owner))!=contents.at(stress.pages[stress.activePage].id)) throw std::runtime_error("Stress order or document identity diverged");
    for(size_t n=0;n<stress.pages.size();++n) if(contents.count(stress.pages[n].id) && (stress.baselines[n]!=stress.pages[n].rtf || stress.pages[n].tags.size()!=2 || stress.pages[n].ticket.empty())) {
      std::cerr<<"Stress cycle "<<cycle<<", page "<<n<<", baseline="<<(stress.baselines[n]==stress.pages[n].rtf)<<", tags="<<stress.pages[n].tags.size()<<", ticket="<<!stress.pages[n].ticket.empty()<<std::endl;
      throw std::runtime_error("Stress metadata or baseline diverged");
    }
  }
  Check(true,"320 deterministic reorder/close/reopen cycles with 64 notes retain order, content, metadata and pin groups");
  CloseClientNotesDialog(owner,stress);owner=Open(L"Tab stress QA");
  Check(TabIds(State(owner))==expected,"Overflow tab stress order persists after restart");auto &conflict=State(owner);
  const auto before=TabIds(conflict);const auto active=conflict.pages[conflict.activePage].id;Type(owner,L" unsaved");const auto draft=NotesWindowText(Edit(owner));
  auto newer=conflict.original.pages;newer.front().ticket=L"External writer";
  Check(client_notes::WriteNotebook(conflict.clientRoot,newer,conflict.original),"Concurrent writer creates tab-reorder save conflict");
  Check(!NotesReorderTab(owner,conflict,8,9) && TabIds(conflict)==before && conflict.pages[conflict.activePage].id==active && NotesWindowText(Edit(owner))==draft,"Save conflict prevents reorder while retaining the active draft and tab order");
  size_t closedIndex=0;while(conflict.pages[closedIndex].id==active) ++closedIndex;conflict.pages[closedIndex].open=false;RefreshNotesTabs(owner,conflict);
  const auto failedOrder=TabIds(conflict);
  Check(!NotesOpenSavedPage(owner,conflict,closedIndex) && !conflict.pages[closedIndex].open && TabIds(conflict)==failedOrder && NotesWindowText(Edit(owner))==draft,"Save conflict prevents reopening another tab without losing the draft");
  Check(client_notes::ReadNotebook(conflict.clientRoot).pages.front().ticket==L"External writer" && fs::exists(conflict.clientRoot/conflict.draftName),"Failed tab actions keep a recoverable draft and protect the newer notebook");DestroyWindow(owner);
}
static LRESULT CALLBACK TrayFixtureProc(HWND window,UINT message,WPARAM w,LPARAM l) {
  if (message==WM_APP_SHOW_LAUNCHER || message==WM_APP_LAUNCHER_TRAY || message==g_taskbarCreated ||
      (message==WM_SIZE && w==SIZE_MINIMIZED) || (message==WM_COMMAND && LOWORD(w)==IDM_CTX_MINIMIZE_TRAY)) return WndProc(window,message,w,l);
  return DefWindowProcW(window,message,w,l);
}
static void LauncherTrayTests() {
  WNDCLASSW type{};type.lpfnWndProc=TrayFixtureProc;type.hInstance=g_hInst;type.lpszClassName=L"ctSpaces tray fixture";RegisterClassW(&type);
  HWND window=CreateWindowW(type.lpszClassName,L"Tray QA",WS_OVERLAPPEDWINDOW,100,100,300,150,nullptr,nullptr,g_hInst,nullptr);
  g_minimizeToTray=false;ShowWindow(window,SW_SHOWNOACTIVATE);
  SendMessageW(window,WM_COMMAND,IDM_CTX_MINIMIZE_TRAY,0);
  Check(g_minimizeToTray && GetPrivateProfileIntW(L"user",L"minimize_to_tray",0,g_sConfigPath.c_str())==1,"Tray preference persists through config mutations");
  SendMessageW(window,WM_SIZE,SIZE_MINIMIZED,0);
  Check(g_launcherInTray && g_launcherTrayIcon && !IsWindowVisible(window),"Enabled minimize hides the launcher only after tray registration succeeds");
  Check(trayCommands.size()>=2 && trayCommands[trayCommands.size()-2]==NIM_ADD && trayCommands.back()==NIM_SETVERSION,"Tray icon uses the accessible version-4 callback contract");
  const auto events=trayCommands.size();SendMessageW(window,g_taskbarCreated,0,0);
  Check(g_launcherInTray && trayCommands.size()==events+2,"Explorer taskbar recreation restores the hidden launcher's icon");
  SendMessageW(window,WM_APP_LAUNCHER_TRAY,0,MAKELPARAM(NIN_KEYSELECT,1));
  Check(IsWindowVisible(window) && !g_launcherInTray && !g_launcherTrayIcon && trayCommands.back()==NIM_DELETE,"Keyboard activation restores the launcher and removes the tray icon");
  Check((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOPMOST)==0,"Foreground restoration does not leave the launcher always on top");
  trayAddWorks=false;SendMessageW(window,WM_SIZE,SIZE_MINIMIZED,0);
  Check(IsWindowVisible(window) && !g_launcherInTray,"Tray registration failure leaves the launcher accessible");trayAddWorks=true;
  trayVersionWorks=false;SendMessageW(window,WM_SIZE,SIZE_MINIMIZED,0);
  Check(IsWindowVisible(window) && !g_launcherTrayIcon && trayCommands.back()==NIM_DELETE,"Unsupported tray callback version rolls back registration safely");trayVersionWorks=true;
  HideLauncherInTray(window);SendMessageW(window,WM_APP_SHOW_LAUNCHER,0,0);
  Check(IsWindowVisible(window) && !g_launcherInTray,"A repeated launch restores a hidden tray launcher");
  SendMessageW(window,WM_COMMAND,IDM_CTX_MINIMIZE_TRAY,0);SendMessageW(window,WM_SIZE,SIZE_MINIMIZED,0);
  Check(!g_minimizeToTray && IsWindowVisible(window) && !g_launcherInTray,"Turning tray mode off retains ordinary minimize behavior");
  realTrayApi=true;const bool registered=HideLauncherInTray(window);
  ShowLauncherForeground(window);realTrayApi=false;
  Check(registered && IsWindowVisible(window) && !g_launcherTrayIcon,"Real Windows tray registration, version negotiation and removal succeed");DestroyWindow(window);
}
static unsigned closeRequests=0;
static LRESULT CALLBACK CloseRaceProc(HWND window,UINT message,WPARAM w,LPARAM l) {
  if(message==WM_CLOSE) {++closeRequests;return 0;} // Deliberate browser veto.
  return DefWindowProcW(window,message,w,l);
}
static void CloseAllLaunchRace() {
  Check(g_sessions.empty(),"Close-race fixture starts without client sessions");
  const auto pid=GetCurrentProcessId();g_sessions.push_back({L"Close race QA",BrowserKind::Edge,pid});
  answers.push_back(IDYES);RequestCloseAllClientsNormally();
  Check(g_bCloseAllClientsPending && g_closeAllAwaitingWindows.size()==1 && g_closeAllAwaitingWindows.front().pid==pid &&
        WaitForSingleObject(g_closeAllAwaitingWindows.front().process.get(),0)==WAIT_TIMEOUT,"Close all holds the tracked process identity while waiting for its first window");
  WNDCLASSW kind{};kind.lpfnWndProc=CloseRaceProc;kind.hInstance=g_hInst;kind.lpszClassName=L"ctSpaces close race fixture";RegisterClassW(&kind);
  HWND window=CreateWindowW(kind.lpszClassName,L"Browser close QA",WS_OVERLAPPEDWINDOW|WS_VISIBLE,50,50,250,100,nullptr,nullptr,g_hInst,nullptr);
  WndProc(g_hGui,WM_TIMER,kCloseAllCheckTimer,0);PumpPanelMessages();
  Check(closeRequests==1 && g_closeAllAwaitingWindows.empty(),"A late browser window receives the pending normal close request");
  for(int i=0;i<8;++i) WndProc(g_hGui,WM_TIMER,kCloseAllCheckTimer,0);
  PumpPanelMessages();Check(closeRequests==1 && IsWindow(window),"A browser close veto is respected without repeated close requests");
  OnSessionEnded(pid);Check(!g_bCloseAllClientsPending && g_closeAllAwaitingWindows.empty(),"Session exit clears the pending close state");DestroyWindow(window);
  g_sessions.push_back({L"Default",BrowserKind::Edge,pid});answers.push_back(IDYES);RequestCloseAllClientsNormally();
  Check(!g_bCloseAllClientsPending,"Default sessions are excluded from the rapid-launch close path");answers.clear();g_sessions.clear();
}
static void CodeBorderRegression() {
  Fixture(L"Code border QA");HWND owner=Open(L"Code border QA");Command(owner,IDC_NOTES_CLOSE_ALL);
  std::string rtf=NotesCodeRtf({1,L"Get-Printer\r\n    Select-Object Name"});
  const auto row=rtf.find("\\trowd");
  std::string lead="\\pard\\plain\\f0\\fs24 ";for(int i=0;i<12;++i) lead+="Before the code box\\par ";
  rtf.insert(row,lead);const auto tail=rtf.rfind('}');
  std::string after;for(int i=0;i<18;++i) after+="After the code box\\par ";rtf.insert(tail,after);
  Check(NotesAddPage(owner,State(owner),{L"Code border",true,rtf,{}}),"Code-border scroll fixture saves");
  const auto codeId=State(owner).pages[State(owner).activePage].id;
  SetWindowPos(owner,HWND_TOP,30,30,1120,960,SWP_SHOWWINDOW);PumpPanelMessages();
  const auto inspect=[&](const char *label) {
    HWND edit=Edit(owner);RECT area{};GetClientRect(edit,&area);PumpPanelMessages();
    HDC screen=GetDC(edit),dc=CreateCompatibleDC(screen);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=area.right;info.bmiHeader.biHeight=-area.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void *bits=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&bits,nullptr,0);auto old=SelectObject(dc,bitmap);
    BitBlt(dc,0,0,area.right,area.bottom,screen,0,0,SRCCOPY);GdiFlush();
    const auto pixel=[&](int x,int y){return static_cast<DWORD*>(bits)[y*area.right+x]&0xFFFFFF;};
    std::vector<int> borders;int left=-1,right=-1;
    for(int y=0;y<area.bottom;++y) {
      int run=0,start=0;for(int x=0;x<area.right;++x) {
        if(pixel(x,y)==RGB(128,128,128)) {if(!run)start=x;++run;}
        else {if(run>200) {borders.push_back(y);left=start;right=x-1;break;}run=0;}
      }
    }
    bool complete=borders.size()==3 && left>=0 && right<area.right;
    if(complete) {for(int y=borders.front();y<=borders.back();++y) complete=complete && pixel(left,y)==RGB(128,128,128) && pixel(right,y)==RGB(128,128,128);}
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(edit,screen);Check(complete,label);
  };
  inspect("Native code box has all four borders before scrolling");
  POINT position{0,180};SendMessageW(Edit(owner),EM_SETSCROLLPOS,0,reinterpret_cast<LPARAM>(&position));
  inspect("Scrolling preserves all code borders without stale lines at the old position");
  for(int i=0;i<24;++i) {SetWindowPos(owner,nullptr,0,0,1020+i%5*20,880+i%4*15,SWP_NOMOVE|SWP_NOZORDER);PumpPanelMessages();}
  inspect("Repeated native resize preserves the entire code-box outline");
  Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Another cached note");Save(owner);
  auto &state=State(owner);state.activePage=static_cast<size_t>(std::find_if(state.pages.begin(),state.pages.end(),[codeId](const auto &page){return page.id==codeId;})-state.pages.begin());Check(LoadNotesPage(owner,state),"Code note reopens from its cached editor");
  inspect("Cached tab reveal retains complete code borders");CloseClientNotesDialog(owner,state);
}
int wmain(int argc,wchar_t **argv) {
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
    if(argc>1 && !wcscmp(argv[1],L"--code-border")) {CodeBorderRegression();DestroyWindow(g_hGui);return 0;}
    if(argc>1 && !wcscmp(argv[1],L"--close-race")) {CloseAllLaunchRace();DestroyWindow(g_hGui);return 0;}
    HWND dialog=Open(L"Workspace QA"); Tests(dialog); Extended(dialog); ResizeAndOverflow(dialog); FrameRegression(dialog); PickerAndTemplates(dialog); Walkthrough(dialog); GuidanceAndBulkTrash(dialog); Productivity(dialog); TicketCodeAndTopActions(dialog); AddDeleteTransitions(dialog); DirectTemplateEditing(dialog); DirectTrashAndCompactHelp(dialog); NotesLinksAndContextMenu(dialog); DestroyWindow(dialog);
    dialog=Open(L"Workspace QA");
    Check(!HasActiveNote(State(dialog)),"Zero open tabs persists across restart");
    Check((GetWindowLongPtrW(Edit(dialog),GWL_STYLE)&WS_VISIBLE)==0,"Restart empty state does not expose blank editor");
    Command(dialog,IDC_NOTES_ADD_TAB); Check(HasActiveNote(State(dialog)),"New note from empty state");
    DestroyWindow(dialog);EmptyDraftRestart();CompactNormalSpacing();OldNotesSpacingUpgrade();DirectSnippetEditing();InspectorNavigation();NotesResizeWrapping();NotesTabOrdering();LauncherTrayTests();CloseAllLaunchRace();CodeBorderRegression();DestroyWindow(g_hGui);
    std::cout << "Notes workspace: " << checks << " checks passed.\n"; return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << std::endl; return 1; }
}
