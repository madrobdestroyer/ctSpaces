// Actual launcher drag/session lifecycle calls against private hidden controls.
// Mouse capture, cursor changes, presentation, clipboard and launches are inert.
#include <windows.h>
#include "RetainedFixture.h"
#include <shellapi.h>
#include <iostream>
#include <stdexcept>
#include <vector>
static unsigned presentationRequests=0,launchRequests=0,forbiddenPaint=0,captureRequests=0,cursorRequests=0,showRequests=0;
static std::vector<DWORD> trayRequests;
static DWORD failTrayAction=MAXDWORD;
static HWND fakeCapture=nullptr;
static BOOL WINAPI NoShow(HWND,int){++presentationRequests;++showRequests;return FALSE;}
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
static HANDLE WINAPI NoProcess(DWORD,BOOL,DWORD){++launchRequests;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
static BOOL WINAPI NoTerminate(HANDLE,UINT){++launchRequests;return FALSE;}
static HINSTANCE WINAPI NoShell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){++launchRequests;return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);}
static BOOL WINAPI NoShellEx(SHELLEXECUTEINFOW *){++launchRequests;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL WINAPI NoCreateProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){++launchRequests;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL WINAPI NoTray(DWORD action,PNOTIFYICONDATAW){trayRequests.push_back(action);return action!=failTrayAction;}
static BOOL WINAPI NoCursorPosition(LPPOINT point){if(point)*point={};return FALSE;}
static BOOL WINAPI NoCaptureRelease(){++captureRequests;fakeCapture=nullptr;return TRUE;}
static HWND WINAPI NoCapture(HWND window){++captureRequests;const auto prior=fakeCapture;fakeCapture=window;return prior;}
static HWND WINAPI FakeCapture(){return fakeCapture;}
static HCURSOR WINAPI NoCursor(HCURSOR){++cursorRequests;return nullptr;}
static HWND WINAPI NoWindowFromPoint(POINT){return nullptr;}
static BOOL WINAPI NoClipboard(HWND){++launchRequests;return FALSE;}
static UINT WINAPI NoPopup(HMENU,UINT,int,int,HWND,LPTPMPARAMS){++presentationRequests;return 0;}
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
#define TerminateProcess NoTerminate
#define ShellExecuteW NoShell
#define ShellExecuteExW NoShellEx
#define CreateProcessW NoCreateProcess
#define Shell_NotifyIconW NoTray
#define GetCursorPos NoCursorPosition
#define SetCursor NoCursor
#define WindowFromPoint NoWindowFromPoint
#define SetCapture NoCapture
#define GetCapture FakeCapture
#define ReleaseCapture NoCaptureRelease
#define OpenClipboard NoClipboard
#define TrackPopupMenuEx NoPopup
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain ProductionEntryNotRun
#if defined(CTSPACES_DRAG_MUTATION_BEFORE)
#include "../build/app-audit-round9-2026-10-15/launcher/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain

int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR,LPCWSTR,UINT flags){return flags&MB_YESNO ? IDNO : IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &){}
static unsigned checks=0,failures=0,scenarios=0,shows=0,focuses=0,foregrounds=0;
static void Check(bool value,const char *message){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';}
static void CALLBACK Observe(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId())return;if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW)++shows;if(event==EVENT_OBJECT_FOCUS)++focuses;if(event==EVENT_SYSTEM_FOREGROUND)++foregrounds;}
static std::vector<std::wstring> Names(){std::vector<std::wstring> names;for(const auto &session:g_sessions)names.push_back(session.clientName);return names;}
static std::string ReadBytes(const fs::path &path){std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Cannot read fixture config");return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
static POINT Center(const RECT &r){return {(r.left+r.right)/2,(r.top+r.bottom)/2};}
static void ResetDrag(){WndProc(g_hGui,WM_CAPTURECHANGED,0,0);fakeCapture=nullptr;}
static void Seed(){
  ResetDrag();++scenarios;g_bUiEnabled=true;g_bCloseAllClientsPending=false;g_bExitWhenProfilesClose=false;
  g_sessions={{L"Alpha",BrowserKind::Edge,101},{L"Bravo",BrowserKind::Chrome,102},{L"Charlie",BrowserKind::Firefox,103},{L"Delta",BrowserKind::Brave,104}};
  g_iSelectedSessionTab=3;g_rcSessionTabs={0,0,1400,34};g_rcPinnedArea={14,38,1386,77};
  BuildSessionTabRects(g_hGui,nullptr);
}
static void BeginSession(int tab){StartMainDragCandidate(g_hGui,MainDragKind::SessionTab,tab,Center(g_sessionTabRects.at(tab)));}
static void MoveToSession(int tab){const auto p=Center(g_sessionTabRects.at(tab));UpdateMainDrag(g_hGui,p,MK_LBUTTON);}
static void SessionMutationCases(){
  Seed();const auto selected=g_sessions[2].identity;BeginSession(2);MoveToSession(4);
  Check(Names()==std::vector<std::wstring>{L"Alpha",L"Charlie",L"Delta",L"Bravo"},"Normal session drag reorders the intended session");
  Check(g_sessions.at(g_iSelectedSessionTab-1).identity==selected,"Normal session drag retains the selected session identity");
  FinishMainDrag(g_hGui,Center(g_sessionTabRects.at(4)));
  Check(g_mainDragKind==MainDragKind::None&&!fakeCapture,"Normal session drag finishes without retaining capture or candidate state");

  Seed();BeginSession(2);OnSessionEnded(101);MoveToSession(3);
  Check(Names()==std::vector<std::wstring>{L"Charlie",L"Delta",L"Bravo"},"An earlier browser exit cannot make a drag move the adjacent session");
  Check(g_iSelectedSessionTab>0&&g_sessions.at(g_iSelectedSessionTab-1).clientName==L"Charlie","Earlier browser exit plus drag retains the selected surviving session");

  Seed();BeginSession(2);OnSessionEnded(102);const auto afterExit=Names();MoveToSession(3);
  Check(Names()==afterExit,"A drag whose session exited cannot reorder a surviving neighbor");
  const auto selectedAfterExit=g_sessions.at(g_iSelectedSessionTab-1).identity;const auto released=Center(g_sessionTabRects.at(3));WndProc(g_hGui,WM_LBUTTONUP,0,MAKELPARAM(released.x,released.y));
  Check(Names()==afterExit&&g_sessions.at(g_iSelectedSessionTab-1).identity==selectedAfterExit&&!fakeCapture,"A vanished-session drag consumes button release instead of selecting the surviving neighbor");

  Seed();BeginSession(2);OnSessionEnded(102);g_selectedBrowser=BrowserKind::Chrome;OnSessionStarted(102,L"Bravo");const auto afterRestart=Names();MoveToSession(3);
  Check(Names()==afterRestart,"A fresh session with reused client and PID cannot inherit an earlier drag");

  Seed();BeginSession(2);MoveToSession(4);OnSessionEnded(101);MoveToSession(1);
  Check(Names()==std::vector<std::wstring>{L"Bravo",L"Charlie",L"Delta"},"A browser exit after the first drag move keeps later moves bound to the original session");

  Seed();BeginSession(2);MoveToSession(4);const auto beforeCaptureLoss=Names();WndProc(g_hGui,WM_CAPTURECHANGED,0,0);MoveToSession(1);
  Check(Names()==beforeCaptureLoss&&g_mainDragKind==MainDragKind::None,"Capture loss ends the session drag and later movement does nothing");

  Seed();BeginSession(2);OnSessionEnded(102);const auto beforeMissingRelease=Names();const auto selectedBeforeMissingRelease=g_sessions.at(g_iSelectedSessionTab-1).identity;const auto missingRelease=Center(g_sessionTabRects.at(3));
  WndProc(g_hGui,WM_LBUTTONUP,0,MAKELPARAM(missingRelease.x,missingRelease.y));
  Check(Names()==beforeMissingRelease&&g_sessions.at(g_iSelectedSessionTab-1).identity==selectedBeforeMissingRelease&&!fakeCapture,"A session exit before any mousemove consumes release without activating an adjacent session");

  Seed();BeginSession(2);OnSessionEnded(101);const auto beforeShiftedRelease=Names();const auto selectedBeforeShiftedRelease=g_sessions.at(g_iSelectedSessionTab-1).identity;const auto shiftedRelease=Center(g_sessionTabRects.at(3));
  WndProc(g_hGui,WM_LBUTTONUP,0,MAKELPARAM(shiftedRelease.x,shiftedRelease.y));
  Check(Names()==beforeShiftedRelease&&g_sessions.at(g_iSelectedSessionTab-1).identity==selectedBeforeShiftedRelease&&!fakeCapture,"An earlier exit before any mousemove consumes the stale-position release");

  Seed();BeginSession(2);
  Check(!FinishMainDrag(g_hGui,Center(g_sessionTabRects.at(2)))&&g_mainDragKind==MainDragKind::None&&!fakeCapture,"An unchanged undragged session candidate still permits ordinary click handling");
}
static void PinPersistenceCases(const fs::path &root){
  const auto seedPins=[&](){Seed();g_sDataDir=root/std::to_wstring(scenarios);fs::create_directories(g_sDataDir/L"Sites");g_sConfigPath=g_sDataDir/L"config.ini";g_pinnedClients={L"Alpha",L"Bravo",L"Charlie",L"Delta"};g_archivedClients.clear();for(const auto &name:g_pinnedClients)fs::create_directory(g_sDataDir/L"Sites"/name);Check(SavePinnedClients(),"Persist exact initial private pin order");BuildPinnedClientRects();};
  seedPins();const auto initial=g_pinnedClients;StartMainDragCandidate(g_hGui,MainDragKind::PinnedClient,1,Center(g_pinnedClientRects[1]));MovePinnedClientForDrag(1,3);FinishMainDrag(g_hGui,Center(g_pinnedClientRects[3]));g_pinnedClients.clear();LoadPinnedClients();
  Check(g_pinnedClients==std::vector<std::wstring>{L"Alpha",L"Charlie",L"Delta",L"Bravo"},"Normal pinned drag persists its order and reloads exactly");

  seedPins();const auto original=g_pinnedClients;const auto bytes=ReadBytes(g_sConfigPath);HANDLE lock=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot lock private config");
  StartMainDragCandidate(g_hGui,MainDragKind::PinnedClient,1,Center(g_pinnedClientRects[1]));MovePinnedClientForDrag(1,3);FinishMainDrag(g_hGui,Center(g_pinnedClientRects[3]));CloseHandle(lock);
  Check(g_pinnedClients==original&&ReadBytes(g_sConfigPath)==bytes,"Locked pin-order save restores exact memory order and original disk bytes");
  StartMainDragCandidate(g_hGui,MainDragKind::PinnedClient,1,Center(g_pinnedClientRects[1]));MovePinnedClientForDrag(1,3);FinishMainDrag(g_hGui,Center(g_pinnedClientRects[3]));g_pinnedClients.clear();LoadPinnedClients();
  Check(g_pinnedClients==std::vector<std::wstring>{L"Alpha",L"Charlie",L"Delta",L"Bravo"},"Pin-order save retries successfully after config replacement lock is released");

  seedPins();StartMainDragCandidate(g_hGui,MainDragKind::PinnedClient,1,Center(g_pinnedClientRects[1]));MovePinnedClientForDrag(1,3);WndProc(g_hGui,WM_CAPTURECHANGED,0,0);g_pinnedClients.clear();LoadPinnedClients();
  Check(g_pinnedClients==std::vector<std::wstring>{L"Alpha",L"Charlie",L"Delta",L"Bravo"},"Capture loss persists completed pinned reordering exactly once");
}
static void TrayStateCases(const fs::path &root){
  ++scenarios;g_sDataDir=root;fs::create_directories(root);g_sConfigPath=root/L"config.ini";
  g_minimizeToTray=false;g_launcherInTray=false;g_launcherTrayIcon=false;
  Check(SaveConfigMutations(L"private tray fixture",{{L"user",std::wstring(L"minimize_to_tray"),std::wstring(L"0")}}),"Initialize isolated tray preference");
  WndProc(g_hGui,WM_COMMAND,IDM_CTX_MINIMIZE_TRAY,0);
  Check(g_minimizeToTray&&GetPrivateProfileIntW(L"user",L"minimize_to_tray",0,g_sConfigPath.c_str())==1,"Tray preference normal toggle commits memory and disk together");
  HANDLE lock=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot lock isolated tray preference");
  const auto before=ReadBytes(g_sConfigPath);WndProc(g_hGui,WM_COMMAND,IDM_CTX_MINIMIZE_TRAY,0);CloseHandle(lock);
  Check(g_minimizeToTray&&ReadBytes(g_sConfigPath)==before,"Locked tray preference save retains memory and exact disk bytes");
  WndProc(g_hGui,WM_COMMAND,IDM_CTX_MINIMIZE_TRAY,0);
  Check(!g_minimizeToTray&&GetPrivateProfileIntW(L"user",L"minimize_to_tray",1,g_sConfigPath.c_str())==0,"Tray preference retries after native config lock release");

  ++scenarios;trayRequests.clear();auto showsBefore=showRequests;WndProc(g_hGui,WM_SIZE,SIZE_MINIMIZED,0);
  Check(trayRequests.empty()&&showRequests==showsBefore&&!g_launcherInTray,"Minimizing with tray preference off does not request a tray icon or hide");
  failTrayAction=NIM_ADD;Check(!HideLauncherInTray(g_hGui)&&!g_launcherInTray&&!g_launcherTrayIcon&&showRequests==showsBefore,"Failed tray insertion leaves the launcher available and never requests hide");
  Check(trayRequests==std::vector<DWORD>{NIM_ADD},"Failed tray insertion stops before version negotiation");

  ++scenarios;trayRequests.clear();failTrayAction=NIM_SETVERSION;
  Check(!HideLauncherInTray(g_hGui)&&!g_launcherInTray&&!g_launcherTrayIcon&&showRequests==showsBefore,"Failed tray version negotiation preserves launcher availability");
  Check(trayRequests==std::vector<DWORD>{NIM_ADD,NIM_SETVERSION,NIM_DELETE},"Failed tray negotiation cleans up only its provisional icon");

  ++scenarios;trayRequests.clear();failTrayAction=MAXDWORD;
  Check(HideLauncherInTray(g_hGui)&&g_launcherInTray&&g_launcherTrayIcon&&showRequests==showsBefore+1,"Successful mocked tray transition records its icon before requesting hide");
  const auto count=trayRequests.size();Check(HideLauncherInTray(g_hGui)&&trayRequests.size()==count,"Repeated hide does not create a duplicate tray icon");
  showsBefore=showRequests;WndProc(g_hGui,WM_APP_LAUNCHER_TRAY,0,MAKELPARAM(NIN_SELECT,2));
  Check(showRequests==showsBefore&&g_launcherInTray,"A callback for another tray icon cannot restore the launcher");
  WndProc(g_hGui,WM_APP_LAUNCHER_TRAY,0,MAKELPARAM(NIN_SELECT,1));
  Check(!g_launcherInTray&&!g_launcherTrayIcon&&trayRequests.back()==NIM_DELETE,"The matching tray callback removes its own icon and requests restore");

  ++scenarios;Check(HideLauncherInTray(g_hGui),"Prepare mocked hidden state for Explorer restart");trayRequests.clear();showsBefore=showRequests;
  WndProc(g_hGui,g_taskbarCreated,0,0);
  Check(g_launcherInTray&&g_launcherTrayIcon&&showRequests==showsBefore&&trayRequests==std::vector<DWORD>{NIM_ADD,NIM_SETVERSION},"Explorer restart recreates exactly one notification icon without requesting a window show");
  trayRequests.clear();failTrayAction=NIM_ADD;WndProc(g_hGui,g_taskbarCreated,0,0);
  Check(!g_launcherInTray&&!g_launcherTrayIcon&&showRequests==showsBefore+1,"Failed Explorer-restart icon recovery requests restore instead of stranding a hidden launcher");
  failTrayAction=MAXDWORD;g_minimizeToTray=false;
}
int wmain(){
  std::vector<HWINEVENTHOOK> hooks;
  try{
    for(DWORD event:{EVENT_SYSTEM_FOREGROUND,EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS}){auto hook=SetWinEventHook(event,event,nullptr,Observe,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);if(!hook)throw std::runtime_error("Cannot audit native background events");hooks.push_back(hook);}
    g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();
    const fs::path root=retained_fixture::CreateRoot(fs::current_path()/L"build"/L"app-audit-round9-2026-10-15"/L"launcher",L"fixture");
    g_sDataDir=root;g_sConfigPath=root/L"config.ini";
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_TAB_CLASSES};if(!InitCommonControlsEx(&controls))throw std::runtime_error("Cannot initialize private controls");
    Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);
    g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden launcher drag fixture",WS_OVERLAPPED|WS_CLIPCHILDREN,0,0,1400,234,nullptr,nullptr,g_hInst,nullptr);RequireOwned(g_hGui);
    const auto child=[&](const wchar_t *kind,DWORD style){HWND result=CreateWindowExW(WS_EX_NOACTIVATE,kind,L"",WS_CHILD|style,0,0,100,24,g_hGui,nullptr,g_hInst,nullptr);RequireOwned(result);return result;};
    g_hClientEdit=child(L"EDIT",ES_AUTOHSCROLL);g_hComboClient=child(L"COMBOBOX",CBS_DROPDOWN);
    g_hClientEditSurface=child(L"STATIC",0);g_hBtnClientIcon=child(L"BUTTON",0);g_hBtnClientDrop=child(L"BUTTON",0);
    g_hBtnGo=child(L"BUTTON",0);g_hBtnPin=child(L"BUTTON",0);g_hBtnRestoreTabs=child(L"BUTTON",0);
    g_hBtnConfig=child(L"BUTTON",0);g_hBtnTmpProf=child(L"BUTTON",0);g_hBtnCloseAll=child(L"BUTTON",0);
    LoadBakedThemesFromHeader();g_themeColors=g_aCustomThemes.front().colors;UpdateThemeBrushes();
    SessionMutationCases();PinPersistenceCases(root/L"pins");TrayStateCases(root/L"tray");ResetDrag();
    Check(launchRequests==0&&forbiddenPaint==0,"No process, browser, shell, clipboard or foreign redraw request occurred; tray and presentation APIs were intercepted");
    DestroyWindow(g_hGui);g_hGui=nullptr;MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if((msg.message>=WM_MOUSEFIRST&&msg.message<=WM_MOUSELAST)||(msg.message>=WM_KEYFIRST&&msg.message<=WM_KEYLAST))throw std::runtime_error("Unexpected device input");DispatchMessageW(&msg);}
    unsigned visible=0;EnumWindows([](HWND w,LPARAM value)->BOOL{DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(w))++*reinterpret_cast<unsigned *>(value);return TRUE;},reinterpret_cast<LPARAM>(&visible));
    Check(!visible&&!shows&&!focuses&&!foregrounds,"Native audit: zero visible windows, show, focus or foreground events");
    for(auto hook:hooks)UnhookWinEvent(hook);Gdiplus::GdiplusShutdown(g_gdiplusToken);
    std::cout<<"Launcher drag mutation: "<<checks<<" checks, "<<scenarios<<" scenarios, "<<failures<<" failures; presentation requests suppressed="<<presentationRequests<<"; capture requests suppressed="<<captureRequests<<"; cursor requests suppressed="<<cursorRequests<<".\n";
    return failures?1:0;
  }catch(const std::exception &error){std::cerr<<"Launcher drag safety/setup failure: "<<error.what()<<'\n';for(auto hook:hooks)UnhookWinEvent(hook);return 2;}
}
