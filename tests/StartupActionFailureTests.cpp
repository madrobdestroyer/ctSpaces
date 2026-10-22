// Background-only startup action/parser failure tests. Presentation,
// process opens and external launches are intercepted before Windows executes.
#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
static unsigned presentationRequests=0,launchRequests=0,forbiddenPaint=0,queuedPaintRequests=0;
static std::vector<DWORD> closeTargets;
static std::vector<DWORD> waitTargets;
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
static BOOL WINAPI SafeInvalidate(HWND,const RECT *,BOOL){++queuedPaintRequests;return FALSE;}
static BOOL WINAPI SafeRedraw(HWND w,const RECT *r,HRGN region,UINT flags){RequireOwned(w);return ::RedrawWindow(w,r,region,flags);}
static BOOL WINAPI SafePost(HWND w,UINT,WPARAM,LPARAM){RequireOwned(w);return TRUE;}
static HANDLE WINAPI NoProcess(DWORD access,BOOL,DWORD pid){if(access&PROCESS_TERMINATE)closeTargets.push_back(pid);if(access==SYNCHRONIZE)waitTargets.push_back(pid);SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
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
#ifdef CTSPACES_STARTUP_ACTION_BEFORE
#include "../build/app-audit-round7-2026-10-15/startup-actions/ctSpaces-before.cpp"
#elif defined(CTSPACES_STARTUP_ACTION_AFTER)
#include "../build/app-audit-round7-2026-10-15/startup-actions/ctSpaces-after.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef OpenProcess
#undef PostMessageW

static unsigned notices=0,checks=0,failures=0;
static std::wstring lastNotice;
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR message,LPCWSTR,UINT){++notices;lastNotice=message?message:L"";return IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &){}
static void Check(bool condition,const char*message){++checks;if(!condition)++failures;std::cout<<(condition?"PASS ":"FAIL ")<<message<<'\n';}
static std::string Bytes(const fs::path &path){std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Private fixture read failed");return {std::istreambuf_iterator<char>(input),{}};}
static UINT BrowserCommand(BrowserKind browser){switch(browser){case BrowserKind::Edge:return IDM_BROWSER_EDGE;case BrowserKind::Chrome:return IDM_BROWSER_CHROME;case BrowserKind::Brave:return IDM_BROWSER_BRAVE;case BrowserKind::Firefox:return IDM_BROWSER_FIREFOX;}throw std::runtime_error("Unknown fixture browser");}
static void BrowserPreferenceFailureCases(){
  const auto fixture=fs::current_path()/L"build"/L"app-audit-round7-2026-10-15"/L"startup-actions"/L"fixtures"/(L"browser-choice-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  if(!fs::create_directories(fixture))throw std::runtime_error("Fresh fixture directory unavailable");
  g_sDataDir=fixture;g_sConfigPath=fixture/L"config.ini";g_bUiEnabled=true;
  unsigned transitions=0;const unsigned failuresBefore=failures;
  for(BrowserKind from:{BrowserKind::Edge,BrowserKind::Chrome,BrowserKind::Brave,BrowserKind::Firefox})for(BrowserKind to:{BrowserKind::Edge,BrowserKind::Chrome,BrowserKind::Brave,BrowserKind::Firefox}){
    if(from==to)continue;g_selectedBrowser=from;
    Check(SaveBrowserPreference(),"Initial browser preference saves in the private config");
    const auto original=Bytes(g_sConfigPath);const unsigned noticeBefore=notices;
    HANDLE lock=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Private config sharing lock failed");
    WndProc(nullptr,WM_COMMAND,BrowserCommand(to),0);
    CloseHandle(lock);
    Check(g_selectedBrowser==from,"Rejected browser command retains the previously saved browser selection");
    Check(Bytes(g_sConfigPath)==original,"Rejected browser command preserves exact saved config bytes");
    Check(notices==noticeBefore+1&&lastNotice.find(L"browser preference")!=std::wstring::npos,"Rejected browser command reports the failed save");
    WndProc(nullptr,WM_COMMAND,BrowserCommand(to),0);
    Check(g_selectedBrowser==to,"Unlocked browser command retry applies the requested browser");
    g_selectedBrowser=from;LoadBrowserPreference();
    Check(g_selectedBrowser==to,"Browser command retry reloads the exact requested browser");
    ++transitions;
  }
  std::cout<<"Browser preference commands: "<<transitions<<" transitions, "<<(failures-failuresBefore)<<" failures\n";
}
static void StartupParserCases(){
  const unsigned failuresBefore=failures;
  const std::wstring invalid[]={L"--client Alpha --browser",L"--client Alpha --browser edge --browser firefox",L"--client Alpha --browser=edge --browser=invalid",L"--client Alpha --browser=chrome --browser",L"--client Alpha --browser=",L"--client Alpha --browser invalid"};
  for(const auto &arguments:invalid){const auto request=ParseClientLaunchRequest(arguments);if(request)std::wcout<<L"Accepted malformed browser intent: "<<arguments<<L"\n";Check(!request,"Incomplete or ambiguous explicit browser selection must not fall back or be ignored");}
  unsigned validCases=0;
  for(BrowserKind browser:{BrowserKind::Edge,BrowserKind::Chrome,BrowserKind::Brave,BrowserKind::Firefox})for(bool equalsForm:{false,true})for(const std::wstring client:{L"Alpha",L"Alpha Office",L"\u65e5\u672c \U0001f680"}){
    std::wstring upper=GetBrowserId(browser);for(auto&character:upper)character=static_cast<wchar_t>(towupper(character));
    for(const std::wstring id:{std::wstring(GetBrowserId(browser)),upper,std::to_wstring(static_cast<int>(browser))}){
      const std::wstring arguments=L"--client "+QuoteCommandLineArgument(client)+(equalsForm?L" --browser=":L" --browser ")+id+L" --url \"https://example.test/a?x=one&y=two\"";
      const auto request=ParseClientLaunchRequest(arguments);
      Check(request&&request->clientName==client&&request->browser==browser&&request->url==L"https://example.test/a?x=one&y=two","Supported browser forms/aliases retain quoted Unicode client and URL intent");++validCases;
    }
  }
  for(const std::wstring invalidId:{L"unknown",L"-1",L"4",L"3.2",L"--qa-guide-startup"})for(bool equalsForm:{false,true})
    Check(!ParseClientLaunchRequest(L"--client Alpha"+std::wstring(equalsForm?L" --browser=":L" --browser ")+invalidId),"Invalid explicit browser values reject instead of selecting a fallback");
  const auto legacy=ParseClientLaunchRequest(L"--qa-instance=fixture --client \"\u65e5\u672c Office\" --qa-data-dir=\"C:\\fixture data\" --qa-shortcut-dir=private --qa-guide-startup --wait-for-pid=123");
  Check(legacy&&legacy->clientName==L"\u65e5\u672c Office"&&!legacy->browser,"Documented QA/wait startup switches preserve browser-less Unicode client requests");
  Check(!ParseClientLaunchRequest(L"--browser=edge")&&!ParseClientLaunchRequest(L"--client")&&!ParseClientLaunchRequest(L"--client= --browser=edge"),"Absent or empty client values do not produce a shortcut launch request");
  std::cout<<"Valid startup browser cases="<<validCases<<"\n";
  std::cout<<"Startup browser parser: "<<(failures-failuresBefore)<<" failures\n";
}
static void CopyDataRejectionCases(){
  constexpr ULONG_PTR tag=0x43545350;
  wchar_t bounded[]={L'-',L'-',L'c',L'l',L'i',L'e',L'n',L't'};
  COPYDATASTRUCT request{tag,sizeof(bounded),bounded};
  Check(!WndProc(nullptr,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&request)),"Nonterminated bounded handoff payload rejects before presentation");
  request.dwData=0;Check(!WndProc(nullptr,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&request)),"Unknown handoff tag rejects before presentation");
  request.dwData=tag;request.cbData=3;Check(!WndProc(nullptr,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&request)),"Partial UTF16 handoff payload rejects before presentation");
  request.cbData=32769*sizeof(wchar_t);Check(!WndProc(nullptr,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&request)),"Oversized handoff payload rejects before reading beyond fixture bytes");
  Check(!WndProc(nullptr,WM_COPYDATA,0,0),"Null handoff record rejects before presentation");
}
static void StartupArgumentBoundaryCases(){
  for(const auto *field:{L"--client",L"--url",L"--browser"}) for(const auto *option:{L"--qa-instance",L"--qa-data-dir",L"--qa-shortcut-dir",L"--wait-for-pid"}) {
    const auto quoted=std::wstring(field)+L" "+QuoteCommandLineArgument(std::wstring(L"Office ")+option+L"=literal branch");
    const auto exact=std::wstring(field)+L" "+QuoteCommandLineArgument(std::wstring(option)+L"=literal");
    Check(!FindStartupArgument(quoted,option) && !FindStartupArgument(exact,option),"Quoted request values cannot select an internal instance, data directory or wait target");
    const auto standalone=quoted+L" "+option+L"="+QuoteCommandLineArgument(L"C:\\private data\\\u65e5\u672c");
    Check(FindStartupArgument(standalone,option)==std::optional<std::wstring>{L"C:\\private data\\\u65e5\u672c"},"A standalone startup value preserves native quoted paths and Unicode");
  }
  Check(!FindStartupArgument(L"--client \"Office --qa-guide-startup branch\"",L"--qa-guide-startup",false) && FindStartupArgument(L"--client Office --qa-guide-startup",L"--qa-guide-startup",false).has_value(),"Only a standalone guide-startup flag changes guide startup intent");
  for(const auto *arguments:{L"--client \"Acme --wait-for-pid=123 Office\"",L"--client \"--wait-for-pid=123\"",L"--url \"https://example.test/?q= --wait-for-pid=123 next\" --client Alpha"}) {
    waitTargets.clear();WaitForPreviousLauncher(arguments);
    Check(waitTargets.empty(),"Client or URL argument text cannot become a startup wait-for-process switch");
  }
  for(const auto *arguments:{L"--client Alpha --wait-for-pid=123",L"--wait-for-pid=123 --client \"Acme Office\""}) {
    waitTargets.clear();WaitForPreviousLauncher(arguments);
    Check(waitTargets==std::vector<DWORD>{123},"A real standalone startup wait switch targets only its explicit process");
  }
  for(const auto *arguments:{L"--wait-for-pid=0",L"--wait-for-pid=4294967296",L"--wait-for-pid=123suffix",L"--wait-for-pid=-1"}) {
    waitTargets.clear();WaitForPreviousLauncher(arguments);
    Check(waitTargets.empty(),"Malformed or out-of-range wait values do not inspect a process");
  }
  for(const auto *arguments:{L"--client Alpha --client Beta",L"--client=Alpha --client=Beta",L"--client Alpha --client=Beta",L"--client Alpha --url=https://first.test --url=https://second.test",L"--client Alpha --url https://first.test --url=https://second.test"})
    Check(!ParseClientLaunchRequest(arguments),"Duplicate client or URL switches reject ambiguous shortcut intent");
  waitTargets.clear();
}
static void GeneratedStartupCases(){
  unsigned seed=0x633012u;
  const std::wstring pieces[]={L"Office",L" ",L"\u65e5\u672c",L"\U0001f680",L"\\",L"\"",L"--wait-for-pid=123",L"--qa-instance=literal",L"--qa-guide-startup",L"--client",L"--url=https://literal.test",L"_"};
  for(unsigned sample=0;sample<256;++sample){
    std::wstring client=L"QA ";
    for(unsigned part=0;part<7;++part){seed=seed*1664525u+1013904223u;client+=pieces[seed%std::size(pieces)];}
    const std::wstring url=L"https://example.test/"+std::to_wstring(sample)+L"?q=quoted \\\"value\\\"";
    for(const auto browser:{BrowserKind::Edge,BrowserKind::Chrome,BrowserKind::Brave,BrowserKind::Firefox}){
      const auto arguments=L"--client "+QuoteCommandLineArgument(client)+L" --url="+QuoteCommandLineArgument(url)+L" --browser="+GetBrowserId(browser);
      const auto request=ParseClientLaunchRequest(arguments);
      Check(request && request->clientName==client && request->url==url && request->browser==browser,"Generated native-quoted request preserves exact client, browser and URL intent");
      Check(!FindStartupArgument(arguments,L"--wait-for-pid") && !FindStartupArgument(arguments,L"--qa-instance") && !FindStartupArgument(arguments,L"--qa-guide-startup",false),"Generated client text cannot introduce internal startup controls");
      const auto explicitArguments=arguments+L" --qa-instance=real_633 --qa-data-dir="+QuoteCommandLineArgument(L"C:\\QA data\\\u65e5\u672c\\")+L" --wait-for-pid=123";
      Check(FindStartupArgument(explicitArguments,L"--qa-instance")==std::optional<std::wstring>{L"real_633"} && FindStartupArgument(explicitArguments,L"--qa-data-dir")==std::optional<std::wstring>{L"C:\\QA data\\\u65e5\u672c\\"} && FindStartupArgument(explicitArguments,L"--wait-for-pid")==std::optional<std::wstring>{L"123"},"Generated quoted values cannot hide or change independent startup switches");
    }
  }
  std::cout<<"Generated startup seed=0x633012, requests=1024\n";
}
int wmain(int argc,wchar_t **argv){try{
  if(argc>1 && !wcscmp(argv[1],L"--generated-round13")){
    GeneratedStartupCases();
    Check(presentationRequests==0 && launchRequests==0 && closeTargets.empty() && waitTargets.empty(),"Generated startup probes never open processes or manipulate the desktop");
    std::cout<<checks<<" generated startup checks; "<<failures<<" failures\n";return failures?1:0;
  }
  BrowserPreferenceFailureCases();StartupParserCases();CopyDataRejectionCases();StartupArgumentBoundaryCases();
  Check(!g_hGui&&!g_hClientEdit&&!g_hComboClient,"Fixture creates no HWNDs");
  Check(presentationRequests==0&&launchRequests==0&&forbiddenPaint==0&&closeTargets.empty(),"No presentation, launch, foreign repaint or process-close action reaches Windows");
  std::cout<<checks<<" startup/action checks; "<<failures<<" failures; suppressed queued paint requests="<<queuedPaintRequests<<'\n';return failures?1:0;
}catch(const std::exception&error){std::cerr<<"Fixture setup failure: "<<error.what()<<'\n';return 2;}}


