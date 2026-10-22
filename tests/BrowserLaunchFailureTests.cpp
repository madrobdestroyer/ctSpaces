// Background-only restore preference failure/retry tests. Presentation,
// process opens and external launches are intercepted before Windows executes.
#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
static unsigned presentationRequests=0,launchRequests=0,forbiddenPaint=0;
static std::vector<DWORD> closeTargets;
static BOOL WINAPI NoShow(HWND,int){++presentationRequests;return FALSE;}
static HWND WINAPI NoFocus(HWND){++presentationRequests;return nullptr;}
static BOOL WINAPI NoForeground(HWND){++presentationRequests;return FALSE;}
static HWND WINAPI NoActive(HWND){++presentationRequests;return nullptr;}
static BOOL WINAPI NoRaise(HWND){++presentationRequests;return FALSE;}
static BOOL WINAPI SafePosition(HWND window,HWND after,int x,int y,int width,int height,UINT flags){
  if((flags&SWP_SHOWWINDOW)||!(flags&SWP_NOACTIVATE)){++presentationRequests;return FALSE;}
  return ::SetWindowPos(window,after,x,y,width,height,flags);
}
static void RequireOwned(HWND window){DWORD pid=0;if(!window||!IsWindow(window)||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId()){++forbiddenPaint;throw std::runtime_error("Refused invalid or foreign redraw target");}}
static BOOL WINAPI SafeInvalidate(HWND w,const RECT *r,BOOL erase){RequireOwned(w);return ::InvalidateRect(w,r,erase);}
static BOOL WINAPI SafeRedraw(HWND w,const RECT *r,HRGN region,UINT flags){RequireOwned(w);return ::RedrawWindow(w,r,region,flags);}
static BOOL WINAPI SafePost(HWND w,UINT,WPARAM,LPARAM){RequireOwned(w);return TRUE;}
static HANDLE WINAPI NoProcess(DWORD access,BOOL,DWORD pid){if(access&PROCESS_TERMINATE)closeTargets.push_back(pid);SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
static HINSTANCE WINAPI NoShell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){++launchRequests;return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);}
static BOOL WINAPI NoShellEx(SHELLEXECUTEINFOW *){++launchRequests;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL WINAPI NoCreateProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){++launchRequests;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL WINAPI NoTray(DWORD,PNOTIFYICONDATAW){++launchRequests;return FALSE;}
static BOOL WINAPI NoCursor(LPPOINT point){if(point)*point={};return FALSE;}
static BOOL WINAPI NoCaptureRelease(){++presentationRequests;return FALSE;}
static HWND WINAPI NoCapture(HWND){++presentationRequests;return nullptr;}
static UINT WINAPI FakePopup(HMENU,UINT,int,int,HWND,LPTPMPARAMS){return 0;}
#define ShowWindow NoShow
#define SetFocus NoFocus
#define SetForegroundWindow NoForeground
#define SetActiveWindow NoActive
#define BringWindowToTop NoRaise
#define SetWindowPos SafePosition
#define InvalidateRect SafeInvalidate
#define RedrawWindow SafeRedraw
#define PostMessageW SafePost
#define OpenProcess NoProcess
#define ShellExecuteW NoShell
#define ShellExecuteExW NoShellEx
#define CreateProcessW NoCreateProcess
#define Shell_NotifyIconW NoTray
#define GetCursorPos NoCursor
#define SetCapture NoCapture
#define ReleaseCapture NoCaptureRelease
#define TrackPopupMenuEx FakePopup
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain ProductionEntryNotRun
#ifdef CTSPACES_BROWSER_LAUNCH_FAILURE_SNAPSHOT
#include "../build/app-audit-round6-2026-10-15/browser-launch/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef OpenProcess
#undef PostMessageW
#include "RetainedFixture.h"

// This fixture creates no windows and invokes no launch/close function.
// A real sharing lock on its private config drives the real restore callback.
static unsigned notices=0,checks=0;
static std::wstring lastNotice;
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR message,LPCWSTR,UINT){++notices;lastNotice=message?message:L"";return IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &){}
static void Check(bool condition,const char *message){++checks;if(!condition)throw std::runtime_error(message);}
static std::string Bytes(const fs::path &path){std::ifstream input(path,std::ios::binary);Check(static_cast<bool>(input),"Fixture file is readable");return {std::istreambuf_iterator<char>(input),{}};}
static void PreferenceFixtureAllocationCases(const fs::path &fixture,const fs::path &parent) {
  const auto independent=retained_fixture::CreateRoot(parent,L"preferences");
  const auto marker=fixture/L"allocation-marker.txt";
  {std::ofstream output(marker,std::ios::binary);output<<"first private root";
    if(!output)throw std::runtime_error("Private allocation marker could not be written");}
  Check(independent!=fixture&&fs::is_directory(fixture)&&fs::is_directory(independent)&&
      Bytes(marker)=="first private root"&&!fs::exists(independent/marker.filename()),
      "Two allocations in one process create independent fresh fixture roots");
  const auto collisions=fixture/L"allocation-collision-proof";
  fs::create_directory(collisions);const auto occupied=collisions/L"occupied";
  Check(fs::create_directory(occupied),"The allocator collision oracle owns its pre-existing candidate");
  const auto sentinel=occupied/L"sentinel.txt";
  {std::ofstream output(sentinel,std::ios::binary);output<<"retain existing evidence";
    if(!output)throw std::runtime_error("Private collision sentinel could not be written");}
  unsigned offered=0;
  const auto allocated=retained_fixture::CreateRoot(collisions,L"preferences",[&]{return ++offered==1?L"occupied":L"fresh";});
  Check(offered==2&&allocated==collisions/L"fresh"&&fs::is_directory(allocated)&&
      Bytes(sentinel)=="retain existing evidence"&&!fs::exists(allocated/sentinel.filename()),
      "A pre-existing candidate and sentinel survive atomic allocation collision fallback");
  unsigned exhausted=0;bool refused=false;
  try{retained_fixture::CreateRoot(collisions,L"preferences",[&]{++exhausted;return L"occupied";});}
  catch(const std::runtime_error &){refused=true;}
  Check(refused&&exhausted==16&&Bytes(sentinel)=="retain existing evidence",
      "Exhausted fixture collisions fail without reusing or changing retained evidence");
  bool parentRefused=false;
  try{retained_fixture::CreateRoot(sentinel,L"preferences");}
  catch(const fs::filesystem_error &){parentRefused=true;}
  Check(parentRefused&&fs::is_regular_file(sentinel)&&Bytes(sentinel)=="retain existing evidence",
      "An unusable fixture parent fails safely without altering its existing sentinel");
}
static std::vector<std::wstring> Preferences(){std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);return g_clientsWithoutTabRestore;}
static void Seed(const std::vector<std::wstring> &values){std::lock_guard<std::mutex> lock(g_restoreTabsPreferenceMutex);g_clientsWithoutTabRestore=values;}
#include "RestoreTabsDeltaTests.inl"
int wmain(){
  fs::path fixture;
  try{
    const auto parent=fs::current_path()/L"build"/L"app-audit-round6-2026-10-15"/L"browser-launch"/L"fixtures";
    fixture=retained_fixture::CreateRoot(parent,L"preferences");
    PreferenceFixtureAllocationCases(fixture,parent);
    fs::create_directories(fixture/L"Sites"/L"Alpha");fs::create_directory(fixture/L"Sites"/L"Beta");
    g_sDataDir=fixture;g_sConfigPath=fixture/L"config.ini";g_selectedBrowser=BrowserKind::Edge;
    unsigned scenarios=0;
    for(const std::wstring client:{L"Alpha",L"Beta"})for(BrowserKind browser:{BrowserKind::Edge,BrowserKind::Chrome,BrowserKind::Brave,BrowserKind::Firefox})for(bool initiallyDisabled:{false,true}){
      const std::wstring target=MakeClientBrowserPreferenceKey(client,browser);
      const std::wstring otherClient=client==L"Alpha"?L"Beta":L"Alpha";
      const BrowserKind otherBrowser=browser==BrowserKind::Firefox?BrowserKind::Edge:BrowserKind::Firefox;
      std::vector<std::wstring> before{MakeClientBrowserPreferenceKey(otherClient,browser),MakeClientBrowserPreferenceKey(client,otherBrowser)};
      if(initiallyDisabled)before.push_back(target);
      Seed(before);Check(SaveRestoreTabsPreferences(),"Restore preference baseline saves");
      g_sessions={{client,browser,0x7ffff351}};g_iSelectedSessionTab=1;
      const auto bytes=Bytes(g_sConfigPath);const unsigned noticeBefore=notices;
      HANDLE locked=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
      Check(locked!=INVALID_HANDLE_VALUE,"Private config denies atomic replacement");
      ToggleRestoreTabsForSelectedClient();
      CloseHandle(locked);
      Check(Preferences()==before,"Failed restore save rolls back every in-memory client/browser preference");
      Check(Bytes(g_sConfigPath)==bytes,"Failed restore save leaves exact saved bytes unchanged");
      Check(notices==noticeBefore+1&&lastNotice.find(L"could not be saved safely")!=std::wstring::npos,"Failed restore save is reported through the intercepted notice");
      ToggleRestoreTabsForSelectedClient();
      auto wanted=before;if(initiallyDisabled)wanted.pop_back();else wanted.push_back(target);
      Check(Preferences()==wanted,"Unlocked retry changes only the selected session's client/browser key");
      Check(ShouldRestoreTabsForClient(client,browser)==initiallyDisabled,"Unlocked retry exposes the requested restore behavior");
      Check(!ShouldRestoreTabsForClient(otherClient,browser)&&!ShouldRestoreTabsForClient(client,otherBrowser),"Other clients and browser slots retain their restore setting");
      Seed({});LoadRestoreTabsPreferences();
      Check(Preferences()==wanted,"Saved retry reloads the exact isolated restore preferences");
      ++scenarios;
    }
    Seed({L"Alpha|edge",L"Missing|chrome"});Check(SaveRestoreTabsPreferences(),"Pruning baseline saves");
    const auto before=Preferences();const auto bytes=Bytes(g_sConfigPath);
    HANDLE locked=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    Check(locked!=INVALID_HANDLE_VALUE,"Private prune config denies replacement");PruneRestoreTabsPreferences();CloseHandle(locked);
    Check(Preferences()==before&&Bytes(g_sConfigPath)==bytes,"Failed pruning rolls back exact memory and saved bytes");
    PruneRestoreTabsPreferences();Check(Preferences()==std::vector<std::wstring>{L"Alpha|edge"},"Clean pruning retry removes only the missing client key");
    TestRestoreTabsDelta(fixture/L"targeted-delta");
    Check(g_activeProfiles.empty()&&closeTargets.empty(),"Preference failure checks never open or close tracked processes");
    Check(!g_hGui&&!g_hClientEdit&&!g_hComboClient&&!g_hBtnRestoreTabs,"Native fixture creates no HWNDs");
    Check(launchRequests==0&&presentationRequests==0&&forbiddenPaint==0,"Zero external launches, presentation requests or repaint calls");
    std::cout<<checks<<" restore preference failure/retry checks passed; "<<scenarios<<" client/browser/initial-state scenarios; zero windows, launches or presentation requests\n";
    return 0;
  }catch(const std::exception &error){std::cerr<<"FAIL after "<<checks<<" checks: "<<error.what()<<"\nFixture retained: "<<fixture.string()<<'\n';return 1;}
}


