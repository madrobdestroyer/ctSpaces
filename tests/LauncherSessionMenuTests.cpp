// Background-only launcher menu reentrancy tests. Native popups, presentation,
// process opens and external launches are intercepted before Windows executes.
#include <windows.h>
#include "RetainedFixture.h"
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
static unsigned presentationRequests=0,focusRequests=0,launchRequests=0,forbiddenPaint=0;
static std::vector<DWORD> closeTargets;
static std::vector<DWORD> terminationTargets;
static constexpr DWORD alphaPid=0x7ffff101,bravoPid=0x7ffff102,charliePid=0x7ffff103;
static std::array<HWND,3> fixtureBrowserWindows{};
static HWND foregroundRequest=nullptr;
static bool fixtureBrowserWindowsReady=false;
static bool refuseClosePost=false;
static std::wstring closeNotice;
static std::function<int(UINT)> closeAllResponse;
static unsigned alreadyRunningNotices=0;
static BOOL WINAPI NoShow(HWND,int){++presentationRequests;return FALSE;}
static HWND WINAPI NoFocus(HWND){++presentationRequests;++focusRequests;return nullptr;}
static BOOL WINAPI NoForeground(HWND window){
  ++presentationRequests;
  if (std::find(fixtureBrowserWindows.begin(), fixtureBrowserWindows.end(), window) !=
      fixtureBrowserWindows.end())
    foregroundRequest=window;
  return FALSE;
}
static HWND WINAPI NoActive(HWND){++presentationRequests;return nullptr;}
static BOOL WINAPI NoRaise(HWND){++presentationRequests;return FALSE;}
static BOOL WINAPI SafePosition(HWND window,HWND after,int x,int y,int width,int height,UINT flags){
  if((flags&SWP_SHOWWINDOW)||!(flags&SWP_NOACTIVATE)){++presentationRequests;return FALSE;}
  return ::SetWindowPos(window,after,x,y,width,height,flags);
}
static void RequireOwned(HWND window){DWORD pid=0;if(!window||!IsWindow(window)||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId()){++forbiddenPaint;throw std::runtime_error("Refused invalid or foreign redraw target");}}
static BOOL WINAPI SafeInvalidate(HWND w,const RECT *r,BOOL erase){RequireOwned(w);return ::InvalidateRect(w,r,erase);}
static BOOL WINAPI SafeRedraw(HWND w,const RECT *r,HRGN region,UINT flags){RequireOwned(w);return ::RedrawWindow(w,r,region,flags);}
static DWORD FixtureBrowserPid(HWND window) {
  for (size_t index=0; index<fixtureBrowserWindows.size(); ++index)
    if (window && fixtureBrowserWindows[index]==window)
      return alphaPid+static_cast<DWORD>(index);
  return 0;
}
static BOOL WINAPI SafePost(HWND w,UINT message,WPARAM,LPARAM) {
  RequireOwned(w);
  if (message==WM_CLOSE) {
    const DWORD pid=FixtureBrowserPid(w);
    if (!pid)
      throw std::runtime_error("Close attempted outside the modeled browser windows");
    if (refuseClosePost)
      return FALSE;
    closeTargets.push_back(pid);
  }
  return TRUE;
}
static BOOL WINAPI EnumerateFixtureBrowserWindows(WNDENUMPROC callback,LPARAM parameter) {
  if (fixtureBrowserWindowsReady)
    for (HWND window : fixtureBrowserWindows)
      if (window && !callback(window,parameter))
        return FALSE;
  return TRUE;
}
static DWORD WINAPI FixtureWindowProcess(HWND window,LPDWORD pid) {
  const DWORD browser=FixtureBrowserPid(window);
  if (!browser)
    return ::GetWindowThreadProcessId(window,pid);
  if (pid)
    *pid=browser;
  return ::GetWindowThreadProcessId(window,nullptr);
}
static BOOL WINAPI FixtureWindowVisible(HWND window) {
  return FixtureBrowserPid(window) ? fixtureBrowserWindowsReady : ::IsWindowVisible(window);
}
static HANDLE WINAPI NoProcess(DWORD access,BOOL,DWORD pid){if(access&PROCESS_TERMINATE)closeTargets.push_back(pid);SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
// Synthetic lifetime tokens keep this menu-routing fixture independent of
// actual processes. A separate native suite tests real process ownership.
static bool FixtureProcess(HANDLE handle){const auto value=reinterpret_cast<uintptr_t>(handle);return value>=alphaPid&&value<=charliePid;}
static DWORD WINAPI FixtureProcessId(HANDLE handle){return FixtureProcess(handle) ? static_cast<DWORD>(reinterpret_cast<uintptr_t>(handle)) : ::GetProcessId(handle);}
static DWORD WINAPI FixtureProcessWait(HANDLE handle,DWORD timeout){return FixtureProcess(handle) ? WAIT_TIMEOUT : ::WaitForSingleObject(handle,timeout);}
static BOOL WINAPI NoTerminate(HANDLE handle,UINT){if(FixtureProcess(handle)){terminationTargets.push_back(FixtureProcessId(handle));return TRUE;}++launchRequests;return FALSE;}
static HINSTANCE WINAPI NoShell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){++launchRequests;return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);}
static BOOL WINAPI NoShellEx(SHELLEXECUTEINFOW *){++launchRequests;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static unsigned createAttempts=0;
static std::wstring attemptedExecutable,attemptedCommand;
static BOOL WINAPI NoCreateProcess(LPCWSTR exe,LPWSTR command,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){++launchRequests;++createAttempts;attemptedExecutable=exe?exe:L"";attemptedCommand=command?command:L"";SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL WINAPI NoTray(DWORD,PNOTIFYICONDATAW){++launchRequests;return FALSE;}
static BOOL WINAPI NoCursor(LPPOINT point){if(point)*point={};return FALSE;}
static HCURSOR WINAPI NoCursorShape(HCURSOR){return nullptr;}
static BOOL WINAPI NoMouseTracking(LPTRACKMOUSEEVENT){return TRUE;}
static HWND fixtureCapture=nullptr;
static BOOL WINAPI NoCaptureRelease(){++presentationRequests;fixtureCapture=nullptr;return TRUE;}
static HWND WINAPI NoCapture(HWND window){++presentationRequests;RequireOwned(window);return std::exchange(fixtureCapture,window);}
static HWND WINAPI FixtureCapture(){return fixtureCapture;}
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
#define EnumWindows EnumerateFixtureBrowserWindows
#define GetWindowThreadProcessId FixtureWindowProcess
#define IsWindowVisible FixtureWindowVisible
#define OpenProcess NoProcess
#define GetProcessId FixtureProcessId
#define WaitForSingleObject FixtureProcessWait
#define TerminateProcess NoTerminate
#define ShellExecuteW NoShell
#define ShellExecuteExW NoShellEx
#define CreateProcessW NoCreateProcess
#define Shell_NotifyIconW NoTray
#define GetCursorPos NoCursor
#define SetCursor NoCursorShape
#define TrackMouseEvent NoMouseTracking
#define SetCapture NoCapture
#define ReleaseCapture NoCaptureRelease
#define GetCapture FixtureCapture
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
#undef EnumWindows
#undef GetWindowThreadProcessId
#undef IsWindowVisible

int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR text,LPCWSTR title,UINT flags){
  if (title && std::wstring_view(title)==L"Close browser")
    closeNotice=text ? text : L"";
  if (title && std::wstring_view(title)==L"Close all clients" && closeAllResponse)
    return closeAllResponse(flags);
  if (title && std::wstring_view(title)==L"Already Running")
    ++alreadyRunningNotices;
  return flags&MB_YESNO ? IDNO : IDOK;
}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &){}
static std::wstring chosenLabel;
static std::function<void()> menuMutation;
static std::function<void(HMENU)> popupInspect;
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
  if(popupInspect)popupInspect(menu);
  if(menuMutation)menuMutation();return result;
}
static void Check(bool value,const char *message){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';}
static unsigned shows=0,focuses=0,foregrounds=0;
static void CALLBACK Observe(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId())return;if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW)++shows;if(event==EVENT_OBJECT_FOCUS)++focuses;if(event==EVENT_SYSTEM_FOREGROUND)++foregrounds;}
static void Seed(){
  ++scenarios;g_sessions={{L"Alpha",BrowserKind::Edge,alphaPid},{L"Bravo",BrowserKind::Chrome,bravoPid},{L"Charlie",BrowserKind::Firefox,charliePid}};
#ifndef CTSPACES_SESSION_MENU_BEFORE
  g_activeProfiles.clear();
  for(const auto &session:g_sessions){
    auto token=std::shared_ptr<void>(reinterpret_cast<void *>(static_cast<uintptr_t>(session.pid)),[](void *){});
    g_activeProfiles.emplace(ClientBrowserKey{session.clientName,session.browser},ActiveProfileInfo{session.pid,{},{},ProfileType::Standard,std::move(token)});
  }
#endif
  g_overflowSessionTabs={1,2,3};g_iSelectedSessionTab=0;
  fixtureBrowserWindowsReady=true;refuseClosePost=false;
  closeTargets.clear();terminationTargets.clear();closeNotice.clear();
  SetClientInputText(L"");
}
static void CloseCase(const wchar_t *label,const std::function<void()> &mutation,const std::vector<DWORD> &expected,const char *message){Seed();chosenLabel=label;menuMutation=mutation;ShowSessionOverflowMenu(g_hGui);Check(closeTargets==expected,message);}
static void ShowCase(const wchar_t *label,const std::function<void()> &mutation,const std::wstring &expected,const char *message){Seed();chosenLabel=label;menuMutation=mutation;ShowSessionOverflowMenu(g_hGui);Check(GetClientInputText()==expected&&closeTargets.empty(),message);}
#ifndef CTSPACES_SESSION_MENU_BEFORE
static void BusySessionMenuCases(bool before) {
  const auto enterArchive = [] {
    g_bClientNotesBusy = true;
    SetUiState(false);
  };
  const auto endArchive = [] {
    g_bClientNotesBusy = false;
    SetUiState(true);
  };
  Seed();
  chosenLabel = L"Close Bravo";
  menuMutation = enterArchive;
  ShowSessionOverflowMenu(g_hGui);
  const bool closeDispatched = closeTargets == std::vector<DWORD>{bravoPid};
  Check(before ? closeDispatched : closeTargets.empty(),
        "The guarded busy Close case observes the expected exact-target dispatch behavior");
  Check(!g_bUiEnabled && g_bClientNotesBusy && !IsWindowEnabled(g_hBtnGo) &&
        g_iSelectedSessionTab == 0 && GetClientInputText().empty(),
        "The Close case leaves the archive owner and initial launcher selection intact");
  endArchive();

  Seed();
  chosenLabel = L"Bravo";
  menuMutation = enterArchive;
  ShowSessionOverflowMenu(g_hGui);
  const bool showDispatched = GetClientInputText() == L"Bravo" && g_iSelectedSessionTab == 2;
  Check(before ? showDispatched : GetClientInputText().empty() && g_iSelectedSessionTab == 0,
        "The guarded busy Show case observes the expected selection behavior");
  Check(!g_bUiEnabled && g_bClientNotesBusy && !IsWindowEnabled(g_hBtnGo) && closeTargets.empty(),
        "The Show case leaves the archive owner in control without sending a Close");
  endArchive();

  CloseCase(L"Close Bravo", {}, {bravoPid}, "The same Close command works once the launcher is idle");
  ShowCase(L"Bravo", {}, L"Bravo", "The same Show command works once the launcher is idle");
  Check(menuCalls == 4 && scenarios == 4 && !launchRequests && !forbiddenPaint &&
        !suppressedClipboardRequests, "Four native menu dispatches stay within private fixture boundaries");
  std::cout << "SESSION_MENU_BUSY closeDispatched=" << closeDispatched
            << " showDispatched=" << showDispatched << " idleControls=1\n";
}

static void SessionClosePointerCases(bool before) {
  const auto prepare = [] {
    Seed();
    // Equal-width tabs for one client in two browsers share the same glyph
    // coordinates when the earlier browser exits.
    g_sessions[2].clientName = L"Bravo";
    auto firefox = g_activeProfiles.extract(ClientBrowserKey{L"Charlie",BrowserKind::Firefox});
    firefox.key().clientName = L"Bravo";
    g_activeProfiles.insert(std::move(firefox));
    g_rcSessionTabs = {0,0,600,32};
    BuildSessionTabRects(g_hGui,nullptr);
    fixtureCapture = nullptr;
  };
  const auto closePoint = [](int tab) {
    const auto &rect = g_sessionTabCloseRects.at(tab);
    if (rect.right <= rect.left || rect.bottom <= rect.top)
      throw std::runtime_error("Private fixture session close glyph is missing");
    return POINT{(rect.left + rect.right)/2,(rect.top + rect.bottom)/2};
  };
  const auto sendPointer = [](UINT message, POINT point) {
    WndProc(g_hGui,message,message==WM_LBUTTONDOWN ? MK_LBUTTON : 0,
            MAKELPARAM(point.x,point.y));
  };

  prepare();
  const auto originalPosition = closePoint(2);
  sendPointer(WM_LBUTTONDOWN,originalPosition);
  OnSessionEnded(bravoPid);
  Check(HitTestSessionClose(g_hGui,originalPosition)==2 &&
        g_sessions[1].pid==charliePid,
        "The Firefox session now occupies the former Chrome close position");
  sendPointer(WM_LBUTTONUP,originalPosition);
  const bool inheritedClose = closeTargets == std::vector<DWORD>{charliePid};
  Check(before ? inheritedClose : closeTargets.empty(),
        "A different browser for the same client cannot inherit a pending Close");

  prepare();
  sendPointer(WM_LBUTTONUP,closePoint(2));
  const bool releaseOnlyClose = closeTargets == std::vector<DWORD>{bravoPid};
  Check(before ? releaseOnlyClose : closeTargets.empty(),
        "Releasing over a close glyph without pressing it does not close the browser");

  prepare();
  sendPointer(WM_LBUTTONDOWN,closePoint(2));
  sendPointer(WM_LBUTTONUP,closePoint(3));
  const bool adjacentClose = closeTargets == std::vector<DWORD>{charliePid};
  Check(before ? adjacentClose : closeTargets.empty(),
        "Pressing one close glyph and releasing over another does not close either browser");

  prepare();
  const auto unchangedPosition = closePoint(2);
  sendPointer(WM_LBUTTONDOWN,unchangedPosition);
  sendPointer(WM_LBUTTONUP,unchangedPosition);
  Check(closeTargets == std::vector<DWORD>{bravoPid},
        "A complete click on the same close glyph reaches exactly its original browser");

  prepare();
  const auto lostPosition = closePoint(2);
  sendPointer(WM_LBUTTONDOWN,lostPosition);
  fixtureCapture = nullptr;
  WndProc(g_hGui,WM_CAPTURECHANGED,0,0);
  sendPointer(WM_LBUTTONUP,lostPosition);
  const bool captureLostClose = closeTargets == std::vector<DWORD>{bravoPid};
  Check(before ? captureLostClose : closeTargets.empty(),
        "Capture loss cancels the pending Close");

  prepare();
  const auto busyPosition = closePoint(2);
  sendPointer(WM_LBUTTONDOWN,busyPosition);
  SetUiState(false);
  sendPointer(WM_LBUTTONUP,busyPosition);
  Check(closeTargets.empty() && !fixtureCapture,
        "A close gesture clears capture when the launcher becomes busy");
  SetUiState(true);
  Check(!launchRequests && !forbiddenPaint && !suppressedClipboardRequests,
        "Close gestures stay within the private window and synthetic lifetime fixture");
  std::cout << "SESSION_CLOSE_POINTER inherited=" << inheritedClose
            << " releaseOnly=" << releaseOnlyClose << " adjacent=" << adjacentClose
            << " captureLost=" << captureLostClose << '\n';
}

static void SessionNormalCloseCases(bool before) {
  Seed();
  SelectSessionTab(2,false);
  fixtureBrowserWindowsReady=false;
  CloseSessionTab(2);
  const bool attemptedTermination=terminationTargets==std::vector<DWORD>{bravoPid};
  Check(before ? attemptedTermination : terminationTargets.empty(),
        "Individual Close does not forcibly stop a browser that has no ready window");
  Check(closeTargets.empty() && g_sessions.size()==3 && g_sessions[1].pid==bravoPid,
        "A windowless close leaves the original tracked session intact");
  Check(before ? closeNotice.empty() && g_iSelectedSessionTab==0
               : closeNotice.find(L"try Close again")!=std::wstring::npos &&
                   g_iSelectedSessionTab==2,
        "A windowless close leaves the selected tab available for a retry");
  fixtureBrowserWindowsReady=true;
  CloseSessionTab(2);
  Check(closeTargets==std::vector<DWORD>{bravoPid},
        "A retry after the window appears requests normal closure of exactly that browser");

  Seed();
  SelectSessionTab(2,false);
  refuseClosePost=true;
  CloseSessionTab(2);
  Check(closeTargets.empty() && terminationTargets.empty(),
        "Refusal to queue WM_CLOSE never becomes a forced stop");
  Check(before ? closeNotice.empty() && g_iSelectedSessionTab==0
               : closeNotice.find(L"try Close again")!=std::wstring::npos &&
                   g_iSelectedSessionTab==2,
        "A failed normal close post offers a retry and preserves selection");
  refuseClosePost=false;
  CloseSessionTab(2);
  Check(closeTargets==std::vector<DWORD>{bravoPid} && terminationTargets.empty(),
        "Retry after a failed post reaches only the original window");
  Check(!launchRequests && !forbiddenPaint && !suppressedClipboardRequests,
        "Normal-close tests stay within the hidden fixture and synthetic lifetime handles");
  std::cout << "SESSION_NORMAL_CLOSE windowlessTermination=" << attemptedTermination << '\n';
}

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
#ifndef CTSPACES_SESSION_MENU_BEFORE
#include "LauncherBrowserSlotTests.inl"
#include "LauncherAppRoundCases.inl"
#include "LauncherWorkflowRoundCases.inl"
#include "LauncherInteractionRoundCases.inl"
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
    for (auto &window : fixtureBrowserWindows) {
      window=child(L"STATIC",0);
      SetWindowTextW(window,L"Private modeled browser");
    }
    LoadBakedThemesFromHeader();g_themeColors=g_aCustomThemes.front().colors;UpdateThemeBrushes();
    const bool pinnedOnly=argc==2&&wcscmp(argv[1],L"--pinned-only")==0;
    const bool busyOnly=argc==2&&wcscmp(argv[1],L"--busy-link-only")==0;
    const bool busySessionBefore=argc==2&&wcscmp(argv[1],L"--busy-session-before")==0;
    const bool busySessionAfter=argc==2&&wcscmp(argv[1],L"--busy-session-after")==0;
    const bool closePointerBefore=argc==2&&wcscmp(argv[1],L"--close-pointer-before")==0;
    const bool closePointerAfter=argc==2&&wcscmp(argv[1],L"--close-pointer-after")==0;
    const bool normalCloseBefore=argc==2&&wcscmp(argv[1],L"--normal-close-before")==0;
    const bool normalCloseAfter=argc==2&&wcscmp(argv[1],L"--normal-close-after")==0;
    const bool pinnedBrowserBefore=argc==2&&wcscmp(argv[1],L"--pinned-browser-before")==0;
    const bool pinnedBrowserAfter=argc==2&&wcscmp(argv[1],L"--pinned-browser-after")==0;
    const bool restoreSlotBefore=argc==2&&wcscmp(argv[1],L"--restore-slot-before")==0;
    const bool restoreSlotAfter=argc==2&&wcscmp(argv[1],L"--restore-slot-after")==0;
    const bool browserSwitchBefore=argc==2&&wcscmp(argv[1],L"--browser-switch-before")==0;
    const bool browserSwitchAfter=argc==2&&wcscmp(argv[1],L"--browser-switch-after")==0;
    const bool sessionContinuityBefore=argc==2&&wcscmp(argv[1],L"--session-continuity-before")==0;
    const bool sessionContinuityAfter=argc==2&&wcscmp(argv[1],L"--session-continuity-after")==0;
    const bool launcherOpenBefore=argc==2&&wcscmp(argv[1],L"--launcher-open-before")==0;
    const bool launcherOpenAfter=argc==2&&wcscmp(argv[1],L"--launcher-open-after")==0;
    const bool showPointerBefore=argc==2&&wcscmp(argv[1],L"--session-show-pointer-before")==0;
    const bool showPointerAfter=argc==2&&wcscmp(argv[1],L"--session-show-pointer-after")==0;
    const bool pinnedPointerBefore=argc==2&&wcscmp(argv[1],L"--pinned-open-pointer-before")==0;
    const bool pinnedPointerAfter=argc==2&&wcscmp(argv[1],L"--pinned-open-pointer-after")==0;
    const bool paintedPointerBefore=argc==2&&wcscmp(argv[1],L"--painted-control-pointer-before")==0;
    const bool paintedPointerAfter=argc==2&&wcscmp(argv[1],L"--painted-control-pointer-after")==0;
    const bool busyGestureBefore=argc==2&&wcscmp(argv[1],L"--launcher-busy-gesture-before")==0;
    const bool busyGestureAfter=argc==2&&wcscmp(argv[1],L"--launcher-busy-gesture-after")==0;
    const bool invalidSessionEditBefore=argc==2&&wcscmp(argv[1],L"--launcher-invalid-session-edit-before")==0;
    const bool closeAllDeadlineBefore=argc==2&&wcscmp(argv[1],L"--launcher-close-all-deadline-before")==0;
    unsigned appRound = 0;
    bool paintedRound = false;
    bool workflowRound = false;
    bool interactionRound = false;
    if (argc == 2) {
      std::wstring_view argument = argv[1];
      constexpr std::wstring_view prefix = L"--app-round-";
      if (argument.starts_with(prefix)) {
        argument.remove_prefix(prefix.size());
        paintedRound = argument.starts_with(L"s-");
        workflowRound = argument.starts_with(L"t-");
        interactionRound = argument.starts_with(L"u-");
        if (paintedRound || workflowRound || interactionRound)
          argument.remove_prefix(2);
        if (argument == L"10")
          appRound = 10;
        else if (argument.size() == 1 && argument.front() >= L'1' && argument.front() <= L'9')
          appRound = static_cast<unsigned>(argument.front() - L'0');
      }
    }
    if (argc != 1 && !pinnedOnly && !busyOnly && !busySessionBefore && !busySessionAfter &&
        !closePointerBefore && !closePointerAfter && !pinnedBrowserBefore && !pinnedBrowserAfter &&
        !normalCloseBefore && !normalCloseAfter && !restoreSlotBefore && !restoreSlotAfter &&
        !browserSwitchBefore && !browserSwitchAfter && !sessionContinuityBefore && !sessionContinuityAfter &&
        !launcherOpenBefore && !launcherOpenAfter && !showPointerBefore && !showPointerAfter &&
        !pinnedPointerBefore && !pinnedPointerAfter && !paintedPointerBefore && !paintedPointerAfter &&
        !busyGestureBefore && !busyGestureAfter && !invalidSessionEditBefore && !closeAllDeadlineBefore && !appRound)
      throw std::runtime_error("Unknown launcher session menu test argument");
    if (appRound) {
#ifndef CTSPACES_SESSION_MENU_BEFORE
      if (interactionRound)
        NativeLauncherInteractionRound(appRound);
      else if (workflowRound)
        NativeLauncherWorkflowRound(appRound);
      else if (paintedRound)
        NativePaintedAppLauncherRound(appRound);
      else
        NativeAppLauncherRound(appRound);
#else
      throw std::runtime_error("Use the current production source family for the launcher round");
#endif
    } else if (invalidSessionEditBefore || closeAllDeadlineBefore) {
#ifndef CTSPACES_SESSION_MENU_BEFORE
      if (invalidSessionEditBefore)
        NativeInvalidSessionEdit(true);
      else
        NativeLateCloseAllWindow(true);
#else
      throw std::runtime_error("Use the current production source family for launcher interaction proofs");
#endif
    } else if (busyGestureBefore || busyGestureAfter) {
#ifndef CTSPACES_SESSION_MENU_BEFORE
      NativeBusyPointerGestures(busyGestureBefore);
#else
      throw std::runtime_error("Use the current production source family for the busy gesture proof");
#endif
    } else if (paintedPointerBefore || paintedPointerAfter) {
#ifndef CTSPACES_SESSION_MENU_BEFORE
      NativePaintedControlPointerCases(paintedPointerBefore);
#else
      throw std::runtime_error("Use the current production source family for the painted control proof");
#endif
    } else if (pinnedPointerBefore || pinnedPointerAfter) {
#ifndef CTSPACES_SESSION_MENU_BEFORE
      NativePinnedOpenPointerCases(pinnedPointerBefore);
#else
      throw std::runtime_error("Use the current production source family for the pinned click proof");
#endif
    } else if (showPointerBefore || showPointerAfter) {
#ifndef CTSPACES_SESSION_MENU_BEFORE
      NativeSessionShowPointerCases(showPointerBefore);
#else
      throw std::runtime_error("Use the current production source family for the session click proof");
#endif
    } else if (launcherOpenBefore || launcherOpenAfter) {
#ifndef CTSPACES_SESSION_MENU_BEFORE
      NativeLauncherOpenCases(launcherOpenBefore);
#else
      throw std::runtime_error("Use the current production source family for the launcher Open proof");
#endif
    } else if (sessionContinuityBefore || sessionContinuityAfter) {
#ifndef CTSPACES_SESSION_MENU_BEFORE
      NativeSessionContinuityCases(sessionContinuityBefore);
#else
      throw std::runtime_error("Use the current production source family for the session continuity proof");
#endif
    } else if (restoreSlotBefore || restoreSlotAfter || browserSwitchBefore || browserSwitchAfter) {
#ifndef CTSPACES_SESSION_MENU_BEFORE
      if (restoreSlotBefore || restoreSlotAfter)
        NativeRestoreSlotCases(restoreSlotBefore);
      else
        NativeBrowserSwitchCases(browserSwitchBefore);
#else
      throw std::runtime_error("Use the current production source family for the browser slot proof");
#endif
    } else if (normalCloseBefore || normalCloseAfter) {
#ifndef CTSPACES_SESSION_MENU_BEFORE
      SessionNormalCloseCases(normalCloseBefore);
#else
      throw std::runtime_error("Use the current production source family for the normal-close proof");
#endif
    } else if (pinnedBrowserBefore || pinnedBrowserAfter) {
#ifndef CTSPACES_SESSION_MENU_BEFORE
      NativePinnedBrowserCases(pinnedBrowserBefore);
#else
      throw std::runtime_error("Use the current production source family for the pinned-browser proof");
#endif
    } else if (closePointerBefore || closePointerAfter) {
#ifndef CTSPACES_SESSION_MENU_BEFORE
      SessionClosePointerCases(closePointerBefore);
#else
      throw std::runtime_error("Use the current production source family for the close-pointer proof");
#endif
    } else if (busySessionBefore || busySessionAfter) {
#ifndef CTSPACES_SESSION_MENU_BEFORE
      BusySessionMenuCases(busySessionBefore);
#else
      throw std::runtime_error("Use the current production source family for the busy-session proof");
#endif
    } else {
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
    }
    DestroyWindow(g_hGui);g_hGui=nullptr;
    MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if((msg.message>=WM_MOUSEFIRST&&msg.message<=WM_MOUSELAST)||(msg.message>=WM_KEYFIRST&&msg.message<=WM_KEYLAST))throw std::runtime_error("Unexpected device input");DispatchMessageW(&msg);}
    unsigned visible=0;EnumWindows([](HWND w,LPARAM value)->BOOL{DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(w))++*reinterpret_cast<unsigned *>(value);return TRUE;},reinterpret_cast<LPARAM>(&visible));
    Check(!visible&&!shows&&!focuses&&!foregrounds,"Native audit: zero visible windows, show, focus and foreground events");
    for(auto hook:hooks)UnhookWinEvent(hook);hooks.clear();Gdiplus::GdiplusShutdown(g_gdiplusToken);
    std::cout<<"Session menu: "<<checks<<" checks, "<<scenarios<<" scenarios, "<<failures<<" failures; suppressed presentation requests="<<presentationRequests<<"; no device input or real process opens.\n";
    return failures?1:0;
  }catch(const std::exception &error){std::cerr<<"Session menu safety/setup failure: "<<error.what()<<'\n';for(auto hook:hooks)UnhookWinEvent(hook);return 2;}
}
