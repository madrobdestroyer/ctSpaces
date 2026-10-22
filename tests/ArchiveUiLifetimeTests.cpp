// Actual archive GUI/message-pump paths; all windows remain hidden and owned.
#include <windows.h>
#include "RetainedFixture.h"
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../ProgressUI.h"
static unsigned suppressed=0,forbidden=0,nativeShows=0,nativeFocus=0,nativeForeground=0;
static bool failEvent=false,failWait=false;
static HANDLE WINAPI PrivateEvent(LPSECURITY_ATTRIBUTES security,BOOL manual,BOOL initial,LPCWSTR name){if(failEvent){SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}return ::CreateEventW(security,manual,initial,name);}
static DWORD WINAPI PrivateWait(DWORD count,const HANDLE*handles,BOOL all,DWORD timeout,DWORD wake){if(failWait){failWait=false;SetLastError(ERROR_INVALID_HANDLE);return WAIT_FAILED;}return ::MsgWaitForMultipleObjects(count,handles,all,timeout,wake);}
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
static LSTATUS WINAPI NoSystemValue(HKEY,LPCWSTR,LPCWSTR,LPDWORD,LPVOID,LPDWORD){++forbidden;return ERROR_ACCESS_DENIED;}
static HANDLE WINAPI Snapshot(DWORD,DWORD){return CreateEventW(nullptr,FALSE,FALSE,nullptr);}
static BOOL WINAPI First(HANDLE,LPPROCESSENTRY32W item){wcscpy_s(item->szExeFile,L"private-archive-fixture.exe");item->th32ProcessID=GetCurrentProcessId();return TRUE;}
static BOOL WINAPI Next(HANDLE,LPPROCESSENTRY32W){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
static void ProgressShow(HWND,const std::wstring&,int){}
static void ProgressHide(){}
static BOOL WINAPI SavePicker(OPENFILENAMEW*);
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
#define SHGetValueW NoSystemValue
#define CreateToolhelp32Snapshot Snapshot
#define Process32FirstW First
#define Process32NextW Next
#define ProgressUI_Show ProgressShow
#define ProgressUI_Hide ProgressHide
#define GetSaveFileNameW SavePicker
#define CreateEventW PrivateEvent
#define MsgWaitForMultipleObjects PrivateWait
#define CTSPACES_INSTALLER_TEST_HOOKS
#define CTSPACES_RESTORE_GUI_TEST_HOOKS
#define wWinMain UnusedArchiveApplicationEntry
#ifdef CTSPACES_ARCHIVE_UI_BEFORE
#include "../build/app-audit-round10-2026-10-15/archive-ui/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef SetActiveWindow

static unsigned checks=0,failures=0,scenarios=0,pickerCalls=0,backupPickerCalls=0,stageCalls=0,notices=0;
static unsigned mode=0;
static bool injected=false,busyDuringPicker=false,busyDuringStage=false,reentrantUiEnabled=false;
static bool exitDeferred=false;
static fs::path fixture;
static void Check(bool value,const char*message){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';}
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR,LPCWSTR,UINT flags){++notices;return flags&MB_YESNO?IDYES:IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path&,std::wstring&){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path&){}
static BOOL WINAPI SavePicker(OPENFILENAMEW*){++backupPickerCalls;return FALSE;}
BOOL CtSpacesRestoreGuiTestPickArchive(OPENFILENAMEW *picker){
 ++pickerCalls;busyDuringPicker=g_bClientNotesBusy.load();
 if(!injected&&(mode==1||mode==2)){
  injected=true;
  if(mode==1)GuiProfRestoreAll();else WndProc(g_hGui,WM_COMMAND,IDM_CTX_EXPORT_ALL,0);
  reentrantUiEnabled=g_bUiEnabled;
 }
 if(mode==3||mode==4)return wcscpy_s(picker->lpstrFile,picker->nMaxFile,(g_sDataDir/L"private.7z").c_str())==0;
 return FALSE;
}
std::wstring CtSpacesRestoreGuiTestStageSuffix(unsigned attempt){return L"private-"+std::to_wstring(attempt);}
CtBackup::Result CtSpacesRestoreGuiTestRunStage(const std::function<CtBackup::Result(_7zUiCtx&)>&operation){
 ++stageCalls;busyDuringStage=g_bClientNotesBusy.load();
 if(mode==4)return RunArchiveTaskWithProgress(L"Private verified restore",operation);
 WndProc(g_hGui,WM_APP_PROFILE_EXITED,0,reinterpret_cast<LPARAM>(new ProfileExitPayload{0x7ffff713,L"Stale private session",BrowserKind::Edge,ProfileType::Standard,false,false}));
 exitDeferred=g_notesDeferredExits.size()==1;
 // The production task runner pumps a queued launcher command while the inert
 // worker waits. No archive extraction or presentation is executed in this case.
 ::PostMessageW(g_hGui,WM_COMMAND,IDM_CTX_EXPORT_ALL,0);
 return RunArchiveTaskWithProgress(L"Private cancellation",[](_7zUiCtx&){Sleep(50);return CtBackup::Result{E_ABORT,L"Private cancellation"};});
}
void CtSpacesRestoreGuiTestAfterStage(CtBackup::StagedRestore&){}
void CtSpacesRestoreGuiTestBeforeCommitGuard(){}
static LRESULT CALLBACK HiddenProc(HWND w,UINT m,WPARAM wp,LPARAM lp){if(m==WM_COMMAND||m==WM_APP_PROFILE_EXITED)return WndProc(w,m,wp,lp);return DefWindowProcW(w,m,wp,lp);}
static void CALLBACK Event(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId())return;if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW)++nativeShows;else if(event==EVENT_OBJECT_FOCUS)++nativeFocus;else if(event==EVENT_SYSTEM_FOREGROUND)++nativeForeground;}
static void Audit(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)continue;if((message.message>=WM_KEYFIRST&&message.message<=WM_KEYLAST)||(message.message>=WM_MOUSEFIRST&&message.message<=WM_MOUSELAST))throw std::runtime_error("Device input excluded");DispatchMessageW(&message);}unsigned visible=0;EnumWindows([](HWND w,LPARAM p)->BOOL{DWORD pid=0;if(GetWindowThreadProcessId(w,&pid)&&pid==GetCurrentProcessId()&&IsWindowVisible(w))++*reinterpret_cast<unsigned*>(p);return TRUE;},reinterpret_cast<LPARAM>(&visible));GUITHREADINFO state{sizeof(state)};if(!GetGUIThreadInfo(GetCurrentThreadId(),&state)||visible||state.hwndFocus||state.hwndActive||state.hwndCapture||nativeShows||nativeFocus||nativeForeground||forbidden)throw std::runtime_error("Background archive safety failed");}
static void Seed(unsigned next){++scenarios;mode=next;pickerCalls=backupPickerCalls=stageCalls=0;injected=busyDuringPicker=busyDuringStage=reentrantUiEnabled=exitDeferred=failEvent=failWait=false;g_sDataDir=fixture/std::to_wstring(scenarios);fs::create_directories(g_sDataDir/L"Sites/Alpha");g_sConfigPath=g_sDataDir/L"config.ini";std::ofstream(g_sDataDir/L"Sites/Alpha/sentinel")<<"PRIVATE ORIGINAL";g_bUiEnabled=true;g_bClientNotesBusy=false;g_bArchiveTaskInProgress=false;g_activeProfiles.clear();g_notesDeferredExits.clear();EnableWindow(g_hGui,TRUE);}
int wmain(int argc,wchar_t**){std::vector<HWINEVENTHOOK> hooks;try{
 if(argc!=1)throw std::runtime_error("Unknown archive UI argument refused");
 for(DWORD event:{EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS,EVENT_SYSTEM_FOREGROUND}){auto hook=SetWinEventHook(event,event,nullptr,Event,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);if(!hook)throw std::runtime_error("Native observer unavailable");hooks.push_back(hook);}
 g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();
 WNDCLASSW cls{};cls.lpfnWndProc=HiddenProc;cls.hInstance=g_hInst;cls.lpszClassName=L"ArchiveUiPrivateHidden";if(!RegisterClassW(&cls))throw std::runtime_error("Hidden class registration failed");
 g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Private archive UI",WS_OVERLAPPED,0,0,600,240,nullptr,nullptr,g_hInst,nullptr);if(!g_hGui)throw std::runtime_error("Hidden owner unavailable");
 fixture=retained_fixture::CreateRoot(fs::current_path()/L"build/qa-aui",L"fixture");
 Seed(0);GuiProfRestoreAll();Check(pickerCalls==1&&stageCalls==0,"Ordinary restore picker Cancel ends before staging");Check(g_bUiEnabled&&!g_bClientNotesBusy.load(),"Ordinary Cancel releases launcher and notes busy state");Audit();
 Seed(1);GuiProfRestoreAll();std::cout<<"NESTED_RESTORE_PICKERS="<<pickerCalls<<"\n";Check(pickerCalls==1,"Restore owns the transaction before its picker can request another restore");Check(!reentrantUiEnabled,"A nested restore request cannot re-enable the launcher during the outer picker");Check(g_bUiEnabled&&!g_bClientNotesBusy.load(),"Nested request leaves the outer Cancel able to restore UI");Audit();
 Seed(2);GuiProfRestoreAll();std::cout<<"PICKER_NESTED_BACKUP_PICKERS="<<backupPickerCalls<<"\n";Check(backupPickerCalls==0,"A queued backup command is blocked during restore file selection");Check(!reentrantUiEnabled,"A rejected queued backup cannot re-enable an active restore's launcher");Check(g_bUiEnabled&&!g_bClientNotesBusy.load(),"Outer Cancel releases transaction after blocked backup");Audit();
 Seed(3);GuiProfRestoreAll();std::cout<<"STAGE_NESTED_BACKUP_PICKERS="<<backupPickerCalls<<"\n";Check(stageCalls==1&&backupPickerCalls==0,"The production archive pump cannot start a backup inside a restore");Check(busyDuringPicker&&busyDuringStage,"Restore keeps the client-tree transaction guard across picker and worker");Check(exitDeferred,"Restore keeps an exit notification owned for deferred UI handling");MSG deferred{};Check(g_notesDeferredExits.empty()&&PeekMessageW(&deferred,g_hGui,WM_APP_PROFILE_EXITED,WM_APP_PROFILE_EXITED,PM_NOREMOVE),"Restore releases and actually reposts deferred exit ownership after cancellation");Check(g_bUiEnabled&&!g_bClientNotesBusy.load()&&!g_bArchiveTaskInProgress,"Staging cancellation releases both busy states and launcher availability");Check(fs::is_regular_file(g_sDataDir/L"Sites/Alpha/sentinel"),"Cancelled staging leaves original private client data intact");Audit();
 Seed(0);g_bClientNotesBusy=true;g_bUiEnabled=false;GuiProfRestoreAll();Check(pickerCalls==0&&g_bClientNotesBusy.load()&&!g_bUiEnabled,"An already-owned client-tree transaction rejects restore without releasing its owner's busy state");g_bClientNotesBusy=false;SetUiState(true);Audit();
 Seed(4);
 const bool positivePathFits=CtBackup::Detail::IsNewClientTargetPathWithinLegacyBudget(g_sDataDir/L"Sites",L"Restored");
 Check(positivePathFits,"The positive verified restore fixture fits the production managed-client path budget");
 if(!positivePathFits)throw std::runtime_error("Positive restore fixture exceeds its production path-budget precondition");
 {const auto source=g_sDataDir/L"source/Sites";fs::create_directories(source/L"Restored");std::ofstream(source/L"Restored/sentinel")<<"PRIVATE BACKUP";const auto created=CtBackup::CreateValidated7zBackup(source,g_sDataDir/L"private.7z",CTSPACES_VERSION_TEXT,nullptr,nullptr);if(!created.ok())throw std::runtime_error("Private positive backup creation failed");}GuiProfRestoreAll();Check(stageCalls==1&&busyDuringPicker&&busyDuringStage,"Ordinary verified restore traverses the real archive worker under the client-tree guard");Check(fs::is_regular_file(g_sDataDir/L"Sites/Restored/sentinel")&&!fs::exists(g_sDataDir/L"Sites/Alpha"),"Ordinary verified restore installs its actual SDK-validated payload");bool kept=false;for(const auto&entry:fs::directory_iterator(g_sDataDir))if(entry.path().filename().wstring().starts_with(L"Sites_PreRestore_")&&fs::is_regular_file(entry.path()/L"Alpha/sentinel"))kept=true;Check(kept,"Ordinary verified restore retains the original private client in recovery");Check(g_bUiEnabled&&!g_bClientNotesBusy.load()&&!g_bArchiveTaskInProgress,"Ordinary verified restore releases both busy states after publication");Audit();
 Seed(0);const auto thrown=RunArchiveTaskWithProgress(L"Private exception",[](_7zUiCtx&)->CtBackup::Result{throw std::runtime_error("Scoped archive exception");});Check(!thrown.ok()&&thrown.details.find(L"Scoped archive exception")!=std::wstring::npos,"Actual worker exception is contained and reports its diagnostic");Check(!g_bArchiveTaskInProgress&&IsWindowEnabled(g_hGui),"Worker exception joins and restores the originally enabled owner");Audit();
 Seed(0);EnableWindow(g_hGui,FALSE);const auto disabled=RunArchiveTaskWithProgress(L"Private disabled owner",[](_7zUiCtx&){return CtBackup::Result{};});Check(disabled.ok()&&!IsWindowEnabled(g_hGui)&&!g_bArchiveTaskInProgress,"Successful task preserves an owner that was already disabled");EnableWindow(g_hGui,TRUE);Audit();
 Seed(0);const auto unknown=RunArchiveTaskWithProgress(L"Private unknown exception",[](_7zUiCtx&)->CtBackup::Result{throw 17;});Check(!unknown.ok()&&unknown.details.find(L"unexpected archive error")!=std::wstring::npos&&!g_bArchiveTaskInProgress,"Unknown worker exception is contained and releases task state");Audit();
 Seed(0);failEvent=true;const DWORD uiThread=GetCurrentThreadId();DWORD executionThread=0;const auto synchronous=RunArchiveTaskWithProgress(L"Private event failure",[&](_7zUiCtx&){executionThread=GetCurrentThreadId();return CtBackup::Result{};});failEvent=false;Check(synchronous.ok()&&executionThread==uiThread&&IsWindowEnabled(g_hGui)&&!g_bArchiveTaskInProgress,"Event allocation failure completes synchronously and restores its owner");Audit();
 Seed(0);failWait=true;const auto waitFailure=RunArchiveTaskWithProgress(L"Private wait failure",[](_7zUiCtx&){Sleep(20);return CtBackup::Result{};});Check(waitFailure.ok()&&!failWait&&IsWindowEnabled(g_hGui)&&!g_bArchiveTaskInProgress,"A failed UI wait still joins the worker and restores task state");Audit();
 Seed(0);::PostQuitMessage(47);const auto quit=RunArchiveTaskWithProgress(L"Private quit",[](_7zUiCtx&){Sleep(30);return CtBackup::Result{};});MSG quitMessage{};const bool quitFound=PeekMessageW(&quitMessage,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)!=FALSE;Check(quit.ok()&&quitFound&&quitMessage.wParam==47,"Actual archive pump preserves WM_QUIT and its exit code after worker completion");Check(IsWindowEnabled(g_hGui)&&!g_bArchiveTaskInProgress,"Quit during worker does not strand its owner disabled");Audit();
 Check(forbidden==0&&nativeShows==0&&nativeFocus==0&&nativeForeground==0,"No native show, focus, foreground, device, registry, clipboard or launch access occurred");
 DestroyWindow(g_hGui);g_hGui=nullptr;for(auto hook:hooks)UnhookWinEvent(hook);
 std::cout<<"Archive UI lifetime: "<<checks<<" checks, "<<scenarios<<" scenarios, "<<failures<<" failures; suppressed="<<suppressed<<"; native show/focus/foreground="<<nativeShows<<'/'<<nativeFocus<<'/'<<nativeForeground<<"; forbidden="<<forbidden<<".\n";
 return failures?1:0;
 }catch(const std::exception&e){std::cerr<<"Archive fixture error: "<<e.what()<<'\n';return 2;}}
