// Temporary native panels. Their owner stays enabled: clicking the editor or
// another window dismisses the panel without changing the editor's layout.
struct NotesPanel {
  HWND owner=nullptr;
  ClientNotesDialogState *state=nullptr;
  uint64_t note=0;
  bool details=false, actions=false, loading=true;
  NotesActionsState actionState;
  std::vector<std::wstring> clients, matches;
  int scrollOffset=0,scrollMax=0,scrollHeader=0;
};
static HWND g_hNotesClientPanel=nullptr;
static void CloseNotesPanel(ClientNotesDialogState &state) {
  if (state.panel && IsWindow(state.panel)) DestroyWindow(state.panel);
  state.panel=nullptr;
}
static bool NotesClientAvailable(const std::wstring &client) {
  fs::path root;
  return IsExistingClientProfile(client) && !IsClientArchived(client) &&
      TryGetSafeClientProfilePath(client,root) && RevalidateSafeClientContainerPath(client,root);
}
static std::wstring NotesRememberedClient(const wchar_t *key) {
  // Keep the full supported identifier, and reject an overlong stored value
  // rather than interpreting its truncated prefix as another client's name.
  wchar_t client[kMaxClientNameLength + 2]{};
  const DWORD length = GetPrivateProfileStringW(L"notes_window",key,L"",client,
      static_cast<DWORD>(std::size(client)),g_sConfigPath.c_str());
  if (!length || length >= std::size(client)-1)
    return {};
  const std::wstring name(client,length);
  return SanitizeName(name)==name ? name : std::wstring{};
}
static std::vector<std::wstring> NotesRecentClients() {
  std::vector<std::wstring> clients;
  for (int i=0;i<6;++i) {
    const auto key=i ? L"recent"+std::to_wstring(i) : L"last_client";
    const auto client=NotesRememberedClient(key.c_str());
    if (!client.empty() && std::none_of(clients.begin(),clients.end(),[&](const auto &c){return client_notes::SamePageName(c,client);})) clients.emplace_back(client);
  }
  return clients;
}
static void NotesRememberClient(const std::wstring &client) {
  auto recent=NotesRecentClients();
  recent.erase(std::remove_if(recent.begin(),recent.end(),[&](const auto &c){return client_notes::SamePageName(c,client);}),recent.end());
  recent.insert(recent.begin(),client);
  std::vector<config_persistence::IniMutation> changes; // Same atomic config path as window dimensions.
  changes.push_back({L"notes_window",std::wstring(L"last_client"),client_preference_encoding::ForIni(client)});
  for (size_t i=1;i<=5;++i) changes.push_back({L"notes_window",L"recent"+std::to_wstring(i),i<recent.size() ? client_preference_encoding::ForIni(recent[i]) : L""});
  SaveConfigMutations(L"recent notes clients",changes,false);
}
static std::vector<std::wstring> NotesNavigationClients() {
  auto clients=NotesDestinationClients(L"");
  const auto recent=NotesRecentClients();
  const auto rank=[&](const auto &client) {
    if (std::any_of(g_pinnedClients.begin(),g_pinnedClients.end(),[&](const auto &pin){return client_notes::SamePageName(pin,client);})) return 0;
    for (size_t i=0;i<recent.size();++i) if (client_notes::SamePageName(client,recent[i])) return static_cast<int>(i)+1;
    return 8;
  };
  std::stable_sort(clients.begin(),clients.end(),[&](const auto &a,const auto &b){return rank(a)<rank(b);});
  return clients;
}
static int NotesPanelHintHeight(HWND dialog,int width,int minimum) {
  const HWND hint=GetDlgItem(dialog,IDC_NOTES_DETAILS_HINT);const auto text=NotesWindowText(hint);HDC dc=GetDC(hint);
  if(!dc)return minimum;
  const auto font=reinterpret_cast<HFONT>(SendMessageW(hint,WM_GETFONT,0,0));const auto previous=SelectObject(dc,font?font:GetStockObject(DEFAULT_GUI_FONT));
  RECT measured{0,0,(std::max)(1,width),0};DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&measured,DT_CALCRECT|DT_WORDBREAK|DT_NOPREFIX);
  SelectObject(dc,previous);ReleaseDC(hint,dc);return (std::max)(minimum,static_cast<int>(measured.bottom-measured.top));
}
static void NotesPanelLayout(HWND dialog,NotesPanel &panel) {
  const auto px=[&](int value){return ScaleByDpi(value,GetDpiForWindow(dialog));};
  const int width=panel.details || panel.actions ? 390 : 360;
  const int height=panel.details ? 502 : panel.actions ? 414 : 122+static_cast<int>((std::max)(size_t{1},panel.matches.size()))*28;
  const auto work=NotesWorkArea(panel.owner ? panel.owner : dialog);
  const int outerWidth=(std::min)(px(width),(std::max)(1,static_cast<int>(work.right-work.left)));
  const int outerHeight=(std::min)(px(height),(std::max)(1,static_cast<int>(work.bottom-work.top)));
  RECT bounds{};GetWindowRect(dialog,&bounds);
  const int left=(std::clamp)(static_cast<int>(bounds.left),static_cast<int>(work.left),static_cast<int>(work.right)-outerWidth);
  const int top=(std::clamp)(static_cast<int>(bounds.top),static_cast<int>(work.top),static_cast<int>(work.bottom)-outerHeight);
  if(bounds.left!=left||bounds.top!=top||bounds.right-bounds.left!=outerWidth||bounds.bottom-bounds.top!=outerHeight)
    SetWindowPos(dialog,nullptr,left,top,outerWidth,outerHeight,SWP_NOZORDER|SWP_NOACTIVATE);
  int contentHeight=panel.details ? px(height) : (std::max)(outerHeight,px(panel.actions ? 226 : 150));
  const auto setScrollbar=[&](bool visible){if(((GetWindowLongPtrW(dialog,GWL_STYLE)&WS_VSCROLL)!=0)!=visible)ShowScrollBar(dialog,SB_VERT,visible);};
  RECT area{};GetClientRect(dialog,&area);int hintHeight=px(114);
  if(panel.details){
    const int available=static_cast<int>(area.right)+((GetWindowLongPtrW(dialog,GWL_STYLE)&WS_VSCROLL)?GetSystemMetricsForDpi(SM_CXVSCROLL,GetDpiForWindow(dialog)):0);
    hintHeight=NotesPanelHintHeight(dialog,available-px(32),px(114));contentHeight=(std::max)(px(height),px(378)+hintHeight+px(10));
  }
  // Decide from the full width first, avoiding hide/show churn when repeated
  // metadata updates still need the existing scrollbar.
  setScrollbar(contentHeight>outerHeight);GetClientRect(dialog,&area);
  if(panel.details){
    const auto measureHint=[&]{hintHeight=NotesPanelHintHeight(dialog,static_cast<int>(area.right)-px(32),px(114));contentHeight=(std::max)(px(height),px(378)+hintHeight+px(10));};
    measureHint();
    // A scrollbar reduces the real field width. Measure again after it is
    // present so its extra wrapping also contributes to the scroll extent.
    if(contentHeight>area.bottom&&!(GetWindowLongPtrW(dialog,GWL_STYLE)&WS_VSCROLL)){setScrollbar(true);GetClientRect(dialog,&area);measureHint();}
  }
  panel.scrollHeader=px(44);panel.scrollMax=(std::max)(0,contentHeight-static_cast<int>(area.bottom));
  panel.scrollOffset=(std::clamp)(panel.scrollOffset,0,panel.scrollMax);
  SCROLLINFO scroll{sizeof(scroll),SIF_RANGE|SIF_PAGE|SIF_POS};scroll.nMax=(std::max)(0,contentHeight-panel.scrollHeader-1);
  scroll.nPage=static_cast<UINT>((std::max)(1,static_cast<int>(area.bottom)-panel.scrollHeader));scroll.nPos=panel.scrollOffset;
  SetScrollInfo(dialog,SB_VERT,&scroll,TRUE);
  const int fieldWidth=(std::max)(1,static_cast<int>(area.right)-px(32));
  const auto placeControl=[&](HWND control,int x,int y,int w,int h,bool body) {
    if(!control) return;
    if(body) y-=panel.scrollOffset;
    RECT old{};GetWindowRect(control,&old);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&old),2);
    if(old.left!=x || old.top!=y || old.right-old.left!=w || old.bottom-old.top!=h)
      SetWindowPos(control,nullptr,x,y,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
    if(body) {
      RECT actual{};GetWindowRect(control,&actual);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&actual),2);
      HRGN clip=nullptr;
      if(panel.scrollMax) {
        const int clipHeight=static_cast<int>(actual.bottom-actual.top);
        const int first=(std::clamp)(panel.scrollHeader-static_cast<int>(actual.top),0,clipHeight);
        const int last=(std::clamp)(static_cast<int>(area.bottom-actual.top),0,clipHeight);
        clip=last>first ? CreateRectRgn(0,first,(std::max)(0,static_cast<int>(actual.right-actual.left)),last) : CreateRectRgn(0,0,0,0);
      }
      SetWindowRgn(control,clip,TRUE);
    }
  };
  const auto place=[&](int id,int y,int h) {placeControl(GetDlgItem(dialog,id),px(16),y,fieldWidth,h,true);};
  placeControl(GetDlgItem(dialog,IDC_NOTES_LABEL1),px(16),px(14),(std::max)(1,static_cast<int>(area.right)-px(68)),px(22),false);
  placeControl(GetDlgItem(dialog,IDCANCEL),(std::max)(0,static_cast<int>(area.right)-px(44)),px(8),px(28),px(28),false);
  if (panel.details) {
    // Static captions are anonymous resource controls. Place them in source
    // order so every edit has its label and no excess dialog-unit padding.
    int caption=0;
    for (HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) if (GetDlgCtrlID(child)==IDC_STATIC || GetDlgCtrlID(child)==65535) {
      placeControl(child,px(16),px(46+caption*66),fieldWidth,px(20),true);++caption;
    }
    place(IDC_NOTES_STATUS_PICK,px(68),px(180));
    place(IDC_NOTES_TAGS_EDIT,px(134),px(30));
    place(IDC_NOTES_INPUT1,px(200),px(30));place(IDC_NOTES_INPUT2,px(266),px(30));
    place(IDC_NOTES_LINK_BROWSER_PICK,px(332),px(180));
    place(IDC_NOTES_DETAILS_HINT,px(378),hintHeight);
  } else if (panel.actions) {
    const int listHeight=(std::min)(px(252),(std::max)(px(66),contentHeight-px(162)));
    place(IDC_NOTES_QUERY,px(46),px(30));place(IDC_NOTES_RESULTS,px(88),listHeight);
    place(IDC_NOTES_INFO,px(98)+listHeight,px(52));
  } else {
    place(IDC_NOTES_CLIENT_SEARCH,px(46),px(30));place(IDC_NOTES_LABEL2,px(86),px(28));
    place(IDC_NOTES_CLIENT_MATCHES,px(114),(std::max)(px(28),contentHeight-px(122)));
    SendDlgItemMessageW(dialog,IDC_NOTES_CLIENT_MATCHES,LB_SETITEMHEIGHT,0,px(28));
    HWND list=GetDlgItem(dialog,IDC_NOTES_CLIENT_MATCHES);RECT visible{};GetClientRect(list,&visible);
    const auto rows=SendMessageW(list,LB_GETCOUNT,0,0),rowHeight=SendMessageW(list,LB_GETITEMHEIGHT,0,0);
    ShowScrollBar(list,SB_VERT,rows>0 && rowHeight>0 && rows*rowHeight>visible.bottom);
  }
  InvalidateRect(dialog,nullptr,FALSE);
}
static void NotesPanelScroll(HWND dialog,NotesPanel &panel,int position) {
  position=(std::clamp)(position,0,panel.scrollMax);
  if(position!=panel.scrollOffset){panel.scrollOffset=position;NotesPanelLayout(dialog,panel);}
}
static void NotesPanelRevealControl(HWND dialog,NotesPanel &panel,HWND child) {
  if(!panel.scrollMax || !child || GetParent(child)!=dialog) return;
  RECT bounds{},area{};GetWindowRect(child,&bounds);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&bounds),2);GetClientRect(dialog,&area);
  int position=panel.scrollOffset;
  if(bounds.top<panel.scrollHeader || bounds.bottom-bounds.top>area.bottom-panel.scrollHeader) position+=static_cast<int>(bounds.top)-panel.scrollHeader;
  else if(bounds.bottom>area.bottom) position+=static_cast<int>(bounds.bottom-area.bottom);
  NotesPanelScroll(dialog,panel,position);
}
static void NotesPaintPanel(HWND dialog,HDC dc) {
  RECT area{};GetClientRect(dialog,&area);FillRect(dc,&area,g_hbrThemeWindow);FrameRect(dc,&area,g_hbrThemeBorder);
  const int saved=SaveDC(dc);auto *panel=reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if(panel && panel->scrollMax) IntersectClipRect(dc,0,panel->scrollHeader,area.right,area.bottom);
  for (int id:{IDC_NOTES_CLIENT_SEARCH,IDC_NOTES_CLIENT_MATCHES,IDC_NOTES_QUERY,IDC_NOTES_RESULTS,
      IDC_NOTES_TAGS_EDIT,IDC_NOTES_INPUT1,IDC_NOTES_INPUT2}) {
    HWND control=GetDlgItem(dialog,id);if (!control || !IsWindowVisible(control)) continue;
    RECT rect{};GetWindowRect(control,&rect);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&rect),2);InflateRect(&rect,1,1);
    FrameRect(dc,&rect,g_hbrThemeBorder);
  }
  RestoreDC(dc,saved);
}
static void NotesPanelMatches(HWND dialog,NotesPanel &panel) {
  const auto query=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_CLIENT_SEARCH));
  auto list=GetDlgItem(dialog,IDC_NOTES_CLIENT_MATCHES);
  SendMessageW(list,LB_RESETCONTENT,0,0); panel.matches.clear();
  // An exact name must remain reachable even when eight pinned/recent clients
  // contain that name. Preserve the normal order for all other suggestions.
  auto clients=panel.clients;
  if (!query.empty()) {
    const auto exact=std::find_if(clients.begin(),clients.end(),[&](const auto &client){return client_name_comparison::EqualIgnoringCase(client,query);});
    if (exact!=clients.end()) std::rotate(clients.begin(),exact,exact+1);
  }
  for (const auto &client:clients) {
    if (!query.empty() && !StrStrIW(client.c_str(),query.c_str())) continue;
    panel.matches.push_back(client);
    std::wstring label=client;
    if (panel.state && client_notes::SamePageName(client,panel.state->clientName)) label+=L"  (current)";
    else if (std::any_of(g_pinnedClients.begin(),g_pinnedClients.end(),[&](const auto &p){return client_notes::SamePageName(client,p);})) label+=L"  (pinned)";
    SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
    if (panel.matches.size()==8) break;
  }
  SendMessageW(list,LB_SETCURSEL,panel.matches.empty() ? -1 : 0,0);
  SetDlgItemTextW(dialog,IDC_NOTES_LABEL2,panel.matches.empty() ? L"No matching active clients." : L"Pinned and recent first. Type to search all clients.");
  NotesPanelLayout(dialog,panel);
}
static bool NotesApplyDetails(HWND dialog,NotesPanel &panel) {
  if (!panel.details || panel.loading || !panel.state || !IsWindow(panel.owner)) return true;
  auto &state=*panel.state;
  // Resolve the captured ID in its original notebook. A stale panel cannot
  // update whichever note happens to be selected later.
  auto page=std::find_if(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==panel.note;});
  if (page==state.pages.end() || !HasActiveNote(state) || state.pages[state.activePage].id!=panel.note) return false;
  const auto ticket=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_INPUT1));
  const auto url=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_INPUT2));
  const auto status=SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_PICK,CB_GETCURSEL,0,0);
  std::vector<std::wstring> tags;
  if (status<0 || status>4 || (ticket.size()>128 || std::any_of(ticket.begin(),ticket.end(),[](wchar_t c){return c<32;})) ||
      (!url.empty() && !ValidNotesUrl(url)) || !NotesParseTags(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_TAGS_EDIT)),tags)) {
    state.detailsDrafts[panel.note]={std::to_wstring(status),NotesWindowText(GetDlgItem(dialog,IDC_NOTES_TAGS_EDIT)),ticket,url};
    SetNotesStatus(panel.owner,state);
    SetDlgItemTextW(dialog,IDC_NOTES_DETAILS_HINT,L"Not saved: use an http/https URL and up to 16 tags of 32 characters each.");
    NotesPanelLayout(dialog,panel);
    return false;
  }
  state.detailsDrafts.erase(panel.note);
  if (page->ticket!=ticket || page->ticketUrl!=url || page->status!=static_cast<uint32_t>(status) || page->tags!=tags) {
    page->ticket=ticket;page->ticketUrl=url;page->status=static_cast<uint32_t>(status);page->tags=std::move(tags);
    page->pristine=false;page->modified=client_notes::Now();state.metadataChanged=true;
    SaveClientNotesDraft(panel.owner,state);UpdateNotesToolbar(panel.owner,state);
  }
  const auto information=state.saveFailed ? std::wstring(L"Could not save. Your changes stay in the notebook draft; use More > Retry saving.") :
      L"Links use your regular PC browser, outside client profiles. Changes save automatically. Status does not update your ticket system.\r\n\r\n"+NotesStatisticsText(GetDlgItem(panel.owner,IDC_CLIENT_NOTES_TEXT));
  SetDlgItemTextW(dialog,IDC_NOTES_DETAILS_HINT,information.c_str());
  NotesPanelLayout(dialog,panel);
  SetNotesStatus(panel.owner,state);
  return true;
}
static void NotesRequestClient(HWND owner,const std::wstring &client,uint64_t note=0) {
  auto request=std::make_unique<NotesClientRequest>();request->client=client;request->note=note;
  if (PostMessageW(owner,WM_APP_SWITCH_NOTES_CLIENT,0,reinterpret_cast<LPARAM>(request.get()))) request.release();
}
static LRESULT CALLBACK NotesClientSearchKey(HWND edit,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR) {
  if (message==WM_KEYDOWN && w==VK_DOWN) {
    SetFocus(GetDlgItem(GetParent(edit),IDC_NOTES_CLIENT_MATCHES));return 0;
  }
  if (message==WM_NCDESTROY) RemoveWindowSubclass(edit,NotesClientSearchKey,id);
  return DefSubclassProc(edit,message,w,l);
}
static LRESULT CALLBACK NotesClientListKey(HWND list,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR) {
  const auto result=DefSubclassProc(list,message,w,l);
  if (message==WM_LBUTTONUP) PostMessageW(GetParent(list),WM_COMMAND,IDOK,0);
  if (message==WM_NCDESTROY) RemoveWindowSubclass(list,NotesClientListKey,id);
  return result;
}
static INT_PTR CALLBACK NotesPanelProc(HWND dialog,UINT message,WPARAM w,LPARAM l) {
  auto *panel=reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (message==WM_INITDIALOG) {
    panel=reinterpret_cast<NotesPanel *>(l);SetWindowLongPtrW(dialog,DWLP_USER,l);
    SetWindowLongPtrW(dialog,GWL_STYLE,(GetWindowLongPtrW(dialog,GWL_STYLE)&~WS_BORDER)|WS_CLIPCHILDREN);
    SetWindowPos(dialog,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    ApplyCleanupDialogTheme(dialog);
    if (panel->actions) {
      panel->actionState.active=panel->state && HasActiveNote(*panel->state);
      NotesActionsFill(dialog,panel->actionState);
      SendDlgItemMessageW(dialog,IDC_NOTES_QUERY,EM_SETCUEBANNER,FALSE,reinterpret_cast<LPARAM>(L"Search actions...  Ctrl+K"));
      SetWindowSubclass(GetDlgItem(dialog,IDC_NOTES_RESULTS),NotesClientListKey,1,0);
      ShowWindow(GetDlgItem(dialog,IDOK),SW_HIDE);
    } else if (panel->details) {
      const auto &page=panel->state->pages[panel->state->activePage];panel->note=page.id;
      for (auto status:kNotesStatuses) SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_PICK,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(status));
      auto combo=GetDlgItem(dialog,IDC_NOTES_STATUS_PICK);SetWindowSubclass(combo,NotesComboSubclass,1,0);
      const auto height=ScaleByDpi(22,GetDpiForWindow(dialog));
      SendMessageW(combo,CB_SETITEMHEIGHT,-1,height);SendMessageW(combo,CB_SETITEMHEIGHT,0,height);
      SendMessageW(combo,CB_SETCURSEL,page.status,0);
      SetDlgItemTextW(dialog,IDC_NOTES_TAGS_EDIT,NotesTagsText(page).c_str());
      SetDlgItemTextW(dialog,IDC_NOTES_INPUT1,page.ticket.c_str());SetDlgItemTextW(dialog,IDC_NOTES_INPUT2,page.ticketUrl.c_str());
      if (auto draft=panel->state->detailsDrafts.find(page.id);draft!=panel->state->detailsDrafts.end()) {
        SendMessageW(combo,CB_SETCURSEL,std::clamp(_wtoi(draft->second[0].c_str()),0,4),0);
        SetDlgItemTextW(dialog,IDC_NOTES_TAGS_EDIT,draft->second[1].c_str());
        SetDlgItemTextW(dialog,IDC_NOTES_INPUT1,draft->second[2].c_str());SetDlgItemTextW(dialog,IDC_NOTES_INPUT2,draft->second[3].c_str());
      }
      for (auto [id,limit]:{std::pair{IDC_NOTES_TAGS_EDIT,1024},{IDC_NOTES_INPUT1,128},{IDC_NOTES_INPUT2,2048}})
        SendDlgItemMessageW(dialog,id,EM_SETLIMITTEXT,limit,0);
      auto browser=GetDlgItem(dialog,IDC_NOTES_LINK_BROWSER_PICK);SetWindowSubclass(browser,NotesComboSubclass,1,0);
      SendMessageW(browser,CB_SETITEMHEIGHT,-1,height);SendMessageW(browser,CB_SETITEMHEIGHT,0,height);
      NotesFillLinkBrowser(browser);
      const auto information=L"Links use your regular PC browser, outside client profiles. Changes save automatically. Status does not update your ticket system.\r\n\r\n"+NotesStatisticsText(GetDlgItem(panel->owner,IDC_CLIENT_NOTES_TEXT));
      SetDlgItemTextW(dialog,IDC_NOTES_DETAILS_HINT,information.c_str());
    } else {
      panel->clients=NotesNavigationClients();NotesPanelMatches(dialog,*panel);
      SendDlgItemMessageW(dialog,IDC_NOTES_CLIENT_SEARCH,EM_SETCUEBANNER,FALSE,reinterpret_cast<LPARAM>(L"Search clients..."));
      SendDlgItemMessageW(dialog,IDC_NOTES_CLIENT_SEARCH,EM_SETLIMITTEXT,kMaxClientNameLength,0);
      SetWindowSubclass(GetDlgItem(dialog,IDC_NOTES_CLIENT_SEARCH),NotesClientSearchKey,1,0);
      SetWindowSubclass(GetDlgItem(dialog,IDC_NOTES_CLIENT_MATCHES),NotesClientListKey,1,0);
      ShowWindow(GetDlgItem(dialog,IDOK),SW_HIDE);
    }
    NotesPanelLayout(dialog,*panel);panel->loading=false;return TRUE;
  }
  if (!panel) return FALSE;
  if(message==WM_ERASEBKGND) return TRUE; // Our paint owns the header and clipped body together.
  if(message==WM_VSCROLL && !l && panel->scrollMax) {
    SCROLLINFO scroll{sizeof(scroll),SIF_TRACKPOS};GetScrollInfo(dialog,SB_VERT,&scroll);
    int position=panel->scrollOffset;RECT area{};GetClientRect(dialog,&area);const int line=ScaleByDpi(28,GetDpiForWindow(dialog));
    switch(LOWORD(w)){case SB_LINEUP:position-=line;break;case SB_LINEDOWN:position+=line;break;case SB_PAGEUP:position-=static_cast<int>(area.bottom)-panel->scrollHeader;break;case SB_PAGEDOWN:position+=static_cast<int>(area.bottom)-panel->scrollHeader;break;case SB_TOP:position=0;break;case SB_BOTTOM:position=panel->scrollMax;break;case SB_THUMBTRACK:case SB_THUMBPOSITION:position=scroll.nTrackPos;break;default:return TRUE;}
    NotesPanelScroll(dialog,*panel,position);return TRUE;
  }
  if(message==WM_MOUSEWHEEL && panel->scrollMax) {NotesPanelScroll(dialog,*panel,panel->scrollOffset-MulDiv(GET_WHEEL_DELTA_WPARAM(w),ScaleByDpi(48,GetDpiForWindow(dialog)),WHEEL_DELTA));return TRUE;}
  if (message==WM_PAINT) {PAINTSTRUCT paint{};auto dc=BeginPaint(dialog,&paint);NotesPaintPanel(dialog,dc);EndPaint(dialog,&paint);return TRUE;}
  if (message==WM_PRINTCLIENT) {NotesPaintPanel(dialog,reinterpret_cast<HDC>(w));return TRUE;}
  if (message==WM_DRAWITEM && l && DrawNotesComboItem(*reinterpret_cast<DRAWITEMSTRUCT *>(l))) return TRUE;
  if (message==WM_ACTIVATE && LOWORD(w)==WA_INACTIVE) {
    // Native combo list windows belong to this panel. Their interaction must
    // not be mistaken for a click outside.
    HWND target=reinterpret_cast<HWND>(l);
    if (!target || (target!=dialog && !IsChild(dialog,target) && GetAncestor(target,GA_ROOTOWNER)!=dialog))
      PostMessageW(dialog,WM_CLOSE,0,0);
    return FALSE;
  }
  if (message==WM_TIMER && w==1) {KillTimer(dialog,1);NotesApplyDetails(dialog,*panel);return TRUE;}
  if (message==WM_COMMAND) {
    const int id=LOWORD(w);
    wchar_t kind[32]{};HWND sender=reinterpret_cast<HWND>(l);
    if(sender && id!=IDCANCEL && GetClassNameW(sender,kind,static_cast<int>(std::size(kind))) &&
        ((!_wcsicmp(kind,L"Edit") && HIWORD(w)==EN_SETFOCUS) ||
         (!_wcsicmp(kind,L"ComboBox") && HIWORD(w)==CBN_SETFOCUS) ||
         (!_wcsicmp(kind,L"ListBox") && HIWORD(w)==LBN_SETFOCUS))) NotesPanelRevealControl(dialog,*panel,sender);
    if (id==IDCANCEL) {DestroyWindow(dialog);return TRUE;}
    if (panel->actions) {
      if (id==IDC_NOTES_QUERY && HIWORD(w)==EN_CHANGE) {NotesActionsFill(dialog,panel->actionState);return TRUE;}
      const auto row=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);
      if (row<0 || static_cast<size_t>(row)>=panel->actionState.results.size()) return TRUE;
      const auto &action=kNotesActions[panel->actionState.results[row]];
      if (id==IDC_NOTES_RESULTS && HIWORD(w)==LBN_SELCHANGE) SetDlgItemTextW(dialog,IDC_NOTES_INFO,action.hint);
      if (id==IDOK || (id==IDC_NOTES_RESULTS && HIWORD(w)==LBN_DBLCLK)) {
        HWND owner=panel->owner;const auto command=action.id;DestroyWindow(dialog);PostMessageW(owner,WM_COMMAND,command,0);
      }
      return TRUE;
    }
    if (panel->details) {
      if (id==IDC_NOTES_LINK_BROWSER_PICK) {
        HWND combo=GetDlgItem(dialog,id);
        if (HIWORD(w)==CBN_SELENDCANCEL) {NotesFillLinkBrowser(combo);return TRUE;}
        if (HIWORD(w)!=CBN_SELENDOK &&
            (HIWORD(w)!=CBN_SELCHANGE || SendMessageW(combo,CB_GETDROPPEDSTATE,0,0))) return TRUE;
        const auto row=SendMessageW(combo,CB_GETCURSEL,0,0);
        bool saved=false;
        if (row>=0) {
          const auto choice=SendMessageW(combo,CB_GETITEMDATA,row,0);
          if (choice!=CB_ERR) saved=NotesSaveLinkBrowser(static_cast<UINT>(choice));
        }
        if (!saved) NotesFillLinkBrowser(combo);
        return TRUE;
      }
      if (HIWORD(w)==EN_CHANGE || (id==IDC_NOTES_STATUS_PICK && HIWORD(w)==CBN_SELCHANGE)) SetTimer(dialog,1,400,nullptr);
      return TRUE;
    }
    if (id==IDC_NOTES_CLIENT_SEARCH && HIWORD(w)==EN_CHANGE) {NotesPanelMatches(dialog,*panel);return TRUE;}
    if (id==IDOK || (id==IDC_NOTES_CLIENT_MATCHES && HIWORD(w)==LBN_DBLCLK)) {
      const auto row=SendDlgItemMessageW(dialog,IDC_NOTES_CLIENT_MATCHES,LB_GETCURSEL,0,0);
      if (row>=0 && static_cast<size_t>(row)<panel->matches.size()) {
        auto client=panel->matches[row];HWND owner=panel->owner;
        DestroyWindow(dialog);NotesRequestClient(owner,client);return TRUE;
      }
    }
  }
  if (message==WM_CLOSE) {DestroyWindow(dialog);return TRUE;}
  if (message==WM_DESTROY) {
    KillTimer(dialog,1);NotesApplyDetails(dialog,*panel);
    if (panel->state && panel->state->panel==dialog) panel->state->panel=nullptr;
    if (g_hNotesClientPanel==dialog) g_hNotesClientPanel=nullptr;
    return FALSE;
  }
  if (message==WM_NCDESTROY) {HandleCleanupDialogTheme(dialog,message,w,l);SetWindowLongPtrW(dialog,DWLP_USER,0);delete panel;return FALSE;}
  if (const auto theme=HandleCleanupDialogTheme(dialog,message,w,l)) return *theme;
  return FALSE;
}
static bool ProcessNotesPanelMessage(MSG &message) {
  HWND panel=g_hNotesClientPanel;
  if (g_hNotesWindow) {
    auto *state=reinterpret_cast<ClientNotesDialogState *>(GetWindowLongPtrW(g_hNotesWindow,DWLP_USER));
    if (state && state->panel) panel=state->panel;
  }
  if (!panel || !IsWindow(panel) || (message.hwnd!=panel && !IsChild(panel,message.hwnd))) return false;
  if (message.message==WM_KEYDOWN && (message.wParam==VK_ESCAPE || message.wParam==VK_RETURN)) {
    HWND focus=GetFocus();wchar_t kind[32]{};
    if (focus && GetClassNameW(focus,kind,static_cast<int>(std::size(kind))) &&
        !_wcsicmp(kind,L"ComboBox") && SendMessageW(focus,CB_GETDROPPEDSTATE,0,0))
      return IsDialogMessageW(panel,&message)!=FALSE;
    auto *data=reinterpret_cast<NotesPanel *>(GetWindowLongPtrW(panel,DWLP_USER));
    SendMessageW(panel,WM_COMMAND,message.wParam==VK_ESCAPE || (data && data->details) ? IDCANCEL : IDOK,0);return true;
  }
  return IsDialogMessageW(panel,&message)!=FALSE;
}
static void NotesShowPanel(HWND owner,ClientNotesDialogState *state,bool details,bool actions=false) {
  if (state) CloseNotesPanel(*state);
  if (g_hNotesClientPanel && IsWindow(g_hNotesClientPanel)) DestroyWindow(g_hNotesClientPanel);
  auto *panel=new NotesPanel;panel->owner=owner;panel->state=state;panel->details=details;panel->actions=actions;
  HWND window=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(details ? IDD_NOTES_DETAILS : actions ? IDD_NOTES_ACTION_PANEL : IDD_NOTES_SWITCH_CLIENT),owner,NotesPanelProc,reinterpret_cast<LPARAM>(panel));
  if (!window) {delete panel;return;}
  if (state) state->panel=window;
  if (!details && !actions) g_hNotesClientPanel=window;
  RECT anchor{};GetWindowRect(state ? GetDlgItem(owner,details ? IDC_NOTES_DETAILS : actions ? IDC_NOTES_MORE : IDC_NOTES_SWITCH_CLIENT) : g_hBtnNotes,&anchor);
  RECT bounds{};GetWindowRect(window,&bounds);
  const auto work=NotesWorkArea(owner);
  const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
  const int left=std::clamp(static_cast<int>(details || actions ? anchor.right-width : anchor.left),static_cast<int>(work.left),(std::max)(static_cast<int>(work.left),static_cast<int>(work.right)-width));
  const int top=std::clamp(static_cast<int>(anchor.bottom+6),static_cast<int>(work.top),(std::max)(static_cast<int>(work.top),static_cast<int>(work.bottom)-height));
  SetWindowPos(window,HWND_TOP,left,top,0,0,SWP_NOSIZE|SWP_SHOWWINDOW);
  SetFocus(GetDlgItem(window,details ? IDC_NOTES_STATUS_PICK : actions ? IDC_NOTES_QUERY : IDC_NOTES_CLIENT_SEARCH));
}
static void OpenRequestedNotesClient(LPARAM data) {
  std::unique_ptr<NotesClientRequest> request(reinterpret_cast<NotesClientRequest *>(data));
  if (!request || !NotesClientAvailable(request->client)) return;
  ShowClientNotes(request->client);
  if (!request->note || !g_hNotesWindow) return;
  auto &state=*reinterpret_cast<ClientNotesDialogState *>(GetWindowLongPtrW(g_hNotesWindow,DWLP_USER));
  if (!client_notes::SamePageName(state.clientName,request->client)) return; // Save failure kept the old notebook.
  for (size_t i=0;i<state.pages.size();++i) if (state.pages[i].id==request->note && !state.pages[i].archived && !state.pages[i].deleted) {
    NotesOpenSavedPage(g_hNotesWindow,state,i);return;
  }
}
static void ShowNotesFromMain() {
  if (!g_bUiEnabled || g_bClientNotesBusy.load()) return;
  auto client=GetSelectedClientNameSanitized(false);
  if (!NotesClientAvailable(client)) {
    client=NotesRememberedClient(L"last_client");
  }
  if (NotesClientAvailable(client)) ShowClientNotes(client);
  else NotesShowPanel(g_hGui,nullptr,false);
}
