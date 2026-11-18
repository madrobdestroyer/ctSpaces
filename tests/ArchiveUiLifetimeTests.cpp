// Actual archive GUI/message-pump paths; all windows remain hidden and owned.
#include <windows.h>
#include "RetainedFixture.h"
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <vector>
#include "../ProgressUI.h"
static unsigned suppressed=0,forbidden=0,nativeShows=0,nativeFocus=0,nativeForeground=0;
static bool failEvent=false,failWait=false,readyQuitWait=false,readyQuitPosted=false;
static HANDLE WINAPI PrivateEvent(LPSECURITY_ATTRIBUTES security,BOOL manual,BOOL initial,LPCWSTR name){if(failEvent){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}return ::CreateEventW(security,manual,initial,name);}
static DWORD WINAPI PrivateWait(DWORD count,const HANDLE*handles,BOOL all,DWORD timeout,DWORD wake){if(failWait){failWait=false;SetLastError(ERROR_INVALID_HANDLE);return WAIT_FAILED;}if(readyQuitWait){readyQuitWait=false;if(count!=1||WaitForSingleObject(handles[0],5000)!=WAIT_OBJECT_0){SetLastError(ERROR_TIMEOUT);return WAIT_FAILED;}::PostQuitMessage(88);readyQuitPosted=true;return WAIT_OBJECT_0;}return ::MsgWaitForMultipleObjects(count,handles,all,timeout,wake);}
static BOOL WINAPI NoShow(HWND,int){++suppressed;return FALSE;}
static HWND WINAPI NoFocus(HWND){++suppressed;return nullptr;}
static BOOL WINAPI NoForeground(HWND){++suppressed;return FALSE;}
static BOOL WINAPI NoRaise(HWND){++suppressed;return FALSE;}
static BOOL WINAPI NoPosition(HWND,HWND,int,int,int,int,UINT){++suppressed;return FALSE;}
static BOOL WINAPI NoPaint(HWND,const RECT*,BOOL){return FALSE;}
static BOOL WINAPI NoRedraw(HWND,const RECT*,HRGN,UINT){return FALSE;}
static BOOL WINAPI NoBeep(UINT){return TRUE;}
static BOOL WINAPI NoClipboard(HWND){++forbidden;return FALSE;}
static HINSTANCE WINAPI NoShell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){++forbidden;return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);}
static BOOL WINAPI NoShellEx(SHELLEXECUTEINFOW*){++forbidden;return FALSE;}
static BOOL WINAPI NoProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){++forbidden;return FALSE;}
static INT_PTR WINAPI NoDialog(HINSTANCE,LPCWSTR,HWND,DLGPROC,LPARAM){++forbidden;return IDCANCEL;}
static INT_PTR WINAPI NoIndirectDialog(HINSTANCE,LPCDLGTEMPLATEW,HWND,DLGPROC,LPARAM){++forbidden;return IDCANCEL;}
static LSTATUS WINAPI NoSystemValue(HKEY,LPCWSTR,LPCWSTR,LPDWORD,LPVOID,LPDWORD){++forbidden;return ERROR_ACCESS_DENIED;}
static HANDLE WINAPI Snapshot(DWORD,DWORD){return CreateEventW(nullptr,FALSE,FALSE,nullptr);}
static BOOL WINAPI First(HANDLE,LPPROCESSENTRY32W item){wcscpy_s(item->szExeFile,L"private-archive-fixture.exe");item->th32ProcessID=GetCurrentProcessId();return TRUE;}
static BOOL WINAPI Next(HANDLE,LPPROCESSENTRY32W){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
static void ProgressShow(HWND,const std::wstring&,int){}
static void ProgressHide(){}
static BOOL WINAPI SavePicker(OPENFILENAMEW*);
static LRESULT CALLBACK BackgroundCbt(int code,WPARAM w,LPARAM l){
 if(code==HCBT_CREATEWND){auto* created=reinterpret_cast<CBT_CREATEWNDW*>(l);created->lpcs->style&=~WS_VISIBLE;created->lpcs->dwExStyle|=WS_EX_NOACTIVATE;}
 if(code==HCBT_ACTIVATE||code==HCBT_SETFOCUS)return 1;
 return CallNextHookEx(nullptr,code,w,l);
}
#define ShowWindow NoShow
#define SetFocus NoFocus
#define SetForegroundWindow NoForeground
#define SetActiveWindow NoFocus
#define BringWindowToTop NoRaise
#define SetWindowPos NoPosition
#define InvalidateRect NoPaint
#define RedrawWindow NoRedraw
#define MessageBeep NoBeep
#define OpenClipboard NoClipboard
#define ShellExecuteW NoShell
#define ShellExecuteExW NoShellEx
#define CreateProcessW NoProcess
#define DialogBoxParamW NoDialog
#define DialogBoxIndirectParamW NoIndirectDialog
#define SHGetValueW NoSystemValue
#define CreateToolhelp32Snapshot Snapshot
#define Process32FirstW First
#define Process32NextW Next
#define ProgressUI_Show ProgressShow
#define ProgressUI_Hide ProgressHide
#define GetSaveFileNameW SavePicker
#define CreateEventW PrivateEvent
#define MsgWaitForMultipleObjects PrivateWait
#ifndef GetFileAttributesW
static DWORD WINAPI ObserveRecoveryAttributes(LPCWSTR path);
#define GetFileAttributesW ObserveRecoveryAttributes
#define CTSPACES_ARCHIVE_UI_OWNS_ATTRIBUTES_SHIM
#endif
#define CTSPACES_INSTALLER_TEST_HOOKS
#define CTSPACES_RESTORE_GUI_TEST_HOOKS
#define wWinMain UnusedArchiveApplicationEntry
#ifdef CTSPACES_ARCHIVE_UI_PRODUCTION_SOURCE
#include CTSPACES_ARCHIVE_UI_PRODUCTION_SOURCE
#elif defined(CTSPACES_ARCHIVE_UI_BEFORE)
#include "../build/app-audit-round10-2026-10-15/archive-ui/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef SetActiveWindow
#ifdef CTSPACES_ARCHIVE_UI_OWNS_ATTRIBUTES_SHIM
#undef GetFileAttributesW
#endif

static unsigned checks=0,failures=0,scenarios=0,pickerCalls=0,backupPickerCalls=0,stageCalls=0,notices=0;
static unsigned mode=0;
static constexpr unsigned kRecoveryCollisionMode = 9;
static constexpr unsigned kMissingSitesMode = 10;
static constexpr unsigned kRetainedStageMode = 11;
static constexpr unsigned kRestoredIconMode = 12;
static constexpr unsigned kNotesPickerCancelMode = 13;
static constexpr unsigned kNotesConfirmCancelMode = 14;
static constexpr unsigned kNotesCommitMode = 15;
static constexpr unsigned kNotesShutdownQueryMode = 16;
static constexpr unsigned kNotesSaveRefusalMode = 17;
static constexpr unsigned kPostStageBrowserMode = 18;
static constexpr unsigned kUnsupportedArchiveMode = 19;
static constexpr unsigned kBrowserDuringConfirmationMode = 20;
static std::optional<ScopedKernelHandle> notesSaveLease;
static bool armPostStageRefusal = false;
static bool pickUnsupportedArchive = false;
static unsigned unsupportedArchiveCommitCalls = 0;
static bool armBrowserDuringConfirmation = false;
static unsigned browserConfirmationCalls = 0;
static DWORD notesSaveLeaseError = ERROR_SUCCESS;
static unsigned postStageCommitCalls = 0;
static bool notesOpenAfterStage = false;
static bool shutdownQueryBusy = false, shutdownQueryRefused = false;
static constexpr UINT kShutdownQueryProbe = WM_APP + 717;
static constexpr wchar_t kStagedManifestName[] = L"ctSpacesBackup.manifest";
static HANDLE stagedManifestLease = INVALID_HANDLE_VALUE;
static fs::path observedRecovery;
static bool injected=false,busyDuringPicker=false,busyDuringStage=false,reentrantUiEnabled=false;
static bool exitDeferred=false;
static constexpr UINT kCommitPumpProbe=WM_APP+716;
static DWORD ownerThread=0,commitThread=0;
static HANDLE commitPumped=nullptr;
static bool commitProbeHandled=false,commitBusy=false,commitBackupBlocked=false;
static bool commitCloseBlocked=false,commitExitBlocked=false,commitExitDeferred=false;
static ProfileExitPayload* commitDeferredPayload=nullptr;
static unsigned commitDeferredHandled=0;
static std::wstring lastNotice;
static CtBackup::Result lastCommitResult;
static CtBackup::StagedRestore lastStagedRestore;
static void CaptureLifecycleStage(const CtBackup::StagedRestore&);
static void CreateCompetingRecovery();
static fs::path fixture;
static void Check(bool value,const char*message){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';}
HRESULT CtBackupTestCreateGuid(GUID* identity){return CoCreateGuid(identity);}
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR text,LPCWSTR,UINT flags){
 ++notices;lastNotice=text?text:L"";
 if (!(flags & MB_YESNO)) return IDOK;
 if (mode == kBrowserDuringConfirmationMode && lastNotice.starts_with(L"Restore this validated")) {
  ++browserConfirmationCalls;
  if (armBrowserDuringConfirmation) {
   const ClientBrowserKey key{L"Alpha", BrowserKind::Edge};
   const auto profile = g_sDataDir / L"Sites/Alpha";
   if (!g_activeProfiles.emplace(key, ActiveProfileInfo{GetCurrentProcessId(), {}, profile,
       ProfileType::Standard, {}, {}}).second)
    throw std::runtime_error("Could not model the private standard browser during confirmation");
  }
 }
 return mode == kNotesConfirmCancelMode && lastNotice.starts_with(L"Restore this validated") ? IDNO : IDYES;
}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path&,std::wstring&){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path&){}
static BOOL WINAPI SavePicker(OPENFILENAMEW*){++backupPickerCalls;return FALSE;}
BOOL CtSpacesRestoreGuiTestPickArchive(OPENFILENAMEW *picker){
 ++pickerCalls;busyDuringPicker=g_bClientNotesBusy.load();
 if (mode == kUnsupportedArchiveMode || mode == kBrowserDuringConfirmationMode)
  return wcscpy_s(picker->lpstrFile, picker->nMaxFile,
      (g_sDataDir / (pickUnsupportedArchive ? L"private.txt" : L"private.7z")).c_str()) == 0;
 if(!injected&&(mode==1||mode==2)){
  injected=true;
  if(mode==1)GuiProfRestoreAll();else WndProc(g_hGui,WM_COMMAND,IDM_CTX_EXPORT_ALL,0);
  reentrantUiEnabled=g_bUiEnabled;
 }
 if((mode>=3&&mode<=kRestoredIconMode)||mode==kNotesConfirmCancelMode||
     mode==kNotesCommitMode||mode==kNotesShutdownQueryMode||mode==kNotesSaveRefusalMode||
     mode==kPostStageBrowserMode)
  return wcscpy_s(picker->lpstrFile,picker->nMaxFile,(g_sDataDir/L"private.7z").c_str())==0;
 return FALSE;
}
std::wstring CtSpacesRestoreGuiTestStageSuffix(unsigned attempt){return L"private-"+std::to_wstring(attempt);}
CtBackup::Result CtSpacesRestoreGuiTestRunStage(const std::function<CtBackup::Result(_7zUiCtx&)>&operation){
 ++stageCalls;busyDuringStage=g_bClientNotesBusy.load();
 if (mode == kNotesShutdownQueryMode) {
  if (!PostMessageW(g_hGui, kShutdownQueryProbe, 0, 0))
   throw std::runtime_error("Private shutdown query could not be queued");
  return RunArchiveTaskWithProgress(L"Private shutdown query", [](_7zUiCtx&) {
   Sleep(50);
   return CtBackup::Result{E_ABORT, L"Private staged restore cancellation"};
  });
 }
 if((mode>=4&&mode<=kRestoredIconMode)||mode==kNotesCommitMode||mode==kNotesSaveRefusalMode||
    mode==kPostStageBrowserMode||mode==kUnsupportedArchiveMode||mode==kBrowserDuringConfirmationMode)
  return RunArchiveTaskWithProgress(L"Private verified restore",operation);
 WndProc(g_hGui,WM_APP_PROFILE_EXITED,0,reinterpret_cast<LPARAM>(new ProfileExitPayload{0x7ffff713,L"Stale private session",BrowserKind::Edge,ProfileType::Standard,false,false}));
 exitDeferred=g_notesDeferredExits.size()==1;
 // The production task runner pumps a queued launcher command while the inert
 // worker waits. No archive extraction or presentation is executed in this case.
 ::PostMessageW(g_hGui,WM_COMMAND,IDM_CTX_EXPORT_ALL,0);
 return RunArchiveTaskWithProgress(L"Private cancellation",[](_7zUiCtx&){Sleep(50);return CtBackup::Result{E_ABORT,L"Private cancellation"};});
}
void CtSpacesRestoreGuiTestAfterStage(CtBackup::StagedRestore& staged) {
 if (mode == kNotesCommitMode)
  notesOpenAfterStage = g_hNotesWindow && IsWindow(g_hNotesWindow);
 if ((mode == kNotesSaveRefusalMode || mode == kPostStageBrowserMode) && armPostStageRefusal) {
  lastStagedRestore = staged;
  CaptureLifecycleStage(staged);
 }
 if (mode == kPostStageBrowserMode && armPostStageRefusal) {
  const ClientBrowserKey key{L"Alpha", BrowserKind::Edge};
  const auto profile = g_sDataDir / L"Sites/Alpha";
  if (!g_activeProfiles.emplace(key, ActiveProfileInfo{GetCurrentProcessId(), {}, profile,
      ProfileType::Standard, {}, {}}).second)
   throw std::runtime_error("Could not model the private standard browser becoming tracked");
 }
 if (mode == kNotesSaveRefusalMode && armPostStageRefusal) {
  const auto notebook = g_sDataDir / L"Sites/Alpha" / client_notes::kNotebookFileName;
  notesSaveLease.emplace(::CreateFileW(notebook.c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  if (!*notesSaveLease) throw std::runtime_error("Could not acquire the private notebook sharing lease");
  const ScopedKernelHandle independentDelete(::CreateFileW(notebook.c_str(), DELETE,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
      FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
  notesSaveLeaseError = independentDelete ? ERROR_SUCCESS : GetLastError();
 }
 if ((mode >= 5 && mode <= 7) || mode == kRecoveryCollisionMode || mode == kMissingSitesMode || mode == kRetainedStageMode) {
  lastStagedRestore = staged;
  CaptureLifecycleStage(staged);
 }
 if (mode == kRetainedStageMode) {
  const auto manifest = staged.stagingRoot / kStagedManifestName;
  // Verification can still read the exact staged file. Cleanup cannot open
  // it for DELETE while this independent reader is present.
  stagedManifestLease = CreateFileW(manifest.c_str(), GENERIC_READ, FILE_SHARE_READ,
      nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
 }
}
CtBackup::Result CtSpacesRestoreGuiTestRunCommit(const std::function<CtBackup::Result(_7zUiCtx&)>&operation){
 if (mode == kNotesSaveRefusalMode || mode == kPostStageBrowserMode) ++postStageCommitCalls;
 if (mode == kUnsupportedArchiveMode || mode == kBrowserDuringConfirmationMode)
  ++unsupportedArchiveCommitCalls;
 return RunArchiveTaskWithProgress(L"Private verified restore commit",[&](_7zUiCtx& context){
  // The production failure hook is thread-local, so arm it on the actual
  // commit worker. The second rename fails after old Sites moves to recovery.
  if(mode==5||mode==6)CtBackup::FailRestoreRenameForTest(2);
  lastCommitResult=operation(context);
  if(mode==kMissingSitesMode&&lastCommitResult.ok())CreateCompetingRecovery();
  return lastCommitResult;
 });
}
void CtSpacesRestoreGuiTestBeforeCommitGuard(){
 if(mode==kRecoveryCollisionMode)CreateCompetingRecovery();
 if(mode<4||mode>7)return;
 commitThread=GetCurrentThreadId();
 if(!PostMessageW(g_hGui,kCommitPumpProbe,0,0))throw std::runtime_error("Commit pump probe could not be queued");
 // A bounded wait on the worker proves the real owner pump remains available;
 // the before implementation is on this owner thread and cannot pump here.
 if(commitThread!=ownerThread && WaitForSingleObject(commitPumped,2000)!=WAIT_OBJECT_0)
  throw std::runtime_error("Restore commit did not keep its owner pump responsive");
}
static LRESULT CALLBACK HiddenProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
 if (m == kShutdownQueryProbe) {
  shutdownQueryBusy = mode == kNotesShutdownQueryMode && g_bClientNotesBusy.load() &&
      g_bArchiveTaskInProgress && g_hNotesWindow && IsWindow(g_hNotesWindow);
  shutdownQueryRefused = WndProc(w, WM_QUERYENDSESSION, 0, ENDSESSION_CLOSEAPP) == FALSE;
  return 0;
 }
 if(m==kCommitPumpProbe){
  commitProbeHandled=GetCurrentThreadId()==ownerThread;
  commitBusy=g_bArchiveTaskInProgress&&g_bClientNotesBusy.load()&&!IsWindowEnabled(g_hGui);
  const auto previous=backupPickerCalls;
  if(commitBusy){
   WndProc(w,WM_COMMAND,IDM_CTX_EXPORT_ALL,0);
   commitBackupBlocked=backupPickerCalls==previous&&!g_bUiEnabled;
   if(mode==6||mode==7){
    // Submit lifecycle actions only inside the verified busy interval. Never
    // run a queued action after the outer restore could have finished.
    WndProc(w,WM_CLOSE,0,0);
    commitCloseBlocked=IsWindow(w)&&g_hGui==w&&g_bClientNotesBusy.load()&&g_bArchiveTaskInProgress;
    WndProc(w,WM_APP_EXIT_CTSPACES,0,0);
    commitExitBlocked=IsWindow(w)&&g_hGui==w&&g_bClientNotesBusy.load()&&g_bArchiveTaskInProgress;
    auto payload=std::make_unique<ProfileExitPayload>(ProfileExitPayload{0x7ffff714,L"Stale commit fixture",BrowserKind::Edge,ProfileType::Standard,false,false});
    commitDeferredPayload=payload.get();
    WndProc(w,WM_APP_PROFILE_EXITED,0,reinterpret_cast<LPARAM>(payload.release()));
    commitExitDeferred=g_notesDeferredExits.size()==1&&g_notesDeferredExits.front().get()==commitDeferredPayload;
    PostQuitMessage(static_cast<int>(70+mode));
   }
  }
  if(commitPumped)SetEvent(commitPumped);
  return 0;
 }
 if(m==WM_APP_PROFILE_EXITED&&reinterpret_cast<ProfileExitPayload*>(lp)==commitDeferredPayload&&!g_bClientNotesBusy.load())++commitDeferredHandled;
 if(m==WM_COMMAND||m==WM_APP_PROFILE_EXITED)return WndProc(w,m,wp,lp);return DefWindowProcW(w,m,wp,lp);
}
static void CALLBACK Event(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId())return;if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW)++nativeShows;else if(event==EVENT_OBJECT_FOCUS)++nativeFocus;else if(event==EVENT_SYSTEM_FOREGROUND)++nativeForeground;}
static void Audit(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)continue;if((message.message>=WM_KEYFIRST&&message.message<=WM_KEYLAST)||(message.message>=WM_MOUSEFIRST&&message.message<=WM_MOUSELAST))throw std::runtime_error("Device input excluded");DispatchMessageW(&message);}unsigned visible=0;EnumWindows([](HWND w,LPARAM p)->BOOL{DWORD pid=0;if(GetWindowThreadProcessId(w,&pid)&&pid==GetCurrentProcessId()&&IsWindowVisible(w))++*reinterpret_cast<unsigned*>(p);return TRUE;},reinterpret_cast<LPARAM>(&visible));GUITHREADINFO state{sizeof(state)};if(!GetGUIThreadInfo(GetCurrentThreadId(),&state)||visible||state.hwndFocus||state.hwndActive||state.hwndCapture||nativeShows||nativeFocus||nativeForeground||forbidden)throw std::runtime_error("Background archive safety failed");}
static void Seed(unsigned next,bool existingSites=true){++scenarios;mode=next;pickerCalls=backupPickerCalls=stageCalls=0;injected=busyDuringPicker=busyDuringStage=reentrantUiEnabled=exitDeferred=failEvent=failWait=false;commitProbeHandled=commitBusy=commitBackupBlocked=commitCloseBlocked=commitExitBlocked=commitExitDeferred=false;commitThread=0;commitDeferredPayload=nullptr;commitDeferredHandled=0;lastNotice.clear();lastCommitResult={};lastStagedRestore={};observedRecovery.clear();g_sDataDir=fixture/std::to_wstring(scenarios);g_sConfigPath=g_sDataDir/L"config.ini";if(existingSites){fs::create_directories(g_sDataDir/L"Sites/Alpha");std::ofstream(g_sDataDir/L"Sites/Alpha/sentinel")<<"PRIVATE ORIGINAL";}else fs::create_directories(g_sDataDir);g_bUiEnabled=true;g_bClientNotesBusy=false;g_bArchiveTaskInProgress=false;g_activeProfiles.clear();g_notesDeferredExits.clear();EnableWindow(g_hGui,TRUE);}

struct PrivateEntry {
 bool directory=false;DWORD volume=0,high=0,low=0;std::string bytes;
 bool operator==(const PrivateEntry&)const=default;
};
using PrivateTree=std::map<std::wstring,PrivateEntry>;
static std::string PrivateBytes(const fs::path& path){std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Private ordinary fixture read failed");return {std::istreambuf_iterator<char>(input),{}};}
static PrivateEntry PrivateObject(const fs::path& path){
 const DWORD attributes=GetFileAttributesW(path.c_str());
 if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Private fixture reparse/unavailable object refused");
 PrivateEntry entry;entry.directory=(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
 HANDLE object=::CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|(entry.directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr);
 if(object==INVALID_HANDLE_VALUE)throw std::runtime_error("Private object could not be bound");
 BY_HANDLE_FILE_INFORMATION info{};const bool good=GetFileInformationByHandle(object,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&GetFileType(object)==FILE_TYPE_DISK&&(entry.directory||info.nNumberOfLinks==1);CloseHandle(object);
 if(!good)throw std::runtime_error("Private object identity is unsafe");
 entry.volume=info.dwVolumeSerialNumber;entry.high=info.nFileIndexHigh;entry.low=info.nFileIndexLow;if(!entry.directory)entry.bytes=PrivateBytes(path);return entry;
}
static PrivateTree PrivateSnapshot(const fs::path& root){
 PrivateTree tree;if(!fs::exists(root))return tree;tree.emplace(L"",PrivateObject(root));
 for(const auto& object:fs::recursive_directory_iterator(root)){const auto entry=PrivateObject(object.path());tree.emplace(object.path().lexically_relative(root).native(),entry);}return tree;
}
static PrivateTree stagedBefore;
static void CaptureLifecycleStage(const CtBackup::StagedRestore& staged){stagedBefore=PrivateSnapshot(staged.stagingRoot);}
static bool PrivateRecoveryName(const fs::path& path){const auto name=path.filename().native();return name.starts_with(L"Sites_PreRestore_")||(name.size()==5&&name[0]==L'R'&&std::all_of(name.begin()+1,name.end(),[](wchar_t c){return (c>=L'0'&&c<=L'9')||(c>=L'A'&&c<=L'F');}));}
static std::vector<fs::path> PrivateRecoveries(){std::vector<fs::path> paths;for(const auto& entry:fs::directory_iterator(g_sDataDir))if(PrivateRecoveryName(entry.path()))paths.push_back(entry.path());return paths;}

#ifdef CTSPACES_ARCHIVE_UI_OWNS_ATTRIBUTES_SHIM
static DWORD WINAPI ObserveRecoveryAttributes(LPCWSTR name) {
 const DWORD attributes = ::GetFileAttributesW(name);
 const DWORD error = GetLastError();
 if (name && (mode == kRecoveryCollisionMode || mode == kMissingSitesMode) && observedRecovery.empty() &&
     attributes == INVALID_FILE_ATTRIBUTES && error == ERROR_FILE_NOT_FOUND) {
  const fs::path path(name);
  if (path.parent_path() == g_sDataDir && PrivateRecoveryName(path)) observedRecovery = path;
 }
 SetLastError(error);
 return attributes;
}
#undef CTSPACES_ARCHIVE_UI_OWNS_ATTRIBUTES_SHIM
#endif

static PrivateTree competingRecovery;
static void CreateCompetingRecovery() {
 if (observedRecovery.parent_path() != g_sDataDir || !PrivateRecoveryName(observedRecovery) ||
     !CreateDirectoryW(observedRecovery.c_str(), nullptr))
  throw std::runtime_error("Could not create a foreign folder at the observed vacant recovery path");
 std::ofstream foreign(observedRecovery / L"foreign-state", std::ios::binary);
 foreign << "PRIVATE FOREIGN RECOVERY OCCUPANT";
 foreign.close();
 if (!foreign) throw std::runtime_error("Could not write the foreign recovery fixture");
 competingRecovery = PrivateSnapshot(observedRecovery);
}

static void RecoveryNoticeOwnership(bool collisionBeforeCommit) {
 Seed(collisionBeforeCommit ? kRecoveryCollisionMode : kMissingSitesMode, collisionBeforeCommit);
 competingRecovery.clear();
 const auto live = g_sDataDir / L"Sites";
 const auto source = g_sDataDir / L"source" / L"Sites";
 const auto archive = g_sDataDir / L"private.7z";
 const bool pathFits = CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(live, L"Restored");
 Check(pathFits, "Recovery notice fixture fits the managed client path budget");
 if (!pathFits) throw std::runtime_error("Recovery notice fixture path is too long");
 fs::create_directories(source / L"Restored");
 std::ofstream(source / L"Restored" / L"sentinel") << "PRIVATE RESTORED PROFILE";
 const auto created = CtBackup::CreateValidated7zBackup(source, archive, CTSPACES_VERSION_TEXT, nullptr, nullptr);
 const bool archiveValid = created.ok() && _7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK;
 Check(archiveValid, "The notice test uses an ordinary CRC-valid backup");
 if (!archiveValid) throw std::runtime_error("Recovery notice fixture backup could not be verified");
 const auto original = PrivateSnapshot(live);
 const auto sourceBefore = PrivateSnapshot(source);
 const auto archiveBefore = PrivateObject(archive);

 GuiProfRestoreAll();

 Check(pickerCalls == 1 && stageCalls == 1 && !lastStagedRestore.stagingRoot.empty(),
       "Restore reaches a real verified stage before the competing recovery appears");
 Check(!competingRecovery.empty() && PrivateSnapshot(observedRecovery) == competingRecovery,
       "Restore preserves every byte and object identity of the competing recovery folder");
 if (collisionBeforeCommit) {
  Check(lastCommitResult.code == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS) &&
        lastNotice.find(L"Error: 0x800700B7") != std::wstring::npos,
        "A recovery collision inside the commit guard gives an accurate refusal");
  Check(PrivateSnapshot(live) == original && !stagedBefore.empty() &&
        PrivateSnapshot(lastStagedRestore.stagingRoot) == stagedBefore,
        "Recovery collision preserves the original collection and its verified staging replacement");
 } else {
  Check(original.empty() && lastCommitResult.ok() && lastNotice.find(L"Restore complete") != std::wstring::npos,
        "Restore into initially missing Sites reports actual completion");
  const auto relativeFile = fs::path(L"Restored") / L"sentinel";
  const auto stagedFile = stagedBefore.find((fs::path(L"Sites") / relativeFile).native());
  Check(stagedFile != stagedBefore.end() && fs::is_regular_file(live / relativeFile) &&
        PrivateObject(live / relativeFile) == stagedFile->second && !fs::exists(lastStagedRestore.stagingRoot),
        "Restore with initially missing Sites installs the verified file and removes staging");
 }
 const auto recoveryLabel = collisionBeforeCommit ? L"Retained recovery location:" : L"Your previous profile data was kept here:";
 const bool falselyNamed = !observedRecovery.empty() && lastNotice.find(recoveryLabel) != std::wstring::npos &&
     lastNotice.find(observedRecovery.wstring()) != std::wstring::npos;
 Check(lastNotice.find(recoveryLabel) == std::wstring::npos && !observedRecovery.empty() &&
       lastNotice.find(observedRecovery.wstring()) == std::wstring::npos,
       "The notice does not attribute the foreign folder to an original collection");
 Check(PrivateSnapshot(source) == sourceBefore && PrivateObject(archive) == archiveBefore,
       "The restore notice scenario preserves the original backup source and archive");
 Check(g_bUiEnabled && !g_bClientNotesBusy.load() && !g_bArchiveTaskInProgress && IsWindowEnabled(g_hGui),
       "The restore notice scenario releases the owner and both busy states");
 std::cout << "FOREIGN_RECOVERY_NOTICE case=" << (collisionBeforeCommit ? "precommit-collision" : "missing-sites")
           << " occupant=" << !competingRecovery.empty() << " falselyNamed=" << falselyNamed << '\n';
 Audit();
}

static void RetainedStageNotice() {
 Seed(kRetainedStageMode);
 struct ManifestLeaseCleanup {
  ~ManifestLeaseCleanup() {
   if (stagedManifestLease != INVALID_HANDLE_VALUE) CloseHandle(stagedManifestLease);
   stagedManifestLease = INVALID_HANDLE_VALUE;
  }
 } leaseCleanup;
 const auto live = g_sDataDir / L"Sites";
 const auto source = g_sDataDir / L"source" / L"Sites";
 const auto archive = g_sDataDir / L"private.7z";
 const bool pathFits = CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(live, L"Restored");
 Check(pathFits, "Retained staging fixture fits the managed client path budget");
 if (!pathFits) throw std::runtime_error("Retained staging fixture path is too long");
 fs::create_directories(source / L"Restored");
 std::ofstream(source / L"Restored" / L"sentinel") << "PRIVATE RESTORED PROFILE";
 const auto created = CtBackup::CreateValidated7zBackup(source, archive, CTSPACES_VERSION_TEXT, nullptr, nullptr);
 const bool archiveValid = created.ok() && _7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK;
 Check(archiveValid, "Retained staging notice uses an ordinary CRC-valid backup");
 if (!archiveValid) throw std::runtime_error("Retained staging fixture backup could not be verified");
 const auto original = PrivateSnapshot(live);
 const auto sourceBefore = PrivateSnapshot(source);
 const auto archiveBefore = PrivateObject(archive);

 GuiProfRestoreAll();

 const auto &stage = lastStagedRestore.stagingRoot;
 const bool leaseHeld = stagedManifestLease != INVALID_HANDLE_VALUE;
 Check(pickerCalls == 1 && stageCalls == 1 && !stage.empty() && leaseHeld,
       "A real read lease stays open on the exact manifest from verified staging");
 DWORD cleanupOpenError = ERROR_SUCCESS;
 if (leaseHeld && !stage.empty()) {
  const auto manifest = stage / kStagedManifestName;
  const HANDLE cleanupProbe = CreateFileW(manifest.c_str(), DELETE | FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (cleanupProbe == INVALID_HANDLE_VALUE) cleanupOpenError = GetLastError();
  else CloseHandle(cleanupProbe);
 }
 Check(cleanupOpenError == ERROR_SHARING_VIOLATION,
       "The same native DELETE-access open used by staging cleanup is refused by Windows");
 Check(lastCommitResult.ok() && lastNotice.find(L"Restore complete.") != std::wstring::npos &&
       !lastCommitResult.details.empty(),
       "Restore reports verified completion and a cleanup warning despite the sharing refusal");
 const auto relativeFile = fs::path(L"Restored") / L"sentinel";
 const auto stagedFile = stagedBefore.find((fs::path(L"Sites") / relativeFile).native());
 Check(stagedFile != stagedBefore.end() && fs::is_regular_file(live / relativeFile) &&
       PrivateObject(live / relativeFile) == stagedFile->second && !fs::exists(live / L"Alpha"),
       "Cleanup refusal preserves the exact verified replacement in live Sites");
 const auto recoveries = PrivateRecoveries();
 Check(recoveries.size() == 1 && PrivateSnapshot(recoveries.front()) == original &&
       lastNotice.find(recoveries.front().wstring()) != std::wstring::npos,
       "The exact prior collection remains independently recoverable and its path is reported");
 const auto retainedStage = PrivateSnapshot(stage);
 const auto originalRoot = stagedBefore.find(L"");
 const auto originalManifest = stagedBefore.find(kStagedManifestName);
 const bool exactRetained = retainedStage.size() == 2 && retainedStage.contains(L"") &&
     retainedStage.contains(kStagedManifestName) && originalRoot != stagedBefore.end() &&
     originalManifest != stagedBefore.end() && retainedStage.at(L"") == originalRoot->second &&
     retainedStage.at(kStagedManifestName) == originalManifest->second;
 Check(exactRetained, "The retained staging root and manifest keep their exact identities and bytes");
 const bool reported = !stage.empty() && lastCommitResult.details.find(stage.wstring()) != std::wstring::npos &&
     lastNotice.find(stage.wstring()) != std::wstring::npos;
 Check(reported, "The successful restore warning reports the exact retained staging path");
 Check(PrivateSnapshot(source) == sourceBefore && PrivateObject(archive) == archiveBefore,
       "Restore and refused cleanup preserve the original backup source and archive");
 Check(g_bUiEnabled && !g_bClientNotesBusy.load() && !g_bArchiveTaskInProgress && IsWindowEnabled(g_hGui),
       "Restore with refused staging cleanup releases its owner and both busy states");
 std::cout << "RESTORE_CLEANUP_NOTICE retained=" << exactRetained << " sharing=" << cleanupOpenError
           << " reported=" << reported << '\n';
 Audit();
}

static constexpr int kPrivateIconPixels = 16;
static void WritePrivateIcon(const fs::path &path, DWORD pixel, const FILETIME &modified) {
 fs::create_directories(path.parent_path());
 std::array<DWORD, kPrivateIconPixels * kPrivateIconPixels> colors;
 colors.fill(pixel);
 // The one-bit AND mask has four bytes per row after DIB alignment.
 const std::array<BYTE, 4 * kPrivateIconPixels> mask{};
 BITMAPINFOHEADER image{};
 image.biSize = sizeof(image);
 image.biWidth = kPrivateIconPixels;
 image.biHeight = 2 * kPrivateIconPixels;
 image.biPlanes = 1;
 image.biBitCount = 32;
 image.biSizeImage = sizeof(colors);
 const ICONDIR directory{0, 1, 1, {{kPrivateIconPixels, kPrivateIconPixels, 0, 0, 1, 32,
     sizeof(image) + sizeof(colors) + sizeof(mask), sizeof(ICONDIR)}}};
 std::ofstream output(path, std::ios::binary);
 output.write(reinterpret_cast<const char *>(&directory), sizeof(directory));
 output.write(reinterpret_cast<const char *>(&image), sizeof(image));
 output.write(reinterpret_cast<const char *>(colors.data()), sizeof(colors));
 output.write(reinterpret_cast<const char *>(mask.data()), sizeof(mask));
 output.close();
 if (!output) throw std::runtime_error("Private solid-color ICO could not be written");
 const HANDLE attributes = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ,
     nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
 if (attributes == INVALID_HANDLE_VALUE) throw std::runtime_error("Private ICO timestamp could not be bound");
 const BOOL timestamped = SetFileTime(attributes, nullptr, nullptr, &modified);
 CloseHandle(attributes);
 if (!timestamped) throw std::runtime_error("Private ICO timestamp could not be set");
}

static DWORD PrivateIconPixel(HICON icon) {
 const HBITMAP bitmap = IconToMenuBitmap(icon, kPrivateIconPixels, kPrivateIconPixels);
 if (!bitmap) return 0;
 GdiFlush();
 DIBSECTION dib{};
 DWORD pixel = 0;
 if (GetObjectW(bitmap, sizeof(dib), &dib) == sizeof(dib) && dib.dsBm.bmBits)
  memcpy(&pixel, dib.dsBm.bmBits, sizeof(pixel));
 DeleteObject(bitmap);
 return pixel & 0x00FFFFFF;
}

static DWORD PrivateIconFilePixel(const fs::path &path) {
 const HICON icon = LoadIconFromIcoBestDownscale(path, kPrivateIconPixels);
 const DWORD pixel = PrivateIconPixel(icon);
 if (icon) DestroyIcon(icon);
 return pixel;
}

static void RestoredClientIconCache() {
 Seed(kRestoredIconMode);
 const std::wstring client = L"Alpha";
 ClearClientIconCache(client);
 struct ClientCacheCleanup {
  const std::wstring &name;
  ~ClientCacheCleanup() { ClearClientIconCache(name); }
 } cacheCleanup{client};
 const auto live = g_sDataDir / L"Sites";
 const auto source = g_sDataDir / L"source" / L"Sites";
 const auto archive = g_sDataDir / L"private.7z";
 const auto liveIcon = live / client / L"client.ico";
 const auto incomingIcon = source / client / L"client.ico";
 const bool pathFits = CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(live, client);
 Check(pathFits, "The restored client icon fixture fits the managed client path budget");
 if (!pathFits) throw std::runtime_error("Restored icon fixture path is too long");
 constexpr DWORD oldPixel = 0x00CC3311;
 constexpr DWORD newPixel = 0x001144CC;
 const FILETIME modified{0x10000000, 0x01DA0000};
 WritePrivateIcon(liveIcon, 0xFF000000 | oldPixel, modified);
 WritePrivateIcon(incomingIcon, 0xFF000000 | newPixel, modified);
 const auto iconLength = fs::file_size(liveIcon);
 const auto iconStamp = fs::last_write_time(liveIcon);
 const auto incomingBytes = PrivateBytes(incomingIcon);
 Check(iconLength == fs::file_size(incomingIcon) && iconStamp == fs::last_write_time(incomingIcon) &&
       PrivateBytes(liveIcon) != incomingBytes,
       "Ordinary current and incoming ICO files differ but share the exact length and write timestamp");
 Check(PrivateIconFilePixel(liveIcon) == oldPixel && PrivateIconFilePixel(incomingIcon) == newPixel,
       "Both ICOs decode through the production loader to distinct opaque colors");
 const auto oldCachedPixel = PrivateIconPixel(GetClientIconForUi(client, kPrivateIconPixels));
 Check(oldCachedPixel == oldPixel, "The launcher icon cache is warmed with the original client's actual pixels");
 const auto created = CtBackup::CreateValidated7zBackup(source, archive, CTSPACES_VERSION_TEXT, nullptr, nullptr);
 const bool archiveValid = created.ok() && _7zTest7z(archive.c_str(), nullptr, nullptr) == S_OK;
 Check(archiveValid, "The incoming icon is carried by an ordinary CRC-valid backup");
 if (!archiveValid) throw std::runtime_error("Restored icon fixture backup could not be verified");
 const auto original = PrivateSnapshot(live);
 const auto sourceBefore = PrivateSnapshot(source);
 const auto archiveBefore = PrivateObject(archive);

 GuiProfRestoreAll();

 Check(pickerCalls == 1 && stageCalls == 1 && lastCommitResult.ok() &&
       lastNotice.find(L"Restore complete.") != std::wstring::npos,
       "The native archive worker verifies and installs the ordinary restored collection");
 const auto recoveries = PrivateRecoveries();
 Check(recoveries.size() == 1 && PrivateSnapshot(recoveries.front()) == original,
       "The original client and its original icon remain exact in recovery");
 const DWORD freshPixel = PrivateIconFilePixel(liveIcon);
 const bool installedIcon = fs::is_regular_file(liveIcon) && PrivateBytes(liveIcon) == incomingBytes &&
     fs::file_size(liveIcon) == iconLength && fs::last_write_time(liveIcon) == iconStamp && freshPixel == newPixel;
 Check(installedIcon, "The restored icon matches the incoming pixels and original cache-key size and time");
 const DWORD cachedPixel = PrivateIconPixel(GetClientIconForUi(client, kPrivateIconPixels));
 const bool cacheFresh = cachedPixel == freshPixel && cachedPixel == newPixel;
 Check(cacheFresh, "The launcher icon cache displays the restored client's new pixels");
 Check(PrivateSnapshot(source) == sourceBefore && PrivateObject(archive) == archiveBefore,
       "The icon restore preserves the original backup source and archive");
 Check(g_bUiEnabled && !g_bClientNotesBusy.load() && !g_bArchiveTaskInProgress && IsWindowEnabled(g_hGui),
       "The icon restore releases its owner and both busy states");
 std::cout << "RESTORED_ICON_CACHE installed=" << installedIcon << " cacheFresh=" << cacheFresh << '\n';
 Audit();
}

static void TrackedProfileRestoreScope(ProfileType type, const wchar_t* name) {
 Seed(8);
 const auto live = g_sDataDir / L"Sites";
 const auto source = g_sDataDir / L"source" / L"Sites";
 const auto archive = g_sDataDir / L"private.7z";
 const bool standard = type == ProfileType::Standard;
 const auto profile = (standard ? live : g_sDataDir) / name;
 const ClientBrowserKey key{name, BrowserKind::Edge};
 const DWORD fixturePid = GetCurrentProcessId();
 const bool pathFits = CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(live, L"Restored");
 Check(pathFits, "Tracked profile restore fixture fits the managed client path budget");
 if (!pathFits) throw std::runtime_error("Tracked profile restore fixture path is too long");

 fs::create_directories(profile);
 std::ofstream(profile / L"tracked-state") << "PRIVATE TRACKED PROFILE";
 fs::create_directories(source / L"Restored");
 std::ofstream(source / L"Restored" / L"sentinel") << "PRIVATE RESTORED PROFILE";
 const auto created = CtBackup::CreateValidated7zBackup(
     source, archive, CTSPACES_VERSION_TEXT, nullptr, nullptr);
 if (!created.ok()) throw std::runtime_error("Tracked profile restore fixture backup could not be verified");
 const auto original = PrivateSnapshot(live);
 const auto sourceBefore = PrivateSnapshot(source);
 const auto archiveBefore = PrivateObject(archive);
 const auto profileBefore = PrivateSnapshot(profile);

 // Seed the same tracking record as a successful managed launch without
 // starting a browser or requiring a real browser profile on disk.
 g_activeProfiles.emplace(key, ActiveProfileInfo{fixturePid, {}, profile, type, {}, {}});
 GuiProfRestoreAll();

 const auto recoveries = PrivateRecoveries();
 if (standard) {
  Check(pickerCalls == 0 && stageCalls == 0,
        "A tracked standard client blocks restore before archive selection");
  Check(lastNotice.find(L"ctSpaces is still tracking") != std::wstring::npos,
        "Restore explains that a tracked standard client is still open");
  Check(PrivateSnapshot(live) == original,
        "Blocked standard-client restore preserves the live collection exactly");
  Check(recoveries.empty(), "Blocked standard-client restore creates no recovery");
 } else {
  Check(pickerCalls == 1 && stageCalls == 1 && busyDuringPicker && busyDuringStage,
        "An unrelated tracked profile permits guarded selection and staging");
  Check(lastCommitResult.ok() && lastNotice.find(L"Restore complete") != std::wstring::npos,
        "Restore reports completion while an unrelated tracked profile stays open");
  const auto restoredFile = live / L"Restored" / L"sentinel";
  Check(fs::is_regular_file(restoredFile) && PrivateBytes(restoredFile) == "PRIVATE RESTORED PROFILE" &&
        !fs::exists(live / L"Alpha") && !fs::exists(g_sDataDir / L"Restore_Temp_private-0"),
        "Ordinary restore installs the verified payload and removes its staging wrapper");
  Check(recoveries.size() == 1 && PrivateSnapshot(recoveries.front()) == original,
        "Ordinary restore preserves the exact previous collection in recovery");
 }
 Check(PrivateSnapshot(profile) == profileBefore,
       "Restore leaves every tracked profile byte and object identity intact");
 const auto tracked = g_activeProfiles.find(key);
 Check(g_activeProfiles.size() == 1 && tracked != g_activeProfiles.end() &&
       tracked->second.pid == fixturePid && tracked->second.type == type &&
       tracked->second.profilePath == profile,
       "Restore leaves the original tracked profile registered");
 Check(PrivateSnapshot(source) == sourceBefore && PrivateObject(archive) == archiveBefore,
       "Restore leaves the private backup source and archive byte- and identity-exact");
 Check(g_bUiEnabled && !g_bClientNotesBusy.load() && !g_bArchiveTaskInProgress && IsWindowEnabled(g_hGui),
       "Restore releases its owner and both busy states after the tracked-profile outcome");
 std::wcout << L"TRACKED_RESTORE_SCOPE profile=" << name << L" picker=" << pickerCalls
            << L" stage=" << stageCalls << L" recoveries=" << recoveries.size() << L'\n';
 Audit();
}

static void CommitLifecycle(unsigned testMode){
 Seed(testMode);
 const bool pathFits=CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(g_sDataDir/L"Sites",L"Restored");
 Check(pathFits,"Lifecycle verified-restore fixture fits the production final client budget");if(!pathFits)throw std::runtime_error("Lifecycle fixture path precondition failed");
 commitPumped=CreateEventW(nullptr,TRUE,FALSE,nullptr);Check(commitPumped!=nullptr,"Lifecycle worker probe owns its bounded private event");if(!commitPumped)throw std::runtime_error("Lifecycle event unavailable");
 const auto source=g_sDataDir/L"source/Sites";fs::create_directories(source/L"Restored");std::ofstream(source/L"Restored/sentinel")<<"PRIVATE LIFECYCLE BACKUP";
 const auto created=CtBackup::CreateValidated7zBackup(source,g_sDataDir/L"private.7z",CTSPACES_VERSION_TEXT,nullptr,nullptr);if(!created.ok())throw std::runtime_error("Lifecycle ordinary archive creation failed");
 const auto original=PrivateSnapshot(g_sDataDir/L"Sites"),sourceBefore=PrivateSnapshot(source);const auto archiveBefore=PrivateObject(g_sDataDir/L"private.7z");
 GuiProfRestoreAll();
 Check(stageCalls==1&&busyDuringPicker&&busyDuringStage&&commitThread!=0&&commitThread!=ownerThread,"Actual lifecycle restore stages and commits on its guarded worker");
 Check(commitProbeHandled&&commitBusy&&commitBackupBlocked,"Lifecycle worker keeps its actual owner pump responsive and blocks reentrant backup");
 Check(PrivateSnapshot(source)==sourceBefore,"Lifecycle restore preserves every private backup-source byte and object identity");
 Check(PrivateObject(g_sDataDir/L"private.7z")==archiveBefore,"Lifecycle restore leaves its original archive bytes and identity unchanged");
 Check(g_bUiEnabled&&!g_bClientNotesBusy.load()&&!g_bArchiveTaskInProgress&&IsWindowEnabled(g_hGui),"Lifecycle completion restores launcher availability and releases both busy guards");
 const auto recoveries=PrivateRecoveries();
 if(testMode==5||testMode==6){
  Check(lastCommitResult.code==HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED),"Injected second native restore rename reports its exact worker failure code");
  Check(lastNotice.find(L"Focused bound restore rename failure")!=std::wstring::npos&&lastNotice.find(L"Error: 0x80070005")!=std::wstring::npos,"Actual restore error notice explains the promotion failure and preserves its code");
  Check(PrivateSnapshot(g_sDataDir/L"Sites")==original,"Failed worker promotion rolls the exact original client bytes and identities back into live Sites");
  Check(!lastStagedRestore.stagingRoot.empty()&&PrivateSnapshot(lastStagedRestore.stagingRoot)==stagedBefore&&PrivateBytes(lastStagedRestore.payloadDir/L"Restored/sentinel")=="PRIVATE LIFECYCLE BACKUP","Failed worker promotion retains exact staged replacement bytes and object identities");
  Check(recoveries.empty(),"Successful rollback leaves no displaced original collection in an orphan recovery folder");
  Check(lastNotice.find(L"Retained recovery location:")==std::wstring::npos,
        "Successful rollback does not report a retained recovery collection");
 }else{
  Check(lastCommitResult.ok()&&lastNotice.find(L"Restore complete")!=std::wstring::npos,"Successful quit-interrupted worker reports completed restore after joining");
  Check(fs::is_regular_file(g_sDataDir/L"Sites/Restored/sentinel")&&PrivateBytes(g_sDataDir/L"Sites/Restored/sentinel")=="PRIVATE LIFECYCLE BACKUP"&&!fs::exists(g_sDataDir/L"Sites/Alpha"),"Successful quit-interrupted worker installs the exact validated client bytes");
  Check(recoveries.size()==1&&PrivateSnapshot(recoveries.front())==original,"Successful quit-interrupted worker keeps exact original bytes and identities in recovery");
  Check(recoveries.size()==1&&lastNotice.find(L"Your previous profile data was kept here:")!=std::wstring::npos&&
        lastNotice.find(recoveries.front().wstring())!=std::wstring::npos,
        "Successful restore reports the exact location of the retained original collection");
  Check(!lastStagedRestore.stagingRoot.empty()&&!fs::exists(lastStagedRestore.stagingRoot)&&lastCommitResult.details.empty(),"Successful worker removes only its completed staging wrapper without a retention warning");
 }
 if(testMode==6||testMode==7){
  Check(commitCloseBlocked&&commitExitBlocked&&IsWindow(g_hGui),"Owner close and explicit Exit remain blocked throughout actual restore commit");
  Check(commitExitDeferred&&g_notesDeferredExits.empty(),"Actual commit owns a deferred browser-exit payload until the restore guard releases it");
  // Audit intentionally drains messages; validate the quit code before it can
  // consume anything, then dispatch the one owned reposted exit explicitly.
  MSG exit{};const bool exitFound=PeekMessageW(&exit,g_hGui,WM_APP_PROFILE_EXITED,WM_APP_PROFILE_EXITED,PM_REMOVE)!=FALSE;
  std::cout<<"DEFERRED_OBSERVED mode="<<testMode<<" found="<<exitFound<<" message="<<exit.message<<" handled_before="<<commitDeferredHandled<<'\n';
  Check(exitFound&&exit.message==WM_APP_PROFILE_EXITED&&reinterpret_cast<ProfileExitPayload*>(exit.lParam)==commitDeferredPayload&&commitDeferredHandled==0,"Restore guard reposts the same owned browser-exit payload exactly once after completion");
  if(exitFound&&exit.message==WM_APP_PROFILE_EXITED)DispatchMessageW(&exit);
  // A posted owned message can delay the virtual quit flag. Once that exact
  // payload is dispatched, inspect quit before Audit drains any other work.
  MSG quit{};const bool quitFound=PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)!=FALSE;
  std::cout<<"QUIT_OBSERVED mode="<<testMode<<" found="<<quitFound<<" message="<<quit.message<<" code="<<quit.wParam<<" expected="<<70+testMode<<'\n';
  Check(quitFound&&quit.message==WM_QUIT&&quit.wParam==70+testMode,"Actual restore worker preserves the queued quit and its exact exit code");
  MSG duplicate{};const bool duplicateFound=PeekMessageW(&duplicate,g_hGui,WM_APP_PROFILE_EXITED,WM_APP_PROFILE_EXITED,PM_NOREMOVE)!=FALSE;
  std::cout<<"DEFERRED_AFTER mode="<<testMode<<" handled="<<commitDeferredHandled<<" duplicate="<<duplicateFound<<" message="<<duplicate.message<<" code="<<duplicate.wParam<<'\n';
  Check(commitDeferredHandled==1&&g_notesDeferredExits.empty()&&!duplicateFound,"Deferred stale browser exit is consumed once without reentry or another pending owner");
 }
 Audit();std::cout<<"RESTORE_COMMIT_LIFECYCLE mode="<<testMode<<" worker="<<(commitThread!=ownerThread)<<" pump="<<commitProbeHandled<<" busy="<<commitBusy<<" result=0x"<<std::hex<<static_cast<unsigned long>(lastCommitResult.code)<<std::dec<<" quit="<<(testMode>=6)<<"\n";
 CloseHandle(commitPumped);commitPumped=nullptr;
}
static void RestoreNotesLifetime(unsigned testMode) {
 Seed(testMode);
 const auto sites = g_sDataDir / L"Sites";
 const auto client = sites / L"Alpha";
 fs::create_directories(client / L"Browsers");
 std::ofstream(client / L"ctSpaces-client-v2") << "ctSpaces-client-schema=2\r\n";
 if (!WritePrivateProfileStringW(L"notes_window", L"help_seen", L"1", g_sConfigPath.c_str()))
  throw std::runtime_error("Could not set private Notes fixture preferences");
 if (testMode != kNotesPickerCancelMode) {
  const auto source = g_sDataDir / L"source/Sites";
  fs::create_directories(source / L"Restored");
  std::ofstream(source / L"Restored/sentinel") << "PRIVATE RESTORED";
  const auto archive = g_sDataDir / L"private.7z";
  if (!CtBackup::CreateValidated7zBackup(source, archive, CTSPACES_VERSION_TEXT, nullptr, nullptr).ok())
   throw std::runtime_error("Could not create ordinary Notes restore archive");
 }
 ShowClientNotes(L"Alpha");
 const HWND notes = g_hNotesWindow;
 const HWND editor = GetDlgItem(notes, IDC_CLIENT_NOTES_TEXT);
 auto *state = reinterpret_cast<ClientNotesDialogState *>(GetWindowLongPtrW(notes, DWLP_USER));
 if (!notes || !editor || !state) throw std::runtime_error("Private native Notes editor did not open");
 constexpr wchar_t pending[] = L"Private editable draft with native Undo";
 SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(pending));
 MarkNotesChanged(notes, *state);
 Check(NotesWindowText(editor) == pending && SendMessageW(editor, EM_CANUNDO, 0, 0),
       "Hidden native Notes editor holds the draft and a usable Undo");
 if (NotesWindowText(editor) != pending || !SendMessageW(editor, EM_CANUNDO, 0, 0))
  throw std::runtime_error("Native Notes draft/Undo precondition failed");

 notesOpenAfterStage = false;
 shutdownQueryBusy = shutdownQueryRefused = false;
 GuiProfRestoreAll();
 if (testMode == kNotesCommitMode) {
  Check(stageCalls == 1 && lastCommitResult.ok() && !g_hNotesWindow && !IsWindow(notes),
        "Accepted verified restore closes the original Notes window before replacing Sites");
  Check(notesOpenAfterStage, "Notes stays open through staging of the accepted archive");
  Check(fs::is_regular_file(sites / L"Restored/sentinel") && !fs::exists(client),
        "Accepted restore installs the verified replacement client");
  const HMODULE richEdit = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
  if (!richEdit) throw std::runtime_error("RichEdit unavailable to inspect the recovered draft");
  bool draftInRecovery = false;
  for (const auto &entry : fs::directory_iterator(g_sDataDir)) {
   if (!PrivateRecoveryName(entry.path())) continue;
   const auto original = client_notes::ReadNotebook(entry.path() / L"Alpha");
   draftInRecovery = std::ranges::any_of(original.pages, [&](const auto &page) {
    return NotesPlain(page).find(pending) != std::wstring::npos;
   });
  }
  FreeLibrary(richEdit);
  Check(draftInRecovery, "Accepted restore durably saves the pending draft in the recovered notebook");
  std::cout << "RESTORE_NOTES_COMMIT stagedOpen=" << notesOpenAfterStage << " recoveredDraft=" << draftInRecovery << '\n';
 } else {
  const bool undoRetained = g_hNotesWindow == notes && IsWindow(notes) &&
      GetDlgItem(notes, IDC_CLIENT_NOTES_TEXT) == editor &&
      NotesWindowText(editor) == pending && SendMessageW(editor, EM_CANUNDO, 0, 0);
  if (testMode == kNotesShutdownQueryMode) {
   Check(shutdownQueryBusy && shutdownQueryRefused, "The actual staging pump refuses a shutdown query while restore is busy");
   Check(pickerCalls == 1 && stageCalls == 1 && fs::is_regular_file(client / L"sentinel") &&
         lastNotice.find(L"Private staged restore cancellation") != std::wstring::npos,
         "Cancelled staging leaves the original client intact");
   Check(undoRetained, "A refused shutdown during restore preserves the same Notes editor, draft and Undo");
   const bool acceptedWhileIdle = WndProc(g_hGui, WM_QUERYENDSESSION, 0, ENDSESSION_CLOSEAPP) == TRUE;
   Check(acceptedWhileIdle && !g_hNotesWindow, "An idle shutdown query still saves and closes Notes");
   std::cout << "RESTORE_SHUTDOWN_QUERY refused=" << shutdownQueryRefused << " busy=" << shutdownQueryBusy
             << " undoRetained=" << undoRetained << '\n';
  } else {
   Check(pickerCalls == 1 && stageCalls == 0 && fs::is_regular_file(client / L"sentinel"),
         "Cancelled restore stops before staging and preserves the original client");
   Check(undoRetained, "Cancelled restore preserves the same Notes window, draft and native Undo");
   if (IsWindow(notes)) SendMessageW(notes, WM_CLOSE, 0, 0);
   Check(!g_hNotesWindow, "Fixture Notes closes normally after the cancellation check");
   std::cout << "RESTORE_NOTES_CANCEL step=" << (testMode == kNotesPickerCancelMode ? "picker" : "confirmation")
             << " undoRetained=" << undoRetained << '\n';
  }
 }
 Check(g_bUiEnabled && !g_bClientNotesBusy.load() && !g_bArchiveTaskInProgress && IsWindowEnabled(g_hGui),
       "Restore leaves the launcher and Notes transaction available");
 Audit();
}

static void InitializeArchiveUiNotesControls() {
 INITCOMMONCONTROLSEX notesControls{sizeof(notesControls), ICC_WIN95_CLASSES};
 if (!InitCommonControlsEx(&notesControls)) throw std::runtime_error("Native Notes controls unavailable");
 BuildThemeColors(false, g_themeColors);
 UpdateThemeBrushes();
}

static void RestoreBeforeStagingRefusal(unsigned testMode) {
 Seed(testMode);
 const bool browserDuringConfirmation = testMode == kBrowserDuringConfirmationMode;
 const auto sites = g_sDataDir / L"Sites";
 const auto client = sites / L"Alpha";
 const auto unsupported = g_sDataDir / L"private.txt";
 const auto archive = g_sDataDir / L"private.7z";
 const auto source = g_sDataDir / L"source/Sites";
 fs::create_directories(client / L"Browsers");
 std::ofstream(client / kClientSchemaMarkerName, std::ios::binary) << kClientSchemaMarkerText;
 std::ofstream(unsupported, std::ios::binary) << "ORDINARY NON-ARCHIVE TEXT";
 if (!WritePrivateProfileStringW(L"notes_window", L"help_seen", L"1", g_sConfigPath.c_str()))
  throw std::runtime_error("Could not set private Notes fixture preferences");
 fs::create_directories(source / L"Restored");
 std::ofstream(source / L"Restored/sentinel", std::ios::binary) << "PRIVATE RESTORED";
 if (!CtBackup::CreateValidated7zBackup(source, archive, CTSPACES_VERSION_TEXT, nullptr, nullptr).ok())
  throw std::runtime_error("Could not create the ordinary replacement archive");
 const auto original = PrivateSnapshot(sites);
 const auto originalText = PrivateObject(unsupported);
 const auto originalArchive = PrivateObject(archive);
 ShowClientNotes(L"Alpha");
 const HWND notes = g_hNotesWindow;
 const HWND editor = GetDlgItem(notes, IDC_CLIENT_NOTES_TEXT);
 auto *state = reinterpret_cast<ClientNotesDialogState *>(GetWindowLongPtrW(notes, DWLP_USER));
 if (!notes || !editor || !state) throw std::runtime_error("Private native Notes editor did not open");
 constexpr wchar_t pending[] = L"Private pending draft during restore selection";
 SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(pending));
 MarkNotesChanged(notes, *state);
 KillTimer(notes, 1);
 KillTimer(notes, 2);
 const auto sameEditor = [&] {
  return g_hNotesWindow == notes && IsWindow(notes) &&
         GetDlgItem(notes, IDC_CLIENT_NOTES_TEXT) == editor &&
         NotesWindowText(editor) == pending && SendMessageW(editor, EM_CANUNDO, 0, 0);
 };
 Check(sameEditor(), "Hidden Notes begins with a pending draft and native Undo");
 const auto priorNotices = notices;
 pickUnsupportedArchive = !browserDuringConfirmation;
 armBrowserDuringConfirmation = browserDuringConfirmation;
 GuiProfRestoreAll();
 pickUnsupportedArchive = false;
 armBrowserDuringConfirmation = false;
 bool refused = pickerCalls == 1 && !stageCalls && !unsupportedArchiveCommitCalls;
 if (browserDuringConfirmation) {
  const auto tracked = g_activeProfiles.find({L"Alpha", BrowserKind::Edge});
  const bool trackingRetained = g_activeProfiles.size() == 1 && tracked != g_activeProfiles.end() &&
      tracked->second.pid == GetCurrentProcessId() && tracked->second.profilePath == client &&
      tracked->second.type == ProfileType::Standard;
  refused = refused && trackingRetained && browserConfirmationCalls == 1 && notices == priorNotices + 2 &&
      lastNotice.find(L"reverified after confirmation. Nothing was restored.") != std::wstring::npos &&
      lastNotice.find(L"ctSpaces is still tracking one or more open browsers.") != std::wstring::npos;
 } else {
  refused = refused && notices == priorNotices + 1 &&
      lastNotice == L"Select a ctSpaces .7z backup or an older .zip backup.";
 }
 Check(refused, "The specific refusal occurs before staging or commit");
 Check(PrivateSnapshot(sites) == original && PrivateRecoveries().empty() &&
       !fs::exists(g_sDataDir / L"Restore_Temp_private-0"),
       "The refusal preserves the exact live collection and creates neither stage nor recovery");
 const bool undoRetained = sameEditor();
 Check(undoRetained, "The refusal preserves the same Notes editor, draft and Undo");
 Check(g_bUiEnabled && !g_bClientNotesBusy.load() && !g_bArchiveTaskInProgress && IsWindowEnabled(g_hGui),
       "The refusal restores launcher availability");
 Audit();

 if (browserDuringConfirmation)
  g_activeProfiles.clear();
 GuiProfRestoreAll();
 const bool retried = pickerCalls == 2 && stageCalls == 1 && unsupportedArchiveCommitCalls == 1 &&
                      (!browserDuringConfirmation || browserConfirmationCalls == 2) &&
                      lastCommitResult.ok() && lastNotice.find(L"Restore complete.") != std::wstring::npos;
 Check(retried, "Restore completes normally after the refusal condition clears");
 Check(!g_hNotesWindow && !IsWindow(notes) && PrivateBytes(sites / L"Restored/sentinel") == "PRIVATE RESTORED",
       "Successful restore closes Notes and installs the verified replacement");
 const auto recoveries = PrivateRecoveries();
 const HMODULE richEdit = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
 if (!richEdit) throw std::runtime_error("RichEdit unavailable to inspect the recovered draft");
 bool recoveredDraft = false;
 if (recoveries.size() == 1) {
  const auto recovered = client_notes::ReadNotebook(recoveries.front() / L"Alpha");
  recoveredDraft = PrivateObject(recoveries.front()) == original.at(L"") &&
      PrivateObject(recoveries.front() / L"Alpha/sentinel") == original.at(L"Alpha\\sentinel") &&
      std::ranges::any_of(recovered.pages, [&](const auto &page) {
       return NotesPlain(page).find(pending) != std::wstring::npos;
      });
 }
 FreeLibrary(richEdit);
 Check(recoveredDraft, "The exact previous collection retains the newly saved draft");
 Check(PrivateObject(unsupported) == originalText && PrivateObject(archive) == originalArchive,
       "Both picked source files remain byte- and identity-exact");
 Check(g_bUiEnabled && !g_bClientNotesBusy.load() && !g_bArchiveTaskInProgress && IsWindowEnabled(g_hGui),
       "Successful restore restores launcher availability");
 std::cout << (browserDuringConfirmation ? "RESTORE_BROWSER_DURING_CONFIRMATION refused=" :
                                           "RESTORE_UNSUPPORTED_EXTENSION refused=")
           << refused << " undoRetained=" << undoRetained
           << " retry=" << retried << " recoveredDraft=" << recoveredDraft << '\n';
 Audit();
}

static void RestorePostStageRefusal(unsigned testMode) {
 Seed(testMode);
 const bool browserAppeared = testMode == kPostStageBrowserMode;
 const auto sites = g_sDataDir / L"Sites";
 const auto client = sites / L"Alpha";
 const auto notebook = client / client_notes::kNotebookFileName;
 const auto archive = g_sDataDir / L"private.7z";
 const auto source = g_sDataDir / L"source/Sites";
 fs::create_directories(client / L"Browsers");
 std::ofstream(client / kClientSchemaMarkerName, std::ios::binary) << kClientSchemaMarkerText;
 if (!WritePrivateProfileStringW(L"notes_window", L"help_seen", L"1", g_sConfigPath.c_str()))
  throw std::runtime_error("Could not set private Notes fixture preferences");
 const std::vector<client_notes::NotePage> saved{{L"Notes", true, "{\\rtf1\\ansi Existing saved text}", {}}};
 if (!client_notes::WriteNotebook(client, saved, client_notes::ReadNotebook(client)))
  throw std::runtime_error("Could not write the private saved notebook");
 fs::create_directories(source / L"Restored");
 std::ofstream(source / L"Restored/sentinel") << "PRIVATE RESTORED";
 if (!CtBackup::CreateValidated7zBackup(source, archive, CTSPACES_VERSION_TEXT, nullptr, nullptr).ok())
  throw std::runtime_error("Could not create ordinary Notes restore archive");
 const auto oldSites = PrivateObject(sites);
 const auto oldNotebook = PrivateObject(notebook);
 const auto oldSentinel = PrivateObject(client / L"sentinel");
 const auto originalArchive = PrivateObject(archive);
 ShowClientNotes(L"Alpha");
 const HWND notes = g_hNotesWindow;
 const HWND editor = GetDlgItem(notes, IDC_CLIENT_NOTES_TEXT);
 auto *state = reinterpret_cast<ClientNotesDialogState *>(GetWindowLongPtrW(notes, DWLP_USER));
 if (!notes || !editor || !state) throw std::runtime_error("Private native Notes editor did not open");
 constexpr wchar_t pending[] = L"Private unsaved text during restore verification";
 SendMessageW(editor, EM_SETSEL, 0, -1);
 SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(pending));
 MarkNotesChanged(notes, *state);
 // Keep this edit pending until the explicit precommit save, regardless of staging duration.
 KillTimer(notes, 1);
 KillTimer(notes, 2);
 const auto sameDraftWithUndo = [&] {
  return g_hNotesWindow == notes && IsWindow(notes) &&
         GetDlgItem(notes, IDC_CLIENT_NOTES_TEXT) == editor &&
         NotesWindowText(editor) == pending && SendMessageW(editor, EM_CANUNDO, 0, 0);
 };
 const auto launcherIdle = [] {
  return g_bUiEnabled && !g_bClientNotesBusy.load() &&
         !g_bArchiveTaskInProgress && IsWindowEnabled(g_hGui);
 };
 Check(sameDraftWithUndo(), "Hidden Notes begins with the pending draft and native Undo");
 armPostStageRefusal = true;
 GuiProfRestoreAll();
 armPostStageRefusal = false;
 if (browserAppeared) {
  const auto tracked = g_activeProfiles.find({L"Alpha", BrowserKind::Edge});
  Check(g_activeProfiles.size() == 1 && tracked != g_activeProfiles.end() &&
        tracked->second.pid == GetCurrentProcessId() && tracked->second.profilePath == client &&
        tracked->second.type == ProfileType::Standard,
        "The original standard client remains registered after appearing during restore staging");
 } else {
  Check(notesSaveLeaseError == ERROR_SHARING_VIOLATION, "Windows confirms the exact notebook cannot be replaced");
 }
 const bool blocked = pickerCalls == 1 && stageCalls == 1 && !postStageCommitCalls && lastStagedRestore.validation;
 Check(blocked, "Restore stops after verification and never enters commit when the final preflight refuses");
 const auto refusedStage = lastStagedRestore.stagingRoot;
 constexpr std::wstring_view stageLabel = L"The verified staging data was retained here:\n";
 const auto stageLabelAt = lastNotice.find(stageLabel);
 const auto expectedWarning = browserAppeared
     ? L"ctSpaces is still tracking one or more open browsers."
     : L"Save or recover the open notes before restoring.";
 const bool reported = lastNotice.find(expectedWarning) != std::wstring::npos &&
     (!browserAppeared || lastNotice.find(L"while the backup was staged") != std::wstring::npos) &&
     stageLabelAt != std::wstring::npos &&
     fs::path(lastNotice.substr(stageLabelAt + stageLabel.size())).lexically_normal() == refusedStage.lexically_normal();
 Check(reported, "The refusal reports the retained verified stage");
 if (!reported)
  std::wcout << L"NOTES_SAVE_REFUSAL_NOTICE=" << lastNotice << L"\nSEALED_STAGE=" << refusedStage.wstring() << L'\n';
 const bool stageRetained = !stagedBefore.empty() && PrivateSnapshot(refusedStage) == stagedBefore;
 Check(stageRetained, "The verified stage keeps its exact objects and contents");
 Check(PrivateObject(sites) == oldSites && PrivateObject(notebook) == oldNotebook &&
       PrivateObject(client / L"sentinel") == oldSentinel && PrivateRecoveries().empty(),
       "The original Sites, saved notebook, and recovery namespace stay unchanged");
 const bool undoRetained = sameDraftWithUndo();
 Check(undoRetained, "The same editor retains the pending draft and usable Undo");
 Check(launcherIdle(), "The refused restore releases both launcher busy guards");
 Audit();

 if (browserAppeared)
  g_activeProfiles.clear();
 else
  notesSaveLease.reset();
 GuiProfRestoreAll();
 const bool retried = pickerCalls == 2 && stageCalls == 2 &&
                      postStageCommitCalls == 1 && lastCommitResult.ok();
 Check(retried, "Retry with the same archive reaches successful commit after the blocking condition ends");
 Check(!g_hNotesWindow && !IsWindow(notes) && PrivateBytes(sites / L"Restored/sentinel") == "PRIVATE RESTORED" &&
       !fs::exists(client), "Retry closes the saved Notes editor and installs the exact replacement");
 const auto recoveries = PrivateRecoveries();
 const HMODULE richEdit = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
 if (!richEdit) throw std::runtime_error("RichEdit unavailable to inspect the recovered draft");
 bool draftRecovered = false;
 if (recoveries.size() == 1) {
  const auto recovered = client_notes::ReadNotebook(recoveries.front() / L"Alpha");
  draftRecovered = PrivateObject(recoveries.front()) == oldSites &&
      PrivateObject(recoveries.front() / L"Alpha/sentinel") == oldSentinel &&
      std::ranges::any_of(recovered.pages, [&](const auto &page) {
       return NotesPlain(page).find(pending) != std::wstring::npos;
      });
 }
 FreeLibrary(richEdit);
 Check(draftRecovered, "Retry durably saves the draft in the exact original collection in recovery");
 Check(PrivateSnapshot(refusedStage) == stagedBefore && PrivateObject(archive) == originalArchive,
       "Retry leaves the initially retained stage and exact source archive available");
 Check(launcherIdle(), "Successful retry releases both launcher busy guards");
 std::cout << (browserAppeared ? "RESTORE_POST_STAGE_BROWSER blocked=" : "RESTORE_NOTES_SAVE_REFUSAL blocked=")
           << blocked << " undoRetained=" << undoRetained
           << " stageRetained=" << stageRetained << " reported=" << reported << " retry=" << retried
           << " recoveredDraft=" << draftRecovered << '\n';
 Audit();
}

int wmain(int argc,wchar_t** argv){std::vector<HWINEVENTHOOK> hooks;try{
 std::cout<<std::unitbuf;
 const bool notesSaveProof = argc == 2 && std::wstring_view(argv[1]) == L"--notes-save-refusal";
 const bool postStageBrowserProof = argc == 2 && std::wstring_view(argv[1]) == L"--post-stage-browser";
 const bool unsupportedArchiveProof = argc == 2 && std::wstring_view(argv[1]) == L"--unsupported-archive";
 const bool browserConfirmationProof = argc == 2 && std::wstring_view(argv[1]) == L"--browser-during-confirmation";
 if(argc!=1 && !notesSaveProof && !postStageBrowserProof && !unsupportedArchiveProof && !browserConfirmationProof)
  throw std::runtime_error("Unknown archive UI argument refused");
 const auto noActivation=SetWindowsHookExW(WH_CBT,BackgroundCbt,nullptr,GetCurrentThreadId());
 if(!noActivation)throw std::runtime_error("Native no-activation guard unavailable");
 for(DWORD event:{EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS,EVENT_SYSTEM_FOREGROUND}){auto hook=SetWinEventHook(event,event,nullptr,Event,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);if(!hook)throw std::runtime_error("Native observer unavailable");hooks.push_back(hook);}
 g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();
 ownerThread=GetCurrentThreadId();
 WNDCLASSW cls{};cls.lpfnWndProc=HiddenProc;cls.hInstance=g_hInst;cls.lpszClassName=L"ArchiveUiPrivateHidden";if(!RegisterClassW(&cls))throw std::runtime_error("Hidden class registration failed");
 g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Private archive UI",WS_OVERLAPPED,0,0,600,240,nullptr,nullptr,g_hInst,nullptr);if(!g_hGui)throw std::runtime_error("Hidden owner unavailable");
 fixture=retained_fixture::CreateRoot(fs::current_path()/L"build/qa-aui",L"fixture");
 std::wcout<<L"FIXTURE_ROOT="<<fixture.wstring()<<L'\n';
 if (notesSaveProof || postStageBrowserProof || unsupportedArchiveProof || browserConfirmationProof) {
  InitializeArchiveUiNotesControls();
  if (unsupportedArchiveProof || browserConfirmationProof)
   RestoreBeforeStagingRefusal(browserConfirmationProof ? kBrowserDuringConfirmationMode : kUnsupportedArchiveMode);
  else
   RestorePostStageRefusal(notesSaveProof ? kNotesSaveRefusalMode : kPostStageBrowserMode);
 } else {
 Seed(0);GuiProfRestoreAll();Check(pickerCalls==1&&stageCalls==0,"Ordinary restore picker Cancel ends before staging");Check(g_bUiEnabled&&!g_bClientNotesBusy.load(),"Ordinary Cancel releases launcher and notes busy state");Audit();
 Seed(1);GuiProfRestoreAll();std::cout<<"NESTED_RESTORE_PICKERS="<<pickerCalls<<"\n";Check(pickerCalls==1,"Restore owns the transaction before its picker can request another restore");Check(!reentrantUiEnabled,"A nested restore request cannot re-enable the launcher during the outer picker");Check(g_bUiEnabled&&!g_bClientNotesBusy.load(),"Nested request leaves the outer Cancel able to restore UI");Audit();
 Seed(2);GuiProfRestoreAll();std::cout<<"PICKER_NESTED_BACKUP_PICKERS="<<backupPickerCalls<<"\n";Check(backupPickerCalls==0,"A queued backup command is blocked during restore file selection");Check(!reentrantUiEnabled,"A rejected queued backup cannot re-enable an active restore's launcher");Check(g_bUiEnabled&&!g_bClientNotesBusy.load(),"Outer Cancel releases transaction after blocked backup");Audit();
 Seed(3);GuiProfRestoreAll();std::cout<<"STAGE_NESTED_BACKUP_PICKERS="<<backupPickerCalls<<"\n";Check(stageCalls==1&&backupPickerCalls==0,"The production archive pump cannot start a backup inside a restore");Check(busyDuringPicker&&busyDuringStage,"Restore keeps the client-tree transaction guard across picker and worker");Check(exitDeferred,"Restore keeps an exit notification owned for deferred UI handling");MSG deferred{};Check(g_notesDeferredExits.empty()&&PeekMessageW(&deferred,g_hGui,WM_APP_PROFILE_EXITED,WM_APP_PROFILE_EXITED,PM_NOREMOVE),"Restore releases and actually reposts deferred exit ownership after cancellation");Check(g_bUiEnabled&&!g_bClientNotesBusy.load()&&!g_bArchiveTaskInProgress,"Staging cancellation releases both busy states and launcher availability");Check(fs::is_regular_file(g_sDataDir/L"Sites/Alpha/sentinel"),"Cancelled staging leaves original private client data intact");Audit();
 Seed(0);g_bClientNotesBusy=true;g_bUiEnabled=false;GuiProfRestoreAll();Check(pickerCalls==0&&g_bClientNotesBusy.load()&&!g_bUiEnabled,"An already-owned client-tree transaction rejects restore without releasing its owner's busy state");g_bClientNotesBusy=false;SetUiState(true);Audit();
 Seed(4);
 commitPumped=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 Check(commitPumped!=nullptr,"The private commit responsiveness probe owns its event");
 const bool positivePathFits=CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(g_sDataDir/L"Sites",L"Restored");
 Check(positivePathFits,"The positive verified restore fixture fits the production managed-client path budget");
 if(!positivePathFits)throw std::runtime_error("Positive restore fixture exceeds its production path-budget precondition");
 {const auto source=g_sDataDir/L"source/Sites";fs::create_directories(source/L"Restored");std::ofstream(source/L"Restored/sentinel")<<"PRIVATE BACKUP";const auto created=CtBackup::CreateValidated7zBackup(source,g_sDataDir/L"private.7z",CTSPACES_VERSION_TEXT,nullptr,nullptr);if(!created.ok())throw std::runtime_error("Private positive backup creation failed");}GuiProfRestoreAll();Check(stageCalls==1&&busyDuringPicker&&busyDuringStage,"Ordinary verified restore traverses the real archive worker under the client-tree guard");Check(fs::is_regular_file(g_sDataDir/L"Sites/Restored/sentinel")&&!fs::exists(g_sDataDir/L"Sites/Alpha"),"Ordinary verified restore installs its actual SDK-validated payload");bool kept=false;for(const auto&entry:fs::directory_iterator(g_sDataDir))if(PrivateRecoveryName(entry.path())&&fs::is_regular_file(entry.path()/L"Alpha/sentinel"))kept=true;Check(kept,"Ordinary verified restore retains the original private client in recovery");Check(g_bUiEnabled&&!g_bClientNotesBusy.load()&&!g_bArchiveTaskInProgress,"Ordinary verified restore releases both busy states after publication");Audit();
 Check(commitThread!=0&&commitThread!=ownerThread,"Final restore verification and promotion run outside the launcher thread");
 Check(commitProbeHandled&&commitBusy,"The actual owner message pump responds while final restore verification holds its busy guards");
 Check(commitBackupBlocked,"A pumped backup command cannot reenter the restore commit or enable its owner");
 std::cout<<"RESTORE_COMMIT_WORKER="<<(commitThread!=0&&commitThread!=ownerThread)<<" PUMP="<<commitProbeHandled<<" BUSY="<<commitBusy<<" REENTRY_BLOCKED="<<commitBackupBlocked<<'\n';
 CloseHandle(commitPumped);commitPumped=nullptr;
 CommitLifecycle(5);CommitLifecycle(6);CommitLifecycle(7);
 TrackedProfileRestoreScope(ProfileType::Temporary, L"Temp");
 TrackedProfileRestoreScope(ProfileType::Default, L"Default");
 TrackedProfileRestoreScope(ProfileType::Standard, L"Alpha");
 RecoveryNoticeOwnership(true);
 RecoveryNoticeOwnership(false);
 RetainedStageNotice();
 RestoredClientIconCache();
 InitializeArchiveUiNotesControls();
 RestoreNotesLifetime(kNotesPickerCancelMode);
 RestoreNotesLifetime(kNotesConfirmCancelMode);
 RestoreNotesLifetime(kNotesCommitMode);
 RestoreNotesLifetime(kNotesShutdownQueryMode);
 Seed(0);const auto thrown=RunArchiveTaskWithProgress(L"Private exception",[](_7zUiCtx&)->CtBackup::Result{throw std::runtime_error("Scoped archive exception");});Check(!thrown.ok()&&thrown.details.find(L"Scoped archive exception")!=std::wstring::npos,"Actual worker exception is contained and reports its diagnostic");Check(!g_bArchiveTaskInProgress&&IsWindowEnabled(g_hGui),"Worker exception joins and restores the originally enabled owner");Audit();
 Seed(0);EnableWindow(g_hGui,FALSE);const auto disabled=RunArchiveTaskWithProgress(L"Private disabled owner",[](_7zUiCtx&){return CtBackup::Result{};});Check(disabled.ok()&&!IsWindowEnabled(g_hGui)&&!g_bArchiveTaskInProgress,"Successful task preserves an owner that was already disabled");EnableWindow(g_hGui,TRUE);Audit();
 Seed(0);const auto unknown=RunArchiveTaskWithProgress(L"Private unknown exception",[](_7zUiCtx&)->CtBackup::Result{throw 17;});Check(!unknown.ok()&&unknown.details.find(L"unexpected archive error")!=std::wstring::npos&&!g_bArchiveTaskInProgress,"Unknown worker exception is contained and releases task state");Audit();
 Seed(0);failEvent=true;const DWORD uiThread=GetCurrentThreadId();DWORD executionThread=0;const auto synchronous=RunArchiveTaskWithProgress(L"Private event failure",[&](_7zUiCtx&){executionThread=GetCurrentThreadId();return CtBackup::Result{};});failEvent=false;Check(synchronous.ok()&&executionThread==uiThread&&IsWindowEnabled(g_hGui)&&!g_bArchiveTaskInProgress,"Event allocation failure completes synchronously and restores its owner");Audit();
 Seed(0);failWait=true;const auto waitFailure=RunArchiveTaskWithProgress(L"Private wait failure",[](_7zUiCtx&){Sleep(20);return CtBackup::Result{};});Check(waitFailure.ok()&&!failWait&&IsWindowEnabled(g_hGui)&&!g_bArchiveTaskInProgress,"A failed UI wait still joins the worker and restores task state");Audit();
 Seed(0);::PostQuitMessage(47);const auto quit=RunArchiveTaskWithProgress(L"Private quit",[](_7zUiCtx&){Sleep(30);return CtBackup::Result{};});MSG quitMessage{};const bool quitFound=PeekMessageW(&quitMessage,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)!=FALSE;Check(quit.ok()&&quitFound&&quitMessage.wParam==47,"Actual archive pump preserves WM_QUIT and its exit code after worker completion");Check(IsWindowEnabled(g_hGui)&&!g_bArchiveTaskInProgress,"Quit during worker does not strand its owner disabled");Audit();
 Seed(0);readyQuitWait=true;readyQuitPosted=false;const auto ready=RunArchiveTaskWithProgress(L"Private completed-worker quit",[](_7zUiCtx&){return CtBackup::Result{};});MSG readyQuit{};const bool readyQuitFound=PeekMessageW(&readyQuit,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)!=FALSE;
 Check(ready.ok()&&readyQuitPosted&&!readyQuitWait,"Ready-worker boundary posts an actual owner quit only after the worker signals completion");
 Check(readyQuitFound&&readyQuit.message==WM_QUIT&&readyQuit.wParam==88,"The final progress drain preserves quit when the worker was already complete");
 Check(IsWindowEnabled(g_hGui)&&!g_bArchiveTaskInProgress,"Ready-worker quit leaves the joined archive owner available");
 std::cout<<"READY_WORKER_QUIT posted="<<readyQuitPosted<<" found="<<readyQuitFound<<" message="<<readyQuit.message<<" code="<<readyQuit.wParam<<'\n';Audit();
 }
 Check(forbidden==0&&nativeShows==0&&nativeFocus==0&&nativeForeground==0,"No native show, focus, foreground, device, registry, clipboard or launch access occurred");
 DestroyWindow(g_hGui);g_hGui=nullptr;for(auto hook:hooks)UnhookWinEvent(hook);UnhookWindowsHookEx(noActivation);
 std::cout<<"Archive UI lifetime: "<<checks<<" checks, "<<scenarios<<" scenarios, "<<failures<<" failures; suppressed="<<suppressed<<"; native show/focus/foreground="<<nativeShows<<'/'<<nativeFocus<<'/'<<nativeForeground<<"; forbidden="<<forbidden<<".\n";
 return failures?1:0;
 }catch(const std::exception&e){std::cerr<<"Archive fixture error: "<<e.what()<<'\n';return 2;}}
