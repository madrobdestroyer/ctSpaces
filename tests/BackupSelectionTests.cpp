#include "../ProgressUI.h"
static void BackupSelectionPostUpdate(HWND,const std::wstring &,int);
static void BackupSelectionPostUpdate(HWND,const std::wstring &,int,const std::wstring &);
#define ProgressUI_PostUpdate BackupSelectionPostUpdate
#define CTSPACES_BACKUP_SELECTION_TEST_HOOKS
#define wmain NotesWorkspaceUnusedMain
#include "NotesWorkspaceTests.cpp"
#undef wmain
#undef ProgressUI_PostUpdate

static constexpr UINT kBackupModalProbe=WM_APP+0x370;
static std::atomic_bool backupModalProbeArmed=false,backupModalProbeDelivered=false;
static HWND backupModalProbeParent=nullptr;
static void BackupSelectionPostUpdate(HWND parent,const std::wstring &text,int percent,const std::wstring &details) {
  ::ProgressUI_PostUpdate(parent,text,percent,details);
  // Hold only the first real worker progress boundary until the UI message
  // pump has observed its pre-existing Library popup. No archive/file
  // operation is substituted, and all windows remain guarded and hidden.
  if(backupModalProbeArmed.exchange(false)) {
    DWORD_PTR result=0;
    backupModalProbeDelivered.store(SendMessageTimeoutW(backupModalProbeParent,kBackupModalProbe,0,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,&result)!=0 && result==1);
  }
}
static void BackupSelectionPostUpdate(HWND parent,const std::wstring &text,int percent) {
  BackupSelectionPostUpdate(parent,text,percent,L"");
}

static std::vector<std::wstring> backupProbed;
static std::wstring backupActive;
static unsigned backupProbeActiveOnCall=0;
ProfileUseState CtSpacesBackupSelectionTestProbe(const std::wstring &name,std::wstring &error) {
  backupProbed.push_back(name);
  if(name==backupActive && (!backupProbeActiveOnCall || backupProbed.size()>=backupProbeActiveOnCall)) {
    error=L"Fixture selected browser is still running.";return ProfileUseState::InUse;
  }
  return ProfileUseState::NotInUse;
}
namespace backup_selection_test {
static unsigned scenarios=0;
static LRESULT CALLBACK RefuseActivation(int code,WPARAM w,LPARAM l) {
  if(code==HCBT_ACTIVATE || code==HCBT_SETFOCUS) return 1;
  return CallNextHookEx(nullptr,code,w,l);
}
static HWND Open(BackupClientSelection &selection) {
  HWND dialog=notes_test::Modeless(g_hInst,MAKEINTRESOURCEW(IDD_BACKUP_CLIENTS),g_hGui,BackupClientsDlgProc,reinterpret_cast<LPARAM>(&selection));
  Check(dialog && IsWindow(dialog),"Actual backup selector creates only a hidden native dialog");return dialog;
}
static void Inventory() {
  ++scenarios;Fixture(L"Alpha");Fixture(L"R&D \x6771\x4eac");Fixture(L"Archived client");
  fs::create_directories(g_sDataDir/L"Sites/Legacy client");g_archivedClients={L"Archived client"};
  BackupClientSelection selection;std::wstring error;
  Check(ReadBackupClientChoices(g_sDataDir/L"Sites",selection,error) && selection.clients.size()==4,"Default inventory includes every ordinary, legacy and archived client");
  Check(BackupSelectedNames(selection).size()==4 && std::all_of(selection.clients.begin(),selection.clients.end(),[](const auto &item){return item.checked;}),"All clients are checked by default without a remembered opt-out");
  Check(std::any_of(selection.clients.begin(),selection.clients.end(),[](const auto &item){return item.name==L"Archived client" && item.archived;}),"Archived clients have an explanatory state without being omitted");
  Put(g_sDataDir/L"Sites/unexpected.txt","must not be silently skipped");
  Check(!ReadBackupClientChoices(g_sDataDir/L"Sites",selection,error) && selection.clients.empty() && !error.empty(),"Unexpected direct data refuses the complete inventory instead of silently omitting it");
  fs::remove(g_sDataDir/L"Sites/unexpected.txt");
  Check(!ReadBackupClientChoices(g_sDataDir/L"missing",selection,error) && selection.clients.empty(),"Unavailable inventory refuses without falling back to all clients");
}
static void Checklist() {
  ++scenarios;BackupClientSelection selection{{{L"Alpha",true,false},{L"R&D \x6771\x4eac",true,false},{L"Archived client",true,true}}};
  HWND dialog=Open(selection),list=GetDlgItem(dialog,IDC_BACKUP_CLIENTS);wchar_t type[64]{};GetClassNameW(list,type,64);
  Check(!_wcsicmp(type,L"SysListView32") && (ListView_GetExtendedListViewStyle(list)&LVS_EX_CHECKBOXES) && (GetWindowLongPtrW(list,GWL_STYLE)&WS_TABSTOP),"Selection uses accessible native checkboxes and a keyboard tab stop");
  wchar_t name[256]{};ListView_GetItemText(list,1,0,name,256);
  Check(!wcscmp(name,L"R&D \x6771\x4eac") && ListView_GetCheckState(list,1),"Native item text preserves literal ampersands and Unicode");
  ListView_GetItemText(list,2,1,name,256);Check(!wcscmp(name,L"Archived"),"Archived state appears in the native item without altering its client name");
  ListView_SetCheckState(list,0,FALSE);
  Check(!selection.clients[0].checked && BackupSelectedNames(selection).size()==2,"Native checkbox notification updates the captured selection");
  SetDlgItemTextW(dialog,IDC_BACKUP_QUERY,L"R&D");
  Check(selection.shown.size()==1 && selection.shown.front()==1 && !selection.clients[0].checked,"Filtering retains hidden unchecked client state");
  Check(NotesWindowText(GetDlgItem(dialog,IDC_BACKUP_NONE))==L"Clear shown" && NotesWindowText(GetDlgItem(dialog,IDC_BACKUP_HINT)).find(L"hidden selections are kept")!=std::wstring::npos,"Filtered bulk controls explicitly describe their limited scope");
  Command(dialog,IDC_BACKUP_NONE);
  Check(!selection.clients[0].checked && !selection.clients[1].checked && selection.clients[2].checked,"Clear shown never changes a hidden client's selection");
  Command(dialog,IDC_BACKUP_ALL);SetDlgItemTextW(dialog,IDC_BACKUP_QUERY,L"");
  Check(!ListView_GetCheckState(list,0) && ListView_GetCheckState(list,1) && ListView_GetCheckState(list,2),"Returning from search restores the same three native check states");
  ListView_SetItemState(list,1,LVIS_FOCUSED|LVIS_SELECTED,LVIS_FOCUSED|LVIS_SELECTED);
  SendMessageW(list,WM_KEYDOWN,VK_SPACE,0);SendMessageW(list,WM_KEYUP,VK_SPACE,0);
  Check(!selection.clients[1].checked,"The native list's Space-key action toggles its focused checkbox without device input");
  Command(dialog,IDC_BACKUP_NONE);Command(dialog,IDOK);
  Check(BackupSelectedNames(selection).empty() && !IsWindowEnabled(GetDlgItem(dialog,IDOK)) && !selection.accepted,"Zero selected disables Continue and direct OK also refuses");
  SetDlgItemTextW(dialog,IDC_BACKUP_QUERY,L"not present");
  Check(selection.shown.empty() && !IsWindowEnabled(GetDlgItem(dialog,IDC_BACKUP_ALL)) && !IsWindowEnabled(GetDlgItem(dialog,IDC_BACKUP_NONE)),"Empty search results disable only shown-row bulk actions");
  SetDlgItemTextW(dialog,IDC_BACKUP_QUERY,L"Archived");Command(dialog,IDC_BACKUP_ALL);Command(dialog,IDOK);
  Check(selection.accepted && BackupSelectedNames(selection)==std::vector<std::wstring>{L"Archived client"},"Continue captures exactly the checked archived client");
  selection.accepted=false;Command(dialog,IDCANCEL);Check(!selection.accepted,"Cancel does not accept a previous selection");
  DestroyWindow(dialog);NotesBackgroundAudit("native checkbox filtering and keyboard scope");
}
static bool InsideAndSeparate(HWND dialog) {
  RECT area{};GetClientRect(dialog,&area);std::vector<RECT> rectangles;
  for(int id:{IDC_BACKUP_QUERY,IDC_BACKUP_SUMMARY,IDC_BACKUP_ALL,IDC_BACKUP_NONE,IDC_BACKUP_CLIENTS,IDC_BACKUP_HINT,IDOK,IDCANCEL}) {
    RECT bounds{};if(!GetWindowRect(GetDlgItem(dialog,id),&bounds)) return false;MapWindowPoints(nullptr,dialog,reinterpret_cast<POINT *>(&bounds),2);
    if(bounds.left<0 || bounds.top<0 || bounds.right>area.right || bounds.bottom>area.bottom || bounds.right<=bounds.left || bounds.bottom<=bounds.top) return false;
    for(const auto &other:rectangles) {RECT overlap{};if(IntersectRect(&overlap,&bounds,&other)) return false;}
    rectangles.push_back(bounds);
  }
  return true;
}
static void Render(HWND dialog,const fs::path &output) {
  RECT area{};GetClientRect(dialog,&area);const int width=area.right,height=area.bottom;
  BITMAPINFO info{};info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB};void *pixels=nullptr;
  HDC dc=CreateCompatibleDC(nullptr);HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
  Check(dc && bitmap && pixels,"Hidden selector render owns a memory bitmap");const auto previous=SelectObject(dc,bitmap);
  // DefDlgProc offsets child WM_PRINT output by its nonclient frame even on a
  // client-only memory target. Paint the parent first, then explicitly translate
  // each immediate native child from its measured window bounds.
  SendMessageW(dialog,WM_PRINT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT|PRF_ERASEBKGND);
  struct RenderChildren {HWND parent;HDC dc;} children{dialog,dc};
  EnumChildWindows(dialog,[](HWND child,LPARAM data)->BOOL {
    const auto &render=*reinterpret_cast<const RenderChildren *>(data);
    if(GetParent(child)!=render.parent || !(GetWindowLongPtrW(child,GWL_STYLE)&WS_VISIBLE)) return TRUE;
    RECT bounds{};GetWindowRect(child,&bounds);MapWindowPoints(nullptr,render.parent,reinterpret_cast<POINT *>(&bounds),2);
    const int saved=SaveDC(render.dc);SetViewportOrgEx(render.dc,bounds.left,bounds.top,nullptr);
    IntersectClipRect(render.dc,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top);
    SendMessageW(child,WM_PRINT,reinterpret_cast<WPARAM>(render.dc),PRF_CLIENT|PRF_ERASEBKGND|PRF_CHILDREN);
    RestoreDC(render.dc,saved);return TRUE;
  },reinterpret_cast<LPARAM>(&children));
  SelectObject(dc,previous);
  {Gdiplus::Bitmap image(bitmap,nullptr);const auto encoder=GetEncoderClsid(L"image/png");
   Check(image.Save(output.c_str(),&encoder,nullptr)==Gdiplus::Ok,"Hidden native selector rendering saves without screen capture");}
  DeleteObject(bitmap);DeleteDC(dc);
}
static void ManyAndBounds(const fs::path &artifacts) {
  ++scenarios;BackupClientSelection selection;
  for(unsigned i=0;i<256;++i) selection.clients.push_back({L"Client "+std::format(L"{:03}",i),true,i%11==0});
  HWND dialog=Open(selection),list=GetDlgItem(dialog,IDC_BACKUP_CLIENTS);
  Check(ListView_GetItemCount(list)==256,"Many-client inventory is scrollable without a small client cap");
  ListView_EnsureVisible(list,255,FALSE);Check(ListView_GetTopIndex(list)>0,"Native list scrolls to the last available client");
  ListView_SetCheckState(list,255,FALSE);SetDlgItemTextW(dialog,IDC_BACKUP_QUERY,L"Client 25");
  Check(selection.shown.size()==6 && !selection.clients[255].checked && BackupSelectedNames(selection).size()==255,"Searching many clients keeps an unchecked offscreen client excluded");
  for(const auto *theme:{L"Dark - Gothic",L"Marine"}) {
    const auto preset=std::find_if(std::begin(g_ctThemeCatalog),std::end(g_ctThemeCatalog),[&](const auto &entry){return !wcscmp(entry.name,theme);});
    Check(preset!=std::end(g_ctThemeCatalog),"Requested screenshot theme exists");
    g_themeColors=BuildThemeColorsFromPalette(preset->p);g_bThemeIsDark=preset->p.dark;UpdateThemeBrushes();ThemeBackupSelection(dialog);
    for(const auto size:{SIZE{460,360},SIZE{760,540}}) {
      SetWindowPos(dialog,nullptr,0,0,ScaleByDpi(size.cx,GetDpiForWindow(dialog)),ScaleByDpi(size.cy,GetDpiForWindow(dialog)),SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
      LayoutBackupSelection(dialog);Check(InsideAndSeparate(dialog),"Selector controls fit and remain separate at minimum and larger bounds");
      Render(dialog,artifacts/(std::wstring(!wcscmp(theme,L"Marine") ? L"marine" : L"gothic")+L"-"+std::to_wstring(size.cx)+L".png"));
    }
  }
  SetDlgItemTextW(dialog,IDC_BACKUP_QUERY,L"");Check(!ListView_GetCheckState(list,255),"Theme and resize retain the exact unchecked native row");
  DestroyWindow(dialog);NotesBackgroundAudit("large filtered selection, native rendering and bounds");
}
static void Flow() {
  ++scenarios;std::wstring error;backupProbed.clear();backupActive=L"Excluded";backupProbeActiveOnCall=0;
  Check(ProbeBackupSelectionClosed({L"Alpha"},error) && backupProbed==std::vector<std::wstring>{L"Alpha"},"Excluded running clients are never probed by a selected-only backup");
  backupActive=L"Alpha";backupProbed.clear();Check(!ProbeBackupSelectionClosed({L"Alpha"},error) && error.find(L"Alpha")!=std::wstring::npos,"A selected running browser blocks backup with its client name");
  Check(!ProbeBackupSelectionClosed({},error),"An explicit empty selection cannot become all clients");
  unsigned mode=0,modalCalls=0;
  notesDialogObserver=[&](LPCWSTR resource,DLGPROC procedure,LPARAM data)->INT_PTR {
    Check(resource==MAKEINTRESOURCEW(IDD_BACKUP_CLIENTS) && procedure==BackupClientsDlgProc,"Backup selection occurs before the destination picker");++modalCalls;
    auto &selection=*reinterpret_cast<BackupClientSelection *>(data);HWND dialog=Open(selection);
    if(mode==0) Command(dialog,IDCANCEL);
    else {
      Command(dialog,IDC_BACKUP_NONE);
      if(mode!=1) {SetDlgItemTextW(dialog,IDC_BACKUP_QUERY,L"Alpha");Command(dialog,IDC_BACKUP_ALL);}
      Command(dialog,IDOK);
    }
    const bool accepted=selection.accepted;DestroyWindow(dialog);return accepted ? IDOK : IDCANCEL;
  };
  backupProbed.clear();files.clear();GuiProfExportAll();
  Check(modalCalls==1 && backupProbed.empty() && files.empty() && !g_bClientNotesBusy.load(),"Selection cancellation starts neither profile probes nor a destination/backup");
  mode=1;GuiProfExportAll();Check(modalCalls==2 && backupProbed.empty(),"Direct zero-selection confirmation cannot fall through into a backup");
  mode=2;backupProbed.clear();backupActive=L"Alpha";GuiProfExportAll();
  Check(backupProbed==std::vector<std::wstring>{L"Alpha"} && files.empty() && !g_bClientNotesBusy.load(),"Selected active-client rejection ends before destination selection");
  notesFilePickerObserver=[](OPENFILENAMEW *)->BOOL{return FALSE;};
  backupActive=L"Excluded";backupProbed.clear();GuiProfExportAll();
  Check(backupProbed==std::vector<std::wstring>{L"Alpha"} && !g_bClientNotesBusy.load(),"Canceling the destination keeps the explicit subset and starts no archive");
  notesFilePickerObserver={};
  backupActive=L"Alpha";backupProbeActiveOnCall=2;backupProbed.clear();const auto destination=g_sDataDir/L"must-not-exist.7z";files.push_back(destination.wstring());GuiProfExportAll();
  Check(backupProbed==std::vector<std::wstring>{L"Alpha",L"Alpha"} && files.empty() && !fs::exists(destination),"Destination return rechecks only captured selected clients and refuses a newly active browser before archive work");
  notesDialogObserver={};backupActive.clear();backupProbeActiveOnCall=0;
  NotesBackgroundAudit("backup scope cancellation and selected-only profile gates");
}
static void ExcludedOpenNotes(const fs::path &artifacts) {
  ++scenarios;
  const std::wstring excluded=L"Excluded notes";
  Fixture(excluded);
  HWND selectorSurface=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden client selector",WS_CHILD,0,0,100,24,g_hGui,nullptr,g_hInst,nullptr);
  Check(selectorSurface!=nullptr,"Subset workflow owns its required hidden launcher selector surface");
  const auto previousSurface=g_hClientEditSurface;g_hClientEditSurface=selectorSurface;
  HWND notes=::Open(excluded);auto &state=State(notes);
  if(!HasActiveNote(state)) Command(notes,IDC_NOTES_ADD_TAB);
  Type(notes,L"Excluded client's saved baseline\r\n");Save(notes);
  const auto id=state.pages[state.activePage].id;
  unsigned choices=0,pickers=0;
  notesDialogObserver=[&](LPCWSTR resource,DLGPROC procedure,LPARAM data)->INT_PTR {
    Check(resource==MAKEINTRESOURCEW(IDD_BACKUP_CLIENTS) && procedure==BackupClientsDlgProc,"Subset workflow reaches the actual client selector");
    ++choices;auto &selection=*reinterpret_cast<BackupClientSelection *>(data);
    for(auto &client:selection.clients) client.checked=client.name==L"Alpha";
    selection.accepted=true;return IDOK;
  };
  notesFilePickerObserver=[&](OPENFILENAMEW *)->BOOL {++pickers;return FALSE;};
  backupActive.clear();backupProbeActiveOnCall=0;backupProbed.clear();files.clear();
  state.detailsDrafts[id]={L"0",L"",L"",L"not a valid URL"};
  GuiProfExportAll();
  const bool invalidDraftExcluded=choices==1 && pickers==1 && backupProbed==std::vector<std::wstring>{L"Alpha"};
  std::cout<<"Excluded open Notes invalid Details: selector="<<choices<<"; destination="<<pickers<<"; expected=1/1; verdict="<<(invalidDraftExcluded?"PASS":"FAIL")<<std::endl;
  Check(state.detailsDrafts.contains(id) && state.detailsDrafts.at(id)[3]==L"not a valid URL" &&
        g_hNotesWindow==notes && IsWindowEnabled(notes) && !g_bClientNotesBusy.load() && g_bUiEnabled,
        "An excluded invalid Details draft remains intact and editable after backup cancellation");
  const auto excludedBytes=client_notes::ReadNotebook(g_sDataDir/L"Sites"/excluded).notebook_snapshot.bytes;
  const fs::path subsetArchive=artifacts/L"excluded-invalid-details.7z";
  notesFilePickerObserver={};choices=0;backupProbed.clear();files.push_back(subsetArchive.wstring());
  _7zSetHInstance(g_hInst);GuiProfExportAll();
  Check(fs::is_regular_file(subsetArchive) && files.empty() && choices==1 && backupProbed==std::vector<std::wstring>{L"Alpha",L"Alpha"},
        "An explicit Alpha-only production backup succeeds with an unrelated invalid Details draft open");
  CtBackup::StagedRestore subsetStaged;
  const auto subsetResult=CtBackup::StageValidated7zRestore(subsetArchive,artifacts/L"excluded-invalid-staged",nullptr,nullptr,subsetStaged);
  Check(subsetResult.ok() && subsetStaged.profileCount==1 && fs::is_directory(subsetStaged.payloadDir/L"Alpha") && !fs::exists(subsetStaged.payloadDir/excluded),
        "Actual SDK verification and staging contain exactly the selected client and omit excluded Notes data");
  Check(state.detailsDrafts.contains(id) && state.detailsDrafts.at(id)[3]==L"not a valid URL" &&
        client_notes::ReadNotebook(g_sDataDir/L"Sites"/excluded).notebook_snapshot.bytes==excludedBytes &&
        g_hNotesWindow==notes && IsWindowEnabled(notes) && !g_bClientNotesBusy.load(),
        "Successful subset backup preserves the excluded client's saved bytes and editable invalid draft");
  notesFilePickerObserver=[&](OPENFILENAMEW *)->BOOL {++pickers;return FALSE;};
  state.detailsDrafts.clear();SetNotesStatus(notes,state);
  auto external=state.original.pages;
  auto changed=std::find_if(external.begin(),external.end(),[&](const auto &page){return page.id==id;});
  Check(changed!=external.end(),"Excluded conflict fixture locates the saved note identity");
  changed->ticket=L"External excluded writer";
  Check(client_notes::WriteNotebook(g_sDataDir/L"Sites"/excluded,external,state.original),"Excluded conflict fixture creates a real concurrent notebook revision");
  Select(Edit(notes),-1,-1);Type(notes,L"Excluded local conflict draft\r\n");
  choices=pickers=0;backupProbed.clear();GuiProfExportAll();
  const bool conflictExcluded=choices==1 && pickers==1 && backupProbed==std::vector<std::wstring>{L"Alpha"};
  std::cout<<"Excluded open Notes concurrent revision: selector="<<choices<<"; destination="<<pickers<<"; expected=1/1; verdict="<<(conflictExcluded?"PASS":"FAIL")<<std::endl;
  const auto disk=client_notes::ReadNotebook(g_sDataDir/L"Sites"/excluded);
  Check(NotesWindowText(Edit(notes)).find(L"Excluded local conflict draft")!=std::wstring::npos &&
        std::any_of(disk.pages.begin(),disk.pages.end(),[&](const auto &page){return page.id==id && page.ticket==L"External excluded writer";}) &&
        IsWindowEnabled(notes) && !g_bClientNotesBusy.load(),
        "Subset selection preserves both the excluded concurrent disk revision and full editable local draft");
  notesDialogObserver={};notesFilePickerObserver={};DestroyWindow(notes);
  g_hClientEditSurface=previousSurface;DestroyWindow(selectorSurface);
  NotesBackgroundAudit("excluded invalid Details and concurrent Notes revision");
  Check(invalidDraftExcluded,"Invalid Details in an excluded client cannot prevent choosing and backing up another client");
  Check(conflictExcluded,"A concurrent revision in an excluded client cannot prevent choosing and backing up another client");
}
struct ModalSnapshotProbe {
  HWND notes=nullptr,library=nullptr;
  bool observed=false,workerBusy=false,notesDisabled=false,libraryEnabled=false,archiveIssued=false;
};
static LRESULT CALLBACK ObserveModalSnapshot(HWND window,UINT message,WPARAM w,LPARAM l,UINT_PTR id,DWORD_PTR data) {
  if(message==kBackupModalProbe) {
    auto &probe=*reinterpret_cast<ModalSnapshotProbe *>(data);
    probe.observed=true;probe.workerBusy=g_bArchiveTaskInProgress && g_bClientNotesBusy.load();
    probe.notesDisabled=!IsWindowEnabled(probe.notes);probe.libraryEnabled=IsWindowEnabled(probe.library)!=FALSE;
    if(probe.libraryEnabled) {
      probe.archiveIssued=true;Command(probe.library,IDC_NOTES_ARCHIVE);
    }
    return 1;
  }
  if(message==WM_NCDESTROY) RemoveWindowSubclass(window,ObserveModalSnapshot,id);
  return DefSubclassProc(window,message,w,l);
}
static void ModalSnapshot(const fs::path &artifacts) {
  ++scenarios;const std::wstring name=L"Modal snapshot";Fixture(name);
  HWND selectorSurface=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden client selector",WS_CHILD,0,0,100,24,g_hGui,nullptr,g_hInst,nullptr);
  Check(selectorSurface!=nullptr,"Modal snapshot owns its hidden launcher selector surface");
  const auto previousSurface=g_hClientEditSurface;g_hClientEditSurface=selectorSurface;
  HWND notes=::Open(name);auto &state=State(notes);
  // Match the production Notes lifetime: an unowned top-level Notes window.
  SetWindowLongPtrW(notes,GWLP_HWNDPARENT,0);
  if(!HasActiveNote(state)) Command(notes,IDC_NOTES_ADD_TAB);
  Type(notes,L"Stable snapshot text\r\n");Save(notes);
  const auto noteId=state.pages[state.activePage].id;
  ModalSnapshotProbe probe;probe.notes=notes;
  Check(SetWindowSubclass(g_hGui,ObserveModalSnapshot,894,reinterpret_cast<DWORD_PTR>(&probe))!=FALSE,
        "Worker progress observes the actual hidden launcher message pump");
  const fs::path archive=artifacts/L"modal-snapshot.7z";
  bool libraryRestored=false,modalOwnerPreserved=false;
  notesDialogObserver=[&](LPCWSTR resource,DLGPROC procedure,LPARAM data)->INT_PTR {
    if(resource==MAKEINTRESOURCEW(IDD_BACKUP_CLIENTS) && procedure==BackupClientsDlgProc) {
      auto &selection=*reinterpret_cast<BackupClientSelection *>(data);
      for(auto &client:selection.clients) client.checked=client.name==name;
      selection.accepted=true;return IDOK;
    }
    Check(resource==MAKEINTRESOURCEW(IDD_NOTES_LIBRARY) && procedure==NotesLibraryProc,
          "Actual All notes action reaches the native Library dialog procedure");
    auto &library=*reinterpret_cast<NotesLibrary *>(data);
    probe.library=notes_test::Modeless(g_hInst,resource,notes,procedure,data);
    Check(probe.library && library.owner==notes && GetWindow(probe.library,GW_OWNER)==notes && GetWindow(notes,GW_OWNER)==nullptr,
          "The native Library popup belongs to the unowned Notes window");
    // The guarded modal fixture explicitly reproduces DialogBox's owner
    // disabling; the actual NotesShowLibrary stack remains live throughout.
    EnableWindow(notes,FALSE);
    Check(!IsWindowEnabled(notes) && IsWindowEnabled(probe.library) && IsWindowEnabled(g_hGui),
          "A Notes-owned Library leaves the separate launcher usable before backup");
    backupProbed.clear();backupActive.clear();backupProbeActiveOnCall=0;
    backupModalProbeParent=g_hGui;backupModalProbeDelivered.store(false);backupModalProbeArmed.store(true);
    files.push_back(archive.wstring());GuiProfExportAll();
    backupModalProbeArmed.store(false);backupModalProbeParent=nullptr;
    libraryRestored=IsWindowEnabled(probe.library)!=FALSE;modalOwnerPreserved=!IsWindowEnabled(notes);
    DestroyWindow(probe.library);probe.library=nullptr;EnableWindow(notes,TRUE);return IDCANCEL;
  };
  _7zSetHInstance(g_hInst);Command(notes,IDC_NOTES_LIBRARY);notesDialogObserver={};
  RemoveWindowSubclass(g_hGui,ObserveModalSnapshot,894);
  Check(probe.observed && backupModalProbeDelivered.load() && probe.workerBusy && probe.notesDisabled,
        "The real archive worker pumps the native popup observation after Notes is frozen");
  std::cout<<"Notes-owned modal during snapshot: enabled="<<probe.libraryEnabled<<"; archive command issued="<<probe.archiveIssued<<"; expected=0/0; verdict="<<(!probe.libraryEnabled && !probe.archiveIssued?"PASS":"FAIL")<<std::endl;
  Check(fs::is_regular_file(archive) && files.empty() && backupProbed==std::vector<std::wstring>{name,name},
        "The selected tiny client completes its real SDK backup and both browser gates");
  CtBackup::StagedRestore staged;
  const auto stagedResult=CtBackup::StageValidated7zRestore(archive,artifacts/L"modal-staged",nullptr,nullptr,staged);
  Check(stagedResult.ok(),"The real modal snapshot backup passes CRC and stages safely");
  const auto archived=client_notes::ReadNotebook(staged.payloadDir/name);
  const auto disk=client_notes::ReadNotebook(g_sDataDir/L"Sites"/name);
  const auto unchanged=[&](const auto &notebook){return std::any_of(notebook.pages.begin(),notebook.pages.end(),[&](const auto &page){return page.id==noteId && !page.archived && NotesPlain(page).find(L"Stable snapshot text")!=std::wstring::npos;});};
  const bool stable=unchanged(archived) && unchanged(disk) && std::any_of(state.pages.begin(),state.pages.end(),[&](const auto &page){return page.id==noteId && !page.archived;});
  std::cout<<"Notes-owned modal snapshot unchanged: "<<(stable?"PASS":"FAIL")<<std::endl;
  Check(libraryRestored && modalOwnerPreserved && IsWindowEnabled(notes) && !g_bClientNotesBusy.load() && g_bUiEnabled,
        "Backup restores its popup but preserves the original modal owner disabled state");
  DestroyWindow(notes);g_hClientEditSurface=previousSurface;DestroyWindow(selectorSurface);
  NotesBackgroundAudit("Notes-owned native popup during real backup worker");
  Check(!probe.libraryEnabled && !probe.archiveIssued,"A Notes-owned modal popup must be frozen during the backup snapshot");
  Check(stable,"Frozen popup actions cannot change selected Notes disk, archive or local state");
}
}
int wmain(int argc,wchar_t **argv) {
  const bool modal=argc==4 && !wcscmp(argv[3],L"--modal-snapshot");
  if((argc!=3 && !modal) || wcscmp(argv[1],L"--headless-background")) {std::cerr<<"Hidden entry and artifact path required; native windows created=0\n";return 2;}
  try {
    backgroundNotesTests=true;NotesBackgroundEvents events;events.Start();
    const auto hook=SetWindowsHookExW(WH_CBT,backup_selection_test::RefuseActivation,nullptr,GetCurrentThreadId());Check(hook!=nullptr,"Native activation/focus is vetoed before it can disturb the desktop");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_WIN95_CLASSES};InitCommonControlsEx(&controls);
    Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);g_hInst=GetModuleHandleW(nullptr);
    g_bThemeIsDark=true;BuildThemeColors(true,g_themeColors);UpdateThemeBrushes();
    g_sDataDir=fs::current_path()/L"build"/(L"backup-selection-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));fs::create_directories(g_sDataDir);g_sConfigPath=g_sDataDir/L"ctSpaces.ini";
    const fs::path artifacts=argv[2];fs::create_directories(artifacts);
    g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden backup parent",WS_OVERLAPPED,0,0,100,100,nullptr,nullptr,g_hInst,nullptr);
    if(modal) backup_selection_test::ModalSnapshot(artifacts);
    else {backup_selection_test::Inventory();backup_selection_test::Checklist();backup_selection_test::ManyAndBounds(artifacts);backup_selection_test::Flow();backup_selection_test::ExcludedOpenNotes(artifacts);}
    DestroyWindow(g_hGui);g_hGui=nullptr;NotesBackgroundAudit("final backup selection cleanup");NotesBackgroundBanner();UnhookWindowsHookEx(hook);
    std::cout<<"Backup selection: "<<checks<<" checks; "<<backup_selection_test::scenarios<<" private groups; 0 failures.\n";return 0;
  }catch(const std::exception &error){std::cerr<<"FAIL: "<<error.what()<<std::endl;NotesBackgroundBanner();return 1;}
}
