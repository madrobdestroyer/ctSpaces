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
#ifdef CTSPACES_ICON_FAILURE_BEFORE
#include "../build/app-audit-round8-2026-10-15/icons/ctSpaces-before.cpp"
#elif defined(CTSPACES_ICON_FAILURE_AFTER)
#include "../build/app-audit-round8-2026-10-15/icons/ctSpaces-after.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef OpenProcess
#undef PostMessageW

static unsigned notices=0,checks=0,failures=0,errors=0;
static int confirmation=IDYES;
static std::wstring lastNotice;
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR message,LPCWSTR,UINT flags){
  ++notices;lastNotice=message?message:L"";
  if(flags&MB_ICONERROR)++errors;
  return (flags&MB_YESNO)==MB_YESNO?confirmation:IDOK;
}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &){}
static void Check(bool condition,const char*message){++checks;if(!condition)++failures;std::cout<<(condition?"PASS ":"FAIL ")<<message<<'\n';}
static std::string Bytes(const fs::path&path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Private fixture read failed");return {std::istreambuf_iterator<char>(in),{}};}
static void Put(const fs::path&path,const std::string&bytes){std::ofstream out(path,std::ios::binary);out.write(bytes.data(),bytes.size());if(!out)throw std::runtime_error("Private fixture write failed");}
static fs::path fixture;
static fs::path Client(const std::wstring&name){
  const auto root=fixture/L"Sites"/name;fs::create_directories(root);
  Put(root/L"profile-sentinel.bin","original client profile data\0unchanged");
  g_sessions={{name,BrowserKind::Edge,0x7ffff351}};g_iSelectedSessionTab=1;g_bUiEnabled=true;
  return root;
}
static bool RemoveWithoutEscape(){try{GuiRemoveIcon();return true;}catch(const std::exception&e){std::cout<<"Escaped removal exception: "<<e.what()<<'\n';return false;}catch(...){std::cout<<"Escaped unknown removal exception\n";return false;}}
static void RemovalFailureCases(){
  for(bool unsafeIcon:{true,false}){
    const auto root=Client(unsafeIcon?L"Unsafe icon":L"Unsafe auxiliary image");
    const auto unsafe=root/(unsafeIcon?L"client.ico":L"temp_icon.png");
    fs::create_directory(unsafe);Put(unsafe/L"sentinel.bin","unsafe path must remain unchanged");
    if(!unsafeIcon)fs::copy_file(L"icons/color_b.ico",root/L"client.ico");
    const auto original=unsafeIcon?std::string():Bytes(root/L"client.ico");
    const unsigned priorErrors=errors;
    Check(RemoveWithoutEscape(),"Unsafe icon/auxiliary path rejection stays inside the actual removal handler");
    Check(g_bUiEnabled,"Unsafe icon/auxiliary path rejection restores main UI availability");
    Check(errors==priorErrors+1,"Unsafe icon/auxiliary path rejection reports a diagnostic error");
    Check(fs::is_directory(unsafe)&&Bytes(unsafe/L"sentinel.bin")=="unsafe path must remain unchanged","Rejected unsafe path retains its exact private sentinel bytes");
    Check(unsafeIcon||(fs::is_regular_file(root/L"client.ico")&&Bytes(root/L"client.ico")==original),"Unsafe auxiliary path is rejected before deleting the original custom icon");
    Check(Bytes(root/L"profile-sentinel.bin")=="original client profile data","Rejected removal preserves unrelated client profile bytes");
  }
  for(bool readonly:{true,false}){
    const auto root=Client(readonly?L"Read only icon":L"Locked icon");const auto icon=root/L"client.ico";
    fs::copy_file(L"icons/color_b.ico",icon);const auto original=Bytes(icon);
    HANDLE lock=INVALID_HANDLE_VALUE;
    if(readonly){if(!SetFileAttributesW(icon.c_str(),FILE_ATTRIBUTE_READONLY))throw std::runtime_error("Read-only fixture setup failed");}
    else {lock=CreateFileW(icon.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Icon lock setup failed");}
    const unsigned priorErrors=errors;
    Check(RemoveWithoutEscape(),"Actual Windows read-only/locked file deletion remains contained");
    if(readonly){
      Check(g_bUiEnabled&&errors==priorErrors&&!fs::exists(icon),"Confirmed removal may remove a read-only private icon and restores UI without a spurious error");
    }else{
      Check(g_bUiEnabled&&errors==priorErrors+1,"Actual locked deletion restores UI and reports the Windows failure");
      Check(fs::exists(icon)&&Bytes(icon)==original,"Actual locked deletion preserves original custom icon bytes");
    }
    if(lock!=INVALID_HANDLE_VALUE)CloseHandle(lock);
    Check(RemoveWithoutEscape()&&g_bUiEnabled&&!fs::exists(icon),"No-icon repeat or retry after releasing the private lock completes successfully");
  }
  {
    const auto root=Client(L"Locked auxiliary removal");const auto icon=root/L"client.ico";const auto auxiliary=root/L"temp_icon.png";
    fs::copy_file(L"icons/color_b.ico",icon);Put(auxiliary,"private locked download");const auto original=Bytes(icon);
    HICON cached=LoadIconFromIcoBestDownscale(icon,32);if(!cached)throw std::runtime_error("Locked auxiliary cache fixture failed");g_iconCache[L"Locked auxiliary removal"].byPx[32]=cached;
    HANDLE lock=CreateFileW(auxiliary.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Auxiliary lock setup failed");const unsigned priorErrors=errors;
    Check(RemoveWithoutEscape()&&g_bUiEnabled&&errors==priorErrors+1,"Locked auxiliary download deletion is contained and reports failure with restored UI");
    Check(fs::is_regular_file(icon)&&Bytes(icon)==original,"Auxiliary deletion failure leaves the original custom icon and its cache source intact");
    Check(Bytes(auxiliary)=="private locked download","Auxiliary deletion failure retains exact locked download bytes");CloseHandle(lock);
    Check(RemoveWithoutEscape()&&!fs::exists(icon)&&!fs::exists(auxiliary)&&!g_iconCache.contains(L"Locked auxiliary removal"),"Unlocked auxiliary deletion retry clears files and cached custom icon together");
  }
  {
    const auto root=Client(L"Cancelled removal");const auto icon=root/L"client.ico";fs::copy_file(L"icons/color_b.ico",icon);const auto original=Bytes(icon);
    confirmation=IDNO;const unsigned priorErrors=errors;
    Check(RemoveWithoutEscape()&&g_bUiEnabled&&Bytes(icon)==original&&errors==priorErrors,"Cancelling actual removal retains icon bytes and restores UI without an error");confirmation=IDYES;
  }
  {
    const auto root=Client(L"Normal removal");fs::copy_file(L"icons/color_b.ico",root/L"client.ico");Put(root/L"temp_icon.png","auxiliary download");
    HICON cached=LoadIconFromIcoBestDownscale(root/L"client.ico",32);if(!cached)throw std::runtime_error("Valid icon cache fixture failed to load");
    g_iconCache[L"Normal removal"].byPx[32]=cached;
    Check(RemoveWithoutEscape()&&g_bUiEnabled&&!fs::exists(root/L"client.ico")&&!fs::exists(root/L"temp_icon.png"),"Successful actual removal deletes only custom icon and auxiliary download");
    Check(!g_iconCache.contains(L"Normal removal")&&!g_retiredIconHandles.contains(L"Normal removal"),"Successful removal invalidates its cached/retired icon handles when watcher is quiescent");
    Check(Bytes(root/L"profile-sentinel.bin")=="original client profile data","Successful icon removal preserves unrelated client profile bytes");
  }
}
static void ReplacementFailureCases(){
  const auto root=Client(L"Private replacement");const auto icon=root/L"client.ico";
  fs::copy_file(L"icons/color_b.ico",icon);const auto original=Bytes(icon);std::wstring error;
  std::vector<std::string> malformed={"not an icon",std::string("\0\0\1\0\1\0",6),Bytes(L"icons/delete_b.ico").substr(0,28)};
  auto invalidHeader=Bytes(L"icons/delete_b.ico");invalidHeader[2]=2;malformed.push_back(invalidHeader);
  unsigned index=0;
  for(const auto&bytes:malformed){const auto stage=root/(L"malformed-"+std::to_wstring(index++)+L".ico");Put(stage,bytes);error.clear();
    Check(!CommitPreparedClientIcon(stage,icon,error)&&!error.empty(),"Production icon commit rejects malformed/truncated/incorrect-type ICO with a diagnostic");
    Check(Bytes(icon)==original&&Bytes(stage)==bytes,"Malformed icon rejection preserves exact old destination and staging bytes");
  }
  const auto stage=root/L"valid-staging.ico";fs::copy_file(L"icons/delete_b.ico",stage);const auto requested=Bytes(stage);
  HANDLE lock=CreateFileW(icon.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Replace lock setup failed");error.clear();
  Check(!CommitPreparedClientIcon(stage,icon,error)&&error.find(L"replace")!=std::wstring::npos,"Production icon commit reports actual Windows locked-destination replacement failure");
  Check(Bytes(icon)==original&&Bytes(stage)==requested,"Locked replacement preserves original destination and complete prepared icon bytes");CloseHandle(lock);error.clear();
  Check(CommitPreparedClientIcon(stage,icon,error)&&Bytes(icon)==requested&&!fs::exists(stage),"Unlocked retry publishes exact complete valid icon and consumes the staged file");
  HICON loaded=LoadIconFromIcoBestDownscale(icon,32);Check(loaded!=nullptr,"Published replacement loads through the production native Windows icon path");if(loaded)DestroyIcon(loaded);
  const auto unsafe=root/L"directory.ico";fs::create_directory(unsafe);Put(unsafe/L"sentinel","unchanged");fs::copy_file(L"icons/color_b.ico",stage);error.clear();
  Check(!CommitPreparedClientIcon(stage,unsafe,error)&&Bytes(unsafe/L"sentinel")=="unchanged"&&fs::is_regular_file(stage),"Unsafe replacement target is rejected without touching its private directory or valid staged icon");
}
int wmain(){try{
  fixture=fs::current_path()/L"build"/L"app-audit-round8-2026-10-15"/L"icons"/L"fixtures"/(L"private-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  if(!fs::create_directories(fixture))throw std::runtime_error("Fresh fixture root unavailable");g_sDataDir=fixture;g_selectedBrowser=BrowserKind::Edge;
  RemovalFailureCases();ReplacementFailureCases();
  Check(!g_hGui&&!g_hClientEdit&&!g_hComboClient,"Private fixture creates no HWNDs and never invokes the app entrypoint");
  Check(presentationRequests==0&&launchRequests==0&&forbiddenPaint==0&&closeTargets.empty(),"No presentation, external launch, foreign repaint, or process-close action reaches Windows");
  std::cout<<checks<<" client icon failure checks; "<<failures<<" failures; suppressed paint requests="<<queuedPaintRequests<<'\n';return failures?1:0;
}catch(const std::exception&e){std::cerr<<"Fixture setup failure: "<<e.what()<<'\n';return 2;}}

