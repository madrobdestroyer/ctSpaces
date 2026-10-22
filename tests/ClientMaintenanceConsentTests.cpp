// Background-only maintenance consent and client-identity regressions. Presentation,
// process opens and external launches are intercepted before Windows executes.
#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <tlhelp32.h>
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
static unsigned snapshotCount=0;
static bool unknownProcesses=false;
static void BeforeSnapshotMutation();
static HANDLE WINAPI FixtureSnapshot(DWORD,DWORD){++snapshotCount;BeforeSnapshotMutation();if(unknownProcesses){SetLastError(ERROR_ACCESS_DENIED);return INVALID_HANDLE_VALUE;}return CreateEventW(nullptr,FALSE,FALSE,nullptr);}
static BOOL WINAPI FixtureFirst(HANDLE,LPPROCESSENTRY32W item){wcscpy_s(item->szExeFile,L"private-fixture.exe");item->th32ProcessID=GetCurrentProcessId();return TRUE;}
static BOOL WINAPI FixtureNext(HANDLE,LPPROCESSENTRY32W){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
#define CreateToolhelp32Snapshot FixtureSnapshot
#define Process32FirstW FixtureFirst
#define Process32NextW FixtureNext
#define CTSPACES_INSTALLER_TEST_HOOKS
#define wWinMain ProductionEntryNotRun
#ifdef CTSPACES_MAINTENANCE_BEFORE
#include "../build/app-audit-round9-2026-10-15/maintenance/ctSpaces-before.cpp"
#else
#include "../ctSpaces.cpp"
#endif
#undef wWinMain
#undef OpenProcess
#undef PostMessageW

static unsigned checks=0,failures=0,cases=0,notices=0;
static std::filesystem::path fixture,alpha,bravo,parked;
static unsigned vacuumMutationAt=0;
static bool mutationAttempted=false,mutationSucceeded=false,cancelConfirmation=false,profileMutation=false;
static std::wstring lastNotice,lastTitle;
static void Check(bool value,const char*description){++checks;if(!value)++failures;std::cout<<(value?"PASS ":"FAIL ")<<description<<'\n';}
static void Put(const fs::path&path,const std::string&value){fs::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<value;if(!out)throw std::runtime_error("Private write failed");}
static std::string Read(const fs::path&path){std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
static void SwapRoot(){mutationAttempted=true;std::error_code error;const auto from=profileMutation?alpha/L"Browsers/edge/Profile":alpha;const auto foreign=profileMutation?bravo/L"Browsers/edge/Profile":bravo;fs::rename(from,parked,error);if(!error)fs::rename(foreign,from,error);mutationSucceeded=!error;}
static void BeforeSnapshotMutation(){if(vacuumMutationAt==2&&snapshotCount==2)SwapRoot();}
int CtSpacesInstallerTestMessageBox(HWND,LPCWSTR text,LPCWSTR title,UINT flags){++notices;lastNotice=text?text:L"";lastTitle=title?title:L"";std::wcout<<L"INTERCEPTED "<<lastTitle<<L": "<<lastNotice<<L'\n';if(lastTitle==L"Confirm Vacuum"){if(vacuumMutationAt==1)SwapRoot();return cancelConfirmation?IDNO:IDYES;}return (flags&MB_YESNO)==MB_YESNO?IDNO:IDOK;}
bool CtSpacesInstallerTestTryGetExeVersion(const fs::path&,std::wstring&){return false;}
void CtSpacesInstallerTestAfterClientDeletePreflight(const fs::path&){}
static const auto oldTime=client_activity::ThreeMonthsBefore(client_activity::Now())-10000000;
static void Seed(const fs::path&root,const char*name){
 Put(root/kClientSchemaMarkerName,kClientSchemaMarkerText);Put(root/L"notes.txt",std::string(name)+" NOTES");
 const auto slot=root/L"Browsers"/GetBrowserId(BrowserKind::Edge);Put(slot/kBrowserSchemaMarkerName,GetBrowserMarkerText(BrowserKind::Edge));
 Put(slot/L"Profile/ctSpaces","ctSpaces-profile=2\r\n");Put(slot/L"Profile/Default/Preferences","{}");
 Put(slot/L"Profile/Default/Cache/data.bin",std::string(name)+" CACHE");Put(slot/L"Profile/Default/Cookies",std::string(name)+" COOKIES");
 if(!client_activity::Write(root,{oldTime,false}))throw std::runtime_error("Seed private age failed");
}
static void Setup(const wchar_t*name){++cases;g_sDataDir=fixture/name;fs::create_directories(g_sDataDir);g_sConfigPath=g_sDataDir/L"config.ini";
 g_sShortcutDesktopOverride=g_sDataDir/L"private-desktop";fs::create_directories(g_sShortcutDesktopOverride);
 alpha=g_sDataDir/L"Sites/Alpha";bravo=g_sDataDir/L"Sites/Bravo";parked=g_sDataDir/L"original-alpha";Seed(alpha,"ALPHA");Seed(bravo,"BRAVO");
 g_sessions={{L"Alpha",BrowserKind::Edge,0x7ffff351}};g_iSelectedSessionTab=1;g_selectedBrowser=BrowserKind::Edge;g_bUiEnabled=true;g_activeProfiles.clear();g_pinnedClients.clear();g_archivedClients.clear();
 snapshotCount=0;vacuumMutationAt=0;mutationAttempted=mutationSucceeded=cancelConfirmation=unknownProcesses=profileMutation=false;
}
static fs::path Profile(const fs::path&root){return root/L"Browsers/edge/Profile";}
static void VacuumCases(){
 for(unsigned mode:{0u,1u,2u,3u,4u,5u}){
  Setup((L"vacuum-"+std::to_wstring(mode)).c_str());vacuumMutationAt=mode==1?1:mode==2?2:0;cancelConfirmation=mode==3;unknownProcesses=mode==4;
  HANDLE locked=INVALID_HANDLE_VALUE;if(mode==5){locked=CreateFileW((Profile(alpha)/L"Default/Cache/data.bin").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);if(locked==INVALID_HANDLE_VALUE)throw std::runtime_error("Private lock setup failed");}
  GuiProfVacuum();if(locked!=INVALID_HANDLE_VALUE)CloseHandle(locked);
  const auto foreign=mutationSucceeded?alpha:bravo;const bool foreignCacheLost=!fs::exists(Profile(foreign)/L"Default/Cache/data.bin");
  std::cout<<"VACUUM mode="<<mode<<" attempted="<<mutationAttempted<<" substituted="<<mutationSucceeded<<" foreign_cache_lost="<<foreignCacheLost<<"\n";
  Check(g_bUiEnabled,"Vacuum restores UI availability after success, cancellation or failure");
  Check(!foreignCacheLost&&Read(foreign/L"notes.txt")=="BRAVO NOTES"&&Read(Profile(foreign)/L"Default/Cookies")=="BRAVO COOKIES","Vacuum never removes another client's private cache, notes or cookies");
  if(mode==1||mode==2){Check(mutationAttempted,"Vacuum replacement reaches its actual confirmation or second idle-probe boundary");Check(!mutationSucceeded||fs::exists(Profile(alpha)/L"Default/Cache/data.bin"),"Changed client directory is retained without cleaning its cache");}
  else if(mode==0)Check(!fs::exists(Profile(alpha)/L"Default/Cache")&&Read(Profile(alpha)/L"Default/Cookies")=="ALPHA COOKIES","Ordinary vacuum clears owned cache and preserves logins");
  else if(mode==5)Check(fs::exists(Profile(alpha)/L"Default/Cache/data.bin")&&lastTitle==L"Vacuum Completed with Warnings","Actual native locked cache remains and is reported with warnings");
  else Check(fs::exists(Profile(alpha)/L"Default/Cache/data.bin"),"Cancelled or unverifiable vacuum retains selected cache bytes");
 }
}
static void VacuumProfileReplacementCase(){
 Setup(L"vacuum-profile-replacement");profileMutation=true;vacuumMutationAt=2;GuiProfVacuum();
 const auto foreignProfile=mutationSucceeded?Profile(alpha):Profile(bravo);
 std::cout<<"VACUUM_PROFILE attempted="<<mutationAttempted<<" substituted="<<mutationSucceeded<<" foreign_cache_lost="<<!fs::exists(foreignProfile/L"Default/Cache/data.bin")<<'\n';
 Check(mutationAttempted,"Nested profile substitution reaches the actual final idle-probe boundary");
 Check(Read(foreignProfile/L"Default/Cache/data.bin")=="BRAVO CACHE"&&Read(foreignProfile/L"Default/Cookies")=="BRAVO COOKIES","Vacuum binds original nested browser profile identity as well as the selected client root");
 Check(Read(alpha/L"notes.txt")=="ALPHA NOTES"&&Read(bravo/L"notes.txt")=="BRAVO NOTES"&&g_bUiEnabled,"Nested profile substitution preserves both clients' notes and restores UI availability");
}
static bool DeletePreviewCandidate(const InactiveClientCandidate&selected,bool manual,std::wstring&outcome){
#ifdef CTSPACES_MAINTENANCE_BEFORE
 return DeleteEntireClient(selected.name,true,manual?std::nullopt:std::optional<client_activity::Record>(selected.activity),outcome);
#else
 return DeleteEntireClient(selected.name,true,manual?std::nullopt:std::optional<client_activity::Record>(selected.activity),outcome,selected.directory);
#endif
}
static void CandidateCases(){
 for(bool manual:{false,true}){
  Setup(manual?L"manual-preview-replacement":L"inactive-preview-replacement");CleanupScanState scan;scan.manualSelection=manual;CollectCleanupCandidates(scan);
  Check(scan.failure.empty()&&scan.candidates.size()==2,"Production cleanup scan returns both recognized closed private clients");
  auto selected=std::ranges::find_if(scan.candidates,[](const auto&item){return item.name==L"Alpha";});if(selected==scan.candidates.end())throw std::runtime_error("Selected scan row absent");
  SwapRoot();std::wstring outcome;
  const bool deleted=DeletePreviewCandidate(*selected,manual,outcome);
  std::wcout<<L"PREVIEW manual="<<manual<<L" substituted="<<mutationSucceeded<<L" deleted="<<deleted<<L" outcome="<<outcome<<L'\n';
  Check(mutationAttempted,"Private client replacement occurs after scan and before selected preview deletion");
  Check(!mutationSucceeded||(!deleted&&Read(alpha/L"notes.txt")=="BRAVO NOTES"&&Read(Profile(alpha)/L"Default/Cookies")=="BRAVO COOKIES"),"Cleanup preview consent does not authorize deleting a different client with identical age metadata");
  Check(Read(parked/L"notes.txt")=="ALPHA NOTES","Displaced originally previewed client's note remains unchanged");
 }
 for(bool manual:{false,true}){
  Setup(manual?L"manual-unchanged-positive":L"inactive-unchanged-positive");CleanupScanState scan;scan.manualSelection=manual;CollectCleanupCandidates(scan);
  auto selected=std::ranges::find_if(scan.candidates,[](const auto&item){return item.name==L"Alpha";});if(selected==scan.candidates.end())throw std::runtime_error("Positive preview row absent");std::wstring outcome;
  Check(DeletePreviewCandidate(*selected,manual,outcome)&&!fs::exists(alpha),"Unchanged confirmed client is completely deleted with no retained observer preventing final absence");
  Check(Read(bravo/L"notes.txt")=="BRAVO NOTES"&&Read(Profile(bravo)/L"Default/Cache/data.bin")=="BRAVO CACHE","Ordinary selected deletion preserves the independent client's notes and cache");
  CleanupScanState rescan;rescan.manualSelection=manual;CollectCleanupCandidates(rescan);
  Check(rescan.failure.empty()&&rescan.candidates.size()==1&&rescan.candidates[0].name==L"Bravo","Rescan excludes deleted client and retains exactly the independent client");
 }
 Setup(L"reopened-after-preview");CleanupScanState oldScan;CollectCleanupCandidates(oldScan);auto oldSelected=std::ranges::find_if(oldScan.candidates,[](const auto&item){return item.name==L"Alpha";});if(oldSelected==oldScan.candidates.end())throw std::runtime_error("Old preview row absent");
 Check(client_activity::Write(alpha,{client_activity::Now(),false}),"Private fixture records a genuine reopen after preview");std::wstring reopenOutcome;
 Check(!DeletePreviewCandidate(*oldSelected,false,reopenOutcome)&&Read(alpha/L"notes.txt")=="ALPHA NOTES","Inactive cleanup rejects original client reopened after preview without deleting note bytes");
 CleanupScanState freshScan;CollectCleanupCandidates(freshScan);Check(freshScan.failure.empty()&&freshScan.candidates.size()==1&&freshScan.candidates[0].name==L"Bravo","Fresh inactivity rescan excludes recently reopened client");
 Setup(L"unknown-process-scan");unknownProcesses=true;CleanupScanState unknown;CollectCleanupCandidates(unknown);unknownProcesses=false;
 Check(unknown.failure.empty()&&unknown.candidates.empty()&&unknown.skipped==2,"Unverifiable process inspection yields no deletable candidates and counts both skipped clients");
 Check(Read(alpha/L"notes.txt")=="ALPHA NOTES"&&Read(bravo/L"notes.txt")=="BRAVO NOTES","Unverifiable cleanup scan preserves both independent client notes");
 Setup(L"cancelled-scan");CleanupScanState cancelled;cancelled.cancelRequested=true;CollectCleanupCandidates(cancelled);Check(cancelled.candidates.empty()&&cancelled.failure.empty(),"Pre-cancelled cleanup scan returns no deletion candidates");
 Check(Read(alpha/L"notes.txt")=="ALPHA NOTES"&&Read(bravo/L"notes.txt")=="BRAVO NOTES","Pre-cancelled scan preserves both independent client notes");
}
int wmain(){try{
 fixture=fs::current_path()/L"build/app-audit-round9-2026-10-15/maintenance/fixtures"/(L"private-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));if(!fs::create_directories(fixture))throw std::runtime_error("Fresh fixture unavailable");
 VacuumCases();VacuumProfileReplacementCase();CandidateCases();
 Check(!g_hGui&&!g_hClientEdit&&!g_hComboClient,"Native fixture creates no windows and never calls the app entrypoint");
 Check(presentationRequests==0&&launchRequests==0&&forbiddenPaint==0&&closeTargets.empty(),"No desktop presentation, actual launch, process termination, foreign repaint or input APIs execute");
 std::cout<<checks<<" maintenance consent checks across "<<cases<<" scenarios; "<<failures<<" failures; suppressed paint requests="<<queuedPaintRequests<<'\n';std::wcout<<L"Retained fixture: "<<fixture<<L'\n';return failures?1:0;
}catch(const std::exception&e){std::cerr<<"SETUP FAILURE "<<e.what()<<'\n';return 2;}}
