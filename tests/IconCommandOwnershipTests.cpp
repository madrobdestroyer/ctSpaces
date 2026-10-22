// Auto-fetch/removal command ownership controls. Modal input, confirmation,
// download and presentation are intercepted; files are private fixtures only.
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <winhttp.h>
static unsigned unexpectedNetworkRequests=0;
static HINTERNET WINAPI NoNetwork(LPCWSTR,DWORD,LPCWSTR,LPCWSTR,DWORD){++unexpectedNetworkRequests;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
#define WinHttpOpen NoNetwork
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
#define GuiAutoIcon IncludedAutoIconNotRun
#define GuiRemoveIcon IncludedRemoveIconNotRun
#define wWinMain ProductionEntryNotRun
#ifdef CTSPACES_ICON_COMMAND_BEFORE
#include "../build/app-audit-round10-2026-10-15/auto-icon/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef GuiAutoIcon
#undef GuiRemoveIcon
#undef wWinMain
#undef OpenProcess
#undef PostMessageW

// Execute exact copied production command bodies. Only the modal input and
// downloader boundaries are replaced; validation/conversion/commit/removal are
// the actual production functions. WinHTTP is unconditionally disabled above.
static unsigned checks=0,failures=0,notices=0,inputCalls=0,downloadCalls=0,scenarios=0;
static fs::path fixture,target,foreign,displaced,png;
enum class SwapAt{None,Input,Download,Confirmation};
static SwapAt swapAt=SwapAt::None;
static bool attempted=false,substituted=false,cancelInput=false,failDownload=false;
static int confirmation=IDYES;
static std::wstring domain=L"https://Microsoft.com/path?q=x",notice;
static void Swap(){
 attempted=true;std::error_code error;fs::rename(target,displaced,error);
 if(!error)fs::rename(foreign,target,error);
 substituted=!error;
}
struct PrivateInput{
 static std::optional<std::wstring> Show(HWND,const std::wstring &,const std::wstring &){
  ++inputCalls;if(swapAt==SwapAt::Input)Swap();
  if(cancelInput)return std::nullopt;return domain;
 }
};
static bool PrivateDownload(const std::wstring &url,const fs::path &destination,std::wstring &error){
 ++downloadCalls;
 if(url!=L"https://www.google.com/s2/favicons?domain=microsoft.com&sz=64")throw std::runtime_error("Unexpected synthetic download URL");
 if(swapAt==SwapAt::Download)Swap();
 if(failDownload){error=L"Synthetic network unavailable";return false;}
 fs::copy_file(png,destination,fs::copy_options::overwrite_existing);return true;
}
#define InputBoxWindow PrivateInput
#define DownloadUrlToFile PrivateDownload
#ifdef CTSPACES_ICON_COMMAND_BEFORE
#include "../build/app-audit-round10-2026-10-15/auto-icon/commands-before.inl"
#else
#include "../build/tests/IconCommandOwnership/generated-commands.inl"
#endif
#undef InputBoxWindow
#undef DownloadUrlToFile

int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR text,LPCWSTR,UINT flags){
 ++notices;notice=text?text:L"";
 if((flags&MB_YESNO)==MB_YESNO){if(swapAt==SwapAt::Confirmation)Swap();return confirmation;}
 return IDOK;
}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path &,std::wstring &){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path &){}
static void Check(bool value,const char *message){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<message<<'\n';}
static void Put(const fs::path &path,const std::string &bytes){std::ofstream out(path,std::ios::binary);out.write(bytes.data(),bytes.size());if(!out)throw std::runtime_error("Private fixture write failed");}
static std::string Read(const fs::path &path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("Private fixture read failed");return {std::istreambuf_iterator<char>(in),{}};}
static void Seed(){
 const auto root=fixture/std::to_wstring(++scenarios);fs::create_directory(root);g_sDataDir=root;
 target=root/L"Sites"/L"Alpha";foreign=root/L"Sites"/L"Bravo";displaced=root/L"OriginalAlpha";
 fs::create_directories(target);fs::create_directory(foreign);
 Put(target/L"notes.txt","ALPHA NOTES");Put(foreign/L"notes.txt","BRAVO NOTES");
 fs::copy_file(L"icons/color_b.ico",target/L"client.ico");fs::copy_file(L"icons/color_b.ico",foreign/L"client.ico");
 g_sessions={{L"Alpha",BrowserKind::Edge,0x7ffff271}};g_iSelectedSessionTab=1;
 g_activeProfiles.clear();g_bUiEnabled=true;g_bDefaultProfileUiBusy=false;
 g_isLaunchInFlight=false;g_bCleanupBusy=false;g_bClientNotesBusy=false;g_bCloseAllClientsPending=false;
 swapAt=SwapAt::None;attempted=false;substituted=false;cancelInput=false;failDownload=false;confirmation=IDYES;domain=L"https://Microsoft.com/path?q=x";notice.clear();
}
static void PreservedNotes(){
 Check(Read((substituted?displaced:target)/L"notes.txt")=="ALPHA NOTES"&&Read((substituted?target:foreign)/L"notes.txt")=="BRAVO NOTES","Exact original and independent client notes are preserved");
 Check(g_bUiEnabled,"Command restores UI availability");
}
int wmain(){try{
 std::cout<<std::unitbuf;
 Gdiplus::GdiplusStartupInput graphics;
 if(Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Private GDI+ initialization failed");
 struct GraphicsScope{~GraphicsScope(){Gdiplus::GdiplusShutdown(g_gdiplusToken);g_gdiplusToken=0;}} graphicsScope;
 fixture=fs::current_path()/L"build/app-audit-round10-2026-10-15/auto-icon"/(L"private-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
 if(!fs::create_directory(fixture))throw std::runtime_error("Fresh private fixture unavailable");
 png=fixture/L"synthetic-response.png";
 Gdiplus::Bitmap image(16,16,PixelFormat32bppARGB);Gdiplus::Graphics draw(&image);draw.Clear(Gdiplus::Color(255,31,127,223));
 const auto encoder=GetEncoderClsid(L"image/png");if(image.Save(png.c_str(),&encoder,nullptr)!=Gdiplus::Ok)throw std::runtime_error("Private PNG creation failed");
 const auto original=Read(L"icons/color_b.ico");
 Seed();GuiAutoIcon();
 Check(Read(target/L"client.ico")!=original,"Normal auto-fetch publishes a converted synthetic image to the selected client");
 Check(Read(foreign/L"client.ico")==original,"Normal auto-fetch preserves independent client icon");PreservedNotes();
 Seed();cancelInput=true;const auto downloadsBeforeCancel=downloadCalls;GuiAutoIcon();
 Check(downloadCalls==downloadsBeforeCancel&&Read(target/L"client.ico")==original,"Cancelled input performs no download and preserves original icon");PreservedNotes();
 Seed();domain=L"https://bad domain.invalid";const auto downloadsBeforeInvalid=downloadCalls;GuiAutoIcon();
 Check(downloadCalls==downloadsBeforeInvalid&&Read(target/L"client.ico")==original,"Invalid domain performs no download and preserves original icon");PreservedNotes();
 Seed();failDownload=true;GuiAutoIcon();Check(Read(target/L"client.ico")==original&&g_bUiEnabled,"Download failure preserves original icon and restores UI");
 failDownload=false;GuiAutoIcon();Check(Read(target/L"client.ico")!=original&&g_bUiEnabled,"Retry after synthetic download failure succeeds");PreservedNotes();
 Seed();HANDLE lock=CreateFileW((target/L"client.ico").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(lock==INVALID_HANDLE_VALUE)throw std::runtime_error("Private destination lock failed");GuiAutoIcon();CloseHandle(lock);
 Check(Read(target/L"client.ico")==original&&g_bUiEnabled,"Locked destination preserves original icon and restores UI");GuiAutoIcon();
 Check(Read(target/L"client.ico")!=original&&g_bUiEnabled,"Unlocked retry publishes a valid auto-fetched icon");PreservedNotes();
 for(auto boundary:{SwapAt::Input,SwapAt::Download}){
  Seed();swapAt=boundary;GuiAutoIcon();const auto other=substituted?target:foreign;
  std::cout<<"AUTO_ICON_SWAP boundary="<<(boundary==SwapAt::Input?"input":"download")<<" attempted="<<attempted<<" substituted="<<substituted<<" foreign_icon_preserved="<<(Read(other/L"client.ico")==original)<<'\n';
  Check(attempted,"Auto-fetch fixture attempts namespace substitution at the designated callback");
  Check(Read(other/L"client.ico")==original,"Auto-fetch never writes a requested icon to a replacement client directory");PreservedNotes();
 }
 Seed();confirmation=IDNO;GuiRemoveIcon();Check(Read(target/L"client.ico")==original,"Cancelled removal preserves exact original icon");PreservedNotes();
 Seed();GuiRemoveIcon();Check(!fs::exists(target/L"client.ico")&&Read(foreign/L"client.ico")==original,"Confirmed normal removal deletes only selected custom icon");PreservedNotes();
 Seed();swapAt=SwapAt::Confirmation;GuiRemoveIcon();const auto other=substituted?target:foreign;
 std::cout<<"REMOVE_ICON_SWAP attempted="<<attempted<<" substituted="<<substituted<<" foreign_icon_exists="<<fs::exists(other/L"client.ico")<<'\n';
 Check(attempted,"Removal fixture attempts namespace substitution inside confirmation");
 Check(fs::is_regular_file(other/L"client.ico")&&Read(other/L"client.ico")==original,"Confirmation never authorizes deletion from a replacement client directory");PreservedNotes();
 Check(!g_hGui&&!g_hClientEdit&&!g_hComboClient,"Icon command fixture creates no HWND and never invokes the app entrypoint");
 Check(presentationRequests==0&&launchRequests==0&&forbiddenPaint==0&&closeTargets.empty()&&unexpectedNetworkRequests==0,"Icon command fixture permits no presentation, network sessions, launches, foreign repaint or process closure");
 std::cout<<"Icon command ownership: "<<checks<<" checks, "<<scenarios<<" scenarios, "<<failures<<" failures; HWNDs=0; actual input/network/foreground/launches=0; suppressed paints="<<queuedPaintRequests<<".\n";
 return failures?1:0;
}catch(const std::exception &error){std::cerr<<"Private fixture error: "<<error.what()<<'\n';return 2;}}

