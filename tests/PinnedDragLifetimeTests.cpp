// Actual launcher drag/session lifecycle calls against private hidden controls.
// Mouse capture, cursor changes, presentation, clipboard and launches are inert.
#include <windows.h>
#include "RetainedFixture.h"
#include <shellapi.h>
#include <shlobj.h>
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
static HWND desktopTarget=nullptr;
static unsigned desktopRequests=0;
static HWND WINAPI NoWindowFromPoint(POINT){return desktopTarget;}
static HRESULT WINAPI NoKnownFolder(REFKNOWNFOLDERID,DWORD,HANDLE,PWSTR *output){++desktopRequests;if(output)*output=nullptr;return E_ACCESSDENIED;}
static BOOL WINAPI NoClipboard(HWND){++launchRequests;return FALSE;}
static UINT WINAPI NoPopup(HMENU,UINT,int,int,HWND,LPTPMPARAMS){++presentationRequests;return 0;}
#define SHGetKnownFolderPath NoKnownFolder
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
#if defined(CTSPACES_PINNED_MUTATION_BEFORE)
#include "../build/app-audit-round10-2026-10-15/pinned/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain

int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR,LPCWSTR,UINT flags){return flags&MB_YESNO ? IDNO : IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &){}
static unsigned checks=0,failures=0,scenarios=0,shows=0,focuses=0,foregrounds=0;
static fs::path fixture;
static void Check(bool value,const char *message){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';}
static void CALLBACK Observe(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId())return;if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW)++shows;if(event==EVENT_OBJECT_FOCUS)++focuses;if(event==EVENT_SYSTEM_FOREGROUND)++foregrounds;}
static std::string ReadBytes(const fs::path &path){std::ifstream stream(path,std::ios::binary);if(!stream)throw std::runtime_error("Cannot read fixture config");return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};}
static POINT Center(const RECT &r){return {(r.left+r.right)/2,(r.top+r.bottom)/2};}
static void ResetDrag(){WndProc(g_hGui,WM_CAPTURECHANGED,0,0);fakeCapture=nullptr;}
static void Seed(){
  ResetDrag();++scenarios;g_bUiEnabled=true;g_bCloseAllClientsPending=false;g_bExitWhenProfilesClose=false;
  g_selectedBrowser=BrowserKind::Edge;
  g_sessions={{L"Alpha",BrowserKind::Edge,0x7ffff351},{L"Bravo",BrowserKind::Edge,0x7ffff352},{L"Charlie",BrowserKind::Edge,0x7ffff353},{L"Delta",BrowserKind::Edge,0x7ffff354}};
  g_iSelectedSessionTab=0;g_rcSessionTabs={0,0,1400,34};g_rcPinnedArea={14,38,1386,77};BuildSessionTabRects(g_hGui,nullptr);
  g_sDataDir=fixture/std::to_wstring(scenarios);fs::create_directories(g_sDataDir/L"Sites");g_sConfigPath=g_sDataDir/L"config.ini";
  g_pinnedClients={L"Alpha",L"Bravo",L"Charlie",L"Delta"};g_archivedClients.clear();
  for(const auto &name:g_pinnedClients)fs::create_directory(g_sDataDir/L"Sites"/name);
  if(!SavePinnedClients())throw std::runtime_error("Cannot seed isolated pin config");
  BuildPinnedClientRects();desktopRequests=0;
}
static void Begin(int index){StartMainDragCandidate(g_hGui,MainDragKind::PinnedClient,index,Center(g_pinnedClientRects.at(index)));}
static void MoveTo(int index){UpdateMainDrag(g_hGui,Center(g_pinnedClientRects.at(index)),MK_LBUTTON);}
static void ReleaseAt(int index){const auto p=Center(g_pinnedClientRects.at(index));WndProc(g_hGui,WM_LBUTTONUP,0,MAKELPARAM(p.x,p.y));}
static void RemoveAndPrune(const wchar_t *name){if(!fs::remove(g_sDataDir/L"Sites"/name))throw std::runtime_error("Cannot remove private fixture client");PrunePinnedClients();BuildPinnedClientRects();}
static void CheckReload(const std::vector<std::wstring>&expected,const char *description){g_pinnedClients.clear();LoadPinnedClients();Check(g_pinnedClients==expected,description);}
static void MutationCases(){
  Seed();Begin(1);MoveTo(3);FinishMainDrag(g_hGui,Center(g_pinnedClientRects.at(3)));
  CheckReload({L"Alpha",L"Charlie",L"Delta",L"Bravo"},"Ordinary pinned drag persists its intended client and exact order");

  Seed();Begin(1);RemoveAndPrune(L"Alpha");const auto afterPrune=g_pinnedClients;MoveTo(2);
  Check(g_pinnedClients==std::vector<std::wstring>{L"Charlie",L"Delta",L"Bravo"}||g_pinnedClients==afterPrune,"Pruning an earlier pin never makes the drag reorder its surviving neighbor");
  FinishMainDrag(g_hGui,Center(g_pinnedClientRects.at(2)));

  Seed();Begin(1);RemoveAndPrune(L"Bravo");const auto remaining=g_pinnedClients;MoveTo(2);
  Check(g_pinnedClients==remaining,"Removing the dragged pin consumes further movement without reordering a neighbor");
  FinishMainDrag(g_hGui,Center(g_pinnedClientRects.at(2)));

  Seed();Begin(1);RemoveAndPrune(L"Bravo");SetClientInputText(L"Alpha");const auto originalInput=GetClientInputText();ReleaseAt(1);
  Check(GetClientInputText()==originalInput,"A vanished undragged pin consumes release instead of opening its former neighbor");
  Check(launchRequests==0,"A vanished pinned-client candidate never requests a surviving neighbor browser launch");

  Seed();Begin(1);RemoveAndPrune(L"Alpha");SetClientInputText(L"Delta");const auto beforeShiftedRelease=GetClientInputText();ReleaseAt(1);
  Check(GetClientInputText()==beforeShiftedRelease,"An earlier pin disappearing before any move consumes release instead of opening the stale-position neighbor");

  Seed();Begin(1);MoveTo(2);RemoveAndPrune(L"Bravo");UpdateMainDrag(g_hGui,POINT{1600,100},MK_LBUTTON);const auto priorDesktop=desktopRequests;
  FinishMainDrag(g_hGui,POINT{1600,100});
  Check(desktopRequests==priorDesktop,"A removed active pin cannot request a Desktop shortcut for an inherited neighbor");

  Seed();Begin(1);MoveTo(3);RemoveAndPrune(L"Alpha");const auto prunedOrder=g_pinnedClients;const auto bytes=ReadBytes(g_sConfigPath);
  HANDLE lock=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot lock isolated pinned config");
  FinishMainDrag(g_hGui,Center(g_pinnedClientRects.at(2)));CloseHandle(lock);
  Check(g_pinnedClients==prunedOrder&&ReadBytes(g_sConfigPath)==bytes,"A locked drag finish preserves a separately committed prune instead of restoring removed pins");
  CheckReload(prunedOrder,"Reload agrees with the committed independent prune after a locked drag finish");

  Seed();Begin(1);MoveTo(3);RemoveAndPrune(L"Alpha");const auto prunedBeforeCapture=g_pinnedClients;const auto captureBytes=ReadBytes(g_sConfigPath);
  lock=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot lock isolated capture-loss pin config");
  WndProc(g_hGui,WM_CAPTURECHANGED,0,0);CloseHandle(lock);
  Check(g_pinnedClients==prunedBeforeCapture&&ReadBytes(g_sConfigPath)==captureBytes,"Capture loss after an independently committed prune never restores stale pins on save failure");

  Seed();const auto original=g_pinnedClients;const auto originalBytes=ReadBytes(g_sConfigPath);Begin(1);MoveTo(3);
  lock=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot lock original pin rollback fixture");
  FinishMainDrag(g_hGui,Center(g_pinnedClientRects.at(3)));CloseHandle(lock);
  Check(g_pinnedClients==original&&ReadBytes(g_sConfigPath)==originalBytes,"Ordinary failed pin-order save restores the exact original memory and disk order");
  Begin(1);MoveTo(3);FinishMainDrag(g_hGui,Center(g_pinnedClientRects.at(3)));
  CheckReload({L"Alpha",L"Charlie",L"Delta",L"Bravo"},"Failed save retries successfully after releasing the native config lock");

  Seed();Begin(1);MoveTo(3);WndProc(g_hGui,WM_CAPTURECHANGED,0,0);const auto captureOrder=g_pinnedClients;MoveTo(1);
  Check(g_mainDragKind==MainDragKind::None&&g_pinnedClients==captureOrder,"Capture loss ends a completed drag and ignores later motion");
  CheckReload({L"Alpha",L"Charlie",L"Delta",L"Bravo"},"Capture loss persists an ordinary completed reorder");

  Seed();Begin(1);Check(!FinishMainDrag(g_hGui,Center(g_pinnedClientRects.at(1))),"An unchanged undragged pinned candidate still permits the ordinary click path");

  Seed();Begin(1);MoveTo(3);FinishMainDrag(g_hGui,POINT{1600,100});
  Check(desktopRequests==1,"An ordinary active pinned Desktop drop reaches only the inert Desktop resolver boundary");
}
int wmain(){
 std::vector<HWINEVENTHOOK> hooks;
 try{
  for(DWORD event:{EVENT_SYSTEM_FOREGROUND,EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS}){auto hook=SetWinEventHook(event,event,nullptr,Observe,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);if(!hook)throw std::runtime_error("Cannot audit background native events");hooks.push_back(hook);}
  g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();
  fixture=retained_fixture::CreateRoot(fs::current_path()/L"build"/L"app-audit-round10-2026-10-15"/L"pinned",L"fixture");
  INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_TAB_CLASSES};if(!InitCommonControlsEx(&controls))throw std::runtime_error("Cannot initialize private controls");
  Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);
  g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden pinned drag fixture",WS_OVERLAPPED|WS_CLIPCHILDREN,0,0,1400,234,nullptr,nullptr,g_hInst,nullptr);RequireOwned(g_hGui);
  const auto child=[&](const wchar_t *kind,DWORD style){HWND result=CreateWindowExW(WS_EX_NOACTIVATE,kind,L"",WS_CHILD|style,0,0,100,24,g_hGui,nullptr,g_hInst,nullptr);RequireOwned(result);return result;};
  g_hClientEdit=child(L"EDIT",ES_AUTOHSCROLL);g_hComboClient=child(L"COMBOBOX",CBS_DROPDOWN);g_hClientEditSurface=child(L"STATIC",0);g_hBtnClientIcon=child(L"BUTTON",0);g_hBtnClientDrop=child(L"BUTTON",0);g_hBtnGo=child(L"BUTTON",0);g_hBtnPin=child(L"BUTTON",0);g_hBtnRestoreTabs=child(L"BUTTON",0);g_hBtnConfig=child(L"BUTTON",0);g_hBtnTmpProf=child(L"BUTTON",0);g_hBtnCloseAll=child(L"BUTTON",0);
  WNDCLASSW desktopClass{};desktopClass.lpfnWndProc=DefWindowProcW;desktopClass.hInstance=g_hInst;desktopClass.lpszClassName=L"Progman";
  if(!RegisterClassW(&desktopClass))throw std::runtime_error("Cannot register private fake desktop class");
  desktopTarget=CreateWindowExW(WS_EX_NOACTIVATE,L"Progman",L"Private fake desktop hit target",WS_OVERLAPPED,0,0,1,1,nullptr,nullptr,g_hInst,nullptr);RequireOwned(desktopTarget);
  LoadBakedThemesFromHeader();g_themeColors=g_aCustomThemes.front().colors;UpdateThemeBrushes();
  MutationCases();ResetDrag();
  Check(launchRequests==0&&forbiddenPaint==0,"No process, shell, browser, clipboard or foreign redraw request occurred; capture, cursor and Desktop APIs were intercepted");
  DestroyWindow(desktopTarget);desktopTarget=nullptr;DestroyWindow(g_hGui);g_hGui=nullptr;
  MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if((msg.message>=WM_MOUSEFIRST&&msg.message<=WM_MOUSELAST)||(msg.message>=WM_KEYFIRST&&msg.message<=WM_KEYLAST))throw std::runtime_error("Unexpected device input");DispatchMessageW(&msg);}
  unsigned visible=0;EnumWindows([](HWND w,LPARAM value)->BOOL{DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(w))++*reinterpret_cast<unsigned *>(value);return TRUE;},reinterpret_cast<LPARAM>(&visible));
  Check(!visible&&!shows&&!focuses&&!foregrounds,"Native audit: zero visible windows, show, focus or foreground events");
  for(auto hook:hooks)UnhookWinEvent(hook);Gdiplus::GdiplusShutdown(g_gdiplusToken);
  std::cout<<"Pinned drag lifetime: "<<checks<<" checks, "<<scenarios<<" scenarios, "<<failures<<" failures; presentation requests suppressed="<<presentationRequests<<"; capture requests suppressed="<<captureRequests<<"; cursor requests suppressed="<<cursorRequests<<"; Desktop resolver requests suppressed="<<desktopRequests<<".\n";return failures?1:0;
 }catch(const std::exception &error){std::cerr<<"Pinned drag safety/setup failure: "<<error.what()<<'\n';for(auto hook:hooks)UnhookWinEvent(hook);return 2;}
}
