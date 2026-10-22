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
static BOOL WINAPI NoCreateProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){++launchRequests;SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
static BOOL WINAPI NoTray(DWORD,PNOTIFYICONDATAW){++launchRequests;return FALSE;}
static BOOL WINAPI NoCursor(LPPOINT point){if(point)*point={};return FALSE;}
static BOOL WINAPI NoCaptureRelease(){++presentationRequests;return FALSE;}
static HWND WINAPI NoCapture(HWND){++presentationRequests;return nullptr;}
static unsigned suppressedClipboardRequests=0;
static BOOL WINAPI NoClipboard(HWND){++suppressedClipboardRequests;return FALSE;}
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
#define IsClipboardFormatAvailable FixtureClipboardFormat
#define TrackPopupMenuEx FakePopup
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain ProductionEntryNotRun
#if defined(CTSPACES_CONFIG_MENU_BEFORE)
#include "../build/app-audit-round11-2026-10-15/launcher/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef OpenProcess
#undef PostMessageW


int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR,LPCWSTR,UINT flags){return flags&MB_YESNO ? IDNO : IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &){}
static UINT chosenCommand=0;
static std::function<void()> mutation;
static unsigned checks=0,failures=0,scenarios=0,popups=0;
static UINT WINAPI FakePopup(HMENU,UINT,int,int,HWND owner,LPTPMPARAMS){RequireOwned(owner);++popups;if(mutation)mutation();return chosenCommand;}
static void Check(bool value,const char *message){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';}
static unsigned shows=0,focuses=0,foregrounds=0;
static void CALLBACK Observe(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId())return;if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW)++shows;if(event==EVENT_OBJECT_FOCUS)++focuses;if(event==EVENT_SYSTEM_FOREGROUND)++foregrounds;}
static void Seed(){
 ++scenarios;g_sessions={{L"Alpha",BrowserKind::Edge,0x7ffff101},{L"Bravo",BrowserKind::Chrome,0x7ffff102}};
 g_activeProfiles.clear();g_iSelectedSessionTab=1;g_selectedBrowser=BrowserKind::Edge;
 g_bUiEnabled=true;g_bCleanupBusy=false;g_bClientNotesBusy=false;g_bCloseAllClientsPending=false;
 g_pinnedClients.clear();g_archivedClients.clear();g_clientsWithoutTabRestore.clear();SetClientInputText(L"Alpha");
 mutation={};chosenCommand=IDM_CTX_CLIENT_NOTES;
}
static void Case(UINT command,const std::function<void()> &change,UINT expected,const char *message){
 Seed();chosenCommand=command;mutation=change;const auto action=ShowConfigMenuFromButton(g_hGui);
 std::wcout<<L"Menu command "<<command<<L" returned "<<action<<L" with client "<<GetSelectedClientNameSanitized(false)<<L"\n";
 Check(action==expected,message);
}
int wmain(){std::vector<HWINEVENTHOOK> hooks;try{
 std::cout<<std::unitbuf;
 for(DWORD event:{EVENT_SYSTEM_FOREGROUND,EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS}){auto hook=SetWinEventHook(event,event,nullptr,Observe,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);if(!hook)throw std::runtime_error("Cannot audit native background events");hooks.push_back(hook);}
 g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();
 g_sDataDir=retained_fixture::CreateRoot(fs::current_path()/L"build"/L"app-audit-round11-2026-10-15"/L"launcher",L"fixture");
 if(!fs::create_directories(g_sDataDir/L"Sites"/L"Alpha"))throw std::runtime_error("Fresh private fixture path unavailable");
 fs::create_directory(g_sDataDir/L"Sites"/L"Bravo");g_sConfigPath=g_sDataDir/L"config.ini";
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_TAB_CLASSES};if(!InitCommonControlsEx(&controls))throw std::runtime_error("Cannot initialize controls");
 Gdiplus::GdiplusStartupInput graphics;Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr);
 g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden config menu fixture",WS_OVERLAPPED|WS_CLIPCHILDREN,0,0,600,234,nullptr,nullptr,g_hInst,nullptr);RequireOwned(g_hGui);
 const auto child=[&](const wchar_t *kind,DWORD style){HWND result=CreateWindowExW(WS_EX_NOACTIVATE,kind,L"",WS_CHILD|style,0,0,100,24,g_hGui,nullptr,g_hInst,nullptr);RequireOwned(result);return result;};
 g_hClientEdit=child(L"EDIT",ES_AUTOHSCROLL);g_hComboClient=child(L"COMBOBOX",CBS_DROPDOWN);g_hClientEditSurface=child(L"STATIC",0);
 g_hBtnClientIcon=child(L"BUTTON",0);g_hBtnClientDrop=child(L"BUTTON",0);g_hBtnGo=child(L"BUTTON",0);g_hBtnPin=child(L"BUTTON",0);g_hBtnRestoreTabs=child(L"BUTTON",0);g_hBtnConfig=child(L"BUTTON",0);g_hBtnTmpProf=child(L"BUTTON",0);g_hBtnCloseAll=child(L"BUTTON",0);
 LoadBakedThemesFromHeader();g_themeColors=g_aCustomThemes.front().colors;UpdateThemeBrushes();
 Case(IDM_CTX_CLIENT_NOTES,[]{},IDM_CTX_CLIENT_NOTES,"Unchanged Client Notes command remains usable");
 Case(IDM_CTX_CLIENT_NOTES,[]{OnSessionStarted(0x7ffff103,L"Bravo");},0,"New session selection during popup cannot retarget Client Notes to another client");
 Case(IDM_CTX_QUICK_NOTE,[]{OnSessionStarted(0x7ffff103,L"Bravo");},0,"New session selection during popup cannot retarget Quick note to another client");
 Case(IDM_CTX_CREATE_SHORTCUT,[]{OnSessionStarted(0x7ffff103,L"Bravo");},0,"New session selection during popup cannot retarget desktop shortcut creation");
 Case(IDM_CTX_CLIENT_NOTES,[]{OnSessionEnded(0x7ffff101);},IDM_CTX_CLIENT_NOTES,"Closing the selected session still permits its same-client notebook command");
 Case(IDM_CTX_CLIENT_NOTES,[]{g_sessions.insert(g_sessions.begin(),Session{L"Bravo",BrowserKind::Chrome,0x7ffff103});++g_iSelectedSessionTab;},IDM_CTX_CLIENT_NOTES,"Index shifts preserving the selected client do not invalidate the command");
 Case(IDM_CTX_CLIENT_NOTES,[]{SelectSessionTab(2,false);},0,"Selection change during popup cannot retarget a client command");
 Case(IDM_CTX_CREATE_SHORTCUT,[]{g_selectedBrowser=BrowserKind::Chrome;},0,"Browser change during popup cannot apply a stale client/browser shortcut action");
 Case(IDM_CTX_MINIMIZE_TRAY,[]{OnSessionStarted(0x7ffff103,L"Bravo");},IDM_CTX_MINIMIZE_TRAY,"Global minimize-to-tray preference survives a client selection change");
 Case(IDM_BROWSER_CHROME,[]{OnSessionStarted(0x7ffff103,L"Bravo");},IDM_BROWSER_CHROME,"Global browser selection survives a client selection change");
 Case(0,[]{OnSessionStarted(0x7ffff103,L"Bravo");},0,"Canceled config menu remains canceled after session mutation");
#ifndef CTSPACES_CONFIG_MENU_BEFORE
 Case(IDM_CTX_CLIENT_NOTES,[]{g_selectedBrowser=BrowserKind::Chrome;},IDM_CTX_CLIENT_NOTES,"Same-client Notes remains usable after a browser-only preference change");
 bool clientCommandsComplete=true;
 for(UINT command:{IDM_CTX_SET_PROFILE_ICON,IDM_CTX_REMOVE_ICON,IDM_CTX_CREATE_SHORTCUT,IDM_CTX_RENAME_PROFILE,IDM_CTX_CLIENT_NOTES,IDM_CTX_QUICK_NOTE,IDM_CTX_ARCHIVE_PROFILE,IDM_CTX_RESET_PROFILE,IDM_CTX_VACUUM_PROFILE,IDM_CTX_FETCH_ICON,IDM_CTX_DELETE_PROFILE})
  clientCommandsComplete&=ConfigCommandUsesSelectedClient(command)&&GetMenuState(g_hConfigMenu,command,MF_BYCOMMAND)!=static_cast<UINT>(-1);
 Check(clientCommandsComplete,"All eleven client-dependent commands in the actual config menu have selection ownership protection");
 bool globalCommandsUnchanged=true;
 for(UINT command:{IDM_CTX_CLEANUP_INACTIVE,IDM_CTX_DELETE_MULTIPLE,IDM_CTX_ARCHIVED_CLIENTS,IDM_CTX_EDIT_DEFAULT_PROFILE,IDM_CTX_THEME_COLOR,IDM_CTX_CLIENT_TITLE_FIRST,IDM_CTX_MINIMIZE_TRAY,IDM_CTX_EXPORT_ALL,IDM_CTX_RESTORE_ALL,IDM_GUIDED_WALKTHROUGH,IDM_WHATS_NEW,IDM_QUICK_TOUR,IDM_ABOUT,IDM_BROWSER_EDGE,IDM_BROWSER_CHROME,IDM_BROWSER_BRAVE,IDM_BROWSER_FIREFOX})
  globalCommandsUnchanged&=!ConfigCommandUsesSelectedClient(command);
 Check(globalCommandsUnchanged,"Global browser, settings, cleanup, archived-client, backup and help commands remain selection independent");
#endif
 Check(popups==scenarios,"Every scenario traverses the actual production config popup routine");
 Check(forbiddenPaint==0&&launchRequests==0&&closeTargets.empty(),"No foreign repaint, browser launch, tray operation or process termination reaches Windows");
 DestroyWindow(g_hGui);g_hGui=nullptr;MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if((msg.message>=WM_MOUSEFIRST&&msg.message<=WM_MOUSELAST)||(msg.message>=WM_KEYFIRST&&msg.message<=WM_KEYLAST))throw std::runtime_error("Unexpected device input");DispatchMessageW(&msg);}
 unsigned visible=0;EnumWindows([](HWND w,LPARAM p)->BOOL{DWORD pid=0;GetWindowThreadProcessId(w,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(w))++*reinterpret_cast<unsigned *>(p);return TRUE;},reinterpret_cast<LPARAM>(&visible));
 Check(!visible&&!shows&&!focuses&&!foregrounds,"Native audit: zero visible windows, show, focus and foreground events");
 for(auto h:hooks)UnhookWinEvent(h);hooks.clear();Gdiplus::GdiplusShutdown(g_gdiplusToken);
 std::cout<<"Config menu lifetime: "<<checks<<" checks, "<<scenarios<<" scenarios, "<<failures<<" failures; suppressed presentation requests="<<presentationRequests<<"; no input or real process opens.\n";return failures?1:0;
}catch(const std::exception &e){std::cerr<<"Config-menu safety/setup failure: "<<e.what()<<'\n';for(auto h:hooks)UnhookWinEvent(h);return 2;}}
