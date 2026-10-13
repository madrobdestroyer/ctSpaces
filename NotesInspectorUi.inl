// Temporary native panels. Their owner stays enabled: clicking the editor or
// another window dismisses the panel without changing the editor's layout.
struct NotesPanel {
  HWND owner=nullptr;
  ClientNotesDialogState *state=nullptr;
  uint64_t note=0;
  bool details=false, actions=false, loading=true;
  NotesActionsState actionState;
  std::vector<std::wstring> clients, matches;
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
static std::vector<std::wstring> NotesRecentClients() {
  std::vector<std::wstring> clients;
  for (int i=0;i<6;++i) {
    wchar_t client[65]{};const auto key=i ? L"recent"+std::to_wstring(i) : L"last_client";
    GetPrivateProfileStringW(L"notes_window",key.c_str(),L"",client,65,g_sConfigPath.c_str());
    if (*client && std::none_of(clients.begin(),clients.end(),[&](const auto &c){return client_notes::SamePageName(c,client);})) clients.emplace_back(client);
  }
  return clients;
}
static void NotesRememberClient(const std::wstring &client) {
  auto recent=NotesRecentClients();
  recent.erase(std::remove_if(recent.begin(),recent.end(),[&](const auto &c){return client_notes::SamePageName(c,client);}),recent.end());
  recent.insert(recent.begin(),client);
  std::vector<config_persistence::IniMutation> changes; // Same atomic config path as window dimensions.
  changes.push_back({L"notes_window",std::wstring(L"last_client"),client});
  for (size_t i=1;i<=5;++i) changes.push_back({L"notes_window",L"recent"+std::to_wstring(i),i<recent.size() ? recent[i] : L""});
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
static void NotesPanelLayout(HWND dialog,const NotesPanel &panel) {
  const auto px=[&](int value){return ScaleByDpi(value,GetDpiForWindow(dialog));};
  const int width=panel.details || panel.actions ? 390 : 360;
  const int height=panel.details ? 382 : panel.actions ? 414 : 122+static_cast<int>((std::max)(size_t{1},panel.matches.size()))*28;
  SetWindowPos(dialog,nullptr,0,0,px(width),px(height),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  const auto place=[&](int id,int x,int y,int w,int h) {
    HWND control=GetDlgItem(dialog,id);RECT old{};GetWindowRect(control,&old);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&old),2);
    if (old.left!=px(x) || old.top!=px(y) || old.right-old.left!=px(w) || old.bottom-old.top!=px(h))
      SetWindowPos(control,nullptr,px(x),px(y),px(w),px(h),SWP_NOZORDER|SWP_NOACTIVATE);
  };
  place(IDC_NOTES_LABEL1,16,14,width-68,22);place(IDCANCEL,width-44,8,28,28);
  if (panel.details) {
    // Static captions are anonymous resource controls. Place them in source
    // order so every edit has its label and no excess dialog-unit padding.
    int caption=0;
    for (HWND child=GetWindow(dialog,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) if (GetDlgCtrlID(child)==IDC_STATIC || GetDlgCtrlID(child)==65535) {
      SetWindowPos(child,nullptr,px(16),px(46+caption*66),px(width-32),px(20),SWP_NOZORDER|SWP_NOACTIVATE);++caption;
    }
    place(IDC_NOTES_STATUS_PICK,16,68,width-32,180);
    place(IDC_NOTES_TAGS_EDIT,16,134,width-32,30);
    place(IDC_NOTES_INPUT1,16,200,width-32,30);place(IDC_NOTES_INPUT2,16,266,width-32,30);
    place(IDC_NOTES_DETAILS_HINT,16,312,width-32,60);
  } else if (panel.actions) {
    place(IDC_NOTES_QUERY,16,46,width-32,30);place(IDC_NOTES_RESULTS,16,88,width-32,252);
    place(IDC_NOTES_INFO,16,350,width-32,52);
  } else {
    place(IDC_NOTES_CLIENT_SEARCH,16,46,width-32,30);place(IDC_NOTES_LABEL2,16,86,width-32,28);
    place(IDC_NOTES_CLIENT_MATCHES,16,114,width-32,height-122);
    SendDlgItemMessageW(dialog,IDC_NOTES_CLIENT_MATCHES,LB_SETITEMHEIGHT,0,px(28));
  }
  InvalidateRect(dialog,nullptr,FALSE);
}
static void NotesPaintPanel(HWND dialog,HDC dc) {
  RECT area{};GetClientRect(dialog,&area);FillRect(dc,&area,g_hbrThemeWindow);FrameRect(dc,&area,g_hbrThemeBorder);
  for (int id:{IDC_NOTES_CLIENT_SEARCH,IDC_NOTES_CLIENT_MATCHES,IDC_NOTES_QUERY,IDC_NOTES_RESULTS,
      IDC_NOTES_TAGS_EDIT,IDC_NOTES_INPUT1,IDC_NOTES_INPUT2}) {
    HWND control=GetDlgItem(dialog,id);if (!control || !IsWindowVisible(control)) continue;
    RECT rect{};GetWindowRect(control,&rect);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&rect),2);InflateRect(&rect,1,1);
    FrameRect(dc,&rect,g_hbrThemeBorder);
  }
}
static void NotesPanelMatches(HWND dialog,NotesPanel &panel) {
  const auto query=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_CLIENT_SEARCH));
  auto list=GetDlgItem(dialog,IDC_NOTES_CLIENT_MATCHES);
  SendMessageW(list,LB_RESETCONTENT,0,0); panel.matches.clear();
  for (const auto &client:panel.clients) {
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
    return false;
  }
  state.detailsDrafts.erase(panel.note);
  if (page->ticket!=ticket || page->ticketUrl!=url || page->status!=static_cast<uint32_t>(status) || page->tags!=tags) {
    page->ticket=ticket;page->ticketUrl=url;page->status=static_cast<uint32_t>(status);page->tags=std::move(tags);
    page->pristine=false;page->modified=client_notes::Now();state.metadataChanged=true;
    SaveClientNotesDraft(panel.owner,state);UpdateNotesToolbar(panel.owner,state);
  }
  SetDlgItemTextW(dialog,IDC_NOTES_DETAILS_HINT,state.saveFailed ? L"Could not save. Your changes stay in the notebook draft; use More > Retry saving." :
      L"Changes save automatically. Click outside to close. Status is local; it does not update your ticket system.");
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
    } else {
      panel->clients=NotesNavigationClients();NotesPanelMatches(dialog,*panel);
      SendDlgItemMessageW(dialog,IDC_NOTES_CLIENT_SEARCH,EM_SETCUEBANNER,FALSE,reinterpret_cast<LPARAM>(L"Search clients..."));
      SendDlgItemMessageW(dialog,IDC_NOTES_CLIENT_SEARCH,EM_SETLIMITTEXT,64,0);
      SetWindowSubclass(GetDlgItem(dialog,IDC_NOTES_CLIENT_SEARCH),NotesClientSearchKey,1,0);
      SetWindowSubclass(GetDlgItem(dialog,IDC_NOTES_CLIENT_MATCHES),NotesClientListKey,1,0);
      ShowWindow(GetDlgItem(dialog,IDOK),SW_HIDE);
    }
    NotesPanelLayout(dialog,*panel);panel->loading=false;return TRUE;
  }
  if (!panel) return FALSE;
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
  MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTONEAREST),&monitor);
  const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
  const int left=std::clamp(static_cast<int>(details || actions ? anchor.right-width : anchor.left),static_cast<int>(monitor.rcWork.left),static_cast<int>(monitor.rcWork.right)-width);
  const int top=std::clamp(static_cast<int>(anchor.bottom+6),static_cast<int>(monitor.rcWork.top),static_cast<int>(monitor.rcWork.bottom)-height);
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
  auto client=GetSelectedClientNameSanitized(false);
  if (!NotesClientAvailable(client)) {
    wchar_t last[65]{};GetPrivateProfileStringW(L"notes_window",L"last_client",L"",last,65,g_sConfigPath.c_str());client=last;
  }
  if (NotesClientAvailable(client)) ShowClientNotes(client);
  else NotesShowPanel(g_hGui,nullptr,false);
}
