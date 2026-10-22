// Manual icon command consent/ownership controls. The picker and presentation
// are intercepted; all files belong to disposable private fixture directories.
#include <windows.h>
#include "RetainedFixture.h"
#include <shellapi.h>
#include <commdlg.h>
static BOOL WINAPI PrivatePick(OPENFILENAMEW *);
#define GetOpenFileNameW PrivatePick
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
static unsigned presentationRequests=0,launchRequests=0,forbiddenPaint=0,queuedPaintRequests=0;
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
static BOOL WINAPI SafeInvalidate(HWND,const RECT *,BOOL){++queuedPaintRequests;return FALSE;}
static BOOL WINAPI SafeRedraw(HWND w,const RECT *r,HRGN region,UINT flags){RequireOwned(w);return FALSE;}
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
#ifdef CTSPACES_MANUAL_ICON_BEFORE
#include "../build/app-audit-round9-2026-10-15/root/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef OpenProcess
#undef PostMessageW

static unsigned checks=0,failures=0,notices=0,pickerCalls=0;
static fs::path fixture,target,foreign,displaced,selected;
static bool replaceDuringPicker=false,attempted=false,substituted=false,cancelPicker=false;
static std::wstring notice;
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR text,LPCWSTR,UINT){++notices;notice=text?text:L"";return IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &){}
static void Check(bool value,const char *message){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';}
static void Put(const fs::path &path,const std::string &bytes){std::ofstream out(path,std::ios::binary);out.write(bytes.data(),bytes.size());if(!out)throw std::runtime_error("Private fixture write failed");}
static std::string Read(const fs::path &path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Private fixture read failed");return {std::istreambuf_iterator<char>(in),{}};}
static BOOL WINAPI PrivatePick(OPENFILENAMEW *picker){
 ++pickerCalls;
 if(replaceDuringPicker){
  attempted=true;std::error_code error;fs::rename(target,displaced,error);
  if(!error)fs::rename(foreign,target,error);
  substituted=!error;
 }
 if(cancelPicker)return FALSE;
 if(!picker||selected.wstring().size()+1>picker->nMaxFile)throw std::runtime_error("Private picker buffer invalid");
 wcscpy_s(picker->lpstrFile,picker->nMaxFile,selected.c_str());return TRUE;
}
static void Seed(unsigned number){
 const auto root=fixture/std::to_wstring(number);fs::create_directory(root);g_sDataDir=root;
 target=root/L"Sites"/L"Alpha";foreign=root/L"Sites"/L"Bravo";displaced=root/L"OriginalAlpha";
 fs::create_directories(target);fs::create_directory(foreign);
 Put(target/L"notes.txt","ALPHA NOTES");Put(foreign/L"notes.txt","BRAVO NOTES");
 fs::copy_file(L"icons/color_b.ico",target/L"client.ico");fs::copy_file(L"icons/color_b.ico",foreign/L"client.ico");
 selected=root/L"selected.ico";fs::copy_file(L"icons/delete_b.ico",selected);
 g_sessions={{L"Alpha",BrowserKind::Edge,0x7ffff261}};g_iSelectedSessionTab=1;
 g_activeProfiles.clear();g_bUiEnabled=true;g_bDefaultProfileUiBusy=false;
 g_isLaunchInFlight=false;g_bCleanupBusy=false;g_bClientNotesBusy=false;g_bCloseAllClientsPending=false;
 replaceDuringPicker=false;attempted=false;substituted=false;cancelPicker=false;notice.clear();
}
int wmain(){try{
 std::cout<<std::unitbuf;
 Gdiplus::GdiplusStartupInput graphics;
 if(Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Private GDI+ initialization failed");
 struct GraphicsScope{~GraphicsScope(){Gdiplus::GdiplusShutdown(g_gdiplusToken);g_gdiplusToken=0;}} graphicsScope;
 fixture=retained_fixture::CreateRoot(fs::current_path()/L"build/app-audit-round9-2026-10-15/root",L"manual-icon");
 const auto original=Read(L"icons/color_b.ico"),requested=Read(L"icons/delete_b.ico");
 Seed(1);GuiSetIcon();
 Check(Read(target/L"client.ico")==requested,"Normal manual selection publishes the requested valid icon to Alpha");
 Check(Read(foreign/L"client.ico")==original&&Read(target/L"notes.txt")=="ALPHA NOTES"&&Read(foreign/L"notes.txt")=="BRAVO NOTES","Normal selection preserves independent client icon and both notes");
 Check(g_bUiEnabled,"Normal manual selection restores UI availability");
 Seed(2);cancelPicker=true;GuiSetIcon();
 Check(Read(target/L"client.ico")==original&&Read(foreign/L"client.ico")==original&&g_bUiEnabled,"Cancelled picker preserves exact icons and restores UI availability");
 Seed(3);replaceDuringPicker=true;GuiSetIcon();
 Check(attempted,"Fixture attempts client substitution inside the intercepted file picker");
 const auto other=substituted?target:foreign;
 std::cout<<"MANUAL_ICON_SUBSTITUTION attempted="<<attempted<<" substituted="<<substituted<<" foreign_icon_preserved="<<(Read(other/L"client.ico")==original)<<'\n';
 Check(Read(other/L"client.ico")==original,"Client substituted while picker is open never receives the original client's requested icon");
 Check(Read(other/L"notes.txt")=="BRAVO NOTES"&&Read((substituted?displaced:target)/L"notes.txt")=="ALPHA NOTES","Picker substitution preserves exact original and independent notes bytes");
 Check(Read((substituted?displaced:target)/L"client.ico")== (substituted?original:requested),"Known-owned normal commit or rejected replacement preserves the appropriate original client icon");
 Check(g_bUiEnabled,"Picker substitution rejection or protected commit restores UI availability");
 Seed(4);HANDLE lock=CreateFileW((target/L"client.ico").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Private icon lock failed");GuiSetIcon();CloseHandle(lock);
 Check(Read(target/L"client.ico")==original&&g_bUiEnabled,"Locked destination preserves exact prior icon and restores UI availability");
 GuiSetIcon();Check(Read(target/L"client.ico")==requested&&g_bUiEnabled,"Unlock and retry publishes exact requested icon");
 Check(pickerCalls==5,"All five picker outcomes use only the synthetic picker boundary");
 Check(!g_hGui&&!g_hClientEdit&&!g_hComboClient,"Manual icon fixture creates no HWND and never invokes the app entrypoint");
 Check(presentationRequests==0&&launchRequests==0&&forbiddenPaint==0&&closeTargets.empty(),"Manual icon fixture permits no presentation, launches, foreign repaint or process closure");
 std::cout<<"Manual icon consent: "<<checks<<" checks, 4 scenarios, "<<failures<<" failures; HWNDs=0; actual picker/foreground/launches=0.\n";return failures?1:0;
}catch(const std::exception &error){std::cerr<<"Private fixture error: "<<error.what()<<'\n';return 2;}}

