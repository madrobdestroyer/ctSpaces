namespace notes_inactive_tab_close {
static unsigned failures=0;

static void Expect(bool condition,const char *message) {
  ++checks;
  std::cout<<(condition ? "PASS " : "FAIL_NOTES_TAB_CLOSE ")<<message<<'\n';
  if (!condition) ++failures;
}

static void ClickClose(HWND tabs,int index) {
  RECT item{},area{};
  Check(TabCtrl_GetItemRect(tabs,index,&item) && GetClientRect(tabs,&area),
        "Read the actual native tab and strip bounds");
  const POINT point{item.right-ScaleByDpi(12,GetDpiForWindow(tabs)),(item.top+item.bottom)/2};
  TCHITTESTINFO hit{point,0};
  Check(PtInRect(&area,point) && TabCtrl_HitTest(tabs,&hit)==index,
        "The visible close glyph resolves to the intended native tab");
  const auto coordinates=MAKELPARAM(point.x,point.y);
  SendMessageW(tabs,WM_LBUTTONDOWN,MK_LBUTTON,coordinates);
  SendMessageW(tabs,WM_LBUTTONUP,0,coordinates);
  Check(GetCapture()!=tabs,"The native close gesture leaves no capture on the tab strip");
}

static bool Run() {
  const std::wstring client=L"Close unrelated Notes tab QA";
  Fixture(client);
  constexpr uint64_t sourceId=1,middleId=2,targetId=3;
  std::vector<client_notes::NotePage> notes{
    {L"Source",true,NotesPlainRtf(L"alpha alpha"),{}},
    {L"Middle",true,NotesPlainRtf(L"middle content"),{}},
    {L"Close me",true,NotesPlainRtf(L"keep my saved content"),{}}
  };
  for (size_t i=0;i<notes.size();++i) {
    notes[i].id=i+1;
    notes[i].open=true;
    notes[i].active=i==0;
  }
  std::string bytes;
  Check(client_notes::SerializeNotebook(notes,bytes),"Create three saved and open notes with the first active");
  Put(g_sDataDir/L"Sites"/client/client_notes::kNotebookFileName,bytes);
  const auto owner=Open(client);
  auto &state=State(owner);
  Check(TabIds(state)==std::vector<uint64_t>{sourceId,middleId,targetId} && state.attachedPageId==sourceId,
        "Open the source with the neighboring and distant tabs visible");
  const auto editor=Edit(owner);
  const auto tabs=GetDlgItem(owner,IDC_NOTES_TABS);
  Select(editor,-1,-1);
  Type(owner,L" pending draft");
  Command(owner,IDC_NOTES_FIND_TOGGLE);
  SetDlgItemTextW(owner,IDC_NOTES_FIND,L"alpha");
  Select(editor,6,11);
  NotesFindRefresh(owner,state);
  const auto findInfo=[&]{return NotesWindowText(GetDlgItem(owner,IDC_NOTES_FIND_INFO));};
  Check(TabCtrl_GetCurSel(tabs)==0 && findInfo()==L"2 of 2" &&
        SendMessageW(editor,EM_CANUNDO,0,0),"The first tab has an active selection, Find count, and native Undo");
  ClickClose(tabs,2);
  const auto saved=client_notes::ReadNotebook(state.clientRoot);
  const auto source=std::ranges::find(saved.pages,sourceId,&client_notes::NotePage::id);
  const auto target=std::ranges::find(saved.pages,targetId,&client_notes::NotePage::id);
  Check(saved.status==client_notes::ReadStatus::Ok && target!=saved.pages.end() && !target->open &&
        !target->archived && !target->deleted && NotesPlain(*target)==L"keep my saved content",
        "Closing the distant tab keeps its saved content and removes only its open flag");
  Check(source!=saved.pages.end() && NotesPlain(*source)==L"alpha alpha pending draft" &&
        TabIds(state)==std::vector<uint64_t>{sourceId,middleId},
        "The pending source draft saves and the two other tabs retain their order");
  Expect(state.attachedPageId==sourceId && Edit(owner)==editor && TabCtrl_GetCurSel(tabs)==0 && source->active,
         "Closing an inactive tab preserves the active tab, editor, and saved active identity");
  Expect(findInfo()==L"2 of 2","Closing an inactive tab preserves the active note's Find count");
  CHARRANGE selected{};
  NotesSelection(editor,selected);
  Check(selected.cpMin==6 && selected.cpMax==11 && SendMessageW(editor,EM_CANUNDO,0,0),
        "The cached source still has its original selection and Undo record");
  if (state.attachedPageId!=sourceId)
    Check(NotesOpenSavedPage(owner,state,0),"Return to the source after observing the baseline tab jump");
  Command(owner,IDC_NOTES_UNDO);
  Check(NotesWindowText(Edit(owner))==L"alpha alpha","The original editor still undoes its pending edit");
  Command(owner,IDC_NOTES_REOPEN);
  Check(state.attachedPageId==targetId && NotesWindowText(Edit(owner))==L"keep my saved content",
        "Reopen last closed still reopens the intended note with its saved content");
  ClickClose(tabs,2);
  Check(state.attachedPageId==middleId && TabIds(state)==std::vector<uint64_t>{sourceId,middleId},
        "Closing the active tab still selects its adjacent tab");
  CloseClientNotesDialog(owner,state);
  NotesBackgroundAudit("inactive and active native tab close");
  return failures==0;
}
}
