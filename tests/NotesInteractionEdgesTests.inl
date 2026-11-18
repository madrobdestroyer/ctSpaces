namespace notes_interaction_edges {
static unsigned failures=0;

static void Expect(bool condition,const char *message) {
  ++checks;
  std::cout<<(condition ? "PASS " : "FAIL_NOTES_INTERACTION ")<<message<<'\n';
  if (!condition) ++failures;
}

static LRESULT CALLBACK RefuseActivation(int code,WPARAM w,LPARAM l) {
  if (code==HCBT_ACTIVATE || code==HCBT_SETFOCUS) return 1;
  return CallNextHookEx(nullptr,code,w,l);
}

static void ClickResult(HWND list,bool row) {
  RECT item{},area{};
  Check(SendMessageW(list,LB_GETITEMRECT,0,reinterpret_cast<LPARAM>(&item))!=LB_ERR &&
        GetClientRect(list,&area),"Read the native results list and its first row");
  if (item.bottom+4>=area.bottom) {
    Check(SetWindowPos(list,nullptr,0,0,area.right+4,item.bottom+16,
                      SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE &&
          GetClientRect(list,&area) && item.bottom+4<area.bottom,
          "Expand the private list to exercise blank space with one row");
  }
  const auto point=MAKELPARAM(item.left+8,row ? (item.top+item.bottom)/2 : item.bottom+4);
  SendMessageW(list,WM_LBUTTONDOWN,MK_LBUTTON,point);
  SendMessageW(list,WM_LBUTTONUP,0,point);
  Check(GetCapture()!=list,"The completed click leaves no capture on the results list");
}

static void PanelResultClicks() {
  struct Hook {
    HHOOK handle=SetWindowsHookExW(WH_CBT,RefuseActivation,nullptr,GetCurrentThreadId());
    ~Hook(){if (handle) UnhookWindowsHookEx(handle);}
  } hook;
  Check(hook.handle!=nullptr,"Veto any native activation or focus during result clicks");
  const std::wstring source=L"Panel click source",target=L"Panel click destination";
  Fixture(source);Fixture(target);
  const auto owner=Open(source);
  auto &state=State(owner);
  Command(owner,IDC_NOTES_ADD_TAB);Type(owner,L"Original private note");Save(owner);
  const auto sourceRoot=state.clientRoot;
  const auto sourceBefore=client_notes::ReadNotebook(sourceRoot).notebook_snapshot.bytes;
  const auto openChooser=[&]{
    Command(owner,IDC_NOTES_SWITCH_CLIENT);
    const auto panel=state.panel;
    Check(panel && IsWindow(panel),"Open the actual client chooser");
    SetDlgItemTextW(panel,IDC_NOTES_CLIENT_SEARCH,target.c_str());
    const auto list=GetDlgItem(panel,IDC_NOTES_CLIENT_MATCHES);
    Check(SendMessageW(list,LB_GETCOUNT,0,0)==1 && SendMessageW(list,LB_GETCURSEL,0,0)==0,
          "The narrowed chooser has one default highlighted client");
    return list;
  };
  auto list=openChooser();
  ClickResult(list,false);
  NotesBackgroundAudit("blank client chooser click");
  const bool chooserStayed=state.clientName==source && IsWindow(list);
  Expect(chooserStayed,"Clicking blank chooser space keeps the existing client and chooser open");
  if (!chooserStayed) {
    Check(SwitchClientNotes(owner,state,source,sourceRoot),"Return the baseline fixture to its original notebook");
    list=openChooser();
  }
  ClickResult(list,true);
  NotesBackgroundAudit("valid client chooser click");
  Check(state.clientName==target && !state.panel &&
        client_notes::ReadNotebook(sourceRoot).notebook_snapshot.bytes==sourceBefore,
        "Clicking the actual client row switches to it and preserves the other notebook bytes");

  const auto openActions=[&]{
    Command(owner,IDC_NOTES_MORE);
    const auto panel=state.panel;
    Check(panel && IsWindow(panel),"Open the actual searchable actions panel");
    SetDlgItemTextW(panel,IDC_NOTES_QUERY,L"New blank note");
    const auto results=GetDlgItem(panel,IDC_NOTES_RESULTS);
    Check(SendMessageW(results,LB_GETCOUNT,0,0)==1 && SendMessageW(results,LB_GETCURSEL,0,0)==0,
          "The narrowed actions panel has one default highlighted command");
    return results;
  };
  list=openActions();
  const auto pageCount=state.pages.size();
  ClickResult(list,false);
  NotesBackgroundAudit("blank action panel click");
  const bool actionsStayed=IsWindow(list) && state.pages.size()==pageCount;
  Expect(actionsStayed,"Clicking blank action space does not run the highlighted command");
  if (!actionsStayed) list=openActions();
  const auto priorCount=state.pages.size();
  ClickResult(list,true);
  NotesBackgroundAudit("valid action row click");
  Check(!state.panel && state.pages.size()==priorCount+1,
        "Clicking the actual action row still runs the intended command once");
  CloseClientNotesDialog(owner,state);
}

static void SearchFollowsEditor() {
  Fixture(L"Find tab transition QA");
  const auto owner=Open(L"Find tab transition QA");
  auto &state=State(owner);
  Command(owner,IDC_NOTES_CLOSE_ALL);
  Check(NotesAddPage(owner,state,{L"Other tab",true,NotesPlainRtf(L"Only other words"),{}}),
        "Create and cache a tab without the query");
  const auto other=state.attachedPageId;
  const auto otherEditor=Edit(owner);
  Check(NotesAddPage(owner,state,{L"Source tab",true,NotesPlainRtf(L"alpha alpha"),{}}),
        "Create a second tab with the two search matches");
  const auto source=state.attachedPageId;
  const auto openPage=[&](uint64_t id){
    const auto page=std::find_if(state.pages.begin(),state.pages.end(),[id](const auto &p){return p.id==id;});
    Check(page!=state.pages.end() && NotesOpenSavedPage(owner,state,static_cast<size_t>(page-state.pages.begin())),
          "Activate the specified tab through the existing native editor");
  };
  Command(owner,IDC_NOTES_REPLACE);
  SetDlgItemTextW(owner,IDC_NOTES_FIND,L"alpha");
  SetDlgItemTextW(owner,IDC_NOTES_REPLACE_TEXT,L"beta");
  Command(owner,IDC_NOTES_REPLACE_NEXT);
  const auto findInfo=[&]{return NotesWindowText(GetDlgItem(owner,IDC_NOTES_FIND_INFO));};
  Check(findInfo().find(L"Replaced 1 match")!=std::wstring::npos,
        "Replace on the source tab reports the successful source edit");
  openPage(other);
  Check(Edit(owner)==otherEditor && state.findVisible && NotesWindowText(Edit(owner))==L"Only other words",
        "Switching to the cached other tab retains its independent editor and search panel");
  Expect(findInfo()==L"No matches",
         "Find immediately reports the cached tab's own count and clears the previous tab's replace notice");
  openPage(source);
  Command(owner,IDC_NOTES_UNDO);
  Check(NotesWindowText(Edit(owner))==L"alpha alpha","Returning to the source tab preserves its own Replace Undo");
  NotesBackgroundAudit("search and Undo across cached tabs");
  CloseClientNotesDialog(owner,state);
}

static bool Run() {
  PanelResultClicks();
  SearchFollowsEditor();
  return failures==0;
}
}
