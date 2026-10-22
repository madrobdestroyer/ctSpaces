// Actual launcher commands and managed-profile launch paths. Native popup,
// presentation, clipboard and process boundaries are inert; only fresh private
// fixture files below build/qa-tlui are writable. Never run the app entrypoint.
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include "RetainedFixture.h"
#include "../ProgressUI.h"

static unsigned tabLayoutPresentationRequests=0,external=0,clipboard=0,foreignPaint=0;
static unsigned processRequests=0,processOraclePasses=0,processSnapshots=0;
static bool indeterminateProcesses=false;
static std::function<bool()> processOracle;
static std::function<void()> processProbeMutation;
static unsigned mutateAtSnapshot=0;
static BOOL WINAPI NoShow(HWND,int){++tabLayoutPresentationRequests;return FALSE;}
static HWND WINAPI NoFocus(HWND){++tabLayoutPresentationRequests;return nullptr;}
static BOOL WINAPI NoForeground(HWND){++tabLayoutPresentationRequests;return FALSE;}
static HWND WINAPI NoActive(HWND){++tabLayoutPresentationRequests;return nullptr;}
static BOOL WINAPI NoRaise(HWND){++tabLayoutPresentationRequests;return FALSE;}
static void Owned(HWND window){DWORD pid=0;if(!window||!IsWindow(window)||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId()){++foreignPaint;throw std::runtime_error("Foreign or invalid UI target refused");}}
static BOOL WINAPI SafePosition(HWND window,HWND after,int x,int y,int width,int height,UINT flags){if((flags&SWP_SHOWWINDOW)||!(flags&SWP_NOACTIVATE)){++tabLayoutPresentationRequests;return FALSE;}Owned(window);return ::SetWindowPos(window,after,x,y,width,height,flags);}
static BOOL WINAPI SafeInvalidate(HWND window,const RECT*rect,BOOL erase){Owned(window);return ::InvalidateRect(window,rect,erase);}
static BOOL WINAPI SafeRedraw(HWND window,const RECT*rect,HRGN region,UINT flags){Owned(window);return ::RedrawWindow(window,rect,region,flags);}
static BOOL WINAPI SafePost(HWND window,UINT,WPARAM,LPARAM){Owned(window);return TRUE;}
static BOOL WINAPI NoClipboard(HWND){++clipboard;return FALSE;}
static BOOL WINAPI ClipboardUnavailable(UINT){return FALSE;}
static BOOL WINAPI NoCursor(LPPOINT p){if(p)*p={};return FALSE;}
static HWND WINAPI NoCapture(HWND){++tabLayoutPresentationRequests;return nullptr;}
static BOOL WINAPI NoRelease(){++tabLayoutPresentationRequests;return FALSE;}
static BOOL WINAPI NoTray(DWORD,PNOTIFYICONDATAW){++external;return FALSE;}
// Whole-client deletion also checks managed desktop shortcuts. Refuse known
// folder discovery so this private fixture cannot even inspect a real desktop.
static HRESULT WINAPI NoKnownFolder(REFKNOWNFOLDERID,DWORD,HANDLE,PWSTR*path){if(path)*path=nullptr;return E_ACCESSDENIED;}
static BOOL WINAPI NoBeep(UINT){return FALSE;}
static HANDLE WINAPI NoOpenProcess(DWORD,BOOL,DWORD){++external;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
static BOOL WINAPI NoTerminate(HANDLE,UINT){++external;return FALSE;}
static HINSTANCE WINAPI NoShell(HWND,LPCWSTR,LPCWSTR,LPCWSTR,LPCWSTR,INT){++external;return reinterpret_cast<HINSTANCE>(ERROR_ACCESS_DENIED);}
static BOOL WINAPI NoShellEx(SHELLEXECUTEINFOW*){++external;return FALSE;}
static BOOL WINAPI InterceptProcess(LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION){
 ++processRequests;if(processOracle&&processOracle())++processOraclePasses;
 SetLastError(ERROR_ACCESS_DENIED);return FALSE;
}
static HANDLE WINAPI FixtureSnapshot(DWORD,DWORD){++processSnapshots;if(processProbeMutation&&processSnapshots==mutateAtSnapshot){auto mutation=std::move(processProbeMutation);processProbeMutation={};mutation();}if(indeterminateProcesses){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}return CreateEventW(nullptr,FALSE,FALSE,nullptr);}
static BOOL WINAPI FixtureFirst(HANDLE,LPPROCESSENTRY32W entry){wcscpy_s(entry->szExeFile,L"private-owned-fixture.exe");entry->th32ProcessID=GetCurrentProcessId();return TRUE;}
static BOOL WINAPI FixtureNext(HANDLE,LPPROCESSENTRY32W){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
static void NoProgressShow(HWND,const std::wstring&,int){}
static void NoProgressHide(HWND){}
static void NoProgressUpdate(HWND,unsigned int){}
static UINT WINAPI FixturePopup(HMENU,UINT,int,int,HWND,LPTPMPARAMS);
#define ShowWindow NoShow
#define SetFocus NoFocus
#define SetForegroundWindow NoForeground
#define SetActiveWindow NoActive
#define BringWindowToTop NoRaise
#define SetWindowPos SafePosition
#define InvalidateRect SafeInvalidate
#define RedrawWindow SafeRedraw
#define PostMessageW SafePost
#define OpenClipboard NoClipboard
#define IsClipboardFormatAvailable ClipboardUnavailable
#define GetCursorPos NoCursor
#define SetCapture NoCapture
#define ReleaseCapture NoRelease
#define Shell_NotifyIconW NoTray
#define SHGetKnownFolderPath NoKnownFolder
#define MessageBeep NoBeep
#define OpenProcess NoOpenProcess
#define TerminateProcess NoTerminate
#define ShellExecuteW NoShell
#define ShellExecuteExW NoShellEx
#define CreateProcessW InterceptProcess
#define CreateToolhelp32Snapshot FixtureSnapshot
#define Process32FirstW FixtureFirst
#define Process32NextW FixtureNext
#define ProgressUI_PostShow NoProgressShow
#define ProgressUI_PostHide NoProgressHide
#define ProgressUI_PostProgress NoProgressUpdate
#define TrackPopupMenuEx FixturePopup
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain ProductionEntrypointNotRun
#include "../ctSpaces.cpp"
#undef wWinMain

static unsigned checks=0,failures=0,scenarios=0,popups=0,notices=0;
static unsigned actualShows=0,actualFocus=0,actualForeground=0;
static UINT popupCommand=0;
static std::function<void(HMENU)> popupInspect;
static std::function<void()> popupMutation;
static fs::path fixture;
static unsigned serial=0;
static constexpr const char* firefoxBase="// PRIVATE owned Firefox settings\r\nuser_pref(\"browser.startup.page\", 1);\r\nuser_pref(\"custom.opaque\", \"do not touch\");\r\n";
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR text,LPCWSTR title,UINT){++notices;std::wcout<<L"INTERCEPTED_NOTICE "<<(title?title:L"")<<L": "<<(text?text:L"")<<L'\n';return IDNO;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path&,std::wstring&){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path&){}
static UINT WINAPI FixturePopup(HMENU menu,UINT,int,int,HWND owner,LPTPMPARAMS){Owned(owner);++popups;if(popupInspect)popupInspect(menu);if(popupMutation)popupMutation();return popupCommand;}
static void Check(bool value,const char*label){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';}
static void Require(bool value,const char*label){if(!value)throw std::runtime_error(label);}
static void Put(const fs::path&path,const std::string&bytes){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));Require(static_cast<bool>(out),"Private fixture write failed");}
static std::string Read(const fs::path&path){std::ifstream in(path,std::ios::binary);Require(static_cast<bool>(in),"Private fixture read failed");return {std::istreambuf_iterator<char>(in),{}};}
using Tree=std::map<fs::path,std::string>;
static Tree Snapshot(const fs::path&root){Tree result;for(const auto&item:fs::recursive_directory_iterator(root))if(item.is_regular_file())result.emplace(fs::relative(item.path(),root),Read(item.path()));return result;}
static fs::path Client(const wchar_t*name=L"Alpha"){return g_sDataDir/L"Sites"/name;}
static fs::path Profile(BrowserKind browser,const wchar_t*client=L"Alpha"){return Client(client)/L"Browsers"/GetBrowserId(browser)/L"Profile";}
static std::string Json(bool edge=false,bool chrome=false,bool brave=false){return std::string("{\r\n\"edge\":{\"vertical_tabs\":{\"opened\":")+(edge?"true":"false")+",\"width\":230}},\"vertical_tabs\":{\"enabled\":"+(chrome?"true":"false")+"},\"brave\":{\"tabs\":{\"vertical_tabs_enabled\":"+(brave?"true":"false")+",\"opaque\":7}},\"session\":{\"restore_on_startup\":5,\"startup_urls\":[]},\"profile\":{\"exit_type\":\"Normal\",\"exited_cleanly\":true},\"unicode\":\"\\u65e5\\ud83d\\ude80\"}\r\n";}
static std::string FirefoxExpected(bool vertical){return std::string(firefoxBase)+"// ctSpaces tab-layout begin v1; separator=none\r\n"+(vertical?"user_pref(\"sidebar.revamp\", true);\r\n":"")+"user_pref(\"sidebar.verticalTabs\", "+(vertical?"true":"false")+");\r\n// ctSpaces tab-layout end v1\r\n";}
static fs::path NativeFile(BrowserKind browser){return browser==BrowserKind::Firefox?Profile(browser)/L"user.js":Profile(browser)/L"Default"/L"Preferences";}
static std::string Expected(BrowserKind browser,bool vertical){return browser==BrowserKind::Firefox?FirefoxExpected(vertical):Json(browser==BrowserKind::Edge&&vertical,browser==BrowserKind::Chrome&&vertical,browser==BrowserKind::Brave&&vertical);}
static bool Radio(HMENU menu,UINT first,UINT last,UINT selected){if(!menu||GetMenuItemCount(menu)!=3)return false;for(UINT id=first;id<=last;++id){const UINT state=GetMenuState(menu,id,MF_BYCOMMAND);if(state==static_cast<UINT>(-1)||(state&MF_GRAYED)||static_cast<bool>(state&MF_CHECKED)!=(id==selected))return false;}return true;}
static HMENU Submenu(HMENU menu,UINT command){MENUITEMINFOW item{sizeof(item)};item.fMask=MIIM_SUBMENU;return GetMenuItemInfoW(menu,command,FALSE,&item)?item.hSubMenu:nullptr;}
static void Seed(){
 ++scenarios;++serial;g_sDataDir=fixture/std::to_wstring(serial);g_sConfigPath=g_sDataDir/L"config.ini";
 Put(g_sConfigPath,"[user]\r\ntab_layout=browser\r\nkeep_me=opaque\r\n");
 for(const wchar_t*name:{L"Alpha",L"Beta"}){
  const auto root=Client(name);Put(root/kClientSchemaMarkerName,kClientSchemaMarkerText);
  Put(root/client_notes::kFileName,std::string("PLAIN NOTES ")+(name[0]=='A'?"Alpha":"Beta"));
  Put(root/client_notes::kRichFileName,"{\\rtf1 PRIVATE rich note}");Put(root/client_notes::kNotebookFileName,"PRIVATE notebook bytes; never a browser preference");
  for(BrowserKind browser:{BrowserKind::Edge,BrowserKind::Chrome,BrowserKind::Brave,BrowserKind::Firefox}){
   const auto profile=Profile(browser,name);Put(profile.parent_path()/kBrowserSchemaMarkerName,GetBrowserMarkerText(browser));
   if(browser==BrowserKind::Firefox){Put(profile/L"user.js",firefoxBase);Put(profile/L"prefs.js","// OWNED prefs.js exact sentinel\r\n");}
   else {Put(profile/L"ctSpaces","ctSpaces-profile=2\r\n");Put(profile/L"Default"/L"Preferences",Json());Put(profile/L"Default"/L"Cookies","PRIVATE cookies binary sentinel");Put(profile/L"Default"/L"Sessions"/L"Session_1","PRIVATE retained session");}
  }
 }
 g_sessions.clear();g_activeProfiles.clear();g_iSelectedSessionTab=0;g_selectedBrowser=BrowserKind::Edge;
 g_bUiEnabled=true;g_isLaunchInFlight=false;g_bCleanupBusy=false;g_bCloseAllClientsPending=false;g_bClientNotesBusy=false;g_bDefaultProfileUiBusy=false;
 g_pinnedClients={L"Alpha",L"Beta"};g_archivedClients.clear();g_clientsWithoutTabRestore.clear();
 // false Restore-tabs means the existing startup value5 is already correct;
 // this isolates layout publication from unrelated startup normalization.
 for(BrowserKind browser:{BrowserKind::Edge,BrowserKind::Chrome,BrowserKind::Brave,BrowserKind::Firefox})g_clientsWithoutTabRestore.push_back(MakeClientBrowserPreferenceKey(L"Alpha",browser));
 g_sEdgePath=g_sDataDir/L"msedge.exe";g_sChromePath=g_sDataDir/L"chrome.exe";g_sBravePath=g_sDataDir/L"brave.exe";g_sFirefoxPath=g_sDataDir/L"firefox.exe";
 for(const auto&exe:{g_sEdgePath,g_sChromePath,g_sBravePath,g_sFirefoxPath})Put(exe,"PRIVATE SENTINEL - NOT EXECUTABLE");
 g_globalTabLayout=client_tab_layout::Layout::Browser;indeterminateProcesses=false;processOracle={};processProbeMutation={};mutateAtSnapshot=0;popupCommand=0;popupInspect={};popupMutation={};SetClientInputText(L"Alpha");
}
static void Command(UINT command){WndProc(g_hGui,WM_COMMAND,MAKEWPARAM(command,0),0);}
static void Choose(UINT command,const std::function<void()>&mutation={}){popupCommand=command;popupMutation=mutation;const UINT returned=ShowConfigMenuFromButton(g_hGui);if(returned)Command(returned);popupMutation={};popupInspect={};}
static client_tab_layout::Override ReadChoice(const wchar_t*client=L"Alpha"){client_tab_layout::Override value{};Require(client_tab_layout::Read(Client(client),value),"Expected readable owned client preference");return value;}
static bool NotesUnchanged(const Tree&before){for(const auto&[path,bytes]:before)if(path.filename()==client_notes::kFileName||path.filename()==client_notes::kRichFileName||path.filename()==client_notes::kNotebookFileName)if(Read(Client()/path)!=bytes)return false;return true;}
static void GlobalAndMenuCases(){
 Seed();const auto beta=Snapshot(Client(L"Beta"));
 const UINT globalCommands[]={IDM_TAB_LAYOUT_GLOBAL_VERTICAL,IDM_TAB_LAYOUT_GLOBAL_HORIZONTAL,IDM_TAB_LAYOUT_GLOBAL_BROWSER};
 const client_tab_layout::Layout globals[]={client_tab_layout::Layout::Vertical,client_tab_layout::Layout::Horizontal,client_tab_layout::Layout::Browser};
 const wchar_t*tokens[]={L"vertical",L"horizontal",L"browser"};
 for(unsigned i=0;i<3;++i){
  bool checkedBefore=false;popupInspect=[&](HMENU){checkedBefore=Radio(g_hGlobalTabLayoutMenu,IDM_TAB_LAYOUT_GLOBAL_BROWSER,IDM_TAB_LAYOUT_GLOBAL_HORIZONTAL,i?globalCommands[i-1]:IDM_TAB_LAYOUT_GLOBAL_BROWSER);};
  Choose(globalCommands[i]);const auto after=g_globalTabLayout.load();g_globalTabLayout=static_cast<client_tab_layout::Layout>(99);LoadGlobalTabLayoutPreference();wchar_t saved[32]{};GetPrivateProfileStringW(L"user",L"tab_layout",L"",saved,32,g_sConfigPath.c_str());
  Check(checkedBefore&&after==globals[i]&&g_globalTabLayout.load()==globals[i]&&std::wstring(saved)==tokens[i],"Actual global popup/WM_COMMAND saves all3 choices, reloads persisted value and checks exactly one native menu item");
 }
 const auto config=Read(g_sConfigPath);HANDLE lock=CreateFileW(g_sConfigPath.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Require(lock!=INVALID_HANDLE_VALUE,"Private config lock unavailable");const auto previous=g_globalTabLayout.load();const unsigned noticeBefore=notices;Choose(IDM_TAB_LAYOUT_GLOBAL_VERTICAL);
 Check(g_globalTabLayout.load()==previous&&Read(g_sConfigPath)==config&&notices>noticeBefore,"Locked actual global command reports failure and preserves both saved and in-memory choice");CloseHandle(lock);
 Choose(IDM_TAB_LAYOUT_GLOBAL_VERTICAL,[]{SetClientInputText(L"Beta");});Check(g_globalTabLayout.load()==client_tab_layout::Layout::Vertical&&Snapshot(Client(L"Beta"))==beta,"Global layout selection survives client change without modifying that client's files");
 SetClientInputText(L"Alpha");
 const UINT clientCommands[]={IDM_TAB_LAYOUT_CLIENT_VERTICAL,IDM_TAB_LAYOUT_CLIENT_HORIZONTAL,IDM_TAB_LAYOUT_CLIENT_FOLLOW};
 const client_tab_layout::Override choices[]={client_tab_layout::Override::Vertical,client_tab_layout::Override::Horizontal,client_tab_layout::Override::FollowGlobal};
 for(unsigned i=0;i<3;++i){bool checkedBefore=false;popupInspect=[&](HMENU){checkedBefore=Radio(g_hClientTabLayoutMenu,IDM_TAB_LAYOUT_CLIENT_FOLLOW,IDM_TAB_LAYOUT_CLIENT_HORIZONTAL,i?clientCommands[i-1]:IDM_TAB_LAYOUT_CLIENT_FOLLOW);};Choose(clientCommands[i]);Check(checkedBefore&&ReadChoice()==choices[i]&&Snapshot(Client(L"Beta"))==beta,"Actual selected-client popup persists all3 client-wide choices with independent checked state and exact Beta isolation");}
 const auto alpha=Snapshot(Client());Choose(IDM_TAB_LAYOUT_CLIENT_VERTICAL,[]{SetClientInputText(L"Beta");});Check(Snapshot(Client())==alpha&&Snapshot(Client(L"Beta"))==beta,"Client change while popup pumps cannot retarget either client's tab-layout write");
 SetClientInputText(L"Alpha");Choose(IDM_TAB_LAYOUT_CLIENT_VERTICAL,[]{g_selectedBrowser=BrowserKind::Brave;});Check(ReadChoice()==client_tab_layout::Override::Vertical&&Snapshot(Client(L"Beta"))==beta,"Browser-only popup change keeps same-client tab-layout command usable and client-wide");
 const auto sidecar=Client()/client_tab_layout::kFileName;const auto sidecarBytes=Read(sidecar);lock=CreateFileW(sidecar.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);Require(lock!=INVALID_HANDLE_VALUE,"Private sidecar lock unavailable");Choose(IDM_TAB_LAYOUT_CLIENT_HORIZONTAL);Check(Read(sidecar)==sidecarBytes&&Snapshot(Client(L"Beta"))==beta,"Locked client sidecar refuses actual menu publication without changing either saved preference");CloseHandle(lock);
 popupCommand=IDM_TAB_LAYOUT_CLIENT_HORIZONTAL;popupInspect=[&](HMENU menu){Check(Radio(Submenu(menu,IDM_TAB_LAYOUT_CLIENT),IDM_TAB_LAYOUT_CLIENT_FOLLOW,IDM_TAB_LAYOUT_CLIENT_HORIZONTAL,IDM_TAB_LAYOUT_CLIENT_VERTICAL),"Actual pinned popup contains enabled3choice submenu with its own client's checked preference");};popupMutation=[]{SetClientInputText(L"Beta");g_selectedBrowser=BrowserKind::Firefox;};ShowPinnedClientOptionsMenu(g_hGui,L"Alpha",{});popupInspect={};popupMutation={};Check(ReadChoice()==client_tab_layout::Override::Horizontal&&Snapshot(Client(L"Beta"))==beta,"Pinned layout command remains bound to original client despite unrelated client/browser selection changes");
 // A captured namespace denies rename while the menu pumps; if a platform
 // permits it, Verify must refuse the stale command rather than edit replacement.
 for(bool pinned:{false,true}){
  Seed();const auto foreign=Client(L"Beta");const auto foreignBefore=Snapshot(foreign);const auto old=Client();const auto moved=g_sDataDir/L"original-alpha";bool attempted=false,renamed=false;std::error_code renameError;
  popupCommand=IDM_TAB_LAYOUT_CLIENT_VERTICAL;popupMutation=[&]{attempted=true;fs::rename(old,moved,renameError);if(!renameError){fs::rename(foreign,old,renameError);renamed=!renameError;}};
  if(pinned)ShowPinnedClientOptionsMenu(g_hGui,L"Alpha",{});else Choose(IDM_TAB_LAYOUT_CLIENT_VERTICAL,popupMutation);
  popupMutation={};const auto foreignNow=renamed?old:foreign;
  Check(attempted&&Snapshot(foreignNow)==foreignBefore&&(!renamed||!fs::exists(old/client_tab_layout::kFileName)),"Config and pinned popup namespace ownership blocks root rename or refuses stale retarget, preserving foreign client bytes");
 }
 Seed();const auto before=Read(g_sConfigPath);const auto allAlpha=Snapshot(Client());
 g_bUiEnabled=false;const bool disabled=!SetGlobalTabLayoutPreference(client_tab_layout::Layout::Vertical)&&!SetClientTabLayoutPreference(L"Alpha",client_tab_layout::Override::Vertical);g_bUiEnabled=true;
 g_isLaunchInFlight=true;const bool launching=!SetGlobalTabLayoutPreference(client_tab_layout::Layout::Vertical)&&!SetClientTabLayoutPreference(L"Alpha",client_tab_layout::Override::Vertical);g_isLaunchInFlight=false;
 Check(disabled&&launching&&Read(g_sConfigPath)==before&&Snapshot(Client())==allAlpha,"Disabled launcher and in-flight launch cannot publish stale global or client preference commands");
}
static void LaunchCases(){
 for(BrowserKind browser:{BrowserKind::Edge,BrowserKind::Chrome,BrowserKind::Brave,BrowserKind::Firefox})for(bool vertical:{true,false}){
  Seed();const auto original=Snapshot(Client());const auto beta=Snapshot(Client(L"Beta"));const auto profile=Profile(browser);const auto native=NativeFile(browser);
  g_globalTabLayout=vertical?client_tab_layout::Layout::Vertical:client_tab_layout::Layout::Horizontal;
  const auto expected=Expected(browser,vertical);const unsigned requests=processRequests,oraclePasses=processOraclePasses;
  processOracle=[&,expected]{return Read(native)==expected&&Snapshot(Client(L"Beta"))==beta&&NotesUnchanged(original);};
  std::wstring error;BrowserKind actual=browser;HANDLE process=reinterpret_cast<HANDLE>(1);const auto pid=LaunchProfile(L"Alpha",false,false,actual,{},&process,nullptr,nullptr,&error);
  std::wcout<<L"LAYOUT_LAUNCH browser="<<GetBrowserId(browser)<<L" vertical="<<vertical<<L" requests="<<processRequests-requests<<L"\n";
  Check(!pid&&!process&&processRequests==requests+1&&processOraclePasses==oraclePasses+1&&Read(native)==expected&&!error.empty(),"All4 actual managed launch paths publish the requested native vertical/horizontal value before their one intercepted process failure");
  Check(Snapshot(Client(L"Beta"))==beta&&NotesUnchanged(original)&&g_activeProfiles.empty()&&g_sessions.empty()&&Read(profile/(browser==BrowserKind::Firefox?L"prefs.js":L"Default/Cookies"))==(browser==BrowserKind::Firefox?"// OWNED prefs.js exact sentinel\r\n":"PRIVATE cookies binary sentinel"),"Native layout launch preserves other-client, Notes, native cookies/prefs.js and registers no process/session");
 }
 for(BrowserKind browser:{BrowserKind::Edge,BrowserKind::Chrome,BrowserKind::Brave,BrowserKind::Firefox}){
  Seed();g_globalTabLayout=client_tab_layout::Layout::Horizontal;Require(SetClientTabLayoutPreference(L"Alpha",client_tab_layout::Override::Vertical),"Override fixture save failed");
  const auto native=NativeFile(browser);const auto expected=Expected(browser,true);const unsigned before=processOraclePasses;processOracle=[&,expected]{return Read(native)==expected;};BrowserKind actual=browser;LaunchProfile(L"Alpha",false,false,actual);const bool overrideWorked=processOraclePasses==before+1;
  Require(SetClientTabLayoutPreference(L"Alpha",client_tab_layout::Override::Horizontal),"Horizontal override fixture save failed");g_globalTabLayout=client_tab_layout::Layout::Vertical;const auto horizontal=Expected(browser,false);processOracle=[&,horizontal]{return Read(native)==horizontal;};LaunchProfile(L"Alpha",false,false,actual);
  Require(SetClientTabLayoutPreference(L"Alpha",client_tab_layout::Override::FollowGlobal),"Follow fixture save failed");processOracle=[&,expected]{return Read(native)==expected;};LaunchProfile(L"Alpha",false,false,actual);
  g_globalTabLayout=client_tab_layout::Layout::Horizontal;processOracle=[&,horizontal]{return Read(native)==horizontal;};LaunchProfile(L"Alpha",false,false,actual);
  Check(overrideWorked&&processOraclePasses==before+4&&Read(native)==horizontal,"All4 browser launches honor both explicit client overrides, then Follow global responds to vertical and horizontal switches");
 }
 for(BrowserKind browser:{BrowserKind::Edge,BrowserKind::Chrome,BrowserKind::Brave}){
  Seed();const auto native=NativeFile(browser);Put(native,Json(true,true,true));const auto bytes=Read(native);const unsigned before=processOraclePasses;processOracle=[&]{return Read(native)==bytes;};BrowserKind actual=browser;LaunchProfile(L"Alpha",false,false,actual);
  Check(processOraclePasses==before+1&&Read(native)==bytes,"Keep browser's choice causes no Chromium layout write, preserving exact existing cross-vendor preferences");
 }
 Seed();BrowserKind firefox=BrowserKind::Firefox;g_globalTabLayout=client_tab_layout::Layout::Vertical;processOracle=[&]{return Read(NativeFile(firefox))==FirefoxExpected(true);};LaunchProfile(L"Alpha",false,false,firefox);g_globalTabLayout=client_tab_layout::Layout::Browser;const unsigned released=processOraclePasses;processOracle=[&]{return Read(NativeFile(firefox))==firefoxBase;};LaunchProfile(L"Alpha",false,false,firefox);
 Check(processOraclePasses==released+1&&Read(NativeFile(firefox))==firefoxBase,"Keep browser's choice releases only the Firefox managed block and restores exact preexisting user.js");
 for(bool unknown:{false,true})for(BrowserKind browser:{BrowserKind::Edge,BrowserKind::Firefox}){
  Seed();g_globalTabLayout=client_tab_layout::Layout::Vertical;const auto alpha=Snapshot(Client()),beta=Snapshot(Client(L"Beta"));const unsigned before=processRequests;
  if(unknown)indeterminateProcesses=true;else{ActiveProfileInfo active{};active.pid=0x7fffa101;active.profilePath=Profile(browser);active.type=ProfileType::Standard;g_activeProfiles.emplace(ClientBrowserKey{L"Alpha",browser},active);}
  BrowserKind actual=browser;std::wstring error;const auto pid=LaunchProfile(L"Alpha",false,false,actual,{},nullptr,nullptr,nullptr,&error);
  Check(!pid&&processRequests==before&&!error.empty()&&Snapshot(Client())==alpha&&Snapshot(Client(L"Beta"))==beta,"Tracked active or indeterminate exact-profile inspection prevents all layout/startup/activity writes and process creation");
 }
 for(unsigned namespaceKind:{0u,1u,2u}){
  Seed();g_globalTabLayout=client_tab_layout::Layout::Vertical;const auto beta=Snapshot(Client(L"Beta"));BrowserKind browser=BrowserKind::Chrome;const auto original=namespaceKind==0?Client():namespaceKind==1?Profile(browser):Profile(browser)/L"Default";const auto moved=g_sDataDir/L"would-retarget";
  bool attempted=false,blocked=false,restored=false;DWORD code=0;mutateAtSnapshot=processSnapshots+2;
  processProbeMutation=[&]{attempted=true;SetLastError(ERROR_SUCCESS);const bool renamed=MoveFileExW(original.c_str(),moved.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;code=GetLastError();blocked=!renamed&&code==ERROR_SHARING_VIOLATION;if(renamed){restored=MoveFileExW(moved.c_str(),original.c_str(),MOVEFILE_WRITE_THROUGH)!=FALSE;Require(restored,"Private rename probe could not restore its original directory");}};
  const auto expected=Expected(browser,true);const unsigned before=processOraclePasses;processOracle=[&,expected]{return Read(NativeFile(browser))==expected&&Snapshot(Client(L"Beta"))==beta;};LaunchProfile(L"Alpha",false,false,browser);
  std::cout<<"LAYOUT_NAMESPACE_SWAP kind="<<namespaceKind<<" attempted="<<attempted<<" blocked="<<blocked<<" error="<<code<<" restored="<<restored<<'\n';
  Check(attempted&&blocked&&processOraclePasses==before+1&&Snapshot(Client(L"Beta"))==beta&&!fs::exists(moved),"Retained launch namespace blocks client-root, exact-profile and Default-parent rename during second idle probe, preserving settings-to-profile binding");
 }
 Seed();g_globalTabLayout=client_tab_layout::Layout::Vertical;const auto alpha=Snapshot(Client()),beta=Snapshot(Client(L"Beta"));const unsigned before=processOraclePasses;const fs::path temp=g_sDataDir/L"Temp";
 processOracle=[&]{const auto bytes=Read(temp/L"user.js");return bytes.find("user_pref(\"sidebar.verticalTabs\", true);")!=std::string::npos&&Snapshot(Client())==alpha&&Snapshot(Client(L"Beta"))==beta;};std::shared_ptr<transient_profile::Identity> directory;bool fresh=false;LaunchProfile(L"Temp",true,false,firefox,{},nullptr,nullptr,nullptr,nullptr,&fresh,&directory);
 Check(fresh&&directory&&processOraclePasses==before+1&&Snapshot(Client())==alpha&&Snapshot(Client(L"Beta"))==beta,"Actual Firefox Temp follows global vertical layout without reading or modifying client overrides");
}
static void SidecarSafetyCases(){
 Seed();Require(SetClientTabLayoutPreference(L"Alpha",client_tab_layout::Override::Vertical),"Sidecar fixture publication failed");std::vector<fs::path> authority;std::wstring error;const auto sidecar=Client()/client_tab_layout::kFileName;
 {client_deletion_safety::BoundTree bound;Require(bound.Capture(Client(),error),"Actual DELETE-capable whole-client capture failed");Check(CollectClientDeleteAuthority(Client(),authority,error,&bound)&&std::ranges::find(authority,fs::path(client_tab_layout::kFileName))!=authority.end()&&ValidateClientDeleteAuthorityEntry(Client(),client_tab_layout::kFileName,error,&bound),"Actual DELETE-capable client guard and native deletion authority accept bounded tab-layout sidecar together");}
 Put(sidecar,"future/corrupt record retained for explicit deletion");const auto malformed=Read(sidecar);popupCommand=0;bool disabled=false;popupInspect=[&](HMENU){disabled=(GetMenuState(g_hClientTabLayoutMenu,IDM_TAB_LAYOUT_CLIENT_VERTICAL,MF_BYCOMMAND)&MF_GRAYED)!=0;};Choose(0);
 BrowserKind browser=BrowserKind::Chrome;const unsigned before=processRequests;const auto alpha=Snapshot(Client());g_globalTabLayout=client_tab_layout::Layout::Vertical;std::wstring launchError;LaunchProfile(L"Alpha",false,false,browser,{},nullptr,nullptr,nullptr,&launchError);
 Check(disabled&&processRequests==before&&!launchError.empty()&&Read(sidecar)==malformed&&CollectClientDeleteAuthority(Client(),authority,error)&&ValidateClientDeleteAuthorityEntry(Client(),client_tab_layout::kFileName,error),"Malformed sidecar disables menu and refuses launch but remains safe explicit-delete authority without guessing a client choice");
 Check(Snapshot(Client())==alpha,"Unreadable choice cannot overwrite any existing client, browser or Notes bytes");
 Put(sidecar,std::string(client_tab_layout::kMaximumBytes+1,'x'));Check(!CollectClientDeleteAuthority(Client(),authority,error)&&!ValidateClientDeleteAuthorityEntry(Client(),client_tab_layout::kFileName,error),"Oversized tab-layout metadata cannot grant unbounded client deletion authority");
 Seed();Require(SetClientTabLayoutPreference(L"Alpha",client_tab_layout::Override::Vertical),"Readable sidecar control unavailable");const auto lockedPath=Client()/client_tab_layout::kFileName;HANDLE lock=CreateFileW(lockedPath.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);Require(lock!=INVALID_HANDLE_VALUE,"Exclusive sidecar lock unavailable");client_tab_layout::Override unchanged=client_tab_layout::Override::Horizontal;Check(!client_tab_layout::Read(Client(),unchanged,&error)&&unchanged==client_tab_layout::Override::Horizontal&&!CollectClientDeleteAuthority(Client(),authority,error),"Unreadable sidecar preserves caller value and cannot be used as deletion authority");CloseHandle(lock);
 Seed();Require(SetClientTabLayoutPreference(L"Alpha",client_tab_layout::Override::Vertical),"Actual-delete sidecar fixture unavailable");const auto beta=Snapshot(Client(L"Beta"));std::wstring outcome;const bool deleted=DeleteEntireClient(L"Alpha",true,std::nullopt,outcome);
 Check(deleted&&!fs::exists(Client())&&Snapshot(Client(L"Beta"))==beta,"Actual DeleteEntireClient removes only explicitly targeted private client with sidecar, all browser slots and Notes; other client remains byte-exact");
}
static void CALLBACK Observe(HWINEVENTHOOK,DWORD event,HWND window,LONG object,LONG,DWORD,DWORD){DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=GetCurrentProcessId())return;if(event==EVENT_OBJECT_SHOW&&object==OBJID_WINDOW)++actualShows;if(event==EVENT_OBJECT_FOCUS)++actualFocus;if(event==EVENT_SYSTEM_FOREGROUND)++actualForeground;}
int wmain(){std::vector<HWINEVENTHOOK>hooks;try{
 std::cout<<std::unitbuf;
 for(DWORD event:{EVENT_SYSTEM_FOREGROUND,EVENT_OBJECT_SHOW,EVENT_OBJECT_FOCUS}){auto hook=SetWinEventHook(event,event,nullptr,Observe,GetCurrentProcessId(),0,WINEVENT_OUTOFCONTEXT);Require(hook!=nullptr,"Native background-event audit unavailable");hooks.push_back(hook);}
 g_hInst=GetModuleHandleW(nullptr);g_bQaInstance=true;g_sExeDir=GetCurrentExecutablePath().parent_path();fixture=retained_fixture::CreateRoot(fs::current_path()/L"build"/L"qa-tlui",L"u");std::wcout<<L"PRIVATE_FIXTURE "<<fixture.wstring()<<L'\n';
 INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES|ICC_TAB_CLASSES};Require(InitCommonControlsEx(&controls)!=FALSE,"Common controls unavailable");Gdiplus::GdiplusStartupInput graphics;Require(Gdiplus::GdiplusStartup(&g_gdiplusToken,&graphics,nullptr)==Gdiplus::Ok,"GDI+ unavailable");
 g_hGui=CreateWindowExW(WS_EX_NOACTIVATE,L"STATIC",L"Hidden tab layout QA",WS_OVERLAPPED|WS_CLIPCHILDREN,0,0,600,234,nullptr,nullptr,g_hInst,nullptr);Owned(g_hGui);
 const auto child=[&](const wchar_t*kind,DWORD style){HWND result=CreateWindowExW(WS_EX_NOACTIVATE,kind,L"",WS_CHILD|style,0,0,100,24,g_hGui,nullptr,g_hInst,nullptr);Owned(result);return result;};
 g_hClientEdit=child(L"EDIT",ES_AUTOHSCROLL);g_hComboClient=child(L"COMBOBOX",CBS_DROPDOWN);g_hClientEditSurface=child(L"STATIC",0);g_hBtnClientIcon=child(L"BUTTON",0);g_hBtnClientDrop=child(L"BUTTON",0);g_hBtnGo=child(L"BUTTON",0);g_hBtnPin=child(L"BUTTON",0);g_hBtnRestoreTabs=child(L"BUTTON",0);g_hBtnConfig=child(L"BUTTON",0);g_hBtnTmpProf=child(L"BUTTON",0);g_hBtnCloseAll=child(L"BUTTON",0);g_hBtnNotes=child(L"BUTTON",0);
 LoadBakedThemesFromHeader();g_themeColors=g_aCustomThemes.front().colors;UpdateThemeBrushes();
 GlobalAndMenuCases();LaunchCases();SidecarSafetyCases();
 Check(processRequests==processOraclePasses&&processRequests>20&&processSnapshots>0,"Every requested native process was intercepted after its independent prepared-layout oracle passed; no OS browser process was created");
 Check(external==0&&foreignPaint==0,"No shell/tray/process-open/termination or foreign repaint reached its native boundary");
 DestroyWindow(g_hGui);g_hGui=nullptr;MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){Require(!((message.message>=WM_MOUSEFIRST&&message.message<=WM_MOUSELAST)||(message.message>=WM_KEYFIRST&&message.message<=WM_KEYLAST)),"Device input is outside this fixture");DispatchMessageW(&message);}
 unsigned visible=0;EnumWindows([](HWND window,LPARAM data)->BOOL{DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==GetCurrentProcessId()&&IsWindowVisible(window))++*reinterpret_cast<unsigned*>(data);return TRUE;},reinterpret_cast<LPARAM>(&visible));
 Check(visible==0&&actualShows==0&&actualFocus==0&&actualForeground==0,"Actual native audit observes zero visible windows, show, focus and foreground events");
 for(auto hook:hooks)UnhookWinEvent(hook);hooks.clear();Gdiplus::GdiplusShutdown(g_gdiplusToken);
 std::cout<<"Browser tab layout UI: "<<checks<<" checks, "<<scenarios<<" scenarios, "<<failures<<" failures; native windows/show/focus/foreground="<<visible<<'/'<<actualShows<<'/'<<actualFocus<<'/'<<actualForeground<<"; intercepted process requests="<<processRequests<<"; external="<<external<<"; foreign paint="<<foreignPaint<<"; clipboard reads=0.\n";
 return failures?1:0;
 }catch(const std::exception&error){std::cerr<<"Tab-layout guarded fixture/setup failure: "<<error.what()<<'\n';for(auto hook:hooks)UnhookWinEvent(hook);return 2;}}
