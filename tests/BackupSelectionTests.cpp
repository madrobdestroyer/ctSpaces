#include "../ProgressUI.h"
#include <tlhelp32.h>

static HANDLE WINAPI BackupSelectionProcessSnapshot(DWORD flags,DWORD pid) {
  if (flags!=TH32CS_SNAPPROCESS || pid) {SetLastError(ERROR_INVALID_PARAMETER);return INVALID_HANDLE_VALUE;}
  return CreateEventW(nullptr,FALSE,FALSE,nullptr);
}
static BOOL WINAPI BackupSelectionFirstProcess(HANDLE,LPPROCESSENTRY32W entry) {
  wcscpy_s(entry->szExeFile,L"private-backup-selection-fixture.exe");
  entry->th32ProcessID=GetCurrentProcessId();
  return TRUE;
}
static BOOL WINAPI BackupSelectionNextProcess(HANDLE,LPPROCESSENTRY32W) {
  SetLastError(ERROR_NO_MORE_FILES);
  return FALSE;
}

static void BackupSelectionPostUpdate(HWND,const std::wstring &,int);
static void BackupSelectionPostUpdate(HWND,const std::wstring &,int,const std::wstring &);
#define CreateToolhelp32Snapshot BackupSelectionProcessSnapshot
#define Process32FirstW BackupSelectionFirstProcess
#define Process32NextW BackupSelectionNextProcess
#define ProgressUI_PostUpdate BackupSelectionPostUpdate
#define CTSPACES_BACKUP_SELECTION_TEST_HOOKS
#define wmain NotesWorkspaceUnusedMain
#include "NotesWorkspaceTests.cpp"
#undef wmain
#undef ProgressUI_PostUpdate
#undef Process32NextW
#undef Process32FirstW
#undef CreateToolhelp32Snapshot

HRESULT CtBackupTestCreateGuid(GUID* identity) {return CoCreateGuid(identity);}

struct ManifestCleanupProbe {
  DWORD workerThread=0;
  bool verified=false;
  fs::path manifest;
  HANDLE reader=INVALID_HANDLE_VALUE;
  ~ManifestCleanupProbe() {if (reader!=INVALID_HANDLE_VALUE) CloseHandle(reader);}
};
static std::atomic<ManifestCleanupProbe*> manifestCleanupProbe=nullptr;

static void HoldVerifiedManifest(const fs::path& manifest,const fs::path&,
                                CtBackup::BackupPublicationBoundary boundary,void* user) {
  if (boundary!=CtBackup::BackupPublicationBoundary::Verified) return;
  auto& probe=*static_cast<ManifestCleanupProbe*>(user);
  probe.verified=true;
  probe.manifest=manifest;
  // A concurrent reader without delete sharing is compatible with archive
  // verification but leaves a real Windows sharing conflict during cleanup.
  probe.reader=CreateFileW(manifest.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,
      OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  CtBackup::SetBackupPublicationHookForTest(nullptr,nullptr);
}

static constexpr UINT kBackupModalProbe=WM_APP+0x370;
static std::atomic_bool backupModalProbeArmed=false,backupModalProbeDelivered=false;
static HWND backupModalProbeParent=nullptr;
static void BackupSelectionPostUpdate(HWND parent,const std::wstring &text,int percent,const std::wstring &details) {
  if (auto* probe=manifestCleanupProbe.exchange(nullptr)) {
    probe->workerThread=GetCurrentThreadId();
    CtBackup::SetBackupPublicationHookForTest(HoldVerifiedManifest,probe);
  }
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
static bool useProductionProfileProbe=false;
ProfileUseState CtSpacesBackupSelectionTestProbe(const std::wstring &name,std::wstring &error) {
  backupProbed.push_back(name);
  if (useProductionProfileProbe) return ProbeClientProfilesInUse(name,error);
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
static void Flow(const fs::path &artifacts) {
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
  backupActive=L"Alpha";backupProbeActiveOnCall=2;backupProbed.clear();const auto destination=artifacts/L"must-not-exist.7z";files.push_back(destination.wstring());GuiProfExportAll();
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
  Check(fs::is_regular_file(archive) && files.empty() && backupProbed==std::vector<std::wstring>{name,name,name},
        "The selected tiny client completes its real SDK backup with three browser checks around the Notes snapshot");
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

static std::string FixtureBytes(const fs::path& path) {
  std::ifstream input(path,std::ios::binary);
  if (!input) throw std::runtime_error("Private backup fixture could not be read");
  return {std::istreambuf_iterator<char>(input),{}};
}

static void SelectPrivateClient(const std::wstring& name) {
  notesDialogObserver=[name](LPCWSTR resource,DLGPROC procedure,LPARAM data)->INT_PTR {
    if (resource!=MAKEINTRESOURCEW(IDD_BACKUP_CLIENTS) || procedure!=BackupClientsDlgProc)
      throw std::runtime_error("Unexpected private backup dialog");
    auto& selection=*reinterpret_cast<BackupClientSelection*>(data);
    for (auto& client:selection.clients) client.checked=client.name==name;
    selection.accepted=BackupSelectedNames(selection)==std::vector<std::wstring>{name};
    return selection.accepted ? IDOK : IDCANCEL;
  };
}

static void RetainedManifestNotice(const fs::path& artifacts,bool before) {
  ++scenarios;
  const std::wstring name=L"Alpha";
  Fixture(name);
  const auto source=g_sDataDir/L"Sites"/name;
  const auto payload=fs::path(L"Browsers")/L"edge"/L"Profile"/L"Default"/L"private-state.txt";
  const std::string sourceBytes="PRIVATE BACKUP COMPLETION DATA";
  Put(source/payload,sourceBytes);
  const auto output=artifacts/L"manifest-output";
  fs::create_directories(output);
  const auto archive=output/L"backup.7z";
  SelectPrivateClient(name);
  backupProbed.clear();files.push_back(archive.wstring());lastNotice.clear();

  ManifestCleanupProbe probe;
  manifestCleanupProbe.store(&probe);
  const DWORD ownerThread=GetCurrentThreadId();
  GuiProfExportAll();
  manifestCleanupProbe.store(nullptr);
  notesDialogObserver={};

  Check(probe.workerThread && probe.workerThread!=ownerThread && probe.verified,
        "The actual archive worker reaches the verified-manifest boundary");
  Check(probe.reader!=INVALID_HANDLE_VALUE && probe.manifest.parent_path()==output && probe.manifest!=archive,
        "A real share-read handle blocks cleanup of only the exact temporary manifest");
  const auto completion=lastNotice;
  Check(files.empty() && backupProbed==std::vector<std::wstring>{name,name} &&
        completion.find(L"Backup complete and verified.")!=std::wstring::npos &&
        completion.find(archive.wstring())!=std::wstring::npos,
        "The real launcher reports successful verified publication after both client checks");
  CloseHandle(probe.reader);probe.reader=INVALID_HANDLE_VALUE;
  const auto retained=FixtureBytes(probe.manifest);
  Check(retained.starts_with("format=ctSpaces-backup\n") && retained.find("profileCount=1\n")!=std::string::npos &&
        std::distance(fs::directory_iterator(output),fs::directory_iterator{})==2,
        "The valid final archive and its exact retained manifest are the only output files");
  Check(SUCCEEDED(_7zTest7z(archive.c_str(),nullptr,nullptr)),"The final backup independently passes native CRC");
  CtBackup::StagedRestore staged;
  const auto stage=CtBackup::StageValidated7zRestore(archive,artifacts/L"manifest-stage",nullptr,nullptr,staged);
  Check(stage.ok() && staged.profileCount==1 && FixtureBytes(staged.payloadDir/name/payload)==sourceBytes,
        "The real restore backend stages the exact selected payload from the completed backup");
  Check(FixtureBytes(source/payload)==sourceBytes && FixtureBytes(probe.manifest)==retained,
        "Backup and validation preserve the original source payload and retained manifest bytes");
  const bool reported=completion.find(probe.manifest.wstring())!=std::wstring::npos;
  std::wcout<<L"MANIFEST_NOTICE path="<<probe.manifest.wstring()<<L" exists=1 reported="<<reported<<L'\n';
  Check(reported!=before,before ? "Before proof reproduces the missing exact retained-manifest path"
                              : "Successful backup notice identifies the exact retained manifest path");
  Check(g_bUiEnabled && !g_bClientNotesBusy.load() && !g_bArchiveTaskInProgress,
        "Successful backup releases the launcher and both transaction guards");
  NotesBackgroundAudit("retained manifest after verified backup");
}

static void NotesOnlyBackup(const fs::path& artifacts,bool before) {
  ++scenarios;
  const std::wstring name=L"Notes only",malformedName=L"Malformed slot";
  const auto root=g_sDataDir/L"Sites"/name,malformed=g_sDataDir/L"Sites"/malformedName;
  fs::create_directories(root);fs::create_directories(malformed);
  Check(CreateV2ClientSkeleton(root) && CreateV2ClientSkeleton(malformed) && fs::is_empty(root/L"Browsers"),
        "The actual app creates an ordinary v2 client skeleton with an existing empty Browsers folder");
  const auto marker=FixtureBytes(root/kClientSchemaMarkerName);
  client_notes::NotePage page;
  page.name=L"Private saved note";page.rich=true;page.id=1;page.active=true;
  page.rtf="{\\rtf1\\ansi PRIVATE NOTES-ONLY CLIENT}";
  Check(client_notes::WriteNotebook(root,{page},client_notes::ReadNotebook(root)),
        "Production notebook storage saves the private notes-only fixture");
  const auto notebook=FixtureBytes(root/client_notes::kNotebookFileName);
  Put(malformed/L"Browsers/edge/unrecognized-state.txt","PRIVATE MALFORMED SLOT");

  useProductionProfileProbe=true;
  std::wstring probeDetails;
  const auto state=ProbeClientProfilesInUse(name,probeDetails);
  Check(g_activeProfiles.empty() && state==(before ? ProfileUseState::Indeterminate : ProfileUseState::NotInUse),
        before ? "Before proof reproduces indeterminate use for an ordinary closed client skeleton"
               : "An ordinary closed client skeleton is positively verified as not in use");
  const auto archive=artifacts/L"notes-only.7z",badArchive=artifacts/L"malformed-slot.7z";
  auto destination=archive;
  unsigned pickerCalls=0;
  notesFilePickerObserver=[&](OPENFILENAMEW* picker)->BOOL {
    ++pickerCalls;
    return wcscpy_s(picker->lpstrFile,picker->nMaxFile,destination.c_str())==0;
  };
  SelectPrivateClient(name);backupProbed.clear();lastNotice.clear();
  GuiProfExportAll();
  const bool published=fs::is_regular_file(archive);
  std::cout<<"NOTES_ONLY_BACKUP picker="<<pickerCalls<<" published="<<published<<'\n';
  if (before) {
    Check(pickerCalls==0 && !published && backupProbed==std::vector<std::wstring>{name} &&
          lastNotice.find(L"could not be inspected completely")!=std::wstring::npos,
          "Before proof reproduces refusal of notes-only backup before destination selection");
  } else {
    Check(pickerCalls==1 && published && backupProbed==std::vector<std::wstring>{name,name} &&
          lastNotice.find(L"Backup complete and verified.")!=std::wstring::npos,
          "The notes-only client completes actual selected backup after both profile-use checks");
    CtBackup::StagedRestore staged;
    const auto stage=CtBackup::StageValidated7zRestore(archive,artifacts/L"notes-only-stage",nullptr,nullptr,staged);
    const auto restored=staged.payloadDir/name;
    Check(stage.ok() && staged.profileCount==1 && fs::is_directory(restored/L"Browsers") &&
          fs::is_empty(restored/L"Browsers") && FixtureBytes(restored/kClientSchemaMarkerName)==marker &&
          FixtureBytes(restored/client_notes::kNotebookFileName)==notebook,
          "The valid archive stages the exact saved notebook, marker, and empty Browsers folder");
  }
  Check(FixtureBytes(root/client_notes::kNotebookFileName)==notebook &&
        FixtureBytes(root/kClientSchemaMarkerName)==marker && fs::is_empty(root/L"Browsers"),
        "The notes-only backup leaves the exact live notebook, marker, and empty Browsers folder");

  destination=badArchive;pickerCalls=0;backupProbed.clear();lastNotice.clear();
  SelectPrivateClient(malformedName);
  GuiProfExportAll();
  Check(pickerCalls==0 && !fs::exists(badArchive) && backupProbed==std::vector<std::wstring>{malformedName} &&
        lastNotice.find(L"could not be inspected completely")!=std::wstring::npos,
        "A malformed browser slot still fails closed before the backup destination picker");
  Check(FixtureBytes(malformed/L"Browsers/edge/unrecognized-state.txt")=="PRIVATE MALFORMED SLOT" &&
        g_bUiEnabled && !g_bClientNotesBusy.load() && !g_bArchiveTaskInProgress,
        "Refusal preserves malformed slot bytes and releases all backup guards");
  notesDialogObserver={};notesFilePickerObserver={};useProductionProfileProbe=false;
  NotesBackgroundAudit("notes-only client and malformed slot backup boundary");
}

static void CancelledBackupKeepsNotes(const fs::path &artifacts, bool before) {
  ++scenarios;
  const std::wstring client = L"Backup draft";
  Fixture(client);
  HWND notes = ::Open(client);
  auto &state = State(notes);
  Type(notes, L"Previously saved text");
  Save(notes);
  Type(notes, L" plus pending work");
  KillTimer(notes, 1);
  KillTimer(notes, 2);
  NotesShowPanel(notes, &state, true);
  const HWND panel = state.panel;
  const HWND editor = Edit(notes);
  const auto saved = client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes;
  Check(IsWindow(panel) && SendMessageW(editor, EM_CANUNDO, 0, 0),
        "The selected private notebook has an open Details panel and Undo");

  SelectPrivateClient(client);
  unsigned pickers = 0;
  notesFilePickerObserver = [&](OPENFILENAMEW *) -> BOOL { ++pickers; return FALSE; };
  backupProbed.clear();
  GuiProfExportAll();
  const bool panelClosed = !IsWindow(panel);
  const bool draftSaved = client_notes::ReadNotebook(state.clientRoot).notebook_snapshot.bytes != saved;
  Check(pickers == 1 && backupProbed == std::vector<std::wstring>{client} &&
        g_hNotesWindow == notes && g_bUiEnabled && !g_bClientNotesBusy.load(),
        "Canceling the destination releases only the backup transaction");
  Check(before ? panelClosed && draftSaved : !panelClosed && !draftSaved && state.panel == panel,
        "Canceling the destination keeps Notes Details open and leaves draft publication to Notes");
  Check(NotesWindowText(editor) == L"Previously saved text plus pending work" &&
        SendMessageW(editor, EM_CANUNDO, 0, 0), "The original editor keeps its exact draft and Undo");

  const auto destination = artifacts / L"confirmed.7z";
  files.push_back(destination.wstring());
  notesFilePickerObserver = {};
  backupProbed.clear();
  GuiProfExportAll();
  CtBackup::StagedRestore staged;
  const auto result = CtBackup::StageValidated7zRestore(destination, artifacts / L"verify", nullptr, nullptr, staged);
  const auto restored = result.ok() ? client_notes::ReadNotebook(staged.payloadDir / client) : client_notes::NotebookResult{};
  Check(result.ok() && staged.profileCount == 1 &&
        std::ranges::any_of(restored.pages, [](const auto &page) {
          return NotesPlain(page) == L"Previously saved text plus pending work";
        }), "Confirming a destination still captures the pending Notes content in the actual archive");
  Check(backupProbed == std::vector<std::wstring>{client,client,client} && files.empty() &&
        g_bUiEnabled && !g_bClientNotesBusy.load(),
        "The accepted backup checks before picking a destination, before saving Notes and before compression");
  std::cout << "BACKUP_CANCEL_NOTES panelClosed=" << panelClosed << " draftSaved=" << draftSaved << '\n';
  notesDialogObserver = {};
  DestroyWindow(notes);
  NotesBackgroundAudit("cancelled and confirmed backup with a selected Notes draft");
}

static void BrowserRefusalKeepsNotes(const fs::path &artifacts, bool before) {
  ++scenarios;
  const std::wstring client=L"Backup pending notes";
  Fixture(client);
  HWND notes=::Open(client);
  auto &state=State(notes);
  Type(notes,L"Private saved text");
  Save(notes);
  Type(notes,L" and an unsaved change");
  KillTimer(notes,1);
  KillTimer(notes,2);
  NotesShowPanel(notes,&state,true);
  const HWND panel=state.panel;
  const HWND editor=Edit(notes);
  const auto saved=client_notes::ReadNotebook(state.clientRoot).notebook_snapshot;
  Check(saved.status==client_notes::ReadStatus::Ok && IsWindow(panel) &&
        SendMessageW(editor,EM_CANUNDO,0,0),
        "The selected client has a saved baseline, an open Details panel and pending Undo");

  SelectPrivateClient(client);
  const auto destination=artifacts/L"backup.7z";
  unsigned pickerCalls=0;
  notesFilePickerObserver=[&](OPENFILENAMEW *picker)->BOOL {
    ++pickerCalls;
    backupActive=client;
    return wcscpy_s(picker->lpstrFile,picker->nMaxFile,destination.c_str())==0;
  };
  backupProbed.clear();
  backupActive.clear();
  lastNotice.clear();
  GuiProfExportAll();
  const bool panelClosed=state.panel!=panel || !IsWindow(panel);
  const bool draftSaved=!client_notes::SameSnapshot(saved,
      client_notes::ReadNotebook(state.clientRoot).notebook_snapshot);
  std::cout<<"BACKUP_BROWSER_REFUSAL panelClosed="<<panelClosed<<" draftSaved="<<draftSaved<<'\n';
  Check(pickerCalls==1 && backupProbed==std::vector<std::wstring>{client,client} &&
        !fs::exists(destination) && lastNotice.find(L"No backup was created")!=std::wstring::npos,
        "A selected profile that becomes busy during the picker refuses backup");
  Check(before ? panelClosed && draftSaved : !panelClosed && !draftSaved,
        "The browser-use refusal preserves the Details panel and original notebook bytes after repair");
  Check(g_hNotesWindow==notes && IsWindowEnabled(notes) &&
        NotesWindowText(editor)==L"Private saved text and an unsaved change" &&
        SendMessageW(editor,EM_CANUNDO,0,0) && g_bUiEnabled &&
        !g_bClientNotesBusy.load() && !g_bArchiveTaskInProgress,
        "Refused backup leaves the same editor, draft, Undo and idle launcher");

  notesFilePickerObserver={};
  backupActive.clear();
  backupProbed.clear();
  files.push_back(destination.wstring());
  GuiProfExportAll();
  Check(backupProbed==std::vector<std::wstring>(before?2:3,client) && files.empty() &&
        g_bUiEnabled && !g_bClientNotesBusy.load(),
        "Retry rechecks the same client and returns the launcher to idle");
  CtBackup::StagedRestore staged;
  const auto result=CtBackup::StageValidated7zRestore(destination,artifacts/L"verify",nullptr,nullptr,staged);
  const auto restored=result.ok() ? client_notes::ReadNotebook(staged.payloadDir/client) : client_notes::NotebookResult{};
  Check(result.ok() && staged.profileCount==1 && restored.status==client_notes::ReadStatus::Ok &&
        std::ranges::any_of(restored.pages,[](const auto &page) {
          return NotesPlain(page)==L"Private saved text and an unsaved change";
        }),"Successful retry contains the selected client's exact Notes text");
  notesDialogObserver={};
  DestroyWindow(notes);
  NotesBackgroundAudit("backup profile refusal with a selected Notes draft");
}

enum class InvalidBackupDestination {
  Extension, Managed, MissingFolder, LockedFile, ReadOnlyFile, FolderCollision
};

struct BackupDestinationRestriction {
  fs::path path;
  HANDLE reader = INVALID_HANDLE_VALUE;
  DWORD savedAttributes = INVALID_FILE_ATTRIBUTES;

  void Release() {
    if (reader != INVALID_HANDLE_VALUE) {
      CloseHandle(reader);
      reader = INVALID_HANDLE_VALUE;
    }
    if (savedAttributes != INVALID_FILE_ATTRIBUTES) {
      SetFileAttributesW(path.c_str(), savedAttributes);
      savedAttributes = INVALID_FILE_ATTRIBUTES;
    }
  }
  ~BackupDestinationRestriction() { Release(); }
};

static void InvalidDestinationKeepsNotes(const fs::path &artifacts, bool before,
                                         std::initializer_list<InvalidBackupDestination> cases) {
  for (const auto kind : cases) {
    ++scenarios;
    const bool managedFolder = kind == InvalidBackupDestination::Managed;
    const bool missingFolder = kind == InvalidBackupDestination::MissingFolder;
    const bool lockedFile = kind == InvalidBackupDestination::LockedFile;
    const bool readOnlyFile = kind == InvalidBackupDestination::ReadOnlyFile;
    const bool folderCollision = kind == InvalidBackupDestination::FolderCollision;
    const bool occupiedFile = lockedFile || readOnlyFile;
    const char *caseName = managedFolder ? "managed" : missingFolder ? "missing-folder" :
        lockedFile ? "locked-file" : readOnlyFile ? "read-only-file" :
        folderCollision ? "folder-collision" : "extension";
    const std::wstring client = managedFolder ? L"Managed destination" :
        missingFolder ? L"Vanished destination folder" : lockedFile ? L"Open backup file" :
        readOnlyFile ? L"Read-only backup file" : folderCollision ? L"Folder took filename" :
        L"Wrong extension";
    const auto caseDir = artifacts / caseName;
    fs::create_directories(caseDir);
    Fixture(client);
    const HWND notes = ::Open(client);
    auto &state = State(notes);
    Type(notes, L"Saved destination fixture");
    Save(notes);
    Type(notes, L" with pending work");
    KillTimer(notes, 1);
    KillTimer(notes, 2);
    NotesShowPanel(notes, &state, true);
    const HWND panel = state.panel;
    const HWND editor = Edit(notes);
    const auto saved = client_notes::ReadNotebook(state.clientRoot).notebook_snapshot;
    Check(saved.status == client_notes::ReadStatus::Ok && IsWindow(panel) &&
              SendMessageW(editor, EM_CANUNDO, 0, 0),
          "The selected private client has a pending draft and open Details");

    SelectPrivateClient(client);
    const auto rejected = managedFolder ? g_sDataDir / L"existing.7z" :
        missingFolder ? caseDir / L"picked" / L"existing.7z" :
        caseDir / (occupiedFile || folderCollision ? L"existing.7z" : L"existing.zip");
    const auto movedFolder = caseDir / L"moved";
    const auto folderContents = rejected / L"keep.txt";
    const std::string folderBytes = "Independent folder contents";
    if (occupiedFile) {
      CtBackup::BackupOptions initialOptions;
      initialOptions.selectedClientNames = std::vector<std::wstring>{client};
      Check(CtBackup::CreateValidated7zBackup(g_sDataDir / L"Sites", rejected,
                CTSPACES_VERSION_TEXT, nullptr, nullptr, initialOptions).ok(),
            "The existing destination is a verified backup");
    } else if (!folderCollision) {
      Put(rejected, "Existing destination bytes");
    }
    const auto originalBackupBytes = folderCollision ? folderBytes : FixtureBytes(rejected);
    BackupDestinationRestriction restriction{rejected};
    unsigned pickerCalls = 0;
    notesFilePickerObserver = [&](OPENFILENAMEW *picker) -> BOOL {
      ++pickerCalls;
      if (kind == InvalidBackupDestination::Extension)
        picker->nFilterIndex = 2;
      if (missingFolder)
        fs::rename(rejected.parent_path(), movedFolder);
      if (folderCollision) {
        Check(!fs::exists(rejected),
              "The picked backup filename is free before another program creates the folder");
        Put(folderContents, folderBytes);
      }
      if (lockedFile) {
        restriction.reader = CreateFileW(rejected.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                         nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        Check(restriction.reader != INVALID_HANDLE_VALUE,
              "An ordinary archive reader opens the selected destination without delete sharing");
      }
      if (readOnlyFile) {
        const DWORD attributes = GetFileAttributesW(rejected.c_str());
        Check(attributes != INVALID_FILE_ATTRIBUTES &&
                  SetFileAttributesW(rejected.c_str(), (attributes & ~FILE_ATTRIBUTE_NORMAL) |
                                                       FILE_ATTRIBUTE_READONLY),
              "The selected existing archive is marked read-only");
        restriction.savedAttributes = attributes;
      }
      return wcscpy_s(picker->lpstrFile, picker->nMaxFile, rejected.c_str()) == 0;
    };
    backupProbed.clear();
    lastNotice.clear();
    GuiProfExportAll();

    const bool panelClosed = state.panel != panel || !IsWindow(panel);
    const bool draftSaved = !client_notes::SameSnapshot(
        saved, client_notes::ReadNotebook(state.clientRoot).notebook_snapshot);
    std::cout << "BACKUP_DESTINATION_REFUSAL " << caseName
              << " panelClosed=" << panelClosed << " draftSaved=" << draftSaved << '\n';
    const auto previousBackup = missingFolder ? movedFolder / rejected.filename() :
        folderCollision ? folderContents : rejected;
    const wchar_t *reason = managedFolder ? L"inside the ctSpaces data folder" :
        missingFolder ? L"The backup folder is unavailable:" :
        folderCollision ? (before ? L"could not replace the selected destination file" :
                           L"already used by a folder") :
        occupiedFile ? (before ? L"could not replace the selected destination file" :
                        lockedFile ? L"Close programs using" : L"read-only") :
        L"must use the .7z extension";
    Check(pickerCalls == 1 && FixtureBytes(previousBackup) == originalBackupBytes &&
              lastNotice.find(reason) != std::wstring::npos,
          "The rejected destination reports the specific problem and preserves the existing file");
    if (missingFolder) {
      Check(!fs::exists(rejected.parent_path()) &&
                lastNotice.find(rejected.parent_path().wstring()) != std::wstring::npos &&
                backupProbed.size() == (before ? 3u : 1u),
            "A folder that disappeared at confirmation is named and rejected before another browser probe after repair");
    }
    if (occupiedFile) {
      Check(backupProbed.size() == (before ? 3u : 1u) &&
                std::distance(fs::directory_iterator(caseDir), fs::directory_iterator{}) == 1 &&
                (!readOnlyFile || (GetFileAttributesW(rejected.c_str()) & FILE_ATTRIBUTE_READONLY)),
            "The occupied file remains exact with no staging litter, and repair refuses it before another browser probe");
      restriction.Release();
    }
    if (folderCollision) {
      Check(fs::is_directory(rejected) &&
                std::distance(fs::directory_iterator(caseDir), fs::directory_iterator{}) == 1 &&
                backupProbed.size() == (before ? 3u : 1u),
            "The folder collision retains only the independent folder and repair refuses it before another browser probe");
      fs::rename(rejected, movedFolder);
    }
    Check(before ? panelClosed && draftSaved : !panelClosed && !draftSaved,
          "Destination refusal preserves the open Details panel and saved notebook bytes after repair");
    Check(g_hNotesWindow == notes && IsWindowEnabled(notes) &&
              NotesWindowText(editor) == L"Saved destination fixture with pending work" &&
              SendMessageW(editor, EM_CANUNDO, 0, 0) && g_bUiEnabled &&
              !g_bClientNotesBusy.load() && !g_bArchiveTaskInProgress,
          "Destination refusal keeps the editor, draft and Undo available and releases the launcher");

    notesFilePickerObserver = {};
    const auto accepted = occupiedFile || folderCollision ? rejected : caseDir / L"retry.7Z";
    files.push_back(accepted.wstring());
    GuiProfExportAll();
    CtBackup::StagedRestore staged;
    const auto result = CtBackup::StageValidated7zRestore(
        accepted, caseDir / L"verify", nullptr, nullptr, staged);
    const auto restored = result.ok() ? client_notes::ReadNotebook(staged.payloadDir / client)
                                      : client_notes::NotebookResult{};
    Check(result.ok() && staged.profileCount == 1 &&
              restored.status == client_notes::ReadStatus::Ok &&
              std::ranges::any_of(restored.pages, [](const auto &page) {
                return NotesPlain(page) == L"Saved destination fixture with pending work";
              }),
          "A retry using a valid destination verifies the selected client's exact pending Notes text");
    if (folderCollision)
      Check(FixtureBytes(movedFolder / folderContents.filename()) == folderBytes,
            "Retry to the same filename leaves the moved folder contents intact");
    notesDialogObserver = {};
    DestroyWindow(notes);
    NotesBackgroundAudit("rejected backup destination with a selected Notes draft");
  }
}
#include "BackupSelectionRoundCases.inl"
#include "BackupSelectionRoundCasesU.inl"
}
int wmain(int argc,wchar_t **argv) {
  const bool modal=argc==4 && !wcscmp(argv[3],L"--modal-snapshot");
  const bool focusedBefore=argc==4 && !wcscmp(argv[3],L"--focused-before");
  const bool focusedAfter=argc==4 && !wcscmp(argv[3],L"--focused-after");
  const bool cancelNotesBefore=argc==4 && !wcscmp(argv[3],L"--cancel-notes-before");
  const bool cancelNotesAfter=argc==4 && !wcscmp(argv[3],L"--cancel-notes-after");
  const bool browserRefusalBefore=argc==4 && !wcscmp(argv[3],L"--browser-refusal-before");
  const bool browserRefusalAfter=argc==4 && !wcscmp(argv[3],L"--browser-refusal-after");
  const bool destinationRefusalBefore=argc==4 && !wcscmp(argv[3],L"--destination-refusal-before");
  const bool destinationRefusalAfter=argc==4 && !wcscmp(argv[3],L"--destination-refusal-after");
  const bool missingFolderBefore=argc==4 && !wcscmp(argv[3],L"--missing-destination-before");
  const bool missingFolderAfter=argc==4 && !wcscmp(argv[3],L"--missing-destination-after");
  const bool occupiedBefore=argc==4 && !wcscmp(argv[3],L"--occupied-destination-before");
  const bool occupiedAfter=argc==4 && !wcscmp(argv[3],L"--occupied-destination-after");
  const bool folderBefore=argc==4 && !wcscmp(argv[3],L"--folder-destination-before");
  const bool folderAfter=argc==4 && !wcscmp(argv[3],L"--folder-destination-after");
  const bool deleteSharedBefore=argc==4 && !wcscmp(argv[3],L"--delete-shared-destination-before");
  const bool deleteSharedAfter=argc==4 && !wcscmp(argv[3],L"--delete-shared-destination-after");
  const bool movedArchiveBefore=argc==4 && !wcscmp(argv[3],L"--moved-archive-stage-before");
  const bool movedArchiveAfter=argc==4 && !wcscmp(argv[3],L"--moved-archive-stage-after");
  const bool archiveRoundU=argc==5 && !wcscmp(argv[3],L"--app-archive-round-u");
  unsigned archiveRound=0;
  if (archiveRoundU || (argc==5 && !wcscmp(argv[3],L"--app-archive-round-t"))) {
    const std::wstring_view round=argv[4];
    if (round==L"10") archiveRound=10;
    else if (round.size()==1 && round.front()>=L'1' && round.front()<=L'9')
      archiveRound=static_cast<unsigned>(round.front()-L'0');
  }
  if((argc!=3 && !modal && !focusedBefore && !focusedAfter && !cancelNotesBefore && !cancelNotesAfter &&
      !browserRefusalBefore && !browserRefusalAfter && !destinationRefusalBefore &&
      !destinationRefusalAfter && !missingFolderBefore && !missingFolderAfter &&
      !occupiedBefore && !occupiedAfter && !folderBefore && !folderAfter &&
      !deleteSharedBefore && !deleteSharedAfter && !movedArchiveBefore && !movedArchiveAfter &&
      !archiveRound) ||
      wcscmp(argv[1],L"--headless-background")) {std::cerr<<"Hidden entry and artifact path required; native windows created=0\n";return 2;}
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
    if((archiveRoundU && archiveRound) || movedArchiveBefore || movedArchiveAfter) {
      _7zSetHInstance(g_hInst);
      backup_selection_test::RunArchiveRoundU(artifacts,archiveRound ? archiveRound : 1,
                                              movedArchiveBefore);
    } else if(archiveRound || deleteSharedBefore || deleteSharedAfter) {
      _7zSetHInstance(g_hInst);
      backup_selection_test::RunArchiveRound(artifacts,archiveRound ? archiveRound : 10,
                                             deleteSharedBefore,!archiveRound);
    } else if(destinationRefusalBefore || destinationRefusalAfter) {
      _7zSetHInstance(g_hInst);
      using Destination=backup_selection_test::InvalidBackupDestination;
      backup_selection_test::InvalidDestinationKeepsNotes(
          artifacts,destinationRefusalBefore,{Destination::Extension,Destination::Managed});
    } else if(missingFolderBefore || missingFolderAfter) {
      _7zSetHInstance(g_hInst);
      backup_selection_test::InvalidDestinationKeepsNotes(
          artifacts,missingFolderBefore,{backup_selection_test::InvalidBackupDestination::MissingFolder});
    } else if(occupiedBefore || occupiedAfter) {
      _7zSetHInstance(g_hInst);
      using Destination=backup_selection_test::InvalidBackupDestination;
      backup_selection_test::InvalidDestinationKeepsNotes(
          artifacts,occupiedBefore,{Destination::LockedFile,Destination::ReadOnlyFile});
    } else if(folderBefore || folderAfter) {
      _7zSetHInstance(g_hInst);
      backup_selection_test::InvalidDestinationKeepsNotes(
          artifacts,folderBefore,{backup_selection_test::InvalidBackupDestination::FolderCollision});
    } else if(browserRefusalBefore || browserRefusalAfter) {
      _7zSetHInstance(g_hInst);
      backup_selection_test::BrowserRefusalKeepsNotes(artifacts,browserRefusalBefore);
    } else if(cancelNotesBefore || cancelNotesAfter) {
      _7zSetHInstance(g_hInst);
      backup_selection_test::CancelledBackupKeepsNotes(artifacts,cancelNotesBefore);
    } else if(focusedBefore || focusedAfter) {
      _7zSetHInstance(g_hInst);
      backup_selection_test::RetainedManifestNotice(artifacts,focusedBefore);
      backup_selection_test::NotesOnlyBackup(artifacts,focusedBefore);
      std::cout<<"FOCUSED_EXPECTED_DEFECTS="<<(focusedBefore ? 2 : 0)<<'\n';
    } else if(modal) backup_selection_test::ModalSnapshot(artifacts);
    else {backup_selection_test::Inventory();backup_selection_test::Checklist();backup_selection_test::ManyAndBounds(artifacts);backup_selection_test::Flow(artifacts);backup_selection_test::ExcludedOpenNotes(artifacts);}
    DestroyWindow(g_hGui);g_hGui=nullptr;NotesBackgroundAudit("final backup selection cleanup");NotesBackgroundBanner();UnhookWindowsHookEx(hook);
    std::cout<<"Backup selection: "<<checks<<" checks; "<<backup_selection_test::scenarios<<" private groups; 0 failures.\n";return 0;
  }catch(const std::exception &error){std::cerr<<"FAIL: "<<error.what()<<std::endl;NotesBackgroundBanner();return 1;}
}
