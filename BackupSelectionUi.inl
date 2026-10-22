// Modal backup scope only. The backend receives an explicit copied selection;
// cancelling or an empty selection never falls back to backing up every client.
struct BackupClientChoice {
  std::wstring name;
  bool checked = true;
  bool archived = false;
};
struct BackupClientSelection {
  std::vector<BackupClientChoice> clients;
  std::vector<size_t> shown;
  bool filling = false;
  bool accepted = false;
  bool failed = false;
};
static bool ReadBackupClientChoices(const fs::path &source,
                                  BackupClientSelection &selection,
                                  std::wstring &error) {
  selection={};
  try {
    const DWORD root=GetFileAttributesW(source.c_str());
    if(root==INVALID_FILE_ATTRIBUTES || !(root&FILE_ATTRIBUTE_DIRECTORY) || (root&FILE_ATTRIBUTE_REPARSE_POINT)) {
      error=L"The client folder could not be listed safely.";return false;
    }
    for(const auto &entry:fs::directory_iterator(source)) {
      const auto name=entry.path().filename().wstring();
      const DWORD attributes=GetFileAttributesW(entry.path().c_str());
      if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) ||
          (attributes&FILE_ATTRIBUTE_REPARSE_POINT) || !CtBackup::Detail::IsValidClientDirectoryName(name)) {
        error=L"This item in the client folder could not be included safely: "+name+
            L". No clients were silently left out. Close temporary/default profiles or resolve the item, then retry.";
        selection={};return false;
      }
      if(selection.clients.size()>=CtArchiveSafety::kMaxItems) {
        error=L"The client list exceeds the backup item limit.";selection={};return false;
      }
      selection.clients.push_back({name,true,IsClientArchived(name)});
    }
    std::sort(selection.clients.begin(),selection.clients.end(),[](const auto &left,const auto &right){
      return CompareStringOrdinal(left.name.c_str(),-1,right.name.c_str(),-1,TRUE)==CSTR_LESS_THAN;
    });
    for(size_t i=1;i<selection.clients.size();++i) {
      if(CompareStringOrdinal(selection.clients[i-1].name.c_str(),-1,selection.clients[i].name.c_str(),-1,TRUE)==CSTR_EQUAL) {
        error=L"Two client folders have the same Windows name. Resolve the duplicate names before backing up.";
        selection={};return false;
      }
    }
    if(selection.clients.empty()) {error=L"No client profiles were found to back up.";return false;}
    return true;
  } catch(const std::exception &) {
    selection={};error=L"The complete client list could not be read. No backup was started; retry when the folder is available.";return false;
  }
}
static std::vector<std::wstring> BackupSelectedNames(const BackupClientSelection &selection) {
  std::vector<std::wstring> names;
  for(const auto &client:selection.clients) if(client.checked) names.push_back(client.name);
  return names;
}
static void BackupSelectionSummary(HWND dialog,const BackupClientSelection &selection) {
  const auto count=std::count_if(selection.clients.begin(),selection.clients.end(),[](const auto &client){return client.checked;});
  const auto text=std::format(L"{} of {} clients selected",count,selection.clients.size());
  SetDlgItemTextW(dialog,IDC_BACKUP_SUMMARY,text.c_str());
  EnableWindow(GetDlgItem(dialog,IDOK),count>0 && !selection.failed);
  const bool filtered=GetWindowTextLengthW(GetDlgItem(dialog,IDC_BACKUP_QUERY))>0;
  SetDlgItemTextW(dialog,IDC_BACKUP_ALL,filtered ? L"Select shown" : L"Select all");
  SetDlgItemTextW(dialog,IDC_BACKUP_NONE,filtered ? L"Clear shown" : L"Clear all");
  EnableWindow(GetDlgItem(dialog,IDC_BACKUP_ALL),!selection.shown.empty() && !selection.failed);
  EnableWindow(GetDlgItem(dialog,IDC_BACKUP_NONE),!selection.shown.empty() && !selection.failed);
  const auto hint=filtered ? std::format(L"{} clients shown. Buttons affect shown clients only; hidden selections are kept.",selection.shown.size()) :
      L"Archived clients are included. Notes and browser data stay together.";
  SetDlgItemTextW(dialog,IDC_BACKUP_HINT,selection.failed ? L"The client list could not be displayed completely. Cancel and retry; no backup has started." : hint.c_str());
}
static void BackupSelectionColumns(HWND dialog) {
  HWND list=GetDlgItem(dialog,IDC_BACKUP_CLIENTS);RECT area{};GetClientRect(list,&area);
  const int status=ScaleByDpi(76,GetDpiForWindow(dialog));
  ListView_SetColumnWidth(list,0,(std::max)(ScaleByDpi(90,GetDpiForWindow(dialog)),static_cast<int>(area.right)-status-GetSystemMetricsForDpi(SM_CXVSCROLL,GetDpiForWindow(dialog))-4));
  ListView_SetColumnWidth(list,1,status);
}
static void FillBackupSelection(HWND dialog,BackupClientSelection &selection) {
  selection.filling=true;selection.failed=false;
  struct FillingGuard {bool &value;~FillingGuard(){value=false;}} filling{selection.filling};
  HWND list=GetDlgItem(dialog,IDC_BACKUP_CLIENTS);
  wchar_t query[241]{};GetDlgItemTextW(dialog,IDC_BACKUP_QUERY,query,static_cast<int>(std::size(query)));
  SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);selection.shown.clear();
  try {selection.shown.reserve(selection.clients.size());}
  catch(const std::exception &) {selection.failed=true;SendMessageW(list,WM_SETREDRAW,TRUE,0);BackupSelectionSummary(dialog,selection);return;}
  for(size_t index=0;index<selection.clients.size();++index) {
    const auto &client=selection.clients[index];
    if(*query && FindStringOrdinal(FIND_FROMSTART,client.name.c_str(),static_cast<int>(client.name.size()),query,-1,TRUE)<0) continue;
    LVITEMW item{};item.mask=LVIF_TEXT|LVIF_PARAM;item.iItem=static_cast<int>(selection.shown.size());
    item.pszText=const_cast<wchar_t *>(client.name.c_str());item.lParam=static_cast<LPARAM>(index);
    const int row=ListView_InsertItem(list,&item);
    if(row<0) {selection.failed=true;selection.shown.clear();ListView_DeleteAllItems(list);break;}
    selection.shown.push_back(index);
    ListView_SetItemText(list,row,1,const_cast<wchar_t *>(client.archived ? L"Archived" : L""));
    ListView_SetCheckState(list,row,client.checked);
  }
  SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);
  BackupSelectionColumns(dialog);BackupSelectionSummary(dialog,selection);
}
static void LayoutBackupSelection(HWND dialog) {
  RECT area{};GetClientRect(dialog,&area);const UINT dpi=GetDpiForWindow(dialog);
  const auto scale=[dpi](int value){return ScaleByDpi(value,dpi);};
  const int margin=scale(14),width=(std::max)(1,static_cast<int>(area.right)-2*margin);
  const int footer=static_cast<int>(area.bottom)-scale(42);
  const auto place=[&](int id,int x,int y,int w,int h){SetWindowPos(GetDlgItem(dialog,id),nullptr,x,y,(std::max)(1,w),(std::max)(1,h),SWP_NOZORDER|SWP_NOACTIVATE);};
  place(-1,margin,margin,width,scale(18));
  place(IDC_BACKUP_QUERY,margin,scale(34),width,scale(26));
  place(IDC_BACKUP_SUMMARY,margin,scale(76),width-scale(200),scale(24));
  place(IDC_BACKUP_ALL,static_cast<int>(area.right)-margin-scale(190),scale(70),scale(92),scale(27));
  place(IDC_BACKUP_NONE,static_cast<int>(area.right)-margin-scale(90),scale(70),scale(90),scale(27));
  place(IDC_BACKUP_CLIENTS,margin,scale(110),width,footer-scale(164));
  place(IDC_BACKUP_HINT,margin,footer-scale(43),width,scale(36));
  place(IDOK,static_cast<int>(area.right)-margin-scale(236),footer,scale(138),scale(28));
  place(IDCANCEL,static_cast<int>(area.right)-margin-scale(90),footer,scale(90),scale(28));
  BackupSelectionColumns(dialog);InvalidateRect(dialog,nullptr,FALSE);
}
static void ThemeBackupSelection(HWND dialog) {
  ApplyCleanupDialogTheme(dialog);HWND list=GetDlgItem(dialog,IDC_BACKUP_CLIENTS);
  ListView_SetBkColor(list,g_themeColors.crControl);ListView_SetTextBkColor(list,g_themeColors.crControl);
  ListView_SetTextColor(list,g_themeColors.crControlText);
}
static INT_PTR CALLBACK BackupClientsDlgProc(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) {
  auto *selection=reinterpret_cast<BackupClientSelection *>(GetWindowLongPtrW(dialog,DWLP_USER));
  if(const auto themed=HandleCleanupDialogTheme(dialog,message,wParam,lParam)) return *themed;
  if(message==WM_INITDIALOG) {
    selection=reinterpret_cast<BackupClientSelection *>(lParam);SetWindowLongPtrW(dialog,DWLP_USER,lParam);
    if(!selection) {EndDialog(dialog,IDCANCEL);return TRUE;}
    SendDlgItemMessageW(dialog,IDC_BACKUP_QUERY,EM_SETLIMITTEXT,240,0);
    SendDlgItemMessageW(dialog,IDC_BACKUP_QUERY,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Find a client"));
    HWND list=GetDlgItem(dialog,IDC_BACKUP_CLIENTS);
    ListView_SetExtendedListViewStyle(list,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_INFOTIP);
    LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=200;column.pszText=const_cast<wchar_t *>(L"Client");ListView_InsertColumn(list,0,&column);
    column.pszText=const_cast<wchar_t *>(L"State");column.cx=80;ListView_InsertColumn(list,1,&column);
    ThemeBackupSelection(dialog);LayoutBackupSelection(dialog);FillBackupSelection(dialog,*selection);return TRUE;
  }
  if(!selection) return FALSE;
  if(message==WM_SIZE && wParam!=SIZE_MINIMIZED) {LayoutBackupSelection(dialog);return TRUE;}
  if(message==WM_GETMINMAXINFO) {
    auto *limits=reinterpret_cast<MINMAXINFO *>(lParam);if(limits){limits->ptMinTrackSize={ScaleByDpi(460,GetDpiForWindow(dialog)),ScaleByDpi(360,GetDpiForWindow(dialog))};}return TRUE;
  }
  if(message==WM_DPICHANGED) {
    const auto *bounds=reinterpret_cast<const RECT *>(lParam);
    if(bounds) SetWindowPos(dialog,nullptr,bounds->left,bounds->top,bounds->right-bounds->left,bounds->bottom-bounds->top,SWP_NOZORDER|SWP_NOACTIVATE);
    LayoutBackupSelection(dialog);return TRUE;
  }
  if(message==WM_THEMECHANGED || message==WM_SYSCOLORCHANGE) {ThemeBackupSelection(dialog);return TRUE;}
  if(message==WM_NOTIFY && lParam) {
    const auto *change=reinterpret_cast<const NMLISTVIEW *>(lParam);
    if(change->hdr.idFrom==IDC_BACKUP_CLIENTS && change->hdr.code==LVN_ITEMCHANGED && !selection->filling &&
        (change->uChanged&LVIF_STATE) && ((change->uOldState^change->uNewState)&LVIS_STATEIMAGEMASK) &&
        change->iItem>=0 && static_cast<size_t>(change->iItem)<selection->shown.size()) {
      selection->clients[selection->shown[static_cast<size_t>(change->iItem)]].checked=((change->uNewState&LVIS_STATEIMAGEMASK)>>12)==2;
      BackupSelectionSummary(dialog,*selection);return TRUE;
    }
  }
  if(message==WM_COMMAND) {
    const int id=LOWORD(wParam);
    if(id==IDC_BACKUP_QUERY && HIWORD(wParam)==EN_CHANGE) {FillBackupSelection(dialog,*selection);return TRUE;}
    if(id==IDC_BACKUP_ALL || id==IDC_BACKUP_NONE) {
      for(const auto index:selection->shown) selection->clients[index].checked=id==IDC_BACKUP_ALL;
      FillBackupSelection(dialog,*selection);return TRUE;
    }
    if(id==IDOK) {
      if(selection->failed || BackupSelectedNames(*selection).empty()) return TRUE;
      selection->accepted=true;EndDialog(dialog,IDOK);return TRUE;
    }
    if(id==IDCANCEL) {selection->accepted=false;EndDialog(dialog,IDCANCEL);return TRUE;}
  }
  if(message==WM_CLOSE) {selection->accepted=false;EndDialog(dialog,IDCANCEL);return TRUE;}
  return FALSE;
}
static std::optional<std::vector<std::wstring>> ChooseBackupClients(const fs::path &sourceDir) {
  BackupClientSelection selection;std::wstring error;
  if(!ReadBackupClientChoices(sourceDir,selection,error)) {MessageBoxW(g_hGui,error.c_str(),L"Choose backup clients",MB_OK|MB_ICONWARNING);return std::nullopt;}
  const auto result=DialogBoxParamW(g_hInst,MAKEINTRESOURCEW(IDD_BACKUP_CLIENTS),g_hGui,BackupClientsDlgProc,reinterpret_cast<LPARAM>(&selection));
  if(result==-1) MessageBoxW(g_hGui,L"The client picker could not be opened. No backup was created.",L"Choose backup clients",MB_OK|MB_ICONWARNING);
  if(result!=IDOK || !selection.accepted || selection.failed) return std::nullopt;
  auto names=BackupSelectedNames(selection);if(names.empty()) return std::nullopt;return names;
}
#ifdef CTSPACES_BACKUP_SELECTION_TEST_HOOKS
extern ProfileUseState CtSpacesBackupSelectionTestProbe(const std::wstring &,std::wstring &);
#endif
static bool ProbeBackupSelectionClosed(const std::vector<std::wstring> &selected,std::wstring &errorDetails) {
  if(selected.empty()) {errorDetails=L"No clients were selected.";return false;}
  for(const auto &client:selected) {
    std::wstring detail;
#ifdef CTSPACES_BACKUP_SELECTION_TEST_HOOKS
    const auto state=CtSpacesBackupSelectionTestProbe(client,detail);
#else
    const auto state=ProbeClientProfilesInUse(client,detail);
#endif
    if(state!=ProfileUseState::NotInUse) {errorDetails=client+L": "+(detail.empty() ? L"The profile could not be verified as closed." : detail);return false;}
  }
  errorDetails.clear();return true;
}
