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
static DWORD WINAPI BeforeCacheAttributes(LPCWSTR path);
#define GetFileAttributesW BeforeCacheAttributes
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain MaintenanceProductionEntryNotRun
#ifdef CTSPACES_MAINTENANCE_BEFORE
#include "../build/app-audit-round6-2026-10-15/maintenance/ctSpaces-before.cpp"
#elif defined(CTSPACES_MAINTENANCE_FROZEN)
#include "../build/app-audit-round6-2026-10-15/maintenance/ctSpaces-fixed.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef GetFileAttributesW
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "DirectoryJunctionFixture.h"
static unsigned checks=0,dialogRequests=0;
static bool enabled=false,attempted=false,mutated=false;
static fs::path fixture,targetCache,foreignProfile,displaced;
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR,LPCWSTR,UINT) {
  ++dialogRequests;return IDNO;
}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path&,std::wstring&) {return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path&) {}
static void Check(bool condition,const char* description) {
  ++checks;if(!condition)throw std::runtime_error(description);
}
static void Put(const fs::path& path,const std::string& value) {
  fs::create_directories(path.parent_path());std::ofstream stream(path,std::ios::binary);stream<<value;
  Check(stream.good(),"Write disposable maintenance fixture");
}
static std::string Read(const fs::path& path) {
  std::ifstream stream(path,std::ios::binary);return {std::istreambuf_iterator<char>(stream),{}};
}
static DWORD WINAPI BeforeCacheAttributes(LPCWSTR path) {
  const DWORD attributes=::GetFileAttributesW(path);
  if(enabled && attributes!=INVALID_FILE_ATTRIBUTES && fs::path(path)==targetCache) {
    enabled=false;attempted=true;std::error_code error;
    fs::rename(targetCache,displaced,error);
    if(!error)fs::rename(foreignProfile,targetCache,error);
    mutated=!error;
  }
  return attributes;
}
int wmain(int argc,wchar_t** argv) {
  fixture=fs::current_path()/L"build/app-audit-round6-2026-10-15/maintenance"/
      (L"fixture-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  try {
    const unsigned mode=argc==2?static_cast<unsigned>(_wtoi(argv[1])):0;
    Check(mode<8,"Select a bounded cache deletion scenario");
    const auto profile=fixture/L"Alpha/Profile";
    targetCache=mode==7?profile/L"cache2":profile/L"Default/Cache";
    foreignProfile=fixture/L"Bravo/Profile/Default";
    displaced=fixture/L"original-cache";
    if(mode==6)Put(targetCache,"CACHE PATH IS A FILE");
    else Put(targetCache/L"nested/cache.bin","ALPHA CACHE");
    Put(profile/L"Default/Cookies","ALPHA PRIVATE COOKIES");
    Put(foreignProfile/L"Cookies","BRAVO PRIVATE COOKIES");
    Put(foreignProfile/L"private-session","BRAVO PRIVATE SESSION");
    if(mode==4)Check(CtTest::CreateDirectoryJunction(targetCache/L"linked-foreign",foreignProfile),
        "Create disposable nested junction toward unrelated profile bytes");
    HANDLE locked=INVALID_HANDLE_VALUE;
    if(mode==5) {
      locked=CreateFileW((targetCache/L"nested/cache.bin").c_str(),GENERIC_READ,
          FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
      Check(locked!=INVALID_HANDLE_VALUE,"Hold cache file against deletion without opening a browser");
    }
    enabled=mode==0;
    bool removed=false;
    const auto relative=mode==2?fs::path(L"Default/MissingCache"):
        mode==3?fs::path(L"../../Bravo/Profile/Default"):
        mode==7?fs::path(L"cache2"):fs::path(L"Default/Cache");
    const bool result=RemoveNestedCacheDirectory(profile,relative,removed);
    if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);
    enabled=false;
    const bool foreignLost=mutated && !fs::exists(targetCache/L"Cookies") &&
        !fs::exists(foreignProfile/L"Cookies");
    std::cout<<"MODE="<<mode<<" ATTEMPTED="<<attempted<<" MUTATED="<<mutated
        <<" RESULT_OK="<<result<<" REMOVED="<<removed<<" FOREIGN_DATA_LOST="<<foreignLost<<'\n';
    std::wcout<<L"Fixture retained: "<<fixture<<L'\n';
    Check(mode!=0 || attempted,"Fault reaches the actual target preflight boundary");
    Check(!foreignLost,"Vacuum never recurses into a different substituted client profile");
    Check(!mutated,"The original cache namespace remains bound through deletion");
    Check((mode<3 || mode==7)?result && removed==(mode!=2):!result && !removed,
          "Normal, missing, invalid, junction and locked cache results remain explicit and safe");
    Check(Read(profile/L"Default/Cookies")=="ALPHA PRIVATE COOKIES" &&
        Read(foreignProfile/L"Cookies")=="BRAVO PRIVATE COOKIES" &&
        Read(foreignProfile/L"private-session")=="BRAVO PRIVATE SESSION",
          "Selected and unrelated cookies and sessions survive cache cleanup");
    Check(mode==6?Read(targetCache)=="CACHE PATH IS A FILE":
        mode==2 || (mode>=3 && mode<=5)?Read(targetCache/L"nested/cache.bin")=="ALPHA CACHE":
        !fs::exists(targetCache),
          "Only the requested cache tree is removed");
    Check(deniedPresentationCalls==0 && deniedLaunchCalls==0 && dialogRequests==0,
          "Filesystem audit requests no focus, window presentation, process launch or native dialog");
    std::cout<<"Client cache maintenance: "<<checks<<" assertions passed\n";return 0;
  } catch(const std::exception& error) {
    std::cerr<<"FAIL: "<<error.what()<<" after "<<checks<<" assertions\n";
    std::wcout<<L"Fixture retained: "<<fixture<<L'\n';return 1;
  }
}
