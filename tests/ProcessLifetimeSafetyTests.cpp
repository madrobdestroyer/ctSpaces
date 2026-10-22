// Background process-lifetime model. Only owned hidden console children are
// created; all production close delivery and termination calls are suppressed.
#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <stdexcept>
#include <set>
#include <vector>
static DWORD stalePid=0;
static HANDLE unrelatedChild=nullptr;
static bool virtualWindow=false;
static const HWND modeledWindow=reinterpret_cast<HWND>(static_cast<uintptr_t>(0x123451));
static unsigned openedByPid=0,terminationRequests=0,closePosts=0,endSessionPosts=0,queryRequests=0,presentationRequests=0,externalLaunches=0;
static bool failDuplicate=false;
static HANDLE exitAuthorizedChildEvent=nullptr;
static DWORD authorizedExitPid=0;
static std::vector<DWORD> terminatedActualPids;
static std::set<HANDLE> lookupHandles;
static HANDLE WINAPI ModelOpenProcess(DWORD,BOOL,DWORD pid){
  ++openedByPid;if(pid!=stalePid||!unrelatedChild){SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
  HANDLE copy=nullptr;if(!DuplicateHandle(GetCurrentProcess(),unrelatedChild,GetCurrentProcess(),&copy,0,FALSE,DUPLICATE_SAME_ACCESS))return nullptr;
  lookupHandles.insert(copy);return copy;
}
static DWORD WINAPI ModelProcessId(HANDLE handle){return lookupHandles.contains(handle) ? stalePid : ::GetProcessId(handle);}
static BOOL WINAPI ModelCloseHandle(HANDLE handle){lookupHandles.erase(handle);return ::CloseHandle(handle);}
static BOOL WINAPI SuppressTerminate(HANDLE handle,UINT){
  terminatedActualPids.push_back(::GetProcessId(handle));
  if(::GetProcessId(handle)==::GetProcessId(unrelatedChild))++terminationRequests;
  if(exitAuthorizedChildEvent&&::GetProcessId(handle)==authorizedExitPid)SetEvent(exitAuthorizedChildEvent);
  return TRUE;
}
static BOOL WINAPI ModelDuplicate(HANDLE sourceProcess,HANDLE source,HANDLE targetProcess,LPHANDLE target,DWORD access,BOOL inherit,DWORD options){
  if(failDuplicate){if(target)*target=nullptr;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return FALSE;}
  return ::DuplicateHandle(sourceProcess,source,targetProcess,target,access,inherit,options);
}
static BOOL WINAPI ModelEnumWindows(WNDENUMPROC callback,LPARAM data){if(virtualWindow)callback(modeledWindow,data);return TRUE;}
static DWORD WINAPI ModelWindowProcess(HWND window,LPDWORD pid){if(window!=modeledWindow){if(pid)*pid=0;return 0;}if(pid)*pid=stalePid;return 1;}
static BOOL WINAPI ModelVisible(HWND window){return window==modeledWindow;}
static int WINAPI ModelCaptionLength(HWND window){return window==modeledWindow?12:0;}
static LONG_PTR WINAPI ModelWindowLong(HWND,int){return 0;}
static HWND WINAPI ModelOwner(HWND,UINT){return nullptr;}
static BOOL WINAPI SuppressPost(HWND window,UINT message,WPARAM,LPARAM){if(window!=modeledWindow)throw std::runtime_error("Refused nonmodeled post target");if(message==WM_CLOSE)++closePosts;if(message==WM_ENDSESSION)++endSessionPosts;return TRUE;}
static LRESULT WINAPI SuppressSend(HWND window,UINT message,WPARAM,LPARAM,UINT,UINT,PDWORD_PTR result){if(window!=modeledWindow)throw std::runtime_error("Refused nonmodeled send target");if(message==WM_QUERYENDSESSION)++queryRequests;if(result)*result=TRUE;return TRUE;}
static BOOL WINAPI NoShow(HWND,int){++presentationRequests;return FALSE;}
static HWND WINAPI NoFocus(HWND){++presentationRequests;return nullptr;}
static BOOL WINAPI NoForeground(HWND){++presentationRequests;return FALSE;}
static HWND WINAPI NoActive(HWND){++presentationRequests;return nullptr;}
static BOOL WINAPI NoRaise(HWND){++presentationRequests;return FALSE;}
static BOOL WINAPI NoPosition(HWND,HWND,int,int,int,int,UINT){++presentationRequests;return FALSE;}
static BOOL WINAPI NoPaint(HWND,const RECT*,BOOL){++presentationRequests;return FALSE;}
static BOOL WINAPI NoRedraw(HWND,const RECT*,HRGN,UINT){++presentationRequests;return FALSE;}
static HINSTANCE WINAPI NoShell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){++externalLaunches;return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);}
static BOOL WINAPI NoShellEx(SHELLEXECUTEINFOW*){++externalLaunches;return FALSE;}
static BOOL WINAPI NoCreateProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){++externalLaunches;return FALSE;}
#define OpenProcess ModelOpenProcess
#define GetProcessId ModelProcessId
#define CloseHandle ModelCloseHandle
#define TerminateProcess SuppressTerminate
#define DuplicateHandle ModelDuplicate
#define EnumWindows ModelEnumWindows
#define GetWindowThreadProcessId ModelWindowProcess
#define IsWindowVisible ModelVisible
#define GetWindowTextLengthW ModelCaptionLength
#define GetWindowLongPtrW ModelWindowLong
#define GetWindow ModelOwner
#define PostMessageW SuppressPost
#define SendMessageTimeoutW SuppressSend
#define ShowWindow NoShow
#define SetFocus NoFocus
#define SetForegroundWindow NoForeground
#define SetActiveWindow NoActive
#define BringWindowToTop NoRaise
#define SetWindowPos NoPosition
#define InvalidateRect NoPaint
#define RedrawWindow NoRedraw
#define ShellExecuteW NoShell
#define ShellExecuteExW NoShellEx
#define CreateProcessW NoCreateProcess
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain ProductionEntryNotRun
#ifdef CTSPACES_PROCESS_LIFETIME_BEFORE
#include "../build/app-audit-round6-2026-10-15/browser-launch/close-lifetime/ctSpaces-before.cpp"
#elif defined(CTSPACES_PROCESS_LIFETIME_AFTER)
#include "../build/app-audit-round6-2026-10-15/browser-launch/close-lifetime/ctSpaces-after.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef CloseHandle
#undef GetProcessId
#undef CreateProcessW
#undef DuplicateHandle
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR,LPCWSTR,UINT){return IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path&,std::wstring&){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path&){}
static unsigned checks=0,failures=0;
static void Check(bool condition,const char*message){++checks;if(!condition)++failures;std::cout<<(condition?"PASS ":"FAIL ")<<message<<'\n';}
template<class T>static void BindLifetime(T&info,const std::shared_ptr<void>&token){if constexpr(requires(T item){item.process;})info.process=token;}
struct Child {
  HANDLE event=nullptr,process=nullptr;DWORD pid=0;
  void Start(const wchar_t*label){
    const auto name=L"Local\\ctSpaces-lifetime-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+label;
    event=CreateEventW(nullptr,TRUE,FALSE,name.c_str());if(!event)throw std::runtime_error("Owned child event creation failed");
    wchar_t exe[32768]{};if(!GetModuleFileNameW(nullptr,exe,32768))throw std::runtime_error("Self path unavailable");
    std::wstring command=L"\""+std::wstring(exe)+L"\" --owned-child \""+name+L"\"";
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION created{};
    if(!CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS,nullptr,nullptr,&startup,&created))throw std::runtime_error("Owned hidden child launch failed");
    CloseHandle(created.hThread);process=created.hProcess;pid=created.dwProcessId;
  }
  void Stop(){if(event)SetEvent(event);if(process){WaitForSingleObject(process,10000);CloseHandle(process);process=nullptr;}if(event){CloseHandle(event);event=nullptr;}}
  ~Child(){Stop();}
};
int wmain(int argc,wchar_t**argv){
  if(argc==3&&std::wstring(argv[1])==L"--owned-child"){HANDLE event=OpenEventW(SYNCHRONIZE,FALSE,argv[2]);if(!event)return 2;const DWORD waited=WaitForSingleObject(event,45000);CloseHandle(event);return waited==WAIT_OBJECT_0?0:3;}
  try{
    Child original,unrelated;original.Start(L"original");unrelated.Start(L"unrelated");
    stalePid=original.pid;unrelatedChild=unrelated.process;
    SetEvent(original.event);if(WaitForSingleObject(original.process,10000)!=WAIT_OBJECT_0)throw std::runtime_error("Original child did not exit");
    HANDLE originalToken=nullptr;if(!DuplicateHandle(GetCurrentProcess(),original.process,GetCurrentProcess(),&originalToken,0,FALSE,DUPLICATE_SAME_ACCESS))throw std::runtime_error("Original lifetime duplicate failed");
    std::shared_ptr<void> lifetime(originalToken,[](void*handle){::CloseHandle(handle);});
    ActiveProfileInfo active{stalePid,L"owned-fixture.exe",L"private-fixture",ProfileType::Standard};BindLifetime(active,lifetime);g_activeProfiles[{L"Alpha",BrowserKind::Edge}]=active;
    g_sessions={{L"Alpha",BrowserKind::Edge,stalePid}};
    Check(WaitForSingleObject(originalToken,0)==WAIT_OBJECT_0,"Original process lifetime is signaled while its UI session remains tracked");
    Check(WaitForSingleObject(unrelated.process,0)==WAIT_TIMEOUT,"Unrelated owned child is alive before modeled stale lookup");
    virtualWindow=false;RequestSessionClose(stalePid);
    Check(terminationRequests==0,"Individual close never targets the replacement PID lookup object");
    virtualWindow=true;RequestSessionCloseNormally(stalePid);
    Check(closePosts==0,"Normal close never posts WM_CLOSE through stale process identity");
    RequestSessionShutdown(stalePid);
    Check(queryRequests==0&&endSessionPosts==0,"Shutdown never queries or ends a replacement window session");
    Check(WaitForSingleObject(unrelated.process,0)==WAIT_TIMEOUT,"All modeled unsafe actions were suppressed; unrelated child is still alive");
    Check(presentationRequests==0&&externalLaunches==0,"Production paths created no windows, presentation or external launches");
    std::cout<<"Fault model: original PID="<<stalePid<<"; unrelated owned child PID="<<unrelated.pid<<"; logical reused-PID opens="<<openedByPid<<"; suppressed termination="<<terminationRequests<<"; suppressed close="<<closePosts<<"; suppressed end-session="<<endSessionPosts<<"\n";
#ifndef CTSPACES_PROCESS_LIFETIME_BEFORE
    const auto noAction=[&](const char*message){
      const unsigned opens=openedByPid,terms=terminationRequests,posts=closePosts,ends=endSessionPosts,queries=queryRequests;
      RequestSessionClose(stalePid);RequestSessionCloseNormally(stalePid);RequestSessionShutdown(stalePid);
      Check(openedByPid==opens&&terminationRequests==terms&&closePosts==posts&&endSessionPosts==ends&&queryRequests==queries,message);
    };
    g_activeProfiles.clear();stalePid=unrelated.pid;
    ActiveProfileInfo live{stalePid,L"owned-fixture.exe",L"private-fixture",ProfileType::Standard};
    g_activeProfiles[{L"Alpha",BrowserKind::Edge}]=live;virtualWindow=true;
    noAction("Missing lifetime token fails closed for all three close paths without reopening a PID");
    live.process=lifetime;g_activeProfiles[{L"Alpha",BrowserKind::Edge}]=live;
    noAction("Wrong-process lifetime token fails closed for all three close paths");
    live.process=std::shared_ptr<void>(INVALID_HANDLE_VALUE,[](void*){});g_activeProfiles[{L"Alpha",BrowserKind::Edge}]=live;
    noAction("Invalid lifetime token fails closed for all three close paths");
    Check(!RetainLaunchedProcess(nullptr,stalePid)&&!RetainLaunchedProcess(INVALID_HANDLE_VALUE,stalePid)&&!RetainLaunchedProcess(unrelated.process,original.pid),"Launch retention rejects null, invalid and mismatched known handles");
    failDuplicate=true;Check(!RetainLaunchedProcess(unrelated.process,stalePid),"Launch retention cleanly rejects native DuplicateHandle failure");failDuplicate=false;
    Check(WaitForSingleObject(unrelated.process,0)==WAIT_TIMEOUT,"A failed retention attempt does not close or stop the source process");
    live.process=RetainLaunchedProcess(unrelated.process,stalePid);Check(static_cast<bool>(live.process),"Live original process lifetime is retained");g_activeProfiles[{L"Alpha",BrowserKind::Edge}]=live;
    const unsigned opens=openedByPid,posts=closePosts,ends=endSessionPosts,queries=queryRequests;
    Check(RequestSessionCloseNormally(stalePid)&&closePosts==posts+1,"Live original normal close posts only to its modeled authorized window");
    Check(RequestSessionShutdown(stalePid)&&queryRequests==queries+1&&endSessionPosts==ends+1,"Live original shutdown reaches query/end-session through the retained lifetime");
    virtualWindow=false;const size_t requested=terminatedActualPids.size();
    Check(RequestSessionClose(stalePid)&&terminatedActualPids.size()==requested+1&&terminatedActualPids.back()==unrelated.pid,"Windowless live original close uses its exact retained object for termination");
    Check(openedByPid==opens&&WaitForSingleObject(unrelated.process,0)==WAIT_TIMEOUT,"Authorized close requests never reopen PID or actually terminate the owned child");
    auto failedShutdown=std::make_shared<CoordinatedShutdownState>();failedShutdown->retryProcessIds.push_back(stalePid);
    const size_t beforeMissing=terminatedActualPids.size();
    Check(!FinishCoordinatedShutdown(failedShutdown)&&openedByPid==opens&&terminatedActualPids.size()==beforeMissing,"Coordinated missing ownership reports failure without reopening or terminating another PID");
    auto exitedShutdown=std::make_shared<CoordinatedShutdownState>();exitedShutdown->targets.push_back({original.pid,lifetime});
    Check(FinishCoordinatedShutdown(exitedShutdown)&&exitedShutdown->targets.empty()&&terminatedActualPids.size()==beforeMissing,"Coordinated shutdown treats signaled original as complete without targeting a replacement");
    auto liveShutdown=std::make_shared<CoordinatedShutdownState>();liveShutdown->targets.push_back({stalePid,live.process});g_activeProfiles.clear();
    exitAuthorizedChildEvent=unrelated.event;authorizedExitPid=unrelated.pid;
    Check(FinishCoordinatedShutdown(liveShutdown)&&liveShutdown->targets.empty()&&terminatedActualPids.back()==unrelated.pid,"Coordinated snapshot retains the exact launched object after active bookkeeping is removed");
    exitAuthorizedChildEvent=nullptr;authorizedExitPid=0;
    Check(WaitForSingleObject(unrelated.process,0)==WAIT_OBJECT_0,"Authorized coordinated fixture child exits only through its own event");
    Child ownership;ownership.Start(L"ownership");
    auto retained=RetainLaunchedProcess(ownership.process,ownership.pid);Check(static_cast<bool>(retained),"Launch retention creates a separately owned native process handle");
    HANDLE retainedHandle=retained.get();CloseHandle(ownership.process);ownership.process=nullptr;
    Check(GetProcessId(retainedHandle)==ownership.pid&&WaitForSingleObject(retainedHandle,0)==WAIT_TIMEOUT,"Retained duplicate survives closure of the original launch handle");
    auto shared=retained;retained.reset();DWORD flags=0;
    Check(GetHandleInformation(retainedHandle,&flags)!=FALSE,"Another shared owner keeps the retained process handle valid");
    SetEvent(ownership.event);Check(WaitForSingleObject(shared.get(),10000)==WAIT_OBJECT_0,"Retained duplicate observes original lifetime exit after its launch handle closed");
    shared.reset();SetLastError(ERROR_SUCCESS);
    Check(!GetHandleInformation(retainedHandle,&flags)&&GetLastError()==ERROR_INVALID_HANDLE,"Last lifetime owner closes the retained native handle exactly once");
    Check(presentationRequests==0&&externalLaunches==0&&openedByPid==0,"All repaired paths and ownership controls stay background-only without numeric PID adoption");
#endif
    std::cout<<checks<<" process lifetime safety checks; "<<failures<<" failures. This injects lookup identity and does not claim physical Windows PID reuse.\n";
    g_activeProfiles.clear();g_sessions.clear();unrelatedChild=nullptr;return failures?1:0;
  }catch(const std::exception&error){std::cerr<<"FAIL setup: "<<error.what()<<'\n';return 2;}
}
