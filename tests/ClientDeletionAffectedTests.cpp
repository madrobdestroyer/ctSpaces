// Focused profile-creation ownership regressions. This suite never starts the app
// entry point, creates visible windows, or permits foreground/input APIs.
#include <windows.h>
#include <shellapi.h>

static unsigned deniedPresentationCalls = 0;
static unsigned deniedLaunchCalls = 0;
static BOOL WINAPI BoundaryNoShowWindow(HWND, int) {
  ++deniedPresentationCalls; return FALSE;
}
static HWND WINAPI BoundaryNoSetFocus(HWND) {
  ++deniedPresentationCalls; return nullptr;
}
static BOOL WINAPI BoundaryNoSetForegroundWindow(HWND) {
  ++deniedPresentationCalls; return FALSE;
}
static HWND WINAPI BoundaryNoSetActiveWindow(HWND) {
  ++deniedPresentationCalls; return nullptr;
}
static BOOL WINAPI BoundarySafeSetWindowPos(HWND window, HWND after, int x,
    int y, int width, int height, UINT flags) {
  if (!(flags & SWP_NOACTIVATE) || (flags & SWP_SHOWWINDOW)) {
    ++deniedPresentationCalls; return FALSE;
  }
  return ::SetWindowPos(window, after, x, y, width, height, flags);
}
static HDWP WINAPI BoundarySafeDeferWindowPos(HDWP defer, HWND window,
    HWND after, int x, int y, int width, int height, UINT flags) {
  if (!(flags & SWP_NOACTIVATE) || (flags & SWP_SHOWWINDOW)) {
    ++deniedPresentationCalls; return nullptr;
  }
  return ::DeferWindowPos(defer, window, after, x, y, width, height, flags);
}
static HINSTANCE WINAPI BoundaryNoShellExecuteW(HWND, LPCWSTR, LPCWSTR,
    LPCWSTR, LPCWSTR, INT) {
  ++deniedLaunchCalls; return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);
}
static BOOL WINAPI BoundaryNoShellExecuteExW(SHELLEXECUTEINFOW *) {
  ++deniedLaunchCalls; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
}
static BOOL WINAPI BoundaryNoCreateProcessW(LPCWSTR, LPWSTR,
    LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID,
    LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION) {
  ++deniedLaunchCalls; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
}
#define ShowWindow BoundaryNoShowWindow
#define SetFocus BoundaryNoSetFocus
#define SetForegroundWindow BoundaryNoSetForegroundWindow
#define SetActiveWindow BoundaryNoSetActiveWindow
#define SetWindowPos BoundarySafeSetWindowPos
#define DeferWindowPos BoundarySafeDeferWindowPos
#define ShellExecuteW BoundaryNoShellExecuteW
#define ShellExecuteExW BoundaryNoShellExecuteExW
#define CreateProcessW BoundaryNoCreateProcessW
#define wmain ExistingClientIsolationEntry
#include "ClientIsolationTests.cpp"
#undef wmain
int wmain() {
  fixture=fs::current_path()/L"build/app-audit-round5-2026-10-15/clients"/
      (L"affected-delete-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  try {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);g_hInst=GetModuleHandleW(nullptr);
    g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();
    g_hGui=CreateWindowExW(0,L"STATIC",L"Message-only deletion regression",0,
        0,0,1,1,HWND_MESSAGE,nullptr,g_hInst,nullptr);
    Check(g_hGui!=nullptr && !IsWindowVisible(g_hGui),"Message-only fixture stays invisible");
    DeleteFaultCases();
    Check(deniedPresentationCalls==0 && deniedLaunchCalls==0,
          "Affected deletion cases request no presentation, focus or launch");
    DestroyWindow(g_hGui);g_hGui=nullptr;CoUninitialize();
    std::wcout<<L"Affected deletion regressions: "<<checks<<L" assertions, "<<scenarios<<L" scenarios passed; "<<fixture<<L'\n';
    return 0;
  } catch(const std::exception& error) {
    std::cerr<<"FAIL: "<<error.what()<<'\n';
    if(lateLock!=INVALID_HANDLE_VALUE)CloseHandle(lateLock);
    if(g_hGui)DestroyWindow(g_hGui);CoUninitialize();return 1;
  }
}