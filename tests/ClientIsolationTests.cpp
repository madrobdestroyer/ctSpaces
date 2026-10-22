#define CTSPACES_INSTALLER_TEST_HOOKS
#define CTSPACES_CLIENT_DELETE_TEST_HOOKS
#define CTSPACES_CLIENT_NAMESPACE_TEST_HOOKS
#define wWinMain CtSpacesProductionWinMain
#include "../ctSpaces.cpp"
#undef wWinMain
#include "DirectoryJunctionFixture.h"
#include <iostream>
#include <fstream>

enum class Fault { None, RootSwap, ParentSwap, ChildSwap, FileSwap, FileEdit, NewFile, LateLock, MetadataSwap, CleanupJunction };
static Fault fault=Fault::None;
static fs::path fixture, otherRoot, preserved, outsideRoot, lastStage;
static HANDLE lateLock=INVALID_HANDLE_VALUE;
static DWORD mutationError=0;
static bool mutationAttempted=false;
static unsigned checks=0,scenarios=0;
static bool closeNotesAllowed=false;
static unsigned noteCloseRequests=0;
static HMODULE richEditModule=nullptr;
static bool probeNamespaceDuringClose=false;
enum class RenameFault { None, SourceSwap, ParentSwap, TargetCollision, CaseCommitCollision, ConfigRollbackCollision, ConfigRollbackSourceSwap, CaseRollbackSourceSwap, UpgradeSwap };
static RenameFault renameFault=RenameFault::None;
static unsigned namespaceMoves=0;
static HANDLE configLock=INVALID_HANDLE_VALUE;
static fs::path collisionTarget;

static void Check(bool pass,const char *description) { ++checks;if(!pass) throw std::runtime_error(description); }
static void Put(const fs::path &path,const std::string &bytes) {
  fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<bytes;
  Check(out.good(),"Write fixture bytes");
}
static std::string ReadBytes(const fs::path &path) { std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}}; }
static bool ConvertEmptyDirectoryWithCompatibleSharing(const fs::path &link,const fs::path &target,bool *writerOpened=nullptr) {
  if(writerOpened)*writerOpened=false;
  const std::wstring print=fs::absolute(target).wstring(),substitute=L"\\??\\"+print;
  struct MountPoint { DWORD tag;WORD length,reserved,substituteOffset,substituteLength,printOffset,printLength;wchar_t names[1]; };
  const size_t bytes=(substitute.size()+print.size()+2)*sizeof(wchar_t);
  std::vector<unsigned char> buffer(offsetof(MountPoint,names)+bytes,0);
  auto *mount=reinterpret_cast<MountPoint *>(buffer.data());mount->tag=IO_REPARSE_TAG_MOUNT_POINT;
  mount->length=static_cast<WORD>(8+bytes);mount->substituteLength=static_cast<WORD>(substitute.size()*sizeof(wchar_t));
  mount->printOffset=static_cast<WORD>((substitute.size()+1)*sizeof(wchar_t));mount->printLength=static_cast<WORD>(print.size()*sizeof(wchar_t));
  memcpy(mount->names,substitute.data(),mount->substituteLength);
  memcpy(reinterpret_cast<unsigned char *>(mount->names)+mount->printOffset,print.data(),mount->printLength);
  HANDLE handle=CreateFileW(link.c_str(),GENERIC_WRITE,FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,nullptr,
      OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
  if(handle==INVALID_HANDLE_VALUE)return false;
  if(writerOpened)*writerOpened=true;
  DWORD returned=0;const bool converted=DeviceIoControl(handle,FSCTL_SET_REPARSE_POINT,buffer.data(),
      static_cast<DWORD>(buffer.size()),nullptr,0,&returned,nullptr)!=FALSE;
  const DWORD code=converted ? ERROR_SUCCESS : GetLastError();CloseHandle(handle);SetLastError(code);return converted;
}
static void Seed(const std::wstring &name,const std::string &sentinel) {
  const auto root=g_sDataDir/L"Sites"/name;
  Put(root/kClientSchemaMarkerName,kClientSchemaMarkerText);
  Put(root/client_notes::kFileName,sentinel);
  for(const auto browser:{BrowserKind::Edge,BrowserKind::Chrome}) {
    const auto slot=root/L"Browsers"/GetBrowserId(browser);
    Put(slot/kBrowserSchemaMarkerName,GetBrowserMarkerText(browser));
    Put(slot/L"Profile"/L"ctSpaces","ctSpaces-profile=2\r\n");
    Put(slot/L"Profile"/L"Default"/L"Preferences","{}");
    Put(slot/L"Profile"/L"Default"/L"private-session",sentinel);
  }
  client_notes::NotePage page{L"Ticket",true,"{\\rtf1 "+sentinel+"}",{}};
  page.id=1;page.ticket=L"Ticket "+name;page.tags={name};page.links={{L"Portal",L"https://example.test/"+name}};
  Check(client_notes::WriteNotebook(root,{page},client_notes::ReadNotebook(root)),"Seed rich notebook with same local note ID for different clients");
}
static void Setup(const wchar_t *name) {
  ++scenarios;g_sDataDir=fixture/name/L"d";fs::create_directories(g_sDataDir);
  g_sConfigPath=g_sDataDir/L"config.ini";g_sShortcutDesktopOverride=fixture/name/L"s";fs::create_directories(g_sShortcutDesktopOverride);
  g_pinnedClients.clear();g_archivedClients.clear();g_allClientNames.clear();g_clientsWithoutTabRestore.clear();g_sessions.clear();g_activeProfiles.clear();
  fault=Fault::None;mutationAttempted=false;mutationError=0;preserved.clear();lastStage.clear();
  renameFault=RenameFault::None;namespaceMoves=0;collisionTarget.clear();
  probeNamespaceDuringClose=false;
  Seed(L"Alpha","ALPHA");Seed(L"Bravo","BRAVO");otherRoot=g_sDataDir/L"Sites"/L"Bravo";
}
static void OtherIntact() {
  Check(ReadBytes(otherRoot/client_notes::kFileName)=="BRAVO","Other client's legacy note is unchanged");
  const auto notebook=client_notes::ReadNotebook(otherRoot);
  Check(notebook.status==client_notes::ReadStatus::Ok && notebook.pages.size()==1 && notebook.pages[0].id==1 &&
      notebook.pages[0].rtf=="{\\rtf1 BRAVO}" && notebook.pages[0].ticket==L"Ticket Bravo" && notebook.pages[0].tags==std::vector<std::wstring>{L"Bravo"},
      "Other client's rich notebook, same-ID note and metadata are unchanged");
  for(const auto browser:{BrowserKind::Edge,BrowserKind::Chrome})
    Check(ReadBytes(otherRoot/L"Browsers"/GetBrowserId(browser)/L"Profile"/L"Default"/L"private-session")=="BRAVO","Other client's browser session bytes are unchanged");
}
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR text,LPCWSTR title,UINT flags) {
  std::wcout<<L"NOTICE "<<title<<L": "<<text<<L'\n';return flags & MB_YESNO ? IDNO : IDOK;
}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &) { return false; }
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &root) {
  if(fault==Fault::None || fault==Fault::MetadataSwap || fault==Fault::CleanupJunction) return;
  mutationAttempted=true;
  if(fault==Fault::RootSwap || fault==Fault::ParentSwap || fault==Fault::ChildSwap) {
    const auto source=fault==Fault::RootSwap ? root : fault==Fault::ParentSwap ? root.parent_path() : root/L"Browsers"/L"edge"/L"Profile"/L"Default";
    preserved=g_sDataDir/L"preserved-object";
    if(!MoveFileExW(source.c_str(),preserved.c_str(),0)) {mutationError=GetLastError();return;}
    if(fault==Fault::RootSwap) Check(MoveFileExW(otherRoot.c_str(),root.c_str(),0)!=FALSE,"Swap another client into chosen pathname only if root guard failed");
    return;
  }
  if(fault==Fault::FileSwap) {
    preserved=g_sDataDir/L"preserved-note";
    Check(MoveFileExW((root/client_notes::kFileName).c_str(),preserved.c_str(),0)!=FALSE,"Swap fixture leaf after preflight");
    Check(CopyFileW((otherRoot/client_notes::kFileName).c_str(),(root/client_notes::kFileName).c_str(),FALSE)!=FALSE,"Copy other fixture note into chosen leaf");
  } else if(fault==Fault::FileEdit) Put(root/client_notes::kFileName,"ALPHA UPDATED EXTERNALLY");
  else if(fault==Fault::NewFile) Put(root/L"new-external-note","NEW EXTERNAL CONTENT");
  else if(fault==Fault::LateLock) {
    lateLock=CreateFileW((root/L"Browsers"/L"chrome"/L"Profile"/L"Default"/L"private-session").c_str(),GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    Check(lateLock!=INVALID_HANDLE_VALUE,"Acquire late external reader without delete sharing");
  }
}
namespace client_deletion_safety {
void BeforeNamespaceUpgrade(const fs::path &source) {
  if(renameFault!=RenameFault::UpgradeSwap) return;
  mutationAttempted=true;preserved=g_sDataDir/L"original-before-upgrade";
  Check(MoveFileExW(source.c_str(),preserved.c_str(),0)!=FALSE,"Place controlled directory replacement in the read-to-DELETE upgrade window");
  fs::copy(preserved,source,fs::copy_options::recursive);
  Put(source/L"replacement-private-state","NEW DIRECTORY OBJECT");
}
void BeforeNamespaceMove(const fs::path &source,const fs::path &destination,HANDLE) {
  ++namespaceMoves;
  const bool initial=namespaceMoves==1;
  const bool rollback=namespaceMoves==(renameFault==RenameFault::CaseRollbackSourceSwap ? 3u : 2u);
  if((renameFault==RenameFault::ConfigRollbackCollision || renameFault==RenameFault::ConfigRollbackSourceSwap || renameFault==RenameFault::CaseRollbackSourceSwap) && initial) {
    configLock=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    Check(configLock!=INVALID_HANDLE_VALUE,"Hold exact original config to reject settings commit after a valid profile move");
    return;
  }
  if((renameFault==RenameFault::SourceSwap && initial) || (renameFault==RenameFault::ParentSwap && initial) ||
      ((renameFault==RenameFault::ConfigRollbackSourceSwap || renameFault==RenameFault::CaseRollbackSourceSwap) && rollback)) {
    mutationAttempted=true;preserved=g_sDataDir/L"rename-displaced-original";
    const auto victim=renameFault==RenameFault::ParentSwap ? source.parent_path() : source;
    if(!MoveFileExW(victim.c_str(),preserved.c_str(),0)) {mutationError=GetLastError();return;}
    Check(MoveFileExW(otherRoot.c_str(),source.c_str(),0)!=FALSE,"Replace rename source only if original directory guard failed");
  } else if((renameFault==RenameFault::TargetCollision && initial) ||
      (renameFault==RenameFault::CaseCommitCollision && namespaceMoves==2) ||
      (renameFault==RenameFault::ConfigRollbackCollision && rollback)) {
    mutationAttempted=true;collisionTarget=destination;
    Check(fs::create_directory(destination),"Create competing target at the actual bound move boundary");
    Put(destination/L"foreign-target-state","FOREIGN TARGET");
  }
}
void BeforeMetadataMove(const fs::path &root) {
  if(fault!=Fault::MetadataSwap) return;
  mutationAttempted=true;preserved=g_sDataDir/L"preserved-original-metadata";
  Check(MoveFileExW((root/L"Browsers").c_str(),preserved.c_str(),0)!=FALSE,"Move only metadata directories after payload deletion");
  Check(fs::create_directory(root/L"Browsers"),"Create different metadata directory object");
  Put(root/L"Browsers"/L"unexpected-state","FOREIGN STATE");
}
void BeforeMetadataCleanup(const fs::path &root) {
  lastStage=root;
  if(fault!=Fault::CleanupJunction) return;
  mutationAttempted=true;preserved=g_sDataDir/L"preserved-staged-metadata";
  Check(MoveFileExW((root/L"Browsers").c_str(),preserved.c_str(),0)!=FALSE,"Move staged metadata before cleanup");
  Check(CtTest::CreateDirectoryJunction(root/L"Browsers",outsideRoot),"Create fixture-only junction before cleanup");
}
}
static INT_PTR CALLBACK NoteGuardDialog(HWND dialog,UINT message,WPARAM,LPARAM data) {
  if(message==WM_INITDIALOG) {SetWindowLongPtrW(dialog,DWLP_USER,data);return TRUE;}
  if(message==WM_CLOSE) {
    ++noteCloseRequests;if(!closeNotesAllowed) return TRUE;
    auto *state=reinterpret_cast<ClientNotesDialogState *>(GetWindowLongPtrW(dialog,DWLP_USER));
    if(probeNamespaceDuringClose) {
      const auto parked=g_sDataDir/L"close-save-displaced-original";
      const bool moved=MoveFileExW(state->clientRoot.c_str(),parked.c_str(),0)!=FALSE;
      const DWORD code=moved ? ERROR_SUCCESS : GetLastError();
      Check(!moved && (code==ERROR_SHARING_VIOLATION || code==ERROR_ACCESS_DENIED) && !fs::exists(parked),
          "Genuine read/list namespace pin rejects root substitution while the selected notebook is flushing");
    }
    state->pages=state->original.pages;state->pages[0].rtf="{\\rtf1 ALPHA FLUSHED}";
    Check(client_notes::WriteNotebook(state->clientRoot,state->pages,state->original),"Guard fixture flushes only its own notebook before close");
    DestroyWindow(dialog);g_hNotesWindow=nullptr;return TRUE;
  }
  return FALSE;
}
static void ArchivedReferenceCases() {
  for(size_t index=0;index<3;++index) {
    const auto name=L"archive-reference-"+std::to_wstring(index);Setup(name.c_str());Seed(L"Charlie","CHARLIE");
    g_archivedClients={L"Alpha",L"Bravo",L"Charlie"};Check(SaveArchivedClients(),"Save archived fixture list");
    const auto chosen=g_archivedClients[index];
    Check(WndProc(g_hGui,WM_APP_QA_RESTORE_ARCHIVED,index,0)!=FALSE,"Actual QA restore dispatcher accepts selected archived entry");
    Check(GetClientInputText()==chosen,"Restored selection remains the original vector element for first, middle and last entries");
    Check(!IsClientArchived(chosen) && g_archivedClients.size()==2,"Only chosen archive membership is removed");
    OtherIntact();
  }
}
static void RenameNamespaceCases() {
  for(const auto mode:{RenameFault::SourceSwap,RenameFault::ParentSwap,RenameFault::TargetCollision,RenameFault::CaseCommitCollision,RenameFault::ConfigRollbackCollision,RenameFault::ConfigRollbackSourceSwap,RenameFault::CaseRollbackSourceSwap,RenameFault::UpgradeSwap}) {
    const auto name=L"rename-fault-"+std::to_wstring(static_cast<unsigned>(mode));Setup(name.c_str());renameFault=mode;
    g_pinnedClients={L"Alpha"};Check(SavePinnedClients(),"Save rename fixture preferences");
    const auto originalConfig=ReadBytes(g_sConfigPath);
    const auto target=(mode==RenameFault::CaseCommitCollision || mode==RenameFault::CaseRollbackSourceSwap) ? L"ALPHA" : L"Alpha renamed";
    std::wstring error;const bool renamed=RenameClientProfile(L"Alpha",target,error);
    std::wcout<<L"RENAME FAULT "<<static_cast<unsigned>(mode)<<L" attempted="<<mutationAttempted<<L" error="<<mutationError<<L" moves="<<namespaceMoves<<L" accepted="<<renamed<<L" outcome="<<error<<L'\n';
    Check(mutationAttempted,"Rename attack reached its real bound move phase");
    if(mode==RenameFault::SourceSwap || mode==RenameFault::ParentSwap) {
      Check(mutationError==ERROR_SHARING_VIOLATION || mutationError==ERROR_ACCESS_DENIED,"Rename source and ancestor cannot be exchanged during commit");
      Check(renamed && ReadBytes(g_sDataDir/L"Sites"/target/client_notes::kFileName)=="ALPHA","Bound move carries only original client notebook into its new identity");
      Check(client_notes::ReadNotebook(g_sDataDir/L"Sites"/target).pages[0].rtf=="{\\rtf1 ALPHA}","Original rich notebook survives safe rename");
    } else {
      Check(!renamed,"Target collision or failed config commit cannot report rename success");
      Check(ReadBytes(g_sConfigPath)==originalConfig && g_pinnedClients==std::vector<std::wstring>{L"Alpha"},"Failed rename retains exact original config and pinned identity");
      if(mode==RenameFault::ConfigRollbackSourceSwap || mode==RenameFault::CaseRollbackSourceSwap) {
        Check(mutationError==ERROR_SHARING_VIOLATION || mutationError==ERROR_ACCESS_DENIED,"Rollback remains bound to original root against source substitution");
        Check(ReadBytes(g_sDataDir/L"Sites"/L"Alpha"/client_notes::kFileName)=="ALPHA","Ordinary and case-only rollback restore the original client note");
      } else if(mode==RenameFault::UpgradeSwap) {
        Check(ReadBytes(preserved/client_notes::kFileName)=="ALPHA" && ReadBytes(g_sDataDir/L"Sites"/L"Alpha"/L"replacement-private-state")=="NEW DIRECTORY OBJECT",
            "Identical-payload replacement and displaced original are both preserved after read-to-DELETE upgrade refusal");
        Check(namespaceMoves==0 && !fs::exists(g_sDataDir/L"Sites"/L"Alpha renamed"),"Root identity upgrade rejects replacement before any bound rename is attempted");
      } else {
        Check(ReadBytes(collisionTarget/L"foreign-target-state")=="FOREIGN TARGET","Competing forward or rollback target is never overwritten");
        bool originalFound=false;
        for(const auto &entry:fs::directory_iterator(g_sDataDir/L"Sites"))
          if(ReadBytes(entry.path()/client_notes::kFileName)=="ALPHA") originalFound=true;
        Check(originalFound,"Original notebook remains at original, renamed or explicitly preserved intermediate root");
      }
    }
    if(configLock!=INVALID_HANDLE_VALUE) {CloseHandle(configLock);configLock=INVALID_HANDLE_VALUE;}
    OtherIntact();
  }
}
static void DeleteFaultCases() {
  for(const auto mode:{Fault::RootSwap,Fault::ParentSwap,Fault::ChildSwap,Fault::FileSwap,Fault::FileEdit,Fault::NewFile,Fault::LateLock,Fault::MetadataSwap,Fault::CleanupJunction}) {
    const auto name=L"delete-fault-"+std::to_wstring(static_cast<unsigned>(mode));Setup(name.c_str());fault=mode;
    outsideRoot=g_sDataDir/L"outside";Put(outsideRoot/L"outside-state","OUTSIDE UNCHANGED");
    const auto root=g_sDataDir/L"Sites"/L"Alpha";
    std::wstring error;const bool deleted=DeleteEntireClient(L"Alpha",true,std::nullopt,error);
    std::wcout<<L"FAULT "<<static_cast<unsigned>(mode)<<L" attempted="<<mutationAttempted<<L" mutationError="<<mutationError<<L" deleted="<<deleted<<L" outcome="<<error<<L'\n';
    Check(mutationAttempted,"Adversarial mutation reached its intended production boundary");
    if(mode==Fault::RootSwap || mode==Fault::ParentSwap || mode==Fault::ChildSwap) {
      Check(mutationError==ERROR_SHARING_VIOLATION || mutationError==ERROR_ACCESS_DENIED,"Root, ancestor and child namespace replacement are blocked by bound directory guards");
      Check(deleted && !fs::exists(root),"Authorized original-client delete completes despite blocked substitution");
      Check(!fs::exists(preserved),"No selected root/ancestor/child was moved out from under deletion");
    } else {
      Check(!deleted,"Changed or locked client tree is not reported successfully deleted");
      if(mode==Fault::FileSwap || mode==Fault::FileEdit || mode==Fault::NewFile) {
        Check(ReadBytes(root/L"Browsers"/L"edge"/L"Profile"/L"Default"/L"private-session")=="ALPHA","Change detection precedes removal of unchanged client payload");
        Check(client_notes::ReadNotebook(root).pages[0].rtf=="{\\rtf1 ALPHA}","Original rich notebook remains before changed-tree rejection");
      }
      if(mode==Fault::FileSwap) Check(ReadBytes(preserved)=="ALPHA" && ReadBytes(root/client_notes::kFileName)=="BRAVO","Neither displaced original note nor replacement leaf is deleted");
      if(mode==Fault::FileEdit) Check(ReadBytes(root/client_notes::kFileName)=="ALPHA UPDATED EXTERNALLY","External same-object note edit remains intact");
      if(mode==Fault::NewFile) Check(ReadBytes(root/L"new-external-note")=="NEW EXTERNAL CONTENT","New external content remains intact");
      if(mode==Fault::MetadataSwap) Check(ReadBytes(root/L"Browsers"/L"unexpected-state")=="FOREIGN STATE" && fs::is_directory(preserved),"Different metadata object and original metadata remain after refusal");
      if(mode==Fault::CleanupJunction) Check(fs::is_directory(lastStage) && fs::is_directory(preserved),"Rejected staging and original metadata are preserved for inspection");
    }
    Check(ReadBytes(outsideRoot/L"outside-state")=="OUTSIDE UNCHANGED","Outside fixture data is never removed by deletion or cleanup");
    OtherIntact();
    if(mode==Fault::LateLock) {
      Check(lateLock!=INVALID_HANDLE_VALUE,"Late-lock fixture remained bound until rejected operation returned");CloseHandle(lateLock);lateLock=INVALID_HANDLE_VALUE;fault=Fault::None;
      Check(IsV2ClientContainer(root),"Locked deletion retains recognized client authority");
      Check(DeleteEntireClient(L"Alpha",true,std::nullopt,error) && !fs::exists(root),"Clean retry succeeds after late lock is released");OtherIntact();
    }
  }
}
static void OpenNotebookOwnership() {
  Setup(L"open-notebook-other-client");
  const auto root=g_sDataDir/L"Sites"/L"Alpha";
  ClientNotesDialogState state{L"Alpha",root,client_notes::ReadNotebook(root)};
  closeNotesAllowed=false;noteCloseRequests=0;
  g_hNotesWindow=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_CLIENT_NOTES),g_hGui,NoteGuardDialog,reinterpret_cast<LPARAM>(&state));
  Check(g_hNotesWindow!=nullptr,"Create hidden client-change protocol fixture");const auto notesWindow=g_hNotesWindow;
  std::wstring error;
  Check(RenameClientProfile(L"Bravo",L"Bravo renamed",error),"Another client can be renamed while original client's notebook remains open");
  otherRoot=g_sDataDir/L"Sites"/L"Bravo renamed";
  Check(g_hNotesWindow==notesWindow && noteCloseRequests==0,"Unrelated rename does not close another client's notes");
  Check(client_notes::ReadNotebook(otherRoot).pages[0].rtf=="{\\rtf1 BRAVO}","Rename carries only the renamed client's same-ID notebook");
  Check(ArchiveClientProfile(L"Bravo renamed",error) && RestoreArchivedClient(L"Bravo renamed",&error),"Archive/unarchive of another client preserves the open notebook owner");
  Check(g_hNotesWindow==notesWindow && noteCloseRequests==0,"Unrelated archive and restore do not close original client's notes");
  Check(!RenameClientProfile(L"Alpha",L"Alpha renamed",error),"Rename stops if the selected client's open note cannot close/save");
  Check(!ArchiveClientProfile(L"Alpha",error),"Archive stops if the selected client's open note cannot close/save");
  Check(!DeleteEntireClient(L"Alpha",true,std::nullopt,error),"Delete stops if the selected client's open note cannot close/save");
  Check(noteCloseRequests==3 && g_hNotesWindow==notesWindow && fs::is_directory(root),"All three failed client changes preserve the same open owner and original root");
  Check(ReadBytes(root/client_notes::kFileName)=="ALPHA" && client_notes::ReadNotebook(root).pages[0].rtf=="{\\rtf1 ALPHA}","Failed close/save guard keeps original note bytes");
  closeNotesAllowed=true;
  Check(RenameClientProfile(L"Alpha",L"Alpha renamed",error) && !g_hNotesWindow,"Matching rename flushes and closes its own notebook before moving the root");
  Check(client_notes::ReadNotebook(g_sDataDir/L"Sites"/L"Alpha renamed").pages[0].rtf=="{\\rtf1 ALPHA FLUSHED}","Flushed notebook moves with its own client identity");
  Check(client_notes::ReadNotebook(otherRoot).pages[0].rtf=="{\\rtf1 BRAVO}","Matching-ID notebook of another client never receives the flushed edit");
}
static void DeleteWhileOtherNotebookOpen() {
  Setup(L"delete-with-unrelated-notebook");
  const auto root=g_sDataDir/L"Sites"/L"Alpha";
  ClientNotesDialogState state{L"Alpha",root,client_notes::ReadNotebook(root)};
  closeNotesAllowed=false;noteCloseRequests=0;
  g_hNotesWindow=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_CLIENT_NOTES),g_hGui,NoteGuardDialog,reinterpret_cast<LPARAM>(&state));
  const auto notesWindow=g_hNotesWindow;std::wstring error;
  Check(DeleteEntireClient(L"Bravo",true,std::nullopt,error),"Deleting another client leaves an unrelated open notebook available");
  Check(g_hNotesWindow==notesWindow && noteCloseRequests==0,"Other-client delete does not close or retarget open notes");
  Check(ReadBytes(root/client_notes::kFileName)=="ALPHA" && client_notes::ReadNotebook(root).pages[0].rtf=="{\\rtf1 ALPHA}","Deleting another client leaves open owner's legacy and rich note bytes unchanged");
  DestroyWindow(g_hNotesWindow);g_hNotesWindow=nullptr;
}
static void ActiveClientGuards() {
  Setup(L"active-client-guards");
  const auto root=g_sDataDir/L"Sites"/L"Alpha";
  const auto notebook=ReadBytes(root/client_notes::kNotebookFileName);
  g_activeProfiles.emplace(ClientBrowserKey{L"Alpha",BrowserKind::Edge},ActiveProfileInfo{});
  std::wstring error;
  Check(!RenameClientProfile(L"Alpha",L"Alpha renamed",error),"Active client cannot be renamed through the native lifecycle helper");
  Check(!ArchiveClientProfile(L"Alpha",error),"Active client cannot be archived through the native lifecycle helper");
  Check(!DeleteEntireClient(L"Alpha",true,std::nullopt,error),"Active client cannot be deleted through the native lifecycle helper");
  Check(ReadBytes(root/client_notes::kNotebookFileName)==notebook && ReadBytes(root/client_notes::kFileName)=="ALPHA", "Active-client refusals preserve exact rich and legacy notebook bytes");
  Check(ReadBytes(root/L"Browsers"/L"edge"/L"Profile"/L"Default"/L"private-session")=="ALPHA", "Active-client refusals preserve browser session state");
  OtherIntact();g_activeProfiles.clear();
}
static void NotesLeaseCompatibility() {
  for(const bool deleting:{false,true}) {
    Setup(deleting ? L"notes-lease-delete" : L"notes-lease-rename");
    const auto root=g_sDataDir/L"Sites"/L"Alpha";
    ClientNotesDialogState state{L"Alpha",root,client_notes::ReadNotebook(root)};
    closeNotesAllowed=true;noteCloseRequests=0;probeNamespaceDuringClose=true;
    g_hNotesWindow=CreateDialogParamW(g_hInst,MAKEINTRESOURCEW(IDD_CLIENT_NOTES),g_hGui,NoteGuardDialog,reinterpret_cast<LPARAM>(&state));
    Check(g_hNotesWindow!=nullptr,"Create hidden actual notebook-lease close fixture");std::wstring error;
    const bool changed=deleting ? DeleteEntireClient(L"Alpha",true,std::nullopt,error) : RenameClientProfile(L"Alpha",L"Alpha renamed",error);
    if(!changed) std::wcout<<L"NOTES LEASE CHANGE REJECTED "<<error<<L'\n';
    Check(changed && !g_hNotesWindow && noteCloseRequests==1,"Original read-only namespace binding permits actual notebook lease flush before client change");
    if(!deleting) Check(client_notes::ReadNotebook(g_sDataDir/L"Sites"/L"Alpha renamed").pages[0].rtf=="{\\rtf1 ALPHA FLUSHED}","Actual lease-compatible flush moves with original client root");
    OtherIntact();
  }
}
static void EmptyParentNamespaceProtection() {
  Setup(L"staging-parent-sentinel");
  const auto root=g_sDataDir/L"Sites"/L"Alpha",parent=g_sDataDir/L"_Delete";
  outsideRoot=g_sDataDir/L"outside";Put(outsideRoot/L"outside-state","OUTSIDE UNCHANGED");
  Check(fs::create_directory(parent),"Create disposable empty deletion parent");
  Put(parent/L"~d0","PREEXISTING FOREIGN FILE");
  {
    client_deletion_safety::BoundTree bound;std::wstring error;
    Check(bound.CaptureNamespace(root,error) && bound.PinAncestor(parent,false,error),"Capture genuine read/list original and staging parent guards");
    Check(bound.ProtectEmptyParent(parent,error),"Reserve nonempty delete-on-close staging sentinel under the tighter directory guard");
    Check(ReadBytes(parent/L"~d0")=="PREEXISTING FOREIGN FILE" && fs::exists(parent/L"~d1"),"Sentinel filename collision never overwrites a preexisting object");
    Check(DeleteFileW((parent/L"~d1").c_str())==FALSE,"An external process cannot remove the exclusively held staging sentinel");
    HANDLE writer=CreateFileW((parent/L"~d1").c_str(),GENERIC_WRITE,FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    Check(writer==INVALID_HANDLE_VALUE,"An external process cannot rewrite the exclusively held staging sentinel");
    Check(!ConvertEmptyDirectoryWithCompatibleSharing(parent,outsideRoot),"Compatible directory writer cannot turn the sentinel-protected staging parent into an NTFS junction");
    Check((GetFileAttributesW(parent.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)==0 && bound.Verify(error),"Rejected in-place conversion preserves real original ancestor identity");
    Check(bound.MoveRoot(parent/L"staged-alpha",error) && bound.MoveRoot(root,error),"Native-compatible protected parent allows original-directory move and rollback");
  }
  Check(!fs::exists(parent/L"~d1") && ReadBytes(parent/L"~d0")=="PREEXISTING FOREIGN FILE","Only the owned sentinel disappears when directory guards release");
  Check(ReadBytes(outsideRoot/L"outside-state")=="OUTSIDE UNCHANGED","Protection attempts never alter outside data");OtherIntact();
  Setup(L"staging-parent-sentinel-only");
  const auto owned=g_sDataDir/L"Sites"/L"Alpha",sentinelOnly=g_sDataDir/L"_Delete";
  outsideRoot=g_sDataDir/L"outside";Put(outsideRoot/L"outside-state","OUTSIDE UNCHANGED");
  Check(fs::create_directory(sentinelOnly),"Create actually empty parent with no foreign file masking the protection");
  {
    client_deletion_safety::BoundTree bound;std::wstring error;
    Check(bound.CaptureNamespace(owned,error) && bound.PinAncestor(sentinelOnly,false,error) && bound.ProtectEmptyParent(sentinelOnly,error),"Bind sole-sentinel parent protection");
    Check(std::distance(fs::directory_iterator(sentinelOnly),fs::directory_iterator{})==1 && fs::exists(sentinelOnly/L"~d0"),"Owned delete-on-close sentinel is the only nonempty-directory protection");
    bool writerOpened=false;const bool converted=ConvertEmptyDirectoryWithCompatibleSharing(sentinelOnly,outsideRoot,&writerOpened);
    const DWORD code=converted ? ERROR_SUCCESS : GetLastError();
    std::wcout<<L"SOLE SENTINEL FSCTL attempted="<<writerOpened<<L" converted="<<converted<<L" error="<<code<<L'\n';
    Check(writerOpened && !converted && code!=ERROR_SUCCESS,"Real shared directory writer reaches FSCTL and the owned sole sentinel rejects in-place junction conversion");
    Check(bound.Verify(error) && (GetFileAttributesW(sentinelOnly.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)==0,"Actual directory and original ancestor IDs survive sole-sentinel conversion attempt");
  }
  Check(fs::is_empty(sentinelOnly) && ReadBytes(outsideRoot/L"outside-state")=="OUTSIDE UNCHANGED","Sole-sentinel release leaves no retained payload and no outside edits");OtherIntact();
  Setup(L"staging-parent-pre-protection-conversion");
  const auto original=g_sDataDir/L"Sites"/L"Alpha",empty=g_sDataDir/L"_Delete";
  outsideRoot=g_sDataDir/L"outside";Put(outsideRoot/L"outside-state","OUTSIDE UNCHANGED");
  Check(fs::create_directory(empty),"Create empty parent for the acquisition race");
  client_deletion_safety::BoundTree captured;std::wstring error;
  Check(captured.CaptureNamespace(original,error) && captured.PinAncestor(empty,false,error),"Capture same-ID ancestor before in-place conversion");
  Check(ConvertEmptyDirectoryWithCompatibleSharing(empty,outsideRoot),"Real compatible-sharing FSCTL fixture reaches the unprotected same-object reparse conversion boundary");
  Check(!captured.ProtectEmptyParent(empty,error) && !captured.Verify(error),"Acquisition rejects in-place reparse conversion even though the directory file ID is unchanged");
  Check(!fs::exists(outsideRoot/L"~d0") && ReadBytes(outsideRoot/L"outside-state")=="OUTSIDE UNCHANGED","Rejected unsafe ancestor does not reserve any sentinel in outside data");
  Check(ReadBytes(original/client_notes::kFileName)=="ALPHA","Rejected parent acquisition preserves selected notebook");OtherIntact();
}
int wmain(int argc,wchar_t **argv) {
  fixture=fs::current_path()/L"build"/(L"cis-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  try {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
    Check(InitCommonControlsEx(&controls)!=FALSE,"Register fixture dialog controls");
    richEditModule=LoadLibraryExW(L"Msftedit.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    Check(richEditModule!=nullptr,"Register the resource's rich editor class for hidden guard dialogs");
    g_hGui=CreateWindowExW(0,L"STATIC",L"Client isolation hidden tests",0,0,0,1,1,HWND_MESSAGE,nullptr,g_hInst,nullptr);
    g_hClientEdit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD,0,0,1,1,g_hGui,nullptr,g_hInst,nullptr);
    g_hComboClient=CreateWindowExW(0,L"COMBOBOX",L"",WS_CHILD | CBS_DROPDOWN,0,0,1,1,g_hGui,nullptr,g_hInst,nullptr);
    if(argc==2 && std::wstring_view(argv[1])==L"--notes-lease-only") NotesLeaseCompatibility();
    else {
      ArchivedReferenceCases();DeleteFaultCases();RenameNamespaceCases();OpenNotebookOwnership();DeleteWhileOtherNotebookOpen();ActiveClientGuards();NotesLeaseCompatibility();EmptyParentNamespaceProtection();
    }
    DestroyWindow(g_hGui);g_hGui=nullptr;FreeLibrary(richEditModule);richEditModule=nullptr;CoUninitialize();
    std::wcout<<L"Client isolation tests passed: "<<checks<<L" assertions, "<<scenarios<<L" scenarios. Fixture retained: "<<fixture<<L'\n';return 0;
  } catch(const std::exception &error) {
    std::cerr<<"Client isolation failure: "<<error.what()<<'\n';std::wcout<<L"Fixture retained: "<<fixture<<L'\n';
    if(lateLock!=INVALID_HANDLE_VALUE)CloseHandle(lateLock);
    if(configLock!=INVALID_HANDLE_VALUE)CloseHandle(configLock);
    if(g_hNotesWindow)DestroyWindow(g_hNotesWindow);
    if(g_hGui)DestroyWindow(g_hGui);
    if(richEditModule)FreeLibrary(richEditModule);
    CoUninitialize();return 1;
  }
}
