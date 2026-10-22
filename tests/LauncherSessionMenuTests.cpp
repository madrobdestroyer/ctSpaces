// Background-only launcher menu reentrancy tests. Native popups, presentation,
// process opens and external launches are intercepted before Windows executes.
#include <windows.h>
#include "RetainedFixture.h"
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
// Synthetic lifetime tokens keep this menu-routing fixture independent of
// actual processes. A separate native suite tests real process ownership.
static bool FixtureProcess(HANDLE handle){const auto value=reinterpret_cast<uintptr_t>(handle);return value>=0x7ffff101&&value<=0x7ffff103;}
static DWORD WINAPI FixtureProcessId(HANDLE handle){return FixtureProcess(handle) ? static_cast<DWORD>(reinterpret_cast<uintptr_t>(handle)) : ::GetProcessId(handle);}
static DWORD WINAPI FixtureProcessWait(HANDLE handle,DWORD timeout){return FixtureProcess(handle) ? WAIT_TIMEOUT : ::WaitForSingleObject(handle,timeout);}
static BOOL WINAPI NoTerminate(HANDLE handle,UINT){if(FixtureProcess(handle)){closeTargets.push_back(FixtureProcessId(handle));return TRUE;}++launchRequests;return FALSE;}
static HINSTANCE WINAPI NoShell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){++launchRequests;return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);}
static BOOL WINAPI NoShellEx(SHELLEXECUTEINFOW *){++launchRequests;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static unsigned createAttempts=0;
static std::wstring attemptedExecutable,attemptedCommand;
static BOOL WINAPI NoCreateProcess(LPCWSTR exe,LPWSTR command,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){++launchRequests;++createAttempts;attemptedExecutable=exe?exe:L"";attemptedCommand=command?command:L"";SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL WINAPI NoTray(DWORD,PNOTIFYICONDATAW){++launchRequests;return FALSE;}
static BOOL WINAPI NoCursor(LPPOINT point){if(point)*point={};return FALSE;}
static BOOL WINAPI NoCaptureRelease(){++presentationRequests;return FALSE;}
static HWND WINAPI NoCapture(HWND){++presentationRequests;return nullptr;}
static unsigned suppressedClipboardRequests=0;
// The default remains a denied clipboard read. Only the busy-link fixture can
// temporarily supply its own HGLOBAL; no system clipboard API is called.
static HGLOBAL ownedClipboard=nullptr;
static bool clipboardOpen=false;
static unsigned clipboardDataRequests=0,clipboardCloseRequests=0;
static BOOL WINAPI NoClipboard(HWND owner){++suppressedClipboardRequests;if(!ownedClipboard)return FALSE;RequireOwned(owner);if(clipboardOpen)throw std::runtime_error("Nested fixture clipboard open");clipboardOpen=true;return TRUE;}
static HANDLE WINAPI FixtureClipboardData(UINT format){if(!clipboardOpen||format!=CF_UNICODETEXT||!ownedClipboard)throw std::runtime_error("Refused foreign clipboard read");++clipboardDataRequests;return ownedClipboard;}
static BOOL WINAPI FixtureClipboardClose(){if(!clipboardOpen)throw std::runtime_error("Unbalanced fixture clipboard close");clipboardOpen=false;++clipboardCloseRequests;return TRUE;}
static BOOL WINAPI FixtureClipboardFormat(UINT format){return format==CF_UNICODETEXT;}
static UINT WINAPI FakePopup(HMENU,UINT,int,int,HWND,LPTPMPARAMS);
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
#define GetProcessId FixtureProcessId
#define WaitForSingleObject FixtureProcessWait
#define TerminateProcess NoTerminate
#define ShellExecuteW NoShell
#define ShellExecuteExW NoShellEx
#define CreateProcessW NoCreateProcess
#define Shell_NotifyIconW NoTray
#define GetCursorPos NoCursor
#define SetCapture NoCapture
#define ReleaseCapture NoCaptureRelease
#define OpenClipboard NoClipboard
#define GetClipboardData FixtureClipboardData
#define CloseClipboard FixtureClipboardClose
#define IsClipboardFormatAvailable FixtureClipboardFormat
#define TrackPopupMenuEx FakePopup
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain ProductionEntryNotRun
#if defined(CTSPACES_PINNED_MENU_BEFORE)
#include "../build/app-audit-round7-2026-10-15/root/ctSpaces-before.cpp"
#elif defined(CTSPACES_SESSION_MENU_BEFORE)
#include "../build/app-audit-round5-2026-10-15/root/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef OpenProcess
#undef PostMessageW

int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR,LPCWSTR,UINT flags){return flags&MB_YESNO ? IDNO : IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &){}
static std::wstring chosenLabel;
static std::function<void()> menuMutation;
static unsigned menuCalls=0,checks=0,failures=0,scenarios=0;
static UINT WINAPI FakePopup(HMENU menu,UINT,int,int,HWND owner,LPTPMPARAMS){
  RequireOwned(owner);++menuCalls;UINT result=0;
  for(int i=0;i<GetMenuItemCount(menu);++i){
    MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_ID|MIIM_DATA;
    if(GetMenuItemInfoW(menu,i,TRUE,&item)&&item.dwItemData){
      const auto *data=reinterpret_cast<const MenuItemData *>(item.dwItemData);
      if(data->text==chosenLabel)result=item.wID;
    }
  }
  if(!chosenLabel.empty()&&!result)throw std::runtime_error("Requested fixture menu entry was absent");
  if(menuMutation)menuMutation();return result;
}
static void Check(bool value,const char *message){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';}
static unsigned shows=0,focuses=0,foregrounds=0;
static void CALLBACK Observe(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId())return;if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW)++shows;if(event==EVENT_OBJECT_FOCUS)++focuses;if(event==EVENT_SYSTEM_FOREGROUND)++foregrounds;}
static constexpr DWORD alphaPid=0x7ffff101,bravoPid=0x7ffff102,charliePid=0x7ffff103;
static void Seed(){
  ++scenarios;g_sessions={{L"Alpha",BrowserKind::Edge,alphaPid},{L"Bravo",BrowserKind::Chrome,bravoPid},{L"Charlie",BrowserKind::Firefox,charliePid}};
#ifndef CTSPACES_SESSION_MENU_BEFORE
  g_activeProfiles.clear();
  for(const auto &session:g_sessions){
    auto token=std::shared_ptr<void>(reinterpret_cast<void *>(static_cast<uintptr_t>(session.pid)),[](void *){});
    g_activeProfiles.emplace(ClientBrowserKey{session.clientName,session.browser},ActiveProfileInfo{session.pid,{},{},ProfileType::Standard,std::move(token)});
  }
#endif
  g_overflowSessionTabs={1,2,3};g_iSelectedSessionTab=0;closeTargets.clear();SetClientInputText(L"");
}
static void CloseCase(const wchar_t *label,const std::function<void()> &mutation,const std::vector<DWORD> &expected,const char *message){Seed();chosenLabel=label;menuMutation=mutation;ShowSessionOverflowMenu(g_hGui);Check(closeTargets==expected,message);}
static void ShowCase(const wchar_t *label,const std::function<void()> &mutation,const std::wstring &expected,const char *message){Seed();chosenLabel=label;menuMutation=mutation;ShowSessionOverflowMenu(g_hGui);Check(GetClientInputText()==expected&&closeTargets.empty(),message);}
#ifndef CTSPACES_SESSION_MENU_BEFORE
static bool rejectTitleWrite=false;
static LRESULT CALLBACK TitleWriteSubclass(HWND w,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
  if(message==WM_SETTEXT&&rejectTitleWrite)return FALSE;
  return DefSubclassProc(w,message,wp,lp);
}
static std::wstring NativeTitle(HWND window){wchar_t text[4096]{};GetWindowTextW(window,text,4096);return text;}
static void NativeTitleCases(){
  const bool previous=g_bClientTitleFirst.load();
  const auto window=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"",WS_POPUP,0,0,300,40,nullptr,nullptr,g_hInst,nullptr);
  RequireOwned(window);SetWindowSubclass(window,TitleWriteSubclass,1,0);
  AppliedWindowTitle applied;const DWORD pid=GetCurrentProcessId();
  const std::wstring page=L"Alpha [Microsoft Edge] - Portal",label=L"Alpha [Microsoft Edge]";
  SetWindowTextW(window,(page+L" - Microsoft Edge").c_str());
  bool exact=true;
  for(unsigned i=0;i<120;++i){g_bClientTitleFirst=i%2!=0;UpdateBrowserWindowTitle(window,pid,L"Alpha",BrowserKind::Edge,applied);if(NativeTitle(window)!=browser_title::Format(page,label,APP_ALIAS,i%2!=0))exact=false;}
  Check(exact,"Actual hidden WM_SETTEXT watcher adapter preserves ambiguous page text over 120 preference toggles");
  SetWindowTextW(window,L"New navigation - Microsoft Edge");
  UpdateBrowserWindowTitle(window,pid,L"Alpha",BrowserKind::Edge,applied);
  Check(NativeTitle(window)==browser_title::Format(L"New navigation",label,APP_ALIAS,g_bClientTitleFirst.load()),"A real native caption change replaces remembered page text");
  applied={};g_bClientTitleFirst=false;SetWindowTextW(window,(page+L" - Microsoft Edge").c_str());rejectTitleWrite=true;
  UpdateBrowserWindowTitle(window,pid,L"Alpha",BrowserKind::Edge,applied);
  Check(NativeTitle(window)==page+L" - Microsoft Edge","Rejected native WM_SETTEXT leaves the original caption intact");
  rejectTitleWrite=false;UpdateBrowserWindowTitle(window,pid,L"Alpha",BrowserKind::Edge,applied);
  Check(NativeTitle(window)==browser_title::Format(page,label,APP_ALIAS,false),"A rejected native title update retries successfully without page text loss");
  const auto before=NativeTitle(window);UpdateBrowserWindowTitle(window,pid+1,L"Alpha",BrowserKind::Edge,applied);
  Check(NativeTitle(window)==before&&applied.pid==0,"A mismatched window PID clears remembered state without writing a caption");
  DestroyWindow(window);g_bClientTitleFirst=previous;
}
#endif
#include "LauncherPinnedMenuTests.inl"
#include "LauncherBusyLinkTests.inl"
int wmain(int argc,wchar_t **argv){
  std::vector<HWINEVENTHOOK> hooks;
  try{
    for(DWORD event:{EVENT_SYSTEM_FOREGROUND,EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS}){auto hook=SetWinEventHook(event,event,nullptr,Observe,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);if(!hook)throw std::runtime_error("Cannot audit native background events");hooks.push_back(hook);}
    g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();
    g_sDataDir=retained_fixture::CreateRoot(fs::current_path()/L"build"/L"app-audit-round7-2026-10-15"/L"launcher",L"fixture");
    g_sConfigPath=g_sDataDir/L"config.ini";
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_TAB_CLASSES};if(!InitCommonControlsEx(&controls))throw std::runtime_error("Cannot initialize controls");
    Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);
    g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden session menu fixture",WS_OVERLAPPED|WS_CLIPCHILDREN,0,0,600,234,nullptr,nullptr,g_hInst,nullptr);
    RequireOwned(g_hGui);
    const auto child=[&](const wchar_t *kind,DWORD style){HWND result=CreateWindowExW(WS_EX_NOACTIVATE,kind,L"",WS_CHILD|style,0,0,100,24,g_hGui,nullptr,g_hInst,nullptr);RequireOwned(result);return result;};
    g_hClientEdit=child(L"EDIT",ES_AUTOHSCROLL);g_hComboClient=child(L"COMBOBOX",CBS_DROPDOWN);
    g_hClientEditSurface=child(L"STATIC",0);g_hBtnClientIcon=child(L"BUTTON",0);g_hBtnClientDrop=child(L"BUTTON",0);
    g_hBtnGo=child(L"BUTTON",0);g_hBtnPin=child(L"BUTTON",0);g_hBtnRestoreTabs=child(L"BUTTON",0);
    g_hBtnConfig=child(L"BUTTON",0);g_hBtnTmpProf=child(L"BUTTON",0);g_hBtnCloseAll=child(L"BUTTON",0);g_hBtnNotes=child(L"BUTTON",0);
    LoadBakedThemesFromHeader();g_themeColors=g_aCustomThemes.front().colors;UpdateThemeBrushes();
    const bool pinnedOnly=argc==2&&wcscmp(argv[1],L"--pinned-only")==0;
    const bool busyOnly=argc==2&&wcscmp(argv[1],L"--busy-link-only")==0;
    if(!pinnedOnly&&!busyOnly){
    CloseCase(L"Close Bravo",[]{g_sessions.erase(g_sessions.begin());},{bravoPid},"Closing Bravo after Alpha exits still targets Bravo");
    ShowCase(L"Bravo",[]{g_sessions.erase(g_sessions.begin());},L"Bravo","Showing Bravo after Alpha exits still selects Bravo");
    CloseCase(L"Close Bravo",[]{g_sessions.erase(g_sessions.begin()+1);},{},"An exited menu target cannot close the next client");
    ShowCase(L"Bravo",[]{g_sessions.erase(g_sessions.begin()+1);},L"","An exited menu target cannot select the next client");
    CloseCase(L"Close Alpha",[]{std::reverse(g_sessions.begin(),g_sessions.end());},{alphaPid},"Reordering during the menu keeps Close bound to the original client");
    ShowCase(L"Alpha",[]{std::reverse(g_sessions.begin(),g_sessions.end());},L"Alpha","Reordering during the menu keeps Show bound to the original client");
    CloseCase(L"Close Bravo",[]{g_sessions[1]=Session{L"Replacement",BrowserKind::Edge,bravoPid};},{},"Reused PID for another client cannot inherit a stale Close command");
    CloseCase(L"Close Bravo",[]{g_sessions[1]=Session{L"Bravo",BrowserKind::Chrome,bravoPid};},{},"A new incarnation of the same client and PID cannot inherit a stale Close command");
    CloseCase(L"Close Alpha",[]{},{alphaPid},"Unchanged Close menu target still works");
    ShowCase(L"Bravo",[]{},L"Bravo","Unchanged Show menu target still works");
    CloseCase(L"",[]{},{},"Canceling the menu never closes a session");
    Check(menuCalls==scenarios,"Every test traversed the actual production overflow-menu routine");
#ifndef CTSPACES_SESSION_MENU_BEFORE
    NativeTitleCases();
#endif
    }
    if(!busyOnly){
      NativePinnedMenuCases();
      Check(forbiddenPaint==0&&launchRequests==0,"No desktop redraw, browser launch, tray operation or process creation occurred");
    }
    NativeBusyLinkCases();
    DestroyWindow(g_hGui);g_hGui=nullptr;
    MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if((msg.message>=WM_MOUSEFIRST&&msg.message<=WM_MOUSELAST)||(msg.message>=WM_KEYFIRST&&msg.message<=WM_KEYLAST))throw std::runtime_error("Unexpected device input");DispatchMessageW(&msg);}
    unsigned visible=0;EnumWindows([](HWND w,LPARAM value)->BOOL{DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(w))++*reinterpret_cast<unsigned *>(value);return TRUE;},reinterpret_cast<LPARAM>(&visible));
    Check(!visible&&!shows&&!focuses&&!foregrounds,"Native audit: zero visible windows, show, focus and foreground events");
    for(auto hook:hooks)UnhookWinEvent(hook);hooks.clear();Gdiplus::GdiplusShutdown(g_gdiplusToken);
    std::cout<<"Session menu: "<<checks<<" checks, "<<scenarios<<" scenarios, "<<failures<<" failures; suppressed presentation requests="<<presentationRequests<<"; no device input or real process opens.\n";
    return failures?1:0;
  }catch(const std::exception &error){std::cerr<<"Session menu safety/setup failure: "<<error.what()<<'\n';for(auto hook:hooks)UnhookWinEvent(hook);return 2;}
}
