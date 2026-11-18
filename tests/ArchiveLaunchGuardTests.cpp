// Actual queued archive command dispatch with a supplied pending-launch state.
// This does not start a browser or reproduce physical shortcut timing.
#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <vector>
#include "../ProgressUI.h"
#ifndef CTSPACES_ARCHIVE_LAUNCH_SOURCE
#error The wrapper must bind the exact production source snapshot.
#endif
#ifndef CTSPACES_ARCHIVE_LAUNCH_FIXTURE_PARENT
#error The wrapper must bind this run's fresh workspace fixture parent.
#endif
static unsigned suppressed=0,forbidden=0,nativeShows=0,nativeFocus=0,nativeForeground=0;
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
static UINT WINAPI NoSendInput(UINT,LPINPUT,int){++forbidden;return 0;}
static SHORT WINAPI NoKeyState(int){++forbidden;return 0;}
static INT_PTR WINAPI ObserveDialog(HINSTANCE,LPCWSTR,HWND,DLGPROC,LPARAM);
static INT_PTR WINAPI NoIndirectDialog(HINSTANCE,LPCDLGTEMPLATEW,HWND,DLGPROC,LPARAM){++forbidden;return IDCANCEL;}
static BOOL WINAPI ObserveOpenPicker(OPENFILENAMEW*);
static BOOL WINAPI NoSavePicker(OPENFILENAMEW*){++forbidden;return FALSE;}
static LSTATUS WINAPI NoSystemValue(HKEY,LPCWSTR,LPCWSTR,LPDWORD,LPVOID,LPDWORD){++forbidden;return ERROR_ACCESS_DENIED;}
static HANDLE WINAPI PrivateProcessSnapshot(DWORD,DWORD){return CreateEventW(nullptr,FALSE,FALSE,nullptr);}
static BOOL WINAPI FirstPrivateProcess(HANDLE,LPPROCESSENTRY32W item){wcscpy_s(item->szExeFile,L"private-archive-launch-fixture.exe");item->th32ProcessID=GetCurrentProcessId();return TRUE;}
static BOOL WINAPI NoNextProcess(HANDLE,LPPROCESSENTRY32W){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
static void NoProgressShow(HWND,const std::wstring&,int){++forbidden;}
static void NoProgressHide(){}
static LRESULT CALLBACK BackgroundCbt(int code,WPARAM w,LPARAM l){
 if(code==HCBT_CREATEWND){auto*created=reinterpret_cast<CBT_CREATEWNDW*>(l);created->lpcs->style&=~WS_VISIBLE;created->lpcs->dwExStyle|=WS_EX_NOACTIVATE;}
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
#define SendInput NoSendInput
#define GetAsyncKeyState NoKeyState
#define GetKeyState NoKeyState
#define DialogBoxParamW ObserveDialog
#define DialogBoxIndirectParamW NoIndirectDialog
#define GetOpenFileNameW ObserveOpenPicker
#define GetSaveFileNameW NoSavePicker
#define SHGetValueW NoSystemValue
#define CreateToolhelp32Snapshot PrivateProcessSnapshot
#define Process32FirstW FirstPrivateProcess
#define Process32NextW NoNextProcess
#define ProgressUI_Show NoProgressShow
#define ProgressUI_Hide NoProgressHide
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain UnusedArchiveLaunchApplicationEntry
#include CTSPACES_ARCHIVE_LAUNCH_SOURCE
#undef wWinMain
#undef SetActiveWindow

namespace {
unsigned checks=0,failures=0,scenarios=0,backupSelectors=0,restorePickers=0;
bool pickerOwnedBusy=false,pickerSawPending=false;
unsigned pendingBackupSelectors=0,pendingRestorePickers=0;
void Check(bool value,const char*description){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<description<<'\n';}
void NoReparseAncestors(const fs::path&path){for(auto current=path;!current.empty();){const auto attributes=GetFileAttributesW(current.c_str());if(attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Reparse fixture path refused");const auto parent=current.parent_path();if(parent==current)break;current=parent;}}
fs::path Reserve(const wchar_t*requested){
 const fs::path parent=fs::path(CTSPACES_ARCHIVE_LAUNCH_FIXTURE_PARENT).lexically_normal(),raw(requested);
 if(!parent.is_absolute()||!raw.is_absolute())throw std::runtime_error("Absolute compile-bound fixture required");
 for(const auto&part:raw)if(part==L"."||part==L"..")throw std::runtime_error("Fixture dot component refused");
 const auto root=raw.lexically_normal();if(_wcsicmp(root.parent_path().c_str(),parent.c_str())||root.filename().empty()||root.filename().native().size()>12)throw std::runtime_error("Fresh direct compile-bound child required");
 NoReparseAncestors(root);if(GetFileAttributesW(root.c_str())!=INVALID_FILE_ATTRIBUTES||!CreateDirectoryW(root.c_str(),nullptr))throw std::runtime_error("Existing fixture refused");return root;
}
struct Entry{DWORD volume=0,high=0,low=0,attributes=0;std::string bytes;bool operator==(const Entry&)const=default;};
using Tree=std::map<std::wstring,Entry>;
Tree SnapshotTree(const fs::path&root){
 Tree result;const auto add=[&](const fs::path&path){NoReparseAncestors(path);HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("Private inventory binding failed");BY_HANDLE_FILE_INFORMATION info{};const bool ok=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&GetFileType(file)==FILE_TYPE_DISK&&((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||info.nNumberOfLinks==1);CloseHandle(file);if(!ok)throw std::runtime_error("Unsafe inventory object refused");Entry entry{info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow,info.dwFileAttributes,{}};if(!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)){if(fs::file_size(path)>65536)throw std::runtime_error("Fixture exceeds payload limit");std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Private read failed");entry.bytes={std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};}result.emplace(path.lexically_relative(root).native(),std::move(entry));};
 add(root);for(const auto&child:fs::recursive_directory_iterator(root))add(child.path());return result;
}
LRESULT CALLBACK HiddenProc(HWND window,UINT message,WPARAM w,LPARAM l){if(message==WM_COMMAND)return WndProc(window,message,w,l);return DefWindowProcW(window,message,w,l);}
void CALLBACK NativeEvent(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId())return;if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW)++nativeShows;else if(event==EVENT_OBJECT_FOCUS)++nativeFocus;else if(event==EVENT_SYSTEM_FOREGROUND)++nativeForeground;}
void Pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){if(message.message==WM_QUIT)throw std::runtime_error("Unexpected quit in queued command proof");if((message.message>=WM_KEYFIRST&&message.message<=WM_KEYLAST)||(message.message>=WM_MOUSEFIRST&&message.message<=WM_MOUSELAST))throw std::runtime_error("Physical input excluded");DispatchMessageW(&message);}}
void Audit(){Pump();unsigned visible=0;EnumWindows([](HWND window,LPARAM data)->BOOL{DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(window))++*reinterpret_cast<unsigned*>(data);return TRUE;},reinterpret_cast<LPARAM>(&visible));GUITHREADINFO state{sizeof(state)};if(!GetGUIThreadInfo(GetCurrentThreadId(),&state)||visible||state.hwndActive||state.hwndFocus||state.hwndCapture||nativeShows||nativeFocus||nativeForeground||forbidden)throw std::runtime_error("Background launch-guard safety failed");}
void Scenario(bool pending,UINT command,const Tree&before){
 ++scenarios;backupSelectors=restorePickers=0;pickerOwnedBusy=pickerSawPending=false;
 g_bClientNotesBusy=false;g_bArchiveTaskInProgress=false;g_bCleanupBusy=false;g_bCloseAllClientsPending=false;g_bDefaultProfileUiBusy=false;g_bExitWhenProfilesClose=false;g_isLaunchInFlight=pending;g_bUiEnabled=!pending;
 Check(g_activeProfiles.empty()&&!g_bClientNotesBusy.load(),"Queued archive scenario begins without a tracked browser or another client-data owner");
 Check(PostMessageW(g_hGui,WM_COMMAND,command,0)!=FALSE,"The archive command is actually queued through the native owner message queue");
 Pump();
 const bool backup=command==IDM_CTX_EXPORT_ALL;
 if(pending){
  if(backup)pendingBackupSelectors=backupSelectors;else pendingRestorePickers=restorePickers;
  Check(backup?backupSelectors==0:restorePickers==0,backup?"A queued backup cannot open its selector during a pending browser launch":"A queued restore cannot open its file picker during a pending browser launch");
  Check(g_isLaunchInFlight.load()&&!g_bUiEnabled&&!g_bClientNotesBusy.load()&&!g_bArchiveTaskInProgress,
        "A rejected queued command preserves the pending launch's disabled UI and independent busy ownership");
 }else{
  Check(backup?backupSelectors==1&&restorePickers==0:restorePickers==1&&backupSelectors==0,
        backup?"An idle backup control reaches exactly its cancelled client selector":"An idle restore control reaches exactly its cancelled archive picker");
  Check(pickerOwnedBusy&&!pickerSawPending,"An ordinary cancelled archive picker runs under its own client-data busy guard");
  Check(!g_isLaunchInFlight.load()&&g_bUiEnabled&&!g_bClientNotesBusy.load()&&!g_bArchiveTaskInProgress,
        "Ordinary picker cancellation releases only its own busy state and restores idle UI availability");
 }
 Check(SnapshotTree(g_sDataDir)==before,"Queued dispatch and cancelled controls preserve exact private client bytes and filesystem identities");Audit();
}
} // namespace

static INT_PTR WINAPI ObserveDialog(HINSTANCE,LPCWSTR resource,HWND,DLGPROC,LPARAM){
 if(resource!=MAKEINTRESOURCEW(IDD_BACKUP_CLIENTS)){++forbidden;return IDCANCEL;}
 ++backupSelectors;pickerOwnedBusy=g_bClientNotesBusy.load();pickerSawPending=g_isLaunchInFlight.load();return IDCANCEL;
}
static BOOL WINAPI ObserveOpenPicker(OPENFILENAMEW*){++restorePickers;pickerOwnedBusy=g_bClientNotesBusy.load();pickerSawPending=g_isLaunchInFlight.load();return FALSE;}
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR,LPCWSTR,UINT flags){++forbidden;return flags&MB_YESNO?IDNO:IDCANCEL;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path&,std::wstring&){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path&){}

int wmain(int argc,wchar_t**argv){
 std::vector<HWINEVENTHOOK> events;HHOOK cbt=nullptr;
 try{
  std::cout<<std::unitbuf;if(argc!=3||wcscmp(argv[1],L"--headless"))throw std::runtime_error("Explicit headless fixture required");
  const auto fixture=Reserve(argv[2]);g_sDataDir=fixture/L"data";fs::create_directories(g_sDataDir/L"Sites/Alpha");
  {std::ofstream output(g_sDataDir/L"Sites/Alpha/ctSpaces-client-notes.txt",std::ios::binary);output<<"Private client note retained across cancelled archive commands\r\n";if(!output)throw std::runtime_error("Private note seed failed");}
  g_sConfigPath=g_sDataDir/L"config.ini";g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();
  cbt=SetWindowsHookExW(WH_CBT,BackgroundCbt,nullptr,GetCurrentThreadId());if(!cbt)throw std::runtime_error("No-activation guard unavailable");
  for(DWORD event:{EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS,EVENT_SYSTEM_FOREGROUND}){const auto hook=SetWinEventHook(event,event,nullptr,NativeEvent,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);if(!hook)throw std::runtime_error("Native background observer unavailable");events.push_back(hook);}
  WNDCLASSW cls{};cls.hInstance=g_hInst;cls.lpfnWndProc=HiddenProc;cls.lpszClassName=L"ArchiveLaunchGuardPrivateHidden";if(!RegisterClassW(&cls))throw std::runtime_error("Hidden class unavailable");
  g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,cls.lpszClassName,L"Private archive launch guard",WS_OVERLAPPED,0,0,600,240,nullptr,nullptr,g_hInst,nullptr);if(!g_hGui)throw std::runtime_error("Hidden owner unavailable");
  const auto before=SnapshotTree(g_sDataDir);
  Scenario(true,IDM_CTX_EXPORT_ALL,before);Scenario(true,IDM_CTX_RESTORE_ALL,before);
  Scenario(false,IDM_CTX_EXPORT_ALL,before);Scenario(false,IDM_CTX_RESTORE_ALL,before);
  unsigned long long fixtureBytes=0;for(const auto&[name,entry]:SnapshotTree(fixture)){(void)name;fixtureBytes+=entry.bytes.size();}
  Check(fixtureBytes<=65536,"All private client fixture bytes remain below sixty-four KiB");
  Audit();Check(!forbidden&&!nativeShows&&!nativeFocus&&!nativeForeground,"No native show/focus/foreground, input, launch, registry, clipboard or unexpected modal calls occurred");
  DestroyWindow(g_hGui);g_hGui=nullptr;for(auto hook:events)UnhookWinEvent(hook);events.clear();UnhookWindowsHookEx(cbt);cbt=nullptr;
  std::cout<<"PENDING_BACKUP_SELECTORS="<<pendingBackupSelectors<<"\nPENDING_RESTORE_PICKERS="<<pendingRestorePickers<<"\nPHYSICAL_FIXTURE_BYTES="<<fixtureBytes<<"\nQUEUED_COMMAND_BOUNDARY_ONLY=1\nVERSION="<<CTSPACES_VERSION_TEXT<<"\nCHECKS="<<checks<<"\nSCENARIOS="<<scenarios<<"\nFAILURES="<<failures<<"\nNATIVE_EVENTS="<<nativeShows<<'/'<<nativeFocus<<'/'<<nativeForeground<<"\nFORBIDDEN="<<forbidden<<"\nREGRESSION_PASSED="<<(failures==0)<<'\n';
  return failures?1:0;
 }catch(const std::exception&error){std::cerr<<"Archive launch guard fixture failed: "<<error.what()<<'\n';if(g_hGui)DestroyWindow(g_hGui);for(auto hook:events)UnhookWinEvent(hook);if(cbt)UnhookWindowsHookEx(cbt);return 2;}
}
