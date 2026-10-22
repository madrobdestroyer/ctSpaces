namespace notes_geometry {
static LRESULT CALLBACK Streams(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
static void TabGrowthViewport() {
  Fixture(L"Tab growth viewport QA");HWND owner=Open(L"Tab growth viewport QA");
  Command(owner,IDC_NOTES_CLOSE_ALL);auto &state=State(owner);const UINT dpi=GetDpiForWindow(owner);
  for(unsigned i=0;i<14;++i) Command(owner,IDC_NOTES_ADD_TAB);
  const HWND tabs=GetDlgItem(owner,IDC_NOTES_TABS),edit=Edit(owner);
  Check(TabCtrl_GetItemCount(tabs)==14 && TabCtrl_GetCurSel(tabs)==13,"Native tab-growth fixture has fourteen open tabs and the last selected");
  Type(owner,L"Saved tab-growth text");Save(owner);
  SendMessageW(edit,EM_STOPGROUPTYPING,0,0);Select(edit,-1,-1);Type(owner,L" unsaved edit");SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
  KillTimer(owner,1);Select(edit,2,8);
  std::string original;Check(StreamClientNotesOut(edit,original),"Tab-growth captures the exact original unsaved native RTF");
  const auto text=NotesWindowText(edit);const auto order=TabIds(state);const auto active=state.attachedPageId;
  const auto baselines=state.baselines;const bool metadata=state.metadataChanged,formatting=state.formattingChanged;
  const auto modified=SendMessageW(edit,EM_GETMODIFY,0,0),canUndo=SendMessageW(edit,EM_CANUNDO,0,0),canRedo=SendMessageW(edit,EM_CANREDO,0,0);
  const auto undoName=SendMessageW(edit,EM_GETUNDONAME,0,0),redoName=SendMessageW(edit,EM_GETREDONAME,0,0);
  CHARRANGE selection{};NotesSelection(edit,selection);
  const auto durable=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
  NotesTabMutationCounts mutations;unsigned streams=0;
  SetWindowSubclass(tabs,ObserveNotesTabMutation,395,reinterpret_cast<DWORD_PTR>(&mutations));
  SetWindowSubclass(edit,Streams,396,reinterpret_cast<DWORD_PTR>(&streams));
  const auto resize=[&](int width) {return SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,dpi),ScaleByDpi(620,dpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;};
  const auto viewport=[&]() {RECT row{};GetClientRect(tabs,&row);HWND arrows=FindWindowExW(tabs,nullptr,UPDOWN_CLASSW,nullptr);if(arrows && (GetWindowLongPtrW(arrows,GWL_STYLE)&WS_VISIBLE)){RECT navigation{};GetWindowRect(arrows,&navigation);MapWindowPoints(nullptr,tabs,reinterpret_cast<POINT *>(&navigation),2);return navigation.left;}return row.right;};
  const auto firstVisible=[&]() {for(int i=0;i<14;++i){RECT item{};TabCtrl_GetItemRect(tabs,i,&item);if(item.right>0)return i;}return 14;};
  Check(resize(905),"The native fourteen-tab fixture resizes to905 DIP without activation");
  RECT narrowLast{};TabCtrl_GetItemRect(tabs,13,&narrowLast);const int narrowFirst=firstVisible(),narrowViewport=viewport();
  Check(narrowFirst>0 && narrowLast.left>=0 && narrowLast.right<=narrowViewport,"The narrow native strip overflows while keeping the last active tab fully visible");
  Check(resize(1707),"The same native fourteen-tab fixture grows to1707 DIP without activation");
  RECT wideLast{};TabCtrl_GetItemRect(tabs,13,&wideLast);const int wideFirst=firstVisible(),wideViewport=viewport(),itemWidth=wideLast.right-wideLast.left;
  HWND arrows=FindWindowExW(tabs,nullptr,UPDOWN_CLASSW,nullptr);
  Check(arrows && (GetWindowLongPtrW(arrows,GWL_STYLE)&WS_VISIBLE) && wideViewport>narrowViewport,"Growth expands the real native strip while fourteen fixed-width tabs still overflow");
  Check(wideLast.left>=0 && wideLast.right<=wideViewport,"Growth retains the selected last tab and its close button inside the usable viewport");
  const bool backfilled=wideFirst<narrowFirst && wideViewport-wideLast.right<itemWidth;
  std::cout<<"Tab growth viewport: dpi="<<dpi<<"; narrowFirst="<<narrowFirst<<"; wideFirst="<<wideFirst<<"; narrowViewport="<<narrowViewport<<"; wideViewport="<<wideViewport<<"; last="<<wideLast.left<<","<<wideLast.right<<"; itemWidth="<<itemWidth<<"; unused="<<(wideViewport-wideLast.right)<<"; backfilled="<<backfilled<<std::endl;
  const int grownPosition=static_cast<int>(SendMessageW(arrows,UDM_GETPOS32,0,0));RECT grownFirst{};TabCtrl_GetItemRect(tabs,0,&grownFirst);
  for(unsigned repeat=0;repeat<5;++repeat) LayoutClientNotes(owner,dpi);
  RECT stableFirst{};TabCtrl_GetItemRect(tabs,0,&stableFirst);
  Check(EqualRect(&grownFirst,&stableFirst) && SendMessageW(arrows,UDM_GETPOS32,0,0)==grownPosition,"Repeated unchanged tab layout preserves the grown native viewport");
  int low=0,high=0;SendMessageW(arrows,UDM_GETRANGE32,reinterpret_cast<WPARAM>(&low),reinterpret_cast<LPARAM>(&high));
  const int manual=(std::min)(low,high);SendMessageW(arrows,UDM_SETPOS32,0,manual);SendMessageW(tabs,WM_HSCROLL,MAKEWPARAM(SB_THUMBPOSITION,manual),reinterpret_cast<LPARAM>(arrows));
  RECT manualFirst{};TabCtrl_GetItemRect(tabs,0,&manualFirst);const auto manualPosition=SendMessageW(arrows,UDM_GETPOS32,0,0);
  for(unsigned repeat=0;repeat<5;++repeat) LayoutClientNotes(owner,dpi);
  RECT manualAfter{};TabCtrl_GetItemRect(tabs,0,&manualAfter);
  Check(EqualRect(&manualFirst,&manualAfter) && SendMessageW(arrows,UDM_GETPOS32,0,0)==manualPosition,"Unchanged layout retains an explicitly manually scrolled tab viewport");
  Check(resize(905),"The native strip can shrink again after an independently changed scroll position");
  RECT shrunkLast{};TabCtrl_GetItemRect(tabs,13,&shrunkLast);
  Check(shrunkLast.left>=0 && shrunkLast.right<=viewport(),"Shrinking a manually scrolled strip makes the unchanged active tab fully visible");
  Check(TabIds(state)==order && TabCtrl_GetItemCount(tabs)==14 && TabCtrl_GetCurSel(tabs)==13 && state.attachedPageId==active,"Growth and shrink preserve every native tab identity, order and active note");
  Check(!mutations.resets && !mutations.deletes && !mutations.inserts && Edit(owner)==edit && !streams,"Native viewport correction never rebuilds tabs or reloads the attached editor");
  CHARRANGE afterSelection{};NotesSelection(edit,afterSelection);
  Check(afterSelection.cpMin==selection.cpMin && afterSelection.cpMax==selection.cpMax && NotesWindowText(edit)==text,"Tab viewport geometry preserves the exact unsaved text and selection");
  Check(state.baselines==baselines && state.metadataChanged==metadata && state.formattingChanged==formatting && SendMessageW(edit,EM_GETMODIFY,0,0)==modified,"Tab viewport geometry preserves saved baselines and all captured dirty flags");
  Check(SendMessageW(edit,EM_CANUNDO,0,0)==canUndo && canUndo && SendMessageW(edit,EM_CANREDO,0,0)==canRedo && SendMessageW(edit,EM_GETUNDONAME,0,0)==undoName && SendMessageW(edit,EM_GETREDONAME,0,0)==redoName,"Tab viewport geometry preserves the independent native Undo and Redo state");
  RemoveWindowSubclass(edit,Streams,396);RemoveWindowSubclass(tabs,ObserveNotesTabMutation,395);
  std::string after;Check(StreamClientNotesOut(edit,after) && after==original,"Tab viewport geometry leaves exact native RTF unchanged");
  Check(client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==durable,"Tab viewport geometry never publishes the unsaved draft to its durable notebook");
  Check(SendMessageW(edit,EM_UNDO,0,0) && NotesWindowText(edit)==L"Saved tab-growth text","One real Undo after all viewport changes removes exactly the independent unsaved edit");
  Check(SendMessageW(edit,EM_REDO,0,0) && NotesWindowText(edit)==text,"One real Redo after all viewport changes restores exactly the independent unsaved edit");
  KillTimer(owner,1);DestroyWindow(owner);NotesBackgroundAudit("fourteen-tab growth viewport cleanup");NotesBackgroundBanner();
  Check(backfilled,"Growing a still-overflowing strip backfills preceding tabs instead of leaving an unused whole-tab gap");
}
static void EmptyEditorLayout(std::wstring_view action=L"all") {
  Fixture(L"Empty layout QA");HWND owner=Open(L"Empty layout QA");auto &state=State(owner);
  Type(owner,L"Saved content survives an empty workspace");Save(owner);
  Command(owner,IDC_NOTES_CLOSE_ALL);
  Check(!HasActiveNote(state) && !Edit(owner),"Closing all tabs leaves no attached editor to repaint");
  Check(!NotesCanRenameTabAt(GetDlgItem(owner,IDC_NOTES_TABS),state,{10,10}),"An empty tab strip is never a rename target");
  const auto saved=client_notes::ReadNotebook(state.clientRoot);
  if(action==L"all" || action==L"rename") {
    const auto dialogs=backgroundGuard.modalSuppressed;
    Command(owner,IDC_NOTES_RENAME_TAB);
    Check(backgroundGuard.modalSuppressed==dialogs && !HasActiveNote(state),"Rename on an empty workspace is ignored before indexing or opening a dialog");
  }
  if(action==L"all" || action==L"theme") {
    for(const UINT message:{WM_THEMECHANGED,WM_SYSCOLORCHANGE}) {
      SendMessageW(owner,message,0,0);
      Check(!Edit(owner),"Empty-workspace theme notifications never repaint a missing editor");
    }
    for(const UINT dpi:{96u,144u,192u}) {
      RECT suggested{100,100,100+ScaleByDpi(900,dpi),100+ScaleByDpi(540,dpi)};
      SendMessageW(owner,WM_DPICHANGED,MAKEWPARAM(dpi,dpi),reinterpret_cast<LPARAM>(&suggested));
      Check(!Edit(owner),"Empty-workspace DPI notifications never repaint a missing editor");
    }
    RefreshNotesForTheme();
    Check(!Edit(owner),"Refreshing the application theme preserves the empty native workspace");
  }
  if(action==L"all" || action==L"layout") for(const UINT dpi:{96u,144u,192u}) for(const auto [width,height]:{std::pair{900,540},std::pair{1540,920},std::pair{900,540}}) {
    SetWindowPos(owner,nullptr,0,0,ScaleByDpi(width,dpi),ScaleByDpi(height,dpi),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    LayoutClientNotes(owner,dpi);
    Check(!HasActiveNote(state) && !Edit(owner) && IsWindow(GetDlgItem(owner,IDC_NOTES_EMPTY)),"Empty-workspace resizing keeps its placeholder without creating or repainting a missing editor");
  }
  Check(client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes==saved.notebook_snapshot.bytes,"Empty-workspace geometry leaves all saved notebook bytes unchanged");
  Check(!state.pages.empty() && NotesOpenSavedPage(owner,state,0) && NotesWindowText(Edit(owner))==L"Saved content survives an empty workspace","A saved note reopens with exact text after empty-workspace resize cycles");
  HWND tabs=GetDlgItem(owner,IDC_NOTES_TABS);RECT tab{};TabCtrl_GetItemRect(tabs,0,&tab);const int middle=(tab.top+tab.bottom)/2;
  Check(NotesCanRenameTabAt(tabs,state,{tab.left+ScaleByDpi(12,GetDpiForWindow(tabs)),middle}),"Only the active tab label is an eligible rename target");
  Check(!NotesCanRenameTabAt(tabs,state,{tab.right-ScaleByDpi(5,GetDpiForWindow(tabs)),middle}) && !NotesCanRenameTabAt(tabs,state,{tab.right+20,middle}),"A tab close button or trailing whitespace cannot rename the surviving note");
  NotesBackgroundAudit("empty notebook editor layout");DestroyWindow(owner);
}
static LRESULT CALLBACK Streams(HWND edit,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  if(message==EM_STREAMIN || message==EM_STREAMOUT) ++*reinterpret_cast<unsigned *>(data);
  if(message==WM_NCDESTROY) RemoveWindowSubclass(edit,Streams,id);
  return DefSubclassProc(edit,message,w,l);
}
static RECT Bounds(HWND owner,int id) {RECT value{};GetWindowRect(GetDlgItem(owner,id),&value);MapWindowPoints(nullptr,owner,reinterpret_cast<POINT *>(&value),2);return value;}
static LONG HintTextHeight(HWND owner) {
  const HWND hint=GetDlgItem(owner,IDC_NOTES_DETAILS_HINT);const auto bounds=Bounds(owner,IDC_NOTES_DETAILS_HINT);const auto text=NotesWindowText(hint);
  HDC dc=GetDC(hint);const auto font=reinterpret_cast<HFONT>(SendMessageW(hint,WM_GETFONT,0,0));const auto previous=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));
  RECT measured{0,0,bounds.right-bounds.left,0};DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
  SelectObject(dc,previous);ReleaseDC(hint,dc);return measured.bottom-measured.top;
}
struct PanelEvents {unsigned positions=0,scrollbarStyles=0;};
static LRESULT CALLBACK PanelChanges(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data){
  auto &events=*reinterpret_cast<PanelEvents *>(data);if(message==WM_WINDOWPOSCHANGING)++events.positions;
  if(message==WM_STYLECHANGING&&static_cast<int>(w)==GWL_STYLE&&l){const auto &change=*reinterpret_cast<STYLESTRUCT *>(l);if((change.styleNew^change.styleOld)&WS_VSCROLL)++events.scrollbarStyles;}
  if(message==WM_NCDESTROY)RemoveWindowSubclass(window,PanelChanges,id);return DefSubclassProc(window,message,w,l);
}
static void LayoutFaults() {
  Fixture(L"Geometry faults QA");HWND owner=Open(L"Geometry faults QA");
  Type(owner,L"Geometry sentinel remains in the original native editor.");const HWND editor=Edit(owner);const auto text=NotesWindowText(editor);unsigned streams=0;SetWindowSubclass(editor,Streams,387,reinterpret_cast<DWORD_PTR>(&streams));
  Command(owner,IDC_NOTES_REPLACE); // Cover every temporary search/replacement control through batch failures too.
  const UINT dpi=GetDpiForWindow(owner);const RECT work=NotesWorkArea(owner);
  const auto resize=[&](int width,int height){return SetWindowPos(owner,nullptr,0,0,
      (std::min)(ScaleByDpi(width,dpi),static_cast<int>(work.right-work.left)),
      (std::min)(ScaleByDpi(height,dpi),static_cast<int>(work.bottom-work.top)),
      SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);};
  const std::array ids={IDC_NOTES_SWITCH_CLIENT,IDC_NOTES_LIBRARY,IDC_NOTES_TRASH,IDC_NOTES_MORE,IDC_NOTES_HELP,IDCANCEL,
      IDC_NOTES_OPEN_TICKET,IDC_NOTES_TICKET_REFERENCE,IDC_NOTES_CONTEXT,IDC_NOTES_STATUS,IDC_NOTES_FIND_TOGGLE,IDC_NOTES_SNIPPETS,
      IDC_NOTES_TEMPLATES,IDC_NOTES_WORKLOG,IDC_NOTES_FORMAT,IDC_NOTES_WORK_STATUS,IDC_NOTES_DETAILS,IDC_NOTES_TABS,IDC_NOTES_ADD_TAB,
      IDC_NOTES_FIND,IDC_NOTES_FIND_PREV,IDC_NOTES_FIND_NEXT,IDC_NOTES_REPLACE,IDC_NOTES_FIND_CLOSE,
      IDC_NOTES_FIND_MATCH_CASE,IDC_NOTES_FIND_WHOLE_WORD,IDC_NOTES_FIND_WRAP,IDC_NOTES_FIND_INFO,
      IDC_NOTES_REPLACE_TEXT,IDC_NOTES_REPLACE_NEXT,IDC_NOTES_REPLACE_ALL,IDC_NOTES_EMPTY,IDC_CLIENT_NOTES_TEXT};
  for(int phase=1;phase<=3;++phase) {
    Check(resize(1190,700),"The large native geometry setup resize succeeds");LayoutClientNotes(owner,dpi);
    RECT large{};GetClientRect(owner,&large);
    std::map<int,RECT> expected;for(const auto id:ids) expected[id]=Bounds(owner,id);
    Check(resize(930,560),"The small native geometry setup resize succeeds");LayoutClientNotes(owner,dpi);
    RECT smallBounds{};GetClientRect(owner,&smallBounds);
    std::cout<<"Geometry setup phase="<<phase<<" dpi="<<dpi<<" large="<<large.right<<","<<large.bottom<<" small="<<smallBounds.right<<","<<smallBounds.bottom<<std::endl;
    Check(!EqualRect(&large,&smallBounds),"Actual native geometry sizes differ before a batch failure is armed");
    geometryFaultHit=false;geometryFault=phase;const bool resized=resize(1190,700)!=FALSE;
    Check(resized && geometryFaultHit && !geometryFault,"The actual resize consumes its injected native batch failure");
    for(const auto id:ids) {const auto current=Bounds(owner,id);std::cout<<"Geometry phase="<<phase<<" id="<<id<<" expected="<<expected[id].left<<","<<expected[id].top<<","<<expected[id].right<<","<<expected[id].bottom<<" actual="<<current.left<<","<<current.top<<","<<current.right<<","<<current.bottom<<std::endl;Check(EqualRect(&current,&expected[id]),"Every recorded native control reaches its expected geometry after batch failure");}
  }
  Check(Edit(owner)==editor && NotesWindowText(editor)==text && streams==0,"Deferred geometry recovery preserves the original editor and text without any native streams");
  RemoveWindowSubclass(editor,Streams,387);
  DestroyWindow(owner);
}
static void PopupBounds() {
  Fixture(L"Geometry popup QA");Fixture(L"Geometry popup second");Fixture(L"Geometry popup third");HWND owner=Open(L"Geometry popup QA");geometryWork={130,90,440,310};
  for(const int kind:{0,1,2}) {
    NotesShowPanel(owner,&State(owner),kind==0,kind==1);HWND window=State(owner).panel;RECT bounds{};GetWindowRect(window,&bounds);
    std::cout<<"Popup kind="<<kind<<" work="<<geometryWork.left<<","<<geometryWork.top<<","<<geometryWork.right<<","<<geometryWork.bottom<<" actual="<<bounds.left<<","<<bounds.top<<","<<bounds.right<<","<<bounds.bottom<<std::endl;
    Check(bounds.left>=geometryWork.left && bounds.top>=geometryWork.top && bounds.right<=geometryWork.right && bounds.bottom<=geometryWork.bottom,"Native Notes popup stays wholly within the available work area");
    auto *panel=reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(window,DWLP_USER));const auto close=Bounds(window,IDCANCEL);RECT area{};GetClientRect(window,&area);
    Check(close.left>=0 && close.top>=0 && close.right<=area.right && close.bottom<=area.bottom,"Popup close button remains within its capped native client area");
    if(kind==0) {
      Check(panel->scrollMax>0 && (GetWindowLongPtrW(window,GWL_STYLE)&WS_VSCROLL),"A capped Details body exposes a real native vertical scrollbar");
      for(const int id:{IDC_NOTES_STATUS_PICK,IDC_NOTES_TAGS_EDIT,IDC_NOTES_INPUT1,IDC_NOTES_INPUT2,IDC_NOTES_LINK_BROWSER_PICK}) {
        const HWND child=GetDlgItem(window,id);const bool combo=id==IDC_NOTES_STATUS_PICK || id==IDC_NOTES_LINK_BROWSER_PICK;
        SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,combo ? CBN_SETFOCUS : EN_SETFOCUS),reinterpret_cast<LPARAM>(child));const auto field=Bounds(window,id);
        HRGN shape=CreateRectRgn(0,0,0,0);const int region=GetWindowRgn(child,shape);RECT visible{};GetRgnBox(shape,&visible);DeleteObject(shape);
        Check(field.top>=panel->scrollHeader && field.bottom<=area.bottom && region!=NULLREGION && visible.bottom>visible.top,"Keyboard focus notification reveals each real Details field inside the scroll viewport");
        const auto currentClose=Bounds(window,IDCANCEL);Check(EqualRect(&close,&currentClose),"Scrolling Details preserves its fixed close-button geometry");
      }
      const HWND hintControl=GetDlgItem(window,IDC_NOTES_DETAILS_HINT);const auto header=Bounds(window,IDC_NOTES_LABEL1);
      const auto visibleHint=[&] {
        const auto hint=Bounds(window,IDC_NOTES_DETAILS_HINT);HRGN shape=CreateRectRgn(0,0,0,0);const int region=GetWindowRgn(hintControl,shape);RECT visible{};GetRgnBox(shape,&visible);DeleteObject(shape);
        const RECT expected{0,(std::clamp)(panel->scrollHeader-hint.top,0L,hint.bottom-hint.top),hint.right-hint.left,(std::clamp)(area.bottom-hint.top,0L,hint.bottom-hint.top)};
        Check(region!=ERROR&&region!=NULLREGION&&EqualRect(&visible,&expected)&&visible.bottom>visible.top&&hint.top+visible.top>=panel->scrollHeader&&hint.top+visible.bottom<=area.bottom,"The native hint clipping region exposes only the body viewport and never paints through the fixed header");
        const auto currentHeader=Bounds(window,IDC_NOTES_LABEL1),currentClose=Bounds(window,IDCANCEL);Check(EqualRect(&header,&currentHeader)&&EqualRect(&close,&currentClose),"Revealing a partially clipped explanation preserves the fixed panel heading and close button");return visible;
      };
      NotesPanelRevealControl(window,*panel,hintControl);const auto firstVisible=visibleHint();
      Check(firstVisible.top==0,"Native scrolling can reveal the first line of the Details explanation even when the hint is taller than the viewport");
      LONG covered=firstVisible.bottom;const auto wholeHint=Bounds(window,IDC_NOTES_DETAILS_HINT);const LONG hintHeight=wholeHint.bottom-wholeHint.top;
      for(unsigned step=0;covered<hintHeight&&panel->scrollOffset<panel->scrollMax&&step<10000;++step){
        const auto before=panel->scrollOffset;SendMessageW(window,WM_VSCROLL,SB_PAGEDOWN,0);Check(panel->scrollOffset>before,"Native page scrolling advances through an explanation larger than one viewport");
        const auto interval=visibleHint();Check(interval.top<=covered&&interval.bottom>covered,"Successive native clipped hint intervals extend contiguous coverage without skipping any text");covered=interval.bottom;
      }
      SendMessageW(window,WM_VSCROLL,SB_BOTTOM,0);const auto finalHint=Bounds(window,IDC_NOTES_DETAILS_HINT);const auto lastVisible=visibleHint();
      Check(panel->scrollOffset==panel->scrollMax&&lastVisible.bottom==hintHeight&&covered==hintHeight&&lastVisible.top<=covered,"The native scroll interval union reaches the complete explanation through its final line without an unreachable gap");
      const auto measured=HintTextHeight(window);std::cout<<"Constrained Details hint width="<<finalHint.right-finalHint.left<<" height="<<finalHint.bottom-finalHint.top<<" wrappedTextHeight="<<measured<<" viewport="<<area.bottom-panel->scrollHeader<<" firstVisible="<<firstVisible.top<<","<<firstVisible.bottom<<" lastVisible="<<lastVisible.top<<","<<lastVisible.bottom<<std::endl;
      Check(measured<=finalHint.bottom-finalHint.top,"All actual wrapped explanation and statistics text fits the native hint control at the constrained panel width");
      const auto previous=panel->scrollOffset;SendMessageW(window,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);
      Check(panel->scrollOffset<previous,"Mouse wheel moves the constrained Details body without changing owner geometry");
      const auto originalUrl=NotesWindowText(GetDlgItem(window,IDC_NOTES_INPUT2));const int originalHeight=finalHint.bottom-finalHint.top,originalExtent=panel->scrollMax;
      SetDlgItemTextW(window,IDC_NOTES_INPUT2,L"invalid-url");KillTimer(window,1);SendDlgItemMessageW(window,IDC_NOTES_INPUT2,EM_SETSEL,2,7);const auto inputSelection=SendDlgItemMessageW(window,IDC_NOTES_INPUT2,EM_GETSEL,0,0);const auto focus=GetFocus();
      Check(!NotesApplyDetails(window,*panel),"An invalid Details URL produces the actual validation explanation for the geometry review");
      const auto errorHint=Bounds(window,IDC_NOTES_DETAILS_HINT);Check(errorHint.bottom-errorHint.top==(std::max)(ScaleByDpi(114,GetDpiForWindow(window)),static_cast<int>(HintTextHeight(window)))&&panel->scrollMax<=originalExtent,"A shorter validation explanation recomputes its native height and clamps the body scroll extent");
      Check(SendDlgItemMessageW(window,IDC_NOTES_INPUT2,EM_GETSEL,0,0)==inputSelection&&GetFocus()==focus,"Reflowing the Details explanation keeps the active input selection and focus unchanged");
      SetDlgItemTextW(window,IDC_NOTES_INPUT2,originalUrl.c_str());KillTimer(window,1);Check(NotesApplyDetails(window,*panel),"Restoring valid Details brings back the complete guidance and statistics");
      const auto restoredHint=Bounds(window,IDC_NOTES_DETAILS_HINT);Check(restoredHint.bottom-restoredHint.top==originalHeight&&panel->scrollMax==originalExtent&&NotesWindowText(hintControl).find(L"selected characters")!=std::wstring::npos,"Restored guidance regains its full measured height and scroll range without losing the statistics");
    } else {
      const int id=kind==1 ? IDC_NOTES_RESULTS : IDC_NOTES_CLIENT_MATCHES;HWND list=GetDlgItem(window,id);
      Check(GetWindowLongPtrW(list,GWL_STYLE)&WS_VSCROLL,"Capped action/client lists retain their native item scrolling");
      SendMessageW(window,WM_COMMAND,MAKEWPARAM(id,LBN_SETFOCUS),reinterpret_cast<LPARAM>(list));const auto field=Bounds(window,id);
      Check(field.top>=panel->scrollHeader && field.bottom<=area.bottom,"The narrowed panel can expose its real selectable list inside the viewport");
    }
    CloseNotesPanel(State(owner));
  }
  MINMAXINFO limits{};SendMessageW(owner,WM_GETMINMAXINFO,0,reinterpret_cast<LPARAM>(&limits));
  Check(limits.ptMinTrackSize.x<=geometryWork.right-geometryWork.left && limits.ptMinTrackSize.y<=geometryWork.bottom-geometryWork.top,"Notes minimum tracking dimensions never exceed the monitor work area");
  geometryWork={-450,-320,-100,-140};
  for(const int kind:{0,1,2}) {NotesShowPanel(owner,&State(owner),kind==0,kind==1);RECT bounds{};GetWindowRect(State(owner).panel,&bounds);Check(bounds.left>=geometryWork.left && bounds.top>=geometryWork.top && bounds.right<=geometryWork.right && bounds.bottom<=geometryWork.bottom,"Constrained panels remain inside a monitor work area with negative virtual-screen coordinates");if(kind==0){const auto hint=Bounds(State(owner).panel,IDC_NOTES_DETAILS_HINT);Check(HintTextHeight(State(owner).panel)<=hint.bottom-hint.top,"The negative-coordinate monitor's narrowed Details hint also fits every actual wrapped text line");}CloseNotesPanel(State(owner));}
  const auto workDpi=GetDpiForWindow(owner);geometryWork={130,90,130+ScaleByDpi(206,workDpi),90+ScaleByDpi(600,workDpi)};NotesShowPanel(owner,&State(owner),true);HWND tall=State(owner).panel;auto *tallPanel=reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(tall,DWLP_USER));const auto tallDpi=GetDpiForWindow(tall);RECT tallBounds{};GetWindowRect(tall,&tallBounds);
  Check(tallBounds.bottom-tallBounds.top==ScaleByDpi(502,tallDpi)&&tallPanel->scrollMax>0&&(GetWindowLongPtrW(tall,GWL_STYLE)&WS_VSCROLL),"A tall but narrow work area needs scrolling only for its measured wrapped explanation");
  const auto tallHint=Bounds(tall,IDC_NOTES_DETAILS_HINT);Check(HintTextHeight(tall)<=tallHint.bottom-tallHint.top,"The narrow tall panel's final hint measurement includes the actual native scrollbar width");
  PanelEvents events;unsigned idleStreams=0;SetWindowSubclass(tall,PanelChanges,389,reinterpret_cast<DWORD_PTR>(&events));SetWindowSubclass(Edit(owner),Streams,390,reinterpret_cast<DWORD_PTR>(&idleStreams));
  for(unsigned repeat=0;repeat<3;++repeat){NotesPanelLayout(tall,*tallPanel);Check(NotesApplyDetails(tall,*tallPanel),"Unchanged Details guidance and statistics can refresh repeatedly without a draft error");}
  RECT stableBounds{};GetWindowRect(tall,&stableBounds);const auto stableHint=Bounds(tall,IDC_NOTES_DETAILS_HINT);
  Check(!events.positions&&!events.scrollbarStyles&&!idleStreams&&EqualRect(&tallBounds,&stableBounds)&&EqualRect(&tallHint,&stableHint),"Repeated unchanged narrow-panel layout never toggles the scrollbar, repositions the native popup or streams its editor");
  RemoveWindowSubclass(Edit(owner),Streams,390);RemoveWindowSubclass(tall,PanelChanges,389);CloseNotesPanel(State(owner));
  geometryWork={130,90,1500,900};NotesShowPanel(owner,&State(owner),true);HWND normal=State(owner).panel;
  auto *normalPanel=reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(normal,DWLP_USER));RECT normalBounds{};GetWindowRect(normal,&normalBounds);const auto dpi=GetDpiForWindow(normal);
  Check(normalBounds.right-normalBounds.left==ScaleByDpi(390,dpi) && normalBounds.bottom-normalBounds.top==ScaleByDpi(502,dpi) && !normalPanel->scrollMax,"An unconstrained Details panel keeps its final information layout size without an outer scrollbar");
  const auto input=Bounds(normal,IDC_NOTES_INPUT1);Check(input.left==ScaleByDpi(16,dpi) && input.top==ScaleByDpi(200,dpi) && input.right-input.left==ScaleByDpi(358,dpi),"Unconstrained Details retains its original field spacing and width");
  const auto hint=Bounds(normal,IDC_NOTES_DETAILS_HINT);RECT normalArea{};GetClientRect(normal,&normalArea);const auto information=NotesWindowText(GetDlgItem(normal,IDC_NOTES_DETAILS_HINT));
  Check(hint.top==ScaleByDpi(378,dpi)&&hint.bottom-hint.top==ScaleByDpi(114,dpi)&&hint.bottom<=normalArea.bottom&&HintTextHeight(normal)<=hint.bottom-hint.top,"The normal native explanation uses its final 114-DIP height and shows every wrapped text line inside the panel");
  Check(information.find(L"words")!=std::wstring::npos&&information.find(L"characters")!=std::wstring::npos&&information.find(L"lines")!=std::wstring::npos&&information.find(L"selected characters")!=std::wstring::npos,"The final Details explanation includes document and selection statistics alongside browser guidance");CloseNotesPanel(State(owner));
  geometryWork={};DestroyWindow(owner);
}
}
static void NotesGeometryRound3(std::wstring_view only) {
  if(only.empty() || only==L"tab-growth") notes_geometry::TabGrowthViewport();
  if(only.empty() || only==L"empty") notes_geometry::EmptyEditorLayout();
  if(only==L"empty-theme") notes_geometry::EmptyEditorLayout(L"theme");
  if(only==L"empty-rename") notes_geometry::EmptyEditorLayout(L"rename");
  if(only.empty() || only==L"layout") notes_geometry::LayoutFaults();
  if(only.empty() || only==L"panels") notes_geometry::PopupBounds();
}
