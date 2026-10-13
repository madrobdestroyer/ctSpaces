// Native notes workspace commands. Included after the editor helpers so all
// persistence and formatting actions use the same validated paths.
static INT_PTR CALLBACK NotesTemplatesProc(HWND,UINT,WPARAM,LPARAM);
static INT_PTR CALLBACK NotesTemplateEditorProc(HWND,UINT,WPARAM,LPARAM);
static void RefreshNotesTemplatesForTheme(HWND);
struct NotesInput {
  std::wstring title, label1, label2, first, second;
  std::wstring hint;
};
static std::wstring NotesWindowText(HWND window) {
  const int size = GetWindowTextLengthW(window);
  std::wstring text(static_cast<size_t>(size) + 1,L'\0');
  text.resize(GetWindowTextW(window,text.data(),size+1)); return text;
}
static INT_PTR CALLBACK NotesInputProc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) {
  auto *input = reinterpret_cast<NotesInput *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (message == WM_INITDIALOG) {
    input = reinterpret_cast<NotesInput *>(lParam); SetWindowLongPtrW(dialog,DWLP_USER,lParam);
    SetWindowTextW(dialog,input->title.c_str());
    SetDlgItemTextW(dialog,IDC_NOTES_LABEL1,input->label1.c_str());
    SetDlgItemTextW(dialog,IDC_NOTES_LABEL2,input->label2.c_str());
    SetDlgItemTextW(dialog,IDC_NOTES_INPUT1,input->first.c_str());
    SetDlgItemTextW(dialog,IDC_NOTES_INPUT2,input->second.c_str());
    SendDlgItemMessageW(dialog,IDC_NOTES_INPUT1,EM_SETLIMITTEXT,2048,0);
    SendDlgItemMessageW(dialog,IDC_NOTES_INPUT2,EM_SETLIMITTEXT,2048,0);
    if (input->hint.empty()) ShowWindow(GetDlgItem(dialog,IDC_NOTES_INPUT_HINT),SW_HIDE);
    else {
      SetDlgItemTextW(dialog,IDC_NOTES_INPUT_HINT,input->hint.c_str());
      const int extra=ScaleByDpi(76,GetDpiForWindow(dialog));
      for (int id : {IDC_NOTES_LABEL1,IDC_NOTES_INPUT1,IDC_NOTES_LABEL2,IDC_NOTES_INPUT2,IDOK,IDCANCEL}) {
        RECT r{}; GetWindowRect(GetDlgItem(dialog,id),&r); MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&r),2);
        SetWindowPos(GetDlgItem(dialog,id),nullptr,r.left,r.top+extra,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
      }
      RECT r{}; GetWindowRect(dialog,&r);
      SetWindowPos(dialog,nullptr,0,0,r.right-r.left,r.bottom-r.top+extra,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    if (input->label2.empty()) {
      ShowWindow(GetDlgItem(dialog,IDC_NOTES_LABEL2),SW_HIDE);
      ShowWindow(GetDlgItem(dialog,IDC_NOTES_INPUT2),SW_HIDE);
      const int delta=ScaleByDpi(60,GetDpiForWindow(dialog));
      for (int id : {IDOK,IDCANCEL}) {
        RECT r{}; GetWindowRect(GetDlgItem(dialog,id),&r); MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&r),2);
        SetWindowPos(GetDlgItem(dialog,id),nullptr,r.left,r.top-delta,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
      }
      RECT r{}; GetWindowRect(dialog,&r);
      SetWindowPos(dialog,nullptr,0,0,r.right-r.left,r.bottom-r.top-delta,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    ApplyCleanupDialogTheme(dialog); return TRUE;
  }
  if (const auto themed=HandleCleanupDialogTheme(dialog,message,wParam,lParam)) return *themed;
  if (message==WM_CLOSE || (message==WM_COMMAND && LOWORD(wParam)==IDCANCEL)) { EndDialog(dialog,IDCANCEL); return TRUE; }
  if (message==WM_COMMAND && LOWORD(wParam)==IDOK && input) {
    input->first=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_INPUT1));
    input->second=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_INPUT2));
    EndDialog(dialog,IDOK); return TRUE;
  }
  return FALSE;
}
static bool NotesAsk(HWND owner,NotesInput &input) {
  return DialogBoxParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_INPUT),owner,NotesInputProc,
      reinterpret_cast<LPARAM>(&input))==IDOK;
}
struct NotesClientPicker {
  std::wstring source, selected;
  bool moving=false, updating=false;
  std::vector<std::wstring> clients;
};
static std::vector<std::wstring> NotesDestinationClients(const std::wstring &source) {
  std::vector<std::wstring> clients;
  std::error_code error;
  for (fs::directory_iterator it(g_sDataDir/L"Sites",error),end; !error && it!=end; it.increment(error)) {
    const auto name=it->path().filename().wstring(); fs::path root;
    if (!client_notes::SamePageName(name,source) && IsExistingClientProfile(name) &&
        !IsClientArchived(name) && TryGetSafeClientProfilePath(name,root) && RevalidateSafeClientContainerPath(name,root)) clients.push_back(name);
  }
  std::sort(clients.begin(),clients.end(),[](const auto &a,const auto &b){return _wcsicmp(a.c_str(),b.c_str())<0;});
  return clients;
}
static void NotesClientSuggestions(HWND dialog,NotesClientPicker &picker,bool typed) {
  if (picker.updating) return;
  HWND combo=GetDlgItem(dialog,IDC_NOTES_CLIENT_PICKER);
  const auto text=NotesWindowText(combo);
  const DWORD selection=static_cast<DWORD>(SendMessageW(combo,CB_GETEDITSEL,0,0));
  picker.updating=true;
  SendMessageW(combo,CB_RESETCONTENT,0,0);
  for (const auto &client : picker.clients) if (!typed || text.empty() || StrStrIW(client.c_str(),text.c_str()))
    SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(client.c_str()));
  if (typed) SendMessageW(combo,CB_SHOWDROPDOWN,SendMessageW(combo,CB_GETCOUNT,0,0)>0,0);
  // Opening a native combo can select its first result. Restore the user's
  // query afterwards so suggestions do not replace unfinished typing.
  SendMessageW(combo,CB_SETCURSEL,static_cast<WPARAM>(-1),0);
  SetWindowTextW(combo,text.c_str());
  SendMessageW(combo,CB_SETEDITSEL,0,selection);
  picker.updating=false;
}
static INT_PTR CALLBACK NotesClientPickerProc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) {
  auto *picker=reinterpret_cast<NotesClientPicker *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (message==WM_INITDIALOG) {
    picker=reinterpret_cast<NotesClientPicker *>(lParam); SetWindowLongPtrW(dialog,DWLP_USER,lParam);
    SetWindowTextW(dialog,picker->moving ? L"Move note to client" : L"Copy note to client");
    picker->clients=NotesDestinationClients(picker->source);
    NotesClientSuggestions(dialog,*picker,false); ApplyCleanupDialogTheme(dialog);
    HWND combo=GetDlgItem(dialog,IDC_NOTES_CLIENT_PICKER);
    SetWindowTheme(combo,L"",L""); SetWindowSubclass(combo,NotesComboSubclass,1,0);
    SendMessageW(combo,CB_SETITEMHEIGHT,-1,ScaleByDpi(30,GetDpiForWindow(dialog)));
    SendMessageW(combo,CB_SETITEMHEIGHT,0,ScaleByDpi(30,GetDpiForWindow(dialog)));
    SendDlgItemMessageW(dialog,IDC_NOTES_CLIENT_PICKER,CB_LIMITTEXT,64,0);
    if (picker->clients.empty()) SetDlgItemTextW(dialog,IDC_NOTES_LABEL2,L"No other active clients are available.");
    EnableWindow(GetDlgItem(dialog,IDOK),!picker->clients.empty()); return TRUE;
  }
  if (message==WM_DRAWITEM && lParam && DrawNotesComboItem(*reinterpret_cast<DRAWITEMSTRUCT *>(lParam))) return TRUE;
  if (message==WM_MEASUREITEM && lParam && reinterpret_cast<MEASUREITEMSTRUCT *>(lParam)->CtlID==IDC_NOTES_CLIENT_PICKER) {
    reinterpret_cast<MEASUREITEMSTRUCT *>(lParam)->itemHeight=ScaleByDpi(30,GetDpiForWindow(dialog)); return TRUE;
  }
  if (const auto themed=HandleCleanupDialogTheme(dialog,message,wParam,lParam)) return *themed;
  if (message==WM_CLOSE || (message==WM_COMMAND && LOWORD(wParam)==IDCANCEL)) { EndDialog(dialog,IDCANCEL); return TRUE; }
  if (!picker || message!=WM_COMMAND) return FALSE;
  if (LOWORD(wParam)==IDC_NOTES_CLIENT_PICKER && HIWORD(wParam)==CBN_EDITCHANGE) {
    NotesClientSuggestions(dialog,*picker,true); return TRUE;
  }
  if (LOWORD(wParam)==IDOK) {
    const auto text=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_CLIENT_PICKER));
    for (const auto &client : picker->clients) if (client_notes::SamePageName(client,text)) {
      picker->selected=client; EndDialog(dialog,IDOK); return TRUE;
    }
    MessageBoxW(dialog,L"Select a listed client or type its full name.",L"Choose destination",MB_OK|MB_ICONINFORMATION);
    SetFocus(GetDlgItem(dialog,IDC_NOTES_CLIENT_PICKER)); return TRUE;
  }
  return FALSE;
}
static std::wstring NotesDate(uint64_t value) {
  if (!value) return L"Not recorded";
  FILETIME utc{static_cast<DWORD>(value),static_cast<DWORD>(value>>32)},local{};
  SYSTEMTIME date{}; FileTimeToLocalFileTime(&utc,&local); FileTimeToSystemTime(&local,&date);
  wchar_t text[80]{}; swprintf_s(text,L"%04u-%02u-%02u %02u:%02u",date.wYear,date.wMonth,date.wDay,date.wHour,date.wMinute);
  return text;
}
static std::wstring NotesUniqueName(const ClientNotesDialogState &state,std::wstring base) {
  if (base.empty()) base=L"Untitled";
  base.resize((std::min)(base.size(),size_t{48}));
  if (!client_notes::ValidPageName(base)) base=L"Note";
  std::wstring name=base;
  for (size_t i=2;std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &p){return client_notes::SamePageName(p.name,name);});++i)
    name=base+L" "+std::to_wstring(i);
  return name;
}
static std::wstring NotesPlain(const client_notes::NotePage &page) {
  if (!page.rich) return page.text;
  HWND edit=CreateWindowExW(0,L"RICHEDIT50W",L"",ES_MULTILINE,0,0,0,0,nullptr,nullptr,g_hInst,nullptr);
  if (!edit) return {};
  SendMessageW(edit,EM_EXLIMITTEXT,0,client_notes::kMaxRichBytes);
  NotesStream stream{page.rtf}; EDITSTREAM data{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};
  SendMessageW(edit,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&data));
  std::wstring text=NotesWindowText(edit); DestroyWindow(edit);
  // RichEdit soft line/cell separators need ordinary whitespace in readable
  // exports; HTML preformatted text does not render a vertical tab as a line.
  std::replace(text.begin(),text.end(),L'\v',L'\n');
  std::replace(text.begin(),text.end(),L'\a',L'\t');
  return text;
}
static void NotesCopyText(HWND owner,const std::wstring &text) {
  HGLOBAL block=GlobalAlloc(GMEM_MOVEABLE,(text.size()+1)*sizeof(wchar_t));
  if (!block) return;
  void *target=GlobalLock(block);
  if (!target) { GlobalFree(block); return; }
  memcpy(target,text.c_str(),(text.size()+1)*sizeof(wchar_t)); GlobalUnlock(block);
  if (OpenClipboard(owner)) {
    EmptyClipboard(); if (SetClipboardData(CF_UNICODETEXT,block)) block=nullptr; CloseClipboard();
  }
  if (block) GlobalFree(block);
}
static void NotesRememberView(HWND dialog,ClientNotesDialogState &state) {
  if (!HasActiveNote(state)) return;
  // Opening legacy content alone must not create a notebook.
  if (state.original.notebook_snapshot.status==client_notes::ReadStatus::Missing &&
      !state.metadataChanged && !state.formattingChanged && !SendDlgItemMessageW(dialog,IDC_CLIENT_NOTES_TEXT,EM_GETMODIFY,0,0)) return;
  HWND edit=GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT); CHARRANGE range{}; NotesSelection(edit,range);
  POINT scroll{}; SendMessageW(edit,EM_GETSCROLLPOS,0,reinterpret_cast<LPARAM>(&scroll));
  auto &page=state.pages[state.activePage];
  const uint32_t cursor=static_cast<uint32_t>((std::max)(0L,range.cpMin));
  const uint32_t y=static_cast<uint32_t>((std::max)(0L,scroll.y));
  if (page.cursor!=cursor || page.scroll!=y) state.metadataChanged=true;
  page.cursor=cursor; page.scroll=y;
  for (size_t i=0;i<state.pages.size();++i) {
    const bool active=i==state.activePage;
    if (state.pages[i].active!=active) state.metadataChanged=true;
    state.pages[i].active=active;
  }
}
static void NotesWriteRecovery(HWND dialog,ClientNotesDialogState &state) {
  if (state.loading || (!state.formattingChanged && !state.metadataChanged)) return;
  if (!RevalidateSafeClientContainerPath(state.clientName,state.clientRoot) || !CaptureNotesPage(dialog,state,false)) return;
  std::string bytes;
  if (client_notes::SerializeNotebook(state.pages,bytes))
    client_notes::WriteAuxiliary(state.clientRoot,state.draftName,bytes);
}
static void NotesPreferAdjacentTab(ClientNotesDialogState &state) {
  state.preferredNextPage=0;
  const auto current=std::find(state.visiblePages.begin(),state.visiblePages.end(),state.activePage);
  if (current==state.visiblePages.end()) return;
  const auto neighbor=current!=state.visiblePages.begin() ? current-1 : current+1;
  if (neighbor!=state.visiblePages.end() && *neighbor<state.pages.size()) state.preferredNextPage=state.pages[*neighbor].id;
}
static void NotesSelectAvailable(HWND dialog,ClientNotesDialogState &state) {
  if (!HasActiveNote(state)) {
    state.activePage=state.pages.size();
    for (size_t i=0;i<state.pages.size();++i) {
      if (!state.pages[i].open || state.pages[i].deleted || state.pages[i].archived) continue;
      if (state.activePage==state.pages.size()) state.activePage=i;
      if (state.pages[i].id==state.preferredNextPage) {state.activePage=i;break;}
    }
  }
  state.preferredNextPage=0;
  RefreshNotesTabs(dialog,state); LoadNotesPage(dialog,state);
}
static bool NotesOpenSavedPage(HWND dialog,ClientNotesDialogState &state,size_t index) {
  if (index>=state.pages.size() || state.pages[index].deleted || state.pages[index].archived ||
      state.baselines.size()!=state.pages.size() || !SaveClientNotesDraft(dialog,state,true)) return false;
  // Reopening is a new tab insertion. Keep an already-open tab in its current
  // position; closed notes append within the existing pinned/unpinned grouping.
  if (!state.pages[index].open) {
    state.pages[index].open=true;
    if (!NotesRelocatePage(state,index,state.pages.size()-1)) return false;
    index=state.pages.size()-1;
  }
  state.activePage=index;state.metadataChanged=true;
  NotesSelectAvailable(dialog,state);
  return SaveClientNotesDraft(dialog,state,true);
}
static bool NotesAddPage(HWND dialog,ClientNotesDialogState &state,client_notes::NotePage page) {
  if (state.pages.size()>=client_notes::kMaxPages) {
    MessageBoxW(dialog,L"This notebook has 10,000 notes, including archived and deleted notes. Export or permanently remove unneeded deleted notes first.",L"Notes limit",MB_OK|MB_ICONINFORMATION); return false;
  }
  page.id=client_notes::NextId(state.pages); page.name=NotesUniqueName(state,page.name);
  page.open=true; page.deleted=page.archived=page.pinned=false;
  page.created=page.modified=client_notes::Now(); page.cursor=page.scroll=0;
  state.pages.push_back(std::move(page)); state.baselines.emplace_back();
  state.activePage=state.pages.size()-1; state.metadataChanged=true;
  NotesSelectAvailable(dialog,state); return SaveClientNotesDraft(dialog,state,true);
}
static void NotesErasePage(HWND dialog,ClientNotesDialogState &state,size_t index) {
  const uint64_t id=state.pages[index].id;
  auto edit=state.editors.find(id);
  if (edit!=state.editors.end()) {
    // The active editor remains visible until LoadNotesPage swaps in the next
    // document; that function retires it after the replacement is ready.
    if (state.attachedEditor!=edit->second) DestroyWindow(edit->second);
    state.editors.erase(edit);
  }
  state.pages.erase(state.pages.begin()+index); state.baselines.erase(state.baselines.begin()+index);
  if (state.activePage>index) --state.activePage;
  else if (state.activePage==index) state.activePage=state.pages.size();
  state.metadataChanged=true;
}
static bool NotesIsUnusedBlankDraft(const client_notes::NotePage &page) {
  const bool automatic=page.name==L"Untitled" || (page.name.starts_with(L"Untitled ") && page.name.size()>9 &&
      std::all_of(page.name.begin()+9,page.name.end(),[](wchar_t c){return c>=L'0' && c<=L'9';}));
  return automatic && page.pristine && !page.archived && !page.deleted && !page.pinned && page.ticket.empty() &&
      page.ticketUrl.empty() && page.links.empty() && page.tags.empty() && page.status==0 && page.history.empty() &&
      NotesPlain(page).find_first_not_of(L" \r\n\t")==std::wstring::npos;
}
static void NotesDiscardEmptyDrafts(HWND dialog,ClientNotesDialogState &state) {
  bool removed=false;
  for (size_t i=state.pages.size();i>0;--i) if (NotesIsUnusedBlankDraft(state.pages[i-1])) {
    NotesErasePage(dialog,state,i-1);removed=true;
  }
  if (removed) NotesSelectAvailable(dialog,state);
}
static void NotesClosePage(HWND dialog,ClientNotesDialogState &state,size_t index) {
  auto &page=state.pages[index];
  if (NotesIsUnusedBlankDraft(page)) {
    NotesErasePage(dialog,state,index); return;
  }
  page.open=false; page.active=false; state.closedPages.push_back(page.id); state.metadataChanged=true;
}
static void NotesRestoreText(HWND dialog,ClientNotesDialogState &state,const std::string &rtf) {
  HWND edit=GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT);
  CHARRANGE all{0,-1}; SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&all));
  NotesStream stream{rtf}; EDITSTREAM data{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};
  SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
  SendMessageW(edit,EM_STREAMIN,SF_RTF|SFF_SELECTION,reinterpret_cast<LPARAM>(&data));
  ApplyNotesTextColors(edit); MarkNotesChanged(dialog,state);
}
static std::wstring NotesLower(std::wstring text) {
  if (!text.empty()) CharLowerBuffW(text.data(),static_cast<DWORD>(text.size())); return text;
}
#include "NotesProductivityUi.inl"
#include "NotesCodeUi.inl"
struct NotesLibrary {
  HWND owner=nullptr;
  ClientNotesDialogState *state=nullptr;
  bool history=false;
  bool readOnly=false;
  size_t page=0;
  std::vector<size_t> results;
  std::map<uint64_t,std::wstring> plain;
  bool selecting=false;
  bool navigating=false;
  std::vector<uint64_t> selected;
  int initialFilter=0;
  bool allClients=false;
  ClientNotesDialogState *localState=nullptr;
  std::unique_ptr<ClientNotesDialogState> snapshot;
  std::vector<std::pair<std::wstring,uint64_t>> origins;
  std::wstring openClient;
  uint64_t openNote=0;
  bool scopeAllowed=false;
};
static std::vector<std::wstring> NotesNavigationClients();
static void NotesRequestClient(HWND,const std::wstring &,uint64_t);
static void NotesLibraryScope(HWND dialog,NotesLibrary &lib,bool across) {
  if (!lib.localState) lib.localState=lib.state;
  lib.selected.clear();lib.results.clear();lib.plain.clear();lib.selecting=false;
  lib.allClients=across;lib.readOnly=across;
  if (across) {
    lib.snapshot=std::make_unique<ClientNotesDialogState>();
    lib.snapshot->clientName=L"All clients (search only)";lib.origins.clear();
    for (const auto &client:NotesNavigationClients()) {
      fs::path root;
      if (!TryGetSafeClientProfilePath(client,root) || !RevalidateSafeClientContainerPath(client,root)) continue;
      const auto notebook=client_notes::ReadNotebook(root);
      if (notebook.status!=client_notes::ReadStatus::Ok) continue;
      for (auto page:notebook.pages) {
        lib.origins.emplace_back(client,page.id);
        // Cache keys in the combined preview are synthetic. Notebook-local
        // IDs may collide and never become mutation targets in this view.
        page.id=lib.origins.size();lib.snapshot->pages.push_back(std::move(page));
      }
    }
    lib.snapshot->activePage=lib.snapshot->pages.size();lib.state=lib.snapshot.get();
  } else {lib.state=lib.localState;lib.snapshot.reset();lib.origins.clear();}
  SetDlgItemTextW(dialog,IDOK,across ? L"Open client" : L"Open");
  for (int id:{IDC_NOTES_ARCHIVE,IDC_NOTES_DELETE,IDC_NOTES_RESTORE,IDC_NOTES_SELECT_ALL,IDC_NOTES_BATCH_EXPORT})
    ShowWindow(GetDlgItem(dialog,id),across ? SW_HIDE : SW_SHOW);
}
static void NotesRememberLibrary(HWND dialog,const NotesLibrary &lib) {
  WINDOWPLACEMENT placement{sizeof(placement)};if (!GetWindowPlacement(dialog,&placement)) return;
  const auto &r=placement.rcNormalPosition;const auto dpi=GetDpiForWindow(dialog);
  const int width=MulDiv(r.right-r.left,96,dpi),height=MulDiv(r.bottom-r.top,96,dpi);
  if (width<900 || height<(lib.history ? 480 : 600) || width>7680 || height>4320) return;
  const wchar_t *section=lib.history ? L"notes_history" : lib.initialFilter==3 ? L"notes_trash" : L"notes_library";
  SaveConfigMutations(L"notes manager size",{{section,std::wstring(L"width"),std::to_wstring(width)},
      {section,std::wstring(L"height"),std::to_wstring(height)}},false);
}
static std::vector<size_t> NotesLibrarySelection(HWND dialog,const NotesLibrary &lib) {
  std::vector<size_t> indices;
  if (lib.selecting) {for (const auto i:lib.results) if (std::find(lib.selected.begin(),lib.selected.end(),lib.state->pages[i].id)!=lib.selected.end()) indices.push_back(i);}
  else {const auto row=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);if (row>=0 && static_cast<size_t>(row)<lib.results.size()) indices.push_back(lib.results[row]);}
  return indices;
}
static LRESULT CALLBACK NotesSelectionListSubclass(HWND list,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  auto *lib=reinterpret_cast<NotesLibrary *>(data);
  if (message==WM_NCDESTROY) RemoveWindowSubclass(list,NotesSelectionListSubclass,id);
  if (lib && lib->selecting && !lib->readOnly && !lib->history) {
    if (message==WM_KEYDOWN && w==VK_SPACE) {SendMessageW(GetParent(list),WM_COMMAND,MAKEWPARAM(IDC_NOTES_RESULTS,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));return 0;}
    if (message==WM_KEYDOWN && (w==VK_UP || w==VK_DOWN || w==VK_HOME || w==VK_END || w==VK_PRIOR || w==VK_NEXT)) {
      lib->navigating=true;const auto result=DefSubclassProc(list,message,w,l);lib->navigating=false;return result;
    }
    if (message==WM_LBUTTONDOWN) {
      const auto hit=SendMessageW(list,LB_ITEMFROMPOINT,0,l),row=SendMessageW(list,LB_GETCURSEL,0,0);
      if (!HIWORD(hit) && LOWORD(hit)==row) {const auto result=DefSubclassProc(list,message,w,l);SendMessageW(GetParent(list),WM_COMMAND,MAKEWPARAM(IDC_NOTES_RESULTS,LBN_SELCHANGE),reinterpret_cast<LPARAM>(list));return result;}
    }
  }
  return DefSubclassProc(list,message,w,l);
}
static void NotesLibraryPreview(HWND dialog,NotesLibrary &library) {
  const LRESULT row=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);
  const bool valid=row>=0 && static_cast<size_t>(row)<library.results.size();
  HWND preview=GetDlgItem(dialog,IDC_NOTES_PREVIEW); SetWindowTextW(preview,L"");
  SetDlgItemTextW(dialog,IDC_NOTES_INFO,L""); EnableWindow(GetDlgItem(dialog,IDOK),valid);
  EnableWindow(GetDlgItem(dialog,IDC_NOTES_OPEN_TICKET),FALSE);
  EnableWindow(GetDlgItem(dialog,IDC_NOTES_BATCH_EXPORT),FALSE);
  if (!library.history) SetDlgItemTextW(dialog,IDC_NOTES_DELETE,
      SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_GETCURSEL,0,0)==3 ? L"Delete forever" : L"To trash");
  for (int id : {IDC_NOTES_ARCHIVE,IDC_NOTES_DELETE,IDC_NOTES_RESTORE}) EnableWindow(GetDlgItem(dialog,id),valid && !library.history && !library.readOnly);
  if (!valid) return;
  const auto &page=library.state->pages[library.history ? library.page : library.results[row]];
  const std::string &rtf=library.history ? page.history[library.results[row]].rtf : page.rtf;
  NotesStream stream{rtf}; EDITSTREAM data{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};
  if (!page.rich && !library.history) SetWindowTextW(preview,page.text.c_str());
  else SendMessageW(preview,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&data));
  SendMessageW(preview,EM_SETREADONLY,FALSE,0); ApplyNotesTextColors(preview); SendMessageW(preview,EM_SETREADONLY,TRUE,0);
  std::wstring info=page.name+L"\r\n"+(page.ticket.empty() ? L"No ticket linked (optional)" : L"Ticket: "+page.ticket);
  if (!library.history) info+=L" | "+std::wstring(kNotesStatuses[page.status])+(page.tags.empty() ? L"" : L" | "+NotesTagsText(page));
  info+=L"\r\nCreated: "+NotesDate(page.created)+L"   Edited: "+NotesDate(page.modified);
  info+=page.deleted ? L"\r\nKept until you choose Delete forever; no automatic expiry." :
      page.archived ? L"\r\nArchived - restore before opening." : page.open ? L"\r\nOpen in a tab." : L"\r\nSaved - tab closed. Open brings it back.";
  if (library.history) info=page.name+L"\r\nVersion: "+NotesDate(page.history[library.results[row]].time);
  EnableWindow(GetDlgItem(dialog,IDC_NOTES_OPEN_TICKET),!library.history && !library.readOnly && ValidNotesUrl(page.ticketUrl));
  if (library.state->showProgress) {
    const auto text=NotesPlain(page);
    const auto done=std::count(text.begin(),text.end(),L'\x2611');
    const auto total=done+std::count(text.begin(),text.end(),L'\x2610');
    if (total) info+=L"   Checklist: "+std::to_wstring(done)+L"/"+std::to_wstring(total);
  }
  SetDlgItemTextW(dialog,IDC_NOTES_INFO,info.c_str());
  if (!library.history && !library.readOnly) {
    const auto selected=NotesLibrarySelection(dialog,library);
    EnableWindow(GetDlgItem(dialog,IDC_NOTES_BATCH_EXPORT),!selected.empty());
    if (library.selecting) {
      EnableWindow(GetDlgItem(dialog,IDOK),FALSE);EnableWindow(GetDlgItem(dialog,IDC_NOTES_OPEN_TICKET),FALSE);
      EnableWindow(GetDlgItem(dialog,IDC_NOTES_DELETE),!selected.empty());
      EnableWindow(GetDlgItem(dialog,IDC_NOTES_ARCHIVE),std::any_of(selected.begin(),selected.end(),[&](size_t i){return !library.state->pages[i].archived && !library.state->pages[i].deleted;}));
      EnableWindow(GetDlgItem(dialog,IDC_NOTES_RESTORE),std::any_of(selected.begin(),selected.end(),[&](size_t i){return library.state->pages[i].archived || library.state->pages[i].deleted;}));
      SetDlgItemTextW(dialog,IDC_NOTES_ACTION_HINT,(std::to_wstring(selected.size())+L" selected. Click notes to tick them; Select notes offers Select all or Clear. Actions affect ticked notes only.").c_str());
    }
  }
  EnableWindow(GetDlgItem(dialog,IDC_NOTES_ARCHIVE),!library.readOnly && !library.history && !page.archived && !page.deleted);
  EnableWindow(GetDlgItem(dialog,IDC_NOTES_RESTORE),!library.readOnly && !library.history && (page.archived || page.deleted));
  if (library.allClients) {
    const auto &origin=library.origins[library.results[row]];
    SetDlgItemTextW(dialog,IDC_NOTES_INFO,(L"Client: "+origin.first+L"\r\n"+info).c_str());
    SetDlgItemTextW(dialog,IDC_NOTES_ACTION_HINT,L"Search only. Open client to edit or manage that client's notes, archive or trash.");
    SetDlgItemTextW(dialog,IDOK,L"Open client");
  }
  SetDlgItemTextW(dialog,IDC_NOTES_DELETE,page.deleted ? L"Delete forever" : L"To trash");
}
static void RefreshNotesLibraryForTheme(HWND window) {
  if (GetWindowLongPtrW(window,DWLP_DLGPROC)==reinterpret_cast<LONG_PTR>(NotesTemplatesProc) ||
      GetWindowLongPtrW(window,DWLP_DLGPROC)==reinterpret_cast<LONG_PTR>(NotesTemplateEditorProc)) {
    RefreshNotesTemplatesForTheme(window); return;
  }
  auto *library=reinterpret_cast<NotesLibrary *>(GetWindowLongPtrW(window,DWLP_USER));
  if (!library) return;
  SendDlgItemMessageW(window,IDC_NOTES_PREVIEW,EM_SETBKGNDCOLOR,0,g_themeColors.crControl);
  NotesLibraryPreview(window,*library);
}
static void LayoutNotesLibrary(HWND);
static void NotesLibraryFill(HWND dialog,NotesLibrary &library) {
  if (library.selecting && !library.history) SetPropW(GetDlgItem(dialog,IDC_NOTES_RESULTS),L"ctSpaces.notes.selection",reinterpret_cast<HANDLE>(1));
  else RemovePropW(GetDlgItem(dialog,IDC_NOTES_RESULTS),L"ctSpaces.notes.selection");
  const auto query=NotesLower(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_QUERY)));
  const int filter=static_cast<int>(SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_GETCURSEL,0,0));
  if (!library.history) {
    SetWindowTextW(dialog,((filter==3 ? L"Trash - " : L"All notes - ")+library.state->clientName).c_str());
    const wchar_t *hints[]={L"Current notes: saved and open notes. Closing a tab keeps its saved note here.",
      L"Open tabs: only notes currently open in this client's notes window.",
      L"Archived: kept out of current notes. Select one and Restore to bring it back.",
      L"Trash: this client's trash. No automatic expiry. Restore or permanently delete.",
      L"Current + archived: all kept notes. Trash is shown separately."};
    SetDlgItemTextW(dialog,IDC_NOTES_FILTER_HINT,filter>=0 && filter<5 ? hints[filter] : hints[0]);
    SetDlgItemTextW(dialog,IDC_NOTES_ACTION_HINT,L"Open reopens a saved note. Archive hides it. To trash keeps a recoverable copy.");
    ShowWindow(GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH),filter==3 && !library.readOnly ? SW_SHOW : SW_HIDE);
    const bool anyTrash=std::any_of(library.state->pages.begin(),library.state->pages.end(),[](const auto &p){return p.deleted;});
    EnableWindow(GetDlgItem(dialog,IDC_NOTES_EMPTY_TRASH),anyTrash);
    if (filter==3) SetDlgItemTextW(dialog,IDC_NOTES_ACTION_HINT,L"Delete forever removes the selected note. Empty trash removes ALL deleted notes, including hidden search results.");
  }
  library.results.clear(); SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_RESETCONTENT,0,0);
  if (library.history) {
    const auto &history=library.state->pages[library.page].history;
    for (size_t i=history.size();i>0;--i) {
      library.results.push_back(i-1); const auto title=NotesDate(history[i-1].time);
      SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str()));
    }
  } else {
    std::vector<size_t> order;
    for (size_t i=0;i<library.state->pages.size();++i) order.push_back(i);
    const auto sort=SendDlgItemMessageW(dialog,IDC_NOTES_SORT,CB_GETCURSEL,0,0);
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){const auto &x=library.state->pages[a],&y=library.state->pages[b];
      if (sort==1) return x.created>y.created;
      if (sort==2) return NotesLower(x.name)<NotesLower(y.name);
      if (sort==3) return StrCmpLogicalW(x.ticket.c_str(),y.ticket.c_str())<0;
      return (x.modified ? x.modified:x.created)>(y.modified ? y.modified:y.created);
    });
    const auto status=SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_FILTER,CB_GETCURSEL,0,0);
    const auto tag=NotesLower(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_TAG_FILTER)));
    for (const auto i:order) {
    const auto &page=library.state->pages[i];
    if ((filter==0 && (page.deleted || page.archived)) || (filter==1 && (!page.open || page.deleted || page.archived)) ||
        (filter==2 && (!page.archived || page.deleted)) || (filter==3 && !page.deleted) || (filter==4 && page.deleted)) continue;
    if (status>0 && page.status!=status-1) continue;
    if (!tag.empty() && std::none_of(page.tags.begin(),page.tags.end(),[&](const auto &t){return NotesLower(t).find(tag)!=std::wstring::npos;})) continue;
    if (!query.empty()) {
      auto [it,inserted]=library.plain.try_emplace(page.id);
      if (inserted) it->second=NotesPlain(page);
      const auto client=library.allClients ? library.origins[i].first+L" " : L"";
      if (NotesLower(client+page.name+L" "+page.ticket+L" "+NotesTagsText(page)+L" "+kNotesStatuses[page.status]+L" "+it->second).find(query)==std::wstring::npos) continue;
    }
    library.results.push_back(i);
    std::wstring title=(page.pinned ? L"* " : L"")+page.name+(!page.ticket.empty() ? L" [Ticket "+page.ticket+L"]" : L"")+
        (page.deleted ? L" [deleted]" : page.archived ? L" [archived]" : page.open ? L" [open]" : L" [saved]");
    if (page.status) title+=L" - "+std::wstring(kNotesStatuses[page.status]);
    if (library.allClients) title=library.origins[i].first+L" / "+title;
    if (library.selecting) title=(std::find(library.selected.begin(),library.selected.end(),page.id)!=library.selected.end() ? L"[x] " : L"[ ] ")+title;
    if (!query.empty()) {
      auto text=library.plain[page.id]; const auto match=NotesLower(text).find(query);
      if (match!=std::wstring::npos) {
        text=text.substr(match>20 ? match-20 : 0,80);
        std::replace(text.begin(),text.end(),L'\r',L' '); std::replace(text.begin(),text.end(),L'\n',L' ');
        title+=L" - "+text;
      }
    }
    SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str()));
  }}
  if (!library.results.empty()) {
    const auto active=std::find(library.results.begin(),library.results.end(),library.state->activePage);
    const auto row=!library.history && active!=library.results.end() ? active-library.results.begin() : 0;
    SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_SETCURSEL,row,0);
  }
  NotesLibraryPreview(dialog,library);
  if (library.results.empty() && !library.history) SetDlgItemTextW(dialog,IDC_NOTES_INFO,query.empty() ? L"No notes in this view. Choose another filter or create a new note." : L"No matches. Try a title, ticket number or phrase, or change the filter.");
  LayoutNotesLibrary(dialog);
}
static void LayoutNotesLibrary(HWND dialog) {
  RECT area{}; GetClientRect(dialog,&area); const UINT dpi=GetDpiForWindow(dialog);
  const auto px=[&](int n){return ScaleByDpi(n,dpi);};
  const int margin=px(16),gap=px(16),row=px(32),bottom=area.bottom-margin-row;
  const int column=(std::min)(px(290),static_cast<int>(area.right)*34/100),right=margin+column+gap;
  const bool guidance=(GetWindowLongPtrW(GetDlgItem(dialog,IDC_NOTES_FILTER_HINT),GWL_STYLE)&WS_VISIBLE)!=0;
  const int contentTop=px(guidance ? 168 : 88),previewTop=px(guidance ? 292 : 146),contentBottom=bottom-gap-(guidance ? px(42) : 0);
  const auto place=[&](int id,int x,int y,int w,int h) {
    SetWindowPos(GetDlgItem(dialog,id),nullptr,x,y,(std::max)(0,w),(std::max)(0,h),SWP_NOZORDER|SWP_NOACTIVATE);
  };
  place(IDC_NOTES_LIBRARY_HEADING,margin,px(8),area.right-margin*2-px(188),px(20));
  place(IDC_NOTES_SCOPE,area.right-margin-px(180),px(3),px(180),px(180));
  place(IDC_NOTES_QUERY,margin,px(36),area.right*60/100-margin,row);
  place(IDC_NOTES_FILTER,area.right*60/100+gap,px(36),area.right*40/100-gap-margin,px(220));
  const int available=area.right-margin*2,smallGap=px(8),selectWidth=px(112),exportWidth=px(152);
  const int filterWidth=(available-selectWidth-exportWidth-smallGap*4)/3;
  place(IDC_NOTES_SORT,margin,px(78),filterWidth,px(220));
  place(IDC_NOTES_STATUS_FILTER,margin+filterWidth+smallGap,px(78),filterWidth,px(240));
  place(IDC_NOTES_TAG_FILTER,margin+(filterWidth+smallGap)*2,px(78),filterWidth,row);
  place(IDC_NOTES_SELECT_ALL,margin+(filterWidth+smallGap)*3,px(78),selectWidth,row);
  place(IDC_NOTES_BATCH_EXPORT,area.right-margin-exportWidth,px(78),exportWidth,row);
  place(IDC_NOTES_FILTER_HINT,margin,px(120),area.right-margin*2,px(36));
  place(IDC_NOTES_RESULTS,margin,contentTop,column,contentBottom-contentTop);
  place(IDC_NOTES_INFO,right,contentTop-2,area.right-right-margin,px(guidance ? 94 : 54));
  place(IDC_NOTES_OPEN_TICKET,right,px(260),px(135),px(25));
  place(IDC_NOTES_PREVIEW,right,previewTop,area.right-right-margin,contentBottom-previewTop);
  place(IDC_NOTES_ACTION_HINT,margin,bottom-px(40),area.right-margin*2,px(34));
  int x=margin;
  for (int id : {IDOK,IDC_NOTES_ARCHIVE,IDC_NOTES_RESTORE,IDC_NOTES_DELETE}) {
    place(id,x,bottom,px(108),row); x+=px(120);
  }
  place(IDCANCEL,area.right-margin-px(108),bottom,px(108),row);
  place(IDC_NOTES_EMPTY_TRASH,x,bottom,px(126),row);
  LRESULT extent=0;
  HDC dc=GetDC(dialog); auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,WM_GETFONT,0,0)));
  for (LRESULT i=0;i<SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCOUNT,0,0);++i) {
    const auto length=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETTEXTLEN,i,0); if (length<0 || length>4096) continue;
    std::wstring title(static_cast<size_t>(length)+1,L'\0'); SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETTEXT,i,reinterpret_cast<LPARAM>(title.data()));
    SIZE size{}; GetTextExtentPoint32W(dc,title.c_str(),static_cast<int>(length),&size); extent=(std::max)(extent,static_cast<LRESULT>(size.cx+px(12)));
  }
  SelectObject(dc,old); ReleaseDC(dialog,dc); SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_SETHORIZONTALEXTENT,extent,0);
  RECT text{}; GetClientRect(GetDlgItem(dialog,IDC_NOTES_PREVIEW),&text); InflateRect(&text,-px(10),-px(10));
  SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_SETRECT,0,reinterpret_cast<LPARAM>(&text));
}
static INT_PTR CALLBACK NotesLibraryProc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) {
  auto *lib=reinterpret_cast<NotesLibrary *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (message==WM_INITDIALOG) {
    ShowWindow(GetDlgItem(dialog,IDC_NOTES_SAVE_TEMPLATE),SW_HIDE);
    lib=reinterpret_cast<NotesLibrary *>(lParam); SetWindowLongPtrW(dialog,DWLP_USER,lParam);
    lib->localState=lib->state;
    lib->scopeAllowed=!lib->history && !lib->readOnly;
    for (auto label:{L"This client",L"All clients - search only"}) SendDlgItemMessageW(dialog,IDC_NOTES_SCOPE,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SendDlgItemMessageW(dialog,IDC_NOTES_SCOPE,CB_SETCURSEL,0,0);
    SetWindowSubclass(GetDlgItem(dialog,IDC_NOTES_SCOPE),NotesComboSubclass,1,0);
    SendDlgItemMessageW(dialog,IDC_NOTES_SCOPE,CB_SETITEMHEIGHT,-1,ScaleByDpi(24,GetDpiForWindow(dialog)));
    SendDlgItemMessageW(dialog,IDC_NOTES_SCOPE,CB_SETITEMHEIGHT,0,ScaleByDpi(24,GetDpiForWindow(dialog)));
    if (lib->history || lib->readOnly) ShowWindow(GetDlgItem(dialog,IDC_NOTES_SCOPE),SW_HIDE);
    SetWindowTextW(dialog,lib->history ? L"Note history - preview before restoring" : (L"All notes - "+lib->state->clientName).c_str());
    for (auto label : {L"Current notes",L"Open tabs",L"Archived",L"Trash",L"Current + archived"})
      SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETCURSEL,std::clamp(lib->initialFilter,0,4),0);
    for (const auto label:{L"Sort: Recently edited",L"Sort: Newest created",L"Sort: Title",L"Sort: Ticket number"}) SendDlgItemMessageW(dialog,IDC_NOTES_SORT,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SendDlgItemMessageW(dialog,IDC_NOTES_SORT,CB_SETCURSEL,0,0);
    SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_FILTER,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"All statuses"));
    for (const auto label:kNotesStatuses) SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_FILTER,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SendDlgItemMessageW(dialog,IDC_NOTES_STATUS_FILTER,CB_SETCURSEL,0,0);
    SendDlgItemMessageW(dialog,IDC_NOTES_TAG_FILTER,EM_SETCUEBANNER,FALSE,reinterpret_cast<LPARAM>(L"Filter tags..."));
    SendDlgItemMessageW(dialog,IDC_NOTES_TAG_FILTER,EM_SETLIMITTEXT,32,0);
    ApplyCleanupDialogTheme(dialog);
    SetWindowSubclass(GetDlgItem(dialog,IDC_NOTES_RESULTS),NotesSelectionListSubclass,1,reinterpret_cast<DWORD_PTR>(lib));
    SetWindowSubclass(GetDlgItem(dialog,IDC_NOTES_FILTER),NotesComboSubclass,1,0);
    for (int id:{IDC_NOTES_SORT,IDC_NOTES_STATUS_FILTER}) {SetWindowSubclass(GetDlgItem(dialog,id),NotesComboSubclass,1,0);SendDlgItemMessageW(dialog,id,CB_SETITEMHEIGHT,-1,ScaleByDpi(30,GetDpiForWindow(dialog)));SendDlgItemMessageW(dialog,id,CB_SETITEMHEIGHT,0,ScaleByDpi(30,GetDpiForWindow(dialog)));}
    SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETITEMHEIGHT,-1,ScaleByDpi(30,GetDpiForWindow(dialog)));
    SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETITEMHEIGHT,0,ScaleByDpi(30,GetDpiForWindow(dialog)));
    SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_SETBKGNDCOLOR,0,g_themeColors.crControl);
    SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_EXLIMITTEXT,0,client_notes::kMaxRichBytes);
    if (lib->history) {
      SetDlgItemTextW(dialog,IDOK,L"Restore version");
      for (int id : {IDC_NOTES_QUERY,IDC_NOTES_FILTER,IDC_NOTES_ARCHIVE,IDC_NOTES_DELETE,IDC_NOTES_RESTORE,IDC_NOTES_FILTER_HINT,IDC_NOTES_ACTION_HINT,IDC_NOTES_EMPTY_TRASH,IDC_NOTES_OPEN_TICKET,IDC_NOTES_SORT,IDC_NOTES_STATUS_FILTER,IDC_NOTES_TAG_FILTER,IDC_NOTES_SELECT_ALL,IDC_NOTES_BATCH_EXPORT}) ShowWindow(GetDlgItem(dialog,id),SW_HIDE);
    }
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(dialog,MONITOR_DEFAULTTONEAREST),&monitor);
    const UINT dpi=GetDpiForWindow(dialog);
    const wchar_t *section=lib->history ? L"notes_history" : lib->initialFilter==3 ? L"notes_trash" : L"notes_library";
    const int savedWidth=std::clamp(static_cast<int>(GetPrivateProfileIntW(section,L"width",1000,g_sConfigPath.c_str())),900,7680);
    const int savedHeight=std::clamp(static_cast<int>(GetPrivateProfileIntW(section,L"height",690,g_sConfigPath.c_str())),lib->history ? 480 : 600,4320);
    const int width=(std::min)(ScaleByDpi(savedWidth,dpi),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)-32);
    const int height=(std::min)(ScaleByDpi(savedHeight,dpi),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top)-32);
    SetWindowPos(dialog,nullptr,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,
        monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    LayoutNotesLibrary(dialog);
    NotesLibraryFill(dialog,*lib);
    if (lib->readOnly) for (int id : {IDOK,IDC_NOTES_ARCHIVE,IDC_NOTES_DELETE,IDC_NOTES_RESTORE,IDC_NOTES_EMPTY_TRASH,IDC_NOTES_OPEN_TICKET,IDC_NOTES_ACTION_HINT,IDC_NOTES_SELECT_ALL,IDC_NOTES_BATCH_EXPORT}) ShowWindow(GetDlgItem(dialog,id),SW_HIDE);
    return TRUE;
  }
  if (message==WM_SIZE) { LayoutNotesLibrary(dialog); return TRUE; }
  if ((message==WM_EXITSIZEMOVE || message==WM_DESTROY) && lib) {NotesRememberLibrary(dialog,*lib);return FALSE;}
  if (message==WM_GETMINMAXINFO && lParam) {
    auto *limits=reinterpret_cast<MINMAXINFO *>(lParam);
    limits->ptMinTrackSize={ScaleByDpi(900,GetDpiForWindow(dialog)),ScaleByDpi(lib && !lib->history ? 600 : 480,GetDpiForWindow(dialog))}; return TRUE;
  }
  if (message==WM_DRAWITEM && lParam && DrawNotesComboItem(*reinterpret_cast<DRAWITEMSTRUCT *>(lParam))) return TRUE;
  if (message==WM_MEASUREITEM && lParam && (reinterpret_cast<MEASUREITEMSTRUCT *>(lParam)->CtlID==IDC_NOTES_FILTER || reinterpret_cast<MEASUREITEMSTRUCT *>(lParam)->CtlID==IDC_NOTES_SORT || reinterpret_cast<MEASUREITEMSTRUCT *>(lParam)->CtlID==IDC_NOTES_STATUS_FILTER)) {
    reinterpret_cast<MEASUREITEMSTRUCT *>(lParam)->itemHeight=ScaleByDpi(30,GetDpiForWindow(dialog)); return TRUE;
  }
  if (const auto themed=HandleCleanupDialogTheme(dialog,message,wParam,lParam)) return *themed;
  if (!lib) return FALSE;
  if (message==WM_TIMER) { KillTimer(dialog,1); NotesLibraryFill(dialog,*lib); return TRUE; }
  if (message==WM_CLOSE || (message==WM_COMMAND && LOWORD(wParam)==IDCANCEL)) { EndDialog(dialog,IDCANCEL); return TRUE; }
  if (message!=WM_COMMAND) return FALSE;
  const int id=LOWORD(wParam);
  if (id==IDC_NOTES_SCOPE && HIWORD(wParam)==CBN_SELCHANGE && !lib->history) {
    if (!lib->scopeAllowed) return TRUE;
    if (!SaveClientNotesDraft(lib->owner,*lib->localState,true)) {
      SendDlgItemMessageW(dialog,IDC_NOTES_SCOPE,CB_SETCURSEL,lib->allClients ? 1 : 0,0);return TRUE;
    }
    NotesLibraryScope(dialog,*lib,SendDlgItemMessageW(dialog,IDC_NOTES_SCOPE,CB_GETCURSEL,0,0)==1);
    NotesLibraryFill(dialog,*lib);return TRUE;
  }
  if (lib->allClients && (id==IDOK || (id==IDC_NOTES_RESULTS && HIWORD(wParam)==LBN_DBLCLK))) {
    const auto row=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);
    if (row>=0 && static_cast<size_t>(row)<lib->results.size()) {
      const auto &origin=lib->origins[lib->results[row]];lib->openClient=origin.first;lib->openNote=origin.second;EndDialog(dialog,IDOK);
    }
    return TRUE;
  }
  if (((id==IDC_NOTES_QUERY || id==IDC_NOTES_TAG_FILTER) && HIWORD(wParam)==EN_CHANGE) || ((id==IDC_NOTES_FILTER || id==IDC_NOTES_SORT || id==IDC_NOTES_STATUS_FILTER) && HIWORD(wParam)==CBN_SELCHANGE)) {
    lib->selected.clear();
    SetTimer(dialog,1,200,nullptr); return TRUE;
  }
  if (id==IDC_NOTES_RESULTS && HIWORD(wParam)==LBN_SELCHANGE) {
    if (lib->selecting && !lib->navigating) {
      const auto row=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);
      if (row>=0 && static_cast<size_t>(row)<lib->results.size()) {
        const auto index=lib->results[row],noteId=lib->state->pages[index].id;auto found=std::find(lib->selected.begin(),lib->selected.end(),noteId);
        if (found==lib->selected.end()) lib->selected.push_back(noteId);else lib->selected.erase(found);
        NotesLibraryFill(dialog,*lib);const auto newRow=std::find(lib->results.begin(),lib->results.end(),index)-lib->results.begin();SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_SETCURSEL,newRow,0);
      }
    }
    NotesLibraryPreview(dialog,*lib); return TRUE;
  }
  if (lib->readOnly) return TRUE;
  if (id==IDC_NOTES_SELECT_ALL && !lib->history) {
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,lib->selecting ? L"Finish selecting" : L"Select several notes");AppendMenuW(menu,MF_STRING,2,L"Select all visible notes");AppendMenuW(menu,MF_STRING,3,L"Clear selection");
    RECT rect{};GetWindowRect(GetDlgItem(dialog,id),&rect);const auto choice=TrackThemedNotesMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,rect.left,rect.bottom,dialog);DestroyMenu(menu);
    if (choice==1) {lib->selecting=!lib->selecting;lib->selected.clear();}
    if (choice==2) {lib->selecting=true;lib->selected.clear();for (const auto i:lib->results) lib->selected.push_back(lib->state->pages[i].id);}
    if (choice==3) lib->selected.clear();
    NotesLibraryFill(dialog,*lib);return TRUE;
  }
  if (id==IDC_NOTES_BATCH_EXPORT && !lib->history) {if (SaveClientNotesDraft(lib->owner,*lib->state,true)) NotesExportHandover(dialog,*lib->state,NotesLibrarySelection(dialog,*lib));return TRUE;}
  if (lib->selecting && !lib->history && (id==IDOK || id==IDC_NOTES_OPEN_TICKET || (id==IDC_NOTES_RESULTS && HIWORD(wParam)==LBN_DBLCLK))) return TRUE;
  if (lib->selecting && !lib->history && (id==IDC_NOTES_ARCHIVE || id==IDC_NOTES_RESTORE || id==IDC_NOTES_DELETE)) {
    auto indices=NotesLibrarySelection(dialog,*lib);if (indices.empty()) return TRUE;auto &state=*lib->state;
    const bool permanent=id==IDC_NOTES_DELETE && std::any_of(indices.begin(),indices.end(),[&](size_t i){return state.pages[i].deleted;});
    if (permanent && MessageBoxW(dialog,(L"Permanently delete "+std::to_wstring(indices.size())+L" selected notes and their histories for client \""+state.clientName+L"\"? This cannot be undone.").c_str(),L"Delete selected notes forever",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES) return TRUE;
    if (!SaveClientNotesDraft(lib->owner,state,true)) return TRUE;
    std::sort(indices.rbegin(),indices.rend());
    for (const auto i:indices) {auto &p=state.pages[i];
      if (id==IDC_NOTES_ARCHIVE && !p.deleted) {p.archived=true;p.open=false;}
      else if (id==IDC_NOTES_RESTORE) {p.archived=p.deleted=false;p.open=false;}
      else if (id==IDC_NOTES_DELETE) {if (p.deleted) NotesErasePage(lib->owner,state,i);else {p.deleted=true;p.open=false;}}
    }
    lib->selected.clear();state.metadataChanged=true;NotesSelectAvailable(lib->owner,state);SaveClientNotesDraft(lib->owner,state,true);NotesLibraryFill(dialog,*lib);return TRUE;
  }
  if (id==IDC_NOTES_EMPTY_TRASH) {
    if (lib->history || SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_GETCURSEL,0,0)!=3) return TRUE;
    auto &state=*lib->state;
    const auto count=std::count_if(state.pages.begin(),state.pages.end(),[](const auto &p){return p.deleted;});
    if (!count) return TRUE;
    const auto warning=L"Permanently delete all "+std::to_wstring(count)+L" notes in Trash for client \""+state.clientName+
        L"\"?\n\nThis includes deleted notes hidden by the current search and removes their history. Current and archived notes are kept. This cannot be undone.";
    if (MessageBoxW(dialog,warning.c_str(),L"Empty this client's trash",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES) return TRUE;
    if (!SaveClientNotesDraft(lib->owner,state,true)) return TRUE;
    for (size_t i=state.pages.size();i>0;--i) if (state.pages[i-1].deleted) NotesErasePage(lib->owner,state,i-1);
    NotesSelectAvailable(lib->owner,state); SaveClientNotesDraft(lib->owner,state,true); NotesLibraryFill(dialog,*lib); return TRUE;
  }
  const LRESULT row=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);
  if (row<0 || static_cast<size_t>(row)>=lib->results.size()) return TRUE;
  auto &state=*lib->state;
  if (lib->history) {
    if (id==IDOK || (id==IDC_NOTES_RESULTS && HIWORD(wParam)==LBN_DBLCLK)) {
      const auto rtf=state.pages[lib->page].history[lib->results[row]].rtf;
      const auto current=state.pages[lib->page].rtf;
      state.pages[lib->page].rtf=rtf;
      client_notes::RememberRevision(state.pages[lib->page],current,true);
      state.pages[lib->page].rtf=current;
      NotesRestoreText(lib->owner,state,rtf); SaveClientNotesDraft(lib->owner,state,true); EndDialog(dialog,IDOK);
    }
    return TRUE;
  }
  const size_t index=lib->results[row]; auto &page=state.pages[index];
  if (id==IDC_NOTES_OPEN_TICKET) { if (ValidNotesUrl(page.ticketUrl)) OpenNotesUrl(lib->owner,state,page.ticketUrl,true); return TRUE; }
  if (id==IDOK || (id==IDC_NOTES_RESULTS && HIWORD(wParam)==LBN_DBLCLK)) {
    if (page.deleted || page.archived) {
      MessageBoxW(dialog,L"Restore this note before opening it.",L"All notes",MB_OK|MB_ICONINFORMATION); return TRUE;
    }
    if (NotesOpenSavedPage(lib->owner,state,index)) EndDialog(dialog,IDOK);
    else NotesLibraryFill(dialog,*lib);
    return TRUE;
  }
  if (id==IDC_NOTES_ARCHIVE) { page.archived=true; page.open=false; }
  else if (id==IDC_NOTES_RESTORE) { page.archived=false; page.deleted=false; page.open=false; }
  else if (id==IDC_NOTES_DELETE) {
    if (page.deleted) {
      if (MessageBoxW(dialog,(L"Permanently delete \""+page.name+L"\" and its history for client \""+state.clientName+L"\"? This cannot be undone.").c_str(),L"Delete note forever",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES) return TRUE;
      NotesErasePage(lib->owner,state,index);
    } else { page.deleted=true; page.open=false; }
  } else return FALSE;
  state.metadataChanged=true; NotesSelectAvailable(lib->owner,state); SaveClientNotesDraft(lib->owner,state,true); NotesLibraryFill(dialog,*lib); return TRUE;
}
static void NotesShowLibrary(HWND dialog,ClientNotesDialogState &state,bool history=false,int initialFilter=0) {
  if (!SaveClientNotesDraft(dialog,state,true) || (history && !HasActiveNote(state))) return;
  NotesLibrary lib; lib.owner=dialog; lib.state=&state; lib.history=history; lib.page=state.activePage; lib.initialFilter=initialFilter;
  DialogBoxParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),dialog,NotesLibraryProc,reinterpret_cast<LPARAM>(&lib));
  if (!lib.openClient.empty()) NotesRequestClient(dialog,lib.openClient,lib.openNote);
}
static void NotesFind(HWND dialog,ClientNotesDialogState &state,bool previous) {
  if (!HasActiveNote(state)) return;
  state.findText=NotesWindowText(GetDlgItem(dialog,IDC_NOTES_FIND));
  if (state.findText.empty()) { SetFocus(GetDlgItem(dialog,IDC_NOTES_FIND)); return; }
  HWND edit=GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT); CHARRANGE range{}; NotesSelection(edit,range);
  FINDTEXTEXW search{}; search.lpstrText=state.findText.data();
  search.chrg={previous ? range.cpMin : range.cpMax,previous ? 0L : -1L};
  LRESULT result=SendMessageW(edit,EM_FINDTEXTEXW,previous ? 0 : FR_DOWN,reinterpret_cast<LPARAM>(&search));
  if (result<0) {
    search.chrg={previous ? GetWindowTextLengthW(edit) : 0,previous ? 0L : -1L};
    result=SendMessageW(edit,EM_FINDTEXTEXW,previous ? 0 : FR_DOWN,reinterpret_cast<LPARAM>(&search));
  }
  if (result>=0) {
    SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&search.chrgText)); SendMessageW(edit,EM_SCROLLCARET,0,0); SetFocus(edit);
  } else MessageBoxW(dialog,L"No matching text in this note.",L"Find",MB_OK|MB_ICONINFORMATION);
}
static bool NotesPickFile(HWND dialog,bool save,std::wstring &path,bool notebook=false) {
  wchar_t buffer[32768]{}; if (save) wcscpy_s(buffer,notebook ? L"Client notes.ctn" : L"Note.rtf");
  OPENFILENAMEW file{sizeof(file)}; file.hwndOwner=dialog; file.lpstrFile=buffer; file.nMaxFile=static_cast<DWORD>(std::size(buffer));
  file.lpstrFilter=notebook ? L"ctSpaces notebook (*.ctn)\0*.ctn\0\0" : (save ? L"Rich text (*.rtf)\0*.rtf\0UTF-8 text (*.txt)\0*.txt\0\0" : L"Notes files (*.rtf;*.txt;*.ctn)\0*.rtf;*.txt;*.ctn\0\0");
  file.lpstrDefExt=notebook ? L"ctn" : L"rtf"; file.Flags=OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  if (!(save ? GetSaveFileNameW(&file) : GetOpenFileNameW(&file))) return false;
  path=buffer; return true;
}
static bool NotesWriteExport(const fs::path &path,const std::string &bytes) {
  return client_notes::WriteAuxiliary(path.parent_path(),path.filename().wstring(),bytes);
}
static bool NotesSameContent(const client_notes::NotePage &a,const client_notes::NotePage &b) {
  return a.name==b.name && a.rtf==b.rtf && a.text==b.text && a.ticket==b.ticket &&
      a.ticketUrl==b.ticketUrl && a.links==b.links && a.status==b.status && a.tags==b.tags;
}
static void NotesRecoverDrafts(HWND dialog,ClientNotesDialogState &state) {
  std::error_code error;
  for (fs::directory_iterator it(state.clientRoot,error),end; !error && it!=end; it.increment(error)) {
    const auto name=it->path().filename().wstring();
    if (name.rfind(L"ctSpaces-notes-draft-",0)!=0 || it->path().extension()!=L".ctn" || name==state.draftName) continue;
    const auto pidText=name.substr(21,name.size()-25);
    wchar_t *tail=nullptr; const DWORD pid=wcstoul(pidText.c_str(),&tail,10);
    if (pidText.empty() || !pid || !tail || *tail) continue;
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);
    if (process) { const bool running=WaitForSingleObject(process,0)==WAIT_TIMEOUT; CloseHandle(process); if (running) continue; }
    else if (GetLastError()!=ERROR_INVALID_PARAMETER) continue;
    const auto snapshot=client_notes::ReadFileSnapshot(state.clientRoot,name.c_str(),client_notes::kMaxNotebookBytes);
    std::vector<client_notes::NotePage> drafts;
    if (snapshot.status!=client_notes::ReadStatus::Ok || !client_notes::ParseNotebook(snapshot.bytes,drafts)) continue;
    if (MessageBoxW(dialog,L"A recoverable draft from an earlier session was found. Recover changed notes as separate copies? Existing saved notes will be kept.",L"Recover notes",MB_YESNO|MB_ICONQUESTION)!=IDYES) continue;
    bool recovered=true;
    for (auto page : drafts) {
      const bool changed=std::none_of(state.pages.begin(),state.pages.end(),[&](const auto &saved){return page.id==saved.id && NotesSameContent(page,saved);});
      if (!changed || page.deleted) continue;
      page.name+=L" recovered"; page.history.clear(); page.pristine=false;
      if (!NotesAddPage(dialog,state,std::move(page))) { recovered=false; break; }
    }
    if (recovered) DeleteFileW(it->path().c_str());
  }
}
static std::vector<client_notes::NotePage> NotesBuiltinTemplates() {
  const wchar_t *names[]={L"Ticket investigation",L"Maintenance checklist",L"Client contacts",L"Handover"};
  const wchar_t *hints[]={L"Record the issue, findings, actions taken and next steps.",
      L"Track a maintenance window, backup, changes and service verification.",
      L"Keep names, roles, contact details and escalation information together.",
      L"Summarize status, completed work, outstanding work and the next owner."};
  const char *rtfs[]={
      "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs28\\b Issue\\b0\\par\\par\\b Findings\\b0\\par\\par\\b Actions taken\\b0\\par\\par\\b Next steps\\b0\\par}",
      "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs28\\b Maintenance checklist\\b0\\par\\uc1\\u9744?\\u8195?Confirm maintenance window\\par\\u9744?\\u8195?Verify backup\\par\\u9744?\\u8195?Perform changes\\par\\u9744?\\u8195?Verify service and notify client\\par}",
      "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs28\\b Client contacts\\b0\\par Name:\\par Role:\\par Phone:\\par Email:\\par Escalation:\\par}",
      "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs28\\b Handover\\b0\\par Current status:\\par Completed work:\\par Outstanding work:\\par Next owner:\\par Next update:\\par}"};
  std::vector<client_notes::NotePage> result;
  for (size_t i=0;i<4;++i) {
    client_notes::NotePage page{names[i],true,rtfs[i],{}}; page.id=i+1; page.ticket=hints[i]; result.push_back(std::move(page));
  }
  return result;
}
struct NotesTemplates {
  HWND owner=nullptr;
  ClientNotesDialogState *workspace=nullptr;
  fs::path root;
  client_notes::NotebookResult original;
  std::vector<client_notes::NotePage> pages;
  bool snippets=false;
  client_notes::NotePage draft;
  int selected=-1;
  bool loading=false,dirty=false;
  bool startFromNote=false;
  bool editorValid=true;
};
static bool NotesLoadTemplates(NotesTemplates &templates) {
  templates.root=g_sDataDir/(templates.snippets ? L"NotesSnippets" : L"NotesTemplates");
  if (!client_notes::SafeDirectory(g_sDataDir)) return false;
  std::error_code error;
  fs::create_directory(templates.root,error);
  if (error || !client_notes::SafeDirectory(templates.root)) return false;
  templates.original=client_notes::ReadNotebook(templates.root);
  if (templates.original.status==client_notes::ReadStatus::Error) return false;
  templates.pages=templates.original.status==client_notes::ReadStatus::Missing ? NotesBuiltinTemplates() : templates.original.pages;
  if (templates.snippets && templates.original.status==client_notes::ReadStatus::Missing) {
    templates.pages.clear(); const wchar_t *names[]={L"Troubleshooting questions",L"Maintenance sign-off",L"Escalation details",L"Next steps"};
    const wchar_t *bodies[]={L"What changed?\nWho is affected?\nWhen did it start?\nWhat have we tried?\n",L"Verification completed:\nClient notified:\nFollow-up required:\n",L"Issue:\nImpact:\nEvidence:\nEscalation owner:\n",L"Next action:\nOwner:\nExpected update:\n"};
    for (size_t i=0;i<4;++i) {client_notes::NotePage p{names[i],true,NotesPlainRtf(bodies[i]),{}};p.id=i+1;p.ticket=L"Insert this reusable block at your cursor, then fill in the details.";templates.pages.push_back(p);}
  }
  return true;
}
static bool NotesSaveTemplates(HWND dialog,NotesTemplates &templates,const std::vector<client_notes::NotePage> &pages) {
  if (!client_notes::WriteNotebook(templates.root,pages,templates.original,[&]{return client_notes::SafeDirectory(g_sDataDir) && client_notes::SafeDirectory(templates.root);})) {
    MessageBoxW(dialog,L"The reusable library could not be saved, or another window changed it. Close and reopen it before trying again. Existing entries were kept.",templates.snippets ? L"Snippets" : L"Templates",MB_OK|MB_ICONWARNING); return false;
  }
  templates.original=client_notes::ReadNotebook(templates.root); templates.pages=pages; return true;
}
static bool NotesTemplateDetails(HWND dialog,const std::vector<client_notes::NotePage> &pages,size_t exclude,client_notes::NotePage &page,bool snippet=false) {
  NotesInput input{snippet ? L"Snippet details" : L"Template details",snippet ? L"Snippet name (up to 64 characters)" : L"Template name (up to 64 characters)",L"Hint (up to 128 characters)",page.name,page.ticket};
  while (NotesAsk(dialog,input)) {
    if (client_notes::ValidPageName(input.first) && input.second.size()<=128 &&
        std::none_of(pages.begin(),pages.end(),[&](const auto &p){return p.id!=(exclude<pages.size() ? pages[exclude].id : 0) && client_notes::SamePageName(p.name,input.first);})) {
      page.name=input.first; page.ticket=input.second; return true;
    }
    MessageBoxW(dialog,L"Use a unique template name of 1-64 characters and a hint of up to 128 characters.",L"Template details",MB_OK|MB_ICONINFORMATION);
  }
  return false;
}
static client_notes::NotePage NotesTemplateFromPage(const client_notes::NotePage &source) {
  auto page=source; page.ticket.clear(); page.ticketUrl.clear(); page.history.clear();
  page.status=0; page.tags.clear();
  page.open=page.active=page.deleted=page.archived=page.pinned=page.pristine=false;
  page.cursor=page.scroll=0; page.zoom=100; return page;
}
static client_notes::NotePage NotesSnippetFromSelection(HWND owner,const client_notes::NotePage &source) {
  auto page=NotesTemplateFromPage(source); HWND edit=GetDlgItem(owner,IDC_CLIENT_NOTES_TEXT);
  CHARRANGE range{};SendMessageW(edit,EM_EXGETSEL,0,reinterpret_cast<LPARAM>(&range));
  if (range.cpMin!=range.cpMax) {
    NotesStream stream;EDITSTREAM data{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamOut};
    SendMessageW(edit,EM_STREAMOUT,SF_RTF|SFF_SELECTION,reinterpret_cast<LPARAM>(&data));
    if (!data.dwError && !stream.overflow && client_notes::NormalizeEditorExport(stream.bytes)) {
      page.rtf=stream.bytes;page.rich=true;page.text.clear();page.links.clear();const auto text=NotesPlain(page);
      for (const auto &link:source.links) if (!link.first.empty() && text.find(link.first)!=std::wstring::npos) page.links.push_back(link);
    } else page.rtf.clear();
  }
  return page;
}
static bool NotesInsertSnippet(HWND dialog,NotesTemplates &library,const client_notes::NotePage &page) {
  auto &workspace=*library.workspace;if (!HasActiveNote(workspace)) return false;
  auto &current=workspace.pages[workspace.activePage];
  for (const auto &link:page.links) if (std::any_of(current.links.begin(),current.links.end(),[&](const auto &prior){return prior.first==link.first && prior.second!=link.second;})) {
    MessageBoxW(dialog,L"This note already uses the same link label with a different address. Rename that link label before inserting the snippet so its destination stays clear.",L"Snippet link conflict",MB_OK|MB_ICONINFORMATION);return false;
  }
  NotesStream stream{page.rtf};EDITSTREAM data{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};
  HWND edit=GetDlgItem(library.owner,IDC_CLIENT_NOTES_TEXT);SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
  SendMessageW(edit,EM_STREAMIN,SF_RTF|SFF_SELECTION,reinterpret_cast<LPARAM>(&data));
  if (data.dwError) {MessageBoxW(dialog,L"The snippet could not be inserted completely. Use Undo to restore the selection before trying again.",L"Insert snippet",MB_OK|MB_ICONWARNING);return false;}
  for (const auto &link:page.links) if (std::none_of(current.links.begin(),current.links.end(),[&](const auto &prior){return prior.first==link.first;})) current.links.push_back(link);
  workspace.metadataChanged=true;ApplyNotesTextColors(edit);MarkNotesChanged(library.owner,workspace);
  return SaveClientNotesDraft(library.owner,workspace,true);
}
#include "NotesTemplatesUi.inl"

static void NotesTemplatePreview(HWND dialog,NotesTemplates &templates) {
  const LRESULT row=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);
  const bool valid=row>=0 && static_cast<size_t>(row)<templates.pages.size();
  for (int id : {IDOK,IDC_NOTES_ARCHIVE,IDC_NOTES_RESTORE,IDC_NOTES_DELETE})
    EnableWindow(GetDlgItem(dialog,id),valid && ((id!=IDC_NOTES_RESTORE && !(templates.snippets && id==IDOK)) || HasActiveNote(*templates.workspace)));
  SetDlgItemTextW(dialog,IDC_NOTES_INFO,valid ? templates.pages[row].ticket.c_str() : templates.snippets ? L"No snippets. Save selected text through More." : L"No templates. Save a note as a template through More.");
  HWND preview=GetDlgItem(dialog,IDC_NOTES_PREVIEW); SetWindowTextW(preview,L"");
  if (valid) {
    NotesStream stream{templates.pages[row].rtf}; EDITSTREAM data{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};
    SendMessageW(preview,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&data));
    SendMessageW(preview,EM_SETREADONLY,FALSE,0); ApplyNotesTextColors(preview); SendMessageW(preview,EM_SETREADONLY,TRUE,0);
  }
}
static void NotesTemplateFill(HWND dialog,NotesTemplates &templates,int selected=0) {
  SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_RESETCONTENT,0,0);
  for (const auto &page : templates.pages) SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(page.name.c_str()));
  if (!templates.pages.empty()) SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_SETCURSEL,(std::min)(selected,static_cast<int>(templates.pages.size())-1),0);
  NotesTemplatePreview(dialog,templates);
}
static void RefreshNotesTemplatesForTheme(HWND dialog) {
  auto *templates=reinterpret_cast<NotesTemplates *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (!templates) return;
  if (GetWindowLongPtrW(dialog,DWLP_DLGPROC)==reinterpret_cast<LONG_PTR>(NotesTemplateEditorProc)) {
    RefreshNotesTemplateEditorTheme(dialog,*templates);return;
  }
  SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_SETBKGNDCOLOR,0,g_themeColors.crControl);
  NotesTemplatePreview(dialog,*templates);
}
static void LayoutNotesTemplates(HWND dialog) {
  LayoutNotesLibrary(dialog);
  const UINT dpi=GetDpiForWindow(dialog); RECT area{},query{}; GetClientRect(dialog,&area);
  GetWindowRect(GetDlgItem(dialog,IDC_NOTES_QUERY),&query);
  SetWindowPos(GetDlgItem(dialog,IDC_NOTES_QUERY),nullptr,0,0,area.right-ScaleByDpi(32,dpi),query.bottom-query.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  int x=ScaleByDpi(16,dpi);
  RECT buttons{};GetWindowRect(GetDlgItem(dialog,IDOK),&buttons);MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&buttons),2);
  for (int id : {IDOK,IDC_NOTES_ARCHIVE,IDC_NOTES_RESTORE,IDC_NOTES_DELETE,IDC_NOTES_SAVE_TEMPLATE}) {
    SetWindowPos(GetDlgItem(dialog,id),nullptr,x,buttons.top,ScaleByDpi(132,dpi),buttons.bottom-buttons.top,SWP_NOZORDER|SWP_NOACTIVATE); x+=ScaleByDpi(144,dpi);
  }
}
static INT_PTR CALLBACK NotesTemplatesProc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) {
  auto *templates=reinterpret_cast<NotesTemplates *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (message==WM_INITDIALOG) {
    templates=reinterpret_cast<NotesTemplates *>(lParam); SetWindowLongPtrW(dialog,DWLP_USER,lParam);
    SetWindowTextW(dialog,templates->snippets ? L"Snippets - insert or customize" : L"Templates - choose, preview or customize"); ApplyCleanupDialogTheme(dialog);
    SetDlgItemTextW(dialog,IDC_NOTES_LIBRARY_HEADING,L"Reusable starter notes - available to every client");
    SetDlgItemTextW(dialog,IDC_NOTES_QUERY,L"Create note uses a starter. Save as new saves your current note. Update from note replaces the selected template.");
    ShowWindow(GetDlgItem(dialog,IDC_NOTES_SAVE_TEMPLATE),templates->snippets ? SW_HIDE : SW_SHOW);
    EnableWindow(GetDlgItem(dialog,IDC_NOTES_SAVE_TEMPLATE),HasActiveNote(*templates->workspace));
    EnableWindow(GetDlgItem(dialog,IDC_NOTES_RESTORE),HasActiveNote(*templates->workspace));
    if (templates->snippets) {SetDlgItemTextW(dialog,IDC_NOTES_LIBRARY_HEADING,L"Reusable text blocks - shared across clients");SetDlgItemTextW(dialog,IDC_NOTES_QUERY,L"Choose a block to insert at the cursor. Save selected text through More > Save selection as snippet.");}
    SendDlgItemMessageW(dialog,IDC_NOTES_QUERY,EM_SETREADONLY,TRUE,0);
    SetWindowLongPtrW(GetDlgItem(dialog,IDC_NOTES_QUERY),GWL_STYLE,GetWindowLongPtrW(GetDlgItem(dialog,IDC_NOTES_QUERY),GWL_STYLE)&~WS_TABSTOP);
    for (int id : {IDC_NOTES_FILTER,IDC_NOTES_FILTER_HINT,IDC_NOTES_ACTION_HINT,IDC_NOTES_EMPTY_TRASH,IDC_NOTES_OPEN_TICKET,IDC_NOTES_SORT,IDC_NOTES_STATUS_FILTER,IDC_NOTES_TAG_FILTER,IDC_NOTES_SELECT_ALL,IDC_NOTES_BATCH_EXPORT}) ShowWindow(GetDlgItem(dialog,id),SW_HIDE);
    SetDlgItemTextW(dialog,IDOK,L"Create note"); SetDlgItemTextW(dialog,IDC_NOTES_ARCHIVE,L"Name / hint");
    SetDlgItemTextW(dialog,IDC_NOTES_RESTORE,L"Update from note"); SetDlgItemTextW(dialog,IDC_NOTES_DELETE,L"Delete template");
    if (templates->snippets) {SetDlgItemTextW(dialog,IDOK,L"Insert snippet");SetDlgItemTextW(dialog,IDC_NOTES_RESTORE,L"Update content");SetDlgItemTextW(dialog,IDC_NOTES_DELETE,L"Delete snippet");}
    SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_SETBKGNDCOLOR,0,g_themeColors.crControl);
    SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_EXLIMITTEXT,0,client_notes::kMaxRichBytes);
    SetWindowPos(dialog,nullptr,0,0,ScaleByDpi(1000,GetDpiForWindow(dialog)),ScaleByDpi(690,GetDpiForWindow(dialog)),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    LayoutNotesTemplates(dialog);
    NotesTemplateFill(dialog,*templates); SetFocus(GetDlgItem(dialog,IDC_NOTES_RESULTS)); return FALSE;
  }
  if (message==WM_SIZE) { LayoutNotesTemplates(dialog); return TRUE; }
  if (message==WM_GETMINMAXINFO) { auto *limits=reinterpret_cast<MINMAXINFO *>(lParam); limits->ptMinTrackSize={ScaleByDpi(900,GetDpiForWindow(dialog)),ScaleByDpi(540,GetDpiForWindow(dialog))}; return TRUE; }
  if (const auto themed=HandleCleanupDialogTheme(dialog,message,wParam,lParam)) return *themed;
  if (message==WM_CLOSE || (message==WM_COMMAND && LOWORD(wParam)==IDCANCEL)) { EndDialog(dialog,IDCANCEL); return TRUE; }
  if (!templates || message!=WM_COMMAND) return FALSE;
  const UINT id=LOWORD(wParam);
  if (id==IDC_NOTES_SAVE_TEMPLATE && !templates->snippets && HasActiveNote(*templates->workspace)) {
    NotesWorkspaceCommand(templates->owner,*templates->workspace,IDC_NOTES_SAVE_TEMPLATE);
    if (NotesLoadTemplates(*templates)) NotesTemplateFill(dialog,*templates);
    return TRUE;
  }
  if (id==IDC_NOTES_RESULTS && HIWORD(wParam)==LBN_SELCHANGE) { NotesTemplatePreview(dialog,*templates); return TRUE; }
  const LRESULT row=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);
  if (row<0 || static_cast<size_t>(row)>=templates->pages.size()) return TRUE;
  if (id==IDOK || (id==IDC_NOTES_RESULTS && HIWORD(wParam)==LBN_DBLCLK)) {
    auto page=NotesTemplateFromPage(templates->pages[row]);
    if (templates->snippets) {
      auto &workspace=*templates->workspace;if (!HasActiveNote(workspace)) return TRUE;
      auto &current=workspace.pages[workspace.activePage];
      for (const auto &link:page.links) if (std::any_of(current.links.begin(),current.links.end(),[&](const auto &prior){return prior.first==link.first && prior.second!=link.second;})) {
        MessageBoxW(dialog,L"This note already uses the same link label with a different address. Rename that link label before inserting the snippet so its destination stays clear.",L"Snippet link conflict",MB_OK|MB_ICONINFORMATION);return TRUE;
      }
      NotesStream stream{page.rtf};EDITSTREAM data{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};
      HWND edit=GetDlgItem(templates->owner,IDC_CLIENT_NOTES_TEXT);SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
      SendMessageW(edit,EM_STREAMIN,SF_RTF|SFF_SELECTION,reinterpret_cast<LPARAM>(&data));
      if (data.dwError) {MessageBoxW(dialog,L"The snippet could not be inserted completely. Use Undo to restore the selection before trying again.",L"Insert snippet",MB_OK|MB_ICONWARNING);return TRUE;}
      for (const auto &link:page.links) if (std::none_of(current.links.begin(),current.links.end(),[&](const auto &prior){return prior.first==link.first;})) current.links.push_back(link);
      workspace.metadataChanged=true;ApplyNotesTextColors(edit);MarkNotesChanged(templates->owner,workspace);SaveClientNotesDraft(templates->owner,workspace,true);EndDialog(dialog,IDOK);return TRUE;
    }
    if (NotesAddPage(templates->owner,*templates->workspace,std::move(page))) EndDialog(dialog,IDOK); return TRUE;
  }
  auto pages=templates->pages;
  if (id==IDC_NOTES_ARCHIVE) {
    if (!NotesTemplateDetails(dialog,pages,row,pages[row],templates->snippets)) return TRUE;
  } else if (id==IDC_NOTES_RESTORE && HasActiveNote(*templates->workspace)) {
    if (MessageBoxW(dialog,templates->snippets ? L"Replace this snippet with the selected text (or the whole current note if nothing is selected)? Previously inserted text stays unchanged." : L"Replace the selected template content with the current note? Existing notes created from it will stay unchanged.",templates->snippets ? L"Update snippet" : L"Update template",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) return TRUE;
    auto replacement=templates->snippets ? NotesSnippetFromSelection(templates->owner,templates->workspace->pages[templates->workspace->activePage]) : NotesTemplateFromPage(templates->workspace->pages[templates->workspace->activePage]);
    if (replacement.rtf.empty()) {MessageBoxW(dialog,L"The selected text could not be captured safely. The saved entry was kept.",L"Reusable content",MB_OK|MB_ICONWARNING);return TRUE;}
    replacement.id=pages[row].id; replacement.name=pages[row].name; replacement.ticket=pages[row].ticket; pages[row]=std::move(replacement);
  } else if (id==IDC_NOTES_DELETE) {
    if (MessageBoxW(dialog,templates->snippets ? L"Delete this snippet? Previously inserted text stays unchanged." : L"Delete this template? Notes created from it will stay unchanged.",templates->snippets ? L"Delete snippet" : L"Delete template",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) return TRUE;
    pages.erase(pages.begin()+row);
  } else return TRUE;
  if (NotesSaveTemplates(dialog,*templates,pages)) NotesTemplateFill(dialog,*templates,static_cast<int>(row));
  return TRUE;
}
struct NotesTourStep {
  const wchar_t *title,*body,*caption;
  UINT image;
  bool permanent=false;
};
static constexpr std::array<NotesTourStep,14> kNotesTourSteps{{
  {L"Start a note and let it save",
   L"1. Click Notes beside Close all in the launcher. It opens the selected client or returns to your last notebook. Use the single client picker to search and switch notebooks.\r\n\r\n"
   L"2. Click + (or Ctrl+T). An Untitled note opens immediately; you do not need to name it first. Type your notes.\r\n\r\n"
   L"3. Pause briefly to let autosave run. Switching tabs or closing the notes window also saves pending edits. There is no Save button.\r\n\r\n"
   L"Notes belong to this client and are stored locally. The window follows your app theme and remembers its size. This walkthrough only shows examples; it does not edit your notes.",
   L"The notes workspace: tabs above the formatting toolbar, with the document below.",IDR_NOTES_TOUR_WORKSPACE},
  {L"Format text, highlights and checklists",
   L"1. Select text, then choose a heading, font size, Bold, Italic or Underline in the toolbar.\r\n\r\n"
   L"2. Use the highlighter's arrow to choose a color. The marker button applies or removes that color.\r\n\r\n"
   L"3. Choose the checklist button to create tasks. Click a box to mark an item done; its text is struck through. Enter adds the next item; Enter on an empty item ends the list.\r\n\r\n"
   L"4. Select words and choose Link to give them a label and web address. All note links open in your PC's default browser, outside client profiles. Right-click for Cut, Copy, Paste and Undo/Redo; pasting inserts plain text.\r\n\r\n"
   L"5. Choose Code in the style dropdown to open a language picker. Paste or type source, then Insert code block. The bordered box keeps its language label and monospace code. Code is stored as text and never run.",
   L"Example formatting: headings, completed checklist items and a yellow highlight.",IDR_NOTES_TOUR_WORKSPACE},
  {L"Name, organize and close tabs",
   L"1. Double-click a tab to rename it when useful. Hover over a shortened title to read its full name.\r\n\r\n"
   L"2. Drag tabs to reorder within their pinned or unpinned group; an accent line marks the drop position. Right-click for Move tab left/right, or use Ctrl+Shift+PgUp/PgDn. The same menu offers Duplicate, Pin, Close, Archive and Delete.\r\n\r\n"
   L"3. Use the tab's X or Ctrl+W to close it. A saved note remains in All notes. Ctrl+Shift+T reopens the last closed tab in this session. Reopened notes go to the end of their group.\r\n\r\n"
   L"Closing a tab does not put the note in trash. A new unnamed note that was never edited is discarded when closed. Ctrl+Tab and Ctrl+Shift+Tab move between open tabs.",
   L"Tabs keep frequently used notes close at hand. All notes also contains saved, closed tabs.",IDR_NOTES_TOUR_WORKSPACE},
  {L"Find and reopen a saved note",
   L"1. Click All notes. The current note is selected if it is in the results.\r\n\r\n"
   L"2. Type part of a title, a ticket number or some note text in the search field. Select a result to preview it.\r\n\r\n"
   L"3. Choose Current notes for open and saved notes, or Open tabs for just the tabs on screen. Archived and Trash have their own views. Current + archived shows all kept notes.\r\n\r\n"
   L"4. Select a current note and click Open to bring it into a tab. Restore an archived or deleted note before opening it. The list's [saved], [open], [archived] and [deleted] labels explain each note's state.",
   L"All notes shows a searchable list, ticket details and a preview of the selected note.",IDR_NOTES_TOUR_LIBRARY},
  {L"Link a note to a ticket (optional)",
   L"1. Open a note, then Details. Add the optional ticket number and URL. Changes save automatically; click outside the panel to close it.\r\n\r\n"
   L"2. Enter a ticket number if useful, for example 44553. Click OK. All notes shows [Ticket 44553] beside its title and Ticket: 44553 above the preview. You can search for the number.\r\n\r\n"
   L"3. Add an HTTP/HTTPS ticket URL to enable Open ticket link in All notes and More. It opens in your default browser and regular sign-in, separate from client profiles.\r\n\r\n"
   L"4. Use Status on the top action row to set this note's work status and tags. The button shows Investigating, Waiting, Follow-up or Resolved. This is local organization and does not update the ticketing system.\r\n\r\n"
   L"These fields are labels and links you provide. They do not fetch the ticket, import its messages or synchronize with a ticketing system. Leave them blank if you do not need them.",
   L"Ticket details explains the optional number and URL. The example uses ticket 44553.",IDR_NOTES_TOUR_TICKET},
  {L"Use or customize a reusable template",
   L"1. Click Templates on the top action row (or More > Templates). Select a saved starter to read or edit its name, hint and formatted content in this same window.\r\n\r\n"
   L"2. Click New template to write a starter directly here. Name it, explain when to use it in the hint, and type its content. Heading, Bold, Italic, Underline and Bullets format the selected text.\r\n\r\n"
   L"3. Save template keeps your starter for every client. Use template saves any pending edits, then creates an independent client note from it. From current note is an optional shortcut for copying an existing note into a new template draft.\r\n\r\n"
   L"4. Switching templates or closing asks whether to save unsaved changes. Updating or deleting a template leaves existing notes unchanged. Templates have their own Save button; client notes still autosave.",
   L"Create, edit, save and use templates together in one window.",IDR_NOTES_TOUR_TEMPLATES},
  {L"Copy or move a note to another client",
   L"1. Open the note you want to transfer. Choose More > Copy to another client or Move to another client.\r\n\r\n"
   L"2. Use the dropdown to select another active client, or type part of its name to filter suggestions. Select the intended client, then click OK.\r\n\r\n"
   L"3. Check the destination in the confirmation before proceeding. Copy keeps the original note. Move saves the destination copy first, then keeps the source in the original client's Trash view.\r\n\r\n"
   L"A transfer does not combine two clients' notebooks. Open Client Notes for the destination client to find the transferred note.",
   L"The client picker supports a dropdown and suggestions while typing.",IDR_NOTES_TOUR_TRANSFER},
  {L"Archive, use trash and restore a note",
   L"1. In All notes, select a current note. Archive keeps it but hides it from Current notes. To trash moves it to Trash.\r\n\r\n"
   L"2. Choose Archived or Trash from the filter to find it again.\r\n\r\n"
   L"3. Select the note and click Restore. It returns as a saved note with its tab closed. Switch to Current notes and click Open.\r\n\r\n"
   L"Trash is this client's trash bin. Notes do not expire automatically, and deleted notes still count toward the 10,000-note limit. Closing a tab, archiving and moving to trash are different actions. Permanent removal is covered in the next step.",
   L"Trash explains retention and offers Restore, Delete forever and Empty trash.",IDR_NOTES_TOUR_TRASH},
  {L"Permanently delete notes or empty trash",
   L"1. Open All notes and choose Trash. Review what is in this client's trash.\r\n\r\n"
   L"2. Delete forever removes only the selected deleted note. Empty trash removes EVERY deleted note for this client in one operation, including notes hidden by a search.\r\n\r\n"
   L"3. Empty trash shows the client and total count before confirmation. No is selected by default. Choose Yes only when you intend permanent removal.\r\n\r\n"
   L"Both actions permanently delete the affected notes and their revision histories. This cannot be undone in the app. Export a client notebook or make a full backup first if you might need them later.\r\n\r\n"
   L"Empty trash keeps current notes, archived notes and other clients' notebooks. A failed save stops the operation before erasing notes.",
   L"Empty trash is in Trash. It includes all deleted notes, even hidden search results.",IDR_NOTES_TOUR_TRASH,true},
  {L"Recover, export and return to your notes",
   L"1. Use Ctrl+F to find text in the open note, or Ctrl+H to replace it. More > Version history previews earlier text before you restore it.\r\n\r\n"
   L"2. If the title reports a failed save, keep the window open. Use More > Retry saving, Copy entire note or Export this note to preserve your draft. Resolve save conflict handles a notebook changed elsewhere.\r\n\r\n"
   L"3. Export this note saves TXT or RTF. Export client notebook saves a .ctn copy including tickets, links, archived/deleted notes and history; it does not include the shared template library. Import previews supported text/RTF or notebook data before adding notes.\r\n\r\n"
   L"Continue for quick capture and organizing your work. Replay any step from Help > Step-by-step walkthrough or More > Notes walkthrough. The guide itself never changes your notes.",
   L"Use All notes for saved notes and More for finding, recovery, importing and exporting.",IDR_NOTES_TOUR_LIBRARY},
  {L"Capture quickly and search More actions",
   L"1. Select a client in the launcher and choose Settings > Quick note, or right-click a pinned client > Quick note.\r\n\r\n"
   L"2. Write immediately. The title is optional; this saves a closed note in the client's notebook. Pause for autosave or close to flush it. An untouched capture creates no empty note.\r\n\r\n"
   L"3. In the full editor, click More or press Ctrl+K. Type a feature name, such as snippet, status, work log or export. Read its hint, then click the action or press Enter. Click outside to close the panel.\r\n\r\n"
   L"All formatting and window commands opens the complete menu. Quick capture is plain text; open the saved note in All notes when you need formatting.",
   L"More lets you find commands by name and read what they do.",IDR_NOTES_TOUR_ACTIONS},
  {L"Set status, tags and sort your notes",
   L"1. In a note, choose More > Status and tags. Pick No status, Investigating, Waiting on client, Follow-up needed or Resolved.\r\n\r\n"
   L"2. Add comma-separated tags, for example network, maintenance. Click Apply. These belong to this note and appear in All notes.\r\n\r\n"
   L"3. All notes combines text search, lifecycle, work status and tag filters. Tag filtering matches part of a tag. Sort by recently edited, newest created, title or ticket number.\r\n\r\n"
   L"Work status is independent of closing a tab, archiving or trash. Resolved notes remain in Current notes until you archive them.",
   L"All notes keeps filters above the results and shows status and tags with the preview.",IDR_NOTES_TOUR_LIBRARY},
  {L"Reuse snippets and keep a work log",
   L"1. Click Snippets at the top. Choose New snippet, give it a name and optional hint, then write directly in its editor and choose Save snippet. Snippets are shared across clients.\r\n\r\n"
   L"2. From selection starts a snippet from selected text in the current note, or the whole note when nothing is selected. You can edit it before saving.\r\n\r\n"
   L"3. Choose a saved snippet to edit its name, hint or content. Save snippet keeps the changes; Insert in note places the latest saved block at your cursor. Delete snippet does not remove text already inserted.\r\n\r\n"
   L"4. Add work log update appends a dated section at the end of this note. Type the latest progress there; earlier updates remain. Templates create separate notes; snippets insert into the current note.",
   L"Snippets are reusable blocks you can preview before inserting.",IDR_NOTES_TOUR_SNIPPETS},
  {L"Select several notes and export a handover",
   L"1. In All notes, choose Select notes > Select several notes, then click notes to tick them. Space toggles the focused note. Select all visible notes selects the filtered results.\r\n\r\n"
   L"2. Archive, Restore, To trash or Delete forever applies to ticked notes. The hint shows how many are selected. Changing search or filters clears the ticks. Finish selecting returns to normal preview and Open.\r\n\r\n"
   L"3. Export handover saves the selected notes to one readable HTML document with client, titles, work status, tags, ticket links and note text. Open it in a browser to read, print or save as PDF.\r\n\r\n"
   L"Rich formatting is simplified to readable text; revision histories are excluded. Use Export client notebook for a full notebook backup. No export sends anything automatically. Click Finish to return to your notes.",
   L"Selection mode scopes actions to ticked notes and provides a printable handover.",IDR_NOTES_TOUR_LIBRARY}
}};
struct NotesTourState {
  size_t position=0;
  IStream *stream=nullptr;
  std::unique_ptr<Gdiplus::Image> image;
  HFONT font=nullptr,strongFont=nullptr;
  void ClearImage() { image.reset(); if (stream) stream->Release(); stream=nullptr; }
  ~NotesTourState() { ClearImage(); if (font) DeleteObject(font); if (strongFont) DeleteObject(strongFont); }
};
static bool NotesTourLoadImage(NotesTourState &tour,UINT id) {
  tour.ClearImage();
  const auto resource=FindResourceW(g_hInst,MAKEINTRESOURCEW(id),RT_RCDATA);
  if (!resource) return false;
  const auto loaded=LoadResource(g_hInst,resource); const auto bytes=SizeofResource(g_hInst,resource);
  const auto data=static_cast<const BYTE *>(LockResource(loaded));
  if (!data || !bytes || !(tour.stream=SHCreateMemStream(data,bytes))) return false;
  tour.image.reset(Gdiplus::Image::FromStream(tour.stream));
  if (!tour.image || tour.image->GetLastStatus()!=Gdiplus::Ok) { tour.ClearImage(); return false; }
  return true;
}
static void LayoutNotesTour(HWND dialog) {
  RECT area{}; GetClientRect(dialog,&area); const UINT dpi=GetDpiForWindow(dialog);
  const auto px=[&](int n){return ScaleByDpi(n,dpi);};
  const int margin=px(20),gap=px(20),bottom=area.bottom-margin-px(32);
  const int left=(std::min)(px(390),static_cast<int>(area.right)*40/100),right=margin+left+gap;
  const auto place=[&](int id,int x,int y,int w,int h) { SetWindowPos(GetDlgItem(dialog,id),nullptr,x,y,(std::max)(1,w),(std::max)(1,h),SWP_NOZORDER|SWP_NOACTIVATE); };
  place(IDC_NOTES_TOUR_TOPICS,margin,px(35),px(370),px(380));
  place(IDC_GUIDE_COUNT,margin+px(390),px(40),area.right-margin*2-px(390),px(22));
  place(IDC_GUIDE_TITLE,margin,px(85),area.right-margin*2,px(30));
  place(IDC_NOTES_TOUR_WARNING,margin,px(123),area.right-margin*2,px(44));
  place(IDC_GUIDE_BODY,margin,px(179),left,bottom-px(195));
  place(IDC_NOTES_TOUR_IMAGE,right,px(179),area.right-right-margin,bottom-px(239));
  place(IDC_NOTES_TOUR_CAPTION,right,bottom-px(52),area.right-right-margin,px(44));
  place(IDC_GUIDE_BACK,area.right-margin-px(304),bottom,px(92),px(32));
  place(IDOK,area.right-margin-px(200),bottom,px(92),px(32));
  place(IDCANCEL,area.right-margin-px(96),bottom,px(96),px(32));
}
static void RefreshNotesTour(HWND dialog,NotesTourState &tour) {
  const auto &step=kNotesTourSteps[tour.position];
  SetDlgItemTextW(dialog,IDC_GUIDE_TITLE,step.title); SetDlgItemTextW(dialog,IDC_GUIDE_BODY,step.body);
  SetDlgItemTextW(dialog,IDC_NOTES_TOUR_CAPTION,(std::wstring(step.caption)+L" Example in a dark theme; yours follows your selected theme.").c_str());
  SetDlgItemTextW(dialog,IDC_NOTES_TOUR_IMAGE,step.caption);
  SetDlgItemTextW(dialog,IDC_NOTES_TOUR_WARNING,step.permanent ? L"PERMANENT DELETION: Empty trash deletes notes and their history. This cannot be undone." : L"Read-only guide: these examples do not change your notes.");
  SetDlgItemTextW(dialog,IDC_GUIDE_COUNT,(L"Step "+std::to_wstring(tour.position+1)+L" of "+std::to_wstring(kNotesTourSteps.size())).c_str());
  SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_SETCURSEL,tour.position,0);
  EnableWindow(GetDlgItem(dialog,IDC_GUIDE_BACK),tour.position>0);
  SetDlgItemTextW(dialog,IDOK,tour.position+1==kNotesTourSteps.size() ? L"Finish" : L"Next");
  SendDlgItemMessageW(dialog,IDC_GUIDE_BODY,EM_SETSEL,0,0); SendDlgItemMessageW(dialog,IDC_GUIDE_BODY,EM_SCROLLCARET,0,0);
  NotesTourLoadImage(tour,step.image); InvalidateRect(GetDlgItem(dialog,IDC_NOTES_TOUR_IMAGE),nullptr,FALSE);
}
static void NotesTourFonts(HWND dialog,NotesTourState &tour,UINT dpi) {
  if (tour.font) DeleteObject(tour.font); if (tour.strongFont) DeleteObject(tour.strongFont);
  tour.font=CreateUiFont(dpi); tour.strongFont=CreateUiStrongFont(dpi);
  EnumChildWindows(dialog,[](HWND child,LPARAM data)->BOOL {
    const auto *tour=reinterpret_cast<const NotesTourState *>(data);
    const int id=GetDlgCtrlID(child); SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(id==IDC_GUIDE_TITLE || id==IDC_NOTES_TOUR_WARNING ? tour->strongFont : tour->font),TRUE); return TRUE;
  },reinterpret_cast<LPARAM>(&tour));
  SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_SETITEMHEIGHT,-1,ScaleByDpi(28,dpi));
  SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_SETITEMHEIGHT,0,ScaleByDpi(28,dpi));
  SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_SETDROPPEDWIDTH,ScaleByDpi(600,dpi),0);
}
static INT_PTR CALLBACK NotesTourProc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) {
  auto *tour=reinterpret_cast<NotesTourState *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (message==WM_INITDIALOG) {
    tour=reinterpret_cast<NotesTourState *>(lParam); if (!tour) return FALSE;
    SetWindowLongPtrW(dialog,DWLP_USER,lParam); ApplyCleanupDialogTheme(dialog);
    for (size_t i=0;i<kNotesTourSteps.size();++i) {
      const auto label=std::to_wstring(i+1)+L". "+kNotesTourSteps[i].title;
      SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
    }
    SetWindowSubclass(GetDlgItem(dialog,IDC_NOTES_TOUR_TOPICS),NotesComboSubclass,1,0);
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(dialog,MONITOR_DEFAULTTONEAREST),&monitor);
    const UINT dpi=GetDpiForWindow(dialog);
    const int width=(std::min)(ScaleByDpi(1160,dpi),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)-32);
    const int height=(std::min)(ScaleByDpi(740,dpi),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top)-32);
    SetWindowPos(dialog,nullptr,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    NotesTourFonts(dialog,*tour,dpi);
    LayoutNotesTour(dialog); RefreshNotesTour(dialog,*tour); return TRUE;
  }
  if (message==WM_DPICHANGED && tour && lParam) {
    const auto &rect=*reinterpret_cast<const RECT *>(lParam);
    SetWindowPos(dialog,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE);
    NotesTourFonts(dialog,*tour,HIWORD(wParam)); LayoutNotesTour(dialog); return TRUE;
  }
  if (message==WM_SIZE) { LayoutNotesTour(dialog); return TRUE; }
  if (message==WM_GETMINMAXINFO && lParam) { auto *limits=reinterpret_cast<MINMAXINFO *>(lParam); limits->ptMinTrackSize={ScaleByDpi(900,GetDpiForWindow(dialog)),ScaleByDpi(560,GetDpiForWindow(dialog))}; return TRUE; }
  if (message==WM_DRAWITEM && lParam) {
    const auto &draw=*reinterpret_cast<DRAWITEMSTRUCT *>(lParam);
    if (DrawNotesComboItem(draw)) return TRUE;
    if (draw.CtlID==IDC_NOTES_TOUR_IMAGE) {
      FillRect(draw.hDC,&draw.rcItem,g_hbrThemeWindow);
      if (tour && tour->image) {
        const float ratio=(std::min)(static_cast<float>(draw.rcItem.right-draw.rcItem.left)/tour->image->GetWidth(),static_cast<float>(draw.rcItem.bottom-draw.rcItem.top)/tour->image->GetHeight());
        const float width=tour->image->GetWidth()*ratio,height=tour->image->GetHeight()*ratio;
        Gdiplus::Graphics graphics(draw.hDC); graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(tour->image.get(),draw.rcItem.left+(draw.rcItem.right-draw.rcItem.left-width)/2,static_cast<float>(draw.rcItem.top),width,height);
      } else { SetBkMode(draw.hDC,TRANSPARENT); SetTextColor(draw.hDC,g_themeColors.crWindowText); RECT rect=draw.rcItem; DrawTextW(draw.hDC,L"Screenshot unavailable. Follow the instructions on the left.",-1,&rect,DT_WORDBREAK|DT_NOPREFIX); }
      return TRUE;
    }
  }
  if (const auto themed=HandleCleanupDialogTheme(dialog,message,wParam,lParam)) return *themed;
  if (!tour) return FALSE;
  if (message==WM_CLOSE || (message==WM_COMMAND && LOWORD(wParam)==IDCANCEL)) { EndDialog(dialog,IDCANCEL); return TRUE; }
  if (message!=WM_COMMAND) return FALSE;
  const int id=LOWORD(wParam);
  if (id==IDC_NOTES_TOUR_TOPICS && HIWORD(wParam)==CBN_SELCHANGE) {
    const auto selected=SendDlgItemMessageW(dialog,IDC_NOTES_TOUR_TOPICS,CB_GETCURSEL,0,0);
    if (selected>=0 && static_cast<size_t>(selected)<kNotesTourSteps.size()) tour->position=static_cast<size_t>(selected);
  } else if (id==IDC_GUIDE_BACK && tour->position>0) --tour->position;
  else if (id==IDOK) { if (tour->position+1==kNotesTourSteps.size()) { EndDialog(dialog,IDOK); return TRUE; } ++tour->position; }
  else return FALSE;
  RefreshNotesTour(dialog,*tour); return TRUE;
}
static INT_PTR CALLBACK NotesHelpProc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) {
  if (message==WM_INITDIALOG) { ApplyCleanupDialogTheme(dialog); return TRUE; }
  if (const auto themed=HandleCleanupDialogTheme(dialog,message,wParam,lParam)) return *themed;
  if (message==WM_CLOSE) { EndDialog(dialog,IDCANCEL); return TRUE; }
  if (message==WM_COMMAND && (LOWORD(wParam)==IDOK || LOWORD(wParam)==IDCANCEL ||
      LOWORD(wParam)==IDC_NOTES_TEMPLATES || LOWORD(wParam)==IDC_NOTES_LIBRARY || LOWORD(wParam)==IDC_NOTES_TRASH || LOWORD(wParam)==IDC_NOTES_WALKTHROUGH)) {
    EndDialog(dialog,LOWORD(wParam)); return TRUE;
  }
  return FALSE;
}
#include "NotesInspectorUi.inl"
static bool NotesWorkspaceCommand(HWND dialog,ClientNotesDialogState &state,UINT command) {
  if (command==IDC_NOTES_SWITCH_CLIENT) { NotesShowPanel(dialog,&state,false);return true; }
  if ((command==IDC_NOTES_DETAILS || command==IDC_NOTES_OPEN_TICKET) && HasActiveNote(state)) {
    if (command==IDC_NOTES_OPEN_TICKET && ValidNotesUrl(state.pages[state.activePage].ticketUrl))
      OpenNotesUrl(dialog,state,state.pages[state.activePage].ticketUrl,true);
    else if (SaveClientNotesDraft(dialog,state,true)) NotesShowPanel(dialog,&state,true);
    return true;
  }
  if (command==IDC_NOTES_HELP) {
    const auto result=DialogBoxParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_HELP),dialog,NotesHelpProc,0);
    if (result!=-1) WritePrivateProfileStringW(L"notes_window",L"help_seen",L"1",g_sConfigPath.c_str());
    if (result==IDC_NOTES_TEMPLATES || result==IDC_NOTES_LIBRARY || result==IDC_NOTES_TRASH || result==IDC_NOTES_WALKTHROUGH) NotesWorkspaceCommand(dialog,state,static_cast<UINT>(result));
    return true;
  }
  if (command==IDC_NOTES_WALKTHROUGH) { NotesTourState tour; DialogBoxParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_WALKTHROUGH),dialog,NotesTourProc,reinterpret_cast<LPARAM>(&tour)); return true; }
  HWND edit=GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT);
  const bool active=HasActiveNote(state);
  if (command==IDC_NOTES_LIBRARY) { NotesShowLibrary(dialog,state); return true; }
  if (command==IDC_NOTES_TRASH) { NotesShowLibrary(dialog,state,false,3); return true; }
  if (command==IDC_NOTES_MORE || command==IDC_NOTES_ACTIONS) {
    NotesShowPanel(dialog,&state,false,true);return true;
  }
  if (command==IDC_NOTES_METADATA && active) {
    if (SaveClientNotesDraft(dialog,state,true)) NotesShowPanel(dialog,&state,true);
    return true;
  }
  if (command==IDC_NOTES_WORKLOG && active) {
    if (!SaveClientNotesDraft(dialog,state,true)) return true;
    IUnknown *ole=nullptr;ITextDocument *document=nullptr;SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&ole));
    if (ole) {ole->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void **>(&document));ole->Release();}
    SendMessageW(edit,EM_STOPGROUPTYPING,0,0);if (document) document->BeginEditCollection();
    CHARRANGE end{-1,-1};SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&end));SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"\r\n\r\n"));
    CHARFORMAT2W normal{sizeof(normal)};normal.dwMask=CFM_BOLD|CFM_ITALIC|CFM_UNDERLINE|CFM_STRIKEOUT|CFM_LINK|CFM_COLOR|CFM_BACKCOLOR|CFM_SIZE|CFM_FACE;
    normal.dwEffects=CFE_AUTOBACKCOLOR;normal.crTextColor=g_themeColors.crControlText;normal.yHeight=240;wcscpy_s(normal.szFaceName,L"Segoe UI");
    SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&normal));
    PARAFORMAT2 paragraph{sizeof(paragraph)};paragraph.dwMask=PFM_NUMBERING|PFM_STARTINDENT|PFM_OFFSET|PFM_SPACEBEFORE|PFM_SPACEAFTER|PFM_LINESPACING;
    paragraph.bLineSpacingRule=5;paragraph.dyLineSpacing=20;SendMessageW(edit,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
    const auto text=L"Update - "+NotesDate(client_notes::Now())+L"\r\n";SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str()));
    if (document) {document->EndEditCollection();document->Release();}MarkNotesChanged(dialog,state);SaveClientNotesDraft(dialog,state,true);SetFocus(edit);return true;
  }
  if (command==IDC_NOTES_HANDOVER && active) {if (SaveClientNotesDraft(dialog,state,true)) NotesExportHandover(dialog,state,{state.activePage});return true;}
  if (command==IDC_NOTES_MORE_MENU) {
    HMENU menu=CreatePopupMenu();
    const auto add=[&](UINT id,const wchar_t *label,bool needsNote=true) {
      AppendMenuW(menu,MF_STRING | (needsNote && !active ? MF_GRAYED : 0),id,label);
    };
    add(IDC_NOTES_FIND_TOGGLE,L"Find in note\tCtrl+F");
    add(IDC_NOTES_REPLACE,L"Find and replace\tCtrl+H");
    add(IDC_NOTES_DUPLICATE,L"Duplicate note"); add(IDC_NOTES_HISTORY,L"Version history");
    add(IDC_NOTES_TICKET,L"Ticket details (optional)..."); add(IDC_NOTES_OPEN_TICKET,L"Open ticket link");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    add(IDC_NOTES_STRIKE,L"Strikethrough"); add(IDC_NOTES_CODE,L"Insert labeled code block..."); add(IDC_NOTES_DATE,L"Insert date and time");
    add(IDC_NOTES_LINK_REMOVE,L"Remove selected link"); add(IDC_NOTES_LINK_COPY,L"Copy selected link address");
    add(IDC_NOTES_COPY_ALL,L"Copy entire note as text");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    add(IDC_NOTES_TEMPLATE,L"New blank note",false);
    add(IDC_NOTES_TEMPLATES,L"Templates - preview, create or edit...",false);
    add(IDC_NOTES_SAVE_TEMPLATE,L"Save current note as template...");
    add(IDC_NOTES_IMPORT,L"Import text, RTF or notebook...",false); add(IDC_NOTES_EXPORT,L"Export this note...");
    add(IDC_NOTES_EXPORT_ALL,L"Export client notebook...",false);
    add(IDC_NOTES_TRANSFER,L"Copy to another client..."); add(IDC_NOTES_MOVE,L"Move to another client...");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    add(IDC_NOTES_ZOOM_IN,L"Zoom in\tCtrl++"); add(IDC_NOTES_ZOOM_OUT,L"Zoom out\tCtrl+-"); add(IDC_NOTES_ZOOM_RESET,L"Actual size\tCtrl+0");
    AppendMenuW(menu,MF_STRING | (state.topmost ? MF_CHECKED : 0),IDC_NOTES_TOPMOST,L"Always on top");
    AppendMenuW(menu,MF_STRING | (state.showProgress ? MF_CHECKED : 0),IDC_NOTES_PROGRESS,L"Checklist progress in All notes");
    add(IDC_NOTES_RETRY,L"Retry saving\tCtrl+S",false); add(IDC_NOTES_CONFLICT,L"Resolve save conflict...",false);
    add(IDC_NOTES_HELP,L"How notes work...",false);
    add(IDC_NOTES_WALKTHROUGH,L"Notes walkthrough...",false);
    RECT anchor{}; GetWindowRect(GetDlgItem(dialog,IDC_NOTES_MORE),&anchor);
    const UINT selected=TrackThemedNotesMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTALIGN,anchor.right,anchor.bottom,dialog);
    DestroyMenu(menu); if (selected) SendMessageW(dialog,WM_COMMAND,selected,0); return true;
  }
  if (command==IDC_NOTES_FIND_TOGGLE || command==IDC_NOTES_FIND_CLOSE) {
    state.findVisible=command==IDC_NOTES_FIND_TOGGLE; LayoutClientNotes(dialog,GetDpiForWindow(dialog));
    SetFocus(state.findVisible ? GetDlgItem(dialog,IDC_NOTES_FIND) : edit); return true;
  }
  if (command==IDC_NOTES_FIND_NEXT || command==IDC_NOTES_FIND_PREV) {
    NotesFind(dialog,state,command==IDC_NOTES_FIND_PREV); return true;
  }
  if (command==IDC_NOTES_RETRY) { SaveClientNotesDraft(dialog,state,true); return true; }
  if (command==IDC_NOTES_TOPMOST || command==IDC_NOTES_PROGRESS) {
    if (command==IDC_NOTES_TOPMOST) {
      state.topmost=!state.topmost;
      SetWindowPos(dialog,state.topmost ? HWND_TOPMOST : HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    } else state.showProgress=!state.showProgress;
    WritePrivateProfileStringW(L"notes_window",command==IDC_NOTES_TOPMOST ? L"topmost" : L"progress",
        (command==IDC_NOTES_TOPMOST ? state.topmost : state.showProgress) ? L"1" : L"0",g_sConfigPath.c_str());
    return true;
  }
  if (command==IDC_NOTES_REOPEN) {
    if (!SaveClientNotesDraft(dialog,state,true)) return true;
    while (!state.closedPages.empty()) {
      const auto id=state.closedPages.back(); state.closedPages.pop_back();
      for (size_t i=0;i<state.pages.size();++i) if (state.pages[i].id==id && !state.pages[i].deleted && !state.pages[i].archived) {
        NotesOpenSavedPage(dialog,state,i); return true;
      }
    }
    NotesShowLibrary(dialog,state); return true;
  }
  if (command==IDC_NOTES_TEMPLATES || command==IDC_NOTES_SAVE_TEMPLATE || command==IDC_NOTES_SNIPPETS || command==IDC_NOTES_SAVE_SNIPPET) {
    if (!SaveClientNotesDraft(dialog,state,true)) return true;
    NotesTemplates templates{dialog,&state};
    templates.snippets=command==IDC_NOTES_SNIPPETS || command==IDC_NOTES_SAVE_SNIPPET;
    if (!NotesLoadTemplates(templates)) { MessageBoxW(dialog,L"The template library could not be opened safely. Existing templates were kept.",L"Templates",MB_OK|MB_ICONWARNING); return true; }
    templates.startFromNote=command==IDC_NOTES_SAVE_TEMPLATE || command==IDC_NOTES_SAVE_SNIPPET;
    DialogBoxParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_TEMPLATE_EDITOR),dialog,NotesTemplateEditorProc,reinterpret_cast<LPARAM>(&templates));
    return true;
  }
  if (command>=IDC_NOTES_TEMPLATE && command<=IDC_NOTES_TEMPLATE+4) {
    if (!SaveClientNotesDraft(dialog,state,true)) return true;
    const size_t index=command-IDC_NOTES_TEMPLATE;
    auto page=index==0 ? client_notes::NotePage{L"Untitled",true,"{\\rtf1\\ansi }",{}} : NotesBuiltinTemplates()[index-1];
    page.ticket.clear(); page.pristine=index==0;
    NotesAddPage(dialog,state,std::move(page)); return true;
  }
  if (command==IDC_NOTES_IMPORT) {
    if (!SaveClientNotesDraft(dialog,state,true)) return true;
    std::wstring path; if (!NotesPickFile(dialog,false,path)) return true;
    const fs::path selected(path);
    const auto bytes=client_notes::ReadFileSnapshot(selected.parent_path(),selected.filename().c_str(),client_notes::kMaxNotebookBytes);
    if (bytes.status!=client_notes::ReadStatus::Ok) { MessageBoxW(dialog,L"The selected file could not be read safely.",L"Import note",MB_OK|MB_ICONWARNING); return true; }
    if (_wcsicmp(selected.extension().c_str(),L".ctn")==0) {
      std::vector<client_notes::NotePage> imported;
      if (!client_notes::ParseNotebook(bytes.bytes,imported) || imported.size()+state.pages.size()>client_notes::kMaxPages) {
        MessageBoxW(dialog,L"This notebook is invalid or would exceed the 10,000-note limit.",L"Import notebook",MB_OK|MB_ICONWARNING); return true;
      }
      std::wstring preview=L"Import "+std::to_wstring(imported.size())+L" notes as separate copies? Archived and deleted entries will also be recovered. Existing notes will remain unchanged.\n\n";
      for (size_t i=0;i<(std::min)(imported.size(),size_t{12});++i) preview+=imported[i].name+L"\n";
      if (MessageBoxW(dialog,preview.c_str(),L"Import notebook preview",MB_OKCANCEL|MB_ICONINFORMATION)!=IDOK) return true;
      for (auto page : imported) if (!NotesAddPage(dialog,state,std::move(page))) break;
      return true;
    }
    if (bytes.bytes.size()>client_notes::kMaxRichBytes) return true;
    client_notes::NotePage page; page.name=selected.stem().wstring();
    if (_wcsicmp(selected.extension().c_str(),L".rtf")==0) {
      if (!client_notes::ValidRichText(bytes.bytes)) { MessageBoxW(dialog,L"This RTF contains unsupported content or is invalid. Save it as plain text and import that copy.",L"Import note",MB_OK|MB_ICONWARNING); return true; }
      page.rich=true; page.rtf=bytes.bytes;
    } else {
      std::string plain=bytes.bytes; if (plain.rfind("\xef\xbb\xbf",0)==0) plain.erase(0,3);
      if (!client_notes::DecodeUtf8(plain,page.text)) { MessageBoxW(dialog,L"Text imports must use UTF-8 encoding.",L"Import note",MB_OK|MB_ICONWARNING); return true; }
    }
    const auto preview=NotesPlain(page);
    if (MessageBoxW(dialog,(L"Import as a new note? Existing notes will remain unchanged.\n\n"+preview.substr(0,700)).c_str(),L"Import preview",MB_OKCANCEL|MB_ICONINFORMATION)==IDOK)
      NotesAddPage(dialog,state,std::move(page));
    return true;
  }
  if (command==IDC_NOTES_EXPORT_ALL) {
    if (!CaptureNotesPage(dialog,state,true)) return true;
    std::wstring path; if (!NotesPickFile(dialog,true,path,true)) return true;
    std::string bytes;
    if (!client_notes::SerializeNotebook(state.pages,bytes) || !NotesWriteExport(path,bytes)) MessageBoxW(dialog,L"The notebook could not be exported.",L"Export notebook",MB_OK|MB_ICONWARNING);
    return true;
  }
  if (command==IDC_NOTES_CONFLICT) {
    CaptureNotesPage(dialog,state,false); NotesWriteRecovery(dialog,state);
    const auto latest=client_notes::ReadNotebook(state.clientRoot);
    if (latest.status==client_notes::ReadStatus::Error) {
      MessageBoxW(dialog,L"The saved notebook is unavailable. Your draft remains open. Use Copy entire note or Export this note to keep an additional copy, then retry saving.",L"Save recovery",MB_OK|MB_ICONWARNING); return true;
    }
    const int choice=MessageBoxW(dialog,L"YES: Keep your changed notes as separate recovered copies alongside the latest saved notes.\nNO: Compare the saved notes in a read-only preview.\nCANCEL: Keep working on your current draft.",L"Resolve save conflict",MB_YESNOCANCEL|MB_ICONQUESTION);
    if (choice==IDCANCEL) return true;
    if (choice==IDNO) {
      ClientNotesDialogState saved{state.clientName,state.clientRoot,latest}; saved.pages=latest.pages;
      // A preview-only library uses disabled mutation commands.
      NotesLibrary preview; preview.owner=dialog; preview.state=&saved;
      preview.readOnly=true;
      DialogBoxParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),dialog,NotesLibraryProc,reinterpret_cast<LPARAM>(&preview));
      return true;
    }
    auto drafts=state.pages;
    size_t required=latest.pages.size();
    for (const auto &p : drafts) if (!p.deleted && std::none_of(latest.pages.begin(),latest.pages.end(),[&](const auto &saved){return saved.id==p.id && NotesSameContent(saved,p);})) ++required;
    if (required>client_notes::kMaxPages) {
      MessageBoxW(dialog,L"There is not enough room to recover all changed notes. Export your drafts before reloading. Your current editor has been kept.",L"Notebook full",MB_OK|MB_ICONWARNING); return true;
    }
    for (const auto &[id,window] : state.editors) DestroyWindow(window);
    state.editors.clear(); state.attachedEditor=nullptr;
    state.pages=latest.pages; state.original=latest; state.baselines.assign(state.pages.size(),{}); state.activePage=state.pages.size();
    for (auto page : drafts) {
      if (page.deleted || std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &p){return p.id==page.id && NotesSameContent(p,page);})) continue;
      if (state.pages.size()>=client_notes::kMaxPages) {
        MessageBoxW(dialog,L"The notebook is full. Remaining draft notes are retained in the recovery file for the next session.",L"Recovered copies",MB_OK|MB_ICONWARNING); break;
      }
      page.name=NotesUniqueName(state,page.name+L" recovered"); page.id=client_notes::NextId(state.pages); page.open=true; page.archived=false;
      state.pages.push_back(std::move(page)); state.baselines.emplace_back(); state.activePage=state.pages.size()-1;
    }
    state.metadataChanged=true; NotesSelectAvailable(dialog,state); SaveClientNotesDraft(dialog,state,true); return true;
  }
  if (!active) {
    if (command==IDC_NOTES_TAB_LEFT || command==IDC_NOTES_TAB_RIGHT) return true;
    if (command>=IDC_NOTES_CLOSE_TAB && command<=IDC_NOTES_PROGRESS) return true;
    return false;
  }
  auto &page=state.pages[state.activePage];
  switch (command) {
  case IDC_NOTES_TAB_LEFT: case IDC_NOTES_TAB_RIGHT: {
    const auto current=std::find(state.visiblePages.begin(),state.visiblePages.end(),state.activePage);
    if (current!=state.visiblePages.end()) {
      const int from=static_cast<int>(current-state.visiblePages.begin());
      NotesReorderTab(dialog,state,from,from+(command==IDC_NOTES_TAB_LEFT ? -1 : 1));
    }
    return true;
  }
  case IDC_NOTES_LINK: case IDC_NOTES_LINK_COPY: case IDC_NOTES_LINK_REMOVE: {
    CHARRANGE selected{}; NotesSelection(edit,selected);
    std::wstring label=NotesRangeText(edit,selected.cpMin,selected.cpMax), address;
    for (const auto &[text,url] : page.links) if (text==label) { address=url; break; }
    if (address.empty() && ValidNotesUrl(label)) address=label;
    if (command==IDC_NOTES_LINK_COPY) { if (!address.empty()) NotesCopyText(dialog,address); return true; }
    if (command==IDC_NOTES_LINK_REMOVE) {
      NotesCharacterFormat(dialog,state,CFM_LINK|CFM_UNDERLINE|CFM_COLOR,0,g_themeColors.crControlText);
      return true;
    }
    NotesInput input{L"Insert or edit link",L"Link text",L"HTTP / HTTPS address",label,address};
    if (!NotesAsk(dialog,input)) return true;
    if (!ValidNotesUrl(input.second) || input.first.size()>2048 || input.first.find_first_of(L"\r\n")!=std::wstring::npos) {
      MessageBoxW(dialog,L"Use a valid HTTP/HTTPS address and a single-line label.",L"Link",MB_OK|MB_ICONWARNING); return true;
    }
    if (input.first.empty()) input.first=input.second;
    auto found=std::find_if(page.links.begin(),page.links.end(),[&](const auto &link){return link.first==input.first;});
    if (found!=page.links.end() && found->second!=input.second && input.first!=label) {
      MessageBoxW(dialog,L"That link text already points to another address in this note. Choose a distinct label.",L"Link text",MB_OK|MB_ICONINFORMATION); return true;
    }
    if (found==page.links.end() && page.links.size()>=256) return true;
    SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
    SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(input.first.c_str()));
    selected.cpMax=selected.cpMin+static_cast<LONG>(input.first.size());
    SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&selected));
    NotesCharacterFormat(dialog,state,CFM_LINK|CFM_UNDERLINE|CFM_COLOR,CFE_LINK|CFE_UNDERLINE,g_bThemeIsDark ? RGB(138,205,235) : RGB(0,91,158));
    if (found==page.links.end()) page.links.emplace_back(input.first,input.second);
    else found->second=input.second;
    state.metadataChanged=true; return true;
  }
  case IDC_NOTES_CLOSE_TAB: case IDC_NOTES_CLOSE_ALL: case IDC_NOTES_CLOSE_OTHERS: {
    if (!SaveClientNotesDraft(dialog,state,true)) return true;
    NotesPreferAdjacentTab(state);
    const auto keep=page.id;
    for (size_t i=state.pages.size();i>0;--i) {
      const auto &p=state.pages[i-1];
      if (!p.open || p.archived || p.deleted) continue;
      if (command==IDC_NOTES_CLOSE_TAB && p.id!=keep) continue;
      if (command==IDC_NOTES_CLOSE_OTHERS && p.id==keep) continue;
      NotesClosePage(dialog,state,i-1);
    }
    NotesSelectAvailable(dialog,state); SaveClientNotesDraft(dialog,state,true); return true;
  }
  case IDC_NOTES_ARCHIVE: case IDC_NOTES_DELETE:
    if (!SaveClientNotesDraft(dialog,state,true)) return true;
    NotesPreferAdjacentTab(state);
    page.open=false; page.active=false;
    if (command==IDC_NOTES_ARCHIVE) page.archived=true; else page.deleted=true;
    state.metadataChanged=true; NotesSelectAvailable(dialog,state); SaveClientNotesDraft(dialog,state,true); return true;
  case IDC_NOTES_PIN:
    page.pinned=!page.pinned; state.metadataChanged=true; RefreshNotesTabs(dialog,state); SaveClientNotesDraft(dialog,state,true); return true;
  case IDC_NOTES_DUPLICATE: {
    if (!SaveClientNotesDraft(dialog,state,true)) return true;
    auto copy=page; copy.name+=L" copy"; copy.history.clear(); copy.pristine=false;
    NotesAddPage(dialog,state,std::move(copy)); return true;
  }
  case IDC_NOTES_HISTORY: NotesShowLibrary(dialog,state,true); return true;
  case IDC_NOTES_COPY_ALL: NotesCopyText(dialog,NotesWindowText(edit)); return true;
  case IDC_NOTES_EXPORT: {
    if (!CaptureNotesPage(dialog,state,true)) return true;
    std::wstring path; if (!NotesPickFile(dialog,true,path)) return true;
    std::string bytes=page.rtf;
    if (_wcsicmp(fs::path(path).extension().c_str(),L".txt")==0 && !client_notes::EncodeUtf8(NotesWindowText(edit),bytes)) return true;
    if (!NotesWriteExport(path,bytes)) MessageBoxW(dialog,L"The note could not be exported.",L"Export note",MB_OK|MB_ICONWARNING);
    return true;
  }
  case IDC_NOTES_TICKET: {
    NotesInput input{L"Note information - created "+NotesDate(page.created)+L", edited "+NotesDate(page.modified),L"Ticket number (optional)",L"Ticket URL (optional, http/https)",page.ticket,page.ticketUrl};
    input.hint=L"Link this note to a support ticket. The number appears in All notes and is searchable. Open ticket link uses your default browser and regular sign-in, separate from client profiles. No ticket lookup or sync is performed.";
    if (NotesAsk(dialog,input)) {
      if (input.first.size()>128 || (!input.second.empty() && !ValidNotesUrl(input.second))) { MessageBoxW(dialog,L"Use a ticket number of at most 128 characters and a valid HTTP/HTTPS URL.",L"Ticket details",MB_OK|MB_ICONWARNING); return true; }
      page.ticket=input.first; page.ticketUrl=input.second; state.metadataChanged=true; SaveClientNotesDraft(dialog,state,true);
    }
    return true;
  }
  case IDC_NOTES_OPEN_TICKET:
    if (ValidNotesUrl(page.ticketUrl)) OpenNotesUrl(dialog,state,page.ticketUrl,true);
    else MessageBoxW(dialog,L"Add a ticket URL in Note information first.",L"Ticket link",MB_OK|MB_ICONINFORMATION);
    return true;
  case IDC_NOTES_TRANSFER: case IDC_NOTES_MOVE: {
    if (!SaveClientNotesDraft(dialog,state,true)) return true;
    NotesClientPicker picker{state.clientName,L"",command==IDC_NOTES_MOVE};
    if (DialogBoxParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_CLIENT_PICKER),dialog,NotesClientPickerProc,reinterpret_cast<LPARAM>(&picker))!=IDOK) return true;
    NotesInput input; input.first=picker.selected;
    fs::path destination;
    if (client_notes::SamePageName(input.first,state.clientName) || !IsExistingClientProfile(input.first) || IsClientArchived(input.first) || !TryGetSafeClientProfilePath(input.first,destination) || !RevalidateSafeClientContainerPath(input.first,destination)) {
      MessageBoxW(dialog,L"Choose a different, existing, unarchived client.",L"Client notes",MB_OK|MB_ICONWARNING); return true;
    }
    if (MessageBoxW(dialog,(std::wstring(command==IDC_NOTES_MOVE ? L"Move" : L"Copy")+L" \""+page.name+L"\" from "+state.clientName+L" to "+input.first+L"?").c_str(),L"Confirm destination",MB_OKCANCEL|MB_ICONQUESTION)!=IDOK) return true;
    auto target=client_notes::ReadNotebook(destination);
    if (target.status==client_notes::ReadStatus::Error || target.pages.size()>=client_notes::kMaxPages) { MessageBoxW(dialog,L"The destination notebook is unavailable or full.",L"Transfer note",MB_OK|MB_ICONWARNING); return true; }
    const auto expectedTarget=target;
    // Normalize any legacy TXT destination with the same native RTF exporter.
    for (auto &p : target.pages) if (!p.rich) {
      HWND scratch=CreateWindowExW(0,L"RICHEDIT50W",p.text.c_str(),ES_MULTILINE,0,0,0,0,nullptr,nullptr,g_hInst,nullptr);
      const bool ok=scratch && StreamClientNotesOut(scratch,p.rtf); if (scratch) DestroyWindow(scratch);
      if (!ok) return true; p.rich=true; p.text.clear();
    }
    ClientNotesDialogState naming; naming.pages=target.pages;
    auto copy=page; copy.id=client_notes::NextId(target.pages); copy.name=NotesUniqueName(naming,copy.name);
    copy.active=false; copy.open=false; copy.pinned=false; copy.pristine=false;
    target.pages.push_back(std::move(copy));
    if (!client_notes::WriteNotebook(destination,target.pages,expectedTarget,[&]{return RevalidateSafeClientContainerPath(input.first,destination);})) {
      MessageBoxW(dialog,L"The destination could not be saved. The source note has been kept.",L"Transfer note",MB_OK|MB_ICONWARNING); return true;
    }
    if (command==IDC_NOTES_MOVE) {
      page.deleted=true; page.open=false; state.metadataChanged=true; NotesSelectAvailable(dialog,state); SaveClientNotesDraft(dialog,state,true);
    }
    return true;
  }
  case IDC_NOTES_STRIKE: {
    CHARFORMAT2W format{sizeof(format)}; SendMessageW(edit,EM_GETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
    NotesCharacterFormat(dialog,state,CFM_STRIKEOUT,(format.dwEffects&CFE_STRIKEOUT) ? 0 : CFE_STRIKEOUT); return true;
  }
  case IDC_NOTES_CODE: {
    NotesCreateCodeBlock(dialog,state); return true;
  }
  case IDC_NOTES_DATE: {
    const auto text=NotesDate(client_notes::Now()); SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(text.c_str())); SetFocus(edit); return true;
  }
  case IDC_NOTES_ZOOM_IN: case IDC_NOTES_ZOOM_OUT: case IDC_NOTES_ZOOM_RESET:
    page.zoom=command==IDC_NOTES_ZOOM_RESET ? 100 : static_cast<uint32_t>(std::clamp(static_cast<int>(page.zoom)+(command==IDC_NOTES_ZOOM_IN ? 10 : -10),25,400));
    SendMessageW(edit,EM_SETZOOM,page.zoom,100); state.metadataChanged=true; SetFocus(edit); return true;
  case IDC_NOTES_REPLACE: case IDC_NOTES_REPLACE_ALL: {
    NotesInput input{L"Find and replace",L"Find text",L"Replace with",NotesWindowText(GetDlgItem(dialog,IDC_NOTES_FIND)),L""};
    if (!NotesAsk(dialog,input) || input.first.empty()) return true;
    const int choice=MessageBoxW(dialog,L"Replace all matches? Choose No to replace the next match only.",L"Replace",MB_YESNOCANCEL|MB_ICONQUESTION);
    if (choice==IDCANCEL) return true;
    CHARRANGE selected{}; NotesSelection(edit,selected); LONG start=choice==IDYES ? 0 : selected.cpMin;
    SendMessageW(edit,EM_STOPGROUPTYPING,0,0);
    IUnknown *ole=nullptr; ITextDocument *document=nullptr;
    SendMessageW(edit,EM_GETOLEINTERFACE,0,reinterpret_cast<LPARAM>(&ole));
    if (ole) { ole->QueryInterface(__uuidof(ITextDocument),reinterpret_cast<void **>(&document)); ole->Release(); }
    if (document) document->BeginEditCollection();
    size_t count=0;
    for (;;) {
      FINDTEXTEXW search{{start,-1},input.first.data(),{}};
      if (SendMessageW(edit,EM_FINDTEXTEXW,FR_DOWN,reinterpret_cast<LPARAM>(&search))<0) break;
      SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&search.chrgText));
      SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(input.second.c_str())); ++count;
      start=search.chrgText.cpMin+static_cast<LONG>(input.second.size());
      if (choice!=IDYES) break;
    }
    if (document) { document->EndEditCollection(); document->Release(); } SetFocus(edit);
    if (!count) MessageBoxW(dialog,L"No matches found.",L"Replace",MB_OK|MB_ICONINFORMATION);
    return true;
  }
  default: return false;
  }
}
static bool NotesTranslateKey(MSG &message) {
  if (message.message!=WM_KEYDOWN || !g_hNotesWindow) return false;
  auto *state=reinterpret_cast<ClientNotesDialogState *>(GetWindowLongPtrW(g_hNotesWindow,DWLP_USER));
  if (!state) return false;
  const bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0, shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
  UINT command=0;
  if (!ctrl && message.wParam==VK_TAB && GetFocus()==GetDlgItem(g_hNotesWindow,IDC_CLIENT_NOTES_TEXT) && NotesListKey(GetFocus(),WM_KEYDOWN,VK_TAB)) { MarkNotesChanged(g_hNotesWindow,*state); return true; }
  if (ctrl) {
    switch (message.wParam) {
    case 'W': command=IDC_NOTES_CLOSE_TAB; break;
    case 'T': command=shift ? IDC_NOTES_REOPEN : IDC_NOTES_ADD_TAB; break;
    case 'F': command=IDC_NOTES_FIND_TOGGLE; break;
    case 'H': command=IDC_NOTES_REPLACE; break;
    case 'S': command=IDC_NOTES_RETRY; break;
    case 'K': command=IDC_NOTES_ACTIONS; break;
    case VK_PRIOR: if (shift) command=IDC_NOTES_TAB_LEFT; break;
    case VK_NEXT: if (shift) command=IDC_NOTES_TAB_RIGHT; break;
    case VK_OEM_PLUS: case VK_ADD: command=IDC_NOTES_ZOOM_IN; break;
    case VK_OEM_MINUS: case VK_SUBTRACT: command=IDC_NOTES_ZOOM_OUT; break;
    case '0': command=IDC_NOTES_ZOOM_RESET; break;
    case VK_TAB:
      if (!state->visiblePages.empty() && SaveClientNotesDraft(g_hNotesWindow,*state,true)) {
        int selected=TabCtrl_GetCurSel(GetDlgItem(g_hNotesWindow,IDC_NOTES_TABS));
        selected=(selected+(shift ? static_cast<int>(state->visiblePages.size())-1 : 1))%static_cast<int>(state->visiblePages.size());
        state->activePage=state->visiblePages[selected]; RefreshNotesTabs(g_hNotesWindow,*state); LoadNotesPage(g_hNotesWindow,*state);
      }
      return true;
    }
  } else if (message.wParam==VK_F3) command=shift ? IDC_NOTES_FIND_PREV : IDC_NOTES_FIND_NEXT;
  else if (message.wParam==VK_ESCAPE && state->dragTab>=0) {
    SendDlgItemMessageW(g_hNotesWindow,IDC_NOTES_TABS,WM_CANCELMODE,0,0);return true;
  }
  else if (message.wParam==VK_ESCAPE && state->findVisible) command=IDC_NOTES_FIND_CLOSE;
  else if (message.wParam==VK_RETURN && GetFocus()==GetDlgItem(g_hNotesWindow,IDC_NOTES_FIND)) command=IDC_NOTES_FIND_NEXT;
  if (!command) return false;
  SendMessageW(g_hNotesWindow,WM_COMMAND,command,0); return true;
}

static bool NotesListKey(HWND edit,UINT message,WPARAM key) {
  CHARRANGE selection{}; NotesSelection(edit,selection);
  const LRESULT line=SendMessageW(edit,EM_LINEFROMCHAR,selection.cpMin,0);
  const LONG start=static_cast<LONG>(SendMessageW(edit,EM_LINEINDEX,line,0));
  const LONG length=static_cast<LONG>(SendMessageW(edit,EM_LINELENGTH,selection.cpMin,0));
  const auto prefix=NotesRangeText(edit,start,start+2);
  PARAFORMAT2 paragraph{sizeof(paragraph)}; SendMessageW(edit,EM_GETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph));
  const bool checklist=IsNotesChecklist(prefix);
  if (key==VK_TAB && message==WM_KEYDOWN && (checklist || paragraph.wNumbering)) {
    paragraph.dwMask=PFM_STARTINDENT;
    paragraph.dxStartIndent=std::clamp(paragraph.dxStartIndent+((GetKeyState(VK_SHIFT)&0x8000) ? -360 : 360),0L,7200L);
    SendMessageW(edit,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph)); return true;
  }
  if (key!=VK_RETURN || message!=WM_CHAR || selection.cpMin!=selection.cpMax) return false;
  if (checklist) {
    if (length<=2) {
      CHARRANGE marker{start,start+2}; SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&marker));
      SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L""));
    } else {
      SendMessageW(edit,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L"\r\n\u2610\u2003"));
      CHARRANGE end{}; NotesSelection(edit,end);
      CHARRANGE inserted{selection.cpMin,end.cpMax};
      SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&inserted));
      CHARFORMAT2W format{sizeof(format)}; format.dwMask=CFM_STRIKEOUT|CFM_BACKCOLOR|CFM_COLOR;
      format.dwEffects=CFE_AUTOBACKCOLOR; format.crTextColor=g_themeColors.crControlText;
      SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
      SendMessageW(edit,EM_EXSETSEL,0,reinterpret_cast<LPARAM>(&end));
    }
    return true;
  }
  if (paragraph.wNumbering && length==0) {
    paragraph.dwMask=PFM_NUMBERING|PFM_STARTINDENT|PFM_OFFSET; paragraph.wNumbering=0; paragraph.dxStartIndent=paragraph.dxOffset=0;
    SendMessageW(edit,EM_SETPARAFORMAT,0,reinterpret_cast<LPARAM>(&paragraph)); return true;
  }
  return false;
}
