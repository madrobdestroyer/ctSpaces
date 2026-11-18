// Actual native ListBox input messages under the private Notes window guards.
// In particular, a click on the current row can still send LBN_SELCHANGE.
namespace notes_library_selection {
static unsigned failures=0;
static LRESULT CALLBACK RefuseNativeFocus(int code,WPARAM w,LPARAM l) {
  if(code==HCBT_ACTIVATE || code==HCBT_SETFOCUS)return 1;
  return CallNextHookEx(nullptr,code,w,l);
}
static void Expect(bool condition,const char *message) {
  ++checks;
  if(condition)std::cout<<"PASS "<<message<<'\n';
  else {++failures;std::cout<<"FAIL_SELECTION "<<message<<'\n';}
}
struct Notifications {unsigned changed=0;};
static LRESULT CALLBACK Observe(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  auto &events=*reinterpret_cast<Notifications *>(data);
  if(message==WM_COMMAND && LOWORD(w)==IDC_NOTES_RESULTS && HIWORD(w)==LBN_SELCHANGE)++events.changed;
  if(message==WM_NCDESTROY)RemoveWindowSubclass(window,Observe,id);
  return DefSubclassProc(window,message,w,l);
}
static void Click(HWND list,int row,bool body=false,UINT down=WM_LBUTTONDOWN) {
  RECT item{};Check(SendMessageW(list,LB_GETITEMRECT,row,reinterpret_cast<LPARAM>(&item))!=LB_ERR,"A native result row provides its actual hit rectangle");
  const auto point=MAKELPARAM(item.left+ScaleByDpi(body?100:10,GetDpiForWindow(list)),(item.top+item.bottom)/2);
  const HWND capture=GetCapture();
  SendMessageW(list,down,MK_LBUTTON,point);SendMessageW(list,WM_LBUTTONUP,0,point);
  Check(GetCapture()==capture,"A complete native result click leaves mouse capture unchanged");
}
static bool Selected(const NotesLibrary &lib,uint64_t id) {
  return std::find(lib.selected.begin(),lib.selected.end(),id)!=lib.selected.end();
}
static void RunOne(bool dark) {
  const std::wstring client=dark?L"Selection dark QA":L"Selection light QA";
  const std::wstring other=dark?L"Selection dark other":L"Selection light other";
  Fixture(client);Fixture(other);
  std::vector<client_notes::NotePage> pages;
  const auto time=client_notes::Now();
  for(unsigned i=0;i<9;++i) {
    const std::wstring name=i<2?L"Ticket investigation "+std::to_wstring(i+1):L"Other note "+std::to_wstring(i+1);
    client_notes::NotePage page{name,true,NotesPlainRtf(name+L" body"),{}};
    page.id=i+1;page.open=i<7;page.active=i==0;page.archived=i==7;page.deleted=i==8;
    page.created=page.modified=time;pages.push_back(std::move(page));
  }
  std::string bytes;Check(client_notes::SerializeNotebook(pages,bytes),"The selection fixture serializes independently saved note identities");
  const auto root=g_sDataDir/L"Sites"/client,foreign=g_sDataDir/L"Sites"/other;
  Put(root/client_notes::kNotebookFileName,bytes);Put(foreign/client_notes::kNotebookFileName,bytes);
  const auto foreignBefore=client_notes::ReadNotebook(foreign).notebook_snapshot.bytes;
  g_bThemeIsDark=dark;BuildThemeColors(dark,g_themeColors);UpdateThemeBrushes();
  HWND owner=Open(client);auto &state=State(owner);
  NotesLibrary lib{owner,&state};HWND window=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),owner,NotesLibraryProc,reinterpret_cast<LPARAM>(&lib));
  Check(window&&lib.results.size()==7,"Seven current notes and separate archive/trash notes reach the actual library");
  HWND list=GetDlgItem(window,IDC_NOTES_RESULTS);Notifications events;SetWindowSubclass(window,Observe,792,reinterpret_cast<DWORD_PTR>(&events));
  lib.selecting=true;for(const auto index:lib.results)lib.selected.push_back(state.pages[index].id);NotesLibraryFill(window,lib);
  Expect(lib.selected.size()==7,"Select-all fixture starts with seven explicit note identities");
  SetDlgItemTextW(window,IDC_NOTES_QUERY,L"Ticket investigation");SendMessageW(window,WM_TIMER,1,0);
  Expect(lib.selected.empty()&&lib.results.size()==2&&SendMessageW(list,LB_GETCURSEL,0,0)==0,"Filtering clears all prior ticks and previews the first matching note");
  const uint64_t first=state.pages[lib.results[0]].id,second=state.pages[lib.results[1]].id;
  Click(list,0);
  std::cout<<"Native current-row notifications="<<events.changed<<" selected="<<lib.selected.size()<<'\n';
  Expect(lib.selected.size()==1&&Selected(lib,first),"Clicking the already-current first checkbox ticks exactly that note once");
  Expect(NotesWindowText(GetDlgItem(window,IDC_NOTES_ACTION_HINT)).starts_with(L"1 selected."),"The native action hint reports the current-row tick");
  Click(list,0);Expect(lib.selected.empty(),"Clicking the same current checkbox again unticks it");
  Click(list,1);Expect(lib.selected.size()==1&&Selected(lib,second),"Clicking a different row ticks its captured identity once without double toggling");
  Click(list,1);Expect(lib.selected.empty(),"A repeated click unticks the newly-current second checkbox");
  Click(list,0,true);Expect(lib.selected.size()==1&&Selected(lib,first),"Clicking row text uses the same one-toggle selection contract");
  SendMessageW(list,WM_KEYDOWN,VK_DOWN,0);SendMessageW(list,WM_KEYUP,VK_DOWN,0);
  Expect(SendMessageW(list,LB_GETCURSEL,0,0)==1&&lib.selected.size()==1&&Selected(lib,first),"Arrow navigation moves only the preview and leaves existing ticks intact");
  SendMessageW(list,WM_KEYDOWN,VK_SPACE,0);SendMessageW(list,WM_KEYUP,VK_SPACE,0);
  Expect(lib.selected.size()==2&&Selected(lib,first)&&Selected(lib,second),"Space explicitly ticks the previewed note once");
  SendMessageW(list,WM_KEYDOWN,VK_SPACE,0);SendMessageW(list,WM_KEYUP,VK_SPACE,0);
  Expect(lib.selected.size()==1&&Selected(lib,first),"A second Space unticks only that previewed note");
  const auto ticked=lib.selected;
  for(const auto key:{VK_HOME,VK_END,VK_PRIOR,VK_NEXT}){SendMessageW(list,WM_KEYDOWN,key,0);SendMessageW(list,WM_KEYUP,key,0);}
  SendMessageW(list,WM_CHAR,L'T',0);
  Expect(lib.selected==ticked,"Home/End/Page and native type-ahead preview without changing bulk ticks");
  RECT item{},area{};SendMessageW(list,LB_GETITEMRECT,1,reinterpret_cast<LPARAM>(&item));GetClientRect(list,&area);
  Check(item.bottom+8<area.bottom,"The native fixture exposes real empty list space below its results");
  const LPARAM blank=MAKELPARAM(10,item.bottom+8);SendMessageW(list,WM_LBUTTONDOWN,MK_LBUTTON,blank);SendMessageW(list,WM_LBUTTONUP,0,blank);
  Expect(lib.selected==ticked,"Clicking empty list space does not tick the nearest result");
  SendMessageW(window,WM_COMMAND,MAKEWPARAM(IDC_NOTES_RESULTS,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));
  Expect(lib.selected==ticked,"A selection-change notification by itself only refreshes the preview");
  lib.selected.clear();NotesLibraryFill(window,lib);SendMessageW(list,LB_SETCURSEL,0,0);
  Click(list,0);Expect(lib.selected.size()==1&&Selected(lib,first),"The first physical click in a double-click sequence ticks once");
  Click(list,0,false,WM_LBUTTONDBLCLK);Expect(lib.selected.empty()&&IsWindow(window),"The second double-click press unticks once without opening or closing bulk selection");
  SetDlgItemTextW(window,IDC_NOTES_QUERY,L"");SendMessageW(window,WM_TIMER,1,0);
  SendDlgItemMessageW(window,IDC_NOTES_FILTER,CB_SETCURSEL,3,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(IDC_NOTES_FILTER,CBN_SELCHANGE),0);SendMessageW(window,WM_TIMER,1,0);
  Check(lib.results.size()==1&&state.pages[lib.results[0]].deleted,"The same native control displays exactly one trashed note");
  const uint64_t trashed=state.pages[lib.results[0]].id;
  Click(list,0);Expect(lib.selected.size()==1&&Selected(lib,trashed)&&IsWindowEnabled(GetDlgItem(window,IDC_NOTES_RESTORE)),"The current Trash checkbox ticks its own note and enables Restore");
  Click(list,0);Expect(lib.selected.empty()&&!IsWindowEnabled(GetDlgItem(window,IDC_NOTES_RESTORE)),"Unticking the same Trash row removes Restore's action scope");
  lib.readOnly=true;Click(list,0);Expect(lib.selected.empty(),"Read-only library interaction cannot add a bulk mutation target");lib.readOnly=false;
  Expect(client_notes::ReadNotebook(root).notebook_snapshot.bytes==bytes&&client_notes::ReadNotebook(foreign).notebook_snapshot.bytes==foreignBefore,"Selection/preview/filter input leaves this notebook and another client's bytes unchanged");
  RemoveWindowSubclass(window,Observe,792);DestroyWindow(window);DestroyWindow(owner);
  NotesBackgroundAudit(dark?"dark library native selection input":"light library native selection input");
}
static bool Run() {
  // Native ListBox internals call focus APIs without passing through source
  // wrappers. Keep a thread-local CBT veto installed throughout every click.
  struct NativeFocusGuard {HHOOK hook=nullptr;~NativeFocusGuard(){if(hook)UnhookWindowsHookEx(hook);}};
  NativeFocusGuard guard{SetWindowsHookExW(WH_CBT,RefuseNativeFocus,nullptr,GetCurrentThreadId())};
  Check(guard.hook!=nullptr,"Native list input has a persistent activation and focus veto");
  RunOne(true);RunOne(false);return !failures;
}
}
