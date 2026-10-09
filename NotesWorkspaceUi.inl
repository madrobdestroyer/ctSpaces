// Native notes workspace commands. Included after the editor helpers so all
// persistence and formatting actions use the same validated paths.
struct NotesInput {
  std::wstring title, label1, label2, first, second;
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
    if (input->label2.empty()) {
      ShowWindow(GetDlgItem(dialog,IDC_NOTES_LABEL2),SW_HIDE);
      ShowWindow(GetDlgItem(dialog,IDC_NOTES_INPUT2),SW_HIDE);
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
  std::wstring text=NotesWindowText(edit); DestroyWindow(edit); return text;
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
static void NotesSelectAvailable(HWND dialog,ClientNotesDialogState &state) {
  if (!HasActiveNote(state)) {
    state.activePage=state.pages.size();
    for (size_t i=0;i<state.pages.size();++i)
      if (state.pages[i].open && !state.pages[i].deleted && !state.pages[i].archived) { state.activePage=i; break; }
  }
  RefreshNotesTabs(dialog,state); LoadNotesPage(dialog,state);
}
static bool NotesAddPage(HWND dialog,ClientNotesDialogState &state,client_notes::NotePage page) {
  if (state.pages.size()>=client_notes::kMaxPages) {
    MessageBoxW(dialog,L"This notebook has 256 notes, including archived and deleted notes. Export or permanently remove unneeded deleted notes first.",L"Notes limit",MB_OK|MB_ICONINFORMATION); return false;
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
    if (state.attachedEditor==edit->second) state.attachedEditor=nullptr;
    DestroyWindow(edit->second); state.editors.erase(edit);
  }
  state.pages.erase(state.pages.begin()+index); state.baselines.erase(state.baselines.begin()+index);
  if (state.activePage>index) --state.activePage;
  else if (state.activePage==index) state.activePage=state.pages.size();
  state.metadataChanged=true;
}
static void NotesClosePage(HWND dialog,ClientNotesDialogState &state,size_t index) {
  auto &page=state.pages[index];
  if (page.pristine && NotesPlain(page).find_first_not_of(L" \r\n\t")==std::wstring::npos) {
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
struct NotesLibrary {
  HWND owner=nullptr;
  ClientNotesDialogState *state=nullptr;
  bool history=false;
  bool readOnly=false;
  size_t page=0;
  std::vector<size_t> results;
  std::map<uint64_t,std::wstring> plain;
};
static void NotesLibraryPreview(HWND dialog,NotesLibrary &library) {
  const LRESULT row=SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_GETCURSEL,0,0);
  const bool valid=row>=0 && static_cast<size_t>(row)<library.results.size();
  HWND preview=GetDlgItem(dialog,IDC_NOTES_PREVIEW); SetWindowTextW(preview,L"");
  SetDlgItemTextW(dialog,IDC_NOTES_INFO,L""); EnableWindow(GetDlgItem(dialog,IDOK),valid);
  for (int id : {IDC_NOTES_ARCHIVE,IDC_NOTES_DELETE,IDC_NOTES_RESTORE}) EnableWindow(GetDlgItem(dialog,id),valid && !library.history);
  if (!valid) return;
  const auto &page=library.state->pages[library.history ? library.page : library.results[row]];
  const std::string &rtf=library.history ? page.history[library.results[row]].rtf : page.rtf;
  NotesStream stream{rtf}; EDITSTREAM data{reinterpret_cast<DWORD_PTR>(&stream),0,NotesStreamIn};
  if (!page.rich && !library.history) SetWindowTextW(preview,page.text.c_str());
  else SendMessageW(preview,EM_STREAMIN,SF_RTF,reinterpret_cast<LPARAM>(&data));
  SendMessageW(preview,EM_SETREADONLY,FALSE,0); ApplyNotesTextColors(preview); SendMessageW(preview,EM_SETREADONLY,TRUE,0);
  std::wstring info=page.name+L"\r\nCreated: "+NotesDate(page.created)+L"   Edited: "+NotesDate(page.modified);
  if (!page.ticket.empty()) info+=L"   Ticket: "+page.ticket;
  if (library.state->showProgress) {
    const auto text=NotesPlain(page);
    const auto done=std::count(text.begin(),text.end(),L'\x2611');
    const auto total=done+std::count(text.begin(),text.end(),L'\x2610');
    if (total) info+=L"   Checklist: "+std::to_wstring(done)+L"/"+std::to_wstring(total);
  }
  SetDlgItemTextW(dialog,IDC_NOTES_INFO,info.c_str());
  EnableWindow(GetDlgItem(dialog,IDC_NOTES_ARCHIVE),!library.history && !page.archived && !page.deleted);
  EnableWindow(GetDlgItem(dialog,IDC_NOTES_RESTORE),!library.history && (page.archived || page.deleted));
  SetDlgItemTextW(dialog,IDC_NOTES_DELETE,page.deleted ? L"Delete forever" : L"Delete");
}
static void RefreshNotesLibraryForTheme(HWND window) {
  auto *library=reinterpret_cast<NotesLibrary *>(GetWindowLongPtrW(window,DWLP_USER));
  if (!library) return;
  SendDlgItemMessageW(window,IDC_NOTES_PREVIEW,EM_SETBKGNDCOLOR,0,g_themeColors.crControl);
  NotesLibraryPreview(window,*library);
}
static void NotesLibraryFill(HWND dialog,NotesLibrary &library) {
  const auto query=NotesLower(NotesWindowText(GetDlgItem(dialog,IDC_NOTES_QUERY)));
  const int filter=static_cast<int>(SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_GETCURSEL,0,0));
  library.results.clear(); SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_RESETCONTENT,0,0);
  if (library.history) {
    const auto &history=library.state->pages[library.page].history;
    for (size_t i=history.size();i>0;--i) {
      library.results.push_back(i-1); const auto title=NotesDate(history[i-1].time);
      SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str()));
    }
  } else for (size_t i=0;i<library.state->pages.size();++i) {
    const auto &page=library.state->pages[i];
    if ((filter==0 && (page.deleted || page.archived)) || (filter==1 && (!page.open || page.deleted || page.archived)) ||
        (filter==2 && (!page.archived || page.deleted)) || (filter==3 && !page.deleted) || (filter==4 && page.deleted)) continue;
    if (!query.empty()) {
      auto [it,inserted]=library.plain.try_emplace(page.id);
      if (inserted) it->second=NotesPlain(page);
      if (NotesLower(page.name+L" "+page.ticket+L" "+it->second).find(query)==std::wstring::npos) continue;
    }
    library.results.push_back(i);
    std::wstring title=(page.pinned ? L"* " : L"")+page.name+(page.deleted ? L" [deleted]" : page.archived ? L" [archived]" : page.open ? L" [open]" : L"");
    if (!query.empty()) {
      auto text=library.plain[page.id]; const auto match=NotesLower(text).find(query);
      if (match!=std::wstring::npos) {
        text=text.substr(match>20 ? match-20 : 0,80);
        std::replace(text.begin(),text.end(),L'\r',L' '); std::replace(text.begin(),text.end(),L'\n',L' ');
        title+=L" - "+text;
      }
    }
    SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str()));
  }
  if (!library.results.empty()) SendDlgItemMessageW(dialog,IDC_NOTES_RESULTS,LB_SETCURSEL,0,0);
  NotesLibraryPreview(dialog,library);
}
static void LayoutNotesLibrary(HWND dialog) {
  RECT area{}; GetClientRect(dialog,&area); const UINT dpi=GetDpiForWindow(dialog);
  const auto px=[&](int n){return ScaleByDpi(n,dpi);};
  const int margin=px(16),gap=px(16),row=px(32),bottom=area.bottom-margin-row;
  const int column=(std::min)(px(290),static_cast<int>(area.right)*34/100),right=margin+column+gap;
  const auto place=[&](int id,int x,int y,int w,int h) {
    SetWindowPos(GetDlgItem(dialog,id),nullptr,x,y,(std::max)(0,w),(std::max)(0,h),SWP_NOZORDER|SWP_NOACTIVATE);
  };
  place(IDC_NOTES_QUERY,margin,px(36),area.right*60/100-margin,row);
  place(IDC_NOTES_FILTER,area.right*60/100+gap,px(36),area.right*40/100-gap-margin,px(220));
  place(IDC_NOTES_RESULTS,margin,px(88),column,bottom-gap-px(88));
  place(IDC_NOTES_INFO,right,px(86),area.right-right-margin,px(54));
  place(IDC_NOTES_PREVIEW,right,px(146),area.right-right-margin,bottom-gap-px(146));
  int x=margin;
  for (int id : {IDOK,IDC_NOTES_ARCHIVE,IDC_NOTES_RESTORE,IDC_NOTES_DELETE}) {
    place(id,x,bottom,px(108),row); x+=px(120);
  }
  place(IDCANCEL,area.right-margin-px(108),bottom,px(108),row);
  RECT text{}; GetClientRect(GetDlgItem(dialog,IDC_NOTES_PREVIEW),&text); InflateRect(&text,-px(10),-px(10));
  SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_SETRECT,0,reinterpret_cast<LPARAM>(&text));
}
static INT_PTR CALLBACK NotesLibraryProc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) {
  auto *lib=reinterpret_cast<NotesLibrary *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if (message==WM_INITDIALOG) {
    lib=reinterpret_cast<NotesLibrary *>(lParam); SetWindowLongPtrW(dialog,DWLP_USER,lParam);
    SetWindowTextW(dialog,lib->history ? L"Note history - preview before restoring" : (L"All notes - "+lib->state->clientName).c_str());
    for (auto label : {L"Current notes",L"Open tabs",L"Archived",L"Recently deleted",L"Current + archived"})
      SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
    SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETCURSEL,0,0);
    ApplyCleanupDialogTheme(dialog);
    SetWindowSubclass(GetDlgItem(dialog,IDC_NOTES_FILTER),NotesComboSubclass,1,0);
    SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETITEMHEIGHT,-1,ScaleByDpi(30,GetDpiForWindow(dialog)));
    SendDlgItemMessageW(dialog,IDC_NOTES_FILTER,CB_SETITEMHEIGHT,0,ScaleByDpi(30,GetDpiForWindow(dialog)));
    SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_SETBKGNDCOLOR,0,g_themeColors.crControl);
    SendDlgItemMessageW(dialog,IDC_NOTES_PREVIEW,EM_EXLIMITTEXT,0,client_notes::kMaxRichBytes);
    if (lib->history) {
      SetDlgItemTextW(dialog,IDOK,L"Restore version");
      for (int id : {IDC_NOTES_QUERY,IDC_NOTES_FILTER,IDC_NOTES_ARCHIVE,IDC_NOTES_DELETE,IDC_NOTES_RESTORE}) ShowWindow(GetDlgItem(dialog,id),SW_HIDE);
    }
    MONITORINFO monitor{sizeof(monitor)}; GetMonitorInfoW(MonitorFromWindow(dialog,MONITOR_DEFAULTTONEAREST),&monitor);
    const UINT dpi=GetDpiForWindow(dialog);
    const int width=(std::min)(ScaleByDpi(1000,dpi),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)-32);
    const int height=(std::min)(ScaleByDpi(690,dpi),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top)-32);
    SetWindowPos(dialog,nullptr,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-width)/2,
        monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-height)/2,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
    LayoutNotesLibrary(dialog);
    NotesLibraryFill(dialog,*lib);
    if (lib->readOnly) for (int id : {IDOK,IDC_NOTES_ARCHIVE,IDC_NOTES_DELETE,IDC_NOTES_RESTORE}) ShowWindow(GetDlgItem(dialog,id),SW_HIDE);
    return TRUE;
  }
  if (message==WM_SIZE) { LayoutNotesLibrary(dialog); return TRUE; }
  if (message==WM_GETMINMAXINFO && lParam) {
    auto *limits=reinterpret_cast<MINMAXINFO *>(lParam);
    limits->ptMinTrackSize={ScaleByDpi(720,GetDpiForWindow(dialog)),ScaleByDpi(480,GetDpiForWindow(dialog))}; return TRUE;
  }
  if (message==WM_DRAWITEM && lParam && DrawNotesComboItem(*reinterpret_cast<DRAWITEMSTRUCT *>(lParam))) return TRUE;
  if (message==WM_MEASUREITEM && lParam && reinterpret_cast<MEASUREITEMSTRUCT *>(lParam)->CtlID==IDC_NOTES_FILTER) {
    reinterpret_cast<MEASUREITEMSTRUCT *>(lParam)->itemHeight=ScaleByDpi(30,GetDpiForWindow(dialog)); return TRUE;
  }
  if (const auto themed=HandleCleanupDialogTheme(dialog,message,wParam,lParam)) return *themed;
  if (!lib) return FALSE;
  if (message==WM_TIMER) { KillTimer(dialog,1); NotesLibraryFill(dialog,*lib); return TRUE; }
  if (message==WM_CLOSE || (message==WM_COMMAND && LOWORD(wParam)==IDCANCEL)) { EndDialog(dialog,IDCANCEL); return TRUE; }
  if (message!=WM_COMMAND) return FALSE;
  const int id=LOWORD(wParam);
  if ((id==IDC_NOTES_QUERY && HIWORD(wParam)==EN_CHANGE) || (id==IDC_NOTES_FILTER && HIWORD(wParam)==CBN_SELCHANGE)) {
    SetTimer(dialog,1,200,nullptr); return TRUE;
  }
  if (id==IDC_NOTES_RESULTS && HIWORD(wParam)==LBN_SELCHANGE) { NotesLibraryPreview(dialog,*lib); return TRUE; }
  if (lib->readOnly) return TRUE;
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
  if (id==IDOK || (id==IDC_NOTES_RESULTS && HIWORD(wParam)==LBN_DBLCLK)) {
    if (page.deleted || page.archived) {
      MessageBoxW(dialog,L"Restore this note before opening it.",L"All notes",MB_OK|MB_ICONINFORMATION); return TRUE;
    }
    page.open=true; state.activePage=index; state.metadataChanged=true;
    NotesSelectAvailable(lib->owner,state); SaveClientNotesDraft(lib->owner,state,true); EndDialog(dialog,IDOK); return TRUE;
  }
  if (id==IDC_NOTES_ARCHIVE) { page.archived=true; page.open=false; }
  else if (id==IDC_NOTES_RESTORE) { page.archived=false; page.deleted=false; page.open=false; }
  else if (id==IDC_NOTES_DELETE) {
    if (page.deleted) {
      if (MessageBoxW(dialog,(L"Permanently delete \""+page.name+L"\" and its history? This cannot be undone.").c_str(),L"Delete note forever",MB_YESNO|MB_DEFBUTTON2|MB_ICONWARNING)!=IDYES) return TRUE;
      NotesErasePage(lib->owner,state,index);
    } else { page.deleted=true; page.open=false; }
  } else return FALSE;
  state.metadataChanged=true; NotesSelectAvailable(lib->owner,state); SaveClientNotesDraft(lib->owner,state,true); NotesLibraryFill(dialog,*lib); return TRUE;
}
static void NotesShowLibrary(HWND dialog,ClientNotesDialogState &state,bool history=false) {
  if (!SaveClientNotesDraft(dialog,state,true) || (history && !HasActiveNote(state))) return;
  NotesLibrary lib; lib.owner=dialog; lib.state=&state; lib.history=history; lib.page=state.activePage;
  DialogBoxParamW(g_hInst,MAKEINTRESOURCEW(IDD_NOTES_LIBRARY),dialog,NotesLibraryProc,reinterpret_cast<LPARAM>(&lib));
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
      a.ticketUrl==b.ticketUrl && a.links==b.links;
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
static bool NotesWorkspaceCommand(HWND dialog,ClientNotesDialogState &state,UINT command) {
  HWND edit=GetDlgItem(dialog,IDC_CLIENT_NOTES_TEXT);
  const bool active=HasActiveNote(state);
  if (command==IDC_NOTES_LIBRARY) { NotesShowLibrary(dialog,state); return true; }
  if (command==IDC_NOTES_MORE) {
    HMENU menu=CreatePopupMenu();
    const auto add=[&](UINT id,const wchar_t *label,bool needsNote=true) {
      AppendMenuW(menu,MF_STRING | (needsNote && !active ? MF_GRAYED : 0),id,label);
    };
    add(IDC_NOTES_FIND_TOGGLE,L"Find in note\tCtrl+F");
    add(IDC_NOTES_REPLACE,L"Find and replace\tCtrl+H");
    add(IDC_NOTES_DUPLICATE,L"Duplicate note"); add(IDC_NOTES_HISTORY,L"Version history");
    add(IDC_NOTES_TICKET,L"Note information / ticket..."); add(IDC_NOTES_OPEN_TICKET,L"Open ticket link");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    add(IDC_NOTES_STRIKE,L"Strikethrough"); add(IDC_NOTES_CODE,L"Code / plain text style"); add(IDC_NOTES_DATE,L"Insert date and time");
    add(IDC_NOTES_LINK_REMOVE,L"Remove selected link"); add(IDC_NOTES_LINK_COPY,L"Copy selected link address");
    add(IDC_NOTES_COPY_ALL,L"Copy entire note as text");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    for (const auto [id,label] : {std::pair{0,L"New blank note"}, {1,L"Ticket investigation"},
        {2,L"Maintenance checklist"}, {3,L"Client contacts"}, {4,L"Handover"}})
      add(IDC_NOTES_TEMPLATE+id,label,false);
    add(IDC_NOTES_IMPORT,L"Import text, RTF or notebook...",false); add(IDC_NOTES_EXPORT,L"Export this note...");
    add(IDC_NOTES_EXPORT_ALL,L"Export client notebook...",false);
    add(IDC_NOTES_TRANSFER,L"Copy to another client..."); add(IDC_NOTES_MOVE,L"Move to another client...");
    AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    add(IDC_NOTES_ZOOM_IN,L"Zoom in\tCtrl++"); add(IDC_NOTES_ZOOM_OUT,L"Zoom out\tCtrl+-"); add(IDC_NOTES_ZOOM_RESET,L"Actual size\tCtrl+0");
    AppendMenuW(menu,MF_STRING | (state.topmost ? MF_CHECKED : 0),IDC_NOTES_TOPMOST,L"Always on top");
    AppendMenuW(menu,MF_STRING | (state.showProgress ? MF_CHECKED : 0),IDC_NOTES_PROGRESS,L"Checklist progress in All notes");
    add(IDC_NOTES_RETRY,L"Retry saving\tCtrl+S",false); add(IDC_NOTES_CONFLICT,L"Resolve save conflict...",false);
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
        state.pages[i].open=true; state.activePage=i; state.metadataChanged=true;
        NotesSelectAvailable(dialog,state); SaveClientNotesDraft(dialog,state,true); return true;
      }
    }
    NotesShowLibrary(dialog,state); return true;
  }
  if (command>=IDC_NOTES_TEMPLATE && command<=IDC_NOTES_TEMPLATE+4) {
    if (!SaveClientNotesDraft(dialog,state,true)) return true;
    const wchar_t *names[]={L"Untitled",L"Ticket investigation",L"Maintenance",L"Contacts",L"Handover"};
    const char *rtfs[]={"{\\rtf1\\ansi }",
      "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs28\\b Issue\\b0\\par\\par\\b Findings\\b0\\par\\par\\b Actions taken\\b0\\par\\par\\b Next steps\\b0\\par}",
      "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs28\\b Maintenance checklist\\b0\\par\\uc1\\u9744?\\u8195?Confirm maintenance window\\par\\u9744?\\u8195?Verify backup\\par\\u9744?\\u8195?Perform changes\\par\\u9744?\\u8195?Verify service and notify client\\par}",
      "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs28\\b Client contacts\\b0\\par Name:\\par Role:\\par Phone:\\par Email:\\par Escalation:\\par}",
      "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Segoe UI;}}\\f0\\fs28\\b Handover\\b0\\par Current status:\\par Completed work:\\par Outstanding work:\\par Next owner:\\par Next update:\\par}"};
    const size_t index=command-IDC_NOTES_TEMPLATE;
    client_notes::NotePage page{names[index],true,rtfs[index],{}}; page.pristine=index==0;
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
        MessageBoxW(dialog,L"This notebook is invalid or would exceed the 256-note limit.",L"Import notebook",MB_OK|MB_ICONWARNING); return true;
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
    if (command>=IDC_NOTES_CLOSE_TAB && command<=IDC_NOTES_PROGRESS) return true;
    return false;
  }
  auto &page=state.pages[state.activePage];
  switch (command) {
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
    if (NotesAsk(dialog,input)) {
      if (input.first.size()>128 || (!input.second.empty() && !ValidNotesUrl(input.second))) { MessageBoxW(dialog,L"Use a ticket number of at most 128 characters and a valid HTTP/HTTPS URL.",L"Ticket details",MB_OK|MB_ICONWARNING); return true; }
      page.ticket=input.first; page.ticketUrl=input.second; state.metadataChanged=true; SaveClientNotesDraft(dialog,state,true);
    }
    return true;
  }
  case IDC_NOTES_OPEN_TICKET:
    if (ValidNotesUrl(page.ticketUrl)) OpenNotesUrl(dialog,state,page.ticketUrl);
    else MessageBoxW(dialog,L"Add a ticket URL in Note information first.",L"Ticket link",MB_OK|MB_ICONINFORMATION);
    return true;
  case IDC_NOTES_TRANSFER: case IDC_NOTES_MOVE: {
    if (!SaveClientNotesDraft(dialog,state,true)) return true;
    NotesInput input{command==IDC_NOTES_MOVE ? L"Move note to client" : L"Copy note to client",L"Exact existing client name",L"",L"",L""};
    if (!NotesAsk(dialog,input)) return true;
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
    CHARFORMAT2W format{sizeof(format)}; format.dwMask=CFM_FACE|CFM_SIZE|CFM_BOLD|CFM_ITALIC;
    wcscpy_s(format.szFaceName,L"Consolas"); format.yHeight=240;
    SendMessageW(edit,EM_SETCHARFORMAT,SCF_SELECTION,reinterpret_cast<LPARAM>(&format));
    MarkNotesChanged(dialog,state); SetFocus(edit); return true;
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
